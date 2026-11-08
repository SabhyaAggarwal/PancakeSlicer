#ifndef slic3r_BeltBackTransform_hpp_
#define slic3r_BeltBackTransform_hpp_

#include "../libslic3r.h"
#include "../Point.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {

class BeltBackTransform
{
public:
    BeltBackTransform() = default;

    bool init_from_config(const PrintConfig &config);

    Vec3d apply(const Vec3d &pos) const;

private:
    bool       m_active  = false;
    Transform3d m_inverse = Transform3d::Identity();
};

}

#endif
