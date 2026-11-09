#pragma once

#include <string>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

std::string split_sublayer_walls(PrintObject &object);

}
