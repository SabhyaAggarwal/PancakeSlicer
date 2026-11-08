#include <catch2/catch_all.hpp>

#include <exception>
#include <slic3r/plugin/PluginConfig.hpp>
#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/plugin/PythonInterpreter.hpp>
#include <slic3r/plugin/PythonPluginBridge.hpp>
#include <slic3r/plugin/PythonPluginInterface.hpp>

#include <catch2/catch_test_macros.hpp>
#include "plugin_test_utils.hpp"
#include <pybind11/pytypes.h>
#include <pybind11/eval.h>
#include <pybind11/gil.h>

#include <nlohmann/json.hpp>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <memory>
#include <string>
#include <pybind11/cast.h>

namespace py = pybind11;
using namespace Slic3r;
using json = nlohmann::json;

namespace {

struct ScopedPluginManager
{
    ScopedDataDir python_data_dir{"plugin-python"};
    bool initialized = PluginManager::instance().initialize();

    ~ScopedPluginManager()
    {
        PluginManager::instance().shutdown();
        PythonInterpreter::instance().shutdown();
    }
};

py::module_ import_orca_module()
{
    (void) PythonPluginBridge::instance();
    return py::module_::import("orca");
}

py::object make_capability(const std::string& class_name,
                           const std::string& body,
                           const std::string& plugin_key,
                           const std::string& capability_name,
                           PluginCapabilityType type = PluginCapabilityType::Script)
{
    py::module_ orca = import_orca_module();

    py::dict globals;
    globals["orca"] = orca;

    py::exec("class " + class_name + "(orca.PythonPluginBase):\n" + body, globals);
    py::object instance = globals[class_name.c_str()]();

    if (!plugin_key.empty()) {
        auto iface = instance.cast<std::shared_ptr<PluginCapabilityInterface>>();
        iface->set_audit_plugin_key(plugin_key);
        iface->set_resolved_identity(capability_name, type);
    }
    return instance;
}

std::shared_ptr<PluginCapabilityInterface> as_interface(const py::object& instance)
{
    return instance.cast<std::shared_ptr<PluginCapabilityInterface>>();
}

PluginConfig& host_config() { return PluginManager::instance().get_config(); }

json py_get_config(const py::object& cap) { return json::parse(cap.attr("get_config")().cast<std::string>()); }

bool py_save_config(const py::object& cap, const json& value) { return cap.attr("save_config")(value.dump()).cast<bool>(); }

PluginCapabilityId capability_id(PluginCapabilityType type, const char* name, const char* plugin_key)
{
    return {type, name, plugin_key};
}

}

TEST_CASE("Capability config API is exposed on every Python capability", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    py::module_ orca = import_orca_module();
    REQUIRE(py::hasattr(orca, "PythonPluginBase"));

    py::object base = orca.attr("PythonPluginBase");
    CHECK(py::hasattr(base, "get_config"));
    CHECK(py::hasattr(base, "save_config"));
    CHECK(py::hasattr(base, "get_config_version"));
    CHECK(py::hasattr(base, "has_config_ui"));
    CHECK(py::hasattr(base, "get_config_ui"));
    CHECK(py::hasattr(base, "get_default_config"));

    CHECK_FALSE(py::hasattr(orca, "config"));
}

