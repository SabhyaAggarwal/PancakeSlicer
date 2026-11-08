#pragma once
#include "DevFirmware.h"

#include <nlohmann/json.hpp>
#include <string>
#include <wx/string.h>

namespace Slic3r {

class MachineObject;

enum PrinterFirmwareType
{
    FIRMWARE_TYPE_ENGINEER = 0,
    FIRMWARE_TYPE_PRODUCTION,
    FIRMEARE_TYPE_UKNOWN,
};

class FirmwareInfo
{
public:
    std::string module_type;
    std::string version;
    std::string url;
    std::string name;
    std::string description;
};


class DevFirmwareVersionInfo
{
public:
    std::string name;
    wxString    product_name;
    std::string sn;
    std::string hw_ver;
    std::string sw_ver;
    std::string sw_new_ver;
    int         firmware_flag = 0;

public:
    bool isValid() const { return !sn.empty(); }

    bool isAirPump() const { return product_name.Contains("Air Pump"); }
    bool isLaszer() const { return product_name.Contains("Laser"); }
    bool isCuttingModule() const { return product_name.Contains("Cutting Module"); }
    bool isRotary() const { return product_name.Contains("Rotary"); }
    bool isExtinguishSystem() const { return product_name.Contains("Extinguishing System"); }
    bool isWTM() const { return name.find("wtm") != std::string::npos; }
    bool isExhaustFan() const { return product_name.Contains("Exhaust Fan"); }
    bool isHmshub() const { return product_name.find("Filament Buffer") != std::string::npos; }
    bool isFilaTrackSwitch() const { return product_name.find("Filament Track") != std::string::npos; }

};


class DevFirmware
{
public:
    DevFirmware(MachineObject* obj) : m_owner(obj) {}

private:
    [[maybe_unused]] MachineObject* m_owner = nullptr;
};

}