#include "SpiralTop.hpp"

#include "AutoMode.hpp"
#include "ConformalShells.hpp"
#include "HeightField.hpp"
#include "PatchTools.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double MaxHelixAspect = 1.5;
constexpr double MinHelixSlope = 12.;

struct Ring
{
    std::vector<Vec2d>  pts;
    std::vector<double> s;

    double length() const { return s.back(); }
    Vec2d  at(double u) const
    {
        u              = std::clamp(u, 0., this->length());
        const size_t i = std::min(size_t(std::upper_bound(s.begin(), s.end(), u) - s.begin()), pts.size() - 1);
        if (i == 0)
            return pts.front();
        const double l = s[i] - s[i - 1];
        return l > 0. ? pts[i - 1] + (pts[i] - pts[i - 1]) * ((u - s[i - 1]) / l) : pts[i];
    }
};

Vec2d foot(const Vec2d &p, const Vec2d &a, const Vec2d &b, double &t)
{
    const Vec2d  d  = b - a;
    const double dd = d.squaredNorm();
    t               = dd > 0. ? std::clamp((p - a).dot(d) / dd, 0., 1.) : 0.;
    return a + t * d;
}

Ring open_ring(const Polygon &loop, const Vec2d &near, double step)
{
    const size_t n = loop.points.size();
    size_t       best = 0;
    double       best_t = 0., dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < n; ++i) {
        double      t;
        const Vec2d f = foot(near, unscale(loop.points[i]), unscale(loop.points[(i + 1) % n]), t);
        if (const double d = (f - near).squaredNorm(); d < dist) {
            dist   = d;
            best   = i;
            best_t = t;
        }
    }
    const Vec2d a0 = unscale(loop.points[best]), b0 = unscale(loop.points[(best + 1) % n]);
    const Vec2d first = a0 + best_t * (b0 - a0);
    std::vector<Vec2d> corners{ first };
    for (size_t k = 1; k <= n; ++k)
        corners.emplace_back(unscale(loop.points[(best + k) % n]));
    corners.emplace_back(first);
    Ring ring;
    ring.pts.emplace_back(first);
    ring.s.emplace_back(0.);
    for (size_t k = 1; k < corners.size(); ++k) {
        const Vec2d  a = corners[k - 1], b = corners[k];
        const double l = (b - a).norm();
        if (l <= EPSILON)
            continue;
        const int m = std::max(1, int(std::ceil(l / step)));
        for (int j = 1; j <= m; ++j) {
            ring.pts.emplace_back(a + (b - a) * (double(j) / m));
            ring.s.emplace_back(ring.s.back() + l / m);
        }
    }
    return ring;
}

}

