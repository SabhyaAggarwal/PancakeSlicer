#ifndef slic3r_VoronoiUtilsCgal_hpp_
#define slic3r_VoronoiUtilsCgal_hpp_

#include <boost/polygon/polygon.hpp>
#include <iterator>


namespace Slic3r::Geometry {
class VoronoiDiagram;

class VoronoiUtilsCgal
{
public:
    static bool is_voronoi_diagram_planar_intersection(const VoronoiDiagram &voronoi_diagram);

    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        bool>::type
    is_voronoi_diagram_planar_angle(const VoronoiDiagram &voronoi_diagram, SegmentIterator segment_begin, SegmentIterator segment_end);
};
}

#endif
