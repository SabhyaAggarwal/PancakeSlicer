#include "ConformalShells.hpp"

#include "HeadEnvelope.hpp"
#include "PatchTools.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
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
#include <limits>
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
    double               shell_height = 0.;
    double               max_slope = 0.;
    double               max_error = 0.;
    double               min_area = 0.;
    double               pitch = 0.2;
    double               max_patch_height = 0.;
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

ExtrusionEntityCollection make_shell(const PrintObject &object, const ConformalPatch &patch, const HeightField &top_of_planar,
                                     const ExPolygons &area, int k, double line_angle, double host_z, double resample)
{
    const PrintRegion        &region  = object.printing_region(size_t(patch.region_id));
    const bool                outer   = k == patch.shells;
    const Flow                flow    = region.flow(object, outer ? frTopSolidInfill : frSolidInfill, patch.shell_height);
    const ExtrusionRole       role    = outer ? erTopSolidInfill : erSolidInfill;
    const NonplanarTopPattern pattern = region.config().nonplanar_top_pattern.value;
    const Polylines polylines = fill_solid(area, pattern == NonplanarTopPattern::Concentric ? ipConcentric : outer ? ipMonotonic : ipRectilinear,
                                           flow.spacing(), line_angle + (patch.shells - k) * 0.5 * PI, outer, flow, host_z);

    const ExtrusionPath proto(role, flow.mm3_per_mm(), float(flow.width()), float(patch.shell_height));
    const double        drop = (patch.shells - k) * patch.shell_height;
    auto at = [&](const Vec2d &p, double &z, float &bead) {
        const float s = patch.surface.sample(p);
        if (std::isnan(s))
            return false;
        z    = s - drop;
        bead = float(patch.shell_height);
        if (k == 1) {
            const float floor_z = top_of_planar.sample(p);
            if (std::isnan(floor_z))
                return false;
            bead = float(z - floor_z);
        }
        return true;
    };
    ExtrusionEntityCollection out;
    lift_polylines(polylines, proto, at, host_z, resample, patch.id, k - 1, out.entities);
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
                const Vec2d p   = S.node(ix, iy);
                const auto  hit = mesh.query_ray_hit(Vec3d(p.x(), p.y(), ray_z), Vec3d(0., 0., -1.));
                S.at(ix, iy) = hit.is_hit() && hit.normal().normalized().z() > 0.05 ?
                                   float(hit.position().z() - mesh.ground_level() + z_offset) : HeightField::NoData;
            }
        }
    });
    return S;
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
    HeightField S;
    {
        double             ground_offset = 0.;
        const TriangleMesh mesh          = object.object_frame_mesh(ground_offset);
        sla::IndexedMesh   imesh(mesh);
        imesh.ground_level_offset(ground_offset);
        S = sample_top_surface(imesh, mesh.bounding_box().max.z() + 1., object.slicing_parameters().object_print_z_min, grid, cancel);
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
    drop_steep_nodes(S0, head, h_c, grid, mask);

    HeadEnvelope neighbour_head = head;
    {
        std::vector<HeadBox> boxes;
        for (HeadBox box : head.boxes) {
            const double r = std::hypot(std::max(std::abs(box.min.x()), std::abs(box.max.x())),
                                        std::max(std::abs(box.min.y()), std::abs(box.max.y())));
            if (box.nozzle && r <= head.tip_allowance.radius)
                box.min.z() = std::max(box.min.z(), head.tip_allowance.height);
            if (box.min.z() < box.max.z())
                boxes.emplace_back(std::move(box));
        }
        neighbour_head.boxes = std::move(boxes);
    }

    HeightField S1 = grid;
    bool        converged = false;
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
                const HeightField z_safe = min_safe_tip_height(H, neighbour_head, like);
                for (int iy = 0; iy < like.ny(); ++iy)
                    for (int ix = 0; ix < like.nx(); ++ix) {
                        const size_t i = size_t(e.iy0 + iy) * grid.nx() + e.ix0 + ix;
                        if (label[i] == c && mask[i] && !band_mask[i] && like.at(ix, iy) < z_safe.at(ix, iy) - settings.max_error)
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
    components            = intersection_ex(offset_ex(expolygons_simplify(components, scale_(pitch)), -float(scale_(pitch))),
                                            candidate);
    {
        ExPolygons kept;
        for (ExPolygon &c : components)
            if (c.area() * SCALING_FACTOR * SCALING_FACTOR >= settings.min_area)
                kept.emplace_back(std::move(c));
        components = std::move(kept);
    }

    if (settings.max_patch_height > 0.) {
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
    std::vector<ExPolygons>  shell_areas;
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
        diag.rms_surface_error = err_n > 0 ? std::sqrt(err_sq / err_n) : 0.;
        pad_lowest(patch.surface, pad);
        pad_lowest(patch.base, pad);
        bool printable = true;
        for (size_t i = 0; i < top.data().size(); ++i) {
            const double shell_z = patch.surface.data()[i] - (settings.shells - 1) * h_c;
            if (std::isnan(shell_z))
                continue;
            const int layer = planar_transition_layer(layer_z, shell_z, h_c, h_min, h_max, PatchKind::Top);
            if (layer < 0) {
                printable = false;
                break;
            }
            top.data()[i] = float(layer_z[size_t(layer)]);
            patch.z_min = std::min(patch.z_min, layer_z[size_t(layer)]);
        }
        if (!printable)
            continue;

        patch.host_layer = size_t(std::lower_bound(layer_z.begin(), layer_z.end(), patch.z_max - EPSILON) - layer_z.begin());
        if (patch.host_layer >= layers.size())
            patch.host_layer = layers.size() - 1;

        planar_tops.emplace_back(std::move(top));
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
            result.diagnostics.emplace_back(std::move(diag));
        }
    }
    if (result.patches.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Non-planar: no curved top patch in " << object.model_object()->name;
        return result;
    }

    tbb::parallel_for(tbb::blocked_range<size_t>(0, result.patches.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t idx = range.begin(); idx < range.end(); ++idx) {
            ConformalPatch &patch = result.patches[idx];
            const double    host_z = layers[patch.host_layer]->print_z;
            const double    line_angle = top_shell_angle(patch, region.config());
            const double overlap = 0.01 * region.config().top_bottom_infill_wall_overlap.value * line_width;
            const ExPolygons area = intersection_ex(offset_ex(shell_areas[idx], float(scale_(overlap))), patch.footprint);
            Point start;
            float lowest = Inf;
            for (int iy = 0; iy < patch.base.ny(); ++iy)
                for (int ix = 0; ix < patch.base.nx(); ++ix)
                    if (const float b = patch.base.at(ix, iy); !std::isnan(b) && b < lowest) {
                        lowest = b;
                        const Vec2d p = patch.base.node(ix, iy);
                        start         = Point::new_scale(p.x(), p.y());
                    }
            patch.shell_paths.reserve(patch.shells);
            for (int k = 1; k <= patch.shells; ++k) {
                throw_if_canceled(object);
                ExtrusionEntityCollection shell = make_shell(object, patch, planar_tops[idx], area, k, line_angle, host_z, pitch);
                chain_and_reorder_extrusion_entities(shell.entities, &start);
                shell.no_sort = true;
                patch.shell_paths.emplace_back(std::move(shell));
            }
        }
    });

    for (const ConformalPatch &patch : result.patches) {
        layers[patch.host_layer]->conformal_patches_after.emplace_back(patch.id);
        for (size_t i = 0; i < layers.size(); ++i)
            if (layer_z[i] > patch.z_min - EPSILON && i <= patch.host_layer)
                layers[i]->conformal_covered = true;
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            throw_if_canceled(object);
            ExPolygons excl;
            for (size_t idx = 0; idx < result.patches.size(); ++idx) {
                const ConformalPatch &patch = result.patches[idx];
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
