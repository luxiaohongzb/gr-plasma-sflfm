#!/usr/bin/env python3
"""
sweep_analyze.py

Read binary files produced by sweep_collector and provide simple
time-domain, frequency-domain and spectrogram analysis per frequency.

Binary format produced by sweep_collector:
 - uint64_t n_freqs
 For each frequency:
 - double freq (Hz)
 - uint64_t n_samples
 - n_samples pairs of float32: (real, imag) interleaved

Usage:
    python3 tools/sweep_analyze.py <file.bin> [--show] [--outdir out]

Outputs: PNG figures saved per-frequency and a summary JSON with basic metrics.
"""

import argparse
import struct
import os
import json
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

try:
    from scipy.signal import spectrogram
except Exception:
    spectrogram = None


def read_sweep_file(path):
    """Read sweep binary file and return dict: {freq: complex ndarray}

    Args:
        path: Path to file
    Returns:
        Ordered list of (freq, np.array(complex64))
    """
    items = []
    with open(path, "rb") as f:
        # read uint64 n_freqs
        data = f.read(8)
        if len(data) < 8:
            raise ValueError("File too short: missing n_freqs")
        (n_freqs,) = struct.unpack('<Q', data)
        for i in range(n_freqs):
            h = f.read(8)
            if len(h) < 8:
                raise ValueError("Unexpected EOF reading freq")
            (freq,) = struct.unpack('<d', h)
            h = f.read(8)
            if len(h) < 8:
                raise ValueError("Unexpected EOF reading n_samples")
            (n_samples,) = struct.unpack('<Q', h)
            # read 2*n_samples float32 values
            nvals = 2 * int(n_samples)
            data = f.read(4 * nvals)
            if len(data) < 4 * nvals:
                raise ValueError(f"Unexpected EOF reading samples for freq {freq}")
            vals = np.frombuffer(data, dtype='<f4')
            # reshape and form complex array
            vals = vals.reshape(-1, 2)
            cplx = vals[:, 0] + 1j * vals[:, 1]
            items.append((float(freq), cplx.astype(np.complex64)))
    return items


def plot_time_domain(freq, x, outpng=None, show=False):
    t = np.arange(len(x))
    plt.figure(figsize=(10, 3))
    plt.plot(t, np.real(x), label='real')
    plt.plot(t, np.imag(x), label='imag')
    plt.title(f'Time domain - {freq:.3f} Hz - {len(x)} samples')
    plt.xlabel('Sample index')
    plt.ylabel('Amplitude')
    plt.legend()
    plt.tight_layout()
    if outpng:
        plt.savefig(outpng)
        print(f'Saved {outpng}')
    if show:
        plt.show()
    plt.close()


def plot_frequency_domain(freq, x, outpng=None, show=False, window=None, nfft=4096):
    X = np.fft.fftshift(np.fft.fft(x * (window if window is not None else 1.0), n=nfft))
    freqs = np.fft.fftshift(np.fft.fftfreq(nfft))
    mag = 20 * np.log10(np.abs(X) + 1e-12)
    plt.figure(figsize=(10, 3))
    plt.plot(freqs, mag)
    plt.title(f'Frequency domain (normalized bins) - {freq:.3f} Hz')
    plt.xlabel('Normalized frequency (cycles/sample)')
    plt.ylabel('Magnitude (dB)')
    plt.tight_layout()
    if outpng:
        plt.savefig(outpng)
        print(f'Saved {outpng}')
    if show:
        plt.show()
    plt.close()


def plot_spectrogram(freq, x, outpng=None, show=False, fs=1.0):
    plt.figure(figsize=(8, 4))
    if spectrogram is not None:
        f, t, Sxx = spectrogram(x, fs=fs, nperseg=256, noverlap=128)
        plt.pcolormesh(t, f, 20 * np.log10(np.abs(Sxx) + 1e-12), shading='gouraud')
        plt.ylabel('Freq (Hz)')
        plt.xlabel('Time (s)')
    else:
        # fallback to matplotlib's specgram
        Pxx, freqs, bins, im = plt.specgram(x, NFFT=256, Fs=fs, noverlap=128)
        plt.ylabel('Freq (Hz)')
        plt.xlabel('Time (s)')
    plt.title(f'Spectrogram - {freq:.3f} Hz')
    plt.colorbar(label='Magnitude (dB)')
    plt.tight_layout()
    if outpng:
        plt.savefig(outpng)
        print(f'Saved {outpng}')
    if show:
        plt.show()
    plt.close()


def make_lfm_reference(fs, bw, fstart, pulse_width, window='hann'):
    N = int(round(pulse_width * fs))
    t = np.arange(N) / fs
    k = bw / pulse_width
    phi = 2 * np.pi * (fstart * t + 0.5 * k * t * t)
    ref = np.exp(1j * phi).astype(np.complex64)
    if window == 'hann':
        ref = ref * np.hanning(N)
    return ref


