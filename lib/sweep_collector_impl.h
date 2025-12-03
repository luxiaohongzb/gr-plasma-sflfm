/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_SWEEP_COLLECTOR_IMPL_H
#define INCLUDED_PLASMA_SWEEP_COLLECTOR_IMPL_H

#include <gnuradio/plasma/sweep_collector.h>
#include <gnuradio/plasma/pmt_constants.h>
#include <pmt/pmt.h>
#include <uhd/types/time_spec.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <fstream>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>

namespace gr {
namespace plasma {

class sweep_collector_impl : public sweep_collector
{
private:
    std::string output_prefix;
    std::string output_dir;
    double sweep_start;
    double sweep_stop;
    double sweep_step;
    std::string freq_meta_key;
    int save_every; // number of sweep periods to wait before saving
    int sweep_count; // increments each completed sweep

    // collected data per frequency (Hz)
    std::map<double, std::vector<gr_complex>> collected;
    std::mutex lock;

    std::vector<double> expected_freqs;

    // message handling
    void handle_message(const pmt::pmt_t &msg);

    void compute_expected_freqs();
    bool check_sweep_complete();
    void write_and_reset();

public:
    sweep_collector_impl(const std::string &output_prefix,
                         const std::string &output_dir,
                         int save_cycles,
                         double sweep_start,
                         double sweep_stop,
                         double sweep_step,
                         const std::string &freq_key);

    ~sweep_collector_impl() override;

    void set_sweep_params(double start, double stop, double step) override;
    void set_freq_meta_key(const std::string &key) override;
    void set_output_prefix(const std::string &prefix) override;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_SWEEP_COLLECTOR_IMPL_H */
