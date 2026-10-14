#include "ShellBase.hpp"

#include "BeadModel.hpp"
#include "ConformalShells.hpp"
#include "PatchTools.hpp"

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

template<class Keep> void collect_lines(const ExtrusionEntity &entity, const Keep &keep, Lines &out)
{
    if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
        for (const ExtrusionEntity *e : collection->entities)
            collect_lines(*e, keep, out);
    } else if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity)) {
        if (!keep(*path))
            return;
        const Points3 &pts = path->polyline.points;
        for (size_t i = 1; i < pts.size(); ++i)
            if (const auto *conformal = dynamic_cast<const ExtrusionPathConformal *>(path);
                conformal == nullptr || conformal->bead_height.size() != pts.size() || conformal->bead_height[i] > 0.f)
                out.emplace_back(pts[i - 1].to_point(), pts[i].to_point());
    } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
        for (const ExtrusionPath &p : multi->paths)
            collect_lines(p, keep, out);
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity)) {
        for (const ExtrusionPath &p : loop->paths)
            collect_lines(p, keep, out);
    }
}

double percentile(std::vector<double> v, double q)
{
    if (v.empty())
        return 0.;
    auto it = v.begin() + ptrdiff_t(q * double(v.size() - 1));
    std::nth_element(v.begin(), it, v.end());
    return *it;
}

struct PlanarLayerView
{
    const Layer *layer = nullptr;
    Mask         sparse, material;

    double road_distance(const Point &p)
    {
        if (!roads) {
            Lines lines;
            for (const LayerRegion *layerm : layer->regions())
                for (const ExtrusionEntity *e : layerm->fills.entities)
                    collect_lines(*e, [](const ExtrusionPath &path) { return path.role() == erInternalInfill; }, lines);
            roads = std::make_unique<AABBTreeLines::LinesDistancer<Line>>(std::move(lines));
        }
        return unscale<double>(roads->distance_from_lines<false>(p));
    }
    double edge_distance(const Point &p)
    {
        if (!edges)
            edges = std::make_unique<AABBTreeLines::LinesDistancer<Line>>(to_lines(layer->lslices));
        return unscale<double>(edges->distance_from_lines<false>(p));
    }

private:
    std::unique_ptr<AABBTreeLines::LinesDistancer<Line>> roads, edges;
};

PlanarLayerView view_layer(const Layer &layer, const HeightField &grid)
{
    PlanarLayerView view;
    view.layer = &layer;
    ExPolygons sparse;
    for (const LayerRegion *layerm : layer.regions())
        for (const Surface &s : layerm->fill_surfaces.surfaces)
            if (s.surface_type == stInternal || s.surface_type == stInternalVoid)
                sparse.emplace_back(s.expolygon);
    view.sparse   = rasterize(union_ex(sparse), grid);
    view.material = rasterize(layer.lslices, grid);
    return view;
}

}

int normal_solid_layers(double thickness, double shell_height, double slope, int layers, int shells)
{
    if (thickness <= 0. || shell_height <= 0.)
        return std::min(layers, shells);
    const int n = int(std::ceil((thickness - NormalThicknessTolerance) / (shell_height * std::cos(slope)) - EPSILON));
    return std::clamp(std::max(n, layers), 0, shells);
}

double slope_p90(const HeightField &surface)
{
    std::vector<double> slopes;
    for (int iy = 0; iy < surface.ny(); ++iy)
        for (int ix = 0; ix < surface.nx(); ++ix)
            if (!std::isnan(surface.at(ix, iy)))
                if (const Vec2d g = surface.gradient(surface.node(ix, iy)); g.allFinite())
                    slopes.push_back(std::atan(g.norm()));
    return percentile(std::move(slopes), 0.9);
}

