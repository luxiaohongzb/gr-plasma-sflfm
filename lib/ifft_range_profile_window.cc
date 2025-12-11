/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ifft_range_profile_window.h"
#include <gnuradio/plasma/qt_update_events.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <iostream>
#include <algorithm>

namespace gr {
namespace plasma {

IFFTRangeProfileWindow::IFFTRangeProfileWindow(QWidget* parent,
                                               double bandwidth,
                                               double sample_rate,
                                               int n_fft_synthesis)
    : QWidget(parent),
      d_bandwidth(bandwidth),
      d_sample_rate(sample_rate),
      d_n_fft_synthesis(n_fft_synthesis),
      d_speed_of_light(3e8),
      d_busy(false),
      d_closed(false)
{
    // 设置窗口标题和大小
    setWindowTitle("IFFT Range Profile");
    resize(1200, 600);

    // 创建主布局
    d_main_layout = new QVBoxLayout(this);
    d_plot_layout = new QHBoxLayout();

    // 创建高分辨率距离像绘图
    d_plot = new QwtPlot(this);
    d_plot->setTitle("高分辨率距离像 (HRRP)");
    d_plot->setAxisTitle(QwtPlot::xBottom, "距离 (m)");
    d_plot->setAxisTitle(QwtPlot::yLeft, "幅度");
    d_plot->setCanvasBackground(Qt::white);

    // 创建曲线
    d_curve = new QwtPlotCurve("HRRP");
    d_curve->setPen(QPen(Qt::blue, 2));
    d_curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    d_curve->attach(d_plot);

    // 创建网格
    d_grid = new QwtPlotGrid();
    d_grid->setPen(QPen(Qt::gray, 0, Qt::DotLine));
    d_grid->attach(d_plot);

    // 创建缩放和平移工具
    d_zoomer = new QwtPlotZoomer(d_plot->canvas());
    d_zoomer->setRubberBandPen(QPen(Qt::red, 2, Qt::DotLine));
    d_zoomer->setTrackerPen(QPen(Qt::black));

    d_panner = new QwtPlotPanner(d_plot->canvas());
    d_panner->setAxisEnabled(QwtPlot::yRight, false);

    // 创建状态标签
    d_status_label = new QLabel("等待数据...", this);
    d_status_label->setStyleSheet("QLabel { padding: 5px; background-color: #f0f0f0; }");

    // 添加到布局
    d_plot_layout->addWidget(d_plot);
    d_main_layout->addLayout(d_plot_layout);
    d_main_layout->addWidget(d_status_label);

    setLayout(d_main_layout);

    std::cout << "[IFFTRangeProfileWindow] Initialized" << std::endl;
}

IFFTRangeProfileWindow::~IFFTRangeProfileWindow()
{
    d_closed = true;
}

bool IFFTRangeProfileWindow::is_closed() const { return d_closed; }

bool IFFTRangeProfileWindow::busy() const { return d_busy; }

void IFFTRangeProfileWindow::set_x_axis_range(double x_min, double x_max)
{
    d_plot->setAxisScale(QwtPlot::xBottom, x_min, x_max);
    d_plot->replot();
}

void IFFTRangeProfileWindow::set_y_axis_range(double y_min, double y_max)
{
    d_plot->setAxisScale(QwtPlot::yLeft, y_min, y_max);
    d_plot->replot();
}

void IFFTRangeProfileWindow::customEvent(QEvent* e)
{
    if (e->type() == RangeProfileUpdateEvent::Type()) {
        d_busy = true;

        RangeProfileUpdateEvent* event = static_cast<RangeProfileUpdateEvent*>(e);
        
        const std::vector<double>& range_axis = event->getRangeAxis();
        const std::vector<double>& profile = event->getProfile();
        
        if (range_axis.empty() || profile.empty()) {
            d_busy = false;
            return;
        }

        // 转换为QVector
        d_x_data.clear();
        d_y_data.clear();
        
        for (size_t i = 0; i < range_axis.size(); i++) {
            d_x_data.append(range_axis[i]);
            d_y_data.append(profile[i]);
        }

        // 更新曲线数据
        d_curve->setSamples(d_x_data, d_y_data);

        // 自动调整Y轴范围
        if (!d_y_data.isEmpty()) {
            double max_val = *std::max_element(d_y_data.begin(), d_y_data.end());
            double min_val = *std::min_element(d_y_data.begin(), d_y_data.end());
            double margin = (max_val - min_val) * 0.1;
            d_plot->setAxisScale(QwtPlot::yLeft, 
                               std::max(0.0, min_val - margin), 
                               max_val + margin);
        }

        // 更新X轴范围
        if (!d_x_data.isEmpty()) {
            double x_min = d_x_data.first();
            double x_max = d_x_data.last();
            d_plot->setAxisScale(QwtPlot::xBottom, x_min, x_max);
        }

        // 更新状态标签
        QString status = QString("接收到 %1 个距离点").arg(range_axis.size());
        if (!d_x_data.isEmpty()) {
            status += QString(" | 距离范围: %.2f - %.2f m")
                     .arg(d_x_data.first())
                     .arg(d_x_data.last());
        }
        d_status_label->setText(status);

        // 重绘
        d_plot->replot();
        d_zoomer->setZoomBase();

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated plot with " 
                  << range_axis.size() << " points" << std::endl;
    }
}

void IFFTRangeProfileWindow::update_plot()
{
    d_plot->replot();
}

} // namespace plasma
} // namespace gr




