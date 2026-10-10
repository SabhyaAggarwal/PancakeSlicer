#pragma once

#include "libslic3r.h"
#include "Point.hpp"
#include "BeltTransform.hpp"
#include "PrintConfig.hpp"
#include "Model.hpp"

namespace Slic3r {

class BeltSliceStrategy
{
public:
    static void apply_preslice_transforms(Transform3d           &trafo,
                                          const PrintConfig     &config,
                                          const ModelVolumePtrs &model_volumes,
                                          double                *out_belt_min_z = nullptr);
};

}