Polyline fill_helix(const ExPolygons &area, double spacing, const Point &start, bool clockwise, HelixRamp *ramp, size_t min_rings)
{
    const coord_t step = coord_t(scale_(spacing));
    if (step <= 0 || area.empty())
        return {};
    {
        double a = 0., mx = 0., my = 0., mxx = 0., myy = 0., mxy = 0.;
        for (const ExPolygon &ex : area)
            for (const Polygon &poly : to_polygons(ex))
                for (size_t i = 0; i < poly.points.size(); ++i) {
                    const Vec2d  p = unscale(poly.points[i]), q = unscale(poly.points[(i + 1) % poly.points.size()]);
                    const double c = p.x() * q.y() - q.x() * p.y();
                    a += c / 2.;
                    mx += (p.x() + q.x()) * c / 6.;
                    my += (p.y() + q.y()) * c / 6.;
                    mxx += (p.x() * p.x() + p.x() * q.x() + q.x() * q.x()) * c / 12.;
                    myy += (p.y() * p.y() + p.y() * q.y() + q.y() * q.y()) * c / 12.;
                    mxy += (p.x() * q.y() + 2. * p.x() * p.y() + 2. * q.x() * q.y() + q.x() * p.y()) * c / 24.;
                }
        if (a <= 0.)
            return {};
        const double cxx = mxx / a - sqr(mx / a), cyy = myy / a - sqr(my / a), cxy = mxy / a - (mx / a) * (my / a);
        const double mid = 0.5 * (cxx + cyy), rad = std::hypot(0.5 * (cxx - cyy), cxy);
        if (mid - rad <= 0. || (mid + rad) / (mid - rad) > sqr(MaxHelixAspect))
            return {};
    }
    const double speck = PI * sqr(scale_(spacing));
    auto         ring_of = [speck](const ExPolygons &level, Polygon &ring) {
        int found = 0;
        for (const ExPolygon &ex : level)
            if (ex.area() > speck) {
                for (const Polygon &hole : ex.holes)
                    if (std::abs(hole.area()) > speck)
                        return false;
                ring = ex.contour;
                ++found;
            }
        return found == 1;
    };
    Polygons   rings;
    ExPolygons level = offset_ex(area, -0.5f * float(step));
    Polygon    ring;
    while (!level.empty()) {
        if (!ring_of(level, ring)) {
            double left = 0.;
            for (const ExPolygon &ex : level)
                left += ex.area();
            if (rings.empty() || left > 9. * speck)
                return {};
            break;
        }
        rings.emplace_back(ring);
        level = offset2_ex(ExPolygons{ ExPolygon(ring) }, -float(step) - 0.5f * float(step), 0.5f * float(step));
    }
    if (rings.size() < min_rings)
        return {};
    for (;;) {
        const Polygon &last = rings.back();
        const Point    c    = last.centroid();
        double         r    = 0.;
        for (const Point &q : last.points)
            r = std::max(r, (q - c).cast<double>().norm());
        if (r <= 1.25 * double(step))
            break;
        const ExPolygons inner = offset_ex(ExPolygons{ ExPolygon(last) }, -float(step));
        const auto       piece = std::max_element(inner.begin(), inner.end(),
                                                  [](const ExPolygon &l, const ExPolygon &r) { return l.area() < r.area(); });
        if (piece == inner.end() || piece->contour.points.size() < 3)
            break;
        if (std::count_if(inner.begin(), inner.end(), [speck](const ExPolygon &ex) { return ex.area() > speck; }) > 1)
            break;
        rings.emplace_back(piece->contour);
    }
    for (Polygon &ring : rings)
        if (ring.is_counter_clockwise() == clockwise)
            ring.reverse();

    const double sample = std::max(0.05, 0.5 * spacing);
    Polyline     out;
    auto         emit = [&out](const Vec2d &p) {
        const Point q = Point::new_scale(p.x(), p.y());
        if (out.points.empty() || out.points.back() != q)
            out.points.emplace_back(q);
    };
    Ring a = open_ring(rings.front(), unscale(start), sample);
    for (const Vec2d &p : a.pts)
        emit(p);
    const double ring_end = unscale<double>(out.length());
    for (size_t i = 0; i < rings.size(); ++i) {
        if (i == 1 && ramp != nullptr)
            *ramp = { ring_end, unscale<double>(out.length()) };
        const double la = a.length();
        if (i + 1 == rings.size()) {
            const Vec2d c = unscale(rings[i].centroid());
            for (size_t k = 0; k < a.pts.size(); ++k)
                emit(a.pts[k] + (c - a.pts[k]) * (la > 0. ? a.s[k] / la : 1.));
            emit(c);
            if (i == 0 && ramp != nullptr)
                *ramp = { ring_end, unscale<double>(out.length()) };
            break;
        }
        Ring         b  = open_ring(rings[i + 1], a.pts.front(), sample);
        const double lb = b.length();
        double u  = 0.;
        size_t bi = 0;
        for (size_t k = 0; k < a.pts.size(); ++k) {
            const double t = la > 0. ? a.s[k] / la : 1.;
            if (k + 1 == a.pts.size()) {
                u = lb;
            } else {
                double best = std::numeric_limits<double>::max(), best_u = u;
                size_t best_i = bi;
                for (size_t j = bi; j + 1 < b.pts.size() && b.s[j] <= u + 0.25 * lb; ++j) {
                    double      tt;
                    const Vec2d f = foot(a.pts[k], b.pts[j], b.pts[j + 1], tt);
                    if (const double d = (f - a.pts[k]).squaredNorm(); d < best) {
                        best   = d;
                        best_u = b.s[j] + tt * (b.s[j + 1] - b.s[j]);
                        best_i = j;
                    }
                }
                if (best_u > u) {
                    u  = best_u;
                    bi = best_i;
                }
            }
            emit(a.pts[k] + (b.at(u) - a.pts[k]) * t);
        }
        a = std::move(b);
    }
    return out.points.size() >= 2 ? out : Polyline();
}

