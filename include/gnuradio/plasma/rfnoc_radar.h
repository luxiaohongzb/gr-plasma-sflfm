/* -*- c++ -*- */
/*
 * Copyright 2024 yantob.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

 #ifndef INCLUDED_PLASMA_RFNOC_RADAR_H
 #define INCLUDED_PLASMA_RFNOC_RADAR_H
 
 #include <gnuradio/plasma/api.h>
 #include <gnuradio/block.h>
 
 namespace gr {
   namespace plasma {
 
     /*!
      * \brief <+description of block+>
      * \ingroup plasma
      *
      */
     class PLASMA_API rfnoc_radar : virtual public gr::block
     {
      public:
       typedef std::shared_ptr<rfnoc_radar> sptr;
 
       /*!
        * \brief Return a shared_ptr to a new instance of plasma::rfnoc_radar.
        *
        * To avoid accidental use of raw pointers, plasma::rfnoc_radar's
        * constructor is in a private implementation
        * class. plasma::rfnoc_radar::make is the public interface for
        * creating new instances.
        */
       static sptr make(const std::string& rfnoc_args,
                        const std::string& device_addr,
                        const std::string& ref,
                        const double bw,
                        const double sample_rate,
                        const double tx_freq,
                        const double rx_freq,
                        const double tx_gain,
                        const double rx_gain,
                        const double start_delay,
                        const bool elevate_priority,
                        const std::string& cal_file,
                        const bool verbose);
       virtual void set_metadata_keys(const std::string& tx_freq_key,
                                      const std::string& rx_freq_key,
                                      const std::string& sample_start_key) = 0;
     };
 
   } // namespace plasma
 } // namespace gr
 
 #endif /* INCLUDED_PLASMA_RFNOC_SFLFM_RADAR_H */
 