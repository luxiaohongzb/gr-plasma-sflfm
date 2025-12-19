// /* -*- c++ -*- */
// /*
//  * Copyright 2024 yantob.
//  *
//  * SPDX-License-Identifier: GPL-3.0-or-later
//  */

//  #ifndef INCLUDED_PLASMA_RFNOC_RADAR_IMPL_H
//  #define INCLUDED_PLASMA_RFNOC_RADAR_IMPL_H
 
//  #include <gnuradio/plasma/rfnoc_radar.h>
//  #include <gnuradio/plasma/pmt_constants.h>
//  #include <uhd/rfnoc/block_id.hpp>
//  #include <uhd/rfnoc/duc_block_control.hpp>
//  #include <uhd/rfnoc/mb_controller.hpp>
//  #include <uhd/rfnoc/radio_control.hpp>
//  #include <uhd/rfnoc_graph.hpp>
//  #include <uhd/types/tune_request.hpp>
//  #include <uhd/utils/graph_utils.hpp>
//  #include <uhd/utils/math.hpp>
//  #include <uhd/utils/safe_main.hpp>
//  #include <uhd/utils/thread.hpp>
//  #include <uhd/usrp/multi_usrp.hpp>
//  #include <rfnoc/wingate/wingate_block_control.hpp>
//  #include <boost/program_options.hpp>
//  #include <boost/thread/thread.hpp>
//  #include <chrono>
//  #include <csignal>
//  #include <fstream>
//  #include <iostream>
//  #include <thread>
 
//  namespace gr {
//    namespace plasma {
 
//      class rfnoc_radar_impl : public rfnoc_radar
//      {
//       private:
//        // Rfnoc params
//        uhd::rfnoc::rfnoc_graph::sptr graph;
//        uhd::rfnoc::radio_block_control::sptr radio;
 
//        std::string rfnoc_args, tx_args, rx_args;
//        size_t cpi;
//        double lfm_width, prf, bandwidth, sample_rate;
//        double tx_freq, tx_gain;
//        double rx_freq, rx_gain;
       
//        uhd::tx_streamer::sptr tx_stream;
//        uhd::rx_streamer::sptr rx_stream;
//        uhd::rx_streamer::sptr rx_stream_process;
 
//        uhd::time_spec_t time_global;
 
//        double start_freq, sbw, step;
//        double start_delay;

//        bool elevate_priority;
//        bool verbose;
 
//        std::string tx_cpu_format, rx_cpu_format;
//        std::string tx_otw_format, rx_otw_format;
//        std::string ref;
//        std::string cal_file;
 
//        gr::thread::thread d_main_thread;
//        boost::thread_group d_tx_rx_thread_group;
 
//        size_t tx_buff_size;
//        pmt::pmt_t tx_data;
//        pmt::pmt_t next_meta; // Metadata for the next Rx pdu
 
//        std::atomic<bool> tx_en;
//        std::atomic<bool> rx_en;
//        std::atomic<bool> tx_finish;
//        std::atomic<bool> rx_finish;
 
//        std::atomic<bool> finished;
//        std::atomic<bool> msg_received;
 
//        // Metadata keys
//        std::string tx_freq_key;
//        std::string rx_freq_key;
//        std::string sample_start_key;
 
//        void control(uhd::rfnoc::radio_block_control::sptr radio;
//                     std::atomic<bool>& finished,
//                     std::atomic<bool>& tx_en,
//                     std::atomic<bool>& rx_en,
//                     std::atomic<bool>& tx_finish,
//                     std::atomic<bool>& rx_finish,
//                     bool elevate_priority);
 
//        int config_rfnoc(uhd::rfnoc::rfnoc_graph::sptr& graph,
//                         uhd::rfnoc::radio_block_control::sptr radio,
//                         const std::string& rfnoc_args,
//                         const std::string& tx_args,
//                         const std::string& rx_args,
//                         const std::string& ref,
//                         const size_t cpi,
//                         const double lfm_width,        
//                         const double prf, 
//                         const double bandwidth,
//                         const double sample_rate,
//                         const double tx_freq,
//                         const double tx_gain,
//                         const double rx_freq,
//                         const double rx_gain);
       
//        void receive(uhd::usrp::multi_usrp::sptr usrp,
//                     uhd::rx_streamer::sptr rx_stream,
//                     std::atomic<bool>& finished,
//                     bool elevate_priority,
//                     double adjusted_rx_delay,
//                     bool rx_stream_now,
//                     std::atomic<bool>& rx_en,
//                     std::atomic<bool>& rx_finish);
       
//        int transmit(double sbw, 
//                     double step,
//                     double start_time, 
//                     bool tx_has_time_spec,
//                     std::atomic<bool>& tx_en,
//                     std::atomic<bool>& tx_finish);
 
//        void read_calibration_file(const std::string& filename);
 
//        void set_metadata_keys(const std::string& tx_freq_key,
//                               const std::string& rx_freq_key,
//                               const std::string& sample_start_key);
 
//       public:
//        rfnoc_radar_impl(const std::string& rfnoc_args,
//                               const std::string& device_addr,
//                               const std::string& ref,           
//                               const size_t cpi,
//                               const double lfm_width,        
//                               const double prf,                            
//                               const double sample_rate,
//                               const double tx_freq,
//                               const double rx_freq,
//                               const double tx_gain,
//                               const double rx_gain,
//                               const double bandwidth,
//                               const double start_delay,
//                               const bool elevate_priority,
//                               const std::string& cal_file,
//                               const bool verbose);
//        ~rfnoc_radar_impl();
 
//        void handle_message(const pmt::pmt_t& msg);
 
//        /**
//         * @brief Initialize all buffers and set up the transmit and recieve threads
//         *
//         */
//        void run();
//        /**
//         * @brief Start the main worker thread
//         */
//        bool start() override;
//        /**
//         * @brief Stop the main worker thread
//         */
//        bool stop() override;
//      };
 
//    } // namespace plasma
//  } // namespace gr
 
//  #endif /* INCLUDED_PLASMA_RFNOC_RADAR_IMPL_H */
 