#pragma once
#include <cassert>
#include <optional>
#include <string>
#include <unordered_map>
#include "libslic3r/CommonDefs.hpp"

#include <nlohmann/json.hpp>
#include <vector>
#include <wx/string.h>

#include "DevDefs.h"
#include <cmath>

namespace Slic3r
{

class MachineObject;
class DevExtderSystem;

struct DevAmsSlotInfo
{
    std::string ams_id;
    std::string slot_id;

public:
    bool operator!=(const DevAmsSlotInfo& other) const { return !(*this == other); }
    bool operator==(const DevAmsSlotInfo& other) const { return ams_id == other.ams_id && slot_id == other.slot_id;}
};

enum DevExtderSwitchState
{
    ES_IDLE = 0,
    ES_BUSY,
    ES_SWITCHING,
    ES_SWITCHING_FAILED
};

class DevExtder
{
    friend class MachineObject;
    friend class ExtderSystemParser;
public:
    DevExtder(DevExtderSystem* owner , int id = -1) : system(owner), m_ext_id(id){};

public:
    int  GetExtId() const { return m_ext_id; }

    wxString GetDisplayLoc() const;
    wxString GetDisplayName() const;

    bool           HasNozzleInstalled() const = delete;

    int            GetNozzleId() const { return m_current_nozzle_id; }
    int            GetTargetNozzleId() const = delete;
    NozzleType     GetNozzleType()     const;
    NozzleFlowType GetNozzleFlowType() const;
    float          GetNozzleDiameter() const;

    int  GetCurrentTemp() const { return m_cur_temp; }
    int  GetTargetTemp() const { return m_target_temp; }

    bool             HasFilamentInExt() const { return m_ext_has_filament; }
    bool             HasFilamentInBuffer() const = delete;
    bool             HasFilamBackup() const { return !m_filam_bak.empty(); }
    std::vector<int> GetFilamBackup() const { return m_filam_bak; }

    static std::unordered_map<int, bool> GetBackupStatus(unsigned int fila_back_group);

    const DevAmsSlotInfo& GetSlotPre() const { return m_spre; }
    const DevAmsSlotInfo& GetSlotNow() const { return m_snow; }
    const DevAmsSlotInfo& GetSlotTarget() const { return m_star; }

private:
    void SetExtId(int val) { m_ext_id = val; }

private:
    DevExtderSystem* system = nullptr;

    int m_ext_id;
    bool   m_has_nozzle = false;
    int    m_current_nozzle_id = 0;
    int    m_target_nozzle_id = 0;
    int    m_cur_temp = 0;
    int    m_target_temp = 0;

    bool             m_ext_has_filament = false;
    bool             m_buffer_has_filament = false;
    std::vector<int> m_filam_bak;
    DevAmsSlotInfo m_spre;
    DevAmsSlotInfo m_snow;
    DevAmsSlotInfo m_star;

    int m_ams_stat = 0;
    int m_rfid_stat = 0;
};

class DevExtderSystem
{
    friend class MachineObject;
    friend class ExtderSystemParser;
public:
    DevExtderSystem(MachineObject* obj);
    ~DevExtderSystem() = default;

public:
    MachineObject* Owner() const { return m_owner; }

    int GetTotalExtderCount() const { assert(m_extders.size() == m_total_extder_count); return m_total_extder_count; }
    int GetTotalExtderSize() const { return static_cast<int>(m_extders.size()); }
    int GetCurrentExtderId() const { return m_current_extder_id; }
    int GetTargetExtderId() const = delete;
    DevExtderSwitchState GetSwitchState() const { return m_switch_extder_state; }
    bool IsSwitching() const { return m_switch_extder_state  == DevExtderSwitchState::ES_SWITCHING;};
    bool IsSwitchingFailed() const { return m_switch_extder_state == DevExtderSwitchState::ES_SWITCHING_FAILED; };
    bool CanQuitSwitching() const;
    bool CanRetrySwitching() const { return IsSwitchingFailed(); };

    int CtrlRetrySwitching();
    int CtrlQuitSwitching();

    std::optional<DevExtder> GetCurrentExtder() const;
    std::optional<DevExtder> GetLoadingExtder() const;
    std::optional<DevExtder> GetExtderById(int extder_id) const;
    const std::vector<DevExtder>&  GetExtruders() const { return m_extders;};

    NozzleType     GetNozzleType(int extder_id)     const { return GetExtderById(extder_id) ? GetExtderById(extder_id)->GetNozzleType() : NozzleType::ntUndefine; }
    NozzleFlowType GetNozzleFlowType(int extder_id) const { return GetExtderById(extder_id) ? GetExtderById(extder_id)->GetNozzleFlowType() : NozzleFlowType::NONE_FLOWTYPE;; }
    float          GetNozzleDiameter(int extder_id) const { return GetExtderById(extder_id) ? GetExtderById(extder_id)->GetNozzleDiameter() : 0.0; }
    int            GetNozzleTempCurrent(int extder_id) const { return GetExtderById(extder_id) ? GetExtderById(extder_id)->GetCurrentTemp() : 0; }
    int            GetNozzleTempTarget(int extder_id) const { return GetExtderById(extder_id) ? GetExtderById(extder_id)->GetTargetTemp() : 0; }
    bool           NozzleDiameterMatchesOrUnknown(int extder_id, float target_diameter) const
    {
        float diameter = GetNozzleDiameter(extder_id);
        if (diameter == 0.0f) {
            return true;
        } else {
            return std::fabs(diameter - target_diameter) < 1e-3;
        }
    }

    std::string GetCurrentAmsId() const;
    std::string GetCurrentSlotId() const;
    std::string GetTargetAmsId() const;
    std::string GetTargetSlotId() const;

    bool IsBusyLoading() const { return m_current_busy_for_loading; }
    int  GetLoadingExtderId() const { return m_current_loading_extder_id; }
    bool HasFilamentBackup() const;
    bool HasFilamentInExt(int exter_id) { return GetExtderById(exter_id) ? GetExtderById(exter_id)->HasFilamentInExt() : false; }

    std::vector<DevAmsSlotId> GetBackupAmsSlotInGroup(const DevAmsSlotId& ams_slot_id);

protected:
    void  AddExtder(const DevExtder& ext) { m_extders[ext.GetExtId()] = ext; };

private:
    MachineObject* m_owner = nullptr;

    int m_total_extder_count = 1;
    std::vector<DevExtder> m_extders;

    int m_current_extder_id = MAIN_EXTRUDER_ID;

    DevExtderSwitchState m_switch_extder_state = DevExtderSwitchState::ES_IDLE;
    int m_target_extder_id = MAIN_EXTRUDER_ID;

    bool m_current_busy_for_loading{ false };
    int m_current_loading_extder_id = INVALID_EXTRUDER_ID;
};

class ExtderSystemParser
{
public:
    static void ParseV1_0(const nlohmann::json& extruder_json, DevExtderSystem* system);
    static void ParseV2_0(const nlohmann::json& extruder_json, DevExtderSystem* system);
};

};