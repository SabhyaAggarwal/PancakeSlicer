//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "Layer.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "DistanceField.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polyline.hpp"
#include "TreeNode.hpp"

#include "../../ClipperUtils.hpp"
#include "../../Geometry.hpp"
#include "libslic3r/libslic3r.h"

#include <cassert>
#include <functional>
#include <cstddef>
#include <limits>
#include <algorithm>
#include <optional>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range2d.h>
#include <mutex>
#include <utility>
#include <vector>

namespace Slic3r::FillLightning {

coord_t Layer::getWeightedDistance(const Point& boundary_loc, const Point& unsupported_location)
{
    return coord_t((boundary_loc - unsupported_location).cast<double>().norm());
}

Point GroundingLocation::p() const
{
    assert(tree_node || boundary_location);
    return tree_node ? tree_node->getLocation() : *boundary_location;
}

inline static Point to_grid_point(const Point &point, const BoundingBox &bbox)
{
    return (point - bbox.min) / locator_cell_size();
}

void Layer::fillLocator(SparseNodeGrid &tree_node_locator, const BoundingBox& current_outlines_bbox)
{
    std::function<void(NodeSPtr)> add_node_to_locator_func = [&tree_node_locator, &current_outlines_bbox](const NodeSPtr &node) {
        tree_node_locator.insert(std::make_pair(to_grid_point(node->getLocation(), current_outlines_bbox), node));
    };
    for (auto& tree : tree_roots)
        tree->visitNodes(add_node_to_locator_func);
}

void Layer::generateNewTrees
(
    const Polygons& current_overhang,
    const Polygons& current_outlines,
    const BoundingBox& current_outlines_bbox,
    const EdgeGrid::Grid& outlines_locator,
    const coord_t supporting_radius,
    const coord_t wall_supporting_radius,
    const std::function<void()> &throw_on_cancel_callback
)
{
    DistanceField distance_field(supporting_radius, current_outlines, current_outlines_bbox, current_overhang);
    throw_on_cancel_callback();

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator, current_outlines_bbox);



    size_t unsupported_cell_idx = 0;
    Point  unsupported_location;
    while (distance_field.tryGetNextPoint(&unsupported_location, &unsupported_cell_idx, unsupported_cell_idx)) {
        throw_on_cancel_callback();
        GroundingLocation grounding_loc = getBestGroundingLocation(
            unsupported_location, current_outlines, current_outlines_bbox, outlines_locator, supporting_radius, wall_supporting_radius, tree_node_locator);

        NodeSPtr new_parent;
        NodeSPtr new_child;
        this->attach(unsupported_location, grounding_loc, new_child, new_parent);
        tree_node_locator.insert(std::make_pair(to_grid_point(new_child->getLocation(), current_outlines_bbox), new_child));
        if (new_parent)
            tree_node_locator.insert(std::make_pair(to_grid_point(new_parent->getLocation(), current_outlines_bbox), new_parent));

        distance_field.update(grounding_loc.p(), unsupported_location);
    }

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_to_svg(debug_out_path("FillLightning-TreeNodes-%d.svg", iRun++), current_outlines, this->tree_roots);
    }
#endif
}

static bool polygonCollidesWithLineSegment(const Point &from, const Point &to, const EdgeGrid::Grid &loc_to_line)
{
    struct Visitor {
        explicit Visitor(const EdgeGrid::Grid &grid, const Line &line) : grid(grid), line(line) {}

        bool operator()(coord_t iy, coord_t ix) {

            auto cell_data_range = grid.cell_data_range(iy, ix);
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++ it_contour_and_segment) {

                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, line.a, line.b)) {
                    this->intersect = true;
                    return false;
                }
            }

            return true;
        }

        const EdgeGrid::Grid& grid;
        Line                  line;
        bool                  intersect = false;
    } visitor(loc_to_line, {from, to});

    loc_to_line.visit_cells_intersecting_line(from, to, visitor);
    return visitor.intersect;
}

