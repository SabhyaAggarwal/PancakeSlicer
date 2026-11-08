#pragma once
#include <ctime>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <wx/string.h>

#include "DevDefs.h"

namespace Slic3r {

class MachineObject;

enum class PrintOptionEnum
{
    AI_Monitoring,
    First_Layer_Detection,
    Buildplate_Mark_Detection,
    Buildplate_Align_Detection,
    Auto_Recovery_Detection,
    Allow_Prompt_Sound_Detection,
    Filament_Tangle_Detection,
    Idle_Heating_Protect_Detection,
    Purify_Air_At_Print_End,
    Snapshot_Detection,
    FOD_Check_Detection,
    Displacement_Detection,
    Smart_Nozzle_Blob_Detection,
};

struct PrintOptionData
{
    bool        is_support_detect{false};
    int         current_detect_value{-1};
    std::string current_detect_sensitivity_value;
    time_t      detect_hold_start{0};
};

class DevPrintOptions
{
    friend class DevPrintOptionsParser;
public:
    DevPrintOptions(MachineObject* obj);

    enum class PurifyAirAtPrintEndState : int {
        PurifyAirDisable   = 0,
        PurifyAirByInside  = 1,
        PurifyAirByOutside = 2,
    };

public:
    void SetPrintingSpeedLevel(DevPrintingSpeedLevel speed_level);
    DevPrintingSpeedLevel GetPrintingSpeedLevel() const { return m_speed_level;}

    PrintOptionData* GetDetectionOption(PrintOptionEnum print_option);

    int command_xcam_control_ai_monitoring(bool on_off, std::string lvl);
    int command_xcam_control_first_layer_inspector(bool on_off, bool print_halt);
    int command_xcam_control_buildplate_marker_detector(bool on_off);
    int command_xcam_control_auto_recovery_step_loss(bool on_off);
    int command_xcam_control_allow_prompt_sound(bool on_off);
    int command_xcam_control_filament_tangle_detect(bool on_off);
    int command_xcam_control_idelheatingprotect_detector(bool on_off);

    int command_xcam_control_build_plate_align_detector(bool on_off);
    int command_xcam_control_fod_check(bool on_off);
    int command_xcam_control_displacement_detection(bool on_off);
    int command_xcam_control_purify_air_at_print_end(int on_off);
    int command_smart_nozzle_blob_detect_mode(int mode);
    int command_snapshot_control(int on_off);

    int command_xcam_control(std::string module_name, bool on_off,  MachineObject *obj ,std::string lvl = "");
    int command_set_printing_option(bool auto_recovery, MachineObject *obj);
    int command_set_prompt_sound(bool prompt_sound, MachineObject *obj);
    int command_set_filament_tangle_detect(bool fliament_tangle_detect, MachineObject *obj);

    int command_set_against_continued_heating_mode(bool on_off);
    int command_set_purify_air_at_print_end(PurifyAirAtPrintEndState state, MachineObject *obj);
    int command_set_snapshot_control(int on_off, MachineObject *obj);

    void parse_auto_recovery_step_loss_status(int flag);
    void parse_allow_prompt_sound_status(int flag);
    void parse_filament_tangle_detect_status(int flag);

    bool GetAiMonitoring() const { return m_ai_monitoring_detection.current_detect_value == 1; }
    bool GetFirstLayerInspector() const{ return m_first_layer_detection.current_detect_value == 1; }
    bool GetBuildplateMarkerDetector() const { return m_buildplate_mark_detection.current_detect_value == 1; }
    bool GetAutoRecoveryStepLoss() const { return m_auto_recovery_detection.current_detect_value == 1; }
    bool GetAllowPromptSound() const { return m_allow_prompt_sound_detection.current_detect_value == 1; }
    bool GetFilamentTangleDetect() const { return m_filament_tangle_detection.current_detect_value == 1; }
    int  GetIdelHeatingProtectEenabled() const { return m_idel_heating_protect_detection.current_detect_value; }
    std::string GetAiMonitoringSensitivity() const { return m_ai_monitoring_detection.current_detect_sensitivity_value; }


private:
    DevPrintingSpeedLevel m_speed_level = SPEED_LEVEL_INVALID;

    PrintOptionData m_ai_monitoring_detection;
    PrintOptionData m_first_layer_detection;
    PrintOptionData m_buildplate_mark_detection;
    PrintOptionData m_buildplate_align_detection;
    PrintOptionData m_auto_recovery_detection;
    PrintOptionData m_allow_prompt_sound_detection;
    PrintOptionData m_filament_tangle_detection;
    PrintOptionData m_idel_heating_protect_detection;
    PrintOptionData m_purify_air_at_print_end;
    PrintOptionData m_snapshot_detection;
    PrintOptionData m_fod_check_detection;
    PrintOptionData m_displacement_detection;
    PrintOptionData m_smart_nozzle_blob_detection;

    std::map<PrintOptionEnum, PrintOptionData*> m_detection_list;

    MachineObject* m_obj;
};

class DevPrintOptionsParser
{
public:
    static void Parse(DevPrintOptions* opts, const nlohmann::json& print_json);

    static void ParseDetectionV1_0(DevPrintOptions *opts, MachineObject *obj, const nlohmann::json &print_json);
    static void ParseDetectionV1_1(DevPrintOptions *opts, MachineObject *obj, const nlohmann::json &print_json, bool enable);
    static void ParseDetectionV1_2(DevPrintOptions *opts, MachineObject *obj, const nlohmann::json &print_json);

    static void ParseDetectionV2_0(DevPrintOptions *opts, std::string cfg);
    static void ParseDetectionV2_1(DevPrintOptions *opts, std::string cfg);
};

}