bool helix_worthwhile(const HeightField &surface, const ExPolygons &area)
{
    const Mask          mask = rasterize(area, surface);
    std::vector<double> slopes;
    for (int iy = 0; iy < surface.ny(); ++iy)
        for (int ix = 0; ix < surface.nx(); ++ix)
            if (mask[size_t(iy) * surface.nx() + ix]) {
                const Vec2d g = surface.gradient(surface.node(ix, iy));
                if (!std::isnan(g.x()) && !std::isnan(g.y()))
                    slopes.emplace_back(std::atan(g.norm()));
            }
    if (slopes.empty())
        return false;
    std::nth_element(slopes.begin(), slopes.begin() + slopes.size() / 2, slopes.end());
    return slopes[slopes.size() / 2] >= Geometry::deg2rad(MinHelixSlope);
}

void ramp_helix_flow(ExtrusionPathConformal &path, const HelixRamp &ramp, bool reversed)
{
    if (ramp.end <= ramp.begin)
        return;
    const Points3 &pts = path.polyline.points;
    double         s   = 0.;
    for (size_t n = 0; n < pts.size(); ++n) {
        const size_t i = reversed ? pts.size() - 1 - n : n;
        if (n > 0)
            s += unscale(Point(pts[i].to_point() - pts[reversed ? i + 1 : i - 1].to_point())).norm();
        if (s > ramp.begin && s < ramp.end)
            path.bead_height[i] *= float(std::max(0.02, (s - ramp.begin) / (ramp.end - ramp.begin)));
    }
}

namespace {

std::optional<ExtrusionLoop> vase_loop(const Layer &layer, int &region_id)
{
    std::optional<ExtrusionLoop> found;
    for (const LayerRegion *layerm : layer.regions()) {
        for (const ExtrusionEntity *ee : layerm->perimeters.flatten().entities) {
            const auto *loop = dynamic_cast<const ExtrusionLoop *>(ee);
            if (loop == nullptr || found)
                return {};
            found     = *loop;
            region_id = int(layerm->region().print_object_region_id());
        }
    }
    return found;
}

std::vector<Vec2d> resampled_from(const Polygon &loop, size_t start, double step)
{
    std::vector<Vec2d> out{ unscale(loop.points[start]) };
    for (size_t k = 1; k <= loop.points.size(); ++k) {
        const Vec2d a = out.back(), b = unscale(loop.points[(start + k) % loop.points.size()]);
        const int   m = std::max(1, int(std::ceil((b - a).norm() / step)));
        for (int j = 1; j <= m; ++j)
            out.emplace_back(a + (b - a) * (double(j) / m));
    }
    return out;
}

}

