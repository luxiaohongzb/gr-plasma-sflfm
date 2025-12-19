// /* -*- c++ -*- */
// /*
//  * Copyright 2024 yantob.
//  *
//  * SPDX-License-Identifier: GPL-3.0-or-later
//  */

//  #include <gnuradio/io_signature.h>
//  #include "rfnoc_radar_impl.h"
//  #include "gpio.h"
 
//  namespace gr {
//    namespace plasma {
 
//      // #pragma message("set the following appropriately and remove this warning")
//      // using input_type = float;
//      // #pragma message("set the following appropriately and remove this warning")
//      // using output_type = float;
//      rfnoc_radar::sptr
//      rfnoc_radar::make(const std::string& rfnoc_args,
//                        const std::string& device_addr,
//                        const std::string& ref,
//                        const double tx_rate,
//                        const double rx_rate,
//                        const double tx_freq,
//                        const double rx_freq,
//                        const double tx_gain,
//                        const double rx_gain,
//                        const double start_delay,
//                        const bool elevate_priority,
//                        const std::string& cal_file,
//                        const bool verbose)
//      {
//        return gnuradio::make_block_sptr<rfnoc_radar_impl>(rfnoc_args,
//                                                           device_addr,
//                                                           ref,
//                                                           tx_rate,
//                                                           rx_rate,
//                                                           tx_freq,
//                                                           rx_freq,
//                                                           tx_gain,
//                                                           rx_gain,
//                                                           start_delay,
//                                                           elevate_priority,
//                                                           cal_file,
//                                                           verbose
//          );
//      }
 
//      static volatile bool stop_signal_called = false;
 
//      // Ctrl+C handler
//     //  void sign_int_handler(int)
//     //  {
//     //      stop_signal_called = true;
//     //  }
 
//      /*
//       * The private constructor
//       */
//      rfnoc_radar_impl::rfnoc_radar_impl(const std::string& rfnoc_args,
//                                         const std::string& device_addr,
//                                         const std::string& ref,
//                                         const double tx_rate,
//                                         const double rx_rate,
//                                         const double tx_freq,
//                                         const double rx_freq,
//                                         const double tx_gain,
//                                         const double rx_gain,
//                                         const double start_delay,
//                                         const bool elevate_priority,
//                                         const std::string& cal_file,
//                                         const bool verbose)
//        : gr::block("rfnoc_radar",
//                gr::io_signature::make(0 /* min inputs */, 0 /* max inputs */, 0),
//                gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */, 0)),
//                rfnoc_args(rfnoc_args),
//                device_addr(device_addr),
//                ref(ref),
//                tx_rate(tx_rate),
//                rx_rate(rx_rate),
//                tx_freq(tx_freq),
//                rx_freq(rx_freq),
//                tx_gain(tx_gain),
//                rx_gain(rx_gain),
//                start_delay(start_delay),
//                elevate_priority(elevate_priority),
//                cal_file(cal_file),
//                verbose(verbose)
//      {
//          // Initialize member variables
//          graph = nullptr;
//          tx_stream = nullptr;
//          rx_stream = nullptr;
//          replay_ctrl = nullptr;
//          usrp_rx = nullptr;
//          finished = false;
//          msg_received = false;
//          next_meta = pmt::make_dict();
//          tx_data = pmt::PMT_NIL;
//          tx_buff_size = 0;

//          tx_cpu_format = rx_cpu_format = "fc32";
//          tx_otw_format = rx_otw_format = "sc16";
 
//          // Now configure RFNoC
 
//          config_rfnoc(graph,
//                       this->rfnoc_args,
//                       this->tx_args,
//                       this->rx_args,
//                       this->ref,
//                       this->cpi,
//                       this->lfm_width,
//                       this->prf,
//                       this->bandwidth,
//                       this->sample_rate,
//                       this->tx_freq,
//                       this->tx_gain,
//                       this->rx_freq,
//                       this->rx_gain);

//         // if (not cal_file.empty()) {
//         //     read_calibration_file(cal_file);
//         // }
 
//          message_port_register_in(pmt::mp("in"));
//          message_port_register_out(pmt::mp("out"));
//          set_msg_handler(pmt::mp("in"), [this](pmt::pmt_t msg) { this->handle_message(msg); });
//      }
 
