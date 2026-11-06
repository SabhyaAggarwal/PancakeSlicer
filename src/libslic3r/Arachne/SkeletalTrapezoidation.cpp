//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "SkeletalTrapezoidation.hpp"

#include <boost/log/trivial.hpp>
#include <boost/polygon/polygon.hpp>
#include <queue>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <math.h>
#include <list>
#include <memory>
#include <utility>
#include <cassert>
#include <cstdlib>
#include <vector>

#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"
#include "libslic3r/Geometry/VoronoiUtils.hpp"
#include "ankerl/unordered_dense.h"
#include "libslic3r/Arachne/SkeletalTrapezoidationEdge.hpp"
#include "libslic3r/Arachne/SkeletalTrapezoidationJoint.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Arachne/SkeletalTrapezoidationGraph.hpp"
#include "libslic3r/Arachne/utils/PolygonsSegmentIndex.hpp"

#ifndef NDEBUG
    #include "libslic3r/EdgeGrid.hpp"
#endif

#define SKELETAL_TRAPEZOIDATION_BEAD_SEARCH_MAX 1000

namespace Slic3r::Arachne
{

#ifdef ARACHNE_DEBUG
static void export_graph_to_svg(const std::string                                 &path,
                                SkeletalTrapezoidationGraph                       &graph,
                                const Polygons                                    &polys,
                                const std::vector<std::shared_ptr<LineJunctions>> &edge_junctions     = {},
                                const bool                                         beat_count         = true,
                                const bool                                         transition_middles = true,
                                const bool                                         transition_ends    = true)
{
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "green", "yellow"};
    coordf_t                       stroke_width = scale_(0.03);
    BoundingBox                    bbox         = get_extents(polys);
    for (const auto &node : graph.nodes)
        bbox.merge(node.p);

    bbox.offset(scale_(1.));

    ::Slic3r::SVG svg(path.c_str(), bbox);
    for (const auto &line : to_lines(polys))
        svg.draw(line, "gray", stroke_width);

    for (const auto &edge : graph.edges)
        svg.draw(Line(edge.from->p, edge.to->p), (edge.data.centralIsSet() && edge.data.isCentral()) ? "blue" : "cyan", stroke_width);

    for (const auto &line_junction : edge_junctions)
        for (const auto &extrusion_junction : *line_junction)
            svg.draw(extrusion_junction.p, "orange", coord_t(stroke_width * 2.));

    if (beat_count) {
        for (const auto &node : graph.nodes) {
            svg.draw(node.p, "red", coord_t(stroke_width * 1.6));
            svg.draw_text(node.p, std::to_string(node.data.bead_count).c_str(), "black", 1);
        }
    }

    if (transition_middles) {
        for (auto &edge : graph.edges) {
            if (std::shared_ptr<std::list<SkeletalTrapezoidationEdge::TransitionMiddle>> transitions = edge.data.getTransitions(); transitions) {
                for (auto &transition : *transitions) {
                    Line edge_line = Line(edge.to->p, edge.from->p);
                    double edge_length = edge_line.length();
                    Point   pt          = edge_line.a + (edge_line.vector().cast<double>() * (double(transition.pos) / edge_length)).cast<coord_t>();
                    svg.draw(pt, "magenta", coord_t(stroke_width * 1.5));
                    svg.draw_text(pt, std::to_string(transition.lower_bead_count).c_str(), "black", 1);
                }
            }
        }
    }

    if (transition_ends) {
        for (auto &edge : graph.edges) {
            if (std::shared_ptr<std::list<SkeletalTrapezoidationEdge::TransitionEnd>> transitions = edge.data.getTransitionEnds(); transitions) {
                for (auto &transition : *transitions) {
                    Line edge_line = Line(edge.to->p, edge.from->p);
                    double edge_length = edge_line.length();
                    Point   pt          = edge_line.a + (edge_line.vector().cast<double>() * (double(transition.pos) / edge_length)).cast<coord_t>();
                    svg.draw(pt, transition.is_lower_end ? "green" : "lime", coord_t(stroke_width * 1.5));
                    svg.draw_text(pt, std::to_string(transition.lower_bead_count).c_str(), "black", 1);
                }
            }
        }
    }
}
#endif

SkeletalTrapezoidation::node_t &SkeletalTrapezoidation::makeNode(const VD::vertex_type &vd_node, Point p) {
    auto he_node_it = vd_node_to_he_node.find(&vd_node);
    if (he_node_it == vd_node_to_he_node.end())
    {
        graph.nodes.emplace_front(SkeletalTrapezoidationJoint(), p);
        node_t& node = graph.nodes.front();
        vd_node_to_he_node.emplace(&vd_node, &node);
        return node;
    }
    else
    {
        return *he_node_it->second;
    }
}

void SkeletalTrapezoidation::transferEdge(const Point &from, const Point &to, const VD::edge_type &vd_edge, edge_t *&prev_edge, const Point &start_source_point, const Point &end_source_point, const std::vector<Segment> &segments) {
    auto he_edge_it = vd_edge_to_he_edge.find(vd_edge.twin());
    if (he_edge_it != vd_edge_to_he_edge.end())
    {
        edge_t* source_twin = he_edge_it->second;
        assert(source_twin);
        auto end_node_it = vd_node_to_he_node.find(vd_edge.vertex1());
        assert(end_node_it != vd_node_to_he_node.end());
        node_t* end_node = end_node_it->second;
        for (edge_t* twin = source_twin; ;twin = twin->prev->twin->prev)
        {
            if(!twin)
            {
                BOOST_LOG_TRIVIAL(warning) << "Encountered a voronoi edge without twin.";
                continue;
            }
            assert(twin);
            graph.edges.emplace_front(SkeletalTrapezoidationEdge());
            edge_t* edge = &graph.edges.front();
            edge->from = twin->to;
            edge->to = twin->from;
            edge->twin = twin;
            twin->twin = edge;
            edge->from->incident_edge = edge;

            if (prev_edge)
            {
                edge->prev = prev_edge;
                prev_edge->next = edge;
            }

            prev_edge = edge;

            if (prev_edge->to == end_node)
            {
                return;
            }

            if (!twin->prev || !twin->prev->twin || !twin->prev->twin->prev)
            {
                BOOST_LOG_TRIVIAL(error) << "Discretized segment behaves oddly!";
                return;
            }

            assert(twin->prev);
            assert(twin->prev->twin);
            assert(twin->prev->twin->prev);

            graph.makeRib(prev_edge, start_source_point, end_source_point);
        }
        assert(prev_edge);
    }
    else
    {
        Points discretized = discretize(vd_edge, segments);
        assert(discretized.size() >= 2);
        if(discretized.size() < 2)
        {
            BOOST_LOG_TRIVIAL(warning) << "Discretized Voronoi edge is degenerate.";
        }

        assert(!prev_edge || prev_edge->to);
        if(prev_edge && !prev_edge->to)
        {
            BOOST_LOG_TRIVIAL(warning) << "Previous edge doesn't go anywhere.";
        }
        node_t* v0 = (prev_edge)? prev_edge->to : &makeNode(*vd_edge.vertex0(), from);
        Point p0 = discretized.front();
        for (size_t p1_idx = 1; p1_idx < discretized.size(); p1_idx++)
        {
            Point p1 = discretized[p1_idx];
            node_t* v1;
            if (p1_idx < discretized.size() - 1)
            {
                graph.nodes.emplace_front(SkeletalTrapezoidationJoint(), p1);
                v1 = &graph.nodes.front();
            }
            else
            {
                v1 = &makeNode(*vd_edge.vertex1(), to);
            }

            graph.edges.emplace_front(SkeletalTrapezoidationEdge());
            edge_t* edge = &graph.edges.front();
            edge->from = v0;
            edge->to = v1;
            edge->from->incident_edge = edge;

            if (prev_edge)
            {
                edge->prev = prev_edge;
                prev_edge->next = edge;
            }

            prev_edge = edge;
            p0 = p1;
            v0 = v1;

            if (p1_idx < discretized.size() - 1) {
                graph.makeRib(prev_edge, start_source_point, end_source_point);
            }
        }
        assert(prev_edge);
        vd_edge_to_he_edge.emplace(&vd_edge, prev_edge);
    }
}

