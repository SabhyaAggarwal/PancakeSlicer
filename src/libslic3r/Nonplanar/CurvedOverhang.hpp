#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

void build_curved_overhangs(PrintObject &object, ConformalResult &result);

}
