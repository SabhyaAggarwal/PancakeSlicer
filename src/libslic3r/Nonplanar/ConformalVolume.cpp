#include "ConformalVolume.hpp"

#include "AutoMode.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "HeightField.hpp"
#include "PatchTools.hpp"
#include "Seams.hpp"
#include "ShellPaths.hpp"
#include "StrengthRoads.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
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
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float Inf = std::numeric_limits<float>::infinity();

struct VolumeSettings
{
    int    region_id     = -1;
    double max_thickness = 0.;
    double h             = 0.;
    double h_min = 0., h_max = 0.;
    double max_slope     = 0.;
    double pitch         = 0.2;
};

VolumeSettings volume_settings(const PrintObject &object)
{
    VolumeSettings s;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        if (full_conformal_thickness(cfg, object) <= 0.)
            continue;
        double nozzle = 0.;
        for (double d : object.print()->config().nozzle_diameter.values)
            if (d > 0. && (nozzle == 0. || d < nozzle))
                nozzle = d;
        s.region_id     = int(region_id);
        s.max_thickness = full_conformal_thickness(cfg, object);
        s.h             = cfg.nonplanar_shell_layer_height.value > 0. ? cfg.nonplanar_shell_layer_height.value :
                                                                        object.config().layer_height.value;
        s.h_min         = 0.5 * s.h;
        s.h_max         = nozzle > 0. ? std::min(1.5 * s.h, 0.8 * nozzle) : 1.5 * s.h;
        s.max_slope     = object.config().nonplanar_mode.value == NonplanarMode::Auto ? 0. : Geometry::deg2rad(cfg.nonplanar_max_slope.value);
        s.pitch         = std::max(0.05, object.config().nonplanar_surface_resolution.value);
        break;
    }
    return s;
}

struct Interval
{
    float b, t;
};

std::vector<std::vector<Interval>> sample_columns(const sla::IndexedMesh &mesh, double z_offset, const HeightField &grid,
                                                  const std::function<void()> &cancel)
{
    std::vector<std::vector<Interval>> columns(grid.data().size());
    const double                       z0 = mesh.ground_level() - 1.;
    tbb::parallel_for(tbb::blocked_range<int>(0, grid.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy) {
            cancel();
            for (int ix = 0; ix < grid.nx(); ++ix) {
                const Vec2d           p = grid.node(ix, iy);
                std::vector<Interval> col;
                bool                  inside = false, broken = false;
                float                 entry  = 0.f;
                for (const auto &hit : mesh.query_ray_hits(Vec3d(p.x(), p.y(), z0), Vec3d::UnitZ())) {
                    if (!hit.is_hit())
                        continue;
                    const double nz = hit.normal().normalized().z();
                    const float  z  = float(hit.position().z() - mesh.ground_level() + z_offset);
                    if (nz < -0.05 && !inside) {
                        inside = true;
                        entry  = z;
                    } else if (nz > 0.05 && inside) {
                        inside = false;
                        if (z > entry)
                            col.push_back({ entry, z });
                    } else if (std::abs(nz) <= 0.05)
                        broken = true;
                }
                if (!inside && !broken)
                    columns[size_t(iy) * grid.nx() + ix] = std::move(col);
            }
        }
    });
    return columns;
}

struct Item
{
    size_t node;
    int    iv;
    bool   alive = true;
};

struct Slab
{
    std::vector<int> items;
    int              ix0 = 0, iy0 = 0, ix1 = -1, iy1 = -1;
    HeightField      top, bottom;
    int              layers = 1;
    ExPolygons       footprint;
};

struct UnionFind
{
    std::vector<int> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int i)
    {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    }
    void unite(int a, int b) { parent[find(a)] = find(b); }
};

}

