#include "BeltSliceStrategy.hpp"
#include "Model.hpp"
#include "BeltTransform.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"

#include <limits>
#include <algorithm>

namespace Slic3r {

void BeltSliceStrategy::apply_preslice_transforms(Transform3d           &trafo,
                                                  const PrintConfig     &config,
                                                  const ModelVolumePtrs &model_volumes,
                                                  double                *out_belt_min_z)
{
    bool has_rotation = false;
    if (config.belt_printer.value) {
        const Matrix3d rot = BeltTransformPipeline::build_rotation_matrix(config, &has_rotation);
        if (has_rotation) {
            Transform3d belt_xform = Transform3d::Identity();
            belt_xform.linear() = rot;
            trafo = belt_xform * trafo;
        }
    }

    if (!has_rotation)
        return;

    BeltTransformPipeline::BeltFloorParams floor;
    const bool has_floor = BeltTransformPipeline::floor_shear(config, floor);
    double min_z = std::numeric_limits<double>::max();
    for (const ModelVolume *mv : model_volumes) {
        if (!mv->is_model_part()) continue;
        Transform3d vol_trafo = trafo * mv->get_matrix();
        const auto &its = mv->mesh().its;
        for (const stl_vertex &v : its.vertices) {
            Vec3d vm = v.cast<double>();
            Vec3d pt = vol_trafo * vm;
            min_z = std::min(min_z, pt.z());
            if (has_floor)
                min_z = std::min(min_z, floor.shear_factor * (floor.from_axis == 0 ? pt.x() : pt.y()));
        }
    }
    if (has_floor && min_z != std::numeric_limits<double>::max())
        min_z -= BeltTransformPipeline::frame_margin(floor);
    const double z_shift_val = (min_z < 0. && min_z != std::numeric_limits<double>::max()) ? -min_z : 0.;
    if (z_shift_val > 0.) {
        Transform3d z_shift = Transform3d::Identity();
        z_shift.matrix()(2, 3) = z_shift_val;
        trafo = z_shift * trafo;
    }
    if (out_belt_min_z && config.belt_printer.value) {
        *out_belt_min_z = (min_z != std::numeric_limits<double>::max()) ? min_z : 0.;
    }
}

}
