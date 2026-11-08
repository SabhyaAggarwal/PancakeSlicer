#ifdef WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

#include <catch2/catch_test_macros.hpp>
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "libslic3r/ProjectTask.hpp"
#include <vector>
#include <catch2/catch_message.hpp>

#include <catch2/catch_all.hpp>

#include <wx/timer.h>

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevMapping.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"

#include <nlohmann/json.hpp>
#include "slic3r/GUI/DeviceCore/DevFilaSwitch.h"
#include "slic3r/GUI/DeviceCore/DevUtil.h"

using json = nlohmann::json;
using namespace Slic3r;

TEST_CASE("AMS tray placeholder state follows the latest status", "[DevFilaSystem]")
{
    MachineObject obj(nullptr, nullptr, "test", "test_dev", "127.0.0.1");

    const json empty_slot = json::parse(R"({
        "ams": {
            "tray_exist_bits": "0",
            "ams": [ { "id": "0", "info": "00000001", "tray": [
                { "id": "0", "tray_slot_placeholder": "1", "tray_color": "00000000" }
            ] } ]
        }
    })");
    DevFilaSystemParser::ParseV1_0(empty_slot, &obj, obj.GetFilaSystem().get(), false);

    DevAmsTray* tray = obj.GetFilaSystem()->GetAmsTray("0", "0");
    REQUIRE(tray != nullptr);
    REQUIRE(tray->is_slot_placeholder);

    const json loaded_slot = json::parse(R"({
        "ams": {
            "tray_exist_bits": "1",
            "ams": [ { "id": "0", "info": "00000001", "tray": [
                { "id": "0", "tray_color": "FF0000FF" }
            ] } ]
        }
    })");
    DevFilaSystemParser::ParseV1_0(loaded_slot, &obj, obj.GetFilaSystem().get(), false);

    CHECK_FALSE(tray->is_slot_placeholder);
}

TEST_CASE("Switch-bound AMS trays map to the left extruder", "[DevMapping]")
{
    MachineObject obj(nullptr, nullptr, "test", "test_dev", "127.0.0.1");

    obj.GetFilaSwitch()->ParseFilaSwitchInfo(json::parse(R"({"aux":"20000000"})"));
    REQUIRE(obj.GetFilaSwitch()->IsInstalled());

    json print_push = json::parse(R"({
        "ams": {
            "tray_exist_bits": "1",
            "ams": [ {
                "id": "0",
                "info": "00000E01",
                "tray": [ { "id": "0", "tray_color": "FF0000FF" } ]
            } ]
        }
    })");
    DevFilaSystemParser::ParseV1_0(print_push, &obj, obj.GetFilaSystem().get(), false);

    const auto& ams_list = obj.GetFilaSystem()->GetAmsList();
    REQUIRE(ams_list.count("0") == 1);
    REQUIRE(ams_list.at("0")->GetBindedExtruderSet().count(MAIN_EXTRUDER_ID) == 1);
    REQUIRE(ams_list.at("0")->GetBindedExtruderSet().count(DEPUTY_EXTRUDER_ID) == 1);

    DevAmsTray* tray = obj.GetFilaSystem()->GetAmsTray("0", "0");
    REQUIRE(tray != nullptr);
    tray->m_fila_type = "PLA";

    FilamentInfo fila;
    fila.id    = 0;
    fila.type  = "PLA";
    fila.color = "FF0000FF";

    std::vector<FilamentInfo> result;
    std::vector<bool> map_opt(4, false);
    map_opt[MappingOption::USE_LEFT_AMS] = true;

    DevMappingUtil::ams_filament_mapping(&obj, {fila}, result, map_opt, {}, false);

    REQUIRE(result.size() == 1);
    CHECK(result[0].tray_id == 0);
    CHECK(result[0].ams_id == "0");
}

