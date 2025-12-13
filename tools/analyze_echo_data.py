#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Echo Data Analysis Script: Pulse Compression Coherent Integration and Phase Analysis for 128 Pulses

Features:
1. Read PDU file data (echo_data.dat and echo_data_meta)
2. Generate LFM matched filter
3. Perform pulse compression for each echo
4. Coherent integration of 128 pulses (Doppler FFT)
5. Phase analysis
6. Visualization of results
"""

import numpy as np
import matplotlib.pyplot as plt
import json
import scipy.signal
import scipy.constants as sc
from pathlib import Path
import sys

# Radar parameters (based on acquisition configuration)
c = sc.c  # Speed of light
GHz = 1e9
MHz = 1e6
KHz = 1e3
us = 1e-6

# Acquisition parameters
BANDWIDTH = 30 * MHz      # Bandwidth 30 MHz
PULSE_WIDTH = 10 * us     # Pulse width 10 microseconds
SAMP_RATE = 60 * MHz      # Sample rate 60 MHz
PRF = 1 * KHz             # Pulse repetition frequency 1 kHz
FC = 3 * GHz              # Carrier frequency 3 GHz
N_PULSE_CPI = 32        # Number of pulses in CPI 128

# Calculated parameters
PRI = 1.0 / PRF           # Pulse repetition interval
N_SAMP_PRI = int(SAMP_RATE / PRF)  # Samples per PRI
N_SAMP_PULSE = int(PULSE_WIDTH * SAMP_RATE)  # Samples per pulse
TS = 1.0 / SAMP_RATE      # Sample interval


def read_pdu_data(data_file, meta_file=None):
    """
    Read PDU file data
    
    Parameters:
    -----------
    data_file : str
        Data file path (binary complex64 format)
    meta_file : str, optional
        Metadata file path (JSON format)
    
    Returns:
    --------
    data : np.ndarray
        Complex data array
    metadata : dict
        Metadata dictionary
    """
    print(f"Reading data file: {data_file}")
    
    # Read binary data (complex64 format, 8 bytes per complex number)
    data = np.fromfile(data_file, dtype=np.complex64)
    print(f"Data points read: {len(data)}")
    
    # Read metadata
    metadata = {}
    if meta_file and Path(meta_file).exists():
        print(f"Reading metadata file: {meta_file}")
        with open(meta_file, 'r') as f:
            metadata = json.load(f)
        print(f"Metadata keys: {list(metadata.keys())}")
    else:
        print("Metadata file not found, using default parameters")
    
    return data, metadata


def generate_lfm_reference(bandwidth, pulse_width, samp_rate, start_freq_offset=None):
    """
    Generate LFM reference signal (matched filter)
    
    Parameters:
    -----------
    bandwidth : float
        Bandwidth (Hz)
    pulse_width : float
        Pulse width (seconds)
    samp_rate : float
        Sample rate (Hz)
    start_freq_offset : float, optional
        Start frequency offset (Hz), default -B/2
    
    Returns:
    --------
    match_filt : np.ndarray
        Matched filter coefficients
    """
    if start_freq_offset is None:
        start_freq_offset = -bandwidth / 2
    
    n_samp = int(pulse_width * samp_rate)
    t = np.arange(n_samp) / samp_rate
    
    # LFM signal: s(t) = exp(j*2*pi*(f0*t + (B/(2*T))*t^2))
    lfm_ref = np.exp(1j * 2 * np.pi * (start_freq_offset * t + 
                                        (bandwidth / (2 * pulse_width)) * t**2))
    
    # Matched filter: conjugate and reverse
    match_filt = np.conj(lfm_ref[::-1])
    
    return match_filt


def pulse_compression(rx_data, match_filt, n_fft=None):
    """
    Pulse compression (matched filtering)
    
    Parameters:
    -----------
    rx_data : np.ndarray
        Received data (single echo)
    match_filt : np.ndarray
        Matched filter coefficients
    n_fft : int, optional
        FFT size, default uses linear convolution length
    
    Returns:
    --------
    pc_result : np.ndarray
        Pulse compression result
    """
    if n_fft is None:
        # Linear convolution length
        n_fft = len(rx_data) + len(match_filt) - 1
    
    # Frequency domain matched filtering
    rx_fft = np.fft.fft(rx_data, n_fft)
    mf_fft = np.fft.fft(match_filt, n_fft)
    pc_fft = rx_fft * mf_fft
    pc_result = np.fft.ifft(pc_fft, n_fft)
    
    return pc_result


def organize_pulses(data, n_pulse_cpi, n_samp_pri):
    """
    Organize data into pulse matrix
    
    Parameters:
    -----------
    data : np.ndarray
        Raw data
    n_pulse_cpi : int
        Number of pulses in CPI
    n_samp_pri : int
        Samples per PRI
    
    Returns:
    --------
    pulse_matrix : np.ndarray
        Pulse matrix [n_samp_pri x n_pulse_cpi]
    """
    expected_len = n_pulse_cpi * n_samp_pri
    
    if len(data) < expected_len:
        print(f"Warning: Data length insufficient ({len(data)} < {expected_len}), padding zeros")
        data = np.append(data, np.zeros(expected_len - len(data), dtype=np.complex64))
    elif len(data) > expected_len:
        print(f"Warning: Data length exceeds ({len(data)} > {expected_len}), truncating")
        data = data[:expected_len]
    
    # Reshape to [fast time x slow time] matrix
    pulse_matrix = data.reshape(n_samp_pri, n_pulse_cpi, order='F')
    
    return pulse_matrix


def doppler_fft(pulse_compressed, n_fft=None):
    """
    Doppler FFT (coherent integration)
    
    Parameters:
    -----------
    pulse_compressed : np.ndarray
        Pulse compressed data matrix [n_range x n_pulse]
    n_fft : int, optional
        FFT size, default equals n_pulse
    
    Returns:
    --------
    rdm : np.ndarray
        Range-Doppler map [n_range x n_doppler]
    """
    n_range, n_pulse = pulse_compressed.shape
    
    if n_fft is None:
        n_fft = n_pulse
    
    # FFT for each range bin (along slow time dimension)
    rdm = np.fft.fft(pulse_compressed, n_fft, axis=1)
    
    # FFT shift to center zero Doppler
    rdm = np.fft.fftshift(rdm, axes=1)
    
    return rdm


def phase_analysis(rdm, range_bin=None):
    """
    Phase analysis
    
    Parameters:
    -----------
    rdm : np.ndarray
        Range-Doppler map
    range_bin : int, optional
        Specify range bin, default selects peak location
    
    Returns:
    --------
    phase_data : dict
        Phase analysis results
    """
    # Find peak location
    if range_bin is None:
        max_idx = np.unravel_index(np.argmax(np.abs(rdm)), rdm.shape)
        range_bin = max_idx[0]
        doppler_bin = max_idx[1]
        print(f"Peak location: Range bin={range_bin}, Doppler bin={doppler_bin}")
    else:
        # Find peak in specified range bin
        doppler_profile = np.abs(rdm[range_bin, :])
        doppler_bin = np.argmax(doppler_profile)
        print(f"Specified range bin={range_bin}, Peak Doppler bin={doppler_bin}")
    
    # Extract phase sequence for this range bin
    phase_seq = np.angle(rdm[range_bin, :])
    
    # Extract phase at peak Doppler bin
    peak_phase = np.angle(rdm[range_bin, doppler_bin])
    
    # Calculate phase change rate (Doppler frequency)
    phase_diff = np.diff(np.unwrap(phase_seq))
    avg_phase_rate = np.mean(phase_diff) if len(phase_diff) > 0 else 0
    
    return {
        'range_bin': range_bin,
        'doppler_bin': doppler_bin,
        'phase_sequence': phase_seq,
        'peak_phase': peak_phase,
        'phase_rate': avg_phase_rate,
        'doppler_profile': np.abs(rdm[range_bin, :])
    }


def plot_results(pulse_matrix, pulse_compressed, rdm, phase_info, 
                 samp_rate, prf, fc, n_pulse_cpi):
    """
    Plot analysis results
    
    Parameters:
    -----------
    pulse_matrix : np.ndarray
        Raw pulse matrix
    pulse_compressed : np.ndarray
        Pulse compression results
    rdm : np.ndarray
        Range-Doppler map
    phase_info : dict
        Phase analysis results
    samp_rate : float
        Sample rate
    prf : float
        Pulse repetition frequency
    fc : float
        Carrier frequency
    n_pulse_cpi : int
        Number of pulses in CPI
    """
    # Calculate axes
    n_range, n_pulse = pulse_compressed.shape
    range_axis = (c / 2) * np.arange(n_range) / samp_rate
    doppler_axis = np.linspace(-prf/2, prf/2, rdm.shape[1])
    velocity_axis = (sc.c / (2 * fc)) * doppler_axis
    
    # Convert to dB
    rdm_db = 20 * np.log10(np.abs(rdm) + 1e-10)
    rdm_db = np.clip(rdm_db, -80, None)
    
    pc_db = 20 * np.log10(np.abs(pulse_compressed) + 1e-10)
    
    # Create figure
    fig = plt.figure(figsize=(16, 10))
    
    # 1. Raw echo data (first and last pulse)
    ax1 = plt.subplot(2, 3, 1)
    plt.plot(range_axis[:len(pulse_matrix)], 
             np.real(pulse_matrix[:, 2]), 'b-', label='Pulse 1')
    # plt.plot(range_axis[:len(pulse_matrix)], 
    #          np.abs(pulse_matrix[:, -1]), 'r-', label=f'Pulse {n_pulse_cpi}')
    plt.xlabel('Range (m)')
    plt.ylabel('Amplitude')
    plt.title('Raw Echo Data')
    plt.legend()
    plt.grid(True)
    
    # 2. Pulse compression results (first and last pulse)
    ax2 = plt.subplot(2, 3, 2)
    pc_range_axis = (c / 2) * np.arange(pulse_compressed.shape[0]) / samp_rate
    plt.plot(pc_range_axis, pc_db[:, 0], 'b-', label='Pulse 1')
    plt.plot(pc_range_axis, pc_db[:, -1], 'r-', label=f'Pulse {n_pulse_cpi}')
    plt.xlabel('Range (m)')
    plt.ylabel('Amplitude (dB)')
    plt.title('Pulse Compression Results')
    plt.legend()
    plt.grid(True)
    
    # 3. Range-Doppler map
    ax3 = plt.subplot(2, 3, 3)
    im1 = plt.imshow(rdm_db, aspect='auto', origin='lower',
                    extent=[velocity_axis[0], velocity_axis[-1], 
                           range_axis[0], range_axis[-1]],
                    cmap='jet', interpolation='nearest')
    plt.colorbar(im1, label='Amplitude (dB)')
    plt.xlabel('Velocity (m/s)')
    plt.ylabel('Range (m)')
    plt.title('Range-Doppler Map (RDM)')
    
    # Mark peak location
    peak_range = range_axis[phase_info['range_bin']]
    peak_vel = velocity_axis[phase_info['doppler_bin']]
    plt.plot(peak_vel, peak_range, 'r*', markersize=15, label='Peak')
    plt.legend()
    
    # 4. Amplitude and phase at peak range bin
    ax4 = plt.subplot(2, 3, 4)
    ax4_twin = ax4.twinx()
    line1 = ax4.plot(velocity_axis, phase_info['doppler_profile'], 'b-', label='Amplitude')
    line2 = ax4_twin.plot(velocity_axis, phase_info['phase_sequence'], 'r-', label='Phase')
    ax4.set_xlabel('Velocity (m/s)')
    ax4.set_ylabel('Amplitude', color='b')
    ax4_twin.set_ylabel('Phase (rad)', color='r')
    ax4.tick_params(axis='y', labelcolor='b')
    ax4_twin.tick_params(axis='y', labelcolor='r')
    plt.title(f'Doppler Spectrum at Range Bin {phase_info["range_bin"]}')
    ax4.grid(True)
    
    # 5. Phase variation (unwrapped)
    ax5 = plt.subplot(2, 3, 5)
    pulse_idx = np.arange(n_pulse_cpi)
    plt.plot(pulse_idx, np.unwrap(phase_info['phase_sequence']), 'g-o', markersize=3)
    plt.xlabel('Pulse Index')
    plt.ylabel('Phase (rad, unwrapped)')
    plt.title('Phase Variation Sequence')
    plt.grid(True)
    
    # 6. Average range profile (coherent integration of all pulses)
    ax6 = plt.subplot(2, 3, 6)
    avg_range_profile = np.mean(pc_db, axis=1)
    plt.plot(pc_range_axis, avg_range_profile, 'k-', linewidth=2)
    plt.xlabel('Range (m)')
    plt.ylabel('Amplitude (dB)')
    plt.title('Average Range Profile (128 Pulses Coherent Integration)')
    plt.grid(True)
    
    plt.tight_layout()
    return fig


def main():
    """Main function"""
    # File paths
    if len(sys.argv) > 1:
        data_file = sys.argv[1]
    else:
        data_file = '/home/mingliu/echo_data.dat'
    
    if len(sys.argv) > 2:
        meta_file = sys.argv[2]
    else:
        meta_file = '/home/mingliu/echo_data_meta'
    
    print("=" * 60)
    print("Echo Data Analysis: Pulse Compression Coherent Integration and Phase Analysis for 128 Pulses")
    print("=" * 60)
    print(f"Data file: {data_file}")
    print(f"Metadata file: {meta_file}")
    print(f"Radar parameters:")
    print(f"  Bandwidth: {BANDWIDTH/1e6:.1f} MHz")
    print(f"  Pulse width: {PULSE_WIDTH/1e-6:.1f} us")
    print(f"  Sample rate: {SAMP_RATE/1e6:.1f} MHz")
    print(f"  PRF: {PRF/1e3:.1f} kHz")
    print(f"  Carrier frequency: {FC/1e9:.1f} GHz")
    print(f"  Pulses in CPI: {N_PULSE_CPI}")
    print("=" * 60)
    
    # 1. Read data
    data, metadata = read_pdu_data(data_file, meta_file)
    
    # 2. Generate matched filter
    print("\nGenerating LFM matched filter...")
    match_filt = generate_lfm_reference(BANDWIDTH, PULSE_WIDTH, SAMP_RATE)
    print(f"Matched filter length: {len(match_filt)} points")
    
    # 3. Organize into pulse matrix
    print(f"\nOrganizing pulse data (expected: {N_PULSE_CPI} pulses, {N_SAMP_PRI} points each)...")
    pulse_matrix = organize_pulses(data, N_PULSE_CPI, N_SAMP_PRI)
    print(f"Pulse matrix shape: {pulse_matrix.shape} [fast time x slow time]")
    
    # 4. Pulse compression
    print("\nPerforming pulse compression...")
    n_fft_pc = 2**int(np.ceil(np.log2(len(pulse_matrix) + len(match_filt) - 1)))
    pulse_compressed = np.zeros((n_fft_pc, N_PULSE_CPI), dtype=np.complex64)
    
    for i in range(N_PULSE_CPI):
        pc_result = pulse_compression(pulse_matrix[:, i], match_filt, n_fft_pc)
        pulse_compressed[:, i] = pc_result
    
    print(f"Pulse compression complete, result matrix shape: {pulse_compressed.shape}")
    
    # 5. Doppler FFT (coherent integration)
    print("\nPerforming Doppler FFT (coherent integration of 128 pulses)...")
    rdm = doppler_fft(pulse_compressed, n_fft=N_PULSE_CPI)
    print(f"Range-Doppler map shape: {rdm.shape}")
    
    # 6. Phase analysis
    print("\nPerforming phase analysis...")
    phase_info = phase_analysis(rdm)
    print(f"Peak range bin: {phase_info['range_bin']}")
    print(f"Peak Doppler bin: {phase_info['doppler_bin']}")
    print(f"Peak phase: {phase_info['peak_phase']:.4f} rad ({np.degrees(phase_info['peak_phase']):.2f} deg)")
    print(f"Average phase rate: {phase_info['phase_rate']:.6f} rad/pulse")
    
    # 7. Plot results
    print("\nGenerating visualization...")
    fig = plot_results(pulse_matrix, pulse_compressed, rdm, phase_info,
                      SAMP_RATE, PRF, FC, N_PULSE_CPI)
    
    # Save figure
    output_file = Path(data_file).parent / 'echo_analysis_results.png'
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"\nResults saved to: {output_file}")
    
    plt.show()
    
    print("\nAnalysis complete!")


if __name__ == '__main__':
    main()
