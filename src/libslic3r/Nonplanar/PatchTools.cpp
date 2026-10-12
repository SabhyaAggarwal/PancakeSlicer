#include "PatchTools.hpp"

#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "Seams.hpp"
#include "ShellPaths.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <functional>
#include <string>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <typeinfo>
#include <utility>

namespace Slic3r::Nonplanar {

void throw_if_canceled(const PrintObject &object)
{
    if (object.print()->canceled())
        throw CanceledException();
}

bool requests_conformal_shells(const Print &print)
{
    for (const PrintObject *object : print.objects()) {
        if (object->shared_regions() == nullptr) {
            const auto &config = print.default_region_config();
            if (config.nonplanar_top_shells.value > 0 || config.nonplanar_bottom_shells.value > 0)
                return true;
            continue;
        }
        for (size_t r = 0; r < object->num_printing_regions(); ++r) {
            const auto &config = object->printing_region(r).config();
            if (config.nonplanar_top_shells.value > 0 || config.nonplanar_bottom_shells.value > 0)
                return true;
        }
    }
    return false;
}

bool sequential_shell_tower(const Print &print)
{
    return print.config().print_sequence == PrintSequence::ByObject &&
           requests_conformal_shells(print) && print.has_wipe_tower();
}

bool vase_with_conformal_roof(const PrintObject &object)
{
    if (!object.print()->config().spiral_mode)
        return false;
    for (size_t r = 0; r < object.num_printing_regions(); ++r)
        if (object.printing_region(r).config().nonplanar_top_shells.value > 0)
            return true;
    return false;
}

void prepare_vase_closure(PrintObject &object)
{
    if (!vase_with_conformal_roof(object))
        return;
    for (size_t r = 0; r < object.num_printing_regions(); ++r) {
        const auto &config = object.printing_region(r).config();
        if (config.nonplanar_top_shells.value <= 0)
            continue;
        size_t roof = object.layers().size();
        for (size_t i = 0; i < object.layers().size(); ++i)
            for (const Surface &surface : object.layers()[i]->get_region(int(r))->slices.surfaces)
                if (surface.is_top())
                    roof = std::min(roof, i);
        for (size_t i = roof; i < object.layers().size(); ++i)
            for (Surface &surface : object.layers()[i]->get_region(int(r))->fill_surfaces.surfaces) {
                surface.surface_type = i == roof ? stInternalBridge : stInternalSolid;
                if (i == roof)
                    surface.bridge_angle = Geometry::deg2rad(config.infill_direction.value);
            }
    }
}

double automatic_shell_height(const PrintObject &object)
{
    double height = std::numeric_limits<double>::infinity();
    for (const Layer *layer : object.layers())
        if (layer->height > 0.)
            height = std::min(height, layer->height);
    return std::isfinite(height) ? height : object.config().layer_height.value;
}

HeightField object_grid(const PrintObject &object, double pitch, Polygons *silhouette)
{
    BoundingBox obj_bbox;
    for (const Layer *layer : object.layers())
        for (const ExPolygon &expoly : layer->lslices) {
            obj_bbox.merge(get_extents(expoly.contour));
            if (silhouette != nullptr)
                silhouette->emplace_back(expoly.contour);
        }
    const BoundingBoxf bbf(unscale(obj_bbox.min), unscale(obj_bbox.max));
    const BoundingBoxf grid_bbox(Vec2d(std::floor(bbf.min.x() / pitch) - 1., std::floor(bbf.min.y() / pitch) - 1.) * pitch,
                                 Vec2d(std::ceil(bbf.max.x() / pitch) + 1., std::ceil(bbf.max.y() / pitch) + 1.) * pitch);
    return HeightField(grid_bbox, pitch);
}

Mask rasterize(const ExPolygons &area, const HeightField &grid)
{
    Mask                             mask(size_t(grid.nx()) * grid.ny(), 0);
    std::vector<std::vector<double>> crossings(grid.ny());
    const double                     p  = grid.pitch();
    const Vec2d                      o  = grid.origin();
    auto add_polygon = [&](const Polygon &poly) {
        const Points &pts = poly.points;
        for (size_t i = 0; i < pts.size(); ++i) {
            Vec2d a = unscale(pts[i]), b = unscale(pts[(i + 1) % pts.size()]);
            if (a.y() == b.y())
                continue;
            if (a.y() > b.y())
                std::swap(a, b);
            const int iy0 = std::max(0, int(std::ceil((a.y() - o.y()) / p)));
            const int iy1 = std::min(grid.ny() - 1, int(std::ceil((b.y() - o.y()) / p)) - 1);
            for (int iy = iy0; iy <= iy1; ++iy) {
                const double y = o.y() + iy * p;
                if (y < a.y() || y >= b.y())
                    continue;
                crossings[iy].emplace_back(a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
            }
        }
    };
    for (const ExPolygon &expoly : area) {
        add_polygon(expoly.contour);
        for (const Polygon &hole : expoly.holes)
            add_polygon(hole);
    }
    for (int iy = 0; iy < grid.ny(); ++iy) {
        std::vector<double> &xs = crossings[iy];
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            const int ix0 = std::max(0, int(std::ceil((xs[i] - o.x()) / p)));
            const int ix1 = std::min(grid.nx() - 1, int(std::floor((xs[i + 1] - o.x()) / p)));
            for (int ix = ix0; ix <= ix1; ++ix)
                mask[size_t(iy) * grid.nx() + ix] = 1;
        }
    }
    return mask;
}

ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid)
{
    Polygons     cells;
    const double h   = 0.5 * grid.pitch();
    const coord_t eps = 10;
    for (int iy = 0; iy < grid.ny(); ++iy)
        for (int ix = 0; ix < grid.nx();) {
            if (!mask[size_t(iy) * grid.nx() + ix]) {
                ++ix;
                continue;
            }
            int end = ix;
            while (end + 1 < grid.nx() && mask[size_t(iy) * grid.nx() + end + 1])
                ++end;
            const Vec2d lo = grid.node(ix, iy) - Vec2d(h, h), hi = grid.node(end, iy) + Vec2d(h, h);
            const Point plo = Point::new_scale(lo.x(), lo.y()) - Point(eps, eps);
            const Point phi = Point::new_scale(hi.x(), hi.y()) + Point(eps, eps);
            cells.emplace_back(Polygon({ plo, Point(phi.x(), plo.y()), phi, Point(plo.x(), phi.y()) }));
            ix = end + 1;
        }
    return union_ex(cells);
}

HeightField crop(const HeightField &src, int ix0, int iy0, int ix1, int iy1)
{
    HeightField out(BoundingBoxf(src.node(ix0, iy0), src.node(ix1, iy1)), src.pitch());
    for (int iy = 0; iy < out.ny(); ++iy)
        for (int ix = 0; ix < out.nx(); ++ix)
            out.at(ix, iy) = src.at(ix0 + ix, iy0 + iy);
    return out;
}

