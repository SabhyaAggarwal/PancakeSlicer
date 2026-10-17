#include "ConformalShells.hpp"

#include "AdaptiveBands.hpp"
#include "AutoMode.hpp"
#include "BeadModel.hpp"
#include "ConformalIroning.hpp"
#include "HeadEnvelope.hpp"
#include "PatchTools.hpp"
#include "Repair.hpp"
#include "Report.hpp"
#include "Seams.hpp"
#include "ShellPaths.hpp"
#include "ShellBase.hpp"
#include "SpiralTop.hpp"
#include "StrengthRoads.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float Inf = std::numeric_limits<float>::infinity();

struct ShellSettings
{
    int                  region_id = -1;
    int                  shells = 0;
    int                  top_shells = 0;
    int                  sparse = 0;
    double               sparse_density = 0.;
    double               solid_angle = 0.;
    double               bottom_keep = 0.;
    double               shell_height = 0.;
    double               max_slope = 0.;
    double               max_error = 0.;
    double               min_area = 0.;
    double               pitch = 0.2;
    double               max_patch_height = 0.;
    bool                 banded = false;
    bool                 adaptive = false;
};

ShellSettings shell_settings(const PrintObject &object, size_t region_id)
{
    ShellSettings s;
    const PrintRegionConfig &cfg = object.printing_region(region_id).config();
    if (!top_shells_enabled(cfg, object.config()))
        return s;
    s.region_id        = int(region_id);
    s.shells           = cfg.nonplanar_top_shells.value;
    s.shell_height     = cfg.nonplanar_shell_layer_height.value > 0. ? cfg.nonplanar_shell_layer_height.value :
                                                                        automatic_shell_height(object);
    s.max_slope        = Geometry::deg2rad(cfg.nonplanar_max_slope.value);
    s.max_error        = cfg.nonplanar_max_surface_error.value;
    s.min_area         = cfg.nonplanar_min_patch_area.value;
    s.pitch            = std::max(0.05, object.config().nonplanar_surface_resolution.value);
    s.max_patch_height = effective_patch_height_cap(object);
    s.banded           = effective_fallback(cfg, object.config()) == NonplanarFallback::None;
    if (object.config().nonplanar_mode.value == NonplanarMode::Auto) {
        const AutoObjectSettings a = auto_object_settings(object, object.printing_region(region_id), s.shell_height);
        s.shells           = a.clip_shells;
        s.max_slope        = 0.;
        s.max_error        = a.max_error;
        s.min_area         = a.min_area;
        s.max_patch_height = a.max_patch_height;
    } else if (s.shells < 0)
        s.shells = AutoMinShells;
    s.adaptive         = s.banded && cfg.nonplanar_adaptive_bands.value;
    s.top_shells       = s.shells;
    if (s.banded && infill_depth(cfg, object) > 0.) {
        const int top_solid = std::max(cfg.top_shell_layers.value, int(std::ceil(cfg.top_shell_thickness.value / s.shell_height - EPSILON)));
        s.sparse_density    = std::min(1., 0.01 * cfg.sparse_infill_density.value);
        if (s.sparse_density > 0.)
            s.sparse = int(std::lround(infill_depth(cfg, object) / s.shell_height));
        s.shells      = std::max(s.shells + (s.sparse > 0 ? 1 : 0), top_solid) + s.sparse;
        s.solid_angle = Geometry::deg2rad(cfg.infill_direction.value);
        s.bottom_keep = std::max(cfg.bottom_shell_layers.value * object.config().layer_height.value, cfg.bottom_shell_thickness.value);
    }
    return s;
}

ExPolygons candidate_top_area(const PrintObject &object, const std::vector<size_t> &region_ids)
{
    Polygons tops;
    for (const Layer *layer : object.layers())
        for (const LayerRegion *layerm : layer->regions())
            if (std::any_of(region_ids.begin(), region_ids.end(),
                            [&](size_t id) { return &layerm->region() == &object.printing_region(id); }))
                for (const Surface &surface : layerm->slices.surfaces)
                    if (surface.surface_type == stTop)
                        polygons_append(tops, to_polygons(surface.expolygon));
    const float seam = float(scale_(0.01));
    return offset_ex(union_ex(offset(tops, seam)), -seam);
}

constexpr size_t ContourLoops = 8;
constexpr int ContourRefine = 4;
constexpr double LengthwiseMaxLines = 4.;
constexpr double LengthwiseMinAspect = 4.;

void split_by_colour(ConformalResult &result, std::vector<HeightField> &planar_tops, std::vector<HeightField> &stack_layers,
                     std::vector<AdaptiveStack> &stacks, std::vector<ExPolygons> &shell_areas,
                     std::vector<ExPolygons> &fill_areas, std::vector<Point> &seams, std::vector<Polyline> &seam_lines,
                     const TopColours &colours, const std::function<int(const Vec3d &)> &colour)
{
    std::vector<HeightField> tops, depths;
    std::vector<AdaptiveStack> new_stacks;
    std::vector<ExPolygons>  areas, fills;
    std::vector<Point>       new_seams;
    std::vector<Polyline>    lines;
    const std::vector<std::pair<size_t, size_t>> sources = split_by_colour(result, colours, colour);
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto [idx, c] = sources[i];
        const ExPolygons own = colour ? union_ex(colours.areas[c], result.patches[i].footprint) : colours.areas[c];
        tops.emplace_back(planar_tops[idx]);
        depths.emplace_back(stack_layers[idx]);
        if (!stacks.empty())
            new_stacks.emplace_back(stacks[idx]);
        areas.emplace_back(intersection_ex(shell_areas[idx], own));
        fills.emplace_back(intersection_ex(fill_areas[idx], own));
        new_seams.emplace_back(seams[idx]);
        lines.emplace_back(seam_lines[idx]);
    }
    planar_tops = std::move(tops);
    stack_layers = std::move(depths);
    stacks      = std::move(new_stacks);
    shell_areas = std::move(areas);
    fill_areas  = std::move(fills);
    seams       = std::move(new_seams);
    seam_lines  = std::move(lines);
}

struct WidthRoads
{
    Polylines                       polylines;
    std::vector<std::vector<float>> widths;
};

Polygons width_cover(const WidthRoads &roads)
{
    Polygons out;
    for (size_t i = 0; i < roads.polylines.size(); ++i) {
        const Points             &pts = roads.polylines[i].points;
        const std::vector<float> &w   = roads.widths[i];
        for (size_t a = 0; a + 1 < pts.size();) {
            size_t b     = a + 1;
            float  w_min = std::min(w[a], w[b]);
            while (b + 1 < pts.size() && std::abs(w[b + 1] - w[a]) < 0.1f * w[a])
                w_min = std::min(w_min, w[++b]);
            Polygons piece = offset(Polyline(Points(pts.begin() + a, pts.begin() + b + 1)), float(scale_(0.5 * w_min)), jtRound, scale_(0.01),
                                    etOpenRound);
            for (Polygon &poly : piece)
                out.emplace_back(std::move(poly));
            a = b;
        }
    }
    return out;
}

Arachne::WallToolPathsParams arachne_params(const Flow &flow)
{
    Arachne::WallToolPathsParams params;
    params.min_bead_width                   = float(0.5 * flow.width());
    params.min_feature_size                 = float(0.25 * flow.nozzle_diameter());
    params.wall_transition_length           = float(3. * flow.nozzle_diameter());
    params.wall_transition_angle            = 10.f;
    params.wall_transition_filter_deviation = float(flow.spacing());
    params.wall_distribution_count          = 1;
    return params;
}

struct ArachneLoops
{
    Polygons                                outline;
    std::unique_ptr<Arachne::WallToolPaths> paths;
};

std::unique_ptr<ArachneLoops> arachne_paths(const ExPolygon &piece, const Flow &flow, double layer_height, size_t max_loops)
{
    const float eps     = float(scale_(0.005));
    auto        out     = std::make_unique<ArachneLoops>();
    Polygons   &outline = out->outline;
    outline = offset(offset_ex(expolygons_simplify({ piece }, scale_(0.01)), -eps), eps);
    if (outline.empty())
        return nullptr;
    const coord_t     bead  = coord_t(scale_(flow.spacing()));
    const BoundingBox bb    = get_extents(outline);
    const size_t      loops = max_loops > 0 ? max_loops : size_t(std::max(bb.size().x(), bb.size().y()) / std::max<coord_t>(bead, 1)) + 1;
    out->paths = std::make_unique<Arachne::WallToolPaths>(outline, bead, bead, loops, 0, layer_height, arachne_params(flow));
    out->paths->getToolPaths();
    return out;
}

void arachne_roads(const ExPolygon &piece, Arachne::WallToolPaths *paths, const Flow &flow, double layer_height, size_t max_loops, WidthRoads &out,
                   Polygons *inner = nullptr)
{
    if (paths == nullptr)
        return;
    const Arachne::WallToolPathsParams params = arachne_params(flow);

    std::vector<const Arachne::ExtrusionLine *> lines;
    for (const Arachne::VariableWidthLines &inset : paths->getToolPaths())
        for (const Arachne::ExtrusionLine &line : inset)
            if (line.junctions.size() >= 2)
                lines.emplace_back(&line);
    std::stable_sort(lines.begin(), lines.end(), [](const auto *a, const auto *b) { return a->inset_idx > b->inset_idx; });
    const float      to_width = float(flow.width() - flow.spacing());
    const float      min_spacing = std::max(params.min_feature_size, float(layer_height));
    const double     link_max = 2. * flow.spacing();
    const ExPolygons link_area{ piece };
    std::optional<ExPolygons> link_grown;
    std::optional<Point> last;
    while (!lines.empty()) {
        size_t best = 0;
        double best_d = std::numeric_limits<double>::max();
        for (size_t i = 0; i < lines.size() && lines[i]->inset_idx == lines.front()->inset_idx; ++i) {
            const auto &js = lines[i]->junctions;
            double      d  = 0.;
            if (last) {
                d = std::min((js.front().p - *last).cast<double>().squaredNorm(), (js.back().p - *last).cast<double>().squaredNorm());
                if (lines[i]->is_closed)
                    for (size_t j = 0; j + 1 < js.size(); ++j)
                        d = std::min(d, Line(js[j].p, js[j + 1].p).distance_to_squared(*last));
            }
            if (d < best_d) {
                best_d = d;
                best   = i;
            }
        }
        const Arachne::ExtrusionLine &line = *lines[best];
        lines.erase(lines.begin() + best);
        Polyline           pl;
        std::vector<float> w;
        for (const Arachne::ExtrusionJunction &j : line.junctions) {
            pl.points.emplace_back(j.p);
            w.emplace_back(std::max(min_spacing, float(unscale<double>(j.w))) + to_width);
        }
        if (line.is_closed) {
            size_t start = 0;
            if (last) {
                double best_seg = std::numeric_limits<double>::max();
                Point  foot;
                for (size_t j = 0; j + 1 < pl.points.size(); ++j) {
                    Point        f;
                    const double d = Line(pl.points[j], pl.points[j + 1]).distance_to_squared(*last, &f);
                    if (d < best_seg) {
                        best_seg = d;
                        start    = j + 1;
                        foot     = f;
                    }
                }
                const double seg = (pl.points[start] - pl.points[start - 1]).cast<double>().norm();
                if (const double t = seg > 0. ? (foot - pl.points[start - 1]).cast<double>().norm() / seg : 0.; t > 0.01 && t < 0.99) {
                    pl.points.insert(pl.points.begin() + start, foot);
                    w.insert(w.begin() + start, float(w[start - 1] + t * (w[start] - w[start - 1])));
                } else if (t <= 0.01)
                    --start;
            }
            pl.points.pop_back();
            w.pop_back();
            start %= pl.points.size();
            std::rotate(pl.points.begin(), pl.points.begin() + start, pl.points.end());
            std::rotate(w.begin(), w.begin() + start, w.end());
            pl.points.emplace_back(pl.points.front());
            w.emplace_back(w.front());
            double cut = scale_(flow.spacing());
            while (pl.points.size() > 2 && cut > 0.) {
                const double seg = (pl.points.back() - pl.points[pl.points.size() - 2]).cast<double>().norm();
                if (seg > cut) {
                    const Vec2d a = pl.points[pl.points.size() - 2].cast<double>(), b = pl.points.back().cast<double>();
                    pl.points.back() = (b + (a - b) * (cut / seg)).cast<coord_t>();
                    break;
                }
                cut -= seg;
                pl.points.pop_back();
                w.pop_back();
            }
            {
                Vec2d c = Vec2d::Zero();
                for (size_t j = 0; j < pl.points.size(); ++j)
                    c += unscale(pl.points[j]);
                c /= double(pl.points.size());
                double r = 0.;
                for (const Point &q : pl.points)
                    r = std::max(r, (unscale(q) - c).norm());
                bool summit = r < 1.25 * flow.width() && !(last && (unscale(*last) - c).norm() < r);
                for (size_t i = 0; summit && i < lines.size(); ++i)
                    for (const Arachne::ExtrusionJunction &j : lines[i]->junctions)
                        summit = summit && (unscale(j.p) - c).norm() >= r;
                if (summit) {
                    const Vec2d  s0 = unscale(pl.points.front()) - c;
                    const double a0 = std::atan2(s0.y(), s0.x());
                    const double rc = std::max(r - flow.width(), 0.5 * r);
                    Points       pre{ Point::new_scale(c.x(), c.y()) };
                    for (int k = 0; k <= 6; ++k) {
                        const double a = a0 + 2. * PI * k / 6.;
                        pre.emplace_back(Point::new_scale(c.x() + rc * std::cos(a), c.y() + rc * std::sin(a)));
                    }
                    pl.points.insert(pl.points.begin(), pre.begin(), pre.end());
                    w.insert(w.begin(), pre.size(), float(flow.width()));
                }
            }
        } else if (last && (pl.points.back() - *last).cast<double>().squaredNorm() < (pl.points.front() - *last).cast<double>().squaredNorm()) {
            pl.reverse();
            std::reverse(w.begin(), w.end());
        }
        if (last && !out.polylines.empty() && (pl.points.front() - *last).cast<double>().norm() <= scale_(link_max) &&
            diff_pl(Polylines{ Polyline(*last, pl.points.front()) },
                    link_grown ? *link_grown : link_grown.emplace(offset_ex(link_area, float(scale_(0.25 * flow.spacing()))))).empty()) {
            append(out.polylines.back().points, std::move(pl.points));
            append(out.widths.back(), std::move(w));
        } else {
            out.polylines.emplace_back(std::move(pl));
            out.widths.emplace_back(std::move(w));
        }
        last = out.polylines.back().points.back();
    }
    if (inner != nullptr && max_loops > 0)
        append(*inner, paths->getInnerContour());
}

