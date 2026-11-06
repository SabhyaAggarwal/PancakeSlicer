#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;







void build_conformal_volume(PrintObject &object, ConformalResult &result);

}