//      /*
//       * Our virtual destructor.
//       */
//      rfnoc_radar_impl::~rfnoc_radar_impl() {}
 
//      void rfnoc_radar_impl::handle_message(const pmt::pmt_t& msg)
//      {
//          if (pmt::is_pdu(msg)) {
//              next_meta = pmt::dict_update(next_meta, pmt::car(msg));
//              tx_data = pmt::cdr(msg);
//              tx_buff_size = pmt::length(tx_data);
 
//              msg_received = true;
//          }
//      }
 
//      bool rfnoc_radar_impl::start()
//      {
//          finished = false;
//          d_main_thread = gr::thread::thread([this] { run(); });
 
//          return block::start();
//      }
 
//      bool rfnoc_radar_impl::stop()
//      {
//          finished = true;
//          if (d_main_thread.joinable()) {
//              d_main_thread.join();
//          }
//          return block::stop();
//      }
 
//      void rfnoc_radar_impl::run() 
//      {
//          while (not msg_received) std::this_thread::sleep_for(std::chrono::microseconds(10));
 
//          std::atomic<bool>& finished = this->finished;
//          if (this->elevate_priority) {
//              uhd::set_thread_priority_safe();
//          }
//          std::atomic<bool>& rx_finish = this->rx_finish;
//          std::atomic<bool>& rx_en= this->rx_en;
//          std::atomic<bool>& tx_finish = this->tx_finish;
//          std::atomic<bool>& tx_en= this->tx_en;
 
//          rx_finish = true;
//          tx_finish = true;
//          tx_en = false;
//          rx_en = false;
 
//          /***********************************************************************
//           * Receive thread
//           **********************************************************************/
//          double start_time = usrp_rx->get_time_now().get_real_secs() + start_delay;
//          bool rx_stream_now = (start_delay == 0.0);
         
//          auto rx_thread = d_tx_rx_thread_group.create_thread([=, &finished, &rx_finish, &rx_en]() {
//              receive(usrp_rx, rx_stream, finished, elevate_priority, start_time, rx_stream_now, rx_en, rx_finish);
//          });
//          uhd::set_thread_name(rx_thread, "rx_stream");
//          /***********************************************************************
//           * Transmit thread
//           **********************************************************************/
//          bool tx_has_time_spec = (start_delay != 0.0);
         
//          auto tx_thread = d_tx_rx_thread_group.create_thread([=, &finished, &tx_finish, &tx_en]() {
//              transmit(sbw, step, start_time, tx_has_time_spec, tx_en, tx_finish);
//          });
//          uhd::set_thread_name(tx_thread, "tx_stream");
 
//          auto control_thread = d_tx_rx_thread_group.create_thread([=, &finished, &tx_finish, &rx_finish, &tx_en, &rx_en]() {
//              control(radio, finished, tx_en, rx_en, tx_finish, rx_finish, elevate_priority);
//          });
//          uhd::set_thread_name(control_thread, "control_stream");    
 
//          d_tx_rx_thread_group.join_all();
//      }

//      void rfnoc_radar_impl::control(uhd::rfnoc::radio_block_control::sptr radio,
//                                     std::atomic<bool>& finished,
//                                     std::atomic<bool>& tx_en,
//                                     std::atomic<bool>& rx_en,
//                                     std::atomic<bool>& tx_finish,
//                                     std::atomic<bool>& rx_finish,
//                                     bool elevate_priority)
//      {
 
//          if (elevate_priority) {
//              uhd::set_thread_priority_safe(1, true); 
//          }

//          while (not finished) {
//              while (tx_finish && rx_finish){
//                  uhd::time_spec_t time_now;
                 
//                  //sleep(1);

//                  // std::cout<<"CONTROL SET"<<std::endl;
//                  //std::unique_lock<std::mutex> lock(mtx);
//                  time_now = radio->get_time_now();
//                  time_global = time_now + uhd::time_spec_t(0.01);
//                  tx_en = true;
//                  rx_en = true;
//                  tx_finish = false;
//                  rx_finish = false;

//                  std::this_thread::sleep_for(std::chrono::microseconds(10000));
//                  //lock.unlock();
//              }
//          }
 
