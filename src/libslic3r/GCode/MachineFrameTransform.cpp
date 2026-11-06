#include "MachineFrameTransform.hpp"
#include "../Geometry.hpp"
#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "../libslic3r.h"

#include <cmath>

namespace Slic3r {

bool MachineFrameTransform::init_from_config(const PrintConfig &config)
{
    m_active            = false;
    m_transform         = Transform3d::Identity();
    m_transform_inverse = Transform3d::Identity();

    if (!config.belt_printer.value)
        return false;





    const BeltRotationAxis axis = config.belt_slice_rotation.value;
    if (axis == BeltRotationAxis::None || axis == BeltRotationAxis::Z)
        return false;

    const double angle_deg = config.belt_frame_tilt_decouple.value
        ? config.belt_frame_tilt_angle.value
        : config.belt_slice_rotation_angle.value;
    if (std::abs(angle_deg) <= EPSILON)
        return false;

    const double angle_rad = Geometry::deg2rad(angle_deg);
    const double sin_a      = std::sin(angle_rad);
    if (std::abs(sin_a) <= EPSILON)
        return false;
    const double cot_a   = std::cos(angle_rad) / sin_a;
    const double inv_sin = 1.0 / std::abs(sin_a);










    Matrix3d shear = Matrix3d::Identity();
    Matrix3d scale = Matrix3d::Identity();
    if (axis == BeltRotationAxis::X) {
        shear(2, 1) =  cot_a;
        scale(1, 1) =  inv_sin;
    } else {
        shear(2, 0) = -cot_a;
        scale(0, 0) =  inv_sin;
    }




    Transform3d combined = Transform3d::Identity();
    combined.linear() = scale * shear;

    if (combined.isApprox(Transform3d::Identity()))
        return false;

    m_transform         = combined;
    m_transform_inverse = combined.inverse();
    m_active            = true;
    return true;
}

Vec3d MachineFrameTransform::apply(const Vec3d &pos) const
{
    if (!m_active)
        return pos;
    return m_transform * pos;
}

Vec3d MachineFrameTransform::apply_inverse(const Vec3d &pos) const
{
    if (!m_active)
        return pos;
    return m_transform_inverse * pos;
}

}
