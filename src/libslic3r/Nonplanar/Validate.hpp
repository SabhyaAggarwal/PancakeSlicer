#pragma once
#include "libslic3r/PrintBase.hpp"

#include <vector>

namespace Slic3r {
class Print;
}

namespace Slic3r::Nonplanar {

StringObjectException validate(const Print &print, std::vector<StringObjectException> *warnings);

}
