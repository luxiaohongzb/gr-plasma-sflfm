/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_SWEEP_COLLECTOR_H
#define INCLUDED_PLASMA_SWEEP_COLLECTOR_H

#include <gnuradio/block.h>
#include <gnuradio/plasma/api.h>

namespace gr {
namespace plasma {

class PLASMA_API sweep_collector : virtual public gr::block
{
public:
    typedef std::shared_ptr<sweep_collector> sptr;

    static sptr make(const std::string &output_prefix = "sweep",
                     const std::string &output_dir = ".",
                     int save_cycles = 1,
                     double sweep_start = 0.0,
                     double sweep_stop = 0.0,
                     double sweep_step = 0.0,
                     const std::string &freq_key = "rx_freq");

    /*! Configure sweep parameters (Hz) */
    virtual void set_sweep_params(double start, double stop, double step) = 0;

    /*! Set metadata key used to find the frequency in incoming PDUs */
    virtual void set_freq_meta_key(const std::string &key) = 0;

    /*! Set output file prefix (path/prefix); files will be timestamped */
    virtual void set_output_prefix(const std::string &prefix) = 0;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_SWEEP_COLLECTOR_H */
