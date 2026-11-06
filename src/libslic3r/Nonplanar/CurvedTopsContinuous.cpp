#include "CurvedTopsContinuous.hpp"
#include "Atom/stripes.h"
#include "SpiralTop.hpp"
#include "BeadModel.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/Slicing.hpp"
#include "CurvedTopsOrder.hpp"
#include "CurvedTopsSchedule.hpp"
#include "CurvedTopsSurface.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {
namespace {
using Key = std::tuple<int, float, size_t>;
Key key(const ExtrusionPath &path, size_t region) { return {path.source_surface_id,path.distance_to_top,region}; }
void paths(const ExtrusionEntity &entity, const std::function<void(const ExtrusionPath&)> &fn)
{
    if (const auto *collection=dynamic_cast<const ExtrusionEntityCollection*>(&entity))
        for(const auto *child:collection->entities) paths(*child,fn);
    else if(const auto *loop=dynamic_cast<const ExtrusionLoop*>(&entity))
        for(const auto &path:loop->paths) fn(path);
    else if(const auto *multi=dynamic_cast<const ExtrusionMultiPath*>(&entity))
        for(const auto &path:multi->paths) fn(path);
    else if(const auto *path=dynamic_cast<const ExtrusionPath*>(&entity)) fn(*path);
}
void erase(ExtrusionEntityCollection &collection, const std::set<Key> &replaced, size_t region)
{
    auto &entities=collection.entities;
    entities.erase(std::remove_if(entities.begin(),entities.end(),[&](ExtrusionEntity *entity) {
        if(auto *child=dynamic_cast<ExtrusionEntityCollection*>(entity)) {
            erase(*child,replaced,region);
            if(!child->entities.empty()) return false;
        } else {
            bool all=true;
            paths(*entity,[&](const ExtrusionPath &path) { all &= path.nonplanar_source && replaced.count(key(path,region))!=0; });
            if(!all) return false;
        }
        delete entity;
        return true;
    }),entities.end());
}
struct Group {
    ExtrusionPath sample{erTopSolidInfill,0.,0.f,0.f};
    LayerRegion *home=nullptr;
    bool has_fill=false, compatible=true;
    size_t roads=0;
    Polygons area;
};

ExtrusionPaths projected_helix(const Group &g, const NonplanarSurface &surface,
                              const NonplanarClearanceModel &head)
{
    const double spacing = g.sample.mm3_per_mm / g.sample.height;
    const double min_height = std::max(g.home->layer()->object()->slicing_parameters().min_layer_height,
        2. * (head.safety_mm + .001));
    if (min_height >= double(g.sample.height) - 1e-6) return {};
    const double min_factor = min_height / g.sample.height;
    HelixRamp ramp;
    const Polyline xy = fill_helix(union_ex(g.area), spacing,
        g.sample.first_point(), false, &ramp, TopHelixMinRings);
    if (xy.points.size() < 4) return {};
    Layer *home = g.home->layer();
    struct HelixLayer : Layer {
        explicit HelixLayer(Layer &source) : Layer(source.id(), source.object(), source.height, source.print_z, source.slice_z) {}
        ~HelixLayer() override = default;
    } shell(*home);
    LayerRegion *region = shell.add_region(&g.home->region());
    region->append_nonplanar_surface(surface);
    auto *collection = new ExtrusionEntityCollection;
    ExtrusionPath input(Polyline3(xy), g.sample);
    input.source_layer_id = home->id();
    collection->entities.push_back(new ExtrusionPath(std::move(input)));
    region->fills.entities.push_back(collection);
    region->project_nonplanar_surfaces();
    ExtrusionPath path(g.sample);
    path.polyline.points.clear();
    bool contiguous = true, previous = false;
    Point3 previous_end;
    paths(region->fills, [&](const ExtrusionPath &piece) {
        if (piece.polyline.points.empty()) return;
        const Point3 &first = piece.polyline.points.front();
        const bool joined = !previous ||
            ((previous_end.to_point() - first.to_point()).cast<double>().norm() <= scale_(.000002) &&
             std::abs(previous_end.z() - first.z()) <= scale_(.003));
        contiguous &= joined;
        for (size_t i = previous && joined ? 1 : 0; i < piece.polyline.points.size(); ++i) {
            const Point3 &p = piece.polyline.points[i];
            const Vec3d q = unscale(p);
            const Point3 quantized(scaled<coord_t>(std::round(1000.*q.x())*.001),
                scaled<coord_t>(std::round(1000.*q.y())*.001), scaled<coord_t>(std::round(1000.*q.z())*.001));
            if (path.polyline.points.empty() || path.polyline.points.back() != quantized)
                path.polyline.points.push_back(quantized);
        }
        previous_end = piece.polyline.points.back();
        previous = true;
    });
    if (!contiguous || path.polyline.points.size() < 4) return {};
    path.source_layer_id = home->id();
    path.continuous_top = true;
    path.set_reverse();
    ExtrusionPaths out;
    double arc = 0.;
    auto flush = [&](ExtrusionPath &run) {
        if (run.polyline.points.size() < 2) return;
        run.polyline.simplify(scale_(.001));
        out.push_back(std::move(run));
        run = ExtrusionPath(Polyline3(), path);
    };
    ExtrusionPath run(path);
    run.polyline.points.clear();
    for (size_t i = 1; i < path.polyline.points.size(); ++i) {
        const Point3 &a = path.polyline.points[i-1], &b = path.polyline.points[i];
        const double length = (unscale(a).head<2>() - unscale(b).head<2>()).norm();
        const double middle = arc + .5*length;
        const double factor = ramp.end > ramp.begin && middle > ramp.begin && middle < ramp.end ?
            std::max(min_factor, (middle-ramp.begin)/(ramp.end-ramp.begin)) : 1.;
        arc += length;
        if (factor < 1.) {
            flush(run);
            ExtrusionPath bead(Polyline3(Points3{a,b}), path);
            bead.height *= float(factor);
            bead.mm3_per_mm *= bead_area(bead.width,bead.height)/bead_area(path.width,path.height);
            out.push_back(std::move(bead));
        } else {
            if (run.polyline.points.empty()) run.polyline.points.push_back(a);
            run.polyline.points.push_back(b);
        }
    }
    flush(run);
    if (!continuous_curved_top_clear(out, head)) return {};
    return out;
}
}