void arachne_roads(const ExPolygon &piece, const Flow &flow, double layer_height, size_t max_loops, WidthRoads &out, Polygons *inner = nullptr)
{
    const std::unique_ptr<ArachneLoops> loops = arachne_paths(piece, flow, layer_height, max_loops);
    arachne_roads(piece, loops ? loops->paths.get() : nullptr, flow, layer_height, max_loops, out, inner);
}

void contour_fill(const HeightField &surface, const ExPolygons &area, double drop, double strip_height, double strip_offset, const Flow &flow, double layer_height,
                  double angle, bool outer, double z, WidthRoads &roads, std::vector<Polylines> &lines)
{
    HeightField fine(BoundingBoxf(surface.origin(), surface.node(surface.nx() - 1, surface.ny() - 1)), surface.pitch() / ContourRefine);
    std::vector<float>().swap(fine.data());
    RunGroups strips;
    for (int iy = 0; iy < fine.ny(); ++iy)
        for (int ix = 0; ix < fine.nx(); ++ix)
            if (const float s = surface.sample(fine.node(ix, iy)); !std::isnan(s) && !spans_wall(surface, fine.node(ix, iy)))
                strips.add(int(std::floor((s - drop - strip_offset) / strip_height)), ix, iy);
    ExPolygons pieces;
    for (const auto &[strip, runs] : strips.groups())
        append(pieces, expolygons_simplify(intersection_ex(area, runs_to_expolygons(runs, fine)), scale_(0.5 * fine.pitch())));
    std::vector<std::unique_ptr<ArachneLoops>> paths(pieces.size());
    std::vector<std::vector<Polylines>>        middles(pieces.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, pieces.size(), 1), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            const ExPolygon &piece = pieces[i];
            paths[i] = arachne_paths(piece, flow, layer_height, ContourLoops);
            if (!paths[i])
                continue;
            const ExPolygons middle = intersection_ex(union_ex(paths[i]->paths->getInnerContour()), ExPolygons{ piece });
            if (middle.empty())
                continue;
            double a = angle;
            {
                const BoundingBox bb  = get_extents(middle);
                const Vec2d       lo  = (unscale(bb.min) - surface.origin()) / surface.pitch();
                const Vec2d       hi  = (unscale(bb.max) - surface.origin()) / surface.pitch();
                const int         ix0 = std::clamp(int(std::floor(lo.x())), 0, surface.nx() - 1), iy0 = std::clamp(int(std::floor(lo.y())), 0, surface.ny() - 1);
                const int         ix1 = std::clamp(int(std::ceil(hi.x())), 0, surface.nx() - 1), iy1 = std::clamp(int(std::ceil(hi.y())), 0, surface.ny() - 1);
                HeightField       local  = crop(surface, ix0, iy0, ix1, iy1);
                const Mask        inside = rasterize(middle, local);
                for (size_t n = 0; n < inside.size(); ++n)
                    if (!inside[n])
                        local.data()[n] = HeightField::NoData;
                contour_line_angle(local, a, 0.5);
            }
            const ExPolygons lined = intersection_ex(offset_ex(middle, float(scale_(0.25 * flow.spacing()))), ExPolygons{ piece });
            Polylines pl = fill_solid(lined, outer ? ipMonotonic : ipRectilinear, flow.spacing(), a, outer, flow, z);
            pl.erase(std::remove_if(pl.begin(), pl.end(), [&middle](const Polyline &l) { return intersection_pl(Polylines{ l }, middle).empty(); }),
                     pl.end());
            if (!pl.empty())
                middles[i] = link_paths(chain_polylines(std::move(pl)), offset_ex(middle, float(scale_(0.5 * flow.spacing()))), flow.spacing(),
                                        MaxGlide);
        }
    });
    for (size_t i = 0; i < pieces.size(); ++i) {
        arachne_roads(pieces[i], paths[i] ? paths[i]->paths.get() : nullptr, flow, layer_height, ContourLoops, roads);
        append(lines, std::move(middles[i]));
    }
}

ExPolygons lengthwise_strips(const ExPolygons &area, double line_width)
{
    ExPolygons out;
    for (ExPolygon &narrow : diff_ex(area, opening_ex(area, float(scale_(0.5 * LengthwiseMaxLines * line_width))))) {
        const double a    = narrow.area() * SCALING_FACTOR * SCALING_FACTOR;
        const double p    = narrow.contour.length() * SCALING_FACTOR;
        const double disc = 0.0625 * p * p - a;
        if (disc <= 0.)
            continue;
        const double l = 0.25 * p + std::sqrt(disc), w = std::max(0.25 * p - std::sqrt(disc), line_width);
        if (l >= LengthwiseMinAspect * w)
            out.emplace_back(std::move(narrow));
    }
    return out;
}

Polylines sparse_strip_fill(const HeightField &surface, const ExPolygons &area, double drop, double strip_height, double spacing,
                            const Point &start)
{
    RunGroups strips;
    for (size_t n = 0; n < surface.data().size(); ++n)
        if (const float s = surface.data()[n]; !std::isnan(s))
            strips.add(int(std::floor((s - drop) / strip_height)), int(n % surface.nx()), int(n / surface.nx()));
    Polylines out;
    for (const auto &[strip, runs] : strips.groups())
        for (const ExPolygon &piece : intersection_ex(area, runs_to_expolygons(runs, surface)))
            for (double inset = 0.5 * spacing;; inset += spacing) {
                const Polygons rings = to_polygons(offset_ex(ExPolygons{ piece }, -float(scale_(inset))));
                if (rings.empty())
                    break;
                for (const Polygon &ring : rings)
                    if (ring.size() >= 3)
                        out.emplace_back(ring.split_at_index(ring.closest_point_index(start)));
            }
    return out;
}

bool outline_is_contour(const ConformalPatch &patch, double tolerance)
{
    const double pitch = patch.surface.pitch();
    float        lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    float        zmin = std::numeric_limits<float>::max(), zmax = std::numeric_limits<float>::lowest();
    for (const float s : patch.surface.data())
        if (!std::isnan(s)) {
            zmin = std::min(zmin, s);
            zmax = std::max(zmax, s);
        }
    ExPolygons inset = offset_ex(patch.footprint, -float(scale_(std::max(2. * pitch, 1.))));
    if (inset.empty())
        inset = offset_ex(patch.footprint, -float(scale_(2. * pitch)));
    for (const ExPolygon &expoly : inset) {
        const Points &pts = expoly.contour.points;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Vec2d a = unscale(pts[i]), b = unscale(pts[(i + 1) % pts.size()]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / pitch)));
            for (int j = 0; j < n; ++j)
                if (const float s = patch.surface.sample(a + (b - a) * (double(j) / n)); !std::isnan(s)) {
                    lo = std::min(lo, s);
                    hi = std::max(hi, s);
                }
        }
    }
    return hi >= lo && hi - lo <= std::max(tolerance, 0.1 * double(zmax - zmin));
}

bool is_round(const ExPolygons &footprint)
{
    double a = 0., p = 0.;
    for (const ExPolygon &expoly : footprint) {
        a += expoly.area();
        for (const Polygon &poly : to_polygons(expoly))
            p += poly.length();
    }
    return p > 0. && 4. * PI * a / (p * p) >= 0.5;
}

struct PlanarFace
{
    ExPolygons area;
    double     fall = 0.;
};
constexpr double FaceMinWidth = 4.;
constexpr double FaceMinArea  = 25.;
std::vector<PlanarFace> planar_faces(const HeightField &surface, const ExPolygons &area)
{
    const int          nx = surface.nx(), ny = surface.ny();
    const size_t       n  = size_t(nx) * ny;
    std::vector<Vec2d> grad(n, Vec2d(NAN, NAN));
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix)
            if (!std::isnan(surface.at(ix, iy)))
                grad[size_t(iy) * nx + ix] = surface.gradient(surface.node(ix, iy));
    const double            spread = std::tan(Geometry::deg2rad(1.)), rise = std::tan(Geometry::deg2rad(3.));
    std::vector<uint8_t>    seen(n, 0);
    std::vector<size_t>     queue;
    std::vector<PlanarFace> out;
    for (size_t seed = 0; seed < n; ++seed) {
        if (seen[seed] || !grad[seed].allFinite() || grad[seed].norm() < rise)
            continue;
        const Vec2d g0  = grad[seed];
        Vec2d       sum = Vec2d::Zero();
        queue.assign(1, seed);
        seen[seed] = 1;
        for (size_t q = 0; q < queue.size(); ++q) {
            const size_t i = queue[q];
            sum += grad[i];
            const int ix = int(i % size_t(nx)), iy = int(i / size_t(nx));
            static constexpr int steps[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto &d : steps) {
                const int jx = ix + d[0], jy = iy + d[1];
                if (jx < 0 || jy < 0 || jx >= nx || jy >= ny)
                    continue;
                const size_t j = size_t(jy) * nx + jx;
                if (!seen[j] && grad[j].allFinite() && (grad[j] - g0).norm() <= spread) {
                    seen[j] = 1;
                    queue.emplace_back(j);
                }
            }
        }
        if (double(queue.size()) * sqr(surface.pitch()) < FaceMinArea)
            continue;
        Mask mask(n, 0);
        for (const size_t i : queue)
            mask[i] = 1;
        ExPolygons face = intersection_ex(opening_ex(mask_to_expolygons(mask, surface), float(scale_(0.5 * FaceMinWidth))), area);
        double     a    = 0.;
        for (const ExPolygon &e : face)
            a += e.area() * SCALING_FACTOR * SCALING_FACTOR;
        if (a >= FaceMinArea)
            out.push_back({ std::move(face), std::atan2(sum.y(), sum.x()) });
    }
    return out;
}

constexpr double SparseWallSlope = 45.;

