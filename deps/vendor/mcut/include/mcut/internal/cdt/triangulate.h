/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef CDT_vW1vZ0lO8rS4gY4uI4fB
#define CDT_vW1vZ0lO8rS4gY4uI4fB

#include "mcut/internal/cdt/kdtree.h"
#include "mcut/internal/cdt/utils.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <stack>
#include <stdexcept>
#include <utility>
#include <vector>


namespace cdt {










struct vertex_insertion_order_t {




    enum Enum {
        RANDOM,
        AS_GIVEN,
    };
};


struct super_geometry_type_t {




    enum Enum {
        SUPER_TRIANGLE,
        CUSTOM,
    };
};




struct action_on_intersecting_constraint_edges_t {




    enum Enum {
        IGNORE,
        RESOLVE,
    };
};






typedef unsigned short layer_depth_t;
typedef layer_depth_t boundary_overlap_count_t;

namespace detail {


    template <typename T>
    std::array<T, 3> arr3(const T& v0, const T& v1, const T& v2)
    {
        const std::array<T, 3> out = { v0, v1, v2 };
        return out;
    }

    namespace defaults {

        const std::size_t nTargetVerts = 0;
        const super_geometry_type_t::Enum superGeomType = super_geometry_type_t::SUPER_TRIANGLE;
        const vertex_insertion_order_t::Enum vertexInsertionOrder = vertex_insertion_order_t::RANDOM;
        const action_on_intersecting_constraint_edges_t::Enum intersectingEdgesStrategy = action_on_intersecting_constraint_edges_t::IGNORE;
        const float minDistToConstraintEdge(0);

    }


    template <typename T, typename Allocator1>
    void insert_unique(std::vector<T, Allocator1>& to, const T& elem)
    {
        if (std::find(to.begin(), to.end(), elem) == to.end()) {
            to.push_back(elem);
        }
    }


    template <typename T, typename Allocator1, typename Allocator2>
    void insert_unique(
        std::vector<T, Allocator1>& to,
        const std::vector<T, Allocator2>& from)
    {
        typedef typename std::vector<T, Allocator2>::const_iterator Cit;
        to.reserve(to.size() + from.size());
        for (Cit cit = from.begin(); cit != from.end(); ++cit) {
            insert_unique(to, *cit);
        }
    }

}















template <typename T, typename TNearPointLocator = locator_kdtree_t<T>>
class triangulator_t {
public:

    std::vector<vec2_<T>> vertices;
    std::vector<triangle_t> triangles;
    std::unordered_set<edge_t> fixedEdges;






    std::vector<std::vector<std::uint32_t>> vertTris;








    std::unordered_map<edge_t, boundary_overlap_count_t> overlapCount;






    std::unordered_map<edge_t, std::vector<edge_t>> pieceToOriginals;



    triangulator_t();




    triangulator_t(vertex_insertion_order_t::Enum vertexInsertionOrder);









    triangulator_t(
        vertex_insertion_order_t::Enum vertexInsertionOrder,
        action_on_intersecting_constraint_edges_t::Enum intersectingEdgesStrategy,
        T minDistToConstraintEdge);











    triangulator_t(
        vertex_insertion_order_t::Enum vertexInsertionOrder,
        const TNearPointLocator& nearPtLocator,
        action_on_intersecting_constraint_edges_t::Enum intersectingEdgesStrategy,
        T minDistToConstraintEdge);












    template <
        typename TVertexIter,
        typename TGetVertexCoordX,
        typename TGetVertexCoordY>
    void insert_vertices(
        TVertexIter first,
        TVertexIter last,
        TGetVertexCoordX get_x_coord,
        TGetVertexCoordY get_y_coord);




    void insert_vertices(const std::vector<vec2_<T>>& vertices);




















    template <
        typename TEdgeIter,
        typename TGetEdgeVertexStart,
        typename TGetEdgeVertexEnd>
    void insert_edges(
        TEdgeIter first,
        TEdgeIter last,
        TGetEdgeVertexStart getStart,
        TGetEdgeVertexEnd getEnd);











    void insert_edges(const std::vector<edge_t>& edges);











    std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>
    get_intersected_triangle(
        const std::uint32_t iA,
        const std::vector<std::uint32_t>& candidates,
        const vec2_<T>& a,
        const vec2_<T>& b,
        const T orientationTolerance) const
    {
        typedef std::vector<std::uint32_t>::const_iterator TriIndCit;
        for (TriIndCit it = candidates.begin(); it != candidates.end(); ++it) {
            const std::uint32_t iT = *it;
            const triangle_t t = triangles[iT];
            const std::uint32_t i = get_vertex_index(t, iA);
            const std::uint32_t iP2 = t.vertices[ccw(i)];
            const T orientP2 = orient2d(vertices[iP2], a, b);
            const point_to_line_location_t::Enum locP2 = classify_orientation(orientP2);

            if (locP2 == point_to_line_location_t::RIGHT_SIDE) {
                const std::uint32_t iP1 = t.vertices[cw(i)];
                const T orientP1 = orient2d(vertices[iP1], a, b);
                const point_to_line_location_t::Enum locP1 = classify_orientation(orientP1);
                if (locP1 == point_to_line_location_t::COLLINEAR) {
                    return std::make_tuple(null_neighbour, iP1, iP1);
                }
                if (locP1 == point_to_line_location_t::LEFT_SIDE) {
                    if (orientationTolerance) {
                        T closestOrient;
                        std::uint32_t iClosestP;
                        if (std::abs(orientP1) <= std::abs(orientP2)) {
                            closestOrient = orientP1;
                            iClosestP = iP1;
                        } else {
                            closestOrient = orientP2;
                            iClosestP = iP2;
                        }
                        if (classify_orientation(
                                closestOrient, orientationTolerance)
                            == point_to_line_location_t::COLLINEAR) {
                            return std::make_tuple(null_neighbour, iClosestP, iClosestP);
                        }
                    }
                    return std::make_tuple(iT, iP1, iP2);
                }
            }
        }
        throw std::runtime_error("Could not find vertex triangle intersected by "
                                 "edge. Note: can be caused by duplicate points.");
    }















    std::stack<std::uint32_t> insert_point_on_edge(
        const std::uint32_t v,
        const std::uint32_t iT1,
        const std::uint32_t iT2)
    {
        const std::uint32_t iTnew1 = add_triangle();
        const std::uint32_t iTnew2 = add_triangle();

        triangle_t& t1 = triangles[iT1];
        triangle_t& t2 = triangles[iT2];
        std::uint32_t i = get_opposite_vertex_index(t1, iT2);
        const std::uint32_t v1 = t1.vertices[i];
        const std::uint32_t v2 = t1.vertices[ccw(i)];
        const std::uint32_t n1 = t1.neighbors[i];
        const std::uint32_t n4 = t1.neighbors[cw(i)];
        i = get_opposite_vertex_index(t2, iT1);
        const std::uint32_t v3 = t2.vertices[i];
        const std::uint32_t v4 = t2.vertices[ccw(i)];
        const std::uint32_t n3 = t2.neighbors[i];
        const std::uint32_t n2 = t2.neighbors[cw(i)];

        using detail::arr3;
        t1 = triangle_t::make(arr3(v1, v2, v), arr3(n1, iTnew2, iTnew1));
        t2 = triangle_t::make(arr3(v3, v4, v), arr3(n3, iTnew1, iTnew2));
        triangles[iTnew1] = triangle_t::make(arr3(v1, v, v4), arr3(iT1, iT2, n4));
        triangles[iTnew2] = triangle_t::make(arr3(v3, v, v2), arr3(iT2, iT1, n2));

        add_adjacent_triangles(v, iT1, iTnew2, iT2, iTnew1);

        change_neighbour(n4, iT1, iTnew1);
        change_neighbour(n2, iT2, iTnew2);
        add_adjacent_triangle(v1, iTnew1);
        add_adjacent_triangle(v3, iTnew2);
        remove_adjacent_triangle(v2, iT2);
        add_adjacent_triangle(v2, iTnew2);
        remove_adjacent_triangle(v4, iT1);
        add_adjacent_triangle(v4, iTnew1);

        std::stack<std::uint32_t> newTriangles;
        newTriangles.push(iT1);
        newTriangles.push(iTnew2);
        newTriangles.push(iT2);
        newTriangles.push(iTnew1);
        return newTriangles;
    }

