#ifndef __BBL_PRINTER_AGENT_HPP__
#define __BBL_PRINTER_AGENT_HPP__

#include "IPrinterAgent.hpp"
#include "bambu_networking.hpp"
#include <functional>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace Slic3r { class ICloudServiceAgent; }

namespace Slic3r {

class BBLPrinterAgent : public IPrinterAgent {
public:
    BBLPrinterAgent();
    ~BBLPrinterAgent() override;

    void set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) override;

    int send_message(std::string dev_id, std::string json_str, int qos, int flag) override;
    int command_ams_refresh_rfid(std::string dev_id, int ams_id, int slot_id, int sequence_id, bool lan_mode) override;
    int command_ams_calibrate(std::string dev_id, int ams_id, int sequence_id, bool lan_mode) override;
    int command_ams_select_tray(std::string dev_id, std::string tray_id, int sequence_id, bool lan_mode) override;
    int command_axis_control(std::string dev_id, std::string axis, double unit, double input_val, int speed,
                              bool is_core_xy, bool supports_mqtt_axis_control, int sequence_id, bool lan_mode) override;
    std::string default_lan_username() const override { return "bblp"; }
    int connect_printer(const PrinterConnectionParams& params) override;
    int disconnect_printer() override;
    int send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag) override;

    int check_cert() override;
    void install_device_cert(std::string dev_id, bool lan_only) override;

    bool start_discovery(bool start, bool sending) override;

    int ping_bind(std::string ping_code) override;
    int bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect) override;
    int bind(std::string dev_ip, std::string dev_id, std::string dev_model, std::string sec_link, std::string timezone, bool improved, OnUpdateStatusFn update_fn) override;
    int unbind(std::string dev_id) override;
    int request_bind_ticket(std::string* ticket) override;
    int get_hms_snapshot(std::string dev_id, std::string file_name, std::function<void(std::string, int)> callback) override;
    int set_server_callback(OnServerErrFn fn) override;

    std::string get_user_selected_machine() override;
    int set_user_selected_machine(std::string dev_id) override;

    int start_subscribe(std::string module) override;
    int stop_subscribe(std::string module) override;
    int add_subscribe(std::vector<std::string> dev_list) override;
    int del_subscribe(std::vector<std::string> dev_list) override;

     static AgentInfo get_agent_info_static();
     AgentInfo get_agent_info() override { return get_agent_info_static(); }

    int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;
    int start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;

    int set_on_ssdp_msg_fn(OnMsgArrivedFn fn) override;
    int set_on_printer_connected_fn(OnPrinterConnectedFn fn) override;
    int set_on_subscribe_failure_fn(GetSubscribeFailureFn fn) override;
    int set_on_message_fn(OnMessageFn fn) override;
    int set_on_user_message_fn(OnMessageFn fn) override;
    int set_on_local_connect_fn(OnLocalConnectedFn fn) override;
    int set_on_local_message_fn(OnMessageFn fn) override;
    int set_queue_on_main_fn(QueueOnMainFn fn) override;
    FilamentSyncMode get_filament_sync_mode() const override;

    std::string to_orca_filament_id(const std::string& printer_filament_id) const override;
    std::string from_orca_filament_id(const std::string& orca_filament_id) const override;

    static std::string to_orca_payload(std::string json_text);
    static std::string from_orca_payload(std::string json_text);

private:
    int publish(const std::string& dev_id, const nlohmann::json& j, bool lan_mode);
};

}

#endif
