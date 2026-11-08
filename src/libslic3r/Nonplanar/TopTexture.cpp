#include "TopTexture.hpp"

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr int    MaxLayers = 3;
constexpr double Fade      = 1.0;
constexpr double Step      = 0.25;
double relief(TopTexture pattern, double depth, double pitch, const Vec2d &p)
{
    const auto   ridge = [pitch](double t) { return 0.5 - 0.5 * std::cos(2. * PI * t / pitch); };
    const double u = (p.x() + p.y()) * std::sqrt(0.5), v = (p.x() - p.y()) * std::sqrt(0.5);
    return pattern == TopTexture::Ribs ? depth * ridge(u) : depth * ridge(u) * ridge(v);
}

struct Tops
{
    ExPolygons                          area;
    BoundingBox                         box;
    AABBTreeLines::LinesDistancer<Line> edge;

    bool contains(const Point &p) const
    {
        return box.contains(p) && std::any_of(area.begin(), area.end(), [&p](const ExPolygon &e) { return e.contains(p); });
    }
};

}

bool has_top_texture(const PrintObject &object)
{
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id)
        if (object.printing_region(region_id).config().top_texture.value != TopTexture::None)
            return true;
    return false;
}

void texture_tops(PrintObject &object)
{
    const LayerPtrs &layers = object.layers();
    for (Layer *layer : layers)
        layer->top_textured = false;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &config = object.printing_region(region_id).config();
        if (config.top_texture.value == TopTexture::None)
            continue;
        const int    n       = std::clamp(config.top_shell_layers.value, 1, MaxLayers);
        const double depth   = config.top_texture_depth.value, pitch = config.top_texture_pitch.value;
        std::vector<Tops> tops(layers.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t k = range.begin(); k < range.end(); ++k) {
                if (region_id >= layers[k]->regions().size())
                    continue;
                for (const Surface *s : layers[k]->regions()[region_id]->fill_surfaces.filter_by_type(stTop))
                    tops[k].area.push_back(s->expolygon);
                if (tops[k].area.empty())
                    continue;
                tops[k].area = union_ex(tops[k].area);
                tops[k].box  = get_extents(tops[k].area);
                tops[k].edge = AABBTreeLines::LinesDistancer<Line>(to_lines(tops[k].area));
            }
        });
        tbb::parallel_for(tbb::blocked_range<size_t>(1, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t k = range.begin(); k < range.end(); ++k) {
                if (region_id >= layers[k]->regions().size())
                    continue;
                bool any = false;
                for (int j = 0; j < n && k + j < layers.size(); ++j)
                    any |= !tops[k + j].area.empty();
                if (!any)
                    continue;
                auto texture = [&](const ExtrusionPath &path) -> ExtrusionPaths {
                    if (path.z_contoured || (path.role() != erTopSolidInfill && path.role() != erSolidInfill && path.role() != erGapFill))
                        return {};
                    Points3          pts;
                    std::vector<int> share;
                    const Points3   &src = path.polyline.points;
                    for (size_t i = 0; i < src.size(); ++i) {
                        const int m = i + 1 < src.size() ?
                                          std::max(1, int(std::ceil(unscale<double>((src[i + 1] - src[i]).head<2>().norm()) / Step))) : 1;
                        for (int t = 0; t < m; ++t) {
                            const Vec2d   a = unscale(src[i].to_point());
                            const Vec2d   p = i + 1 < src.size() ? Vec2d(a + (unscale(src[i + 1].to_point()) - a) * (double(t) / m)) : a;
                            const Point   q = Point::new_scale(p);
                            int           s = 0;
                            double        z = 0.;
                            for (int j = 0; j < n && k + j < layers.size(); ++j)
                                if (tops[k + j].contains(q)) {
                                    const double edge = unscale<double>(std::abs(tops[k + j].edge.distance_from_lines<false>(q)));
                                    const double fade = std::clamp(edge / Fade, 0., 1.);
                                    s = n - j;
                                    z = double(s) / n * relief(config.top_texture.value, depth, pitch, p) * fade * fade * (3. - 2. * fade);
                                    break;
                                }
                            pts.emplace_back(q.x(), q.y(), scaled<coord_t>(z));
                            share.push_back(s);
                        }
                    }
                    if (std::all_of(share.begin(), share.end(), [](int s) { return s == 0; }))
                        return {};
                    ExtrusionPaths pieces;
                    for (size_t a = 0; a < pts.size();) {
                        size_t b = a;
                        while (b + 1 < pts.size() && share[b + 1] == share[a])
                            ++b;
                        ExtrusionPath piece(Polyline3(), path);
                        piece.polyline.points.assign(pts.begin() + a, pts.begin() + std::min(b + 2, pts.size()));
                        if (share[a] > 0) {
                            piece.z_contoured  = true;
                            piece.contour_base = float(share[a] - 1) / float(share[a]);
                        } else
                            for (Point3 &q : piece.polyline.points)
                                q.z() = 0;
                        if (piece.polyline.points.size() >= 2)
                            pieces.emplace_back(std::move(piece));
                        a = b + 1;
                    }
                    return pieces;
                };
                std::function<void(ExtrusionEntityCollection &)> visit = [&](ExtrusionEntityCollection &c) {
                    ExtrusionEntitiesPtr entities;
                    for (ExtrusionEntity *e : c.entities) {
                        entities.push_back(e);
                        if (auto *nested = dynamic_cast<ExtrusionEntityCollection *>(e)) {
                            visit(*nested);
                            continue;
                        }
                        ExtrusionPaths *paths = nullptr;
                        if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(e))
                            paths = &multi->paths;
                        else if (auto *loop = dynamic_cast<ExtrusionLoop *>(e))
                            paths = &loop->paths;
                        if (paths != nullptr) {
                            ExtrusionPaths out;
                            for (ExtrusionPath &path : *paths) {
                                ExtrusionPaths pieces = typeid(path) == typeid(ExtrusionPath) ? texture(path) : ExtrusionPaths();
                                if (pieces.empty())
                                    out.emplace_back(std::move(path));
                                else {
                                    append(out, std::move(pieces));
                                    layers[k]->top_textured = true;
                                }
                            }
                            *paths = std::move(out);
                        } else if (auto *path = dynamic_cast<ExtrusionPath *>(e); path != nullptr && typeid(*path) == typeid(ExtrusionPath)) {
                            ExtrusionPaths pieces = texture(*path);
                            if (pieces.empty())
                                continue;
                            entities.pop_back();
                            delete e;
                            layers[k]->top_textured = true;
                            for (ExtrusionPath &piece : pieces)
                                entities.push_back(new ExtrusionPath(std::move(piece)));
                        }
                    }
                    c.entities = std::move(entities);
                };
                LayerRegion &region = *layers[k]->regions()[region_id];
                visit(region.fills);
                order_textured(region);
            }
        });
    }
}

}
