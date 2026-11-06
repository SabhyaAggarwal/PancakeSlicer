#include "AutoOrient.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Point.hpp"
#include <cstddef>
#include <math.h>
#include <utility>
#include <vector>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>

namespace Slic3r::Nonplanar {

namespace {

constexpr double FlatSlope = 1. * M_PI / 180.;
constexpr double WallSlope = 70. * M_PI / 180.;



constexpr double TimeBase = 537., TimePerSurface = 0.099, TimePerHeight = 38.8, TimePerCurvedTop = 0.325,
                 TimePerSupport = 0.02;


constexpr double CurvedDefect = 0.25, SupportDefect = 2.;
constexpr double UnstablePenalty = 1e5;


constexpr double TipCost = 600.;

struct Mesh
{
    const indexed_triangle_set &its;
    std::vector<Vec3f>          normals;
    std::vector<float>          areas;
    double                      surface = 0.;
};

struct Hit
{
    uint32_t cell;
    float    z, nz;
    bool     operator<(const Hit &o) const { return std::tie(cell, z) < std::tie(o.cell, o.z); }
};


Matrix3d frame(const Vec3d &up)
{
    const Vec3d u  = up.normalized();
    const Vec3d a  = std::abs(u.x()) < 0.9 ? Vec3d::UnitX() : Vec3d::UnitY();
    const Vec3d e1 = u.cross(a).normalized();
    Matrix3d    r;
    r.row(0) = e1;
    r.row(1) = u.cross(e1);
    r.row(2) = u;
    return r;
}

OrientScore score(const Mesh &mesh, const Vec3d &up_in, const OrientParams &params)
{
    OrientScore s;
    s.up               = up_in.normalized();
    const Matrix3f R   = frame(s.up).cast<float>();
    const Vec3f    up  = s.up.cast<float>();
    const double   cap = std::atan(params.layer_height / params.outer_width);
    const double   support_cos = std::cos(params.support_angle * M_PI / 180.);


    for (size_t i = 0; i < mesh.normals.size(); ++i) {
        const double nz = mesh.normals[i].dot(up), stair = mesh.areas[i] * nz * nz;
        if (nz <= 0.) {

            if (-nz < support_cos)
                s.under_stair += stair;
            continue;
        }
        const double beta = std::acos(std::min(nz, 1.)), proj = mesh.areas[i] * nz;
        if (beta < FlatSlope)
            s.flat_top += proj;
        else if (beta <= cap) {
            s.curved_top += proj;
            s.curved_stair += stair;
        } else {
            if (beta < WallSlope)
                s.steep_top += proj;
            s.steep_stair += stair;
        }
    }



    std::vector<Vec3f> p(mesh.its.vertices.size());
    Vec3f              lo = Vec3f::Constant(std::numeric_limits<float>::max()), hi = -lo;
    for (size_t i = 0; i < p.size(); ++i) {
        p[i] = R * mesh.its.vertices[i];
        lo   = lo.cwiseMin(p[i]);
        hi   = hi.cwiseMax(p[i]);
    }
    s.height = hi.z() - lo.z();

    const float pitch = std::max(0.5f, std::sqrt((hi.x() - lo.x()) * (hi.y() - lo.y()) / 20000.f));
    const int   nx = int(std::ceil((hi.x() - lo.x()) / pitch)) + 1, ny = int(std::ceil((hi.y() - lo.y()) / pitch)) + 1;
    std::vector<Hit> hits;
    hits.reserve(mesh.its.indices.size());
    for (size_t f = 0; f < mesh.its.indices.size(); ++f) {
        const float nz = mesh.normals[f].dot(up);
        if (std::abs(nz) < 1e-6f)
            continue;
        const auto &idx = mesh.its.indices[f];
        const Vec3f a = p[idx[0]] - lo, b = p[idx[1]] - lo, c = p[idx[2]] - lo;
        const int   x0 = std::max(0, int(std::ceil(std::min({ a.x(), b.x(), c.x() }) / pitch - 0.5f)));
        const int   x1 = std::min(nx - 1, int(std::floor(std::max({ a.x(), b.x(), c.x() }) / pitch - 0.5f)));
        const int   y0 = std::max(0, int(std::ceil(std::min({ a.y(), b.y(), c.y() }) / pitch - 0.5f)));
        const int   y1 = std::min(ny - 1, int(std::floor(std::max({ a.y(), b.y(), c.y() }) / pitch - 0.5f)));
        const float d  = (b.y() - c.y()) * (a.x() - c.x()) + (c.x() - b.x()) * (a.y() - c.y());
        if (std::abs(d) < 1e-12f)
            continue;
        for (int ix = x0; ix <= x1; ++ix)
            for (int iy = y0; iy <= y1; ++iy) {
                const float px = (ix + 0.5f) * pitch, py = (iy + 0.5f) * pitch;
                const float l1 = ((b.y() - c.y()) * (px - c.x()) + (c.x() - b.x()) * (py - c.y())) / d;
                const float l2 = ((c.y() - a.y()) * (px - c.x()) + (a.x() - c.x()) * (py - c.y())) / d;
                const float l3 = 1.f - l1 - l2;
                if (l1 >= 0.f && l2 >= 0.f && l3 >= 0.f)
                    hits.push_back({ uint32_t(ix * ny + iy), l1 * a.z() + l2 * b.z() + l3 * c.z(), nz });
            }
    }
    std::sort(hits.begin(), hits.end());
    const double cell_area = double(pitch) * pitch, first = params.first_layer_height;
    for (size_t i = 0; i < hits.size(); ++i) {
        const Hit &h      = hits[i];
        const bool lowest = i == 0 || hits[i - 1].cell != h.cell;
        if (lowest) {
            s.footprint += cell_area;
            if (h.z < first && h.nz < -0.9f)
                s.bed += cell_area;
        }



        if (const double gap = h.z - (lowest ? 0.f : hits[i - 1].z); h.nz < -support_cos && h.z > first && gap > 2. * params.layer_height) {
            s.support_area += cell_area;
            s.support_volume += gap * cell_area;
        }
    }
    s.time_s = TimeBase + TimePerSurface * mesh.surface + TimePerHeight * s.height + TimePerCurvedTop * (s.curved_top + s.steep_top) +
               TimePerSupport * s.support_volume;
    rescore(s);
    return s;
}

Mesh make_mesh(const indexed_triangle_set &its)
{
    Mesh mesh{ its, its_face_normals(its), {} };
    mesh.areas.resize(its.indices.size());
    for (size_t i = 0; i < its.indices.size(); ++i) {
        const auto &f = its.indices[i];
        mesh.areas[i] = 0.5f * (its.vertices[f[1]] - its.vertices[f[0]]).cross(its.vertices[f[2]] - its.vertices[f[0]]).norm();
        mesh.surface += mesh.areas[i];
    }
    return mesh;
}


std::vector<Vec3d> flat_face_directions(const Mesh &mesh, size_t count)
{
    std::map<std::tuple<int, int, int>, std::pair<double, Vec3d>> buckets;
    for (size_t i = 0; i < mesh.normals.size(); ++i) {
        const Vec3f &n = mesh.normals[i];
        if (!n.allFinite())
            continue;
        auto &b = buckets.try_emplace({ int(std::lround(n.x() * 50)), int(std::lround(n.y() * 50)), int(std::lround(n.z() * 50)) },
                                      0., Vec3d::Zero()).first->second;
        b.first += mesh.areas[i];
        b.second += mesh.areas[i] * n.cast<double>();
    }
    std::vector<std::pair<double, Vec3d>> sorted;
    for (const auto &[key, b] : buckets)
        if (b.second.norm() > 0.)
            sorted.emplace_back(b.first, -b.second.normalized());
    std::sort(sorted.begin(), sorted.end(), [](const auto &l, const auto &r) { return l.first > r.first; });
    std::vector<Vec3d> out;
    for (size_t i = 0; i < std::min(count, sorted.size()); ++i)
        out.push_back(sorted[i].second);
    return out;
}


OrientScore refine(const Mesh &mesh, OrientScore best, const OrientParams &params)
{
    for (double step : { 8., 4., 2., 1. }) {
        const double t = std::tan(step * M_PI / 180.);
        for (bool improved = true; improved;) {
            improved         = false;
            const Matrix3d f = frame(best.up);
            for (const Vec2d d : { Vec2d(1, 0), Vec2d(-1, 0), Vec2d(0, 1), Vec2d(0, -1) }) {
                OrientScore s = score(mesh, best.up + t * (d.x() * f.row(0).transpose() + d.y() * f.row(1).transpose()), params);
                if (s.cost < best.cost - 1e-6) {
                    best     = s;
                    improved = true;
                }
            }
        }
    }
    return best;
}

}

OrientParams orient_params(const DynamicPrintConfig &config)
{
    OrientParams p;
    const double nozzle = config.option<ConfigOptionFloats>("nozzle_diameter") ? config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0) : 0.4;
    const double shell  = config.has("nonplanar_shell_layer_height") ? config.opt_float("nonplanar_shell_layer_height") : 0.;
    if (shell > 0.)
        p.layer_height = shell;
    else if (config.has("layer_height"))
        p.layer_height = config.opt_float("layer_height");

