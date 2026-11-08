#include "BBLPrinterAgent.hpp"
#include "BBLNetworkPlugin.hpp"
#include "IPrinterAgent.hpp"
#include "NetworkAgentFactory.hpp"
#include "bambu_networking.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <exception>
#include <functional>
#include <nlohmann/json.hpp>
#include <cmath>
#include <slic3r/GUI/DeviceManager.hpp>
#include <string>
#include <utility>
#include <vector>
using json = nlohmann::json;

#include <type_traits>
#include <unordered_map>
#include <memory>

namespace Slic3r { class ICloudServiceAgent; }

namespace Slic3r {

namespace {

struct BambuFilamentIdMap { std::unordered_map<std::string, std::string> to_bambu, to_orca; };

const BambuFilamentIdMap& bambu_filament_id_map()
{
    static const BambuFilamentIdMap map = [] {
        BambuFilamentIdMap m;
        const std::string path = resources_dir() + "/printers/bambu_filament_ids.json";
        try {
            boost::nowide::ifstream file(path);
            if (!file.is_open()) {
                BOOST_LOG_TRIVIAL(warning) << "Bambu filament id map not found, ids pass through untranslated: " << path;
                return m;
            }
            json doc;
            file >> doc;
            for (const auto& [orca_filament_id, row] : doc.at("filaments").items()) {
                const std::string bambu_id = row.at("bambu_id").get<std::string>();
                m.to_bambu.emplace(orca_filament_id, bambu_id);
                m.to_orca.emplace(bambu_id, orca_filament_id);
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Bambu filament id map unreadable, ids pass through untranslated: " << e.what();
            m = {};
        }
        return m;
    }();
    return map;
}

void rewrite_filament_ids(json& j, const std::unordered_map<std::string, std::string>& map)
{
    if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            if (value.is_string() && (key == "tray_info_idx" || key == "filament_id" || key == "filamentId")) {
                auto it = map.find(value.get_ref<const std::string&>());
                if (it != map.end())
                    value = it->second;
            } else
                rewrite_filament_ids(value, map);
        }
    } else if (j.is_array())
        for (auto& element : j)
            rewrite_filament_ids(element, map);
}

std::string rewrite_filament_ids(std::string text, const std::unordered_map<std::string, std::string>& map)
{
    if (map.empty() || (text.find("tray_info_idx") == std::string::npos && text.find("filament_id") == std::string::npos &&
                        text.find("filamentId") == std::string::npos))
        return text;
    try {
        json j = json::parse(text);
        rewrite_filament_ids(j, map);
        return j.dump();
    } catch (const std::exception&) {
        return text;
    }
}

OnMessageFn to_orca_messages(OnMessageFn fn)
{
    if (!fn)
        return fn;
    return [fn = std::move(fn)](std::string dev_id, std::string msg) { fn(std::move(dev_id), BBLPrinterAgent::to_orca_payload(std::move(msg))); };
}

template <typename To, typename From>
To as_abi(From fn)
{
    static_assert(std::is_function_v<std::remove_pointer_t<From>>, "as_abi retypes a function pointer");
    return reinterpret_cast<To>(reinterpret_cast<void (*)()>(fn));
}

}

std::string BBLPrinterAgent::to_orca_filament_id(const std::string& printer_filament_id) const
{
    const auto& map = bambu_filament_id_map().to_orca;
    auto it = map.find(printer_filament_id);
    return it != map.end() ? it->second : printer_filament_id;
}

std::string BBLPrinterAgent::from_orca_filament_id(const std::string& orca_filament_id) const
{
    const auto& map = bambu_filament_id_map().to_bambu;
    auto it = map.find(orca_filament_id);
    return it != map.end() ? it->second : orca_filament_id;
}

std::string BBLPrinterAgent::to_orca_payload(std::string json_text)
{
    return rewrite_filament_ids(std::move(json_text), bambu_filament_id_map().to_orca);
}

std::string BBLPrinterAgent::from_orca_payload(std::string json_text)
{
    return rewrite_filament_ids(std::move(json_text), bambu_filament_id_map().to_bambu);
}

BBLPrinterAgent::BBLPrinterAgent() = default;

BBLPrinterAgent::~BBLPrinterAgent() = default;

void BBLPrinterAgent::set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud)
{
    (void) cloud;
}

