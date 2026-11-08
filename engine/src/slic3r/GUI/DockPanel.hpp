#pragma once

#include "WebPanel.hpp"

#include <functional>
#include <optional>
#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

std::string plugin_pane_name(const std::string& plugin_key, const std::string& title);

class DockPanel : public WebPanel
{
public:
    using MessageHandler = std::function<void(const nlohmann::json& data)>;
    using CloseHandler   = std::function<void()>;

    DockPanel(wxWindow*          parent,
              const std::string& html,
              MessageHandler     on_message,
              CloseHandler       on_close,
              CloseHandler       on_destroyed);
    ~DockPanel() override;

    void push_message(const nlohmann::json& data);
    void request_close();
    void destroy_silently();
    void fire_close();

protected:
    std::optional<std::string> page_html() override { return m_html; }
    bool on_page_message(const std::string& kind, const nlohmann::json& data) override;

private:
    void remove_pane();

    std::string    m_html;
    bool           m_closing{false};
    MessageHandler m_on_message;
    CloseHandler   m_on_close;
    CloseHandler   m_on_destroyed;
};

}}
