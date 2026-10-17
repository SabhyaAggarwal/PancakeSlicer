#include "CurvedBridge.hpp"

#include "AutoMode.hpp"
#include "ConformalShells.hpp"
#include "FlowMod.hpp"
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
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double SagRatio = 1. / 80.;
constexpr double MinSpan = 4.;
constexpr double MaxSpan = 45.;
constexpr double MinWidth = 1.;
constexpr double AnchorReach = 1.5;
constexpr double MinSag = 0.05;
constexpr double FloorGap = 0.05;
constexpr int MaxStack = 16;

double unit_catenary(double ratio, double t)
{
    double a = 1. / (8. * ratio);
    for (int it = 0; it < 30; ++it) {
        const double u = 0.5 / a, f = a * (std::cosh(u) - 1.) - ratio, df = std::cosh(u) - 1. - u * std::sinh(u);
        if (std::abs(f) < 1e-12 || df == 0.)
            break;
        a = std::max(a - f / df, 1e-3);
    }
    return a * (std::cosh(0.5 / a) - std::cosh((t - 0.5) / a));
}

void take_paths(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &bbox, ExtrusionPaths &out)
{
    ExtrusionEntitiesPtr keep;
    keep.reserve(collection.entities.size());
    for (ExtrusionEntity *ee : collection.entities) {
        if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            take_paths(*sub, area, bbox, out);
            if (sub->entities.empty())
                delete ee;
            else
                keep.emplace_back(ee);
            continue;
        }
        const ExtrusionPaths *paths = nullptr;
        ExtrusionPaths        single;
        if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee))
            paths = &loop->paths;
        else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(ee))
            paths = &multi->paths;
        else if (auto *path = dynamic_cast<ExtrusionPath *>(ee); path != nullptr && dynamic_cast<ExtrusionPathConformal *>(ee) == nullptr) {
            single.emplace_back(*path);
            paths = &single;
        }
        if (paths == nullptr || ee->role() == erIroning || !get_extents(ee->as_polylines()).overlap(bbox) ||
            intersection_pl(ee->as_polylines(), area).empty()) {
            keep.emplace_back(ee);
            continue;
        }
        append(out, *paths);
        delete ee;
    }
    collection.entities = std::move(keep);
}

}