void pad_lowest(HeightField &field, int iterations)
{
    constexpr float Inf = std::numeric_limits<float>::infinity();
    for (int it = 0; it < iterations; ++it) {
        HeightField next = field;
        for (int iy = 0; iy < field.ny(); ++iy)
            for (int ix = 0; ix < field.nx(); ++ix) {
                if (!std::isnan(field.at(ix, iy)))
                    continue;
                float lowest = Inf;
                for (auto [dx, dy] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
                    const int x = ix + dx, y = iy + dy;
                    if (x >= 0 && y >= 0 && x < field.nx() && y < field.ny() && !std::isnan(field.at(x, y)))
                        lowest = std::min(lowest, field.at(x, y));
                }
                if (lowest < Inf)
                    next.at(ix, iy) = lowest;
            }
        field = std::move(next);
    }
}

std::vector<int> label_components(const Mask &mask, int nx, int ny, int &count)
{
    std::vector<int>    label(mask.size(), -1);
    std::vector<size_t> stack;
    count = 0;
    for (size_t seed = 0; seed < mask.size(); ++seed) {
        if (!mask[seed] || label[seed] >= 0)
            continue;
        label[seed] = count;
        stack.assign(1, seed);
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            const int ix = int(i % size_t(nx)), iy = int(i / size_t(nx));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = ix + dx, y = iy + dy;
                    if (x < 0 || y < 0 || x >= nx || y >= ny)
                        continue;
                    const size_t j = size_t(y) * nx + x;
                    if (mask[j] && label[j] < 0) {
                        label[j] = count;
                        stack.emplace_back(j);
                    }
                }
        }
        ++count;
    }
    return label;
}

void drop_steep_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, const HeightField &grid, Mask &mask)
{
    const int                 n_az   = 72;
    const std::vector<double> limits = head.slope_map(n_az, gap);
    Mask                      steep(mask.size(), 0);
    for (int iy = 0; iy < grid.ny(); ++iy)
        for (int ix = 0; ix < grid.nx(); ++ix) {
            const size_t i = size_t(iy) * grid.nx() + ix;
            if (!mask[i])
                continue;
            const Vec2d g = surface.gradient(grid.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y())) {
                steep[i] = 1;
                continue;
            }
            if (g.norm() < 1e-9)
                continue;
            const double phi = std::atan2(g.y(), g.x());
            const int    k   = (int(std::lround(phi / (2. * PI) * n_az)) % n_az + n_az) % n_az;
            steep[i]         = std::atan(g.norm()) > limits[size_t(k)];
        }
    for (size_t i = 0; i < mask.size(); ++i)
        if (steep[i])
            mask[i] = 0;
}

void drop_unsafe_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, double max_error,
                       Mask &mask, std::vector<int> *limiting_components)
{
    Mask plane_safe = mask;
    drop_steep_nodes(surface, head, gap, surface, plane_safe);
    const HeightField current_safe = min_safe_tip_height(surface, head, surface, limiting_components, true);
    HeightField previous = surface;
    for (float &z : previous.data())
        z -= float(gap);
    std::vector<int> substrate_components;
    const HeightField substrate_safe = min_safe_tip_height(previous, head, surface,
                                                           limiting_components ? &substrate_components : nullptr);
    for (size_t i = 0; i < mask.size(); ++i) {
        if (limiting_components && substrate_safe.data()[i] > current_safe.data()[i])
            (*limiting_components)[i] = substrate_components[i];
        if (mask[i] && !plane_safe[i] &&
            surface.data()[i] < std::max(current_safe.data()[i], substrate_safe.data()[i]) - max_error)
            mask[i] = 0;
    }
}

std::string most_limiting_component(const HeadEnvelope &head, const std::vector<int> &limiting_components,
                                    const Mask &area)
{
    std::vector<size_t> counts(head.boxes.size(), 0);
    for (size_t i = 0; i < area.size(); ++i)
        if (area[i] && limiting_components[i] >= 0)
            ++counts[size_t(limiting_components[i])];
    const auto most = std::max_element(counts.begin(), counts.end());
    return most != counts.end() && *most > 0 ? head.boxes[size_t(most - counts.begin())].name : std::string();
}

void clip_extrusions(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &area_bbox)
{
    ExtrusionEntitiesPtr out;
    out.reserve(collection.entities.size());
    bool any_cut = false;
    for (ExtrusionEntity *ee : collection.entities) {
        if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            clip_extrusions(*sub, area, area_bbox);
            if (sub->entities.empty())
                delete ee;
            else
                out.emplace_back(ee);
            continue;
        }
        const ExtrusionPaths *paths = nullptr;
        ExtrusionPaths        single;
        if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee))
            paths = &loop->paths;
        else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(ee))
            paths = &multi->paths;
        else if (auto *path = dynamic_cast<ExtrusionPath *>(ee); path != nullptr && typeid(*path) == typeid(ExtrusionPath)) {
            single.emplace_back(*path);
            paths = &single;
        }
        if (paths == nullptr || !get_extents(ee->as_polylines()).overlap(area_bbox)) {
            out.emplace_back(ee);
            continue;
        }
        bool                      cut = false;
        ExtrusionEntityCollection pieces;
        for (const ExtrusionPath &path : *paths) {
            const Polylines src { path.polyline.to_polyline() };
            if (intersection_pl(src, area).empty()) {
                pieces.entities.emplace_back(new ExtrusionPath(path));
                continue;
            }
            cut = true;
            for (Polyline &pl : diff_pl(src, area))
                if (pl.is_valid() && pl.length() >= scale_(2. * path.width))
                    pieces.entities.emplace_back(new ExtrusionPath(Polyline3(pl), path));
        }
        if (!cut) {
            out.emplace_back(ee);
            continue;
        }
        any_cut = true;
        delete ee;
        for (ExtrusionEntity *piece : pieces.entities)
            out.emplace_back(piece->clone_move());
    }
    collection.entities = std::move(out);
    if (any_cut)
        collection.no_sort = false;
}

int planar_transition_layer(const std::vector<double> &boundaries, double shell_face, double shell_height,
                            double h_min, double h_max, PatchKind kind)
{
    if (boundaries.empty())
        return -1;
    const bool top = kind == PatchKind::Top;
    const double target = shell_face + (top ? -1. : 1.) * (shell_height + h_min);
    int layer = top ? int(std::upper_bound(boundaries.begin(), boundaries.end(), target + EPSILON) - boundaries.begin()) - 1 :
                      int(std::lower_bound(boundaries.begin(), boundaries.end(), target - EPSILON) - boundaries.begin());
    layer = std::clamp(layer, 0, int(boundaries.size()) - 1);
    auto height = [&]() { return top ? shell_face - boundaries[size_t(layer)] : boundaries[size_t(layer)] - shell_face; };
    while (height() > h_max) {
        layer += top ? 1 : -1;
        if (layer < 0 || layer >= int(boundaries.size()))
            return -1;
    }
    return height() >= h_min ? layer : -1;
}

