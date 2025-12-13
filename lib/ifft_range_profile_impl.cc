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
                
                // 计算距离轴
                double coarse_res = SPEED_OF_LIGHT / (2.0 * d_bandwidth);
                std::vector<double> pc_range_axis(pc_result.size());
                std::vector<double> pc_profile(pc_result.size());
                
                for (size_t i = 0; i < pc_result.size(); i++) {
                    pc_range_axis[i] = i * coarse_res;
                    pc_profile[i] = std::abs(pc_result[i]);
                }
                
                pc_event->setPulseCompressionData(pc_range_axis, pc_profile, freq_copy);
                qApp->postEvent(d_main_gui, pc_event);
                
                std::cout << "[ifft_range_profile] Sent pulse compression result for freq " 
                          << freq_copy / 1e9 << " GHz to GUI" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ifft_range_profile] Error processing frequency " 
                      << freq_copy / 1e9 << " GHz: " << e.what() << std::endl;
        } catch (const af::exception& e) {
            std::cerr << "[ifft_range_profile] ArrayFire error processing frequency " 
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
                } catch (const std::exception& e) {
                    std::cerr << "[ifft_range_profile] Thread error: " << e.what() << std::endl;
                } catch (const af::exception& e) {
                    std::cerr << "[ifft_range_profile] ArrayFire thread error: " << e.what() << std::endl;
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
    
    // 截断或填充到 n_fft_fast 长度，以保持与原始 FFT 实现的一致性
    int result_len = pc_result.elements();
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

    // 执行IFFT合成
    std::vector<gr_complex> hrrp;
    perform_ifft_synthesis(pulse_compressed, hrrp);

    std::cout << "[ifft_range_profile] IFFT synthesis complete: " 
              << hrrp.size() << " samples" << std::endl;

    // 寻找最强的粗距离门
    int n_fft_fast = pulse_compressed[0].size();
    std::vector<double> coarse_profile(n_fft_fast);
    
    for (int i = 0; i < n_fft_fast; i++) {
        double max_val = 0.0;
        for (size_t j = 0; j < pulse_compressed.size(); j++) {
            double val = std::abs(pulse_compressed[j][i]);
            if (val > max_val) max_val = val;
        }
        coarse_profile[i] = max_val;
    }

    // 找到峰值位置
    int max_idx = 0;
    double max_val = 0.0;
    for (int i = 0; i < n_fft_fast; i++) {
        if (coarse_profile[i] > max_val) {
            max_val = coarse_profile[i];
            max_idx = i;
        }
    }

    // 提取该粗距离门的精细距离像
    std::vector<double> fine_profile(d_n_fft_synthesis);
    for (int i = 0; i < d_n_fft_synthesis; i++) {
        fine_profile[i] = std::abs(hrrp[max_idx * d_n_fft_synthesis + i]);
    }

    // 计算距离轴
    double coarse_res = SPEED_OF_LIGHT / (2.0 * d_bandwidth);
    double fine_window = SPEED_OF_LIGHT / (2.0 * std::abs(d_sweep_step));
    
    std::cout << "[ifft_range_profile] Peak at coarse bin " << max_idx 
              << ", range ~" << max_idx * coarse_res << " m" << std::endl;
    std::cout << "[ifft_range_profile] Theoretical resolution: " 
              << SPEED_OF_LIGHT / (2.0 * n_pulse_cpi * std::abs(d_sweep_step)) 
              << " m" << std::endl;

    // 发送到GUI
    if (d_main_gui && !d_main_gui->is_closed()) {
        RangeProfileUpdateEvent* event = new RangeProfileUpdateEvent();
        event->setNumSamples(d_n_fft_synthesis);
        
        // 精细距离轴
        std::vector<double> range_axis(d_n_fft_synthesis);
        double base_range = max_idx * coarse_res;
        for (int i = 0; i < d_n_fft_synthesis; i++) {
            range_axis[i] = base_range + (i * fine_window / d_n_fft_synthesis);
        }
        
        event->setRangeProfile(range_axis, fine_profile);
        qApp->postEvent(d_main_gui, event);
    }

    // 清空数据准备下一次扫频
    d_freq_data.clear();
    } catch (const std::exception& e) {
        std::cerr << "[ifft_range_profile] Error in process_sweep: " << e.what() << std::endl;
    } catch (const af::exception& e) {
        std::cerr << "[ifft_range_profile] ArrayFire error in process_sweep: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[ifft_range_profile] Unknown error in process_sweep" << std::endl;
    }
}

} // namespace plasma
} // namespace gr




