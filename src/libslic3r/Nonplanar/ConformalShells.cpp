#include "ConformalShells.hpp"

#include "HeadEnvelope.hpp"
#include "PatchTools.hpp"
#include "Seams.hpp"
#include "ShellPaths.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
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

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
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
    InfillPattern        sparse_pattern = ipRectilinear;
    double               sparse_angle = 0.;
    double               bottom_keep = 0.;
    double               shell_height = 0.;
    double               max_slope = 0.;
    double               max_error = 0.;
    double               min_area = 0.;
    double               pitch = 0.2;
    double               max_patch_height = 0.;
    bool                 banded = false;
};

ShellSettings shell_settings(const PrintObject &object)
{
    ShellSettings s;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        if (cfg.nonplanar_top_shells.value <= 0)
            continue;
        s.region_id        = int(region_id);
        s.shells           = cfg.nonplanar_top_shells.value;
        s.shell_height     = cfg.nonplanar_shell_layer_height.value > 0. ? cfg.nonplanar_shell_layer_height.value :
                                                                            object.config().layer_height.value;
        s.max_slope        = Geometry::deg2rad(cfg.nonplanar_max_slope.value);
        s.max_error        = cfg.nonplanar_max_surface_error.value;
        s.min_area         = cfg.nonplanar_min_patch_area.value;
        s.pitch            = std::max(0.05, object.config().nonplanar_surface_resolution.value);
        s.max_patch_height = object.config().nonplanar_max_patch_height.value;
        s.banded           = cfg.nonplanar_fallback.value == NonplanarFallback::None;
        s.top_shells       = s.shells;
        if (s.banded && cfg.nonplanar_infill_depth.value > 0.) {
            const int top_solid = std::max(cfg.top_shell_layers.value, int(std::ceil(cfg.top_shell_thickness.value / s.shell_height - EPSILON)));
            s.shells            = std::max(s.shells, top_solid);
            s.sparse_density    = std::min(1., 0.01 * cfg.sparse_infill_density.value);
            if (s.sparse_density > 0.)
                s.sparse = int(std::lround(cfg.nonplanar_infill_depth.value / s.shell_height));
            s.shells        += s.sparse;
            s.sparse_pattern = cfg.sparse_infill_pattern.value == ipGyroid ? ipGyroid : ipRectilinear;
            s.sparse_angle   = Geometry::deg2rad(cfg.infill_direction.value);
            s.bottom_keep    = std::max(cfg.bottom_shell_layers.value * object.config().layer_height.value, cfg.bottom_shell_thickness.value);
        }
        break;
    }
    return s;
}

ExPolygons candidate_top_area(const PrintObject &object)
{
    Polygons tops;
    for (const Layer *layer : object.layers())
        for (const LayerRegion *layerm : layer->regions())
            if (layerm->region().config().nonplanar_top_shells.value > 0)
                for (const Surface &surface : layerm->slices.surfaces)
                    if (surface.surface_type == stTop)
                        polygons_append(tops, to_polygons(surface.expolygon));
    const float seam = float(scale_(0.01));
    return offset_ex(union_ex(offset(tops, seam)), -seam);
}

void split_by_colour(ConformalResult &result, std::vector<HeightField> &planar_tops, std::vector<HeightField> &stack_layers,
                     std::vector<ExPolygons> &shell_areas,
                     std::vector<ExPolygons> &fill_areas, std::vector<Point> &seams, std::vector<Polyline> &seam_lines,
                     const TopColours &colours)
{
    std::vector<HeightField> tops, depths;
    std::vector<ExPolygons>  areas, fills;
    std::vector<Point>       new_seams;
    std::vector<Polyline>    lines;
    for (const auto &[idx, c] : split_by_colour(result, colours)) {
        tops.emplace_back(planar_tops[idx]);
        depths.emplace_back(stack_layers[idx]);
        areas.emplace_back(intersection_ex(shell_areas[idx], colours.areas[c]));
        fills.emplace_back(intersection_ex(fill_areas[idx], colours.areas[c]));
        new_seams.emplace_back(seams[idx]);
        lines.emplace_back(seam_lines[idx]);
    }
    planar_tops = std::move(tops);
    stack_layers = std::move(depths);
    shell_areas = std::move(areas);
    fill_areas  = std::move(fills);
    seams       = std::move(new_seams);
    seam_lines  = std::move(lines);
}

