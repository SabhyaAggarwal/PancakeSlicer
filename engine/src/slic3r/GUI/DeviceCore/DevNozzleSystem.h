#pragma once
#include "DevDefs.h"
#include "DevFirmware.h"

#include "libslic3r/CommonDefs.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"
#include <string>
#include <vector>
#include <optional>
#include <wx/string.h>
#include <map>
#include <memory>

namespace Slic3r
{
   class MachineObject;
   class DevNozzleRack;

   struct DevNozzle
   {
       friend class DevNozzleSystemParser;

   public:
       int             m_nozzle_id = -1;
       NozzleFlowType  m_nozzle_flow = NozzleFlowType::S_FLOW;
       NozzleType      m_nozzle_type = NozzleType::ntUndefine;
       float           m_diameter = 0.0f;

   public:
       static NozzleFlowType       ToNozzleFlowType(const NozzleVolumeType& type);
       static NozzleVolumeType     ToNozzleVolumeType(const NozzleFlowType& type);

       static wxString             GetNozzleFlowTypeStr(NozzleFlowType type);
       static std::string          GetNozzleFlowTypeString(NozzleFlowType type);
       static std::string          ToNozzleFlowString(const NozzleFlowType& type);
       static wxString             GetNozzleTypeStr(NozzleType type);

   public:
       bool     IsEmpty() const { return m_nozzle_id < 0; }

       void     SetRack(const std::weak_ptr<DevNozzleRack>& rack) { m_nozzle_rack = rack; }

       int            GetNozzleId() const { return m_nozzle_id; }
       int            GetNozzlePosId() const;
       NozzleType     GetNozzleType() const { return m_nozzle_type; }
       NozzleFlowType GetNozzleFlowType() const { return m_nozzle_flow; }
       NozzleDiameterType GetNozzleDiameterType() const;
       float          GetNozzleDiameter() const { return m_diameter; }
       float          GetNozzleWear() const { return m_wear; }
       int            GetNozzlePrintTime() const { return m_nozzle_print_time; }

       DevFirmwareVersionInfo GetFirmwareInfo() const;

       wxString GetNozzleDiameterStr() const { return wxString::Format("%.1f mm", m_diameter); }
       wxString GetNozzleFlowTypeStr() const { return GetNozzleFlowTypeStr(m_nozzle_flow); }
       wxString GetNozzleTypeStr() const { return GetNozzleTypeStr(m_nozzle_type); }

       std::string GetFilamentId() const { return m_fila_id; }
       std::string GetFilamentColor() const { return m_filament_clr; }

       bool AtLeftExtruder() const;
       bool AtRightExtruder() const;

       int  GetLogicExtruderId() const;
       int  GetExtruderId() const;
       bool IsOnRack() const { return m_on_rack; }
       bool IsInfoReliable() const;

       bool IsNormal() const;
       bool IsAbnormal() const;
       bool IsUnknown() const;

       void SetOnRack(bool on_rack) { m_on_rack = on_rack; }
       void SetStatus(int stat) { m_stat = stat; }

   private:
       int  GetTotalExtruderCount() const;

   private:
       bool m_on_rack = false;

       int   m_stat = 0;
       float m_wear = 0.0f;

       std::string m_fila_id;
       std::string m_filament_clr;

       std::weak_ptr<DevNozzleRack> m_nozzle_rack;
       int m_nozzle_print_time{0};
   };

   class DevNozzleSystem
   {
       friend class DevNozzleSystemParser;
   private:
       enum Status : int
       {
           NOZZLE_SYSTEM_IDLE = 0,
           NOZZLE_SYSTEM_REFRESHING = 1,
       };

   public:
       DevNozzleSystem(MachineObject* owner);
       ~DevNozzleSystem() = default;

   public:
       MachineObject* GetOwner() const { return m_owner; }

       DevNozzle                       GetNozzleByPosId(int pos_id) const { return pos_id < 0x10 ? GetExtNozzle(pos_id) : GetRackNozzle(pos_id - 0x10); }

       bool                            ContainsExtNozzle(int id) const { return m_ext_nozzles.find(id) != m_ext_nozzles.end(); }
       DevNozzle                       GetExtNozzle(int id) const;
       const std::map<int, DevNozzle>& GetExtNozzles() const { return m_ext_nozzles; }
       int                             GetExtNozzleCount() const { return (int) m_ext_nozzles.size(); }

       void  SetSupportNozzleRack(bool supported);
       std::shared_ptr<DevNozzleRack>  GetNozzleRack() const { return m_nozzle_rack; }
       DevNozzle                       GetRackNozzle(int idx) const;
       const std::map<int, DevNozzle>& GetRackNozzles() const;

       bool IsRackMaximumInstalled() const;
       const std::vector<DevNozzle> CollectNozzles(int ext_loc, NozzleFlowType flow_type, float diameter = -1.0f) const;
       std::vector<MultiNozzleUtils::NozzleGroupInfo> GetNozzleGroups() const;

       bool  IsIdle() const { return m_state_0_4 == NOZZLE_SYSTEM_IDLE; }
       bool  IsRefreshing() const { return m_state_0_4 == NOZZLE_SYSTEM_REFRESHING; }

       bool  HasUnreliableNozzles() const;
       bool  HasUnknownNozzles() const;
       int GetReadingIdx() const { return m_reading_idx; }
       int GetReadingCount() const { return m_reading_count; }

       void AddFirmwareInfoWTM(const DevFirmwareVersionInfo& info);
       void ClearFirmwareInfoWTM();
       DevFirmwareVersionInfo GetExtruderNozzleFirmware() const { return m_ext_nozzle_firmware_info; }

       std::optional<int> GetReplaceNozzleSrc() const { return m_replace_nozzle_src; }
       std::optional<int> GetReplaceNozzleTar() const { return m_replace_nozzle_tar; }

   private:
       void ClearNozzles();

   private:
       MachineObject* m_owner = nullptr;

       int m_extder_exist = 0;
       int m_state_0_4 = 0;

       std::optional<int> m_replace_nozzle_src;
       std::optional<int> m_replace_nozzle_tar;
       int m_reading_idx = 0;
       int m_reading_count = 0;

       std::map<int, DevNozzle> m_ext_nozzles;
       DevFirmwareVersionInfo m_ext_nozzle_firmware_info;

       std::shared_ptr<DevNozzleRack> m_nozzle_rack;
   };

   class DevNozzleSystemParser
   {
   public:
       static void  ParseV1_0(const nlohmann::json& nozzletype_json, const nlohmann::json& diameter_json, DevNozzleSystem* system, std::optional<int> flag_e3d);
       static void  ParseV2_0(const nlohmann::json& device_json, DevNozzleSystem* system);
   };
};