    std::array<std::uint32_t, 2> trianglesAt(const vec2_<T>& pos) const
    {
        std::array<std::uint32_t, 2> out = { null_neighbour, null_neighbour };
        for (std::uint32_t i = std::uint32_t(0); i < std::uint32_t(triangles.size()); ++i) {
            const triangle_t& t = triangles[i];
            const vec2_<T>& v1 = vertices[t.vertices[0]];
            const vec2_<T>& v2 = vertices[t.vertices[1]];
            const vec2_<T>& v3 = vertices[t.vertices[2]];
            const point_to_triangle_location_t::Enum loc = locate_point_wrt_triangle(pos, v1, v2, v3);
            if (loc == point_to_triangle_location_t::OUTSIDE)
                continue;
            out[0] = i;
            if (check_on_edge(loc))
                out[1] = t.neighbors[edge_neighbour(loc)];
            return out;
        }
        throw std::runtime_error("No triangle was found at position");
    }





















    template <
        typename TEdgeIter,
        typename TGetEdgeVertexStart,
        typename TGetEdgeVertexEnd>
    void conform_to_edges(
        TEdgeIter first,
        TEdgeIter last,
        TGetEdgeVertexStart getStart,
        TGetEdgeVertexEnd getEnd);











    std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>
    TintersectedTriangle(
        const std::uint32_t iA,
        const std::vector<std::uint32_t>& candidates,
        const vec2_<T>& a,
        const vec2_<T>& b,
        const T orientationTolerance = T(0)) const
    {
        typedef std::vector<std::uint32_t>::const_iterator TriIndCit;
        for (TriIndCit it = candidates.begin(); it != candidates.end(); ++it) {
            const std::uint32_t iT = *it;
            const triangle_t t = triangles[iT];
            const std::uint32_t i = get_vertex_index(t, iA);
            const std::uint32_t iP2 = t.vertices[ccw(i)];
            const T orientP2 = orient2D(vertices[iP2], a, b);
            const point_to_line_location_t::Enum locP2 = classify_orientation(orientP2);
            if (locP2 == point_to_line_location_t::RIGHT_SIDE) {
                const std::uint32_t iP1 = t.vertices[cw(i)];
                const T orientP1 = orient2D(vertices[iP1], a, b);
                const point_to_line_location_t::Enum locP1 = classify_orientation(orientP1);
                if (locP1 == point_to_line_location_t::COLLINEAR) {
                    return std::make_tuple(null_neighbour, iP1, iP1);
                }
                if (locP1 == point_to_line_location_t::LEFT_SIDE) {
                    if (orientationTolerance) {
                        T closestOrient;
                        std::uint32_t iClosestP;
                        if (std::abs(orientP1) <= std::abs(orientP2)) {
                            closestOrient = orientP1;
                            iClosestP = iP1;
                        } else {
                            closestOrient = orientP2;
                            iClosestP = iP2;
                        }
                        if (classify_orientation(
                                closestOrient, orientationTolerance)
                            == point_to_line_location_t::COLLINEAR) {
                            return std::make_tuple(null_neighbour, iClosestP, iClosestP);
                        }
                    }
                    return std::make_tuple(iT, iP1, iP2);
                }
            }
        }
        throw std::runtime_error("Could not find vertex triangle intersected by "
                                 "edge. Note: can be caused by duplicate points.");
    }


















    std::stack<std::uint32_t> insert_point_in_triangle(
        const std::uint32_t v,
        const std::uint32_t iT)
    {
        const std::uint32_t iNewT1 = add_triangle();
        const std::uint32_t iNewT2 = add_triangle();

        triangle_t& t = triangles[iT];
        const std::array<std::uint32_t, 3> vv = t.vertices;
        const std::array<std::uint32_t, 3> nn = t.neighbors;
        const std::uint32_t v1 = vv[0], v2 = vv[1], v3 = vv[2];
        const std::uint32_t n1 = nn[0], n2 = nn[1], n3 = nn[2];


        using detail::arr3;
        triangles[iNewT1] = triangle_t::make(arr3(v2, v3, v), arr3(n2, iNewT2, iT));
        triangles[iNewT2] = triangle_t::make(arr3(v3, v1, v), arr3(n3, iT, iNewT1));
        t = triangle_t::make(arr3(v1, v2, v), arr3(n1, iNewT1, iNewT2));

        add_adjacent_triangles(v, iT, iNewT1, iNewT2);

        add_adjacent_triangle(v1, iNewT2);
        add_adjacent_triangle(v2, iNewT1);
        remove_adjacent_triangle(v3, iT);
        add_adjacent_triangle(v3, iNewT1);
        add_adjacent_triangle(v3, iNewT2);

        change_neighbour(n2, iT, iNewT1);
        change_neighbour(n3, iT, iNewT2);

        std::stack<std::uint32_t> newTriangles;
        newTriangles.push(iT);
        newTriangles.push(iNewT1);
        newTriangles.push(iNewT2);
        return newTriangles;
    }

    std::array<std::uint32_t, 2> walking_search_triangle_at(
        const vec2_<T>& pos) const
    {
        std::array<std::uint32_t, 2> out = { null_neighbour, null_neighbour };

        const std::uint32_t startVertex = m_nearPtLocator.nearPoint(pos, vertices);
        const std::uint32_t iT = walk_triangles(startVertex, pos);

        const triangle_t& t = triangles[iT];
        const vec2_<T>& v1 = vertices[t.vertices[0]];
        const vec2_<T>& v2 = vertices[t.vertices[1]];
        const vec2_<T>& v3 = vertices[t.vertices[2]];
        const point_to_triangle_location_t::Enum loc = locate_point_wrt_triangle(pos, v1, v2, v3);
        if (loc == point_to_triangle_location_t::OUTSIDE)
            throw std::runtime_error("No triangle was found at position");
        out[0] = iT;
        if (check_on_edge(loc))
            out[1] = t.neighbors[edge_neighbour(loc)];
        return out;
    }











    void conform_to_edges(const std::vector<edge_t>& edges);





    void eraseSuperTriangle();

    void erase_outer_triangles();






    void erase_outer_triangles_and_holes();




    void initialise_with_custom_supergeometry();






    bool is_finalized() const;















    std::vector<layer_depth_t>
    calculate_triangle_depths() const
    {
        std::vector<layer_depth_t> triDepths(
            triangles.size(), std::numeric_limits<layer_depth_t>::max());
        std::stack<std::uint32_t> seeds(std::deque<std::uint32_t>(1, vertTris[0].front()));
        layer_depth_t layerDepth = 0;
        layer_depth_t deepestSeedDepth = 0;

        std::unordered_map<layer_depth_t, std::unordered_set<std::uint32_t>> seedsByDepth;

        do {
            const std::unordered_map<std::uint32_t, layer_depth_t>& newSeeds = peel_layer(seeds, layerDepth, triDepths);

            seedsByDepth.erase(layerDepth);
            typedef std::unordered_map<std::uint32_t, layer_depth_t>::const_iterator Iter;
            for (Iter it = newSeeds.begin(); it != newSeeds.end(); ++it) {
                deepestSeedDepth = std::max(deepestSeedDepth, it->second);
                seedsByDepth[it->second].insert(it->first);
            }
            const std::unordered_set<std::uint32_t>& nextLayerSeeds = seedsByDepth[layerDepth + 1];
            seeds = std::stack<std::uint32_t>(
                std::deque<std::uint32_t>(nextLayerSeeds.begin(), nextLayerSeeds.end()));
            ++layerDepth;
        } while (!seeds.empty() || deepestSeedDepth > layerDepth);

        return triDepths;
    }
















    void do_edgeflip(std::uint32_t iT, std::uint32_t iTopo);






    void remove_triangles(const std::unordered_set<std::uint32_t>& removedTriangles);


private:

