/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_IFFT_RANGE_PROFILE_IMPL_H
#define INCLUDED_PLASMA_IFFT_RANGE_PROFILE_IMPL_H

#include "ifft_range_profile_window.h"
#include <gnuradio/plasma/ifft_range_profile.h>
#include <gnuradio/fft/fft.h>
#include <QApplication>
#include <QWidget>
#include <map>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>

namespace gr {
namespace plasma {

class ifft_range_profile_impl : public ifft_range_profile
{
private:
    // Block parameters
    double d_bandwidth;
    double d_pulse_width;
    double d_sample_rate;
    double d_prf;
    double d_sweep_start;
    double d_sweep_stop;
    double d_sweep_step;
    std::string d_freq_key;
    int d_n_fft_synthesis;
    double d_dynamic_range_db;

    // GUI parameters
    int d_argc;
    char* d_argv;
    IFFTRangeProfileWindow* d_main_gui;
    QApplication* d_qapp;

    // Processing
    std::mutex d_mutex;
    std::map<double, std::vector<gr_complex>> d_freq_data;
    std::vector<double> d_expected_freqs;
    
    // Message handling
    pmt::pmt_t d_in_port;
    size_t d_msg_queue_depth;
    std::atomic<bool> d_finished;
    std::atomic<bool> d_processing;

    // Constants
    static constexpr double SPEED_OF_LIGHT = 3e8;

    void compute_expected_freqs();
    bool check_sweep_complete();
    void process_sweep();
    void perform_pulse_compression(const std::vector<gr_complex>& input,
                                   std::vector<gr_complex>& output,
                                   int n_pri);
    void perform_ifft_synthesis(const std::vector<std::vector<gr_complex>>& pc_data,
                               std::vector<gr_complex>& hrrp);

public:
    ifft_range_profile_impl(double bandwidth,
                           double pulse_width,
                           double sample_rate,
                           double prf,
                           double sweep_start,
                           double sweep_stop,
                           double sweep_step,
                           const std::string& freq_key,
                           int n_fft_synthesis,
                           QWidget* parent);
    ~ifft_range_profile_impl();

    bool start() override;
    bool stop() override;

    void exec_() override;
    QWidget* qwidget() override;
#ifdef ENABLE_PYTHON
    PyObject* pyqwidget() override;
#else
    void* pyqwidget() override;
#endif

    void handle_rx_msg(pmt::pmt_t msg);
    void set_dynamic_range(const double) override;
    void set_msg_queue_depth(size_t) override;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_IFFT_RANGE_PROFILE_IMPL_H */




