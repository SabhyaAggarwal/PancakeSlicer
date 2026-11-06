#ifndef slic3r_Fill_hpp_
#define slic3r_Fill_hpp_

#include <cstddef>
#include <memory.h>
#include <float.h>
#include <stdint.h>
#include <string>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class LayerRegion;
class PrintObject;



double calculate_infill_rotation_angle(const PrintObject *object, size_t layer_id,
                                      const double &fixed_infill_angle, const std::string &template_string);


class Filler
{
public:
    Filler() : fill(nullptr) {}
    ~Filler() {
        delete fill;
        fill = nullptr;
    }
    Fill        *fill;
    FillParams   params;
};

}

#endif
