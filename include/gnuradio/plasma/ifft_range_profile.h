/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_IFFT_RANGE_PROFILE_H
#define INCLUDED_PLASMA_IFFT_RANGE_PROFILE_H

#include <gnuradio/block.h>
#include <gnuradio/plasma/api.h>
#include <gnuradio/plasma/device.h>
#ifdef ENABLE_PYTHON
#pragma push_macro("slots")
#undef slots
#include "Python.h"
#pragma pop_macro("slots")
#endif

// Forward declarations to avoid including Qt headers in the public API
class QApplication;
class QWidget;

namespace gr {
namespace plasma {

/*!
 * \brief IFFT Range Profile synthesis and real-time visualization
 * \ingroup plasma
 *
 * This block receives PDU messages from sweep_collector containing
 * echo data at different frequencies, performs IFFT synthesis to
 * generate high-resolution range profiles, and displays them in 
 * real-time using Qt.
 */
class PLASMA_API ifft_range_profile : virtual public gr::block
{
public:
    typedef std::shared_ptr<ifft_range_profile> sptr;

    /*!
     * \brief Return a shared_ptr to a new instance of plasma::ifft_range_profile.
     *
     * \param bandwidth Signal bandwidth in Hz
     * \param pulse_width Pulse width in seconds
     * \param sample_rate Sample rate in Hz
     * \param prf Pulse repetition frequency in Hz
     * \param sweep_start Sweep start frequency in Hz
     * \param sweep_stop Sweep stop frequency in Hz
     * \param sweep_step Frequency step in Hz
     * \param freq_key Metadata key for frequency
     * \param n_fft_synthesis IFFT size for synthesis
     * \param parent Qt parent widget
     */
    static sptr make(double bandwidth,
                     double pulse_width,
                     double sample_rate,
                     double prf,
                     double sweep_start,
                     double sweep_stop,
                     double sweep_step,
                     const std::string& freq_key,
                     int n_fft_synthesis = 256,
                     QWidget* parent = nullptr);

    virtual void exec_() = 0;
    virtual QWidget* qwidget() = 0;
#ifdef ENABLE_PYTHON
    virtual PyObject* pyqwidget() = 0;
#else
    virtual void* pyqwidget() = 0;
#endif

    virtual void set_dynamic_range(const double) = 0;
    virtual void set_msg_queue_depth(size_t depth) = 0;
    virtual void set_backend(Device::Backend) = 0;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_IFFT_RANGE_PROFILE_H */




