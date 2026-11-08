#pragma once
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <vector>

namespace Slic3r {
class Print;
}

namespace Slic3r::Nonplanar {

StringObjectException validate(const Print &print, std::vector<StringObjectException> *warnings);

double curved_max_angle_limit(NonplanarMode mode);

void report_head(const Print &print);

StringObjectException validate_layer_gcodes(const Print &print, std::vector<StringObjectException> *warnings);

}
