#include "ConformalTravel.hpp"

#include "libslic3r/Exception.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace Nonplanar {

namespace {

double segment_distance(const Vec2d &a, const Vec2d &b, const Vec2d &c, const Vec2d &d)
{
    auto point_segment = [](const Vec2d &p, const Vec2d &s0, const Vec2d &s1) {
        const Vec2d  v  = s1 - s0;
        const double l2 = v.squaredNorm();
        const double t  = l2 > 0. ? std::clamp((p - s0).dot(v) / l2, 0., 1.) : 0.;
        return (s0 + t * v - p).norm();
    };
    auto cross = [](const Vec2d &u, const Vec2d &v) { return u.x() * v.y() - u.y() * v.x(); };
    const double d1 = cross(b - a, c - a), d2 = cross(b - a, d - a), d3 = cross(d - c, a - c), d4 = cross(d - c, b - c);
    if (((d1 > 0. && d2 < 0.) || (d1 < 0. && d2 > 0.)) && ((d3 > 0. && d4 < 0.) || (d3 < 0. && d4 > 0.)))
        return 0.;
    return std::min({ point_segment(a, c, d), point_segment(b, c, d), point_segment(c, a, b), point_segment(d, a, b) });
}

double max_land_depth(const MaterialColumns &columns, const HeadEnvelope &land, const Vec3d &a, const Vec3d &b)
{
    double     depth = -std::numeric_limits<double>::infinity();
    const int  steps = std::max(1, int(std::ceil((b - a).norm() / 0.1)));
    for (int k = 0; k < steps; ++k)
        depth = std::max(depth, -columns.swept_clearance(a + (b - a) * (double(k) / steps), a + (b - a) * (double(k + 1) / steps), land));
    return depth;
}

Vec3d off_bead(const MaterialColumns &columns, const HeadEnvelope &land, const Vec3d &tip, double hop)
{
    const Vec3d up = tip + Vec3d(0., 0., hop);
    Vec2d       g  = material_gradient(columns, tip.head<2>());
    if (g.norm() > 1.5 || g.norm() < 0.02)
        return up;
    const Vec3d  off      = tip + hop * Vec3d(-g.x(), -g.y(), 1.);
    const double accepted = std::max(-columns.clearance(tip, land), 0.) + 0.01;
    return max_land_depth(columns, land, tip, off) > accepted ? up : off;
}

}

void FreshBeads::add(const Vec3d &a, const Vec3d &b, double width, double time)
{
    m_beads.push_back({ a.head<2>(), b.head<2>(), 0.5 * width, time });
}

void FreshBeads::prune(double now, double age)
{
    while (!m_beads.empty() && m_beads.front().time < now - age)
        m_beads.pop_front();
}

int FreshBeads::crossings(const Vec2d &a, const Vec2d &b, double now, double age, double reach) const
{
    int count = 0;
    for (auto it = m_beads.rbegin(); it != m_beads.rend() && it->time >= now - age; ++it)
        if (segment_distance(a, b, it->a, it->b) <= reach + it->half_width)
            ++count;
    return count;
}

HeadEnvelope nozzle_land(const HeadEnvelope &head)
{
    HeadEnvelope land;
    const double r = head.tip_flat_radius + 0.1;
    land.boxes.push_back({ "nozzle_land", Vec3d(-r, -r, 0.), Vec3d(r, r, std::max(head.tip_allowance.height, 1.)), false, false, true });
    return land;
}

Vec2d material_gradient(const MaterialColumns &columns, const Vec2d &p, double radius)
{
    const double h = 0.125;
    auto top = [&](const Vec2d &q) { return columns.top_in_rect(BoundingBoxf(q - Vec2d(h, h), q + Vec2d(h, h))); };
    auto diff = [&](const Vec2d &d) {
        const double a = top(p - d), b = top(p + d);
        return std::isfinite(a) && std::isfinite(b) ? (b - a) / (2. * radius) : 0.;
    };
    return Vec2d(diff(Vec2d(radius, 0.)), diff(Vec2d(0., radius)));
}

