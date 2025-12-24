/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "range_plot_widget.h"
#include <QPen>
#include <QColor>
#include <QBrush>
#include <QFont>
#include <QTimer>
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
      d_y_max_auto(1.0),
      d_peak_markers_enabled(false),
      d_peak_count(10),
      d_x_offset(-630)
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

    // 峰值标记控件
    d_peak_enable_checkbox = new QCheckBox("显示峰值", this);
    d_peak_count_label = new QLabel("峰值个数:", this);
    d_peak_count_combo = new QComboBox(this);
    d_peak_count_combo->addItem("1", 1);
    d_peak_count_combo->addItem("2", 2);
    d_peak_count_combo->addItem("3", 3);
    d_peak_count_combo->addItem("5", 5);
    d_peak_count_combo->addItem("10", 10);
    d_peak_count_combo->setCurrentIndex(4);  // 默认10个
    d_peak_count_combo->setEnabled(false);  // 初始禁用

    connect(d_peak_enable_checkbox, &QCheckBox::toggled, this, &RangePlotWidget::onPeakMarkersToggled);
    connect(d_peak_count_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &RangePlotWidget::onPeakCountChanged);

    // X轴校准控件
    d_x_offset_label = new QLabel("X轴校准偏移(m):", this);
    d_x_offset_spin = new QDoubleSpinBox(this);
    d_x_offset_spin->setRange(-10000.0, 10000.0);
    d_x_offset_spin->setDecimals(2);
    d_x_offset_spin->setSingleStep(0.1);
    d_x_offset_spin->setValue(-630);
    d_x_offset_spin->setToolTip("设置X轴零点偏移量");
    
    d_x_offset_apply_btn = new QPushButton("应用校准", this);
    d_x_offset_apply_btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 5px; font-weight: bold; }");
    d_x_offset_apply_btn->setToolTip("应用X轴偏移校准");
    
    d_x_offset_reset_btn = new QPushButton("重置校准", this);
    d_x_offset_reset_btn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; padding: 5px; font-weight: bold; }");
    d_x_offset_reset_btn->setToolTip("将X轴偏移重置为0");

    connect(d_x_offset_apply_btn, &QPushButton::clicked, this, &RangePlotWidget::onApplyXOffset);
    connect(d_x_offset_reset_btn, &QPushButton::clicked, this, &RangePlotWidget::onResetXOffset);
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
    d_control_layout->addSpacing(20);
    
    d_control_layout->addWidget(d_peak_enable_checkbox);
    d_control_layout->addWidget(d_peak_count_label);
    d_control_layout->addWidget(d_peak_count_combo);
    d_control_layout->addSpacing(20);
    
    d_control_layout->addWidget(d_x_offset_label);
    d_control_layout->addWidget(d_x_offset_spin);
    d_control_layout->addWidget(d_x_offset_apply_btn);
    d_control_layout->addWidget(d_x_offset_reset_btn);
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
    d_x_data_raw.clear();
    d_x_data.clear();
    d_y_data.clear();
    
    for (size_t i = 0; i < x_data.size() && i < y_data.size(); i++) {
        d_x_data_raw.append(x_data[i]);
        d_x_data.append(x_data[i] + d_x_offset);  // 应用偏移
        d_y_data.append(y_data[i]);
    }
    
    updatePlot();
}

void RangePlotWidget::updateData(const QVector<double>& x_data, const QVector<double>& y_data)
{
    d_x_data_raw = x_data;
    d_x_data.clear();
    d_y_data = y_data;
    
    // 应用X轴偏移
    for (int i = 0; i < x_data.size(); i++) {
        d_x_data.append(x_data[i] + d_x_offset);
    }
    
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

    // 更新峰值标记
    if (d_peak_markers_enabled) {
        findAndMarkPeaks();
    }

    // 重绘
    d_plot->replot();
    d_zoomer->setZoomBase();
}

void RangePlotWidget::enablePeakMarkers(bool enable)
{
    d_peak_markers_enabled = enable;
    d_peak_enable_checkbox->setChecked(enable);
    d_peak_count_combo->setEnabled(enable);
    
    if (enable) {
        findAndMarkPeaks();
    } else {
        // 清除所有峰值标记
        for (auto marker : d_peak_markers) {
            marker->detach();
            delete marker;
        }
        d_peak_markers.clear();
    }
    
    d_plot->replot();
}

void RangePlotWidget::setPeakCount(int count)
{
    d_peak_count = count;
    
    // 更新下拉框显示
    for (int i = 0; i < d_peak_count_combo->count(); i++) {
        if (d_peak_count_combo->itemData(i).toInt() == count) {
            d_peak_count_combo->setCurrentIndex(i);
            break;
        }
    }
    
    if (d_peak_markers_enabled) {
        findAndMarkPeaks();
        d_plot->replot();
    }
}

