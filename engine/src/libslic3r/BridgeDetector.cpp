#include "BridgeDetector.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "Geometry.hpp"
#include "libslic3r.h"
#include "Polygon.hpp"
#include "Line.hpp"
#include "Polyline.hpp"
#include "Point.hpp"
#include <algorithm>
#include <utility>
#include <vector>
#include <cstddef>
#include <cmath>

namespace Slic3r {

BridgeDetector::BridgeDetector(
    ExPolygon         _expolygon,
    const ExPolygons &_lower_slices, 
    coord_t           _spacing) :
    expolygons(expolygons_owned),
    lower_slices(_lower_slices),
    spacing(_spacing)
{
    this->expolygons_owned.push_back(std::move(_expolygon));
    initialize();
}

BridgeDetector::BridgeDetector(
    const ExPolygons  &_expolygons,
    const ExPolygons  &_lower_slices,
    coord_t            _spacing) : 
    expolygons(_expolygons),
    lower_slices(_lower_slices),
    spacing(_spacing)
{
    initialize();
}

void BridgeDetector::initialize()
{
    this->resolution = PI/36.0; 
    this->angle = -1.;

    Polygons grown = offset(this->expolygons, float(this->spacing));
    
	Polygons contours;
	contours.reserve(this->lower_slices.size());
	for (const ExPolygon &expoly : this->lower_slices)
		contours.push_back(expoly.contour);
    this->_edges = intersection_pl(to_polylines(grown), contours);
    
    #ifdef SLIC3R_DEBUG
    printf("  bridge has %zu support(s)\n", this->_edges.size());
    #endif
    
    this->_anchor_regions = intersection_ex(grown, union_safety_offset(this->lower_slices));
    
}

bool BridgeDetector::detect_angle(double bridge_direction_override)
{
    if (this->_edges.empty() || this->_anchor_regions.empty()) 
        return false;

    std::vector<BridgeDirection> candidates;
    if (bridge_direction_override == 0.) {
        std::vector<double> angles = bridge_direction_candidates();
        candidates.reserve(angles.size());
        for (size_t i = 0; i < angles.size(); ++ i)
            candidates.emplace_back(BridgeDirection(angles[i]));
    } else
        candidates.emplace_back(BridgeDirection(bridge_direction_override));
    
    Polygons clip_area = offset(this->expolygons, 0.5f * float(this->spacing));
    
        
    bool have_coverage = false;
    for (size_t i_angle = 0; i_angle < candidates.size(); ++ i_angle)
    {
        const double angle = candidates[i_angle].angle;

        Lines lines;
        {
            BoundingBox bbox = get_extents_rotated(this->_anchor_regions, - angle);
            lines.reserve((bbox.max(1) - bbox.min(1) + this->spacing) / this->spacing);
            double s = sin(angle);
            double c = cos(angle);
            for (coord_t y = bbox.min(1); y <= bbox.max(1); y += this->spacing)
                lines.push_back(Line(
                    Point((coord_t)round(c * bbox.min(0) - s * y), (coord_t)round(c * y + s * bbox.min(0))),
                    Point((coord_t)round(c * bbox.max(0) - s * y), (coord_t)round(c * y + s * bbox.max(0)))));
        }

        double total_length = 0;
        double max_length = 0;
        {
            Lines clipped_lines = intersection_ln(lines, clip_area);
            size_t archored_line_num = 0;
            for (size_t i = 0; i < clipped_lines.size(); ++i) {
                const Line &line = clipped_lines[i];
                if (expolygons_contain(this->_anchor_regions, line.a) && expolygons_contain(this->_anchor_regions, line.b)) {
                    double len = line.length();
                    total_length += len;
                    max_length = std::max(max_length, len);
                    archored_line_num++;
                }
            }
            if (clipped_lines.size() > 0 && archored_line_num > 0) {
                candidates[i_angle].archored_percent = (double)archored_line_num / (double)clipped_lines.size();
            }
        }
        if (total_length == 0.)
            continue;

        have_coverage = true;
        candidates[i_angle].coverage = total_length;
        candidates[i_angle].max_length = max_length;
    }

    if (! have_coverage)
        return false;
    
    std::sort(candidates.begin(), candidates.end());
    
    size_t i_best = 0;
    for (size_t i = 1; i < candidates.size() && candidates[i_best].coverage - candidates[i].coverage < this->spacing; ++ i)
        if (candidates[i].max_length < candidates[i_best].max_length)
            i_best = i;

    this->angle = candidates[i_best].angle;
    if (this->angle >= PI)
        this->angle -= PI;
    
    #ifdef SLIC3R_DEBUG
    printf("  Optimal infill angle is %d degrees\n", (int)Slic3r::Geometry::rad2deg(this->angle));
    #endif
    
    return true;
}

std::vector<double> BridgeDetector::bridge_direction_candidates() const
{
    std::vector<double> angles;
    for (int i = 0; i <= PI/this->resolution; ++i)
        angles.push_back(i * this->resolution);
    
    {
        Lines lines = to_lines(this->expolygons);
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
            angles.push_back(line->direction());
    }
    
    for (const Polyline &edge : this->_edges)
        if (edge.first_point() != edge.last_point())
            angles.push_back(Line(edge.first_point(), edge.last_point()).direction());
    
    double min_resolution = PI/180.0;
    std::sort(angles.begin(), angles.end());
    for (size_t i = 1; i < angles.size(); ++i) {
        if (Slic3r::Geometry::directions_parallel(angles[i], angles[i-1], min_resolution)) {
            angles.erase(angles.begin() + i);
            --i;
        }
    }
    if (Slic3r::Geometry::directions_parallel(angles.front(), angles.back(), min_resolution))
        angles.pop_back();

    return angles;
}

static void get_trapezoids2(const ExPolygon& expoly, Polygons* polygons)
{
    Polygons     src_polygons = to_polygons(expoly);
    const Points pp = to_points(src_polygons);

    BoundingBox bb(pp);

    std::vector<coord_t> xx;
    xx.reserve(pp.size());
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p)
        xx.push_back(p->x());
    std::sort(xx.begin(), xx.end());

