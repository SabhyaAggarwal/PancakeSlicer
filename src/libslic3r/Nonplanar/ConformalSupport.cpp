#include "ConformalSupport.hpp"

#include "AutoMode.hpp"
#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "PatchTools.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Polygon.hpp"
#include "ShellPaths.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fill/FillBase.hpp"
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
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float Inf = std::numeric_limits<float>::infinity();

ExPolygons support_area(const SupportLayer &layer, double close)
{
    ExPolygons area = layer.support_islands;
    append(area, layer.base_areas);
    append(area, closing_ex(layer.support_fills.polygons_covered_by_width(float(SCALED_EPSILON)), float(scale_(close))));
    return union_ex(area);
}

int nearest_node(const HeightField &grid, const Vec2d &p)
{
    const Vec2d o  = (p - grid.origin()) / grid.pitch();
    const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
    return ix < 0 || iy < 0 || ix >= grid.nx() || iy >= grid.ny() ? -1 : iy * grid.nx() + ix;
}

HeightField masked(const HeightField &field, const Mask &mask, int pad)
{
    HeightField out = field;
    for (size_t n = 0; n < mask.size(); ++n)
        if (!mask[n])
            out.data()[n] = HeightField::NoData;
    pad_lowest(out, pad);
    return out;
}

}

bool conformal_support_configured(const PrintObject &object)
{
    return object.config().nonplanar_mode.value != NonplanarMode::Off && object.config().nonplanar_support_interface.value &&
           !object.print()->config().nonplanar_head.value.empty();
}

bool conformal_support_wanted(const PrintObject &object)
{
    return conformal_support_configured(object) && !object.support_layers().empty() && object.layers().size() > 1;
}

