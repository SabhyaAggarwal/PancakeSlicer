#pragma once
#include "slic3r/plugin/PythonPluginInterface.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <pybind11/pybind11.h>
#include <map>
#include <string>

namespace Slic3r {

struct SlicingPipelineContext {
    std::string          orca_version;
    SlicingPipelineStepPlugin  step { SlicingPipelineStepPlugin::posSlice };
    Print*               print  { nullptr };
    const PrintObject*   object { nullptr };
    std::string          gcode_path;
    std::string          host;
    std::string          output_name;
    const DynamicPrintConfig* full_config { nullptr };
    bool cancelled() const;
};

class SlicingPipelinePluginCapability : public PluginCapabilityInterface {
public:
    PluginCapabilityType get_type() const override { return PluginCapabilityType::SlicingPipeline; }
    virtual ExecutionResult execute(SlicingPipelineContext& ctx) = 0;
    static void RegisterBindings(pybind11::module_& module);
};

}