TEST_CASE("get_config returns only cap_config and save_config persists it", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-roundtrip");
    host_config().load();

    py::object cap = make_capability("RoundTripCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

    py::object initial = cap.attr("get_config")();
    REQUIRE(py::isinstance<py::str>(initial));
    CHECK(json::parse(initial.cast<std::string>()) == json::object());
    CHECK(cap.attr("get_config_version")().cast<std::string>().empty());

    REQUIRE(py_save_config(cap, json{{"speed", 5}, {"name", "fast"}}));

    const auto stored = host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"));
    REQUIRE(stored);
    CHECK(stored->id == capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"));
    CHECK(stored->config == json{{"speed", 5}, {"name", "fast"}});

    const json reloaded = py_get_config(cap);
    CHECK(reloaded.size() == 2);
    CHECK(reloaded.contains("speed"));
    CHECK_FALSE(reloaded.contains("plugin_key"));
    CHECK_FALSE(reloaded.contains("capability"));
    CHECK_FALSE(reloaded.contains("cap_config"));
    CHECK_FALSE(reloaded.contains("plugin_version"));
}

TEST_CASE("save_config rejects a string that is not valid JSON", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-badjson");
    host_config().load();

    py::object cap = make_capability("BadJsonCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

    REQUIRE(py_save_config(cap, json{{"keep", "me"}}));

    CHECK_FALSE(cap.attr("save_config")("{not json").cast<bool>());
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"keep", "me"}});
}

TEST_CASE("Saving one capability's config does not touch another's", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-isolation");
    host_config().load();

    const std::string body = "    def get_name(self): return 'cap'\n";
    py::object a_cap1 = make_capability("IsoCapA1", body, "plugin_a", "cap_a");
    py::object a_cap2 = make_capability("IsoCapA2", body, "plugin_a", "cap_b");
    py::object b_cap1 = make_capability("IsoCapB1", body, "plugin_b", "cap_a");

    REQUIRE(py_save_config(a_cap1, json{{"value", 1}}));
    REQUIRE(py_save_config(a_cap2, json{{"value", 2}}));
    REQUIRE(py_save_config(b_cap1, json{{"value", 3}}));

    REQUIRE(py_save_config(a_cap1, json{{"value", 99}}));

    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"value", 99}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_b", "plugin_a"))->config == json{{"value", 2}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_b"))->config == json{{"value", 3}});

    py::object importer = make_capability("IsoCapImporter", body, "plugin_a", "cap_a", PluginCapabilityType::Importer);
    REQUIRE(py_save_config(importer, json{{"value", 4}}));
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"value", 99}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Importer, "cap_a", "plugin_a"))->config == json{{"value", 4}});

    host_config().load();
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"value", 99}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Importer, "cap_a", "plugin_a"))->config == json{{"value", 4}});

    CHECK(py_get_config(a_cap2).at("value") == 2);
    CHECK(py_get_config(b_cap1).at("value") == 3);
}

TEST_CASE("Config API refuses a capability the host never materialized", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-unowned");
    host_config().load();

    py::object orphan = make_capability("OrphanCap", "    def get_name(self): return 'cap'\n", "", "");

    CHECK_THROWS(orphan.attr("get_config")());
    CHECK_THROWS(orphan.attr("get_config_version")());
    CHECK_THROWS(orphan.attr("save_config")(json::object().dump()));
}

TEST_CASE("Custom config UI hooks dispatch to the Python override", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    py::object cap = make_capability("CustomUiCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def has_config_ui(self): return True\n"
                                     "    def get_config_ui(self): return '<p>hello</p>'\n",
                                     "plugin_a", "cap_a");

    auto iface = as_interface(cap);
    REQUIRE(iface);
    CHECK(iface->has_config_ui());
    CHECK(iface->get_config_ui() == "<p>hello</p>");
}

TEST_CASE("A capability that omits the config UI hooks gets the default editor", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-bare");
    host_config().load();

    py::object bare = make_capability("BareCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

    auto iface = as_interface(bare);
    REQUIRE(iface);
    CHECK_FALSE(iface->has_config_ui());
    CHECK(iface->get_config_ui().empty());

    REQUIRE(py_save_config(bare, json{{"speed", 5}}));
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"speed", 5}});
}