bool contour_line_angle(const HeightField &surface, double &angle, double min_coherence)
{
    double jxx = 0., jxy = 0., jyy = 0.;
    for (int iy = 0; iy < surface.ny(); ++iy)
        for (int ix = 0; ix < surface.nx(); ++ix) {
            const Vec2d g = surface.gradient(surface.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y()))
                continue;
            jxx += g.x() * g.x();
            jxy += g.x() * g.y();
            jyy += g.y() * g.y();
        }
    const double trace = jxx + jyy;
    const double diff  = std::hypot(jxx - jyy, 2. * jxy);
    if (trace < 1e-9 || diff < min_coherence * trace)
        return false;
    angle = 0.5 * std::atan2(2. * jxy, jxx - jyy) + 0.5 * PI;
    return true;
}

double top_shell_angle(const ConformalPatch &patch, const PrintRegionConfig &config)
{
    double angle = Geometry::deg2rad(config.solid_infill_direction.value);
    if (config.nonplanar_top_pattern.value == NonplanarTopPattern::Contour && !contour_line_angle(patch.surface, angle))
        angle = Geometry::deg2rad(config.solid_infill_direction.value);
    return angle;
}

Polylines fill_solid(const ExPolygons &area, InfillPattern pattern, double spacing, double angle, bool monotonic,
                     const Flow &flow, double z)
{
    return fill_sparse(area, pattern, spacing, 1., angle, monotonic, flow, z);
}

Polylines fill_sparse(const ExPolygons &area, InfillPattern pattern, double spacing, double density, double angle, bool monotonic,
                      const Flow &flow, double z)
{
    std::unique_ptr<Fill> fill(Fill::new_from_type(pattern));
    fill->layer_id        = 0;
    fill->z               = z;
    fill->spacing         = spacing;
    fill->overlap         = 0.;
    fill->angle           = float(angle);
    fill->fixed_angle     = true;
    fill->link_max_length = coord_t(scale_(3. * spacing));
    fill->bounding_box    = get_extents(area);
    FillParams params;
    params.density     = float(density);
    params.dont_adjust = false;
    params.monotonic   = monotonic;
    params.flow        = flow;

    Polylines out;
    Surface   surface(stTop, ExPolygon());
    for (const ExPolygon &expoly : area) {
        surface.expolygon = expoly;
        try {
            append(out, fill->fill_surface(&surface, params));
        } catch (InfillFailedException &) {
        }
    }
    return out;
}

void lift_polylines(const Polylines &polylines, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                    int patch_id, int shell, ExtrusionEntitiesPtr &out, const std::vector<std::vector<float>> *widths)
{
    for (size_t ipl = 0; ipl < polylines.size(); ++ipl) {
        const Polyline           &pl    = polylines[ipl];
        const std::vector<float> *width = widths != nullptr ? &(*widths)[ipl] : nullptr;
        Points3            pts;
        std::vector<float> heights, bead_widths;
        auto flush = [&]() {
            if (pts.size() >= 2) {
                auto *path = new ExtrusionPathConformal(Polyline3(pts), proto, std::move(heights));
                path->bead_width = std::move(bead_widths);
                path->patch_id   = patch_id;
                path->shell      = shell;
                path->simplify(0.01);
                out.emplace_back(path);
            }
            pts.clear();
            heights.clear();
            bead_widths.clear();
        };
        for (size_t i = 0; i + 1 < pl.points.size(); ++i) {
            const Vec2d a = unscale(pl.points[i]), b = unscale(pl.points[i + 1]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / resample)));
            for (int j = i == 0 ? 0 : 1; j <= n; ++j) {
                const Vec2d p = a + (b - a) * (double(j) / n);
                double      z    = 0.;
                float       bead = 0.f;
                if (!at(p, z, bead)) {
                    flush();
                    continue;
                }
                if (width != nullptr)
                    bead_widths.emplace_back((*width)[i] + float(double(j) / n) * ((*width)[i + 1] - (*width)[i]));
                pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(z - host_z)));
                heights.emplace_back(bead);
            }
        }
        flush();
    }
}

double head_band_height(const HeadEnvelope &head)
{
    double lowest = std::numeric_limits<double>::infinity();
    for (const HeadBox &box : head.boxes) {
        double floor_z = box.min.z();
        if (box.nozzle) {
            const double r = std::hypot(std::max(std::abs(box.min.x()), std::abs(box.max.x())),
                                        std::max(std::abs(box.min.y()), std::abs(box.max.y())));
            if (r <= head.tip_allowance.radius)
                floor_z = std::max(floor_z, head.tip_allowance.height);
            if (floor_z >= box.max.z())
                continue;
        } else
            floor_z -= head.margin;
        lowest = std::min(lowest, floor_z);
    }
    return lowest;
}

