#include "LeanVase.hpp"

#include "AtomToolpath.hpp"
#include "HeadEnvelope.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/libslic3r.h"

#include <math.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr int    Azimuths    = 360;
constexpr double MinLeanDeg  = 12.;
constexpr double MinTiltDeg  = 3.;
constexpr double InwardDeg   = 35.;
constexpr double TiltMargin  = 2.;
constexpr double WindLift    = 6.;
constexpr double WindTiltDeg = 4.;
constexpr double ScarfLength = 2.;
constexpr double SeamScatter = 8.;
constexpr double ScarfFloor  = 0.15;
constexpr int    TiltSmooth  = 7;
constexpr size_t MaxPoints   = 4000000;
constexpr double LeanChord   = 1.5;
constexpr double TableGap    = 0.5;
std::optional<std::vector<Vec2d>> meridian(const indexed_triangle_set &mesh, double phi, double inset)
{
    Matrix3d m;
    m << std::cos(phi), std::sin(phi), 0., 0., 0., 1., std::sin(phi), -std::cos(phi), 0.;
    MeshSlicingParamsEx params;
    params.trafo.linear() = m;
    const std::vector<ExPolygons> cut = slice_mesh_ex(mesh, { 0.f }, params);
    if (cut.empty())
        return std::nullopt;
    const Polygon *best = nullptr;
    size_t         start = 0;
    double         zmin  = std::numeric_limits<double>::infinity();
    for (const ExPolygon &ex : cut.front())
        for (const Point &p : ex.contour.points)
            if (p.x() > 0)
                zmin = std::min(zmin, unscale<double>(p.y()));
    double rmax = -1.;
    for (const ExPolygon &ex : cut.front())
        for (size_t i = 0; i < ex.contour.points.size(); ++i) {
            const Point &p = ex.contour.points[i];
            if (p.x() > 0 && unscale<double>(p.y()) < zmin + 0.05 && unscale<double>(p.x()) > rmax) {
                rmax  = unscale<double>(p.x());
                best  = &ex.contour;
                start = i;
            }
        }
    if (best == nullptr)
        return std::nullopt;
    std::vector<Vec2d> out { unscale(best->points[start]) };
    const size_t       n = best->points.size();
    for (size_t k = 1; k < n; ++k) {
        const Vec2d p = unscale(best->points[(start + k) % n]);
        const Vec2d d = p - out.back();
        if (d.norm() < 0.1)
            continue;
        if (d.y() < -0.05 || (d.x() < 0. && d.y() < 0.2 * -d.x()) || p.x() < inset)
            break;
        out.push_back(p);
    }
    if (out.size() < 2)
        return std::nullopt;
    std::vector<Vec2d> line(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        const Vec2d a = out[i > 0 ? i - 1 : 0], b = out[std::min(i + 1, out.size() - 1)];
        Vec2d       d = (b - a).normalized();
        const Vec2d outward(d.y(), -d.x());
        line[i] = out[i] - inset * outward;
        line[i].x() = std::max(line[i].x(), inset);
    }
    return line;
}

struct Profile
{
    std::vector<Vec2d>  rz;
    std::vector<double> lean, tilt, count;
};

Profile profile(std::vector<Vec2d> rz, double z_bottom, double h, double max_tilt)
{
    Profile p;
    p.rz = std::move(rz);
    const size_t n = p.rz.size();
    p.lean.resize(n);
    p.tilt.resize(n);
    p.count.assign(n, 0.);
    std::vector<double> arc(n, 0.);
    for (size_t i = 1; i < n; ++i)
        arc[i] = arc[i - 1] + (p.rz[i] - p.rz[i - 1]).norm();
    for (size_t i = 0; i < n; ++i) {
        size_t lo = i, hi = i;
        while (lo > 0 && arc[i] - arc[lo] < LeanChord)
            --lo;
        while (hi + 1 < n && arc[hi] - arc[i] < LeanChord)
            ++hi;
        const Vec2d d = p.rz[std::max(hi, std::min(i + 1, n - 1))] - p.rz[std::min(lo, i > 0 ? i - 1 : 0)];
        p.lean[i]     = std::atan2(d.x(), d.y());
        p.tilt[i]     = std::abs(p.lean[i]) < Geometry::deg2rad(MinLeanDeg) ? 0. :
                        p.lean[i] > 0. ? std::min(p.lean[i], max_tilt) : std::max(p.lean[i], -Geometry::deg2rad(InwardDeg));
    }
    for (size_t i = 1; i < n; ++i) {
        const Vec2d  a = p.rz[i - 1], b = p.rz[i];
        const double ds = (b - a).norm();
        double       z0 = a.y(), z1 = b.y(), part = 1.;
        if (z1 <= z_bottom)
            part = 0.;
        else if (z0 < z_bottom)
            part = (z1 - z_bottom) / std::max(z1 - z0, 1e-9);
        const double c = std::cos(0.5 * (p.lean[i - 1] + p.lean[i]) - 0.5 * (p.tilt[i - 1] + p.tilt[i]));
        p.count[i]     = p.count[i - 1] + part * ds * std::max(c, 0.5) / h;
    }
    return p;
}

