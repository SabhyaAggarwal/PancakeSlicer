#ifndef __I_PRINTER_AGENT_HPP__
#define __I_PRINTER_AGENT_HPP__

#include "bambu_networking.hpp"
#define ORCA_NETWORK_ERR_CMD_NOT_SUPPORTED -7010
#define ORCA_NETWORK_ERR_CAP_NOT_AVAILABLE -7020
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>
#include <nlohmann/json.hpp>
#include <boost/format.hpp>

namespace Slic3r {

class ICloudServiceAgent;

struct AgentInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
};

struct PrinterConnectionParams
{
    std::string dev_id;
    std::string host;
    std::string port;
    std::string username;
    std::string password;
    bool use_ssl = false;
    std::string ca_file;
};

enum class FilamentSyncMode {
    none = 0,
    subscription,
    pull
};

enum class CameraStreamMode {
    none = 0,
    http,
    https,
    rtsp,
    webrtc,
    http_snapshot
};

class IPrinterAgent {
public:
    virtual ~IPrinterAgent() = default;

    virtual void set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) = 0;

    virtual int send_message(std::string dev_id, std::string json_str, int qos, int flag) = 0;

    virtual int command_ams_refresh_rfid(std::string, int, int, int, bool)
    { return ORCA_NETWORK_ERR_CMD_NOT_SUPPORTED; }
    virtual int command_ams_calibrate(std::string, int, int, bool)
    { return ORCA_NETWORK_ERR_CMD_NOT_SUPPORTED; }
    virtual int command_ams_select_tray(std::string, std::string, int, bool)
    { return ORCA_NETWORK_ERR_CMD_NOT_SUPPORTED; }
    virtual int command_start_camera(std::string)
    { return ORCA_NETWORK_ERR_CMD_NOT_SUPPORTED; }

private:
    int publish_command_json(std::string dev_id, const nlohmann::json& j, bool lan_mode)
    {
        return lan_mode ? send_message_to_printer(dev_id, j.dump(), 0, 0)
                         : send_message(dev_id, j.dump(), 0, 0);
    }

