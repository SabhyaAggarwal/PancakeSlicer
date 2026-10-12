#include "Repair.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::Nonplanar {

HeightField surface_cap_field(const RepairHints &hints, const HeightField &like)
{
    HeightField cap = like;
    std::fill(cap.data().begin(), cap.data().end(), HeightField::NoData);
    for (const RepairHints::SurfaceCap &c : hints.surface_caps) {
        const int ix0 = std::max(0, int(std::floor((c.p.x() - c.radius - like.origin().x()) / like.pitch())));
        const int iy0 = std::max(0, int(std::floor((c.p.y() - c.radius - like.origin().y()) / like.pitch())));
        const int ix1 = std::min(like.nx() - 1, int(std::ceil((c.p.x() + c.radius - like.origin().x()) / like.pitch())));
        const int iy1 = std::min(like.ny() - 1, int(std::ceil((c.p.y() + c.radius - like.origin().y()) / like.pitch())));
        for (int iy = iy0; iy <= iy1; ++iy)
            for (int ix = ix0; ix <= ix1; ++ix)
                if ((like.node(ix, iy) - c.p).squaredNorm() <= c.radius * c.radius) {
                    float &z = cap.at(ix, iy);
                    z = std::isnan(z) ? float(c.z) : std::min(z, float(c.z));
                }
    }
    return cap;
}

}