void cut_into_bands(const ExtrusionEntitiesPtr &paths, double pitch, double tolerance,
                    const std::function<void(int band, ExtrusionPathConformal *piece)> &sink,
                    const std::function<int(const Point3 &)> &cut_rank)
{
    auto band_of = [pitch](double z) { return int(std::floor(z / pitch)); };
    auto fits    = [&](double lo, double hi) { return hi <= (band_of(lo) + 1) * pitch + tolerance; };
    for (const ExtrusionEntity *ee : paths) {
        const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
        if (path == nullptr || path->polyline.points.size() < 2)
            continue;
        const Points3 &src = path->polyline.points;
        auto point = [](const Point3 &p) { return Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), unscale<double>(p.z())); };

        const bool         widths = !path->bead_width.empty();
        Points3            pts;
        std::vector<float> heights, bead_widths;
        double             lo = 0., hi = 0.;
        auto start = [&](const Vec3d &p, float h, float w) {
            pts.assign(1, Point3(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(p.z()))));
            heights.assign(1, h);
            bead_widths.assign(widths ? 1 : 0, w);
            lo = hi = p.z();
        };
        auto add = [&](const Vec3d &p, float h, float w) {
            pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(p.z())));
            heights.emplace_back(h);
            if (widths)
                bead_widths.emplace_back(w);
            lo = std::min(lo, p.z());
            hi = std::max(hi, p.z());
        };
        auto flush = [&]() {
            if (pts.size() >= 2) {
                auto *piece       = new ExtrusionPathConformal(Polyline3(pts), *path, std::vector<float>(heights));
                piece->bead_width = bead_widths;
                piece->patch_id   = path->patch_id;
                piece->shell      = path->shell;
                sink(band_of(lo), piece);
            }
        };

        auto width_at = [&](size_t i) { return widths ? path->bead_width[i] : 0.f; };
        start(point(src.front()), path->bead_height.front(), width_at(0));
        for (size_t i = 1; i < src.size(); ++i) {
            Vec3d       a  = point(pts.back());
            float       ha = heights.back();
            float       wa = widths ? bead_widths.back() : 0.f;
            const Vec3d b  = point(src[i]);
            const float hb = path->bead_height[i];
            const float wb = width_at(i);
            bool        snapped = false;
            for (int guard = 0; guard < 10000; ++guard) {
                if (fits(std::min(lo, b.z()), std::max(hi, b.z()))) {
                    add(b, hb, wb);
                    break;
                }
                if (cut_rank && !snapped) {
                    snapped          = true;
                    const int band   = band_of(lo);
                    double    tlo    = std::numeric_limits<double>::infinity(), thi = -tlo;
                    size_t    cut    = 0;
                    int       rank   = 0;
                    for (size_t j = pts.size() - 1; j > 0; --j) {
                        const double z = unscale<double>(pts[j].z());
                        tlo = std::min(tlo, z);
                        thi = std::max(thi, z);
                        if (band_of(tlo) == band)
                            break;
                        if (const int r = cut_rank(pts[j]); r > rank && fits(tlo, thi)) {
                            rank = r;
                            cut  = j;
                        }
                    }
                    if (cut > 0) {
                        Points3            tail(pts.begin() + cut, pts.end());
                        std::vector<float> tail_h(heights.begin() + cut, heights.end());
                        std::vector<float> tail_w(widths ? bead_widths.begin() + cut : bead_widths.end(), bead_widths.end());
                        pts.resize(cut + 1);
                        heights.resize(cut + 1);
                        if (widths)
                            bead_widths.resize(cut + 1);
                        flush();
                        pts         = std::move(tail);
                        heights     = std::move(tail_h);
                        bead_widths = std::move(tail_w);
                        lo = std::numeric_limits<double>::infinity();
                        hi = -lo;
                        for (const Point3 &q : pts) {
                            lo = std::min(lo, unscale<double>(q.z()));
                            hi = std::max(hi, unscale<double>(q.z()));
                        }
                        a  = point(pts.back());
                        ha = heights.back();
                        wa = widths ? bead_widths.back() : 0.f;
                        continue;
                    }
                }
                double t0 = 0., t1 = 1.;
                for (int it = 0; it < 40; ++it) {
                    const double t = 0.5 * (t0 + t1);
                    const double z = a.z() + t * (b.z() - a.z());
                    (fits(std::min(lo, z), std::max(hi, z)) ? t0 : t1) = t;
                }
                const Vec3d cut = a + t0 * (b - a);
                const float hc  = ha + float(t0) * (hb - ha);
                const float wc  = wa + float(t0) * (wb - wa);
                if (t0 > 0.)
                    add(cut, hc, wc);
                flush();
                start(cut, hc, wc);
                a  = cut;
                ha = hc;
                wa = wc;
            }
        }
        flush();
    }
}

void cut_by_colour(const ExtrusionEntitiesPtr &paths, const std::function<int(const Vec3d &)> &colour, double pullback,
                   double min_length, const std::function<void(int colour, ExtrusionPathConformal *piece)> &sink)
{
    struct Sample { Vec3d p; float h; float w; };
    auto point = [](const Point3 &p) { return Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), unscale<double>(p.z())); };
    auto trim = [](std::vector<Sample> &run, double length, bool front) {
        if (front)
            std::reverse(run.begin(), run.end());
        while (length > 0. && run.size() >= 2) {
            Sample      &last = run.back();
            const Sample &prev = run[run.size() - 2];
            const double  l    = (last.p - prev.p).head<2>().norm();
            if (l > length) {
                const double t = length / l;
                last.p = last.p + t * (prev.p - last.p);
                last.h = last.h + float(t) * (prev.h - last.h);
                last.w = last.w + float(t) * (prev.w - last.w);
                break;
            }
            length -= l;
            run.pop_back();
        }
        if (front)
            std::reverse(run.begin(), run.end());
    };
    for (const ExtrusionEntity *ee : paths) {
        const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
        if (path == nullptr || path->polyline.points.size() < 2)
            continue;
        const Points3       &src = path->polyline.points;
        const bool           widths = path->bead_width.size() == src.size();
        auto width_at = [&](size_t i) { return widths ? path->bead_width[i] : 0.f; };
        std::vector<Sample>  run{ { point(src.front()), path->bead_height.front(), width_at(0) } };
        int                  run_colour = colour(run.front().p);
        bool                 cut_front  = false;
        auto flush = [&](bool cut_back) {
            if (cut_back)
                trim(run, pullback, false);
            if (cut_front)
                trim(run, pullback, true);
            double length = 0.;
            for (size_t i = 1; i < run.size(); ++i)
                length += (run[i].p - run[i - 1].p).head<2>().norm();
            if (run.size() >= 2 && length >= min_length) {
                Points3            pts;
                std::vector<float> heights, bead_widths;
                for (const Sample &s : run) {
                    pts.emplace_back(coord_t(scale_(s.p.x())), coord_t(scale_(s.p.y())), coord_t(scale_(s.p.z())));
                    heights.emplace_back(s.h);
                    if (widths)
                        bead_widths.emplace_back(s.w);
                }
                auto *piece       = new ExtrusionPathConformal(Polyline3(std::move(pts)), *path, std::move(heights));
                piece->bead_width = std::move(bead_widths);
                piece->patch_id = path->patch_id;
                piece->shell    = path->shell;
                sink(run_colour, piece);
            }
        };
        for (size_t i = 1; i < src.size(); ++i) {
            const Sample b{ point(src[i]), path->bead_height[i], width_at(i) };
            const int    cb = colour(b.p);
            if (cb != run_colour) {
                const Sample a = run.back();
                double       t0 = 0., t1 = 1.;
                for (int it = 0; it < 24; ++it) {
                    const double t = 0.5 * (t0 + t1);
                    (colour(a.p + t * (b.p - a.p)) == run_colour ? t0 : t1) = t;
                }
                const Sample cut{ a.p + t0 * (b.p - a.p), a.h + float(t0) * (b.h - a.h), a.w + float(t0) * (b.w - a.w) };
                run.push_back(cut);
                flush(true);
                run.assign(1, cut);
                run_colour = cb;
                cut_front  = true;
            }
            run.push_back(b);
        }
        flush(false);
    }
}

