#pragma once
#include "HeadEnvelope.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Point.hpp"

#include <vector>

namespace Slic3r::Nonplanar {

class TiltBudget
{
public:
    static constexpr int Azimuths = 72;

    TiltBudget() = default;
    TiltBudget(const ToolKinematics &kinematics, std::vector<double> slope_limit, double margin = 3. * M_PI / 180.,
               double max_tilt = 0.5 * M_PI, double max_residual = 0.5 * M_PI, double residual_from = 0.);

    bool empty() const { return m_reach.empty(); }

    double slope_limit(double phi) const;
    double reach(double psi) const;

    double tilt(const Vec2d &g) const;
    Vec3d  axis(const Vec2d &g) const;

private:
    std::vector<double> m_slope_limit;
    std::vector<double> m_reach;
    double              m_margin = 0.;
    double              m_max_residual = 0.5 * M_PI;
    double              m_residual_from = 0.;
};

}
