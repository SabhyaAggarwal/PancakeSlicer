#ifndef slic3r_PluginsDialog_hpp_
#define slic3r_PluginsDialog_hpp_

#include "Widgets/WebViewHostDialog.hpp"
#include "PluginSort.hpp"

#include <atomic>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <wx/event.h>
#include <wx/chartype.h>
#include <wx/evtloop.h>
#include <wx/app.h>
#include <wx/gdicmn.h>
#include <wx/progdlg.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <boost/filesystem.hpp>
#include <wx/toplevel.h>

class wxWindow;
namespace Slic3r { struct PluginDescriptor; }


namespace Slic3r {

class PluginCapabilityInterface;
struct PluginCapabilityId;
enum class PluginCapabilityType;

namespace GUI {

void refresh_plugin_metadata_blocking(bool fetch_cloud);

void open_plugin_hub();

bool install_local_plugin_package(const boost::filesystem::path& package_file, wxWindow* parent, wxString& message);

namespace detail {

template<typename Run, typename OnFinish>
void run_off_thread_with_progress(Run&& run,
                                  OnFinish&& on_finish,
                                  wxWindow* parent,
                                  const wxString& title,
                                  const wxString& message,
                                  int maximum,
                                  int style,
                                  std::shared_ptr<std::atomic<bool>> alive,
                                  bool finish_after_dialog_destroyed,
                                  std::function<void()> restore)
{
    wxProgressDialog* progress = new wxProgressDialog(title, message, maximum, parent, style);
    wxTimer* timer             = new wxTimer();

    timer->Bind(wxEVT_TIMER, [alive, progress, message](wxTimerEvent&) {
        if ((!alive || alive->load(std::memory_order_acquire)) && progress)
            progress->Pulse(message);
    });

    timer->Start(100);

    std::thread([alive,
                 progress,
                 timer,
                 run                                            = std::forward<Run>(run),
                 on_finish                                      = std::forward<OnFinish>(on_finish),
                 finish_after_dialog_destroyed,
                 restore                                        = std::move(restore)]() mutable {
        try {
            run();
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "Plugin dialog worker failed: " << ex.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Plugin dialog worker failed with an unknown exception";
        }

        if (wxTheApp == nullptr)
            return;

        wxTheApp->CallAfter([alive,
                             progress,
                             timer,
                             on_finish = std::move(on_finish),
                             finish_after_dialog_destroyed,
                             restore   = std::move(restore)]() mutable {
            timer->Stop();
            delete timer;

            if (!alive || alive->load(std::memory_order_acquire)) {
                progress->Destroy();
                if (restore)
                    restore();
                on_finish();
            } else if (finish_after_dialog_destroyed) {
                on_finish();
            }
        });
    }).detach();
}

template<typename Run>
std::invoke_result_t<std::decay_t<Run>&> run_wait_with_progress(Run&& run,
                                                                wxWindow* parent,
                                                                const wxString& title,
                                                                const wxString& message,
                                                                int maximum,
                                                                int style,
                                                                std::shared_ptr<std::atomic<bool>> alive,
                                                                std::function<void()> restore)
{
    using Result = std::invoke_result_t<std::decay_t<Run>&>;

    bool finished = false;
    wxEventLoop loop;
    auto on_finish = [&finished, &loop]() {
        finished = true;
        if (loop.IsRunning())
            loop.Exit();
    };

    if constexpr (std::is_void_v<Result>) {
        struct WaitState
        {
            std::mutex mutex;
            std::exception_ptr exception;
        };

        auto state = std::make_shared<WaitState>();
        run_off_thread_with_progress(
            [run = std::forward<Run>(run), state]() mutable {
                try {
                    run();
                } catch (...) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->exception = std::current_exception();
                }
            },
            on_finish, parent, title, message, maximum, style, std::move(alive), true, std::move(restore));

        if (!finished)
            loop.Run();

        std::exception_ptr exception;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            exception = state->exception;
        }
        if (exception)
            std::rethrow_exception(exception);
    } else {
        using StoredResult = std::decay_t<Result>;
        struct WaitState
        {
            std::mutex mutex;
            std::optional<StoredResult> result;
            std::exception_ptr exception;
        };

        auto state = std::make_shared<WaitState>();
        run_off_thread_with_progress(
            [run = std::forward<Run>(run), state]() mutable {
                try {
                    StoredResult result = run();
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->result.emplace(std::move(result));
                } catch (...) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->exception = std::current_exception();
                }
            },
            on_finish, parent, title, message, maximum, style, std::move(alive), true, std::move(restore));