int BBLPrinterAgent::command_ams_refresh_rfid(std::string dev_id, int ams_id, int slot_id, int sequence_id, bool lan_mode)
{
    nlohmann::json j;
    if (ams_id == -1) {
        const std::string gcode   = (boost::format("M620 R%1% \n") % slot_id).str();
        j["print"]["command"]     = "gcode_line";
        j["print"]["param"]       = gcode;
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        return publish(dev_id, j, lan_mode);
    }

    j["print"]["command"]     = "ams_get_rfid";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["ams_id"]      = ams_id;
    j["print"]["slot_id"]     = slot_id;
    return publish(dev_id, j, lan_mode);
}

int BBLPrinterAgent::command_ams_calibrate(std::string dev_id, int ams_id, int sequence_id, bool lan_mode)
{
    const std::string gcode = (boost::format("M620 C%1% \n") % ams_id).str();
    nlohmann::json j;
    j["print"]["command"] = "gcode_line";
    j["print"]["param"] = gcode;
    j["print"]["sequence_id"] = std::to_string(sequence_id);
    return publish(dev_id, j, lan_mode);
}

int BBLPrinterAgent::command_ams_select_tray(std::string dev_id, std::string tray_id, int sequence_id, bool lan_mode)
{
    const std::string gcode = (boost::format("M620 P%1% \n") % tray_id).str();
    nlohmann::json j;
    j["print"]["command"] = "gcode_line";
    j["print"]["param"] = gcode;
    j["print"]["sequence_id"] = std::to_string(sequence_id);
    return publish(dev_id, j, lan_mode);
}

int BBLPrinterAgent::command_axis_control(std::string dev_id, std::string axis, double unit, double input_val, int speed,
                                           bool is_core_xy, bool supports_mqtt_axis_control, int sequence_id, bool lan_mode)
{
    nlohmann::json j;
    j["print"]["sequence_id"] = std::to_string(sequence_id);

    if (supports_mqtt_axis_control) {
        int dir = input_val > 0 ? 1 : -1;
        if (!is_core_xy && (axis == "Y" || axis == "Z"))
            dir = -dir;

        j["print"]["command"] = "xyz_ctrl";
        j["print"]["axis"] = axis;
        j["print"]["dir"] = dir;
        j["print"]["mode"] = (std::abs(input_val) >= 10) ? 1 : 0;
        return publish(dev_id, j, lan_mode);
    }

    double value = input_val;
    if (!is_core_xy && (axis == "Y" || axis == "Z"))
        value = -input_val;

    std::string value_str = (boost::format("%.1f") % (value * unit)).str();
    std::string gcode;
    if (axis == "X" || axis == "Y" || axis == "Z") {
        gcode = (boost::format("M211 S \nM211 X1 Y1 Z1\nM1002 push_ref_mode\nG91 \nG1 %1%%2% F%3%\nM1002 pop_ref_mode\nM211 R\n")
                 % axis % value_str % speed).str();
    } else if (axis == "E") {
        gcode = (boost::format("M83 \nG0 %1%%2% F%3%\n") % axis % value_str % speed).str();
    } else {
        return -1;
    }

    j["print"]["command"] = "gcode_line";
    j["print"]["param"] = gcode;
    return publish(dev_id, j, lan_mode);
}

int BBLPrinterAgent::publish(const std::string& dev_id, const nlohmann::json& j, bool lan_mode)
{
    const int rtn = lan_mode ? send_message_to_printer(dev_id, j.dump(), 0, 0) : send_message(dev_id, j.dump(), 0, 0);
    if (rtn == 0)
        BOOST_LOG_TRIVIAL(info) << "publish_json: " << j.dump() << " code: " << rtn;
    else
        BOOST_LOG_TRIVIAL(error) << "publish_json: " << j.dump() << " code: " << rtn;
    return rtn;
}

int BBLPrinterAgent::send_message(std::string dev_id, std::string json_str, int qos, int flag)
{
    json_str = from_orca_payload(std::move(json_str));
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_send_message();
    if (func && agent) {
        switch (plugin.network_abi()) {
        case NetworkAbi::Legacy: {
            auto legacy_func = as_abi<func_send_message_legacy>(func);
            return legacy_func(agent, std::move(dev_id), std::move(json_str), qos);
        }
        case NetworkAbi::V0203:
        case NetworkAbi::Current:
            return func(agent, std::move(dev_id), std::move(json_str), qos, flag);
        default:
            return -1;
        }
    }
    return -1;
}

