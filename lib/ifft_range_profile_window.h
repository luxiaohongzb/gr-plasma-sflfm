/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H
#define INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H

#include <gnuradio/plasma/qt_update_events.h>
#include "range_plot_widget.h"
#include <pmt/pmt.h>

#include <QBoxLayout>
#include <QLabel>
#include <QWidget>

#include <atomic>
#include <complex>
#include <vector>

namespace gr {
namespace plasma {

class IFFTRangeProfileWindow : public QWidget
{
    Q_OBJECT

public:
    IFFTRangeProfileWindow(QWidget* parent = nullptr,
                          double bandwidth = 30e6,
                          double sample_rate = 60e6,
                          int n_fft_synthesis = 256);
    ~IFFTRangeProfileWindow();

    bool is_closed() const;
    bool busy() const;

    void set_x_axis_range(double x_min, double x_max);
    void set_y_axis_range(double y_min, double y_max);

public slots:
    void customEvent(QEvent* e) override;

private:
    // 通用绘图组件
    RangePlotWidget* d_plot;  // 高分辨率距离像
    RangePlotWidget* d_pc_plot;  // 脉冲压缩结果

    // QT widgets
    QVBoxLayout* d_main_layout;
    QHBoxLayout* d_plot_layout;
    QLabel* d_status_label;

    // Parameters
    double d_bandwidth;
    double d_sample_rate;
    int d_n_fft_synthesis;
    double d_speed_of_light;

    // Status variables
    std::atomic<bool> d_busy;
    bool d_closed;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H */




