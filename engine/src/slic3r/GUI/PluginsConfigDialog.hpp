#pragma once

#include <slic3r/GUI/Widgets/WebViewHostDialog.hpp>
#include <libslic3r/Preset.hpp>
#include <slic3r/plugin/PluginConfig.hpp>

#include <atomic>
#include <memory>
#include "slic3r/plugin/PythonPluginInterface.hpp"
#include <string>
#include <wx/string.h>

class wxWindow;

namespace Slic3r { namespace GUI {

class PluginsConfigDialog : public WebViewHostDialog
{
public:
    PluginsConfigDialog(wxWindow* parent, Preset::Type type, const std::string& overrides_json);
    ~PluginsConfigDialog() override;

    std::string overrides_json() const { return serialize_plugin_overrides(m_overrides); }

private:
    void on_script_message(const nlohmann::json& payload) override;
    void handle_web_command(const nlohmann::json& payload);

    const Preset* current_preset() const;
    void          send_capabilities();
    void          send_capability_config(const PluginCapabilityId& id);
    void          send_save_error(const PluginCapabilityId& id, const std::string& error);
    void          show_status(const wxString& message, const char* level);

    PluginCapabilityId identifier_from(const nlohmann::json& payload) const;

    Preset::Type              m_type = Preset::TYPE_INVALID;
    PresetPluginConfigService m_service;
    CapabilityConfigDocument  m_overrides;
    std::string               m_parse_error;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};

}}