//          // send a mini EOB packet
 
//          //md.end_of_burst = true;
//          // tx_stream->send("", 0, md);
//      }
 
//      int rfnoc_radar_impl::transmit(double sbw, 
//                                     double step,
//                                     double start_time, 
//                                     bool tx_has_time_spec,
//                                     std::atomic<bool>& tx_en,
//                                     std::atomic<bool>& tx_finish)
//      {
//          if (elevate_priority) {
//              uhd::set_thread_priority_safe(1, true);
//          }
         

//          /************************************************************************
//           * Send data to replay (== record the data)
//           ***********************************************************************/
//          uhd::tx_metadata_t tx_md;
 
//          tx_md.start_of_burst = true;
//          tx_md.end_of_burst   = true;
//          // We use a very big timeout here, any network buffering issue etc. is not
//          // a problem for this application, and we want to upload all the data in one
//          // send() call.
//          size_t num_tx_samps = tx_stream->send(tx_buffer, samples_to_replay, tx_md, 5.0);
//          if (num_tx_samps != samples_to_replay) {
//              std::cout << "ERROR: Unable to send " << samples_to_replay << " samples (sent "
//                  << num_tx_samps << ")" << std::endl;
//              return EXIT_FAILURE;
//          }
         
//          /************************************************************************
//           * Start transmit data
//           ***********************************************************************/

//          while (not finished) {    
//              while(tx_en) {
//                  // replay_ctrl->config_play(replay_buff_addr, replay_buff_size, replay_chan);
//                  // uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
//                  // stream_cmd.num_samps = samples_to_replay;
//                  // std::cout << "Issuing replay command for " << samples_to_replay << " samps..." << std::endl;
//                  // stream_cmd.stream_now = true;
//                 //  stream_cmd.time_spec = time_global;
//                  replay_ctrl->play(
//                      replay_buff_addr, replay_buff_size, replay_chan, time_global, false);
//                 //  replay_ctrl->issue_stream_cmd(stream_cmd, replay_chan);
//                 //  replay_ctrl->config_play(replay_buff_addr, replay_buff_size, replay_chan);
//                  tx_finish = true;
//                  tx_en = false;
//              }
//          }
//          // send a mini EOB packet

//          // md.end_of_burst = true;

//          std::cout << "Letting device settle..." << std::endl;
//          std::this_thread::sleep_for(std::chrono::seconds(1));
//          return EXIT_SUCCESS;
//      }
 
//      void rfnoc_radar_impl::receive(uhd::usrp::multi_usrp::sptr usrp,
//          uhd::rx_streamer::sptr rx_stream,
//          std::atomic<bool>& finished,
//          bool elevate_priority,
//          double start_time,
//          bool rx_stream_now,
//          std::atomic<bool>& rx_en,
//          std::atomic<bool>& rx_finish)
//      {
//          if (elevate_priority) {
//              uhd::set_thread_priority_safe(1, true);
//          }
//          // setup variables
//          std::vector<gr_complex> dummy_vec(n_delay);
 
//          uhd::rx_metadata_t md;
//          uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE);
//          //uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS );
 
//          //rx_stream->issue_stream_cmd(cmd);
 
//          // md.start_of_burst = true;
//          // md.end_of_burst = true;
 
//          // Set up and allocate buffers
//          //std::cout<<"Tx buff size:"<< tx_buff_size <<std::endl;
//          cmd.num_samps = tx_buff_size + n_delay;
//          //std::cout<<"cmd.num_samps:"<< cmd.num_samps <<std::endl;
//          //cmd.time_spec = uhd::time_spec_t(start_time);
//          //cmd.time_spec = time_global;
//          //cmd.stream_now = rx_stream_now;
//          cmd.stream_now = false;
//          pmt::pmt_t rx_data_pmt = pmt::make_c32vector(tx_buff_size, 0);
//          gr_complex* rx_data_ptr = pmt::c32vector_writable_elements(rx_data_pmt, tx_buff_size);
 
 
//          //double time_until_start = start_time - usrp->get_time_now().get_real_secs();
//          //double recv_timeout = 0.01 + time_until_start;
//          double recv_timeout = 0.025;
//          bool stop_called = false;
//          //std::cout<<"time_until_start:" << time_until_start <<std::endl; 
//          //std::cout<<"recv_timeout:" << recv_timeout <<std::endl; 
 
