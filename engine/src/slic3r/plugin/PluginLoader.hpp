#pragma once


#include <boost/filesystem/path.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Slic3r { struct PluginDescriptor; }

namespace Slic3r {
struct Plugin;
}

namespace Slic3r::plugin_loader {
bool load(const PluginDescriptor&                          descriptor,
          bool                                             skip_deps,
          const std::vector<std::string>&                  capabilities_to_enable,
          const std::function<std::string(const Plugin&)>& registry_precheck,
          Plugin&                                          out,
          std::string&                                     error);

void unload(Plugin& plugin);

bool install_packages(const std::vector<std::string>& pkgs, std::string& error);

bool inspect_local_plugin_package(const boost::filesystem::path& filepath,
                                  PluginDescriptor&              plugin_descriptor,
                                  bool&                          existing_installation,
                                  std::string&                   error);

bool install_plugin(const boost::filesystem::path& filepath,
                    const std::string&             cloud_user_id,
                    PluginDescriptor&              plugin_descriptor,
                    std::string&                   error);
bool install_plugin(const boost::filesystem::path& filepath, const std::string& cloud_user_id, std::string& error);

}