    p.outer_width = 0.75 * nozzle;
    if (const auto *w = config.option<ConfigOptionFloatOrPercent>("top_surface_line_width"); w && w->value > 0.)
        p.outer_width = std::min(p.outer_width, w->get_abs_value(nozzle));
    if (config.has("initial_layer_print_height"))
        p.first_layer_height = config.opt_float("initial_layer_print_height");
    if (const auto *a = config.option<ConfigOptionInt>("support_threshold_angle"); a && a->value > 0)
        p.support_angle = a->value;
    return p;
}

namespace {

double support_of(const OrientScore &s) { return s.measured && s.printed_support <= 0. ? 0. : s.support_area; }
}

void rescore(OrientScore &s)
{
    s.defect = s.steep_stair + s.under_stair + s.curved_stair * (CurvedDefect * s.curved_share + 1. - s.curved_share) +
               SupportDefect * support_of(s);

    s.stable = s.bed >= std::max(20., 0.03 * s.footprint);
    s.cost   = s.time_s + OrientDefectCost * s.defect + TipCost * std::max(0., s.height / std::sqrt(std::max(s.bed, 1.)) - 1.) +
             (s.stable ? 0. : UnstablePenalty);
}

OrientScore score_orientation(const indexed_triangle_set &its, const Vec3d &up, const OrientParams &params)
{
    return score(make_mesh(its), up, params);
}

