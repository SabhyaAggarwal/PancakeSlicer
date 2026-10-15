#include "CurvedOverhang.hpp"

#include "AutoMode.hpp"

#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
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
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float Inf = std::numeric_limits<float>::infinity();
constexpr double ThinWallLayers = 15.;
constexpr double TargetContact  = 0.5;
constexpr double MinContact     = 0.4;
constexpr double DefaultMaxBend = 40.;
constexpr double SameHeight = 0.01;
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
    const double     g = object.config().layer_height.value;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        if (!overhangs_enabled(cfg, object.config()))
            continue;
        s.region_id = int(region_id);
        s.pitch     = std::max(0.05, object.config().nonplanar_surface_resolution.value);
        s.max_bend  = Geometry::deg2rad(cfg.nonplanar_max_slope.value > 0. && object.config().nonplanar_mode.value != NonplanarMode::Auto ? cfg.nonplanar_max_slope.value : DefaultMaxBend);
        s.min_area  = effective_fallback(cfg, object.config()) == NonplanarFallback::None ? 0. :
                      support_free(object.config()) ? std::min(cfg.nonplanar_min_patch_area.value, SupportFreeMinArea) :
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

double head_budget(const HeadEnvelope &head)
{
    double out = Inf;
    for (const HeadBox &box : head.boxes) {
        if (!box.nozzle) {
            out = std::min(out, box.min.z() - head.margin);
            continue;
        }
        const double r = std::hypot(std::max(std::abs(box.min.x()), std::abs(box.max.x())),
                                    std::max(std::abs(box.min.y()), std::abs(box.max.y())));
        out = std::min(out, r <= head.tip_allowance.radius ? std::max(box.min.z(), head.tip_allowance.height) : box.min.z());
    }
    return out;
}

ExPolygons positive_region(const HeightField &f)
{
    Polygons  polys;
    const int nx = f.nx(), ny = f.ny();
    auto val = [&f](int ix, int iy) { const float v = f.at(ix, iy); return std::isnan(v) ? -1. : double(v); };
    auto pt  = [&f](double ix, double iy) {
        const Vec2d p = f.origin() + f.pitch() * Vec2d(ix, iy);
        return Point::new_scale(p.x(), p.y());
    };
    for (int iy = 0; iy + 1 < ny; ++iy) {
        int run = -1;
        for (int ix = 0; ix + 1 < nx; ++ix) {
            const int    cx[4] = { ix, ix + 1, ix + 1, ix };
            const int    cy[4] = { iy, iy, iy + 1, iy + 1 };
            double       v[4];
            int          inside = 0;
            for (int c = 0; c < 4; ++c)
                inside += (v[c] = val(cx[c], cy[c])) >= 0.;
            if (inside == 4) {
                if (run < 0)
                    run = ix;
                continue;
            }
            if (run >= 0) {
                polys.emplace_back(Points{ pt(run, iy), pt(ix, iy), pt(ix, iy + 1), pt(run, iy + 1) });
                run = -1;
            }
            if (inside == 0)
                continue;
            Points cell;
            for (int a = 0; a < 4; ++a) {
                const int b = (a + 1) % 4;
                if (v[a] >= 0.)
                    cell.emplace_back(pt(cx[a], cy[a]));
                if ((v[a] >= 0.) != (v[b] >= 0.)) {
                    const bool a_lo = cy[a] < cy[b] || (cy[a] == cy[b] && cx[a] < cx[b]);
                    const int  lo = a_lo ? a : b, hi = a_lo ? b : a;
                    const double t = v[lo] / (v[lo] - v[hi]);
                    cell.emplace_back(pt(cx[lo] + t * (cx[hi] - cx[lo]), cy[lo] + t * (cy[hi] - cy[lo])));
                }
            }
            if (cell.size() >= 3)
                polys.emplace_back(std::move(cell));
        }
        if (run >= 0)
            polys.emplace_back(Points{ pt(run, iy), pt(nx - 1, iy), pt(nx - 1, iy + 1), pt(run, iy + 1) });
    }
    return union_ex(polys);
}