struct BandedFill
{
    bool       strips       = false;
    double     strip_height = 0.;
    double     strip_offset = 0.;
    ExPolygons lengthwise;
    const HeightField *layers = nullptr;
    int                sparse = 0;
    int                hidden = 0;
    double             density = 1.;
    double             angle = 0.;
    bool               bridge_first = false;
    const AdaptiveStack *adaptive = nullptr;
    double               outer_height = 0.;
    bool               walls = false;
    double             sparse_angle = std::numeric_limits<double>::quiet_NaN();
    Polygons voids;
};

ExPolygons hidden_layer_area(const HeightField &layers, int above, const ExPolygons &area)
{
    Mask mask(layers.data().size(), 0);
    for (size_t n = 0; n < mask.size(); ++n)
        mask[n] = !std::isnan(layers.data()[n]) && above < int(layers.data()[n]);
    const float r = float(scale_(layers.pitch()));
    ExPolygons  cells = offset_ex(offset_ex(offset_ex(mask_to_expolygons(mask, layers), r), -2.f * r), r);
    return intersection_ex(area, expolygons_simplify(cells, scale_(0.25 * layers.pitch())));
}

float nearest(const HeightField &field, const Vec2d &p)
{
    const Vec2d o  = (p - field.origin()) / field.pitch();
    const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
    return ix < 0 || iy < 0 || ix >= field.nx() || iy >= field.ny() ? HeightField::NoData : field.at(ix, iy);
}

ExtrusionEntityCollection make_shell(const PrintObject &object, const ConformalPatch &patch, const HeightField &top_of_planar,
                                     const ExPolygons &area, int k, const ShellFill &fill, const Point &start, const Point3 *from,
                                     double host_z, double resample, const Polyline &seam_line, const BandedFill *banded = nullptr)
{
    const PrintRegion        &region  = object.printing_region(size_t(patch.region_id));
    const bool                outer   = k == patch.shells;
    const bool                sparse  = banded != nullptr && k <= banded->sparse;
    const bool                plug    = banded != nullptr && !sparse && k <= banded->hidden;
    const AdaptiveStack      *adaptive = banded != nullptr ? banded->adaptive : nullptr;
    if (adaptive != nullptr && outer && banded->outer_height <= 0.) {
        ExtrusionEntityCollection out;
        ExPolygons                rest   = area;
        ExPolygons                rest_lengthwise = banded->lengthwise;
        const Flow                nominal = region.flow(object, frTopSolidInfill, patch.shell_height);
        const double              narrow  = std::min(double(nominal.width()), BandedTopWidth * nominal.nozzle_diameter());
        for (size_t i = 0; i < adaptive->levels.size() && !(rest.empty() && rest_lengthwise.empty()); ++i) {
            const bool       last = i + 1 == adaptive->levels.size();
            const double     w    = outer_bead_width(adaptive->levels[i], nominal.nozzle_diameter(), narrow);
            const ExPolygons zone = last ? rest : opening_ex(intersection_ex(rest, adaptive->zones[i]), float(scale_(0.5 * w)));
            const ExPolygons strips = last ? rest_lengthwise : intersection_ex(rest_lengthwise, adaptive->zones[i]);
            if (zone.empty() && strips.empty())
                continue;
            rest            = diff_ex(rest, zone);
            rest_lengthwise = diff_ex(rest_lengthwise, strips);
            BandedFill zone_fill   = *banded;
            zone_fill.outer_height = adaptive->levels[i];
            zone_fill.lengthwise   = strips;
            ExtrusionEntityCollection part = make_shell(object, patch, top_of_planar,
                                                        intersection_ex(offset_ex(zone, float(scale_(0.5 * w))), area), k, fill,
                                                        start, nullptr, host_z, resample, seam_line, &zone_fill);
            out.append(std::move(part.entities));
            part.entities.clear();
        }
        out.no_sort = true;
        return out;
    }
    const double              height  = adaptive != nullptr && outer ? banded->outer_height : patch.shell_height;
    Flow                      flow    = region.flow(object, outer ? frTopSolidInfill : sparse ? frInfill : frSolidInfill, height);
    if (banded != nullptr && outer) {
        const float narrow = std::min(flow.width(), float(BandedTopWidth * flow.nozzle_diameter()));
        flow = flow.with_width(adaptive != nullptr ? float(outer_bead_width(height, flow.nozzle_diameter(), narrow)) : narrow);
    }
    const bool                bridge  = plug && banded->bridge_first && k == banded->sparse + 1;
    const ExtrusionRole       role    = outer ? erTopSolidInfill : sparse ? erInternalInfill : bridge ? erInternalBridgeInfill : erSolidInfill;
    const double drop = adaptive != nullptr ? adaptive->nominal_drop(k) : (patch.shells - k) * patch.shell_height;
    const bool   surface_spaced = region.config().nonplanar_surface_spacing.value && !fill.spiral && !(banded != nullptr && banded->strips) &&
                                !sparse && !plug;
    std::vector<Polylines> linked;
    HelixRamp              ramp;
    WidthRoads             roads;
    ExPolygons whole = banded == nullptr || banded->lengthwise.empty() ? area : union_ex(area, banded->lengthwise);
    Polylines  wall;
    const Flow wall_flow = region.flow(object, frPerimeter, patch.shell_height);
    if (plug && banded->walls && banded->layers != nullptr) {
        const ExPolygons layer_area = hidden_layer_area(*banded->layers, patch.shells - k, whole);
        for (const ExPolygon &expoly : offset_ex(layer_area, -0.5f * float(scale_(wall_flow.spacing()))))
            for (const Polygon &poly : to_polygons(expoly))
                if (poly.size() >= 3) {
                    wall.emplace_back(poly.split_at_index(int(poly.closest_point_index(start))));
                    wall.back().points.emplace_back(wall.back().points.front());
                }
        const double     overlap = 0.01 * region.config().infill_wall_overlap.value * wall_flow.width();
        const ExPolygons strip   = diff_ex(layer_area, offset_ex(layer_area, -float(scale_(wall_flow.spacing() - overlap))));
        whole = diff_ex(whole, intersection_ex(strip, hidden_layer_area(*banded->layers, patch.shells - k + 1, whole)));
    }
    const auto off_voids = [&](const ExPolygons &fill_area, double spacing) {
        return banded == nullptr || banded->voids.empty() ? fill_area :
                                                            diff_ex(fill_area, offset(banded->voids, float(scale_(0.45 * spacing))));
    };
    ExPolygons rest = area;
    if (banded != nullptr && outer && !sparse && !plug && !fill.spiral && adaptive == nullptr)
        for (const PlanarFace &face : planar_faces(patch.surface, area)) {
            ShellFill down;
            down.angle   = face.fall;
            down.outline = false;
            append(linked, shell_polylines(off_voids(face.area, flow.spacing()), down, ipMonotonic, flow.spacing(), true, flow, host_z,
                                           start, MaxGlide, nullptr, &seam_line));
            rest = diff_ex(rest, face.area);
        }
    if (plug) {
        ShellFill cross;
        cross.angle   = banded->angle + ((banded->hidden - k) % 2 == 0 ? 0.5 * PI : 0.);
        cross.outline = false;
        linked        = shell_polylines(off_voids(whole, flow.spacing()), cross, ipRectilinear, flow.spacing(), false, flow, host_z, start, MaxGlide, nullptr, &seam_line);
    } else if (sparse) {
        const double spacing = flow.spacing() / banded->density;
        if (banded->strips)
            for (Polyline &pl : sparse_strip_fill(patch.surface, whole, drop, banded->strip_height, spacing, start))
                linked.push_back({ std::move(pl) });
        else {
            ShellFill lines = fill;
            if (!std::isnan(banded->sparse_angle))
                lines.angle = banded->sparse_angle;
            linked = shell_polylines(off_voids(whole, spacing), lines, ipRectilinear, spacing, false, flow, host_z, start, MaxGlide, nullptr, &seam_line);
        }
    } else if (banded != nullptr && banded->strips)
        contour_fill(patch.surface, outer ? expolygons_simplify(rest, scale_(0.5 * patch.surface.pitch())) : rest, drop,
                     banded->strip_height, banded->strip_offset, flow, height, fill.angle, outer, host_z, roads, linked);
    else if (!rest.empty())
        append(linked, shell_polylines(fill.spiral ? rest : off_voids(rest, flow.spacing()), fill, outer ? ipMonotonic : ipRectilinear, flow.spacing(), outer, flow,
                                       host_z, start, MaxGlide, surface_spaced ? &patch.surface.get() : nullptr, &seam_line, &ramp));
    if (banded != nullptr && !sparse && !plug)
        for (const ExPolygon &strip : banded->lengthwise)
            arachne_roads(strip, flow, height, 0, roads);
    std::vector<Polylines> pinholes;
    if (banded != nullptr && !sparse &&
        cover_has_gaps(offset_ex(whole, -float(scale_(0.5 * flow.spacing()))), linked, flow.width(), roads.polylines, roads.widths,
                       flow.spacing())) {
        Polygons cover = width_cover(roads);
        append(cover, linked_cover(linked, flow.width()));
        for (Polyline &pl : pinhole_roads(whole, std::move(cover), flow.width(), flow.spacing()))
            pinholes.push_back({ std::move(pl) });
    }

    const ExtrusionPath proto(role, flow.mm3_per_mm(), float(flow.width()), float(height));
    auto at = [&](const Vec2d &p, double &z, float &bead) {
        const float s = patch.surface.sample(p);
        if (std::isnan(s) || (banded != nullptr && (sparse ? spans_slope(patch.surface, p, SparseWallSlope) : spans_wall(patch.surface, p))))
            return false;
        z    = s - drop;
        bead = float(patch.shell_height);
        if (adaptive != nullptr) {
            double d, b;
            bool   deepest;
            if (!adaptive->layer(k, p, d, b, deepest))
                return false;
            z    = s - d;
            bead = float(b);
            if (deepest) {
                const float floor_z = banded->layers != nullptr ? nearest(top_of_planar, p) : top_of_planar.sample(p);
                if (std::isnan(floor_z))
                    return false;
                bead = float(z - floor_z);
            }
        } else if (banded != nullptr && banded->layers != nullptr) {
            const float layers = nearest(*banded->layers, p);
            const int   above  = patch.shells - k;
            if (std::isnan(layers) || above >= int(layers))
                return false;
            if (above == int(layers) - 1) {
                const float floor_z = nearest(top_of_planar, p);
                if (!std::isfinite(floor_z))
                    return false;
                bead = float(z - floor_z);
            }
        } else if (k == 1) {
            const float floor_z = top_of_planar.sample(p);
            if (!std::isfinite(floor_z))
                return false;
            bead = float(z - floor_z);
        }
        if (surface_spaced)
            bead = surface_spaced_bead(bead, flow.width(), patch.surface.gradient(p), fill.angle);
        return true;
    };
    ExtrusionEntityCollection out;
    if (!wall.empty()) {
        auto wall_at = [&](const Vec2d &p, double &z, float &bead) {
            const float layers = nearest(*banded->layers, p);
            return !std::isnan(layers) && int(layers) - 1 > patch.shells - k && at(p, z, bead);
        };
        std::vector<Polylines> rings;
        for (Polyline &pl : wall)
            rings.push_back({ std::move(pl) });
        lift_linked(rings, ExtrusionPath(erPerimeter, wall_flow.mm3_per_mm(), float(wall_flow.width()), float(patch.shell_height)), wall_at,
                    host_z, resample, patch.id, k - 1, out.entities);
    }
    lift_polylines(roads.polylines, proto, at, host_z, resample, patch.id, k - 1, out.entities, &roads.widths);
    const size_t helix_from = out.entities.size();
    lift_linked(linked, proto, at, host_z, resample, patch.id, k - 1, out.entities);
    if (out.entities.size() == helix_from + 1)
        ramp_helix_flow(*static_cast<ExtrusionPathConformal *>(out.entities.back()), ramp, fill.outward);
    else if (ramp.end > ramp.begin && !fill.outward) {
        double before = 0.;
        Point  last;
        for (size_t i = helix_from; i < out.entities.size(); ++i) {
            auto &piece = *static_cast<ExtrusionPathConformal *>(out.entities[i]);
            if (i > helix_from)
                before += unscale(Point(piece.first_point() - last)).norm();
            ramp_helix_flow(piece, HelixRamp{ ramp.begin - before, ramp.end - before }, false);
            before += unscale<double>(piece.polyline.to_polyline().length());
            last = piece.last_point();
        }
    }
    lift_linked(pinholes, proto, at, host_z, resample, patch.id, k - 1, out.entities);
    if (from != nullptr && !out.entities.empty())
        prepend_glide(*static_cast<ExtrusionPathConformal *>(out.entities.front()), *from,
                      offset_ex(area, float(scale_(0.25 * flow.spacing()))), at, host_z, resample, MaxGlide);
    out.no_sort = true;
    return out;
}

}

