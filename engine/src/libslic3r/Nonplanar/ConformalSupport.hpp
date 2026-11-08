#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

bool conformal_support_configured(const PrintObject &object);

bool conformal_support_wanted(const PrintObject &object);

void build_conformal_support(PrintObject &object, ConformalResult &result);

}
