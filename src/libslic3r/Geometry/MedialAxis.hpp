#ifndef slic3r_Geometry_MedialAxis_hpp_
#define slic3r_Geometry_MedialAxis_hpp_

#include <stddef.h>
#include <utility>
#include <vector>
#include <cstddef>

#include "Voronoi.hpp"
#include "../ExPolygon.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r::Geometry {

class MedialAxis {
public:
    MedialAxis(double min_width, double max_width, const ExPolygon &expolygon);
    void build(ThickPolylines* polylines);
    void build(Polylines* polylines);

private:

    const ExPolygon     &m_expolygon;
    Lines                m_lines;

    double               m_min_width;
    double               m_max_width;


    using VD = VoronoiDiagram;
    VD                   m_vd;


    struct EdgeData {
        bool    active      { false };
        double  width_start { 0 };
        double  width_end   { 0 };
    };

    std::pair<EdgeData&, bool> edge_data(const VD::edge_type &edge) {
        size_t edge_id = &edge - &m_vd.edges().front();
        return { m_edge_data[edge_id / 2], (edge_id & 1) != 0 };
    }
    std::vector<EdgeData> m_edge_data;

    void process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline);
    bool validate_edge(const VD::edge_type* edge);
};

}

#endif