int BBLPrinterAgent::connect_printer(const PrinterConnectionParams& params)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_connect_printer();
#if !BBL_RELEASE_TO_PUBLIC
    const bool use_ssl_for_mqtt = GUI::wxGetApp().app_config &&
                                  GUI::wxGetApp().app_config->get_bool("enable_ssl_for_mqtt");
#else
    bool use_ssl_for_mqtt = true;
    if (auto* dev_manager = GUI::wxGetApp().getDeviceManager()) {
        if (auto* machine = dev_manager->get_my_machine(params.dev_id))
            use_ssl_for_mqtt = machine->local_use_ssl;
    }
#endif
    if (func && agent) {
        return func(agent, params.dev_id, params.host, params.username, params.password, use_ssl_for_mqtt);
    }
    return -1;
}

int BBLPrinterAgent::disconnect_printer()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_disconnect_printer();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

int BBLPrinterAgent::send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag)
{
    json_str = from_orca_payload(std::move(json_str));
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_send_message_to_printer();
    if (func && agent) {
        switch (plugin.network_abi()) {
        case NetworkAbi::Legacy: {
            auto legacy_func = as_abi<func_send_message_to_printer_legacy>(func);
            return legacy_func(agent, std::move(dev_id), std::move(json_str), qos);
        }
        case NetworkAbi::V0203:
        case NetworkAbi::Current:
            return func(agent, std::move(dev_id), std::move(json_str), qos, flag);
        default:
            return -1;
        }
    }
    return -1;
}

int BBLPrinterAgent::check_cert()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_check_cert();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

void BBLPrinterAgent::install_device_cert(std::string dev_id, bool lan_only)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_install_device_cert();
    if (func && agent) {
        func(agent, dev_id, lan_only);
    }
}

bool BBLPrinterAgent::start_discovery(bool start, bool sending)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_start_discovery();
    if (func && agent) {
        return func(agent, start, sending);
    }
    return false;
}

int BBLPrinterAgent::ping_bind(std::string ping_code)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_ping_bind();
    if (func && agent) {
        return func(agent, ping_code);
    }
    return -1;
}

int BBLPrinterAgent::bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_bind_detect();
    if (func && agent) {
        return func(agent, dev_ip, sec_link, detect);
    }
    return -1;
}

int BBLPrinterAgent::bind(std::string dev_ip, std::string dev_id, std::string dev_model, std::string sec_link, std::string timezone, bool improved, OnUpdateStatusFn update_fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_bind();
    if (func && agent) {
        switch (plugin.network_abi()) {
        case NetworkAbi::Legacy:
        case NetworkAbi::V0203: {
            auto older_func = as_abi<func_bind_pre0208>(func);
            return older_func(agent, dev_ip, dev_id, sec_link, timezone, improved, update_fn);
        }
        case NetworkAbi::Current:
            return func(agent, dev_ip, dev_id, dev_model, sec_link, timezone, improved, update_fn);
        default:
            return -1;
        }
    }
    return -1;
}

int BBLPrinterAgent::unbind(std::string dev_id)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_unbind();
    if (func && agent) {
        return func(agent, dev_id);
    }
    return -1;
}

int BBLPrinterAgent::request_bind_ticket(std::string* ticket)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_request_bind_ticket();
    if (func && agent) {
        return func(agent, ticket);
    }
    return -1;
}

int BBLPrinterAgent::get_hms_snapshot(std::string dev_id, std::string file_name, std::function<void(std::string, int)> callback)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_hms_snapshot();
    if (func && agent) {
        return func(agent, dev_id, file_name, callback);
    }
    return -1;
}

int BBLPrinterAgent::set_server_callback(OnServerErrFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_server_callback();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

std::string BBLPrinterAgent::get_user_selected_machine()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_selected_machine();
    if (func && agent) {
        return func(agent);
    }
    return "";
}

int BBLPrinterAgent::set_user_selected_machine(std::string dev_id)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_user_selected_machine();
    if (func && agent) {
        return func(agent, dev_id);
    }
    return -1;
}