namespace {
constexpr double MinBandReach = 0.05;
constexpr float  Inf          = std::numeric_limits<float>::infinity();
}

namespace {
constexpr double WallSlope = 60.;

}

bool spans_wall(const HeightField &surface, const Vec2d &p)
{
    const Vec2d o  = (p - surface.origin()) / surface.pitch();
    const int   ix = std::clamp(int(std::floor(o.x())), 0, surface.nx() - 2);
    const int   iy = std::clamp(int(std::floor(o.y())), 0, surface.ny() - 2);
    const float z[4] = { surface.at(ix, iy), surface.at(ix + 1, iy), surface.at(ix, iy + 1), surface.at(ix + 1, iy + 1) };
    static const double rise = std::tan(Geometry::deg2rad(WallSlope)) * std::sqrt(2.);
    return *std::max_element(z, z + 4) - *std::min_element(z, z + 4) > rise * surface.pitch();
}

void band_schedule(const HeadEnvelope &head, double shell_height, double max_patch_height, double &pitch, double &tolerance)
{
    const double reach = head_band_height(head);
    const double cap        = max_patch_height > 0. ? std::min(reach, max_patch_height) : reach;
    const double band_reach = std::max(cap, MinBandReach);
    if (cap < MinBandReach)
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: head " << head.id << " leaves " << cap
                                   << " mm of clearance above the tip; Z bands use " << MinBandReach << " mm";
    tolerance = std::min(0.5 * band_reach, 0.45 * shell_height);
    pitch     = band_reach - tolerance;
}

namespace {
constexpr double ColourPitch = 0.05;
constexpr double ColourMinWidth = 0.4;
}

TopColours top_colours(const PrintObject &object, const ExPolygons &candidate)
{
    TopColours            out;
    std::map<int, size_t> index;
    std::vector<Polygons> tops;
    Polygons              covered;
    for (size_t i = object.layer_count(); i-- > 0;) {
        Polygons layer_tops;
        for (const LayerRegion *layerm : object.get_layer(int(i))->regions()) {
            const PrintRegionConfig &cfg = layerm->region().config();
            Polygons top;
            for (const Surface &surface : layerm->slices.surfaces)
                if (surface.surface_type == stTop)
                    polygons_append(top, to_polygons(surface.expolygon));
            if (top.empty())
                continue;
            const auto [entry, inserted] = index.try_emplace(cfg.top_surface_filament_id.value, out.region_ids.size());
            if (inserted) {
                out.region_ids.emplace_back(layerm->region().print_object_region_id());
                out.filaments.emplace_back(cfg.top_surface_filament_id.value);
                tops.emplace_back();
            }
            polygons_append(tops[entry->second], covered.empty() ? top : diff(top, covered));
            polygons_append(layer_tops, std::move(top));
        }
        if (!layer_tops.empty())
            covered = union_(covered, layer_tops);
    }
    if (tops.size() < 2)
        return out;
    const float seam = float(scale_(0.01));
    ExPolygons  assigned;
    for (const Polygons &top : tops) {
        out.areas.emplace_back(diff_ex(intersection_ex(candidate, offset(top, seam)), assigned));
        append(assigned, out.areas.back());
    }
    const float half_road = float(scale_(0.5 * ColourMinWidth));
    ExPolygons  slivers;
    for (ExPolygons &area : out.areas) {
        ExPolygons kept = opening_ex(area, half_road);
        append(slivers, diff_ex(area, kept));
        area = std::move(kept);
    }
    for (const ExPolygon &sliver : union_ex(slivers)) {
        const ExPolygons around = offset_ex(sliver, 2.f * half_road);
        size_t           best = 0;
        double           most = -1.;
        for (size_t c = 0; c < out.areas.size(); ++c)
            if (const double a = area(intersection_ex(around, out.areas[c])); a > most) {
                most = a;
                best = c;
            }
        out.areas[best] = union_ex(out.areas[best], ExPolygons{ sliver });
    }
    BoundingBoxf bbox;
    for (const ExPolygon &expoly : candidate)
        for (const Point &pt : expoly.contour.points)
            bbox.merge(unscale(pt));
    bbox.min -= Vec2d(1., 1.);
    bbox.max += Vec2d(1., 1.);
    out.lookup = HeightField(bbox, ColourPitch);
    for (size_t c = 0; c < out.areas.size(); ++c) {
        const Mask mask = rasterize(out.areas[c], out.lookup);
        for (size_t n = 0; n < mask.size(); ++n)
            if (mask[n])
                out.lookup.data()[n] = float(c);
    }
    const int nx = out.lookup.nx(), ny = out.lookup.ny();
    for (int pass = 0; pass < int(1. / ColourPitch) + 2; ++pass) {
        std::vector<float> next = out.lookup.data();
        bool               grew = false;
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix)
                if (std::isnan(out.lookup.at(ix, iy)))
                    for (const auto &[dx, dy] : { std::pair(1, 0), std::pair(-1, 0), std::pair(0, 1), std::pair(0, -1) })
                        if (ix + dx >= 0 && ix + dx < nx && iy + dy >= 0 && iy + dy < ny && !std::isnan(out.lookup.at(ix + dx, iy + dy))) {
                            next[size_t(iy) * nx + ix] = out.lookup.at(ix + dx, iy + dy);
                            grew                       = true;
                            break;
                        }
        out.lookup.data() = std::move(next);
        if (!grew)
            break;
    }
    return out;
}