void build_vase_lid(PrintObject &object, ConformalResult &result)
{
    LayerPtrs &layers = object.layers();
    for (Layer *layer : layers) {
        layer->conformal_patches_before.clear();
        layer->conformal_patches_after.clear();
        layer->conformal_covered = false;
    }
    if (!object.print()->config().spiral_mode.value || layers.size() < 2)
        return;
    bool wanted = false;
    for (size_t r = 0; r < object.num_printing_regions(); ++r) {
        const PrintRegionConfig &cfg = object.printing_region(r).config();
        if (bottom_shells_enabled(cfg, object.config()))
            return;
        wanted |= top_shells_enabled(cfg, object.config()) && cfg.nonplanar_spiral_top.value;
    }
    if (!wanted)
        return;
    const double h     = object.config().layer_height.value;
    const double pitch = std::max(0.05, object.config().nonplanar_surface_resolution.value);
    auto skip = [&object](const char *why) {
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: no vase lid on " << object.model_object()->name << ": " << why;
    };

    HeightField S;
    {
        const HeightField  grid          = object_grid(object, pitch);
        double             ground_offset = 0.;
        const TriangleMesh mesh          = object.object_frame_mesh(ground_offset);
        sla::IndexedMesh   imesh(mesh);
        imesh.ground_level_offset(ground_offset);
        S = sample_top_surface(imesh, mesh.bounding_box().max.z() + 1., object.slicing_parameters().object_print_z_min, grid,
                               [&object]() { throw_if_canceled(object); }, material_below(object));
        smooth_scan_noise(S, [&object]() { throw_if_canceled(object); });
        pad_lowest(S, 2);
    }

    size_t                       host      = layers.size();
    std::optional<ExtrusionLoop> wall;
    int                          region_id = -1;
    for (size_t i = 1; i < layers.size() && host == layers.size(); ++i) {
        int                          rid  = -1;
        std::optional<ExtrusionLoop> loop = vase_loop(*layers[i], rid);
        if (!loop)
            continue;
        const PrintRegionConfig &cfg = object.printing_region(size_t(rid)).config();
        if (!top_shells_enabled(cfg, object.config()) || !cfg.nonplanar_spiral_top.value || i < size_t(cfg.bottom_shell_layers.value))
            continue;
        double rim = std::numeric_limits<double>::max();
        for (const Vec2d &p : resampled_from(loop->polygon(), 0, pitch)) {
            const float z = S.sample(p);
            if (std::isnan(z))
                return skip("the top surface misses the wall loop");
            rim = std::min(rim, double(z));
        }
        if (rim - layers[i]->print_z < 1.5 * h) {
            host      = i;
            wall      = std::move(loop);
            region_id = rid;
        }
    }
    if (!wall)
        return skip("no wall layer reaches the top");
    const PrintRegionConfig &cfg       = object.printing_region(size_t(region_id)).config();
    const double             host_z    = layers[host]->print_z;
    const bool               clockwise = cfg.wall_direction.value != WallDirection::CounterClockwise;
    Polygon                  loop      = wall->polygon();
    if (loop.is_counter_clockwise() == clockwise)
        loop.reverse();
    const ExtrusionPath &bead    = wall->paths.front();
    const double         width   = bead.width;
    const double         spacing = width - h * (1. - 0.25 * PI);
    size_t start = 0;
    for (size_t k = 1; k < loop.points.size(); ++k)
        if (loop.points[k].y() > loop.points[start].y())
            start = k;
    const Point C = loop.points[start];
    Polygon     inside = loop;
    inside.make_counter_clockwise();
    const ExPolygon  lid(inside);
    const ExPolygons area = offset_ex(lid, 0.5f * float(scale_(spacing)));

    double            apex = -std::numeric_limits<double>::max();
    const BoundingBox bb   = get_extents(lid);
    for (coord_t y = bb.min.y(); y <= bb.max.y(); y += coord_t(scale_(pitch)))
        for (coord_t x = bb.min.x(); x <= bb.max.x(); x += coord_t(scale_(pitch)))
            if (const Point q(x, y); lid.contains(q)) {
                const float z = S.sample(unscale(q));
                if (std::isnan(z))
                    return skip("the top leaves part of the loop open");
                apex = std::max(apex, double(z));
            }
    if (apex < host_z + 2. * h)
        return skip("the top is flat");

    const ExtrusionPath proto(erExternalPerimeter, bead.mm3_per_mm, float(width), float(h));
    auto rel = [host_z](const Vec2d &p, double z) { return Point3(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(z - host_z))); };

    const std::vector<Vec2d> ring = resampled_from(loop, start, pitch);
    std::vector<double>      ring_s{ 0. };
    double                   rim_max = -std::numeric_limits<double>::max();
    for (size_t k = 1; k < ring.size(); ++k)
        ring_s.emplace_back(ring_s.back() + (ring[k] - ring[k - 1]).norm());
    for (const Vec2d &p : ring)
        rim_max = std::max(rim_max, double(S.sample(p)));
    const int turns = std::max(0, int(std::ceil((rim_max - h - host_z) / h - EPSILON)));
    Points3            wall_pts;
    std::vector<float> wall_beads;
    for (int n = 1; n <= turns; ++n)
        for (size_t k = n == 1 ? 0 : 1; k < ring.size(); ++k) {
            const double nominal = host_z + (n - 1 + ring_s[k] / ring_s.back()) * h;
            const double cap     = S.sample(ring[k]) - h;
            const double z       = std::min(nominal, cap);
            const double t       = z - std::min(nominal - h, cap);
            wall_pts.emplace_back(rel(ring[k], z));
            wall_beads.emplace_back(t < 0.05 * h ? 0.f : float(t));
        }
    const double z_start = turns > 0 ? unscale<double>(wall_pts.back().z()) + host_z : host_z;

    HelixRamp ramp;
    Polyline  helix = fill_helix(area, spacing, C, clockwise, &ramp);
    if (!helix.is_valid())
        return skip("the lid has more than one centre");
    helix.points.front() = C;
    const double       first_turn = ramp.begin;
    const double       rise       = S.sample(unscale(C)) - z_start;
    Points3            lid_pts;
    std::vector<float> lid_beads;
    double             s = 0.;
    for (size_t i = 0; i + 1 < helix.points.size(); ++i) {
        const Vec2d  a = unscale(helix.points[i]), b = unscale(helix.points[i + 1]);
        const double l = (b - a).norm();
        const int    m = std::max(1, int(std::ceil(l / pitch)));
        for (int j = i == 0 ? 0 : 1; j <= m; ++j) {
            const Vec2d  p = a + (b - a) * (double(j) / m);
            const double z = S.sample(p) - rise * std::max(0., 1. - (s + l * j / m) / first_turn);
            if (std::isnan(z))
                return skip("the top surface misses the lid");
            lid_pts.emplace_back(rel(p, z));
            lid_beads.emplace_back(float(h));
        }
        s += l;
    }

    ConformalPatch patch;
    patch.id           = 0;
    patch.kind         = PatchKind::Top;
    patch.footprint    = area;
    patch.surface      = std::move(S);
    patch.shells       = 1;
    patch.shell_height = h;
    patch.host_layer   = host;
    patch.region_id    = region_id;
    patch.z_min        = host_z;
    patch.z_max        = apex;
    ExtrusionEntityCollection shell;
    for (auto [pts, beads] : { std::make_pair(&wall_pts, &wall_beads), std::make_pair(&lid_pts, &lid_beads) }) {
        if (pts->size() < 2)
            continue;
        auto *path = new ExtrusionPathConformal(Polyline3(std::move(*pts)), proto, std::move(*beads));
        path->patch_id = 0;
        path->shell    = 0;
        path->simplify(0.01);
        if (pts == &lid_pts)
            ramp_helix_flow(*path, ramp, false);
        shell.entities.emplace_back(path);
    }
    shell.no_sort = true;
    patch.shell_paths.emplace_back(std::move(shell));

    result.planar_exclusion.assign(layers.size(), ExPolygons());
    for (LayerRegion *layerm : layers[host]->regions()) {
        layerm->fills.clear();
        layerm->thin_fills.clear();
    }
    for (size_t i = host + 1; i < layers.size(); ++i) {
        for (LayerRegion *layerm : layers[i]->regions()) {
            layerm->perimeters.clear();
            layerm->fills.clear();
            layerm->thin_fills.clear();
        }
        result.planar_exclusion[i] = layers[i]->lslices;
        layers[i]->conformal_covered = true;
    }
    layers[host]->conformal_patches_after.emplace_back(0);
    PatchDiagnostics diag;
    diag.patch_id = 0;
    result.diagnostics.emplace_back(std::move(diag));
    result.patches.emplace_back(std::move(patch));
    BOOST_LOG_TRIVIAL(info) << "Non-planar: vase lid on layer " << host << " of " << object.model_object()->name << ", " << turns
                            << " wall turn(s) up the rim, apex at " << apex << " mm";
}

bool lid_start(const PrintObject &object, Point &start)
{
    const std::vector<ConformalPatch> &patches = object.conformal().patches;
    if (patches.size() != 1 || patches.front().shell_paths.empty() || patches.front().shell_paths.front().entities.empty())
        return false;
    const auto *path = dynamic_cast<const ExtrusionPathConformal *>(patches.front().shell_paths.front().entities.front());
    if (path == nullptr || path->role() != erExternalPerimeter || path->polyline.points.empty())
        return false;
    start = path->polyline.points.front().to_point();
    return true;
}

}
