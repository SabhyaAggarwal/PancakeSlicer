#ifndef __ORCA_CLOUD_SERVICE_AGENT_HPP__
#define __ORCA_CLOUD_SERVICE_AGENT_HPP__

#include "CloudProvider.hpp"
#include "ICameraSignalingChannel.hpp"
#include "ICloudServiceAgent.hpp"
#include "bambu_networking.hpp"
#include <cstdlib>
#include "libslic3r/ProjectTask.hpp"
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

class wxSecretStore;

namespace Slic3r {

class AppConfig;
struct BundleMetadata;
struct PluginDescriptor;
struct PluginChangelog;

struct PluginDownloadData
{
    std::string plugin_id;
    std::string download_link;
    std::string requested_os;
    std::string returned_os;
};

struct PluginDownloadNotFound
{
    std::string id;
    std::string reason;
};

enum class RefreshResult {
    Success,
    AuthRejected,
    Transient
};

namespace auth_constants {
    constexpr int LOOPBACK_PORT = 41172;
    constexpr const char* LOOPBACK_PATH = "/callback";
    constexpr const char* TOKEN_PATH = "/auth/v1/token";
    constexpr const char* LOGOUT_PATH = "/auth/v1/logout";
}
namespace secret_constants {
    constexpr const char* USER_SECRET_FILENAME = "orca_refresh_token.sec";
}

#ifndef ORCA_SYNC_STRUCTS_DEFINED
#define ORCA_SYNC_STRUCTS_DEFINED

struct ProfileUpsert {
    std::string id;
    std::string name;
    nlohmann::json content;
    long long updated_time;
    long long created_time;
};

struct SyncPullResponse {
    long long next_cursor;
    std::vector<ProfileUpsert> upserts;
    std::vector<std::string> deletes;
};

struct SyncPushResult {
    bool success;
    int http_code;
    int conflict_code;
    long long new_updated_time;
    ProfileUpsert server_version;
    bool server_deleted;
    std::string error_message;
};

struct SyncState {
    long long last_sync_timestamp;
};

#endif
class OrcaCloudServiceAgent : public ICloudServiceAgent {
public:
    struct SessionInfo {
        std::string access_token;
        std::string refresh_token;
        std::string user_id;
        std::string user_name;
        std::string user_nickname;
        std::string user_avatar;
        std::chrono::system_clock::time_point expires_at{};
        bool logged_in = false;
    };

    struct PkceBundle {
        std::string verifier;
        std::string challenge;
        std::string state;
        std::string redirect;
        int loopback_port = auth_constants::LOOPBACK_PORT;
    };

    using SessionHandler = std::function<bool(const std::string&)>;
    using OnLoginCompleteHandler = std::function<void(bool success, const std::string& user_id)>;

    explicit OrcaCloudServiceAgent(std::string log_dir);
    ~OrcaCloudServiceAgent() override;

    std::string get_id() const override { return ORCA_CLOUD_PROVIDER; }

    void configure_urls(AppConfig* app_config);
    void set_api_base_url(const std::string& url);
    void set_auth_base_url(const std::string& url);
    void set_cloud_base_url(const std::string& url);
    std::string get_cloud_base_url() const { return cloud_base_url; }
    void set_use_encrypted_token_file(bool use);
    bool get_use_encrypted_token_file() const;

    int init_log() override;
    int set_config_dir(std::string config_dir) override;
    int set_cert_file(std::string folder, std::string filename) override;
    int set_country_code(std::string country_code) override;
    int start() override;

    int change_user(std::string user_info) override;
    bool is_user_login() override;
    int user_logout(bool request = false) override;
    std::string get_user_id() override;
    std::string get_user_name() override;
    std::string get_user_avatar() override;
    std::string get_user_nickname() override;

    std::string build_login_cmd() override;
    std::string build_logout_cmd() override;
    std::string build_login_info() override;

    std::string get_access_token() const override;
    std::string get_refresh_token() const override;
    bool ensure_token_fresh(const std::string& reason) override;

    std::string get_cloud_service_host() override;
    std::string get_cloud_login_url(const std::string& language = "") override;
    int connect_server() override;
    bool is_server_connected() override;
    int refresh_connection() override;
    void stop_health_check();
    bool is_refresh_running() const { return refresh_running.load(); }
    int start_subscribe(std::string module) override;
    int stop_subscribe(std::string module) override;
    int add_subscribe(std::vector<std::string> dev_list) override;
    int del_subscribe(std::vector<std::string> dev_list) override;
    void enable_multi_machine(bool enable) override;

