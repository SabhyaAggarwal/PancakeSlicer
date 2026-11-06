#ifndef slic3r_VoronoiUtils_hpp_
#define slic3r_VoronoiUtils_hpp_

#include <boost/polygon/polygon.hpp>
#include <iterator>
#include <limits>

#include "libslic3r/Geometry/Voronoi.hpp"
#include "libslic3r/Arachne/utils/PolygonsPointIndex.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Geometry {


template<typename PT> struct SegmentCellRange
{
    const PT             source_segment_start_point;
    const PT             source_segment_end_point;
    const VoronoiDiagram::edge_type *edge_begin = nullptr;
    const VoronoiDiagram::edge_type *edge_end   = nullptr;

    SegmentCellRange() = delete;
    explicit SegmentCellRange(const PT &source_segment_start_point, const PT &source_segment_end_point)
        : source_segment_start_point(source_segment_start_point), source_segment_end_point(source_segment_end_point)
    {}

    bool is_valid() const { return edge_begin && edge_end && edge_begin != edge_end; }
};


template<typename PT> struct PointCellRange
{
    const PT             source_point;
    const VoronoiDiagram::edge_type *edge_begin = nullptr;
    const VoronoiDiagram::edge_type *edge_end   = nullptr;

    PointCellRange() = delete;
    explicit PointCellRange(const PT &source_point) : source_point(source_point) {}

    bool is_valid() const { return edge_begin && edge_end && edge_begin != edge_end; }
};

class VoronoiUtils
{
public:
    static Vec2i64 to_point(const VoronoiDiagram::vertex_type *vertex);

    static Vec2i64 to_point(const VoronoiDiagram::vertex_type &vertex);

    static bool is_finite(const VoronoiDiagram::vertex_type &vertex);

    static VoronoiDiagram::vertex_type make_rotated_vertex(VoronoiDiagram::vertex_type &vertex, double angle);

    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        typename std::iterator_traits<SegmentIterator>::reference>::type
    get_source_segment(const VoronoiDiagram::cell_type &cell, SegmentIterator segment_begin, SegmentIterator segment_end);

    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        typename boost::polygon::segment_point_type<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type
    get_source_point(const VoronoiDiagram::cell_type &cell, SegmentIterator segment_begin, SegmentIterator segment_end);

    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        Arachne::PolygonsPointIndex>::type
    get_source_point_index(const VoronoiDiagram::cell_type &cell, SegmentIterator segment_begin, SegmentIterator segment_end);








    template<typename Segment>
    static typename boost::polygon::enable_if<typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
        typename boost::polygon::geometry_concept<Segment>::type>::type>::type,
        Points>::type
    discretize_parabola(const Point &source_point, const Segment &source_segment, const Point &start, const Point &end, coord_t approximate_step_size, float transitioning_angle);




















    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        Geometry::SegmentCellRange<
            typename boost::polygon::segment_point_type<typename std::iterator_traits<SegmentIterator>::value_type>::type>>::type
    compute_segment_cell_range(const VoronoiDiagram::cell_type &cell, SegmentIterator segment_begin, SegmentIterator segment_end);




















    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        Geometry::PointCellRange<
            typename boost::polygon::segment_point_type<typename std::iterator_traits<SegmentIterator>::value_type>::type>>::type
    compute_point_cell_range(const VoronoiDiagram::cell_type &cell, SegmentIterator segment_begin, SegmentIterator segment_end);

    template<typename T> static bool is_in_range(double value)
    {
        return double(std::numeric_limits<T>::lowest()) <= value && value <= double(std::numeric_limits<T>::max());
    }

    template<typename T> static bool is_in_range(const VoronoiDiagram::vertex_type &vertex)
    {
        return VoronoiUtils::is_finite(vertex) && is_in_range<T>(vertex.x()) && is_in_range<T>(vertex.y());
    }

    template<typename T> static bool is_in_range(const VoronoiDiagram::edge_type &edge)
    {
        if (edge.vertex0() == nullptr || edge.vertex1() == nullptr)
            return false;

        return is_in_range<T>(*edge.vertex0()) && is_in_range<T>(*edge.vertex1());
    }
};

}

#endif
