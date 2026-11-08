#include <catch2/catch_all.hpp>

#include <memory>
#include <slic3r/Utils/NetworkAgentFactory.hpp>

#include <catch2/catch_test_macros.hpp>
#include <pybind11/pytypes.h>
#include <catch2/catch_message.hpp>
#include "python_test_support.hpp"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <string>
#include <pybind11/cast.h>

namespace Slic3r { class ICloudServiceAgent; }
namespace Slic3r { class IPrinterAgent; }

using namespace Slic3r;
namespace py = pybind11;

TEST_CASE("unit: printer-agent registry register / lookup / duplicate-reject", "[registry][unit]")
{
    const std::string id = "orca-test::registry-probe-7f3a";
    auto stub_factory = [](std::shared_ptr<ICloudServiceAgent>, const std::string&)
        -> std::shared_ptr<IPrinterAgent> { return nullptr; };

    REQUIRE_FALSE(NetworkAgentFactory::is_printer_agent_registered(id));

    REQUIRE(NetworkAgentFactory::register_printer_agent(id, "Registry Probe", stub_factory));
    REQUIRE(NetworkAgentFactory::is_printer_agent_registered(id));

    REQUIRE_FALSE(NetworkAgentFactory::register_printer_agent(id, "Impostor", stub_factory));

    const PrinterAgentInfo* info = NetworkAgentFactory::get_printer_agent_info(id);
    REQUIRE(info != nullptr);
    CHECK(info->display_name == "Registry Probe");

    auto agents = NetworkAgentFactory::get_registered_printer_agents();
    int  count  = 0;
    for (const auto& a : agents)
        if (a.id == id)
            ++count;
    CHECK(count == 1);
}

TEST_CASE("integration: orca.printer_agent binding surface", "[integration][Python]")
{
    py::module_ orca = import_orca_module();

    REQUIRE(py::hasattr(orca, "printer_agent"));
    py::object pa = orca.attr("printer_agent");

    REQUIRE(py::hasattr(pa, "PrinterAgentBase"));
    py::object base = pa.attr("PrinterAgentBase");
    for (const char* method : { "get_agent_info", "connect_printer", "disconnect_printer",
                                "send_message", "send_message_to_printer",
                                "command_ams_refresh_rfid", "command_ams_calibrate",
                                "command_ams_select_tray", "command_start_camera",
                                "command_xyz_abs", "command_auto_leveling", "command_go_home",
                                "command_set_bed", "command_set_nozzle", "command_axis_control",
                                "start_discovery", "bind_detect",
                                "start_print", "get_filament_sync_mode" }) {
        CAPTURE(method);
        CHECK(py::hasattr(base, method));
    }

    REQUIRE(py::hasattr(pa, "AgentInfo"));
    py::object info = pa.attr("AgentInfo")("moonraker", "Moonraker", "1.0", "test agent");
    CHECK(info.attr("id").cast<std::string>() == "moonraker");
    CHECK(info.attr("name").cast<std::string>() == "Moonraker");

    REQUIRE(py::hasattr(pa, "FilamentSyncMode"));
    py::object mode = pa.attr("FilamentSyncMode");
    CHECK(py::hasattr(mode, "Pull"));
    CHECK(py::hasattr(mode, "Subscription"));
    CHECK(py::hasattr(mode, "None_"));

    REQUIRE(py::hasattr(pa, "CameraStreamMode"));
    py::object camera_mode = pa.attr("CameraStreamMode");
    CHECK(py::hasattr(camera_mode, "HTTPS"));
    CHECK_FALSE(py::hasattr(camera_mode, "WebRTC"));

    CHECK(py::hasattr(orca, "PluginType"));
}

TEST_CASE("integration: orca plugin-registration API surface + discovery-context guards", "[integration][Python]")
{
    py::module_ orca = import_orca_module();

    for (const char* name : { "plugin", "register_capability", "base", "PythonPluginBase",
                              "PluginType", "PluginResult", "script", "printer_agent", "host" }) {
        CAPTURE(name);
        CHECK(py::hasattr(orca, name));
    }

    CHECK(py::hasattr(orca.attr("base"), "register_capabilities"));
    py::object cap_base = orca.attr("PythonPluginBase");
    for (const char* method : { "get_name", "get_type", "on_load", "on_unload" }) {
        CAPTURE(method);
        CHECK(py::hasattr(cap_base, method));
    }

    CHECK(py::hasattr(orca.attr("script"), "ScriptPluginCapabilityBase"));

    py::object types = orca.attr("PluginType");
    for (const char* value : { "PrinterConnection", "Script" }) {
        CAPTURE(value);
        CHECK(py::hasattr(types, value));
    }

    try {
        orca.attr("register_capability")(py::int_(0));

        FAIL("register_capability outside discovery context must raise");
    } catch (const py::error_already_set& error) {
        CHECK(error.matches(PyExc_ValueError));
    }

    try {
        orca.attr("plugin")(py::int_(0));

        FAIL("@orca.plugin outside discovery context must raise");
    } catch (const py::error_already_set& error) {
        CHECK(error.matches(PyExc_ValueError));
    }
}