TEST_CASE("Without a switch the binding set equals the single bound extruder", "[DevMapping]")
{
    MachineObject obj(nullptr, nullptr, "test", "test_dev", "127.0.0.1");
    REQUIRE_FALSE(obj.GetFilaSwitch()->IsInstalled());

    json print_push = json::parse(R"({
        "ams": {
            "tray_exist_bits": "111",
            "ams": [
                { "id": "0", "info": "00000001", "tray": [ { "id": "0", "tray_color": "FF0000FF" } ] },
                { "id": "1", "info": "00000101", "tray": [ { "id": "0", "tray_color": "00FF00FF" } ] },
                { "id": "2",                     "tray": [ { "id": "0", "tray_color": "0000FFFF" } ] }
            ]
        }
    })");
    DevFilaSystemParser::ParseV1_0(print_push, &obj, obj.GetFilaSystem().get(), false);

    const auto& ams_list = obj.GetFilaSystem()->GetAmsList();
    REQUIRE(ams_list.count("0") == 1);
    REQUIRE(ams_list.count("1") == 1);
    REQUIRE(ams_list.count("2") == 1);
    for (const char* id : {"0", "1", "2"}) {
        const auto& ams = ams_list.at(id);
        INFO("ams " << id);
        REQUIRE(ams->GetBindedExtruderSet().size() == 1);
        REQUIRE(ams->GetBindedExtruderSet().count(ams->GetExtruderId()) == 1);
    }
    REQUIRE(ams_list.at("0")->GetExtruderId() == MAIN_EXTRUDER_ID);
    REQUIRE(ams_list.at("1")->GetExtruderId() == DEPUTY_EXTRUDER_ID);
    REQUIRE(ams_list.at("2")->GetExtruderId() == MAIN_EXTRUDER_ID);

    for (const char* id : {"0", "1", "2"}) {
        DevAmsTray* tray = obj.GetFilaSystem()->GetAmsTray(id, "0");
        REQUIRE(tray != nullptr);
        tray->m_fila_type = "PLA";
    }

    FilamentInfo fila;
    fila.id    = 0;
    fila.type  = "PLA";
    fila.color = "FF0000FF";

    std::vector<FilamentInfo> result;
    std::vector<bool> map_opt(4, false);
    map_opt[MappingOption::USE_LEFT_AMS] = true;
    DevMappingUtil::ams_filament_mapping(&obj, {fila}, result, map_opt, {}, false);
    REQUIRE(result.size() == 1);
    CHECK(result[0].ams_id != "0");
    CHECK(result[0].ams_id != "2");

    result.clear();
    map_opt[MappingOption::USE_LEFT_AMS]  = false;
    map_opt[MappingOption::USE_RIGHT_AMS] = true;
    DevMappingUtil::ams_filament_mapping(&obj, {fila}, result, map_opt, {}, false);
    REQUIRE(result.size() == 1);
    CHECK(result[0].ams_id == "0");
}

TEST_CASE("Switch-bound AMS with an invalid track is excluded from mapping", "[DevMapping]")
{
    MachineObject obj(nullptr, nullptr, "test", "test_dev", "127.0.0.1");
    obj.GetFilaSwitch()->ParseFilaSwitchInfo(json::parse(R"({"aux":"20000000"})"));
    REQUIRE(obj.GetFilaSwitch()->IsInstalled());

    json print_push = json::parse(R"({
        "ams": {
            "tray_exist_bits": "1",
            "ams": [ { "id": "0", "info": "0F000E01", "tray": [ { "id": "0", "tray_color": "FF0000FF" } ] } ]
        }
    })");
    DevFilaSystemParser::ParseV1_0(print_push, &obj, obj.GetFilaSystem().get(), false);

    const auto& ams_list = obj.GetFilaSystem()->GetAmsList();
    REQUIRE(ams_list.count("0") == 1);
    REQUIRE(ams_list.at("0")->GetBindedExtruderSet().empty());
    REQUIRE_FALSE(ams_list.at("0")->GetSwitcherPos().has_value());
    REQUIRE_FALSE(obj.GetFilaSwitch()->IsReady());

    DevAmsTray* tray = obj.GetFilaSystem()->GetAmsTray("0", "0");
    REQUIRE(tray != nullptr);
    tray->m_fila_type = "PLA";

    FilamentInfo fila;
    fila.id    = 0;
    fila.type  = "PLA";
    fila.color = "FF0000FF";

    std::vector<FilamentInfo> result;
    std::vector<bool> map_opt(4, false);
    map_opt[MappingOption::USE_LEFT_AMS]  = true;
    map_opt[MappingOption::USE_RIGHT_AMS] = true;
    DevMappingUtil::ams_filament_mapping(&obj, {fila}, result, map_opt, {}, false);
    REQUIRE(result.size() == 1);
    CHECK(result[0].tray_id == -1);

    MachineObject obj_no_switch(nullptr, nullptr, "test", "test_dev", "127.0.0.1");
    REQUIRE_FALSE(obj_no_switch.GetFilaSwitch()->IsInstalled());
    DevFilaSystemParser::ParseV1_0(print_push, &obj_no_switch, obj_no_switch.GetFilaSystem().get(), false);
    REQUIRE(obj_no_switch.GetFilaSystem()->GetAmsList().count("0") == 0);
}
