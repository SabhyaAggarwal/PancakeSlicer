#ifndef __ORCA_PRINTER_AGENT_HPP__
#define __ORCA_PRINTER_AGENT_HPP__

#include "IPrinterAgent.hpp"
#include "bambu_networking.hpp"
#include <string>
#include <mutex>
#include <memory>

namespace Slic3r { class ICloudServiceAgent; }

namespace Slic3r {

class OrcaPrinterAgent : public IPrinterAgent {
public:
    explicit OrcaPrinterAgent(std::string log_dir);
    ~OrcaPrinterAgent() override;

    void set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) override;

    int send_message(std::string dev_id, std::string json_str, int qos, int flag) override;
    int connect_printer(const PrinterConnectionParams& params) override;
    int disconnect_printer() override;
    int send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag) override;

    bool start_discovery(bool start, bool sending) override;

    int bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect) override;
    int set_server_callback(OnServerErrFn fn) override;

    std::string get_user_selected_machine() override;
    int set_user_selected_machine(std::string dev_id) override;

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

private:
    std::string log_dir;
    std::string selected_machine;
    std::shared_ptr<ICloudServiceAgent> m_cloud_agent;

    OnMsgArrivedFn on_ssdp_msg_fn;
    OnPrinterConnectedFn on_printer_connected_fn;
    GetSubscribeFailureFn on_subscribe_failure_fn;
    OnMessageFn on_message_fn;
    OnMessageFn on_user_message_fn;
    OnLocalConnectedFn on_local_connect_fn;
    OnMessageFn on_local_message_fn;
    QueueOnMainFn queue_on_main_fn;
    OnServerErrFn on_server_err_fn;

    mutable std::mutex state_mutex;
};

}

#endif
