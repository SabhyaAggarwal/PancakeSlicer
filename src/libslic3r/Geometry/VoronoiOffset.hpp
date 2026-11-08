#ifndef slic3r_VoronoiOffset_hpp_
#define slic3r_VoronoiOffset_hpp_

#include <boost/polygon/polygon.hpp>
#include <cmath>
#include <vector>

#include "Voronoi.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

namespace Voronoi {

using VD = Slic3r::Geometry::VoronoiDiagram;

inline const Point& contour_point(const VD::cell_type &cell, const Line &line)
	{ return ((cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line.a : line.b); }
inline Point&       contour_point(const VD::cell_type &cell, Line &line)
	{ return ((cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line.a : line.b); }

inline const Point& contour_point(const VD::cell_type &cell, const Lines &lines)
	{ return contour_point(cell, lines[cell.source_index()]); }
inline Point&       contour_point(const VD::cell_type &cell, Lines &lines)
	{ return contour_point(cell, lines[cell.source_index()]); }

inline Vec2d 		vertex_point(const VD::vertex_type &v) { return Vec2d(v.x(), v.y()); }
inline Vec2d 		vertex_point(const VD::vertex_type *v) { return Vec2d(v->x(), v->y()); }

enum class VertexCategory : unsigned char
{
	OnContour,
	Inside,
	Outside,
	Unknown,
};

enum class EdgeCategory : unsigned char
{
	PointsToContour,
	PointsInside,
	PointsOutside,
	Unknown
};

enum class CellCategory : unsigned char
{
	Boundary,
	Inside,
	Outside,
	Unknown
};

inline VertexCategory 	vertex_category(const VD::vertex_type &v)
	{ return static_cast<VertexCategory>(v.color()); }
inline VertexCategory 	vertex_category(const VD::vertex_type *v)
	{ return static_cast<VertexCategory>(v->color()); }
inline void 		  	set_vertex_category(VD::vertex_type &v, VertexCategory c)
	{ v.color(static_cast<VD::vertex_type::color_type>(c)); }
inline void 		  	set_vertex_category(VD::vertex_type *v, VertexCategory c)
	{ v->color(static_cast<VD::vertex_type::color_type>(c)); }

inline EdgeCategory 	edge_category(const VD::edge_type &e)
	{ return static_cast<EdgeCategory>(e.color()); }
inline EdgeCategory 	edge_category(const VD::edge_type *e)
	{ return static_cast<EdgeCategory>(e->color()); }
inline void 			set_edge_category(VD::edge_type &e, EdgeCategory c)
	{ e.color(static_cast<VD::edge_type::color_type>(c)); }
inline void 			set_edge_category(VD::edge_type *e, EdgeCategory c)
	{ e->color(static_cast<VD::edge_type::color_type>(c)); }

inline CellCategory   	cell_category(const VD::cell_type &v)
	{ return static_cast<CellCategory>(v.color()); }
inline CellCategory   	cell_category(const VD::cell_type *v)
	{ return static_cast<CellCategory>(v->color()); }
inline void 		  	set_cell_category(const VD::cell_type &v, CellCategory c)
	{ v.color(static_cast<VD::cell_type::color_type>(c)); }
inline void 		  	set_cell_category(const VD::cell_type *v, CellCategory c)
	{ v->color(static_cast<VD::cell_type::color_type>(c)); }

void reset_inside_outside_annotations(VD &vd);

void annotate_inside_outside(VD &vd, const Lines &lines);

std::vector<double> signed_vertex_distances(const VD &vd, const Lines &lines);

static inline bool edge_offset_no_intersection(const Vec2d &intersection_point)
	{ return std::isnan(intersection_point.x()); }
static inline bool edge_offset_has_intersection(const Vec2d &intersection_point)
	{ return ! edge_offset_no_intersection(intersection_point); }
std::vector<Vec2d> edge_offset_contour_intersections(
	const VD &vd, const Lines &lines, const std::vector<double> &distances,
	double offset_distance);

std::vector<Vec2d> skeleton_edges_rough(
    const VD                    &vd,
    const Lines                 &lines,
    const double                 threshold_alpha);

Polygons offset(
    const Geometry::VoronoiDiagram  &vd,
    const Lines                     &lines,
    const std::vector<double>       &signed_vertex_distances,
    double                           offset_distance,
    double                           discretization_error);

Polygons offset(
	const VD 		&vd, 
	const Lines 	&lines, 
	double 			 offset_distance, 
	double 			 discretization_error);

}

}

#endif