PatchBase measure_patch_base(const PrintObject &object, const ConformalPatch &unit, const HeightField &planar_top,
                             const HeightField &layers, const ExPolygons &area, const std::vector<const ConformalPatch *> &pieces,
                             int sparse, int solid, bool bridged, const std::vector<double> &layer_z)
{
    PatchBase base;
    base.solid_layers = solid;
    base.bridged      = bridged;
    for (const ConformalPatch *p : pieces)
        base.patches.push_back(p->id);
    const auto       object_layers = object.layers();
    const Mask       inside        = rasterize(area, planar_top);
    std::unique_ptr<AABBTreeLines::LinesDistancer<Line>> curved_sparse;
    if (sparse > 0) {
        Lines lines;
        for (const ConformalPatch *p : pieces)
            if (size_t(sparse - 1) < p->shell_paths.size())
                collect_lines(p->shell_paths[size_t(sparse - 1)], [](const ExtrusionPath &) { return true; }, lines);
        if (!lines.empty())
            curved_sparse = std::make_unique<AABBTreeLines::LinesDistancer<Line>>(std::move(lines));
    }
    std::map<size_t, PlanarLayerView> views;
    auto view = [&](size_t l) -> PlanarLayerView & {
        auto it = views.find(l);
        return it != views.end() ? it->second : views.emplace(l, view_layer(*object_layers[l], planar_top)).first->second;
    };
    auto layer_of = [&layer_z](double z) {
        const auto it = std::lower_bound(layer_z.begin(), layer_z.end(), z - EPSILON);
        return it == layer_z.end() || std::abs(*it - z) > 1e-4 ? size_t(-1) : size_t(it - layer_z.begin());
    };
    struct Node { BaseSupport support; double reach; int solid; double vertical; double cos_slope; };
    std::vector<Node>   nodes;
    std::vector<double> slopes;
    const double        h_c = unit.shell_height;
    for (int iy = 0; iy < planar_top.ny(); ++iy)
        for (int ix = 0; ix < planar_top.nx(); ++ix) {
            const size_t i = size_t(iy) * planar_top.nx() + ix;
            const float  b = planar_top.data()[i], m = layers.data()[i];
            if (!inside[i] || !std::isfinite(b) || std::isnan(m))
                continue;
            const Vec2d p = planar_top.node(ix, iy);
            const Vec2d g = unit.surface.gradient(p);
            if (!g.allFinite())
                continue;
            Node n{ BaseSupport::PlanarSolid, 0., std::min(int(m), solid), 0., 1. / std::sqrt(1. + g.squaredNorm()) };
            slopes.push_back(std::atan(g.norm()));
            const Point q = Point::new_scale(p.x(), p.y());
            if (int(m) > solid && sparse > 0) {
                n.support = BaseSupport::CurvedSparse;
                n.reach   = curved_sparse ? unscale<double>(curved_sparse->distance_from_lines<false>(q)) : BridgeSpanAir;
            } else if (const size_t l = layer_of(b); l != size_t(-1)) {
                PlanarLayerView &v = view(l);
                if (!v.material[i]) {
                    n.support = BaseSupport::Air;
                    n.reach   = std::min(0.5 * BridgeSpanAir, v.edge_distance(q));
                } else if (v.sparse[i]) {
                    n.support = BaseSupport::PlanarSparse;
                    n.reach   = std::min(0.5 * BridgeSpanAir, v.road_distance(q));
                } else {
                    for (size_t k = l + 1; k-- > 0 && n.vertical < 2.;) {
                        const PlanarLayerView &w = view(k);
                        if (!w.material[i] || w.sparse[i])
                            break;
                        n.vertical += object_layers[k]->height;
                    }
                }
            }
            n.vertical += n.solid * h_c;
            nodes.push_back(n);
        }
    if (nodes.empty())
        return base;
    const double cell = planar_top.pitch() * planar_top.pitch();
    base.area         = double(nodes.size()) * cell;
    base.slope_p90    = Geometry::rad2deg(percentile(slopes, 0.9));
    double span[size_t(BaseSupport::Count)]{};
    {
        std::vector<double> reach[size_t(BaseSupport::Count)], all;
        for (const Node &n : nodes)
            if (n.support == BaseSupport::CurvedSparse || n.support == BaseSupport::PlanarSparse) {
                reach[size_t(n.support)].push_back(n.reach);
                all.push_back(n.reach);
            }
        for (size_t s = 0; s < size_t(BaseSupport::Count); ++s)
            span[s] = std::min(BridgeSpanAir, 2. * percentile(reach[s], 0.95));
        base.span      = std::min(BridgeSpanAir, 2. * percentile(all, 0.95));
        base.reach_p50 = percentile(all, 0.5);
    }
    std::vector<double> thickness, sag;
    double              sag_sq = 0.;
    for (const Node &n : nodes) {
        base.share[size_t(n.support)] += cell;
        thickness.push_back(n.vertical * n.cos_slope);
        double s = 0.;
        if (n.support == BaseSupport::Air)
            s = bridge_sag(2. * n.reach) * std::pow(BridgeSagLayerFactor, std::max(0, n.solid - 1));
        else if (n.support != BaseSupport::PlanarSolid && span[size_t(n.support)] > 0.) {
            const double L = span[size_t(n.support)];
            const double x = std::min(n.reach, 0.5 * L) / L;
            s = bridge_sag(L, bridged && n.support == BaseSupport::CurvedSparse) * 4. * x * (1. - x) *
                std::pow(BridgeSagLayerFactor, std::max(0, n.solid - 1));
        }
        sag_sq += s * s;
        base.sag_max = std::max(base.sag_max, s);
    }
    for (double &s : base.share)
        s /= base.area;
    base.solid_min = percentile(thickness, 0.02);
    base.solid_p50 = percentile(thickness, 0.5);
    base.sag_rms   = std::sqrt(sag_sq / double(nodes.size()));
    return base;
}