Vec3d at_count(const Profile &p, double k)
{
    const auto   it = std::lower_bound(p.count.begin(), p.count.end(), k);
    const size_t i  = std::clamp<size_t>(size_t(it - p.count.begin()), 1, p.count.size() - 1);
    const double t  = std::clamp((k - p.count[i - 1]) / std::max(p.count[i] - p.count[i - 1], 1e-12), 0., 1.);
    const Vec2d  rz = p.rz[i - 1] + t * (p.rz[i] - p.rz[i - 1]);
    return { rz.x(), rz.y(), p.tilt[i - 1] + t * (p.tilt[i] - p.tilt[i - 1]) };
}

Vec3d lean_axis(double tilt, double phi)
{
    return { std::sin(tilt) * std::cos(phi), std::sin(tilt) * std::sin(phi), std::cos(tilt) };
}

std::vector<Profile> trace_wall(const PrintObject &object, double w, double h, double z_bottom, std::string &why)
{
    const double max_tilt = Geometry::deg2rad(std::max(0., object.print()->config().nonplanar_max_tilt.value - TiltMargin));
    const double threshold = Geometry::deg2rad(90. - object.config().multiplanar_overhang_threshold.value);

    double              ground = 0.;
    const TriangleMesh  mesh   = object.object_frame_mesh(ground);
    indexed_triangle_set its   = mesh.its;
    for (stl_vertex &v : its.vertices)
        v.z() += float(ground);

    double top = 0.;
    for (const stl_vertex &v : its.vertices)
        top = std::max(top, double(v.z()));
    std::vector<Profile> meridians(Azimuths);
    double               lean_max = 0.;
    for (int j = 0; j < Azimuths; ++j) {
        std::optional<std::vector<Vec2d>> rz = meridian(its, 2. * M_PI * j / Azimuths, 0.5 * w);
        if (!rz || rz->back().y() < z_bottom + 2. * h) {
            why = "a meridian of the wall could not be traced from the bottom to the rim";
            return {};
        }
        if (rz->back().y() < top - std::max(0.5, 2. * h)) {
            why = "the wall turns down or closes below the top (an undercut, a texture, a handle or a closed top)";
            return {};
        }
        meridians[j] = profile(std::move(*rz), z_bottom, h, max_tilt);
    }
    for (double z = z_bottom; z < top && lean_max <= threshold; z += 0.5) {
        double sum = 0.;
        for (const Profile &p : meridians) {
            const auto   it = std::lower_bound(p.rz.begin(), p.rz.end(), z, [](const Vec2d &a, double v) { return a.y() < v; });
            const size_t i  = std::min(size_t(it - p.rz.begin()), p.rz.size() - 1);
            sum += p.lean[i];
        }
        lean_max = std::max(lean_max, sum / double(meridians.size()));
    }
    if (lean_max <= threshold) {
        why = "the wall, averaged round, leans no further than the overhang threshold";
        return {};
    }
    return meridians;
}

}

bool lean_vase_considered(const PrintObject &object)
{
    const Print &print = *object.print();
    return print.config().spiral_mode.value && print.config().nonplanar_kinematics.value != RotaryLayout::None &&
           object.config().nonplanar_mode.value != NonplanarMode::Off && object.config().nonplanar_atom_toolpath.value.empty() &&
           object.num_printing_regions() > 0 && !object.layers().empty();
}

bool lean_vase_flares(const PrintObject &object)
{
    const Print &print = *object.print();
    if (!print.config().spiral_mode.value || print.config().nonplanar_kinematics.value == RotaryLayout::None ||
        object.config().nonplanar_mode.value == NonplanarMode::Off || !object.config().nonplanar_atom_toolpath.value.empty() ||
        object.num_printing_regions() == 0)
        return false;
    const PrintRegion &region = object.printing_region(0);
    const double       h      = object.config().layer_height.value;
    std::string        why;
    return !trace_wall(object, region.flow(object, frExternalPerimeter, h).width(), h,
                       std::max(region.config().bottom_shell_layers.value, 1) * h, why).empty();
}

