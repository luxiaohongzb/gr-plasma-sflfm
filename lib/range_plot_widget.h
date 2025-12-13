/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_RANGE_PLOT_WIDGET_H
#define INCLUDED_PLASMA_RANGE_PLOT_WIDGET_H

#include <qwt/qwt_plot.h>
#include <qwt/qwt_plot_canvas.h>
#include <qwt/qwt_plot_curve.h>
#include <qwt/qwt_plot_grid.h>
#include <qwt/qwt_plot_zoomer.h>
#include <qwt/qwt_plot_panner.h>
#include <qwt/qwt_plot_magnifier.h>
#include <qwt/qwt_plot_picker.h>
#include <qwt/qwt_event_pattern.h>
#include <qwt/qwt_text.h>
#include <qwt/qwt_picker_machine.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QButtonGroup>
#include <QRadioButton>

#include <vector>
#include <string>

namespace gr {
namespace plasma {

/**
 * @brief 通用距离像绘图组件
 * 
 * 提供以下功能：
 * - 距离轴范围选择（手动输入或自动）
 * - 鼠标滚轮缩放
 * - 框选缩放
 * - 平移
 * - 重置视图
 */
class RangePlotWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param title 绘图标题
     * @param x_label X轴标签（通常是"距离 (m)"）
     * @param y_label Y轴标签（通常是"幅度"）
     * @param parent 父窗口
     */
    RangePlotWidget(const QString& title = "Plot",
                    const QString& x_label = "距离 (m)",
                    const QString& y_label = "幅度",
                    QWidget* parent = nullptr);
    
    ~RangePlotWidget();

    /**
     * @brief 更新绘图数据
     * @param x_data X轴数据（距离）
     * @param y_data Y轴数据（幅度）
     */
    void updateData(const std::vector<double>& x_data, const std::vector<double>& y_data);
    
    /**
     * @brief 更新绘图数据（使用QVector）
     */
    void updateData(const QVector<double>& x_data, const QVector<double>& y_data);

    /**
     * @brief 设置曲线颜色
     */
    void setCurveColor(const QColor& color);

    /**
     * @brief 设置曲线名称
     */
    void setCurveName(const QString& name);

    /**
     * @brief 设置标题
     */
    void setTitle(const QString& title);

    /**
     * @brief 获取绘图对象（用于高级操作）
     */
    QwtPlot* plot() { return d_plot; }

    /**
     * @brief 获取曲线对象（用于高级操作）
     */
    QwtPlotCurve* curve() { return d_curve; }

    /**
     * @brief 设置X轴范围
     */
    void setXAxisRange(double x_min, double x_max);

    /**
     * @brief 设置Y轴范围（自动或手动）
     */
    void setYAxisRange(double y_min, double y_max);

    /**
     * @brief 自动调整Y轴范围
     */
    void autoScaleY();

    /**
     * @brief 重置视图到全范围
     */
    void resetView();

public slots:
    /**
     * @brief 手动设置X轴范围
     */
    void onSetXRange();

    /**
     * @brief 手动设置Y轴范围
     */
    void onSetYRange();

    /**
     * @brief 自动X轴范围
     */
    void onAutoXRange();

    /**
     * @brief 自动Y轴范围
     */
    void onAutoYRange();

    /**
     * @brief 重置视图
     */
    void onResetView();

private:
    // Qwt plot objects
    QwtPlot* d_plot;
    QwtPlotCurve* d_curve;
    QwtPlotGrid* d_grid;
    QwtPlotZoomer* d_zoomer;
    QwtPlotPanner* d_panner;
    QwtPlotMagnifier* d_magnifier;
    QwtPlotPicker* d_picker;  // 用于鼠标悬停显示坐标

    // Control widgets
    QGroupBox* d_control_group;
    QHBoxLayout* d_control_layout;
    
    // X axis controls
    QLabel* d_x_min_label;
    QLabel* d_x_max_label;
    QDoubleSpinBox* d_x_min_spin;
    QDoubleSpinBox* d_x_max_spin;
    QPushButton* d_x_set_btn;
    QPushButton* d_x_auto_btn;
    
    // Y axis controls
    QLabel* d_y_min_label;
    QLabel* d_y_max_label;
    QDoubleSpinBox* d_y_min_spin;
    QDoubleSpinBox* d_y_max_spin;
    QPushButton* d_y_set_btn;
    QPushButton* d_y_auto_btn;
    
    // View controls
    QPushButton* d_reset_btn;
    // QPushButton* d_zoom_in_btn;
    // QPushButton* d_zoom_out_btn;

    // Layout
    QVBoxLayout* d_main_layout;

    // Data storage
    QVector<double> d_x_data;
    QVector<double> d_y_data;

    // State
    bool d_auto_x_range;
    bool d_auto_y_range;
    double d_x_min_auto;
    double d_x_max_auto;
    double d_y_min_auto;
    double d_y_max_auto;

    void updateAxisControls();
    void updatePlot();
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_RANGE_PLOT_WIDGET_H */