double low_plane_top_radius(const PrintObject &object, double layer_height)
{
    const double min_z = PrintRegionConfig::defaults().zaa_min_z.value;
    if (0.5 * layer_height <= min_z + EPSILON)
        return 0.;
    double radius = 0.;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegion       &region = object.printing_region(region_id);
        const PrintRegionConfig &cfg    = region.config();
        if (cfg.zaa_enabled)
            return 0.;
        if (!top_shells_enabled(cfg, object.config()) || effective_fallback(cfg, object.config()) != NonplanarFallback::None)
            continue;
        const double h_c = cfg.nonplanar_shell_layer_height.value > 0. ? cfg.nonplanar_shell_layer_height.value : automatic_shell_height(object);
        const Flow   top = region.flow(object, frTopSolidInfill, h_c);
        const double w   = std::min(double(top.width()), BandedCapWidth * top.nozzle_diameter());
        radius = std::max(radius, 0.5 * (0.5 * layer_height - min_z) * w / std::max(h_c, layer_height));
    }
    return radius;
}

double low_plane_rim(const PrintObject &object)
{
    double rim = 0.;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegion &region = object.printing_region(region_id);
        const double       h      = object.config().layer_height.value;
        rim = std::max(rim, std::max(0, region.config().wall_loops.value) * double(region.flow(object, frPerimeter, h).width()) +
                                double(region.flow(object, frTopSolidInfill, h).width()));
    }
    return rim;
}

HeightField sample_top_surface(const sla::IndexedMesh &mesh, double ray_z, double z_offset, const HeightField &like,
                               const std::function<void()> &throw_if_canceled,
                               const std::function<bool(const Vec2d &, double)> &solid_below)
{
    HeightField S = like;
    tbb::parallel_for(tbb::blocked_range<int>(0, S.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy) {
            if (throw_if_canceled)
                throw_if_canceled();
            for (int ix = 0; ix < S.nx(); ++ix) {
                const Vec2d p = S.node(ix, iy);
                S.at(ix, iy) = HeightField::NoData;
                for (const Vec2d &q : { p, Vec2d(p + Vec2d(1e-4, 1.7e-4)) }) {
                    const auto hit = mesh.query_ray_hit(Vec3d(q.x(), q.y(), ray_z), Vec3d(0., 0., -1.));
                    const double nz = hit.is_hit() ? hit.normal().normalized().z() : 0.;
                    if (nz > 0.05 || (nz < -0.05 && solid_below &&
                                      solid_below(p, hit.position().z() - mesh.ground_level() + z_offset))) {
                        const Vec3d n = hit.normal();
                        const double z = hit.position().z() + (n.x() * (q.x() - p.x()) + n.y() * (q.y() - p.y())) / n.z();
                        const float  s = float(z - mesh.ground_level() + z_offset);
                        if (!(S.at(ix, iy) >= s))
                            S.at(ix, iy) = s;
                    }
                }
            }
        }
    });
    return S;
}

HeightField sample_underside(const sla::IndexedMesh &mesh, double z_offset, const HeightField &S, const std::function<void()> &throw_if_canceled)
{
    HeightField U = S;
    tbb::parallel_for(tbb::blocked_range<int>(0, U.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy) {
            throw_if_canceled();
            for (int ix = 0; ix < U.nx(); ++ix) {
                const float s = S.at(ix, iy);
                if (std::isnan(s))
                    continue;
                const Vec2d p   = U.node(ix, iy);
                const double z = vertical_ray_hit(mesh, p, s - z_offset + mesh.ground_level() - 0.01, -1., true);
                U.at(ix, iy)    = float(std::isnan(z) ? z_offset : z - mesh.ground_level() + z_offset);
            }
        }
    });
    return U;
}

