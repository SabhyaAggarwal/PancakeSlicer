#include "StrengthRoads.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"
#include <cstddef>

#include "HeightField.hpp"
#include "PatchTools.hpp"
#include "ShellPaths.hpp"

#include "libslic3r/Flow.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Print.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double KappaFull = 0.05;
constexpr double AspectFull = 2.;
constexpr double MinCoherence = 0.5;
constexpr int Step = 2;

}

bool strength_roads_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    const NonplanarMode mode = object.nonplanar_mode.value;
    return config.nonplanar_strength_roads.value && (mode == NonplanarMode::Auto || mode == NonplanarMode::Manual);
}

StrengthField::StrengthField(const PrintObject &object)
{
    double a = 0., sx = 0., sy = 0., sxx = 0., syy = 0., sxy = 0.;
    Vec2d  o(0., 0.);
    bool   has_origin = false;
    for (const Layer *layer : object.layers())
        for (const ExPolygon &expoly : layer->lslices)
            for (const Polygon &ring : to_polygons(expoly)) {
                const Points &pts = ring.points;
                if (pts.size() < 3)
                    continue;
                if (!has_origin) {
                    o          = unscale(pts.front());
                    has_origin = true;
                }
                for (size_t i = 0; i < pts.size(); ++i) {
                    const Vec2d  p = unscale(pts[i]) - o, q = unscale(pts[(i + 1) % pts.size()]) - o;
                    const double c = p.x() * q.y() - q.x() * p.y();
                    a += 0.5 * c;
                    sx += (p.x() + q.x()) * c / 6.;
                    sy += (p.y() + q.y()) * c / 6.;
                    sxx += (p.x() * p.x() + p.x() * q.x() + q.x() * q.x()) * c / 12.;
                    syy += (p.y() * p.y() + p.y() * q.y() + q.y() * q.y()) * c / 12.;
                    sxy += (p.x() * q.y() + 2. * p.x() * p.y() + 2. * q.x() * q.y() + q.x() * p.y()) * c / 24.;
                }
            }
    if (a <= 0.)
        return;
    const double cx = sx / a, cy = sy / a;
    const double cxx = sxx / a - cx * cx, cyy = syy / a - cy * cy, cxy = sxy / a - cx * cy;
    const double mid = 0.5 * (cxx + cyy), rad = std::hypot(0.5 * (cxx - cyy), cxy);
    m_long_axis      = 0.5 * std::atan2(2. * cxy, cxx - cyy);
    m_aspect         = mid - rad > 1e-12 ? std::sqrt((mid + rad) / (mid - rad)) : std::numeric_limits<double>::infinity();
}

std::vector<Vec2d> StrengthField::node_vectors(const HeightField &layer, const ExPolygons &area) const
{
    const Mask   inside = rasterize(area, layer);
    const double h      = Step * layer.pitch();
    auto         z_at   = [&layer](int ix, int iy) {
        return ix >= 0 && iy >= 0 && ix < layer.nx() && iy < layer.ny() ? layer.at(ix, iy) : HeightField::NoData;
    };
    const double w_long = std::clamp((m_aspect - 1.) / (AspectFull - 1.), 0., 1.);
    const Vec2d  v_long(std::cos(2. * m_long_axis), std::sin(2. * m_long_axis));
    std::vector<Vec2d> out;
    for (int iy = 0; iy < layer.ny(); ++iy)
        for (int ix = 0; ix < layer.nx(); ++ix) {
            const float z = layer.at(ix, iy);
            if (!inside[size_t(iy) * layer.nx() + ix] || std::isnan(z))
                continue;
            Vec2d v = w_long * v_long;
            const float xm = z_at(ix - Step, iy), xp = z_at(ix + Step, iy), ym = z_at(ix, iy - Step), yp = z_at(ix, iy + Step);
            const float a0 = z_at(ix - Step, iy - Step), a1 = z_at(ix + Step, iy - Step), a2 = z_at(ix - Step, iy + Step),
                        a3 = z_at(ix + Step, iy + Step);
            if (!std::isnan(xm + xp + ym + yp + a0 + a1 + a2 + a3)) {
                const double fx = (xp - xm) / (2. * h), fy = (yp - ym) / (2. * h);
                const double fxx = (xp - 2. * z + xm) / (h * h), fyy = (yp - 2. * z + ym) / (h * h), fxy = (a3 - a1 - a2 + a0) / (4. * h * h);
                const double w = std::sqrt(1. + fx * fx + fy * fy);
                const double E = 1. + fx * fx, F = fx * fy, G = 1. + fy * fy, det = E * G - F * F;
                const double L = fxx / w, M = fxy / w, N = fyy / w;
                const double s11 = (G * L - F * M) / det, s12 = (G * M - F * N) / det, s21 = (E * M - F * L) / det, s22 = (E * N - F * M) / det;
                const double tr = 0.5 * (s11 + s22), disc = std::sqrt(std::max(0., 0.25 * (s11 - s22) * (s11 - s22) + s12 * s21));
                const double k1 = tr + disc, k2 = tr - disc;
                const double kmin = std::abs(k1) < std::abs(k2) ? k1 : k2;
                const double w_curv = std::clamp((std::max(std::abs(k1), std::abs(k2)) - std::abs(kmin)) / KappaFull, 0., 1.);
                Vec2d u(s12, kmin - s11);
                if (Vec2d alt(kmin - s22, s21); alt.squaredNorm() > u.squaredNorm())
                    u = alt;
                if (w_curv > 0. && u.squaredNorm() > 1e-24) {
                    const double t = std::atan2(u.y(), u.x());
                    v = w_curv * Vec2d(std::cos(2. * t), std::sin(2. * t)) + (1. - w_curv) * v;
                }
            }
            out.emplace_back(v);
        }
    return out;
}

bool StrengthField::direction(const HeightField &layer, const ExPolygons &area, double &angle) const
{
    const std::vector<Vec2d> v = node_vectors(layer, area);
    if (v.empty())
        return false;
    Vec2d sum(0., 0.);
    for (const Vec2d &x : v)
        sum += x;
    sum /= double(v.size());
    if (sum.norm() < MinCoherence)
        return false;
    angle = 0.5 * std::atan2(sum.y(), sum.x());
    return true;
}

double StrengthField::aligned_share(const HeightField &layer, const ExPolygons &area, double angle) const
{
    size_t directed = 0, aligned = 0;
    for (const Vec2d &v : node_vectors(layer, area))
        if (v.norm() >= MinCoherence) {
            ++directed;
            aligned += std::cos(std::atan2(v.y(), v.x()) - 2. * angle) >= 0.5;
        }
    return directed > 0 ? double(aligned) / double(directed) : 0.;
}

bool level_enough(const HeightField &surface, const ExPolygons &area, double angle, double reference)
{
    const double spacing = 1.;
    const Flow   flow(float(spacing), 0.2f, float(spacing));
    auto tilt = [&](double a) { return path_tilt(fill_solid(area, ipRectilinear, spacing, a, false, flow, 0.), surface, surface.pitch()).mean; };
    return tilt(angle) <= tilt(reference) + std::tan(5. * PI / 180.);
}

}
