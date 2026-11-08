#pragma once
#include <nlohmann/json.hpp>
#include <wx/string.h>

namespace Slic3r {

class MachineObject;

class DevInfo
{
public:
    DevInfo(MachineObject* obj) : m_owner(obj) {};

public:

private:

    [[maybe_unused]] MachineObject* m_owner = nullptr;
};

}