std::vector<std::pair<size_t, size_t>> split_by_colour(ConformalResult &result, const TopColours &colours,
                                                       const std::function<int(const Vec3d &)> &colour)
{
    std::function<int(const Vec3d &)> colour_at = colour;
    if (!colour_at)
        colour_at = [&colours](const Vec3d &p) { return colours.at(p.head<2>()); };
    std::vector<std::pair<size_t, size_t>> sources;
    std::vector<ConformalPatch>   patches;
    std::vector<PatchDiagnostics> diagnostics;
    for (size_t idx = 0; idx < result.patches.size(); ++idx) {
        ConformalPatch                      &comp = result.patches[idx];
        std::vector<std::optional<size_t>>   piece(colours.areas.size());
        auto patch_of = [&](int c) -> ConformalPatch & {
            if (!piece[size_t(c)]) {
                piece[size_t(c)] = patches.size();
                ConformalPatch &p = patches.emplace_back();
                p.kind         = comp.kind;
                p.footprint    = intersection_ex(comp.footprint, colours.areas[size_t(c)]);
                p.wall_band    = intersection_ex(comp.wall_band, colours.areas[size_t(c)]);
                p.surface      = comp.surface;
                p.base         = comp.base;
                p.shells       = comp.shells;
                p.shell_height = comp.shell_height;
                p.z_min        = comp.z_min;
                p.z_max        = comp.z_max;
                p.host_layer   = comp.host_layer;
                p.region_id    = colours.region_ids[size_t(c)];
                p.band         = comp.band;
                p.shell_paths.assign(comp.shell_paths.size(), ExtrusionEntityCollection());
                for (ExtrusionEntityCollection &shell : p.shell_paths)
                    shell.no_sort = true;
                diagnostics.emplace_back(result.diagnostics[idx]);
                sources.emplace_back(idx, size_t(c));
            }
            return patches[*piece[size_t(c)]];
        };
        for (size_t k = 0; k < comp.shell_paths.size(); ++k) {
            const ExtrusionEntitiesPtr &entities = comp.shell_paths[k].entities;
            if (entities.empty())
                continue;
            const double width = static_cast<const ExtrusionPath *>(entities.front())->width;
            cut_by_colour(entities, colour_at, 0.25 * width, width,
                          [&](int c, ExtrusionPathConformal *path) { patch_of(c).shell_paths[k].entities.emplace_back(path); });
        }
    }
    for (size_t i = 0; i < patches.size(); ++i) {
        patches[i].id          = int(i);
        diagnostics[i].patch_id = int(i);
        for (ExtrusionEntityCollection &shell : patches[i].shell_paths)
            for (ExtrusionEntity *ee : shell.entities)
                static_cast<ExtrusionPathConformal *>(ee)->patch_id = int(i);
    }
    for (PatchDiagnostics &diag : result.diagnostics)
        if (diag.patch_id < 0)
            diagnostics.emplace_back(std::move(diag));
    result.patches     = std::move(patches);
    result.diagnostics = std::move(diagnostics);
    return sources;
}

void group_hosts_by_filament(PrintObject &object, ConformalResult &result)
{
    if (result.patches.empty() || !prints_several_filaments(object))
        return;
    LayerPtrs   &layers = object.layers();
    const size_t n      = layers.size();
    double       reach  = head_band_height(resolve_head_envelope(object.print()->config()));
    if (const double cap = object.config().nonplanar_max_patch_height.value; cap > 0.)
        reach = std::min(reach, cap);
    if (!(reach > 0.) || !std::isfinite(reach) || n == 0)
        return;
    std::vector<double> layer_z(n);
    for (size_t i = 0; i < n; ++i)
        layer_z[i] = layers[i]->print_z;

    std::vector<std::set<int>> planar(n);
    for (size_t i = 0; i < n; ++i)
        for (const LayerRegion *layerm : layers[i]->regions()) {
            const PrintRegionConfig &cfg = layerm->region().config();
            if (!layerm->perimeters.empty())
                planar[i].insert({ cfg.outer_wall_filament_id.value, cfg.inner_wall_filament_id.value });
            if (!layerm->fills.empty())
                planar[i].insert({ cfg.sparse_infill_filament_id.value, cfg.internal_solid_filament_id.value,
                                   cfg.top_surface_filament_id.value, cfg.bottom_surface_filament_id.value });
        }
    struct Item
    {
        int         filament;
        BoundingBox bbox;
        bool        movable;
    };
    std::vector<Item> items(result.patches.size());
    for (size_t i = 0; i < result.patches.size(); ++i) {
        const ConformalPatch &p = result.patches[i];
        items[i].filament       = patch_has_extrusions(p) ? patch_filament_id(object, p) : 0;
        items[i].bbox           = get_extents(p.footprint);
        items[i].bbox.offset(scale_(1.));
        items[i].movable        = items[i].filament > 0 && p.band >= 0 && (p.kind == PatchKind::Top || p.kind == PatchKind::Volume);
    }
    auto has_filament = [&](size_t layer, int filament, size_t except) {
        if (planar[layer].count(filament))
            return true;
        for (size_t q = 0; q < result.patches.size(); ++q)
            if (q != except && items[q].filament == filament && result.patches[q].host_layer == layer)
                return true;
        return false;
    };
    auto allowed = [&](size_t p, size_t h) {
        const ConformalPatch &P = result.patches[p];
        for (size_t q = 0; q < result.patches.size(); ++q) {
            if (q == p || items[q].filament == 0)
                continue;
            const ConformalPatch &Q = result.patches[q];
            const bool before = Q.host_layer <= h, after = Q.host_layer >= h;
            if (before && Q.z_max > P.z_min + reach + EPSILON)
                return false;
            if (after && P.z_max > Q.z_min + reach + EPSILON)
                return false;
            if (P.band >= 0 && Q.band >= 0 && P.band != Q.band && items[p].bbox.overlap(items[q].bbox)) {
                const bool same_slot = Q.host_layer == h && items[q].filament == items[p].filament;
                if (Q.band < P.band && !(Q.host_layer < h || same_slot))
                    return false;
                if (Q.band > P.band && !(Q.host_layer > h || same_slot))
                    return false;
            }
        }
        return true;
    };

    std::vector<size_t> order(result.patches.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const ConformalPatch &pa = result.patches[a], &pb = result.patches[b];
        return pa.host_layer != pb.host_layer ? pa.host_layer < pb.host_layer : pa.band < pb.band;
    });
    std::vector<size_t> old_host(result.patches.size());
    for (size_t i = 0; i < result.patches.size(); ++i)
        old_host[i] = result.patches[i].host_layer;
    size_t moved = 0;
    for (size_t p : order) {
        ConformalPatch &P = result.patches[p];
        if (!items[p].movable || has_filament(P.host_layer, items[p].filament, p))
            continue;
        const size_t lo = size_t(std::lower_bound(layer_z.begin(), layer_z.end(), P.z_max - EPSILON) - layer_z.begin());
        const size_t hi = size_t(std::upper_bound(layer_z.begin(), layer_z.end(), P.z_min + reach + EPSILON) - layer_z.begin());
        std::vector<size_t> candidates;
        for (size_t h = lo; h < std::min(hi, n); ++h)
            if (h != P.host_layer && has_filament(h, items[p].filament, p))
                candidates.emplace_back(h);
        std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
            return std::abs(double(a) - double(P.host_layer)) < std::abs(double(b) - double(P.host_layer));
        });
        for (size_t h : candidates)
            if (allowed(p, h)) {
                P.host_layer = h;
                ++moved;
                break;
            }
    }
    if (moved == 0)
        return;
    for (size_t p = 0; p < result.patches.size(); ++p) {
        ConformalPatch &P = result.patches[p];
        if (P.host_layer == old_host[p])
            continue;
        const coord_t dz = coord_t(scale_(layer_z[old_host[p]] - layer_z[P.host_layer]));
        std::function<void(ExtrusionEntityCollection &)> rebase = [&](ExtrusionEntityCollection &collection) {
            for (ExtrusionEntity *ee : collection.entities)
                if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee))
                    rebase(*sub);
                else if (auto *path = dynamic_cast<ExtrusionPathConformal *>(ee))
                    for (Point3 &pt : path->polyline.points)
                        pt.z() += dz;
        };
        for (ExtrusionEntityCollection &shell : P.shell_paths)
            rebase(shell);
        rebase(P.ironing);
        std::vector<int> &from = layers[old_host[p]]->conformal_patches_after;
        from.erase(std::remove(from.begin(), from.end(), int(p)), from.end());
        layers[P.host_layer]->conformal_patches_after.emplace_back(int(p));
    }
    for (Layer *layer : layers)
        std::stable_sort(layer->conformal_patches_after.begin(), layer->conformal_patches_after.end(), [&result](int a, int b) {
            const ConformalPatch &pa = result.patches[size_t(a)], &pb = result.patches[size_t(b)];
            return pa.band != pb.band ? pa.band < pb.band : a < b;
        });
    BOOST_LOG_TRIVIAL(info) << "Non-planar: " << moved << " band patch(es) of " << object.model_object()->name
                            << " moved onto layers printing their filament";
}

