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
      d_processing(false)
{
    // 注册消息端口
    d_in_port = pmt::intern("in");
    message_port_register_in(d_in_port);
    set_msg_handler(d_in_port,
                   [this](pmt::pmt_t msg) { handle_rx_msg(msg); });

    // 计算预期频率列表
    compute_expected_freqs();

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
    return block::start();
}

bool ifft_range_profile_impl::stop()
{
    d_finished = true;
    // 等待处理完成
    while (d_processing.load()) {
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
                process_sweep();
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
    // 构造参考LFM信号
    int n_samp_pulse = static_cast<int>(d_pulse_width * d_sample_rate);
    std::vector<gr_complex> lfm_ref(n_samp_pulse);
    
    double sweep_start_freq = -d_bandwidth / 2.0;
    double chirp_rate = d_bandwidth / (2.0 * d_pulse_width);
    
    for (int i = 0; i < n_samp_pulse; i++) {
        double t = i / d_sample_rate;
        double phase = 2.0 * M_PI * (sweep_start_freq * t + chirp_rate * t * t);
        lfm_ref[i] = gr_complex(std::cos(phase), std::sin(phase));
    }

    // 计算FFT大小
    int n_fft_fast = 1;
    while (n_fft_fast < (n_pri + n_samp_pulse)) {
        n_fft_fast *= 2;
    }

    // 创建FFT对象
    fft::fft_complex_fwd fft_forward(n_fft_fast);
    fft::fft_complex_rev fft_reverse(n_fft_fast);

    // FFT参考信号
    std::memset(fft_forward.get_inbuf(), 0, n_fft_fast * sizeof(gr_complex));
    std::memcpy(fft_forward.get_inbuf(), lfm_ref.data(), n_samp_pulse * sizeof(gr_complex));
    fft_forward.execute();
    
    std::vector<gr_complex> ref_spec(n_fft_fast);
    std::memcpy(ref_spec.data(), fft_forward.get_outbuf(), n_fft_fast * sizeof(gr_complex));

    // FFT输入信号
    std::memset(fft_forward.get_inbuf(), 0, n_fft_fast * sizeof(gr_complex));
    std::memcpy(fft_forward.get_inbuf(), input.data(), 
                std::min(n_pri, (int)input.size()) * sizeof(gr_complex));
    fft_forward.execute();

    // 匹配滤波：乘以参考信号的共轭
    for (int i = 0; i < n_fft_fast; i++) {
        fft_reverse.get_inbuf()[i] = fft_forward.get_outbuf()[i] * std::conj(ref_spec[i]);
    }

    // IFFT
    fft_reverse.execute();
    
    output.resize(n_fft_fast);
    std::memcpy(output.data(), fft_reverse.get_outbuf(), n_fft_fast * sizeof(gr_complex));
}

void ifft_range_profile_impl::perform_ifft_synthesis(
    const std::vector<std::vector<gr_complex>>& pc_data,
    std::vector<gr_complex>& hrrp)
{
    if (pc_data.empty()) {
        return;
    }

    int n_fast = pc_data[0].size();
    int n_slow = pc_data.size();

    // 创建IFFT对象
    fft::fft_complex_rev ifft_synthesis(d_n_fft_synthesis);

    // 对每个快时间单元进行IFFT合成
    hrrp.resize(n_fast * d_n_fft_synthesis);

    for (int i = 0; i < n_fast; i++) {
        // 准备IFFT输入
        std::memset(ifft_synthesis.get_inbuf(), 0, d_n_fft_synthesis * sizeof(gr_complex));
        for (int j = 0; j < std::min(n_slow, d_n_fft_synthesis); j++) {
            ifft_synthesis.get_inbuf()[j] = pc_data[j][i];
        }

        // 执行IFFT
        ifft_synthesis.execute();

        // 保存结果
        for (int j = 0; j < d_n_fft_synthesis; j++) {
            hrrp[i * d_n_fft_synthesis + j] = ifft_synthesis.get_outbuf()[j];
        }
    }
}

void ifft_range_profile_impl::process_sweep()
{
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
}

} // namespace plasma
} // namespace gr




