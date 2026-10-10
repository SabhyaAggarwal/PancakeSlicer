//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_LAYER_H
#define LIGHTNING_LAYER_H

#include "../../EdgeGrid.hpp"
#include "../../Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polyline.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <list>
#include <unordered_map>
#include <optional>

namespace Slic3r::FillLightning
{

class Node;
using NodeSPtr = std::shared_ptr<Node>;
using SparseNodeGrid = std::unordered_multimap<Point, std::weak_ptr<Node>, PointHash>;

struct GroundingLocation
{
    NodeSPtr tree_node;
    std::optional<Point> boundary_location;
    Point p() const;
};

class Layer
{
public:
    std::vector<NodeSPtr> tree_roots;

    void generateNewTrees
    (
        const Polygons& current_overhang,
        const Polygons& current_outlines,
        const BoundingBox& current_outlines_bbox,
        const EdgeGrid::Grid& outline_locator,
        coord_t supporting_radius,
        coord_t wall_supporting_radius,
        const std::function<void()> &throw_on_cancel_callback
    );

    GroundingLocation getBestGroundingLocation
    (
        const Point& unsupported_location,
        const Polygons& current_outlines,
        const BoundingBox& current_outlines_bbox,
        const EdgeGrid::Grid& outline_locator,
        coord_t supporting_radius,
        coord_t wall_supporting_radius,
        const SparseNodeGrid& tree_node_locator,
        const NodeSPtr& exclude_tree = nullptr
    );

    bool attach(const Point& unsupported_location, const GroundingLocation& ground, NodeSPtr& new_child, NodeSPtr& new_root);

    void reconnectRoots
    (
        std::vector<NodeSPtr>& to_be_reconnected_tree_roots,
        const Polygons& current_outlines,
        const BoundingBox& current_outlines_bbox,
        const EdgeGrid::Grid& outline_locator,
        coord_t supporting_radius,
        coord_t wall_supporting_radius
    );

    Polylines convertToLines(const Polygons& limit_to_outline, coord_t line_overlap) const;

    coord_t getWeightedDistance(const Point& boundary_loc, const Point& unsupported_location);

    void fillLocator(SparseNodeGrid& tree_node_locator, const BoundingBox& current_outlines_bbox);
};

}

#endif