bool replaces(const OrientScore &candidate, const OrientScore &original)
{
    if (!candidate.stable)
        return false;

    if (!original.stable)
        return true;
    if (candidate.height > 1.25 * original.height)
        return false;


    const double top = original.flat_top + original.curved_top + original.steep_top;
    if (support_of(original) == 0. && original.steep_top <= 0.01 * top)
        return candidate.measured && original.measured && candidate.time_s <= 0.9 * original.time_s && support_of(candidate) == 0.;
    const double gain = original.cost - candidate.cost;
    if (gain < std::max(0.10 * original.cost, 60.))
        return false;

    const double before = support_of(original), after = support_of(candidate), removed = before - after;
    if (before > 5. && removed >= 0.25 * before && removed >= std::min(50., before))
        return true;


    const bool measured = candidate.measured && original.measured;
    return (measured || gain >= 0.25 * original.cost) && after <= before + 5. + 0.02 * top;
}

OrientResult auto_orient(const indexed_triangle_set &its, const OrientParams &params)
{
    const Mesh         mesh = make_mesh(its);
    std::vector<Vec3d> dirs{ Vec3d::UnitZ(), -Vec3d::UnitZ(), Vec3d::UnitX(), -Vec3d::UnitX(), Vec3d::UnitY(), -Vec3d::UnitY() };
    for (const Vec3d &d : flat_face_directions(mesh, 12))
        dirs.push_back(d);

    for (int i = 0; i < params.samples; ++i) {
        const double z = 1. - 2. * (i + 0.5) / params.samples, r = std::sqrt(1. - z * z), th = M_PI * (1. + std::sqrt(5.)) * (i + 0.5);
        dirs.emplace_back(std::cos(th) * r, std::sin(th) * r, z);
    }
    std::vector<OrientScore> scores(dirs.size());
    tbb::parallel_for(size_t(0), dirs.size(), [&](size_t i) { scores[i] = score(mesh, dirs[i], params); });

    OrientResult result;
    result.original = scores.front();
    std::vector<OrientScore> best;
    std::vector<size_t>      order(scores.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t l, size_t r) { return scores[l].cost < scores[r].cost; });
    const auto distinct = [](const std::vector<OrientScore> &v, const Vec3d &up, double deg) {
        return std::none_of(v.begin(), v.end(), [&](const OrientScore &s) { return s.up.dot(up) > std::cos(deg * M_PI / 180.); });
    };
    for (size_t i : order)
        if (int(best.size()) < params.refine && distinct(best, scores[i].up, 5.))
            best.push_back(scores[i]);
    tbb::parallel_for(size_t(0), best.size(), [&](size_t i) { best[i] = refine(mesh, best[i], params); });
    std::sort(best.begin(), best.end(), [](const OrientScore &l, const OrientScore &r) { return l.cost < r.cost; });
    for (const OrientScore &s : best)
        if (distinct(result.ranked, s.up, 2.))
            result.ranked.push_back(s);

    result.rotated = ! result.ranked.empty() && replaces(result.ranked.front(), result.original);
    result.chosen  = result.rotated ? result.ranked.front() : result.original;
    return result;
}

Matrix3d rotation_to_z(const Vec3d &up)
{


    if (up.z() < 0.) {
        const Matrix3d flip = Eigen::AngleAxisd(M_PI, Vec3d::UnitX()).toRotationMatrix();
        return rotation_to_z(flip * up) * flip;
    }
    Vec3d    axis;
    double   angle;
    Matrix3d r;
    Geometry::rotation_from_two_vectors(up.normalized(), Vec3d::UnitZ(), axis, angle, &r);
    return r;
}

}
