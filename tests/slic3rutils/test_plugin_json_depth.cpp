#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include <slic3r/plugin/PluginFsUtils.hpp>
#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/plugin/PythonInterpreter.hpp>

#include "plugin_test_utils.hpp"

#include <cstdint>
#include <exception>
#include <utility>

#include <nlohmann/json.hpp>
#include <pybind11/embed.h>
#include <pybind11/gil.h>
#include <pybind11/pytypes.h>

using namespace Slic3r;

namespace {
struct ScopedPluginManager
{
    ScopedDataDir python_data_dir{"plugin-json-depth"};
    bool          initialized = PluginManager::instance().initialize();
    ~ScopedPluginManager()
    {
        PluginManager::instance().shutdown();
        PythonInterpreter::instance().shutdown();
    }
};
}

TEST_CASE("py_to_json raises instead of overflowing on pathologically deep input", "[PluginHost][Python]")
{
    ScopedPluginManager manager;
    REQUIRE(manager.initialized);
    namespace py = pybind11;
    py::gil_scoped_acquire gil;

    py::object deep = py::int_(0);
    for (int i = 0; i < 300; ++i) {
        py::list wrapper;
        wrapper.append(deep);
        deep = std::move(wrapper);
    }
    CHECK_THROWS_AS(py_to_json(deep), std::exception);
}

TEST_CASE("py_to_json still converts reasonably nested input", "[PluginHost][Python]")
{
    ScopedPluginManager manager;
    REQUIRE(manager.initialized);
    namespace py = pybind11;
    py::gil_scoped_acquire gil;

    py::dict d;
    d["a"] = py::int_(1);
    py::list inner;
    inner.append(py::str("x"));
    inner.append(py::int_(2));
    d["b"] = inner;

    const nlohmann::json j = py_to_json(d);
    CHECK(j.at("a").get<std::int64_t>() == 1);
    CHECK(j.at("b").at(0).get<std::string>() == "x");
    CHECK(j.at("b").at(1).get<std::int64_t>() == 2);
}