void closing_cells(const PrintObject &object, const ConformalPatch &unit, const HeightField &planar_top, const HeightField &layers,
                   const ExPolygons &area, int solid, double thickness, const std::vector<double> &layer_z,
                   std::vector<ExPolygons> &solid_cells, std::vector<ExPolygons> &bridge_cells)
{
    const auto       object_layers = object.layers();
    const Mask       inside        = rasterize(area, planar_top);
    std::map<size_t, PlanarLayerView> views;
    auto view = [&](size_t l) -> const PlanarLayerView & {
        auto it = views.find(l);
        return it != views.end() ? it->second : views.emplace(l, view_layer(*object_layers[l], planar_top)).first->second;
    };
    std::map<size_t, Mask> to_solid, to_bridge;
    auto mark = [&planar_top](std::map<size_t, Mask> &masks, size_t l, size_t i) {
        Mask &m = masks[l];
        if (m.empty())
            m.assign(planar_top.data().size(), 0);
        m[i] = 1;
    };
    for (int iy = 0; iy < planar_top.ny(); ++iy)
        for (int ix = 0; ix < planar_top.nx(); ++ix) {
            const size_t i = size_t(iy) * planar_top.nx() + ix;
            const float  b = planar_top.data()[i], m = layers.data()[i];
            if (!inside[i] || !std::isfinite(b) || std::isnan(m) || int(m) > solid)
                continue;
            const Vec2d g = unit.surface.gradient(planar_top.node(ix, iy));
            if (!g.allFinite())
                continue;
            double need = std::max(EPSILON, (thickness - NormalThicknessTolerance) * std::sqrt(1. + g.squaredNorm()) - int(m) * unit.shell_height);
            const auto it = std::lower_bound(layer_z.begin(), layer_z.end(), double(b) - EPSILON);
            if (it == layer_z.end() || std::abs(*it - b) > 1e-4)
                continue;
            size_t last = size_t(-1);
            for (size_t l = size_t(it - layer_z.begin()) + 1; l-- > 0 && need > 0.;) {
                const PlanarLayerView &v = view(l);
                if (!v.material[i])
                    break;
                if (v.sparse[i]) {
                    mark(to_solid, l, i);
                    last = l;
                }
                need -= object_layers[l]->height;
            }
            if (last != size_t(-1) && (last == 0 || view(last - 1).sparse[i]))
                mark(to_bridge, last, i);
        }
    for (const auto &[l, m] : to_solid)
        append(solid_cells[l], mask_to_expolygons(m, planar_top));
    for (const auto &[l, m] : to_bridge)
        append(bridge_cells[l], mask_to_expolygons(m, planar_top));
}