Points SkeletalTrapezoidation::discretize(const VD::edge_type& vd_edge, const std::vector<Segment>& segments)
{
    assert(Geometry::VoronoiUtils::is_in_range<coord_t>(vd_edge));





    const VD::cell_type *left_cell  = vd_edge.cell();
    const VD::cell_type *right_cell = vd_edge.twin()->cell();

    Point start = Geometry::VoronoiUtils::to_point(vd_edge.vertex0()).cast<coord_t>();
    Point end   = Geometry::VoronoiUtils::to_point(vd_edge.vertex1()).cast<coord_t>();

    bool point_left = left_cell->contains_point();
    bool point_right = right_cell->contains_point();
    if ((!point_left && !point_right) || vd_edge.is_secondary())
    {
        return Points({ start, end });
    }
    else if (point_left != point_right)
    {
        Point          p = Geometry::VoronoiUtils::get_source_point(*(point_left ? left_cell : right_cell), segments.begin(), segments.end());
        const Segment& s = Geometry::VoronoiUtils::get_source_segment(*(point_left ? right_cell : left_cell), segments.begin(), segments.end());
        return Geometry::VoronoiUtils::discretize_parabola(p, s, start, end, discretization_step_size, transitioning_angle);
    }
    else
    {



        Point   left_point    = Geometry::VoronoiUtils::get_source_point(*left_cell, segments.begin(), segments.end());
        Point   right_point   = Geometry::VoronoiUtils::get_source_point(*right_cell, segments.begin(), segments.end());
        coord_t d             = (right_point - left_point).cast<int64_t>().norm();
        Point   middle        = (left_point + right_point) / 2;
        Point   x_axis_dir    = perp(Point(right_point - left_point));
        coord_t x_axis_length = x_axis_dir.cast<int64_t>().norm();

        const auto projected_x = [x_axis_dir, x_axis_length, middle](Point from)
        {
            Point vec = from - middle;
            assert(( vec.cast<int64_t>().dot(x_axis_dir.cast<int64_t>())/ int64_t(x_axis_length)) <= std::numeric_limits<coord_t>::max());
            coord_t x = vec.cast<int64_t>().dot(x_axis_dir.cast<int64_t>()) / int64_t(x_axis_length);
            return x;
        };

        coord_t start_x = projected_x(start);
        coord_t end_x = projected_x(end);


        float bound = 0.5 / tan((M_PI - transitioning_angle) * 0.5);
        int64_t marking_start_x = - int64_t(d) * bound;
        int64_t marking_end_x = int64_t(d) * bound;

        assert((middle.cast<int64_t>() + x_axis_dir.cast<int64_t>() * marking_start_x / int64_t(x_axis_length)).x() <= std::numeric_limits<coord_t>::max());
        assert((middle.cast<int64_t>() + x_axis_dir.cast<int64_t>() * marking_start_x / int64_t(x_axis_length)).y() <= std::numeric_limits<coord_t>::max());
        assert((middle.cast<int64_t>() + x_axis_dir.cast<int64_t>() * marking_end_x / int64_t(x_axis_length)).x() <= std::numeric_limits<coord_t>::max());
        assert((middle.cast<int64_t>() + x_axis_dir.cast<int64_t>() * marking_end_x / int64_t(x_axis_length)).y() <= std::numeric_limits<coord_t>::max());
        Point marking_start = middle + (x_axis_dir.cast<int64_t>() * marking_start_x / int64_t(x_axis_length)).cast<coord_t>();
        Point marking_end = middle + (x_axis_dir.cast<int64_t>() * marking_end_x / int64_t(x_axis_length)).cast<coord_t>();
        int64_t direction = 1;

        if (start_x > end_x)
        {
            direction = -1;
            std::swap(marking_start, marking_end);
            std::swap(marking_start_x, marking_end_x);
        }


        Point a = start;
        Point b = end;
        Points ret;
        ret.emplace_back(a);


        bool add_marking_start = marking_start_x * direction > int64_t(start_x) * direction;
        bool add_marking_end = marking_end_x * direction > int64_t(start_x) * direction;


        Point ab = b - a;
        coord_t ab_size = ab.cast<int64_t>().norm();
        coord_t step_count = (ab_size + discretization_step_size / 2) / discretization_step_size;
        if (step_count % 2 == 1)
        {
            step_count++;
        }
        for (coord_t step = 1; step < step_count; step++)
        {
            Point here = a + (ab.cast<int64_t>() * int64_t(step) / int64_t(step_count)).cast<coord_t>();
            coord_t x_here = projected_x(here);
            if (add_marking_start && marking_start_x * direction < int64_t(x_here) * direction)
            {
                ret.emplace_back(marking_start);
                add_marking_start = false;
            }
            if (add_marking_end && marking_end_x * direction < int64_t(x_here) * direction)
            {
                ret.emplace_back(marking_end);
                add_marking_end = false;
            }
            ret.emplace_back(here);
        }
        if (add_marking_end && marking_end_x * direction < int64_t(end_x) * direction)
        {
            ret.emplace_back(marking_end);
        }
        ret.emplace_back(b);
        return ret;
    }
}

SkeletalTrapezoidation::SkeletalTrapezoidation(const Polygons& polys, const BeadingStrategy& beading_strategy,
                                               double transitioning_angle, coord_t discretization_step_size,
                                               coord_t transition_filter_dist, coord_t allowed_filter_deviation,
                                               coord_t beading_propagation_transition_dist
    ): transitioning_angle(transitioning_angle),
    discretization_step_size(discretization_step_size),
    transition_filter_dist(transition_filter_dist),
    allowed_filter_deviation(allowed_filter_deviation),
    beading_propagation_transition_dist(beading_propagation_transition_dist),
    beading_strategy(beading_strategy)
{
    constructFromPolygons(polys);
}

void SkeletalTrapezoidation::constructFromPolygons(const Polygons& polys)
{
#ifdef ARACHNE_DEBUG
    this->outline = polys;
#endif


    assert([&polys]() -> bool {
        EdgeGrid::Grid grid;
        grid.set_bbox(get_extents(polys));
        grid.create(polys, scaled<coord_t>(10.));
        return !grid.has_intersecting_edges();
    }());

    vd_edge_to_he_edge.clear();
    vd_node_to_he_node.clear();

    std::vector<Segment> segments;
    for (size_t poly_idx = 0; poly_idx < polys.size(); poly_idx++)
        for (size_t point_idx = 0; point_idx < polys[poly_idx].size(); point_idx++)
            segments.emplace_back(&polys, poly_idx, point_idx);

#ifdef ARACHNE_DEBUG
    {
        static int iRun = 0;
        BoundingBox bbox = get_extents(polys);
        SVG svg(debug_out_path("arachne_voronoi-input-%d.svg", iRun++).c_str(), bbox);
        svg.draw_outline(polys, "black", scaled<coordf_t>(0.03f));
    }
#endif

    VD voronoi_diagram;
    voronoi_diagram.construct_voronoi(segments.cbegin(), segments.cend());

#ifdef ARACHNE_DEBUG_VORONOI
    {
        static int iRun = 0;
        dump_voronoi_to_svg(debug_out_path("arachne_voronoi-diagram-%d.svg", iRun++).c_str(), voronoi_diagram, to_points(polys), to_lines(polys));
    }
#endif

    assert(this->graph.edges.empty() && this->graph.nodes.empty() && this->vd_edge_to_he_edge.empty() && this->vd_node_to_he_node.empty());
    for (const VD::cell_type &cell : voronoi_diagram.cells()) {
        if (!cell.incident_edge())
            continue;

        Point                start_source_point;
        Point                end_source_point;
        const VD::edge_type *starting_voronoi_edge = nullptr;
        const VD::edge_type *ending_voronoi_edge   = nullptr;


        if (cell.contains_point()) {
            Geometry::PointCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_point_cell_range(cell, segments.cbegin(), segments.cend());
            start_source_point    = cell_range.source_point;
            end_source_point      = cell_range.source_point;
            starting_voronoi_edge = cell_range.edge_begin;
            ending_voronoi_edge   = cell_range.edge_end;

            if (!cell_range.is_valid())
                continue;
        } else {
            assert(cell.contains_segment());
            Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, segments.cbegin(), segments.cend());
            assert(cell_range.is_valid());
            start_source_point    = cell_range.source_segment_start_point;
            end_source_point      = cell_range.source_segment_end_point;
            starting_voronoi_edge = cell_range.edge_begin;
            ending_voronoi_edge   = cell_range.edge_end;
        }

        if (!starting_voronoi_edge || !ending_voronoi_edge) {
            assert(false && "Each cell should start / end in a polygon vertex");
            continue;
        }


        assert(Geometry::VoronoiUtils::is_in_range<coord_t>(*starting_voronoi_edge));
        edge_t *prev_edge = nullptr;
        transferEdge(start_source_point, Geometry::VoronoiUtils::to_point(starting_voronoi_edge->vertex1()).cast<coord_t>(), *starting_voronoi_edge, prev_edge, start_source_point, end_source_point, segments);
        node_t *starting_node                    = vd_node_to_he_node[starting_voronoi_edge->vertex0()];
        starting_node->data.distance_to_boundary = 0;

        graph.makeRib(prev_edge, start_source_point, end_source_point);
        for (const VD::edge_type* vd_edge = starting_voronoi_edge->next(); vd_edge != ending_voronoi_edge; vd_edge = vd_edge->next()) {
            assert(vd_edge->is_finite());
            assert(Geometry::VoronoiUtils::is_in_range<coord_t>(*vd_edge));

            Point v1 = Geometry::VoronoiUtils::to_point(vd_edge->vertex0()).cast<coord_t>();
            Point v2 = Geometry::VoronoiUtils::to_point(vd_edge->vertex1()).cast<coord_t>();
            transferEdge(v1, v2, *vd_edge, prev_edge, start_source_point, end_source_point, segments);
            graph.makeRib(prev_edge, start_source_point, end_source_point);
        }

        transferEdge(Geometry::VoronoiUtils::to_point(ending_voronoi_edge->vertex0()).cast<coord_t>(), end_source_point, *ending_voronoi_edge, prev_edge, start_source_point, end_source_point, segments);
        prev_edge->to->data.distance_to_boundary = 0;
    }

#ifdef ARACHNE_DEBUG
    assert(Geometry::VoronoiUtilsCgal::is_voronoi_diagram_planar_intersection(voronoi_diagram));
#endif

    separatePointyQuadEndNodes();

    graph.collapseSmallEdges();



    for (edge_t& edge : graph.edges)
        if (!edge.prev)
            edge.from->incident_edge = &edge;
}

void SkeletalTrapezoidation::separatePointyQuadEndNodes()
{

    for (edge_t& edge : graph.edges)
    {
        if (edge.prev)
        {
            continue;
        }
        edge_t* quad_start = &edge;
        if (!quad_start->from->quad_start_seen)
        {
            quad_start->from->quad_start_seen = true;
        }
        else
        {
            graph.nodes.emplace_back(*quad_start->from);
            node_t* new_node = &graph.nodes.back();

            new_node->quad_start_seen = false;
            new_node->incident_edge = quad_start;
            quad_start->from = new_node;
            quad_start->twin->to = new_node;
        }
    }
}