TEST_CASE("get_default_config supplies the value Restore defaults writes back", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    SECTION("not overridden -> an empty config")
    {
        py::object bare = make_capability("NoDefaultsCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

        auto iface = as_interface(bare);
        REQUIRE(iface);
        CHECK(iface->get_default_config() == json::object());
    }

    SECTION("overridden -> exactly what the plugin returns")
    {
        py::object cap = make_capability("DefaultsCap",
                                         "    def get_name(self): return 'cap_a'\n"
                                         "    def get_default_config(self):\n"
                                         "        return {'speed': 5, 'nested': {'on': True}, 'items': [1, 2]}\n",
                                         "plugin_a", "cap_a");

        auto iface = as_interface(cap);
        REQUIRE(iface);
        CHECK(iface->get_default_config() == json{{"speed", 5}, {"nested", {{"on", true}}}, {"items", {1, 2}}});
    }

    SECTION("overridden but returns None -> an empty config, never a null")
    {
        py::object cap = make_capability("NoneDefaultsCap",
                                         "    def get_name(self): return 'cap_a'\n"
                                         "    def get_default_config(self): pass\n",
                                         "plugin_a", "cap_a");

        auto iface = as_interface(cap);
        REQUIRE(iface);

        const json restored = iface->get_default_config();
        CHECK(restored == json::object());
        CHECK_FALSE(restored.is_null());
    }

    SECTION("overridden but returns a non-object -> an empty config")
    {
        py::object cap = make_capability("ScalarDefaultsCap",
                                         "    def get_name(self): return 'cap_a'\n"
                                         "    def get_default_config(self): return [1, 2, 3]\n",
                                         "plugin_a", "cap_a");

        auto iface = as_interface(cap);
        REQUIRE(iface);
        CHECK(iface->get_default_config() == json::object());
    }
}

TEST_CASE("Restoring defaults overwrites only the target capability", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-restore");
    host_config().load();

    const std::string defaults_body = "    def get_name(self): return 'cap'\n"
                                      "    def get_default_config(self): return {'speed': 1}\n";
    py::object target   = make_capability("RestoreTargetCap", defaults_body, "plugin_a", "cap_a");
    py::object bystander = make_capability("RestoreBystanderCap", defaults_body, "plugin_b", "cap_a");

    const json edited = json{{"speed", 99}};
    REQUIRE(py_save_config(target, edited));
    REQUIRE(py_save_config(bystander, edited));

    auto iface = as_interface(target);
    REQUIRE(host_config().store_capability_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"), iface->get_default_config()));

    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"speed", 1}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_b"))->config == json{{"speed", 99}});
}

TEST_CASE("A raising get_default_config leaves the stored config untouched", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    ScopedDataDir data_dir_guard("plugin-config-py-restore-raise");
    host_config().load();

    py::object cap = make_capability("RaisingDefaultsCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def get_default_config(self): raise RuntimeError('boom')\n",
                                     "plugin_a", "cap_a");

    REQUIRE(py_save_config(cap, json{{"keep", "me"}}));

    auto iface = as_interface(cap);
    REQUIRE(iface);
    CHECK_THROWS_AS(iface->get_default_config(), py::error_already_set);

    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"keep", "me"}});
}

TEST_CASE("A raising config UI hook surfaces as an exception the host can catch", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;

    py::object cap = make_capability("RaisingCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def has_config_ui(self): return True\n"
                                     "    def get_config_ui(self): raise RuntimeError('boom')\n",
                                     "plugin_a", "cap_a");

    auto iface = as_interface(cap);
    REQUIRE(iface);

    CHECK_THROWS_AS(iface->get_config_ui(), py::error_already_set);

    CHECK(iface->get_name() == "cap_a");
}

TEST_CASE("A config UI hook returning the wrong type does not crash the host", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil;
    py::object cap = make_capability("BadTypeCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def has_config_ui(self): return 'not a bool'\n",
                                     "plugin_a", "cap_a");

    auto iface = as_interface(cap);
    REQUIRE(iface);

    try {
        (void) iface->has_config_ui();
    } catch (const std::exception&) {
    }

    CHECK(iface->get_name() == "cap_a");
    CHECK(iface->get_config_ui().empty());
}
