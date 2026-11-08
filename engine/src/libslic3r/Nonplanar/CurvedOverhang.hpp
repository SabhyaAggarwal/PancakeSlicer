#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

constexpr double BoreHalfSpan = 2.5;

void build_curved_overhangs(PrintObject &object, ConformalResult &result);

}