    void add_super_triangle(const box2d_t<T>& box);
    void create_vertex(const vec2_<T>& pos, const std::vector<std::uint32_t>& tris);
    void insert_vertex(std::uint32_t iVert);
    void enforce_delaunay_property_using_edge_flips(
        const vec2_<T>& v,
        std::uint32_t iVert,
        std::stack<std::uint32_t>& triStack);

    std::vector<edge_t> insert_vertex_and_flip_fixed_edges(std::uint32_t iVert);





    void insert_edge(edge_t edge, edge_t originalEdge);










    void conform_to_edge(
        edge_t edge,
        std::vector<edge_t> originalEdges,
        boundary_overlap_count_t overlaps);

    std::uint32_t walk_triangles(std::uint32_t startVertex, const vec2_<T>& pos) const;
    bool check_is_edgeflip_needed(
        const vec2_<T>& v,
        std::uint32_t iV,
        std::uint32_t iV1,
        std::uint32_t iV2,
        std::uint32_t iV3) const;
    bool
    check_is_edgeflip_needed(const vec2_<T>& v, std::uint32_t iT, std::uint32_t iTopo, std::uint32_t iVert) const;
    void change_neighbour(std::uint32_t iT, std::uint32_t oldNeighbor, std::uint32_t newNeighbor);
    void change_neighbour(
        std::uint32_t iT,
        std::uint32_t iVedge1,
        std::uint32_t iVedge2,
        std::uint32_t newNeighbor);
    void add_adjacent_triangle(std::uint32_t iVertex, std::uint32_t iTriangle);
    void
    add_adjacent_triangles(std::uint32_t iVertex, std::uint32_t iT1, std::uint32_t iT2, std::uint32_t iT3);
    void add_adjacent_triangles(
        std::uint32_t iVertex,
        std::uint32_t iT1,
        std::uint32_t iT2,
        std::uint32_t iT3,
        std::uint32_t iT4);
    void remove_adjacent_triangle(std::uint32_t iVertex, std::uint32_t iTriangle);
    std::uint32_t triangulate_pseudo_polygon(
        std::uint32_t ia,
        std::uint32_t ib,
        std::vector<std::uint32_t>::const_iterator pointsFirst,
        std::vector<std::uint32_t>::const_iterator pointsLast);
    std::uint32_t find_delaunay_point(
        std::uint32_t ia,
        std::uint32_t ib,
        std::vector<std::uint32_t>::const_iterator pointsFirst,
        std::vector<std::uint32_t>::const_iterator pointsLast) const;
    std::uint32_t pseudo_polygon_outer_triangle(std::uint32_t ia, std::uint32_t ib) const;
    std::uint32_t add_triangle(const triangle_t& t);
    std::uint32_t add_triangle();





    void finalise_triangulation(const std::unordered_set<std::uint32_t>& removedTriangles);
    std::unordered_set<std::uint32_t> grow_to_boundary(std::stack<std::uint32_t> seeds) const;

    void fixEdge(
        const edge_t& edge,
        const boundary_overlap_count_t overlaps)
    {
        fixedEdges.insert(edge);
        overlapCount[edge] = overlaps;
    }

    void fixEdge(const edge_t& edge)
    {
        if (!fixedEdges.insert(edge).second) {
            ++overlapCount[edge];
        }
    }

    void fixEdge(
        const edge_t& edge,
        const edge_t& originalEdge)
    {
        fixEdge(edge);
        if (edge != originalEdge)
            detail::insert_unique(pieceToOriginals[edge], originalEdge);
    }






    void make_dummies(const std::uint32_t iT)
    {
        const triangle_t& t = triangles[iT];

        typedef std::array<std::uint32_t, 3>::const_iterator VCit;
        for (VCit iV = t.vertices.begin(); iV != t.vertices.end(); ++iV)
            remove_adjacent_triangle(*iV, iT);

        typedef std::array<std::uint32_t, 3>::const_iterator NCit;
        for (NCit iTn = t.neighbors.begin(); iTn != t.neighbors.end(); ++iTn)
            change_neighbour(*iTn, iT, null_neighbour);

        m_dummyTris.push_back(iT);
    }





    void erase_dummies()
    {
        if (m_dummyTris.empty())
            return;
        const std::unordered_set<std::uint32_t> dummySet(m_dummyTris.begin(), m_dummyTris.end());
        std::unordered_map<std::uint32_t, std::uint32_t> triIndMap;
        triIndMap[null_neighbour] = null_neighbour;
        for (std::uint32_t iT(0), iTnew(0); iT < std::uint32_t(triangles.size()); ++iT) {
            if (dummySet.count(iT))
                continue;
            triIndMap[iT] = iTnew;
            triangles[iTnew] = triangles[iT];
            iTnew++;
        }
        triangles.erase(triangles.end() - dummySet.size(), triangles.end());


        typedef typename std::vector<std::vector<std::uint32_t>>::iterator VertTrisIt;
        for (VertTrisIt vTris = vertTris.begin(); vTris != vertTris.end(); ++vTris) {
            for (std::vector<std::uint32_t>::iterator iT = vTris->begin(); iT != vTris->end(); ++iT)
                *iT = triIndMap[*iT];
        }

        for (std::vector<triangle_t>::iterator t = triangles.begin(); t != triangles.end(); ++t) {
            std::array<std::uint32_t, 3>& nn = t->neighbors;
            for (std::array<std::uint32_t, 3>::iterator iN = nn.begin(); iN != nn.end(); ++iN)
                *iN = triIndMap[*iN];
        }

        m_dummyTris = std::vector<std::uint32_t>();
    }
















    std::unordered_map<std::uint32_t, layer_depth_t>
    peel_layer(
        std::stack<std::uint32_t> seeds,
        const layer_depth_t layerDepth,
        std::vector<layer_depth_t>& triDepths) const
    {
        std::unordered_map<std::uint32_t, layer_depth_t> behindBoundary;
        while (!seeds.empty()) {
            const std::uint32_t iT = seeds.top();
            seeds.pop();
            triDepths[iT] = layerDepth;
            behindBoundary.erase(iT);
            const triangle_t& t = triangles[iT];
            for (std::uint32_t i(0); i < std::uint32_t(3); ++i) {
                const edge_t opEdge(t.vertices[ccw(i)], t.vertices[cw(i)]);
                const std::uint32_t iN = t.neighbors[get_opposite_neighbour_from_vertex(i)];
                if (iN == null_neighbour || triDepths[iN] <= layerDepth)
                    continue;
                if (fixedEdges.count(opEdge)) {
                    const std::unordered_map<edge_t, layer_depth_t>::const_iterator cit = overlapCount.find(opEdge);
                    const layer_depth_t triDepth = cit == overlapCount.end()
                        ? layerDepth + 1
                        : layerDepth + cit->second + 1;
                    behindBoundary[iN] = triDepth;
                    continue;
                }
                seeds.push(iN);
            }
        }
        return behindBoundary;
    }

    std::vector<std::uint32_t> m_dummyTris;
    TNearPointLocator m_nearPtLocator;
    std::size_t m_nTargetVerts;
    super_geometry_type_t::Enum m_superGeomType;
    vertex_insertion_order_t::Enum m_vertexInsertionOrder;
    action_on_intersecting_constraint_edges_t::Enum m_intersectingEdgesStrategy;
    T m_minDistToConstraintEdge;
};




namespace detail {

    static std::mt19937 randGenerator(9001);

