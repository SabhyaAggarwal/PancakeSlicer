#pragma once
#include <map>
#include <mutex>
#include "libslic3r/CommonDefs.hpp"

#include <string>
#include <vector>
#include <wx/object.h>
#include <wx/string.h>
#include <wx/timer.h>

namespace Slic3r
{
struct BBLocalMachine;
class MachineObject;
class NetworkAgent;
class AppConfig;

namespace GUI {
class GUI_App;
};

class DeviceManagerRefresher;
class DeviceManager
{
    friend class GUI::GUI_App;
    friend class DeviceManagerRefresher;
private:
    NetworkAgent* m_agent{ nullptr };
    AppConfig* m_app_config{ nullptr };
    DeviceManagerRefresher* m_refresher{ nullptr };

    bool m_enable_mutil_machine = false;

    std::mutex listMutex;
    std::string selected_machine;
    std::string local_selected_machine;
    std::map<std::string, MachineObject*> localMachineList;
    std::map<std::string, MachineObject*> userMachineList;

public:
    DeviceManager(NetworkAgent* agent = nullptr, bool enable_refresher = true,
                  AppConfig* app_config = nullptr);
    ~DeviceManager();

public:
    NetworkAgent* get_agent() const { return m_agent; }
    AppConfig* get_app_config() const;
    void set_agent(NetworkAgent* agent);

    void start_refresher();
    void stop_refresher();

    MachineObject* get_selected_machine();
    bool set_selected_machine(std::string dev_id);

    void OnSelectedMachineLost();

    void record_user_last_machine(const std::string& dev_id);
    std::string get_user_last_machine() const;

    void           set_local_selected_machine(std::string dev_id) { local_selected_machine = dev_id; };
    MachineObject* get_local_selected_machine() const { return get_local_machine(local_selected_machine); }

    void erase_local_machine(std::string dev_id) { localMachineList.erase(dev_id); }
    std::map<std::string, MachineObject*> get_local_machinelist() const { return localMachineList; }
    MachineObject* get_local_machine(std::string dev_id) const
    {
        auto it = localMachineList.find(dev_id);
        return (it != localMachineList.end()) ? it->second : nullptr;
    }

    std::map<std::string, MachineObject*> get_user_machinelist() const { return userMachineList; }
    std::string get_first_online_user_machine() const;
    void erase_user_machine(std::string dev_id) { userMachineList.erase(dev_id); }
    void clean_user_info(bool keep_local_selection = false);

    void clear_other_devices();

    void load_last_machine();
    void update_user_machine_list_info(const std::string& provider);
    void parse_user_print_info(std::string body);
    void reload_printer_settings();

    MachineObject* get_user_machine(std::string dev_id, const std::string& provider);

    void add_user_subscribe();
    void del_user_subscribe();
    void subscribe_device_list(std::vector<std::string> dev_list);

    MachineObject* get_my_machine(std::string dev_id);
    std::map<std::string, MachineObject*> get_my_machine_list(const std::string& agent_id = "");
    std::map<std::string, MachineObject*> get_my_cloud_machine_list(const std::string& agent_id = "");
    void modify_device_name(std::string dev_id, std::string dev_name, const std::string& provider);

    std::string get_current_printer_agent_id() const;

    void on_machine_alive(std::string json_str);
    int query_bind_status(std::string& msg, const std::string& provider);

    void EnableMultiMachine(bool enable = true);
    bool IsMultiMachineEnabled() const { return m_enable_mutil_machine; }
    std::vector<std::string> subscribe_list_cache;
    std::map<std::string, std::vector<std::string>> device_subseries;

private:
    void load_local_machines_from_config();

    void keep_alive();
    void check_pushing();
    std::string get_current_cloud_provider() const;

    void OnMachineBindStateChanged(MachineObject* obj, const std::string& new_state);
    void OnSelectedMachineChanged(const std::string& pre_dev_id, const std::string& new_dev_id);


public:
    MachineObject* insert_local_device(const BBLocalMachine& machine,
        std::string connection_type, std::string bind_state, std::string version,
        std::string access_code);
    static void update_local_machine(const MachineObject& m);
    static void update_local_machine(const MachineObject& m, AppConfig* config);
};

class DeviceManagerRefresher : public wxObject
{
    wxTimer* m_timer{nullptr};
    int m_timer_interval_msec = 5000;

    DeviceManager* m_manager{nullptr};

public:
    DeviceManagerRefresher(DeviceManager* manger);
    ~DeviceManagerRefresher();

public:
    void Start() { m_timer->Start(m_timer_interval_msec); }
    void Stop() { m_timer->Stop(); }

protected:
    virtual void on_timer(wxTimerEvent& event);
};
};
