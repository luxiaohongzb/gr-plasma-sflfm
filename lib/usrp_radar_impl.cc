/* -*- c++ -*- */
/*
 * Copyright 2022 gr-plasma author.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "usrp_radar_impl.h"
#include <gnuradio/io_signature.h>

namespace gr {
namespace plasma {

usrp_radar::sptr usrp_radar::make(const std::string& args,
                                  const double tx_rate,
                                  const double rx_rate,
                                  const double tx_freq,
                                  const double rx_freq,
                                  const double tx_gain,
                                  const double rx_gain,
                                  const double start_delay,
                                  const bool elevate_priority,
                                  const std::string& cal_file,
                                  const bool verbose,
                                  const double hop_start_freq,
                                  const double hop_end_freq,
                                  const double hop_step,
                                  const double lo_stabilize_time,
                                  const std::string& clock_source,
                                  const std::string& time_source,
                                  const std::string& freq_mode)
{
    return gnuradio::make_block_sptr<usrp_radar_impl>(args,
                                                      tx_rate,
                                                      rx_rate,
                                                      tx_freq,
                                                      rx_freq,
                                                      tx_gain,
                                                      rx_gain,
                                                      start_delay,
                                                      elevate_priority,
                                                      cal_file,
                                                      verbose,
                                                      hop_start_freq,
                                                      hop_end_freq,
                                                      hop_step,
                                                      lo_stabilize_time,
                                                      clock_source,
                                                      time_source,
                                                      freq_mode);
}

usrp_radar_impl::usrp_radar_impl(const std::string& args,
                                 const double tx_rate,
                                 const double rx_rate,
                                 const double tx_freq,
                                 const double rx_freq,
                                 const double tx_gain,
                                 const double rx_gain,
                                 const double start_delay,
                                 const bool elevate_priority,
                                 const std::string& cal_file,
                                 const bool verbose,
                                 const double hop_start_freq,
                                 const double hop_end_freq,
                                 const double hop_step,
                                 const double lo_stabilize_time,
                                 const std::string& clock_source,
                                 const std::string& time_source,
                                 const std::string& freq_mode)
    : gr::block(
          "usrp_radar", gr::io_signature::make(0, 0, 0), gr::io_signature::make(0, 0, 0)),
      usrp_args(args),
      tx_rate(tx_rate),
      rx_rate(rx_rate),
      tx_freq(tx_freq),
      rx_freq(rx_freq),
      tx_gain(tx_gain),
      rx_gain(rx_gain),
      start_delay(start_delay),
      elevate_priority(elevate_priority),
      cal_file(cal_file),
      verbose(verbose),
      hop_start_freq(hop_start_freq),
      hop_end_freq(hop_end_freq),
      hop_step(hop_step),
      lo_stabilize_time(lo_stabilize_time),
      clock_source(clock_source),
      time_source(time_source),
      freq_mode(freq_mode)
{
    // Additional parameters. I have the hooks in to make them configurable, but we don't
    // need them right now.
    this->tx_subdev = this->rx_subdev = "";
    this->tx_cpu_format = this->rx_cpu_format = "fc32";
    this->tx_otw_format = this->rx_otw_format = "sc16";
    this->tx_device_addr = this->rx_device_addr = "";
    this->tx_channel_nums = std::vector<size_t>(1, 0);
    this->rx_channel_nums = std::vector<size_t>(1, 0);
    this->tx_buffs = std::vector<const void*>(tx_channel_nums.size(), nullptr);

    this->n_tx_total = 0;
    this->new_msg_received = false;
    this->next_meta = pmt::make_dict();
    this->paused = true;
    this->resume_time = 0.0;
    this->tx_burst_seq = 0;
    this->tx_burst_seq_sent = 0;
    this->tx_inflight = false;

    config_usrp(this->usrp,
                this->usrp_args,
                this->tx_rate,
                this->rx_rate,
                this->tx_freq,
                this->rx_freq,
                this->tx_gain,
                this->rx_gain,
                this->tx_subdev,
                this->rx_subdev,
                this->verbose,
                this->clock_source,
                this->time_source);

    n_delay = 0;
    if (not cal_file.empty()) {
        read_calibration_file(cal_file);
    }

    message_port_register_in(PMT_IN);
    message_port_register_out(PMT_OUT);
    set_msg_handler(PMT_IN, [this](pmt::pmt_t msg) { this->handle_message(msg); });
}

usrp_radar_impl::~usrp_radar_impl() {}


bool usrp_radar_impl::start()
{
    finished = false;
    if (verbose) {
        double now = usrp ? usrp->get_time_now().get_real_secs() : 0.0;
        std::cout << boost::format("[usrp_radar] start() at %.6f") % now << std::endl;
    }
    d_main_thread = gr::thread::thread([this] { run(); });
    return block::start();
}

bool usrp_radar_impl::stop()
{
    finished = true;
    return block::stop();
}

void usrp_radar_impl::handle_message(const pmt::pmt_t& msg)
{
    if (pmt::is_pdu(msg)) {
        next_meta = pmt::dict_update(next_meta, pmt::car(msg));
        tx_data = pmt::cdr(msg);
        tx_buff_size = pmt::length(tx_data);

        new_msg_received = true;
        if (verbose) {
            double now = usrp->get_time_now().get_real_secs();
            std::cout << boost::format("[usrp_radar] PDU received: %lu samples at %.6f") % tx_buff_size % now << std::endl;
        }
    }
}

void usrp_radar_impl::run()
{
    

    std::atomic<bool>& finished = this->finished;
    if (this->elevate_priority) {
        uhd::set_thread_priority_safe();
    }

    /***********************************************************************
     * Receive thread
     **********************************************************************/
    double start_time = usrp->get_time_now().get_real_secs() + start_delay;
    bool rx_stream_now = (start_delay == 0.0 && (rx_channel_nums.size() == 1));
    // create a receive streamer
    uhd::stream_args_t rx_stream_args(rx_cpu_format, rx_otw_format);
    rx_stream_args.channels = rx_channel_nums;
    rx_stream_args.args = uhd::device_addr_t(rx_device_addr);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(rx_stream_args);
    auto rx_thread = d_tx_rx_thread_group.create_thread([=, &finished]() {
        receive(usrp, rx_stream, finished, elevate_priority, start_time, rx_stream_now);
    });
    uhd::set_thread_name(rx_thread, "rx_stream");
    if (verbose) {
        std::cout << boost::format("[usrp_radar RX] thread created, start_time=%.6f stream_now=%d") % start_time % (int)rx_stream_now << std::endl;
    }
    /***********************************************************************
     * Transmit thread
     **********************************************************************/
    bool tx_has_time_spec = (start_delay != 0.0);
    uhd::stream_args_t tx_stream_args(tx_cpu_format, tx_otw_format);
    tx_stream_args.channels = tx_channel_nums;
    tx_stream_args.args = uhd::device_addr_t(tx_device_addr);
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(tx_stream_args);
    auto tx_thread = d_tx_rx_thread_group.create_thread([=, &finished]() {
        transmit(
            usrp, tx_stream, finished, elevate_priority, start_time, tx_has_time_spec);
    });
    uhd::set_thread_name(tx_thread, "tx_stream");
    if (verbose) {
        std::cout << boost::format("[usrp_radar TX] thread created, start_time=%.6f has_time_spec=%d") % start_time % (int)tx_has_time_spec << std::endl;
    }

    if (freq_mode == "single") {
        // Single frequency mode: set frequency once and continuously transmit
        double now_dev = usrp->get_time_now().get_real_secs();
        double t_cmd = now_dev + 0.02;
        usrp->set_command_time(uhd::time_spec_t(t_cmd));
        usrp->set_tx_freq(tx_freq);
        usrp->set_rx_freq(rx_freq);
        usrp->clear_command_time();
        if (verbose) {
            double actual_tx = usrp->get_tx_freq();
            double actual_rx = usrp->get_rx_freq();
            std::cout << boost::format("[usrp_radar] Single frequency mode: TX=%.3f MHz; RX=%.3f MHz; actual TX LO=%.3f MHz; RX LO=%.3f MHz") 
                      % (tx_freq / 1e6) % (rx_freq / 1e6) % (actual_tx / 1e6) % (actual_rx / 1e6) << std::endl;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(lo_stabilize_time);
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        resume_time = usrp->get_time_now().get_real_secs() + 0.05;
        tx_inflight = true;
        paused = false;
        tx_burst_seq.fetch_add(1);
        if (verbose) {
            std::cout << boost::format("[usrp_radar] resume_time=%.6f seq=%lu") % resume_time % tx_burst_seq.load() << std::endl;
        }
        // In single frequency mode, continuously transmit without frequency hopping
        // Add stabilization delay between bursts to limit transmission rate
        while (!finished) {
            {
                std::unique_lock<std::mutex> lk(d_mutex);
                d_tx_done.wait_for(lk, std::chrono::milliseconds(500), [this]{ return !tx_inflight.load(); });
            }
            if (!finished) {
                // Wait for stabilization time before next burst (similar to hopping mode)
                paused = true;
                auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(lo_stabilize_time);
                while (std::chrono::steady_clock::now() < deadline) {
                    if (finished) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (!finished) {
                    resume_time = usrp->get_time_now().get_real_secs() + 0.05;
                    tx_inflight = true;
                    paused = false;
                    tx_burst_seq.fetch_add(1);
                    if (verbose) {
                        std::cout << boost::format("[usrp_radar] Single freq mode: resume_time=%.6f seq=%lu") % resume_time % tx_burst_seq.load() << std::endl;
                    }
                }
            }
        }
    } else {
        // Frequency hopping mode (original behavior)
        std::vector<double> freqs;
        if (hop_step > 0) {
            for (double f = hop_start_freq; f <= hop_end_freq + 1e-9; f += hop_step) freqs.push_back(f);
        } else if (hop_step < 0) {
            for (double f = hop_start_freq; f >= hop_end_freq - 1e-9; f += hop_step) freqs.push_back(f);
        } else {
            freqs.push_back(tx_freq);
        }

        if (freqs.size() > 1) {
            // Cycle through frequencies repeatedly until finished is set
            while (!finished) {
                for (double f : freqs) {
                    if (finished) break;
                    double now_dev = usrp->get_time_now().get_real_secs();
                    double t_cmd = now_dev + 0.02;
                    usrp->set_command_time(uhd::time_spec_t(t_cmd));
                    tx_freq = f;
                    rx_freq = f;
                    usrp->set_tx_freq(tx_freq);
                    usrp->set_rx_freq(rx_freq);
                    usrp->clear_command_time();
                    if (verbose) {
                        double actual_tx = usrp->get_tx_freq();
                        double actual_rx = usrp->get_rx_freq();
                        std::cout << boost::format("[usrp_radar] Frequency hop: requested=%.3f MHz; actual TX LO=%.3f MHz; RX LO=%.3f MHz") % (f / 1e6) % (actual_tx / 1e6) % (actual_rx / 1e6) << std::endl;
                    }
                    auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(lo_stabilize_time);
                    while (std::chrono::steady_clock::now() < deadline) {
                        bool tx_ok = false, rx_ok = false;
                        // try { tx_ok = usrp->get_tx_sensor("lo_locked").to_bool(); } catch (...) {}
                        // try { rx_ok = usrp->get_rx_sensor("lo_locked").to_bool(); } catch (...) {}
                        // if (tx_ok && rx_ok) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    resume_time = usrp->get_time_now().get_real_secs() + 0.05;
                    tx_inflight = true;
                    paused = false;
                    tx_burst_seq.fetch_add(1);
                    if (verbose) {
                        std::cout << boost::format("[usrp_radar] resume_time=%.6f seq=%lu") % resume_time % tx_burst_seq.load() << std::endl;
                    }
                    {
                        std::unique_lock<std::mutex> lk(d_mutex);
                        d_tx_done.wait_for(lk, std::chrono::milliseconds(500), [this]{ return !tx_inflight.load(); });
                    }
                    paused = true;
                }
            }
        } else {
            // Single-pass (no hopping) behavior
            for (double f : freqs) {
                if (finished) break;
                double now_dev = usrp->get_time_now().get_real_secs();
                double t_cmd = now_dev + 0.02;
                usrp->set_command_time(uhd::time_spec_t(t_cmd));
                tx_freq = f;
                rx_freq = f;
                usrp->set_tx_freq(tx_freq);
                usrp->set_rx_freq(rx_freq);
                usrp->clear_command_time();
                if (verbose) {
                    double actual_tx = usrp->get_tx_freq();
                    double actual_rx = usrp->get_rx_freq();
                    std::cout << boost::format("[usrp_radar] Frequency hop: requested=%.3f MHz; actual TX LO=%.3f MHz; RX LO=%.3f MHz") % (f / 1e6) % (actual_tx / 1e6) % (actual_rx / 1e6) << std::endl;
                }
                auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(lo_stabilize_time);
                while (std::chrono::steady_clock::now() < deadline) {
                    bool tx_ok = false, rx_ok = false;
                    // try { tx_ok = usrp->get_tx_sensor("lo_locked").to_bool(); } catch (...) {}
                    // try { rx_ok = usrp->get_rx_sensor("lo_locked").to_bool(); } catch (...) {}
                    // if (tx_ok && rx_ok) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                resume_time = usrp->get_time_now().get_real_secs() + 0.05;
                tx_inflight = true;
                paused = false;
                tx_burst_seq.fetch_add(1);
                {
                    std::unique_lock<std::mutex> lk(d_mutex);
                    d_tx_done.wait_for(lk, std::chrono::milliseconds(500), [this]{ return !tx_inflight.load(); });
                }
                paused = true;
            }
        }
    }

    // Mark finished when we exit the hopping loop
    finished = true;
    d_tx_rx_thread_group.join_all();
}

void usrp_radar_impl::config_usrp(uhd::usrp::multi_usrp::sptr& usrp,
                                  const std::string& args,
                                  const double tx_rate,
                                  const double rx_rate,
                                  const double tx_freq,
                                  const double rx_freq,
                                  const double tx_gain,
                                  const double rx_gain,
                                  const std::string& tx_subdev,
                                  const std::string& rx_subdev,
                                  bool verbose,
                                  const std::string& clock_source,
                                  const std::string& time_source)
{
    usrp = uhd::usrp::multi_usrp::make(args);
    
    // 设置参考时钟源和时间源（使用主板索引 0）
    try {
        if (not clock_source.empty() && clock_source != "default") {
            usrp->set_clock_source(clock_source, 0);
            if (verbose) {
                std::cout << boost::format("[usrp_radar] Clock source set to: %s") % clock_source << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << boost::format("[usrp_radar] Warning: Failed to set clock source '%s': %s") 
                     % clock_source % e.what() << std::endl;
    }
    
    try {
        if (not time_source.empty() && time_source != "default") {
            usrp->set_time_source(time_source, 0);
            if (verbose) {
                std::cout << boost::format("[usrp_radar] Time source set to: %s") % time_source << std::endl;
            }
            
            // If using GPSDO, wait for GPS lock and ensure time synchronization
            if (time_source == "gpsdo") {
                if (verbose) {
                    std::cout << "[usrp_radar] Waiting for GPSDO lock..." << std::endl;
                }
                
                // Wait for GPS lock (check GPS sensor)
                bool gps_locked = false;
                int lock_attempts = 0;
                const int max_lock_attempts = 100; // 10 seconds max wait
                while (!gps_locked && lock_attempts < max_lock_attempts) {
                    try {
                        uhd::sensor_value_t gps_locked_val = usrp->get_mboard_sensor("gps_locked", 0);
                        gps_locked = gps_locked_val.to_bool();
                        if (!gps_locked) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            lock_attempts++;
                        }
                    } catch (const std::exception& e) {
                        // Sensor might not be available, try alternative check
                        try {
                            uhd::sensor_value_t gps_time_val = usrp->get_mboard_sensor("gps_time", 0);
                            gps_locked = true; // If we can read GPS time, assume locked
                        } catch (...) {
                            // If sensors are not available, wait a bit and assume GPSDO is ready
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            gps_locked = true;
                            break;
                        }
                    }
                }
                
                if (gps_locked) {
                    // GPSDO automatically syncs time via PPS, but we need to wait for next PPS edge
                    // Wait for next PPS to ensure time is synchronized (wait slightly more than 1 second)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
                    
                    // Get current GPSDO time
                    double gps_time = usrp->get_time_now().get_real_secs();
                    
                    if (verbose) {
                        try {
                            uhd::sensor_value_t gps_time_val = usrp->get_mboard_sensor("gps_time", 0);
                            std::cout << boost::format("[usrp_radar] GPSDO locked. GPS time: %d, USRP time: %.6f") 
                                         % gps_time_val.to_int() % gps_time << std::endl;
                        } catch (...) {
                            std::cout << boost::format("[usrp_radar] GPSDO locked. USRP time: %.6f") 
                                         % gps_time << std::endl;
                        }
                    }
                } else {
                    std::cerr << "[usrp_radar] Warning: GPSDO lock not achieved after timeout. Continuing anyway..." << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << boost::format("[usrp_radar] Warning: Failed to set time source '%s': %s") 
                     % time_source % e.what() << std::endl;
    }
    
    if (not tx_subdev.empty()) {
        usrp->set_tx_subdev_spec(tx_subdev);
    }
    if (not rx_subdev.empty()) {
        usrp->set_rx_subdev_spec(rx_subdev);
    }
    usrp->set_tx_rate(tx_rate);
    usrp->set_rx_rate(rx_rate);
    usrp->set_tx_freq(tx_freq);
    usrp->set_rx_freq(rx_freq);
    usrp->set_tx_gain(tx_gain);
    usrp->set_rx_gain(rx_gain);

    if (verbose) {
        std::cout << boost::format("Using Device: %s") % usrp->get_pp_string()
                  << std::endl;
        try {
            std::cout << boost::format("Clock Source: %s") % usrp->get_clock_source(0) << std::endl;
        } catch (...) {}
        try {
            std::cout << boost::format("Time Source: %s") % usrp->get_time_source(0) << std::endl;
        } catch (...) {}
        std::cout << boost::format("Actual TX Rate: %f Msps") %
                         (usrp->get_tx_rate() / 1e6)
                  << std::endl;
        std::cout << boost::format("Actual RX Rate: %f Msps") %
                         (usrp->get_rx_rate() / 1e6)
                  << std::endl;
        std::cout << boost::format("Actual TX Freq: %f MHz") % (usrp->get_tx_freq() / 1e6)
                  << std::endl;
        std::cout << boost::format("Actual RX Freq: %f MHz") % (usrp->get_rx_freq() / 1e6)
                  << std::endl;
        std::cout << boost::format("Actual TX Gain: %f dB") % usrp->get_tx_gain()
                  << std::endl;
        std::cout << boost::format("Actual RX Gain: %f dB") % usrp->get_rx_gain()
                  << std::endl;
    }
}

void usrp_radar_impl::receive(uhd::usrp::multi_usrp::sptr usrp,
                              uhd::rx_streamer::sptr rx_stream,
                              std::atomic<bool>& finished,
                              bool elevate_priority,
                              double start_time,
                              bool rx_stream_now)
{
    if (elevate_priority) {
        uhd::set_thread_priority_safe(1, true);
    }

    // setup variables
    uhd::rx_metadata_t md;
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.time_spec = uhd::time_spec_t(start_time);
    cmd.stream_now = rx_stream_now;
    rx_stream->issue_stream_cmd(cmd);
    if (verbose) {
        std::cout << boost::format("[usrp_radar RX] issue START_CONTINUOUS at %.6f stream_now=%d") % start_time % (int)rx_stream_now << std::endl;
    }

    // Set up and allocate buffers
    pmt::pmt_t rx_data_pmt = pmt::make_c32vector(tx_buff_size, 0);
    gr_complex* rx_data_ptr = pmt::c32vector_writable_elements(rx_data_pmt, tx_buff_size);


    double time_until_start = start_time - usrp->get_time_now().get_real_secs();
    double recv_timeout = 0.1 + time_until_start;
    bool stop_called = false;

    // TODO: Handle multiple channels (e.g., one out port per channel)
    while (true) {
        if (finished and not stop_called) {
            rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            stop_called = true;
        }
        if (this->paused) {
            if (!stop_called) {
                rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
                stop_called = true;
                if (verbose) {
                    std::cout << "[usrp_radar RX] STOP_CONTINUOUS due to paused" << std::endl;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        } else if (stop_called) {
            uhd::stream_cmd_t restart_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
            restart_cmd.num_samps = tx_buff_size;
            restart_cmd.time_spec = uhd::time_spec_t(this->resume_time);
            restart_cmd.stream_now = false;
            rx_stream->issue_stream_cmd(restart_cmd);
            if (verbose) {
                double now = usrp->get_time_now().get_real_secs();
                std::cout << boost::format("[usrp_radar RX] RESTART NUM_SAMPS_AND_DONE num=%lu resume=%.6f now=%.6f stream_now=%d") % tx_buff_size % this->resume_time % now % (int)restart_cmd.stream_now << std::endl;
            }
            stop_called = false;
        }
        try {
            if (n_delay > 0) {
                std::vector<gr_complex> dummy_vec(n_delay);
                size_t n_rx =
                    rx_stream->recv(dummy_vec.data(), n_delay, md, recv_timeout);
                n_delay -= n_rx;
            }
            size_t total_rx = 0;
            while (total_rx < tx_buff_size) {
                size_t n_rx = rx_stream->recv(
                    rx_data_ptr + total_rx, tx_buff_size - total_rx, md, recv_timeout);
                if (n_rx == 0) {
                    continue;
                }
                total_rx += n_rx;
                recv_timeout = 0.1; 
            }
            if (verbose) {
                double now = usrp->get_time_now().get_real_secs();
                double rx_lo = usrp->get_rx_freq();
                double tx_lo = usrp->get_tx_freq();
                std::cout << boost::format("[usrp_radar RX] time=%.3f s; RX LO=%.3f MHz; TX LO=%.3f MHz; n_samples=%lu") % now % (rx_lo / 1e6) % (tx_lo / 1e6) % total_rx << std::endl;
            }
            pmt::pmt_t meta = this->next_meta;
            // Always add the RX frequency metadata (use device's reported RX LO)
            try {
                double dev_rx = usrp->get_rx_freq();
                meta = pmt::dict_add(meta, pmt::intern(rx_freq_key), pmt::from_double(dev_rx));
            } catch (...) {
                // fallback to configured rx_freq if device query fails
                meta = pmt::dict_add(meta, pmt::intern(rx_freq_key), pmt::from_double(rx_freq));
            }
            if (pmt::length(meta) > 0) {
                this->next_meta = pmt::make_dict();
            }
            message_port_pub(PMT_OUT, pmt::cons(meta, rx_data_pmt));
        } catch (uhd::io_error& e) {
            std::cerr << "Caught an IO exception. " << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }

        // Handle errors
        switch (md.error_code) {
        case uhd::rx_metadata_t::ERROR_CODE_NONE:
            if ((finished or stop_called) and md.end_of_burst) {
                return;
            }
            break;
        default:
            break;
        }
    }
}

void usrp_radar_impl::transmit(uhd::usrp::multi_usrp::sptr usrp,
                               uhd::tx_streamer::sptr tx_stream,
                               std::atomic<bool>& finished,
                               bool elevate_priority,
                               double start_time,
                               double has_time_spec)
{
    if (elevate_priority) {
        uhd::set_thread_priority_safe(1, true);
    }

    // Create the metadata, and populate the time spec at the latest possible moment
    uhd::tx_metadata_t md;
    md.has_time_spec = has_time_spec;
    md.time_spec = uhd::time_spec_t(start_time);

    double timeout = 0.1 + start_time;
    bool eob_sent = false;
    while (not finished) {
        if (this->paused) {
            if (!eob_sent) {
                md.end_of_burst = true;
                tx_stream->send("", 0, md);
                md.end_of_burst = false;
                eob_sent = true;
                if (verbose) {
                    std::cout << "[usrp_radar TX] EOB sent while paused" << std::endl;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (tx_burst_seq > tx_burst_seq_sent) {
            if (new_msg_received) {
                tx_buffs[0] = pmt::c32vector_writable_elements(tx_data, tx_buff_size);
                next_meta = pmt::dict_add(
                    next_meta, pmt::intern(tx_freq_key), pmt::from_double(tx_freq));
                next_meta = pmt::dict_add(
                    next_meta, pmt::intern(sample_start_key), pmt::from_long(n_tx_total));
                new_msg_received = false;
                if (verbose) {
                    std::cout << boost::format("[usrp_radar TX] payload prepared: %lu samples") % tx_buff_size << std::endl;
                }
            }
            if (tx_buffs[0] == nullptr) {
                if (verbose) {
                    std::cout << "[usrp_radar TX] no payload, skip burst" << std::endl;
                }
                tx_burst_seq_sent = tx_burst_seq.load();
                tx_inflight = false;
                {
                    std::lock_guard<std::mutex> lk(d_mutex);
                }
                d_tx_done.notify_all();
                continue;
            }
            md.has_time_spec = true;
            md.time_spec = uhd::time_spec_t(this->resume_time);
            n_tx_total += tx_stream->send(tx_buffs, tx_buff_size, md, timeout) *
                          tx_stream->get_num_channels();
            md.has_time_spec = false;
            timeout = 0.1;
            tx_burst_seq_sent = tx_burst_seq.load();
            tx_inflight = false;
            {
                std::lock_guard<std::mutex> lk(d_mutex);
            }
            d_tx_done.notify_all();
            eob_sent = false;
            if (verbose) {
                double now = usrp->get_time_now().get_real_secs();
                double tx_lo = usrp->get_tx_freq();
                double rx_lo = usrp->get_rx_freq();
                std::cout << boost::format("[usrp_radar TX] time=%.3f s; TX LO=%.3f MHz; RX LO=%.3f MHz; n_tx_total=%lu") % now % (tx_lo / 1e6) % (rx_lo / 1e6) % n_tx_total << std::endl;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    md.end_of_burst = true;
    tx_stream->send("", 0, md);
}

void usrp_radar_impl::read_calibration_file(const std::string& filename)
{
    std::ifstream file(filename);
    nlohmann::json json;
    if (file) {
        file >> json;
        std::string radio_type = usrp->get_mboard_name();
        for (auto& config : json[radio_type]) {
            if (config["samp_rate"] == usrp->get_tx_rate() and
                config["master_clock_rate"] == usrp->get_master_clock_rate()) {
                n_delay = config["delay"];
                break;
            }
        }
        if (n_delay == 0)
            UHD_LOG_INFO("USRP Radar",
                         "Calibration file found, but no data exists for this "
                         "combination of radio, master clock rate, and sample rate");
    } else {
        UHD_LOG_INFO("USRP Radar", "No calibration file found");
    }

    file.close();
}

void usrp_radar_impl::set_metadata_keys(const std::string& tx_freq_key,
                                        const std::string& rx_freq_key,
                                        const std::string& sample_start_key)
{
    this->tx_freq_key = tx_freq_key;
    this->rx_freq_key = rx_freq_key;
    this->sample_start_key = sample_start_key;
}

} /* namespace plasma */
} /* namespace gr */