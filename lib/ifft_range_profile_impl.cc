/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ifft_range_profile_impl.h"
#include <gnuradio/io_signature.h>
#include <gnuradio/plasma/pmt_constants.h>
#include <gnuradio/plasma/qt_update_events.h>
#include <QApplication>
#include <volk/volk.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

namespace gr {
namespace plasma {

ifft_range_profile::sptr ifft_range_profile::make(double bandwidth,
                                                  double pulse_width,
                                                  double sample_rate,
                                                  double prf,
                                                  double sweep_start,
                                                  double sweep_stop,
                                                  double sweep_step,
                                                  const std::string& freq_key,
                                                  int n_fft_synthesis,
                                                  QWidget* parent)
{
    return gnuradio::make_block_sptr<ifft_range_profile_impl>(
        bandwidth, pulse_width, sample_rate, prf,
        sweep_start, sweep_stop, sweep_step,
        freq_key, n_fft_synthesis, parent);
}

ifft_range_profile_impl::ifft_range_profile_impl(double bandwidth,
                                                 double pulse_width,
                                                 double sample_rate,
                                                 double prf,
                                                 double sweep_start,
                                                 double sweep_stop,
                                                 double sweep_step,
                                                 const std::string& freq_key,
                                                 int n_fft_synthesis,
                                                 QWidget* parent)
    : gr::block("ifft_range_profile",
                gr::io_signature::make(0, 0, 0),
                gr::io_signature::make(0, 0, 0)),
      d_bandwidth(bandwidth),
      d_pulse_width(pulse_width),
      d_sample_rate(sample_rate),
      d_prf(prf),
      d_sweep_start(sweep_start),
      d_sweep_stop(sweep_stop),
      d_sweep_step(sweep_step),
      d_freq_key(freq_key),
      d_n_fft_synthesis(n_fft_synthesis),
      d_dynamic_range_db(60.0),
      d_argc(1),
      d_finished(false),
      d_msg_queue_depth(100),
      d_processing(false),
      d_pc_processing_count(0),
      d_backend(AF_BACKEND_DEFAULT)
{
    // 注册消息端口
    d_in_port = pmt::intern("in");
    message_port_register_in(d_in_port);
    set_msg_handler(d_in_port,
                   [this](pmt::pmt_t msg) { handle_rx_msg(msg); });

    // 计算预期频率列表
    compute_expected_freqs();
    
    // 初始化匹配滤波器
    update_matched_filter();

    // 初始化Qt应用
    d_argc = 1;
    d_argv = new char;
    d_argv[0] = '\0';
    
    if (qApp != NULL) {
        d_qapp = qApp;
    } else {
        d_qapp = new QApplication(d_argc, &d_argv);
    }

    // 创建GUI窗口
    d_main_gui = new IFFTRangeProfileWindow(parent, bandwidth, sample_rate, n_fft_synthesis);
}

ifft_range_profile_impl::~ifft_range_profile_impl()
{
    if (d_main_gui != nullptr) {
        delete d_main_gui;
    }
}

bool ifft_range_profile_impl::start()
{
    d_finished = false;
    // 在启动时设置一次后端，确保后续操作使用正确的后端
    af::setBackend(d_backend);
    af::sync();
    return block::start();
}

bool ifft_range_profile_impl::stop()
{
    d_finished = true;
    // 等待扫频处理完成
    while (d_processing.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // 等待所有脉冲压缩处理完成
    while (d_pc_processing_count.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return block::stop();
}

void ifft_range_profile_impl::exec_() { d_qapp->exec(); }

QWidget* ifft_range_profile_impl::qwidget() { return d_main_gui; }

#ifdef ENABLE_PYTHON
PyObject* ifft_range_profile_impl::pyqwidget()
{
    PyObject* w = PyLong_FromVoidPtr((void*)d_main_gui);
    PyObject* retarg = Py_BuildValue("N", w);
    return retarg;
}
#else
void* ifft_range_profile_impl::pyqwidget() { return NULL; }
#endif

void ifft_range_profile_impl::set_dynamic_range(const double range_db)
{
    d_dynamic_range_db = range_db;
}

void ifft_range_profile_impl::set_msg_queue_depth(size_t depth)
{
    d_msg_queue_depth = depth;
}

void ifft_range_profile_impl::set_backend(Device::Backend backend)
{
    switch (backend) {
    case Device::CPU:
        d_backend = AF_BACKEND_CPU;
        break;
    case Device::CUDA:
        d_backend = AF_BACKEND_CUDA;
        break;
    case Device::OPENCL:
        d_backend = AF_BACKEND_OPENCL;
        break;
    default:
        d_backend = AF_BACKEND_DEFAULT;
        break;
    }
    af::setBackend(d_backend);
    // 重新生成匹配滤波器以使用新的后端
    update_matched_filter();
}

void ifft_range_profile_impl::update_matched_filter()
{
    // 注意：后端应该在 start() 或 set_backend() 中已经设置
    // 这里为了安全起见也设置一次，但通常不需要
     af::setBackend(d_backend);  // 可以注释掉，因为后端是全局的
    
    // 使用 plasma_dsp 生成 LFM 参考信号
    double start_freq = -d_bandwidth / 2.0;
    af::array lfm_waveform = ::plasma::lfm(start_freq, d_bandwidth, d_pulse_width, d_sample_rate).as(c32);
    
    // 创建匹配滤波器：共轭翻转
    d_match_filt = af::conjg(lfm_waveform);
    d_match_filt = af::flip(d_match_filt, 0);
    
    std::cout << "[ifft_range_profile] Matched filter updated: " 
              << d_match_filt.elements() << " samples" << std::endl;
}

void ifft_range_profile_impl::compute_expected_freqs()
{
    d_expected_freqs.clear();
    if (d_sweep_step == 0.0) {
        return;
    }
    
    if (d_sweep_step > 0) {
        for (double f = d_sweep_start; f <= d_sweep_stop + 1e-9; f += d_sweep_step) {
            d_expected_freqs.push_back(f);
        }
    } else {
        for (double f = d_sweep_start; f >= d_sweep_stop - 1e-9; f += d_sweep_step) {
            d_expected_freqs.push_back(f);
        }
    }
    
    std::cout << "[ifft_range_profile] Expected " << d_expected_freqs.size() 
              << " frequency points" << std::endl;
}

bool ifft_range_profile_impl::check_sweep_complete()
{
    if (d_expected_freqs.empty())
        return false;
    
    for (double f : d_expected_freqs) {
        if (d_freq_data.find(f) == d_freq_data.end() || d_freq_data[f].empty())
            return false;
    }
    return true;
}

void ifft_range_profile_impl::handle_rx_msg(pmt::pmt_t msg)
{
    // 检查消息队列深度，避免阻塞
    if (nmsgs(d_in_port) > d_msg_queue_depth) {
        std::cout << "[ifft_range_profile] Message queue full (" 
                  << nmsgs(d_in_port) << " > " << d_msg_queue_depth 
                  << "), dropping message" << std::endl;
        return;
    }

    if (!pmt::is_pdu(msg)) {
        std::cout << "[ifft_range_profile] Not a PDU message" << std::endl;
        return;
    }

    pmt::pmt_t meta = pmt::car(msg);
    pmt::pmt_t vec = pmt::cdr(msg);

    // 获取频率元数据
    pmt::pmt_t freq_pmt = pmt::dict_ref(meta, pmt::intern(d_freq_key), pmt::PMT_NIL);
    if (freq_pmt == pmt::PMT_NIL) {
        std::cout << "[ifft_range_profile] No frequency metadata" << std::endl;
        return;
    }

    double freq = pmt::to_double(freq_pmt);

    if (!pmt::is_c32vector(vec)) {
        std::cout << "[ifft_range_profile] PDU data is not c32vector" << std::endl;
        return;
    }

    size_t n = pmt::length(vec);
    auto vec_data = pmt::c32vector_elements(vec);
    const gr_complex* data = vec_data.data();

    std::cout << "[ifft_range_profile] Received freq=" << freq / 1e9 << " GHz, " 
              << n << " samples" << std::endl;

    {
        std::lock_guard<std::mutex> lock(d_mutex);
        // 存储当前频率的数据
        d_freq_data[freq].assign(data, data + n);
    }

    // 立即处理并显示当前频率的脉冲压缩结果
    // 在独立线程中异步处理，避免阻塞消息处理
    // 每个频率的脉冲压缩显示是独立的，可以并行处理
    d_pc_processing_count++;
    
    // 复制数据到局部变量，避免在独立线程中访问共享数据
    std::vector<gr_complex> freq_data_copy;
    double freq_copy = freq;
    {
        std::lock_guard<std::mutex> lock(d_mutex);
        auto it = d_freq_data.find(freq);
        if (it != d_freq_data.end() && !it->second.empty()) {
            freq_data_copy = it->second;
        } else {
            d_pc_processing_count--;
            return;
        }
    }
    
    std::thread([this, freq_data_copy, freq_copy]() {
        try {
            // 注意：后端应该在 start() 中已经设置，但为了安全起见在这里也设置一次
            // 因为 ArrayFire 后端是全局的，在多线程环境中可能会有竞态条件
            af::setBackend(d_backend);
            af::sync();
            
            // 检查并更新匹配滤波器
            try {
                size_t match_filt_size = d_match_filt.elements();
                if (match_filt_size == 0) {
                    update_matched_filter();
                }
            } catch (const af::exception& e) {
                update_matched_filter();
            }
            
            // 执行脉冲压缩
            int n_pri = static_cast<int>(d_sample_rate / d_prf);
            std::vector<gr_complex> pc_result;
            perform_pulse_compression(freq_data_copy, pc_result, n_pri);
            
            // 发送脉冲压缩结果到GUI
            if (d_main_gui && !d_main_gui->is_closed() && !pc_result.empty()) {
                PulseCompressionUpdateEvent* pc_event = new PulseCompressionUpdateEvent();
                
                // 计算距离轴：基于采样率的距离步长（过采样）
                // 每个采样点对应的距离间隔 = c * Ts / 2
                double range_step = SPEED_OF_LIGHT / (2.0 * d_sample_rate);
                std::vector<double> pc_range_axis(pc_result.size());
                std::vector<double> pc_profile(pc_result.size());
                
                for (size_t i = 0; i < pc_result.size(); i++) {
                    pc_range_axis[i] = i * range_step;
                    pc_profile[i] = std::abs(pc_result[i]);
                }
                
                pc_event->setPulseCompressionData(pc_range_axis, pc_profile, freq_copy);
                qApp->postEvent(d_main_gui, pc_event);
                
                std::cout << "[ifft_range_profile] Sent pulse compression result for freq " 
                          << freq_copy / 1e9 << " GHz to GUI" << std::endl;
            }
        } catch (const af::exception& e) {
            std::cerr << "[ifft_range_profile] ArrayFire error processing frequency " 
                      << freq_copy / 1e9 << " GHz: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[ifft_range_profile] Error processing frequency " 
                      << freq_copy / 1e9 << " GHz: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ifft_range_profile] Unknown error processing frequency " 
                      << freq_copy / 1e9 << " GHz" << std::endl;
        }
        d_pc_processing_count--;
    }).detach();

    // 检查是否收集完整扫频
    bool complete = false;
    {
        std::lock_guard<std::mutex> lock(d_mutex);
        complete = check_sweep_complete();
    }

    if (complete) {
        // 检查是否正在处理，避免重复处理
        bool expected = false;
        if (d_processing.compare_exchange_strong(expected, true)) {
            std::cout << "[ifft_range_profile] Sweep complete, processing..." << std::endl;
            // 在单独线程中异步处理，避免阻塞消息处理
            std::thread([this]() {
                try {
                    // 确保 ArrayFire 上下文正确初始化
                    af::sync();
                    process_sweep();
                } catch (const af::exception& e) {
                    std::cerr << "[ifft_range_profile] ArrayFire thread error: " << e.what() << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[ifft_range_profile] Thread error: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[ifft_range_profile] Unknown thread error" << std::endl;
                }
                d_processing = false;
            }).detach();
        } else {
            std::cout << "[ifft_range_profile] Sweep complete but already processing, skipping" << std::endl;
        }
    }
}

void ifft_range_profile_impl::perform_pulse_compression(
    const std::vector<gr_complex>& input,
    std::vector<gr_complex>& output,
    int n_pri)
{
    // 确保后端已设置（应该在 process_sweep 中已经设置）
    // 这里不再设置后端，因为已经在 process_sweep 中设置过了
    
    // 计算输入数据长度
    int input_len = std::min(n_pri, static_cast<int>(input.size()));
    int n_samp_pulse = static_cast<int>(d_pulse_width * d_sample_rate);
    
    // 计算FFT大小（下一个2的幂次），用于保持与原始实现的一致性
    int n_fft_fast = 1;
    while (n_fft_fast < (input_len + n_samp_pulse)) {
        n_fft_fast *= 2;
    }
    
    // 检查匹配滤波器是否有效
    size_t match_filt_size = 0;
    try {
        match_filt_size = d_match_filt.elements();
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] Error accessing matched filter: " << e.what() << std::endl;
        output.clear();
        return;
    }
    
    if (match_filt_size == 0) {
        std::cerr << "[ifft_range_profile] Error: matched filter is empty!" << std::endl;
        output.clear();
        return;
    }
    
    // 创建输入信号的 ArrayFire 数组
    af::array input_af(af::dim4(input_len), reinterpret_cast<const af::cfloat*>(input.data()));
    
    // 使用预计算的匹配滤波器进行卷积
    // 匹配滤波器已经包含了共轭和翻转
    // AF_CONV_EXPAND 表示输出长度 = input_len + match_filt_len - 1
    af::array pc_result;
    try {
        pc_result = af::convolve1(input_af, d_match_filt, AF_CONV_EXPAND, AF_CONV_AUTO);
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire convolution error: " << e.what() << std::endl;
        output.clear();
        return;
    }
    
    // 截掉前面对应匹配滤波器长度的样本（去除负距离/暂态响应部分）
    // 保留从 0 米开始的有效数据
    int valid_start_idx = static_cast<int>(match_filt_size);
    int result_len = pc_result.elements();
    
    if (valid_start_idx < result_len) {
        // 从 valid_start_idx 开始截取
        pc_result = pc_result(af::seq(valid_start_idx, result_len - 1));
        result_len = pc_result.elements();
    }
    
    // 截断或填充到 n_fft_fast 长度，以保持与原始 FFT 实现的一致性
    if (result_len > n_fft_fast) {
        // 截断到 n_fft_fast
        pc_result = pc_result(af::seq(n_fft_fast));
    } else if (result_len < n_fft_fast) {
        // 零填充到 n_fft_fast
        af::array padded = af::constant(0, n_fft_fast, c32);
        padded(af::seq(result_len)) = pc_result;
        pc_result = padded;
    }
    
    // 将结果复制回输出向量
    // 使用 host() 方法直接写入预分配的缓冲区，避免额外的内存分配
    output.resize(n_fft_fast);
    try {
        // 确保所有 ArrayFire 操作完成
        af::sync();
        pc_result.host(reinterpret_cast<af::cfloat*>(output.data()));
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire host() error: " << e.what() << std::endl;
        output.clear();
        return;
    }
}

// void ifft_range_profile_impl::perform_phase_calibration(
//     std::vector<std::vector<gr_complex>>& pc_data,
//     int search_range_bins)
// {
//     if (pc_data.empty() || pc_data[0].empty()) {
//         std::cout << "[ifft_range_profile] Cannot perform phase calibration: empty data" << std::endl;
//         return;
//     }

//     int n_freq = pc_data.size();      // 频点数（慢时间）
//     int n_range = pc_data[0].size();  // 距离单元数（快时间）
    
//     // 限制搜索范围，避免越界
//     int search_limit = std::min(search_range_bins, n_range);
    
//     std::cout << "[ifft_range_profile] Phase calibration: searching leakage in first " 
//               << search_limit << " range bins across " << n_freq << " frequencies" << std::endl;

//     // 1. 逐频点寻找直耦峰值
//     std::vector<int> leak_indices(n_freq);
//     std::vector<gr_complex> leak_phasors(n_freq);
    
//     for (int freq_idx = 0; freq_idx < n_freq; freq_idx++) {
//         // 在当前频点的前 search_limit 个距离单元中找最大值
//         double max_magnitude = 0.0;
//         int max_idx = 0;
        
//         for (int range_idx = 0; range_idx < search_limit; range_idx++) {
//             double mag = std::abs(pc_data[freq_idx][range_idx]);
//             if (mag > max_magnitude) {
//                 max_magnitude = mag;
//                 max_idx = range_idx;
//             }
//         }
        
//         leak_indices[freq_idx] = max_idx;
//         leak_phasors[freq_idx] = pc_data[freq_idx][max_idx];
        
//         std::cout << "[ifft_range_profile] Freq " << freq_idx 
//                   << ": leakage peak at bin " << max_idx 
//                   << ", magnitude=" << max_magnitude 
//                   << ", phase=" << std::arg(leak_phasors[freq_idx]) << " rad" << std::endl;
//     }

//     // 2. 计算校准因子：conj(leak_phasors) / abs(leak_phasors)
//     // 这会将直耦相位归零
//     std::vector<gr_complex> calibration_factors(n_freq);
//     for (int i = 0; i < n_freq; i++) {
//         float mag = std::abs(leak_phasors[i]);
//         if (mag > 1e-10f) {  // 避免除零
//             calibration_factors[i] = std::conj(leak_phasors[i]) / mag;
//         } else {
//             calibration_factors[i] = gr_complex(1.0f, 0.0f);
//             std::cout << "[ifft_range_profile] Warning: leakage magnitude too small at freq " 
//                       << i << ", skipping calibration" << std::endl;
//         }
//     }

//     // 3. 应用相位校准到所有距离单元
//     std::cout << "[ifft_range_profile] Applying phase calibration..." << std::endl;
//     for (int freq_idx = 0; freq_idx < n_freq; freq_idx++) {
//         gr_complex cal_factor = calibration_factors[freq_idx];
//         for (int range_idx = 0; range_idx < n_range; range_idx++) {
//             pc_data[freq_idx][range_idx] *= cal_factor;
//         }
//     }

//     // 4. 验证校准效果：检查校准后直耦相位
//     std::cout << "[ifft_range_profile] Calibration verification:" << std::endl;
//     for (int freq_idx = 0; freq_idx < n_freq; freq_idx++) {
//         gr_complex calibrated_leakage = pc_data[freq_idx][leak_indices[freq_idx]];
//         double phase_after = std::arg(calibrated_leakage);
//         std::cout << "  Freq " << freq_idx << ": phase after calibration = " 
//                   << phase_after << " rad (should be near 0)" << std::endl;
//     }
    
//     std::cout << "[ifft_range_profile] Phase calibration complete" << std::endl;
// }

void ifft_range_profile_impl::perform_ifft_synthesis(
    const std::vector<std::vector<gr_complex>>& pc_data,
    std::vector<gr_complex>& hrrp)
{
    if (pc_data.empty()) {
        return;
    }

    // 确保后端已设置（应该在 process_sweep 中已经设置）
    // 这里不再设置后端，因为已经在 process_sweep 中设置过了
    
    int n_fast = pc_data[0].size();
    int n_slow = pc_data.size();

    // 将脉冲压缩数据组织成矩阵：每行是一个频率，每列是一个快时间单元
    // 创建一个临时向量来存储所有数据
    std::vector<gr_complex> temp_data(n_fast * n_slow);
    for (int j = 0; j < n_slow; j++) {
        for (int i = 0; i < n_fast; i++) {
            if (i < static_cast<int>(pc_data[j].size())) {
                temp_data[j * n_fast + i] = pc_data[j][i];
            } else {
                temp_data[j * n_fast + i] = gr_complex(0, 0);
            }
        }
    }
    
    // 创建 ArrayFire 数组：形状为 (n_slow, n_fast)，每行是一个频率的数据，每列是一个快时间单元
    af::array pc_matrix;
    try {
        pc_matrix = af::array(af::dim4(n_slow, n_fast), reinterpret_cast<const af::cfloat*>(temp_data.data()));
        
        // 转置矩阵，使得每行是一个快时间单元的数据（跨频率），每列是一个频率
        // 形状变为 (n_fast, n_slow)，每行是一个快时间单元，每列是一个频率
        pc_matrix = pc_matrix.T();
        
        // ArrayFire 的 ifftNorm 默认沿第0维（行）进行 IFFT
        // 所以对每一行（每个快时间单元）进行 IFFT，沿频率维度（列方向）进行
        // 第三个参数 odim0 用于零填充到指定大小 d_n_fft_synthesis
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire array creation error: " << e.what() << std::endl;
        hrrp.clear();
        return;
    }
    
    af::array ifft_result;
    try {
        ifft_result = af::ifftNorm(pc_matrix, 1.0, d_n_fft_synthesis);
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire IFFT error: " << e.what() << std::endl;
        hrrp.clear();
        return;
    }
    // ifft_result 形状是 (n_fast, d_n_fft_synthesis)，每行是一个快时间单元的精细距离像
    
    // 将结果复制回输出向量，按行优先顺序
    // 使用 host() 方法直接写入预分配的缓冲区，避免额外的内存分配
    hrrp.resize(n_fast * d_n_fft_synthesis);
    try {
        // 确保所有 ArrayFire 操作完成
        af::sync();
        ifft_result.host(reinterpret_cast<af::cfloat*>(hrrp.data()));
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire host() error in IFFT: " << e.what() << std::endl;
        hrrp.clear();
        return;
    }
}

void ifft_range_profile_impl::perform_frequency_domain_synthesis(
    const std::map<double, std::vector<gr_complex>>& freq_data_map,
    const std::vector<double>& freq_list,
    const std::vector<gr_complex>& calibration_factors,
    std::vector<gr_complex>& hrrp,
    int& n_fft_fast)
{
    if (freq_data_map.empty() || freq_list.empty()) {
        hrrp.clear();
        return;
    }

    int n_pulse_cpi = freq_list.size();
    int n_pri = static_cast<int>(d_sample_rate / d_prf);
    
    std::cout << "[ifft_range_profile] Starting frequency domain synthesis..." << std::endl;
    std::cout << "  n_pri=" << n_pri << ", n_pulse_cpi=" << n_pulse_cpi << std::endl;

    // 1. 生成参考信号的频谱（匹配滤波器）
    int n_samp_pulse = static_cast<int>(d_pulse_width * d_sample_rate);
    double start_freq = -d_bandwidth / 2.0;
    
    // 生成时域LFM参考信号
    std::vector<gr_complex> ref_signal(n_pri, gr_complex(0, 0));
    for (int i = 0; i < n_samp_pulse; i++) {
        double t = i / d_sample_rate;
        double phase = 2.0 * M_PI * (start_freq * t + (d_bandwidth / (2.0 * d_pulse_width)) * t * t);
        ref_signal[i] = gr_complex(std::cos(phase), std::sin(phase));
    }
    
    // FFT得到参考频谱
    af::array ref_af(n_pri, reinterpret_cast<const af::cfloat*>(ref_signal.data()));
    af::array ref_spec = af::fft(ref_af);
    ref_spec = af::shift(ref_spec, n_pri / 2);  // fftshift
    
    // 生成频域遮罩（带宽限制）
    std::vector<float> mask_data(n_pri, 0.0f);
    double df_bin = d_sample_rate / n_pri;
    for (int i = 0; i < n_pri; i++) {
        double f = (i - n_pri / 2.0) * df_bin;
        if (std::abs(f) <= d_bandwidth / 2.0 * 1.2) {
            mask_data[i] = 1.0f;
        }
    }
    af::array ref_mask(n_pri, mask_data.data());
    
    // 2. 计算合成参数
    double df_step = (n_pulse_cpi > 1) ? (freq_list[1] - freq_list[0]) : d_bandwidth;
    double total_bw = (n_pulse_cpi - 1) * std::abs(df_step) + d_bandwidth;
    double fs_high_req = total_bw * 2.0;  // 2倍过采样
    int k_up = static_cast<int>(std::ceil(fs_high_req / d_sample_rate));
    double fs_high = k_up * d_sample_rate;
    int l_high = n_pri * k_up;
    
    n_fft_fast = l_high;  // 返回合成后的大小
    
    std::cout << "  Total BW: " << total_bw / 1e6 << " MHz" << std::endl;
    std::cout << "  Synthesis Fs: " << fs_high / 1e6 << " MHz (K=" << k_up << ")" << std::endl;
    std::cout << "  Synthesis length: " << l_high << " samples" << std::endl;
    
    // 3. 初始化总频谱（校准后）
    std::vector<gr_complex> spec_total(l_high, gr_complex(0, 0));
    
    // 4. 逐脉冲处理
    for (int n = 0; n < n_pulse_cpi; n++) {
        double freq = freq_list[n];
        auto it = freq_data_map.find(freq);
        if (it == freq_data_map.end()) {
            std::cerr << "[ifft_range_profile] Missing data for freq " << freq / 1e9 << " GHz" << std::endl;
            continue;
        }
        
        const std::vector<gr_complex>& echo_pulse = it->second;
        if (echo_pulse.size() < static_cast<size_t>(n_pri)) {
            std::cerr << "[ifft_range_profile] Insufficient data at freq " << n << std::endl;
            continue;
        }
        
        // (1) FFT到频域
        af::array echo_af(n_pri, reinterpret_cast<const af::cfloat*>(echo_pulse.data()));
        af::array echo_spec = af::fft(echo_af);
        echo_spec = af::shift(echo_spec, n_pri / 2);  // fftshift
        
        // (2) 频域脉压：MF_Spec = Echo_Spec * conj(Ref_Spec) * Mask
        af::array mf_spec = echo_spec * af::conjg(ref_spec) * ref_mask;
        
        // (3) 扩展到高采样率数组
        std::vector<gr_complex> s_expanded(l_high, gr_complex(0, 0));
        std::vector<gr_complex> mf_spec_host(n_pri);
        mf_spec.host(reinterpret_cast<af::cfloat*>(mf_spec_host.data()));
        
        int center_start = (l_high - n_pri) / 2;
        for (int i = 0; i < n_pri; i++) {
            s_expanded[center_start + i] = mf_spec_host[i];
        }
        
        // (4) 频域搬移
        double freq_offset = freq - freq_list[0];
        int shift_bins = static_cast<int>(std::round(freq_offset / df_bin));
        
        // 循环移位
        std::vector<gr_complex> s_shifted(l_high);
        for (int i = 0; i < l_high; i++) {
            int src_idx = (i - shift_bins + l_high) % l_high;
            s_shifted[i] = s_expanded[src_idx];
        }
        
        // (5) 累加（应用相位校准）
        gr_complex cal_factor = (n < static_cast<int>(calibration_factors.size())) ? 
                                 calibration_factors[n] : gr_complex(1, 0);
        
        for (int i = 0; i < l_high; i++) {
            spec_total[i] += s_shifted[i] * cal_factor;
        }
        
        std::cout << "  Processed freq " << n << ": " << freq / 1e9 << " GHz, shift=" << shift_bins << " bins" << std::endl;
    }
    
    // 5. IFFT得到时域结果
    af::array spec_total_af(l_high, reinterpret_cast<const af::cfloat*>(spec_total.data()));
    spec_total_af = af::shift(spec_total_af, -l_high / 2);  // ifftshift
    af::array result_af = af::ifft(spec_total_af);
    
    // 复制结果
    hrrp.resize(l_high);
    result_af.host(reinterpret_cast<af::cfloat*>(hrrp.data()));
    
    std::cout << "[ifft_range_profile] Frequency domain synthesis complete: " << hrrp.size() << " samples" << std::endl;
}

void ifft_range_profile_impl::process_sweep()
{
    // 在独立线程中处理，需要确保 ArrayFire 后端已正确设置
    // 注意：ArrayFire 的后端设置是全局的，在多线程环境中需要小心
    try {
        // 注意：后端应该在 start() 中已经设置，但为了安全起见在这里也设置一次
        // 因为 ArrayFire 后端是全局的，在多线程环境中可能会有竞态条件
        af::setBackend(d_backend);
        af::sync();
        
        // 检查匹配滤波器是否在当前后端，如果不是则重新创建
        // 注意：ArrayFire 没有直接的方法检查数组的后端，所以我们通过尝试访问来检测
        try {
            // 尝试访问匹配滤波器的元素数量，如果后端不匹配会抛出异常
            size_t match_filt_size = d_match_filt.elements();
            if (match_filt_size == 0) {
                std::cout << "[ifft_range_profile] Matched filter is empty, recreating..." << std::endl;
                update_matched_filter();
            }
        } catch (const af::exception& e) {
            // 如果后端不匹配，重新创建匹配滤波器
            std::cout << "[ifft_range_profile] Matched filter backend mismatch, recreating..." << std::endl;
            update_matched_filter();
        }
        
        std::lock_guard<std::mutex> lock(d_mutex);

    // 计算PRI采样数
    int n_pri = static_cast<int>(d_sample_rate / d_prf);
    int n_pulse_cpi = d_expected_freqs.size();

    std::cout << "[ifft_range_profile] n_pri=" << n_pri 
              << ", n_pulse_cpi=" << n_pulse_cpi << std::endl;

    // 按频率排序收集数据
    std::vector<std::vector<gr_complex>> pulse_compressed;

    for (double freq : d_expected_freqs) {
        auto it = d_freq_data.find(freq);
        if (it != d_freq_data.end() && !it->second.empty()) {
            // 执行脉冲压缩
            std::vector<gr_complex> pc_result;
            perform_pulse_compression(it->second, pc_result, n_pri);
            pulse_compressed.push_back(pc_result);
            
            std::cout << "[ifft_range_profile] Pulse compression for freq " 
                      << freq / 1e9 << " GHz: " << pc_result.size() 
                      << " samples" << std::endl;
            
            // 注意：脉冲压缩结果已经在 handle_rx_msg 中发送到GUI了
            // 这里不再重复发送，避免重复显示
        }
    }

    if (pulse_compressed.empty()) {
        std::cout << "[ifft_range_profile] No pulse compressed data" << std::endl;
        d_freq_data.clear();
        return;
    }

    // ===== 相位校准 =====
    // 计算距离步长：基于采样率（过采样）
    double range_step = SPEED_OF_LIGHT / (2.0 * d_sample_rate);
    
    // 提取校准因子（基于脉冲压缩结果中的直耦峰值）
    int search_range_bins = static_cast<int>(171);  // 搜索前10米
    search_range_bins = std::max(10, std::min(search_range_bins, static_cast<int>(pulse_compressed[0].size() / 2)));
    
    std::cout << "[ifft_range_profile] Extracting calibration factors (search range: " 
              << search_range_bins << " bins)" << std::endl;
    
    // 提取直耦相位（不修改数据）
    int n_freq = pulse_compressed.size();
    std::vector<gr_complex> calibration_factors(n_freq);
    
    for (int freq_idx = 0; freq_idx < n_freq; freq_idx++) {
        // 在当前频点的前 search_range_bins 个距离单元中找最大值
        double max_magnitude = 0.0;
        int max_idx = 0;
        
        for (int range_idx = 0; range_idx < search_range_bins && range_idx < static_cast<int>(pulse_compressed[freq_idx].size()); range_idx++) {
            double mag = std::abs(pulse_compressed[freq_idx][range_idx]);
            if (mag > max_magnitude) {
                max_magnitude = mag;
                max_idx = range_idx;
            }
        }
        
        gr_complex leak_phasor = pulse_compressed[freq_idx][max_idx];
        float mag = std::abs(leak_phasor);
        if (mag > 1e-10f) {
            calibration_factors[freq_idx] = std::conj(leak_phasor) / mag;
        } else {
            calibration_factors[freq_idx] = gr_complex(1.0f, 0.0f);
        }
        
        std::cout << "  Freq " << freq_idx << ": leakage at bin " << max_idx 
                  << ", phase=" << std::arg(leak_phasor) << " rad" << std::endl;
    }

    // ===== 频域合成（新算法）=====
    std::vector<gr_complex> hrrp;
    int n_fft_fast = 0;  // 将由频域合成函数返回
    perform_frequency_domain_synthesis(d_freq_data, d_expected_freqs, calibration_factors, hrrp, n_fft_fast);

    std::cout << "[ifft_range_profile] Frequency domain synthesis complete: " 
              << hrrp.size() << " samples" << std::endl;

    // 检查结果
    if (hrrp.empty() || n_fft_fast == 0) {
        std::cerr << "[ifft_range_profile] Synthesis failed" << std::endl;
        d_freq_data.clear();
        return;
    }

    // 频域合成后，hrrp是一维高分辨距离像
    // 计算新的距离步长（基于合成后的采样率）
    double df_step = (n_pulse_cpi > 1) ? std::abs(d_expected_freqs[1] - d_expected_freqs[0]) : d_bandwidth;
    double total_bw = (n_pulse_cpi - 1) * df_step + d_bandwidth;
    double fs_high_req = total_bw * 2.0;
    int k_up = static_cast<int>(std::ceil(fs_high_req / d_sample_rate));
    double fs_high = k_up * d_sample_rate;
    double range_step_high = SPEED_OF_LIGHT / (2.0 * fs_high);
    
    // 寻找最强峰值（避开直耦）
    int search_start = static_cast<int>(10.0 / range_step_high);  // 跳过前10米
    int max_idx = search_start;
    double max_val = 0.0;
    for (int i = search_start; i < n_fft_fast && i < static_cast<int>(hrrp.size()); i++) {
        double val = std::abs(hrrp[i]);
        if (val > max_val) {
            max_val = val;
            max_idx = i;
        }
    }
    
    double peak_range = max_idx * range_step_high;
    
    std::cout << "[ifft_range_profile] Peak at bin " << max_idx 
              << ", range ~" << peak_range << " m" << std::endl;
    std::cout << "[ifft_range_profile] Theoretical resolution: " 
              << SPEED_OF_LIGHT / (2.0 * total_bw) 
              << " m" << std::endl;

    // 发送到GUI
    if (d_main_gui && !d_main_gui->is_closed()) {
        // ===== 1. 发送1D距离像（峰值附近的局部切片）=====
        // 提取峰值附近的一段用于细节显示
        int window_size = 1024;  // 显示窗口大小
        int slice_start = std::max(0, max_idx - window_size / 2);
        int slice_end = std::min(n_fft_fast, max_idx + window_size / 2);
        int slice_len = slice_end - slice_start;
        
        RangeProfileUpdateEvent* event = new RangeProfileUpdateEvent();
        event->setNumSamples(slice_len);
        
        std::vector<double> range_axis(slice_len);
        std::vector<double> fine_profile(slice_len);
        for (int i = 0; i < slice_len; i++) {
            range_axis[i] = (slice_start + i) * range_step_high;
            fine_profile[i] = std::abs(hrrp[slice_start + i]);
        }
        
        event->setRangeProfile(range_axis, fine_profile);
        qApp->postEvent(d_main_gui, event);

        // ===== 2. 发送2D HRRP热力图数据（使用脉冲压缩结果）=====
        if (!pulse_compressed.empty()) {
            HRRP2DUpdateEvent* event_2d = new HRRP2DUpdateEvent();
            
            // 使用脉冲压缩结果显示频率 x 距离的2D图
            int n_freq_2d = pulse_compressed.size();
            int n_range_2d = pulse_compressed[0].size();
            
            std::vector<double> hrrp_2d_data(n_freq_2d * n_range_2d);
            
            // 转换为 dB (绝对幅值)
            for (int freq_idx = 0; freq_idx < n_freq_2d; freq_idx++) {
                for (int range_idx = 0; range_idx < n_range_2d; range_idx++) {
                    size_t src_idx = freq_idx * n_range_2d + range_idx;
                    double amp = std::abs(pulse_compressed[freq_idx][range_idx]);
                    double db_val = 20.0 * std::log10(amp + 1e-10);
                    hrrp_2d_data[src_idx] = db_val;
                }
            }
            
            // 距离轴：基于采样率
            std::vector<double> coarse_axis(n_range_2d);
            for (int i = 0; i < n_range_2d; i++) {
                coarse_axis[i] = i * range_step;
            }
            
            // 频率轴（索引）
            std::vector<double> fine_axis(n_freq_2d);
            for (int i = 0; i < n_freq_2d; i++) {
                fine_axis[i] = static_cast<double>(i);
            }
            
            event_2d->setHRRP2DData(hrrp_2d_data, coarse_axis, fine_axis, 
                                    n_range_2d, n_freq_2d);
            qApp->postEvent(d_main_gui, event_2d);
            
            std::cout << "[ifft_range_profile] Sent 2D data: " 
                      << n_range_2d << " x " << n_freq_2d << " (range x freq)" << std::endl;
        }

        // ===== 3. 发送拼接后的完整一维距离像（频域合成结果）=====
        FullRangeProfileUpdateEvent* event_full = new FullRangeProfileUpdateEvent();
        
        // 计算精细分辨率
        double fine_resolution = SPEED_OF_LIGHT / (2.0 * total_bw);
        
        // 频域合成后的hrrp就是完整的高分辨距离像
        std::vector<double> full_range_axis(n_fft_fast);
        std::vector<double> full_profile(n_fft_fast);
        
        for (int i = 0; i < n_fft_fast; i++) {
            full_range_axis[i] = i * range_step_high;
            double amp = std::abs(hrrp[i]);
            full_profile[i] = 20.0 * std::log10(amp + 1e-10);  // dB
        }
        
        event_full->setFullRangeProfile(full_range_axis, full_profile, fine_resolution);
        qApp->postEvent(d_main_gui, event_full);
        
        std::cout << "[ifft_range_profile] Sent full range profile: " 
                  << n_fft_fast << " points, resolution=" << fine_resolution << " m" << std::endl;
    }

    // 清空数据准备下一次扫频
    d_freq_data.clear();
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire error in process_sweep: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ifft_range_profile] Error in process_sweep: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[ifft_range_profile] Unknown error in process_sweep" << std::endl;
    }
}

} // namespace plasma
} // namespace gr




