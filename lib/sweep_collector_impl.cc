/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

 #include "sweep_collector_impl.h"
 #include <gnuradio/io_signature.h>
 #include <gnuradio/plasma/pmt_constants.h>
 #include <boost/format.hpp>
 #include <chrono>
 #include <filesystem>
 #include <iostream>
 
 namespace gr {
 namespace plasma {
 
 using namespace std::chrono_literals;
 
 
 sweep_collector::sptr sweep_collector::make(const std::string &output_prefix,
                                             const std::string &output_dir,
                                             int save_cycles,
                                             double sweep_start,
                                             double sweep_stop,
                                             double sweep_step,
                                             const std::string &freq_key)
 {
     return gnuradio::make_block_sptr<sweep_collector_impl>(output_prefix,
                                                             output_dir,
                                                             save_cycles,
                                                             sweep_start,
                                                             sweep_stop,
                                                             sweep_step,
                                                             freq_key);
 }
 
 sweep_collector_impl::sweep_collector_impl(const std::string &output_prefix,
                                            const std::string &output_dir,
                                            int save_cycles,
                                            double sweep_start,
                                            double sweep_stop,
                                            double sweep_step,
                                            const std::string &freq_key)
     : gr::block("sweep_collector", gr::io_signature::make(0, 0, 0),
                 gr::io_signature::make(0, 0, 0)),
       output_prefix(output_prefix),
       output_dir(output_dir),
       sweep_start(sweep_start),
       sweep_stop(sweep_stop),
       sweep_step(sweep_step),
       freq_meta_key(freq_key),
       save_every(std::max(1, save_cycles)),
       sweep_count(0)
 {
     message_port_register_in(pmt::intern("in"));
     message_port_register_out(pmt::intern("out"));
     set_msg_handler(pmt::intern("in"), [this](pmt::pmt_t msg) { handle_message(msg); });
 
     compute_expected_freqs();
 }
 
 sweep_collector_impl::~sweep_collector_impl() {}
 
 void sweep_collector_impl::set_sweep_params(double start, double stop, double step)
 {
     std::lock_guard<std::mutex> g(lock);
     sweep_start = start;
     sweep_stop = stop;
     sweep_step = step;
     compute_expected_freqs();
     collected.clear();
 }
 
 void sweep_collector_impl::set_freq_meta_key(const std::string &key)
 {
     std::lock_guard<std::mutex> g(lock);
     freq_meta_key = key;
 }
 
 void sweep_collector_impl::set_output_prefix(const std::string &prefix)
 {
     std::lock_guard<std::mutex> g(lock);
     output_prefix = prefix;
 }
 
 void sweep_collector_impl::compute_expected_freqs()
 {
     expected_freqs.clear();
     if (sweep_step == 0.0) {
         return;
     }
     if (sweep_step > 0) {
         for (double f = sweep_start; f <= sweep_stop + 1e-9; f += sweep_step) {
             expected_freqs.push_back(f);
         }
     } else {
         for (double f = sweep_start; f >= sweep_stop - 1e-9; f += sweep_step) {
             expected_freqs.push_back(f);
         }
     }
 }
 
 bool sweep_collector_impl::check_sweep_complete()
 {
     if (expected_freqs.empty())
         return false;
     for (double f : expected_freqs) {
         auto it = collected.find(f);
         if (it == collected.end() || it->second.empty())
             return false;
     }
     return true;
 }
 
 void sweep_collector_impl::write_and_reset()
 {
     // increment sweep counter (one sweep completed)
     ++sweep_count;
 
     // Only write if we haven't reached the target number of sweeps yet
     if (sweep_count > save_every) {
         std::cout << "[sweep_collector] Completed sweep " << sweep_count << " (limit reached, not writing)" << std::endl;
         collected.clear();
         return;
     }
 
     // Write output after every sweep (up to save_every sweeps)
     // create timestamped filename under output_dir
     auto now = std::chrono::system_clock::now();
     auto t = std::chrono::system_clock::to_time_t(now);
     char buf[64];
     std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
     std::string dir = output_dir.empty() ? std::string(".") : output_dir;
     // ensure directory exists
     try {
         std::error_code ec;
         std::filesystem::create_directories(dir, ec);
         if (ec) {
             std::cerr << "sweep_collector: failed to create output_dir " << dir << ": " << ec.message() << std::endl;
         }
     } catch (...) {
     }
 
     std::string filename = dir + "/" + output_prefix + std::string("_") + buf + std::string("_c") + std::to_string(sweep_count) + std::string(".bin");
 
     std::ofstream out(filename, std::ios::binary);
     if (!out) {
         std::cerr << "sweep_collector: failed to open output file: " << filename << std::endl;
         collected.clear();
         return;
     }
 
     std::cout << "[sweep_collector] Writing " << collected.size() << " frequencies to " << filename << std::endl;
 
     // Write number of frequencies
     uint64_t n_freqs = collected.size();
     out.write(reinterpret_cast<const char *>(&n_freqs), sizeof(n_freqs));
 
     for (auto &kv : collected) {
         double freq = kv.first;
         uint64_t n_samples = kv.second.size();
         std::cout << "[sweep_collector]   Freq " << freq << " Hz: " << n_samples << " samples" << std::endl;
         // write frequency (double) and sample count
         out.write(reinterpret_cast<const char *>(&freq), sizeof(freq));
         out.write(reinterpret_cast<const char *>(&n_samples), sizeof(n_samples));
         // write complex samples as interleaved float32 (real, imag)
         for (const gr_complex &c : kv.second) {
             float re = std::real(c);
             float im = std::imag(c);
             out.write(reinterpret_cast<const char *>(&re), sizeof(re));
             out.write(reinterpret_cast<const char *>(&im), sizeof(im));
         }
     }
     out.close();
 
     std::cout << "[sweep_collector] File written successfully: " << filename << std::endl;
 
     // reset for next sweep
     collected.clear();
 }
 
 void sweep_collector_impl::handle_message(const pmt::pmt_t &msg)
 {
     if (!pmt::is_pdu(msg)) {
         std::cout << "[sweep_collector] handle_message: not a PDU" << std::endl;
         return;
     }
 
     pmt::pmt_t meta = pmt::car(msg);
     pmt::pmt_t vec = pmt::cdr(msg);
 
     // Print incoming metadata for debugging
     try {
         std::string meta_str = pmt::write_string(meta);
         std::cout << "[sweep_collector] Received PDU meta: " << meta_str << std::endl;
     } catch (...) {
         std::cout << "[sweep_collector] Received PDU meta (unprintable)" << std::endl;
     }
 
     pmt::pmt_t freq_pmt = pmt::dict_ref(meta, pmt::intern(freq_meta_key), pmt::PMT_NIL);
     double freq = 0.0;
     if (freq_pmt != pmt::PMT_NIL) {
         try {
             freq = pmt::to_double(freq_pmt);
             std::cout << "[sweep_collector] Parsed frequency meta (" << freq_meta_key << ") = " << freq << " Hz" << std::endl;
         } catch (...) {
             std::cout << "[sweep_collector] frequency meta present but cannot convert to double" << std::endl;
             return; // can't interpret frequency
         }
     } else {
         std::cout << "[sweep_collector] No frequency metadata (key='" << freq_meta_key << "'), ignoring PDU" << std::endl;
         return;
     }
 
     if (!pmt::is_c32vector(vec)) {
       //  std::cout << "[sweep_collector] PDU data is not c32vector; type: " << pmt::typeof(vec) << std::endl;
         return;
     }
 
     size_t n = pmt::length(vec);
     std::cout << "[sweep_collector] PDU sample count: " << n << std::endl;
     auto vec_data = pmt::c32vector_elements(vec);
     const gr_complex *data = vec_data.data();
     if (n > 0) {
         size_t show = std::min<size_t>(4, n);
         std::cout << "[sweep_collector] first " << show << " samples: ";
         for (size_t i = 0; i < show; ++i) {
             std::cout << "(" << std::real(data[i]) << "," << std::imag(data[i]) << ") ";
         }
         std::cout << std::endl;
     }
 
     {
         std::lock_guard<std::mutex> g(lock);
         // Replace the data for this frequency with the latest PDU
         auto &bucket = collected[freq];
         bucket.clear();  // Clear old data
         bucket.insert(bucket.end(), data, data + n);  // Insert new data
     }

     // Forward the message to output port for ifft_range_profile
     message_port_pub(pmt::intern("out"), msg);
 
     // If we've collected one buffer for each expected frequency, write out
     bool complete = false;
     {
         std::lock_guard<std::mutex> g(lock);
         complete = check_sweep_complete();
     }
     if (complete) {
         std::lock_guard<std::mutex> g(lock);
         std::cout << "-------------------write_and_reset ----------------" << std::endl;
         write_and_reset();
     }
 }
 
 } // namespace plasma
 } // namespace gr
 