    template <class RandomIt>
    void random_shuffle(RandomIt first, RandomIt last)
    {
        typename std::iterator_traits<RandomIt>::difference_type i, n;
        n = last - first;
        for (i = n - 1; i > 0; --i) {
            std::swap(first[i], first[randGenerator() % (i + 1)]);
        }
    }

}




template <typename T, typename TNearPointLocator>
template <
    typename TVertexIter,
    typename TGetVertexCoordX,
    typename TGetVertexCoordY>
void triangulator_t<T, TNearPointLocator>::insert_vertices(
    const TVertexIter first,
    const TVertexIter last,
    TGetVertexCoordX get_x_coord,
    TGetVertexCoordY get_y_coord)
{
    if (is_finalized()) {
        throw std::runtime_error(
            "triangulator_t was finalized with 'erase...' method. Inserting new "
            "vertices is not possible");
    }

    detail::randGenerator.seed(9001);

    if (vertices.empty()) {
        add_super_triangle(expand_with_points<T>(first, last, get_x_coord, get_y_coord));
    }

    const std::size_t nExistingVerts = vertices.size();

    vertices.reserve(nExistingVerts + std::distance(first, last));

    for (TVertexIter it = first; it != last; ++it) {
        create_vertex(vec2_<T>::make(get_x_coord(*it), get_y_coord(*it)), std::vector<std::uint32_t>());
    }

    switch (m_vertexInsertionOrder) {

    case vertex_insertion_order_t::AS_GIVEN: {

        for (TVertexIter it = first; it != last; ++it) {
            insert_vertex(std::uint32_t(nExistingVerts + std::distance(first, it)));
        }

        break;
    }
    case vertex_insertion_order_t::RANDOM: {
        std::vector<std::uint32_t> ii(std::distance(first, last));
        typedef std::vector<std::uint32_t>::iterator Iter;
        std::uint32_t value = static_cast<std::uint32_t>(nExistingVerts);
        for (Iter it = ii.begin(); it != ii.end(); ++it, ++value)
            *it = value;
        detail::random_shuffle(ii.begin(), ii.end());
        for (Iter it = ii.begin(); it != ii.end(); ++it)
            insert_vertex(*it);
        break;
    }
    }
}

template <typename T, typename TNearPointLocator>
template <
    typename TEdgeIter,
    typename TGetEdgeVertexStart,
    typename TGetEdgeVertexEnd>
void triangulator_t<T, TNearPointLocator>::insert_edges(
    TEdgeIter first,
    const TEdgeIter last,
    TGetEdgeVertexStart getStart,
    TGetEdgeVertexEnd getEnd)
{
    if (is_finalized()) {
        throw std::runtime_error(
            "triangulator_t was finalized with 'erase...' method. Inserting new "
            "edges is not possible");
    }
    for (; first != last; ++first) {

        const edge_t edge(
            std::uint32_t(getStart(*first) + m_nTargetVerts),
            std::uint32_t(getEnd(*first) + m_nTargetVerts));
        insert_edge(edge, edge);
    }
    erase_dummies();
}

template <typename T, typename TNearPointLocator>
template <
    typename TEdgeIter,
    typename TGetEdgeVertexStart,
    typename TGetEdgeVertexEnd>
void triangulator_t<T, TNearPointLocator>::conform_to_edges(
    TEdgeIter first,
    const TEdgeIter last,
    TGetEdgeVertexStart getStart,
    TGetEdgeVertexEnd getEnd)
{
    if (is_finalized()) {
        throw std::runtime_error(
            "triangulator_t was finalized with 'erase...' method. Conforming to "
            "new edges is not possible");
    }
    for (; first != last; ++first) {

        const edge_t e(
            std::uint32_t(getStart(*first) + m_nTargetVerts),
            std::uint32_t(getEnd(*first) + m_nTargetVerts));
        conform_to_edge(e, std::vector<edge_t>(1, e), 0);
    }
    erase_dummies();
}

}

#ifndef CDT_USE_AS_COMPILED_LIBRARY


#include <algorithm>
#include <cassert>
#include <deque>
#include <stdexcept>