struct Piece
{
    int                                     sub   = 0;
    int                                     k     = 0;
    double                                  min_z = 0.;
    std::unique_ptr<ExtrusionPathConformal> path;
    bool                                    capped = false;
};

bool build_overhang_pass(PrintObject &object, ConformalResult &result, int pass, HeightField &prev_top)
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
    const double       wall_width = std::max(0, region.config().wall_loops.value) * s;
    auto               cancel = [&object]() { throw_if_canceled(object); };
    const bool   free_mode    = support_free(object.config());
    const int    threshold    = object.config().support_threshold_angle.value;
    const double printable    = Geometry::deg2rad(std::clamp(90. - (threshold > 0 ? threshold : 30), 45., 55.));
    const double contact_req  = std::max(MinContact, 1. - g * std::tan(printable) / w);
    const double grad_req     = g / ((1. - (free_mode ? contact_req : MinContact)) * w);
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

    const HeightField grid      = object_grid(object, pitch);
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
        polygons_append(bottoms, to_polygons(layer_bottoms));
    }
    if (bottoms.empty())
        return false;
    const float      seam      = float(scale_(0.01));
    ExPolygons       candidate = offset_ex(union_ex(offset(bottoms, seam)), -seam);
    const Mask       cand_mask = rasterize(candidate, grid);

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

    const double grad_need = g / ((1. - TargetContact) * w);
    const double grad_bend = free_mode ? std::min(grad_need, 1. / std::tan(printable)) : grad_need;
    const double grad_min  = std::max(0., g / ((1. - MinContact) * w) - std::tan(settings.max_bend));
    std::vector<float> slope(n_nodes, HeightField::NoData);
    Mask               mask(n_nodes, 0);
    enum Why : uint8_t { Taken, Unsampled, Resting, Rim, SelfSupporting, Unfit, Small, NoRoot, NoBeads };
    static constexpr const char *why_tag[] = { "overhang_outline", "overhang_unsampled", "overhang_resting", "overhang_rim",
                                               "overhang_self_supporting", "overhang_unfit", "overhang_small",
                                               "overhang_no_root", "overhang_no_beads" };
    std::vector<uint8_t> why(n_nodes, Taken);
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
                continue;
            }
            const Vec2d gr = node_gradient(Sb, ix, iy);
            if (std::isnan(gr.x()) || std::isnan(gr.y())) {
                why[i] = Rim;
                continue;
            }
            if (top.data()[i] - Sb.data()[i] < ThinWallLayers * g && (!free_mode || gr.norm() >= 1. / std::tan(printable)))
                continue;
            slope[i] = float(gr.norm());
            mask[i]  = slope[i] < grad_bend && slope[i] >= grad_min;
            if (!mask[i])
                why[i] = slope[i] >= grad_bend ? SelfSupporting : Unfit;
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
    int                    n_comp = 0;
    const std::vector<int> label  = label_components(mask, grid.nx(), grid.ny(), n_comp);
    if (n_comp == 0) {
        report_fallback();
        return more;
    }

    std::string        head_warning;
    const HeadEnvelope head = resolve_head_envelope(object.print()->config(), &head_warning);
    if (!head_warning.empty())
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: " << head_warning;
    double tau = std::min(0.5 * g, head_budget(head));
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
        return int(std::min<ptrdiff_t>(std::lower_bound(sb_hi.begin(), sb_hi.end(), z - 1e-6) - sb_hi.begin(),
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
    std::vector<int> all_nodes;
    const size_t     first_patch = result.patches.size();
    for (int c = 0; c < n_comp; ++c) {
        throw_if_canceled(object);
        std::vector<int> nodes;
        int              ix0 = grid.nx(), iy0 = grid.ny(), ix1 = -1, iy1 = -1;
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (label[size_t(iy) * grid.nx() + ix] == c) {
                    nodes.emplace_back(iy * grid.nx() + ix);
                    ix0 = std::min(ix0, ix), iy0 = std::min(iy0, iy), ix1 = std::max(ix1, ix), iy1 = std::max(iy1, iy);
                }
        Mask cmask(n_nodes, 0);
        for (int i : nodes)
            cmask[size_t(i)] = 1;
        const ExPolygons footprint = mask_to_expolygons(cmask, grid);
        const ExPolygons footprint_ext = offset_ex(footprint, float(scale_(std::max(0.5 * w, wall_width))));
        auto reject = [&](Why reason) {
            for (int i : nodes)
                why[size_t(i)] = reason;
        };
        if (area(footprint) * SCALING_FACTOR * SCALING_FACTOR < settings.min_area) {
            reject(Small);
            continue;
        }

        std::vector<float> dist(n_nodes, Inf);
        std::vector<int>   front;
        for (int i : nodes) {
            const int ix = i % grid.nx(), iy = i / grid.nx();
            const Layer &below = *layers[layer_at(Sb.data()[size_t(i)] - 0.5 * g)];
            for (const auto &[dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } }) {
                const int qx = ix + dx, qy = iy + dy;
                if (qx < 0 || qy < 0 || qx >= grid.nx() || qy >= grid.ny() || cmask[size_t(qy) * grid.nx() + qx])
                    continue;
                const Vec2d q  = grid.node(qx, qy);
                const Point pt = Point::new_scale(q.x(), q.y());
                if (std::any_of(below.lslices.begin(), below.lslices.end(), [&pt](const ExPolygon &e) { return e.contains(pt); })) {
                    dist[size_t(i)] = 0.f;
                    front.emplace_back(i);
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

        std::vector<float> slopes;
        float              z_ref = Inf;
        for (int i : nodes) {
            slopes.emplace_back(slope[size_t(i)]);
            z_ref = std::min(z_ref, Sb.data()[size_t(i)]);
        }
        std::sort(slopes.begin(), slopes.end());
        const double tan_max = std::tan(settings.max_bend);
        const double g10     = slopes[slopes.size() / (free_mode ? 50 : 10)];
        const double g50     = slopes[slopes.size() / 2];
        double       kappa   = g50 < FlatUnderside ? 0. : std::max(0., std::min(grad_need / std::max(g10, 1e-3) - 1., tan_max / g50));
        double       beta    = std::clamp(grad_need - (1. + kappa) * g10, 0., std::max(0., tan_max - kappa * g10));
        std::vector<Vec2d> grad_s, grad_d;
        if (free_mode) {
            HeightField s_field = grid, d_field = grid;
            std::fill(s_field.data().begin(), s_field.data().end(), HeightField::NoData);
            std::fill(d_field.data().begin(), d_field.data().end(), HeightField::NoData);
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
                    k_lo          = std::min(k_lo, (b + D_c.data()[i] + 0.5 * g) / g);
                    k_hi          = std::max(k_hi, (F_c.data()[i] + D_c.data()[i] + 0.5 * g) / g);
                } else
                    S_c.data()[i] = F_c.data()[i] = D_c.data()[i] = HeightField::NoData;
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
                const ExPolygons  Q       = positive_region(f1);
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
                std::vector<Piece> &out  = per_k[size_t(k - k_first)];
                auto                emit = [&](const ExtrusionPath &proto, const Polyline &pl) {
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
                                out.push_back(Piece{ sub, k, min_z, std::move(path), pts.front().capped });
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

                const ExPolygons zone = intersection_ex(area_k, offset_ex(outline, -float(scale_(w))));
                for (int m = 0;; ++m) {
                    const ExPolygons inset = offset_ex(Q, -float(scale_(0.5 * w + m * s)));
                    if (inset.empty() || intersection_ex(inset, area_k).empty())
                        break;
                    for (const Polyline &pl : intersection_pl(to_polylines(inset), zone))
                        emit(m == 0 ? outer_proto : inner_proto, pl);
                }
                const ExPolygons side = diff_ex(area_k, offset_ex(outline, -float(scale_(w))));
                if (!side.empty())
                    for (const Polyline &pl : intersection_pl(to_polylines(offset_ex(area_k, -float(scale_(0.5 * w)))), side))
                        emit(side_proto, pl);
            }
        });

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
        for (auto &[host, subs] : by_host) {
            ConformalPatch patch;
            patch.id           = int(result.patches.size());
            patch.kind         = PatchKind::Overhang;
            patch.footprint    = footprint;
            patch.surface      = S_pad;
            patch.base         = F_pad;
            patch.shell_height = g;
            patch.region_id    = settings.region_id;
            patch.host_layer   = size_t(host);
            patch.z_min        = Inf;
            patch.z_max        = -Inf;
            Point last         = Point::new_scale(grid.node(ix0, iy0).x(), grid.node(ix0, iy0).y());
            for (auto &[sub, pieces] : subs) {
                std::stable_sort(pieces.begin(), pieces.end(), [](const Piece *a, const Piece *b) { return a->min_z < b->min_z; });
                ExtrusionEntityCollection shell;
                for (size_t i = 0; i < pieces.size();) {
                    size_t               j = i;
                    ExtrusionEntitiesPtr group;
                    for (; j < pieces.size() && pieces[j]->min_z <= pieces[i]->min_z + SameHeight; ++j) {
                        ExtrusionPathConformal *path = pieces[j]->path.release();
                        path->patch_id               = patch.id;
                        path->shell                  = int(patch.shell_paths.size());
                        for (const Point3 &p : path->polyline.points) {
                            const double z = layer_top[size_t(host)] + unscale<double>(p.z());
                            patch.z_min    = std::min(patch.z_min, z);
                            patch.z_max    = std::max(patch.z_max, z);
                        }
                        group.emplace_back(path);
                    }
                    chain_and_reorder_extrusion_entities(group, &last);
                    for (ExtrusionEntity *ee : group)
                        if (auto *path = static_cast<ExtrusionPathConformal *>(ee); path->polyline.points.back().z() < path->polyline.points.front().z())
                            path->reverse();
                    last = group.back()->last_point();
                    append(shell.entities, std::move(group));
                    i = j;
                }
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
    if (result.patches.size() == first_patch)
        return more;
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
        if (result.self_supported.size() != layers.size())
            result.self_supported.assign(layers.size(), ExPolygons());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t l = range.begin(); l < range.end(); ++l)
                if (!self_supported[l].empty()) {
                    Mask m(n_nodes, 0);
                    for (int i : self_supported[l])
                        m[size_t(i)] = 1;
                    append(result.self_supported[l], offset_ex(mask_to_expolygons(m, grid), float(scale_(pitch))));
                    result.self_supported[l] = union_ex(result.self_supported[l]);
                }
        });
    }

    Mask all_mask(n_nodes, 0);
    for (int i : all_nodes)
        all_mask[size_t(i)] = 1;
    const ExPolygons all_footprint = mask_to_expolygons(all_mask, grid);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t l = range.begin(); l < range.end(); ++l) {
            throw_if_canceled(object);
            Mask below(n_nodes, 0);
            bool any = false;
            for (int i : all_nodes)
                if (layer_top[l] > Sb.data()[size_t(i)] + EPSILON && layer_bottom[l] < floor_z.data()[size_t(i)] - EPSILON)
                    below[size_t(i)] = 1, any = true;
            if (!any)
                continue;
            ExPolygons excl = intersection_ex(mask_to_expolygons(below, grid), all_footprint);
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
    HeightField prev_top;
    for (int pass = 0; pass < MaxPasses && build_overhang_pass(object, result, pass, prev_top); ++pass)
        ;
}

}