void SkeletalTrapezoidation::generateToolpaths(std::vector<VariableWidthLines> &generated_toolpaths, bool filter_outermost_central_edges)
{
#ifdef ARACHNE_DEBUG
    static int iRun = 0;
#endif

    p_generated_toolpaths = &generated_toolpaths;

    updateIsCentral();

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-updateIsCentral-final-%d.svg", iRun), this->graph, this->outline);
#endif

    filterCentral(central_filter_dist());

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-filterCentral-final-%d.svg", iRun), this->graph, this->outline);
#endif

    if (filter_outermost_central_edges)
        filterOuterCentral();

    updateBeadCount();

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-updateBeadCount-final-%d.svg", iRun), this->graph, this->outline);
#endif

    filterNoncentralRegions();

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-filterNoncentralRegions-final-%d.svg", iRun), this->graph, this->outline);
#endif

    generateTransitioningRibs();

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateTransitioningRibs-final-%d.svg", iRun), this->graph, this->outline);
#endif

    generateExtraRibs();

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateExtraRibs-final-%d.svg", iRun), this->graph, this->outline);
#endif

    generateSegments();

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateSegments-final-%d.svg", iRun), this->graph, this->outline);
#endif

#ifdef ARACHNE_DEBUG
    ++iRun;
#endif
}

void SkeletalTrapezoidation::updateIsCentral()
{













    coord_t outer_edge_filter_length = beading_strategy.getTransitionThickness(0) / 2;

    float cap = sin(beading_strategy.getTransitioningAngle() * 0.5);
    for (edge_t& edge: graph.edges)
    {
        assert(edge.twin);
        if(!edge.twin)
        {
            BOOST_LOG_TRIVIAL(warning) << "Encountered a Voronoi edge without twin!";
            continue;
        }
        if(edge.twin->data.centralIsSet())
        {
            edge.data.setIsCentral(edge.twin->data.isCentral());
        }
        else if(edge.data.type == SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD)
        {
            edge.data.setIsCentral(false);
        }
        else if(std::max(edge.from->data.distance_to_boundary, edge.to->data.distance_to_boundary) < outer_edge_filter_length)
        {
            edge.data.setIsCentral(false);
        }
        else
        {
            Point a = edge.from->p;
            Point b = edge.to->p;
            Point ab = b - a;
            coord_t dR = std::abs(edge.to->data.distance_to_boundary - edge.from->data.distance_to_boundary);
            coord_t dD = ab.cast<int64_t>().norm();
            edge.data.setIsCentral(dR < dD * cap);
        }
    }
}

void SkeletalTrapezoidation::filterCentral(coord_t max_length)
{
    for (edge_t& edge : graph.edges)
    {
        if (isEndOfCentral(edge) && edge.to->isLocalMaximum() && !edge.to->isLocalMaximum())
        {
            filterCentral(edge.twin, 0, max_length);
        }
    }
}

bool SkeletalTrapezoidation::filterCentral(edge_t* starting_edge, coord_t traveled_dist, coord_t max_length)
{
    coord_t length = (starting_edge->from->p - starting_edge->to->p).cast<int64_t>().norm();
    if (traveled_dist + length > max_length)
    {
        return false;
    }

    bool should_dissolve = true;
    for (edge_t* next_edge = starting_edge->next; next_edge && next_edge != starting_edge->twin; next_edge = next_edge->twin->next)
    {
        if (next_edge->data.isCentral())
        {
            should_dissolve &= filterCentral(next_edge, traveled_dist + length, max_length);
        }
    }

    should_dissolve &= !starting_edge->to->isLocalMaximum();
    if (should_dissolve)
    {
        starting_edge->data.setIsCentral(false);
        starting_edge->twin->data.setIsCentral(false);
    }
    return should_dissolve;
}

void SkeletalTrapezoidation::filterOuterCentral()
{
    for (edge_t& edge : graph.edges)
    {
        if (!edge.prev)
        {
            edge.data.setIsCentral(false);
            edge.twin->data.setIsCentral(false);
        }
    }
}

void SkeletalTrapezoidation::updateBeadCount()
{
    for (edge_t& edge : graph.edges)
    {
        if (edge.data.isCentral())
        {
            edge.to->data.bead_count = beading_strategy.getOptimalBeadCount(edge.to->data.distance_to_boundary * 2);
        }
    }


    for (node_t& node : graph.nodes)
    {
        if (node.isLocalMaximum())
        {
            if (node.data.distance_to_boundary < 0)
            {
                BOOST_LOG_TRIVIAL(warning) << "Distance to boundary not yet computed for local maximum!";
                node.data.distance_to_boundary = std::numeric_limits<coord_t>::max();
                edge_t* edge = node.incident_edge;
                do
                {
                    node.data.distance_to_boundary = std::min(node.data.distance_to_boundary, edge->to->data.distance_to_boundary + coord_t((edge->from->p - edge->to->p).cast<int64_t>().norm()));
                } while (edge = edge->twin->next, edge != node.incident_edge);
            }
            coord_t bead_count = beading_strategy.getOptimalBeadCount(node.data.distance_to_boundary * 2);
            node.data.bead_count = bead_count;
        }
    }
}

void SkeletalTrapezoidation::filterNoncentralRegions()
{
    for (edge_t& edge : graph.edges)
    {
        if (!isEndOfCentral(edge))
        {
            continue;
        }
        if(edge.to->data.bead_count < 0 && edge.to->data.distance_to_boundary != 0)
        {
            BOOST_LOG_TRIVIAL(warning) << "Encountered an uninitialized bead at the boundary!";
        }
        assert(edge.to->data.bead_count >= 0 || edge.to->data.distance_to_boundary == 0);
        const coord_t max_dist = scaled<coord_t>(0.4);
        filterNoncentralRegions(&edge, edge.to->data.bead_count, 0, max_dist);
    }
}

bool SkeletalTrapezoidation::filterNoncentralRegions(edge_t* to_edge, coord_t bead_count, coord_t traveled_dist, coord_t max_dist)
{
    coord_t r = to_edge->to->data.distance_to_boundary;

    edge_t* next_edge = to_edge->next;
    for (; next_edge && next_edge != to_edge->twin; next_edge = next_edge->twin->next)
    {
        if (next_edge->to->data.distance_to_boundary >= r || shorter_then(next_edge->to->p - next_edge->from->p, scaled<coord_t>(0.01)))
        {
            break;
        }
    }
    if (next_edge == to_edge->twin || ! next_edge)
    {
        return false;
    }

    const coord_t length = (next_edge->to->p - next_edge->from->p).cast<int64_t>().norm();

    bool dissolve = false;
    if (next_edge->to->data.bead_count == bead_count)
    {
        dissolve = true;
    }
    else if (next_edge->to->data.bead_count < 0)
    {
        dissolve = filterNoncentralRegions(next_edge, bead_count, traveled_dist + length, max_dist);
    }
    else
    {

        dissolve = (traveled_dist + length < max_dist) && std::abs(next_edge->to->data.bead_count - bead_count) == 1;
    }

    if (dissolve)
    {
        next_edge->data.setIsCentral(true);
        next_edge->twin->data.setIsCentral(true);
        next_edge->to->data.bead_count = beading_strategy.getOptimalBeadCount(next_edge->to->data.distance_to_boundary * 2);
        next_edge->to->data.transition_ratio = 0;
    }
    return dissolve;
}

void SkeletalTrapezoidation::generateTransitioningRibs()
{


    ptr_vector_t<std::list<TransitionMiddle>> edge_transitions;
    generateTransitionMids(edge_transitions);

    for (edge_t& edge : graph.edges)
    {
        if (edge.data.isCentral() && edge.from->data.bead_count != edge.to->data.bead_count)
        {
            assert(edge.data.hasTransitions() || edge.twin->data.hasTransitions());
        }
    }

    filterTransitionMids();

#ifdef ARACHNE_DEBUG
    static int iRun = 0;
    export_graph_to_svg(debug_out_path("ST-generateTransitioningRibs-mids-%d.svg", iRun++), this->graph, this->outline);
#endif

    ptr_vector_t<std::list<TransitionEnd>> edge_transition_ends;
    generateAllTransitionEnds(edge_transition_ends);

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateTransitioningRibs-ends-%d.svg", iRun++), this->graph, this->outline);
#endif

    applyTransitions(edge_transition_ends);


#ifdef ARACHNE_DEBUG
    ++iRun;
#endif
}


void SkeletalTrapezoidation::generateTransitionMids(ptr_vector_t<std::list<TransitionMiddle>>& edge_transitions)
{
    for (edge_t& edge : graph.edges)
    {
        assert(edge.data.centralIsSet());
        if (!edge.data.isCentral())
        {
            continue;
        }
        coord_t start_R = edge.from->data.distance_to_boundary;
        coord_t end_R = edge.to->data.distance_to_boundary;
        int start_bead_count = edge.from->data.bead_count;
        int end_bead_count = edge.to->data.bead_count;

        if (start_R == end_R)
        {
            assert(edge.from->data.bead_count == edge.to->data.bead_count);
            if(edge.from->data.bead_count != edge.to->data.bead_count)
            {
                BOOST_LOG_TRIVIAL(warning) << "Bead count " << edge.from->data.bead_count << " is different from " << edge.to->data.bead_count << " even though distance to boundary is the same.";
            }
            continue;
        }
        else if (start_R > end_R)
        {
            continue;
        }

        if (edge.from->data.bead_count == edge.to->data.bead_count)
        {
            continue;
        }

        if (start_bead_count > beading_strategy.getOptimalBeadCount(start_R * 2)
            || end_bead_count > beading_strategy.getOptimalBeadCount(end_R * 2))
        {
            BOOST_LOG_TRIVIAL(error) << "transitioning segment overlap! (?)";
        }
        assert(start_R < end_R);
        if(start_R >= end_R)
        {
            BOOST_LOG_TRIVIAL(warning) << "Transitioning the wrong way around! This function expects to transition from small R to big R, but was transitioning from " << start_R << " to " << end_R;
        }
        coord_t edge_size = (edge.from->p - edge.to->p).cast<int64_t>().norm();
        for (int transition_lower_bead_count = start_bead_count; transition_lower_bead_count < end_bead_count; transition_lower_bead_count++)
        {
            coord_t mid_R = beading_strategy.getTransitionThickness(transition_lower_bead_count) / 2;
            if (mid_R > end_R)
            {
                BOOST_LOG_TRIVIAL(error) << "transition on segment lies outside of segment!";
                mid_R = end_R;
            }
            if (mid_R < start_R)
            {
                BOOST_LOG_TRIVIAL(error) << "transition on segment lies outside of segment!";
                mid_R = start_R;
            }
            coord_t mid_pos = int64_t(edge_size) * int64_t(mid_R - start_R) / int64_t(end_R - start_R);

            assert(mid_pos >= 0);
            assert(mid_pos <= edge_size);
            if(mid_pos < 0 || mid_pos > edge_size)
            {
                BOOST_LOG_TRIVIAL(warning) << "Transition mid is out of bounds of the edge.";
            }
            auto transitions = edge.data.getTransitions();
            constexpr bool ignore_empty = true;
            assert((! edge.data.hasTransitions(ignore_empty)) || mid_pos >= transitions->back().pos);
            if (! edge.data.hasTransitions(ignore_empty))
            {
                edge_transitions.emplace_back(std::make_shared<std::list<TransitionMiddle>>());
                edge.data.setTransitions(edge_transitions.back());
                transitions = edge.data.getTransitions();
            }
            transitions->emplace_back(mid_pos, transition_lower_bead_count, mid_R);
        }
        assert((edge.from->data.bead_count == edge.to->data.bead_count) || edge.data.hasTransitions());
    }
}