//          // TODO: Handle multiple channels (e.g., one out port per channel)
//          while (true) {
//              while(rx_en){
//                  if (finished and not stop_called) {
//                      rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
//                      // std::cout<< "issue stop command" << std::endl;
//                      stop_called = true;
//                  }
//                  try {
 
//                      // cmd.num_samps = tx_buff_size;
 
//                      cmd.time_spec = time_global;
 
//                      // if (n_delay > 0) {
//                      //     // Throw away n_delay samples at the beginning
 
//                      //     std::vector<gr_complex> dummy_vec(n_delay);
//                      //     rx_stream->issue_stream_cmd(cmd);
//                      //     size_t n_rx =
//                      //         rx_stream->recv(dummy_vec.data(), n_delay, md, recv_timeout);
//                      //         //std::cout<<"Discarded samples num:" << n_rx <<std::endl; 
//                      //     rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
//                      //     //n_delay -= n_rx;
//                      // }
 
//                      rx_stream->issue_stream_cmd(cmd);
//                      //std::cout<<"Starting to receive samples..."<<std::endl;
//                      size_t n_rx = rx_stream->recv(dummy_vec.data(), n_delay, md, recv_timeout);
//                      // std::cout<<"Throw samples num:" << n_rx <<std::endl;
//                      //std::cout<<"Expected samples num:" << tx_buff_size <<std::endl;              
//                      size_t n_receive = rx_stream->recv(rx_data_ptr, tx_buff_size, md, recv_timeout);
//                      //std::cout<<"Receive timeout:" << md.error_code <<std::endl;
//                      //size_t n_receive = rx_stream->recv(rx_data_ptr, 10000, md, recv_timeout);    
//                      // std::cout<<"Received samples num:" << n_receive <<std::endl;
//                      //rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
//                      //recv_timeout = 0.01;
//                      // Copy any new metadata to the output and reset the metadata
//                      if (n_receive == tx_buff_size)
//                      {
//                          // std::cout<<"Received samples num equal transmitted samples num" << std::endl;
//                          pmt::pmt_t meta = this->next_meta;
//                          if (pmt::length(meta) > 0) {
//                              meta = pmt::dict_add(   
//                              meta, pmt::intern(rx_freq_key), pmt::from_double(rx_freq));
//                              this->next_meta = pmt::make_dict();
//                          }
//                          message_port_pub(PMT_OUT, pmt::cons(meta, rx_data_pmt));
//                      }
//                      // TODO: May need to resize the buffer if the transmit waveform changes
//                  } catch (uhd::io_error& e) {
//                      std::cerr << "Caught an IO exception. " << std::endl;
//                      std::cerr << e.what() << std::endl;
//                      return;
//                  }
 
//                  // Handle errors
//                  switch (md.error_code) {
//                      case uhd::rx_metadata_t::ERROR_CODE_NONE:
//                      if ((finished or stop_called) and md.end_of_burst) {
//                      return;
//                      }
//                      break;
//                      default:
//                      //std::cout<<"Error! Error code:" << md.error_code << std::endl;
//                      break;
 
//                  }
//                  // std::unique_lock<std::mutex> lock(mtx);
//                  rx_finish = true;
//                  rx_en = false;
//                  //lock.unlock();
//              }
//          }
//      }
 
//      int rfnoc_radar_impl::config_rfnoc(uhd::rfnoc::rfnoc_graph::sptr& graph,
//                                         uhd::rfnoc::radio_block_control::sptr& radio,
//                                         const std::string& rfnoc_args,
//                                         const std::string& tx_args,
//                                         const std::string& rx_args,
//                                         const std::string& ref,
//                                         const size_t cpi,
//                                         const double lfm_width,        
//                                         const double prf, 
//                                         const double bandwidth,
//                                         const double sample_rate,
//                                         const double tx_freq,
//                                         const double tx_gain,                                     
//                                         const double rx_freq,
//                                         const double rx_gain)
//      {
//         /************************************************************************
//           * Create device and block controls
//           ***********************************************************************/
//         if (verbose) std::cout << "Creating the RFNoC graph with args: " << rfnoc_args << "..." << std::endl;
//         graph = uhd::rfnoc::rfnoc_graph::make(rfnoc_args);
 
