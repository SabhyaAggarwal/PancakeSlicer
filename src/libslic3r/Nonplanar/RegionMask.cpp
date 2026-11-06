#include "RegionDetect.hpp"
#include "RegionMask.hpp"

#include "HeadEnvelope.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <cmath>
#include <mutex>
#include <utility>

namespace Slic3r::Nonplanar {
namespace {
void project_faces(const indexed_triangle_set &mesh, const Transform3d &transform, Polygons &out)
{
    const bool mirrored = transform.linear().determinant() < 0.;
    for (const auto &idx : mesh.indices) {
        const Vec3d a = transform * mesh.vertices[idx[0]].cast<double>();
        const Vec3d b = transform * mesh.vertices[idx[1]].cast<double>();
        const Vec3d c = transform * mesh.vertices[idx[2]].cast<double>();
        const double z = (b-a).cross(c-a).z() * (mirrored ? -1. : 1.);
        if (z <= 1e-10) continue;
        Polygon p({Point::new_scale(a.x(), a.y()), Point::new_scale(b.x(), b.y()), Point::new_scale(c.x(), c.y())});
        p.make_counter_clockwise();
        out.push_back(std::move(p));
    }
}

void prepare_paint(const PrintObject &object, RegionMaskCache &cache)
{
    if (cache.ready) return;
    Polygons enforce, block;
    for (const ModelVolume *volume : object.model_object()->volumes) {
        if (!volume->is_model_part()) continue;
        const Transform3d transform = object.trafo_sliced() * volume->get_matrix();
        if (!volume->nonplanar_facets.empty()) {
            const auto enforced = volume->nonplanar_facets.get_facets(*volume, EnforcerBlockerType::ENFORCER);
            cache.has_enforce |= !enforced.indices.empty();
            project_faces(enforced, transform, enforce);
            project_faces(volume->nonplanar_facets.get_facets(*volume, EnforcerBlockerType::BLOCKER), transform, block);
        }
    }
    cache.enforce = union_ex(enforce);
    cache.block = union_ex(block);
    cache.ready = true;
}

ExPolygons apply_paint(const PrintObject &object, size_t layer_id, const ExPolygons &domain, RegionMaskCache &cache)
{
    prepare_paint(object, cache);


    auto [it, inserted] = cache.paint.try_emplace(layer_id);
    if (inserted) {
        it->second.enforce = cache.enforce;
        it->second.block = cache.block;

        const Layer *layer = object.get_layer(int(layer_id));
        for (const LayerRegion *region : layer->regions()) {
            if (region->region().config().nonplanar_region_source.value == NonplanarRegionSource::PaintOnly)
                it->second.restrict_auto = true;
            else
                for (const Surface &surface : region->slices.surfaces) it->second.auto_domain.push_back(surface.expolygon);
        }
        if (it->second.restrict_auto) it->second.auto_domain = union_ex(it->second.auto_domain);
        else it->second.auto_domain.clear();
    }
    ExPolygons allowed = cache.has_enforce ? intersection_ex(domain, it->second.enforce) :
                         it->second.restrict_auto ? intersection_ex(domain, it->second.auto_domain) : domain;
    return diff_ex(allowed, it->second.block);
}
}

ExPolygons nonplanar_paint_mask(const PrintObject &object, size_t layer_id, const ExPolygons &domain)
{
    if (layer_id >= object.layer_count() || domain.empty()) return {};
    RegionMaskCache &cache = *object.m_nonplanar_region_cache;
    std::lock_guard<std::mutex> lock(cache.mutex);
    return apply_paint(object, layer_id, domain, cache);
}

ExPolygons nonplanar_region_mask(const PrintObject &object, size_t layer_id)
{
    if (layer_id >= object.layer_count()) return {};
    RegionMaskCache &cache = *object.m_nonplanar_region_cache;
    std::lock_guard<std::mutex> lock(cache.mutex);
    prepare_paint(object, cache);
    if (!cache.has_enforce && !cache.detected) {
        const HeadEnvelope head = resolve_head_envelope(object.print()->config());
        for (const ModelVolume *volume : object.model_object()->volumes) {
            if (!volume->is_model_part()) continue;
            const Transform3d transform = object.trafo_sliced() * volume->get_matrix();
            for (Region &region : detect_nonplanar_regions(*volume, transform, head))
                cache.detected_bands.push_back({region.min_z, region.max_z, std::move(region.projection)});
        }
        cache.detected = true;
    }
    if (const auto found = cache.tops.find(layer_id); found != cache.tops.end()) return found->second;
    const Layer *layer = object.get_layer(int(layer_id));
    ExPolygons tops;
    for (const LayerRegion *region : layer->regions())
        for (const Surface &surface : region->slices.surfaces)
            if (surface.surface_type == stTop) tops.push_back(surface.expolygon);

    if (tops.empty()) tops = layer->upper_layer ? diff_ex(layer->lslices, layer->upper_layer->lslices) : layer->lslices;
    if (!cache.has_enforce) {
        Polygons detected;
        for (const auto &band : cache.detected_bands)
            if (band.min_z <= layer->slice_z + layer->height && band.max_z >= layer->slice_z - layer->height)
                detected.insert(detected.end(), band.projection.begin(), band.projection.end());
        tops = intersection_ex(tops, union_ex(detected));
    }
    ExPolygons result = apply_paint(object, layer_id, tops, cache);
    cache.tops.emplace(layer_id, result);
    return result;
}
}
