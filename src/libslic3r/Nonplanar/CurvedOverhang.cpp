#include "CurvedOverhang.hpp"
#include "libslic3r/Nonplanar/HeightField.hpp"

#include "AutoMode.hpp"

#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "CurvedBridge.hpp"
#include "HeadEnvelope.hpp"
#include "HeightField.hpp"
#include "LayerSchedule.hpp"
#include "PatchTools.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cassert>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float Inf = std::numeric_limits<float>::infinity();
constexpr double ThinWallLayers = 15.;
constexpr double TargetContact  = 0.5;
constexpr double MinContact     = 0.4;
constexpr double TunedLayerRatio = 0.5;

double contact(double tuned, double g, double d)
{
    const double tall = g / (TunedLayerRatio * d);
    return tall < 1. - EPSILON ? 1. - (1. - tuned) * tall : tuned;
}
constexpr double DefaultMaxBend = 40.;
constexpr double SupportFreeMinArea = 1.;
constexpr double FlatUnderside = 0.05;
constexpr int MaxPasses = 4;

struct OverhangSettings
{
    int    region_id = -1;
    double pitch     = 0.2;
    double max_bend  = 0.;
    double min_area  = 0.;
    double skin      = 0.;
};

OverhangSettings overhang_settings(const PrintObject &object)
{
    OverhangSettings s;
    const double     g     = object.config().layer_height.value;
    const double     scale = bead_scale(object.print()->config());
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        if (!overhangs_enabled(cfg, object.config()))
            continue;
        s.region_id = int(region_id);
        s.pitch     = std::max(0.05, object.config().nonplanar_surface_resolution.value);
        s.max_bend  = Geometry::deg2rad(cfg.nonplanar_max_slope.value > 0. && object.config().nonplanar_mode.value != NonplanarMode::Auto ? cfg.nonplanar_max_slope.value : DefaultMaxBend);
        s.min_area  = effective_fallback(cfg, object.config()) == NonplanarFallback::None ? 0. :
                      support_free(object.config()) ? std::min(cfg.nonplanar_min_patch_area.value, SupportFreeMinArea * scale * scale) :
                                                      cfg.nonplanar_min_patch_area.value;
        s.skin      = std::max({ 2. * g, cfg.bottom_shell_layers.value * g, cfg.bottom_shell_thickness.value });
        break;
    }
    return s;
}

HeightField sample_part_top(const sla::IndexedMesh &mesh, const HeightField &bottom, double z_offset,
                            const std::function<void()> &throw_if_canceled)
{
    HeightField out(bottom.bbox(), bottom.pitch());
    tbb::parallel_for(tbb::blocked_range<int>(0, bottom.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy) {
            throw_if_canceled();
            for (int ix = 0; ix < bottom.nx(); ++ix) {
                const float b = bottom.at(ix, iy);
                if (std::isnan(b))
                    continue;
                const Vec2d p = bottom.node(ix, iy);
                for (const Vec2d &q : { p, Vec2d(p + Vec2d(1e-4, 1e-4)) }) {
                    const auto hit = mesh.query_ray_hit(Vec3d(q.x(), q.y(), b - z_offset + 0.02 + mesh.ground_level()), Vec3d(0., 0., 1.));
                    if (hit.is_hit() && hit.normal().normalized().z() > 0.05) {
                        out.at(ix, iy) = float(hit.position().z() - mesh.ground_level() + z_offset);
                        break;
                    }
                }
            }
        }
    });
    return out;
}

Vec2d node_gradient(const HeightField &f, int ix, int iy)
{
    auto get  = [&f](int x, int y) { return x >= 0 && y >= 0 && x < f.nx() && y < f.ny() ? f.at(x, y) : HeightField::NoData; };
    auto axis = [&](int dx, int dy) {
        const float c = f.at(ix, iy), lo = get(ix - dx, iy - dy), hi = get(ix + dx, iy + dy);
        if (!std::isnan(lo) && !std::isnan(hi))
            return double(hi - lo) / (2. * f.pitch());
        if (!std::isnan(hi))
            return double(hi - c) / f.pitch();
        if (!std::isnan(lo))
            return double(c - lo) / f.pitch();
        return std::numeric_limits<double>::quiet_NaN();
    };
    return Vec2d(axis(1, 0), axis(0, 1));
}

double between_supports(const std::vector<int> &nodes, const std::vector<std::pair<int, int>> &roots, const HeightField &grid, double span)
{
    const int                    nx = grid.nx();
    std::unordered_map<int, int> local;
    local.reserve(nodes.size());
    for (size_t j = 0; j < nodes.size(); ++j)
        local.emplace(nodes[j], int(j));
    const float          inf = std::numeric_limits<float>::infinity();
    std::vector<float>   d1(nodes.size(), inf), d2(nodes.size(), inf);
    std::vector<int>     s1(nodes.size(), -1), s2(nodes.size(), -1);
    using QItem = std::tuple<float, int, int>;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> queue;
    auto relax = [&](int j, float d, int s) {
        if (s == s1[j]) {
            if (d >= d1[j])
                return;
            d1[j] = d;
        } else if (s == s2[j]) {
            if (d >= d2[j])
                return;
            d2[j] = d;
            if (d2[j] < d1[j])
                std::swap(d1[j], d2[j]), std::swap(s1[j], s2[j]);
        } else if (d < d1[j]) {
            d2[j] = d1[j], s2[j] = s1[j], d1[j] = d, s1[j] = s;
        } else if (d < d2[j]) {
            d2[j] = d, s2[j] = s;
        } else
            return;
        queue.emplace(d, j, s);
    };
    for (const auto &[i, s] : roots)
        relax(local.at(i), 0.f, s);
    while (!queue.empty()) {
        const auto [d, j, s] = queue.top();
        queue.pop();
        if (!(s == s1[j] && d == d1[j]) && !(s == s2[j] && d == d2[j]))
            continue;
        const int i = nodes[size_t(j)], ix = i % nx;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || ix + dx < 0 || ix + dx >= nx)
                    continue;
                if (const auto it = local.find(i + dy * nx + dx); it != local.end())
                    relax(it->second, d + float(grid.pitch() * std::hypot(dx, dy)), s);
            }
    }
    size_t between = 0;
    for (size_t j = 0; j < nodes.size(); ++j)
        between += d1[j] + d2[j] <= span;
    return double(between) / double(nodes.size());
}

void smooth_inside(HeightField &f, const Mask &in, int passes)
{
    static constexpr float k[5] = { 1.f, 4.f, 6.f, 4.f, 1.f };
    const int              nx = f.nx(), ny = f.ny();
    std::vector<float>     tmp(f.data().size());
    for (int pass = 0; pass < passes; ++pass)
        for (int axis = 0; axis < 2; ++axis) {
            for (int iy = 0; iy < ny; ++iy)
                for (int ix = 0; ix < nx; ++ix) {
                    const size_t i = size_t(iy) * nx + ix;
                    tmp[i]         = f.data()[i];
                    if (!in[i])
                        continue;
                    float s0 = 0.f, s1 = 0.f, s2 = 0.f, sv = 0.f, stv = 0.f;
                    for (int t = -2; t <= 2; ++t) {
                        const int x = axis == 0 ? ix + t : ix, y = axis == 0 ? iy : iy + t;
                        if (x < 0 || y < 0 || x >= nx || y >= ny || !in[size_t(y) * nx + x])
                            continue;
                        const float wk = k[t + 2], v = f.data()[size_t(y) * nx + x];
                        s0 += wk, s1 += wk * t, s2 += wk * t * t, sv += wk * v, stv += wk * t * v;
                    }
                    const float det = s0 * s2 - s1 * s1;
                    tmp[i]          = s1 != 0.f && det > 1e-3f * s0 * s0 ? (s2 * sv - s1 * stv) / det : sv / s0;
                }
            std::copy(tmp.begin(), tmp.end(), f.data().begin());
        }
}