void SkeletalTrapezoidation::filterTransitionMids()
{
    for (edge_t& edge : graph.edges)
    {
        if (! edge.data.hasTransitions())
        {
            continue;
        }
        auto& transitions = *edge.data.getTransitions();


        assert(transitions.front().lower_bead_count <= transitions.back().lower_bead_count);
        assert(edge.from->data.distance_to_boundary <= edge.to->data.distance_to_boundary);

        const Point a = edge.from->p;
        const Point b = edge.to->p;
        Point ab = b - a;
        coord_t ab_size = ab.cast<int64_t>().norm();

        bool going_up = true;
        std::list<TransitionMidRef> to_be_dissolved_back = dissolveNearbyTransitions(&edge, transitions.back(), ab_size - transitions.back().pos, transition_filter_dist, going_up);
        bool should_dissolve_back = !to_be_dissolved_back.empty();
        for (TransitionMidRef& ref : to_be_dissolved_back)
        {
            dissolveBeadCountRegion(&edge, transitions.back().lower_bead_count + 1, transitions.back().lower_bead_count);
            ref.edge->data.getTransitions()->erase(ref.transition_it);
        }

        {
            coord_t trans_bead_count = transitions.back().lower_bead_count;
            coord_t upper_transition_half_length = (1.0 - beading_strategy.getTransitionAnchorPos(trans_bead_count)) * beading_strategy.getTransitioningLength(trans_bead_count);
            should_dissolve_back |= filterEndOfCentralTransition(&edge, ab_size - transitions.back().pos, upper_transition_half_length, trans_bead_count);
        }

        if (should_dissolve_back)
        {
            transitions.pop_back();
        }
        if (transitions.empty())
        {
            continue;
        }

        going_up = false;
        std::list<TransitionMidRef> to_be_dissolved_front = dissolveNearbyTransitions(edge.twin, transitions.front(), transitions.front().pos, transition_filter_dist, going_up);
        bool should_dissolve_front = !to_be_dissolved_front.empty();
        for (TransitionMidRef& ref : to_be_dissolved_front)
        {
            dissolveBeadCountRegion(edge.twin, transitions.front().lower_bead_count, transitions.front().lower_bead_count + 1);
            ref.edge->data.getTransitions()->erase(ref.transition_it);
        }

        {
            coord_t trans_bead_count = transitions.front().lower_bead_count;
            coord_t lower_transition_half_length = beading_strategy.getTransitionAnchorPos(trans_bead_count) * beading_strategy.getTransitioningLength(trans_bead_count);
            should_dissolve_front |= filterEndOfCentralTransition(edge.twin, transitions.front().pos, lower_transition_half_length, trans_bead_count + 1);
        }

        if (should_dissolve_front)
        {
            transitions.pop_front();
        }
        if (transitions.empty())
        {
            continue;
        }
    }
}

std::list<SkeletalTrapezoidation::TransitionMidRef> SkeletalTrapezoidation::dissolveNearbyTransitions(edge_t* edge_to_start, TransitionMiddle& origin_transition, coord_t traveled_dist, coord_t max_dist, bool going_up)
{
    std::list<TransitionMidRef> to_be_dissolved;
    if (traveled_dist > max_dist)
        return to_be_dissolved;

    bool should_dissolve = true;
    for (edge_t* edge = edge_to_start->next; edge && edge != edge_to_start->twin; edge = edge->twin->next){
        if (!edge->data.isCentral())
            continue;

        Point a = edge->from->p;
        Point b = edge->to->p;
        Point ab = b - a;
        coord_t ab_size = ab.cast<int64_t>().norm();
        bool is_aligned = edge->isUpward();
        edge_t* aligned_edge = is_aligned? edge : edge->twin;
        bool seen_transition_on_this_edge = false;

        const coord_t origin_radius          = origin_transition.feature_radius;
        const coord_t radius_here            = edge->from->data.distance_to_boundary;
        const bool    dissolve_result_is_odd = bool(origin_transition.lower_bead_count % 2) == going_up;
        const coord_t width_deviation        = std::abs(origin_radius - radius_here) * 2;
        const coord_t line_width_deviation = dissolve_result_is_odd ? width_deviation : width_deviation / 2;
        if (line_width_deviation > allowed_filter_deviation)
            should_dissolve = false;

        if (should_dissolve && aligned_edge->data.hasTransitions()) {
            auto& transitions = *aligned_edge->data.getTransitions();
            for (auto transition_it = transitions.begin(); transition_it != transitions.end(); ++ transition_it) {

                coord_t pos = is_aligned? transition_it->pos : ab_size - transition_it->pos;
                if (traveled_dist + pos < max_dist && transition_it->lower_bead_count == origin_transition.lower_bead_count) {
                    if (traveled_dist + pos < beading_strategy.getTransitioningLength(transition_it->lower_bead_count)) {

                        assert(going_up != is_aligned || transition_it->lower_bead_count == 0);
                    }
                    to_be_dissolved.emplace_back(aligned_edge, transition_it);
                    seen_transition_on_this_edge = true;
                }
            }
        }
        if (should_dissolve && !seen_transition_on_this_edge) {
            std::list<SkeletalTrapezoidation::TransitionMidRef> to_be_dissolved_here = dissolveNearbyTransitions(edge, origin_transition, traveled_dist + ab_size, max_dist, going_up);
            if (to_be_dissolved_here.empty()) {
                to_be_dissolved.clear();
                return to_be_dissolved;
            }
            to_be_dissolved.splice(to_be_dissolved.end(), to_be_dissolved_here);
            should_dissolve = should_dissolve && !to_be_dissolved.empty();
        }
    }

    if (!should_dissolve)
        to_be_dissolved.clear();

    return to_be_dissolved;
}


void SkeletalTrapezoidation::dissolveBeadCountRegion(edge_t* edge_to_start, coord_t from_bead_count, coord_t to_bead_count)
{
    assert(from_bead_count != to_bead_count);
    if (edge_to_start->to->data.bead_count != from_bead_count)
        return;

    edge_to_start->to->data.bead_count = to_bead_count;
    for (edge_t* edge = edge_to_start->next; edge && edge != edge_to_start->twin; edge = edge->twin->next)
    {
        if (!edge->data.isCentral())
        {
            continue;
        }
        dissolveBeadCountRegion(edge, from_bead_count, to_bead_count);
    }
}

bool SkeletalTrapezoidation::filterEndOfCentralTransition(edge_t* edge_to_start, coord_t traveled_dist, coord_t max_dist, coord_t replacing_bead_count)
{
    if (traveled_dist > max_dist)
    {
        return false;
    }

    bool is_end_of_central = true;
    bool should_dissolve = false;
    for (edge_t* next_edge = edge_to_start->next; next_edge && next_edge != edge_to_start->twin; next_edge = next_edge->twin->next)
    {
        if (next_edge->data.isCentral())
        {
            coord_t length = (next_edge->to->p - next_edge->from->p).cast<int64_t>().norm();
            should_dissolve |= filterEndOfCentralTransition(next_edge, traveled_dist + length, max_dist, replacing_bead_count);
            is_end_of_central = false;
        }
    }
    if (is_end_of_central && traveled_dist < max_dist)
    {
        should_dissolve = true;
    }

    if (should_dissolve)
    {
        edge_to_start->to->data.bead_count = replacing_bead_count;
    }
    return should_dissolve;
}

void SkeletalTrapezoidation::generateAllTransitionEnds(ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends)
{
    for (edge_t& edge : graph.edges)
    {
        if (! edge.data.hasTransitions())
        {
            continue;
        }
        auto& transition_positions = *edge.data.getTransitions();

        assert(edge.from->data.distance_to_boundary <= edge.to->data.distance_to_boundary);
        for (TransitionMiddle& transition_middle : transition_positions)
        {
            assert(transition_positions.front().pos <= transition_middle.pos);
            assert(transition_middle.pos <= transition_positions.back().pos);
            generateTransitionEnds(edge, transition_middle.pos, transition_middle.lower_bead_count, edge_transition_ends);
        }
    }
}

