#pragma once

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <memory.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <slic3r/plugin/PythonPluginBridge.hpp>

namespace {

void ensure_python_initialized()
{
    if (Py_IsInitialized())
        return;

    static std::unique_ptr<pybind11::scoped_interpreter> interpreter;

    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.parse_argv = 0;

    const auto python_home = boost::dll::program_location().parent_path() / "python";
#ifdef _WIN32
    const auto stdlib = python_home / "Lib";
#else
    const auto stdlib = python_home / "lib" /
                        ("python" + std::to_string(PY_MAJOR_VERSION) + "." + std::to_string(PY_MINOR_VERSION));
#endif

    if (boost::filesystem::exists(stdlib / "encodings")) {
        const std::string home = python_home.string();
        const PyStatus status  = PyConfig_SetBytesString(&config, &config.home, home.c_str());

        if (PyStatus_Exception(status)) {
            const char* message = status.err_msg ? status.err_msg : "Failed to set Python home";
            PyConfig_Clear(&config);
            throw std::runtime_error(message);
        }
    }

    interpreter = std::make_unique<pybind11::scoped_interpreter>(&config);
}

pybind11::module_ import_orca_module()
{
    ensure_python_initialized();

    (void) Slic3r::PythonPluginBridge::instance();
    return pybind11::module_::import("orca");
}

}
