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
#include <QTabWidget>
#include <QPushButton>
#include <QGroupBox>
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
      d_closed(false),
      d_cancellation_enabled(false),
      d_has_leakage_background(false),
      d_recording_leakage(false)
{
    // 设置窗口标题和大小
    setWindowTitle("IFFT Range Profile");
    resize(1400, 800);

    // 创建主布局
    d_main_layout = new QVBoxLayout(this);

    // 创建选项卡
    d_tab_widget = new QTabWidget(this);

    // ========== Tab 1: 1D 绘图 ==========
    QWidget* tab1d = new QWidget();
    d_plot_layout = new QHBoxLayout(tab1d);

    // 创建高分辨率距离像绘图（使用通用组件）
    d_plot = new RangePlotWidget("高分辨率距离像 (HRRP)", "距离 (m)", "幅度", tab1d);
    d_plot->setCurveColor(Qt::blue);
    d_plot->setCurveName("HRRP");
    d_plot->autoScaleY();

    // 创建脉冲压缩结果绘图（使用通用组件）
    d_pc_plot = new RangePlotWidget("脉冲压缩结果", "距离 (m)", "幅度", tab1d);
    d_pc_plot->setCurveColor(Qt::green);
    d_pc_plot->setCurveName("Pulse Compression");

    d_plot_layout->addWidget(d_plot);
    d_plot_layout->addWidget(d_pc_plot);
    tab1d->setLayout(d_plot_layout);

    // ========== Tab 2: 2D 热力图 ==========
    d_hrrp_2d_plot = new Spectrogram2DWidget(
        "2D高分辨距离像 (IFFT合成后)",
        "精细距离单元",
        "粗距离 (m)",
        this);

    // ========== Tab 3: 拼接后的完整一维距离像 ==========
    d_full_range_plot = new RangePlotWidget(
        "拼接后的完整高分辨距离像",
        "距离 (m)",
        "幅度 (dB)",
        this);
    d_full_range_plot->setCurveColor(Qt::darkMagenta);
    d_full_range_plot->setCurveName("Full HRRP");
    d_full_range_plot->setXAxisRange(0, 200);
    d_full_range_plot->setYAxisRange(-30, 0);
    // 添加选项卡
    d_tab_widget->addTab(tab1d, "1D 距离像");
    d_tab_widget->addTab(d_hrrp_2d_plot, "2D HRRP 热力图");
    d_tab_widget->addTab(d_full_range_plot, "拼接完整距离像");

    // 创建状态标签
    d_status_label = new QLabel("等待数据...", this);
    d_status_label->setStyleSheet("QLabel { padding: 5px; background-color: #f0f0f0; }");

    // ========== 直耦消除控制面板 ==========
    d_leakage_control_group = new QGroupBox("直耦消除控制", this);
    d_leakage_control_layout = new QHBoxLayout(d_leakage_control_group);
    
    d_record_leakage_btn = new QPushButton("录制直耦背景", d_leakage_control_group);
    d_record_leakage_btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px; font-weight: bold; }");
    connect(d_record_leakage_btn, &QPushButton::clicked, this, &IFFTRangeProfileWindow::onRecordLeakage);
    
    d_toggle_cancellation_btn = new QPushButton("启用直耦对消", d_leakage_control_group);
    d_toggle_cancellation_btn->setEnabled(false);  // 初始禁用，需要先录制背景
    d_toggle_cancellation_btn->setStyleSheet("QPushButton:disabled { background-color: #cccccc; }");
    connect(d_toggle_cancellation_btn, &QPushButton::clicked, this, &IFFTRangeProfileWindow::onToggleCancellation);
    
    d_leakage_status_label = new QLabel("状态: 未录制背景", d_leakage_control_group);
    d_leakage_status_label->setStyleSheet("QLabel { padding: 5px; font-weight: bold; }");
    
    d_leakage_control_layout->addWidget(d_record_leakage_btn);
    d_leakage_control_layout->addWidget(d_toggle_cancellation_btn);
    d_leakage_control_layout->addStretch();
    d_leakage_control_layout->addWidget(d_leakage_status_label);

    // 添加到主布局
    d_main_layout->addWidget(d_tab_widget, 1);
    d_main_layout->addWidget(d_leakage_control_group);
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

        // 如果正在录制直耦背景
        if (d_recording_leakage) {
            d_leakage_background = profile;
            d_has_leakage_background = true;
            d_recording_leakage = false;
            
            // 更新UI状态
            d_leakage_status_label->setText(QString("状态: 已录制背景 (%1 点)").arg(profile.size()));
            d_leakage_status_label->setStyleSheet("QLabel { padding: 5px; font-weight: bold; color: #4CAF50; }");
            
            // 恢复录制按钮
            d_record_leakage_btn->setText("重新录制背景");
            d_record_leakage_btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px; font-weight: bold; }");
            d_record_leakage_btn->setEnabled(true);
            
            // 启用对消按钮
            d_toggle_cancellation_btn->setEnabled(true);
            d_toggle_cancellation_btn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; padding: 8px; font-weight: bold; }");
            
            std::cout << "[IFFTRangeProfileWindow] Leakage background recorded successfully: " 
                      << profile.size() << " points" << std::endl;
        }

        // 应用直耦对消（如果启用）
        std::vector<double> display_profile = profile;
        if (d_cancellation_enabled && d_has_leakage_background && 
            profile.size() == d_leakage_background.size()) {
            
            for (size_t i = 0; i < profile.size(); i++) {
                // 线性域相减
                display_profile[i] = profile[i] - d_leakage_background[i];
                // 确保非负
                if (display_profile[i] < 0) display_profile[i] = 0;
            }
            
            std::cout << "[IFFTRangeProfileWindow] Applied leakage cancellation to HRRP" << std::endl;
        }

        // 使用通用组件更新数据
        d_plot->updateData(range_axis, display_profile);

        // 更新标题显示对消状态
        QString title = "高分辨率距离像 (HRRP)";
        if (d_cancellation_enabled) {
            title += " [直耦对消已启用]";
        }
        d_plot->setTitle(title);

        // 更新状态标签
        QString status = QString("接收到 %1 个距离点").arg(range_axis.size());
        if (!range_axis.empty()) {
            status += QString(" | 距离范围: %.2f - %.2f m")
                     .arg(range_axis.front())
                     .arg(range_axis.back());
        }
        if (d_cancellation_enabled) {
            status += " | 直耦对消: 开";
        }
        d_status_label->setText(status);

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated HRRP plot with " 
                  << range_axis.size() << " points"
                  << (d_cancellation_enabled ? " (cancellation ON)" : "") << std::endl;
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

        // 限制显示范围到 0-800 米
        std::vector<double> limited_range_axis;
        std::vector<double> limited_profile;
        for (size_t i = 0; i < range_axis.size(); i++) {
            if (range_axis[i] >= 0 && range_axis[i] <= 800.0) {
                limited_range_axis.push_back(range_axis[i]);
                limited_profile.push_back(profile[i]);
            }
        }

        // 更新标题显示频率
        QString title = QString("脉冲压缩结果 (频率: %.3f GHz)").arg(frequency / 1e9);
        d_pc_plot->setTitle(title);

        // 使用通用组件更新数据
        d_pc_plot->updateData(limited_range_axis, limited_profile);

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated pulse compression plot with " 
                  << limited_range_axis.size() << " points at " << frequency / 1e9 << " GHz" << std::endl;
    } else if (e->type() == HRRP2DUpdateEvent::Type()) {
        d_busy = true;

        HRRP2DUpdateEvent* event = static_cast<HRRP2DUpdateEvent*>(e);
        
        const std::vector<double>& data = event->getData();
        const std::vector<double>& coarse_axis = event->getCoarseAxis();
        const std::vector<double>& fine_axis = event->getFineAxis();
        size_t n_coarse = event->getNumCoarse();
        size_t n_fine = event->getNumFine();
        
        if (data.empty() || n_coarse == 0 || n_fine == 0) {
            d_busy = false;
            return;
        }

        // 更新2D热力图
        d_hrrp_2d_plot->updateData(data, coarse_axis, fine_axis, n_coarse, n_fine);

        // 更新状态标签
        QString status = QString("2D HRRP: %1 x %2 (粗距离 x 精细距离)")
                        .arg(n_coarse)
                        .arg(n_fine);
        if (!coarse_axis.empty()) {
            status += QString(" | 粗距离范围: %.2f - %.2f m")
                     .arg(coarse_axis.front())
                     .arg(coarse_axis.back());
        }
        d_status_label->setText(status);

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated 2D HRRP with " 
                  << n_coarse << " x " << n_fine << " points" << std::endl;
    } else if (e->type() == FullRangeProfileUpdateEvent::Type()) {
        d_busy = true;

        FullRangeProfileUpdateEvent* event = static_cast<FullRangeProfileUpdateEvent*>(e);
        
        const std::vector<double>& range_axis = event->getRangeAxis();
        const std::vector<double>& profile = event->getProfile();
        double fine_res = event->getFineResolution();
        
        if (range_axis.empty() || profile.empty()) {
            d_busy = false;
            return;
        }

        // 更新拼接后的距离像绘图
        d_full_range_plot->updateData(range_axis, profile);
        d_full_range_plot->setXAxisRange(0, 200);
        d_full_range_plot->setYAxisRange(-30, 0);
        
        // 更新标题显示分辨率
        QString title = QString("拼接后的完整高分辨距离像 (分辨率: %.3f m)").arg(fine_res);
        d_full_range_plot->setTitle(title);

        // 更新状态标签
        QString status = QString("拼接距离像: %1 个点 | 距离范围: %.2f - %.2f m | 分辨率: %.3f m")
                        .arg(range_axis.size())
                        .arg(range_axis.front())
                        .arg(range_axis.back())
                        .arg(fine_res);
        d_status_label->setText(status);

        d_busy = false;
        
        std::cout << "[IFFTRangeProfileWindow] Updated full range profile with " 
                  << range_axis.size() << " points, resolution=" << fine_res << " m" << std::endl;
    }
}