void SkeletalTrapezoidation::generateTransitionEnds(edge_t& edge, coord_t mid_pos, coord_t lower_bead_count, ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends)
{
    const Point a = edge.from->p;
    const Point b = edge.to->p;
    const Point ab = b - a;
    const coord_t ab_size = ab.cast<int64_t>().norm();

    const coord_t transition_length = beading_strategy.getTransitioningLength(lower_bead_count);
    const float transition_mid_position = beading_strategy.getTransitionAnchorPos(lower_bead_count);
    constexpr float inner_bead_width_ratio_after_transition = 1.0;

    constexpr coord_t start_rest = 0;
    const float mid_rest = transition_mid_position * inner_bead_width_ratio_after_transition;
    constexpr float end_rest = inner_bead_width_ratio_after_transition;

    {
        const coord_t start_pos = ab_size - mid_pos;
        const coord_t transition_half_length = transition_mid_position * int64_t(transition_length);
        const coord_t end_pos = start_pos + transition_half_length;
        generateTransitionEnd(*edge.twin, start_pos, end_pos, transition_half_length, mid_rest, start_rest, lower_bead_count, edge_transition_ends);
    }

    {
        const coord_t start_pos = mid_pos;
        const coord_t transition_half_length = (1.0 - transition_mid_position) * transition_length;
        const coord_t end_pos = mid_pos + transition_half_length;
#ifdef DEBUG
        if (! generateTransitionEnd(edge, start_pos, end_pos, transition_half_length, mid_rest, end_rest, lower_bead_count, edge_transition_ends))
        {
            BOOST_LOG_TRIVIAL(warning) << "There must have been at least one direction in which the bead count is increasing enough for the transition to happen!";
        }
#else
        generateTransitionEnd(edge, start_pos, end_pos, transition_half_length, mid_rest, end_rest, lower_bead_count, edge_transition_ends);
#endif
    }
}

bool SkeletalTrapezoidation::generateTransitionEnd(edge_t& edge, coord_t start_pos, coord_t end_pos, coord_t transition_half_length, double start_rest, double end_rest, coord_t lower_bead_count, ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends)
{
    Point a = edge.from->p;
    Point b = edge.to->p;
    Point ab = b - a;
    coord_t ab_size = ab.cast<int64_t>().norm();

    assert(start_pos <= ab_size);
    if(start_pos > ab_size)
    {
        BOOST_LOG_TRIVIAL(warning) << "Start position of edge is beyond edge range.";
    }

    bool going_up = end_rest > start_rest;

    assert(edge.data.isCentral());
    if (!edge.data.isCentral())
    {
        BOOST_LOG_TRIVIAL(warning) << "This function shouldn't generate ends in or beyond non-central regions.";
        return false;
    }

    if (end_pos > ab_size)
    {
        float rest = end_rest - (start_rest - end_rest) * (end_pos - ab_size) / (start_pos - end_pos);
        assert(rest >= 0);
        assert(rest <= std::max(end_rest, start_rest));
        assert(rest >= std::min(end_rest, start_rest));

        coord_t central_edge_count = 0;
        for (edge_t* outgoing = edge.next; outgoing && outgoing != edge.twin; outgoing = outgoing->twin->next)
        {
            if (!outgoing->data.isCentral()) continue;
            central_edge_count++;
        }

        bool is_only_going_down = true;
        bool has_recursed = false;
        for (edge_t* outgoing = edge.next; outgoing && outgoing != edge.twin;)
        {
            edge_t* next = outgoing->twin->next;
            if (!outgoing->data.isCentral())
            {
                outgoing = next;
                continue;
            }
            if (central_edge_count > 1 && going_up && isGoingDown(outgoing, 0, end_pos - ab_size + transition_half_length, lower_bead_count))
            {


                outgoing = next;
                continue;
            }
            bool is_going_down = generateTransitionEnd(*outgoing, 0, end_pos - ab_size, transition_half_length, rest, end_rest, lower_bead_count, edge_transition_ends);
            is_only_going_down &= is_going_down;
            outgoing = next;
            has_recursed = true;
        }
        if (!going_up || (has_recursed && !is_only_going_down))
        {
            edge.to->data.transition_ratio = rest;
            edge.to->data.bead_count = lower_bead_count;
        }
        return is_only_going_down;
    }
    else
    {
        bool is_lower_end = end_rest == 0;
        coord_t pos = -1;

        edge_t* upward_edge = nullptr;
        if (edge.isUpward())
        {
            upward_edge = &edge;
            pos = end_pos;
        }
        else
        {
            upward_edge = edge.twin;
            pos = ab_size - end_pos;
        }

        if(!upward_edge->data.hasTransitionEnds())
        {

            edge_transition_ends.emplace_back(std::make_shared<std::list<TransitionEnd>>());
            upward_edge->data.setTransitionEnds(edge_transition_ends.back());
        }
        auto transitions = upward_edge->data.getTransitionEnds();


        assert(ab_size == (edge.twin->from->p - edge.twin->to->p).cast<int64_t>().norm());
        assert(pos <= ab_size);
        if (transitions->empty() || pos < transitions->front().pos)
        {
            transitions->emplace_front(pos, lower_bead_count, is_lower_end);
        }
        else
        {
            transitions->emplace_back(pos, lower_bead_count, is_lower_end);
        }
        return false;
    }
}


bool SkeletalTrapezoidation::isGoingDown(edge_t* outgoing, coord_t traveled_dist, coord_t max_dist, coord_t lower_bead_count) const
{


    if (outgoing->to->data.distance_to_boundary == 0)
    {
        return true;
    }
    bool is_upward = outgoing->to->data.distance_to_boundary >= outgoing->from->data.distance_to_boundary;
    edge_t* upward_edge = is_upward? outgoing : outgoing->twin;
    if (outgoing->to->data.bead_count > lower_bead_count + 1)
    {
        assert(upward_edge->data.hasTransitions() && "If the bead count is going down there has to be a transition mid!");
        if(!upward_edge->data.hasTransitions())
        {
            BOOST_LOG_TRIVIAL(warning) << "If the bead count is going down there has to be a transition mid!";
        }
        return false;
    }
    coord_t length = (outgoing->to->p - outgoing->from->p).cast<int64_t>().norm();
    if (upward_edge->data.hasTransitions())
    {
        auto& transition_mids = *upward_edge->data.getTransitions();
        TransitionMiddle& mid = is_upward? transition_mids.front() : transition_mids.back();
        if (
            mid.lower_bead_count == lower_bead_count &&
            ((is_upward && mid.pos + traveled_dist < max_dist)
                || (!is_upward && length - mid.pos + traveled_dist < max_dist))
        )
        {
            return true;
        }
    }
    if (traveled_dist + length > max_dist)
    {
        return false;
    }
    if (outgoing->to->data.bead_count <= lower_bead_count
        && !(outgoing->to->data.bead_count == lower_bead_count && outgoing->to->data.transition_ratio > 0.0))
    {
        return true;
    }

    bool is_only_going_down = true;
    bool has_recursed = false;
    for (edge_t* next = outgoing->next; next && next != outgoing->twin; next = next->twin->next)
    {
        if (!next->data.isCentral())
        {
            continue;
        }
        bool is_going_down = isGoingDown(next, traveled_dist + length, max_dist, lower_bead_count);
        is_only_going_down &= is_going_down;
        has_recursed = true;
    }
    return has_recursed && is_only_going_down;
}

static inline Point normal(const Point& p0, coord_t len)
{
    int64_t _len = p0.cast<int64_t>().norm();
    if (_len < 1)
        return Point(len, 0);
    return (p0.cast<int64_t>() * int64_t(len) / _len).cast<coord_t>();
};

void SkeletalTrapezoidation::applyTransitions(ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends)
{
    const auto _snap_dist = snap_dist();
    for (edge_t& edge : graph.edges)
    {
        if (edge.twin->data.hasTransitionEnds())
        {
            coord_t length = (edge.from->p - edge.to->p).cast<int64_t>().norm();
            auto& twin_transition_ends = *edge.twin->data.getTransitionEnds();
            if (! edge.data.hasTransitionEnds())
            {
                edge_transition_ends.emplace_back(std::make_shared<std::list<TransitionEnd>>());
                edge.data.setTransitionEnds(edge_transition_ends.back());
            }
            auto& transition_ends = *edge.data.getTransitionEnds();
            for (TransitionEnd& end : twin_transition_ends)
            {
                transition_ends.emplace_back(length - end.pos, end.lower_bead_count, end.is_lower_end);
            }
            twin_transition_ends.clear();
        }
    }

    for (edge_t& edge : graph.edges)
    {
        if (! edge.data.hasTransitionEnds())
        {
            continue;
        }

        assert(edge.data.isCentral());

        auto& transitions = *edge.data.getTransitionEnds();
        transitions.sort([](const TransitionEnd& a, const TransitionEnd& b) { return a.pos < b.pos; } );

        node_t* from = edge.from;
        node_t* to = edge.to;
        Point a = from->p;
        Point b = to->p;
        Point ab = b - a;
        coord_t ab_size = (ab).cast<int64_t>().norm();

        edge_t* last_edge_replacing_input = &edge;
        for (TransitionEnd& transition_end : transitions)
        {
            coord_t new_node_bead_count = transition_end.is_lower_end? transition_end.lower_bead_count : transition_end.lower_bead_count + 1;
            coord_t end_pos = transition_end.pos;
            node_t* close_node = (end_pos < ab_size / 2)? from : to;
            if ((end_pos < _snap_dist || end_pos > ab_size - _snap_dist)
                && close_node->data.bead_count == new_node_bead_count
            )
            {
                assert(end_pos <= ab_size);
                close_node->data.transition_ratio = 0;
                continue;
            }
            Point mid = a + normal(ab, end_pos);

            assert(last_edge_replacing_input->data.isCentral());
            assert(last_edge_replacing_input->data.type != SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD);
            last_edge_replacing_input = graph.insertNode(last_edge_replacing_input, mid, new_node_bead_count);
            assert(last_edge_replacing_input->data.type != SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD);
            assert(last_edge_replacing_input->data.isCentral());
        }
    }
}

bool SkeletalTrapezoidation::isEndOfCentral(const edge_t& edge_to) const
{
    if (!edge_to.data.isCentral())
    {
        return false;
    }
    if (!edge_to.next)
    {
        return true;
    }
    for (const edge_t* edge = edge_to.next; edge && edge != edge_to.twin; edge = edge->twin->next)
    {
        if (edge->data.isCentral())
        {
            return false;
        }
        assert(edge->twin);
    }
    return true;
}