void build_curved_bridges(PrintObject &object, ConformalResult &result)
{
    LayerPtrs &layers = object.layers();
    int        region_id = -1;
    for (size_t r = 0; r < object.num_printing_regions(); ++r)
        if (bridges_enabled(object.printing_region(r).config(), object.config())) {
            region_id = int(r);
            break;
        }
    if (region_id < 0 || layers.size() < 3 || object.config().enable_support.value)
        return;
    if (result.planar_exclusion.size() != layers.size())
        result.planar_exclusion.assign(layers.size(), ExPolygons());
    result.bridged.assign(layers.size(), ExPolygons());

    const PrintRegion &region = object.printing_region(size_t(region_id));
    const double       pitch  = std::max(0.05, object.config().nonplanar_surface_resolution.value);
    const double       nozzle = region.flow(object, frSolidInfill, object.config().layer_height.value).nozzle_diameter();
    const bool         flow_mod = region.config().nonplanar_flow_mod.value;
    const HeadEnvelope head = resolve_head_envelope(object.print()->config());
    const double max_sag = layer_changes_filament(object) ? head_band_height(head) : std::numeric_limits<double>::infinity();
    auto one_filament = [&object](size_t r) {
        const PrintRegionConfig &cfg = object.printing_region(r).config();
        const int                id  = cfg.outer_wall_filament_id.value;
        return cfg.inner_wall_filament_id.value == id && cfg.sparse_infill_filament_id.value == id &&
               cfg.internal_solid_filament_id.value == id && cfg.top_surface_filament_id.value == id &&
               cfg.bottom_surface_filament_id.value == id;
    };

    const HeightField grid    = object_grid(object, pitch);
    const size_t      n_nodes = grid.data().size();
    std::map<size_t, Mask> slice_masks;
    auto slice_mask = [&](size_t l) -> const Mask & {
        auto it = slice_masks.find(l);
        return it != slice_masks.end() ? it->second : slice_masks.emplace(l, rasterize(layers[l]->lslices, grid)).first->second;
    };
    HeightField material = grid;
    material.data().assign(n_nodes, -std::numeric_limits<float>::infinity());
    size_t swept = 0;
    std::vector<Mask> taken(layers.size());
    const double first_top = layers.front()->print_z;

    for (size_t i = 1; i + 1 < layers.size(); ++i) {
        throw_if_canceled(object);
        for (; swept < i; ++swept) {
            const Mask &m = slice_mask(swept);
            for (size_t n = 0; n < n_nodes; ++n)
                if (m[n])
                    material.data()[n] = std::max(material.data()[n], float(layers[swept]->print_z));
        }
        slice_masks.erase(slice_masks.begin(), slice_masks.lower_bound(i - 1));
        ExPolygons     bridge_area;
        ExtrusionPaths bridge_roads;
        for (LayerRegion *layerm : layers[i]->regions()) {
            if (!bridges_enabled(layerm->region().config(), object.config()))
                continue;
            for (const Surface &surface : layerm->slices.surfaces)
                if (surface.surface_type == stBottomBridge)
                    bridge_area.emplace_back(surface.expolygon);
            for (const Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stBottomBridge)
                    bridge_area.emplace_back(surface.expolygon);
            std::function<void(const ExtrusionEntityCollection &)> visit = [&](const ExtrusionEntityCollection &c) {
                for (const ExtrusionEntity *ee : c.entities)
                    if (const auto *sub = dynamic_cast<const ExtrusionEntityCollection *>(ee))
                        visit(*sub);
                    else if (const auto *path = dynamic_cast<const ExtrusionPath *>(ee); path != nullptr && path->role() == erBridgeInfill)
                        bridge_roads.emplace_back(*path);
            };
            visit(layerm->fills);
        }
        if (bridge_area.empty() || bridge_roads.empty())
            continue;
        const ExPolygons air = opening_ex(diff_ex(union_ex(bridge_area), layers[i - 1]->lslices), float(scale_(0.5 * MinWidth)));
        const Mask      &support = slice_mask(i - 1);

        for (const ExPolygon &component : air) {
            const ExPolygons a{ component };
            const Mask       in_a = rasterize(a, grid);
            std::vector<int> nodes;
            for (size_t n = 0; n < n_nodes; ++n)
                if (in_a[n])
                    nodes.emplace_back(int(n));
            if (nodes.empty() ||
                (!taken[i].empty() && std::any_of(nodes.begin(), nodes.end(), [&](int n) { return taken[i][size_t(n)] != 0; })))
                continue;

            double c2 = 0., s2 = 0., bead = 0.;
            for (const ExtrusionPath &road : bridge_roads) {
                const Points &pts = road.polyline.to_polyline().points;
                for (size_t k = 1; k < pts.size(); ++k) {
                    const Vec2d p = unscale(pts[k - 1]), q = unscale(pts[k]), mid = 0.5 * (p + q), d = q - p;
                    if (!component.contains(Point::new_scale(mid.x(), mid.y())))
                        continue;
                    const double len = d.norm(), phi = std::atan2(d.y(), d.x());
                    c2 += len * std::cos(2. * phi);
                    s2 += len * std::sin(2. * phi);
                    bead = std::max(bead, double(road.height));
                }
            }
            if (c2 == 0. && s2 == 0.)
                continue;
            const ExPolygons inner = offset_ex(a, -float(scale_(MinWidth)));
            const bool       cut   = std::any_of(bridge_roads.begin(), bridge_roads.end(), [&inner](const ExtrusionPath &road) {
                const Polyline pl = road.polyline.to_polyline();
                auto in = [&inner](const Point &pt) { return std::any_of(inner.begin(), inner.end(), [&pt](const ExPolygon &e) { return e.contains(pt); }); };
                return pl.points.size() >= 2 && (in(pl.first_point()) || in(pl.last_point()));
            });
            if (cut)
                continue;
            const double theta = 0.5 * std::atan2(s2, c2);
            const Vec2d  dir(std::cos(theta), std::sin(theta));

            auto node_at = [&grid](const Vec2d &p) {
                const Vec2d u = (p - grid.origin()) / grid.pitch();
                const int   ix = int(std::lround(u.x())), iy = int(std::lround(u.y()));
                return ix < 0 || iy < 0 || ix >= grid.nx() || iy >= grid.ny() ? -1 : iy * grid.nx() + ix;
            };
            auto march = [&](const Vec2d &p, const Vec2d &u, double &edge) {
                const double step = 0.5 * pitch;
                double       t    = step;
                for (; t <= MaxSpan + AnchorReach; t += step) {
                    const int n = node_at(p + t * u);
                    if (n < 0)
                        return false;
                    if (!in_a[size_t(n)])
                        break;
                }
                edge = t - 0.5 * step;
                for (; t <= edge + AnchorReach; t += step) {
                    const int n = node_at(p + t * u);
                    if (n < 0)
                        return false;
                    if (support[size_t(n)])
                        return true;
                }
                return false;
            };
            HeightField sag1 = grid;
            sag1.data().assign(n_nodes, 0.f);
            size_t anchored = 0;
            double s_max = 0., span_max = 0.;
            for (int n : nodes) {
                const Vec2d p = grid.node(n % grid.nx(), n / grid.nx());
                double      d1 = 0., d2 = 0.;
                if (!march(p, dir, d1) || !march(p, -dir, d2))
                    continue;
                ++anchored;
                if (support[size_t(n)])
                    continue;
                const double span = d1 + d2;
                if (span < MinSpan || span > MaxSpan)
                    continue;
                const float s = float(span * unit_catenary(SagRatio, d2 / span));
                sag1.data()[size_t(n)] = s;
                s_max    = std::max(s_max, double(s));
                span_max = std::max(span_max, span);
            }
            if (2 * anchored < nodes.size() || s_max < MinSag)
                continue;

            int n_avail = 0;
            for (; n_avail < MaxStack && i + size_t(n_avail) < layers.size(); ++n_avail) {
                const Mask &m      = slice_mask(i + size_t(n_avail));
                size_t      inside = 0;
                for (int n : nodes)
                    inside += m[size_t(n)] != 0;
                if (inside < nodes.size() * 49 / 50)
                    break;
            }
            const double g    = layers[i + 1]->height;
            const double dmax = std::max(0.02, FlowModMaxThickness * nozzle - g);
            if (n_avail < 2)
                continue;
            const int n_flat = std::clamp(int(std::ceil(s_max / dmax - 1e-9)), 1, n_avail - 1);
            bool owned = false;
            for (int j = 0; j <= n_flat && !owned; ++j)
                owned = area(intersection_ex(a, result.planar_exclusion[i + size_t(j)])) > 0.05 * area(a);
            if (owned)
                continue;
            double    k      = std::min({ 1., n_flat * dmax / s_max, max_sag / s_max });
            auto      fall   = [n_flat](int j) { return 1. - double(j) / n_flat; };

            const BoundingBox bb = get_extents(a);
            const int   pad = 2;
            int         ix0 = std::max(0, int(std::floor((unscale<double>(bb.min.x()) - grid.origin().x()) / pitch)) - pad);
            int         iy0 = std::max(0, int(std::floor((unscale<double>(bb.min.y()) - grid.origin().y()) / pitch)) - pad);
            int         ix1 = std::min(grid.nx() - 1, int(std::ceil((unscale<double>(bb.max.x()) - grid.origin().x()) / pitch)) + pad);
            int         iy1 = std::min(grid.ny() - 1, int(std::ceil((unscale<double>(bb.max.y()) - grid.origin().y()) / pitch)) + pad);
            HeightField like = crop(grid, ix0, iy0, ix1, iy1);
            like.data().assign(like.data().size(), 0.f);
            for (int n : nodes)
                if (const float s = sag1.data()[size_t(n)]; s > 0.f) {
                    const double room = layers[i]->print_z - bead - std::max(double(material.data()[size_t(n)]), first_top) - FloorGap;
                    k                 = std::min(k, std::max(0., room) / s);
                }
            for (int round = 0; round < 3 && k * s_max >= MinSag; ++round) {
                const double k_round = k;
                HeightField  below   = material;
                for (int j = 0; j <= n_flat; ++j) {
                    if (j > 0) {
                        const size_t l = i + size_t(j) - 1;
                        const Mask  &m = slice_mask(l);
                        for (size_t n = 0; n < n_nodes; ++n)
                            if (m[n])
                                below.data()[n] = std::max(below.data()[n], float(layers[l]->print_z - k_round * fall(j - 1) * sag1.data()[n]));
                    }
                    if (fall(j) <= 0.)
                        continue;
                    const HeightField safe = min_safe_tip_height(below, head, like, true);
                    for (int n : nodes) {
                        const float s = sag1.data()[size_t(n)];
                        if (s <= 0.f)
                            continue;
                        const int   ix = n % grid.nx() - ix0, iy = n / grid.nx() - iy0;
                        const float z  = safe.at(ix, iy);
                        if (std::isfinite(z))
                            k = std::min(k, std::max(0., layers[i + size_t(j)]->print_z - z) / (fall(j) * s));
                    }
                }
                if (k >= k_round - 1e-6)
                    break;
            }
            if (k * s_max < MinSag)
                continue;

            bool mixed = false;
            for (int j = 0; j <= n_flat && !mixed; ++j)
                for (const LayerRegion *layerm : layers[i + size_t(j)]->regions())
                    if (!one_filament(layerm->region().print_object_region_id()) && !intersection_ex(layerm->slices.surfaces, a).empty())
                        mixed = true;
            if (mixed)
                continue;
            const int first_patch = int(result.patches.size());
            for (int j = 0; j <= n_flat; ++j) {
                const size_t l      = i + size_t(j);
                const double host_z = layers[l]->print_z;
                HeightField surface = like;
                for (int iy = 0; iy < like.ny(); ++iy)
                    for (int ix = 0; ix < like.nx(); ++ix)
                        surface.at(ix, iy) = float(host_z - k * fall(j) * sag1.at(ix0 + ix, iy0 + iy));
                const double rise = j == 0 ? 0. : k * (fall(j - 1) - fall(j));
                for (LayerRegion *layerm : layers[l]->regions()) {
                    ExtrusionPaths taken_paths, fills;
                    take_paths(layerm->perimeters, a, bb, taken_paths);
                    take_paths(layerm->fills, a, bb, fills);
                    append(taken_paths, std::move(fills));
                    if (taken_paths.empty())
                        continue;
                    ConformalPatch patch;
                    patch.id           = int(result.patches.size());
                    patch.kind         = PatchKind::Bridge;
                    patch.footprint    = a;
                    patch.shell_height = layers[l]->height;
                    patch.region_id    = int(layerm->region().print_object_region_id());
                    patch.host_layer   = l;
                    patch.shells       = 1;
                    ExtrusionEntitiesPtr lifted;
                    for (const ExtrusionPath &path : taken_paths) {
                        const float h  = path.height;
                        LiftFn      at = [&](const Vec2d &p, double &z, float &bead_height) {
                            const float s = sag1.sample(p);
                            const double sag = std::isnan(s) ? 0. : double(s);
                            z           = host_z - k * fall(j) * sag;
                            bead_height = float(h + rise * sag);
                            return true;
                        };
                        lift_polylines(Polylines{ path.polyline.to_polyline() }, path, at, host_z, pitch, patch.id, 0, lifted);
                    }
                    if (j > 0 && flow_mod) {
                        FlowModParams params;
                        params.land_radius = head.tip_flat_radius;
                        params.max_height  = FlowModMaxThickness * nozzle;
                        params.smoothing   = FlowModSmoothing * nozzle;
                        params.resample    = pitch;
                        for (ExtrusionEntity *ee : lifted)
                            modulate_flow(*static_cast<ExtrusionPathConformal *>(ee), surface, nullptr, nullptr, params, result.flow_mod);
                    }
                    patch.z_min = std::numeric_limits<double>::infinity();
                    patch.z_max = -std::numeric_limits<double>::infinity();
                    for (const ExtrusionEntity *ee : lifted)
                        for (const Point3 &p : static_cast<const ExtrusionPathConformal *>(ee)->polyline.points) {
                            patch.z_min = std::min(patch.z_min, host_z + unscale<double>(p.z()));
                            patch.z_max = std::max(patch.z_max, host_z + unscale<double>(p.z()));
                        }
                    ExtrusionEntityCollection shell;
                    shell.entities = std::move(lifted);
                    shell.no_sort  = true;
                    patch.shell_paths.emplace_back(std::move(shell));
                    layers[l]->conformal_patches_before.emplace_back(patch.id);
                    layers[l]->conformal_covered = true;
                    append(result.planar_exclusion[l], a);
                    result.patches.emplace_back(std::move(patch));
                    if (taken[l].empty())
                        taken[l].assign(n_nodes, 0);
                    for (int n : nodes)
                        taken[l][size_t(n)] = 1;
                }
            }
            if (int(result.patches.size()) == first_patch)
                continue;
            append(result.bridged[i], a);
            PatchDiagnostics diag;
            diag.patch_id      = first_patch;
            diag.max_slope_deg = Geometry::rad2deg(std::atan(4. * k * s_max / std::max(span_max, MinSpan)));
            result.diagnostics.emplace_back(std::move(diag));
            BOOST_LOG_TRIVIAL(info) << "Non-planar: curved bridge in " << object.model_object()->name << " at Z " << layers[i]->print_z
                                    << ": " << area(a) * SCALING_FACTOR * SCALING_FACTOR << " mm2, span " << span_max << " mm, sag "
                                    << k * s_max << " mm (target " << s_max << "), flattened over " << n_flat << " layers";
        }
    }
}

}
