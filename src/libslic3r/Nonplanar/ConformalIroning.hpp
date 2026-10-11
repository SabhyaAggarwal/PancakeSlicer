#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

void build_conformal_ironing(const PrintObject &object, ConformalResult &result);

}
