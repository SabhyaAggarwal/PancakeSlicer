#ifndef MESHSPLITIMPL_HPP
#define MESHSPLITIMPL_HPP

#include <vector>
#include "Point.hpp"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cassert>
#include <utility>
#include <limits>
#include "libslic3r.h"
#include <iterator>
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4805 )
#endif

#include "TriangleMesh.hpp"
#include "libnest2d/tools/benchmark.h"
#include "Execution/ExecutionSeq.hpp"
#include "Execution/ExecutionTBB.hpp"

namespace Slic3r {

template<class ExPolicy>
std::vector<Vec3i32> create_face_neighbors_index(ExPolicy &&ex, const indexed_triangle_set &its);

namespace meshsplit_detail {

template<class Its, class Enable = void> struct ItsWithNeighborsIndex_ {
    using Index = typename Its::Index;
    static const indexed_triangle_set &get_its(const Its &m) { return m.get_its();}
    static const Index &get_index(const Its &m) { return m.get_index(); }
};

template<> struct ItsWithNeighborsIndex_<indexed_triangle_set> {
    using Index = std::vector<Vec3i32>;
    static const indexed_triangle_set &get_its(const indexed_triangle_set &its) noexcept { return its; }
    static Index get_index(const indexed_triangle_set &its) noexcept
    {
        return create_face_neighbors_index(ex_tbb, its);
    }
};

template<class NeighborIndex>
struct NeighborVisitor {
    NeighborVisitor(const indexed_triangle_set &its, const NeighborIndex &neighbor_index) :
        its(its), neighbor_index(neighbor_index) {
        m_visited.assign(its.indices.size(), false);
        m_facestack.reserve(its.indices.size());
    }
    NeighborVisitor(const indexed_triangle_set &its, NeighborIndex &&aneighbor_index) :
        its(its), neighbor_index(m_neighbor_index_data), m_neighbor_index_data(std::move(aneighbor_index)) {
        m_visited.assign(its.indices.size(), false);
        m_facestack.reserve(its.indices.size());
    }

    template<typename Visitor>
    void visit(Visitor visitor)
    {
        auto facet = std::find(m_visited.begin() + m_seed, m_visited.end(), false);
        m_seed = facet - m_visited.begin();

        if (facet != m_visited.end()) {
            auto idx = m_seed ++;
            if (! visitor(idx))
                return;
            this->push(idx);
            m_visited[idx] = true;
            while (! m_facestack.empty()) {
                size_t facet_idx = this->pop();
                for (auto neighbor_idx : neighbor_index[facet_idx]) {
                    assert(neighbor_idx < int(m_visited.size()));
                    if (neighbor_idx >= 0 && !m_visited[neighbor_idx]) {
                        if (! visitor(size_t(neighbor_idx)))
                            return;
                        m_visited[neighbor_idx] = true;
                        this->push(stack_el(neighbor_idx));
                    }
                }
            }
        }
    }

    const indexed_triangle_set  &its;
    const NeighborIndex         &neighbor_index;

private:
    const NeighborIndex          m_neighbor_index_data;

    std::vector<char>            m_visited;

    using                        stack_el = size_t;
    std::vector<stack_el>        m_facestack;
    void                         push(const stack_el &s) { m_facestack.emplace_back(s); }
    stack_el                     pop() { stack_el ret = m_facestack.back(); m_facestack.pop_back(); return ret; }

    size_t                       m_seed { 0 };
};

}
template<class IndexT> struct ItsNeighborsWrapper
{
    using Index = IndexT;
    const indexed_triangle_set &its;
    const IndexT               &index_ref;
    const IndexT                index;

    ItsNeighborsWrapper(const indexed_triangle_set &its, const IndexT &index) : its{its}, index_ref{index} {}
    ItsNeighborsWrapper(const indexed_triangle_set &its, IndexT &&aindex) : its{its}, index_ref{index}, index(std::move(aindex)) {}

    const auto& get_its() const noexcept { return its; }
    const auto& get_index() const noexcept { return index_ref; }
};

template<class Fn>
struct SplitOutputFn {

    Fn fn;

    SplitOutputFn(Fn f): fn{std::move(f)} {}

