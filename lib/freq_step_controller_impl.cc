#include "freq_step_controller_impl.h"
#include <gnuradio/io_signature.h>
#include <gnuradio/pdu.h>

namespace gr {
namespace plasma {

freq_step_controller::sptr freq_step_controller::make(double start_freq,
                                                      double stop_freq,
                                                      double step_freq,
                                                      bool loop)
{
    return gnuradio::make_block_sptr<freq_step_controller_impl>(start_freq,
                                                                stop_freq,
                                                                step_freq,
                                                                loop);
}

freq_step_controller_impl::freq_step_controller_impl(double start_freq,
                                                     double stop_freq,
                                                     double step_freq,
                                                     bool loop)
    : gr::block("freq_step_controller",
                gr::io_signature::make(0, 0, 0),
                gr::io_signature::make(0, 0, 0)),
      d_start(start_freq),
      d_stop(stop_freq),
      d_step(step_freq),
      d_loop(loop),
      d_current(start_freq)
{
    d_in_port = PMT_IN;
    d_out_port = PMT_OUT;
    d_ctrl_port = pmt::intern("ctrl");
    message_port_register_in(d_in_port);
    message_port_register_out(d_out_port);
    message_port_register_out(d_ctrl_port);
    set_msg_handler(d_in_port, [this](const pmt::pmt_t& msg) { handle_message(msg); });
}

freq_step_controller_impl::~freq_step_controller_impl() {}

void freq_step_controller_impl::handle_message(const pmt::pmt_t& msg)
{
    if (!pmt::is_pdu(msg)) {
        return;
    }
    pmt::pmt_t meta = pmt::car(msg);
    pmt::pmt_t data = pmt::cdr(msg);
    size_t n = pmt::length(data);
    message_port_pub(d_out_port,
                     pmt::cons(meta, pmt::init_c32vector(n, pmt::c32vector_elements(data, n))));

    pmt::pmt_t ctrl_meta = pmt::make_dict();
    ctrl_meta = pmt::dict_add(ctrl_meta, d_tx_freq_key, pmt::from_double(d_current));
    message_port_pub(d_ctrl_port, pmt::cons(ctrl_meta, pmt::make_c32vector(0, 0)));
    advance();
}

void freq_step_controller_impl::advance()
{
    if (d_step == 0.0) {
        return;
    }
    double next = d_current + d_step;
    if (d_start <= d_stop) {
        if (next > d_stop) {
            if (d_loop) {
                d_current = d_start;
            } else {
                d_current = d_stop;
            }
            return;
        }
    } else {
        if (next < d_stop) {
            if (d_loop) {
                d_current = d_start;
            } else {
                d_current = d_stop;
            }
            return;
        }
    }
    d_current = next;
}

void freq_step_controller_impl::init_meta_dict(std::string tx_freq_key)
{
    d_tx_freq_key = pmt::intern(tx_freq_key);
}

} // namespace plasma
} // namespace gr