void RangePlotWidget::onPeakMarkersToggled(bool checked)
{
    enablePeakMarkers(checked);
}

void RangePlotWidget::onPeakCountChanged(int index)
{
    int count = d_peak_count_combo->itemData(index).toInt();
    d_peak_count = count;
    
    if (d_peak_markers_enabled) {
        findAndMarkPeaks();
        d_plot->replot();
    }
}

void RangePlotWidget::onApplyXOffset()
{
    double new_offset = d_x_offset_spin->value();
    
    if (std::abs(new_offset - d_x_offset) < 1e-6) {
        // 偏移量没有变化
        return;
    }
    
    d_x_offset = new_offset;
    
    // 重新应用偏移到X轴数据
    d_x_data.clear();
    for (int i = 0; i < d_x_data_raw.size(); i++) {
        d_x_data.append(d_x_data_raw[i] + d_x_offset);
    }
    
    // 更新绘图
    updatePlot();
    
    std::cout << "[RangePlotWidget] Applied X-axis offset: " << d_x_offset << " m" << std::endl;
    
    // 显示提示信息
    d_x_offset_apply_btn->setText("已应用");
    QTimer::singleShot(1000, [this]() {
        d_x_offset_apply_btn->setText("应用校准");
    });
}

void RangePlotWidget::onResetXOffset()
{
    d_x_offset = 0.0;
    d_x_offset_spin->setValue(0.0);
    
    // 恢复原始X轴数据
    d_x_data.clear();
    for (int i = 0; i < d_x_data_raw.size(); i++) {
        d_x_data.append(d_x_data_raw[i]);
    }
    
    // 更新绘图
    updatePlot();
    
    std::cout << "[RangePlotWidget] Reset X-axis offset to 0" << std::endl;
    
    // 显示提示信息
    d_x_offset_reset_btn->setText("已重置");
    QTimer::singleShot(1000, [this]() {
        d_x_offset_reset_btn->setText("重置校准");
    });
}

void RangePlotWidget::findAndMarkPeaks()
{
    // 清除旧的峰值标记
    for (auto marker : d_peak_markers) {
        marker->detach();
        delete marker;
    }
    d_peak_markers.clear();
    
    if (d_x_data.isEmpty() || d_y_data.isEmpty()) {
        return;
    }
    
    // 创建索引和值的对应关系
    std::vector<std::pair<int, double>> peaks;
    for (int i = 0; i < d_y_data.size(); i++) {
        peaks.push_back({i, d_y_data[i]});
    }
    
    // 按值降序排序
    std::sort(peaks.begin(), peaks.end(), 
              [](const std::pair<int, double>& a, const std::pair<int, double>& b) {
                  return a.second > b.second;
              });
    
    // 取前N个峰值
    int num_peaks = std::min(d_peak_count, static_cast<int>(peaks.size()));
    
    // 为每个峰值创建标记
    for (int i = 0; i < num_peaks; i++) {
        int idx = peaks[i].first;
        double x_val = d_x_data[idx];
        double y_val = d_y_data[idx];
        
        // 创建峰值标记
        QwtPlotMarker* marker = new QwtPlotMarker();
        
        // 设置标记位置
        marker->setValue(x_val, y_val);
        
        // 设置标记样式（红色圆点）
        QwtSymbol* symbol = new QwtSymbol(QwtSymbol::Ellipse);
        symbol->setSize(12, 12);
        symbol->setPen(QPen(Qt::red, 2));
        symbol->setBrush(QBrush(Qt::red));
        marker->setSymbol(symbol);
        
        // 设置标签文字
        QString label = QString("#%1\nX: %2\nY: %3")
                       .arg(i + 1)
                       .arg(x_val, 0, 'f', 2)
                       .arg(y_val, 0, 'f', 2);
        
        QwtText text(label);
        text.setFont(QFont("Arial", 9, QFont::Bold));
        text.setColor(Qt::red);
        
        QColor bg(Qt::white);
        bg.setAlpha(200);
        text.setBackgroundBrush(QBrush(bg));
        text.setBorderPen(QPen(Qt::red, 1));
        text.setBorderRadius(3);
        
        marker->setLabel(text);
        marker->setLabelAlignment(Qt::AlignTop | Qt::AlignRight);
        
        // 添加到绘图
        marker->attach(d_plot);
        d_peak_markers.push_back(marker);
        
        std::cout << "[RangePlotWidget] Peak #" << (i+1) 
                  << " at X=" << x_val 
                  << ", Y=" << y_val << std::endl;
    }
}

} // namespace plasma
} // namespace gr

