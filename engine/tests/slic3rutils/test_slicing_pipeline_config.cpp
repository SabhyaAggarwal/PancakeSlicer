#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <ios>
#include <chrono>
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include <slic3r/plugin/PluginConfig.hpp>
#include "slic3r/plugin/PluginFsUtils.hpp"
#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/plugin/PythonInterpreter.hpp>
#include <slic3r/plugin/PythonPluginInterface.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "fff_print/test_helpers.hpp"
#include "plugin_test_utils.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <pybind11/embed.h>

#include <fstream>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;
namespace fs = boost::filesystem;
using json   = nlohmann::json;

namespace {

struct ScopedPluginManager
{
    ScopedDataDir python_data_dir{"plugin-python"};
    bool initialized = false;

    ScopedPluginManager() { initialized = PluginManager::instance().initialize(); }
    ~ScopedPluginManager()
    {
        PluginManager::instance().shutdown();
        PythonInterpreter::instance().shutdown();
    }
};

const char* const CONFIG_PROBE_SOURCE = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Config Probe"
# description = "Echoes its own config back to the test"
# author = "OrcaSlicer"
# version = "1.0"
# type = "slicing-pipeline"
# ///
import json

import orca

class ConfigEcho(orca.slicing.SlicingPipelineCapabilityBase):
    def get_name(self):
        return "ConfigEcho"

    def get_default_config(self):
        return {"alpha": "1.25", "beta": "hello"}

    def execute(self, ctx):
        if ctx.step != orca.slicing.Step.posSlice or ctx.object is None:
            return orca.ExecutionResult.success()
        try:
            text = repr(sorted(json.loads(self.get_config()).items()))
        except Exception as e:  # what plugins' defaults-fallback code swallows silently
            text = "config-error: " + repr(e)
        orca._probe_config = text  # read back by the test through pybind
        return orca.ExecutionResult.success("config probed")

@orca.plugin
class ConfigProbePackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(ConfigEcho)
)PY";

fs::path write_plugin(const std::string& stem, const std::string& source)
{
    const fs::path plugin_dir = fs::path(get_orca_plugins_dir()) / stem;
    fs::create_directories(plugin_dir);

    std::ofstream out((plugin_dir / (stem + ".py")).string(), std::ios::binary);
    out << source;
    out.close();

    return plugin_dir;
}

}

TEST_CASE("slicing-pipeline dispatch delivers the stored config to self.get_config()", "[slicing_pipeline][PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("pipeline-config");
    write_plugin("ConfigProbe", CONFIG_PROBE_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.get_config().load();
    manager.discover_plugins( false, true);

    std::string error;
    manager.load_plugin("ConfigProbe", true, {});
    REQUIRE(manager.wait_for_plugin_load("ConfigProbe", std::chrono::seconds(120), error));
    INFO("load error: " << error);
    REQUIRE(manager.is_plugin_loaded("ConfigProbe"));

    const PluginCapabilityId id{PluginCapabilityType::SlicingPipeline, "ConfigEcho", "ConfigProbe"};

    const auto seeded = manager.get_config().get_config(id);
    REQUIRE(seeded);
    CHECK(seeded->config == json({{"alpha", "1.25"}, {"beta", "hello"}}));

    REQUIRE(manager.get_config().store_capability_config(id, json({{"alpha", "9.5"}, {"beta", "hello"}})));

    Print print;
    Model model;
    auto  config = DynamicPrintConfig::full_print_config();
    config.set_key_value("slicing_pipeline_plugin", new ConfigOptionStrings({"ConfigEcho"}));
    config.set_key_value("plugins", new ConfigOptionStrings({"ConfigProbe;;ConfigEcho"}));
    init_print({cube(20)}, print, model, config);
    print.process();

    std::string observed = "<capability never executed>";
    {
        PythonGILState gil;
        REQUIRE(static_cast<bool>(gil));
        pybind11::module_ orca = pybind11::module_::import("orca");
        if (pybind11::hasattr(orca, "_probe_config"))
            observed = orca.attr("_probe_config").cast<std::string>();
    }
    INFO("config observed by Python: " << observed);
    CHECK(observed.find("'alpha', '9.5'") != std::string::npos);
    CHECK(observed.find("'beta', 'hello'") != std::string::npos);
    CHECK(observed.find("1.25") == std::string::npos);

    manager.unload_plugin("ConfigProbe");
}
