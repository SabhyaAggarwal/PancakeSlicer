#pragma once
#include "DevDefs.h"
#include "DevFilaSystem.h"

#include "libslic3r/MultiNozzleUtils.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace Slic3r { struct DevNozzle; }

namespace Slic3r
{
class MachineObject;

namespace GUI
{
class Plater;
}
};

namespace Slic3r
{

class DevUtilBackend
{
public:
    DevUtilBackend() = delete;

public:
    static MultiNozzleUtils::NozzleInfo GetNozzleInfo(const DevNozzle& dev_nozzle);

    static std::shared_ptr<MultiNozzleUtils::NozzleGroupResultBase> GetNozzleGroupResult(Slic3r::GUI::Plater *plater);
    static std::unordered_map<NozzleDef, int> CollectNozzleInfo(MultiNozzleUtils::NozzleGroupResultBase *nozzle_group_res, int logic_ext_id);

    static std::optional<DevFilamentDryingPreset> GetFilamentDryingPreset(const std::string& fila_id);
};

};