def pulse_compress_and_coh_accum(x, ref, n_coh=None):
    N = len(ref)
    h = np.conj(ref[::-1])
    y_all = np.convolve(x, h, mode='same')
    if n_coh is None or n_coh <= 0:
        return y_all, y_all
    K = len(y_all) // N
    if K <= 0:
        return y_all, y_all
    y_use = y_all[: K * N]
    frames = y_use.reshape(K, N)
    n_use = min(n_coh, K)
    coh = np.sum(frames[:n_use], axis=0)
    return y_all, coh


def plot_range_profile(freq, y, fs, outpng=None, show=False, range_axis=False):
    N = len(y)
    x_axis = np.arange(N)
    if range_axis:
        x_axis = x_axis * (3.0e8 / (2.0 * fs))
    mag_db = 20 * np.log10(np.abs(y) + 1e-12)
    plt.figure(figsize=(10, 3))
    plt.plot(x_axis, mag_db)
    plt.title(f'Pulse-compressed profile - {freq:.3f} Hz')
    plt.xlabel('Range (m)' if range_axis else 'Sample index')
    plt.ylabel('Magnitude (dB)')
    plt.tight_layout()
    if outpng:
        plt.savefig(outpng)
        print(f'Saved {outpng}')
    if show:
        plt.show()
    plt.close()


def analyze_and_save(items, outdir, show=False, save=True, lfm=False, fs=None, bw=None, fstart=None, pulse_width=None, n_coh=None, range_axis=False, window='hann'):
    metrics = {}
    Path(outdir).mkdir(parents=True, exist_ok=True)
    for freq, x in items:
        base = os.path.join(outdir, f'freq_{int(freq)}')
        # time domain
        plot_time_domain(freq, x, outpng=(base + '_time.png' if save else None), show=show)
        if lfm and fs is not None and bw is not None and pulse_width is not None and fstart is not None:
            ref = make_lfm_reference(fs, bw, fstart, pulse_width, window=window)
            ys, coh = pulse_compress_and_coh_accum(x, ref, n_coh=n_coh)
            if coh is not None:
                plot_range_profile(freq, coh, fs, outpng=(base + '_range.png' if save else None), show=show, range_axis=range_axis)

        # compute metrics
        power = np.mean(np.abs(x) ** 2)
        peak_idx = int(np.argmax(np.abs(np.fft.fft(x))))
        m = {
            'n_samples': int(len(x)),
            'power': float(power),
            'peak_bin': int(peak_idx),
        }
        if lfm and fs is not None and bw is not None and pulse_width is not None and fstart is not None and 'coh' in locals() and coh is not None:
            m['coh_bins'] = int(len(coh))
            m['coh_used'] = int(n_coh if (n_coh is not None and n_coh > 0) else (len(x) // max(1, int(round(pulse_width * fs)))))
        metrics[freq] = m
    # save metrics
    with open(os.path.join(outdir, 'metrics.json'), 'w') as f:
        json.dump(metrics, f, indent=2)
    print('Saved metrics.json')


def main():
    parser = argparse.ArgumentParser(description='Analyze sweep_collector binary file')
    parser.add_argument('file', help='Path to binary file')
    parser.add_argument('--outdir', '-o', default='sweep_analysis', help='Output directory')
    parser.add_argument('--show', action='store_true', help='Show plots interactively')
    parser.add_argument('--mode', choices=['plot', 'save'], default='save')
    parser.add_argument('--lfm', action='store_true')
    parser.add_argument('--fs', type=float, default=10e6)
    parser.add_argument('--bw', type=float, default=4e6)
    parser.add_argument('--fstart', type=float, default=-2e6)
    parser.add_argument('--pw', type=float, default=10e-6)
    parser.add_argument('--coh', type=int, default=0)
    parser.add_argument('--range-axis', action='store_true')
    parser.add_argument('--window', choices=['none', 'hann'], default='hann')
    args = parser.parse_args()

    items = read_sweep_file(args.file)
    print(f'Read {len(items)} frequency entries from {args.file}')
    show = (args.mode == 'plot')
    save = (args.mode == 'save')
    n_coh = (None if args.coh == 0 else args.coh)
    analyze_and_save(items,
                     args.outdir,
                     show=show,
                     save=save,
                     lfm=args.lfm,
                     fs=args.fs,
                     bw=args.bw,
                     fstart=args.fstart,
                     pulse_width=args.pw,
                     n_coh=n_coh,
                     range_axis=args.range_axis,
                     window=(None if args.window == 'none' else args.window))


if __name__ == '__main__':
    main()

