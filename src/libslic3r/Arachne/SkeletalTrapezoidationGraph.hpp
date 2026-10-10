//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef SKELETAL_TRAPEZOIDATION_GRAPH_H
#define SKELETAL_TRAPEZOIDATION_GRAPH_H

#include <optional>
#include <utility>

#include "utils/HalfEdgeGraph.hpp"
#include "SkeletalTrapezoidationEdge.hpp"
#include "SkeletalTrapezoidationJoint.hpp"
#include "libslic3r/Arachne/utils/HalfEdge.hpp"
#include "libslic3r/Arachne/utils/HalfEdgeNode.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r
{
class Line;
class Point;
};

namespace Slic3r::Arachne
{

class STHalfEdgeNode;

class STHalfEdge : public HalfEdge<SkeletalTrapezoidationJoint, SkeletalTrapezoidationEdge, STHalfEdgeNode, STHalfEdge>
{
    using edge_t = STHalfEdge;
    using node_t = STHalfEdgeNode;
public:
    STHalfEdge(SkeletalTrapezoidationEdge data);

    bool canGoUp(bool strict = false) const;

    bool isUpward() const;

    std::optional<coord_t> distToGoUp() const;

    STHalfEdge* getNextUnconnected();
};

class STHalfEdgeNode : public HalfEdgeNode<SkeletalTrapezoidationJoint, SkeletalTrapezoidationEdge, STHalfEdgeNode, STHalfEdge>
{
    using edge_t = STHalfEdge;
    using node_t = STHalfEdgeNode;
public:
    STHalfEdgeNode(SkeletalTrapezoidationJoint data, Point p);

    bool isMultiIntersection();

    bool isCentral() const;

    bool isLocalMaximum(bool strict = false) const;
};

class SkeletalTrapezoidationGraph: public HalfEdgeGraph<SkeletalTrapezoidationJoint, SkeletalTrapezoidationEdge, STHalfEdgeNode, STHalfEdge>
{
    using edge_t = STHalfEdge;
    using node_t = STHalfEdgeNode;
public:

    void collapseSmallEdges(coord_t snap_dist = 5);

    void makeRib(edge_t*& prev_edge, const Point &start_source_point, const Point &end_source_point);

    edge_t* insertNode(edge_t* edge, Point mid, coord_t mide_node_bead_count);

    std::pair<edge_t*, edge_t*> insertRib(edge_t& edge, node_t* mid_node);

protected:
    Line getSource(const edge_t& edge) const;
};

}
#endif 
