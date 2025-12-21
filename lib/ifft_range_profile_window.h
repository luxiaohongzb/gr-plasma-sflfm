/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H
#define INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H

#include <gnuradio/plasma/qt_update_events.h>
#include "range_plot_widget.h"
#include "spectrogram_2d_widget.h"
#include <pmt/pmt.h>

#include <QBoxLayout>
#include <QLabel>
#include <QWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QGroupBox>

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
    void onRecordLeakage();
    void onToggleCancellation();

public:
    // 直耦消除相关接口
    bool isLeakageCancellationEnabled() const { return d_cancellation_enabled; }
    bool hasLeakageBackground() const { return d_has_leakage_background; }
    const std::vector<double>& getLeakageBackground() const { return d_leakage_background; }

private:
    // 通用绘图组件
    RangePlotWidget* d_plot;  // 高分辨率距离像
    RangePlotWidget* d_pc_plot;  // 脉冲压缩结果
    Spectrogram2DWidget* d_hrrp_2d_plot;  // 2D HRRP热力图
    RangePlotWidget* d_full_range_plot;  // 拼接后的完整一维距离像

    // QT widgets
    QVBoxLayout* d_main_layout;
    QHBoxLayout* d_plot_layout;
    QTabWidget* d_tab_widget;  // 选项卡用于切换1D和2D显示
    QLabel* d_status_label;
    
    // 直耦消除控件
    QGroupBox* d_leakage_control_group;
    QHBoxLayout* d_leakage_control_layout;
    QPushButton* d_record_leakage_btn;
    QPushButton* d_toggle_cancellation_btn;
    QLabel* d_leakage_status_label;

    // Parameters
    double d_bandwidth;
    double d_sample_rate;
    int d_n_fft_synthesis;
    double d_speed_of_light;

    // Status variables
    std::atomic<bool> d_busy;
    bool d_closed;
    
    // 直耦消除相关
    bool d_cancellation_enabled;
    bool d_has_leakage_background;
    bool d_recording_leakage;
    std::vector<double> d_leakage_background;  // 存储直耦背景（脉冲压缩后的距离像）
    std::vector<double> d_latest_pc_profile;   // 最新的脉冲压缩数据（用于录制）
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_IFFT_RANGE_PROFILE_WINDOW_H */




