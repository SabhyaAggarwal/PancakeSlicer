#include "PatchTools.hpp"

#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <limits>
#include <memory>
#include <typeinfo>
#include <utility>

namespace Slic3r::Nonplanar {

void throw_if_canceled(const PrintObject &object)
{
    if (object.print()->canceled())
        throw CanceledException();
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
                if (pl.is_valid())
                    pieces.entities.emplace_back(new ExtrusionPath(Polyline3(pl), path));
        }
        if (!cut) {
            out.emplace_back(ee);
            continue;
        }
        delete ee;
        for (ExtrusionEntity *piece : pieces.entities)
            out.emplace_back(piece->clone_move());
    }
    collection.entities = std::move(out);
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
    params.density     = 1.f;
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
                    int patch_id, int shell, ExtrusionEntitiesPtr &out)
{
    for (const Polyline &pl : polylines) {
        Points3            pts;
        std::vector<float> heights;
        auto flush = [&]() {
            if (pts.size() >= 2) {
                auto *path = new ExtrusionPathConformal(Polyline3(pts), proto, std::move(heights));
                path->patch_id = patch_id;
                path->shell    = shell;
                path->simplify(0.01);
                out.emplace_back(path);
            }
            pts.clear();
            heights.clear();
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
                    const std::function<void(int band, ExtrusionPathConformal *piece)> &sink)
{
    auto band_of = [pitch](double z) { return int(std::floor(z / pitch)); };
    auto fits    = [&](double lo, double hi) { return hi <= (band_of(lo) + 1) * pitch + tolerance; };
    for (const ExtrusionEntity *ee : paths) {
        const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
        if (path == nullptr || path->polyline.points.size() < 2)
            continue;
        const Points3 &src = path->polyline.points;
        auto point = [](const Point3 &p) { return Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), unscale<double>(p.z())); };

        Points3            pts;
        std::vector<float> heights;
        double             lo = 0., hi = 0.;
        auto start = [&](const Vec3d &p, float h) {
            pts.assign(1, Point3(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(p.z()))));
            heights.assign(1, h);
            lo = hi = p.z();
        };
        auto add = [&](const Vec3d &p, float h) {
            pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(p.z())));
            heights.emplace_back(h);
            lo = std::min(lo, p.z());
            hi = std::max(hi, p.z());
        };
        auto flush = [&]() {
            if (pts.size() >= 2) {
                auto *piece     = new ExtrusionPathConformal(Polyline3(pts), *path, std::vector<float>(heights));
                piece->patch_id = path->patch_id;
                piece->shell    = path->shell;
                sink(band_of(lo), piece);
            }
        };

        start(point(src.front()), path->bead_height.front());
        for (size_t i = 1; i < src.size(); ++i) {
            Vec3d       a  = point(pts.back());
            float       ha = heights.back();
            const Vec3d b  = point(src[i]);
            const float hb = path->bead_height[i];
            for (int guard = 0; guard < 10000; ++guard) {
                if (fits(std::min(lo, b.z()), std::max(hi, b.z()))) {
                    add(b, hb);
                    break;
                }
                double t0 = 0., t1 = 1.;
                for (int it = 0; it < 40; ++it) {
                    const double t = 0.5 * (t0 + t1);
                    const double z = a.z() + t * (b.z() - a.z());
                    (fits(std::min(lo, z), std::max(hi, z)) ? t0 : t1) = t;
                }
                const Vec3d cut = a + t0 * (b - a);
                const float hc  = ha + float(t0) * (hb - ha);
                if (t0 > 0.)
                    add(cut, hc);
                flush();
                start(cut, hc);
                a  = cut;
                ha = hc;
            }
        }
        flush();
    }
}

}