ExPolygons smooth_contours(const ExPolygons &polygons, double step, double window)
{
    auto smooth = [&](Polygon &poly) {
        const double length = unscale<double>(poly.length());
        const size_t n      = size_t(length / step);
        if (n < 8)
            return;
        Points samples;
        samples.reserve(n);
        const Points &pts = poly.points;
        double        at  = 0.;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Vec2d a = unscale(pts[i]), b = unscale(pts[(i + 1) % pts.size()]);
            const double l = (b - a).norm();
            for (; at < l; at += length / double(n)) {
                const Vec2d p = a + (b - a) * (at / l);
                samples.emplace_back(Point::new_scale(p.x(), p.y()));
            }
            at -= l;
        }
        const int half = std::max(1, int(std::lround(0.5 * window / (length / double(n)))));
        if (int(samples.size()) <= 2 * half + 2)
            return;
        Points out(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            Vec2d sum = Vec2d::Zero();
            for (int t = -half; t <= half; ++t)
                sum += samples[size_t((ptrdiff_t(i) + t + ptrdiff_t(samples.size())) % ptrdiff_t(samples.size()))].cast<double>();
            out[i] = (sum / double(2 * half + 1)).cast<coord_t>();
        }
        poly.points = std::move(out);
        poly.douglas_peucker(scale_(0.005));
    };
    ExPolygons out = polygons;
    for (ExPolygon &e : out) {
        smooth(e.contour);
        for (Polygon &h : e.holes)
            smooth(h);
    }
    return union_ex(out);
}

struct Piece
{
    int                                     sub   = 0;
    int                                     k     = 0;
    double                                  min_z = 0.;
    std::unique_ptr<ExtrusionPathConformal> path;
    bool                                    capped = false;
    bool                                    side   = false;
};

ExtrusionEntitiesPtr order_by_support(const std::vector<Piece *> &pieces, Point &last, double w, double g)
{
    const size_t  n      = pieces.size();
    const double  under = scale_(0.5 * w + g), beside = scale_(w), level = scale_(0.5 * g);
    const coord_t cell   = std::max<coord_t>(1, coord_t(std::max(under, beside)));
    struct Sample { Point p; coord_t z; int piece; };
    std::vector<Sample>                              samples;
    std::vector<size_t>                              first(n + 1, 0);
    std::unordered_map<int64_t, std::vector<size_t>> cells;
    auto key = [cell](const Point &p) { return (int64_t(p.x() / cell) << 32) ^ int64_t(uint32_t(p.y() / cell)); };
    const double step = scale_(0.5 * w);
    for (size_t i = 0; i < n; ++i) {
        first[i]           = samples.size();
        const Points3 &pts = pieces[i]->path->polyline.points;
        for (size_t s = 0; s < pts.size(); ++s) {
            const Point3 &b = s + 1 < pts.size() ? pts[s + 1] : pts[s];
            const int     m = s + 1 < pts.size() ? std::max(1, int(std::ceil((b - pts[s]).head<2>().cast<double>().norm() / step))) : 1;
            for (int t = 0; t < m; ++t) {
                const Vec3d q = pts[s].cast<double>() + (b - pts[s]).cast<double>() * (double(t) / m);
                samples.push_back({ Point(coord_t(q.x()), coord_t(q.y())), coord_t(q.z()), int(i) });
            }
        }
    }
    first[n] = samples.size();
    for (size_t s = 0; s < samples.size(); ++s)
        cells[key(samples[s].p)].emplace_back(s);
    std::vector<std::vector<int>> dependents(n);
    std::vector<int>              waiting(n, 0);
    for (size_t i = 0; i < n; ++i) {
        std::vector<int> support;
        for (size_t own = first[i]; own < first[i + 1]; ++own) {
            const Sample &a = samples[own];
            for (coord_t dx = -1; dx <= 1; ++dx)
                for (coord_t dy = -1; dy <= 1; ++dy)
                    if (auto it = cells.find(key(a.p + Point(dx * cell, dy * cell))); it != cells.end())
                        for (size_t s : it->second) {
                            const Sample &b = samples[s];
                            const Piece  &q = *pieces[size_t(b.piece)];
                            if (size_t(b.piece) == i)
                                continue;
                            const double d  = (b.p - a.p).cast<double>().norm();
                            const double dz = double(a.z - b.z);
                            if ((dz >= level && d <= under) ||
                                (std::abs(dz) < level && d < beside && (q.side == pieces[i]->side ? q.k < pieces[i]->k : pieces[i]->side)))
                                support.emplace_back(b.piece);
                        }
        }
        sort_remove_duplicates(support);
        waiting[i] = int(support.size());
        for (int q : support)
            dependents[size_t(q)].emplace_back(int(i));
    }
    ExtrusionEntitiesPtr out;
    std::vector<bool>    done(n, false);
    for (size_t taken = 0; taken < n; ++taken) {
        size_t best = n;
        bool   flip = false;
        double best_d = Inf;
        for (size_t i = 0; i < n; ++i)
            if (!done[i] && waiting[i] == 0) {
                const ExtrusionPathConformal &path = *pieces[i]->path;
                const double a = (path.first_point() - last).cast<double>().squaredNorm();
                const double b = (path.last_point() - last).cast<double>().squaredNorm();
                if (std::min(a, b) < best_d)
                    best = i, best_d = std::min(a, b), flip = b < a;
            }
        if (best == n)
            for (size_t i = 0; i < n; ++i)
                if (!done[i] && (best == n || pieces[i]->min_z < pieces[best]->min_z))
                    best = i, flip = false;
        done[best] = true;
        for (int d : dependents[best])
            --waiting[size_t(d)];
        ExtrusionPathConformal *path = pieces[best]->path.release();
        if (flip)
            path->reverse();
        last = path->last_point();
        out.emplace_back(path);
    }
    return out;
}

struct OverhangCandidates
{
    HeightField grid;
    ExPolygons  area;
    Mask        mask;
    bool        ready = false;
};

