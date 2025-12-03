#ifndef INCLUDED_PLASMA_FREQ_STEP_CONTROLLER_IMPL_H
#define INCLUDED_PLASMA_FREQ_STEP_CONTROLLER_IMPL_H

#include <gnuradio/plasma/freq_step_controller.h>
#include <gnuradio/plasma/pmt_constants.h>

namespace gr {
namespace plasma {

class freq_step_controller_impl : public freq_step_controller
{
private:
    pmt::pmt_t d_in_port;
    pmt::pmt_t d_out_port;
    pmt::pmt_t d_ctrl_port;
    pmt::pmt_t d_tx_freq_key;
    double d_start;
    double d_stop;
    double d_step;
    bool d_loop;
    double d_current;

    void handle_message(const pmt::pmt_t& msg);
    void advance();

public:
    freq_step_controller_impl(double start_freq,
                              double stop_freq,
                              double step_freq,
                              bool loop);
    ~freq_step_controller_impl();

    void init_meta_dict(std::string tx_freq_key) override;
};

} // namespace plasma
} // namespace gr

#endif /* INCLUDED_PLASMA_FREQ_STEP_CONTROLLER_IMPL_H */