constexpr double ContourStripHeight = 2.;
constexpr int ContourDirectionBins = 12;
constexpr double ContourDirectionWindow = 2.;

Polylines fill_piece(const ExPolygons &piece, const ExPolygons &area, double angle, const Flow &flow, bool outer, double z)
{
    const ExPolygons grown = intersection_ex(offset_ex(piece, float(scale_(2. * flow.spacing()))), area);
    return intersection_pl(fill_solid(grown, outer ? ipMonotonic : ipRectilinear, flow.spacing(), angle, outer, flow, z), piece);
}

Polylines contour_fill(const HeightField &surface, const ExPolygons &area, double drop, const Flow &flow, bool outer, double z)
{
    const int nx = surface.nx(), ny = surface.ny();
    std::vector<double> sxx(size_t(nx + 1) * (ny + 1), 0.), sxy(sxx.size(), 0.), syy(sxx.size(), 0.);
    auto at = [nx](std::vector<double> &v, int ix, int iy) -> double & { return v[size_t(iy) * (nx + 1) + ix]; };
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            Vec2d g = surface.gradient(surface.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y()))
                g = Vec2d::Zero();
            at(sxx, ix + 1, iy + 1) = g.x() * g.x() + at(sxx, ix, iy + 1) + at(sxx, ix + 1, iy) - at(sxx, ix, iy);
            at(sxy, ix + 1, iy + 1) = g.x() * g.y() + at(sxy, ix, iy + 1) + at(sxy, ix + 1, iy) - at(sxy, ix, iy);
            at(syy, ix + 1, iy + 1) = g.y() * g.y() + at(syy, ix, iy + 1) + at(syy, ix + 1, iy) - at(syy, ix, iy);
        }
    const int        half = std::max(1, int(std::lround(0.5 * ContourDirectionWindow / surface.pitch())));
    std::vector<int> direction(size_t(nx) * ny, 0);
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            const int x0 = std::max(0, ix - half), x1 = std::min(nx, ix + half + 1), y0 = std::max(0, iy - half), y1 = std::min(ny, iy + half + 1);
            auto box = [&](std::vector<double> &v) { return at(v, x1, y1) - at(v, x0, y1) - at(v, x1, y0) + at(v, x0, y0); };
            const double jxx = box(sxx), jxy = box(sxy), jyy = box(syy);
            if (jxx + jyy > 1e-12) {
                const double contour = std::fmod(0.5 * std::atan2(2. * jxy, jxx - jyy) + 1.5 * PI, PI);
                direction[size_t(iy) * nx + ix] = std::min(ContourDirectionBins - 1, int(contour / PI * ContourDirectionBins));
            }
        }

    std::map<int, Mask> strips;
    for (size_t n = 0; n < surface.data().size(); ++n)
        if (const float s = surface.data()[n]; !std::isnan(s)) {
            Mask &mask = strips[int(std::floor((s - drop) / ContourStripHeight))];
            mask.resize(surface.data().size(), 0);
            mask[n] = 1;
        }
    Polylines out;
    for (const auto &[strip, mask] : strips)
        for (const ExPolygon &piece : intersection_ex(area, mask_to_expolygons(mask, surface))) {
            const BoundingBox bb  = get_extents(piece);
            const Vec2d       lo  = (unscale(bb.min) - surface.origin()) / surface.pitch();
            const Vec2d       hi  = (unscale(bb.max) - surface.origin()) / surface.pitch();
            const int         ix0 = std::clamp(int(std::floor(lo.x())), 0, nx - 1), iy0 = std::clamp(int(std::floor(lo.y())), 0, ny - 1);
            const int         ix1 = std::clamp(int(std::ceil(hi.x())), 0, nx - 1), iy1 = std::clamp(int(std::ceil(hi.y())), 0, ny - 1);
            HeightField       local  = crop(surface, ix0, iy0, ix1, iy1);
            const Mask        inside = rasterize({ piece }, local);
            for (size_t n = 0; n < inside.size(); ++n)
                if (!inside[n])
                    local.data()[n] = HeightField::NoData;
            double angle = 0.;
            if (contour_line_angle(local, angle, 0.5)) {
                append(out, fill_piece({ piece }, area, angle, flow, outer, z));
                continue;
            }
            std::vector<Mask> classes(ContourDirectionBins);
            for (int iy = 0; iy < local.ny(); ++iy)
                for (int ix = 0; ix < local.nx(); ++ix)
                    if (inside[size_t(iy) * local.nx() + ix]) {
                        Mask &m = classes[size_t(direction[size_t(iy0 + iy) * nx + ix0 + ix])];
                        m.resize(inside.size(), 0);
                        m[size_t(iy) * local.nx() + ix] = 1;
                    }
            for (int bin = 0; bin < ContourDirectionBins; ++bin)
                if (!classes[size_t(bin)].empty())
                    append(out, fill_piece(intersection_ex(ExPolygons{ piece }, mask_to_expolygons(classes[size_t(bin)], local)), area,
                                           (bin + 0.5) * PI / ContourDirectionBins, flow, outer, z));
        }
    return out;
}

