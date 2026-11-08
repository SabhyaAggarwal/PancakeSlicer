#ifndef __I_CLOUD_SERVICE_AGENT_HPP__
#define __I_CLOUD_SERVICE_AGENT_HPP__

#include "ICameraSignalingChannel.hpp"
#include "bambu_networking.hpp"
#include "CloudProvider.hpp"
#include "../../libslic3r/ProjectTask.hpp"
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>

namespace Slic3r {

struct CloudEvent {
    std::string provider;
};

using AppOnServerConnectedFn = std::function<void(CloudEvent event, int return_code, int reason_code)>;
using AppOnHttpErrorFn = std::function<void(CloudEvent event, unsigned http_code, std::string http_body)>;

class ICloudServiceAgent {
public:
    virtual ~ICloudServiceAgent() = default;

    virtual int init_log() = 0;

    virtual int set_config_dir(std::string config_dir) = 0;

    virtual int set_cert_file(std::string folder, std::string filename) = 0;

    virtual int set_country_code(std::string country_code) = 0;

    virtual std::string get_id() const = 0;
    virtual int start() = 0;

    virtual int change_user(std::string user_info) = 0;

    virtual bool is_user_login() = 0;

    virtual int user_logout(bool request = false) = 0;

    virtual std::string get_user_id() = 0;

    virtual std::string get_user_name() = 0;

    virtual std::string get_user_avatar() = 0;

    virtual std::string get_user_nickname() = 0;

    virtual std::string build_login_cmd() = 0;

    virtual std::string build_logout_cmd() = 0;

    virtual std::string build_login_info() = 0;

    virtual std::string get_access_token() const = 0;

    virtual std::string get_refresh_token() const = 0;

    virtual bool ensure_token_fresh(const std::string& reason) = 0;

    virtual std::string get_cloud_service_host() = 0;

    virtual std::string get_cloud_login_url(const std::string& language = "") = 0;

    virtual int connect_server() = 0;

    virtual bool is_server_connected() = 0;

    virtual int refresh_connection() = 0;

    virtual int start_subscribe(std::string module) = 0;

    virtual int stop_subscribe(std::string module) = 0;

    virtual int add_subscribe(std::vector<std::string> dev_list) = 0;

    virtual int del_subscribe(std::vector<std::string> dev_list) = 0;

    virtual void enable_multi_machine(bool enable) = 0;

    virtual int get_user_presets(std::map<std::string, std::map<std::string, std::string>>* user_presets) = 0;

    virtual std::string request_setting_id(std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code) = 0;

    virtual int put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code, bool force = false) = 0;

    virtual int get_setting_list(std::string bundle_version, ProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr) = 0;

    virtual int get_setting_list2(std::string bundle_version, CheckFn chk_fn, ProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr) = 0;

    virtual int delete_setting(std::string setting_id) = 0;

    virtual int get_my_message(int type, int after, int limit, unsigned int* http_code, std::string* http_body) = 0;

    virtual int check_user_task_report(int* task_id, bool* printable) = 0;

    virtual int get_user_print_info(unsigned int* http_code, std::string* http_body) = 0;

    virtual int get_user_tasks(TaskQueryParams params, std::string* http_body) = 0;

    virtual int get_printer_firmware(std::string dev_id, unsigned* http_code, std::string* http_body) = 0;

    virtual int get_task_plate_index(std::string task_id, int* plate_index) = 0;

    virtual int get_user_info(int* identifier) = 0;

    virtual int get_subtask_info(std::string subtask_id, std::string* task_json, unsigned int* http_code, std::string* http_body) = 0;

    virtual int get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string* slice_json) = 0;

    virtual int query_bind_status(std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body) = 0;

    virtual int modify_printer_name(std::string dev_id, std::string dev_name) = 0;

    virtual int get_camera_url(std::string dev_id, std::function<void(std::string)> callback) = 0;

    virtual std::unique_ptr<ICameraSignalingChannel>
    create_camera_signaling_channel(const std::string& dev_id)
    {
        (void) dev_id;
        return nullptr;
    }

    virtual int get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback) = 0;

    virtual int start_publish(PublishParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, std::string* out) = 0;

    virtual int get_model_publish_url(std::string* url) = 0;

    virtual int get_subtask(BBLModelTask* task, OnGetSubTaskFn getsub_fn) = 0;

    virtual int get_model_mall_home_url(std::string* url) = 0;

    virtual int get_model_mall_detail_url(std::string* url, std::string id) = 0;

    virtual int get_my_profile(std::string token, unsigned int* http_code, std::string* http_body) = 0;

    virtual int get_my_token(std::string ticket, unsigned int* http_code, std::string* http_body) = 0;

    virtual int track_enable(bool enable) = 0;

    virtual int track_remove_files() = 0;

    virtual int track_event(std::string evt_key, std::string content) = 0;

    virtual int track_header(std::string header) = 0;

    virtual int track_update_property(std::string name, std::string value, std::string type = "string") = 0;

    virtual int track_get_property(std::string name, std::string& value, std::string type = "string") = 0;

    virtual bool get_track_enable() = 0;

    virtual int put_model_mall_rating(int design_id, int score, std::string content, std::vector<std::string> images, unsigned int& http_code, std::string& http_error) = 0;

    virtual int get_oss_config(std::string& config, std::string country_code, unsigned int& http_code, std::string& http_error) = 0;

    virtual int put_rating_picture_oss(std::string& config, std::string& pic_oss_path, std::string model_id, int profile_id, unsigned int& http_code, std::string& http_error) = 0;

    virtual int get_model_mall_rating_result(int job_id, std::string& rating_result, unsigned int& http_code, std::string& http_error) = 0;

    virtual int get_mw_user_preference(std::function<void(std::string)> callback) = 0;

    virtual int get_mw_user_4ulist(int seed, int limit, std::function<void(std::string)> callback) = 0;

    virtual std::string get_version() = 0;

    virtual int set_on_server_connected_fn(AppOnServerConnectedFn fn) = 0;

    virtual int set_on_http_error_fn(AppOnHttpErrorFn fn) = 0;

    virtual int set_get_country_code_fn(GetCountryCodeFn fn) = 0;

    virtual int set_queue_on_main_fn(QueueOnMainFn fn) = 0;
};

}

#endif