void SkeletalTrapezoidation::generateExtraRibs()
{
    const auto _snap_dist = snap_dist();
    for (auto edge_it = graph.edges.begin(); edge_it != graph.edges.end(); ++edge_it)
    {
        edge_t& edge = *edge_it;

        if (!edge.data.isCentral()
            || shorter_then(edge.to->p - edge.from->p, discretization_step_size)
            || edge.from->data.distance_to_boundary >= edge.to->data.distance_to_boundary)
        {
            continue;
        }


        std::vector<coord_t> rib_thicknesses = beading_strategy.getNonlinearThicknesses(edge.from->data.bead_count);

        if (rib_thicknesses.empty()) continue;


        node_t* from = edge.from;
        node_t* to = edge.to;
        Point a = from->p;
        Point b = to->p;
        Point ab = b - a;
        coord_t ab_size = ab.cast<int64_t>().norm();
        coord_t a_R = edge.from->data.distance_to_boundary;
        coord_t b_R = edge.to->data.distance_to_boundary;

        edge_t* last_edge_replacing_input = &edge;
        for (coord_t rib_thickness : rib_thicknesses)
        {
            if (rib_thickness / 2 <= a_R)
            {
                continue;
            }
            if (rib_thickness / 2 >= b_R)
            {
                break;
            }

            coord_t new_node_bead_count = std::min(edge.from->data.bead_count, edge.to->data.bead_count);
            coord_t end_pos = int64_t(ab_size) * int64_t(rib_thickness / 2 - a_R) / int64_t(b_R - a_R);
            assert(end_pos > 0);
            assert(end_pos < ab_size);
            node_t* close_node = (end_pos < ab_size / 2)? from : to;
            if ((end_pos < _snap_dist || end_pos > ab_size - _snap_dist)
                && close_node->data.bead_count == new_node_bead_count
            )
            {
                assert(end_pos <= ab_size);
                close_node->data.transition_ratio = 0;
                continue;
            }
            Point mid = a + normal(ab, end_pos);

            assert(last_edge_replacing_input->data.isCentral());
            assert(last_edge_replacing_input->data.type != SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD);
            last_edge_replacing_input = graph.insertNode(last_edge_replacing_input, mid, new_node_bead_count);
            assert(last_edge_replacing_input->data.type != SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD);
            assert(last_edge_replacing_input->data.isCentral());
        }
    }
}









void SkeletalTrapezoidation::generateSegments()
{
    std::vector<edge_t*> upward_quad_mids;
    for (edge_t& edge : graph.edges)
    {
        if (edge.prev && edge.next && edge.isUpward())
        {
            upward_quad_mids.emplace_back(&edge);
        }
    }

    std::sort(upward_quad_mids.begin(), upward_quad_mids.end(), [](edge_t* a, edge_t* b)
    {
        if (a->to->data.distance_to_boundary == b->to->data.distance_to_boundary)
        {
            if (a->from->data.distance_to_boundary == a->to->data.distance_to_boundary
                && b->from->data.distance_to_boundary == b->to->data.distance_to_boundary)
            {
                coord_t max = std::numeric_limits<coord_t>::max();
                coord_t a_dist_from_up = std::min(a->distToGoUp().value_or(max), a->twin->distToGoUp().value_or(max)) - (a->to->p - a->from->p).cast<int64_t>().norm();
                coord_t b_dist_from_up = std::min(b->distToGoUp().value_or(max), b->twin->distToGoUp().value_or(max)) - (b->to->p - b->from->p).cast<int64_t>().norm();
                return a_dist_from_up < b_dist_from_up;
            }
            else if (a->from->data.distance_to_boundary == a->to->data.distance_to_boundary)
            {
                return true;
            }
            else if (b->from->data.distance_to_boundary == b->to->data.distance_to_boundary)
            {
                return false;
            }
            else
            {

            }
        }
        return a->to->data.distance_to_boundary > b->to->data.distance_to_boundary;
    });

    ptr_vector_t<BeadingPropagation> node_beadings;
    {
        for (node_t& node : graph.nodes)
        {
            if (node.data.bead_count <= 0)
            {
                continue;
            }
            if (node.data.transition_ratio == 0)
            {
                node_beadings.emplace_back(new BeadingPropagation(beading_strategy.compute(node.data.distance_to_boundary * 2, node.data.bead_count)));
                node.data.setBeading(node_beadings.back());
                assert(node_beadings.back()->beading.total_thickness == node.data.distance_to_boundary * 2);
                if(node_beadings.back()->beading.total_thickness != node.data.distance_to_boundary * 2)
                {
                    BOOST_LOG_TRIVIAL(warning) << "If transitioning to an endpoint (ratio 0), the node should be exactly in the middle.";
                }
            }
            else
            {
                Beading low_count_beading = beading_strategy.compute(node.data.distance_to_boundary * 2, node.data.bead_count);
                Beading high_count_beading = beading_strategy.compute(node.data.distance_to_boundary * 2, node.data.bead_count + 1);
                Beading merged = interpolate(low_count_beading, 1.0 - node.data.transition_ratio, high_count_beading);
                node_beadings.emplace_back(new BeadingPropagation(merged));
                node.data.setBeading(node_beadings.back());
                assert(merged.total_thickness == node.data.distance_to_boundary * 2);
                if(merged.total_thickness != node.data.distance_to_boundary * 2)
                {
                    BOOST_LOG_TRIVIAL(warning) << "If merging two beads, the new bead must be exactly in the middle.";
                }
            }
        }
    }

#ifdef ARACHNE_DEBUG
    static int iRun = 0;
    export_graph_to_svg(debug_out_path("ST-generateSegments-before-propagation-%d.svg", iRun), this->graph, this->outline);
#endif

    propagateBeadingsUpward(upward_quad_mids, node_beadings);

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateSegments-upward-propagation-%d.svg", iRun), this->graph, this->outline);
#endif

    propagateBeadingsDownward(upward_quad_mids, node_beadings);

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateSegments-downward-propagation-%d.svg", iRun), this->graph, this->outline);
#endif

    ptr_vector_t<LineJunctions> edge_junctions;
    generateJunctions(node_beadings, edge_junctions);

#ifdef ARACHNE_DEBUG
    export_graph_to_svg(debug_out_path("ST-generateSegments-junctions-%d.svg", iRun), this->graph, this->outline, edge_junctions);
#endif

    connectJunctions(edge_junctions);

    generateLocalMaximaSingleBeads();

#ifdef ARACHNE_DEBUG
    ++iRun;
#endif
}

SkeletalTrapezoidation::edge_t* SkeletalTrapezoidation::getQuadMaxRedgeTo(edge_t* quad_start_edge)
{
    assert(quad_start_edge->prev == nullptr);
    assert(quad_start_edge->from->data.distance_to_boundary == 0);
    coord_t max_R = -1;
    edge_t* ret = nullptr;
    for (edge_t* edge = quad_start_edge; edge; edge = edge->next)
    {
        coord_t r = edge->to->data.distance_to_boundary;
        if (r > max_R)
        {
            max_R = r;
            ret = edge;
        }
    }

    if (!ret->next && ret->to->data.distance_to_boundary - scaled<coord_t>(0.005) < ret->from->data.distance_to_boundary)
    {
        ret = ret->prev;
    }
    assert(ret);
    assert(ret->next);
    return ret;
}

void SkeletalTrapezoidation::propagateBeadingsUpward(std::vector<edge_t*>& upward_quad_mids, ptr_vector_t<BeadingPropagation>& node_beadings)
{
    const auto _central_filter_dist = central_filter_dist();
    for (auto upward_quad_mids_it = upward_quad_mids.rbegin(); upward_quad_mids_it != upward_quad_mids.rend(); ++upward_quad_mids_it)
    {
        edge_t* upward_edge = *upward_quad_mids_it;
        if (upward_edge->to->data.bead_count >= 0)
        {
            continue;
        }
        if (! upward_edge->from->data.hasBeading())
        {
            continue;
        }
        BeadingPropagation& lower_beading = *upward_edge->from->data.getBeading();
        if (upward_edge->to->data.hasBeading())
        {
            continue;
        }
        assert((upward_edge->from->data.distance_to_boundary != upward_edge->to->data.distance_to_boundary || shorter_then(upward_edge->to->p - upward_edge->from->p, _central_filter_dist)) && "zero difference R edges should always be central");
        coord_t length = (upward_edge->to->p - upward_edge->from->p).cast<int64_t>().norm();
        BeadingPropagation upper_beading = lower_beading;
        upper_beading.dist_to_bottom_source += length;
        upper_beading.is_upward_propagated_only = true;
        node_beadings.emplace_back(new BeadingPropagation(upper_beading));
        upward_edge->to->data.setBeading(node_beadings.back());
        assert(upper_beading.beading.total_thickness <= upward_edge->to->data.distance_to_boundary * 2);
    }
}

void SkeletalTrapezoidation::propagateBeadingsDownward(std::vector<edge_t*>& upward_quad_mids, ptr_vector_t<BeadingPropagation>& node_beadings)
{
    for (edge_t* upward_quad_mid : upward_quad_mids)
    {

        if (!upward_quad_mid->data.isCentral())
        {

            if (upward_quad_mid->from->data.distance_to_boundary == upward_quad_mid->to->data.distance_to_boundary
                && upward_quad_mid->from->data.hasBeading()
                && ! upward_quad_mid->to->data.hasBeading()
            )
            {
                propagateBeadingsDownward(upward_quad_mid->twin, node_beadings);
            }
            else
            {
                propagateBeadingsDownward(upward_quad_mid, node_beadings);
            }
        }
    }
}