    int get_user_presets(std::map<std::string, std::map<std::string, std::string>>* user_presets) override;
    std::string request_setting_id(std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code) override;
    int put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code, bool force = false) override;
    SyncPushResult sync_push(const std::string& profile_id,
                             const std::string& name,
                             const nlohmann::json& content,
                             const std::string& original_updated_time = "",
                             bool force                               = false);
    int get_setting_list(std::string bundle_version, ProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr) override;
    int get_setting_list2(std::string bundle_version, CheckFn chk_fn, ProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr) override;
    int delete_setting(std::string setting_id) override;

    int get_my_message(int type, int after, int limit, unsigned int* http_code, std::string* http_body) override;
    int check_user_task_report(int* task_id, bool* printable) override;
    int get_user_print_info(unsigned int* http_code, std::string* http_body) override;
    int get_user_tasks(TaskQueryParams params, std::string* http_body) override;
    int get_printer_firmware(std::string dev_id, unsigned* http_code, std::string* http_body) override;
    int get_task_plate_index(std::string task_id, int* plate_index) override;
    int get_user_info(int* identifier) override;
    int get_subtask_info(std::string subtask_id, std::string* task_json, unsigned int* http_code, std::string* http_body) override;
    int get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string* slice_json) override;
    int query_bind_status(std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body) override;
    int modify_printer_name(std::string dev_id, std::string dev_name) override;

    int get_camera_url(std::string dev_id, std::function<void(std::string)> callback) override;
    int get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback) override;
    int start_publish(PublishParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, std::string* out) override;
    int get_model_publish_url(std::string* url) override;
    int get_subtask(BBLModelTask* task, OnGetSubTaskFn getsub_fn) override;
    int get_model_mall_home_url(std::string* url) override;
    int get_model_mall_detail_url(std::string* url, std::string id) override;
    int get_my_profile(std::string token, unsigned int* http_code, std::string* http_body) override;
    int get_my_token(std::string ticket, unsigned int* http_code, std::string* http_body) override;

    int track_enable(bool enable) override;
    int track_remove_files() override;
    int track_event(std::string evt_key, std::string content) override;
    int track_header(std::string header) override;
    int track_update_property(std::string name, std::string value, std::string type = "string") override;
    int track_get_property(std::string name, std::string& value, std::string type = "string") override;
    bool get_track_enable() override;

    int put_model_mall_rating(int design_id, int score, std::string content, std::vector<std::string> images, unsigned int& http_code, std::string& http_error) override;
    int get_oss_config(std::string& config, std::string country_code, unsigned int& http_code, std::string& http_error) override;
    int put_rating_picture_oss(std::string& config, std::string& pic_oss_path, std::string model_id, int profile_id, unsigned int& http_code, std::string& http_error) override;
    int get_model_mall_rating_result(int job_id, std::string& rating_result, unsigned int& http_code, std::string& http_error) override;

    int get_mw_user_preference(std::function<void(std::string)> callback) override;
    int get_mw_user_4ulist(int seed, int limit, std::function<void(std::string)> callback) override;
    std::string get_version() override;

    int set_on_server_connected_fn(AppOnServerConnectedFn fn) override;
    int set_on_http_error_fn(AppOnHttpErrorFn fn) override;
    int set_get_country_code_fn(GetCountryCodeFn fn) override;
    int set_queue_on_main_fn(QueueOnMainFn fn) override;

    void load_sync_state();
    void save_sync_state();
    void clear_sync_state();
    const SyncState& get_sync_state() const { return sync_state; }

    bool unsubscribe_bundle(const std::string& bundle_id);
    std::string get_bundle_url(const std::string& bundle_id) const;
    int get_subscribed_bundles(std::vector<std::pair<std::string, std::string>>* bundles,std::vector<std::string>& notfound, std::vector<std::string>& unauthorized);
    int get_shared_bundle(const std::string& bundle_id, std::map<std::string, std::map<std::string, std::string>>* presets, BundleMetadata* bundle_metadata);

    int fetch_subscribed_manifests_into_descriptors(std::vector<PluginDescriptor>& descriptors, std::vector<std::string>& not_found, std::vector<std::string>& unauthorized);
    int fetch_mine_manifests_into_descriptors(std::vector<PluginDescriptor>& descriptors);
    int get_plugin_download_url(const std::string& uuid,
                                const std::string& requested_version,
                                std::vector<PluginDownloadData>& data,
                                std::vector<PluginDownloadNotFound>& not_found,
                                std::vector<std::string>& unauthorized);
    std::string get_plugin_url(const std::string& sharing_token) const;
    int subscribe_plugin(const std::string& plugin_uuid);
    int unsubscribe_plugins(const std::vector<std::string>& plugin_uuids);
    int fetch_plugin_changelogs(const std::vector<std::string>& uuids, std::unordered_map<std::string, std::vector<PluginChangelog>>& changelog);

    void set_session_handler(SessionHandler handler);
    void set_on_login_complete_handler(OnLoginCompleteHandler handler);

    const PkceBundle& pkce();
    void regenerate_pkce();

    void persist_user_secret(const std::string& secret);
    bool load_user_secret(std::string& out_secret);
    void clear_user_secret(bool all_backends = false);

    bool          refresh_if_expiring(std::chrono::seconds skew, const std::string& reason);
    RefreshResult refresh_from_storage(const std::string& reason, bool async = false);
    RefreshResult refresh_now(const std::string& refresh_token, const std::string& reason, bool async = false);
    RefreshResult refresh_session_with_token(const std::string& refresh_token, const std::string& reason = "");

    bool set_user_session(const std::string& token,
                          const std::string& user_id,
                          const std::string& username,
                          const std::string& nickname,
                          const std::string& avatar,
                          const std::string& refresh_token = "",
                          bool persist = true);
    bool set_user_session(const nlohmann::json& session_json, bool notify_login = true);
    void clear_session(bool all_backends = false);

    static std::string generate_uuid_for_setting_id(const std::string& name, const std::string& user_id = "");