bool prints_several_filaments(const PrintObject &object)
{
    std::set<int> filaments;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        for (int id : { cfg.outer_wall_filament_id.value, cfg.inner_wall_filament_id.value, cfg.sparse_infill_filament_id.value,
                        cfg.internal_solid_filament_id.value, cfg.top_surface_filament_id.value, cfg.bottom_surface_filament_id.value })
            filaments.insert(id);
    }
    if (object.has_support_material())
        for (int id : { object.config().support_filament.value, object.config().support_interface_filament.value })
            if (id > 0)
                filaments.insert(id);
    return filaments.size() > 1;
}

double multi_material_patch_height(const PrintObject &object, const HeadEnvelope &head, double max_patch_height)
{
    if (!prints_several_filaments(object))
        return max_patch_height;
    double layer_height = 0.;
    for (const Layer *layer : object.layers())
        layer_height = std::max(layer_height, layer->height);
    const double reach = head_band_height(head) - layer_height;
    return max_patch_height > 0. ? std::min(max_patch_height, reach) : reach;
}

std::vector<ConformalPatch> split_into_bands(ConformalResult &result, const HeightField &S, const std::vector<double> &layer_z,
                                             double pitch, double tolerance, const BandSeams *seams, bool host_above, int patch_offset, std::vector<size_t> *band_component)
{
    std::vector<ConformalPatch>   components = std::move(result.patches);
    std::vector<PatchDiagnostics> comp_diag  = std::move(result.diagnostics);
    result.patches.clear();
    result.diagnostics.clear();
    for (PatchDiagnostics &diag : comp_diag)
        if (diag.patch_id < 0)
            result.diagnostics.emplace_back(std::move(diag));

    struct Band
    {
        int              band;
        size_t           comp;
        ConformalPatch   patch;
        PatchDiagnostics diag;
        double           err_sq = 0.;
        size_t           err_n  = 0;
    };
    std::vector<Band> bands;
    auto band_of = [pitch](double z) { return int(std::floor(z / pitch)); };
    for (size_t c = 0; c < components.size(); ++c) {
        ConformalPatch       &comp = components[c];
        std::map<int, size_t> index;
        auto band = [&](int b) -> Band & {
            const auto [it, inserted] = index.try_emplace(b, bands.size());
            if (inserted) {
                Band nb{ b, c, {}, {} };
                ConformalPatch &p = nb.patch;
                p.kind         = comp.kind;
                p.surface      = comp.surface;
                p.base         = comp.base;
                p.shells       = comp.shells;
                p.shell_height = comp.shell_height;
                p.region_id    = comp.region_id;
                p.z_min        = Inf;
                p.z_max        = -Inf;
                p.shell_paths.assign(size_t(comp.shells), ExtrusionEntityCollection());
                bands.emplace_back(std::move(nb));
            }
            return bands[it->second];
        };
        std::function<int(const Point3 &)> cut_rank;
        Mask                               on_seam, near_edge;
        const HeightField                 &G = comp.surface;
        if (seams != nullptr) {
            const ExPolygons &area = seams->areas[c];
            on_seam   = rasterize(union_ex(offset(seams->seam_lines[c], float(scale_(0.75 * seams->spacing)))), G);
            near_edge = rasterize(diff_ex(area, offset_ex(area, -float(scale_(1.5 * seams->spacing)))), G);
            cut_rank  = [&](const Point3 &q) {
                const Vec2d o  = (unscale(q.to_point()) - G.origin()) / G.pitch();
                const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
                if (ix < 0 || iy < 0 || ix >= G.nx() || iy >= G.ny())
                    return 0;
                const size_t n = size_t(iy) * G.nx() + ix;
                return on_seam[n] ? 2 : near_edge[n] ? 1 : 0;
            };
        }
        for (size_t k = 0; k < comp.shell_paths.size(); ++k)
            cut_into_bands(comp.shell_paths[k].entities, pitch, tolerance, [&](int b, ExtrusionPathConformal *piece) {
                ConformalPatch &p = band(b).patch;
                for (const Point3 &pt : piece->polyline.points) {
                    p.z_min = std::min(p.z_min, unscale<double>(pt.z()));
                    p.z_max = std::max(p.z_max, unscale<double>(pt.z()));
                }
                p.shell_paths[k].entities.emplace_back(piece);
            }, cut_rank);
        comp.shell_paths.clear();

        const HeightField &F  = comp.surface;
        const Mask         in = rasterize(comp.footprint, F);
        std::map<int, Mask> cells;
        for (int iy = 0; iy < F.ny(); ++iy)
            for (int ix = 0; ix < F.nx(); ++ix) {
                const size_t n = size_t(iy) * F.nx() + ix;
                const float  z = F.data()[n];
                if (!in[n] || std::isnan(z))
                    continue;
                const int b    = band_of(z);
                Mask     &mask = cells[b];
                mask.resize(in.size(), 0);
                mask[n] = 1;
                Band       &bd = band(b);
                const Vec2d  p = F.node(ix, iy);
                const Vec2d  g = F.gradient(p);
                const Vec2d  o = (p - S.origin()) / S.pitch();
                const double e = S.at(int(std::lround(o.x())), int(std::lround(o.y()))) - z;
                if (!std::isnan(e)) {
                    bd.diag.max_surface_error = std::max(bd.diag.max_surface_error, e);
                    bd.err_sq += e * e;
                    ++bd.err_n;
                }
                if (!std::isnan(g.x()) && !std::isnan(g.y()))
                    bd.diag.max_slope_deg = std::max(bd.diag.max_slope_deg, Geometry::rad2deg(std::atan(g.norm())));
                bd.patch.z_max = std::max(bd.patch.z_max, double(z));
            }
        for (const auto &[b, mask] : cells) {
            ConformalPatch &p = band(b).patch;
            p.footprint       = intersection_ex(comp.footprint, mask_to_expolygons(mask, F));
            p.wall_band       = intersection_ex(p.footprint, comp.wall_band);
        }
        if (!index.empty()) {
            PatchDiagnostics &diag = bands[index.begin()->second].diag;
            diag.fallback_area = comp_diag[c].fallback_area;
            diag.repaired_nodes = comp_diag[c].repaired_nodes;
            diag.repair_lowering = comp_diag[c].repair_lowering;
        }
    }

    std::stable_sort(bands.begin(), bands.end(), [](const Band &a, const Band &b) { return a.band < b.band; });
    for (Band &bd : bands) {
        ConformalPatch &p = bd.patch;
        p.id              = patch_offset + int(result.patches.size());
        p.band            = bd.band;
        const double top  = (bd.band + 1) * pitch + tolerance;
        if (host_above) {
            const auto it = std::lower_bound(layer_z.begin(), layer_z.end(), top - EPSILON);
            p.host_layer  = std::min(size_t(it - layer_z.begin()), layer_z.size() - 1);
        } else {
            const auto it = std::upper_bound(layer_z.begin(), layer_z.end(), top + EPSILON);
            p.host_layer  = it == layer_z.begin() ? 0 : size_t(it - layer_z.begin()) - 1;
        }
        const coord_t dz  = coord_t(scale_(layer_z[p.host_layer]));
        if (seams != nullptr) {
            const HeightField &G      = p.surface;
            const Mask         inside = rasterize(seams->areas[bd.comp], G);
            Point              cursor = seams->seams[bd.comp];
            RoadGrid           roads;
            for (size_t k = 0; k < p.shell_paths.size(); ++k) {
                ExtrusionEntityCollection &shell = p.shell_paths[k];
                const double drop   = double(p.shells - 1 - int(k)) * p.shell_height;
                const double floor  = bd.band * pitch;
                const bool   hidden = int(k) < seams->hidden;
                const double roof   = hidden ? (bd.band + 1) * pitch + tolerance : Inf;
                const HeightField *count = hidden && seams->layers != nullptr ? &(*seams->layers)[bd.comp] : nullptr;
                auto glide_z = [&](const Vec2d &q, double &z) {
                    const Vec2d o  = (q - G.origin()) / G.pitch();
                    const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
                    if (ix < 0 || iy < 0 || ix >= G.nx() || iy >= G.ny() || !inside[size_t(iy) * G.nx() + ix] ||
                        (seams->wall && seams->wall(G, q)))
                        return false;
                    if (count != nullptr) {
                        const Vec2d c  = (q - count->origin()) / count->pitch();
                        const int   cx = int(std::lround(c.x())), cy = int(std::lround(c.y()));
                        if (cx < 0 || cy < 0 || cx >= count->nx() || cy >= count->ny() || !(count->at(cx, cy) > float(p.shells - 1 - int(k))))
                            return false;
                    }
                    const float s = G.sample(q);
                    z             = s - drop;
                    return !std::isnan(s) && z >= floor - 0.01 && z <= roof + 0.01;
                };
                order_band_pieces(shell.entities, cursor, roads, glide_z, seams->max_glide, G.pitch(), hidden, floor,
                                  (bd.band + 1) * pitch + tolerance);
                if (k > 0 && !shell.entities.empty() && !p.shell_paths[k - 1].entities.empty())
                    glide_from(*static_cast<ExtrusionPathConformal *>(shell.entities.front()),
                               static_cast<const ExtrusionPathConformal *>(p.shell_paths[k - 1].entities.back())->polyline.points.back(),
                               glide_z, seams->max_glide, G.pitch());
                shell.no_sort = true;
            }
            for (ExtrusionEntityCollection &shell : p.shell_paths)
                for (ExtrusionEntity *ee : shell.entities) {
                    auto *path     = static_cast<ExtrusionPathConformal *>(ee);
                    path->patch_id = p.id;
                    for (Point3 &pt : path->polyline.points)
                        pt.z() -= dz;
                }
        }
        for (ExtrusionEntityCollection &shell : p.shell_paths) {
            if (seams != nullptr)
                break;
            ExtrusionEntity *lowest_piece = nullptr;
            coord_t          lowest_z     = std::numeric_limits<coord_t>::max();
            for (ExtrusionEntity *ee : shell.entities) {
                auto *path     = static_cast<ExtrusionPathConformal *>(ee);
                path->patch_id = p.id;
                for (Point3 &pt : path->polyline.points) {
                    pt.z() -= dz;
                    if (pt.z() < lowest_z) {
                        lowest_z     = pt.z();
                        lowest_piece = ee;
                    }
                }
            }
            ExtrusionEntitiesPtr left = std::move(shell.entities);
            shell.entities.clear();
            if (lowest_piece != nullptr) {
                std::swap(*std::find(left.begin(), left.end(), lowest_piece), left.front());
                shell.entities.push_back(left.front());
                left.erase(left.begin());
            }
            while (!left.empty()) {
                const Point last    = shell.entities.back()->last_point();
                size_t      best    = 0;
                bool        flip    = false;
                double      best_d2 = std::numeric_limits<double>::max();
                for (size_t j = 0; j < left.size(); ++j) {
                    const double d_first = (left[j]->first_point() - last).cast<double>().squaredNorm();
                    const double d_last  = (left[j]->last_point() - last).cast<double>().squaredNorm();
                    if (std::min(d_first, d_last) < best_d2) {
                        best_d2 = std::min(d_first, d_last);
                        best    = j;
                        flip    = d_last < d_first;
                    }
                }
                if (flip)
                    left[best]->reverse();
                shell.entities.push_back(left[best]);
                std::swap(left[best], left.back());
                left.pop_back();
            }
            shell.no_sort = true;
        }
        if (p.z_min > p.z_max)
            p.z_min = p.z_max;
        ConformalPatch &comp = components[bd.comp];
        comp.host_layer      = std::max(comp.host_layer, p.host_layer);
        if (band_component != nullptr)
            band_component->emplace_back(bd.comp);
        bd.diag.patch_id          = p.id;
        bd.diag.rms_surface_error = bd.err_n > 0 ? std::sqrt(bd.err_sq / bd.err_n) : 0.;
        result.diagnostics.emplace_back(std::move(bd.diag));
        result.patches.emplace_back(std::move(p));
    }
    return components;
}


}