static ConformalResult build_region_shells(PrintObject &object, const std::vector<size_t> &region_ids, int patch_offset,
                                           std::shared_ptr<ObjectMesh> &mesh, const ExPolygons *whole_tops)
{
    const size_t    region_id = region_ids.front();
    ConformalResult result;
    result.mesh = mesh;
    const auto      t_start = std::chrono::steady_clock::now();
    LayerPtrs      &layers  = object.layers();
    result.planar_exclusion.assign(layers.size(), ExPolygons());

    const ShellSettings settings = shell_settings(object, region_id);
    if (settings.shells <= 0 || layers.empty())
        return result;

    ExPolygons candidate = candidate_top_area(object, region_ids);
    if (candidate.empty())
        return result;

    std::string        head_warning;
    const HeadEnvelope head = resolve_head_envelope(object.print()->config(), &head_warning);
    if (!head_warning.empty())
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: " << head_warning;

    const double      pitch = settings.pitch;
    Polygons          silhouette_src;
    const HeightField grid    = object_grid(object, pitch, &silhouette_src);
    const size_t      n_nodes = grid.data().size();

    auto cancel = [&object]() { throw_if_canceled(object); };
    HeightField S, U;
    ExPolygons  whole;
    {
        const ObjectMesh &mesh = object_mesh(object, result);
        S = sample_top_surface(mesh.imesh, mesh.mesh.bounding_box().max.z() + 1., object.slicing_parameters().object_print_z_min, grid, cancel,
                               material_below(object));
        if (settings.shells > settings.top_shells)
            U = sample_underside(mesh.imesh, object.slicing_parameters().object_print_z_min, S, cancel);
        if (const double noise = smooth_scan_noise(S, cancel); noise > 0.)
            BOOST_LOG_TRIVIAL(info) << "Non-planar: " << object.model_object()->name << " top surface noise " << noise << " mm";
    }
    {
        Mask smooth(n_nodes, 0);
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix) {
                const Vec2d  p = grid.node(ix, iy);
                const double h = 0.5 * pitch;
                smooth[size_t(iy) * grid.nx() + ix] = !std::isnan(S.at(ix, iy)) && !spans_wall(S, p + Vec2d(-h, -h)) &&
                                                      !spans_wall(S, p + Vec2d(h, -h)) && !spans_wall(S, p + Vec2d(-h, h)) &&
                                                      !spans_wall(S, p + Vec2d(h, h));
            }
        const float slit = float(scale_(0.5));
        candidate        = union_ex(candidate, intersection_ex(diff_ex(closing_ex(to_polygons(candidate), slit), candidate),
                                                                mask_to_expolygons(smooth, grid)));
        if (whole_tops != nullptr) {
            Polygons all = to_polygons(*whole_tops);
            polygons_append(all, to_polygons(candidate));
            whole = closing_ex(all, slit);
        }
    }
    const Mask        cand_mask = rasterize(candidate, grid);
    throw_if_canceled(object);

    const PrintRegion &region       = object.printing_region(size_t(settings.region_id));
    const double       line_width   = region.flow(object, frTopSolidInfill, settings.shell_height).width();
    const double       wall_width   = std::max(0, region.config().wall_loops.value) *
                                region.flow(object, frPerimeter, settings.shell_height).width();
    const ExPolygons   band_cand    = wall_width > 0. ?
                                          diff_ex(candidate, offset_ex(union_ex(silhouette_src), float(-scale_(wall_width)))) :
                                          ExPolygons();
    const Mask         band_mask    = rasterize(band_cand, grid);
    Polygons voids;
    for (const ExPolygon &island : candidate)
        for (const Polygon &hole : island.holes)
            if (const Point c = hole.centroid(); std::isnan(S.sample(unscaled(c))) && hole.contains(c))
                voids.emplace_back(hole).make_counter_clockwise();
    const double       h_c          = settings.shell_height;
    const double       h_min        = 0.5 * h_c;
    const double       h_max        = 0.8 * region.flow(object, settings.shells == 1 ? frTopSolidInfill : frSolidInfill, h_c).nozzle_diameter();
    if (h_c > h_max) {
        PatchDiagnostics diag;
        diag.fallback_area   = area(candidate) * SCALING_FACTOR * SCALING_FACTOR;
        diag.fallback_reason = "shell_height";
        result.diagnostics.emplace_back(std::move(diag));
        return result;
    }
    const double       first_top    = layers.front()->print_z;
    std::vector<double> layer_z(layers.size());
    for (size_t i = 0; i < layers.size(); ++i)
        layer_z[i] = layers[i]->print_z;

    HeightField S0 = S;
    for (size_t i = 0; i < n_nodes; ++i)
        if (!cand_mask[i])
            S0.data()[i] = HeightField::NoData;
    if (settings.max_slope > 0.)
        S0 = slope_cap_minorant(S0, settings.max_slope);
    Mask mask(n_nodes, 0);
    for (size_t i = 0; i < n_nodes; ++i)
        mask[i] = cand_mask[i] && !std::isnan(S0.data()[i]);
    enum DropReason : uint8_t { Kept, DropNearBed, DropSteep, DropSteepStrip, DropFlatGround, DropFlatSliver, DropNarrow };
    static constexpr const char *drop_reason_tag[] = { "outline", "near_bed", "steep", "steep_strip", "flat_ground", "flat_sliver", "narrow" };
    std::vector<uint8_t> dropped(n_nodes, Kept);
    auto drop = [&](size_t i, DropReason why) {
        mask[i]    = 0;
        dropped[i] = why;
    };
    const int min_shells = settings.shells > settings.top_shells ? 1 : settings.top_shells;
    if (settings.banded)
        for (size_t i = 0; i < n_nodes; ++i)
            if (mask[i] && S0.data()[i] - min_shells * h_c - h_min < first_top)
                drop(i, DropNearBed);
    if (settings.banded) {
        const double outer_w   = std::min(line_width, BandedCapWidth * region.flow(object, frTopSolidInfill, h_c).nozzle_diameter());
        const PrintConfig &print_config = object.print()->config();
        const double tilt      = print_config.nonplanar_kinematics.value == RotaryLayout::None ? 0. :
                                     std::clamp(Geometry::deg2rad(print_config.nonplanar_max_tilt.value), 0., 0.5 * M_PI);
        const double max_slope = h_c / outer_w;
        auto local_height = [&layers](double z) {
            const auto it = std::lower_bound(layers.begin(), layers.end(), z, [](const Layer *layer, double v) { return layer->print_z < v; });
            return it == layers.end() ? layers.back()->height : (*it)->height;
        };
        Mask                steep(n_nodes, 0);
        std::vector<double> slope(n_nodes, 0.);
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (const size_t i = size_t(iy) * grid.nx() + ix; mask[i])
                    if (const Vec2d g = S0.gradient(grid.node(ix, iy)); g.allFinite()) {
                        const double eff = tilt > 0. ? std::tan(std::max(0., std::atan(g.norm()) - tilt)) : g.norm();
                        if (eff > std::max(max_slope, local_height(S0.at(ix, iy)) / outer_w))
                            steep[i] = 1, slope[i] = eff;
                    }
        const Mask steep_wide = rasterize(opening_ex(mask_to_expolygons(steep, grid), float(scale_(line_width))), grid);
        for (size_t i = 0; i < n_nodes; ++i)
            if (steep[i] && steep_wide[i])
                drop(i, DropSteep);
        constexpr double FlatSlope  = 0.0175;
        constexpr double FlatGround = 2.;
        Mask             flat(n_nodes, 0);
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (const size_t i = size_t(iy) * grid.nx() + ix; mask[i])
                    if (const Vec2d g = S0.gradient(grid.node(ix, iy)); g.allFinite() && g.norm() < FlatSlope)
                        flat[i] = 1;
        const double reach = region.config().zaa_enabled ? region.config().zaa_min_z.value : PrintRegionConfig::defaults().zaa_min_z.value;
        ExPolygons   flat_area;
        for (ExPolygon &ground : opening_ex(mask_to_expolygons(flat, grid), float(scale_(FlatGround)))) {
            const Mask on = rasterize(ExPolygons{ ground }, grid);
            double     lo = DBL_MAX, hi = -DBL_MAX;
            size_t     sample = n_nodes;
            for (size_t i = 0; i < n_nodes; ++i)
                if (on[i] && flat[i]) {
                    lo = std::min(lo, double(S0.data()[i])), hi = std::max(hi, double(S0.data()[i]));
                    sample = i;
                }
            if (sample == n_nodes)
                continue;
            const Point at = Point::new_scale(grid.node(int(sample % size_t(grid.nx())), int(sample / size_t(grid.nx()))));
            const Layer *host = nullptr;
            for (auto it = layers.rbegin(); it != layers.rend() && host == nullptr; ++it) {
                const double plane = (*it)->print_z - 0.5 * (*it)->height;
                if (plane > hi + EPSILON)
                    continue;
                if (plane < lo - EPSILON ||
                    std::any_of((*it)->lslices.begin(), (*it)->lslices.end(), [&at](const ExPolygon &e) { return e.contains(at); }))
                    host = *it;
            }
            if (host != nullptr && hi - host->print_z <= reach + EPSILON && lo - host->print_z >= reach - host->height - EPSILON)
                flat_area.emplace_back(std::move(ground));
        }
        const Mask       flat_planar = rasterize(flat_area, grid);
        for (size_t i = 0; i < n_nodes; ++i)
            if (flat_planar[i] && flat[i])
                drop(i, DropFlatGround);
        {
            constexpr double     StripSlope = 1.213;
            std::vector<uint8_t> seen(n_nodes, 0);
            std::vector<size_t>  strip, stack;
            for (size_t seed = 0; seed < n_nodes; ++seed) {
                if (seen[seed] || !steep[seed] || !mask[seed])
                    continue;
                strip.clear();
                stack.assign(1, seed);
                seen[seed]   = 1;
                while (!stack.empty()) {
                    const size_t i = stack.back();
                    stack.pop_back();
                    strip.push_back(i);
                    const int ix = int(i % size_t(grid.nx())), iy = int(i / size_t(grid.nx()));
                    for (const auto &[dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } })
                        if (const int jx = ix + dx, jy = iy + dy; jx >= 0 && jy >= 0 && jx < grid.nx() && jy < grid.ny())
                            if (const size_t j = size_t(jy) * grid.nx() + jx; !seen[j] && steep[j] && mask[j])
                                seen[j] = 1, stack.push_back(j);
                }
                std::vector<double> slopes;
                for (const size_t i : strip)
                    slopes.push_back(slope[i]);
                auto p90 = slopes.begin() + ptrdiff_t(0.9 * double(slopes.size() - 1));
                std::nth_element(slopes.begin(), p90, slopes.end());
                if (*p90 > StripSlope)
                    for (const size_t i : strip)
                        drop(i, DropSteepStrip);
            }
        }
        if (!flat_area.empty()) {
            const Mask near_flat = rasterize(offset_ex(flat_area, float(scale_(3. * line_width))), grid);
            const Mask wide      = rasterize(opening_ex(mask_to_expolygons(mask, grid), float(scale_(1.5 * line_width))), grid);
            for (size_t i = 0; i < n_nodes; ++i)
                if (mask[i] && near_flat[i] && !wide[i])
                    drop(i, DropFlatSliver);
        }
    }
    std::vector<int> limiting_components(n_nodes, -1);
    if (!settings.banded)
        drop_unsafe_nodes(S0, head, h_c, settings.max_error, mask, &limiting_components);
    if (!settings.banded) {
        int                    n_comp = 0;
        const std::vector<int> label  = label_components(mask, grid.nx(), grid.ny(), n_comp);
        std::vector<int>       ix0(size_t(n_comp), grid.nx()), iy0(size_t(n_comp), grid.ny()), ix1(size_t(n_comp), -1), iy1(size_t(n_comp), -1);
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (const int c = label[size_t(iy) * grid.nx() + ix]; c >= 0) {
                    ix0[size_t(c)] = std::min(ix0[size_t(c)], ix), iy0[size_t(c)] = std::min(iy0[size_t(c)], iy);
                    ix1[size_t(c)] = std::max(ix1[size_t(c)], ix), iy1[size_t(c)] = std::max(iy1[size_t(c)], iy);
                }
        std::vector<std::vector<size_t>> low(static_cast<size_t>(n_comp));
        tbb::parallel_for(tbb::blocked_range<int>(0, n_comp), [&](const tbb::blocked_range<int> &range) {
            for (int c = range.begin(); c < range.end(); ++c) {
                const size_t k = size_t(c);
                HeightField  own = crop(S0, ix0[k], iy0[k], ix1[k], iy1[k]);
                for (int iy = 0; iy < own.ny(); ++iy)
                    for (int ix = 0; ix < own.nx(); ++ix)
                        if (label[size_t(iy0[k] + iy) * grid.nx() + ix0[k] + ix] != c)
                            own.at(ix, iy) = HeightField::NoData;
                const HeightField safe  = min_safe_tip_height(own, head, own, nullptr, true);
                const HeightField lower = head_lipschitz_minorant(own, head, 8, cancel);
                size_t            high  = 0;
                for (int iy = 0; iy < own.ny(); ++iy)
                    for (int ix = 0; ix < own.nx(); ++ix)
                        if (const float z = own.at(ix, iy); !std::isnan(z)) {
                            high += !(z - lower.at(ix, iy) <= settings.max_error);
                            if (z < safe.at(ix, iy) - settings.max_error)
                                low[k].emplace_back(size_t(iy0[k] + iy) * grid.nx() + ix0[k] + ix);
                        }
                if (low[k].size() >= high)
                    low[k].clear();
            }
        });
        for (const std::vector<size_t> &l : low)
            for (size_t i : l)
                mask[i] = 0;
    }

    HeightField cap, S1u;
    if (const RepairHints *hints = object.nonplanar_repair(); hints != nullptr && !hints->surface_caps.empty())
        cap = surface_cap_field(*hints, grid), S1u = grid;

    HeightField S1        = settings.banded ? S0 : grid;
    bool        converged = settings.banded;
    if (settings.banded && !cap.empty()) {
        S1u = S0;
        S1 = apply_surface_cap(S0, cap);
    }
    for (int round = 0; round < 16 && !converged; ++round) {
        int                    n_comp = 0;
        const std::vector<int> label  = label_components(mask, grid.nx(), grid.ny(), n_comp);
        struct Extent { int ix0, iy0, ix1, iy1; float zcap; };
        std::vector<Extent> ext(size_t(n_comp), Extent{ grid.nx(), grid.ny(), -1, -1, -Inf });
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (const int c = label[size_t(iy) * grid.nx() + ix]; c >= 0) {
                    Extent &e = ext[size_t(c)];
                    e.ix0 = std::min(e.ix0, ix), e.iy0 = std::min(e.iy0, iy), e.ix1 = std::max(e.ix1, ix), e.iy1 = std::max(e.iy1, iy);
                }
        auto component_field = [&](const HeightField &src, int c) {
            const Extent &e   = ext[size_t(c)];
            HeightField   out = crop(src, e.ix0, e.iy0, e.ix1, e.iy1);
            for (int iy = 0; iy < out.ny(); ++iy)
                for (int ix = 0; ix < out.nx(); ++ix)
                    if (label[size_t(e.iy0 + iy) * grid.nx() + e.ix0 + ix] != c)
                        out.at(ix, iy) = HeightField::NoData;
            return out;
        };
        std::fill(S1.data().begin(), S1.data().end(), HeightField::NoData);
        tbb::parallel_for(tbb::blocked_range<int>(0, n_comp), [&](const tbb::blocked_range<int> &range) {
            for (int c = range.begin(); c < range.end(); ++c) {
                const Extent     &e  = ext[size_t(c)];
                const HeightField s0 = component_field(S0, c);
                const HeightField s1 = head_lipschitz_minorant(cap.empty() ? s0 : apply_surface_cap(s0, component_field(cap, c)), head, 8, cancel);
                const HeightField s1u = cap.empty() ? HeightField() : head_lipschitz_minorant(s0, head, 8, cancel);
                for (int iy = 0; iy < s1.ny(); ++iy)
                    for (int ix = 0; ix < s1.nx(); ++ix)
                        if (label[size_t(e.iy0 + iy) * grid.nx() + e.ix0 + ix] == c) {
                            S1.at(e.ix0 + ix, e.iy0 + iy) = s1.at(ix, iy);
                            if (!cap.empty())
                                S1u.at(e.ix0 + ix, e.iy0 + iy) = s1u.at(ix, iy);
                        }
            }
        });
        bool changed = false;
        for (size_t i = 0; i < n_nodes; ++i) {
            if (!mask[i])
                continue;
            const float s1 = S1.data()[i];
            if (std::isnan(s1) || S.data()[i] - (cap.empty() ? s1 : S1u.data()[i]) > settings.max_error ||
                s1 - settings.shells * h_c - h_min < first_top ||
                planar_transition_layer(layer_z, s1 - (settings.shells - 1) * h_c, h_c, h_min, h_max, PatchKind::Top) < 0) {
                mask[i] = 0;
                changed = true;
            } else
                ext[size_t(label[i])].zcap = std::max(ext[size_t(label[i])].zcap, s1);
        }
        std::vector<std::vector<size_t>> drops(static_cast<size_t>(n_comp));
        auto host_print_z = [&layers](float z) {
            const auto it = std::lower_bound(layers.begin(), layers.end(), double(z) - EPSILON,
                                             [](const Layer *layer, double v) { return layer->print_z < v; });
            return float(it == layers.end() ? layers.back()->print_z : (*it)->print_z);
        };
        tbb::parallel_for(tbb::blocked_range<int>(0, n_comp), [&](const tbb::blocked_range<int> &range) {
            for (int c = range.begin(); c < range.end(); ++c) {
                const Extent &e = ext[size_t(c)];
                if (e.zcap == -Inf)
                    continue;
                const float host_z = host_print_z(e.zcap);
                const double lowest_shell = (settings.shells - 1) * h_c;
                const HeightField like = component_field(S1, c);
                float like_min = Inf;
                for (float z : like.data())
                    if (!std::isnan(z))
                        like_min = std::min(like_min, z);
                HeadEnvelope near_head = head;
                near_head.boxes.clear();
                bool all_x = false, all_y = false;
                double rx = 0., ry = 0.;
                for (const HeadBox &box : head.boxes)
                    if (box.min.z() - head.margin <= host_z - like_min + lowest_shell) {
                        const double lx = std::max(std::abs(box.min.x()), std::abs(box.max.x())) + head.lateral_margin;
                        const double ly = std::max(std::abs(box.min.y()), std::abs(box.max.y())) + head.lateral_margin;
                        box.infinite_x ? void(all_x = true) : void(rx = std::max(rx, lx));
                        box.infinite_y ? void(all_y = true) : void(ry = std::max(ry, ly));
                        near_head.boxes.emplace_back(box);
                    }
                if (near_head.boxes.empty())
                    continue;
                const int wnx = int(std::ceil(rx / pitch)) + 2, wny = int(std::ceil(ry / pitch)) + 2;
                const int wx0 = all_x ? 0 : std::max(0, e.ix0 - wnx), wx1 = all_x ? grid.nx() - 1 : std::min(grid.nx() - 1, e.ix1 + wnx);
                const int wy0 = all_y ? 0 : std::max(0, e.iy0 - wny), wy1 = all_y ? grid.ny() - 1 : std::min(grid.ny() - 1, e.iy1 + wny);
                HeightField H = crop(grid, wx0, wy0, wx1, wy1);
                for (int iy = wy0; iy <= wy1; ++iy)
                    for (int ix = wx0; ix <= wx1; ++ix)
                        if (const size_t i = size_t(iy) * grid.nx() + ix;
                            (label[i] != c || !mask[i] || band_mask[i]) && !std::isnan(S.data()[i]))
                            H.at(ix - wx0, iy - wy0) = std::min(S.data()[i], host_z);
                const HeightField z_safe = min_safe_tip_height(H, near_head, like, nullptr, true);
                for (int iy = 0; iy < like.ny(); ++iy)
                    for (int ix = 0; ix < like.nx(); ++ix) {
                        const size_t i = size_t(e.iy0 + iy) * grid.nx() + e.ix0 + ix;
                        if (label[i] == c && mask[i] && !band_mask[i] && like.at(ix, iy) - lowest_shell < z_safe.at(ix, iy))
                            drops[size_t(c)].emplace_back(i);
                    }
            }
        });
        for (const std::vector<size_t> &d : drops)
            for (size_t i : d) {
                mask[i] = 0;
                changed = true;
            }
        throw_if_canceled(object);
        converged = !changed;
    }
    if (!converged) {
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: head clipping did not converge, no curved shells for " << object.model_object()->name;
        std::fill(mask.begin(), mask.end(), 0);
    }

    ExPolygons components = opening_ex(mask_to_expolygons(mask, grid), float(scale_(0.5 * line_width)));
    if (settings.banded) {
        const Mask opened = rasterize(components, grid);
        for (size_t i = 0; i < n_nodes; ++i)
            if (mask[i] && !opened[i])
                dropped[i] = DropNarrow;
    }
    components            = settings.banded ? intersection_ex(components, candidate) :
                                              intersection_ex(offset_ex(expolygons_simplify(components, scale_(pitch)), -float(scale_(pitch))),
                                                              candidate);
    if (whole_tops != nullptr && settings.banded)
        if (const ExPolygons other = diff_ex(whole, candidate); !other.empty())
            components = union_ex(components, intersection_ex(intersection_ex(offset_ex(components, float(scale_(1.5 * pitch))), candidate),
                                                              offset_ex(other, float(scale_(1.5 * pitch)))));
    {
        std::vector<BoundingBox> island_bboxes;
        for (const ExPolygon &island : candidate)
            island_bboxes.emplace_back(get_extents(island.contour));
        ExPolygons kept;
        for (ExPolygon &c : components) {
            const double a = c.area() * SCALING_FACTOR * SCALING_FACTOR;
            bool         keep = settings.banded || a >= settings.min_area;
            for (size_t k = 0; !keep && a >= 1. && k < candidate.size(); ++k)
                if (island_bboxes[k].contains(c.contour.points.front()) && candidate[k].contains(c.contour.points.front()))
                    keep = a >= 0.25 * candidate[k].area() * SCALING_FACTOR * SCALING_FACTOR;
            if (keep)
                kept.emplace_back(std::move(c));
        }
        components = std::move(kept);
    }

    const RepairHints *repair = object.nonplanar_repair();
    if (!settings.banded && (settings.max_patch_height > 0. || (repair != nullptr && !repair->order_bands.empty()))) {
        ExPolygons bands;
        for (const ExPolygon &component : components) {
            double max_height = settings.max_patch_height > 0. ? settings.max_patch_height : Inf;
            if (repair != nullptr)
                for (const RepairHints::OrderBand &b : repair->order_bands)
                    if (component.contains(Point::new_scale(b.p.x(), b.p.y())))
                        max_height = std::min(max_height, b.height);
            const Mask pmask = rasterize({ component }, grid);
            float zlo = Inf, zhi = -Inf;
            for (size_t i = 0; i < n_nodes; ++i)
                if (pmask[i] && !std::isnan(S1.data()[i])) {
                    zlo = std::min(zlo, S1.data()[i]);
                    zhi = std::max(zhi, S1.data()[i]);
                }
            if (zhi - zlo <= max_height) {
                bands.emplace_back(component);
                continue;
            }
            const int count = int(std::ceil((zhi - zlo) / max_height));
            const double band_height = (zhi - zlo) / count;
            for (int band = 0; band < count; ++band) {
                Mask selected(n_nodes, 0);
                for (size_t i = 0; i < n_nodes; ++i)
                    if (pmask[i] && !std::isnan(S1.data()[i])) {
                        const int owner = std::min(count - 1, int((S1.data()[i] - zlo) / band_height));
                        selected[i] = owner == band;
                    }
                append(bands, intersection_ex(mask_to_expolygons(selected, grid), ExPolygons{ component }));
            }
        }
        components = std::move(bands);
        auto highest = [&](const ExPolygon &component) {
            const Mask pmask = rasterize({ component }, grid);
            float z = -Inf;
            for (size_t i = 0; i < n_nodes; ++i)
                if (pmask[i] && !std::isnan(S1.data()[i]))
                    z = std::max(z, S1.data()[i]);
            return z;
        };
        std::stable_sort(components.begin(), components.end(), [&](const ExPolygon &a, const ExPolygon &b) {
            return highest(a) < highest(b);
        });
    }

    std::vector<HeightField> planar_tops;
    std::vector<HeightField> stack_layers;
    std::vector<AdaptiveStack> adaptive_stacks;
    const double             nozzle       = region.flow(object, frTopSolidInfill, h_c).nozzle_diameter();
    std::vector<ExPolygons>  shell_areas;
    std::vector<std::pair<ExPolygon, const char *>> skipped;
    const int                solid_layers = settings.shells - settings.sparse;
    const double             top_thickness = std::max(region.config().top_shell_layers.value * object.config().layer_height.value,
                                                      region.config().top_shell_thickness.value);
    for (ExPolygon &component : components) {
        const Mask pmask = rasterize({ component }, grid);
        int        ix0 = grid.nx(), iy0 = grid.ny(), ix1 = -1, iy1 = -1;
        float      zlo = Inf, zhi = -Inf;
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (const size_t i = size_t(iy) * grid.nx() + ix; pmask[i] && !std::isnan(S1.data()[i])) {
                    ix0 = std::min(ix0, ix), iy0 = std::min(iy0, iy), ix1 = std::max(ix1, ix), iy1 = std::max(iy1, iy);
                    zlo = std::min(zlo, S1.data()[i]), zhi = std::max(zhi, S1.data()[i]);
                }
        if (ix1 < 0 || zhi - zlo < 0.01) {
            skipped.emplace_back(component, "flat");
            continue;
        }
        const int pad = 3;
        ix0 = std::max(0, ix0 - pad), iy0 = std::max(0, iy0 - pad);
        ix1 = std::min(grid.nx() - 1, ix1 + pad), iy1 = std::min(grid.ny() - 1, iy1 + pad);

        ConformalPatch patch;
        patch.id           = patch_offset + int(result.patches.size());
        patch.kind         = PatchKind::Top;
        patch.footprint    = { component };
        patch.wall_band    = intersection_ex(patch.footprint, band_cand);
        patch.shells       = settings.shells;
        patch.shell_height = h_c;
        patch.region_id    = settings.region_id;
        patch.surface      = crop(S1, ix0, iy0, ix1, iy1);
        for (int iy = 0; iy < patch.surface.ny(); ++iy)
            for (int ix = 0; ix < patch.surface.nx(); ++ix)
                if (!pmask[size_t(iy0 + iy) * grid.nx() + ix0 + ix])
                    patch.surface.edit().at(ix, iy) = HeightField::NoData;
        choose_patch_settings(object, head, settings.max_error, first_top, patch);
        if (settings.shells > settings.top_shells)
            patch.shells = settings.shells;
        int patch_solid = solid_layers;
        if (settings.shells > settings.top_shells) {
            patch_solid  = normal_solid_layers(top_thickness, h_c, slope_p90(patch.surface), solid_layers, settings.shells);
            patch.sparse = settings.shells - patch_solid;
        }

        PatchDiagnostics diag;
        diag.patch_id   = patch.id;
        double err_sq   = 0.;
        size_t err_n    = 0;
        HeightField top = patch.surface;
        patch.base      = patch.surface;
        patch.z_min     = Inf;
        patch.z_max     = -Inf;
        for (int iy = 0; iy < patch.surface.ny(); ++iy)
            for (int ix = 0; ix < patch.surface.nx(); ++ix) {
                const float s1 = patch.surface.at(ix, iy);
                if (std::isnan(s1))
                    continue;
                const double b = s1 - patch.shells * h_c;
                patch.base.edit().at(ix, iy) = float(b);
                patch.z_max    = std::max(patch.z_max, double(s1));
                const double e = S.at(ix0 + ix, iy0 + iy) - s1;
                if (!cap.empty())
                    if (const double lowered = S1u.at(ix0 + ix, iy0 + iy) - s1; lowered > 1e-4) {
                        ++diag.repaired_nodes;
                        diag.repair_lowering = std::max(diag.repair_lowering, lowered);
                    }
                diag.max_surface_error = std::max(diag.max_surface_error, e);
                err_sq += e * e;
                ++err_n;
                const Vec2d g = patch.surface.gradient(patch.surface.node(ix, iy));
                if (!std::isnan(g.x()) && !std::isnan(g.y()))
                    diag.max_slope_deg = std::max(diag.max_slope_deg, Geometry::rad2deg(std::atan(g.norm())));
            }
        diag.limiting_component = most_limiting_component(head, limiting_components, pmask);
        diag.rms_surface_error = err_n > 0 ? std::sqrt(err_sq / err_n) : 0.;
        pad_lowest(patch.surface.edit(), pad);
        pad_lowest(patch.base.edit(), pad);
        ExPolygons  shell_area = diff_ex(patch.footprint, patch.wall_band);
        Mask        stacks     = rasterize(offset_ex(shell_area, -float(scale_(line_width))), top);
        Mask        at_wall(stacks.size(), 0);
        {
            const int nx = top.nx(), ny = top.ny(), reach = int(std::ceil(line_width / top.pitch()));
            for (int iy = 0; iy + 1 < ny; ++iy)
                for (int ix = 0; ix + 1 < nx; ++ix)
                    if (spans_wall(S, top.node(ix, iy) + Vec2d(0.5 * top.pitch(), 0.5 * top.pitch()))) {
                        for (int jy = iy; jy <= iy + 1; ++jy)
                            for (int jx = ix; jx <= ix + 1; ++jx)
                                at_wall[size_t(jy) * nx + jx] = 1;
                        for (int jy = std::max(0, iy - reach); jy <= std::min(ny - 1, iy + 1 + reach); ++jy)
                            for (int jx = std::max(0, ix - reach); jx <= std::min(nx - 1, ix + 1 + reach); ++jx)
                                stacks[size_t(jy) * nx + jx] = 0;
                    }
        }
        bool        printable = true;
        HeightField count     = top;
        const int   n_top     = settings.shells > settings.top_shells ? settings.top_shells : patch.shells;
        std::optional<AdaptiveStack> stack;
        if (settings.adaptive)
            stack = adaptive_stack(top, HeightField(), n_top * h_c, h_c, patch.shells - n_top, nozzle,
                                   std::min(line_width, BandedTopWidth * nozzle), settings.max_error);
        if (stack && repair != nullptr)
            for (const RepairHints::Disc &d : repair->uniform_beads)
                for (size_t i = 0; i < stack->outer.data().size(); ++i)
                    if (!std::isnan(stack->outer.data()[i]) &&
                        (stack->outer.node(int(i % size_t(stack->outer.nx())), int(i / size_t(stack->outer.nx()))) - d.p).norm() <= d.radius)
                        stack->make_uniform(i);
        auto thick_layer_transition = [&](double face) {
            const auto above = std::partition_point(layer_z.begin(), layer_z.end(), [&](double z) { return face - z >= h_min; });
            return above == layer_z.begin() || above == layer_z.end() ? -1 : int(above - layer_z.begin()) - 1;
        };
        for (size_t i = 0; i < top.data().size(); ++i) {
            const float s = patch.surface.data()[i];
            if (std::isnan(s))
                continue;
            int layer = -1, m = stacks[i] ? patch.shells : n_top;
            for (; m >= (settings.shells > settings.top_shells ? std::min(n_top, min_shells) : n_top); --m) {
                if (m > n_top) {
                    const float u = U.at(ix0 + int(i % size_t(top.nx())), iy0 + int(i / size_t(top.nx())));
                    if (std::isnan(u) || s - m * h_c - h_min < u + (m > patch_solid ? settings.bottom_keep : h_c))
                        continue;
                }
                if (stack && m == n_top && !std::isnan(stack->outer.data()[i])) {
                    const double h0   = stack->outer.data()[i];
                    const int    n    = int(stack->count.data()[i]);
                    const double bead = n > 1 ? stack->inner_height(h0, n) : h0;
                    if ((layer = planar_transition_layer(layer_z, s - m * h_c + bead, bead, h_min, h_max, PatchKind::Top)) >= 0)
                        break;
                    stack->make_uniform(i);
                }
                if ((layer = planar_transition_layer(layer_z, s - (m - 1) * h_c, h_c, h_min, h_max, PatchKind::Top)) >= 0)
                    break;
            }
            if (layer < 0) {
                m     = n_top;
                layer = thick_layer_transition(s - (m - 1) * h_c);
            }
            if (layer < 0) {
                if (std::isnan(top.data()[i]))
                    continue;
                printable = false;
                break;
            }
            count.data()[i] = float(m);
            top.data()[i]   = float(layer_z[size_t(layer)]);
            patch.z_min = std::min(patch.z_min, layer_z[size_t(layer)]);
        }
        pad_lowest(count, pad);
        pad_lowest(top, pad);
        if (settings.banded)
            for (size_t i = 0; i < at_wall.size(); ++i)
                if (at_wall[i] && !std::isnan(top.data()[i]))
                    top.data()[i] = Inf;
        if (!printable) {
            skipped.emplace_back(component, "no_transition_layer");
            continue;
        }
        if (stack) {
            if (patch.shells > n_top)
                stack->layers = count;
            stack->update_zones();
            patch.shells = stack->shells();
            adaptive_stacks.emplace_back(std::move(*stack));
        }

        patch.host_layer = size_t(std::lower_bound(layer_z.begin(), layer_z.end(), patch.z_max - EPSILON) - layer_z.begin());
        if (patch.host_layer >= layers.size())
            patch.host_layer = layers.size() - 1;

        planar_tops.emplace_back(std::move(top));
        stack_layers.emplace_back(std::move(count));
        shell_areas.emplace_back(std::move(shell_area));
        result.diagnostics.emplace_back(std::move(diag));
        result.patches.emplace_back(std::move(patch));
    }

    for (const ExPolygon &cand : candidate) {
        double     covered = 0.;
        int        owner   = -1;
        const auto cand_bb = get_extents(cand);
        ExPolygons covering;
        for (const ConformalPatch &patch : result.patches)
            if (get_extents(patch.footprint).overlap(cand_bb))
                if (const double a = area(intersection_ex(patch.footprint, ExPolygons{ cand })); a > 0.) {
                    covered += a;
                    expolygons_append(covering, patch.footprint);
                    if (owner < 0)
                        owner = patch.id;
                }
        const double fallback = std::max(0., cand.area() - covered) * SCALING_FACTOR * SCALING_FACTOR;
        if (settings.banded && fallback > 0.) {
            ExPolygons uncovered{ cand };
            for (const ConformalPatch &patch : result.patches)
                if (get_extents(patch.footprint).overlap(cand_bb))
                    uncovered = diff_ex(uncovered, patch.footprint);
            for (const auto &[component, reason] : skipped)
                if (get_extents(component).overlap(cand_bb)) {
                    ExPolygons part = intersection_ex(ExPolygons{ component }, uncovered);
                    if (const double a = area(part) * SCALING_FACTOR * SCALING_FACTOR; a > 0.) {
                        PatchDiagnostics diag;
                        diag.fallback_area   = a;
                        diag.fallback_reason = reason;
                        if (fallback_hatched(reason))
                            record_fallback_shape(diag, std::move(part), S);
                        result.diagnostics.emplace_back(std::move(diag));
                    }
                    uncovered = diff_ex(uncovered, ExPolygons{ component });
                }
            const double            rest = area(uncovered) * SCALING_FACTOR * SCALING_FACTOR;
            std::array<double, std::size(drop_reason_tag)> by_reason{};
            const Mask              umask = rasterize(uncovered, grid);
            for (size_t i = 0; i < n_nodes; ++i)
                if (umask[i])
                    by_reason[dropped[i]] += pitch * pitch;
            const double nodes = std::accumulate(by_reason.begin() + 1, by_reason.end(), 0.);
            const double scale = nodes > rest ? rest / nodes : 1.;
            by_reason[Kept]    = std::max(0., rest - nodes * scale);
            for (size_t r = 0; r < by_reason.size(); ++r)
                if (by_reason[r] * (r == Kept ? 1. : scale) > 1e-3) {
                    PatchDiagnostics diag;
                    diag.fallback_area   = by_reason[r] * (r == Kept ? 1. : scale);
                    diag.fallback_reason = drop_reason_tag[r];
                    if (r != Kept && fallback_hatched(diag.fallback_reason)) {
                        Mask nodes_mask(n_nodes, 0);
                        for (size_t i = 0; i < n_nodes; ++i)
                            nodes_mask[i] = umask[i] && dropped[i] == r;
                        record_fallback_shape(diag, mask_to_expolygons(nodes_mask, grid), S);
                    }
                    result.diagnostics.emplace_back(std::move(diag));
                }
        } else if (owner >= 0) {
            PatchDiagnostics &diag = result.diagnostics[size_t(owner - patch_offset)];
            diag.fallback_area += fallback;
            diag.fallback_reason = "edge";
            if (fallback > 0.) {
                ExPolygons left = diff_ex(ExPolygons{ cand }, covering);
                expolygons_append(left, std::move(diag.fallback_shape));
                diag.fallback_hatch.clear();
                record_fallback_shape(diag, union_ex(left), S);
            }
        } else if (fallback > 0.) {
            PatchDiagnostics diag;
            diag.fallback_area = fallback;
            double best = 0.;
            diag.fallback_reason = settings.banded ? "edge" : "head";
            for (const auto &[component, reason] : skipped)
                if (const double a = area(intersection_ex(ExPolygons{ component }, ExPolygons{ cand })); a > best) {
                    best                 = a;
                    diag.fallback_reason = reason;
                }
            diag.limiting_component = most_limiting_component(head, limiting_components, rasterize({ cand }, grid));
            if (fallback_hatched(diag.fallback_reason))
                record_fallback_shape(diag, ExPolygons{ cand }, S);
            result.diagnostics.emplace_back(std::move(diag));
        }
    }
    if (result.patches.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Non-planar: no curved top patch in " << object.model_object()->name;
        return result;
    }

    const double material_max_patch_height = settings.banded ?
        multi_material_patch_height(object, head, settings.max_patch_height) : settings.max_patch_height;
    double band_max_patch_height = material_max_patch_height;
    if (settings.banded && repair != nullptr)
        for (const RepairHints::OrderBand &band : repair->order_bands)
            band_max_patch_height = std::min(band_max_patch_height > 0. ? band_max_patch_height : Inf, band.height);
    if (const double rotary = settings.banded ? rotary_band_reach(object, head, h_c) : 0.; rotary > 0.)
        band_max_patch_height = band_max_patch_height > 0. ? std::min(band_max_patch_height, rotary) : rotary;
    double band_pitch = 0., band_tolerance = 0.;
    if (settings.banded)
    {
        double thinnest = h_c;
        for (const AdaptiveStack &st : adaptive_stacks)
            thinnest = std::min(thinnest, st.min_height());
        band_schedule(head, thinnest, band_max_patch_height, band_pitch, band_tolerance);
    }

    const bool          strength = strength_roads_enabled(region.config(), object.config());
    const StrengthField strength_field = strength ? StrengthField(object) : StrengthField();

    const SeamPaint       paint = seam_paint(object);
    std::vector<Point>    seams(result.patches.size());
    std::vector<Polyline> seam_lines(result.patches.size());
    std::vector<ExPolygons> fill_areas(result.patches.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, result.patches.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t idx = range.begin(); idx < range.end(); ++idx) {
            ConformalPatch &patch = result.patches[idx];
            const double    host_z = settings.banded ? 0. : layers[patch.host_layer]->print_z;
            const double overlap = 0.01 * region.config().top_bottom_infill_wall_overlap.value * line_width;
            const ExPolygons area = intersection_ex(offset_ex(shell_areas[idx], float(scale_(overlap))), patch.footprint);
            std::vector<ShellFill> fills = plan_shell_fills(patch, region.config(), line_width);
            const bool helix = spiral_top_enabled(region.config(), object.config()) && helix_worthwhile(patch.surface, area);
            if (!fills.empty())
                fills.back().helix = fills.back().spiral && helix;
            std::vector<ShellFill> banded_fills = fills;
            BandedFill             banded;
            banded.voids        = voids;
            banded.strip_height = band_pitch;
            banded.strip_offset = 0.5 * band_tolerance;
            banded.adaptive     = settings.adaptive ? &adaptive_stacks[idx] : nullptr;
            const NonplanarTopPattern pattern    = top_pattern(patch, region.config());
            const bool                user_rings = settings.banded && pattern == NonplanarTopPattern::Concentric;
            if (settings.banded && (pattern == NonplanarTopPattern::Contour || user_rings)) {
                double angle = 0.;
                for (size_t k = 0; k < banded_fills.size(); ++k)
                    banded_fills[k] = ShellFill();
                if (!user_rings && contour_line_angle(patch.surface, angle, 0.9)) {
                    for (ShellFill &f : banded_fills) {
                        f.angle = angle;
                        f.outline = false;
                    }
                } else if (is_round(patch.footprint) && outline_is_contour(patch, h_c))
                    for (size_t k = 0; k < banded_fills.size(); ++k) {
                        banded_fills[k].spiral = true;
                        banded_fills[k].outward = (banded_fills.size() - 1 - k) % 2 == 1;
                        banded_fills[k].helix   = k + 1 == banded_fills.size() && helix;
                    }
                else {
                    banded.strips = true;
                    for (size_t k = 0; k < banded_fills.size(); ++k)
                        banded_fills[k].angle = fills[k].angle;
                }
                if (!banded_fills.front().spiral)
                    banded.lengthwise = lengthwise_strips(area, line_width);
            } else if (settings.banded) {
                for (ShellFill &f : banded_fills)
                    f.outline = false;
                banded.lengthwise = lengthwise_strips(area, line_width);
            }
            if (settings.shells > settings.top_shells) {
                banded.layers  = &stack_layers[idx];
                banded.sparse  = patch.sparse;
                patch.bridged_base  = patch.sparse > 0 && settings.sparse_density > 0. &&
                                     past_bridging_limit(region.flow(object, frInfill, h_c).spacing() / settings.sparse_density, h_c);
                banded.bridge_first = patch.bridged_base;
                banded.hidden  = settings.shells - settings.top_shells;
                banded.density = settings.sparse_density;
                const ShellFill &outermost = settings.banded ? banded_fills.back() : fills.back();
                banded.angle = outermost.spiral || banded.strips ? settings.solid_angle : outermost.angle;
                if (strength) {
                    banded.walls = true;
                    double     principal = 0.;
                    const ExPolygons hidden_area = hidden_layer_area(stack_layers[idx], settings.top_shells, area);
                    const bool directed = !hidden_area.empty() && strength_field.direction(patch.surface, hidden_area, principal);
                    if (!outermost.spiral && !banded.strips && banded.sparse > 0) {
                        double sparse = outermost.angle;
                        if (directed && std::abs(std::remainder(principal - sparse, PI)) > PI / 12. &&
                            strength_field.aligned_share(patch.surface, hidden_area, principal) >
                                strength_field.aligned_share(patch.surface, hidden_area, sparse) + 0.05 &&
                            level_enough(patch.surface, hidden_area, principal, sparse))
                            banded.sparse_angle = sparse = principal;
                        banded.angle = sparse + ((banded.hidden - banded.sparse - 1) % 2 == 0 ? 0. : 0.5 * PI);
                    } else if (directed)
                        banded.angle = principal;
                }
            }
            const ExPolygons fill_area = banded.lengthwise.empty() ? area : diff_ex(area, banded.lengthwise);
            const SeamPosition          seam_position = object.config().seam_position.value;
            patch.shell_paths.reserve(patch.shells);
            const Point                 seam          = shell_seam(area, patch.surface, seam_position, unsigned(patch.id), &paint);
            seams[idx]                                = seam;
            seam_lines[idx]                           = seam_line(patch.surface, seam, area);
            fill_areas[idx]                           = area;
            if (settings.banded) {
                std::vector<ExtrusionEntityCollection> shells(size_t(patch.shells));
                auto make_shells = [&](int up_to) {
                    tbb::parallel_for(tbb::blocked_range<int>(0, up_to, 1), [&](const tbb::blocked_range<int> &range) {
                        for (int k = range.begin() + 1; k <= range.end(); ++k) {
                            throw_if_canceled(object);
                            shells[size_t(k - 1)] = make_shell(object, patch, planar_tops[idx], fill_area, k, banded_fills[size_t(k - 1)], seam,
                                                               nullptr, host_z, pitch, seam_lines[idx], &banded);
                        }
                    });
                };
                make_shells(patch.shells);
                if (patch.sparse > 0 && banded.layers != nullptr && !settings.adaptive) {
                    const double reach = region.flow(object, frInfill, h_c).spacing() / settings.sparse_density;
                    const int    solid = patch.shells - patch.sparse;
                    HeightField &count = stack_layers[idx], &floor = planar_tops[idx];
                    size_t       lowered = 0;
                    for (const size_t i : unsupported_nodes(shells[size_t(patch.sparse - 1)], count, solid, reach)) {
                        const float s = patch.surface.data()[i];
                        if (std::isnan(s) || !std::isfinite(floor.data()[i]))
                            continue;
                        if (const int layer = planar_transition_layer(layer_z, s - (solid - 1) * h_c, h_c, h_min, h_max, PatchKind::Top); layer >= 0) {
                            count.data()[i] = float(solid);
                            floor.data()[i] = float(layer_z[size_t(layer)]);
                            ++lowered;
                        }
                    }
                    if (lowered > 0)
                        make_shells(patch.sparse + 1);
                }
                for (ExtrusionEntityCollection &shell : shells)
                    patch.shell_paths.emplace_back(std::move(shell));
            }
            std::optional<Point3>       from;
            for (int k = 1; k <= patch.shells && !settings.banded; ++k) {
                throw_if_canceled(object);
                ShellFill fill = fills[size_t(k - 1)];
                if (settings.banded)
                    fill = banded_fills[size_t(k - 1)];
                const Point start = fill.spiral || !from ? seam : from->to_point();
                patch.shell_paths.emplace_back(make_shell(object, patch, planar_tops[idx], fill_area, k, fill, start, from ? &*from : nullptr, host_z,
                                                          pitch, seam_lines[idx], settings.banded ? &banded : nullptr));
                const ExtrusionEntitiesPtr &entities = patch.shell_paths.back().entities;
                from.reset();
                if (!entities.empty() && !settings.banded)
                    from = static_cast<const ExtrusionPathConformal *>(entities.back())->polyline.points.back();
            }
            if (settings.banded && ironing_enabled(patch, region.config()))
                patch.ironing = make_conformal_ironing(object, patch, area, seam, seam_lines[idx], host_z, true, spans_wall);
        }
    });

    if (const TopColours colours = top_colours(object, candidate); colours.areas.size() > 1) {
        std::function<int(const Vec3d &)> colour;
        if (settings.banded && settings.shells > settings.top_shells) {
            const double hidden = (settings.top_shells - 0.5) * settings.shell_height;
            colour = [planar = planar_colour(object, colours, candidate, 0.2), &S1, &colours, hidden](const Vec3d &p) {
                const float s = S1.sample(p.head<2>());
                return !std::isnan(s) && s - p.z() > hidden ? planar(p) : colours.at(p.head<2>());
            };
        }
        split_by_colour(result, planar_tops, stack_layers, adaptive_stacks, shell_areas, fill_areas, seams, seam_lines, colours, colour);
        const int own = region.config().top_surface_filament_id.value;
        for (ConformalPatch &patch : result.patches) {
            if (object.printing_region(size_t(patch.region_id)).config().top_surface_filament_id.value == own)
                patch.region_id = int(region_id);
            patch.id += patch_offset;
            for (ExtrusionEntityCollection &shell : patch.shell_paths)
                for (ExtrusionEntity *entity : shell.entities)
                    static_cast<ExtrusionPathConformal *>(entity)->patch_id = patch.id;
        }
        for (PatchDiagnostics &diag : result.diagnostics)
            if (diag.patch_id >= 0)
                diag.patch_id += patch_offset;
        BOOST_LOG_TRIVIAL(info) << "Non-planar: " << colours.areas.size() << " top colours in " << object.model_object()->name;
    }

    std::vector<ConformalPatch> component_patches;
    std::vector<size_t>         patch_component;
    if (settings.banded) {
        BandSeams band_seams{ fill_areas, seams, seam_lines, line_width, MaxGlide, spans_wall,
                              settings.shells - settings.top_shells, &stack_layers };
        if (settings.adaptive)
            band_seams.drop = [&adaptive_stacks](size_t c, int k, const Vec2d &q, double &drop) {
                double bead;
                bool   deepest;
                return adaptive_stacks[c].layer(k + 1, q, drop, bead, deepest);
            };
        component_patches = split_into_bands(result, S, layer_z, band_pitch, band_tolerance, &band_seams,
                                             layer_changes_filament(object), patch_offset, &patch_component, event_cuts(object));
    }
    std::vector<ConformalPatch> &units = settings.banded ? component_patches : result.patches;

    if (region.config().nonplanar_flow_mod.value) {
        const double  nozzle = region.flow(object, frSolidInfill, h_c).nozzle_diameter();
        FlowModParams params;
        params.land_radius = head.tip_flat_radius;
        params.max_height  = FlowModMaxThickness * nozzle;
        params.smoothing   = FlowModSmoothing * nozzle;
        params.resample    = pitch;
        std::vector<std::vector<size_t>> groups(units.size());
        for (size_t i = 0; i < result.patches.size(); ++i)
            groups[settings.banded ? patch_component[i] : i].emplace_back(i);
        std::vector<FlowModStats> stats(groups.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, groups.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t c = range.begin(); c < range.end(); ++c) {
                if (groups[c].empty())
                    continue;
                throw_if_canceled(object);
                const HeightField &surface = result.patches[groups[c].front()].surface;
                std::optional<ShellDeficit> below;
                const int n_shells = settings.adaptive ? result.patches[groups[c].front()].shells : units[c].shells;
                for (int k = 0; k < n_shells; ++k) {
                    const bool   sparse = k < result.patches[groups[c].front()].sparse;
                    ShellDeficit laid(surface);
                    for (size_t idx : groups[c])
                        for (ExtrusionEntity *ee : result.patches[idx].shell_paths[size_t(k)].entities)
                            modulate_flow(*static_cast<ExtrusionPathConformal *>(ee), surface, below && !sparse ? &*below : nullptr,
                                          sparse ? nullptr : &laid, params, stats[c]);
                    if (sparse)
                        below.reset();
                    else
                        below.emplace(std::move(laid));
                }
            }
        });
        for (const FlowModStats &s : stats)
            result.flow_mod.merge(s);
        const FlowModStats &fm = result.flow_mod;
        BOOST_LOG_TRIVIAL(info) << "Non-planar flow: " << fm.points << " points, normal gap " << fm.min_gap << ".." << fm.max_gap
                                << " mm, laid " << fm.min_laid << ".." << fm.max_laid << " mm; clamped " << fm.max_clamped << " thick, "
                                << fm.min_clamped << " thin; " << fm.dragged << " dragged, " << fm.contact << " in tip contact";
    }

    for (const ConformalPatch &patch : result.patches)
        layers[patch.host_layer]->conformal_patches_after.emplace_back(patch.id);
    std::vector<char> printed(units.size(), !settings.banded);
    for (size_t c : patch_component)
        printed[c] = 1;
    for (size_t idx = 0; idx < units.size(); ++idx)
        if (!printed[idx]) {
            PatchDiagnostics diag;
            diag.fallback_area   = area(units[idx].footprint) * SCALING_FACTOR * SCALING_FACTOR;
            diag.fallback_reason = "no_pieces";
            result.diagnostics.emplace_back(std::move(diag));
        }
    for (size_t idx = 0; idx < units.size(); ++idx)
        if (printed[idx])
            for (size_t i = 0; i < layers.size(); ++i)
                if (layer_z[i] > units[idx].z_min - EPSILON && i <= units[idx].host_layer)
                    layers[i]->conformal_covered = true;
    std::vector<ExPolygons> clip_areas = shell_areas;
    if (settings.banded)
        for (ExPolygons &area : clip_areas)
            area = offset_ex(area, -float(scale_(0.5 * line_width)));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            throw_if_canceled(object);
            ExPolygons excl;
            for (size_t idx = 0; idx < units.size(); ++idx) {
                const ConformalPatch &patch = units[idx];
                if (!printed[idx] || i > patch.host_layer || layer_z[i] <= patch.z_min - EPSILON)
                    continue;
                const HeightField &B = planar_tops[idx];
                Mask               above(B.data().size(), 0);
                bool               any = false;
                const double bottom = layer_z[i] - layers[i]->height;
                for (size_t n = 0; n < above.size(); ++n)
                    if (!std::isnan(B.data()[n]) && layer_z[i] > B.data()[n] + EPSILON &&
                        !(bottom >= patch.surface.data()[n] - EPSILON)) {
                        above[n] = 1;
                        any      = true;
                    }
                if (any)
                    append(excl, intersection_ex(mask_to_expolygons(above, B), clip_areas[idx]));
            }
            if (excl.empty())
                continue;
            excl                         = union_ex(excl);
            const BoundingBox excl_bbox  = get_extents(excl);
            for (LayerRegion *layerm : layers[i]->regions()) {
                clip_extrusions(layerm->fills, excl, excl_bbox);
                clip_extrusions(layerm->perimeters, excl, excl_bbox);
            }
            result.planar_exclusion[i] = std::move(excl);
        }
    });
    {
        std::vector<ExPolygons> solid_cells(layers.size()), bridge_cells(layers.size());
        for (size_t idx = 0; idx < units.size(); ++idx)
            if (printed[idx])
                closing_cells(object, units[idx], planar_tops[idx], stack_layers[idx], shell_areas[idx],
                              units[idx].shells - units[idx].sparse, top_thickness, layer_z, solid_cells, bridge_cells);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
                if (!solid_cells[i].empty())
                    close_layer(object, i, union_ex(solid_cells[i]), union_ex(bridge_cells[i]));
        });
    }
    {
        std::vector<std::vector<const ConformalPatch *>> pieces(units.size());
        for (size_t i = 0; i < result.patches.size(); ++i)
            pieces[settings.banded ? patch_component[i] : i].push_back(&result.patches[i]);
        result.base.resize(units.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, units.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx)
                if (printed[idx])
                    result.base[idx] = measure_patch_base(object, units[idx], planar_tops[idx], stack_layers[idx], shell_areas[idx], pieces[idx],
                                                          units[idx].sparse, units[idx].shells - units[idx].sparse, units[idx].bridged_base, layer_z);
        });
        result.base.erase(std::remove_if(result.base.begin(), result.base.end(), [](const PatchBase &b) { return b.patches.empty(); }),
                          result.base.end());
    }

    BOOST_LOG_TRIVIAL(info) << "Non-planar: " << result.patches.size() << " curved top patch(es) in " << object.model_object()->name
                            << ", " << grid.nx() << "x" << grid.ny() << " nodes, "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() << " s";
    return result;
}

