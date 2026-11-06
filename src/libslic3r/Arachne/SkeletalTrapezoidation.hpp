//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef SKELETAL_TRAPEZOIDATION_H
#define SKELETAL_TRAPEZOIDATION_H

#include <boost/polygon/voronoi.hpp>
#include <ankerl/unordered_dense.h>
#include <memory>
#include <utility>
#include <list>
#include <vector>

#include "utils/PolygonsSegmentIndex.hpp"
#include "utils/ExtrusionJunction.hpp"
#include "utils/ExtrusionLine.hpp"
#include "SkeletalTrapezoidationEdge.hpp"
#include "SkeletalTrapezoidationJoint.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"
#include "SkeletalTrapezoidationGraph.hpp"
#include "../Geometry/Voronoi.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"




namespace Slic3r::Arachne {

using VD = Slic3r::Geometry::VoronoiDiagram;




















class SkeletalTrapezoidation
{
    using graph_t = SkeletalTrapezoidationGraph;
    using edge_t = STHalfEdge;
    using node_t = STHalfEdgeNode;
    using Beading = BeadingStrategy::Beading;
    using BeadingPropagation = SkeletalTrapezoidationJoint::BeadingPropagation;
    using TransitionMiddle = SkeletalTrapezoidationEdge::TransitionMiddle;
    using TransitionEnd = SkeletalTrapezoidationEdge::TransitionEnd;

    template<typename T>
    using ptr_vector_t = std::vector<std::shared_ptr<T>>;

    double  transitioning_angle;
    coord_t discretization_step_size;
    coord_t transition_filter_dist;
    coord_t allowed_filter_deviation;
    coord_t beading_propagation_transition_dist;

    inline coord_t central_filter_dist() { return scaled<coord_t>(0.02); }

    inline coord_t snap_dist() { return scaled<coord_t>(0.02); }









    const BeadingStrategy& beading_strategy;

public:
    using Segment  = PolygonsSegmentIndex;
    using NodeSet  = ankerl::unordered_dense::set<node_t*>;


















    SkeletalTrapezoidation(const Polygons& polys,
                           const BeadingStrategy& beading_strategy,
                           double transitioning_angle
    , coord_t discretization_step_size
    , coord_t transition_filter_dist
    , coord_t allowed_filter_deviation
    , coord_t beading_propagation_transition_dist);









    graph_t graph;









    void generateToolpaths(std::vector<VariableWidthLines> &generated_toolpaths, bool filter_outermost_central_edges = false);

#ifdef ARACHNE_DEBUG
    Polygons outline;
#endif

protected:



    struct TransitionMidRef
    {
        edge_t* edge;
        std::list<TransitionMiddle>::iterator transition_it;
        TransitionMidRef(edge_t* edge, std::list<TransitionMiddle>::iterator transition_it)
            : edge(edge)
            , transition_it(transition_it)
        {}
    };














    void constructFromPolygons(const Polygons& polys);





    ankerl::unordered_dense::map<const VD::edge_type *, edge_t *> vd_edge_to_he_edge;
    ankerl::unordered_dense::map<const VD::vertex_type *, node_t *> vd_node_to_he_node;
    node_t &makeNode(const VD::vertex_type &vd_node, Point p);




    std::vector<VariableWidthLines> *p_generated_toolpaths;





    void transferEdge(const Point &from, const Point &to, const VD::edge_type &vd_edge, edge_t *&prev_edge, const Point &start_source_point, const Point &end_source_point, const std::vector<Segment> &segments);


























    Points discretize(const VD::edge_type& segment, const std::vector<Segment>& segments);






    void separatePointyQuadEndNodes();




    void updateIsCentral();







    void filterCentral(coord_t max_length);






    bool filterCentral(edge_t* starting_edge, coord_t traveled_dist, coord_t max_length);









    void filterOuterCentral();





    void updateBeadCount();






    void filterNoncentralRegions();








    bool filterNoncentralRegions(edge_t* to_edge, coord_t bead_count, coord_t traveled_dist, coord_t max_dist);








    void generateTransitionMids(ptr_vector_t<std::list<TransitionMiddle>>& edge_transitions);








    void filterTransitionMids();















    std::list<TransitionMidRef> dissolveNearbyTransitions(edge_t* edge_to_start, TransitionMiddle& origin_transition, coord_t traveled_dist, coord_t max_dist, bool going_up);







    void dissolveBeadCountRegion(edge_t* edge_to_start, coord_t from_bead_count, coord_t to_bead_count);














    bool filterEndOfCentralTransition(edge_t* edge_to_start, coord_t traveled_dist, coord_t max_dist, coord_t replacing_bead_count);





    void generateAllTransitionEnds(ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);




    void applyTransitions(ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);











    void generateTransitioningRibs();










    void generateTransitionEnds(edge_t& edge, coord_t mid_R, coord_t transition_lower_bead_count, ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);





















    bool generateTransitionEnd(edge_t& edge, coord_t start_pos, coord_t end_pos, coord_t transition_half_length, double start_rest, double end_rest, coord_t transition_lower_bead_count, ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);


















    bool isGoingDown(edge_t* outgoing, coord_t traveled_dist, coord_t transition_half_length, coord_t lower_bead_count) const;








    bool isEndOfCentral(const edge_t& edge) const;








    void generateExtraRibs();








    void generateSegments();







    edge_t* getQuadMaxRedgeTo(edge_t* quad_start_edge);














    void propagateBeadingsUpward(std::vector<edge_t*>& upward_quad_mids, ptr_vector_t<BeadingPropagation>& node_beadings);












    void propagateBeadingsDownward(std::vector<edge_t*>& upward_quad_mids, ptr_vector_t<BeadingPropagation>& node_beadings);




    void propagateBeadingsDownward(edge_t* edge_to_peak, ptr_vector_t<BeadingPropagation>& node_beadings);


















    static Beading interpolate(const Beading& left, double ratio_left_to_whole, const Beading& right, coord_t switching_radius);












    static Beading interpolate(const Beading& left, double ratio_left_to_whole, const Beading& right);










    std::shared_ptr<BeadingPropagation> getOrCreateBeading(node_t* node, ptr_vector_t<BeadingPropagation>& node_beadings);










    std::shared_ptr<BeadingPropagation> getNearestBeading(node_t* node, coord_t max_dist);





    void generateJunctions(ptr_vector_t<BeadingPropagation>& node_beadings, ptr_vector_t<LineJunctions>& edge_junctions);











    void addToolpathSegment(const ExtrusionJunction& from, const ExtrusionJunction& to, bool is_odd, bool force_new_path, bool from_is_3way, bool to_is_3way);




    void connectJunctions(ptr_vector_t<LineJunctions>& edge_junctions);




    void generateLocalMaximaSingleBeads();
};

}
#endif