public:
    virtual int command_xyz_abs(std::string dev_id, int sequence_id, bool lan_mode)
    {
        nlohmann::json j;
        j["print"]["command"]     = "gcode_line";
        j["print"]["param"]       = "G90 \n";
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        return publish_command_json(dev_id, j, lan_mode);
    }
    virtual int command_auto_leveling(std::string dev_id, int sequence_id, bool lan_mode)
    {
        nlohmann::json j;
        j["print"]["command"]     = "gcode_line";
        j["print"]["param"]       = "G29 \n";
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        return publish_command_json(dev_id, j, lan_mode);
    }
    virtual int command_go_home(std::string dev_id, bool is_printing, bool supports_mqtt_homing, int sequence_id, bool lan_mode)
    {
        nlohmann::json j;
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        if (supports_mqtt_homing) {
            j["print"]["command"] = "back_to_center";
        } else {
            j["print"]["command"] = "gcode_line";
            j["print"]["param"]   = is_printing ? "G28 X\n" : "G28 \n";
        }
        return publish_command_json(dev_id, j, lan_mode);
    }
    virtual int command_set_bed(std::string dev_id, int temp, bool supports_mqtt_bed_ctrl, int sequence_id, bool lan_mode)
    {
        nlohmann::json j;
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        if (supports_mqtt_bed_ctrl) {
            j["print"]["command"] = "set_bed_temp";
            j["print"]["temp"]    = temp;
        } else {
            j["print"]["command"] = "gcode_line";
            j["print"]["param"]   = (boost::format("M140 S%1%\n") % temp).str();
        }
        return publish_command_json(dev_id, j, lan_mode);
    }
    virtual int command_set_nozzle(std::string dev_id, int temp, int sequence_id, bool lan_mode)
    {
        nlohmann::json j;
        j["print"]["command"]     = "gcode_line";
        j["print"]["param"]       = (boost::format("M104 S%1%\n") % temp).str();
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        return publish_command_json(dev_id, j, lan_mode);
    }

    virtual int command_axis_control(std::string dev_id, std::string axis, double unit, double input_val, int speed,
                                      bool is_core_xy, bool supports_mqtt_axis_control, int sequence_id, bool lan_mode)
    {
        (void) supports_mqtt_axis_control;

        double value = input_val;
        if (!is_core_xy && (axis == "Y" || axis == "Z"))
            value = -input_val;

        std::string value_str = (boost::format("%.1f") % (value * unit)).str();
        std::string gcode;
        if (axis == "X" || axis == "Y" || axis == "Z") {
            gcode = (boost::format("G91 \nG1 %1%%2% F%3%\nG90 \n") % axis % value_str % speed).str();
        } else if (axis == "E") {
            gcode = (boost::format("M83 \nG0 %1%%2% F%3%\n") % axis % value_str % speed).str();
        } else {
            return -1;
        }

        nlohmann::json j;
        j["print"]["command"]     = "gcode_line";
        j["print"]["param"]       = gcode;
        j["print"]["sequence_id"] = std::to_string(sequence_id);
        return publish_command_json(dev_id, j, lan_mode);
    }

    virtual std::string default_lan_username() const { return {}; }

    virtual int connect_printer(const PrinterConnectionParams& params) = 0;

    virtual int disconnect_printer() = 0;

    virtual int send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag) = 0;

    virtual int check_cert() { return BAMBU_NETWORK_SUCCESS; }

    virtual void install_device_cert(std::string dev_id, bool lan_only)
    {
        (void) dev_id;
        (void) lan_only;
    }

    virtual bool start_discovery(bool start, bool sending) = 0;

    virtual int ping_bind(std::string ping_code)
    {
        (void) ping_code;
        return BAMBU_NETWORK_SUCCESS;
    }

    virtual int bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect) = 0;

    virtual int bind(std::string dev_ip, std::string dev_id, std::string dev_model, std::string sec_link,
                     std::string timezone, bool improved, OnUpdateStatusFn update_fn)
    {
        (void) dev_ip;
        (void) dev_id;
        (void) dev_model;
        (void) sec_link;
        (void) timezone;
        (void) improved;
        (void) update_fn;
        return BAMBU_NETWORK_SUCCESS;
    }

    virtual int unbind(std::string dev_id)
    {
        (void) dev_id;
        return BAMBU_NETWORK_SUCCESS;
    }

    virtual int request_bind_ticket(std::string* ticket)
    {
        if (ticket)
            *ticket = {};
        return BAMBU_NETWORK_SUCCESS;
    }

    virtual int get_hms_snapshot(std::string dev_id, std::string file_name,
                                 std::function<void(std::string, int)> callback)
    {
        (void) dev_id;
        (void) file_name;
        (void) callback;
        return -1;
    }

    virtual int set_server_callback(OnServerErrFn fn) = 0;

    virtual std::string get_user_selected_machine() = 0;

    virtual int set_user_selected_machine(std::string dev_id) = 0;

    virtual int start_subscribe(std::string module) { (void) module; return BAMBU_NETWORK_SUCCESS; }

    virtual int stop_subscribe(std::string module) { (void) module; return BAMBU_NETWORK_SUCCESS; }

    virtual int add_subscribe(std::vector<std::string> dev_list) { (void) dev_list; return BAMBU_NETWORK_SUCCESS; }

    virtual int del_subscribe(std::vector<std::string> dev_list) { (void) dev_list; return BAMBU_NETWORK_SUCCESS; }

    virtual int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) = 0;

    virtual int start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) = 0;

    virtual int start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) = 0;

    virtual int start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) = 0;

    virtual int start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) = 0;

    virtual int set_on_ssdp_msg_fn(OnMsgArrivedFn fn) = 0;

    virtual int set_on_printer_connected_fn(OnPrinterConnectedFn fn) = 0;

    virtual int set_on_subscribe_failure_fn(GetSubscribeFailureFn fn) = 0;

    virtual int set_on_message_fn(OnMessageFn fn) = 0;

    virtual int set_on_user_message_fn(OnMessageFn fn) = 0;

    virtual int set_on_local_connect_fn(OnLocalConnectedFn fn) = 0;

    virtual int set_on_local_message_fn(OnMessageFn fn) = 0;

    virtual int set_queue_on_main_fn(QueueOnMainFn fn) = 0;

    virtual AgentInfo get_agent_info() = 0;

    virtual FilamentSyncMode get_filament_sync_mode() const { return FilamentSyncMode::none; }

    virtual CameraStreamMode get_camera_stream_mode() const { return CameraStreamMode::none; }

    virtual bool fetch_filament_info(std::string dev_id, FilamentSyncMode sync_mode = FilamentSyncMode::pull) { return false; }

    virtual std::string to_orca_filament_id(const std::string& printer_filament_id) const { return printer_filament_id; }
    virtual std::string from_orca_filament_id(const std::string& orca_filament_id) const { return orca_filament_id; }

    virtual std::string get_camera_url() const { return {}; }
};

}

#endif