    Polygons rectangle;
    rectangle.emplace_back(Polygon());
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end()-1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x != next_x) {
            rectangle.front() = { { *x, bb.min.y() }, { next_x, bb.min.y() }, { next_x, bb.max.y() }, { *x, bb.max.y() } };
            polygons_append(*polygons, intersection(rectangle, src_polygons));
        }
    }
}

static void get_trapezoids2(const ExPolygon &expoly, Polygons* polygons, double angle)
{
    ExPolygon clone = expoly;
    clone.rotate(PI/2 - angle, Point(0,0));
    get_trapezoids2(clone, polygons);
    for (Polygon &polygon : *polygons)
        polygon.rotate(-(PI/2 - angle), Point(0,0));
}



void get_trapezoids3_half(const ExPolygon& expoly, Polygons* polygons, float spacing)
{

    Points pp = to_points(expoly);

    if (pp.empty()) return;

    BoundingBox bb(pp);

    coord_t min_x = pp[0].x(), max_x = pp[0].x();
    std::vector<coord_t> xx;
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        if (min_x > p->x()) min_x = p->x();
        if (max_x < p->x()) max_x = p->x();
    }
    for (coord_t x = min_x; x < max_x - (coord_t)(spacing / 2); x += (coord_t)spacing) {
        xx.push_back(x);
    }
    xx.push_back(max_x);
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end() - 1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x == next_x) continue;

        Polygon poly;
        poly.points.resize(4);
        poly[0].x() = *x + (coord_t)spacing / 4;
        poly[0].y() = bb.min(1);
        poly[1].x() = next_x - (coord_t)spacing / 4;
        poly[1].y() = bb.min(1);
        poly[2].x() = next_x - (coord_t)spacing / 4;
        poly[2].y() = bb.max(1);
        poly[3].x() = *x + (coord_t)spacing / 4;
        poly[3].y() = bb.max(1);

        polygons_append(*polygons, intersection(Polygons{ poly }, to_polygons(expoly)));
    }
}