AtomToolpath lean_vase_toolpath(const PrintObject &object, std::string &why)
{
    AtomToolpath plan;
    const double h      = object.config().layer_height.value;
    const PrintRegion &region = object.printing_region(0);
    const Flow   flow   = region.flow(object, frExternalPerimeter, h);
    const double w      = flow.width();
    const double z_base = object.slicing_parameters().object_print_z_min;
    const size_t bottom = std::min<size_t>(size_t(std::max(region.config().bottom_shell_layers.value, 1)), object.layers().size());
    const double z_bottom = object.layers()[bottom - 1]->print_z - z_base;
    std::vector<Profile> meridians = trace_wall(object, w, h, z_bottom, why);
    if (meridians.empty())
        return {};
    double rings = std::numeric_limits<double>::infinity();
    for (const Profile &p : meridians)
        rings = std::min(rings, p.count.back());

    auto add = [&plan, w](const Vec3d &tip, const Vec3d &axis, bool deposit, double height) {
        AtomPoint p;
        p.tip     = tip;
        p.axis    = axis.normalized();
        p.deposit = deposit && !plan.points.empty();
        p.width   = w;
        p.height  = height;
        plan.points.push_back(p);
    };
    auto travel = [&](const Vec3d &to, const Vec3d &axis) {
        if (!plan.points.empty()) {
            const Vec3d from = plan.points.back().tip;
            const double up  = std::max(from.z(), to.z()) + 0.6;
            add(Vec3d(from.x(), from.y(), up), Vec3d::UnitZ(), false, 0.);
            add(Vec3d(to.x(), to.y(), up), Vec3d::UnitZ(), false, 0.);
        }
        add(to, axis, false, 0.);
    };

    const double spacing = flow.spacing();
    for (size_t l = 0; l < bottom; ++l) {
        const Layer *layer = object.layers()[l];
        const double z     = layer->print_z - z_base;
        ExPolygons   ring  = offset_ex(layer->lslices, -float(scale_(0.5 * w)));
        bool         first = true;
        while (!ring.empty()) {
            for (const ExPolygon &ex : ring)
                for (const Polygon *poly : { &ex.contour }) {
                    Points pts = poly->points;
                    if (pts.size() < 3)
                        continue;
                    size_t s = 0;
                    if (!plan.points.empty()) {
                        double best = std::numeric_limits<double>::infinity();
                        for (size_t i = 0; i < pts.size(); ++i) {
                            const double d = (unscale(pts[i]) - plan.points.back().tip.head<2>()).squaredNorm();
                            if (d < best) { best = d; s = i; }
                        }
                    }
                    const Vec2d p0 = unscale(pts[s]);
                    const Vec3d start(p0.x(), p0.y(), z);
                    if (first || plan.points.empty() || (plan.points.back().tip - start).norm() > 1.6 * w)
                        travel(start, Vec3d::UnitZ());
                    else
                        add(start, Vec3d::UnitZ(), true, layer->height);
                    first = false;
                    for (size_t i = 1; i <= pts.size(); ++i) {
                        const Vec2d q = unscale(pts[(s + i) % pts.size()]);
                        add(Vec3d(q.x(), q.y(), z), Vec3d::UnitZ(), true, layer->height);
                    }
                }
            ring = offset_ex(ring, -float(scale_(spacing)));
        }
    }

    const int K          = int(std::floor(rings));
    const double min_tilt = Geometry::deg2rad(MinTiltDeg);
    const double max_tilt = Geometry::deg2rad(std::max(0., object.print()->config().nonplanar_max_tilt.value - TiltMargin));
    const HeadEnvelope head = resolve_head_envelope(object.print()->config());
    auto drop = [&head](double t) {
        double lowest = 0.;
        for (const HeadBox &b : head.boxes)
            if (!b.nozzle && !b.infinite_x && !b.infinite_y)
                lowest = std::min(lowest, -std::sin(t) * std::max(std::abs(b.min.x()), std::abs(b.max.x())) + std::cos(t) * b.min.z());
        return lowest;
    };
    std::vector<double> table_cap(181);
    for (size_t i = 0; i < table_cap.size(); ++i) {
        const double z = 0.1 * double(i);
        double       t = max_tilt;
        while (t > 0. && z + drop(t) < TableGap)
            t -= Geometry::deg2rad(0.5);
        table_cap[i] = std::max(t, 0.);
    }
    auto cap_at = [&table_cap, max_tilt](double z) { const double i = z / 0.1; return i >= double(table_cap.size() - 1) ? max_tilt : table_cap[size_t(std::max(0., i))]; };
    bool      wound      = false;
    int       dir        = 1;
    int       tilted_dir = 0;
    int       seam       = 0;
    double    offset     = 0.;
    uint32_t  rng        = 0x9e3779b9u;
    auto      uniform    = [&rng]() { rng = rng * 1664525u + 1013904223u; return double(rng >> 8) / double(1u << 24); };
    for (int k = 1; k <= K; ++k) {
        std::vector<Vec3d> ring(Azimuths);
        double             tilt_max = 0., tilt_signed = 0.;
        for (int j = 0; j < Azimuths; ++j) {
            ring[j] = at_count(meridians[j], double(k));
            if (std::abs(ring[j].z()) > tilt_max) {
                tilt_max    = std::abs(ring[j].z());
                tilt_signed = ring[j].z();
            }
        }
        const bool lean    = tilt_max > 0.;
        const int  sign    = tilt_signed < 0. ? -1 : 1;
        if (lean) {
            std::vector<double> t(Azimuths);
            for (int j = 0; j < Azimuths; ++j)
                t[j] = std::max(sign * ring[j].z(), min_tilt);
            for (int j = 0; j < Azimuths; ++j) {
                double sum = 0.;
                for (int d = -TiltSmooth; d <= TiltSmooth; ++d)
                    sum += t[(j + d + Azimuths) % Azimuths];
                ring[j].z() = sign * sum / (2 * TiltSmooth + 1);
            }
        }
        const int  wanted  = lean ? (sign > 0 ? 0 : Azimuths / 2) : seam;
        bool       restart = k == 1 || wanted != seam;
        if (lean) {
            if (!wound) {
                const double top = plan.points.empty() ? 0. : plan.points.back().tip.z();
                const Vec3d  centre(0., 0., std::max(top, ring[wanted].y()) + WindLift);
                const double t = Geometry::deg2rad(WindTiltDeg);
                travel(centre, Vec3d::UnitZ());
                for (int i = 0; i <= 12; ++i)
                    add(centre, lean_axis(t, M_PI + dir * M_PI * i / 12.), false, 0.);
                tilted_dir = dir;
                wound      = true;
                restart    = true;
            }
            dir        = -tilted_dir;
            tilted_dir = dir;
        }
        seam = wanted;
        if (restart)
            offset = 0.;
        const double base    = 2. * M_PI * seam / Azimuths;
        const double o_min   = ScarfLength / std::max(ring[seam].x(), 1.);
        const double band    = Geometry::deg2rad(SeamScatter);
        const double room    = dir > 0 ? band - offset - o_min : offset - o_min + band;
        const double overlap = o_min + uniform() * std::max(0., room);
        const double span    = 2. * M_PI + overlap;
        const double lo = std::min(base, base + dir * 2. * M_PI), hi = std::max(base, base + dir * 2. * M_PI);
        const int    steps   = std::max(8, int(std::ceil(span / (2. * M_PI / Azimuths))));
        for (int i = 0; i <= steps; ++i) {
            const double arc = span * i / steps;
            const double phi = base + offset + dir * arc;
            double       jf  = std::fmod(phi / (2. * M_PI) * Azimuths, double(Azimuths));
            if (jf < 0.)
                jf += Azimuths;
            const int    j0 = std::min(int(jf), Azimuths - 1), j1 = (j0 + 1) % Azimuths;
            const double t  = jf - j0;
            const Vec3d  q  = (1. - t) * ring[j0] + t * ring[j1];
            const Vec3d  tip(q.x() * std::cos(phi), q.x() * std::sin(phi), q.y());
            const double tilt = sign * std::min(std::abs(q.z()), std::max(cap_at(q.y() + z_base), min_tilt));
            const Vec3d  axis = lean ? lean_axis(tilt, std::clamp(phi, lo, hi)) : Vec3d::UnitZ();
            const double scarf = std::clamp(std::min(arc, span - arc) / overlap, ScarfFloor, 1.);
            if (i == 0 && restart)
                travel(tip, axis);
            else {
                add(tip, axis, true, h);
                plan.points.back().width = w * scarf;
            }
        }
        offset += dir * overlap;
        if (plan.points.size() > MaxPoints) {
            why = "the wall needs more rings than a vase plan holds";
            return {};
        }
        if (!lean)
            dir = -dir;
    }
    const double first_z = object.layers().front()->print_z;
    for (AtomPoint &p : plan.points)
        p.tip.z() = std::max(p.tip.z() + z_base, first_z);
    if (plan.points.size() < 2)
        why = "the vase has no wall above its bottom";
    return plan;
}

}
