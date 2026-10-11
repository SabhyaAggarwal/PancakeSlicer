#include "ConformalBottom.hpp"

#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "PatchTools.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
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
#include <cmath>
#include <limits>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float Inf = std::numeric_limits<float>::infinity();

struct BottomSettings
{
    int    region_id    = -1;
    int    shells       = 0;
    double shell_height = 0.;
    double max_slope    = 0.;
    double max_error    = 0.;
    double min_area     = 0.;
    double pitch        = 0.2;
};

BottomSettings bottom_settings(const PrintObject &object)
{
    BottomSettings s;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        if (cfg.nonplanar_bottom_shells.value <= 0)
            continue;
        s.region_id    = int(region_id);
        s.shells       = cfg.nonplanar_bottom_shells.value;
        s.shell_height = cfg.nonplanar_shell_layer_height.value > 0. ? cfg.nonplanar_shell_layer_height.value :
                                                                        object.config().layer_height.value;
        s.max_slope    = Geometry::deg2rad(cfg.nonplanar_max_slope.value);
        s.max_error    = cfg.nonplanar_max_surface_error.value;
        s.min_area     = cfg.nonplanar_min_patch_area.value;
        s.pitch        = std::max(0.05, object.config().nonplanar_surface_resolution.value);
        break;
    }
    return s;
}

HeightField component_minorant(const HeightField &S, const Mask &mask, const HeadEnvelope &head, const std::function<void()> &cancel)
{
    int                    n_comp = 0;
    const std::vector<int> label  = label_components(mask, S.nx(), S.ny(), n_comp);
    struct Extent { int ix0, iy0, ix1, iy1; };
    std::vector<Extent> ext(size_t(n_comp), Extent{ S.nx(), S.ny(), -1, -1 });
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix)
            if (const int c = label[size_t(iy) * S.nx() + ix]; c >= 0) {
                Extent &e = ext[size_t(c)];
                e.ix0 = std::min(e.ix0, ix), e.iy0 = std::min(e.iy0, iy), e.ix1 = std::max(e.ix1, ix), e.iy1 = std::max(e.iy1, iy);
            }
    HeightField out(S.bbox(), S.pitch());
    tbb::parallel_for(tbb::blocked_range<int>(0, n_comp), [&](const tbb::blocked_range<int> &range) {
        for (int c = range.begin(); c < range.end(); ++c) {
            const Extent &e  = ext[size_t(c)];
            HeightField   in = crop(S, e.ix0, e.iy0, e.ix1, e.iy1);
            for (int iy = 0; iy < in.ny(); ++iy)
                for (int ix = 0; ix < in.nx(); ++ix)
                    if (label[size_t(e.iy0 + iy) * S.nx() + e.ix0 + ix] != c)
                        in.at(ix, iy) = HeightField::NoData;
            const HeightField s1 = head_lipschitz_minorant(in, head, 8, cancel);
            for (int iy = 0; iy < s1.ny(); ++iy)
                for (int ix = 0; ix < s1.nx(); ++ix)
                    if (label[size_t(e.iy0 + iy) * S.nx() + e.ix0 + ix] == c)
                        out.at(e.ix0 + ix, e.iy0 + iy) = s1.at(ix, iy);
        }
    });
    return out;
}

bool contains(const ExPolygons &area, const std::vector<BoundingBox> &bboxes, const Point &p)
{
    for (size_t i = 0; i < area.size(); ++i)
        if (bboxes[i].contains(p) && area[i].contains(p))
            return true;
    return false;
}

std::vector<BoundingBox> extents(const ExPolygons &area)
{
    std::vector<BoundingBox> out;
    out.reserve(area.size());
    for (const ExPolygon &expoly : area)
        out.emplace_back(get_extents(expoly.contour));
    return out;
}

}