void SkeletalTrapezoidation::propagateBeadingsDownward(edge_t* edge_to_peak, ptr_vector_t<BeadingPropagation>& node_beadings)
{
    coord_t length = (edge_to_peak->to->p - edge_to_peak->from->p).cast<int64_t>().norm();
    BeadingPropagation& top_beading = *getOrCreateBeading(edge_to_peak->to, node_beadings);
    assert(top_beading.beading.total_thickness >= edge_to_peak->to->data.distance_to_boundary * 2);
    if(top_beading.beading.total_thickness < edge_to_peak->to->data.distance_to_boundary * 2)
    {
        BOOST_LOG_TRIVIAL(warning) << "Top bead is beyond the center of the total width.";
    }
    assert(!top_beading.is_upward_propagated_only);

    if(!edge_to_peak->from->data.hasBeading())
    {
        BeadingPropagation propagated_beading = top_beading;
        propagated_beading.dist_from_top_source += length;
        node_beadings.emplace_back(new BeadingPropagation(propagated_beading));
        edge_to_peak->from->data.setBeading(node_beadings.back());
        assert(propagated_beading.beading.total_thickness >= edge_to_peak->from->data.distance_to_boundary * 2);
        if(propagated_beading.beading.total_thickness < edge_to_peak->from->data.distance_to_boundary * 2)
        {
            BOOST_LOG_TRIVIAL(warning) << "Propagated bead is beyond the center of the total width.";
        }
    }
    else
    {
        BeadingPropagation& bottom_beading = *edge_to_peak->from->data.getBeading();
        coord_t total_dist = top_beading.dist_from_top_source + length + bottom_beading.dist_to_bottom_source;
        double ratio_of_top = static_cast<float>(bottom_beading.dist_to_bottom_source) / std::min(total_dist, beading_propagation_transition_dist);
        ratio_of_top = std::max(0.0, ratio_of_top);
        if (ratio_of_top >= 1.0)
        {
            bottom_beading = top_beading;
            bottom_beading.dist_from_top_source += length;
        }
        else
        {
            Beading merged_beading = interpolate(top_beading.beading, ratio_of_top, bottom_beading.beading, edge_to_peak->from->data.distance_to_boundary);
            bottom_beading = BeadingPropagation(merged_beading);
            bottom_beading.is_upward_propagated_only = false;
            assert(merged_beading.total_thickness >= edge_to_peak->from->data.distance_to_boundary * 2);
            if(merged_beading.total_thickness < edge_to_peak->from->data.distance_to_boundary * 2)
            {
                BOOST_LOG_TRIVIAL(warning) << "Merged bead is beyond the center of the total width.";
            }
        }
    }
}


SkeletalTrapezoidation::Beading SkeletalTrapezoidation::interpolate(const Beading& left, double ratio_left_to_whole, const Beading& right, coord_t switching_radius)
{
    assert(ratio_left_to_whole >= 0.0 && ratio_left_to_whole <= 1.0);
    Beading ret = interpolate(left, ratio_left_to_whole, right);



    coord_t next_inset_idx;
    for (next_inset_idx = left.toolpath_locations.size() - 1; next_inset_idx >= 0; next_inset_idx--)
    {
        if (switching_radius > left.toolpath_locations[next_inset_idx])
        {
            break;
        }
    }
    if (next_inset_idx < 0)
    {
        assert(left.toolpath_locations.empty() || left.toolpath_locations.front() >= switching_radius);
        return ret;
    }
    if (next_inset_idx + 1 == coord_t(left.toolpath_locations.size()))
    {
        return ret;
    }


    if (next_inset_idx >= coord_t(ret.toolpath_locations.size()))
    {
        return ret;
    }
    assert(next_inset_idx < coord_t(left.toolpath_locations.size()));
    assert(left.toolpath_locations[next_inset_idx] <= switching_radius);
    assert(left.toolpath_locations[next_inset_idx + 1] >= switching_radius);
    if (ret.toolpath_locations[next_inset_idx] > switching_radius)
    {






        float new_ratio = static_cast<float>(switching_radius - right.toolpath_locations[next_inset_idx]) / static_cast<float>(left.toolpath_locations[next_inset_idx] - right.toolpath_locations[next_inset_idx]);
        new_ratio = std::min(1.0, new_ratio + 0.1);
        return interpolate(left, new_ratio, right);
    }
    return ret;
}


SkeletalTrapezoidation::Beading SkeletalTrapezoidation::interpolate(const Beading& left, double ratio_left_to_whole, const Beading& right)
{
    assert(ratio_left_to_whole >= 0.0 && ratio_left_to_whole <= 1.0);
    float ratio_right_to_whole = 1.0 - ratio_left_to_whole;

    Beading ret = (left.total_thickness > right.total_thickness)? left : right;
    for (size_t inset_idx = 0; inset_idx < std::min(left.bead_widths.size(), right.bead_widths.size()); inset_idx++)
    {
        if(left.bead_widths[inset_idx] == 0 || right.bead_widths[inset_idx] == 0)
        {
            ret.bead_widths[inset_idx] = 0;
        }
        else
        {
            ret.bead_widths[inset_idx] = ratio_left_to_whole * left.bead_widths[inset_idx] + ratio_right_to_whole * right.bead_widths[inset_idx];
        }
        ret.toolpath_locations[inset_idx] = ratio_left_to_whole * left.toolpath_locations[inset_idx] + ratio_right_to_whole * right.toolpath_locations[inset_idx];
    }
    return ret;
}

void SkeletalTrapezoidation::generateJunctions(ptr_vector_t<BeadingPropagation>& node_beadings, ptr_vector_t<LineJunctions>& edge_junctions)
{
    for (edge_t& edge_ : graph.edges)
    {
        edge_t* edge = &edge_;
        if (edge->from->data.distance_to_boundary > edge->to->data.distance_to_boundary)
        {
            continue;
        }

        coord_t start_R = edge->to->data.distance_to_boundary;
        coord_t end_R = edge->from->data.distance_to_boundary;

        if ((edge->from->data.bead_count == edge->to->data.bead_count && edge->from->data.bead_count >= 0)
            || end_R >= start_R)
        {
            continue;
        }

        Beading* beading = &getOrCreateBeading(edge->to, node_beadings)->beading;
        edge_junctions.emplace_back(std::make_shared<LineJunctions>());
        edge_.data.setExtrusionJunctions(edge_junctions.back());
        LineJunctions& ret = *edge_junctions.back();

        assert(beading->total_thickness >= edge->to->data.distance_to_boundary * 2);
        if(beading->total_thickness < edge->to->data.distance_to_boundary * 2)
        {
            BOOST_LOG_TRIVIAL(warning) << "Generated junction is beyond the center of total width.";
        }

        Point a = edge->to->p;
        Point b = edge->from->p;
        Point ab = b - a;

        const size_t num_junctions = beading->toolpath_locations.size();
        size_t junction_idx;

        for (junction_idx = (std::max(size_t(1), beading->toolpath_locations.size()) - 1) / 2; junction_idx < num_junctions; junction_idx--)
        {
            coord_t bead_R = beading->toolpath_locations[junction_idx];



            if (bead_R <= start_R + 1)
            {
                break;
            }
        }



        if (junction_idx + 1 < num_junctions
            && beading->toolpath_locations[junction_idx + 1] <= start_R + scaled<coord_t>(0.005)
            && beading->total_thickness < start_R + scaled<coord_t>(0.005)
        )
        {
            junction_idx++;
        }

        for (; junction_idx < num_junctions; junction_idx--)
        {
            coord_t bead_R = beading->toolpath_locations[junction_idx];
            assert(bead_R >= 0);
            if (bead_R < end_R)
            {
                break;
            }
            Point junction(a + (ab.cast<int64_t>() * int64_t(bead_R - start_R) / int64_t(end_R - start_R)).cast<coord_t>());
            if (bead_R > start_R - scaled<coord_t>(0.005))
            {
                junction = a;
            }
            ret.emplace_back(junction, beading->bead_widths[junction_idx], junction_idx);
        }
    }
}

std::shared_ptr<SkeletalTrapezoidationJoint::BeadingPropagation> SkeletalTrapezoidation::getOrCreateBeading(node_t* node, ptr_vector_t<BeadingPropagation>& node_beadings)
{
    if (! node->data.hasBeading())
    {
        if (node->data.bead_count == -1)
        {
            const coord_t nearby_dist = scaled<coord_t>(0.1);
            auto nearest_beading = getNearestBeading(node, nearby_dist);
            if (nearest_beading)
            {
                return nearest_beading;
            }


            bool has_central_edge = false;
            bool first = true;
            coord_t dist = std::numeric_limits<coord_t>::max();
            for (edge_t* edge = node->incident_edge; edge && (first || edge != node->incident_edge); edge = edge->twin->next)
            {
                if (edge->data.isCentral())
                {
                    has_central_edge = true;
                }
                assert(edge->to->data.distance_to_boundary >= 0);
                dist = std::min(dist, edge->to->data.distance_to_boundary + coord_t((edge->to->p - edge->from->p).cast<int64_t>().norm()));
                first = false;
            }
            if (!has_central_edge)
            {
                BOOST_LOG_TRIVIAL(error) << "Unknown beading for non-central node!";
            }
            assert(dist != std::numeric_limits<coord_t>::max());
            node->data.bead_count = beading_strategy.getOptimalBeadCount(dist * 2);
        }
        assert(node->data.bead_count != -1);
        node_beadings.emplace_back(new BeadingPropagation(beading_strategy.compute(node->data.distance_to_boundary * 2, node->data.bead_count)));
        node->data.setBeading(node_beadings.back());
    }
    assert(node->data.hasBeading());
    return node->data.getBeading();
}

std::shared_ptr<SkeletalTrapezoidationJoint::BeadingPropagation> SkeletalTrapezoidation::getNearestBeading(node_t* node, coord_t max_dist)
{
    struct DistEdge
    {
        edge_t* edge_to;
        coord_t dist;
        DistEdge(edge_t* edge_to, coord_t dist)
        : edge_to(edge_to), dist(dist)
        {}
    };

    auto compare = [](const DistEdge& l, const DistEdge& r) -> bool { return l.dist > r.dist; };
    std::priority_queue<DistEdge, std::vector<DistEdge>, decltype(compare)> further_edges(compare);
    bool first = true;
    for (edge_t* outgoing = node->incident_edge; outgoing && (first || outgoing != node->incident_edge); outgoing = outgoing->twin->next)
    {
        further_edges.emplace(outgoing, (outgoing->to->p - outgoing->from->p).cast<int64_t>().norm());
        first = false;
    }

    for (coord_t counter = 0; counter < SKELETAL_TRAPEZOIDATION_BEAD_SEARCH_MAX; counter++)
    {
        if (further_edges.empty()) return nullptr;
        DistEdge here = further_edges.top();
        further_edges.pop();
        if (here.dist > max_dist) return nullptr;
        if (here.edge_to->to->data.hasBeading())
        {
            return here.edge_to->to->data.getBeading();
        }
        else
        {
            for (edge_t* further_edge = here.edge_to->next; further_edge && further_edge != here.edge_to->twin; further_edge = further_edge->twin->next)
            {
                further_edges.emplace(further_edge, here.dist + (further_edge->to->p - further_edge->from->p).cast<int64_t>().norm());
            }
        }
    }
    return nullptr;
}