bool outline_is_contour(const ConformalPatch &patch, double tolerance)
{
    const double pitch = patch.surface.pitch();
    float        lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    for (const ExPolygon &expoly : offset_ex(patch.footprint, -float(scale_(2. * pitch)))) {
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
    return hi >= lo && hi - lo <= tolerance;
}

constexpr double WallSlope = 60.;

bool spans_wall(const HeightField &surface, const Vec2d &p)
{
    const Vec2d o  = (p - surface.origin()) / surface.pitch();
    const int   ix = std::clamp(int(std::floor(o.x())), 0, surface.nx() - 2);
    const int   iy = std::clamp(int(std::floor(o.y())), 0, surface.ny() - 2);
    const float z[4] = { surface.at(ix, iy), surface.at(ix + 1, iy), surface.at(ix, iy + 1), surface.at(ix + 1, iy + 1) };
    static const double rise = std::tan(Geometry::deg2rad(WallSlope)) * std::sqrt(2.);
    return *std::max_element(z, z + 4) - *std::min_element(z, z + 4) > rise * surface.pitch();
}

constexpr double BandedTopWidth = 0.75;

struct BandedFill
{
    bool strips = false;
    const HeightField *layers = nullptr;
    int                sparse = 0;
    double             density = 1.;
    InfillPattern      pattern = ipRectilinear;
    double             angle = 0.;
};

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
    Flow                      flow    = region.flow(object, outer ? frTopSolidInfill : sparse ? frInfill : frSolidInfill, patch.shell_height);
    if (banded != nullptr && outer)
        flow = flow.with_width(std::min(flow.width(), float(BandedTopWidth * flow.nozzle_diameter())));
    const ExtrusionRole       role    = outer ? erTopSolidInfill : sparse ? erInternalInfill : erSolidInfill;
    const double drop = (patch.shells - k) * patch.shell_height;
    const bool   surface_spaced = region.config().nonplanar_surface_spacing.value && !fill.spiral && !(banded != nullptr && banded->strips) && !sparse;
    std::vector<Polylines> linked;
    if (sparse) {
        for (Polyline &pl : fill_sparse(area, banded->pattern, flow.spacing(), banded->density, banded->angle + (k % 2) * 0.5 * PI, false,
                                        flow, k * patch.shell_height))
            linked.push_back({ std::move(pl) });
    } else if (banded != nullptr && banded->strips)
        linked = link_paths(chain_nearest(contour_fill(patch.surface, area, drop, flow, outer, host_z), start), area, flow.spacing(),
                            MaxGlide);
    else
        linked = shell_polylines(area, fill, outer ? ipMonotonic : ipRectilinear, flow.spacing(), outer, flow, host_z, start, MaxGlide,
                                 surface_spaced ? &patch.surface : nullptr, &seam_line);

    const ExtrusionPath proto(role, flow.mm3_per_mm(), float(flow.width()), float(patch.shell_height));
    auto at = [&](const Vec2d &p, double &z, float &bead) {
        const float s = patch.surface.sample(p);
        if (std::isnan(s) || (banded != nullptr && spans_wall(patch.surface, p)))
            return false;
        z    = s - drop;
        bead = float(patch.shell_height);
        if (banded != nullptr && banded->layers != nullptr) {
            const float layers = nearest(*banded->layers, p);
            const int   above  = patch.shells - k;
            if (std::isnan(layers) || above >= int(layers))
                return false;
            if (above == int(layers) - 1) {
                const float floor_z = nearest(top_of_planar, p);
                if (std::isnan(floor_z))
                    return false;
                bead = float(z - floor_z);
            }
        } else if (k == 1) {
            const float floor_z = top_of_planar.sample(p);
            if (std::isnan(floor_z))
                return false;
            bead = float(z - floor_z);
        }
        if (surface_spaced)
            bead = surface_spaced_bead(bead, flow.width(), patch.surface.gradient(p), fill.angle);
        return true;
    };
    ExtrusionEntityCollection out;
    lift_linked(linked, proto, at, host_z, resample, patch.id, k - 1, out.entities);
    if (from != nullptr && !out.entities.empty())
        prepend_glide(*static_cast<ExtrusionPathConformal *>(out.entities.front()), *from,
                      offset_ex(area, float(scale_(0.25 * flow.spacing()))), at, host_z, resample, MaxGlide);
    out.no_sort = true;
    return out;
}

}