HeightField sample_bottom_surface(const sla::IndexedMesh &mesh, const HeightField &ray_start, double z_offset,
                                  const std::function<void()> &throw_if_canceled)
{
    HeightField S = ray_start;
    tbb::parallel_for(tbb::blocked_range<int>(0, S.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy) {
            if (throw_if_canceled)
                throw_if_canceled();
            for (int ix = 0; ix < S.nx(); ++ix) {
                const float start = ray_start.at(ix, iy);
                if (std::isnan(start))
                    continue;
                const Vec2d p = S.node(ix, iy);
                S.at(ix, iy)  = HeightField::NoData;
                for (const Vec2d &q : { p, Vec2d(p + Vec2d(1e-4, 1e-4)) }) {
                    const auto hit = mesh.query_ray_hit(Vec3d(q.x(), q.y(), start + mesh.ground_level()), Vec3d(0., 0., 1.));
                    if (hit.is_hit() && hit.normal().normalized().z() < -0.05) {
                        S.at(ix, iy) = float(hit.position().z() - mesh.ground_level() + z_offset);
                        break;
                    }
                }
            }
        }
    });
    return S;
}

void build_conformal_bottom(PrintObject &object, ConformalResult &result)
{
    LayerPtrs &layers = object.layers();
    for (Layer *layer : layers)
        layer->conformal_patches_before.clear();
    const BottomSettings settings = bottom_settings(object);
    if (settings.shells <= 0 || layers.size() < 2)
        return;
    if (result.planar_exclusion.size() != layers.size())
        result.planar_exclusion.assign(layers.size(), ExPolygons());

    const double h_c   = settings.shell_height;
    const double h_min = 0.5 * h_c;
    const double gap   = object.config().support_top_z_distance.value;
    const double pitch = settings.pitch;
    auto         cancel = [&object]() { throw_if_canceled(object); };

    Polygons          silhouette;
    const HeightField grid      = object_grid(object, pitch, &silhouette);
    const size_t      n_nodes   = grid.data().size();
    HeightField       ray_start = grid;
    Polygons          bottoms;
    for (size_t i = 1; i < layers.size(); ++i) {
        ExPolygons layer_bottoms;
        for (const LayerRegion *layerm : layers[i]->regions())
            if (layerm->region().config().nonplanar_bottom_shells.value > 0)
                for (const Surface &surface : layerm->slices.surfaces)
                    if (surface.surface_type == stBottom || surface.surface_type == stBottomBridge)
                        layer_bottoms.emplace_back(surface.expolygon);
        if (layer_bottoms.empty())
            continue;
        const Mask  m     = rasterize(layer_bottoms, grid);
        const float start = float(layers[i - 1]->slice_z - layers[i - 1]->height);
        for (size_t n = 0; n < n_nodes; ++n)
            if (m[n] && !(ray_start.data()[n] <= start))
                ray_start.data()[n] = start;
        polygons_append(bottoms, to_polygons(layer_bottoms));
    }
    if (bottoms.empty())
        return;
    const float      seam      = float(scale_(0.01));
    const ExPolygons candidate = offset_ex(union_ex(offset(bottoms, seam)), -seam);
    const Mask       cand_mask = rasterize(candidate, grid);

    HeightField Sb;
    {
        double             ground_offset = 0.;
        const TriangleMesh mesh          = object.object_frame_mesh(ground_offset);
        sla::IndexedMesh   imesh(mesh);
        imesh.ground_level_offset(ground_offset);
        Sb = sample_bottom_surface(imesh, ray_start, object.slicing_parameters().object_print_z_min, cancel);
    }

    std::string        head_warning;
    const HeadEnvelope head = resolve_head_envelope(object.print()->config(), &head_warning);
    if (!head_warning.empty())
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: " << head_warning;

    HeightField S0 = Sb;
    for (size_t i = 0; i < n_nodes; ++i)
        if (!cand_mask[i])
            S0.data()[i] = HeightField::NoData;
    if (settings.max_slope > 0.)
        S0 = slope_cap_minorant(S0, settings.max_slope);
    Mask mask(n_nodes, 0);
    for (size_t i = 0; i < n_nodes; ++i)
        mask[i] = cand_mask[i] && !std::isnan(S0.data()[i]);
    drop_steep_nodes(S0, head, h_c, grid, mask);
    HeightField S1 = component_minorant(S0, mask, head, cancel);
    for (size_t i = 0; i < n_nodes; ++i)
        if (mask[i] && (std::isnan(S1.data()[i]) || Sb.data()[i] - S1.data()[i] > settings.max_error))
            mask[i] = 0;
    S1 = component_minorant(S0, mask, head, cancel);

    std::vector<double> layer_bottom(layers.size());
    for (size_t i = 0; i < layers.size(); ++i)
        layer_bottom[i] = layers[i]->print_z - layers[i]->height;
    std::vector<int> floor_layer(n_nodes, -1);
    for (size_t i = 0; i < n_nodes; ++i) {
        if (!mask[i] || std::isnan(S1.data()[i]))
            continue;
        const auto it = std::lower_bound(layer_bottom.begin(), layer_bottom.end(),
                                         S1.data()[i] + settings.shells * h_c + h_min - EPSILON);
        if (it == layer_bottom.end())
            mask[i] = 0;
        else
            floor_layer[i] = int(it - layer_bottom.begin());
    }
    throw_if_canceled(object);
    const PrintRegion &region     = object.printing_region(size_t(settings.region_id));
    const double       line_width = region.flow(object, frSolidInfill, h_c).width();
    const double       wall_width = std::max(0, region.config().wall_loops.value) *
                                region.flow(object, frPerimeter, object.config().layer_height.value).width();
    const ExPolygons   band_cand  = wall_width > 0. ?
                                        diff_ex(candidate, offset_ex(union_ex(silhouette), float(-scale_(wall_width)))) :
                                        ExPolygons();

    ExPolygons components = opening_ex(mask_to_expolygons(mask, grid), float(scale_(line_width)));
    components            = intersection_ex(offset_ex(expolygons_simplify(components, scale_(pitch)), -float(scale_(pitch))),
                                            candidate);
    const double band_relief = std::max(h_c, settings.shells * h_c + gap - settings.max_error);
    const size_t first_patch = result.patches.size();
    std::vector<HeightField> floors;
    double                   covered = 0.;
    for (const ExPolygon &component : components) {
        if (component.area() * SCALING_FACTOR * SCALING_FACTOR < settings.min_area)
            continue;
        const Mask cmask = rasterize({ component }, grid);
        int        ix0 = grid.nx(), iy0 = grid.ny(), ix1 = -1, iy1 = -1;
        float      zlo = Inf, zhi = -Inf;
        for (int iy = 0; iy < grid.ny(); ++iy)
            for (int ix = 0; ix < grid.nx(); ++ix)
                if (const size_t i = size_t(iy) * grid.nx() + ix; cmask[i] && floor_layer[i] >= 0) {
                    ix0 = std::min(ix0, ix), iy0 = std::min(iy0, iy), ix1 = std::max(ix1, ix), iy1 = std::max(iy1, iy);
                    zlo = std::min(zlo, S1.data()[i]), zhi = std::max(zhi, S1.data()[i]);
                }
        if (ix1 < 0 || zhi - zlo < 0.01)
            continue;
        const int pad = 3;
        ix0 = std::max(0, ix0 - pad), iy0 = std::max(0, iy0 - pad);
        ix1 = std::min(grid.nx() - 1, ix1 + pad), iy1 = std::min(grid.ny() - 1, iy1 + pad);
        HeightField surface = crop(S1, ix0, iy0, ix1, iy1);
        for (int iy = 0; iy < surface.ny(); ++iy)
            for (int ix = 0; ix < surface.nx(); ++ix)
                if (const size_t i = size_t(iy0 + iy) * grid.nx() + ix0 + ix; !cmask[i] || floor_layer[i] < 0)
                    surface.at(ix, iy) = HeightField::NoData;
        HeightField component_floor = surface;
        for (int iy = 0; iy < surface.ny(); ++iy)
            for (int ix = 0; ix < surface.nx(); ++ix)
                if (!std::isnan(surface.at(ix, iy)))
                    component_floor.at(ix, iy) = float(layer_bottom[size_t(floor_layer[size_t(iy0 + iy) * grid.nx() + ix0 + ix])]);
        HeightField padded = surface, padded_floor = component_floor;
        pad_lowest(padded, pad);
        pad_lowest(padded_floor, pad);

        const int    n_bands = std::max(1, int(std::ceil((zhi - zlo) / band_relief - 1e-6)));
        const double band_h  = (zhi - zlo) / n_bands;
        for (int band = 0; band < n_bands; ++band) {
            Mask   bmask(n_nodes, 0);
            size_t host = layers.size();
            for (int iy = iy0; iy <= iy1; ++iy)
                for (int ix = ix0; ix <= ix1; ++ix)
                    if (const size_t i = size_t(iy) * grid.nx() + ix; cmask[i] && floor_layer[i] >= 0 &&
                        std::min(n_bands - 1, int(std::floor((S1.data()[i] - zlo) / band_h))) == band) {
                        bmask[i] = 1;
                        host     = std::min(host, size_t(floor_layer[i]));
                    }
            ExPolygons footprint = intersection_ex(mask_to_expolygons(bmask, grid), ExPolygons{ component });
            if (host >= layers.size() || footprint.empty())
                continue;

            ConformalPatch patch;
            patch.id           = int(result.patches.size());
            patch.kind         = PatchKind::Bottom;
            patch.footprint    = std::move(footprint);
            patch.wall_band    = intersection_ex(patch.footprint, band_cand);
            patch.shells       = settings.shells;
            patch.shell_height = h_c;
            patch.region_id    = settings.region_id;
            patch.surface      = padded;
            patch.base         = padded_floor;
            patch.host_layer   = host;
            patch.z_min        = Inf;
            patch.z_max        = -Inf;

            PatchDiagnostics diag;
            diag.patch_id = patch.id;
            double err_sq = 0.;
            size_t err_n  = 0;
            for (int iy = 0; iy < surface.ny(); ++iy)
                for (int ix = 0; ix < surface.nx(); ++ix) {
                    const size_t i = size_t(iy0 + iy) * grid.nx() + ix0 + ix;
                    if (!bmask[i])
                        continue;
                    patch.z_min    = std::min(patch.z_min, double(surface.at(ix, iy)));
                    patch.z_max    = std::max(patch.z_max, double(component_floor.at(ix, iy)));
                    const double e = Sb.data()[i] - surface.at(ix, iy);
                    diag.max_surface_error = std::max(diag.max_surface_error, e);
                    err_sq += e * e;
                    ++err_n;
                    const Vec2d g = surface.gradient(surface.node(ix, iy));
                    if (!std::isnan(g.x()) && !std::isnan(g.y()))
                        diag.max_slope_deg = std::max(diag.max_slope_deg, Geometry::rad2deg(std::atan(g.norm())));
                }
            diag.rms_surface_error = err_n > 0 ? std::sqrt(err_sq / err_n) : 0.;
            covered += area(patch.footprint);
            floors.emplace_back(component_floor);
            result.diagnostics.emplace_back(std::move(diag));
            result.patches.emplace_back(std::move(patch));
        }
    }
    if (const double fallback = std::max(0., area(candidate) - covered) * SCALING_FACTOR * SCALING_FACTOR; fallback > 0.) {
        PatchDiagnostics diag;
        diag.fallback_area = fallback;
        result.diagnostics.emplace_back(std::move(diag));
    }
    if (result.patches.size() == first_patch) {
        BOOST_LOG_TRIVIAL(info) << "Non-planar: no curved bottom patch in " << object.model_object()->name;
        return;
    }

    std::vector<ExPolygons> shell_areas(result.patches.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(first_patch, result.patches.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t idx = range.begin(); idx < range.end(); ++idx) {
            ConformalPatch    &patch  = result.patches[idx];
            const double       host_z = layers[patch.host_layer]->print_z;
            const Flow         flow   = region.flow(object, frSolidInfill, h_c);
            shell_areas[idx]          = diff_ex(patch.footprint, patch.wall_band);
            const double     overlap = 0.01 * region.config().top_bottom_infill_wall_overlap.value * line_width;
            const ExPolygons area    = intersection_ex(offset_ex(shell_areas[idx], float(scale_(overlap))), patch.footprint);
            const Mask       inside  = rasterize(patch.footprint, patch.surface);
            Point            start;
            float            lowest = Inf;
            for (int iy = 0; iy < patch.surface.ny(); ++iy)
                for (int ix = 0; ix < patch.surface.nx(); ++ix)
                    if (const float z = patch.surface.at(ix, iy); inside[size_t(iy) * patch.surface.nx() + ix] && z < lowest) {
                        lowest        = z;
                        const Vec2d p = patch.surface.node(ix, iy);
                        start         = Point::new_scale(p.x(), p.y());
                    }
            const double base_angle = Geometry::deg2rad(region.config().solid_infill_direction.value);
            for (int k = 1; k <= patch.shells; ++k) {
                throw_if_canceled(object);
                const bool          outer = k == patch.shells;
                const ExtrusionRole role  = k == 1 ? erBottomSurface : erSolidInfill;
                const Polylines polylines = fill_solid(area, k == 1 ? ipMonotonic : ipRectilinear, flow.spacing(),
                                                       base_angle + (k - 1) * 0.5 * PI, k == 1, flow, host_z);
                const ExtrusionPath proto(role, flow.mm3_per_mm(), float(flow.width()), float(h_c));
                auto at = [&](const Vec2d &p, double &z, float &bead) {
                    const float s = patch.surface.sample(p);
                    if (std::isnan(s))
                        return false;
                    z    = s + k * h_c;
                    bead = float(h_c);
                    if (const float f = patch.base.sample(p); outer && !std::isnan(f)) {
                        z    = std::max(z, double(f));
                        bead = float(z - (s + (k - 1) * h_c));
                    }
                    return true;
                };
                ExtrusionEntityCollection shell;
                lift_polylines(polylines, proto, at, host_z, pitch, patch.id, k - 1, shell.entities);
                chain_and_reorder_extrusion_entities(shell.entities, &start);
                shell.no_sort = true;
                patch.shell_paths.emplace_back(std::move(shell));
            }
        }
    });

    for (size_t idx = first_patch; idx < result.patches.size(); ++idx) {
        const ConformalPatch &patch = result.patches[idx];
        layers[patch.host_layer]->conformal_patches_before.emplace_back(patch.id);
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i]->print_z > patch.z_min + EPSILON && layer_bottom[i] < patch.z_max - EPSILON)
                layers[i]->conformal_covered = true;
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            throw_if_canceled(object);
            ExPolygons excl;
            for (size_t idx = first_patch; idx < result.patches.size(); ++idx) {
                const ConformalPatch &patch = result.patches[idx];
                if (layers[i]->print_z <= patch.z_min + EPSILON || layer_bottom[i] >= patch.z_max - EPSILON)
                    continue;
                const HeightField &F = floors[idx - first_patch];
                Mask               below(F.data().size(), 0);
                bool               any = false;
                for (size_t n = 0; n < below.size(); ++n)
                    if (!std::isnan(F.data()[n]) && layer_bottom[i] < F.data()[n] - EPSILON) {
                        below[n] = 1;
                        any      = true;
                    }
                if (any)
                    append(excl, intersection_ex(mask_to_expolygons(below, F), shell_areas[idx]));
            }
            if (excl.empty())
                continue;
            excl                        = union_ex(excl);
            const BoundingBox excl_bbox = get_extents(excl);
            for (LayerRegion *layerm : layers[i]->regions()) {
                clip_extrusions(layerm->fills, excl, excl_bbox);
                clip_extrusions(layerm->perimeters, excl, excl_bbox);
            }
            append(result.planar_exclusion[i], std::move(excl));
            result.planar_exclusion[i] = union_ex(result.planar_exclusion[i]);
        }
    });

    BOOST_LOG_TRIVIAL(info) << "Non-planar: " << result.patches.size() - first_patch << " curved bottom patch(es) in "
                            << object.model_object()->name;
}

