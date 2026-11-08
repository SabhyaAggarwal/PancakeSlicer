#pragma once
#include <string>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct AtomToolpath;

bool lean_vase_considered(const PrintObject &object);

bool lean_vase_flares(const PrintObject &object);

AtomToolpath lean_vase_toolpath(const PrintObject &object, std::string &why);

bool lean_vase_in_time(const PrintObject &object, const AtomToolpath &plan, std::string &why);

}