GroundingLocation Layer::getBestGroundingLocation
(
    const Point& unsupported_location,
    const Polygons& current_outlines,
    const BoundingBox& current_outlines_bbox,
    const EdgeGrid::Grid& outline_locator,
    const coord_t supporting_radius,
    const coord_t wall_supporting_radius,
    const SparseNodeGrid& tree_node_locator,
    const NodeSPtr& exclude_tree
)
{

    Point node_location;
    {
        double d2 = std::numeric_limits<double>::max();
        for (const Polygon &contour : current_outlines)
            if (contour.size() > 2) {
                Point prev = contour.points.back();
                for (const Point &p2 : contour.points) {
                    Point closest_point;
                    if (double d = line_alg::distance_to_squared(Line{prev, p2}, unsupported_location, &closest_point); d < d2) {
                        d2 = d;
                        node_location = closest_point;
                    }
                    prev = p2;
                }
            }
    }

    const auto within_dist = coord_t((node_location - unsupported_location).cast<double>().norm());

    NodeSPtr sub_tree{nullptr};
    coord_t  current_dist = getWeightedDistance(node_location, unsupported_location);
    if (current_dist >= wall_supporting_radius) {
        const coord_t search_radius = std::min(current_dist, within_dist);
        BoundingBox region(unsupported_location - Point(search_radius, search_radius), unsupported_location + Point(search_radius + locator_cell_size(), search_radius + locator_cell_size()));
        region.min = to_grid_point(region.min, current_outlines_bbox);
        region.max = to_grid_point(region.max, current_outlines_bbox);

        Point      current_dist_grid_addr{std::numeric_limits<coord_t>::lowest(), std::numeric_limits<coord_t>::lowest()};
        std::mutex current_dist_mutex;
        tbb::parallel_for(tbb::blocked_range2d<coord_t>(region.min.y(), region.max.y(), region.min.x(), region.max.x()), [&current_dist, current_dist_copy = current_dist, &current_dist_mutex, &sub_tree, &current_dist_grid_addr, &exclude_tree = std::as_const(exclude_tree), &outline_locator = std::as_const(outline_locator), &supporting_radius = std::as_const(supporting_radius), &tree_node_locator = std::as_const(tree_node_locator), &unsupported_location = std::as_const(unsupported_location)](const tbb::blocked_range2d<coord_t> &range) -> void {
            for (coord_t grid_addr_y = range.rows().begin(); grid_addr_y < range.rows().end(); ++grid_addr_y)
                for (coord_t grid_addr_x = range.cols().begin(); grid_addr_x < range.cols().end(); ++grid_addr_x) {
                    const Point local_grid_addr{grid_addr_x, grid_addr_y};
                    NodeSPtr    local_sub_tree{nullptr};
                    coord_t     local_current_dist = current_dist_copy;
                    const auto  it_range           = tree_node_locator.equal_range(local_grid_addr);
                    for (auto it = it_range.first; it != it_range.second; ++it) {
                        const NodeSPtr candidate_sub_tree = it->second.lock();
                        if ((candidate_sub_tree && candidate_sub_tree != exclude_tree) &&
                            !(exclude_tree && exclude_tree->hasOffspring(candidate_sub_tree)) &&
                            !polygonCollidesWithLineSegment(unsupported_location, candidate_sub_tree->getLocation(), outline_locator)) {
                            if (const coord_t candidate_dist = candidate_sub_tree->getWeightedDistance(unsupported_location, supporting_radius); candidate_dist < local_current_dist) {
                                local_current_dist = candidate_dist;
                                local_sub_tree     = candidate_sub_tree;
                            }
                        }
                    }






                    {
                        std::lock_guard<std::mutex> lock(current_dist_mutex);
                        if (local_current_dist < current_dist ||
                            (local_current_dist == current_dist && (grid_addr_y < current_dist_grid_addr.y() ||
                              (grid_addr_y == current_dist_grid_addr.y() && grid_addr_x < current_dist_grid_addr.x())))) {
                            current_dist           = local_current_dist;
                            sub_tree               = local_sub_tree;
                            current_dist_grid_addr = local_grid_addr;
                        }
                    }
                }
        });
    }

    return ! sub_tree ?
        GroundingLocation{ nullptr, node_location } :
        GroundingLocation{ sub_tree, std::optional<Point>() };
}

bool Layer::attach(
    const Point& unsupported_location,
    const GroundingLocation& grounding_loc,
    NodeSPtr& new_child,
    NodeSPtr& new_root)
{

    if (grounding_loc.boundary_location) {
        new_root = Node::create(grounding_loc.p(), std::make_optional(grounding_loc.p()));
        new_child = new_root->addChild(unsupported_location);
        tree_roots.push_back(new_root);
        return true;
    } else {
        new_child = grounding_loc.tree_node->addChild(unsupported_location);
        return false;
    }
}

void Layer::reconnectRoots
(
    std::vector<NodeSPtr>& to_be_reconnected_tree_roots,
    const Polygons& current_outlines,
    const BoundingBox& current_outlines_bbox,
    const EdgeGrid::Grid& outline_locator,
    const coord_t supporting_radius,
    const coord_t wall_supporting_radius
)
{
    constexpr coord_t tree_connecting_ignore_offset = 100;

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator, current_outlines_bbox);

    const coord_t within_max_dist = outline_locator.resolution() * 2;
    for (const auto &root_ptr : to_be_reconnected_tree_roots)
    {
        auto old_root_it = std::find(tree_roots.begin(), tree_roots.end(), root_ptr);

        if (root_ptr->getLastGroundingLocation())
        {
            const Point& ground_loc = *root_ptr->getLastGroundingLocation();
            if (ground_loc != root_ptr->getLocation())
            {
                Point new_root_pt;

                if (lineSegmentPolygonsIntersection(root_ptr->getLocation(), ground_loc, outline_locator, new_root_pt, within_max_dist)) {
                    auto new_root = Node::create(new_root_pt, new_root_pt);
                    root_ptr->addChild(new_root);
                    new_root->reroot();

                    tree_node_locator.insert(std::make_pair(to_grid_point(new_root->getLocation(), current_outlines_bbox), new_root));

                    *old_root_it = std::move(new_root);
                    continue;
                }
            }
        }

        const coord_t tree_connecting_ignore_width = wall_supporting_radius - tree_connecting_ignore_offset;
        GroundingLocation ground =
            getBestGroundingLocation
            (
                root_ptr->getLocation(),
                current_outlines,
                current_outlines_bbox,
                outline_locator,
                supporting_radius,
                tree_connecting_ignore_width,
                tree_node_locator,
                root_ptr
            );
        if (ground.boundary_location)
        {
            if (*ground.boundary_location == root_ptr->getLocation())
                continue;

            auto new_root = Node::create(ground.p(), ground.p());
            auto attach_ptr = root_ptr->closestNode(new_root->getLocation());
            attach_ptr->reroot();

            new_root->addChild(attach_ptr);
            tree_node_locator.insert(std::make_pair(to_grid_point(new_root->getLocation(), current_outlines_bbox), new_root));

            *old_root_it = std::move(new_root);
        }
        else
        {
            assert(ground.tree_node);
            assert(ground.tree_node != root_ptr);
            assert(!root_ptr->hasOffspring(ground.tree_node));
            assert(!ground.tree_node->hasOffspring(root_ptr));

            auto attach_ptr = root_ptr->closestNode(ground.tree_node->getLocation());
            attach_ptr->reroot();

            ground.tree_node->addChild(attach_ptr);


            *old_root_it = std::move(tree_roots.back());
            tree_roots.pop_back();
        }
    }
}

