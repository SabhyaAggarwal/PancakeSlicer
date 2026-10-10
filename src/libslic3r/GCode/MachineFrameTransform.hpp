#ifndef slic3r_MachineFrameTransform_hpp_
#define slic3r_MachineFrameTransform_hpp_

#include "../libslic3r.h"
#include "../Point.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {

class MachineFrameTransform
{
public:
    MachineFrameTransform() = default;

    bool init_from_config(const PrintConfig &config);

    Vec3d apply(const Vec3d &pos) const;

    Vec3d apply_inverse(const Vec3d &pos) const;

    bool is_active() const { return m_active; }

    const Transform3d& transform() const { return m_transform; }

private:
    bool        m_active            = false;
    Transform3d m_transform         = Transform3d::Identity();
    Transform3d m_transform_inverse = Transform3d::Identity();
};

}

#endif