        if (!finished)
            loop.Run();

        std::optional<StoredResult> result;
        std::exception_ptr exception;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->result)
                result.emplace(std::move(*state->result));
            exception = state->exception;
        }
        if (exception)
            std::rethrow_exception(exception);
        return std::move(*result);
    }
}

}

class PluginsDialog : public Slic3r::GUI::WebViewHostDialog
{
public:
    PluginsDialog(wxWindow* parent,
                  wxWindowID id         = wxID_ANY,
                  const wxString& title = wxT(""),
                  const wxPoint& pos    = wxDefaultPosition,
                  const wxSize& size    = wxDefaultSize,
                  long style            = wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX | wxMAXIMIZE_BOX | wxRESIZE_BORDER);

    ~PluginsDialog();

    void set_open_terminal_dlg_fn();
    void update_plugin_dialog_ui();

private:
    void open_plugin_on_cloud(const std::string& sharing_token);
    void open_plugin_hub();
    void on_script_message(const nlohmann::json& payload) override;
    void handle_web_command(const nlohmann::json& payload);
    void restore_z_order();

    void send_plugins();
    void set_plugin_sort(const std::string& sort_key, const std::string& sort_order);
    nlohmann::json build_plugins_payload() const;

    bool get_descriptor(const std::string& plugin_key, Slic3r::PluginDescriptor& descriptor) const;

    void refresh_plugin_metadata_async(const wxString& title, const wxString& message, bool fetch_cloud);
    void prompt_for_missing_plugins();
    void refresh_plugins();
    void toggle_plugin(const std::string& plugin_key, bool enabled);
    void toggle_plugin_capability(const std::string& plugin_key, PluginCapabilityType type, const std::string& capability_name, bool enabled);
    void handle_plugin_menu_action(const std::string& plugin_key, const std::string& action);

    void install_plugin_from_file();
    bool install_plugin_package(const std::string& package_path);
    bool install_cloud_plugin(const std::string& uuid, const std::string& version, const wxString& name);
    void run_script_plugin_capability(const std::string& plugin_key, const std::string& capability_name);
    void send_capability_config(const PluginCapabilityId& id);
    void save_capability_config(const PluginCapabilityId& id, const nlohmann::json& config);
    void restore_capability_config(const PluginCapabilityId& id);
    void show_status(const wxString& message, const char* level);
    wxString plugin_display_name(const std::string& plugin_key) const;
    void resolve_pending_activation();
    void update_plugin(const std::string& plugin_key);

    void open_plugin_folder(const Slic3r::PluginDescriptor& plugin);
    void delete_local_plugin(const Slic3r::PluginDescriptor& plugin);
    void unsubscribe_cloud_plugin(const Slic3r::PluginDescriptor& plugin);
    void reload_local_plugin(const std::string& plugin_key, bool clear_cache);
    void reinstall_cloud_plugin(const Slic3r::PluginDescriptor& plugin);

    template<typename Run, typename OnFinish>
    void run_with_dialog(Run&& run,
                         OnFinish&& on_finish,
                         const wxString& title,
                         const wxString& message,
                         int maximum = 100,
                         int style   = wxPD_APP_MODAL | wxPD_AUTO_HIDE,
                         bool finish_after_dialog_destroyed = false)
    {
        detail::run_off_thread_with_progress(std::forward<Run>(run), std::forward<OnFinish>(on_finish), this, title, message, maximum, style,
                                             m_alive, finish_after_dialog_destroyed, [this] { restore_z_order(); });
    }

    template<typename Run>
    std::invoke_result_t<std::decay_t<Run>&> run_with_dialog_wait(Run&& run,
                                                                  const wxString& title,
                                                                  const wxString& message,
                                                                  int maximum = 100,
                                                                  int style   = wxPD_APP_MODAL | wxPD_AUTO_HIDE)
    {
        return detail::run_wait_with_progress(std::forward<Run>(run), this, title, message, maximum, style, m_alive, [this] { restore_z_order(); });
    }

    std::function<void()> m_open_terminal_dlg_fn;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    PluginSortKey m_plugin_sort_key       = PluginSortKey::None;
    PluginSortOrder m_plugin_sort_order   = PluginSortOrder::Asc;

    std::string m_activating_plugin_key;
};

}
}

#endif
