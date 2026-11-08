#pragma once
#include "libslic3r/CommonDefs.hpp"

#include <nlohmann/json.hpp>
#include <wx/string.h>

#include <limits>
#include <optional>

namespace Slic3r
{

class MachineObject;


class DevConfig
{
public:
    DevConfig(MachineObject* obj) : m_obj(obj) {};
    ~DevConfig() = default;

public:
    bool HasChamber() const { return m_has_chamber; }
    bool SupportChamberTempDisplay() const;
    bool SupportChamberEdit() const { return m_support_chamber_edit; }
    int  GetChamberTempEditMin() const { return m_chamber_temp_edit_min; }
    int  GetChamberTempEditMax() const { return m_chamber_temp_edit_max; }
    int  GetChamberTempSwitchHeat() const { return m_chamber_temp_switch_heat; }

    bool SupportFirstLayerInspect() const { return m_support_first_layer_inspect; }
    bool SupportSaveRemotePrintFileToStorage() const { return m_support_save_remote_print_file_to_storage; }
    bool SupportAIMonitor() const { return m_support_ai_monitor; }

    bool SupportPrintWithoutSD() const { return m_support_print_without_sd; }
    bool SupportPrintAllPlates() const { return m_support_print_all; }

    bool SupportCalibrationLidar() const { return m_support_calibration_lidar; }
    bool SupportCalibrationNozzleOffset() const { return m_support_calibration_nozzle_offset; }
    bool SupportCalibrationHighTempBed() const { return m_support_calibration_high_temp_bed; }
    bool SupportCaliClumpPos() const { return m_support_calibration_clump_pos; }

    bool SupportCalibrationPA_FlowAuto() const { return m_support_calibration_pa_flow_auto; }

public:
    void ParseConfig(const nlohmann::json& print_json);

    void ParseChamberConfig(const nlohmann::json& print_json);
    void ParsePrintOptionsConfig(const nlohmann::json& print_json);
    void ParseCalibrationConfig(const nlohmann::json& print_json);

private:
    [[maybe_unused]] MachineObject* m_obj;

    bool m_has_chamber = false;
    std::optional<bool> m_support_chamber_temp_display;
    bool m_support_chamber_edit = false;
    int  m_chamber_temp_edit_min = 0;
    int  m_chamber_temp_edit_max = 60;
    int  m_chamber_temp_switch_heat = std::numeric_limits<int>::max();
    bool m_support_first_layer_inspect = false;
    bool m_support_save_remote_print_file_to_storage = false;
    bool m_support_ai_monitor = false;

    bool m_support_print_without_sd = false;
    bool m_support_print_all = false;

    bool m_support_calibration_lidar = false;
    bool m_support_calibration_nozzle_offset = false;
    bool m_support_calibration_high_temp_bed = false;
    bool m_support_calibration_clump_pos     = false;

    bool m_support_calibration_pa_flow_auto = false;
};

};