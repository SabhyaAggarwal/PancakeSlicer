#pragma once
#include <vector>
namespace Slic3r {
class PrintObject;
class Layer;
namespace Nonplanar {
void make_continuous_curved_tops(const PrintObject &object, const std::vector<Layer*> &shells);
}
}