void build_conformal_support(PrintObject &object, ConformalResult &result)
{
    LayerPtrs        &layers   = object.layers();
    SupportLayerPtrs &supports = object.support_layers();
    if (layers.size() < 2 || supports.empty())
        return;
    if (result.planar_exclusion.size() != layers.size())
        result.planar_exclusion.assign(layers.size(), ExPolygons());

    const PrintObjectConfig &ocfg  = object.config();
    const PrintRegion       &region0 = object.printing_region(0);
    const double h      = ocfg.layer_height.value;
    const double h_min  = 0.5 * h;
    const double h_max  = 0.8 * region0.flow(object, frSolidInfill, h).nozzle_diameter();
    const double gap    = ocfg.support_top_z_distance.value;
    const double pitch  = std::max(0.05, ocfg.nonplanar_surface_resolution.value);
    if (h_min + h > h_max + EPSILON)
        return;
    const HeadEnvelope head  = resolve_head_envelope(object.print()->config());
    const double       reach = head_band_height(head);
    auto cancel = [&object]() { throw_if_canceled(object); };

    Polygons          silhouette;
    const HeightField grid    = object_grid(object, pitch, &silhouette);
    const size_t      n_nodes = grid.data().size();

    std::vector<int> top(n_nodes, -1), run(n_nodes, -1);
    std::vector<double> support_z(supports.size());
    for (size_t j = 0; j < supports.size(); ++j)
        support_z[j] = supports[j]->print_z;
    const int batch = 32;
    for (int hi = int(supports.size()) - 1; hi >= 0; hi -= batch) {
        const int         lo = std::max(0, hi - batch + 1);
        std::vector<Mask> cover(size_t(hi - lo + 1));
        tbb::parallel_for(tbb::blocked_range<int>(lo, hi + 1), [&](const tbb::blocked_range<int> &range) {
            for (int j = range.begin(); j < range.end(); ++j)
                cover[size_t(j - lo)] = rasterize(support_area(*supports[size_t(j)], 1.), grid);
        });
        for (int j = hi; j >= lo; --j) {
            const Mask &m = cover[size_t(j - lo)];
            for (size_t n = 0; n < n_nodes; ++n)
                if (m[n]) {
                    if (top[n] < 0)
                        top[n] = run[n] = j;
                    else if (run[n] == j + 1)
                        run[n] = j;
                }
        }
        cancel();
    }

    HeightField Sb;
    {
        const double z_min     = object.slicing_parameters().object_print_z_min;
        HeightField  ray_start = grid;
        for (size_t n = 0; n < n_nodes; ++n)
            if (top[n] >= 0)
                ray_start.data()[n] = float(support_z[size_t(top[n])] - z_min);
        double             ground_offset = 0.;
        const TriangleMesh mesh          = object.object_frame_mesh(ground_offset);
        sla::IndexedMesh   imesh(mesh);
        imesh.ground_level_offset(ground_offset);
        Sb = sample_bottom_surface(imesh, ray_start, z_min, cancel);
    }

    constexpr int    OverhangOwner = -2;
    std::vector<int> owner(n_nodes, -1);
    Mask             taken(n_nodes, 0);
    std::vector<const ConformalPatch *> skin(n_nodes, nullptr);
    std::vector<const ConformalPatch *> owners;
    for (const ConformalPatch &patch : result.patches)
        if (patch.kind == PatchKind::Bottom)
            owners.emplace_back(&patch);
    for (const ConformalPatch &patch : result.patches)
        if (patch.kind != PatchKind::Bottom && patch.kind != PatchKind::Top)
            owners.emplace_back(&patch);
    for (const ConformalPatch *owner_patch : owners) {
        const ConformalPatch &patch = *owner_patch;
        const HeightField    &under = patch.kind == PatchKind::Volume ? patch.base : patch.surface;
        const Mask m = rasterize(patch.footprint, grid);
        for (size_t n = 0; n < n_nodes; ++n)
            if (m[n] && !taken[n]) {
                if (const float z = Sb.data()[n]; !std::isnan(z)) {
                    const float own = under.sample(grid.node(int(n % grid.nx()), int(n / grid.nx())));
                    if (!std::isnan(own) && std::abs(own - z) > 1.f)
                        continue;
                }
                taken[n] = 1;
                if (patch.kind == PatchKind::Bottom)
                    owner[n] = patch.id;
                else if (patch.kind == PatchKind::Overhang) {
                    owner[n] = OverhangOwner;
                    skin[n]  = &patch;
                }
            }
    }
    for (size_t n = 0; n < n_nodes; ++n)
        if (owner[n] != -1) {
            const ConformalPatch &patch = owner[n] >= 0 ? result.patches[size_t(owner[n])] : *skin[n];
            Sb.data()[n]                = top[n] >= 0 ? patch.surface.sample(grid.node(int(n % grid.nx()), int(n / grid.nx()))) : HeightField::NoData;
        }
    Mask cand(n_nodes, 0);
    for (size_t n = 0; n < n_nodes; ++n)
        if (top[n] >= 0 && !std::isnan(Sb.data()[n]) && (owner[n] != -1 || !taken[n])) {
            const double d = Sb.data()[n] - support_z[size_t(top[n])];
            cand[n]        = d >= gap - h - EPSILON && d <= gap + 2. * h + EPSILON;
        }
    if (ocfg.nonplanar_mode.value == NonplanarMode::Auto) {
        const double limit = auto_curved_underside_slope();
        for (size_t n = 0; n < n_nodes; ++n)
            if (cand[n] && owner[n] == -1) {
                const Vec2d gr = Sb.gradient(grid.node(int(n % grid.nx()), int(n / grid.nx())));
                if (!std::isnan(gr.x()) && !std::isnan(gr.y()) && gr.norm() > limit)
                    cand[n] = 0;
            }
    }

    const int cfg_layers = ocfg.support_interface_top_layers.value;
    double    stack      = h_max + h;
    for (const ConformalPatch &patch : result.patches)
        if (patch.kind == PatchKind::Bottom)
            stack = std::max(stack, (patch.shells - 1) * patch.shell_height + h_max + h);
    const int n_i = std::clamp(1 + int(std::floor((reach - stack - gap) / h + 1e-6)), 1, std::clamp(cfg_layers, 1, 2));

    std::vector<int> keep(n_nodes, -1);
    for (size_t n = 0; n < n_nodes; ++n) {
        if (!cand[n])
            continue;
        const double u1 = Sb.data()[n] - gap - (n_i - 1) * h;
        const auto   it = std::upper_bound(support_z.begin() + run[n], support_z.begin() + top[n] + 1, u1 - h_min + EPSILON);
        const int    j  = int(it - support_z.begin()) - 1;
        if (j < run[n] || u1 - support_z[size_t(j)] > h_max + EPSILON)
            cand[n] = 0;
        else
            keep[n] = j;
    }

    std::vector<double> layer_bottom(layers.size());
    for (size_t i = 0; i < layers.size(); ++i)
        layer_bottom[i] = layers[i]->print_z - layers[i]->height;
    std::vector<int> floor_layer(n_nodes, -1);
    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n] && owner[n] == -1) {
            floor_layer[n] = planar_transition_layer(layer_bottom, Sb.data()[n], h, h_min, h_max, PatchKind::Bottom);
            if (floor_layer[n] <= 0)
                cand[n] = 0;
        }

    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n] && owner[n] == OverhangOwner) {
            const auto it = std::lower_bound(layers.begin(), layers.end(), double(Sb.data()[n]) - EPSILON,
                                             [](const Layer *l, double z) { return l->print_z < z; });
            if (it == layers.begin() || it == layers.end())
                cand[n] = 0;
            else
                owner[n] = -3 - int(it - layers.begin());
        }

    const Flow iflow = cfg_layers > 0 ? support_material_interface_flow(&object, float(h)) : support_material_flow(&object, float(h));
    {
        const ExPolygons opened = opening_ex(mask_to_expolygons(cand, grid), float(scale_(iflow.width())));
        const Mask       kept   = rasterize(opened, grid);
        for (size_t n = 0; n < n_nodes; ++n)
            cand[n] = cand[n] && kept[n];
        int                    n_comp = 0;
        const std::vector<int> label  = label_components(cand, grid.nx(), grid.ny(), n_comp);
        std::vector<float>     zlo(size_t(n_comp), Inf), zhi(size_t(n_comp), -Inf);
        std::vector<char>      owned(size_t(n_comp), 0);
        for (size_t n = 0; n < n_nodes; ++n)
            if (const int c = label[n]; c >= 0) {
                zlo[size_t(c)] = std::min(zlo[size_t(c)], Sb.data()[n]);
                zhi[size_t(c)] = std::max(zhi[size_t(c)], Sb.data()[n]);
                owned[size_t(c)] = char(owned[size_t(c)] | (owner[n] != -1));
            }
        for (size_t n = 0; n < n_nodes; ++n)
            if (const int c = label[n]; c >= 0 && !owned[size_t(c)] && zhi[size_t(c)] - zlo[size_t(c)] < h_min)
                cand[n] = 0;
    }
    cancel();

    const double     line_width    = region0.flow(object, frSolidInfill, h).width();
    const double     wall_width    = float(std::max(0, region0.config().wall_loops.value)) * region0.flow(object, frPerimeter, h).width();
    const ExPolygons inner_outline = offset_ex(union_ex(silhouette), float(-scale_(wall_width)));
    {
        const Mask inside = rasterize(inner_outline, grid);
        for (size_t n = 0; n < n_nodes; ++n)
            if (cand[n] && owner[n] == -1 && !inside[n])
                cand[n] = 0;
    }

    std::map<int, Mask> floor_bands;
    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n] && owner[n] == -1) {
            Mask &m = floor_bands[floor_layer[n]];
            m.resize(n_nodes, 0);
            m[n] = 1;
        }

    HeightField      Fb            = grid;
    Mask             all_floor(n_nodes, 0);
    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n] && owner[n] == -1) {
            all_floor[n]  = 1;
            Fb.data()[n] = float(layer_bottom[size_t(floor_layer[n])]);
        }
    const HeightField floor_surface = masked(Sb, all_floor, 3);
    double angle  = Geometry::deg2rad(region0.config().solid_infill_direction.value);
    const bool rings = !contour_line_angle(masked(Sb, cand, 0), angle, 0.3);

    const size_t first_floor = result.patches.size();
    for (auto &[floor_id, mask] : floor_bands) {
        ConformalPatch patch;
        patch.id           = int(result.patches.size());
        patch.kind         = PatchKind::Bottom;
        patch.from_support = true;
        patch.footprint    = mask_to_expolygons(mask, grid);
        patch.wall_band    = diff_ex(patch.footprint, inner_outline);
        patch.shells       = 1;
        patch.shell_height = h;
        patch.region_id    = 0;
        patch.surface      = floor_surface;
        patch.base         = masked(Fb, mask, 0);
        patch.host_layer   = size_t(floor_id);
        patch.band         = floor_id;
        patch.z_min        = double(Inf);
        patch.z_max        = layer_bottom[size_t(floor_id)];
        for (size_t n = 0; n < n_nodes; ++n)
            if (mask[n]) {
                owner[n]    = patch.id;
                patch.z_min = std::min(patch.z_min, double(Sb.data()[n]));
            }
        const Point probe = patch.footprint.front().contour.points.front();
        for (const LayerRegion *layerm : layers[patch.host_layer]->regions())
            if (layerm->region().print_object_region_id() >= 0 && layerm->slices.any_bottom_contains(probe)) {
                patch.region_id = layerm->region().print_object_region_id();
                break;
            }
        result.patches.emplace_back(std::move(patch));
    }

    std::map<int, int> floor_patch;
    for (size_t idx = first_floor; idx < result.patches.size(); ++idx)
        floor_patch[int(result.patches[idx].host_layer)] = int(idx);
    auto owner_at = [&](const Vec2d &p) {
        const int n = nearest_node(grid, p);
        if (n < 0)
            return -1;
        const int o = owner[size_t(n)];
        if (o < int(first_floor) || !cand[size_t(n)])
            return o;
        const float s = floor_surface.sample(p);
        if (std::isnan(s))
            return o;
        const auto it = floor_patch.find(planar_transition_layer(layer_bottom, s, h, h_min, h_max, PatchKind::Bottom));
        return it == floor_patch.end() ? o : it->second;
    };
    auto in_band  = [&](int band, const Vec2d &p) {
        const double d = 0.25 * pitch;
        for (const Vec2d &q : { p, Vec2d(p + Vec2d(d, 0.)), Vec2d(p - Vec2d(d, 0.)), Vec2d(p + Vec2d(0., d)), Vec2d(p - Vec2d(0., d)) })
            if (owner_at(q) == band)
                return true;
        return false;
    };
    auto link = [&](int band, Polylines &&pieces, const Point &start, double max_link) {
        Polylines chained;
        {
            std::vector<char> used(pieces.size(), 0);
            Point             here = start;
            for (size_t taken = 0; taken < pieces.size(); ++taken) {
                size_t best = 0;
                bool   flip = false;
                double best_d = std::numeric_limits<double>::max();
                for (size_t i = 0; i < pieces.size(); ++i)
                    if (!used[i])
                        for (const bool f : { false, true }) {
                            const double d = ((f ? pieces[i].last_point() : pieces[i].first_point()) - here).cast<double>().squaredNorm();
                            if (d < best_d)
                                best_d = d, best = i, flip = f;
                        }
                used[best] = 1;
                if (flip)
                    pieces[best].reverse();
                here = pieces[best].last_point();
                chained.emplace_back(std::move(pieces[best]));
            }
        }
        std::vector<Polylines> out;
        for (Polyline &pl : chained) {
            if (!out.empty()) {
                const Vec2d  a = unscale(out.back().back().last_point()), b = unscale(pl.first_point());
                const double d = (b - a).norm();
                const bool   mid    = in_band(band, 0.5 * (a + b));
                const bool   inside = mid && in_band(band, a + 0.25 * (b - a)) && in_band(band, a + 0.75 * (b - a));
                if (d <= max_link && mid) {
                    Points &run = out.back().back().points;
                    run.insert(run.end(), pl.points.begin() + 1, pl.points.end());
                    continue;
                }
                if (d <= 5. && inside) {
                    out.back().emplace_back(Polyline(Points{ out.back().back().last_point(), pl.first_point() }));
                    out.back().emplace_back(std::move(pl));
                    continue;
                }
            }
            out.push_back(Polylines{ std::move(pl) });
        }
        return out;
    };
    auto band_start = [&](int band) {
        Point start;
        float lowest = Inf;
        for (size_t n = 0; n < n_nodes; ++n)
            if (owner[n] == band && cand[n] && Sb.data()[n] < lowest) {
                lowest = Sb.data()[n];
                start  = Point::new_scale(grid.node(int(n % grid.nx()), int(n / grid.nx())));
            }
        return start;
    };

    std::map<int, std::pair<float, float>> band_range;
    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n] && owner[n] != -1) {
            auto [it, inserted] = band_range.try_emplace(owner[n], Sb.data()[n], Sb.data()[n]);
            it->second.first    = std::min(it->second.first, Sb.data()[n]);
            it->second.second   = std::max(it->second.second, Sb.data()[n]);
        }
    auto in_range = [&](int band, float s) {
        const auto it = band_range.find(band);
        return it != band_range.end() && s >= it->second.first - h_min && s <= it->second.second + h_min;
    };

    ExPolygons floor_shells;
    for (size_t idx = first_floor; idx < result.patches.size(); ++idx)
        append(floor_shells, diff_ex(result.patches[idx].footprint, result.patches[idx].wall_band));
    if (result.patches.size() > first_floor) {
        const PrintRegion &region  = object.printing_region(size_t(result.patches[first_floor].region_id));
        const Flow         flow    = region.flow(object, frSolidInfill, h);
        const double       overlap = 0.01 * region.config().top_bottom_infill_wall_overlap.value * line_width;
        ExPolygons         footprint;
        for (size_t idx = first_floor; idx < result.patches.size(); ++idx)
            append(footprint, result.patches[idx].footprint);
        const ExPolygons area = intersection_ex(offset_ex(union_ex(floor_shells), float(scale_(overlap))), union_ex(footprint));
        std::map<int, Polylines> pieces;
        split_by_owner(fill_solid(area, rings ? ipConcentric : ipRectilinear, flow.spacing(), angle, false, flow, 0.), owner_at, 0.5 * pitch,
                       [&](int o, Polyline &&pl) { pieces[o].emplace_back(std::move(pl)); });
        tbb::parallel_for(tbb::blocked_range<size_t>(first_floor, result.patches.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx) {
                ConformalPatch &patch = result.patches[idx];
                const double    host_z = layers[patch.host_layer]->print_z;
                const double    floor_z = layer_bottom[patch.host_layer];
                auto at = [&](const Vec2d &p, double &z, float &bead) {
                    const float s = floor_surface.sample(p);
                    if (std::isnan(s) || !in_range(patch.id, s))
                        return false;
                    z    = floor_z;
                    bead = float(std::clamp(floor_z - s, 0.5 * h_min, h_max));
                    return true;
                };
                const ExtrusionPath       proto(erBottomSurface, flow.mm3_per_mm(), float(flow.width()), float(h));
                ExtrusionEntityCollection shell;
                auto it = pieces.find(patch.id);
                if (it != pieces.end())
                    lift_linked(link(patch.id, std::move(it->second), band_start(patch.id), 2.5 * flow.spacing()), proto, at,
                                host_z, pitch, patch.id, 0, shell.entities);
                shell.no_sort = true;
                patch.shell_paths.emplace_back(std::move(shell));
            }
        });
    }

    if (result.patches.size() > first_floor) {
        Mask shell_mask = rasterize(floor_shells, grid);
        for (size_t n = 0; n < n_nodes; ++n)
            shell_mask[n] = shell_mask[n] && all_floor[n];
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                Mask below(n_nodes, 0);
                bool any = false;
                for (size_t n = 0; n < n_nodes; ++n)
                    if (shell_mask[n] && layer_bottom[i] < Fb.data()[n] - EPSILON && layers[i]->print_z > Sb.data()[n] + EPSILON) {
                        below[n] = 1;
                        any      = true;
                    }
                if (!any)
                    continue;
                const ExPolygons  excl      = mask_to_expolygons(below, grid);
                const BoundingBox excl_bbox = get_extents(excl);
                for (LayerRegion *layerm : layers[i]->regions()) {
                    clip_extrusions(layerm->fills, excl, excl_bbox);
                    clip_extrusions(layerm->perimeters, excl, excl_bbox);
                }
                layers[i]->conformal_covered = true;
                append(result.planar_exclusion[i], excl);
                result.planar_exclusion[i] = union_ex(result.planar_exclusion[i]);
            }
        });
        for (size_t idx = first_floor; idx < result.patches.size(); ++idx)
            layers[result.patches[idx].host_layer]->conformal_patches_before.emplace_back(int(idx));
    }

    {
        ExPolygons wall_bands;
        for (const ConformalPatch &patch : result.patches)
            if (patch.kind == PatchKind::Bottom)
                append(wall_bands, patch.wall_band);
        const Mask in_wall_band = rasterize(wall_bands, grid);
        for (size_t n = 0; n < n_nodes; ++n)
            if (cand[n] && (owner[n] == -1 || in_wall_band[n]))
                cand[n] = 0;
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, supports.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t j = range.begin(); j < range.end(); ++j) {
            Mask above(n_nodes, 0);
            bool any = false;
            for (size_t n = 0; n < n_nodes; ++n)
                if (cand[n] && int(j) > keep[n] && int(j) <= top[n]) {
                    above[n] = 1;
                    any      = true;
                }
            if (!any)
                continue;
            const ExPolygons excl = mask_to_expolygons(above, grid);
            clip_extrusions(supports[j]->support_fills, excl, get_extents(excl));
        }
    });

    const Mask      &si_mask = cand;
    const ExPolygons si_area = mask_to_expolygons(si_mask, grid);
    if (si_area.empty())
        return;
    HeightField kept_top = grid;
    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n])
            kept_top.data()[n] = float(support_z[size_t(keep[n])]);
    const HeightField underside = masked(Sb, si_mask, 3);
    const HeightField planar    = masked(kept_top, si_mask, 3);

    const bool          interface  = cfg_layers > 0;
    const ExtrusionRole role       = interface ? erSupportMaterialInterface : erSupportMaterial;
    const double        spacing    = iflow.spacing() + (interface ? ocfg.support_interface_spacing.value : ocfg.support_base_pattern_spacing.value);
    const bool          concentric = interface && (ocfg.support_interface_pattern.value == smipConcentric ||
                                                   ocfg.support_interface_pattern.value == smipSpiralInset ||
                                                   (ocfg.support_interface_pattern.value == smipAuto && gap == 0.));
    std::vector<int> si_owner(n_nodes, -1);
    for (size_t n = 0; n < n_nodes; ++n)
        if (cand[n])
            si_owner[n] = owner[n];
    const ExPolygons fill_area = offset_ex(si_area, float(scale_(spacing)));
    auto si_owner_at = [&](const Vec2d &p) { const int n = nearest_node(grid, p); return n < 0 || si_owner[size_t(n)] == -1 ? -1 : owner_at(p); };
    std::map<int, std::vector<Polylines>> by_owner;
    for (int k = 1; k < n_i; ++k) {
        const bool      loops = concentric || rings;
        const Polylines lines = fill_solid(loops ? si_area : fill_area, loops ? ipConcentric : ipRectilinear, spacing,
                                           angle + (n_i - k + 1) * 0.5 * PI, false, iflow, 0.);
        split_by_owner(lines, si_owner_at, 0.5 * pitch, [&](int o, Polyline &&pl) {
            std::vector<Polylines> &per_layer = by_owner[o];
            per_layer.resize(size_t(n_i));
            per_layer[size_t(k - 1)].emplace_back(std::move(pl));
        });
    }
    std::map<int, Mask> band_nodes;
    for (size_t n = 0; n < n_nodes; ++n)
        if (const int o = cand[n] ? si_owner_at(grid.node(int(n % grid.nx()), int(n / grid.nx()))) : -1; o != -1) {
            Mask &m = band_nodes[o];
            m.resize(n_nodes, 0);
            m[n] = 1;
        }
    std::vector<std::pair<int, Mask>> bands;
    for (const auto &[band, m] : band_nodes) {
        int                    n_comp = 0;
        const std::vector<int> label  = label_components(m, grid.nx(), grid.ny(), n_comp);
        const size_t           first  = bands.size();
        for (int c = 0; c < n_comp; ++c)
            bands.emplace_back(band, Mask(n_nodes, 0));
        for (size_t n = 0; n < n_nodes; ++n)
            if (label[n] >= 0)
                bands[first + size_t(label[n])].second[n] = 1;
    }
    std::vector<Polylines> contact(bands.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, bands.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            const int   band = bands[i].first;
            const Mask *m    = &bands[i].second;
            double sx = 0., sy = 0., sxx = 0., sxy = 0., syy = 0., cnt = 0.;
            for (size_t n = 0; n < n_nodes; ++n)
                if ((*m)[n]) {
                    const Vec2d q = grid.node(int(n % grid.nx()), int(n / grid.nx()));
                    sx += q.x(), sy += q.y(), sxx += q.x() * q.x(), sxy += q.x() * q.y(), syy += q.y() * q.y(), cnt += 1.;
                }
            const double cxx = sxx / cnt - sx * sx / (cnt * cnt), cxy = sxy / cnt - sx * sy / (cnt * cnt),
                         cyy = syy / cnt - sy * sy / (cnt * cnt);
            const double a   = 0.5 * std::atan2(2. * cxy, cxx - cyy);
            const ExPolygons grown = offset_ex(mask_to_expolygons(*m, grid), float(scale_(spacing + pitch)));
            const Polylines lines = fill_solid(grown, concentric ? ipConcentric : ipRectilinear, spacing,
                                               concentric ? angle : a + 0.5 * PI, false, iflow, 0.);
            split_by_owner(lines, si_owner_at, 0.5 * pitch, [&](int o, Polyline &&pl) {
                if (o == band)
                    contact[i].emplace_back(std::move(pl));
            });
        }
    });
    for (size_t i = 0; i < bands.size(); ++i) {
        std::vector<Polylines> &per_layer = by_owner[bands[i].first];
        per_layer.resize(size_t(n_i));
        append(per_layer[size_t(n_i - 1)], std::move(contact[i]));
    }
    cancel();

    const size_t first_si = result.patches.size();
    for (auto &entry : by_owner) {
        const int band_id = entry.first;
        auto &per_layer = entry.second;
        const bool   skin_band = band_id <= -3;
        const size_t host      = skin_band ? size_t(-3 - band_id) : result.patches[size_t(band_id)].host_layer;
        ConformalPatch        patch;
        patch.id           = int(result.patches.size());
        patch.kind         = PatchKind::SupportInterface;
        patch.from_support = true;
        patch.shells       = n_i;
        patch.shell_height = h;
        patch.region_id    = skin_band ? 0 : result.patches[size_t(band_id)].region_id;
        patch.surface      = underside;
        patch.base         = planar;
        patch.host_layer   = host;
        patch.band         = skin_band ? int(host) : std::max(0, result.patches[size_t(band_id)].band);
        patch.z_min        = double(Inf);
        patch.z_max        = double(-Inf);
        Mask fmask(n_nodes, 0);
        for (size_t n = 0; n < n_nodes; ++n)
            fmask[n] = cand[n] && owner[n] == band_id;
        patch.footprint     = mask_to_expolygons(fmask, grid);
        const double host_z = layers[patch.host_layer]->print_z;
        const Point  start  = band_start(band_id);
        for (int k = 1; k <= n_i; ++k) {
            const ExtrusionPath proto(role, iflow.mm3_per_mm(), float(iflow.width()), float(h));
            auto at = [&](const Vec2d &p, double &z, float &bead) {
                const float s = underside.sample(p);
                if (std::isnan(s) || !in_range(band_id, s))
                    return false;
                z    = s - gap - (n_i - k) * h;
                bead = float(h);
                if (k == 1) {
                    const int   n  = nearest_node(grid, p);
                    const float pz = n >= 0 && !std::isnan(kept_top.data()[size_t(n)]) ? kept_top.data()[size_t(n)] : planar.sample(p);
                    if (std::isnan(pz))
                        return false;
                    bead = float(std::clamp(z - pz, h_min, h_max));
                }
                return true;
            };
            ExtrusionEntityCollection layer;
            lift_linked(link(band_id, std::move(per_layer[size_t(k - 1)]), start, 2.5 * spacing), proto, at, host_z, pitch,
                        patch.id, k - 1, layer.entities);
            layer.no_sort = true;
            for (const ExtrusionEntity *ee : layer.entities)
                for (const Point3 &pt : static_cast<const ExtrusionPathConformal *>(ee)->polyline.points) {
                    patch.z_min = std::min(patch.z_min, host_z + unscale<double>(pt.z()));
                    patch.z_max = std::max(patch.z_max, host_z + unscale<double>(pt.z()));
                }
            patch.shell_paths.emplace_back(std::move(layer));
        }
        if (patch.z_min > patch.z_max)
            continue;
        std::vector<int> &after  = layers[patch.host_layer]->conformal_patches_after;
        if (!skin_band)
            if (auto it = std::find(after.begin(), after.end(), band_id); it != after.end()) {
                after.insert(it, patch.id);
                result.patches.emplace_back(std::move(patch));
                continue;
            }
        std::vector<int> &before = layers[patch.host_layer]->conformal_patches_before;
        before.insert(skin_band ? std::find_if(before.begin(), before.end(), [&result](int id) {
                                      return result.patches[size_t(id)].kind == PatchKind::Overhang;
                                  }) :
                                  std::find(before.begin(), before.end(), band_id),
                      patch.id);
        result.patches.emplace_back(std::move(patch));
    }

    BOOST_LOG_TRIVIAL(info) << "Non-planar: curved support interface in " << object.model_object()->name << ": "
                            << result.patches.size() - first_si << " interface patch(es), " << first_si - first_floor
                            << " floor patch(es), " << n_i << " interface layer(s)";
}

}
