#ifndef __NETWORK_AGENT_FACTORY_HPP__
#define __NETWORK_AGENT_FACTORY_HPP__

#include "CloudProvider.hpp"
#include "ICloudServiceAgent.hpp"
#include "IPrinterAgent.hpp"
#include "NetworkAgent.hpp"
#include "OrcaCloudServiceAgent.hpp"
#include "BBLCloudServiceAgent.hpp"
#include "BBLNetworkPlugin.hpp"
#include "libslic3r/AppConfig.hpp"
#include <memory>
#include <string>
#include <functional>
#include <utility>
#include <vector>

namespace Slic3r {
static constexpr char ORCA_PRINTER_AGENT_ID[] = "orca";
static constexpr char BBL_PRINTER_AGENT_ID[] = "bbl";

using PrinterAgentFactory =
    std::function<std::shared_ptr<IPrinterAgent>(std::shared_ptr<ICloudServiceAgent> cloud_agent, const std::string& log_dir)>;

struct PrinterAgentInfo
{
    std::string         id;
    std::string         display_name;
    std::string         plugin_identifier;
    PrinterAgentFactory factory;

    bool is_plugin() const { return !plugin_identifier.empty(); }
    PrinterAgentInfo(const std::string& id_, const std::string& display_name_, PrinterAgentFactory factory_)
        : id(id_), display_name(display_name_), factory(std::move(factory_))
    {}

    PrinterAgentInfo(const std::string& id_, const std::string& display_name_, const std::string& plugin_identifier, PrinterAgentFactory factory_)
        : id(id_), display_name(display_name_), plugin_identifier(plugin_identifier), factory(std::move(factory_))
    {}
};

class NetworkAgentFactory
{
public:
    static void register_all_agents();

    static bool register_printer_agent(const std::string& id, const std::string& display_name, PrinterAgentFactory factory);

    static bool is_printer_agent_registered(const std::string& id);

    static const PrinterAgentInfo* get_printer_agent_info(const std::string& id);

    static std::string get_printer_agent_plugin_identifier(const std::string& id);

    static std::vector<PrinterAgentInfo> get_registered_printer_agents();

    static std::shared_ptr<IPrinterAgent> create_printer_agent_by_id(const std::string&                  id,
                                                                     std::shared_ptr<ICloudServiceAgent> cloud_agent,
                                                                     const std::string&                  log_dir);

    static void clear_printer_agent_cache();

    static std::shared_ptr<ICloudServiceAgent> create_cloud_agent(const std::string& provider, const std::string& log_dir)
    {
        if (provider == ORCA_CLOUD_PROVIDER) {
            return std::make_shared<OrcaCloudServiceAgent>(log_dir);
        } else if (provider == BBL_CLOUD_PROVIDER) {
            auto& plugin = BBLNetworkPlugin::instance();
            if (!plugin.is_loaded()) {
                return nullptr;
            }
            if (!plugin.has_agent()) {
                plugin.create_agent(log_dir);
            }
            if (!plugin.has_agent()) {
                return nullptr;
            }
            return std::make_shared<BBLCloudServiceAgent>();
        }
        return nullptr;
    }

    static void register_python_plugin(const std::string& plugin_key);
    static void deregister_python_plugin(const std::string& plugin_key);

    static void register_python_printer_agent(const std::string& plugin_key, const std::string& capability_name);
    static void deregister_python_printer_agent(const std::string& plugin_key, const std::string& capability_name);

private:
    NetworkAgentFactory()                                      = delete;
    ~NetworkAgentFactory()                                     = delete;
    NetworkAgentFactory(const NetworkAgentFactory&)            = delete;
    NetworkAgentFactory& operator=(const NetworkAgentFactory&) = delete;
};

std::unique_ptr<NetworkAgent> create_agent_from_config(const std::string& log_dir, AppConfig* app_config);

}

#endif
