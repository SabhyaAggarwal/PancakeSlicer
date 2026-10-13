#include "TiltBudget.hpp"

#include "PatchTools.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"


#include <algorithm>
#include <cmath>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

double table_position(double phi, size_t n)
{
    double u = phi / (2. * M_PI) * double(n);
    return u - std::floor(u / double(n)) * double(n);
}

}

TiltBudget::TiltBudget(const ToolKinematics &kinematics, std::vector<double> slope_limit, double margin, double max_tilt,
                       double max_residual, double residual_from)
    : m_slope_limit(std::move(slope_limit)), m_margin(margin), m_max_residual(max_residual), m_residual_from(residual_from)
{
    if (kinematics.rotary_count() == 0)
        return;
    m_reach.resize(Azimuths);
    for (int k = 0; k < Azimuths; ++k)
        m_reach[k] = std::min(kinematics.max_tilt(2. * M_PI * k / Azimuths), max_tilt);
}

double TiltBudget::slope_limit(double phi) const
{
    if (m_slope_limit.empty())
        return 0.5 * M_PI;
    const size_t n = m_slope_limit.size();
    const double u = table_position(phi, n);
    const size_t i = size_t(u) % n;
    const double t = u - std::floor(u);
    return (1. - t) * m_slope_limit[i] + t * m_slope_limit[(i + 1) % n];
}

double TiltBudget::reach(double psi) const
{
    if (m_reach.empty())
        return 0.;
    const size_t i = size_t(table_position(psi, m_reach.size())) % m_reach.size();
    return std::min(m_reach[i], m_reach[(i + 1) % m_reach.size()]);
}

double TiltBudget::tilt(const Vec2d &g) const
{
    const double beta = std::atan(g.norm());
    if (beta <= 0. || this->empty())
        return 0.;
    const double phi = std::atan2(g.y(), g.x());
    return std::clamp(std::max(beta - this->slope_limit(phi) + m_margin, beta > m_residual_from ? beta - m_max_residual : 0.), 0.,
                      std::min(beta, this->reach(phi + M_PI)));
}

Vec3d TiltBudget::axis(const Vec2d &g) const
{
    const double tilt = this->tilt(g);
    if (tilt <= 0.)
        return Vec3d::UnitZ();
    const double phi = std::atan2(g.y(), g.x());
    return Vec3d(-std::sin(tilt) * std::cos(phi), -std::sin(tilt) * std::sin(phi), std::cos(tilt));
}

bool TiltBudget::needs_tilt(const Vec2d &g) const
{
    return ! this->empty() && g.norm() > 0. && std::atan(g.norm()) > this->slope_limit(std::atan2(g.y(), g.x()));
}

TiltBudget make_tilt_budget(const PrintConfig &config, const HeadEnvelope &head, double gap)
{
    if (config.nonplanar_kinematics.value == RotaryLayout::None)
        return {};
    return TiltBudget(*make_tool_kinematics(config), head.slope_map(TiltBudget::Azimuths, gap), 3. * M_PI / 180.,
                      head_band_tilt_limit(head));
}

}
