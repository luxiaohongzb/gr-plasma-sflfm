/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ifft_range_profile_window.h"
#include <gnuradio/plasma/qt_update_events.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QColor>
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

    // 创建高分辨率距离像绘图（使用通用组件）
    d_plot = new RangePlotWidget("高分辨率距离像 (HRRP)", "距离 (m)", "幅度", this);
    d_plot->setCurveColor(Qt::blue);
    d_plot->setCurveName("HRRP");

    // 创建脉冲压缩结果绘图（使用通用组件）
    d_pc_plot = new RangePlotWidget("脉冲压缩结果", "距离 (m)", "幅度", this);
    d_pc_plot->setCurveColor(Qt::green);
    d_pc_plot->setCurveName("Pulse Compression");

    // 创建状态标签
    d_status_label = new QLabel("等待数据...", this);
    d_status_label->setStyleSheet("QLabel { padding: 5px; background-color: #f0f0f0; }");

    // 添加到布局
    d_plot_layout->addWidget(d_plot);
    d_plot_layout->addWidget(d_pc_plot);
    d_main_layout->addLayout(d_plot_layout);
    d_main_layout->addWidget(d_status_label);

    setLayout(d_main_layout);

    std::cout << "[IFFTRangeProfileWindow] Initialized" << std::endl;
}

IFFTRangeProfileWindow::~IFFTRangeProfileWindow()
{
    d_closed = true;
    // Qt 会自动清理子对象
}

bool IFFTRangeProfileWindow::is_closed() const { return d_closed; }

bool IFFTRangeProfileWindow::busy() const { return d_busy; }

void IFFTRangeProfileWindow::set_x_axis_range(double x_min, double x_max)
{
    d_plot->setXAxisRange(x_min, x_max);
}

void IFFTRangeProfileWindow::set_y_axis_range(double y_min, double y_max)
{
    d_plot->setYAxisRange(y_min, y_max);
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

        // 使用通用组件更新数据
        d_plot->updateData(range_axis, profile);

        // 更新状态标签
        QString status = QString("接收到 %1 个距离点").arg(range_axis.size());
        if (!range_axis.empty()) {
            status += QString(" | 距离范围: %.2f - %.2f m")
                     .arg(range_axis.front())
                     .arg(range_axis.back());
        }
        d_status_label->setText(status);

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated plot with " 
                  << range_axis.size() << " points" << std::endl;
    } else if (e->type() == PulseCompressionUpdateEvent::Type()) {
        d_busy = true;

        PulseCompressionUpdateEvent* event = static_cast<PulseCompressionUpdateEvent*>(e);
        
        const std::vector<double>& range_axis = event->getRangeAxis();
        const std::vector<double>& profile = event->getProfile();
        double frequency = event->getFrequency();
        
        if (range_axis.empty() || profile.empty()) {
            d_busy = false;
            return;
        }

        // 更新标题显示频率
        QString title = QString("脉冲压缩结果 (频率: %.3f GHz)").arg(frequency / 1e9);
        d_pc_plot->setTitle(title);

        // 使用通用组件更新数据
        d_pc_plot->updateData(range_axis, profile);

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated pulse compression plot with " 
                  << range_axis.size() << " points at " << frequency / 1e9 << " GHz" << std::endl;
    }
}


} // namespace plasma
} // namespace gr