void SkeletalTrapezoidation::addToolpathSegment(const ExtrusionJunction& from, const ExtrusionJunction& to, bool is_odd, bool force_new_path, bool from_is_3way, bool to_is_3way)
{
    if (from == to) return;

    std::vector<VariableWidthLines> &generated_toolpaths = *p_generated_toolpaths;

    size_t inset_idx = from.perimeter_index;
    if (inset_idx >= generated_toolpaths.size())
    {
        generated_toolpaths.resize(inset_idx + 1);
    }
    assert((generated_toolpaths[inset_idx].empty() || !generated_toolpaths[inset_idx].back().junctions.empty()) && "empty extrusion lines should never have been generated");
    if (generated_toolpaths[inset_idx].empty()
        || generated_toolpaths[inset_idx].back().is_odd != is_odd
        || generated_toolpaths[inset_idx].back().junctions.back().perimeter_index != inset_idx
    )
    {
        force_new_path = true;
    }
    if (!force_new_path
        && shorter_then(generated_toolpaths[inset_idx].back().junctions.back().p - from.p, scaled<coord_t>(0.010))
        && std::abs(generated_toolpaths[inset_idx].back().junctions.back().w - from.w) < scaled<coord_t>(0.010)
        && ! from_is_3way
    )
    {
        generated_toolpaths[inset_idx].back().junctions.push_back(to);
    }
    else if (!force_new_path
             && shorter_then(generated_toolpaths[inset_idx].back().junctions.back().p - to.p, scaled<coord_t>(0.010))
             && std::abs(generated_toolpaths[inset_idx].back().junctions.back().w - to.w) < scaled<coord_t>(0.010)
             && ! to_is_3way
    )
    {
        if ( ! is_odd)
        {
            BOOST_LOG_TRIVIAL(error) << "Reversing even wall line causes it to be printed CCW instead of CW!";
        }
        generated_toolpaths[inset_idx].back().junctions.push_back(from);
    }
    else
    {
        generated_toolpaths[inset_idx].emplace_back(inset_idx, is_odd);
        generated_toolpaths[inset_idx].back().junctions.push_back(from);
        generated_toolpaths[inset_idx].back().junctions.push_back(to);
    }
};

void SkeletalTrapezoidation::connectJunctions(ptr_vector_t<LineJunctions>& edge_junctions)
{
    using EdgeSet = ankerl::unordered_dense::set<edge_t*>;

    EdgeSet unprocessed_quad_starts(graph.edges.size() * 5 / 2);
    for (edge_t& edge : graph.edges)
    {
        if (!edge.prev)
        {
            unprocessed_quad_starts.emplace(&edge);
        }
    }

    EdgeSet passed_odd_edges;

    while (!unprocessed_quad_starts.empty())
    {
        edge_t* poly_domain_start = *unprocessed_quad_starts.begin();
        edge_t* quad_start = poly_domain_start;
        bool new_domain_start = true;
        do
        {
            edge_t* quad_end = quad_start;
            while (quad_end->next)
            {
                quad_end = quad_end->next;
            }

            edge_t* edge_to_peak = getQuadMaxRedgeTo(quad_start);

            edge_t* edge_from_peak = edge_to_peak->next; assert(edge_from_peak);

            unprocessed_quad_starts.erase(quad_start);

            if (! edge_to_peak->data.hasExtrusionJunctions())
            {
                edge_junctions.emplace_back(std::make_shared<LineJunctions>());
                edge_to_peak->data.setExtrusionJunctions(edge_junctions.back());
            }


            const LineJunctions *from_junctions_ptr = edge_to_peak->data.getExtrusionJunctions().get();
            LineJunctions        from_merged;
            if (! edge_from_peak->twin->data.hasExtrusionJunctions())
            {
                edge_junctions.emplace_back(std::make_shared<LineJunctions>());
                edge_from_peak->twin->data.setExtrusionJunctions(edge_junctions.back());
            }

            const LineJunctions *to_junctions_ptr = edge_from_peak->twin->data.getExtrusionJunctions().get();
            LineJunctions        to_merged;
            if (edge_to_peak->prev)
            {
                from_merged = *from_junctions_ptr;
                const LineJunctions &from_prev_junctions = *edge_to_peak->prev->data.getExtrusionJunctions();
                while (!from_merged.empty() && !from_prev_junctions.empty() && from_merged.back().perimeter_index <= from_prev_junctions.front().perimeter_index)
                {
                    from_merged.pop_back();
                }
                from_merged.reserve(from_merged.size() + from_prev_junctions.size());
                from_merged.insert(from_merged.end(), from_prev_junctions.begin(), from_prev_junctions.end());
                from_junctions_ptr = &from_merged;
                assert(!edge_to_peak->prev->prev);
                if(edge_to_peak->prev->prev)
                {
                    BOOST_LOG_TRIVIAL(warning) << "The edge we're about to connect is already connected.";
                }
            }
            if (edge_from_peak->next)
            {
                to_merged = *to_junctions_ptr;
                const LineJunctions &to_next_junctions = *edge_from_peak->next->twin->data.getExtrusionJunctions();
                while (!to_merged.empty() && !to_next_junctions.empty() && to_merged.back().perimeter_index <= to_next_junctions.front().perimeter_index)
                {
                    to_merged.pop_back();
                }
                to_merged.reserve(to_merged.size() + to_next_junctions.size());
                to_merged.insert(to_merged.end(), to_next_junctions.begin(), to_next_junctions.end());
                to_junctions_ptr = &to_merged;
                assert(!edge_from_peak->next->next);
                if(edge_from_peak->next->next)
                {
                    BOOST_LOG_TRIVIAL(warning) << "The edge we're about to connect is already connected!";
                }
            }
            const LineJunctions &from_junctions = *from_junctions_ptr;
            const LineJunctions &to_junctions   = *to_junctions_ptr;
            assert(std::abs(int(from_junctions.size()) - int(to_junctions.size())) <= 1);
            if(std::abs(int(from_junctions.size()) - int(to_junctions.size())) > 1)
            {
                BOOST_LOG_TRIVIAL(warning) << "Can't create a transition when connecting two perimeters where the number of beads differs too much! " << from_junctions.size() << " vs. " << to_junctions.size();
            }

            size_t segment_count = std::min(from_junctions.size(), to_junctions.size());
            for (size_t junction_rev_idx = 0; junction_rev_idx < segment_count; junction_rev_idx++)
            {
                const ExtrusionJunction& from = from_junctions[from_junctions.size() - 1 - junction_rev_idx];
                const ExtrusionJunction& to = to_junctions[to_junctions.size() - 1 - junction_rev_idx];
                assert(from.perimeter_index == to.perimeter_index);
                if(from.perimeter_index != to.perimeter_index)
                {
                    BOOST_LOG_TRIVIAL(warning) << "Connecting two perimeters with different indices! Perimeter " << from.perimeter_index << " and " << to.perimeter_index;
                }
                const bool from_is_odd =
                    quad_start->to->data.bead_count > 0 && quad_start->to->data.bead_count % 2 == 1
                    && quad_start->to->data.transition_ratio == 0
                    && junction_rev_idx == segment_count - 1
                    && shorter_then(from.p - quad_start->to->p, scaled<coord_t>(0.005));
                const bool to_is_odd =
                    quad_end->from->data.bead_count > 0 && quad_end->from->data.bead_count % 2 == 1
                    && quad_end->from->data.transition_ratio == 0
                    && junction_rev_idx == segment_count - 1
                    && shorter_then(to.p - quad_end->from->p, scaled<coord_t>(0.005));
                const bool is_odd_segment = from_is_odd && to_is_odd;
                if (is_odd_segment
                    && passed_odd_edges.count(quad_start->next->twin) > 0)
                {
                    continue;
                }
                bool from_is_3way = from_is_odd && quad_start->to->isMultiIntersection();
                bool to_is_3way = to_is_odd && quad_end->from->isMultiIntersection();
                passed_odd_edges.emplace(quad_start->next);

                addToolpathSegment(from, to, is_odd_segment, new_domain_start, from_is_3way, to_is_3way);
            }
            new_domain_start = false;
        }
        while(quad_start = quad_start->getNextUnconnected(), quad_start != poly_domain_start);
    }
}

void SkeletalTrapezoidation::generateLocalMaximaSingleBeads()
{
    std::vector<VariableWidthLines> &generated_toolpaths = *p_generated_toolpaths;

    for (auto& node : graph.nodes)
    {
        if (! node.data.hasBeading())
        {
            continue;
        }
        Beading& beading = node.data.getBeading()->beading;
        if (beading.bead_widths.size() % 2 == 1 && node.isLocalMaximum(true) && !node.isCentral())
        {
            const size_t inset_index = beading.bead_widths.size() / 2;
            constexpr bool is_odd = true;
            if (inset_index >= generated_toolpaths.size())
            {
                generated_toolpaths.resize(inset_index + 1);
            }
            generated_toolpaths[inset_index].emplace_back(inset_index, is_odd);
            ExtrusionLine& line = generated_toolpaths[inset_index].back();
            const coord_t width = beading.bead_widths[inset_index];




            const coord_t     r          = width / 8;
            constexpr coord_t n_segments = 6;
            for (coord_t segment = 0; segment < n_segments; segment++) {
                float a = 2.0 * M_PI / n_segments * segment;
                line.junctions.emplace_back(node.p + Point(r * cos(a), r * sin(a)), width, inset_index);
            }
        }
    }
}







}
