#include "PluginHooks.hpp"

#include "PluginManager.hpp"
#include "PythonInterpreter.hpp"
#include "PythonPluginInterface.hpp"
#include "pluginTypes/slicingPipeline/SlicingPipelinePluginCapability.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/LifecycleEvents.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r_version.h"

#include <boost/log/trivial.hpp>

#include <exception>
#include <cstddef>
#include <memory>
#include "slic3r/plugin/PluginDescriptor.hpp"
#include <string>
#include <stdexcept>
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::plugin_hooks {
namespace {

void install_capability_resolver()
{
    ConfigBase::set_resolve_capability_fn([](const std::string& cap_name, const std::string& cap_type) {
        PluginManager& plugin_mgr = PluginManager::instance();
        const PluginCapabilityType type = plugin_capability_type_from_string(cap_type);
        auto plugin_cap = plugin_mgr.get_plugin_capability(cap_name, type, false);
        if (!plugin_cap)
            return std::string();

        PluginDescriptor descriptor;
        if (!plugin_mgr.try_get_plugin_descriptor_for_capability(cap_name, type, descriptor))
            return std::string();

        const std::string identity = descriptor.is_cloud_plugin() ? descriptor.name : descriptor.plugin_key;
        return identity + ';' + descriptor.cloud_uuid() + ';' + cap_name;
    });
}

void install_lifecycle_event_hook()
{
    set_lifecycle_hook_fn([](LifecycleEvent event, const LifecycleEventContext& ctx) {
        PluginManager::instance().dispatch_lifecycle_event(event, ctx);
    });
}

void install_slicing_pipeline_hook()
{
    Print::set_slicing_pipeline_hook_fn(
        [](Print& print, const PrintObject* object, SlicingPipelineStepPlugin step) {
            const auto* caps  = print.config().option<ConfigOptionStrings>("slicing_pipeline_plugin");
            const auto* plugs = print.full_print_config().option<ConfigOptionStrings>("plugins");
            if (caps == nullptr || caps->values.empty())
                return;

            execute_capabilities_from_refs<SlicingPipelinePluginCapability>(
                *caps, plugs, PluginCapabilityType::SlicingPipeline,
                [&](std::shared_ptr<SlicingPipelinePluginCapability> cap, const PluginCapabilityRef& ref) {
                    const std::string plugin_key = ref.uuid.empty() ? ref.name : ref.uuid;
                    ExecutionResult r;
                    try {
                        PythonGILState gil;
                        if (!gil)
                            throw std::runtime_error("Python interpreter is shutting down");
                        if (print.canceled())
                            throw CanceledException();
                        SlicingPipelineContext ctx;
                        ctx.orca_version = SoftFever_VERSION;
                        ctx.step   = step;
                        ctx.print  = &print;
                        ctx.object = object;
                        r = cap->execute(ctx);
                    } catch (const CanceledException&) {
                        throw;
                    } catch (const std::exception& ex) {
                        throw SlicingError(std::string("Slicing pipeline plugin '") +
                                           ref.capability_name + "' error: " + ex.what());
                    }
                    if (r.status == PluginResult::FatalError)
                        throw SlicingError(std::string("Slicing pipeline plugin '") +
                                           ref.capability_name + "' error: " + r.message);
                    if (!r.message.empty()) {
                        static const char* const kStepNames[] = {
                            "posSlice", "posPerimeters", "posEstimateCurledExtrusions", "posPrepareInfill", "posInfill",
                            "posIroning", "posContouring", "posSupportMaterial", "posDetectOverhangsForLift",
                            "posSimplifyPath", "psWipeTower", "psSkirtBrim", "psGCodePostProcess"
                        };
                        const char* step_name = static_cast<size_t>(step) < sizeof(kStepNames) / sizeof(kStepNames[0])
                                                    ? kStepNames[static_cast<int>(step)] : "Unknown";
                        BOOST_LOG_TRIVIAL(info) << "Slicing pipeline plugin '" << ref.capability_name
                                                << "' [" << step_name << "]: " << r.message;
                    }
                });
        });
}

}

void install()
{
    install_capability_resolver();
    install_slicing_pipeline_hook();
    install_lifecycle_event_hook();
}

void uninstall()
{
    ConfigBase::set_resolve_capability_fn(nullptr);
    Print::set_slicing_pipeline_hook_fn(nullptr);
    set_lifecycle_hook_fn(nullptr);
}

}