#if 0













static unsigned int moveInside(const Polygons& polygons, Point& from, int distance, int64_t maxDist2)
{
    Point   ret                                    = from;
    int64_t bestDist2                              = std::numeric_limits<int64_t>::max();
    auto    bestPoly                               = static_cast<unsigned int>(-1);
    bool    is_already_on_correct_side_of_boundary = false;
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        const Polygon &poly = polygons[poly_idx];
        if (poly.size() < 2)
            continue;
        Point p0 = poly[poly.size() - 2];
        Point p1 = poly.back();


        bool projected_p_beyond_prev_segment = (p1 - p0).cast<int64_t>().dot((from - p0).cast<int64_t>()) >= (p1 - p0).cast<int64_t>().squaredNorm();
        for (const Point& p2 : poly)
        {



            const Point& a = p1;
            const Point& b = p2;
            const Point& p = from;
            Point ab = b - a;
            Point ap = p - a;
            int64_t ab_length2 = ab.cast<int64_t>().squaredNorm();
            if (ab_length2 <= 0)
            {
                p1 = p2;
                continue;
            }
            int64_t dot_prod = ab.cast<int64_t>().dot(ap.cast<int64_t>());
            if (dot_prod <= 0)
            {
                if (projected_p_beyond_prev_segment)
                {
                    projected_p_beyond_prev_segment = false;
                    Point& x = p1;

                    int64_t dist2 = (x - p).cast<int64_t>().squaredNorm();
                    if (dist2 < bestDist2)
                    {
                        bestDist2 = dist2;
                        bestPoly = poly_idx;
                        if (distance == 0) {
                            ret = x;
                        } else {

                            Point inward_dir = perp((ab.cast<double>().normalized() * scaled<double>(10.0) + (p1 - p0).cast<double>().normalized() * scaled<double>(10.0)).eval()).cast<coord_t>();

                            ret = x + (inward_dir.cast<double>().normalized() * distance).cast<coord_t>();
                            is_already_on_correct_side_of_boundary = inward_dir.cast<int64_t>().dot((p - x).cast<int64_t>()) * distance >= 0;
                        }
                    }
                }
                else
                {
                    projected_p_beyond_prev_segment = false;
                    p0 = p1;
                    p1 = p2;
                    continue;
                }
            }
            else if (dot_prod >= ab_length2)
            {
                projected_p_beyond_prev_segment = true;
                p0 = p1;
                p1 = p2;
                continue;
            }
            else
            {
                projected_p_beyond_prev_segment = false;
                Point x = (a.cast<int64_t>() + ab.cast<int64_t>() * dot_prod / ab_length2).cast<coord_t>();

                int64_t dist2 = (p - x).cast<int64_t>().squaredNorm();
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestPoly = poly_idx;
                    if (distance == 0) { ret = x; }
                    else
                    {

                        Vec2d inward_dir = perp((ab.cast<double>().normalized() * distance).eval());
                        ret = x + inward_dir.cast<coord_t>();
                        is_already_on_correct_side_of_boundary = inward_dir.dot((p - x).cast<double>()) >= 0;
                    }
                }
            }
            p0 = p1;
            p1 = p2;
        }
    }
    if (is_already_on_correct_side_of_boundary)
    {
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        else
        {

        }
        return bestPoly;
    }
    else if (bestDist2 < maxDist2)
    {
        from = ret;
        return bestPoly;
    }
    return static_cast<unsigned int>(-1);
}
#endif

Polylines Layer::convertToLines(const Polygons& limit_to_outline, const coord_t line_overlap) const
{
    if (tree_roots.empty())
        return {};

    Polylines result_lines;
    for (const auto &tree : tree_roots)
        tree->convertToPolylines(result_lines, line_overlap);

    return intersection_pl(result_lines, limit_to_outline);
}

}