bool build_overhang_pass(PrintObject &object, ConformalResult &result, int pass, HeightField &prev_top, OverhangCandidates &candidates)
{
    LayerPtrs             &layers   = object.layers();
    const OverhangSettings settings = overhang_settings(object);
    if (settings.region_id < 0 || layers.size() < 2)
        return false;
    if (result.planar_exclusion.size() != layers.size())
        result.planar_exclusion.assign(layers.size(), ExPolygons());

    const double       g      = object.config().layer_height.value;
    const double       pitch  = settings.pitch;
    const PrintRegion &region = object.printing_region(size_t(settings.region_id));
    const Flow         flow   = region.flow(object, frExternalPerimeter, g);
    const double       w = flow.width(), s = flow.spacing();
    const double       d = smallest_nozzle(object.print()->config());
    const double       min_contact = contact(MinContact, g, d);
    const double       wall_width = std::max(0, region.config().wall_loops.value) * s;
    auto               cancel = [&object]() { throw_if_canceled(object); };
    const bool   free_mode    = support_free(object.config());
    const int    threshold    = object.config().support_threshold_angle.value;
    const double printable    = Geometry::deg2rad(std::clamp(90. - (threshold > 0 ? threshold : 30), 45., 55.));
    const double contact_req  = std::max(min_contact, 1. - g * std::tan(printable) / w);
    const double grad_req     = g / ((1. - (free_mode ? contact_req : min_contact)) * w);
    std::vector<std::vector<int>> self_supported(free_mode ? layers.size() : 0);
    Polygons                      capped_cover;

    std::vector<double> layer_bottom(layers.size()), layer_top(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        layer_top[i]    = layers[i]->print_z;
        layer_bottom[i] = layers[i]->print_z - layers[i]->height;
    }
    auto layer_at = [&layer_top](double z) {
        return size_t(std::min<ptrdiff_t>(std::lower_bound(layer_top.begin(), layer_top.end(), z - EPSILON) - layer_top.begin(),
                                          ptrdiff_t(layer_top.size()) - 1));
    };

    if (!candidates.ready)
        candidates.grid = object_grid(object, pitch);
    const HeightField &grid     = candidates.grid;
    const size_t      n_nodes   = grid.data().size();
    HeightField       ray_start = grid;
    Polygons          bottoms;
    for (size_t i = 1; i < layers.size(); ++i) {
        ExPolygons layer_bottoms;
        for (const LayerRegion *layerm : layers[i]->regions())
            if (overhangs_enabled(layerm->region().config(), object.config()))
                for (const Surface &surface : layerm->slices.surfaces)
                    if (surface.surface_type == stBottom || surface.surface_type == stBottomBridge)
                        layer_bottoms.emplace_back(surface.expolygon);
        if (i < result.bridged.size() && !result.bridged[i].empty())
            layer_bottoms = diff_ex(layer_bottoms, offset(result.bridged[i], float(scale_(0.5 * w))));
        if (layer_bottoms.empty())
            continue;
        const Mask  m     = rasterize(layer_bottoms, grid);
        const float start = float(layers[i - 1]->slice_z - layers[i - 1]->height);
        for (size_t n = 0; n < n_nodes; ++n)
            if (m[n] && !(ray_start.data()[n] <= start) && (prev_top.empty() || start > prev_top.data()[n] - 2. * g))
                ray_start.data()[n] = start;
        if (!candidates.ready)
            polygons_append(bottoms, to_polygons(layer_bottoms));
    }
    if (!candidates.ready) {
        if (bottoms.empty())
            return false;
        const float seam = float(scale_(0.01));
        candidates.area  = offset_ex(union_ex(offset(bottoms, seam)), -seam);
        if (!result.clearance_ready) {
            result.clearance       = clearance_zone(object);
            result.clearance_ready = true;
        }
        if (!result.clearance.empty())
            candidates.area = diff_ex(candidates.area, result.clearance);
        candidates.mask  = rasterize(candidates.area, grid);
        candidates.ready = true;
    }
    ExPolygons  candidate = candidates.area;
    const Mask &cand_mask = candidates.mask;

    HeightField Sb, top;
    {
        const sla::IndexedMesh &imesh = object_mesh(object, result).imesh;
        const double z_offset = object.slicing_parameters().object_print_z_min;
        for (size_t n = 0; n < n_nodes; ++n)
            if (!cand_mask[n])
                ray_start.data()[n] = HeightField::NoData;
        Sb = sample_bottom_surface(imesh, ray_start, z_offset, cancel);
        if (!prev_top.empty())
            for (size_t n = 0; n < n_nodes; ++n)
                if (!(Sb.data()[n] >= prev_top.data()[n] - 0.5 * g))
                    Sb.data()[n] = HeightField::NoData;
        top = sample_part_top(imesh, Sb, z_offset, cancel);
    }
    const bool more = std::any_of(Sb.data().begin(), Sb.data().end(), [](float b) { return !std::isnan(b); });
    prev_top        = top;
    if (pass > 0) {
        Mask sampled(n_nodes, 0);
        for (size_t n = 0; n < n_nodes; ++n)
            sampled[n] = !std::isnan(Sb.data()[n]);
        candidate = intersection_ex(candidate, mask_to_expolygons(sampled, grid));
    }

    const double grad_need = g / ((1. - contact(TargetContact, g, d)) * w);
    const double grad_bend = free_mode ? std::min(grad_need, 1. / std::tan(printable)) : grad_need;
    const bool field = field_layers(object);
    auto field_bend = [&](double z) {
        const double g_field = layers[layer_at(z)]->height;
        return std::min(grad_bend, g_field / ((1. - min_contact) * w));
    };
    const double grad_min  = std::max(0., g / ((1. - min_contact) * w) - std::tan(settings.max_bend));
    std::vector<float> slope(n_nodes, HeightField::NoData);
    Mask               mask(n_nodes, 0);
    enum Why : uint8_t { Taken, Unsampled, Resting, Rim, SelfSupporting, Unfit, Small, NoRoot, NoBeads, Bridged, Thread };
    static constexpr const char *why_tag[] = { "overhang_outline", "overhang_unsampled", "overhang_resting", "overhang_rim",
                                               "overhang_self_supporting", "overhang_unfit", "overhang_small",
                                               "overhang_no_root", "overhang_no_beads", "overhang_bridged", "overhang_thread" };
    std::vector<uint8_t> why(n_nodes, Taken);
    Mask                 flat_mask(n_nodes, 0);
    for (int iy = 0; iy < grid.ny(); ++iy)
        for (int ix = 0; ix < grid.nx(); ++ix) {
            const size_t i = size_t(iy) * grid.nx() + ix;
            if (!cand_mask[i])
                continue;
            if (std::isnan(Sb.data()[i])) {
                why[i] = Unsampled;
                continue;
            }
            if (!(top.data()[i] - Sb.data()[i] >= 0.5 * g) || !(Sb.data()[i] >= layer_top.front() + 0.5 * g)) {
                why[i] = Resting;
                flat_mask[i] = free_mode && top.data()[i] - Sb.data()[i] < 0.5 * g && Sb.data()[i] >= layer_top.front() + 0.5 * g;
                continue;
            }
            const Vec2d gr = node_gradient(Sb, ix, iy);
            if (std::isnan(gr.x()) || std::isnan(gr.y())) {
                why[i] = Rim;
                continue;
            }
            if (free_mode && gr.norm() >= 1. / std::tan(printable))
                flat_mask[i] = 2;
            if (top.data()[i] - Sb.data()[i] < ThinWallLayers * g && (!free_mode || gr.norm() >= 1. / std::tan(printable)))
                continue;
            slope[i] = float(gr.norm());
            const double bend = field ? field_bend(Sb.data()[i]) : grad_bend;
            mask[i]  = slope[i] < bend && slope[i] >= grad_min;
            if (!mask[i])
                why[i] = slope[i] >= bend ? SelfSupporting : Unfit;
        }
    {
        const ExPolygons opened = opening_ex(mask_to_expolygons(mask, grid), float(scale_(0.5 * w)));
        const Mask       keep   = rasterize(opened, grid);
        for (size_t i = 0; i < n_nodes; ++i)
            if (mask[i] && !keep[i])
                mask[i] = 0, why[i] = Unfit;
    }
    std::map<std::string, double> left;
    const double node_area = pitch * pitch;
    ExPolygons accepted;
    auto report_fallback = [&]() {
        const ExPolygons uncovered = diff_ex(candidate, accepted);
        const double     rest      = area(uncovered) * SCALING_FACTOR * SCALING_FACTOR;
        std::array<double, std::size(why_tag)> by_why{};
        const Mask umask = rasterize(uncovered, grid);
        for (size_t i = 0; i < n_nodes; ++i)
            if (umask[i])
                by_why[why[i]] += node_area;
        const double nodes = std::accumulate(by_why.begin() + 1, by_why.end(), 0.);
        const double scale = nodes > rest ? rest / nodes : 1.;
        by_why[Taken]      = std::max(0., rest - nodes * scale);
        for (size_t r = 0; r < by_why.size(); ++r)
            if (const double a = by_why[r] * (r == Taken ? 1. : scale); a > 1e-3)
                left[why_tag[r]] += a;
        for (const auto &[reason, fallback] : left)
            if (fallback > 0.) {
                PatchDiagnostics diag;
                diag.fallback_area   = fallback;
                diag.fallback_reason = reason;
                result.diagnostics.emplace_back(std::move(diag));
            }
    };
    auto publish_self_supported = [&]() {
        if (!free_mode)
            return;
        std::vector<std::vector<int>> flat(layers.size());
        for (int iy = 0; iy < (object.has_support_material() ? grid.ny() : 0); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix) {
                const size_t i    = size_t(iy) * grid.nx() + ix;
                bool         face = flat_mask[i] != 0;
                for (int dy = -1; dy <= 1 && face && flat_mask[i] == 2; ++dy)
                    for (int dx = -1; dx <= 1 && face; ++dx) {
                        const int x = ix + dx, y = iy + dy;
                        face = x >= 0 && y >= 0 && x < grid.nx() && y < grid.ny() && flat_mask[size_t(y) * grid.nx() + x];
                    }
                if (face)
                    for (size_t l = layer_at(Sb.data()[i] - g); l < layers.size() && layer_bottom[l] <= Sb.data()[i] + g; ++l)
                        flat[l].emplace_back(int(i));
            }
        auto publish = [&](const std::vector<std::vector<int>> &nodes, std::vector<ExPolygons> &out, bool reach) {
            if (std::all_of(nodes.begin(), nodes.end(), [](const std::vector<int> &n) { return n.empty(); }))
                return;
            if (out.size() != layers.size())
                out.assign(layers.size(), ExPolygons());
            tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
                for (size_t l = range.begin(); l < range.end(); ++l)
                    if (!nodes[l].empty()) {
                        Mask m(n_nodes, 0);
                        for (int i : nodes[l])
                            m[size_t(i)] = 1;
                        if (!reach)
                            append(out[l], offset_ex(mask_to_expolygons(m, grid), float(scale_(pitch))));
                        else if (l > 0) {
                            const ExPolygons carried = offset_ex(layers[l - 1]->lslices, float(scale_(layers[l]->height * std::tan(printable))));
                            const ExPolygons beyond  = diff_ex(layers[l]->lslices, carried);
                            append(out[l], diff_ex(intersection_ex(mask_to_expolygons(m, grid), carried), offset_ex(beyond, float(scale_(w)))));
                        }
                        out[l] = union_ex(out[l]);
                    }
            });
        };
        publish(self_supported, result.self_supported, false);
        publish(flat, result.flat_printed, true);
    };
    int                    n_comp = 0;
    const std::vector<int> label  = label_components(mask, grid.nx(), grid.ny(), n_comp);
    if (n_comp == 0) {
        report_fallback();
        publish_self_supported();
        return more;
    }

    const HeadEnvelope head = resolve_head_envelope(object.print()->config());
    double tau = std::min(0.5 * g, head_band_height(head));
    if (tau < 0.02) {
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: the head reaches within " << tau
                                   << " mm of the tip; curved overhangs cannot keep below it";
        tau = 0.02;
    }
    std::vector<double> sb_hi;
    std::vector<int>    sb_layer, sb_index;
    for (size_t j = 0; j < layers.size(); ++j) {
        const int n = std::max(1, int(std::ceil(layers[j]->height / tau - 1e-6)));
        for (int k = 0; k < n; ++k) {
            sb_hi.emplace_back(layer_bottom[j] + layers[j]->height * (k + 1) / n);
            sb_layer.emplace_back(int(j));
            sb_index.emplace_back(k);
        }
    }
    auto sub_band = [&sb_hi](double z) {
        return int(std::min<ptrdiff_t>(std::lower_bound(sb_hi.begin(), sb_hi.end(), z - 1e-4) - sb_hi.begin(),
                                       ptrdiff_t(sb_hi.size()) - 1));
    };

    HeightField floor_z = grid;
    for (size_t i = 0; i < n_nodes; ++i) {
        if (label[i] < 0)
            continue;
        const auto it = std::lower_bound(layer_bottom.begin(), layer_bottom.end(), Sb.data()[i] + settings.skin - EPSILON);
        floor_z.data()[i] = std::min(top.data()[i], it == layer_bottom.end() ? Inf : float(*it));
        if (free_mode) {
            const double top_bottom = layer_bottom[layer_at(top.data()[i])];
            if (floor_z.data()[i] > top_bottom && top_bottom - Sb.data()[i] >= 0.5 * g)
                floor_z.data()[i] = float(top_bottom);
        }
    }

    const int        pad_nodes = int(std::ceil((3. + 2. * w) / pitch)) + 2;
    std::vector<Polylines> root_roads(layers.size());
    std::vector<int> all_nodes;
    const size_t     first_patch = result.patches.size();
    std::vector<std::vector<int>>   comp_nodes(static_cast<size_t>(n_comp));
    std::vector<std::array<int, 4>> comp_box(size_t(n_comp), std::array<int, 4>{ grid.nx(), grid.ny(), -1, -1 });
    for (int iy = 0; iy < grid.ny(); ++iy)
        for (int ix = 0; ix < grid.nx(); ++ix)
            if (const int l = label[size_t(iy) * grid.nx() + ix]; l >= 0) {
                comp_nodes[size_t(l)].emplace_back(iy * grid.nx() + ix);
                std::array<int, 4> &b = comp_box[size_t(l)];
                b = { std::min(b[0], ix), std::min(b[1], iy), std::max(b[2], ix), std::max(b[3], iy) };
            }
    Mask               cmask(n_nodes, 0);
    std::vector<float> dist(n_nodes, Inf);
    HeightField        s_field, d_field;
    if (free_mode) {
        s_field = d_field = grid;
        std::fill(s_field.data().begin(), s_field.data().end(), HeightField::NoData);
        std::fill(d_field.data().begin(), d_field.data().end(), HeightField::NoData);
    }
    const std::vector<int> *marked = nullptr;
    for (int c = 0; c < n_comp; ++c) {
        throw_if_canceled(object);
        if (marked != nullptr)
            for (int i : *marked) {
                cmask[size_t(i)] = 0;
                dist[size_t(i)]  = Inf;
                if (free_mode)
                    s_field.data()[size_t(i)] = d_field.data()[size_t(i)] = HeightField::NoData;
            }
        const std::vector<int> &nodes = comp_nodes[size_t(c)];
        marked                        = &nodes;
        const int ix0 = comp_box[size_t(c)][0], iy0 = comp_box[size_t(c)][1], ix1 = comp_box[size_t(c)][2], iy1 = comp_box[size_t(c)][3];
        for (int i : nodes)
            cmask[size_t(i)] = 1;
        const ExPolygons footprint = mask_to_expolygons(cmask, grid, ix0, iy0, ix1, iy1);
        const ExPolygons footprint_ext = offset_ex(footprint, float(scale_(std::max(0.5 * w, wall_width))));
        auto reject = [&](Why reason) {
            for (int i : nodes)
                why[size_t(i)] = reason;
        };
        if (area(footprint) * SCALING_FACTOR * SCALING_FACTOR < settings.min_area) {
            reject(Small);
            continue;
        }

        std::vector<int>   front;
        std::vector<std::pair<size_t, size_t>> front_island;
        for (int i : nodes) {
            const int    ix = i % grid.nx(), iy = i / grid.nx();
            const size_t l  = layer_at(Sb.data()[size_t(i)] - 0.5 * g);
            const Layer &below = *layers[l];
            for (const auto &[dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } }) {
                const int qx = ix + dx, qy = iy + dy;
                if (qx < 0 || qy < 0 || qx >= grid.nx() || qy >= grid.ny() || cmask[size_t(qy) * grid.nx() + qx])
                    continue;
                const Vec2d q  = grid.node(qx, qy);
                const Point pt = Point::new_scale(q.x(), q.y());
                if (const auto e = std::find_if(below.lslices.begin(), below.lslices.end(), [&pt](const ExPolygon &e) { return e.contains(pt); });
                    e != below.lslices.end()) {
                    dist[size_t(i)] = 0.f;
                    front.emplace_back(i);
                    front_island.emplace_back(l, size_t(e - below.lslices.begin()));
                    break;
                }
            }
        }
        if (front.empty()) {
            reject(NoRoot);
            continue;
        }
        {
            using QItem = std::pair<float, int>;
            std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> queue;
            for (int i : front)
                queue.emplace(0.f, i);
            while (!queue.empty()) {
                const auto [d, i] = queue.top();
                queue.pop();
                if (d > dist[size_t(i)])
                    continue;
                const int ix = i % grid.nx(), iy = i / grid.nx();
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int qx = ix + dx, qy = iy + dy;
                        if ((dx == 0 && dy == 0) || qx < 0 || qy < 0 || qx >= grid.nx() || qy >= grid.ny())
                            continue;
                        const size_t q = size_t(qy) * grid.nx() + qx;
                        if (!cmask[q])
                            continue;
                        const float nd = d + float(pitch * std::hypot(dx, dy));
                        if (nd < dist[q]) {
                            dist[q] = nd;
                            queue.emplace(nd, int(q));
                        }
                    }
            }
        }

        {
            float far = 0.f;
            for (int i : nodes)
                far = std::max(far, dist[size_t(i)]);
            const bool strip = std::none_of(footprint.begin(), footprint.end(), [](const ExPolygon &e) { return !e.holes.empty(); });
            if (far <= Nonplanar::ThreadDepth) {
                const int    tip = *std::max_element(nodes.begin(), nodes.end(), [&](int a, int b) { return dist[size_t(a)] < dist[size_t(b)]; });
                const Vec2d  q   = grid.node(tip % grid.nx(), tip / grid.nx());
                const Point  pt  = Point::new_scale(q.x(), q.y());
                const Layer &at  = *layers[layer_at(Sb.data()[size_t(tip)] - 0.5 * g)];
                const float ww   = float(scale_(std::max(wall_width, w)));
                bool        bore = false;
                for (const ExPolygon &e : at.lslices)
                    if (e.contour.contains(pt) && !e.contains(pt))
                        for (Polygon hole : e.holes)
                            if (hole.contains(pt) && fit_round(hole) && std::abs(hole.area()) * SCALING_FACTOR * SCALING_FACTOR <= Nonplanar::ThreadBoreArea) {
                                hole.make_counter_clockwise();
                                bore = !intersection_ex(opening_ex(ExPolygons{ e }, 1.5f * ww), offset_ex(ExPolygon(hole), 2.f * ww)).empty();
                            }
                if (bore) {
                    reject(Thread);
                    continue;
                }
            }
            std::vector<float> ceiling;
            for (int i : nodes)
                ceiling.emplace_back(slope[size_t(i)]);
            std::nth_element(ceiling.begin(), ceiling.begin() + ptrdiff_t(ceiling.size() / 2), ceiling.end());
            const bool arc = !ceiling.empty() && ceiling[ceiling.size() / 2] >= FlatUnderside;
            if (strip && arc && far <= BoreHalfSpan) {
                Mask root(n_nodes, 0);
                for (int i : front)
                    root[size_t(i)] = 1;
                int groups = 0;
                for (int seed : front)
                    if (root[size_t(seed)] == 1) {
                        ++groups;
                        std::vector<int> stack{ seed };
                        root[size_t(seed)] = 2;
                        while (!stack.empty()) {
                            const int i = stack.back();
                            stack.pop_back();
                            const int ix = i % grid.nx(), iy = i / grid.nx();
                            for (int dy = -1; dy <= 1; ++dy)
                                for (int dx = -1; dx <= 1; ++dx)
                                    if (const int qx = ix + dx, qy = iy + dy; qx >= 0 && qy >= 0 && qx < grid.nx() && qy < grid.ny())
                                        if (const size_t q = size_t(qy) * grid.nx() + qx; root[q] == 1) {
                                            root[q] = 2;
                                            stack.emplace_back(int(q));
                                        }
                        }
                    }
                if (groups >= 2) {
                    reject(Bridged);
                    continue;
                }
            }
            if (ceiling[ceiling.size() / 2] <= 0.5 * g / w) {
                std::vector<std::pair<size_t, size_t>> islands(front_island);
                std::sort(islands.begin(), islands.end());
                islands.erase(std::unique(islands.begin(), islands.end()), islands.end());
                std::vector<int> support(islands.size());
                int              n_supports = 0;
                for (size_t a = 0; a < islands.size(); ++a) {
                    const ExPolygon &ea    = layers[islands[a].first]->lslices[islands[a].second];
                    int              below = -1;
                    for (size_t b = 0; b < a && below != -2; ++b)
                        if (islands[b].first < islands[a].first && support[b] != below) {
                            const ExPolygon &eb = layers[islands[b].first]->lslices[islands[b].second];
                            if (get_extents(ea).overlap(get_extents(eb)) && !intersection_ex(ea, eb).empty())
                                below = below == -1 && support[b] >= 0 ? support[b] : -2;
                        }
                    support[a] = below == -1 ? n_supports++ : below;
                }
                std::vector<std::pair<int, int>> roots;
                for (size_t k = 0; k < front.size(); ++k)
                    if (const int s = support[size_t(std::lower_bound(islands.begin(), islands.end(), front_island[k]) - islands.begin())]; s >= 0)
                        roots.emplace_back(front[k], s);
                if (between_supports(nodes, roots, grid, MaxBridgeSpan) >= 0.5) {
                    reject(Bridged);
                    continue;
                }
            }
        }

        std::vector<float> slopes;
        float              z_ref = Inf;
        for (int i : nodes) {
            slopes.emplace_back(slope[size_t(i)]);
            z_ref = std::min(z_ref, Sb.data()[size_t(i)]);
        }
        std::sort(slopes.begin(), slopes.end());
        double tan_max = std::tan(settings.max_bend);
        const double g10     = slopes[slopes.size() / (free_mode ? 50 : 10)];
        const double g50     = slopes[slopes.size() / 2];
        if (free_mode && g50 < FlatUnderside)
            tan_max = std::max(tan_max, grad_need);
        double       kappa   = g50 < FlatUnderside ? 0. : std::max(0., std::min(grad_need / std::max(g10, 1e-3) - 1., tan_max / g50));
        double       beta    = std::clamp(grad_need - (1. + kappa) * g10, 0., std::max(0., tan_max - kappa * g10));
        std::vector<Vec2d> grad_s, grad_d;
        if (free_mode) {
            for (int i : nodes) {
                s_field.data()[size_t(i)] = Sb.data()[size_t(i)];
                d_field.data()[size_t(i)] = dist[size_t(i)];
            }
            for (int i : nodes) {
                grad_s.emplace_back(node_gradient(s_field, i % grid.nx(), i / grid.nx()));
                grad_d.emplace_back(node_gradient(d_field, i % grid.nx(), i / grid.nx()));
            }
            std::vector<double> layer_slope(nodes.size());
            auto evaluate = [&](double kp, double bt, double &p90) {
                size_t grown = 0, n = 0;
                for (size_t j = 0; j < nodes.size(); ++j) {
                    if (!grad_s[j].allFinite() || !grad_d[j].allFinite())
                        continue;
                    const Vec2d gD = kp * grad_s[j] + bt * grad_d[j], gT = grad_s[j] + gD;
                    layer_slope[n++] = gD.norm();
                    grown += gT.dot(grad_d[j]) > 0. && gT.norm() >= grad_req;
                }
                if (n == 0)
                    return p90 = 0., size_t(0);
                std::nth_element(layer_slope.begin(), layer_slope.begin() + (n * 9) / 10, layer_slope.begin() + n);
                p90 = layer_slope[(n * 9) / 10];
                return grown;
            };
            struct Candidate { double kappa, beta, p90; size_t grown; };
            std::vector<Candidate> candidates(1, Candidate{ kappa, beta, 0., 0 });
            candidates.front().grown = evaluate(kappa, beta, candidates.front().p90);
            const double kappa_max = std::min(20., tan_max / std::max(g50, FlatUnderside));
            for (int a = 0; a <= 8; ++a)
                for (int b = 0; b <= 12; ++b) {
                    Candidate c{ kappa_max * a / 8., tan_max * b / 12., 0., 0 };
                    c.grown = evaluate(c.kappa, c.beta, c.p90);
                    if (c.p90 <= tan_max + 1e-9)
                        candidates.emplace_back(c);
                }
            const Candidate *pick = &candidates.front();
            for (const Candidate &c : candidates)
                if (c.grown > pick->grown || (c.grown == pick->grown && pick != &candidates.front() && c.p90 < pick->p90))
                    pick = &c;
            kappa = pick->kappa;
            beta  = pick->beta;
        }
        double contact_min = 1., contact_med = 1. - g / (((1. + kappa) * g50 + beta) * w);
        for (float sl : slopes)
            contact_min = std::min(contact_min, 1. - g / (((1. + kappa) * sl + beta) * w));

        const int    cx0 = std::max(0, ix0 - pad_nodes), cy0 = std::max(0, iy0 - pad_nodes);
        const int    cx1 = std::min(grid.nx() - 1, ix1 + pad_nodes), cy1 = std::min(grid.ny() - 1, iy1 + pad_nodes);
        HeightField  S_c = crop(Sb, cx0, cy0, cx1, cy1), F_c = crop(floor_z, cx0, cy0, cx1, cy1), D_c = S_c;
        Mask         in_c(S_c.data().size(), 0), band_c(S_c.data().size(), 0);
        double       k_lo = Inf, k_hi = -Inf;
        for (int iy = 0; iy < S_c.ny(); ++iy)
            for (int ix = 0; ix < S_c.nx(); ++ix) {
                const size_t i = size_t(iy) * S_c.nx() + ix;
                if (cmask[size_t(cy0 + iy) * grid.nx() + cx0 + ix]) {
                    in_c[i]       = 1;
                    const float b = S_c.data()[i];
                    D_c.data()[i] = float(kappa * (b - z_ref) + beta * dist[size_t(cy0 + iy) * grid.nx() + cx0 + ix]);
                } else
                    S_c.data()[i] = F_c.data()[i] = D_c.data()[i] = HeightField::NoData;
            }
        smooth_inside(D_c, in_c, std::max(2, int(std::ceil(sqr(0.6 / pitch)))));
        for (size_t i = 0; i < in_c.size(); ++i)
            if (in_c[i]) {
                k_lo = std::min(k_lo, (S_c.data()[i] + D_c.data()[i] + 0.5 * g) / g);
                k_hi = std::max(k_hi, (F_c.data()[i] + D_c.data()[i] + 0.5 * g) / g);
            }
        for (int iy = 0; iy < S_c.ny(); ++iy)
            for (int ix = 0; ix < S_c.nx(); ++ix)
                if (in_c[size_t(iy) * S_c.nx() + ix])
                    for (int y = std::max(0, iy - pad_nodes); y <= std::min(S_c.ny() - 1, iy + pad_nodes); ++y)
                        std::fill(band_c.begin() + size_t(y) * S_c.nx() + std::max(0, ix - pad_nodes),
                                  band_c.begin() + size_t(y) * S_c.nx() + std::min(S_c.nx() - 1, ix + pad_nodes) + 1, uint8_t(1));
        HeightField S_pad = S_c, F_pad = F_c, D_pad = D_c;
        pad_lowest(S_pad, 2);
        pad_lowest(F_pad, 2);
        pad_lowest(D_pad, 2);
        auto nearest = [](const HeightField &f, const Vec2d &p) {
            const Vec2d u  = (p - f.origin()) / f.pitch();
            const int   ix = int(std::floor(u.x())), iy = int(std::floor(u.y()));
            float       best = HeightField::NoData;
            double      best_d = Inf;
            for (int y = iy; y <= iy + 1; ++y)
                for (int x = ix; x <= ix + 1; ++x)
                    if (x >= 0 && y >= 0 && x < f.nx() && y < f.ny() && !std::isnan(f.at(x, y)))
                        if (const double d = (f.node(x, y) - p).squaredNorm(); d < best_d)
                            best_d = d, best = f.at(x, y);
            return best;
        };
        auto floor_at = [&](const Vec2d &p) {
            const float F = nearest(F_c, p);
            return std::isnan(F) ? nearest(F_pad, p) : F;
        };

        const int                       k_first = int(std::floor(k_lo)), k_last = int(std::ceil(k_hi));
        std::vector<std::vector<Piece>> per_k(size_t(std::max(0, k_last - k_first + 1)));
        const ExtrusionPath             outer_proto(free_mode ? erOverhangPerimeter : erExternalPerimeter, flow.mm3_per_mm(), float(w), float(g));
        const ExtrusionPath             side_proto(erExternalPerimeter, flow.mm3_per_mm(), float(w), float(g));
        const ExtrusionPath             inner_proto(erSolidInfill, flow.mm3_per_mm(), float(w), float(g));
        using AtFn = std::function<bool(const Vec2d &, double &, float &, bool &)>;
        const bool flat = kappa == 0. && g50 < FlatUnderside;
        auto emit_into = [&](const AtFn &at, std::vector<Piece> &out, int k, const ExtrusionPath &proto, const Polyline &pl, bool side) {
            struct V { Vec2d p; double z; float bead; bool capped; };
            std::vector<V> run;
            auto flush = [&]() {
                if (run.size() < 2) {
                    run.clear();
                    return;
                }
                auto close = [&](int sub, std::vector<V> &pts) {
                    if (pts.size() < 2)
                        return;
                    const double host_z = layer_top[size_t(sb_layer[size_t(sub)])];
                    double       min_z  = Inf;
                    for (const V &v : pts)
                        min_z = std::min(min_z, v.z);
                    Points3            pts3;
                    std::vector<float> heights;
                    for (const V &v : pts) {
                        pts3.emplace_back(coord_t(scale_(v.p.x())), coord_t(scale_(v.p.y())), coord_t(scale_(v.z - host_z)));
                        heights.emplace_back(v.bead);
                    }
                    double length = 0.;
                    for (size_t i = 1; i < pts.size(); ++i)
                        length += (pts[i].p - pts[i - 1].p).norm();
                    if (length < 0.5 * w)
                        return;
                    auto path = std::make_unique<ExtrusionPathConformal>(Polyline3(std::move(pts3)), proto, std::move(heights));
                    path->simplify(0.01);
                    if (path->polyline.points.size() >= 2)
                        out.push_back(Piece{ sub, k, min_z, std::move(path), pts.front().capped, side });
                };
                int            cur = sub_band(run.front().z);
                std::vector<V> pts{ run.front() };
                for (size_t i = 1; i < run.size(); ++i) {
                    const V &a = run[i - 1], &b = run[i];
                    for (int next = sub_band(b.z); next != cur;) {
                        const int    step = next > cur ? 1 : -1;
                        const double bz   = step > 0 ? sb_hi[size_t(cur)] : sb_hi[size_t(cur - 1)];
                        const double t    = std::clamp((bz - a.z) / (b.z - a.z), 0., 1.);
                        const V      x{ a.p + t * (b.p - a.p), bz, float(a.bead + t * (b.bead - a.bead)), a.capped };
                        pts.emplace_back(x);
                        close(cur, pts);
                        pts = { x };
                        cur += step;
                    }
                    pts.emplace_back(b);
                }
                close(cur, pts);
                run.clear();
            };
            for (size_t i = 0; i + 1 < pl.points.size(); ++i) {
                const Vec2d a = unscale(pl.points[i]), b = unscale(pl.points[i + 1]);
                const int   n = std::max(1, int(std::ceil((b - a).norm() / pitch)));
                for (int j = i == 0 ? 0 : 1; j <= n; ++j) {
                    V v{ a + (b - a) * (double(j) / n), 0., 0.f, false };
                    if (!at(v.p, v.z, v.bead, v.capped)) {
                        flush();
                        continue;
                    }
                    if (!run.empty() && v.capped != run.back().capped)
                        flush();
                    run.emplace_back(v);
                }
            }
            flush();
        };

        tbb::parallel_for(tbb::blocked_range<int>(k_first, k_last + 1), [&](const tbb::blocked_range<int> &range) {
            for (int k = range.begin(); k < range.end(); ++k) {
                throw_if_canceled(object);
                HeightField f1 = S_c, f2 = S_c;
                bool        any = false;
                double      z_top = -Inf;
                for (size_t i = 0; i < f1.data().size(); ++i) {
                    if (!in_c[i] && !band_c[i]) {
                        f1.data()[i] = f2.data()[i] = HeightField::NoData;
                        continue;
                    }
                    const double b = S_pad.data()[i], L = k * g - D_pad.data()[i];
                    const double F = in_c[i] ? F_c.data()[i] : floor_at(S_c.node(i % S_c.nx(), int(i / S_c.nx())));
                    f1.data()[i]   = float(L - 0.5 * g - b);
                    f2.data()[i]   = float(F - (L - 0.5 * g));
                    if (in_c[i] && f1.data()[i] >= -g && f2.data()[i] > 0.f) {
                        any   = true;
                        z_top = std::max(z_top, std::min(L, F) - 0.5 * g);
                    }
                }
                if (!any)
                    continue;
                const ExPolygons &outline = layers[layer_at(z_top)]->lslices;
                const ExPolygons  Q       = smooth_contours(positive_region(f1), 0.1, 1.);
                ExPolygons area_k = intersection_ex(intersection_ex(intersection_ex(Q, positive_region(f2)), footprint_ext), outline);
                area_k            = opening_ex(area_k, float(scale_(0.25 * w)));
                if (area_k.empty())
                    continue;

                auto at = [&](const Vec2d &p, double &z, float &bead, bool &capped) {
                    const float  b = S_pad.sample(p), D = D_pad.sample(p), F = floor_at(p);
                    if (std::isnan(b) || std::isnan(D) || std::isnan(F))
                        return false;
                    const double L = k * g - D;
                    if (L < b)
                        return false;
                    z      = L;
                    bead   = float(g);
                    capped = L + 0.5 * g >= F;
                    if (capped) {
                        z    = std::min(L + 0.5 * g, double(F));
                        bead = float(std::max(0.25 * g, z - (L - g)));
                    }
                    return z - bead >= layer_top.front() - EPSILON;
                };
                std::vector<Piece> &out = per_k[size_t(k - k_first)];
                auto hang_at = [&](const Vec2d &p, double &z, float &bead, bool &capped) {
                    if (!at(p, z, bead, capped))
                        return false;
                    if (!capped)
                        z = S_pad.sample(p) + g;
                    return true;
                };
                auto side_at = [&](const Vec2d &p, double &z, float &bead, bool &capped) {
                    if (!at(p, z, bead, capped))
                        return false;
                    return !(flat && z - bead < S_pad.sample(p) + 0.75 * g);
                };

                ExPolygons edge_k;
                if (flat) {
                    HeightField f1_bottom = f1;
                    for (float &v : f1_bottom.data())
                        v -= float(0.5 * g);
                    edge_k = positive_region(f1_bottom);
                }
                const ExPolygons zone = intersection_ex(area_k, offset_ex(outline, -float(scale_(w))));
                for (int m = 0;; ++m) {
                    const ExPolygons inset = flat ? (m == 0 ? edge_k : offset_ex(edge_k, -float(scale_(m * s)))) :
                                                    offset_ex(Q, -float(scale_(0.5 * w + m * s)));
                    if (inset.empty() || intersection_ex(inset, area_k).empty())
                        break;
                    for (const Polyline &pl : intersection_pl(to_polylines(inset), zone))
                        emit_into(m == 0 && flat ? AtFn(hang_at) : AtFn(at), out, k, m == 0 ? outer_proto : inner_proto, pl, false);
                }
                const ExPolygons side = flat ? ExPolygons() : diff_ex(area_k, offset_ex(outline, -float(scale_(w))));
                if (!side.empty())
                    for (const Polyline &pl : intersection_pl(to_polylines(offset_ex(area_k, -float(scale_(0.5 * w)))), side))
                        emit_into(side_at, out, k, side_proto, pl, true);
            }
        });

        if (flat)
            for (int j = 0;; ++j) {
                auto edge_at = [&](const Vec2d &p, double &z, float &bead, bool &capped) {
                    const float b = S_pad.sample(p), F = floor_at(p);
                    if (std::isnan(b) || std::isnan(F))
                        return false;
                    const double L = b + (j + 1) * g;
                    if (j == 0 ? L > F + EPSILON : L - 0.5 * g >= F)
                        return false;
                    z = L, bead = float(g), capped = j > 0 && L + 0.5 * g >= F;
                    if (capped)
                        z = F, bead = float(F - (L - g));
                    return z - bead >= layer_top.front() - EPSILON;
                };
                const ExPolygons &edge_outline = layers[layer_at(z_ref + (j + 0.5) * g)]->lslices;
                std::vector<Piece> edge;
                Polylines roads = intersection_pl(to_polylines(offset_ex(edge_outline, -float(scale_(0.5 * w)))), footprint);
                for (size_t a = 0; a < roads.size(); ++a)
                    for (size_t b = 0; b < roads.size(); ++b)
                        if (a != b && !roads[a].empty() && !roads[b].empty() && roads[a].last_point() == roads[b].first_point()) {
                            roads[a].points.insert(roads[a].points.end(), roads[b].points.begin() + 1, roads[b].points.end());
                            roads[b].points.clear();
                            b = size_t(-1);
                        }
                for (const Polyline &pl : roads)
                    if (!pl.empty())
                        emit_into(edge_at, edge, k_last + 1, j == 0 ? outer_proto : side_proto, pl, true);
                if (edge.empty())
                    break;
                per_k.emplace_back(std::move(edge));
            }

        if (flat) {
            const ExPolygons &support = layers[layer_at(z_ref - 0.5 * g)]->lslices;
            auto root_at = [&](const Vec2d &, double &z, float &bead, bool &capped) {
                z = z_ref + g, bead = float(g), capped = false;
                return z - bead >= layer_top.front() - EPSILON;
            };
            std::vector<Piece> root;
            const ExPolygons inside = intersection_ex(offset_ex(footprint, float(scale_(w + pitch))),
                                                      offset_ex(layers[layer_at(z_ref + 0.5 * g)]->lslices, -float(scale_(w))));
            for (const Polyline &pl : intersection_pl(to_polylines(support), inside))
                emit_into(root_at, root, k_first - 1, inner_proto, pl, false);
            for (const Piece &piece : root) {
                Polyline pl;
                for (const Point3 &q : piece.path->polyline.points)
                    pl.points.emplace_back(q.x(), q.y());
                root_roads[layer_at(z_ref + 0.5 * g)].emplace_back(std::move(pl));
            }
            per_k.emplace_back(std::move(root));
        }

        if (free_mode)
            for (const std::vector<Piece> &pieces : per_k)
                for (const Piece &piece : pieces)
                    if (piece.capped) {
                        Polyline pl;
                        for (const Point3 &q : piece.path->polyline.points)
                            pl.points.emplace_back(q.x(), q.y());
                        polygons_append(capped_cover, offset(pl, float(scale_(0.5 * w))));
                    }
        std::map<int, std::map<int, std::vector<Piece *>>> by_host;
        for (std::vector<Piece> &pieces : per_k)
            for (Piece &piece : pieces)
                by_host[sb_layer[size_t(piece.sub)]][piece.sub].emplace_back(&piece);
        if (by_host.empty()) {
            reject(NoBeads);
            continue;
        }
        append(accepted, footprint);
        PatchDiagnostics diag;
        diag.patch_id      = int(result.patches.size());
        diag.max_slope_deg = Geometry::rad2deg(std::atan(kappa * slopes.back() + beta));
        result.diagnostics.emplace_back(std::move(diag));
        const PatchField surface_field(S_pad), base_field(F_pad);
        for (auto &[host, subs] : by_host) {
            ConformalPatch patch;
            patch.id           = int(result.patches.size());
            patch.kind         = PatchKind::Overhang;
            patch.footprint    = footprint;
            patch.surface      = surface_field;
            patch.base         = base_field;
            patch.shell_height = g;
            patch.region_id    = settings.region_id;
            patch.host_layer   = size_t(host);
            patch.z_min        = Inf;
            patch.z_max        = -Inf;
            Point last         = Point::new_scale(grid.node(ix0, iy0).x(), grid.node(ix0, iy0).y());
            for (auto &[sub, pieces] : subs) {
                ExtrusionEntityCollection shell;
                shell.entities = order_by_support(pieces, last, w, g);
                for (ExtrusionEntity *ee : shell.entities) {
                    auto *path     = static_cast<ExtrusionPathConformal *>(ee);
                    path->patch_id = patch.id;
                    path->shell    = int(patch.shell_paths.size());
                    if (path->polyline.points.back().z() < path->polyline.points.front().z())
                        path->reverse();
                    for (const Point3 &p : path->polyline.points) {
                        const double z = layer_top[size_t(host)] + unscale<double>(p.z());
                        patch.z_min    = std::min(patch.z_min, z);
                        patch.z_max    = std::max(patch.z_max, z);
                    }
                }
                last = shell.entities.back()->last_point();
                shell.no_sort = true;
                patch.shell_paths.emplace_back(std::move(shell));
            }
            patch.shells = int(patch.shell_paths.size());
            layers[size_t(host)]->conformal_patches_before.emplace_back(patch.id);
            result.patches.emplace_back(std::move(patch));
        }
        all_nodes.insert(all_nodes.end(), nodes.begin(), nodes.end());
        size_t inward = 0, short_contact = 0;
        if (free_mode)
            for (size_t j = 0; j < nodes.size(); ++j) {
                const int   i  = nodes[j];
                const Vec2d gt = (1. + kappa) * grad_s[j] + beta * grad_d[j], &gd = grad_d[j];
                if (!gt.allFinite() || !gd.allFinite() || gt.dot(gd) <= 0. || gt.norm() < grad_req) {
                    ++(gt.allFinite() && gd.allFinite() && gt.dot(gd) > 0. ? short_contact : inward);
                    left["overhang_supported"] += node_area;
                    continue;
                }
                for (size_t l = layer_at(Sb.data()[size_t(i)] - g); l < layers.size() && layer_bottom[l] <= Sb.data()[size_t(i)] + g; ++l)
                    self_supported[l].emplace_back(i);
            }
        BOOST_LOG_TRIVIAL(info) << "Non-planar: curved overhang in " << object.model_object()->name << ": "
                                << area(footprint) * SCALING_FACTOR * SCALING_FACTOR << " mm2, bend "
                                << Geometry::rad2deg(std::atan(kappa * g50 + beta)) << " deg, contact " << 100. * contact_med
                                << " % (least " << 100. * contact_min << " %), " << by_host.size() << " host layers"
                                << "; kappa " << kappa << " beta " << beta << " slope p10/p50 " << g10 << "/" << g50
                                << ", supported nodes: " << inward << " inward, " << short_contact << " short of contact, of " << nodes.size();
    }
    report_fallback();
    if (result.patches.size() == first_patch) {
        publish_self_supported();
        return more;
    }
    if (free_mode) {
        const ExPolygons cover     = union_ex(capped_cover);
        const BoundingBox cover_box = get_extents(cover);
        const Mask        free_node = rasterize(cover, grid);
        auto under_floor = [&](const Vec3d &p) {
            const Vec2d u  = (p.head<2>() - grid.origin()) / pitch;
            const int   ix = int(std::lround(u.x())), iy = int(std::lround(u.y()));
            if (ix < 0 || iy < 0 || ix >= grid.nx() || iy >= grid.ny())
                return false;
            const size_t i = size_t(iy) * grid.nx() + ix;
            return free_node[i] && p.z() > Sb.data()[i] - g && p.z() < floor_z.data()[i] + 0.5 * g;
        };
        tbb::parallel_for(tbb::blocked_range<size_t>(0, first_patch), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx)
                if (ConformalPatch &patch = result.patches[idx]; patch.kind == PatchKind::Top && !cover.empty() &&
                                                                  get_extents(patch.footprint).overlap(cover_box))
                    for (ExtrusionEntityCollection &shell : patch.shell_paths)
                        erase_conformal(shell, layers[patch.host_layer]->print_z, 0.25 * pitch, under_floor);
        });
        publish_self_supported();
    }

    Mask all_mask(n_nodes, 0);
    for (int i : all_nodes)
        all_mask[size_t(i)] = 1;
    const ExPolygons all_footprint = mask_to_expolygons(all_mask, grid);
    std::vector<Polylines> spans(layers.size());
    {
        std::function<void(const ExtrusionEntityCollection &, double)> walk = [&](const ExtrusionEntityCollection &c, double host_z) {
            for (const ExtrusionEntity *ee : c.entities) {
                if (const auto *sub = dynamic_cast<const ExtrusionEntityCollection *>(ee)) {
                    walk(*sub, host_z);
                    continue;
                }
                const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
                if (path == nullptr || path->bead_height.size() != path->polyline.points.size())
                    continue;
                std::vector<Vec3d> pts;
                const Points3     &src = path->polyline.points;
                double             lo = Inf, hi = -Inf;
                for (size_t i = 0; i < src.size(); ++i) {
                    const size_t k = std::min(i + 1, src.size() - 1);
                    const Vec3d  a = unscale(src[i]) + Vec3d(0., 0., host_z), b = unscale(src[k]) + Vec3d(0., 0., host_z);
                    const int    n = k > i ? std::max(1, int(std::ceil((b - a).head<2>().norm() / (0.25 * w)))) : 1;
                    for (int j = 0; j < n; ++j)
                        pts.emplace_back(a + double(j) / n * (b - a));
                    lo = std::min(lo, a.z() - path->bead_height[i]), hi = std::max(hi, a.z());
                }
                for (size_t l = size_t(std::lower_bound(layer_top.begin(), layer_top.end(), lo - g) - layer_top.begin());
                     l < layers.size() && layer_bottom[l] <= hi; ++l) {
                    const double mid = 0.5 * (layer_bottom[l] + layer_top[l]);
                    Polyline     run;
                    for (size_t i = 0; i <= pts.size(); ++i) {
                        if (i < pts.size() && pts[i].z() >= mid)
                            run.points.emplace_back(Point::new_scale(pts[i].x(), pts[i].y()));
                        else if (!run.points.empty()) {
                            if (run.points.size() == 1)
                                run.points.emplace_back(run.points.front());
                            else
                                run.simplify(scale_(0.001));
                            spans[l].emplace_back(std::move(run));
                            run.points.clear();
                        }
                    }
                }
            }
        };
        for (size_t idx = first_patch; idx < result.patches.size(); ++idx)
            for (const ExtrusionEntityCollection &shell : result.patches[idx].shell_paths)
                walk(shell, layers[result.patches[idx].host_layer]->print_z);
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t l = range.begin(); l < range.end(); ++l) {
            throw_if_canceled(object);
            Mask below(n_nodes, 0);
            bool any = !root_roads[l].empty();
            for (int i : all_nodes)
                if (layer_top[l] > Sb.data()[size_t(i)] + EPSILON && layer_bottom[l] < floor_z.data()[size_t(i)] - EPSILON)
                    below[size_t(i)] = 1, any = true;
            if (!any)
                continue;
            ExPolygons excl = intersection_ex(intersection_ex(mask_to_expolygons(below, grid), all_footprint),
                                              offset(spans[l], float(scale_(0.5 * w))));
            if (!root_roads[l].empty())
                excl = union_ex(excl, offset(root_roads[l], float(scale_(0.5 * w))));
            if (excl.empty())
                continue;
            const BoundingBox excl_bbox = get_extents(excl);
            for (LayerRegion *layerm : layers[l]->regions()) {
                clip_extrusions(layerm->fills, excl, excl_bbox);
                clip_extrusions(layerm->perimeters, excl, excl_bbox);
            }
            layers[l]->conformal_covered = true;
            append(result.planar_exclusion[l], std::move(excl));
            result.planar_exclusion[l] = union_ex(result.planar_exclusion[l]);
        }
    });
    return more;
}

}
void build_curved_overhangs(PrintObject &object, ConformalResult &result)
{
    HeightField        prev_top;
    OverhangCandidates candidates;
    for (int pass = 0; pass < MaxPasses && build_overhang_pass(object, result, pass, prev_top, candidates); ++pass)
        ;
}

}