int BBLPrinterAgent::start_subscribe(std::string module)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_start_subscribe();
    if (func && agent) {
        return func(agent, module);
    }
    return -1;
}

int BBLPrinterAgent::stop_subscribe(std::string module)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_stop_subscribe();
    if (func && agent) {
        return func(agent, module);
    }
    return -1;
}

int BBLPrinterAgent::add_subscribe(std::vector<std::string> dev_list)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_add_subscribe();
    if (func && agent) {
        return func(agent, dev_list);
    }
    return -1;
}

int BBLPrinterAgent::del_subscribe(std::vector<std::string> dev_list)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_del_subscribe();
    if (func && agent) {
        return func(agent, dev_list);
    }
    return -1;
}

AgentInfo BBLPrinterAgent::get_agent_info_static()
{
    return AgentInfo{BBL_PRINTER_AGENT_ID, "Bambu Lab", "", "Bambu Lab printer agent"};
}

namespace {

template <typename LegacyFn, typename Fn0203, typename CurrentFn, typename... CallbackFns>
int dispatch_start(CurrentFn func, PrintParams& params, const CallbackFns&... callbacks)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    if (!func || !agent)
        return -1;
    params.ams_mapping_info = BBLPrinterAgent::from_orca_payload(std::move(params.ams_mapping_info));
    switch (plugin.network_abi()) {
    case NetworkAbi::Legacy:
        return as_abi<LegacyFn>(func)(agent, BBLNetworkPlugin::as_legacy(params), callbacks...);
    case NetworkAbi::V0203:
        return as_abi<Fn0203>(func)(agent, BBLNetworkPlugin::as_0203(params), callbacks...);
    case NetworkAbi::Current:
        return func(agent, std::move(params), callbacks...);
    default:
        return -1;
    }
}

}

int BBLPrinterAgent::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    return dispatch_start<func_start_print_legacy, func_start_print_0203>(
        BBLNetworkPlugin::instance().get_start_print(), params, update_fn, cancel_fn, wait_fn);
}

int BBLPrinterAgent::start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    return dispatch_start<func_start_local_print_with_record_legacy, func_start_local_print_with_record_0203>(
        BBLNetworkPlugin::instance().get_start_local_print_with_record(), params, update_fn, cancel_fn, wait_fn);
}

int BBLPrinterAgent::start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    return dispatch_start<func_start_send_gcode_to_sdcard_legacy, func_start_send_gcode_to_sdcard_0203>(
        BBLNetworkPlugin::instance().get_start_send_gcode_to_sdcard(), params, update_fn, cancel_fn, wait_fn);
}

int BBLPrinterAgent::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    return dispatch_start<func_start_local_print_legacy, func_start_local_print_0203>(
        BBLNetworkPlugin::instance().get_start_local_print(), params, update_fn, cancel_fn);
}

int BBLPrinterAgent::start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    return dispatch_start<func_start_sdcard_print_legacy, func_start_sdcard_print_0203>(
        BBLNetworkPlugin::instance().get_start_sdcard_print(), params, update_fn, cancel_fn);
}

int BBLPrinterAgent::set_on_ssdp_msg_fn(OnMsgArrivedFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_ssdp_msg_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

int BBLPrinterAgent::set_on_printer_connected_fn(OnPrinterConnectedFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_printer_connected_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

int BBLPrinterAgent::set_on_subscribe_failure_fn(GetSubscribeFailureFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_subscribe_failure_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

int BBLPrinterAgent::set_on_message_fn(OnMessageFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_message_fn();
    if (func && agent) {
        return func(agent, to_orca_messages(std::move(fn)));
    }
    return -1;
}

int BBLPrinterAgent::set_on_user_message_fn(OnMessageFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_user_message_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

int BBLPrinterAgent::set_on_local_connect_fn(OnLocalConnectedFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_local_connect_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

int BBLPrinterAgent::set_on_local_message_fn(OnMessageFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_local_message_fn();
    if (func && agent) {
        return func(agent, to_orca_messages(std::move(fn)));
    }
    return -1;
}

int BBLPrinterAgent::set_queue_on_main_fn(QueueOnMainFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_queue_on_main_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

FilamentSyncMode BBLPrinterAgent::get_filament_sync_mode() const
{
    return FilamentSyncMode::subscription;
}

}