ConformalResult build_conformal_shells(PrintObject &object)
{
    ConformalResult result;
    result.planar_exclusion.resize(object.layers().size());
    for (Layer *layer : object.layers()) {
        layer->conformal_patches_after.clear();
        layer->conformal_covered = false;
    }
    std::vector<std::vector<size_t>> groups;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg  = object.printing_region(region_id).config();
        auto                     same = std::find_if(groups.begin(), groups.end(), [&](const std::vector<size_t> &group) {
            const t_config_option_keys diff = object.printing_region(group.front()).config().diff(cfg);
            return std::all_of(diff.begin(), diff.end(), [](const std::string &key) { return boost::starts_with(key, "fuzzy_skin"); });
        });
        if (same == groups.end())
            groups.push_back({ region_id });
        else
            same->push_back(region_id);
    }
    ExPolygons all_tops;
    if (groups.size() > 1) {
        std::vector<size_t> all(object.num_printing_regions());
        for (size_t r = 0; r < all.size(); ++r)
            all[r] = r;
        all_tops = candidate_top_area(object, all);
    }
    for (const std::vector<size_t> &group : groups) {
        ConformalResult region = build_region_shells(object, group, int(result.patches.size()), result.mesh,
                                                     groups.size() > 1 ? &all_tops : nullptr);
        result.mesh            = region.mesh;
        result.flow_mod.merge(region.flow_mod);
        append(result.base, std::move(region.base));
        for (ConformalPatch &patch : region.patches)
            result.patches.emplace_back(std::move(patch));
        for (PatchDiagnostics &diag : region.diagnostics)
            result.diagnostics.emplace_back(std::move(diag));
        for (size_t i = 0; i < result.planar_exclusion.size(); ++i)
            result.planar_exclusion[i] = union_ex(result.planar_exclusion[i], region.planar_exclusion[i]);
    }
    return result;
}

}
