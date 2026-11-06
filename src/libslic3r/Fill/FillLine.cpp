#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../ShortestPath.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include <utility>
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include <cassert>
#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Line.hpp"
#include <cmath>
#include <cstddef>
#include <cstdlib>

#include "FillLine.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

void FillLine::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{

    expolygon.rotate(- direction.first);

    this->_min_spacing = scale_(this->spacing);
    assert(params.density > 0.0001f && params.density <= 1.f);
    this->_line_spacing = coord_t(coordf_t(this->_min_spacing) / params.density);
    this->_diagonal_distance = this->_line_spacing * 2;
    this->_line_oscillation = this->_line_spacing - this->_min_spacing;
    BoundingBox bounding_box = expolygon.contour.bounding_box();


    if (params.density > 0.9999f && !params.dont_adjust) {
        this->_line_spacing = this->_adjust_solid_spacing(bounding_box.size()(0), this->_line_spacing);
        this->spacing = unscale<double>(this->_line_spacing);
    } else {


        bounding_box.merge(align_to_grid(
            bounding_box.min,
            Point(this->_line_spacing, this->_line_spacing),
            direction.second.rotated(- direction.first)));
    }


    coord_t x_max = bounding_box.max(0) + SCALED_EPSILON;
    Lines lines;
    for (coord_t x = bounding_box.min(0); x <= x_max; x += this->_line_spacing)
        lines.push_back(this->_line(lines.size(), x, bounding_box.min(1), bounding_box.max(1)));







    Polylines polylines_src;
    polylines_src.reserve(lines.size());
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++ it) {
        polylines_src.push_back(Polyline());
        Points &pts = polylines_src.back().points;
        pts.reserve(2);
        pts.push_back(it->a);
        pts.push_back(it->b);
    }
    Polylines polylines = intersection_pl(std::move(polylines_src), offset(expolygon, scale_(0.02)));


    const float INFILL_OVERLAP_OVER_SPACING = 0.3f;

    coord_t extra = coord_t(floor(this->_min_spacing * INFILL_OVERLAP_OVER_SPACING + 0.5f));
    for (Polylines::iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
        Point *first_point = &it_polyline->points.front();
        Point *last_point  = &it_polyline->points.back();
        if (first_point->y() > last_point->y())
            std::swap(first_point, last_point);
        first_point->y() -= extra;
        last_point->y() += extra;
    }

    size_t n_polylines_out_old = polylines_out.size();


    if (! polylines.empty()) {

        ExPolygon expolygon_off;
        {
            ExPolygons expolygons_off = offset_ex(expolygon, this->_min_spacing/2);
            if (! expolygons_off.empty()) {

                assert(expolygons_off.size() == 1);
                std::swap(expolygon_off, expolygons_off.front());
            }
        }
        bool first = true;
        for (Polyline &polyline : chain_polylines(std::move(polylines))) {
            if (! first) {

                Points &pts_end = polylines_out.back().points;
                const Point &first_point = polyline.points.front();
                const Point &last_point = pts_end.back();

                const Vector distance = last_point - first_point;


                if (this->_can_connect(std::abs(distance(0)), std::abs(distance(1))) &&
                    expolygon_off.contains(Line(last_point, first_point))) {

                    pts_end.insert(pts_end.end(), polyline.points.begin(), polyline.points.end());
                    continue;
                }
            }

            polylines_out.emplace_back(std::move(polyline));
            first = false;
        }
    }


    for (Polylines::iterator it = polylines_out.begin() + n_polylines_out_old; it != polylines_out.end(); ++ it) {


        it->rotate(direction.first);
    }
}

}