HeightField sample_top_surface(const sla::IndexedMesh &mesh, double ray_z, double z_offset, const HeightField &like,
                               const std::function<void()> &throw_if_canceled)
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
                    if (hit.is_hit() && hit.normal().normalized().z() > 0.05) {
                        const Vec3d n = hit.normal();
                        const double z = hit.position().z() + (n.x() * (q.x() - p.x()) + n.y() * (q.y() - p.y())) / n.z();
                        S.at(ix, iy) = float(z - mesh.ground_level() + z_offset);
                        break;
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
                const auto  hit = mesh.query_ray_hit(Vec3d(p.x(), p.y(), s - z_offset + mesh.ground_level() - 0.01), Vec3d(0., 0., -1.));
                U.at(ix, iy)    = float(hit.is_hit() ? hit.position().z() - mesh.ground_level() + z_offset : z_offset);
            }
        }
    });
    return U;
}

ConformalResult build_conformal_shells(PrintObject &object)
{
    ConformalResult result;
    const auto      t_start = std::chrono::steady_clock::now();
    LayerPtrs      &layers  = object.layers();
    for (Layer *layer : layers) {
        layer->conformal_patches_after.clear();
        layer->conformal_covered = false;
    }
    const ShellSettings settings = shell_settings(object);
    if (settings.shells <= 0 || layers.empty())
        return result;

    result.planar_exclusion.assign(layers.size(), ExPolygons());

    const ExPolygons candidate = candidate_top_area(object);
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
    {
        double             ground_offset = 0.;
        const TriangleMesh mesh          = object.object_frame_mesh(ground_offset);
        sla::IndexedMesh   imesh(mesh);
        imesh.ground_level_offset(ground_offset);
        S = sample_top_surface(imesh, mesh.bounding_box().max.z() + 1., object.slicing_parameters().object_print_z_min, grid, cancel);
        if (settings.shells > settings.top_shells)
            U = sample_underside(imesh, object.slicing_parameters().object_print_z_min, S, cancel);
    }
    const Mask        cand_mask = rasterize(candidate, grid);
    throw_if_canceled(object);

    const PrintRegion &region       = object.printing_region(size_t(settings.region_id));
    const double       line_width   = region.flow(object, frTopSolidInfill, settings.shell_height).width();
    const double       wall_width   = std::max(0, region.config().wall_loops.value) *
                                region.flow(object, frPerimeter, object.config().layer_height.value).width();
    const ExPolygons   band_cand    = wall_width > 0. ?
                                          diff_ex(candidate, offset_ex(union_ex(silhouette_src), float(-scale_(wall_width)))) :
                                          ExPolygons();
    const Mask         band_mask    = rasterize(band_cand, grid);
    const double       h_c          = settings.shell_height;
    const double       h_min        = 0.5 * h_c;
    const double       h_max        = 0.8 * region.flow(object, settings.shells == 1 ? frTopSolidInfill : frSolidInfill, h_c).nozzle_diameter();
    if (h_c > h_max)
        return result;
    const double       first_top    = layers.front()->print_z;

    HeightField S0 = S;
    for (size_t i = 0; i < n_nodes; ++i)
        if (!cand_mask[i])
            S0.data()[i] = HeightField::NoData;
    if (settings.max_slope > 0.)
        S0 = slope_cap_minorant(S0, settings.max_slope);
    Mask mask(n_nodes, 0);
    for (size_t i = 0; i < n_nodes; ++i)
        mask[i] = cand_mask[i] && !std::isnan(S0.data()[i]);
    if (settings.banded)
        for (size_t i = 0; i < n_nodes; ++i)
            if (mask[i] && S0.data()[i] - settings.top_shells * h_c - h_min < first_top)
                mask[i] = 0;
    std::vector<int> limiting_components(n_nodes, -1);
    if (!settings.banded)
        drop_unsafe_nodes(S0, head, h_c, settings.max_error, mask, &limiting_components);

    HeightField S1        = settings.banded ? S0 : grid;
    bool        converged = settings.banded;
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
                const HeightField s1 = head_lipschitz_minorant(component_field(S0, c), head, 8, cancel);
                for (int iy = 0; iy < s1.ny(); ++iy)
                    for (int ix = 0; ix < s1.nx(); ++ix)
                        if (label[size_t(e.iy0 + iy) * grid.nx() + e.ix0 + ix] == c)
                            S1.at(e.ix0 + ix, e.iy0 + iy) = s1.at(ix, iy);
            }
        });
        bool changed = false;
        for (size_t i = 0; i < n_nodes; ++i) {
            if (!mask[i])
                continue;
            const float s1 = S1.data()[i];
            if (std::isnan(s1) || S.data()[i] - s1 > settings.max_error || s1 - settings.shells * h_c - h_min < first_top) {
                mask[i] = 0;
                changed = true;
            } else
                ext[size_t(label[i])].zcap = std::max(ext[size_t(label[i])].zcap, s1);
        }
        std::vector<std::vector<size_t>> drops(static_cast<size_t>(n_comp));
        tbb::parallel_for(tbb::blocked_range<int>(0, n_comp), [&](const tbb::blocked_range<int> &range) {
            for (int c = range.begin(); c < range.end(); ++c) {
                const Extent &e = ext[size_t(c)];
                if (e.zcap == -Inf)
                    continue;
                HeightField H = grid;
                for (size_t i = 0; i < n_nodes; ++i)
                    if ((label[i] != c || !mask[i] || band_mask[i]) && !std::isnan(S.data()[i]))
                        H.data()[i] = std::min(S.data()[i], e.zcap);
                const HeightField like   = component_field(S1, c);
                const HeightField z_safe = min_safe_tip_height(H, head, like, nullptr, true);
                const double lowest_shell = (settings.shells - 1) * h_c;
                for (int iy = 0; iy < like.ny(); ++iy)
                    for (int ix = 0; ix < like.nx(); ++ix) {
                        const size_t i = size_t(e.iy0 + iy) * grid.nx() + e.ix0 + ix;
                        if (label[i] == c && mask[i] && !band_mask[i] &&
                            like.at(ix, iy) - lowest_shell < z_safe.at(ix, iy) - settings.max_error)
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

    ExPolygons components = opening_ex(mask_to_expolygons(mask, grid), float(scale_(line_width)));
    components            = settings.banded ? intersection_ex(components, candidate) :
                                              intersection_ex(offset_ex(expolygons_simplify(components, scale_(pitch)), -float(scale_(pitch))),
                                                              candidate);
    {
        ExPolygons kept;
        for (ExPolygon &c : components)
            if (settings.banded || c.area() * SCALING_FACTOR * SCALING_FACTOR >= settings.min_area)
                kept.emplace_back(std::move(c));
        components = std::move(kept);
    }

    if (settings.max_patch_height > 0. && !settings.banded) {
        ExPolygons bands;
        for (const ExPolygon &component : components) {
            const Mask pmask = rasterize({ component }, grid);
            float zlo = Inf, zhi = -Inf;
            for (size_t i = 0; i < n_nodes; ++i)
                if (pmask[i] && !std::isnan(S1.data()[i])) {
                    zlo = std::min(zlo, S1.data()[i]);
                    zhi = std::max(zhi, S1.data()[i]);
                }
            if (zhi - zlo <= settings.max_patch_height) {
                bands.emplace_back(component);
                continue;
            }
            const int count = int(std::ceil((zhi - zlo) / settings.max_patch_height));
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

    std::vector<double> layer_z(layers.size());
    for (size_t i = 0; i < layers.size(); ++i)
        layer_z[i] = layers[i]->print_z;

    std::vector<HeightField> planar_tops;
    std::vector<HeightField> stack_layers;
    std::vector<ExPolygons>  shell_areas;
    const int                solid_layers = settings.shells - settings.sparse;
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
        if (ix1 < 0 || zhi - zlo < 0.01)
            continue;
        const int pad = 3;
        ix0 = std::max(0, ix0 - pad), iy0 = std::max(0, iy0 - pad);
        ix1 = std::min(grid.nx() - 1, ix1 + pad), iy1 = std::min(grid.ny() - 1, iy1 + pad);

        ConformalPatch patch;
        patch.id           = int(result.patches.size());
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
                    patch.surface.at(ix, iy) = HeightField::NoData;

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
                const double b = s1 - settings.shells * h_c;
                patch.base.at(ix, iy) = float(b);
                patch.z_max    = std::max(patch.z_max, double(s1));
                const double e = S.at(ix0 + ix, iy0 + iy) - s1;
                diag.max_surface_error = std::max(diag.max_surface_error, e);
                err_sq += e * e;
                ++err_n;
                const Vec2d g = patch.surface.gradient(patch.surface.node(ix, iy));
                if (!std::isnan(g.x()) && !std::isnan(g.y()))
                    diag.max_slope_deg = std::max(diag.max_slope_deg, Geometry::rad2deg(std::atan(g.norm())));
            }
        diag.limiting_component = most_limiting_component(head, limiting_components, pmask);
        diag.rms_surface_error = err_n > 0 ? std::sqrt(err_sq / err_n) : 0.;
        pad_lowest(patch.surface, pad);
        pad_lowest(patch.base, pad);
        bool        printable = true;
        HeightField count     = top;
        for (size_t i = 0; i < top.data().size(); ++i) {
            const float s = patch.surface.data()[i];
            if (std::isnan(s))
                continue;
            int layer = -1, m = settings.shells;
            for (; m >= settings.top_shells; --m) {
                if (m > settings.top_shells) {
                    const float u = U.at(ix0 + int(i % size_t(top.nx())), iy0 + int(i / size_t(top.nx())));
                    if (std::isnan(u) || s - m * h_c - h_min < u + (m > solid_layers ? settings.bottom_keep : h_c))
                        continue;
                }
                if ((layer = planar_transition_layer(layer_z, s - (m - 1) * h_c, h_c, h_min, h_max, PatchKind::Top)) >= 0)
                    break;
            }
            if (layer < 0) {
                printable = false;
                break;
            }
            count.data()[i] = float(m);
            top.data()[i]   = float(layer_z[size_t(layer)]);
            patch.z_min = std::min(patch.z_min, layer_z[size_t(layer)]);
        }
        if (!printable)
            continue;

        patch.host_layer = size_t(std::lower_bound(layer_z.begin(), layer_z.end(), patch.z_max - EPSILON) - layer_z.begin());
        if (patch.host_layer >= layers.size())
            patch.host_layer = layers.size() - 1;

        planar_tops.emplace_back(std::move(top));
        stack_layers.emplace_back(std::move(count));
        shell_areas.emplace_back(diff_ex(patch.footprint, patch.wall_band));
        result.diagnostics.emplace_back(std::move(diag));
        result.patches.emplace_back(std::move(patch));
    }

    for (const ExPolygon &cand : candidate) {
        double     covered = 0.;
        int        owner   = -1;
        const auto cand_bb = get_extents(cand);
        for (const ConformalPatch &patch : result.patches)
            if (get_extents(patch.footprint).overlap(cand_bb))
                if (const double a = area(intersection_ex(patch.footprint, ExPolygons{ cand })); a > 0.) {
                    covered += a;
                    if (owner < 0)
                        owner = patch.id;
                }
        const double fallback = std::max(0., cand.area() - covered) * SCALING_FACTOR * SCALING_FACTOR;
        if (owner >= 0)
            result.diagnostics[size_t(owner)].fallback_area += fallback;
        else if (fallback > 0.) {
            PatchDiagnostics diag;
            diag.fallback_area = fallback;
            diag.limiting_component = most_limiting_component(head, limiting_components, rasterize({ cand }, grid));
            result.diagnostics.emplace_back(std::move(diag));
        }
    }
    if (result.patches.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Non-planar: no curved top patch in " << object.model_object()->name;
        return result;
    }

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
            const std::vector<ShellFill> fills = plan_shell_fills(patch, region.config(), line_width);
            std::vector<ShellFill> banded_fills = fills;
            BandedFill             banded;
            if (settings.banded && region.config().nonplanar_top_pattern.value == NonplanarTopPattern::Contour) {
                double angle = 0.;
                for (size_t k = 0; k < banded_fills.size(); ++k)
                    banded_fills[k] = ShellFill();
                if (contour_line_angle(patch.surface, angle, 0.9))
                    for (ShellFill &f : banded_fills)
                        f.angle = angle;
                else if (outline_is_contour(patch, h_c))
                    for (size_t k = 0; k < banded_fills.size(); ++k) {
                        banded_fills[k].spiral = true;
                        banded_fills[k].outward = (banded_fills.size() - 1 - k) % 2 == 1;
                    }
                else
                    banded.strips = true;
            }
            if (settings.shells > settings.top_shells) {
                banded.layers  = &stack_layers[idx];
                banded.sparse  = settings.sparse;
                banded.density = settings.sparse_density;
                banded.pattern = settings.sparse_pattern;
                banded.angle   = settings.sparse_angle;
            }
            const SeamPosition          seam_position = object.config().seam_position.value;
            patch.shell_paths.reserve(patch.shells);
            const Point                 seam          = shell_seam(area, patch.surface, seam_position, unsigned(patch.id), &paint);
            seams[idx]                                = seam;
            seam_lines[idx]                           = seam_line(patch.surface, seam, area);
            fill_areas[idx]                           = area;
            std::optional<Point3>       from;
            for (int k = 1; k <= patch.shells; ++k) {
                throw_if_canceled(object);
                ShellFill fill = fills[size_t(k - 1)];
                if (settings.banded)
                    fill = banded_fills[size_t(k - 1)];
                const Point start = fill.spiral || !from ? seam : from->to_point();
                patch.shell_paths.emplace_back(make_shell(object, patch, planar_tops[idx], area, k, fill, start, from ? &*from : nullptr, host_z,
                                                          pitch, seam_lines[idx], settings.banded ? &banded : nullptr));
                const ExtrusionEntitiesPtr &entities = patch.shell_paths.back().entities;
                from.reset();
                if (!entities.empty() && !settings.banded)
                    from = static_cast<const ExtrusionPathConformal *>(entities.back())->polyline.points.back();
            }
        }
    });

    if (const TopColours colours = top_colours(object, candidate); colours.areas.size() > 1) {
        split_by_colour(result, planar_tops, stack_layers, shell_areas, fill_areas, seams, seam_lines, colours);
        BOOST_LOG_TRIVIAL(info) << "Non-planar: " << colours.areas.size() << " top colours in " << object.model_object()->name;
    }

    std::vector<ConformalPatch> component_patches;
    if (settings.banded) {
        double pitch = 0., tolerance = 0.;
        const double max_patch_height = multi_material_patch_height(object, head, settings.max_patch_height);
        band_schedule(head, h_c, max_patch_height, pitch, tolerance);
        const BandSeams band_seams{ fill_areas, seams, seam_lines, line_width, MaxGlide, spans_wall,
                                    settings.shells - settings.top_shells, &stack_layers };
        component_patches = split_into_bands(result, S, layer_z, pitch, tolerance, &band_seams,
                                             max_patch_height != settings.max_patch_height);
    }
    std::vector<ConformalPatch> &units = settings.banded ? component_patches : result.patches;

    for (const ConformalPatch &patch : result.patches)
        layers[patch.host_layer]->conformal_patches_after.emplace_back(patch.id);
    for (const ConformalPatch &patch : units)
        for (size_t i = 0; i < layers.size(); ++i)
            if (layer_z[i] > patch.z_min - EPSILON && i <= patch.host_layer)
                layers[i]->conformal_covered = true;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            throw_if_canceled(object);
            ExPolygons excl;
            for (size_t idx = 0; idx < units.size(); ++idx) {
                const ConformalPatch &patch = units[idx];
                if (i > patch.host_layer || layer_z[i] <= patch.z_min - EPSILON)
                    continue;
                const HeightField &B = planar_tops[idx];
                Mask               above(B.data().size(), 0);
                bool               any = false;
                for (size_t n = 0; n < above.size(); ++n)
                    if (!std::isnan(B.data()[n]) && layer_z[i] > B.data()[n] + EPSILON) {
                        above[n] = 1;
                        any      = true;
                    }
                if (any)
                    append(excl, intersection_ex(mask_to_expolygons(above, B), shell_areas[idx]));
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

    BOOST_LOG_TRIVIAL(info) << "Non-planar: " << result.patches.size() << " curved top patch(es) in " << object.model_object()->name
                            << ", " << grid.nx() << "x" << grid.ny() << " nodes, "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() << " s";
    return result;
}

}
