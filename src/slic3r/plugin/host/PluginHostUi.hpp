#pragma once

#include <pybind11/pybind11.h>

#include <string>

namespace Slic3r {

class PluginHostUi
{
public:
    static void RegisterBindings(pybind11::module_& host);

    static void close_windows_for_plugin(const std::string& plugin_key);
};

}
