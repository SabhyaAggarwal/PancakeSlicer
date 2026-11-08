#pragma once
#include "SlicingPipelinePluginCapability.hpp"
#include <pybind11/pybind11.h>
#include "slic3r/plugin/PyPluginTrampoline.hpp"
#include "slic3r/plugin/PluginAuditManager.hpp"
#include <boost/filesystem.hpp>
#include "slic3r/plugin/PythonPluginInterface.hpp"

namespace Slic3r {
class PySlicingPipelinePluginCapabilityTrampoline : public PyPluginCommonTrampoline<SlicingPipelinePluginCapability> {
public:
    using PyPluginCommonTrampoline<SlicingPipelinePluginCapability>::PyPluginCommonTrampoline;
    ExecutionResult execute(SlicingPipelineContext& ctx) override {
        ORCA_PY_OVERRIDE_AUDITED(
            [&]{
                if (!ctx.gcode_path.empty())
                    ::Slic3r::PluginAuditManager::instance().add_scoped_allowed_root(
                        boost::filesystem::path(ctx.gcode_path).parent_path());
            },
            PYBIND11_OVERRIDE_PURE,
            ExecutionResult, SlicingPipelinePluginCapability, execute, ctx);
    }
};
}