//          // 查找无线电块
//         auto radio_blocks = graph->find_blocks<uhd::rfnoc::radio_control>("");
//         if (radio_blocks.empty()) {
//             std::cout << "错误: 未找到无线电模块! " << std::endl;
            
//             return EXIT_FAILURE;
//         }
//         radio = graph->get_block<uhd::rfnoc::radio_control>(radio_blocks.front());
            
//         // 设置频率
//         radio->set_tx_frequency(tx_freq, 0);
//         radio->set_rx_frequency(rx_freq, 0);
        
//         // 设置增益
//         radio->set_tx_gain(tx_gain, 0);
//         radio->set_rx_gain(rx_gain, 0);
          
//         // 设置带宽
//         radio->set_tx_bandwidth(bandwidth, 0);
//         radio->set_rx_bandwidth(bandwidth, 0);

//         // 设置天线
//         radio->set_tx_antenna("TX/RX", 0);
//         radio->set_rx_antenna("RX2", 0);
        
//         // 设置采样率
//         // radio->set_rate(sample_rate);
        
//         std::cout << boost::format("无线电配置: TX频率=%.2f MHz, RX频率=%.2f MHz, TX增益=%.1f dB, RX增益=%.1f dB")
//             % (tx_freq / 1e6) % (rx_freq / 1e6) % tx_gain % rx_gain << std::endl;

//         // 查找数字上变频模块
//         auto duc_blocks = graph->find_blocks<uhd::rfnoc::duc_block_control>("");
//         if (duc_blocks.empty()) {
//             std::cout << "错误: 未找到数字上变频模块! " << std::endl;
            
//             return EXIT_FAILURE;
//         }
//         auto duc = graph->get_block<uhd::rfnoc::duc_block_control>(duc_blocks.front());
            
//         // 设置频率
//         double duc_freq = duc->set_input_rate(sample_rate, 0);
        
//         std::cout << boost::format("数字上变频配置：频率=%.2f") % duc_freq << std::endl;

//         // 查找数字下变频模块
//         auto ddc_blocks = graph->find_blocks<uhd::rfnoc::ddc_block_control>("");
//         if (ddc_blocks.empty()) {
//             std::cout << "错误: 未找到数字下变频模块! " << std::endl;
            
//             return EXIT_FAILURE;
//         }
//         auto ddc = graph->get_block<uhd::rfnoc::ddc_block_control>(ddc_blocks.front());
            
//         // 设置频率
//         double ddc_freq = ddc->set_output_rate(sample_rate, 0);
        
//         std::cout << boost::format("数字下变频配置：频率=%.2f") % ddc_freq << std::endl;
         
//         // 查找radar模块
//         std::cout << "查找Radar模块..." << std::endl;
//         auto radar_blocks = graph->find_blocks<rfnoc::wingate::wingate_block_control>("");
        
//         if (radar_blocks.empty()) {
//             std::cout << "错误: 未找到自定义LFM模块! " << std::endl;
            
//             return EXIT_FAILURE;
//         }

//         // 获取块控制器
//         std::cout << "获取块控制器..." << std::endl;
//         auto radar_block = graph->get_block<rfnoc::wingate::wingate_block_control>(
//             radar_blocks.front()
//         );
//         if (!radar_block) {
//           std::cout << "错误：无法获取块控制器！" << std::endl;
//           return EXIT_FAILURE;
//         }

//         // 设置LFM参数
//         std::cout << boost::format("设置Radar参数: 回波宽度=%u, LFM宽度=%u, CPI=%u")
//             % prt_samples % samples_per_pulse % num_pulses
//             << std::endl;

//         // size_t prt_samples = sample_rate / prf;
//         size_t prt_samples = 16384;
//         size_t samples_per_pulse = sample_rate * lfm_width;
//         size_t num_pulses = cpi;

//         // 设置模块参数（假设的API - 根据你的实际实现调整）
//         radar_block->set_pulse_size_value(prt_samples);
//         radar_block->set_lfm_size_value(samples_per_pulse);
//         radar_block->set_cpi_value(num_pulses);

