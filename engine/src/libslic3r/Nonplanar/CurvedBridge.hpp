#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

constexpr double MaxBridgeSpan = 45.;

void build_curved_bridges(PrintObject &object, ConformalResult &result);

}
