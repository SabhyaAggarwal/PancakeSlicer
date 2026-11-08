#pragma once

#include <pybind11/pybind11.h>

namespace Slic3r::host_bindings {

void register_geometry(pybind11::module_& host);
void register_mesh(pybind11::module_& host);
void register_presets(pybind11::module_& host);
void register_model(pybind11::module_& host);
void register_app(pybind11::module_& host);
void register_slicing(pybind11::module_& host);
void register_plugin(pybind11::module_& host);
}
