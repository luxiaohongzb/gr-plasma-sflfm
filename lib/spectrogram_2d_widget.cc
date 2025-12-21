/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "spectrogram_2d_widget.h"
#include <qwt/qwt_scale_draw.h>
#include <qwt/qwt_plot_layout.h>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace gr {
namespace plasma {

// ============================================================================
// HRRP2DRasterData Implementation
// ============================================================================

HRRP2DRasterData::HRRP2DRasterData()
    : d_n_rows(0), d_n_cols(0),
      d_x_min(0), d_x_max(1),
      d_y_min(0), d_y_max(1),
      d_z_min(0), d_z_max(1)
{
}

HRRP2DRasterData::~HRRP2DRasterData() {}

void HRRP2DRasterData::setData(const std::vector<double>& data,
                                size_t n_rows, size_t n_cols,
                                double x_min, double x_max,
                                double y_min, double y_max)
{
    d_data = data;
    d_n_rows = n_rows;
    d_n_cols = n_cols;
    d_x_min = x_min;
    d_x_max = x_max;
    d_y_min = y_min;
    d_y_max = y_max;

    // 计算Z轴范围
    if (!d_data.empty()) {
        auto minmax = std::minmax_element(d_data.begin(), d_data.end());
        d_z_min = *minmax.first;
        d_z_max = *minmax.second;
        
        // 避免除零
        if (d_z_max - d_z_min < 1e-10) {
            d_z_max = d_z_min + 1.0;
        }
    }
    
    // 设置 QwtRasterData 的 interval
    setInterval(Qt::XAxis, QwtInterval(d_x_min, d_x_max));
    setInterval(Qt::YAxis, QwtInterval(d_y_min, d_y_max));
    setInterval(Qt::ZAxis, QwtInterval(d_z_min, d_z_max));
}

double HRRP2DRasterData::value(double x, double y) const
{
    if (d_data.empty() || d_n_rows == 0 || d_n_cols == 0) {
        return 0.0;
    }

    // 将坐标转换为数组索引
    // x对应列（精细距离），y对应行（粗距离）
    double x_frac = (x - d_x_min) / (d_x_max - d_x_min);
    double y_frac = (y - d_y_min) / (d_y_max - d_y_min);

    // 限制在有效范围内
    x_frac = std::max(0.0, std::min(1.0, x_frac));
    y_frac = std::max(0.0, std::min(1.0, y_frac));

    size_t col = static_cast<size_t>(x_frac * (d_n_cols - 1));
    size_t row = static_cast<size_t>(y_frac * (d_n_rows - 1));

    col = std::min(col, d_n_cols - 1);
    row = std::min(row, d_n_rows - 1);

    size_t idx = row * d_n_cols + col;
    if (idx < d_data.size()) {
        return d_data[idx];
    }
    return 0.0;
}

// ============================================================================
// Spectrogram2DWidget Implementation
// ============================================================================

Spectrogram2DWidget::Spectrogram2DWidget(const QString& title,
                                         const QString& x_label,
                                         const QString& y_label,
                                         QWidget* parent)
    : QWidget(parent),
      d_colormap_type(0)
{
    // 创建主布局
    d_main_layout = new QVBoxLayout(this);

    // 创建绘图
    d_plot = new QwtPlot(this);
    d_plot->setTitle(title);
    d_plot->setAxisTitle(QwtPlot::xBottom, x_label);
    d_plot->setAxisTitle(QwtPlot::yLeft, y_label);

    // 创建频谱图
    d_spectrogram = new QwtPlotSpectrogram();
    d_spectrogram->setRenderThreadCount(0);  // 使用多线程渲染
    d_spectrogram->setDisplayMode(QwtPlotSpectrogram::ImageMode, true);
    d_spectrogram->setDisplayMode(QwtPlotSpectrogram::ContourMode, false);

    // 创建栅格数据
    d_raster_data = new HRRP2DRasterData();
    d_spectrogram->setData(d_raster_data);

    // 设置颜色映射
    d_spectrogram->setColorMap(createColorMap(0));  // Jet colormap

    // 附加到绘图
    d_spectrogram->attach(d_plot);

    // 设置颜色条
    setupColorBar();

    // 创建缩放/平移工具
    QwtPlotCanvas* canvas = new QwtPlotCanvas();
    canvas->setFrameStyle(QFrame::Box | QFrame::Plain);
    d_plot->setCanvas(canvas);

    d_zoomer = new QwtPlotZoomer(canvas);
    d_zoomer->setRubberBandPen(QColor(Qt::darkGreen));
    d_zoomer->setTrackerPen(QColor(Qt::darkGreen));
    d_zoomer->setMousePattern(QwtEventPattern::MouseSelect2,
                              Qt::RightButton, Qt::ControlModifier);
    d_zoomer->setMousePattern(QwtEventPattern::MouseSelect3,
                              Qt::RightButton);

    d_panner = new QwtPlotPanner(canvas);
    d_panner->setMouseButton(Qt::MiddleButton);

    d_magnifier = new QwtPlotMagnifier(canvas);
    d_magnifier->setMouseButton(Qt::NoButton);  // 禁用鼠标按钮，只用滚轮

    // 创建控制面板
    d_control_group = new QGroupBox("控制", this);
    d_control_layout = new QHBoxLayout(d_control_group);

    // 颜色映射选择
    QLabel* colormap_label = new QLabel("颜色映射:", d_control_group);
    d_colormap_combo = new QComboBox(d_control_group);
    d_colormap_combo->addItem("Jet");
    d_colormap_combo->addItem("Hot");
    d_colormap_combo->addItem("Cool");
    d_colormap_combo->addItem("Viridis");
    d_colormap_combo->addItem("Grayscale");
    connect(d_colormap_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Spectrogram2DWidget::onColorMapChanged);

    // Y轴范围控制
    d_y_min_label = new QLabel("Y最小:", d_control_group);
    d_y_min_spin = new QDoubleSpinBox(d_control_group);
    d_y_min_spin->setRange(0, 10000);
    d_y_min_spin->setDecimals(1);
    d_y_min_spin->setValue(0);

    d_y_max_label = new QLabel("Y最大:", d_control_group);
    d_y_max_spin = new QDoubleSpinBox(d_control_group);
    d_y_max_spin->setRange(0, 10000);
    d_y_max_spin->setDecimals(1);
    d_y_max_spin->setValue(100);

    d_y_set_btn = new QPushButton("设置Y范围", d_control_group);
    connect(d_y_set_btn, &QPushButton::clicked, [this]() {
        setYAxisRange(d_y_min_spin->value(), d_y_max_spin->value());
    });

    // 重置按钮
    d_reset_btn = new QPushButton("重置视图", d_control_group);
    connect(d_reset_btn, &QPushButton::clicked, this, &Spectrogram2DWidget::onResetView);

    // 添加到控制布局
    d_control_layout->addWidget(colormap_label);
    d_control_layout->addWidget(d_colormap_combo);
    d_control_layout->addStretch();
    d_control_layout->addWidget(d_y_min_label);
    d_control_layout->addWidget(d_y_min_spin);
    d_control_layout->addWidget(d_y_max_label);
    d_control_layout->addWidget(d_y_max_spin);
    d_control_layout->addWidget(d_y_set_btn);
    d_control_layout->addStretch();
    d_control_layout->addWidget(d_reset_btn);

    // 添加到主布局
    d_main_layout->addWidget(d_plot, 1);
    d_main_layout->addWidget(d_control_group);

    setLayout(d_main_layout);
}

Spectrogram2DWidget::~Spectrogram2DWidget()
{
    // Qt会自动清理子对象
}

void Spectrogram2DWidget::setupColorBar()
{
    QwtScaleWidget* rightAxis = d_plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("幅度 (dB)");
    rightAxis->setColorBarEnabled(true);
    rightAxis->setColorBarWidth(20);

    d_plot->setAxisScale(QwtPlot::yRight, -60, 0);
    d_plot->enableAxis(QwtPlot::yRight);

    // 设置颜色条的颜色映射
    rightAxis->setColorMap(QwtInterval(-60, 0), createColorMap(d_colormap_type));
}

QwtLinearColorMap* Spectrogram2DWidget::createColorMap(int type)
{
    QwtLinearColorMap* colorMap = nullptr;

    switch (type) {
    case 0:  // Jet
        colorMap = new QwtLinearColorMap(Qt::darkBlue, Qt::darkRed);
        colorMap->addColorStop(0.25, Qt::blue);
        colorMap->addColorStop(0.4, Qt::cyan);
        colorMap->addColorStop(0.5, Qt::green);
        colorMap->addColorStop(0.6, Qt::yellow);
        colorMap->addColorStop(0.75, Qt::red);
        break;

    case 1:  // Hot
        colorMap = new QwtLinearColorMap(Qt::black, Qt::white);
        colorMap->addColorStop(0.33, Qt::darkRed);
        colorMap->addColorStop(0.5, Qt::red);
        colorMap->addColorStop(0.67, Qt::yellow);
        break;

    case 2:  // Cool
        colorMap = new QwtLinearColorMap(Qt::cyan, Qt::magenta);
        break;

    case 3:  // Viridis-like
        colorMap = new QwtLinearColorMap(QColor(68, 1, 84), QColor(253, 231, 37));
        colorMap->addColorStop(0.25, QColor(59, 82, 139));
        colorMap->addColorStop(0.5, QColor(33, 145, 140));
        colorMap->addColorStop(0.75, QColor(94, 201, 98));
        break;

    case 4:  // Grayscale
        colorMap = new QwtLinearColorMap(Qt::black, Qt::white);
        break;

    default:
        colorMap = new QwtLinearColorMap(Qt::darkBlue, Qt::darkRed);
        break;
    }

    return colorMap;
}

void Spectrogram2DWidget::updateData(const std::vector<double>& data,
                                      const std::vector<double>& coarse_axis,
                                      const std::vector<double>& fine_axis,
                                      size_t n_coarse,
                                      size_t n_fine)
{
    if (data.empty() || n_coarse == 0 || n_fine == 0) {
        return;
    }

    // 获取轴范围
    double x_min = fine_axis.empty() ? 0 : fine_axis.front();
    double x_max = fine_axis.empty() ? static_cast<double>(n_fine) : fine_axis.back();
    double y_min = coarse_axis.empty() ? 0 : coarse_axis.front();
    double y_max = coarse_axis.empty() ? static_cast<double>(n_coarse) : coarse_axis.back();

    // 设置数据
    d_raster_data->setData(data, n_coarse, n_fine, x_min, x_max, y_min, y_max);

    // 更新轴范围
    d_plot->setAxisScale(QwtPlot::xBottom, x_min, x_max);
    d_plot->setAxisScale(QwtPlot::yLeft, y_min, y_max);

    // 更新Y轴控制
    d_y_min_spin->setValue(y_min);
    d_y_max_spin->setValue(y_max);

    // 更新颜色条范围
    auto minmax = std::minmax_element(data.begin(), data.end());
    double z_min = *minmax.first;
    double z_max = *minmax.second;
    d_plot->setAxisScale(QwtPlot::yRight, z_min, z_max);

    QwtScaleWidget* rightAxis = d_plot->axisWidget(QwtPlot::yRight);
    rightAxis->setColorMap(QwtInterval(z_min, z_max), createColorMap(d_colormap_type));

    // 重绘
    d_plot->replot();

    std::cout << "[Spectrogram2DWidget] Updated with " << n_coarse << " x " << n_fine 
              << " data, range: [" << z_min << ", " << z_max << "]" << std::endl;
}

void Spectrogram2DWidget::setTitle(const QString& title)
{
    d_plot->setTitle(title);
}

void Spectrogram2DWidget::setColorMap(int type)
{
    d_colormap_type = type;
    d_spectrogram->setColorMap(createColorMap(type));
    
    // 更新颜色条
    QwtScaleWidget* rightAxis = d_plot->axisWidget(QwtPlot::yRight);
    QwtInterval interval = d_plot->axisInterval(QwtPlot::yRight);
    rightAxis->setColorMap(interval, createColorMap(type));
    
    d_plot->replot();
}

void Spectrogram2DWidget::setYAxisRange(double y_min, double y_max)
{
    d_plot->setAxisScale(QwtPlot::yLeft, y_min, y_max);
    d_plot->replot();
}

void Spectrogram2DWidget::resetView()
{
    d_zoomer->zoom(0);  // 回到原始缩放级别
}

void Spectrogram2DWidget::onColorMapChanged(int index)
{
    setColorMap(index);
}

void Spectrogram2DWidget::onResetView()
{
    resetView();
}

} // namespace plasma
} // namespace gr
