#ifndef INCLUDED_PLASMA_FREQ_STEP_CONTROLLER_H
#define INCLUDED_PLASMA_FREQ_STEP_CONTROLLER_H

#include <gnuradio/block.h>
#include <gnuradio/plasma/api.h>

namespace gr {
namespace plasma {

class PLASMA_API freq_step_controller : virtual public gr::block
{
public:
    typedef std::shared_ptr<freq_step_controller> sptr;

    static sptr make(double start_freq,
                     double stop_freq,
                     double step_freq,
                     bool loop);

    virtual void init_meta_dict(std::string tx_freq_key) = 0;
};

} // namespace plasma
} // namespace gr