Polygons BridgeDetector::coverage(double angle, bool precise) const
{
    if (angle == -1)
        angle = this->angle;

    Polygons covered;

    if (angle != -1) {
        Polygons anchors = to_polygons(this->_anchor_regions);
        polygons_rotate(anchors, PI / 2.0 - angle);

        for (ExPolygon expolygon : this->expolygons) {
            expolygon.rotate(PI / 2.0 - angle);
            for (ExPolygon &expoly : offset_ex(expolygon, 0.5f * float(this->spacing))) {
                Polygons trapezoids;
                if (!precise) get_trapezoids2(expoly, &trapezoids, PI / 2);
                else get_trapezoids3_half(expoly, &trapezoids, float(this->spacing));
                for (Polygon &trapezoid : trapezoids) {
                    size_t n_supported = 0;
                    if (!precise) {
                        for (const Line &supported_line : intersection_ln(trapezoid.lines(), anchors))
                            if (supported_line.length() >= this->spacing)
                                ++n_supported;
                    } else {
                        Polygons intersects = intersection(Polygons{trapezoid}, anchors);
                        n_supported = intersects.size();

                        if (n_supported >= 2) {
                            BoundingBox center_bound = intersects[0].bounding_box();
                            coord_t min_y = center_bound.center()(1), max_y = center_bound.center()(1);
                            for (Polygon &poly_bound : intersects) {
                                center_bound = poly_bound.bounding_box();
                                if (min_y > center_bound.center()(1)) min_y = center_bound.center()(1);
                                if (max_y < center_bound.center()(1)) max_y = center_bound.center()(1);
                            }
                            coord_t min_x = trapezoid[0](0), max_x = trapezoid[0](0);
                            for (Point &p : trapezoid.points) {
                                if (min_x > p(0)) min_x = p(0);
                                if (max_x < p(0)) max_x = p(0);
                            }
                            min_x -= (this->spacing / 4 + 1);
                            max_x += (this->spacing / 4 + 1);
                            coord_t mid_x = (min_x + max_x) / 2;
                            for (Point &p : trapezoid.points) {
                                if (p(1) < min_y) p(1) = min_y;
                                if (p(1) > max_y) p(1) = max_y;
                                if (p(0) > min_x && p(0) < mid_x) p(0) = min_x;
                                if (p(0) < max_x && p(0) > mid_x) p(0) = max_x;
                            }
                        }
                    }

                    if (n_supported >= 2) {
                        covered.push_back(std::move(trapezoid));
                    }
                }
            }
        }

        covered = union_(covered);
        polygons_rotate(covered, -(PI/2.0 - angle));
#if 0
        {
            my @lines = map @{$_->lines}, @$trapezoids;
            $_->rotate(-(PI/2 - $angle), [0,0]) for @lines;
            
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output(
                "coverage_" . rad2deg($angle) . ".svg",
                expolygons          => [$self->expolygon],
                green_expolygons    => $self->_anchor_regions,
                red_expolygons      => $coverage,
                lines               => \@lines,
            );
        }
#endif
    }
    return covered;
}

void
BridgeDetector::unsupported_edges(double angle, Polylines* unsupported) const
{
    if (angle == -1) angle = this->angle;
    if (angle == -1) return;

    Polygons grown_lower = offset(this->lower_slices, float(this->spacing));

    for (ExPolygons::const_iterator it_expoly = this->expolygons.begin(); it_expoly != this->expolygons.end(); ++ it_expoly) {    
        Lines unsupported_lines = to_lines(diff_pl(to_polylines(*it_expoly), grown_lower));
        for (const Line &line : unsupported_lines)
            if (! Slic3r::Geometry::directions_parallel(line.direction(), angle)) {
                unsupported->emplace_back(Polyline());
                unsupported->back().points.emplace_back(line.a);
                unsupported->back().points.emplace_back(line.b);
            }
    }
    
}

Polylines
BridgeDetector::unsupported_edges(double angle) const
{
    Polylines pp;
    this->unsupported_edges(angle, &pp);
    return pp;
}

}