bool over_material(const MaterialColumns &columns, const Vec3d &a, const Vec3d &b, double depth)
{
    const double floor = std::min(a.z(), b.z()) - depth;
    const double h     = 0.125;
    const int    steps = std::max(1, int(std::ceil((b - a).head<2>().norm() / 0.25)));
    for (int k = 0; k <= steps; ++k) {
        const Vec2d q = (a + (b - a) * (double(k) / steps)).head<2>();
        if (!(columns.top_in_rect(BoundingBoxf(q - Vec2d(h, h), q + Vec2d(h, h))) >= floor))
            return false;
    }
    return true;
}

bool material_route(const MaterialColumns &columns, const Vec3d &a, const Vec3d &b, double depth, Vec2d &via)
{
    via = Vec2d::Constant(std::numeric_limits<double>::quiet_NaN());
    if (over_material(columns, a, b, depth))
        return true;
    const Vec2d  A = a.head<2>(), B = b.head<2>();
    const double L = (B - A).norm();
    if (L < 1.)
        return false;
    const Vec2d  mid  = 0.5 * (A + B);
    const Vec2d  perp = Vec2d(-(B - A).y(), (B - A).x()) / L;
    const double mz   = 0.5 * (a.z() + b.z());
    double       best = 1.25 * L + 2.;
    bool         found = false;
    for (double k : { 0.15, 0.3 })
        for (double side : { -1., 1. }) {
            const Vec2d  w   = mid + side * k * L * perp;
            const double len = (w - A).norm() + (B - w).norm();
            const Vec3d  W(w.x(), w.y(), mz);
            if (len <= best && over_material(columns, a, W, depth) && over_material(columns, W, b, depth)) {
                best = len, via = w, found = true;
            }
        }
    return found;
}

double pressure_retract_scale(double end_bead_height, double layer_height)
{
    return end_bead_height > 0. && layer_height > 0. ? std::clamp(end_bead_height / layer_height, 0.5, 1.25) : 1.;
}

Vec3d normal_departure(const MaterialColumns &columns, const HeadEnvelope &land, const Vec3d &from, double hop)
{
    return hop > 0. ? off_bead(columns, land, from, hop) : from;
}

