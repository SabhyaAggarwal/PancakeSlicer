#pragma once
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <vector>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

class HeightField;

bool strength_roads_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object);

class StrengthField
{
public:
    StrengthField() = default;
    explicit StrengthField(const PrintObject &object);

    double aspect() const { return m_aspect; }
    bool direction(const HeightField &layer, const ExPolygons &area, double &angle) const;
    double aligned_share(const HeightField &layer, const ExPolygons &area, double angle) const;

private:
    std::vector<Vec2d> node_vectors(const HeightField &layer, const ExPolygons &area) const;

    double m_long_axis = 0.;
    double m_aspect    = 1.;
};

bool level_enough(const HeightField &surface, const ExPolygons &area, double angle, double reference);

inline double ply_angle(double principal, int j) { return principal + (j % 2 != 0 ? 0.5 * PI : 0.); }

}