void contour_support_interface(PrintObject &object)
{
    const ConformalResult &conformal = object.conformal();
    std::vector<const ConformalPatch *> patches;
    double                              z_lo = Inf, z_hi = -Inf;
    for (const ConformalPatch &patch : conformal.patches)
        if (patch.kind == PatchKind::Bottom) {
            patches.emplace_back(&patch);
            z_lo = std::min(z_lo, patch.z_min);
            z_hi = std::max(z_hi, patch.z_max);
        }
    SupportLayerPtrs &support_layers = object.support_layers();
    if (patches.empty() || support_layers.empty())
        return;
    const double gap = object.config().support_top_z_distance.value;
    std::vector<std::vector<BoundingBox>> footprint_bboxes;
    for (const ConformalPatch *patch : patches)
        footprint_bboxes.emplace_back(extents(patch->footprint));

    auto in_range = [&](const SupportLayer &layer) {
        return layer.print_z >= z_lo - gap - layer.height - EPSILON && layer.print_z <= z_hi - gap + layer.height + EPSILON;
    };
    std::vector<ExPolygons> covered_above(support_layers.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, support_layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t j = range.begin(); j < range.end(); ++j)
            if (in_range(*support_layers[j]) && j + 1 < support_layers.size())
                covered_above[j] = union_ex(support_layers[j + 1]->support_fills.polygons_covered_by_width(float(SCALED_EPSILON)));
    });
    tbb::parallel_for(tbb::blocked_range<size_t>(0, support_layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t j = range.begin(); j < range.end(); ++j) {
            throw_if_canceled(object);
            SupportLayer &layer = *support_layers[j];
            if (!in_range(layer))
                continue;
            const double                    z_s          = layer.print_z, h_s = layer.height;
            const ExPolygons               &above        = covered_above[j];
            const std::vector<BoundingBox>  above_bboxes = extents(above);

            auto lift = [&](const Vec2d &p) {
                const Point pt = Point::new_scale(p.x(), p.y());
                if (contains(above, above_bboxes, pt))
                    return 0.;
                for (size_t k = 0; k < patches.size(); ++k)
                    if (contains(patches[k]->footprint, footprint_bboxes[k], pt))
                        if (const float s = patches[k]->surface.sample(p); !std::isnan(s)) {
                            const double dz = s - gap - z_s;
                            return std::abs(dz) <= h_s ? dz : 0.;
                        }
                return 0.;
            };
            auto contour_path = [&](ExtrusionPath &path) {
                if ((path.role() != erSupportMaterialInterface && path.role() != erSupportMaterial) || path.z_contoured ||
                    path.polyline.points.size() < 2)
                    return;
                const double resample = patches.front()->surface.pitch();
                Points3      out;
                bool         lifted = false;
                const Points3 &pts  = path.polyline.points;
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    const Vec2d a = unscale(pts[i].to_point()), b = unscale(pts[i + 1].to_point());
                    const int   n = std::max(1, int(std::ceil((b - a).norm() / resample)));
                    for (int step = i == 0 ? 0 : 1; step <= n; ++step) {
                        const Vec2d  p  = a + (b - a) * (double(step) / n);
                        const double dz = lift(p);
                        lifted |= std::abs(dz) > EPSILON;
                        out.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(dz)));
                    }
                }
                if (!lifted)
                    return;
                path.polyline    = Polyline3(std::move(out));
                path.z_contoured = true;
            };
            std::function<void(ExtrusionEntityCollection &)> visit = [&](ExtrusionEntityCollection &collection) {
                for (ExtrusionEntity *ee : collection.entities) {
                    if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee))
                        visit(*sub);
                    else if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee))
                        for (ExtrusionPath &path : loop->paths)
                            contour_path(path);
                    else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(ee))
                        for (ExtrusionPath &path : multi->paths)
                            contour_path(path);
                    else if (auto *path = dynamic_cast<ExtrusionPath *>(ee); path != nullptr && typeid(*path) == typeid(ExtrusionPath))
                        contour_path(*path);
                }
            };
            visit(layer.support_fills);
        }
    });
}

int patch_filament_id(const PrintObject &object, const ConformalPatch &patch)
{
    const PrintRegionConfig &cfg = object.printing_region(size_t(patch.region_id)).config();
    return patch.kind == PatchKind::Top ? cfg.top_surface_filament_id.value : cfg.bottom_surface_filament_id.value;
}

}