SurfaceTravel plan_surface_travel(const MaterialColumns &columns, const Vec3d &from, const Vec3d &to, bool from_on_bead,
                                  bool to_on_bead, const SurfaceTravelParams &params)
{
    SurfaceTravel out;
    const double  c   = params.clearance;
    const Vec3d dep = from_on_bead ? off_bead(columns, params.land, from, c) : from;
    const Vec3d arr = to_on_bead ? off_bead(columns, params.land, to, c) : to;

    const Vec2d  A = dep.head<2>(), B = arr.head<2>();
    const double L = (B - A).norm();
    const double reach = params.land.boxes.empty() ? 0.3 : params.land.boxes.front().max.x();
    auto wet = [&](const std::vector<Vec2d> &route) {
        int n = 0;
        if (params.fresh != nullptr)
            for (size_t i = 0; i + 1 < route.size(); ++i)
                n += params.fresh->crossings(route[i], route[i + 1], params.now, params.wet_age, reach);
        return n;
    };
    std::vector<Vec2d> route{ A, B };
    if (std::isfinite(params.via.x()))
        route.insert(route.begin() + 1, params.via);
    out.wet = wet(route);
    if (out.wet > 0 && L > 1. && route.size() == 2) {
        const Vec2d  mid  = 0.5 * (A + B);
        const Vec2d  perp = Vec2d(-(B - A).y(), (B - A).x()) / L;
        double       best = L;
        for (double k : { 0.15, 0.3 })
            for (double side : { -1., 1. }) {
                const Vec2d              w = mid + side * k * L * perp;
                if (params.bed.defined && !params.bed.contains(w))
                    continue;
                const std::vector<Vec2d> r{ A, w, B };
                const double             len = (w - A).norm() + (B - w).norm();
                if (len > 1.25 * L + 2.)
                    continue;
                const int n = wet(r);
                if (n < out.wet || (n == out.wet && out.detoured && len < best)) {
                    route = r; out.wet = n; best = len; out.detoured = true;
                }
            }
    }

    std::vector<Vec2d> xy{ route.front() };
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const int n = std::max(1, int(std::ceil((route[i + 1] - route[i]).norm() / params.sample)));
        for (int k = 1; k <= n; ++k)
            xy.emplace_back(route[i] + (route[i + 1] - route[i]) * (double(k) / n));
    }
    const size_t        n   = xy.size();
    std::vector<double> req(n > 1 ? n - 1 : 0, -std::numeric_limits<double>::infinity());
    std::vector<double> base(req.size(), -std::numeric_limits<double>::infinity());
    for (size_t i = 0; i + 1 < n; ++i) {
        const Vec2d  d = xy[i + 1] - xy[i];
        const double l = d.norm();
        const double slope = l > EPSILON ? material_gradient(columns, 0.5 * (xy[i] + xy[i + 1])).dot(d / l) : 0.;
        const double hop   = c * std::clamp(1. - 0.5 * slope, 0.6, 1.6);
        if (!params.nozzle.boxes.empty()) {
            base[i] = safe_travel_z(columns, params.nozzle, xy[i], xy[i + 1], 0.);
            req[i]  = base[i] + hop;
        }
        if (!params.body.boxes.empty())
            req[i] = std::max(req[i], safe_travel_z(columns, params.body, xy[i], xy[i + 1], params.body_clearance));
    }
    std::vector<double> z(n, -std::numeric_limits<double>::infinity());
    for (size_t i = 0; i + 1 < n; ++i) {
        z[i]     = std::max(z[i], req[i]);
        z[i + 1] = std::max(z[i + 1], req[i]);
    }
    const double floor = std::min({ dep.z(), arr.z(), params.floor });
    for (size_t i = 0; i + 1 < n; ++i)
        if (!std::isfinite(base[i])) {
            z[i]     = std::max(z[i], floor);
            z[i + 1] = std::max(z[i + 1], floor);
        }
    for (double &v : z)
        v = std::max({ v, params.min_z, c });
    z.front() = std::max(z.front(), dep.z());
    z.back()  = std::max(z.back(), arr.z());
    for (size_t i = 1; i < n; ++i)
        z[i] = std::max(z[i], z[i - 1] - params.max_grade * (xy[i] - xy[i - 1]).norm());
    for (size_t i = n - 1; i > 0; --i)
        z[i - 1] = std::max(z[i - 1], z[i] - params.max_grade * (xy[i] - xy[i - 1]).norm());
    if (params.ceiling > 0.)
        for (double v : z)
            if (v > params.ceiling + EPSILON)
                throw RuntimeError("Non-planar travel cannot clear printed material within the printer height. Reduce the model height or move the objects apart.");

    out.max_hop = std::max(dep.z() - from.z(), arr.z() - to.z());
    for (size_t i = 0; i + 1 < n; ++i)
        if (std::isfinite(base[i]))
            out.max_hop = std::max(out.max_hop, std::max(z[i], z[i + 1]) - base[i]);

    auto push = [&out](const Vec3d &p) {
        if (out.points.empty() || (out.points.back() - p).norm() > EPSILON)
            out.points.emplace_back(p);
    };
    if ((dep - from).norm() > EPSILON)
        push(dep);
    push(Vec3d(xy.front().x(), xy.front().y(), z.front()));
    for (size_t i = 1; i + 1 < n; ++i) {
        const Vec3d p(xy[i].x(), xy[i].y(), z[i]);
        const Vec3d prev = out.points.empty() ? dep : out.points.back();
        const Vec3d next(xy[i + 1].x(), xy[i + 1].y(), z[i + 1]);
        const Vec3d u = p - prev, v = next - p;
        if (u.norm() > EPSILON && v.norm() > EPSILON && u.normalized().dot(v.normalized()) > 1. - 1e-6)
            continue;
        push(p);
    }
    push(Vec3d(xy.back().x(), xy.back().y(), z.back()));
    push(arr);
    push(to);
    return out;
}

}
}
