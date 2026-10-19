#pragma once
#include <string>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct AtomToolpath;

bool lean_vase_considered(const PrintObject &object);

bool lean_vase_flares(const PrintObject &object);

AtomToolpath lean_vase_toolpath(const PrintObject &object, std::string &why);

}