void IFFTRangeProfileWindow::onRecordLeakage()
{
    // 如果已经在录制中，不重复触发
    if (d_recording_leakage) {
        return;
    }
    
    d_recording_leakage = true;
    d_record_leakage_btn->setText("录制中...");
    d_record_leakage_btn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; padding: 8px; font-weight: bold; }");
    d_record_leakage_btn->setEnabled(false);  // 禁用按钮防止重复点击
    d_leakage_status_label->setText("状态: 等待下一帧数据...");
    d_leakage_status_label->setStyleSheet("QLabel { padding: 5px; font-weight: bold; color: #FF9800; }");
    
    std::cout << "[IFFTRangeProfileWindow] Recording leakage background (waiting for next frame)..." << std::endl;
}

void IFFTRangeProfileWindow::onToggleCancellation()
{
    if (!d_has_leakage_background) {
        d_leakage_status_label->setText("状态: 请先录制背景！");
        return;
    }
    
    d_cancellation_enabled = !d_cancellation_enabled;
    
    if (d_cancellation_enabled) {
        d_toggle_cancellation_btn->setText("关闭直耦对消");
        d_toggle_cancellation_btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; padding: 8px; font-weight: bold; }");
        d_leakage_status_label->setText("状态: 直耦对消已启用");
        d_leakage_status_label->setStyleSheet("QLabel { padding: 5px; font-weight: bold; color: #4CAF50; }");
    } else {
        d_toggle_cancellation_btn->setText("启用直耦对消");
        d_toggle_cancellation_btn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; padding: 8px; font-weight: bold; }");
        d_leakage_status_label->setText("状态: 直耦对消已关闭");
        d_leakage_status_label->setStyleSheet("QLabel { padding: 5px; font-weight: bold; }");
    }
    
    std::cout << "[IFFTRangeProfileWindow] Leakage cancellation " 
              << (d_cancellation_enabled ? "enabled" : "disabled") << std::endl;
}


} // namespace plasma
} // namespace gr




