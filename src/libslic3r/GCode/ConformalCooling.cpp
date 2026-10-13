#include "ConformalCooling.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <string>

namespace Slic3r::Nonplanar {

SupportSnapshot::SupportSnapshot(const MaterialColumns &columns, const BoundingBoxf &area)
{
    if (!area.defined)
        return;
    m_top = columns.tops(area, m_origin, m_nx, m_pitch);
    m_ny  = m_nx > 0 ? int(m_top.size() / size_t(m_nx)) : 0;
    if (m_ny == 0)
        m_top.clear();
}

double SupportSnapshot::top(const Vec2d &xy) const
{
    if (m_top.empty())
        return -std::numeric_limits<double>::infinity();
    const int x = int(std::floor((xy.x() - m_origin.x()) / m_pitch));
    const int y = int(std::floor((xy.y() - m_origin.y()) / m_pitch));
    if (x < 0 || y < 0 || x >= m_nx || y >= m_ny)
        return -std::numeric_limits<double>::infinity();
    return m_top[size_t(y) * m_nx + x];
}

std::vector<float> road_fan_demand(const std::vector<ConformalPoint> &points, const std::vector<double> &speeds, double width,
                                   const SupportSnapshot &support, const std::function<Vec2d(const Vec2d &)> &gradient,
                                   const RoadFanParams &params)
{
    const size_t       n = points.size() < 2 ? 0 : points.size() - 1;
    std::vector<float> raw(n, 0.f);
    std::vector<double> t0(n, 0.), t1(n, 0.);
    double              t = 0.;
    for (size_t i = 0; i < n; ++i) {
        const ConformalPoint &a = points[i], &b = points[i + 1];
        const Vec3d           d   = b.pose.tip - a.pose.tip;
        const double          len = d.norm();
        t0[i] = t;
        t += i < speeds.size() && speeds[i] > 0. ? len / speeds[i] : 0.;
        t1[i] = t;
        const double h   = 0.5 * (a.bead_height + b.bead_height);
        const double dxy = d.head<2>().norm();
        if (h <= 0. || dxy < EPSILON)
            continue;
        const double h_z    = h * std::abs(0.5 * (a.pose.axis + b.pose.axis).normalized().z());
        const Vec3d  mid    = 0.5 * (a.pose.tip + b.pose.tip);
        const Vec2d  across = Vec2d(-d.y(), d.x()) / dxy;
        const Vec2d  g      = gradient ? gradient(mid.head<2>()) : Vec2d(Vec2d::Zero());
        const double tol    = h_z + 0.1;
        int          hang   = 0;
        if (!support.empty()) {
            for (int k = 0; k < params.lateral_samples; ++k) {
                const double off    = 0.8 * width * ((k + 0.5) / params.lateral_samples - 0.5);
                const Vec2d  xy     = mid.head<2>() + off * across;
                const double bottom = mid.z() - h_z + g.dot(off * across);
                if (support.top(xy) < bottom - tol)
                    ++hang;
            }
        }
        const double downhill = -d.z() / dxy;
        const double stretch  = std::clamp((downhill - params.downhill_start) / (params.downhill_full - params.downhill_start), 0., 1.);
        raw[i] = float(std::max(double(hang) / params.lateral_samples, params.downhill_weight * stretch));
    }
    std::vector<float> out(n, 0.f);
    std::deque<size_t> window;
    size_t             next = 0;
    for (size_t i = 0; i < n; ++i) {
        for (; next < n && t0[next] < t1[i] + params.lead; ++next) {
            while (!window.empty() && raw[window.back()] <= raw[next])
                window.pop_back();
            window.push_back(next);
        }
        while (!window.empty() && t1[window.front()] < t0[i] - params.hold)
            window.pop_front();
        const float u = window.empty() ? 0.f : raw[window.front()];
        out[i]        = float(std::round(u / params.step) * params.step);
    }
    return out;
}

std::vector<std::string> road_fan_tags(const std::vector<float> &demand, float &level)
{
    std::vector<std::string> tags(demand.size());
    char                     buf[32];
    for (size_t i = 0; i < demand.size(); ++i)
        if (std::abs(demand[i] - level) > 1e-3f) {
            level = demand[i];
            std::snprintf(buf, sizeof(buf), "%s%.2f\n", NpFanTag, level);
            tags[i] = buf;
        }
    return tags;
}

bool pass_is_solid(const ExtrusionEntityCollection &pass)
{
    bool any = false;
    for (const ExtrusionEntity *ee : pass.entities) {
        if (ee->is_collection() ? !pass_is_solid(*static_cast<const ExtrusionEntityCollection *>(ee)) : ee->role() == erInternalInfill)
            return false;
        any = true;
    }
    return any;
}

bool pass_is_top(const ExtrusionEntityCollection &pass)
{
    return std::any_of(pass.entities.begin(), pass.entities.end(), [](const ExtrusionEntity *ee) {
        return ee->is_collection() ? pass_is_top(*static_cast<const ExtrusionEntityCollection *>(ee)) : ee->role() == erTopSolidInfill;
    });
}

}
