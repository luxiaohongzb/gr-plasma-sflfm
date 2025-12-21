/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_PLASMA_SPECTROGRAM_2D_WIDGET_H
#define INCLUDED_PLASMA_SPECTROGRAM_2D_WIDGET_H

#include <qwt/qwt_plot.h>
#include <qwt/qwt_plot_canvas.h>
#include <qwt/qwt_plot_spectrogram.h>
#include <qwt/qwt_matrix_raster_data.h>
#include <qwt/qwt_color_map.h>
#include <qwt/qwt_scale_widget.h>
#include <qwt/qwt_plot_zoomer.h>
#include <qwt/qwt_plot_panner.h>
#include <qwt/qwt_plot_magnifier.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QComboBox>

#include <vector>

namespace gr {
namespace plasma {

/**
 * @brief 自定义矩阵数据类，用于QwtPlotSpectrogram
 */
class HRRP2DRasterData : public QwtRasterData
{
public:
    HRRP2DRasterData();
    virtual ~HRRP2DRasterData();

    /**
     * @brief 设置数据
     * @param data 2D数据 (展平为1D, 行优先)
     * @param n_rows 行数（粗距离单元数）
     * @param n_cols 列数（精细距离单元数）
     * @param x_min X轴最小值
     * @param x_max X轴最大值
     * @param y_min Y轴最小值
     * @param y_max Y轴最大值
     */
    void setData(const std::vector<double>& data, 
                 size_t n_rows, size_t n_cols,
                 double x_min, double x_max,
                 double y_min, double y_max);

    virtual double value(double x, double y) const override;
    
    // 获取数据范围
    QwtInterval xInterval() const { return QwtInterval(d_x_min, d_x_max); }
    QwtInterval yInterval() const { return QwtInterval(d_y_min, d_y_max); }
    QwtInterval zInterval() const { return QwtInterval(d_z_min, d_z_max); }

private:
    std::vector<double> d_data;
    size_t d_n_rows;
    size_t d_n_cols;
    double d_x_min, d_x_max;
    double d_y_min, d_y_max;
    double d_z_min, d_z_max;
};

/**
 * @brief 2D热力图绘图组件（用于IFFT合成后的高分辨距离像）
 * 
 * 显示 粗距离(Y) vs 精细距离(X) 的二维强度图
 */
class Spectrogram2DWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param title 绘图标题
     * @param x_label X轴标签（精细距离）
     * @param y_label Y轴标签（粗距离）
     * @param parent 父窗口
     */
    Spectrogram2DWidget(const QString& title = "2D HRRP",
                        const QString& x_label = "精细距离单元",
                        const QString& y_label = "粗距离 (m)",
                        QWidget* parent = nullptr);
    
    ~Spectrogram2DWidget();

    /**
     * @brief 更新2D数据
     * @param data 2D数据 (展平为1D, 行优先: n_coarse x n_fine)
     * @param coarse_axis 粗距离轴 (m)
     * @param fine_axis 精细距离轴 (索引或米)
     * @param n_coarse 粗距离单元数（行数）
     * @param n_fine 精细距离单元数（列数）
     */
    void updateData(const std::vector<double>& data,
                    const std::vector<double>& coarse_axis,
                    const std::vector<double>& fine_axis,
                    size_t n_coarse,
                    size_t n_fine);

    /**
     * @brief 设置标题
     */
    void setTitle(const QString& title);

    /**
     * @brief 设置颜色映射类型
     */
    void setColorMap(int type);

    /**
     * @brief 设置Y轴范围（粗距离）
     */
    void setYAxisRange(double y_min, double y_max);

    /**
     * @brief 重置视图
     */
    void resetView();

public slots:
    void onColorMapChanged(int index);
    void onResetView();

private:
    void setupColorBar();
    QwtLinearColorMap* createColorMap(int type);

    // Qwt plot objects
    QwtPlot* d_plot;
    QwtPlotSpectrogram* d_spectrogram;
    HRRP2DRasterData* d_raster_data;
    QwtPlotZoomer* d_zoomer;
    QwtPlotPanner* d_panner;
    QwtPlotMagnifier* d_magnifier;

    // Control widgets
    QGroupBox* d_control_group;
    QHBoxLayout* d_control_layout;
    QComboBox* d_colormap_combo;
    QPushButton* d_reset_btn;
    QLabel* d_y_min_label;
    QLabel* d_y_max_label;
    QDoubleSpinBox* d_y_min_spin;
    QDoubleSpinBox* d_y_max_spin;
    QPushButton* d_y_set_btn;

    // Layout
    QVBoxLayout* d_main_layout;

    // Current color map type
    int d_colormap_type;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_SPECTROGRAM_2D_WIDGET_H */