void close_layer(PrintObject &object, size_t layer_id, const ExPolygons &solid, const ExPolygons &bridge)
{
    Layer &layer = *object.layers()[layer_id];
    for (LayerRegion *layerm : layer.regions()) {
        ExPolygons sparse;
        for (const Surface &s : layerm->fill_surfaces.surfaces)
            if (s.surface_type == stInternal || s.surface_type == stInternalVoid)
                sparse.emplace_back(s.expolygon);
        const ExPolygons area = intersection_ex(sparse, solid);
        if (area.empty())
            continue;
        const ExPolygons bridged = intersection_ex(area, bridge);
        clip_extrusions(layerm->fills, area, get_extents(area));
        Surfaces kept;
        for (Surface &s : layerm->fill_surfaces.surfaces)
            if (s.surface_type == stInternal || s.surface_type == stInternalVoid) {
                for (ExPolygon &e : diff_ex(ExPolygons{ s.expolygon }, area))
                    kept.emplace_back(s, std::move(e));
            } else
                kept.emplace_back(std::move(s));
        for (const ExPolygon &e : diff_ex(area, bridged))
            kept.emplace_back(stInternalSolid, e);
        for (const ExPolygon &e : bridged)
            kept.emplace_back(stInternalBridge, e);
        layerm->fill_surfaces.surfaces = std::move(kept);
        const PrintRegionConfig &config = layerm->region().config();
        const double angle = Geometry::deg2rad(config.solid_infill_direction.value) + (layer_id % 2 == 1 ? 0.5 * PI : 0.);
        const Flow   flow  = layerm->flow(frSolidInfill);
        auto *collection = new ExtrusionEntityCollection();
        for (const auto &[part, role] : { std::pair{ diff_ex(area, bridged), erSolidInfill }, std::pair{ bridged, erInternalBridgeInfill } }) {
            if (part.empty())
                continue;
            for (Polyline &pl : fill_solid(part, ipRectilinear, flow.spacing(), angle, false, flow, layer.print_z)) {
                ExtrusionPath path(role, flow.mm3_per_mm(), flow.width(), float(layer.height));
                path.polyline = Polyline3(pl);
                collection->entities.push_back(new ExtrusionPath(std::move(path)));
            }
        }
        if (collection->entities.empty())
            delete collection;
        else
            layerm->fills.entities.push_back(collection);
    }
}

std::vector<size_t> unsupported_nodes(const ExtrusionEntityCollection &below, const HeightField &layers, int solid, double reach)
{
    std::vector<size_t> out;
    Lines               lines;
    collect_lines(below, [](const ExtrusionPath &) { return true; }, lines);
    const AABBTreeLines::LinesDistancer<Line> roads(std::move(lines));
    for (int iy = 0; iy < layers.ny(); ++iy)
        for (int ix = 0; ix < layers.nx(); ++ix) {
            const size_t i = size_t(iy) * layers.nx() + ix;
            if (std::isnan(layers.data()[i]) || int(layers.data()[i]) <= solid)
                continue;
            const Vec2d p = layers.node(ix, iy);
            if (unscale<double>(roads.distance_from_lines<false>(Point::new_scale(p.x(), p.y()))) > reach)
                out.push_back(i);
        }
    return out;
}

}