private:
    int sync_pull(
        std::function<void(const SyncPullResponse&)> on_success,
        std::function<void(int http_code, const std::string& error)> on_error
    );

    struct HttpResult {
        bool         success{false};
        unsigned int status{0};
        std::string  body;
    };

    bool resolve_unauthorized(HttpResult& res,
                              const std::function<HttpResult()>& perform,
                              const std::string& reason);

    int http_get(const std::string& path, std::string* response_body, unsigned int* http_code);
    int http_post(const std::string& path, const std::string& body, std::string* response_body, unsigned int* http_code);
    int http_put(const std::string& path, const std::string& body, std::string* response_body, unsigned int* http_code);
    int http_delete(const std::string& path, std::string* response_body, unsigned int* http_code);
    std::map<std::string, std::string> data_headers();
    RefreshResult attempt_refresh_after_unauthorized(const std::string& reason);

    bool http_post_token(const std::string& body, std::string* response_body, unsigned int* http_code, const std::string& url = "");
    bool http_post_auth(const std::string& path, const std::string& body, std::string* response_body, unsigned int* http_code);
    bool exchange_auth_code(const std::string& auth_code, const std::string& state, std::string& session_payload);
    void update_redirect_uri();
    void compute_fallback_path();
    bool decode_jwt_expiry(const std::string& token, std::chrono::system_clock::time_point& out_tp);
    bool should_refresh_locked(std::chrono::seconds skew) const;

    bool run_health_check(const std::atomic_bool* cancel);

    void invoke_server_connected_callback(int return_code, int reason_code);
    void invoke_http_error_callback(unsigned http_code, const std::string& http_body);

    std::string map_to_json(const std::map<std::string, std::string>& map);
    void json_to_map(const std::string& json, std::map<std::string, std::string>& map);

    std::string token_lock_path() const;

    std::string log_dir;
    std::string config_dir;
    std::string api_base_url;
    std::string auth_base_url;
    std::string cloud_base_url;
    std::string country_code;
    std::map<std::string, std::string> extra_headers;
    std::map<std::string, std::string> auth_headers;
    mutable std::mutex headers_mutex;
    bool m_use_encrypted_token_file{false};

    PkceBundle pkce_bundle;
    std::string secret_fallback_path;
    std::atomic_bool secret_stored{false};
    SessionHandler session_handler;
    OnLoginCompleteHandler on_login_complete_handler;
    SessionInfo session;
    mutable std::mutex session_mutex;

    std::atomic<long long> last_refresh_success_epoch{0};
    const long long        agent_start_epoch{std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count()};

    bool is_connected{false};
    bool enable_track{false};
    bool multi_machine_enabled{false};

    SyncState sync_state;
    std::string sync_state_path;

    AppOnServerConnectedFn on_server_connected_fn;
    AppOnHttpErrorFn on_http_error_fn;
    GetCountryCodeFn get_country_code_fn;
    QueueOnMainFn queue_on_main_fn;
    mutable std::mutex callback_mutex;

    mutable std::recursive_mutex state_mutex;
    std::thread refresh_thread;
    std::atomic_bool refresh_running{false};
    std::thread health_check_thread;
    std::atomic_bool health_check_running{false};
    std::atomic_bool health_check_stopped{false};
};

}

#endif