void make_continuous_curved_tops(const PrintObject &object, const std::vector<Layer*> &shells)
{
    const auto start=std::chrono::steady_clock::now();
    std::map<Key,Group> groups;
    std::map<int,std::set<size_t>> regions;
    for(Layer *layer:shells)
        for(size_t r=0;r<layer->region_count();++r) {
            LayerRegion *region=layer->get_region(int(r));
            if (!region->region().config().nonplanar_sublayer_walls.value) continue;
            for (const Surface &surface : region->slices.surfaces)
                if (surface.is_nonplanar())
                    polygons_append(groups[Key{surface.source_surface_id, surface.distance_to_top, r}].area,
                                    to_polygons(surface.expolygon));
            for(const auto *collection:{&region->perimeters,&region->fills})
                paths(*collection,[&](const ExtrusionPath &path) {
                    if(!path.nonplanar_source || path.polyline.points.size()<2) return;
                    Group &g=groups[key(path,r)];
                    regions[path.source_surface_id].insert(r);
                    if(g.roads && std::abs(g.sample.height-path.height)>.00001) g.compatible=false;
                    const bool fill=path.role()==erTopSolidInfill;
                    if(g.roads==0 || (fill && !g.has_fill)) g.sample=path;
                    g.has_fill |= fill;
                    ++g.roads;
                    if(!g.home || g.home->layer()->id()<layer->id()) g.home=region;
                });
        }
    std::function<void(const ExtrusionEntity&,size_t)> check_unit = [&](const ExtrusionEntity &entity,size_t r) {
        if(const auto *c=dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
            for(const auto *child:c->entities) check_unit(*child,r);
            return;
        }
        std::set<Key> owners;
        bool planar=false;
        paths(entity,[&](const ExtrusionPath &p) { if(p.nonplanar_source) owners.insert(key(p,r)); else planar=true; });
        if(planar || owners.size()!=1)
            for(const auto &owner:owners) groups[owner].compatible=false;
    };
    for(const Layer *layer:shells)
        for(size_t r=0;r<layer->region_count();++r) {
            check_unit(layer->get_region(int(r))->perimeters,r);
            check_unit(layer->get_region(int(r))->fills,r);
        }
    using Cycles=std::vector<std::vector<std::array<float,3>>>;
    std::map<std::pair<int,double>,Cycles> cache;
    std::set<Key> replaced;
    std::vector<std::pair<LayerRegion*,ExtrusionPaths>> generated;
    for(auto &[id,g]:groups) {
        const auto [surface_id,offset,region_id]=id;
        if(!g.compatible || !g.has_fill || regions[surface_id].size()!=1) continue;
        const auto &surfaces=object.nonplanar_surfaces();
        const auto it=std::find_if(surfaces.begin(),surfaces.end(),[&](const NonplanarSurface &surface) { return surface.stable_id==surface_id; });
        if(it==surfaces.end()) continue;
        if (it->stats.max.z-it->stats.min.z < 2. * g.sample.height) {
            BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<": shallow crown keeps native roads";
            continue;
        }
        const double spacing=g.sample.mm3_per_mm/g.sample.height;
        if(!std::isfinite(spacing) || spacing<.1 || spacing>2.) continue;
        if(object.print()->canceled()) throw CanceledException();
        const auto head=NonplanarClearanceModel::from_print(object.print()->config(),g.sample.height);
        ExtrusionPaths helix = projected_helix(g, *it, head);
        if (!helix.empty()) {
            BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<" shell "<<offset<<": "<<g.roads
                                   <<" roads -> 1 helix, "<<helix.size()<<" flow pieces";
            generated.emplace_back(g.home,std::move(helix));
            replaced.insert(id);
            continue;
        }
        const ExPolygons raw_inset = offset_ex(it->horizontal_projection(), -float(scale_(.5 * spacing)));
        if (raw_inset.empty() || !diff_ex(raw_inset, union_ex(g.area)).empty()) {
            BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<": source mesh exceeds the clipped footprint; keeping original roads";
            continue;
        }
        const auto cache_key=std::make_pair(surface_id,spacing);
        if(cache.count(cache_key)==0) {
            std::map<std::array<float,3>,uint32_t> vertices;
            std::vector<float> xyz;
            std::vector<uint32_t> faces;
            for(const auto &[facet_id,facet]:it->mesh)
                for(const auto &v:facet.vertex) {
                    const std::array<float,3> point{v.x,v.y,v.z};
                    const auto [vertex,inserted]=vertices.emplace(point,uint32_t(vertices.size()));
                    if(inserted) xyz.insert(xyz.end(),point.begin(),point.end());
                    faces.push_back(vertex->second);
                }
            try { cache[cache_key]=Atom::continuous_stripes(xyz,faces,float(spacing),0.f); }
            catch(const std::length_error &e) {
                BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<": "<<e.what()<<"; keeping original roads";
                cache[cache_key]={};
            }
        }
        const auto &cycles=cache.at(cache_key);
        if(cycles.size()!=1) continue;
        Points3 points;
        points.reserve(cycles.front().size());
        for(const auto &p:cycles.front()) {
            const Point3 q(scaled<coord_t>(std::round(1000.*p[0])*.001),scaled<coord_t>(std::round(1000.*p[1])*.001),
                           scaled<coord_t>(std::round(1000.*(p[2]-offset))*.001));
            if(points.empty() || points.back()!=q) points.push_back(q);
        }
        if(points.size()<4) continue;
        points.pop_back();
        const auto seam=std::min_element(points.begin(),points.end(),[](const Point3 &a,const Point3 &b) {
            return std::make_tuple(a.z(),a.x(),a.y())<std::make_tuple(b.z(),b.x(),b.y());
        });
        std::rotate(points.begin(),seam,points.end());
        points.push_back(points.front());
        ExtrusionPath path(Polyline3(std::move(points)),g.sample);
        path.polyline.simplify(scale_(.001));
        if(path.polyline.points.size()<4) continue;
        path.source_layer_id=g.home->layer()->id();
        path.continuous_top=true;
        if (!diff_pl(Polylines{path.polyline.to_polyline()}, union_ex(g.area)).empty()) {
            BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<": cycle leaves the clipped footprint; keeping original roads";
            continue;
        }

        if(!continuous_curved_top_clear(path,head)) {
            path.reverse();
            if(!continuous_curved_top_clear(path,head)) {
                BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<": neither cycle direction clears its earlier roads; keeping original roads";
                continue;
            }
        }
        BOOST_LOG_TRIVIAL(info)<<"Continuous curved top "<<surface_id<<" shell "<<offset<<": "<<g.roads<<" roads -> 1 cycle, "<<path.polyline.points.size()<<" points";
        path.set_reverse();
        generated.emplace_back(g.home,ExtrusionPaths{std::move(path)});
        replaced.insert(id);
    }
    if(replaced.empty()) return;
    for(Layer *layer:shells)
        for(size_t r=0;r<layer->region_count();++r) {
            erase(layer->get_region(int(r))->perimeters,replaced,r);
            erase(layer->get_region(int(r))->fills,replaced,r);
        }
    for(auto &[region,paths]:generated) {
        auto *collection=new ExtrusionEntityCollection;
        collection->no_sort = true;
        auto *road = new ExtrusionMultiPath(std::move(paths));
        road->set_reverse();
        collection->entities.push_back(road);
        region->fills.entities.push_back(collection);
    }
    BOOST_LOG_TRIVIAL(info)<<"Continuous curved tops: "<<generated.size()<<" shells in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s";
}
}
