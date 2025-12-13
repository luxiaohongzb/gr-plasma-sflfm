/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "range_plot_widget.h"
#include <QPen>
#include <QColor>
#include <QBrush>
#include <QFont>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace gr {
namespace plasma {

// 自定义 Picker 类，用于显示鼠标悬停时的坐标
class RangePlotPicker : public QwtPlotPicker
{
public:
    RangePlotPicker(int xAxis, int yAxis, QWidget* canvas)
        : QwtPlotPicker(xAxis, yAxis, QwtPlotPicker::CrossRubberBand,
                       QwtPicker::AlwaysOn, canvas)
    {
        setRubberBandPen(QPen(Qt::gray, 1, Qt::DashLine));
        setTrackerPen(QPen(Qt::black));
        setStateMachine(new QwtPickerTrackerMachine());
        setTrackerMode(QwtPicker::AlwaysOn);
        setRubberBand(QwtPicker::CrossRubberBand);
    }
    
    QwtText trackerTextF(const QPointF& pos) const override
    {
        QColor bg(Qt::white);
        bg.setAlpha(220);
        
        QString text = QString("X: %1\nY: %2")
                      .arg(pos.x(), 0, 'f', 2)
                      .arg(pos.y(), 0, 'f', 2);
        
        QwtText qwtText(text);
        qwtText.setBackgroundBrush(QBrush(bg));
        qwtText.setColor(Qt::black);
        qwtText.setBorderPen(QPen(Qt::gray));
        qwtText.setBorderRadius(4);
        qwtText.setRenderFlags(Qt::AlignLeft | Qt::AlignTop);
        qwtText.setFont(QFont("Arial", 10));
        
        return qwtText;
    }
};

RangePlotWidget::RangePlotWidget(const QString& title,
                                 const QString& x_label,
                                 const QString& y_label,
                                 QWidget* parent)
    : QWidget(parent),
      d_auto_x_range(true),
      d_auto_y_range(true),
      d_x_min_auto(0.0),
      d_x_max_auto(1000.0),
      d_y_min_auto(0.0),
      d_y_max_auto(1.0)
{
    // 创建主布局
    d_main_layout = new QVBoxLayout(this);

    // 创建绘图对象
    d_plot = new QwtPlot(this);
    d_plot->setTitle(title);
    d_plot->setAxisTitle(QwtPlot::xBottom, x_label);
    d_plot->setAxisTitle(QwtPlot::yLeft, y_label);
    d_plot->setCanvasBackground(Qt::white);
    d_plot->setAutoReplot(false);

    // 创建曲线
    d_curve = new QwtPlotCurve("Data");
    d_curve->setPen(QPen(Qt::blue, 2));
    d_curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    d_curve->attach(d_plot);

    // 创建网格
    d_grid = new QwtPlotGrid();
    d_grid->setPen(QPen(Qt::gray, 0, Qt::DotLine));
    d_grid->attach(d_plot);

    // 创建缩放工具（框选缩放）
    d_zoomer = new QwtPlotZoomer(d_plot->canvas());
    d_zoomer->setRubberBandPen(QPen(Qt::red, 2, Qt::DotLine));
    d_zoomer->setTrackerPen(QPen(Qt::black));
    d_zoomer->setMousePattern(QwtEventPattern::MouseSelect1, Qt::LeftButton);
    d_zoomer->setMousePattern(QwtEventPattern::MouseSelect2, Qt::RightButton, Qt::ControlModifier);
    d_zoomer->setMousePattern(QwtEventPattern::MouseSelect3, Qt::RightButton);

    // 创建鼠标悬停坐标显示工具（使用自定义 RangePlotPicker）
    d_picker = new RangePlotPicker(QwtPlot::xBottom, QwtPlot::yLeft, d_plot->canvas());

    // 创建平移工具
    d_panner = new QwtPlotPanner(d_plot->canvas());
    d_panner->setAxisEnabled(QwtPlot::yRight, false);
    d_panner->setMouseButton(Qt::MiddleButton);

    // 创建滚轮缩放工具
    d_magnifier = new QwtPlotMagnifier(d_plot->canvas());
    d_magnifier->setMouseButton(Qt::RightButton, Qt::ControlModifier);
    d_magnifier->setWheelFactor(1.1);  // 设置滚轮缩放因子

    // 创建控制面板
    d_control_group = new QGroupBox("范围控制", this);
    d_control_layout = new QHBoxLayout();

    // X轴控制
    d_x_min_label = new QLabel("X最小:", this);
    d_x_max_label = new QLabel("X最大:", this);
    d_x_min_spin = new QDoubleSpinBox(this);
    d_x_max_spin = new QDoubleSpinBox(this);
    d_x_min_spin->setRange(-1e6, 1e6);
    d_x_max_spin->setRange(-1e6, 1e6);
    d_x_min_spin->setDecimals(2);
    d_x_max_spin->setDecimals(2);
    d_x_set_btn = new QPushButton("设置X", this);
    d_x_auto_btn = new QPushButton("自动X", this);
    d_x_auto_btn->setCheckable(true);
    d_x_auto_btn->setChecked(true);

    connect(d_x_set_btn, &QPushButton::clicked, this, &RangePlotWidget::onSetXRange);
    connect(d_x_auto_btn, &QPushButton::toggled, this, [this](bool checked) {
        d_auto_x_range = checked;
        d_x_min_spin->setEnabled(!checked);
        d_x_max_spin->setEnabled(!checked);
        d_x_set_btn->setEnabled(!checked);
        if (checked) {
            onAutoXRange();
        }
    });

    // Y轴控制
    d_y_min_label = new QLabel("Y最小:", this);
    d_y_max_label = new QLabel("Y最大:", this);
    d_y_min_spin = new QDoubleSpinBox(this);
    d_y_max_spin = new QDoubleSpinBox(this);
    d_y_min_spin->setRange(-1e6, 1e6);
    d_y_max_spin->setRange(-1e6, 1e6);
    d_y_min_spin->setDecimals(2);
    d_y_max_spin->setDecimals(2);
    d_y_set_btn = new QPushButton("设置Y", this);
    d_y_auto_btn = new QPushButton("自动Y", this);
    d_y_auto_btn->setCheckable(true);
    d_y_auto_btn->setChecked(true);

    connect(d_y_set_btn, &QPushButton::clicked, this, &RangePlotWidget::onSetYRange);
    connect(d_y_auto_btn, &QPushButton::toggled, this, [this](bool checked) {
        d_auto_y_range = checked;
        d_y_min_spin->setEnabled(!checked);
        d_y_max_spin->setEnabled(!checked);
        d_y_set_btn->setEnabled(!checked);
        if (checked) {
            onAutoYRange();
        }
    });

    // 视图控制按钮
    d_reset_btn = new QPushButton("重置视图", this);
    // d_zoom_in_btn = new QPushButton("放大", this);
    // d_zoom_out_btn = new QPushButton("缩小", this);

    connect(d_reset_btn, &QPushButton::clicked, this, &RangePlotWidget::onResetView);
    // connect(d_zoom_in_btn, &QPushButton::clicked, this, [this]() {
    //     // 使用 zoomer 进行缩放
    //     QRectF rect = d_zoomer->zoomRect();
    //     QPointF center = rect.center();
    //     double width = rect.width() / 1.2;
    //     double height = rect.height() / 1.2;
    //     QRectF newRect(center.x() - width/2, center.y() - height/2, width, height);
    //     d_zoomer->zoom(newRect);
    //     d_plot->replot();
    // });
    // connect(d_zoom_out_btn, &QPushButton::clicked, this, [this]() {
    //     // 使用 zoomer 进行缩放
    //     QRectF rect = d_zoomer->zoomRect();
    //     QPointF center = rect.center();
    //     double width = rect.width() * 1.2;
    //     double height = rect.height() * 1.2;
    //     QRectF newRect(center.x() - width/2, center.y() - height/2, width, height);
    //     d_zoomer->zoom(newRect);
    //     d_plot->replot();
    // });

    // 布局控制面板
    d_control_layout->addWidget(d_x_min_label);
    d_control_layout->addWidget(d_x_min_spin);
    d_control_layout->addWidget(d_x_max_label);
    d_control_layout->addWidget(d_x_max_spin);
    d_control_layout->addWidget(d_x_set_btn);
    d_control_layout->addWidget(d_x_auto_btn);
    d_control_layout->addSpacing(20);
    
    d_control_layout->addWidget(d_y_min_label);
    d_control_layout->addWidget(d_y_min_spin);
    d_control_layout->addWidget(d_y_max_label);
    d_control_layout->addWidget(d_y_max_spin);
    d_control_layout->addWidget(d_y_set_btn);
    d_control_layout->addWidget(d_y_auto_btn);
    d_control_layout->addSpacing(20);
    
    d_control_layout->addWidget(d_reset_btn);
    // d_control_layout->addWidget(d_zoom_in_btn);
    // d_control_layout->addWidget(d_zoom_out_btn);
    d_control_layout->addStretch();

    d_control_group->setLayout(d_control_layout);

    // 添加到主布局
    d_main_layout->addWidget(d_plot);
    d_main_layout->addWidget(d_control_group);

    setLayout(d_main_layout);

    // 初始化控件状态
    d_x_min_spin->setEnabled(false);
    d_x_max_spin->setEnabled(false);
    d_x_set_btn->setEnabled(false);
    d_y_min_spin->setEnabled(false);
    d_y_max_spin->setEnabled(false);
    d_y_set_btn->setEnabled(false);
}

RangePlotWidget::~RangePlotWidget()
{
    // Qt 会自动清理子对象
}

void RangePlotWidget::updateData(const std::vector<double>& x_data, const std::vector<double>& y_data)
{
    d_x_data.clear();
    d_y_data.clear();
    
    for (size_t i = 0; i < x_data.size() && i < y_data.size(); i++) {
        d_x_data.append(x_data[i]);
        d_y_data.append(y_data[i]);
    }
    
    updatePlot();
}

void RangePlotWidget::updateData(const QVector<double>& x_data, const QVector<double>& y_data)
{
    d_x_data = x_data;
    d_y_data = y_data;
    updatePlot();
}

void RangePlotWidget::setCurveColor(const QColor& color)
{
    d_curve->setPen(QPen(color, 2));
    d_plot->replot();
}

void RangePlotWidget::setCurveName(const QString& name)
{
    d_curve->setTitle(name);
}

void RangePlotWidget::setTitle(const QString& title)
{
    d_plot->setTitle(title);
}

void RangePlotWidget::setXAxisRange(double x_min, double x_max)
{
    d_x_min_auto = x_min;
    d_x_max_auto = x_max;
    if (d_auto_x_range) {
        d_plot->setAxisScale(QwtPlot::xBottom, x_min, x_max);
        d_plot->replot();
    }
    updateAxisControls();
}

void RangePlotWidget::setYAxisRange(double y_min, double y_max)
{
    d_y_min_auto = y_min;
    d_y_max_auto = y_max;
    if (d_auto_y_range) {
        d_plot->setAxisScale(QwtPlot::yLeft, y_min, y_max);
        d_plot->replot();
    }
    updateAxisControls();
}

void RangePlotWidget::autoScaleY()
{
    if (d_y_data.isEmpty()) {
        return;
    }
    
    double min_val = *std::min_element(d_y_data.begin(), d_y_data.end());
    double max_val = *std::max_element(d_y_data.begin(), d_y_data.end());
    double margin = (max_val - min_val) * 0.1;
    
    setYAxisRange(std::max(0.0, min_val - margin), max_val + margin);
}

void RangePlotWidget::resetView()
{
    onResetView();
}

void RangePlotWidget::onSetXRange()
{
    double x_min = d_x_min_spin->value();
    double x_max = d_x_max_spin->value();
    if (x_max > x_min) {
        d_plot->setAxisScale(QwtPlot::xBottom, x_min, x_max);
        d_plot->replot();
        d_zoomer->setZoomBase();
    }
}

void RangePlotWidget::onSetYRange()
{
    double y_min = d_y_min_spin->value();
    double y_max = d_y_max_spin->value();
    if (y_max > y_min) {
        d_plot->setAxisScale(QwtPlot::yLeft, y_min, y_max);
        d_plot->replot();
    }
}

void RangePlotWidget::onAutoXRange()
{
    if (d_x_data.isEmpty()) {
        return;
    }
    
    double x_min = d_x_data.first();
    double x_max = d_x_data.last();
    
    if (x_max > x_min) {
        d_x_min_auto = x_min;
        d_x_max_auto = x_max;
        d_plot->setAxisScale(QwtPlot::xBottom, x_min, x_max);
        d_plot->replot();
        d_zoomer->setZoomBase();
    }
    updateAxisControls();
}

void RangePlotWidget::onAutoYRange()
{
    autoScaleY();
}

void RangePlotWidget::onResetView()
{
    onAutoXRange();
    onAutoYRange();
    d_zoomer->setZoomBase();
    d_plot->replot();
}

void RangePlotWidget::updateAxisControls()
{
    d_x_min_spin->setValue(d_x_min_auto);
    d_x_max_spin->setValue(d_x_max_auto);
    d_y_min_spin->setValue(d_y_min_auto);
    d_y_max_spin->setValue(d_y_max_auto);
}

void RangePlotWidget::updatePlot()
{
    if (d_x_data.isEmpty() || d_y_data.isEmpty()) {
        return;
    }

    // 更新曲线数据
    d_curve->setSamples(d_x_data, d_y_data);

    // 自动调整范围
    if (d_auto_x_range) {
        onAutoXRange();
    }
    
    if (d_auto_y_range) {
        onAutoYRange();
    }

    // 重绘
    d_plot->replot();
    d_zoomer->setZoomBase();
}

} // namespace plasma
} // namespace gr