    SplitOutputFn &operator *() { return *this; }
    void           operator=(indexed_triangle_set &&its) { fn(std::move(its)); }
    void           operator=(indexed_triangle_set &its) { fn(its); }
    SplitOutputFn& operator++() { return *this; };
};

template<class Its, class OutputIt>
void its_split(const Its &m, OutputIt out_it)
{
    using namespace meshsplit_detail;

    const indexed_triangle_set &its = ItsWithNeighborsIndex_<Its>::get_its(m);

    struct VertexConv {
        size_t part_id      = std::numeric_limits<size_t>::max();
        size_t vertex_image;
    };
    std::vector<VertexConv> vidx_conv(its.vertices.size());

    meshsplit_detail::NeighborVisitor visitor(its, meshsplit_detail::ItsWithNeighborsIndex_<Its>::get_index(m));

    std::vector<size_t> facets;
    for (size_t part_id = 0;; ++part_id) {
        facets.clear();
        visitor.visit([&facets](size_t idx) { facets.emplace_back(idx); return true; });
        if (facets.empty())
            break;

        indexed_triangle_set mesh;
        mesh.indices.reserve(facets.size());
        mesh.vertices.reserve(std::min(facets.size() * 3, its.vertices.size()));

        for (size_t face_id : facets) {
            const auto &face = its.indices[face_id];
            Vec3i32       new_face;
            for (size_t v = 0; v < 3; ++v) {
                auto vi = face(v);

                if (vidx_conv[vi].part_id != part_id) {
                    vidx_conv[vi] = {part_id, mesh.vertices.size()};
                    mesh.vertices.emplace_back(its.vertices[size_t(vi)]);
                }

                new_face(v) = vidx_conv[vi].vertex_image;
            }

            mesh.indices.emplace_back(new_face);
        }

        *out_it = std::move(mesh);
        ++out_it;
    }
}

template<class Its>
std::vector<indexed_triangle_set> its_split(const Its &its)
{
    auto ret = reserve_vector<indexed_triangle_set>(3);
    its_split(its, std::back_inserter(ret));

    return ret;
}

template<class Its>
bool its_is_splittable(const Its &m)
{
    meshsplit_detail::NeighborVisitor visitor(meshsplit_detail::ItsWithNeighborsIndex_<Its>::get_its(m), meshsplit_detail::ItsWithNeighborsIndex_<Its>::get_index(m));
    bool has_some = false;
    bool has_some2 = false;
    visitor.visit([&has_some](size_t idx) { has_some = true; return true; });
    if (has_some)
        visitor.visit([&has_some2](size_t idx) { has_some2 = true; return false; });
    return has_some && has_some2;
}

template<class Its>
size_t its_number_of_patches(const Its &m)
{
    meshsplit_detail::NeighborVisitor visitor(meshsplit_detail::ItsWithNeighborsIndex_<Its>::get_its(m), meshsplit_detail::ItsWithNeighborsIndex_<Its>::get_index(m));
    size_t num_patches = 0;
    for (;;) {
        bool has_some = false;
        visitor.visit([&has_some](size_t idx) { has_some = true; return true; });
        if (! has_some)
            break;
        ++ num_patches;
    }
    return num_patches;
}

template<class ExPolicy>
std::vector<Vec3i32> create_face_neighbors_index(ExPolicy &&ex, const indexed_triangle_set &its)
{
    const std::vector<stl_triangle_vertex_indices> &indices = its.indices;

    if (indices.empty()) return {};

    assert(! its.vertices.empty());

    auto               vertex_triangles = VertexFaceIndex{its};
    static constexpr int no_value         = -1;
    std::vector<Vec3i32> neighbors(indices.size(),
                                 Vec3i32(no_value, no_value, no_value));

    std::atomic<bool> ambiguous{false};
    if (! IsSequentialEP<ExPolicy>)
        execution::for_each(ex, size_t(0), indices.size(),
        [&ambiguous, &indices, &vertex_triangles] (size_t face_idx)
        {
            for (int edge_index = 0; edge_index < 3 && ! ambiguous.load(std::memory_order_relaxed); ++edge_index) {
                const Vec2i32 edge = its_triangle_edge(indices[face_idx], edge_index);
                int opposite = 0, same = 0;
                for (const size_t other_face : vertex_triangles[edge[0]])
                    if (other_face != face_idx)
                        if (const int vertex_index = its_triangle_vertex_index(indices[other_face], edge[1]); vertex_index >= 0) {
                            if (indices[other_face][(vertex_index + 1) % 3] == edge[0])
                                ++ opposite;
                            else if (indices[other_face][(vertex_index + 2) % 3] == edge[0])
                                ++ same;
                        }
                if (opposite > 1 || (opposite > 0 && same > 0))
                    ambiguous.store(true, std::memory_order_relaxed);
            }
        }, execution::max_concurrency(ex));

    auto pair_face = [&neighbors, &indices, &vertex_triangles] (size_t face_idx)
        {
            Vec3i32& neighbor = neighbors[face_idx];
            const stl_triangle_vertex_indices & triangle_indices = indices[face_idx];
            for (int edge_index = 0; edge_index < 3; ++edge_index) {
                int& neighbor_edge = neighbor[edge_index];
                if (neighbor_edge != no_value)
                    continue;
                Vec2i32 edge_indices = its_triangle_edge(triangle_indices, edge_index);
                for (const size_t other_face : vertex_triangles[edge_indices[0]]) {
                    if (other_face <= face_idx) continue;
                    const stl_triangle_vertex_indices &face_indices = indices[other_face];
                    int vertex_index = its_triangle_vertex_index(face_indices, edge_indices[1]);
                    if (vertex_index < 0) continue;
                    if (edge_indices[0] != face_indices[(vertex_index + 1) % 3]) continue;
                    if (neighbors[other_face][vertex_index] != no_value)
                        continue;
                    neighbor_edge = other_face;
                    neighbors[other_face][vertex_index] = face_idx;
                    break;
                }
            }
        };
    if (ambiguous)
        execution::for_each(ex_seq, size_t(0), indices.size(), pair_face);
    else
        execution::for_each(ex, size_t(0), indices.size(), pair_face, execution::max_concurrency(ex));

    return neighbors;
}

}

#ifdef _MSC_VER
#pragma warning( pop )
#endif

#endif
