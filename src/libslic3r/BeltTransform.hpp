#pragma once

#include "libslic3r.h"
#include "Point.hpp"
#include "BoundingBox.hpp"
#include "PrintConfig.hpp"
#include "Geometry.hpp"
#include "Config.hpp"

#include <cmath>

namespace Slic3r {

class ModelObject;






























class BeltTransformPipeline
{
public:






    static bool axis_remap_enabled(const PrintConfig &config) { return config.belt_printer.value; }

    static bool has_rotation(const PrintConfig &config)
    {
        return config.belt_slice_rotation.value != BeltRotationAxis::None &&
               std::abs(config.belt_slice_rotation_angle.value) > EPSILON;
    }










    struct PhysicalTilt { double tilt_x_deg = 0.; double tilt_y_deg = 0.; };

    static PhysicalTilt physical_tilt(BeltRotationAxis axis, double angle_deg)
    {
        PhysicalTilt t;
        double mag = std::abs(angle_deg);
        switch (axis) {
        case BeltRotationAxis::X: t.tilt_x_deg = mag; break;
        case BeltRotationAxis::Y: t.tilt_y_deg = mag; break;
        default: break;
        }
        return t;
    }






    static Matrix3d build_rotation_matrix(const PrintConfig &config, bool *has_rot_out = nullptr);




    static Transform3d build_forward_transform(const PrintConfig &config);



    struct BeltFloorParams {
        double shear_factor = 0.0;
        int    from_axis    = 1;
        double z_shift      = 0.0;
    };





    static bool floor_shear(const PrintConfig &config, BeltFloorParams &out);






    static double frame_margin(const BeltFloorParams &fp) { return 10. * std::abs(fp.shear_factor); }


    struct BeltHeightResult {
        double          object_height;
        BeltFloorParams floor_params;
    };




    static BeltHeightResult compute_belt_height_and_floor(
        const PrintConfig &config, const BoundingBoxf3 &bbox,
        double original_height);
};

}
