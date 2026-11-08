#include "../ClipperUtils.hpp"

#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "FillCornerSmoothing.hpp"
#include <utility>
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include <cmath>
#include <cstddef>
#include <algorithm>
#include "FillHoneycomb.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

void FillHoneycomb::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    CacheID cache_id(params.density, this->spacing);
    Cache::iterator it_m = this->cache.find(cache_id);
    if (it_m == this->cache.end()) {
        it_m = this->cache.insert(it_m, std::pair<CacheID, CacheData>(cache_id, CacheData()));
        CacheData &m        = it_m->second;
        coord_t min_spacing = coord_t(scale_(this->spacing)) * params.multiline;
        m.distance          = coord_t(min_spacing / params.density);
        m.hex_side          = coord_t(m.distance / (sqrt(3)/2));
        m.hex_width         = m.distance * 2;
        coord_t hex_height  = m.hex_side * 2;
        m.pattern_height    = hex_height + m.hex_side;
        m.y_short           = coord_t(m.distance * sqrt(3)/3);
        m.x_offset          = min_spacing / 2;
        m.y_offset          = coord_t(m.x_offset * sqrt(3)/3);
        m.hex_center        = Point(m.hex_width/2, m.hex_side);
    }
    CacheData &m = it_m->second;

    Polylines all_polylines;
    {

        BoundingBox bounding_box = expolygon.contour.bounding_box();
        {
            Polygon bb_polygon = bounding_box.polygon();
            bb_polygon.rotate(direction.first, m.hex_center);
            bounding_box = bb_polygon.bounding_box();

            bounding_box.merge(align_to_grid(bounding_box.min, Point(m.hex_width, m.pattern_height)));
        }

        coord_t x = bounding_box.min(0);
        while (x <= bounding_box.max(0)) {
            Polyline p;
            coord_t ax[2] = { x + m.x_offset, x + m.distance - m.x_offset };
            for (size_t i = 0; i < 2; ++ i) {
                std::reverse(p.points.begin(), p.points.end());
                for (coord_t y = bounding_box.min(1); y <= bounding_box.max(1); y += m.y_short + m.hex_side + m.y_short + m.hex_side) {
                    p.points.push_back(Point(ax[1], y + m.y_offset));
                    p.points.push_back(Point(ax[0], y + m.y_short - m.y_offset));
                    p.points.push_back(Point(ax[0], y + m.y_short + m.hex_side + m.y_offset));
                    p.points.push_back(Point(ax[1], y + m.y_short + m.hex_side + m.y_short - m.y_offset));
                    p.points.push_back(Point(ax[1], y + m.y_short + m.hex_side + m.y_short + m.hex_side + m.y_offset));
                }
                ax[0] = ax[0] + m.distance;
                ax[1] = ax[1] + m.distance;
                std::swap(ax[0], ax[1]);
                x += m.distance;
            }
            p.rotate(-direction.first, m.hex_center);
            p.simplify(5 * spacing);
            smooth_polyline_corners(p, params.smooth_factor, scaled<double>(params.resolution));
            all_polylines.push_back(p);
        }
    }
    multiline_fill(all_polylines, params, spacing);

    all_polylines = intersection_pl(std::move(all_polylines), expolygon);
    chain_or_connect_infill(std::move(all_polylines), expolygon, polylines_out, this->spacing, params);
}

}
