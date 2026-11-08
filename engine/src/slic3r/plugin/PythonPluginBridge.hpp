#ifndef slic3r_PythonPluginBridge_hpp_
#define slic3r_PythonPluginBridge_hpp_

#include <memory>
#include <string>
#include <vector>

namespace Slic3r { class PluginCapabilityInterface; }
namespace Slic3r { class PluginCapabilityInterface; }
namespace Slic3r { class PluginCapabilityInterface; }
namespace Slic3r { class PluginCapabilityInterface; }


namespace Slic3r {

struct CapturedCapability
{
    std::shared_ptr<PluginCapabilityInterface> instance;
    std::string name;
};

class PythonPluginBridge
{
public:
    static PythonPluginBridge& instance();

    void begin_plugin_capture(const std::string& plugin_key);

    std::vector<CapturedCapability> finalize_plugin_capture(
        const std::string& capture_key, const std::string& plugin_key, std::string& error);

    void cancel_plugin_capture(const std::string& plugin_key);

    void clear_pending_captures();

private:
    PythonPluginBridge() = default;
    PythonPluginBridge(const PythonPluginBridge&) = delete;
    PythonPluginBridge& operator=(const PythonPluginBridge&) = delete;
};

}

#endif
