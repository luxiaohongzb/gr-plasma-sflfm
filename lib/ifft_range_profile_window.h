/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H
#define INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H

#include <gnuradio/plasma/qt_update_events.h>
#include <pmt/pmt.h>

#include <qwt/qwt_plot.h>
#include <qwt/qwt_plot_canvas.h>
#include <qwt/qwt_plot_curve.h>
#include <qwt/qwt_plot_grid.h>
#include <qwt/qwt_plot_layout.h>
#include <qwt/qwt_plot_panner.h>
#include <qwt/qwt_plot_zoomer.h>
#include <qwt/qwt_scale_draw.h>
#include <qwt/qwt_symbol.h>

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
    // Qwt plot objects
    QwtPlot* d_plot;
    QwtPlot* d_2d_plot;  // 用于显示二维距离像
    QwtPlotCurve* d_curve;
    QwtPlotCurve* d_coarse_curve;  // 粗距离像曲线
    QwtPlotGrid* d_grid;
    QwtPlotZoomer* d_zoomer;
    QwtPlotPanner* d_panner;

    // QT widgets
    QVBoxLayout* d_main_layout;
    QHBoxLayout* d_plot_layout;
    QLabel* d_status_label;

    // Parameters
    double d_bandwidth;
    double d_sample_rate;
    int d_n_fft_synthesis;
    double d_speed_of_light;

    // Data storage
    QVector<double> d_x_data;
    QVector<double> d_y_data;
    QVector<double> d_coarse_x_data;
    QVector<double> d_coarse_y_data;

    // Status variables
    std::atomic<bool> d_busy;
    bool d_closed;

    void update_plot();
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H */




