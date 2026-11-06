//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_TREE_NODE_H
#define LIGHTNING_TREE_NODE_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <vector>

#include "../../EdgeGrid.hpp"
#include "../../Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "SVG.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Line.hpp"



namespace Slic3r::FillLightning
{

inline coord_t locator_cell_size() { return scaled<coord_t>(4.); }

class Node;

using NodeSPtr = std::shared_ptr<Node>;















class Node : public std::enable_shared_from_this<Node>
{
public:

    template<typename ...Arg> NodeSPtr static create(Arg&&...arg)
    {
        struct EnableMakeShared : public Node
        {
            explicit EnableMakeShared(Arg&&...arg) : Node(std::forward<Arg>(arg)...) {}
        };
        return std::make_shared<EnableMakeShared>(std::forward<Arg>(arg)...);
    }






    const Point& getLocation() const { return m_p; }





    void setLocation(const Point& p) { m_p = p; }







    NodeSPtr addChild(const Point& p);






    NodeSPtr addChild(NodeSPtr& new_child);



















    void propagateToNextLayer
    (
        std::vector<NodeSPtr>& next_trees,
        const Polygons& next_outlines,
        const EdgeGrid::Grid& outline_locator,
        coord_t prune_distance,
        coord_t smooth_magnitude,
        coord_t max_remove_colinear_dist
    ) const;












    void visitBranches(const std::function<void(const Point&, const Point&)>& visitor) const;











    void visitNodes(const std::function<void(NodeSPtr)>& visitor);











    coord_t getWeightedDistance(const Point& unsupported_location, const coord_t& supporting_radius) const;







    bool isRoot() const { return m_is_root; }








    void reroot(const NodeSPtr &new_parent = nullptr);






    NodeSPtr closestNode(const Point& loc);










    bool hasOffspring(const NodeSPtr& to_be_checked) const;

    Node() = delete;

protected:






    explicit Node(const Point& p, const std::optional<Point>& last_grounding_location = std::nullopt);






    NodeSPtr deepCopy() const;




    bool realign(const Polygons& outlines, const EdgeGrid::Grid& outline_locator, std::vector<NodeSPtr>& rerooted_parts);

    struct RectilinearJunction
    {
        coord_t total_recti_dist;
        Point junction_loc;
    };







    void straighten(coord_t magnitude, coord_t max_remove_colinear_dist);







    RectilinearJunction straighten(coord_t magnitude, const Point& junction_above, coord_t accumulated_dist, int64_t max_remove_colinear_dist2);




    coord_t prune(const coord_t& distance);

public:









    void convertToPolylines(Polylines &output, coord_t line_overlap) const;





    const std::optional<Point>& getLastGroundingLocation() const { return m_last_grounding_location; }

    void draw_tree(SVG& svg) { for (auto& child : m_children) { svg.draw(Line(m_p, child->getLocation()), "yellow"); child->draw_tree(svg); } }

protected:











    void convertToPolylines(size_t long_line_idx, Polylines &output, std::mt19937_64 &rng) const;

    void removeJunctionOverlap(Polylines &polylines, coord_t line_overlap) const;

    bool m_is_root;
    Point m_p;
    std::weak_ptr<Node> m_parent;
    std::vector<NodeSPtr> m_children;

    std::optional<Point> m_last_grounding_location;

    friend BoundingBox get_extents(const NodeSPtr &root_node);
    friend BoundingBox get_extents(const std::vector<NodeSPtr> &tree_roots);

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
    friend void export_to_svg(const NodeSPtr &root_node, Slic3r::SVG &svg);
    friend void export_to_svg(const std::string &path, const Polygons &contour, const std::vector<NodeSPtr> &root_nodes);
#endif
};

bool inside(const Polygons &polygons, const Point &p);
bool lineSegmentPolygonsIntersection(const Point& a, const Point& b, const EdgeGrid::Grid& outline_locator, Point& result, coord_t within_max_dist);

inline BoundingBox get_extents(const NodeSPtr &root_node)
{
    BoundingBox bbox;
    for (const NodeSPtr &children : root_node->m_children)
        bbox.merge(get_extents(children));
    bbox.merge(root_node->getLocation());
    return bbox;
}

inline BoundingBox get_extents(const std::vector<NodeSPtr> &tree_roots)
{
    BoundingBox bbox;
    for (const NodeSPtr &root_node : tree_roots)
        bbox.merge(get_extents(root_node));
    return bbox;
}

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
void export_to_svg(const NodeSPtr &root_node, SVG &svg);
void export_to_svg(const std::string &path, const Polygons &contour, const std::vector<NodeSPtr> &root_nodes);
#endif

}

#endif