void build_conformal_volume(PrintObject &object, ConformalResult &result)
{
    const VolumeSettings settings = volume_settings(object);
    LayerPtrs           &layers   = object.layers();
    if (settings.region_id < 0 || layers.size() < 2 || object.print()->config().spiral_mode.value)
        return;
    const auto t_start = std::chrono::steady_clock::now();
    if (result.planar_exclusion.size() != layers.size())
        result.planar_exclusion.assign(layers.size(), ExPolygons());
    auto cancel = [&object]() { throw_if_canceled(object); };

    const double      pitch     = settings.pitch;
    const HeightField grid      = object_grid(object, pitch);
    const size_t      n_nodes   = grid.data().size();
    const int         nx        = grid.nx();
    const float       first_top = float(layers.front()->print_z);
    std::vector<double> layer_z(layers.size()), layer_mid(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        layer_z[i]   = layers[i]->print_z;
        layer_mid[i] = layers[i]->print_z - 0.5 * layers[i]->height;
    }

    std::vector<std::vector<Interval>> columns;
    {
        columns = sample_columns(object_mesh(object, result).imesh, object.slicing_parameters().object_print_z_min, grid, cancel);
    }

    bool mixed = false;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id)
        mixed |= full_conformal_thickness(object.printing_region(region_id).config(), object) <= 0.;
    std::vector<Mask> region_mask;
    if (mixed) {
        region_mask.resize(layers.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                ExPolygons slices;
                for (const LayerRegion *layerm : layers[i]->regions())
                    if (full_conformal_thickness(layerm->region().config(), object) > 0.)
                        for (const Surface &surface : layerm->slices.surfaces)
                            slices.emplace_back(surface.expolygon);
                region_mask[i] = rasterize(slices, grid);
            }
        });
    }
    std::vector<Mask> skin_mask(result.self_supported.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, skin_mask.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i)
            if (!result.self_supported[i].empty())
                skin_mask[i] = rasterize(result.self_supported[i], grid);
    });
    std::vector<Item>             items;
    std::vector<std::vector<int>> node_items(n_nodes);
    for (size_t n = 0; n < n_nodes; ++n)
        for (int iv = 0; iv < int(columns[n].size()); ++iv) {
            const Interval &in = columns[n][size_t(iv)];
            const float     b  = std::max(in.b, first_top);
            if (in.t - in.b > settings.max_thickness || in.t - b < settings.h_min)
                continue;
            if (mixed) {
                const size_t l = std::min(layers.size() - 1, size_t(std::lower_bound(layer_mid.begin(), layer_mid.end(),
                                                                                      0.5 * (in.b + in.t)) - layer_mid.begin()));
                if (!region_mask[l][n])
                    continue;
            }
            node_items[n].emplace_back(int(items.size()));
            items.push_back({ n, iv });
        }
    region_mask.clear();
    if (items.empty())
        return;

    std::string        head_warning;
    const HeadEnvelope head = resolve_head_envelope(object.print()->config(), &head_warning);
    if (!head_warning.empty())
        BOOST_LOG_TRIVIAL(warning) << "Non-planar: " << head_warning;

    const PrintRegion       &region     = object.printing_region(size_t(settings.region_id));
    const PrintRegionConfig &rcfg       = region.config();
    const double             line_width = region.flow(object, frSolidInfill, settings.h).width();
    const double             h          = settings.h;
    const Flow               top_flow   = region.flow(object, frTopSolidInfill, h);
    const double             steep_tan  = 2. * max_shell_slope(h, std::min(double(top_flow.width()), 0.75 * top_flow.nozzle_diameter()));

    auto kill = [&](int item) {
        bool      &alive = items[size_t(item)].alive;
        const bool was   = alive;
        alive            = false;
        return was;
    };
    auto face_b = [&](const Item &it) { return std::max(columns[it.node][size_t(it.iv)].b, first_top); };
    auto face_t = [&](const Item &it) { return columns[it.node][size_t(it.iv)].t; };
    auto overlap = [&](int a, int b) {
        return items[size_t(a)].alive && items[size_t(b)].alive &&
               std::max(face_b(items[size_t(a)]), face_b(items[size_t(b)])) < std::min(face_t(items[size_t(a)]), face_t(items[size_t(b)]));
    };
    auto build_slabs = [&]() {
        std::vector<Slab> slabs;
        for (;;) {
            std::vector<int> fork;
            for (int iy = 0; iy < grid.ny(); ++iy)
                for (int ix = 0; ix < nx; ++ix)
                    for (int a : node_items[size_t(iy) * nx + ix]) {
                        if (!items[size_t(a)].alive)
                            continue;
                        bool forks = false;
                        for (int dy = -1; dy <= 1 && !forks; ++dy)
                            for (int dx = -1; dx <= 1 && !forks; ++dx) {
                                const int x = ix + dx, y = iy + dy;
                                if ((dx == 0 && dy == 0) || x < 0 || y < 0 || x >= nx || y >= grid.ny())
                                    continue;
                                int count = 0;
                                for (int b : node_items[size_t(y) * nx + x])
                                    count += overlap(a, b);
                                forks = count > 1;
                            }
                        if (forks)
                            fork.emplace_back(a);
                    }
            for (int a : fork)
                kill(a);
            if (!fork.empty())
                continue;
            UnionFind uf(items.size());
            for (int iy = 0; iy < grid.ny(); ++iy)
                for (int ix = 0; ix < nx; ++ix) {
                    const size_t n = size_t(iy) * nx + ix;
                    for (auto [dx, dy] : { std::pair{ 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } }) {
                        const int x = ix + dx, y = iy + dy;
                        if (x < 0 || x >= nx || y >= grid.ny())
                            continue;
                        for (int a : node_items[n])
                            for (int b : node_items[size_t(y) * nx + x])
                                if (overlap(a, b))
                                    uf.unite(a, b);
                    }
                }
            bool cut = false;
            for (size_t n = 0; n < n_nodes; ++n)
                for (size_t i = 0; i < node_items[n].size(); ++i)
                    for (size_t j = i + 1; j < node_items[n].size(); ++j) {
                        const int a = node_items[n][i], b = node_items[n][j];
                        if (items[size_t(a)].alive && items[size_t(b)].alive && uf.find(a) == uf.find(b)) {
                            kill(a);
                            kill(b);
                            cut = true;
                        }
                    }
            if (cut)
                continue;
            std::vector<int> slab_of(items.size(), -1);
            for (int i = 0; i < int(items.size()); ++i) {
                if (!items[size_t(i)].alive)
                    continue;
                int &s = slab_of[size_t(uf.find(i))];
                if (s < 0) {
                    s = int(slabs.size());
                    slabs.emplace_back();
                }
                Slab     &slab = slabs[size_t(s)];
                const int ix = int(items[size_t(i)].node % size_t(nx)), iy = int(items[size_t(i)].node / size_t(nx));
                slab.items.emplace_back(i);
                if (slab.ix1 < 0)
                    slab.ix0 = ix, slab.iy0 = iy, slab.ix1 = ix, slab.iy1 = iy;
                slab.ix0 = std::min(slab.ix0, ix), slab.iy0 = std::min(slab.iy0, iy);
                slab.ix1 = std::max(slab.ix1, ix), slab.iy1 = std::max(slab.iy1, iy);
            }
            return slabs;
        }
    };
    auto local_index = [&](const Slab &slab, const Item &it) {
        return size_t(int(it.node / size_t(nx)) - slab.iy0) * size_t(slab.ix1 - slab.ix0 + 1) + size_t(int(it.node % size_t(nx)) - slab.ix0);
    };

    std::vector<Slab> slabs;
    for (bool changed = true; changed;) {
        cancel();
        slabs = build_slabs();
        std::vector<char> dropped(slabs.size(), 0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, slabs.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t s = range.begin(); s < range.end(); ++s) {
                Slab &slab = slabs[s];
                slab.top   = crop(grid, slab.ix0, slab.iy0, slab.ix1, slab.iy1);
                slab.bottom = slab.top;
                for (int i : slab.items) {
                    const size_t n         = local_index(slab, items[size_t(i)]);
                    slab.top.data()[n]    = face_t(items[size_t(i)]);
                    slab.bottom.data()[n] = face_b(items[size_t(i)]);
                }
                if (settings.max_slope > 0.) {
                    slab.top    = slope_cap_minorant(slab.top, settings.max_slope);
                    slab.bottom = slope_cap_minorant(slab.bottom, settings.max_slope);
                }
                {
                    HeightField top = slab.top;
                    std::fill(top.data().begin(), top.data().end(), HeightField::NoData);
                    for (int i : slab.items)
                        top.data()[local_index(slab, items[size_t(i)])] = slab.top.data()[local_index(slab, items[size_t(i)])];
                    const Mask steep = steep_nodes(top, steep_tan, 2. * line_width);
                    for (int i : slab.items)
                        if (steep[local_index(slab, items[size_t(i)])])
                            dropped[s] |= kill(i);
                }
                float tlo = Inf, thi = -Inf, blo = Inf, bhi = -Inf;
                std::vector<float> thickness;
                for (int i : slab.items) {
                    const size_t n = local_index(slab, items[size_t(i)]);
                    const float  t = slab.top.data()[n], b = std::max(slab.bottom.data()[n], first_top);
                    slab.bottom.data()[n] = b;
                    tlo = std::min(tlo, t), thi = std::max(thi, t), blo = std::min(blo, b), bhi = std::max(bhi, b);
                    thickness.emplace_back(t - b);
                }
                if (thi - tlo < settings.h_min && bhi - blo < settings.h_min) {
                    for (int i : slab.items)
                        dropped[s] |= kill(i);
                    continue;
                }
                std::nth_element(thickness.begin(), thickness.begin() + thickness.size() / 2, thickness.end());
                slab.layers = std::max(1, int(std::lround(thickness[thickness.size() / 2] / h)));
                Mask mask(slab.top.data().size(), 0);
                for (int i : slab.items) {
                    const size_t n    = local_index(slab, items[size_t(i)]);
                    const double bead = (slab.top.data()[n] - slab.bottom.data()[n]) / slab.layers;
                    if (bead < settings.h_min - EPSILON || bead > settings.h_max + EPSILON)
                        dropped[s] |= kill(i);
                    else
                        mask[n] = 1;
                }
                slab.footprint    = opening_ex(mask_to_expolygons(mask, slab.top), float(scale_(line_width)));
                const Mask inside = rasterize(slab.footprint, slab.top);
                for (int i : slab.items)
                    if (items[size_t(i)].alive && !inside[local_index(slab, items[size_t(i)])])
                        dropped[s] |= kill(i);
            }
        });
        changed = std::find(dropped.begin(), dropped.end(), 1) != dropped.end();
    }
    auto skin_owned = [&](const Slab &slab) {
        size_t alive = 0, owned = 0;
        for (int i : slab.items)
            if (const Item &it = items[size_t(i)]; it.alive) {
                ++alive;
                const size_t l = std::min(skin_mask.size() - 1, size_t(std::lower_bound(layer_z.begin(), layer_z.end(), double(face_b(it))) - layer_z.begin()));
                owned += !skin_mask[l].empty() && skin_mask[l][it.node];
            }
        return 2 * owned >= alive;
    };
    slabs.erase(std::remove_if(slabs.begin(), slabs.end(), [&](const Slab &slab) {
                    return slab.footprint.empty() || (!skin_mask.empty() && skin_owned(slab));
                }), slabs.end());
    if (slabs.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Non-planar: no full conformal slab in " << object.model_object()->name;
        return;
    }

    double band_pitch = 0., band_tolerance = 0.;
    const double max_patch_height = multi_material_patch_height(object, head, effective_patch_height_cap(object));
    const bool   host_above       = max_patch_height != effective_patch_height_cap(object);
    const double rotary           = rotary_band_reach(object, head, h);
    band_schedule(head, h, rotary > 0. ? (max_patch_height > 0. ? std::min(max_patch_height, rotary) : rotary) : max_patch_height,
                  band_pitch, band_tolerance);
    const Flow ext    = region.flow(object, frExternalPerimeter, h);
    const Flow per    = region.flow(object, frPerimeter, h);
    const Flow solid  = region.flow(object, frSolidInfill, h);
    const Flow sparse = region.flow(object, frInfill, h);
    const Flow top    = [&]() {
        const Flow f = region.flow(object, frTopSolidInfill, h);
        return f.with_width(std::min(f.width(), float(0.75 * f.nozzle_diameter())));
    }();
    const int    loops   = std::max(0, rcfg.wall_loops.value);
    const double overlap_w = 0.01 * rcfg.top_bottom_infill_wall_overlap.value * solid.width();
    const double density = 0.01 * rcfg.sparse_infill_density.value;
    const bool   surface_spacing = rcfg.nonplanar_surface_spacing.value;
    const bool          strength       = strength_roads_enabled(rcfg, object.config());
    const StrengthField strength_field = strength ? StrengthField(object) : StrengthField();

    ExPolygons slab_area;
    for (const Slab &slab : slabs)
        append(slab_area, slab.footprint);
    const TopColours colours = top_colours(object, union_ex(slab_area));
    std::vector<HeightField> layer_colours;
    if (colours.areas.size() > 1) {
        BoundingBoxf bbox;
        for (const ExPolygon &expoly : slab_area)
            for (const Point &pt : expoly.contour.points)
                bbox.merge(unscale(pt));
        bbox.min -= Vec2d(1., 1.);
        bbox.max += Vec2d(1., 1.);
        layer_colours.assign(layers.size(), HeightField());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                HeightField field(bbox, 0.1);
                for (const LayerRegion *layerm : layers[i]->regions()) {
                    const auto c = std::find(colours.filaments.begin(), colours.filaments.end(),
                                             layerm->region().config().outer_wall_filament_id.value);
                    if (c == colours.filaments.end())
                        continue;
                    const Mask mask = rasterize(to_expolygons(layerm->slices.surfaces), field);
                    for (size_t n = 0; n < mask.size(); ++n)
                        if (mask[n])
                            field.data()[n] = float(c - colours.filaments.begin());
                }
                layer_colours[i] = std::move(field);
            }
        });
    }
    auto colour_at = [&](const Vec3d &p) {
        const size_t i = std::min(layers.size() - 1, size_t(std::lower_bound(layer_z.begin(), layer_z.end(), p.z() - EPSILON) - layer_z.begin()));
        const HeightField &field = layer_colours[i];
        const Vec2d        o     = (p.head<2>() - field.origin()) / field.pitch();
        const int          ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
        const float        c  = ix >= 0 && iy >= 0 && ix < field.nx() && iy < field.ny() ? field.at(ix, iy) : HeightField::NoData;
        return std::isnan(c) ? colours.at(p.head<2>()) : int(c);
    };

    std::vector<ConformalPatch>   components(slabs.size());
    std::vector<ConformalResult>  banded(slabs.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, slabs.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t s = range.begin(); s < range.end(); ++s) {
            Slab           &slab  = slabs[s];
            ConformalPatch &patch = components[s];
            slab.footprint = intersection_ex(offset_ex(expolygons_simplify(slab.footprint, scale_(pitch)), -float(scale_(pitch))),
                                             slab.footprint);
            patch.kind         = PatchKind::Volume;
            patch.footprint    = slab.footprint;
            patch.shells       = slab.layers;
            patch.shell_height = h;
            patch.region_id    = settings.region_id;
            patch.z_min        = Inf;
            patch.z_max        = -Inf;
            const int pad = 3;
            const int ix0 = std::max(0, slab.ix0 - pad), iy0 = std::max(0, slab.iy0 - pad);
            const int ix1 = std::min(nx - 1, slab.ix1 + pad), iy1 = std::min(grid.ny() - 1, slab.iy1 + pad);
            patch.surface = crop(grid, ix0, iy0, ix1, iy1);
            patch.base    = patch.surface;
            HeightField S = grid;
            for (int i : slab.items) {
                const Item  &it = items[size_t(i)];
                const size_t n  = local_index(slab, it);
                const int    ix = int(it.node % size_t(nx)) - ix0, iy = int(it.node / size_t(nx)) - iy0;
                patch.surface.at(ix, iy) = slab.top.data()[n];
                patch.base.at(ix, iy)    = slab.bottom.data()[n];
                S.data()[it.node]        = slab.top.data()[n];
                patch.z_min              = std::min(patch.z_min, double(slab.bottom.data()[n]));
                patch.z_max              = std::max(patch.z_max, double(slab.top.data()[n]));
            }
            pad_lowest(patch.surface, pad);
            pad_lowest(patch.base, pad);

            const int        L           = patch.shells;
            ExPolygons infill_area = loops == 0 ? patch.footprint : ExPolygons();
            for (int i = 0; i < loops; ++i) {
                const double inner = ext.spacing() + i * per.spacing();
                ExPolygons   area  = offset_ex(patch.footprint, -float(scale_(inner - overlap_w)));
                if (i + 1 < loops)
                    area = diff_ex(area, offset_ex(offset_ex(patch.footprint, -float(scale_(inner + 0.5 * per.spacing()))),
                                                   float(scale_(0.5 * per.spacing() + overlap_w))));
                append(infill_area, std::move(area));
            }
            if (loops > 1)
                infill_area = union_ex(infill_area);
            bool on_air = false;
            for (const float b : patch.base.data())
                on_air |= !std::isnan(b) && b > first_top + EPSILON;
            std::vector<ShellFill> fills = plan_shell_fills(patch, rcfg, solid.spacing());
            const Point seam = shell_seam(patch.footprint, patch.base, object.config().seam_position.value, unsigned(s));
            const double base_angle = Geometry::deg2rad(rcfg.solid_infill_direction.value);
            double     principal = 0.;
            bool ply = strength && strength_field.direction(patch.surface, patch.footprint, principal);
            if (ply) {
                auto share = [&](double a) {
                    return strength_field.aligned_share(patch.surface, patch.footprint, a) +
                           strength_field.aligned_share(patch.surface, patch.footprint, a + 0.5 * PI);
                };
                ply = share(principal) > share(base_angle) + 0.05;
            }

            patch.shell_paths.reserve(size_t(L));
            for (int k = 1; k <= L; ++k) {
                throw_if_canceled(object);
                const double t0 = double(k) / L;
                auto lofted = [&patch, t0, L](const Vec2d &p, double &z, float &bead) {
                    const float t = patch.surface.sample(p), b = patch.base.sample(p);
                    if (std::isnan(t) || std::isnan(b))
                        return false;
                    bead = std::max(0.f, (t - b) / L);
                    z    = b + t0 * (t - b);
                    return true;
                };
                ExtrusionEntityCollection layer;
                layer.no_sort = true;
                for (int i = 0; i < loops; ++i) {
                    const double          d    = i == 0 ? 0.5 * ext.spacing() : ext.spacing() + (i - 0.5) * per.spacing();
                    const Flow           &flow = i == 0 ? ext : per;
                    std::vector<Polylines> rings;
                    for (const ExPolygon &expoly : offset_ex(patch.footprint, -float(scale_(d))))
                        for (const Polygon &poly : to_polygons(expoly)) {
                            Polyline ring = poly.split_at_index(int(poly.closest_point_index(seam)));
                            ring.points.emplace_back(ring.points.front());
                            rings.push_back({ std::move(ring) });
                        }
                    const ExtrusionPath proto(i == 0 ? erExternalPerimeter : erPerimeter, flow.mm3_per_mm(), float(flow.width()), float(h));
                    lift_linked(rings, proto, lofted, 0., pitch, -1, k - 1, layer.entities);
                }
                const bool    solid_layer = k <= rcfg.bottom_shell_layers.value || k > L - rcfg.top_shell_layers.value || density >= 1. - EPSILON;
                ExtrusionRole role        = k == L ? erTopSolidInfill : k == 1 && on_air ? erBottomSurface : solid_layer ? erSolidInfill : erInternalInfill;
                const Flow   &flow        = k == L ? top : solid_layer ? solid : sparse;
                ShellFill     fill        = fills[size_t(k - 1)];
                if (ply && solid_layer && k < L && role != erBottomSurface && !fill.spiral)
                    fill.angle = ply_angle(principal, L - k);
                std::vector<Polylines> linked;
                const bool spaced = surface_spacing && solid_layer && !fill.spiral;
                if (solid_layer)
                    linked = shell_polylines(infill_area, fill, k == L || role == erBottomSurface ? ipMonotonic : ipRectilinear, flow.spacing(),
                                             k == L || role == erBottomSurface, flow, 0., seam, MaxGlide, spaced ? &patch.surface : nullptr);
                else if (density > EPSILON) {
                    fill        = ShellFill{};
                    fill.angle  = ply ? ply_angle(principal, L - k) : base_angle + (k % 2) * 0.5 * PI;
                    linked = shell_polylines(infill_area, fill, ipRectilinear, flow.spacing() / density, false, flow, 0., seam, MaxGlide);
                }
                auto at = [&](const Vec2d &p, double &z, float &bead) {
                    if (!lofted(p, z, bead))
                        return false;
                    if (spaced)
                        bead = surface_spaced_bead(bead, flow.width(), patch.surface.gradient(p), fill.angle);
                    return true;
                };
                const ExtrusionPath proto(role, flow.mm3_per_mm(), float(flow.width()), float(h));
                lift_linked(linked, proto, at, 0., pitch, -1, k - 1, layer.entities);
                patch.shell_paths.emplace_back(std::move(layer));
            }

            ConformalResult &bands = banded[s];
            slab.top    = patch.surface;
            slab.bottom = patch.base;
            bands.patches.emplace_back(std::move(patch));
            bands.diagnostics.emplace_back();
            bands.diagnostics.back().patch_id = 0;
            if (colours.areas.size() > 1)
                split_by_colour(bands, colours, colour_at);
            split_into_bands(bands, S, layer_z, band_pitch, band_tolerance, nullptr, host_above);
        }
    });

    std::vector<BoundingBox> slab_bboxes;
    for (const Slab &slab : slabs)
        slab_bboxes.emplace_back(get_extents(slab.footprint));
    auto in_slab = [&](const Vec3d &p) {
        const Point pt = Point::new_scale(p.x(), p.y());
        for (size_t s = 0; s < slabs.size(); ++s) {
            if (!slab_bboxes[s].contains(pt))
                continue;
            const float t = slabs[s].top.sample(p.head<2>()), b = slabs[s].bottom.sample(p.head<2>());
            if (!std::isnan(t) && !std::isnan(b) && p.z() > b - EPSILON && p.z() < t + 0.05 &&
                std::any_of(slabs[s].footprint.begin(), slabs[s].footprint.end(), [&pt](const ExPolygon &e) { return e.contains(pt); }))
                return true;
        }
        return false;
    };
    tbb::parallel_for(tbb::blocked_range<size_t>(0, result.patches.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t idx = range.begin(); idx < range.end(); ++idx) {
            ConformalPatch &patch  = result.patches[idx];
            const double    host_z = layers[patch.host_layer]->print_z;
            for (ExtrusionEntityCollection &shell : patch.shell_paths)
                erase_conformal(shell, host_z, 0.25 * pitch, in_slab);
            erase_conformal(patch.ironing, host_z, 0.25 * pitch, in_slab);
        }
    });
    if (!result.self_supported.empty())
        for (const Slab &slab : slabs) {
            float lo = std::numeric_limits<float>::infinity(), hi = -lo;
            for (float b : slab.bottom.data())
                if (!std::isnan(b))
                    lo = std::min(lo, b), hi = std::max(hi, b);
            const ExPolygons grown = offset_ex(slab.footprint, float(scale_(pitch)));
            for (size_t l = 0; l < layers.size() && l < result.self_supported.size(); ++l)
                if (!result.self_supported[l].empty() && layers[l]->print_z >= lo - layers[l]->height &&
                    layers[l]->print_z - layers[l]->height <= hi + layers[l]->height)
                    result.self_supported[l] = diff_ex(result.self_supported[l], grown);
        }

    for (ConformalResult &bands : banded) {
        const int offset = int(result.patches.size());
        for (ConformalPatch &patch : bands.patches) {
            patch.id += offset;
            for (ExtrusionEntityCollection &shell : patch.shell_paths)
                for (ExtrusionEntity *ee : shell.entities)
                    static_cast<ExtrusionPathConformal *>(ee)->patch_id = patch.id;
            layers[patch.host_layer]->conformal_patches_after.emplace_back(patch.id);
            result.patches.emplace_back(std::move(patch));
        }
        for (PatchDiagnostics &diag : bands.diagnostics) {
            if (diag.patch_id >= 0)
                diag.patch_id += offset;
            result.diagnostics.emplace_back(std::move(diag));
        }
    }
    for (Layer *layer : layers)
        std::stable_sort(layer->conformal_patches_after.begin(), layer->conformal_patches_after.end(),
                         [&result](int a, int b) { return result.patches[size_t(a)].band < result.patches[size_t(b)].band; });

    for (size_t s = 0; s < slabs.size(); ++s)
        for (size_t i = 1; i < layers.size(); ++i)
            if (layer_z[i] > components[s].z_min + EPSILON && layer_mid[i] < components[s].z_max)
                layers[i]->conformal_covered = true;
    tbb::parallel_for(tbb::blocked_range<size_t>(1, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            throw_if_canceled(object);
            ExPolygons excl;
            for (const Slab &slab : slabs) {
                Mask mask(n_nodes, 0);
                bool any = false;
                for (int it : slab.items) {
                    const Interval &in = columns[items[size_t(it)].node][size_t(items[size_t(it)].iv)];
                    if (layer_mid[i] >= in.b && layer_mid[i] <= in.t) {
                        mask[items[size_t(it)].node] = 1;
                        any                          = true;
                    }
                }
                if (any)
                    append(excl, intersection_ex(mask_to_expolygons(mask, grid), slab.footprint));
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

    BOOST_LOG_TRIVIAL(info) << "Non-planar: " << slabs.size() << " full conformal slab(s) in " << object.model_object()->name << ", "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() << " s";
}

}