//         // 读取回参数验证
//         size_t rtn_wave_size_read = radar_block->get_pulse_size_value();
//         size_t lfm_size_read = radar_block->get_lfm_size_value();
//         size_t cpi_read = radar_block->get_cpi_value();

//         std::cout << "参数回读验证：" << std::endl;
//         std::cout << boost::format("  回波宽度: 设置=%u, 读取=%u")
//             % prt_samples % rtn_wave_size_read << std::endl;
//         std::cout << boost::format("  LFM宽度: 设置=%u, 读取=%u")
//             % samples_per_pulse % lfm_size_read << std::endl;
//         std::cout << boost::format("  CPI: 设置=%u, 读取=%u")
//             % num_pulses % cpi_read << std::endl;

//         uhd::stream_args_t tx_stream_args(tx_cpu_format, tx_otw_format);
//         uhd::stream_args_t rx_stream_args(rx_cpu_format, rx_otw_format);
    
//         // 如果指定了独立的TX/RX参数，使用它们
//         if (!tx_args.empty()) {
//             tx_stream_args.args["tx_args"] = tx_args;
//         }
//         if (!rx_args.empty()) {
//             rx_stream_args.args["rx_args"] = rx_args;
//         }

//         // 创建流
//         auto tx_streamer = graph->create_tx_streamer(1, tx_stream_args);
//         auto tx_streamer_lfm = graph->create_tx_streamer(1, tx_stream_args);
//         auto rx_streamer = graph->create_rx_streamer(1, rx_stream_args);
//         auto rx_streamer_process = graph->create_rx_streamer(1, rx_stream_args);

//         graph->connect(radio_blocks.front(), 0, ddc_blocks.front(), 0);
//         graph->connect(duc_blocks.front(), 0, radio_blocks.front(), 0);

//         // graph->connect(ddc_blocks.front(), 0, split_blocks.front(), 0);
//         graph->connect(ddc_blocks.front(), 0, radar_blocks.front(), 0);
        
//         // graph->connect(split_blocks.front(), 0, radar_blocks.front(), 0);
//         // graph->connect(split_blocks.front(), 1, rx_streamer, 0);

//         graph->connect(tx_streamer_lfm, 0, radar_blocks.front(), 1);
//         graph->connect(radar_blocks.front(), 0, rx_streamer_process, 0);

//         graph->connect(tx_streamer, 0, duc_blocks.front(), 0);

//         graph->commit();

//         for (size_t i = 0; i < graph->get_num_mboards(); ++i) {
//           graph->get_mb_controller(i)->set_clock_source(ref);
//         }
 
//         // Allow for some setup time
//         std::this_thread::sleep_for(std::chrono::milliseconds(200));

//         return EXIT_SUCCESS;
//     }

//     // void rfnoc_radar_impl::read_calibration_file(const std::string& filename)
//     // {
//     //     std::ifstream file(filename);
//     //     nlohmann::json json;
//     //     if (file) {
//     //         file >> json;
//     //         std::string radio_type = usrp_rx->get_mboard_name();
//     //         for (auto& config : json[radio_type]) {
//     //             if (config["samp_rate"] == usrp_rx->get_tx_rate() 
//     //             //and
//     //                 //config["master_clock_rate"] == usrp_tx->get_master_clock_rate()
                    
//     //                 ) {
//     //                 n_delay = config["delay"];
//     //                 break;
//     //             }
//     //         }
//     //         if (n_delay == 0)
//     //             UHD_LOG_INFO("USRP Radar",
//     //                         "Calibration file found, but no data exists for this "
//     //                         "combination of radio, master clock rate, and sample rate");
//     //     } else {
//     //         UHD_LOG_INFO("USRP Radar", "No calibration file found");
//     //     }

//     //     file.close();
//     // }
 
//      void rfnoc_radar_impl::set_metadata_keys(const std::string& tx_freq_key,
//                                               const std::string& rx_freq_key,
//                                               const std::string& sample_start_key)
//      {
//          this->tx_freq_key = tx_freq_key;
//          this->rx_freq_key = rx_freq_key;
//          this->sample_start_key = sample_start_key;
//      }
//    } /* namespace plasma */
//  } /* namespace gr */
 