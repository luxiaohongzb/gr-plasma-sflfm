#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/plasma/freq_step_controller.h>
#include <freq_step_controller_pydoc.h>

void bind_freq_step_controller(py::module& m)
{
    using freq_step_controller = ::gr::plasma::freq_step_controller;

    py::class_<freq_step_controller, gr::block, gr::basic_block, std::shared_ptr<freq_step_controller>>(m, "freq_step_controller", D(freq_step_controller))
        .def(py::init(&freq_step_controller::make),
             py::arg("start_freq"),
             py::arg("stop_freq"),
             py::arg("step_freq"),
             py::arg("loop"),
             D(freq_step_controller, make))
        .def("init_meta_dict",
             &freq_step_controller::init_meta_dict,
             py::arg("tx_freq_key"),
             D(freq_step_controller, init_meta_dict));
}