namespace cdt {

template <typename T, typename TNearPointLocator>
triangulator_t<T, TNearPointLocator>::triangulator_t()
    : m_nTargetVerts(detail::defaults::nTargetVerts)
    , m_superGeomType(detail::defaults::superGeomType)
    , m_vertexInsertionOrder(detail::defaults::vertexInsertionOrder)
    , m_intersectingEdgesStrategy(detail::defaults::intersectingEdgesStrategy)
    , m_minDistToConstraintEdge(detail::defaults::minDistToConstraintEdge)
{
}

template <typename T, typename TNearPointLocator>
triangulator_t<T, TNearPointLocator>::triangulator_t(
    const vertex_insertion_order_t::Enum vertexInsertionOrder)
    : m_nTargetVerts(detail::defaults::nTargetVerts)
    , m_superGeomType(detail::defaults::superGeomType)
    , m_vertexInsertionOrder(vertexInsertionOrder)
    , m_intersectingEdgesStrategy(detail::defaults::intersectingEdgesStrategy)
    , m_minDistToConstraintEdge(detail::defaults::minDistToConstraintEdge)
{
}

template <typename T, typename TNearPointLocator>
triangulator_t<T, TNearPointLocator>::triangulator_t(
    const vertex_insertion_order_t::Enum vertexInsertionOrder,
    const action_on_intersecting_constraint_edges_t::Enum intersectingEdgesStrategy,
    const T minDistToConstraintEdge)
    : m_nTargetVerts(detail::defaults::nTargetVerts)
    , m_superGeomType(detail::defaults::superGeomType)
    , m_vertexInsertionOrder(vertexInsertionOrder)
    , m_intersectingEdgesStrategy(intersectingEdgesStrategy)
    , m_minDistToConstraintEdge(minDistToConstraintEdge)
{
}

template <typename T, typename TNearPointLocator>
triangulator_t<T, TNearPointLocator>::triangulator_t(
    const vertex_insertion_order_t::Enum vertexInsertionOrder,
    const TNearPointLocator& nearPtLocator,
    const action_on_intersecting_constraint_edges_t::Enum intersectingEdgesStrategy,
    const T minDistToConstraintEdge)
    : m_nTargetVerts(detail::defaults::nTargetVerts)
    , m_nearPtLocator(nearPtLocator)
    , m_superGeomType(detail::defaults::superGeomType)
    , m_vertexInsertionOrder(vertexInsertionOrder)
    , m_intersectingEdgesStrategy(intersectingEdgesStrategy)
    , m_minDistToConstraintEdge(minDistToConstraintEdge)
{
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::change_neighbour(
    const std::uint32_t iT,
    const std::uint32_t iVedge1,
    const std::uint32_t iVedge2,
    const std::uint32_t newNeighbor)
{
    triangle_t& t = triangles[iT];
    t.neighbors[opposite_triangle_index(t, iVedge1, iVedge2)] = newNeighbor;
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::eraseSuperTriangle()
{
    if (m_superGeomType != super_geometry_type_t::SUPER_TRIANGLE)
        return;

    std::unordered_set<std::uint32_t> toErase;
    toErase.reserve(
        vertTris[0].size() + vertTris[1].size() + vertTris[2].size());
    for (std::uint32_t iT(0); iT < std::uint32_t(triangles.size()); ++iT) {
        triangle_t& t = triangles[iT];
        if (t.vertices[0] < 3 || t.vertices[1] < 3 || t.vertices[2] < 3)
            toErase.insert(iT);
    }
    finalise_triangulation(toErase);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::erase_outer_triangles()
{

    const std::stack<std::uint32_t> seed(std::deque<std::uint32_t>(1, vertTris[0].front()));
    const std::unordered_set<std::uint32_t> toErase = grow_to_boundary(seed);
    finalise_triangulation(toErase);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::erase_outer_triangles_and_holes()
{
    const std::vector<layer_depth_t> triDepths = calculate_triangle_depths();
    std::unordered_set<std::uint32_t> toErase;
    toErase.reserve(triangles.size());

    for (std::size_t iT = 0; iT != triangles.size(); ++iT) {
        if (triDepths[iT] % 2 == 0){
            toErase.insert(static_cast<std::uint32_t>(iT));
        }
    }

    finalise_triangulation(toErase);
}


inline edge_t remap_no_supertriangle(const edge_t& e)
{
    return edge_t(e.v1() - 3, e.v2() - 3);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::remove_triangles(
    const std::unordered_set<std::uint32_t>& removedTriangles)
{
    if (removedTriangles.empty())
        return;

    std::unordered_map<std::uint32_t, std::uint32_t> triIndMap;
    for (std::uint32_t iT(0), iTnew(0); iT < std::uint32_t(triangles.size()); ++iT) {
        if (removedTriangles.count(iT))
            continue;
        triIndMap[iT] = iTnew;
        triangles[iTnew] = triangles[iT];
        iTnew++;
    }
    triangles.erase(triangles.end() - removedTriangles.size(), triangles.end());

    vertTris = std::vector<std::vector<std::uint32_t>>();
    for (std::uint32_t iT = 0; iT < triangles.size(); ++iT) {
        triangle_t& t = triangles[iT];

        std::array<std::uint32_t, 3>& nn = t.neighbors;
        for (std::array<std::uint32_t, 3>::iterator n = nn.begin(); n != nn.end(); ++n) {
            if (removedTriangles.count(*n)) {
                *n = null_neighbour;
            } else if (*n != null_neighbour) {
                *n = triIndMap[*n];
            }
        }
    }
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::finalise_triangulation(
    const std::unordered_set<std::uint32_t>& removedTriangles)
{
    erase_dummies();

    if (m_superGeomType == super_geometry_type_t::SUPER_TRIANGLE) {
        vertices.erase(vertices.begin(), vertices.begin() + 3);
        if (removedTriangles.empty())
            vertTris.erase(vertTris.begin(), vertTris.begin() + 3);

        {
            std::unordered_set<edge_t> updatedFixedEdges;
            typedef std::unordered_set<edge_t>::const_iterator It;
            for (It e = fixedEdges.begin(); e != fixedEdges.end(); ++e) {
                updatedFixedEdges.insert(remap_no_supertriangle(*e));
            }
            fixedEdges = updatedFixedEdges;
        }
        {
            std::unordered_map<edge_t, boundary_overlap_count_t> updatedOverlapCount;
            typedef std::unordered_map<edge_t, boundary_overlap_count_t>::const_iterator
                It;
            for (It it = overlapCount.begin(); it != overlapCount.end(); ++it) {
                updatedOverlapCount.insert(std::make_pair(
                    remap_no_supertriangle(it->first), it->second));
            }
            overlapCount = updatedOverlapCount;
        }
        {
            std::unordered_map<edge_t, std::vector<edge_t>> updatedPieceToOriginals;
            typedef std::unordered_map<edge_t, std::vector<edge_t>>::const_iterator It;
            for (It it = pieceToOriginals.begin(); it != pieceToOriginals.end();
                 ++it) {
                std::vector<edge_t> ee = it->second;
                for (std::vector<edge_t>::iterator eeIt = ee.begin(); eeIt != ee.end();
                     ++eeIt) {
                    *eeIt = remap_no_supertriangle(*eeIt);
                }
                updatedPieceToOriginals.insert(
                    std::make_pair(remap_no_supertriangle(it->first), ee));
            }
            pieceToOriginals = updatedPieceToOriginals;
        }
    }

    remove_triangles(removedTriangles);

    if (m_superGeomType == super_geometry_type_t::SUPER_TRIANGLE) {
        for (std::vector<triangle_t>::iterator t = triangles.begin(); t != triangles.end();
             ++t) {
            std::array<std::uint32_t, 3>& vv = t->vertices;
            for (std::array<std::uint32_t, 3>::iterator v = vv.begin(); v != vv.end(); ++v) {
                *v -= 3;
            }
        }
    }
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::initialise_with_custom_supergeometry()
{
    m_nearPtLocator.initialize(vertices);
    m_nTargetVerts = vertices.size();
    m_superGeomType = super_geometry_type_t::CUSTOM;
}

template <typename T, typename TNearPointLocator>
std::unordered_set<std::uint32_t> triangulator_t<T, TNearPointLocator>::grow_to_boundary(
    std::stack<std::uint32_t> seeds) const
{
    std::unordered_set<std::uint32_t> traversed;
    while (!seeds.empty()) {
        const std::uint32_t iT = seeds.top();
        seeds.pop();
        traversed.insert(iT);
        const triangle_t& t = triangles[iT];
        for (std::uint32_t i(0); i < std::uint32_t(3); ++i) {
            const edge_t opEdge(t.vertices[ccw(i)], t.vertices[cw(i)]);
            if (fixedEdges.count(opEdge))
                continue;
            const std::uint32_t iN = t.neighbors[get_opposite_neighbour_from_vertex(i)];
            if (iN != null_neighbour && traversed.count(iN) == 0)
                seeds.push(iN);
        }
    }
    return traversed;
}

template <typename T, typename TNearPointLocator>
std::uint32_t triangulator_t<T, TNearPointLocator>::add_triangle(const triangle_t& t)
{
    if (m_dummyTris.empty()) {
        triangles.push_back(t);
        return std::uint32_t(triangles.size() - 1);
    }
    const std::uint32_t nxtDummy = m_dummyTris.back();
    m_dummyTris.pop_back();
    triangles[nxtDummy] = t;
    return nxtDummy;
}

template <typename T, typename TNearPointLocator>
std::uint32_t triangulator_t<T, TNearPointLocator>::add_triangle()
{
    if (m_dummyTris.empty()) {
        const triangle_t dummy = {
            { null_vertex, null_vertex, null_vertex },
            { null_neighbour, null_neighbour, null_neighbour }
        };
        triangles.push_back(dummy);
        return std::uint32_t(triangles.size() - 1);
    }
    const std::uint32_t nxtDummy = m_dummyTris.back();
    m_dummyTris.pop_back();
    return nxtDummy;
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::insert_edges(
    const std::vector<edge_t>& edges)
{
    insert_edges(edges.begin(), edges.end(), edge_get_v1, edge_get_v2);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::conform_to_edges(
    const std::vector<edge_t>& edges)
{
    conform_to_edges(edges.begin(), edges.end(), edge_get_v1, edge_get_v2);
}

namespace detail {

    template <typename T>
    T lerp(const T& a, const T& b, const T t)
    {
        return (T(1) - t) * a + t * b;
    }


    template <typename T>
    vec2_<T> get_intersection_point_coords(
        const vec2_<T>& a,
        const vec2_<T>& b,
        const vec2_<T>& c,
        const vec2_<T>& d)
    {

        if (get_square_distance(a, b) < get_square_distance(c, d)) {


            const T a_cd = orient2d(c, d, a);
            const T b_cd = orient2d(c, d, b);
            const T t = a_cd / (a_cd - b_cd);
            return vec2_<T>::make(lerp(a.x(), b.x(), t), lerp(a.y(), b.y(), t));
        } else {


            const T c_ab = orient2d(a, b, c);
            const T d_ab = orient2d(a, b, d);
            const T t = c_ab / (c_ab - d_ab);
            return vec2_<T>::make(lerp(c.x(), d.x(), t), lerp(c.y(), d.y(), t));
        }
    }

}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::insert_edge(
    const edge_t edge,
    const edge_t originalEdge)
{
    const std::uint32_t iA = edge.v1();
    std::uint32_t iB = edge.v2();
    if (iA == iB)
        return;
    const std::vector<std::uint32_t>& aTris = vertTris[iA];
    const std::vector<std::uint32_t>& bTris = vertTris[iB];
    const vec2_<T>& a = vertices[iA];
    const vec2_<T>& b = vertices[iB];
    if (check_vertices_share_edge(aTris, bTris)) {
        fixEdge(edge, originalEdge);
        return;
    }

    const T distanceTolerance = m_minDistToConstraintEdge == T(0)
        ? T(0)
        : m_minDistToConstraintEdge * distance(a, b);

    std::uint32_t iT;
    std::uint32_t iVleft, iVright;
    std::tie(iT, iVleft, iVright) = get_intersected_triangle(iA, aTris, a, b, distanceTolerance);

    if (iT == null_neighbour) {
        const edge_t edgePart(iA, iVleft);
        fixEdge(edgePart, originalEdge);
        return insert_edge(edge_t(iVleft, iB), originalEdge);
    }
    std::vector<std::uint32_t> intersected(1, iT);
    std::vector<std::uint32_t> ptsLeft(1, iVleft);
    std::vector<std::uint32_t> ptsRight(1, iVright);
    std::uint32_t iV = iA;
    triangle_t t = triangles[iT];
    while (std::find(t.vertices.begin(), t.vertices.end(), iB) == t.vertices.end()) {
        const std::uint32_t iTopo = get_opposite_triangle_index(t, iV);
        const triangle_t& tOpo = triangles[iTopo];
        const std::uint32_t iVopo = get_opposed_vertex_index(tOpo, iT);
        const vec2_<T> vOpo = vertices[iVopo];


        if (m_intersectingEdgesStrategy == action_on_intersecting_constraint_edges_t::RESOLVE && fixedEdges.count(edge_t(iVleft, iVright))) {
            const std::uint32_t iNewVert = static_cast<std::uint32_t>(vertices.size());


            const edge_t splitEdge(iVleft, iVright);
            const edge_t half1(iVleft, iNewVert);
            const edge_t half2(iNewVert, iVright);
            const boundary_overlap_count_t overlaps = overlapCount[splitEdge];

            fixedEdges.erase(splitEdge);
            overlapCount.erase(splitEdge);

            fixEdge(half1, overlaps);
            fixEdge(half2, overlaps);

            std::vector<edge_t> newOriginals(1, splitEdge);
            const std::unordered_map<edge_t, std::vector<edge_t>>::const_iterator originalsIt = pieceToOriginals.find(splitEdge);
            if (originalsIt != pieceToOriginals.end()) {
                newOriginals = originalsIt->second;
                pieceToOriginals.erase(originalsIt);
            }
            detail::insert_unique(pieceToOriginals[half1], newOriginals);
            detail::insert_unique(pieceToOriginals[half2], newOriginals);


            const vec2_<T> newV = detail::get_intersection_point_coords(
                vertices[iA],
                vertices[iB],
                vertices[iVleft],
                vertices[iVright]);
            create_vertex(newV, std::vector<std::uint32_t>());
            std::stack<std::uint32_t> triStack = insert_point_on_edge(iNewVert, iT, iTopo);
            enforce_delaunay_property_using_edge_flips(newV, iNewVert, triStack);


            insert_edge(edge_t(iA, iNewVert), originalEdge);
            insert_edge(edge_t(iNewVert, iB), originalEdge);
            return;
        }

        intersected.push_back(iTopo);
        iT = iTopo;
        t = triangles[iT];

        const point_to_line_location_t::Enum loc = locate_point_wrt_line(vOpo, a, b, distanceTolerance);
        if (loc == point_to_line_location_t::LEFT_SIDE) {
            ptsLeft.push_back(iVopo);
            iV = iVleft;
            iVleft = iVopo;
        } else if (loc == point_to_line_location_t::RIGHT_SIDE) {
            ptsRight.push_back(iVopo);
            iV = iVright;
            iVright = iVopo;
        } else
            iB = iVopo;
    }

    typedef std::vector<std::uint32_t>::const_iterator TriIndCit;
    for (TriIndCit it = intersected.begin(); it != intersected.end(); ++it)
        make_dummies(*it);

    const std::uint32_t iTleft = triangulate_pseudo_polygon(iA, iB, ptsLeft.begin(), ptsLeft.end());
    std::reverse(ptsRight.begin(), ptsRight.end());
    const std::uint32_t iTright = triangulate_pseudo_polygon(iB, iA, ptsRight.begin(), ptsRight.end());
    change_neighbour(iTleft, null_neighbour, iTright);
    change_neighbour(iTright, null_neighbour, iTleft);

    if (iB != edge.v2())
    {

        const edge_t edgePart(iA, iB);
        fixEdge(edgePart, originalEdge);
        return insert_edge(edge_t(iB, edge.v2()), originalEdge);
    } else {
        fixEdge(edge, originalEdge);
    }
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::conform_to_edge(
    const edge_t edge,
    std::vector<edge_t> originalEdges,
    const boundary_overlap_count_t overlaps)
{
    const std::uint32_t iA = edge.v1();
    std::uint32_t iB = edge.v2();
    if (iA == iB)
        return;
    const std::vector<std::uint32_t>& aTris = vertTris[iA];
    const std::vector<std::uint32_t>& bTris = vertTris[iB];
    const vec2_<T>& a = vertices[iA];
    const vec2_<T>& b = vertices[iB];
    if (check_vertices_share_edge(aTris, bTris)) {
        overlaps > 0 ? fixEdge(edge, overlaps) : fixEdge(edge);

        if (!originalEdges.empty() && edge != originalEdges.front()) {
            detail::insert_unique(pieceToOriginals[edge], originalEdges);
        }
        return;
    }

    const T distanceTolerance = m_minDistToConstraintEdge == T(0)
        ? T(0)
        : m_minDistToConstraintEdge * distance(a, b);
    std::uint32_t iT;
    std::uint32_t iVleft, iVright;
    std::tie(iT, iVleft, iVright) = get_intersected_triangle(iA, aTris, a, b, distanceTolerance);

    if (iT == null_neighbour) {
        const edge_t edgePart(iA, iVleft);
        overlaps > 0 ? fixEdge(edgePart, overlaps) : fixEdge(edgePart);
        detail::insert_unique(pieceToOriginals[edgePart], originalEdges);
        return conform_to_edge(edge_t(iVleft, iB), originalEdges, overlaps);
    }

    std::uint32_t iV = iA;
    triangle_t t = triangles[iT];
    while (std::find(t.vertices.begin(), t.vertices.end(), iB) == t.vertices.end()) {
        const std::uint32_t iTopo = get_opposite_triangle_index(t, iV);
        const triangle_t& tOpo = triangles[iTopo];
        const std::uint32_t iVopo = get_opposed_vertex_index(tOpo, iT);
        const vec2_<T> vOpo = vertices[iVopo];


        if (m_intersectingEdgesStrategy == action_on_intersecting_constraint_edges_t::RESOLVE && fixedEdges.count(edge_t(iVleft, iVright))) {
            const std::uint32_t iNewVert = static_cast<std::uint32_t>(vertices.size());


            const edge_t splitEdge(iVleft, iVright);
            const edge_t half1(iVleft, iNewVert);
            const edge_t half2(iNewVert, iVright);
            const boundary_overlap_count_t overlaps = overlapCount[splitEdge];

            fixedEdges.erase(splitEdge);
            overlapCount.erase(splitEdge);

            fixEdge(half1, overlaps);
            fixEdge(half2, overlaps);

            std::vector<edge_t> newOriginals(1, splitEdge);
            const std::unordered_map<edge_t, std::vector<edge_t>>::const_iterator originalsIt = pieceToOriginals.find(splitEdge);
            if (originalsIt != pieceToOriginals.end()) {
                newOriginals = originalsIt->second;
                pieceToOriginals.erase(originalsIt);
            }
            detail::insert_unique(pieceToOriginals[half1], newOriginals);
            detail::insert_unique(pieceToOriginals[half2], newOriginals);


            const vec2_<T> newV = detail::get_intersection_point_coords(
                vertices[iA],
                vertices[iB],
                vertices[iVleft],
                vertices[iVright]);
            create_vertex(newV, std::vector<std::uint32_t>());
            std::stack<std::uint32_t> triStack = insert_point_on_edge(iNewVert, iT, iTopo);
            enforce_delaunay_property_using_edge_flips(newV, iNewVert, triStack);
            conform_to_edge(edge_t(iA, iNewVert), originalEdges, overlaps);
            conform_to_edge(edge_t(iNewVert, iB), originalEdges, overlaps);
            return;
        }

        iT = iTopo;
        t = triangles[iT];

        const point_to_line_location_t::Enum loc = locate_point_wrt_line(vOpo, a, b, distanceTolerance);
        if (loc == point_to_line_location_t::LEFT_SIDE) {
            iV = iVleft;
            iVleft = iVopo;
        } else if (loc == point_to_line_location_t::RIGHT_SIDE) {
            iV = iVright;
            iVright = iVopo;
        } else
            iB = iVopo;
    }



    const std::uint32_t iMid = static_cast<std::uint32_t>(vertices.size());
    const vec2_<T>& start = vertices[iA];
    const vec2_<T>& end = vertices[iB];
    create_vertex(
        vec2_<T>::make((start.x() + end.x()) / T(2), (start.y() + end.y()) / T(2)),
        std::vector<std::uint32_t>());
    const std::vector<edge_t> flippedFixedEdges = insert_vertex_and_flip_fixed_edges(iMid);

    conform_to_edge(edge_t(iA, iMid), originalEdges, overlaps);
    conform_to_edge(edge_t(iMid, iB), originalEdges, overlaps);


    for (std::vector<edge_t>::const_iterator it = flippedFixedEdges.begin();
         it != flippedFixedEdges.end();
         ++it) {
        fixedEdges.erase(*it);

        boundary_overlap_count_t prevOverlaps = 0;
        const std::unordered_map<edge_t, boundary_overlap_count_t>::const_iterator
            overlapsIt
            = overlapCount.find(*it);
        if (overlapsIt != overlapCount.end()) {
            prevOverlaps = overlapsIt->second;
            overlapCount.erase(overlapsIt);
        }

        std::vector<edge_t> prevOriginals(1, *it);
        const std::unordered_map<edge_t, std::vector<edge_t>>::const_iterator originalsIt = pieceToOriginals.find(*it);
        if (originalsIt != pieceToOriginals.end()) {
            prevOriginals = originalsIt->second;
        }
        conform_to_edge(*it, prevOriginals, prevOverlaps);
    }
    if (iB != edge.v2())
        conform_to_edge(edge_t(iB, edge.v2()), originalEdges, overlaps);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::add_super_triangle(const box2d_t<T>& box)
{
    m_nTargetVerts = 3;
    m_superGeomType = super_geometry_type_t::SUPER_TRIANGLE;

    const vec2_<T> center = {
        (box.min.x() + box.max.x()) / T(2), (box.min.y() + box.max.y()) / T(2)
    };
    const T w = box.max.x() - box.min.x();
    const T h = box.max.y() - box.min.y();
    T r = std::sqrt(w * w + h * h) / T(2);
    r *= T(1.1);
    const T R = T(2) * r;
    const T shiftX = R * std::sqrt(T(3)) / T(2);
    const vec2_<T> posV1 = { center.x() - shiftX, center.y() - r };
    const vec2_<T> posV2 = { center.x() + shiftX, center.y() - r };
    const vec2_<T> posV3 = { center.x(), center.y() + R };
    create_vertex(posV1, std::vector<std::uint32_t>(1, std::uint32_t(0)));
    create_vertex(posV2, std::vector<std::uint32_t>(1, std::uint32_t(0)));
    create_vertex(posV3, std::vector<std::uint32_t>(1, std::uint32_t(0)));
    const triangle_t superTri = {
        { std::uint32_t(0), std::uint32_t(1), std::uint32_t(2) },
        { null_neighbour, null_neighbour, null_neighbour }
    };
    add_triangle(superTri);
    m_nearPtLocator.initialize(vertices);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::create_vertex(
    const vec2_<T>& pos,
    const std::vector<std::uint32_t>& tris)
{
    vertices.push_back(pos);
    vertTris.push_back(tris);
}

template <typename T, typename TNearPointLocator>
std::vector<edge_t>
triangulator_t<T, TNearPointLocator>::insert_vertex_and_flip_fixed_edges(
    const std::uint32_t iVert)
{
    std::vector<edge_t> flippedFixedEdges;

    const vec2_<T>& v = vertices[iVert];
    std::array<std::uint32_t, 2> trisAt = walking_search_triangle_at(v);
    std::stack<std::uint32_t> triStack = trisAt[1] == null_neighbour
        ? insert_point_in_triangle(iVert, trisAt[0])
        : insert_point_on_edge(iVert, trisAt[0], trisAt[1]);
    while (!triStack.empty()) {
        const std::uint32_t iT = triStack.top();
        triStack.pop();

        const triangle_t& t = triangles[iT];
        const std::uint32_t iTopo = get_opposite_triangle_index(t, iVert);
        if (iTopo == null_neighbour)
            continue;














        const triangle_t& tOpo = triangles[iTopo];
        const std::uint32_t i = get_opposite_vertex_index(tOpo, iT);
        const std::uint32_t iV2 = tOpo.vertices[i];
        const std::uint32_t iV1 = tOpo.vertices[cw(i)];
        const std::uint32_t iV3 = tOpo.vertices[ccw(i)];

        if (check_is_edgeflip_needed(v, iVert, iV1, iV2, iV3)) {

            const edge_t flippedEdge(iV1, iV3);
            if (fixedEdges.count(flippedEdge))
                flippedFixedEdges.push_back(flippedEdge);

            do_edgeflip(iT, iTopo);
            triStack.push(iT);
            triStack.push(iTopo);
        }
    }

    m_nearPtLocator.add_point(iVert, vertices);
    return flippedFixedEdges;
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::insert_vertex(const std::uint32_t iVert)
{
    const vec2_<T>& v = vertices[iVert];

    std::array<std::uint32_t, 2> trisAt = walking_search_triangle_at(v);

    std::stack<std::uint32_t> triStack = (trisAt[1] == null_neighbour)
        ? insert_point_in_triangle(iVert, trisAt[0])
        : insert_point_on_edge(iVert, trisAt[0], trisAt[1]);

    enforce_delaunay_property_using_edge_flips(v, iVert, triStack);

    m_nearPtLocator.add_point(iVert, vertices);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::enforce_delaunay_property_using_edge_flips(
    const vec2_<T>& v,
    const std::uint32_t iVert,
    std::stack<std::uint32_t>& triStack)
{
    while (!triStack.empty()) {

        const std::uint32_t iT = triStack.top();
        triStack.pop();

        const triangle_t& t = triangles[iT];
        const std::uint32_t iTopo = get_opposite_triangle_index(t, iVert);

        if (iTopo == null_neighbour) {
            continue;
        }

        if (check_is_edgeflip_needed(v, iT, iTopo, iVert)) {

            do_edgeflip(iT, iTopo);

            triStack.push(iT);
            triStack.push(iTopo);
        }
    }
}
























template <typename T, typename TNearPointLocator>
bool triangulator_t<T, TNearPointLocator>::check_is_edgeflip_needed(
    const vec2_<T>& v,
    const std::uint32_t iV,
    const std::uint32_t iV1,
    const std::uint32_t iV2,
    const std::uint32_t iV3) const
{
    const vec2_<T>& v1 = vertices[iV1];
    const vec2_<T>& v2 = vertices[iV2];
    const vec2_<T>& v3 = vertices[iV3];
    if (m_superGeomType == super_geometry_type_t::SUPER_TRIANGLE) {




        if (iV < 3)
        {

            if (iV1 < 3)
                return locate_point_wrt_line(v1, v2, v3) == locate_point_wrt_line(v, v2, v3);
            if (iV3 < 3)
                return locate_point_wrt_line(v3, v1, v2) == locate_point_wrt_line(v, v1, v2);
            return false;
        }
        if (iV2 < 3)
        {

            if (iV1 < 3)
                return locate_point_wrt_line(v1, v, v3) == locate_point_wrt_line(v2, v, v3);
            if (iV3 < 3)
                return locate_point_wrt_line(v3, v1, v) == locate_point_wrt_line(v2, v1, v);
            return false;
        }

        if (iV1 < 3)
            return locate_point_wrt_line(v1, v2, v3) == locate_point_wrt_line(v, v2, v3);
        if (iV3 < 3)
            return locate_point_wrt_line(v3, v1, v2) == locate_point_wrt_line(v, v1, v2);
    }
    return check_is_in_circumcircle(v, v1, v2, v3);
}

template <typename T, typename TNearPointLocator>
bool triangulator_t<T, TNearPointLocator>::check_is_edgeflip_needed(
    const vec2_<T>& v,
    const std::uint32_t iT,
    const std::uint32_t iTopo,
    const std::uint32_t iV) const
{













    const triangle_t& tOpo = triangles[iTopo];
    const std::uint32_t i = get_opposite_vertex_index(tOpo, iT);
    const std::uint32_t iV2 = tOpo.vertices[i];
    const std::uint32_t iV1 = tOpo.vertices[cw(i)];
    const std::uint32_t iV3 = tOpo.vertices[ccw(i)];


    if (fixedEdges.count(edge_t(iV1, iV3)))
        return false;

    return check_is_edgeflip_needed(v, iV, iV1, iV2, iV3);
}

template <typename T, typename TNearPointLocator>
std::uint32_t triangulator_t<T, TNearPointLocator>::walk_triangles(
    const std::uint32_t startVertex,
    const vec2_<T>& pos) const
{

    std::uint32_t currTri = vertTris[startVertex][0];

    std::unordered_set<std::uint32_t> visited;

    bool found = false;
    while (!found) {

        const triangle_t& t = triangles[currTri];
        found = true;

        const std::uint32_t offset(detail::randGenerator() % 3);

        for (std::uint32_t i_(0); i_ < std::uint32_t(3); ++i_) {

            const std::uint32_t i((i_ + offset) % 3);
            const vec2_<T>& vStart = vertices[t.vertices[i]];
            const vec2_<T>& vEnd = vertices[t.vertices[ccw(i)]];
            const point_to_line_location_t::Enum edgeCheck = locate_point_wrt_line(pos, vStart, vEnd);

            if (edgeCheck == point_to_line_location_t::RIGHT_SIDE && t.neighbors[i] != null_neighbour && visited.insert(t.neighbors[i]).second) {
                found = false;
                currTri = t.neighbors[i];
                break;
            }
        }
    }
    return currTri;
}

















template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::do_edgeflip(
    const std::uint32_t iT,
    const std::uint32_t iTopo)
{
    triangle_t& t = triangles[iT];
    triangle_t& tOpo = triangles[iTopo];

    const std::array<std::uint32_t, 3>& triNs = t.neighbors;
    const std::array<std::uint32_t, 3>& triOpoNs = tOpo.neighbors;
    const std::array<std::uint32_t, 3>& triVs = t.vertices;
    const std::array<std::uint32_t, 3>& triOpoVs = tOpo.vertices;


    std::uint32_t i = get_opposite_vertex_index(t, iTopo);

    const std::uint32_t v1 = triVs[i];
    const std::uint32_t v2 = triVs[ccw(i)];
    const std::uint32_t n1 = triNs[i];
    const std::uint32_t n3 = triNs[cw(i)];

    i = get_opposite_vertex_index(tOpo, iT);

    const std::uint32_t v3 = triOpoVs[i];
    const std::uint32_t v4 = triOpoVs[ccw(i)];
    const std::uint32_t n4 = triOpoNs[i];
    const std::uint32_t n2 = triOpoNs[cw(i)];


    using detail::arr3;

    t = triangle_t::make(arr3(v4, v1, v3), arr3(n3, iTopo, n4));
    tOpo = triangle_t::make(arr3(v2, v3, v1), arr3(n2, iT, n1));


    change_neighbour(n1, iT, iTopo);
    change_neighbour(n4, iTopo, iT);



    if (!is_finalized()) {

        add_adjacent_triangle(v1, iTopo);
        add_adjacent_triangle(v3, iT);

        remove_adjacent_triangle(v2, iT);
        remove_adjacent_triangle(v4, iTopo);
    }
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::change_neighbour(
    const std::uint32_t iT,
    const std::uint32_t oldNeighbor,
    const std::uint32_t newNeighbor)
{
    if (iT == null_neighbour) {
        return;
    }

    triangle_t& t = triangles[iT];

    t.neighbors[get_neighbour_index(t, oldNeighbor)] = newNeighbor;
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::add_adjacent_triangle(
    const std::uint32_t iVertex,
    const std::uint32_t iTriangle)
{
    vertTris[iVertex].push_back(iTriangle);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::add_adjacent_triangles(
    const std::uint32_t iVertex,
    const std::uint32_t iT1,
    const std::uint32_t iT2,
    const std::uint32_t iT3)
{
    std::vector<std::uint32_t>& vTris = vertTris[iVertex];

    vTris.reserve(vTris.size() + 3);

    vTris.push_back(iT1);
    vTris.push_back(iT2);
    vTris.push_back(iT3);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::add_adjacent_triangles(
    const std::uint32_t iVertex,
    const std::uint32_t iT1,
    const std::uint32_t iT2,
    const std::uint32_t iT3,
    const std::uint32_t iT4)
{
    std::vector<std::uint32_t>& vTris = vertTris[iVertex];

    vTris.reserve(vTris.size() + 4);

    vTris.push_back(iT1);
    vTris.push_back(iT2);
    vTris.push_back(iT3);
    vTris.push_back(iT4);
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::remove_adjacent_triangle(
    const std::uint32_t iVertex,
    const std::uint32_t iTriangle)
{
    std::vector<std::uint32_t>& tris = vertTris[iVertex];
    tris.erase(std::find(tris.begin(), tris.end(), iTriangle));
}

template <typename T, typename TNearPointLocator>
std::uint32_t triangulator_t<T, TNearPointLocator>::triangulate_pseudo_polygon(
    const std::uint32_t ia,
    const std::uint32_t ib,
    const std::vector<std::uint32_t>::const_iterator pointsFirst,
    const std::vector<std::uint32_t>::const_iterator pointsLast)
{
    if (pointsFirst == pointsLast)
        return pseudo_polygon_outer_triangle(ia, ib);


    const std::uint32_t ic = find_delaunay_point(ia, ib, pointsFirst, pointsLast);

    std::vector<std::uint32_t>::const_iterator newLast = pointsFirst;

    while (*newLast != ic) {
        ++newLast;
    }

    const std::vector<std::uint32_t>::const_iterator newFirst = newLast + 1;

    const std::uint32_t iT2 = triangulate_pseudo_polygon(ic, ib, newFirst, pointsLast);
    const std::uint32_t iT1 = triangulate_pseudo_polygon(ia, ic, pointsFirst, newLast);

    const triangle_t t = { { ia, ib, ic }, { null_neighbour, iT2, iT1 } };
    const std::uint32_t iT = add_triangle(t);


    if (iT1 != null_neighbour) {
        if (pointsFirst == newLast) {
            change_neighbour(iT1, ia, ic, iT);
        } else {
            triangles[iT1].neighbors[0] = iT;
        }
    }
    if (iT2 != null_neighbour) {
        if (newFirst == pointsLast) {
            change_neighbour(iT2, ic, ib, iT);
        } else {
            triangles[iT2].neighbors[0] = iT;
        }
    }

    add_adjacent_triangle(ia, iT);
    add_adjacent_triangle(ib, iT);
    add_adjacent_triangle(ic, iT);

    return iT;
}

template <typename T, typename TNearPointLocator>
std::uint32_t triangulator_t<T, TNearPointLocator>::find_delaunay_point(
    const std::uint32_t ia,
    const std::uint32_t ib,
    const std::vector<std::uint32_t>::const_iterator pointsFirst,
    const std::vector<std::uint32_t>::const_iterator pointsLast) const
{
    MCUT_ASSERT(pointsFirst != pointsLast);

    const vec2_<T>& a = vertices[ia];
    const vec2_<T>& b = vertices[ib];

    std::uint32_t ic = *pointsFirst;
    vec2_<T> c = vertices[ic];

    for (std::vector<std::uint32_t>::const_iterator it = pointsFirst + 1; it != pointsLast; ++it) {

        const vec2_<T> v = vertices[*it];

        if (!check_is_in_circumcircle(v, a, b, c))
        {
            continue;
        }

        ic = *it;
        c = vertices[ic];
    }
    return ic;
}

template <typename T, typename TNearPointLocator>
std::uint32_t triangulator_t<T, TNearPointLocator>::pseudo_polygon_outer_triangle(
    const std::uint32_t ia,
    const std::uint32_t ib) const
{

    const std::vector<std::uint32_t>& aTris = vertTris[ia];
    const std::vector<std::uint32_t>& bTris = vertTris[ib];

    for (std::vector<std::uint32_t>::const_iterator it = aTris.begin(); it != aTris.end(); ++it)
    {
        if (std::find(bTris.begin(), bTris.end(), *it) != bTris.end())
        {
            return *it;
        }
    }

    return null_neighbour;
}

template <typename T, typename TNearPointLocator>
void triangulator_t<T, TNearPointLocator>::insert_vertices(
    const std::vector<vec2_<T>>& newVertices)
{
    return insert_vertices(
        newVertices.begin(), newVertices.end(), get_x_coord_vec2d<T>, get_y_coord_vec2d<T>);
}

template <typename T, typename TNearPointLocator>
bool triangulator_t<T, TNearPointLocator>::is_finalized() const
{
    return vertTris.empty() && !vertices.empty();
}

}
#endif

#endif
