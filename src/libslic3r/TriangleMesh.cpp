#include "BoundingBox.hpp"
#include "TriangleMesh.hpp"
#include "Polygon.hpp"
#include "TriangleMeshSlicer.hpp"
#include "MeshSplitImpl.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "Geometry/ConvexHull.hpp"
#include "Point.hpp"
#include "Execution/ExecutionTBB.hpp"
#include "Execution/ExecutionSeq.hpp"
#include "Utils.hpp"
#include "libslic3r.h"
#include "AABBTreeIndirect.hpp"
#include "I18N.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <Eigen/Geometry>
#include <libqhull_r/libqhull_r.h>
#include <cstdio>
#include <cstring>
#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertex.h>
#include <libqhullcpp/QhullPoint.h>
#include <libqhullcpp/QhullVertexSet.h>

#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <queue>
#include <map>
#include <math.h>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>
#include <type_traits>

#include <boost/format.hpp>
#include <tbb/parallel_sort.h>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/predef/other/endian.h>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <igl/Hit.h>

#include <assert.h>
#include "ExtrusionEntity.hpp"

namespace Slic3r {

static void update_bounding_box(const indexed_triangle_set &its, TriangleMeshStats &out)
{
    BoundingBoxf3 bbox      = Slic3r::bounding_box(its);
    out.min                 = bbox.min.cast<float>();
    out.max                 = bbox.max.cast<float>();
    out.size                = out.max - out.min;    
}

static void fill_initial_stats(const indexed_triangle_set &its, TriangleMeshStats &out)
{
    out.number_of_facets    = its.indices.size();
    out.volume              = its_volume(its);
    update_bounding_box(its, out);

    const std::vector<Vec3i32> face_neighbors = its_face_neighbors(its);
    out.number_of_parts = its_number_of_patches(its, face_neighbors);
    out.open_edges      = its_num_open_edges(face_neighbors);
}

TriangleMesh::TriangleMesh(const std::vector<Vec3f> &vertices, const std::vector<Vec3i32> &faces) : its { faces, vertices }
{
    fill_initial_stats(this->its, m_stats);
}

TriangleMesh::TriangleMesh(std::vector<Vec3f> &&vertices, const std::vector<Vec3i32> &&faces) : its { std::move(faces), std::move(vertices) }
{
    fill_initial_stats(this->its, m_stats);
}

TriangleMesh::TriangleMesh(const indexed_triangle_set &its) : its(its)
{
    fill_initial_stats(this->its, m_stats);
}

TriangleMesh::TriangleMesh(indexed_triangle_set &&its, const RepairedMeshErrors& errors ) : its(std::move(its))
{
    m_stats.repaired_errors = errors;
    fill_initial_stats(this->its, m_stats);
}

static bool trianglemesh_repair_on_import(stl_file &stl)
{
    if (stl.stats.number_of_facets == 0)
        return true;

    BOOST_LOG_TRIVIAL(debug) << "TriangleMesh::repair() started";
    assert(stl_validate(&stl));
    stl_check_facets_exact(&stl);
    assert(stl_validate(&stl));
    stl.stats.facets_w_1_bad_edge = (stl.stats.connected_facets_2_edge - stl.stats.connected_facets_3_edge);
    stl.stats.facets_w_2_bad_edge = (stl.stats.connected_facets_1_edge - stl.stats.connected_facets_2_edge);
    stl.stats.facets_w_3_bad_edge = (stl.stats.number_of_facets - stl.stats.connected_facets_1_edge);
    if (stl.stats.connected_facets_3_edge < int(stl.stats.number_of_facets))
        return false;

    stl_fix_normal_directions(&stl);
    assert(stl_validate(&stl));
    stl_fix_normal_values(&stl);
    assert(stl_validate(&stl));
    stl_calculate_volume(&stl);
    assert(stl_validate(&stl));
    stl_verify_neighbors(&stl);
    assert(stl_validate(&stl));

    if (auto nr_degenerated = stl.stats.degenerate_facets; stl.stats.number_of_facets > 0 && nr_degenerated > 0)
        stl_check_facets_exact(&stl);

    BOOST_LOG_TRIVIAL(debug) << "TriangleMesh::repair() finished";
    return true;
}

bool TriangleMesh::from_stl(stl_file& stl, bool repair)
{
    if (repair && ! trianglemesh_repair_on_import(stl)) {
        const uint32_t unconnected = stl.stats.number_of_facets - stl.stats.connected_facets_3_edge;
        this->its.clear();
        this->its.vertices.reserve(size_t(stl.stats.number_of_facets) * 3);
        this->its.indices.reserve(stl.stats.number_of_facets);
        for (uint32_t i = 0; i < stl.stats.number_of_facets; ++ i) {
            const int first = int(this->its.vertices.size());
            for (const stl_vertex &v : stl.facet_start[i].vertex)
                this->its.vertices.emplace_back(v);
            this->its.indices.emplace_back(first, first + 1, first + 2);
        }
        its_merge_vertices(this->its, false);
        RepairedMeshErrors errors = its_repair_on_load(this->its);
        errors.degenerate_facets += stl.stats.degenerate_facets;
        errors.facets_removed += stl.stats.facets_removed;
        BOOST_LOG_TRIVIAL(warning) << "Mesh repaired on load: " << unconnected << " of " << stl.stats.number_of_facets
                                   << " STL facets not meeting edge to edge: " << errors.summary();
        fill_initial_stats(this->its, this->m_stats);
        m_stats.repaired_errors = errors;
        return true;
    }

    stl_generate_shared_vertices(&stl, this->its);
    fill_initial_stats(this->its, this->m_stats);
    if (repair) {
        m_stats.repaired_errors.edges_fixed       = stl.stats.edges_fixed;
        m_stats.repaired_errors.degenerate_facets = stl.stats.degenerate_facets;
        m_stats.repaired_errors.facets_removed    = stl.stats.facets_removed;
        m_stats.repaired_errors.facets_reversed   = stl.stats.facets_reversed;
        m_stats.repaired_errors.backwards_edges   = stl.stats.backwards_edges;
    }
    return true;
}

bool TriangleMesh::ReadSTLFile(const char *input_file, bool repair, ImportstlProgressFn stlFn, int custom_header_length)
{
    stl_file stl;
    if (!stl_open(&stl, input_file, stlFn, custom_header_length))
        return false;
    return from_stl(stl, repair);
}

bool TriangleMesh::write_ascii(const char* output_file) const
{
    return its_write_stl_ascii(output_file, "", this->its);
}

bool TriangleMesh::write_binary(const char* output_file) const
{
    return its_write_stl_binary(output_file, "", this->its);
}

float TriangleMesh::volume()
{
    if (m_stats.volume == -1)
        m_stats.volume = its_volume(this->its);
    return m_stats.volume;
}

void TriangleMesh::WriteOBJFile(const char* output_file) const
{
    its_write_obj(this->its, output_file);
}

void TriangleMesh::scale(float factor)
{
    this->scale(Vec3f(factor, factor, factor));
}

void TriangleMesh::scale(const Vec3f &versor)
{
    auto s = versor.array();
    m_stats.min.array() *= s;
    m_stats.max.array() *= s;
    m_stats.size.array() *= s;
    if (m_stats.volume > 0.0)
        m_stats.volume *= s(0) * s(1) * s(2);
    if (versor.x() == versor.y() && versor.x() == versor.z()) {
        float s = versor.x();
        for (stl_vertex &v : this->its.vertices)
            v *= s;
    } else {
        for (stl_vertex &v : this->its.vertices) {
            v.x() *= versor.x();
            v.y() *= versor.y();
            v.z() *= versor.z();
        }
    }
}

void TriangleMesh::translate(const Vec3f &displacement)
{
    if (displacement.x() != 0.f || displacement.y() != 0.f || displacement.z() != 0.f) {
        for (stl_vertex& v : this->its.vertices)
            v += displacement;
        m_stats.min += displacement;
        m_stats.max += displacement;
    }
}

void TriangleMesh::translate(float x, float y, float z)
{
    this->translate(Vec3f(x, y, z));
}

void TriangleMesh::rotate(float angle, const Axis &axis)
{
    if (angle != 0.f) {
        angle = Slic3r::Geometry::rad2deg(angle);
        switch (axis) {
        case X:  its_rotate_x(this->its, angle); break;
        case Y:  its_rotate_y(this->its, angle); break;
        case Z:  its_rotate_z(this->its, angle); break;
        default: assert(false);                  return;
        }
        update_bounding_box(this->its, this->m_stats);
    }
}

void TriangleMesh::rotate(float angle, const Vec3d& axis)
{
    if (angle != 0.f) {
        Vec3d axis_norm = axis.normalized();
        Transform3d m = Transform3d::Identity();
        m.rotate(Eigen::AngleAxisd(angle, axis_norm));
        its_transform(its, m);
        update_bounding_box(this->its, this->m_stats);
    }
}

void TriangleMesh::mirror(const Axis axis)
{
    switch (axis) {
    case X:
        for (stl_vertex &v : its.vertices)
            v.x() *= -1.f;
        break;
    case Y:
        for (stl_vertex& v : this->its.vertices)
            v.y() *= -1.0;
        break;
    case Z:
        for (stl_vertex &v : this->its.vertices)
            v.z() *= -1.0;
        break;
    default:
        assert(false);
        return;
    };
    its_flip_triangles(this->its);
    int iaxis = int(axis);
    std::swap(m_stats.min[iaxis], m_stats.max[iaxis]);
    m_stats.min[iaxis] *= -1.0;
    m_stats.max[iaxis] *= -1.0;
}

void TriangleMesh::transform(const Transform3d& t, bool fix_left_handed)
{
    its_transform(its, t);
    double det = t.matrix().block(0, 0, 3, 3).determinant();
    if (fix_left_handed && det < 0.) {
        its_flip_triangles(its);
        det = -det;
    }
    m_stats.volume *= det;
    update_bounding_box(this->its, this->m_stats);
}

void TriangleMesh::transform(const Matrix3d& m, bool fix_left_handed)
{
    its_transform(its, m);
    double det = m.block(0, 0, 3, 3).determinant();
    if (fix_left_handed && det < 0.) {
        its_flip_triangles(its);
        det = -det;
    }
    m_stats.volume *= det;
    update_bounding_box(this->its, this->m_stats);
}

void TriangleMesh::flip_triangles()
{
    its_flip_triangles(its);
    m_stats.volume = - m_stats.volume;
}

void TriangleMesh::align_to_origin()
{
    this->translate(- m_stats.min(0), - m_stats.min(1), - m_stats.min(2));
}

void TriangleMesh::rotate(double angle, Point* center)
{
    if (angle != 0.) {
        Vec2f c = center->cast<float>();
        this->translate(-c(0), -c(1), 0);
        its_rotate_z(this->its, (float)angle);
        this->translate(c(0), c(1), 0);
    }
}

bool TriangleMesh::is_splittable() const
{
    return its_is_splittable(this->its);
}

std::vector<TriangleMesh> TriangleMesh::split() const
{
    std::vector<indexed_triangle_set> itss = its_split(this->its);
    std::vector<TriangleMesh> out;
    out.reserve(itss.size());
    for (indexed_triangle_set &m : itss) {
        out.emplace_back(std::move(m));
        if (TriangleMesh &triangle_mesh = out.back(); triangle_mesh.volume() < 0)
            triangle_mesh.flip_triangles();

    }
    return out;
}

void TriangleMesh::merge(const TriangleMesh &mesh)
{
    its_merge(this->its, mesh.its);
    m_stats = m_stats.merge(mesh.m_stats);
}

ExPolygons TriangleMesh::horizontal_projection() const
{
    return union_ex(project_mesh(this->its, Transform3d::Identity(), []() {}));
}

Polygon TriangleMesh::convex_hull() const
{
    Points pp;
    pp.reserve(this->its.vertices.size());
    for (size_t i = 0; i < this->its.vertices.size(); ++ i) {
        const stl_vertex &v = this->its.vertices[i];
        pp.emplace_back(Point::new_scale(v(0), v(1)));
    }
    return Slic3r::Geometry::convex_hull(pp);
}

BoundingBoxf3 TriangleMesh::bounding_box() const
{
    BoundingBoxf3 bb;
    bb.defined = true;
    bb.min = m_stats.min.cast<double>();
    bb.max = m_stats.max.cast<double>();
    return bb;
}

BoundingBoxf3 TriangleMesh::transformed_bounding_box(const Transform3d &trafo) const
{
    BoundingBoxf3 bbox;
    for (const stl_vertex &v : this->its.vertices)
        bbox.merge(trafo * v.cast<double>());
    return bbox;
}

BoundingBoxf3 TriangleMesh::transformed_bounding_box(const Transform3d& trafod, double world_min_z) const
{
    std::vector<char>           sides;
    size_t                      num_above = 0;
    Eigen::AlignedBox<float, 3> bbox;
    Transform3f                 trafo = trafod.cast<float>();
    sides.reserve(its.vertices.size());
    for (const stl_vertex &v : this->its.vertices) {
        const stl_vertex pt   = trafo * v;
        const int        sign = pt.z() > world_min_z ? 1 : pt.z() < world_min_z ? -1 : 0;
        sides.emplace_back(sign);
        if (sign >= 0) {
            ++ num_above;
            bbox.extend(pt);
        }
    }

    if (num_above < its.vertices.size()) {
        for (const stl_triangle_vertex_indices &tri : its.indices) {
            const int s[3] = { sides[tri(0)], sides[tri(1)], sides[tri(2)] };
            if (std::min(s[0], std::min(s[1], s[2])) < 0 && std::max(s[0], std::max(s[1], s[2])) > 0) {
                int iprev = 2;
                for (int iedge = 0; iedge < 3; ++ iedge) {
                    if (s[iprev] * s[iedge] == -1) {
                        const stl_vertex p1 = trafo * its.vertices[tri(iprev)];
                        const stl_vertex p2 = trafo * its.vertices[tri(iedge)];
                        const float t = (world_min_z - p1.z()) / (p2.z() - p1.z());
                        bbox.extend(Vec3f(p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_min_z));
                    }
                    iprev = iedge;
                }
            }
        }
    }

    BoundingBoxf3 out;
    if (! bbox.isEmpty()) {
        out.min = bbox.min().cast<double>();
        out.max = bbox.max().cast<double>();
        out.defined = true;
    };
    return out;
}

TriangleMesh TriangleMesh::convex_hull_3d() const
{
    TriangleMesh mesh(its_convex_hull(this->its));
    return mesh;
}

std::vector<ExPolygons> TriangleMesh::slice(const std::vector<double> &z) const
{
    std::vector<float> z_f(z.begin(), z.end());
    return slice_mesh_ex(this->its, z_f, 0.0004f);
}

size_t TriangleMesh::memsize() const
{
    size_t memsize = 8 + this->its.memsize() + sizeof(this->m_stats);
    return memsize;
}

struct EdgeToFace {
    int  vertex_low;
    int  vertex_high;
    int  face;
    int  face_edge;
    bool operator==(const EdgeToFace &other) const { return vertex_low == other.vertex_low && vertex_high == other.vertex_high; }
    bool operator<(const EdgeToFace &other) const { return vertex_low < other.vertex_low || (vertex_low == other.vertex_low && vertex_high < other.vertex_high); }
};

template<typename FaceFilter, typename ThrowOnCancelCallback>
static std::vector<EdgeToFace> create_edge_map(
    const indexed_triangle_set &its, FaceFilter face_filter, ThrowOnCancelCallback throw_on_cancel)
{
    std::vector<EdgeToFace> edges_map;
    edges_map.reserve(its.indices.size() * 3);
    for (uint32_t facet_idx = 0; facet_idx < its.indices.size(); ++ facet_idx)
        if (face_filter(facet_idx))
            for (int i = 0; i < 3; ++ i) {
                edges_map.push_back({});
                EdgeToFace &e2f = edges_map.back();
                e2f.vertex_low  = its.indices[facet_idx][i];
                e2f.vertex_high = its.indices[facet_idx][(i + 1) % 3];
                e2f.face        = facet_idx;
                e2f.face_edge   = i + 1;
                if (e2f.vertex_low > e2f.vertex_high) {
                    std::swap(e2f.vertex_low, e2f.vertex_high);
                    e2f.face_edge = - e2f.face_edge;
                }
            }
    throw_on_cancel();
    std::sort(edges_map.begin(), edges_map.end());

    return edges_map;
}

template<typename FaceFilter, typename ThrowOnCancelCallback>
static inline std::vector<Vec3i32> its_face_edge_ids_impl(const indexed_triangle_set &its, FaceFilter face_filter, ThrowOnCancelCallback throw_on_cancel)
{
    std::vector<Vec3i32> out(its.indices.size(), Vec3i32(-1, -1, -1));

    std::vector<EdgeToFace> edges_map = create_edge_map(its, face_filter, throw_on_cancel);

    int num_edges = 0;
    for (size_t i = 0; i < edges_map.size(); ++ i) {
        EdgeToFace &edge_i = edges_map[i];
        if (edge_i.face == -1)
            continue;
        size_t j;
        bool found = false;
        for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++ j)
            if (edge_i.face_edge * edges_map[j].face_edge < 0 && edges_map[j].face != -1) {
                found = true;
                break;
            }
        if (! found) {
            for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++ j)
                if (edges_map[j].face != -1) {
                    found = true;
                    break;
                }
        }
        out[edge_i.face](std::abs(edge_i.face_edge) - 1) = num_edges;
        if (found) {
            EdgeToFace &edge_j = edges_map[j];
            out[edge_j.face](std::abs(edge_j.face_edge) - 1) = num_edges;
            edge_j.face = -1;
        }
        ++ num_edges;
        if ((i & 0x0ffff) == 0)
            throw_on_cancel();
    }

    return out;
}

std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its)
{
    return its_face_edge_ids_impl(its, [](const uint32_t){ return true; }, [](){});
}

std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its, std::function<void()> throw_on_cancel_callback)
{
    return its_face_edge_ids_impl(its, [](const uint32_t){ return true; }, throw_on_cancel_callback);
}

std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its, const std::vector<bool> &face_mask)
{
    return its_face_edge_ids_impl(its, [&face_mask](const uint32_t idx){ return face_mask[idx]; }, [](){});
}

std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its, std::vector<Vec3i32> &face_neighbors, bool assign_unbound_edges, int *num_edges)
{
    std::vector<Vec3i32> out(face_neighbors.size());
    int last_edge_id = 0;
    for (int i = 0; i < int(face_neighbors.size()); ++ i) {
        const stl_triangle_vertex_indices   &triangle  = its.indices[i];
        const Vec3i32                         &neighbors = face_neighbors[i];
        for (int j = 0; j < 3; ++ j) {
            int n = neighbors[j];
            if (n > i) {
                const stl_triangle_vertex_indices &triangle2 = its.indices[n];
                int   edge_id = last_edge_id ++;
                Vec2i32 edge    = its_triangle_edge(triangle, j);
                std::swap(edge(0), edge(1));
                int   k       = its_triangle_edge_index(triangle2, edge);
                if (k == -1) {
                    std::swap(edge(0), edge(1));
                    k = its_triangle_edge_index(triangle2, edge);
                }
                assert(k >= 0);
                out[i](j) = edge_id;
                out[n](k) = edge_id;
            } else if (n == -1) {
                out[i](j) = assign_unbound_edges ? last_edge_id ++ : -1;
            } else {
                assert(n < i);
            }
        }
    }
    if (num_edges)
        *num_edges = last_edge_id;
    return out;
}

int its_merge_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    auto sorted = reserve_vector<int>(its.vertices.size());
    for (int i = 0; i < int(its.vertices.size()); ++ i)
        sorted.emplace_back(i);
    std::sort(sorted.begin(), sorted.end(), [&its](int il, int ir) {
        const Vec3f &l = its.vertices[il];
        const Vec3f &r = its.vertices[ir];
        return l.x() < r.x() || (l.x() == r.x() && (l.y() < r.y() || (l.y() == r.y() && (l.z() < r.z() || (l.z() == r.z() && il < ir)))));
    });

    std::vector<int> map_vertices(its.vertices.size(), -1);
    for (int i = 0; i < int(sorted.size());) {
        const int    u = sorted[i];
        const Vec3f &p = its.vertices[u];
        int j = i;
        for (++ j; j < int(sorted.size()); ++ j) {
            const int    v = sorted[j];
            const Vec3f &q = its.vertices[v];
            if (p != q)
                break;
            assert(v > u);
            map_vertices[v] = u;
        }
        i = j;
    }

    int k = 0;
    for (int i = 0; i < int(its.vertices.size()); ++ i) {
        if (map_vertices[i] == -1) {
            map_vertices[i] = k;
            if (k < i)
                its.vertices[k] = its.vertices[i];
            ++ k;
        } else {
            assert(map_vertices[i] < i);
            map_vertices[i] = map_vertices[map_vertices[i]];
        }
    }

    int num_erased = int(its.vertices.size()) - k;

    if (num_erased) {
        its.vertices.erase(its.vertices.begin() + k, its.vertices.end());
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++ i)
                face(i) = map_vertices[face(i)];
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }

    return num_erased;
}

void its_flip_triangles(indexed_triangle_set &its)
{
    for (stl_triangle_vertex_indices &face : its.indices)
        std::swap(face(1), face(2));
}

int its_remove_degenerate_faces(indexed_triangle_set &its, bool shrink_to_fit)
{
    auto it = std::remove_if(its.indices.begin(), its.indices.end(), [](auto &face) {
        return face(0) == face(1) || face(0) == face(2) || face(1) == face(2);
    });

    int removed = std::distance(it, its.indices.end());
    its.indices.erase(it, its.indices.end());

    if (removed && shrink_to_fit)
        its.indices.shrink_to_fit();

    return removed;
}

int its_compactify_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    std::vector<int> vertex_map(its.vertices.size(), 0);
    for (const stl_triangle_vertex_indices &face : its.indices)
        for (int i = 0; i < 3; ++ i)
            vertex_map[face(i)] = 1;
    int last = 0;
    for (int i = 0; i < int(vertex_map.size()); ++ i)
        if (vertex_map[i]) {
            if (last < i)
                its.vertices[last] = its.vertices[i];
            vertex_map[i] = last ++;
        }
    int removed = int(its.vertices.size()) - last;
    if (removed) {
        its.vertices.erase(its.vertices.begin() + last, its.vertices.end());
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++ i)
                face(i) = vertex_map[face(i)];
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }
    return removed;
}

std::string RepairedMeshErrors::summary() const
{
    std::string out;
    auto add = [&out](int n, const char *format) {
        if (n > 0)
            out += (out.empty() ? "" : ", ") + (boost::format(_u8L(format)) % n).str();
    };
    add(edges_fixed, L("edges joined: %1%"));
    add(degenerate_facets, L("degenerate facets removed: %1%"));
    add(duplicate_facets, L("duplicate facets removed: %1%"));
    add(nonmanifold_edges, L("edges shared by three or more facets made manifold: %1%"));
    add(facets_removed - degenerate_facets - duplicate_facets, L("other facets removed: %1%"));
    add(holes_filled, L("holes closed: %1%"));
    add(facets_added, L("facets added: %1%"));
    add(facets_reversed, L("facets reversed: %1%"));
    add(backwards_edges, L("backwards edges: %1%"));
    return out;
}

namespace {

struct EdgeTable
{
    struct Entry
    {
        uint64_t key;
        int      face_edge;
    };
    std::vector<Entry> entries;
    std::vector<int>   valence;
    std::vector<int>   first;

    explicit EdgeTable(const indexed_triangle_set &its)
    {
        entries.reserve(its.indices.size() * 3);
        for (int f = 0; f < int(its.indices.size()); ++ f)
            for (int i = 0; i < 3; ++ i) {
                const uint64_t a = uint32_t(its.indices[f](i)), b = uint32_t(its.indices[f]((i + 1) % 3));
                entries.push_back({ std::min(a, b) << 32 | std::max(a, b), f * 3 + i });
            }
        tbb::parallel_sort(entries.begin(), entries.end(), [](const Entry &l, const Entry &r) { return l.key < r.key || (l.key == r.key && l.face_edge < r.face_edge); });
        valence.assign(entries.size(), 0);
        first.assign(entries.size(), 0);
        for (size_t i = 0; i < entries.size();) {
            size_t j = i;
            while (j < entries.size() && entries[j].key == entries[i].key)
                ++ j;
            for (size_t k = i; k < j; ++ k) {
                valence[entries[k].face_edge] = int(j - i);
                first[entries[k].face_edge]   = int(i);
            }
            i = j;
        }
    }
    int twin(int face_edge) const
    {
        const int i = first[face_edge];
        return entries[i].face_edge == face_edge ? entries[i + 1].face_edge : entries[i].face_edge;
    }
};

int remove_faces(indexed_triangle_set &its, const std::vector<char> &remove)
{
    size_t k = 0;
    for (size_t f = 0; f < its.indices.size(); ++ f)
        if (! remove[f])
            its.indices[k ++] = its.indices[f];
    const int removed = int(its.indices.size() - k);
    its.indices.resize(k);
    return removed;
}

bool fill_loop(indexed_triangle_set &its, const std::vector<int> &loop, const std::function<bool(int, int)> &edge_exists)
{
    const int    n        = int(loop.size());
    const double infinite = std::numeric_limits<double>::max();
    auto area = [&its, &loop](int i, int k, int j) {
        const Vec3d a = its.vertices[loop[i]].cast<double>(), b = its.vertices[loop[k]].cast<double>(), c = its.vertices[loop[j]].cast<double>();
        return 0.5 * (b - a).cross(c - a).norm();
    };
    std::vector<double> cost(size_t(n) * n, 0.);
    std::vector<int>    split(size_t(n) * n, -1);
    for (int len = 2; len < n; ++ len)
        for (int i = 0; i + len < n; ++ i) {
            const int j    = i + len;
            double    best = infinite;
            if (len == n - 1 || ! edge_exists(loop[i], loop[j]))
                for (int k = i + 1; k < j; ++ k)
                    if (const double l = cost[size_t(i) * n + k], r = cost[size_t(k) * n + j]; l < infinite && r < infinite)
                        if (const double c = l + r + area(i, k, j); c < best) {
                            best                     = c;
                            split[size_t(i) * n + j] = k;
                        }
            cost[size_t(i) * n + j] = best;
        }
    if (cost[n - 1] == infinite)
        return false;
    std::vector<std::pair<int, int>> stack { { 0, n - 1 } };
    while (! stack.empty()) {
        const auto [i, j] = stack.back();
        stack.pop_back();
        if (j - i < 2)
            continue;
        const int k = split[size_t(i) * n + j];
        its.indices.emplace_back(loop[i], loop[k], loop[j]);
        stack.push_back({ i, k });
        stack.push_back({ k, j });
    }
    return true;
}

void close_small_holes(indexed_triangle_set &its, const EdgeTable &edges, float max_hole_size, RepairedMeshErrors &out)
{
    std::multimap<int, int> next;
    for (int fe = 0; fe < int(edges.valence.size()); ++ fe)
        if (edges.valence[fe] == 1)
            next.emplace(its.indices[fe / 3]((fe % 3 + 1) % 3), its.indices[fe / 3](fe % 3));
    std::set<uint64_t> added;
    auto edge_key    = [](int a, int b) { return uint64_t(uint32_t(std::min(a, b))) << 32 | uint32_t(std::max(a, b)); };
    auto edge_exists = [&edges, &added, &edge_key](int a, int b) {
        const uint64_t key = edge_key(a, b);
        return added.count(key) > 0 ||
               std::binary_search(edges.entries.begin(), edges.entries.end(), EdgeTable::Entry { key, 0 },
                                  [](const EdgeTable::Entry &l, const EdgeTable::Entry &r) { return l.key < r.key; });
    };
    const BoundingBoxf3 bbox     = bounding_box(its);
    const double        max_size = std::min<double>(max_hole_size, 0.5 * bbox.size().norm());
    const size_t        max_vertices = 1000;
    const size_t        faces_before = its.indices.size();
    while (! next.empty()) {
        std::vector<int> path { next.begin()->first };
        std::vector<std::vector<int>> loops;
        while (! path.empty()) {
            auto it = next.find(path.back());
            if (it == next.end())
                break;
            const int v = it->second;
            next.erase(it);
            if (auto at = std::find(path.begin(), path.end(), v); at != path.end()) {
                loops.emplace_back(at, path.end());
                path.erase(at + 1, path.end());
                if (path.size() == 1 && next.find(v) == next.end())
                    path.clear();
            } else
                path.push_back(v);
        }
        for (const std::vector<int> &loop : loops) {
            if (loop.size() < 3 || loop.size() > max_vertices)
                continue;
            BoundingBoxf3 box;
            for (int v : loop)
                box.merge(its.vertices[v].cast<double>());
            if (box.size().norm() > max_size)
                continue;
            const size_t faces = its.indices.size();
            if (! fill_loop(its, loop, edge_exists))
                continue;
            for (size_t f = faces; f < its.indices.size(); ++ f)
                for (int i = 0; i < 3; ++ i)
                    added.insert(edge_key(its.indices[f](i), its.indices[f]((i + 1) % 3)));
            ++ out.holes_filled;
            out.facets_added += int(its.indices.size() - faces);
        }
    }
}

}
static RepairedMeshErrors repair_pass(indexed_triangle_set &its, float max_hole_size)
{
    RepairedMeshErrors out;

    if (const size_t open = its_num_open_edges(its); open > 0) {
        indexed_triangle_set merged = its;
        if (its_merge_vertices(merged, false) > 0)
            if (const size_t open_merged = its_num_open_edges(merged); open_merged < open) {
                out.edges_fixed = int(open - open_merged);
                its             = std::move(merged);
            }
    }
    if (its_num_open_edges(its) > 0) {
        auto boundary_edges = [&its](std::vector<int> &boundary) {
            boundary.clear();
            double length = 0.;
            const EdgeTable edges(its);
            for (int fe = 0; fe < int(edges.valence.size()); ++ fe)
                if (edges.valence[fe] == 1) {
                    boundary.push_back(fe);
                    length += (its.vertices[its.indices[fe / 3]((fe % 3 + 1) % 3)] - its.vertices[its.indices[fe / 3](fe % 3)]).norm();
                }
            return boundary.empty() ? 0. : length / boundary.size();
        };
        std::vector<int> boundary;
        const double     mean = boundary_edges(boundary);
        const double     snap = std::min(0.002, 0.05 * mean);
        const double     cell = std::max(mean, 1e-3);
        auto cell_of  = [cell](const Vec3d &p) { return Vec3i64((p / cell).array().floor().cast<int64_t>()); };
        auto cell_key = [](const Vec3i64 &c) { return uint64_t(c.x() * 73856093) ^ uint64_t(c.y() * 19349663) ^ uint64_t(c.z() * 83492791); };
        auto boundary_grid = [&its, &cell_of, &cell_key](const std::vector<int> &boundary) {
            std::unordered_map<uint64_t, std::vector<int>> grid;
            std::vector<char>                              seen(its.vertices.size(), 0);
            for (int fe : boundary)
                for (int v : { its.indices[fe / 3](fe % 3), its.indices[fe / 3]((fe % 3 + 1) % 3) })
                    if (! seen[v]) {
                        seen[v] = 1;
                        grid[cell_key(cell_of(its.vertices[v].cast<double>()))].push_back(v);
                    }
            return grid;
        };
        auto box_cells = [&cell_of, &cell_key](const Vec3d &lo, const Vec3d &hi, auto &&fn) {
            const Vec3i64 c0 = cell_of(lo), c1 = cell_of(hi);
            for (int64_t x = c0.x(); x <= c1.x(); ++ x)
                for (int64_t y = c0.y(); y <= c1.y(); ++ y)
                    for (int64_t z = c0.z(); z <= c1.z(); ++ z)
                        fn(cell_key(Vec3i64(x, y, z)));
        };
        const Vec3d reach = Vec3d::Constant(snap);
        {
            const auto       grid = boundary_grid(boundary);
            std::vector<int> target(its.vertices.size());
            std::iota(target.begin(), target.end(), 0);
            auto root = [&target](int v) {
                while (target[v] != v)
                    v = target[v] = target[target[v]];
                return v;
            };
            for (const auto &[key, vertices] : grid)
                for (int v : vertices) {
                    const Vec3d p = its.vertices[v].cast<double>();
                    box_cells(p - reach, p + reach, [&](uint64_t k) {
                        if (auto it = grid.find(k); it != grid.end())
                            for (int w : it->second)
                                if (w != v && (its.vertices[w].cast<double>() - p).norm() <= snap)
                                    if (const int rv = root(v), rw = root(w); rv != rw)
                                        target[std::max(rv, rw)] = std::min(rv, rw);
                    });
                }
            int merged = 0;
            for (int v = 0; v < int(target.size()); ++ v)
                if ((target[v] = root(v)) != v)
                    ++ merged;
            if (merged > 0) {
                for (stl_triangle_vertex_indices &face : its.indices)
                    for (int i = 0; i < 3; ++ i)
                        face(i) = target[face(i)];
                out.edges_fixed += merged;
            }
        }
        for (int pass = 0; pass < 4; ++ pass) {
            if (pass > 0 || out.edges_fixed > 0)
                boundary_edges(boundary);
            if (boundary.empty())
                break;
            const auto        grid = boundary_grid(boundary);
            std::vector<char> split(its.indices.size(), 0);
            const size_t      faces = its.indices.size();
            for (int fe : boundary) {
                const int f = fe / 3, i = fe % 3;
                if (split[f])
                    continue;
                const int    a = its.indices[f](i), b = its.indices[f]((i + 1) % 3), c = its.indices[f]((i + 2) % 3);
                const Vec3d  pa = its.vertices[a].cast<double>(), ab = its.vertices[b].cast<double>() - pa;
                const double len = ab.norm();
                if (len <= 2. * snap)
                    continue;
                std::vector<std::pair<double, int>> on_edge;
                auto test_cell = [&](uint64_t key) {
                    if (auto it = grid.find(key); it != grid.end())
                        for (int v : it->second) {
                            if (v == a || v == b || v == c)
                                continue;
                            const Vec3d  ap = its.vertices[v].cast<double>() - pa;
                            const double t  = ap.dot(ab) / (len * len);
                            if (t * len > snap && (1. - t) * len > snap && (ap - t * ab).norm() <= snap)
                                on_edge.push_back({ t, v });
                        }
                };
                const Vec3d   lo = pa.cwiseMin(pa + ab) - reach, hi = pa.cwiseMax(pa + ab) + reach;
                const Vec3i64 span = cell_of(hi) - cell_of(lo) + Vec3i64::Ones();
                if (span.x() * span.y() * span.z() <= 27)
                    box_cells(lo, hi, test_cell);
                else
                    for (int k = 0, steps = int(std::ceil(len / cell)); k <= steps; ++ k) {
                        const Vec3d p = pa + ab * (double(k) / steps);
                        box_cells(p - Vec3d::Constant(cell), p + Vec3d::Constant(cell), test_cell);
                    }
                if (on_edge.empty())
                    continue;
                std::sort(on_edge.begin(), on_edge.end());
                on_edge.erase(std::unique(on_edge.begin(), on_edge.end(), [](const auto &l, const auto &r) { return l.second == r.second; }), on_edge.end());
                split[f]       = 1;
                its.indices[f] = stl_triangle_vertex_indices(a, on_edge.front().second, c);
                for (size_t k = 0; k < on_edge.size(); ++ k)
                    its.indices.emplace_back(on_edge[k].second, k + 1 < on_edge.size() ? on_edge[k + 1].second : b, c);
                out.edges_fixed += int(on_edge.size());
            }
            if (its.indices.size() == faces)
                break;
            out.facets_added += int(its.indices.size() - faces);
        }
    }
    out.degenerate_facets = its_remove_degenerate_faces(its, false);

    {
        std::vector<std::pair<std::array<int, 3>, int>> sorted;
        sorted.reserve(its.indices.size());
        for (int f = 0; f < int(its.indices.size()); ++ f) {
            std::array<int, 3> v { its.indices[f](0), its.indices[f](1), its.indices[f](2) };
            std::sort(v.begin(), v.end());
            sorted.push_back({ v, f });
        }
        tbb::parallel_sort(sorted.begin(), sorted.end());
        std::vector<char> remove(its.indices.size(), 0);
        for (size_t i = 1; i < sorted.size(); ++ i)
            if (sorted[i].first == sorted[i - 1].first)
                remove[sorted[i].second] = 1;
        out.duplicate_facets = remove_faces(its, remove);
    }

    std::optional<EdgeTable> table;
    auto edge_table = [&table, &its]() -> const EdgeTable & {
        if (! table)
            table.emplace(its);
        return *table;
    };

    for (bool changed = true; changed;) {
        changed = false;
        const EdgeTable  &edges = edge_table();
        std::vector<char> remove(its.indices.size(), 0);
        auto open_edges = [&edges](int face) {
            return int(edges.valence[face * 3] == 1) + int(edges.valence[face * 3 + 1] == 1) + int(edges.valence[face * 3 + 2] == 1);
        };
        for (size_t i = 0; i < edges.entries.size();) {
            size_t j = i;
            while (j < edges.entries.size() && edges.entries[j].key == edges.entries[i].key)
                ++ j;
            if (j - i > 2) {
                std::vector<std::pair<int, int>> faces;
                for (size_t k = i; k < j; ++ k)
                    if (int face = edges.entries[k].face_edge / 3; ! remove[face])
                        faces.push_back({ open_edges(face), face });
                std::sort(faces.begin(), faces.end(), std::greater<>());
                size_t left = faces.size();
                for (const auto &[open, face] : faces)
                    if (left > 2 && open > 0) {
                        remove[face] = 1;
                        -- left;
                    }
                if (left == 2 && faces.size() > 2)
                    ++ out.nonmanifold_edges;
            }
            i = j;
        }
        if (int removed = remove_faces(its, remove); removed > 0) {
            out.facets_removed += removed;
            changed = true;
            table.reset();
        }
    }

    {
        const EdgeTable   &edges  = edge_table();
        const int          nfaces = int(its.indices.size());
        std::vector<int>   patch(nfaces, -1);
        std::vector<char>  flip(nfaces, 0);
        std::vector<int>   queue;
        std::optional<AABBTreeIndirect::Tree3f> tree;
        std::vector<igl::Hit<float>>            hits;
        auto faces_inward = [&its, &tree, &hits, &flip](std::vector<int> faces) {
            if (! tree)
                tree.emplace(AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices));
            auto area = [&its](int f) { return its_unnormalized_normal(its, f).squaredNorm(); };
            const size_t samples = std::min<size_t>(7, faces.size());
            std::nth_element(faces.begin(), faces.begin() + (samples - 1), faces.end(), [&area](int l, int r) { return area(l) > area(r); });
            int inward = 0, outward = 0;
            for (size_t k = 0; k < samples; ++ k) {
                const int f = faces[k];
                Vec3d     n = its_unnormalized_normal(its, f).cast<double>();
                if (n.squaredNorm() < 1e-12)
                    continue;
                n = (flip[f] ? -1. : 1.) * n.normalized();
                const Vec3d centroid = (its.vertices[its.indices[f](0)] + its.vertices[its.indices[f](1)] + its.vertices[its.indices[f](2)]).cast<double>() / 3.;
                AABBTreeIndirect::intersect_ray_all_hits(its.vertices, its.indices, *tree, Vec3d(centroid + 1e-3 * n), n, hits);
                int    crossings = 0;
                double last      = -1.;
                for (const igl::Hit<float> &hit : hits)
                    if (hit.id != f && std::abs(hit.t - last) > 1e-5) {
                        ++ crossings;
                        last = hit.t;
                    }
                ++ (crossings % 2 ? inward : outward);
            }
            return inward > outward;
        };
        for (int seed = 0; seed < nfaces; ++ seed) {
            if (patch[seed] >= 0)
                continue;
            queue.assign(1, seed);
            patch[seed]  = seed;
            size_t flips = 0;
            for (size_t q = 0; q < queue.size(); ++ q) {
                const int f = queue[q];
                for (int i = 0; i < 3; ++ i) {
                    if (edges.valence[f * 3 + i] != 2)
                        continue;
                    const int tw = edges.twin(f * 3 + i), g = tw / 3;
                    if (patch[g] >= 0)
                        continue;
                    const bool same = its.indices[f](i) == its.indices[g](tw % 3);
                    patch[g]        = seed;
                    flip[g]         = char(flip[f] != same);
                    flips += flip[g];
                    queue.push_back(g);
                }
            }
            const bool open = std::any_of(queue.begin(), queue.end(), [&edges](int f) {
                return edges.valence[f * 3] != 2 || edges.valence[f * 3 + 1] != 2 || edges.valence[f * 3 + 2] != 2;
            });
            if (open ? faces_inward(queue) : 2 * flips > queue.size())
                for (int f : queue)
                    flip[f] = ! flip[f];
        }
        for (int f = 0; f < nfaces; ++ f)
            if (flip[f]) {
                std::swap(its.indices[f](1), its.indices[f](2));
                ++ out.facets_reversed;
                table.reset();
            }
    }

    {
        const size_t faces = its.indices.size();
        close_small_holes(its, edge_table(), max_hole_size, out);
        if (its.indices.size() > faces)
            table.reset();
    }

    {
        struct Shell
        {
            std::vector<int> faces;
            bool             closed = true;
            double           volume = 0.;
            BoundingBoxf3    box;
        };
        const EdgeTable   &edges  = edge_table();
        const int          nfaces = int(its.indices.size());
        std::vector<char>  seen(nfaces, 0);
        std::vector<Shell> shells;
        double             total = 0.;
        for (int seed = 0; seed < nfaces; ++ seed) {
            if (seen[seed])
                continue;
            Shell &shell = shells.emplace_back();
            shell.faces.assign(1, seed);
            seen[seed] = 1;
            for (size_t q = 0; q < shell.faces.size(); ++ q) {
                const int   f = shell.faces[q];
                const Vec3d a = its.vertices[its.indices[f](0)].cast<double>(), b = its.vertices[its.indices[f](1)].cast<double>(),
                            c = its.vertices[its.indices[f](2)].cast<double>();
                shell.volume += a.dot(b.cross(c)) / 6.;
                shell.box.merge(a);
                shell.box.merge(b);
                shell.box.merge(c);
                for (int i = 0; i < 3; ++ i) {
                    if (edges.valence[f * 3 + i] != 2) {
                        shell.closed = false;
                        continue;
                    }
                    if (const int g = edges.twin(f * 3 + i) / 3; ! seen[g]) {
                        seen[g] = 1;
                        shell.faces.push_back(g);
                    }
                }
            }
            if (shell.closed)
                total += shell.volume;
        }
        auto flip = [&its, &out](const Shell &shell) {
            for (int f : shell.faces)
                std::swap(its.indices[f](1), its.indices[f](2));
            out.facets_reversed += int(shell.faces.size());
        };
        auto held = [&shells](const Shell &shell) {
            return std::any_of(shells.begin(), shells.end(), [&shell](const Shell &other) {
                return &other != &shell && other.closed && other.box.contains(shell.box.min) && other.box.contains(shell.box.max);
            });
        };
        for (const Shell &shell : shells)
            if (shell.closed && (total < 0. || (shell.volume < 0. && ! held(shell))))
                flip(shell);
    }

    out.facets_removed += out.degenerate_facets + out.duplicate_facets;
    if (out.facets_removed > 0 || out.edges_fixed > 0)
        its_compactify_vertices(its, false);
    return out;
}

RepairedMeshErrors its_close_holes(indexed_triangle_set &its, float max_hole_size)
{
    RepairedMeshErrors out;
    if (! its.indices.empty())
        close_small_holes(its, EdgeTable(its), max_hole_size, out);
    return out;
}

RepairedMeshErrors its_repair_on_load(indexed_triangle_set &its, float max_hole_size)
{
    RepairedMeshErrors out;
    for (int pass = 0; pass < 3 && ! its.indices.empty(); ++ pass) {
        const RepairedMeshErrors fixed = repair_pass(its, max_hole_size);
        if (! fixed.repaired())
            break;
        out.merge(fixed);
    }
    if (out.repaired())
        its_shrink_to_fit(its);
    return out;
}

bool its_store_triangle(const indexed_triangle_set &its,
                        const char *                obj_filename,
                        size_t                      triangle_index)
{
    if (its.indices.size() <= triangle_index) return false;
    Vec3i32                t = its.indices[triangle_index];
    indexed_triangle_set its2;
    its2.indices  = {{0, 1, 2}};
    its2.vertices = {its.vertices[t[0]], its.vertices[t[1]],
                     its.vertices[t[2]]};
    return its_write_obj(its2, obj_filename);
}

bool its_store_triangles(const indexed_triangle_set &its,
                         const char *                obj_filename,
                         const std::vector<size_t> & triangles)
{
    indexed_triangle_set its2;
    its2.vertices.reserve(triangles.size() * 3);
    its2.indices.reserve(triangles.size());
    std::map<size_t, size_t> vertex_map;
    for (auto ti : triangles) {
        if (its.indices.size() <= ti) return false;
        Vec3i32 t = its.indices[ti];
        Vec3i32 new_t;
        for (size_t i = 0; i < 3; ++i) {
            size_t vi = t[i];
            auto   it = vertex_map.find(vi);
            if (it != vertex_map.end()) {
                new_t[i] = it->second;
                continue;
            }
            size_t new_vi = its2.vertices.size();
            its2.vertices.push_back(its.vertices[vi]);
            vertex_map[vi] = new_vi;
            new_t[i]       = new_vi;
        }
        its2.indices.push_back(new_t);
    }
    return its_write_obj(its2, obj_filename);
}

void its_shrink_to_fit(indexed_triangle_set &its)
{
    its.indices.shrink_to_fit();
    its.vertices.shrink_to_fit();
}

template<typename TransformVertex>
void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const TransformVertex &transform_fn, const float z, Points &all_pts)
{
    all_pts.reserve(all_pts.size() + its.indices.size() * 3);
    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f pts[3] = { transform_fn(its.vertices[tri(0)]), transform_fn(its.vertices[tri(1)]), transform_fn(its.vertices[tri(2)]) };
        int iprev = 2;
        for (int iedge = 0; iedge < 3; ++ iedge) {
            const Vec3f &p1 = pts[iprev];
            const Vec3f &p2 = pts[iedge];
            if ((p1.z() < z && p2.z() > z) || (p2.z() < z && p1.z() > z)) {
                float t = (z - p1.z()) / (p2.z() - p1.z());
                all_pts.emplace_back(scaled<coord_t>(p1.x() + (p2.x() - p1.x()) * t), scaled<coord_t>(p1.y() + (p2.y() - p1.y()) * t));
            }
            if (p2.z() >= z)
                all_pts.emplace_back(scaled<coord_t>(p2.x()), scaled<coord_t>(p2.y()));
            iprev = iedge;
        }
    }
}

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Matrix3f &m, const float z, Points &all_pts)
{
    return its_collect_mesh_projection_points_above(its, [m](const Vec3f &p){ return m * p; }, z, all_pts);
}

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Transform3f &t, const float z, Points &all_pts)
{
    return its_collect_mesh_projection_points_above(its, [t](const Vec3f &p){ return t * p; }, z, all_pts);
}

template<typename TransformVertex>
Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const TransformVertex &transform_fn, const float z)
{
    Points all_pts;
    its_collect_mesh_projection_points_above(its, transform_fn, z, all_pts);
    return Geometry::convex_hull(std::move(all_pts));
}

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Matrix3f &m, const float z)
{
    return its_convex_hull_2d_above(its, [m](const Vec3f &p){ return m * p; }, z);
}

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Transform3f &t, const float z)
{
    return its_convex_hull_2d_above(its, [t](const Vec3f &p){ return t * p; }, z);
}

indexed_triangle_set its_make_cube(double xd, double yd, double zd)
{
    auto x = float(xd), y = float(yd), z = float(zd);
    return {
        { {0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7},
          {0, 4, 7}, {0, 7, 1}, {1, 7, 6}, {1, 6, 2},
          {2, 6, 5}, {2, 5, 3}, {4, 0, 3}, {4, 3, 5} },
        { {x, y, 0}, {x, 0, 0}, {0, 0, 0}, {0, y, 0},
          {x, y, z}, {0, y, z}, {0, 0, z}, {x, 0, z} }
    };
}

indexed_triangle_set its_make_prism(float width, float length, float height)
{
    float x = width / 2.f, y = length / 2.f;
    return {
        {
            {0, 1, 2},
            {4, 3, 5},
            {1, 4, 2}, {2, 4, 5},
            {0, 2, 5}, {0, 5, 3},
            {3, 4, 1}, {3, 1, 0}
        },
        {
            {-x, -y, 0.f}, {x, -y, 0.f}, {0.f, -y, height},
            {-x, y, 0.f}, {x, y, 0.f}, {0.f, y, height},
        }
    };
}

indexed_triangle_set its_make_cylinder(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    size_t n_steps    = (size_t)ceil(2. * PI / fa);
    double angle_step = 2. * PI / n_steps;

    auto &vertices = mesh.vertices;
    auto &facets   = mesh.indices;
    vertices.reserve(2 * n_steps + 2);
    facets.reserve(4 * n_steps);

    vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    vertices.emplace_back(Vec3f(0.f, 0.f, float(h)));

    Vec2f p = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, r);
    vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
    vertices.emplace_back(Vec3f(p(0), p(1), float(h)));
    for (size_t i = 1; i < n_steps; ++i) {
        p = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, float(r));
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
        vertices.emplace_back(Vec3f(p(0), p(1), float(h)));
        int id = (int)vertices.size() - 1;
        facets.emplace_back( 0, id - 1, id - 3);
        facets.emplace_back(id,      1, id - 2);
        facets.emplace_back(id, id - 2, id - 3);
        facets.emplace_back(id, id - 3, id - 1);
    }
    int id = (int)vertices.size() - 1;
    facets.emplace_back( 0, 2, id - 1);
    facets.emplace_back( 3, 1,     id);
    facets.emplace_back(id, 2,      3);
    facets.emplace_back(id, id - 1, 2);

    return mesh;
}

indexed_triangle_set its_make_frustum(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    size_t n_steps    = (size_t)ceil(2. * PI / fa);
    double angle_step = 2. * PI / n_steps;

    auto &vertices = mesh.vertices;
    auto &facets   = mesh.indices;
    vertices.reserve(2 * n_steps + 2);
    facets.reserve(4 * n_steps);

    vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    vertices.emplace_back(Vec3f(0.f, 0.f, float(h)));

    Vec2f vec_top = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, 0.5f*r);
    Vec2f vec_botton = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, r);

    vertices.emplace_back(Vec3f(vec_botton(0), vec_botton(1), 0.f));
    vertices.emplace_back(Vec3f(vec_top(0), vec_top(1), float(h)));
    for (size_t i = 1; i < n_steps; ++i) {
        vec_top = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, 0.5f*float(r));
        vec_botton = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, float(r));
        vertices.emplace_back(Vec3f(vec_botton(0), vec_botton(1), 0.f));
        vertices.emplace_back(Vec3f(vec_top(0), vec_top(1), float(h)));
        int id = (int)vertices.size() - 1;
        facets.emplace_back( 0, id - 1, id - 3);
        facets.emplace_back(id,      1, id - 2);
        facets.emplace_back(id, id - 2, id - 3);
        facets.emplace_back(id, id - 3, id - 1);
    }
    int id = (int)vertices.size() - 1;
    facets.emplace_back( 0, 2, id - 1);
    facets.emplace_back( 3, 1,     id);
    facets.emplace_back(id, 2,      3);
    facets.emplace_back(id, id - 1, 2);

    return mesh;
}

indexed_triangle_set its_make_torus(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    auto& facets = mesh.indices;

    size_t n_major = (size_t)ceil(2. * PI / fa);
    size_t n_minor = (size_t)ceil(2. * PI / fa);

    double major_step = 2. * PI / n_major;
    double minor_step = 2. * PI / n_minor;

    vertices.reserve(n_major * n_minor);
    facets.reserve(n_major * n_minor * 2);

    for (size_t i = 0; i < n_major; ++i) {
        double major_angle = i * major_step;
        double cos_major = cos(major_angle);
        double sin_major = sin(major_angle);
        for (size_t j = 0; j < n_minor; ++j) {
            double minor_angle = j * minor_step;
            double cos_minor = cos(minor_angle);
            double sin_minor = sin(minor_angle);
            float x = float((r + h * cos_minor) * cos_major);
            float y = float((r + h * cos_minor) * sin_major);
            float z = float(h * sin_minor);
            vertices.emplace_back(Vec3f(x, y, z));
        }
    }

    for (size_t i = 0; i < n_major; ++i) {
        size_t inext = (i + 1) % n_major;
        for (size_t j = 0; j < n_minor; ++j) {
            size_t jnext = (j + 1) % n_minor;
            int v0 = int(i * n_minor + j);
            int v1 = int(inext * n_minor + j);
            int v2 = int(inext * n_minor + jnext);
            int v3 = int(i * n_minor + jnext);
            facets.emplace_back(v0, v1, v2);
            facets.emplace_back(v0, v2, v3);
        }
    }

    return mesh;
}

indexed_triangle_set its_make_cone(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    auto& facets = mesh.indices;
    vertices.reserve(3 + 2 * size_t(2 * PI / fa));

    vertices.emplace_back(Vec3f::Zero());
    vertices.emplace_back(Vec3f(0., 0., h));

    size_t i = 0;
    for (double angle=0; angle<2*PI; angle+=fa) {
        vertices.emplace_back(r*std::cos(angle), r*std::sin(angle), 0.);
        if (angle > 0.) {
            facets.emplace_back(0, i+2, i+1);
            facets.emplace_back(1, i+1, i+2);
        }
        ++i;
    }
    facets.emplace_back(0, 2, i+1);
    facets.emplace_back(1, i+1, 2);

    return mesh;
}

indexed_triangle_set its_make_pyramid(float base, float height)
{
    float a = base / 2.f;
    return {
        {
            {0, 1, 2},
            {0, 2, 3},
            {0, 1, 4},
            {1, 2, 4},
            {2, 3, 4},
            {3, 0, 4}
        },
        {
            {-a, -a, 0}, {a, -a, 0}, {a, a, 0},
            {-a, a, 0}, {0.f, 0.f, height}
        }
    };
}

indexed_triangle_set its_make_sphere(double radius, double fa)
{
    int   sectorCount = int(ceil(2. * M_PI / fa));
    int   stackCount  = int(ceil(M_PI / fa));
    float sectorStep  = float(2. * M_PI / sectorCount);
    float stackStep   = float(M_PI / stackCount);

    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    vertices.reserve((stackCount - 1) * sectorCount + 2);
    for (int i = 0; i <= stackCount; ++ i) {
        double stackAngle = 0.5 * M_PI - stackStep * i;
        double xy = radius * cos(stackAngle);
        double z  = radius * sin(stackAngle);
        if (i == 0 || i == stackCount)
            vertices.emplace_back(Vec3f(float(xy), 0.f, float(z)));
        else
            for (int j = 0; j < sectorCount; ++ j) {
                double sectorAngle = sectorStep * j;
                vertices.emplace_back(Vec3d(xy * std::cos(sectorAngle), xy * std::sin(sectorAngle), z).cast<float>());
            }
    }

    auto& facets = mesh.indices;
    facets.reserve(2 * (stackCount - 1) * sectorCount);
    for (int i = 0; i < stackCount; ++ i) {
        int k1 = (i == 0) ? 0 : (1 + (i - 1) * sectorCount);
        int k1_first = k1;
        int k2 = (i == 0) ? 1 : (k1 + sectorCount);
        int k2_first = k2;
        for (int j = 0; j < sectorCount; ++ j) {
            int k1_next = k1;
            int k2_next = k2;
            if (i != 0) {
                k1_next = (j + 1 == sectorCount) ? k1_first : (k1 + 1);
                facets.emplace_back(k1, k2, k1_next);
            }
            if (i + 1 != stackCount) {
                k2_next = (j + 1 == sectorCount) ? k2_first : (k2 + 1);
                facets.emplace_back(k1_next, k2, k2_next);
            }
            k1 = k1_next;
            k2 = k2_next;
        }
    }

    return mesh;
}

indexed_triangle_set its_make_frustum_dowel(double radius, double h, int sectorCount)
{
    int   stackCount = 2;
    float sectorStep = float(2. * M_PI / sectorCount);
    float stackStep = float(M_PI / stackCount);

    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    vertices.reserve((stackCount - 1) * sectorCount + 2);
    for (int i = 0; i <= stackCount; ++i) {
        double stackAngle = 0.5 * M_PI - stackStep * i;
        double xy = radius * cos(stackAngle);
        double z = radius * sin(stackAngle);
        if (i == 0 || i == stackCount)
            vertices.emplace_back(Vec3f(float(xy), 0.f, float(h * sin(stackAngle))));
        else
            for (int j = 0; j < sectorCount; ++j) {
                double sectorAngle = sectorStep * j + 0.25 * M_PI;
                vertices.emplace_back(Vec3d(xy * std::cos(sectorAngle), xy * std::sin(sectorAngle), z).cast<float>());
            }
    }

    auto& facets = mesh.indices;
    facets.reserve(2 * (stackCount - 1) * sectorCount);
    for (int i = 0; i < stackCount; ++i) {
        int k1 = (i == 0) ? 0 : (1 + (i - 1) * sectorCount);
        int k1_first = k1;
        int k2 = (i == 0) ? 1 : (k1 + sectorCount);
        int k2_first = k2;
        for (int j = 0; j < sectorCount; ++j) {
            int k1_next = k1;
            int k2_next = k2;
            if (i != 0) {
                k1_next = (j + 1 == sectorCount) ? k1_first : (k1 + 1);
                facets.emplace_back(k1, k2, k1_next);
            }
            if (i + 1 != stackCount) {
                k2_next = (j + 1 == sectorCount) ? k2_first : (k2 + 1);
                facets.emplace_back(k1_next, k2, k2_next);
            }
            k1 = k1_next;
            k2 = k2_next;
        }
    }

    return mesh;
}

indexed_triangle_set its_make_snap(double r, double h, float space_proportion, float bulge_proportion)
{
    const float radius = (float)r;
    const float height = (float)h;
    const size_t sectors_cnt = 10;
    const float halfPI = 0.5f * (float)PI;

    const float space_len = space_proportion * radius;

    const float b_len = radius;
    const float m_len = (1 + bulge_proportion) * radius;
    const float t_len = 0.5f * radius;

    const float b_height = 0.f;
    const float m_height = 0.5f * height;
    const float t_height = height;

    const float b_angle = acos(space_len/b_len);
    const float t_angle = acos(space_len/t_len);

    const float b_angle_step = b_angle / (float)sectors_cnt;
    const float t_angle_step = t_angle / (float)sectors_cnt;

    const Vec2f b_vec = Eigen::Vector2f(0, b_len);
    const Vec2f t_vec = Eigen::Vector2f(0, t_len);


    auto add_side_vertices = [b_vec, t_vec, b_height, m_height, t_height](std::vector<stl_vertex>& vertices, float b_angle, float t_angle, const Vec2f& m_vec) {
        Vec2f b_pt = Eigen::Rotation2Df(b_angle) * b_vec;
        Vec2f m_pt = Eigen::Rotation2Df(b_angle) * m_vec;
        Vec2f t_pt = Eigen::Rotation2Df(t_angle) * t_vec;

        vertices.emplace_back(Vec3f(b_pt(0), b_pt(1), b_height));
        vertices.emplace_back(Vec3f(m_pt(0), m_pt(1), m_height));
        vertices.emplace_back(Vec3f(t_pt(0), t_pt(1), t_height));
    };

    auto add_side_facets = [](std::vector<stl_triangle_vertex_indices>& facets, int vertices_cnt, int frst_id, int scnd_id) {
        int id = vertices_cnt - 1;

        facets.emplace_back(frst_id, id - 2, id - 5);

        facets.emplace_back(id - 2, id - 1, id - 5);
        facets.emplace_back(id - 1, id - 4, id - 5);
        facets.emplace_back(id - 4, id - 1, id);
        facets.emplace_back(id, id - 3, id - 4);

        facets.emplace_back(id, scnd_id, id - 3);
    };

    const float f = (b_len - m_len) / m_len;

    auto get_m_len = [b_len, f](float angle) {
        const float rad_sqr = b_len * b_len;
        const float sin_sqr = sin(angle) * sin(angle);
        const float f_sqr = (1-f)*(1-f);
        return sqrtf(rad_sqr / (1 + (1 / f_sqr - 1) * sin_sqr));
    };

    auto add_sub_mesh = [add_side_vertices, add_side_facets, get_m_len,
                        b_height, t_height, b_angle, t_angle, b_angle_step, t_angle_step]
                        (indexed_triangle_set& mesh, float center_x, float angle_rotation, int frst_vertex_id) {
        auto& vertices = mesh.vertices;
        auto& facets     = mesh.indices;

        vertices.emplace_back(Vec3f(center_x, 0.f, b_height));
        vertices.emplace_back(Vec3f(center_x, 0.f, t_height));

        float b_angle_start = angle_rotation - b_angle;
        float t_angle_start = angle_rotation - t_angle;
        const float b_angle_stop  = angle_rotation + b_angle;

        const int frst_id = frst_vertex_id;
        const int scnd_id = frst_id + 1;

        {
            const Vec2f m_vec = Eigen::Vector2f(0, get_m_len(b_angle_start));
            add_side_vertices(vertices, b_angle_start, t_angle_start, m_vec);

            int id = (int)vertices.size() - 1;

            facets.emplace_back(frst_id, id - 2, id - 1);
            facets.emplace_back(frst_id, id - 1, id);
            facets.emplace_back(frst_id, id, scnd_id);
        }

        while (!is_approx(b_angle_start, b_angle_stop)) {
            b_angle_start += b_angle_step;
            t_angle_start += t_angle_step;

            const Vec2f m_vec = Eigen::Vector2f(0, get_m_len(b_angle_start));
            add_side_vertices(vertices, b_angle_start, t_angle_start, m_vec);

            add_side_facets(facets, (int)vertices.size(), frst_id, scnd_id);
        }

        {
            int id = (int)vertices.size() - 1;

            facets.emplace_back(frst_id, scnd_id, id);
            facets.emplace_back(frst_id, id, id - 1);
            facets.emplace_back(frst_id, id - 1, id - 2);
        }
    };


    indexed_triangle_set mesh;

    mesh.vertices.reserve(2 * (3 * (2 * sectors_cnt + 1) + 2));
    mesh.indices.reserve(2 * (6 * 2 * sectors_cnt + 6));

    add_sub_mesh(mesh, -space_len, halfPI    , 0);
    add_sub_mesh(mesh,  space_len, 3 * halfPI, (int)mesh.vertices.size());

    return mesh;
}

indexed_triangle_set its_convex_hull(const std::vector<Vec3f> &pts)
{
    std::vector<Vec3f>  dst_vertices;
    std::vector<Vec3i32>  dst_facets;

    if (! pts.empty()) {
        orgQhull::Qhull qhull;
        qhull.disableOutputStream();
    #if ! REALfloat
        std::vector<realT> src_vertices;
    #endif
        try {
    #if REALfloat
            qhull.runQhull("", 3, (int)pts.size(), (const realT*)(pts.front().data()), "Qt");
    #else
            src_vertices.reserve(pts.size() * 3);
            for (const stl_vertex &v : pts)
                for (int i = 0; i < 3; ++ i)
                    src_vertices.emplace_back(v(i));
            qhull.runQhull("", 3, (int)src_vertices.size() / 3, src_vertices.data(), "Qt");
    #endif
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "its_convex_hull: Unable to create convex hull";
            return {};
        }

        std::vector<int>    map_dst_vertices;
    #ifndef NDEBUG
        Vec3f               centroid = Vec3f::Zero();
        for (const stl_vertex& pt : pts)
            centroid += pt;
        centroid /= float(pts.size());
    #endif
        for (const orgQhull::QhullFacet &facet : qhull.facetList()) {
            Vec3i32  indices;
            int    cnt = 0;
            for (const orgQhull::QhullVertex vertex : facet.vertices()) {
                int id = vertex.id();
                assert(id >= 0);
                if (id >= int(map_dst_vertices.size()))
                    map_dst_vertices.resize(next_highest_power_of_2(size_t(id + 1)), -1);
                if (int i = map_dst_vertices[id]; i == -1) {
                    i = int(dst_vertices.size());
                    map_dst_vertices[id] = i;
                    orgQhull::QhullPoint pt(vertex.point());
                    dst_vertices.emplace_back(pt[0], pt[1], pt[2]);
                    indices[cnt] = i;
                } else {
                    indices[cnt] = i;
                }
                if (cnt ++ == 3)
                    break;
            }
            assert(cnt == 3);
            if (cnt == 3) {
                Vec3f n  = (dst_vertices[indices(1)] - dst_vertices[indices(0)]).cross(dst_vertices[indices(2)] - dst_vertices[indices(1)]);
                auto *n2 = facet.getBaseT()->normal;
                auto  d = n.x() * n2[0] + n.y() * n2[1] + n.z() * n2[2];
    #ifndef NDEBUG
                Vec3f n3 = (dst_vertices[indices(0)] - centroid);
                auto  d3 = n.dot(n3);
                assert((d < 0.f) == (d3 < 0.f));
    #endif
                if (d < 0.f)
                    std::swap(indices[1], indices[2]);
                dst_facets.emplace_back(indices);
            }
        }
    }

    return { std::move(dst_facets), std::move(dst_vertices) };
}

void its_reverse_all_facets(indexed_triangle_set &its)
{
    for (stl_triangle_vertex_indices &face : its.indices)
        std::swap(face[0], face[1]);
}

void its_merge(indexed_triangle_set &A, const indexed_triangle_set &B)
{
    auto N   = int(A.vertices.size());
    auto N_f = A.indices.size();

    A.vertices.insert(A.vertices.end(), B.vertices.begin(), B.vertices.end());
    A.indices.insert(A.indices.end(), B.indices.begin(), B.indices.end());

    for(size_t n = N_f; n < A.indices.size(); n++)
        A.indices[n] += Vec3i32{N, N, N};
}

void its_merge(indexed_triangle_set &A, const std::vector<Vec3f> &triangles)
{
    const size_t offs = A.vertices.size();
    A.vertices.insert(A.vertices.end(), triangles.begin(), triangles.end());
    A.indices.reserve(A.indices.size() + A.vertices.size() / 3);

    for(int i = int(offs); i < int(A.vertices.size()); i += 3)
        A.indices.emplace_back(i, i + 1, i + 2);
}

void its_merge(indexed_triangle_set &A, const Pointf3s &triangles)
{
    auto trianglesf = reserve_vector<Vec3f> (triangles.size());
    for (auto &t : triangles)
        trianglesf.emplace_back(t.cast<float>());

    its_merge(A, trianglesf);
}

float its_volume(const indexed_triangle_set &its)
{
    if (its.empty()) return 0.;

    auto p0 = its.vertices.front();
    float volume = 0.f;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        its_triangle triangle = its_triangle_vertices(its, i);
        Vec3f U = triangle[1] - triangle[0];
        Vec3f V = triangle[2] - triangle[0];
        Vec3f C = U.cross(V);
        Vec3f normal = C.normalized();
        float area = 0.5 * C.norm();
        float height = normal.dot(triangle[0] - p0);
        volume += (area * height) / 3.0f;
    }

    return volume;
}

MassProperties MassProperties::transformed(const Transform3d &trafo) const
{
    const Matrix3d linear = trafo.linear();
    const double   scale  = std::abs(linear.determinant());
    return { mass * scale, volume * scale, trafo * center, linear * spread * linear.transpose() };
}

MassProperties its_mass_properties(const indexed_triangle_set &its)
{
    if (its.indices.empty())
        return {};

    const Vec3d p0        = its.vertices.front().cast<double>();
    double      volume6   = 0.;
    Vec3d       moment24  = Vec3d::Zero();
    Matrix3d    second120 = Matrix3d::Zero();
    for (const stl_triangle_vertex_indices &face : its.indices) {
        const Vec3d  a = its.vertices[face(0)].cast<double>() - p0;
        const Vec3d  b = its.vertices[face(1)].cast<double>() - p0;
        const Vec3d  c = its.vertices[face(2)].cast<double>() - p0;
        const Vec3d  s = a + b + c;
        const double v = a.dot(b.cross(c));
        volume6 += v;
        moment24 += v * s;
        second120 += v * (a * a.transpose() + b * b.transpose() + c * c.transpose() + s * s.transpose());
    }
    if (volume6 == 0.)
        return {};
    const Vec3d  center = moment24 / (4. * volume6);
    const double volume = std::abs(volume6) / 6.;
    return { volume, volume, p0 + center, second120 / (20. * volume6) - center * center.transpose() };
}

float its_average_edge_length(const indexed_triangle_set &its)
{
    if (its.indices.empty())
        return 0.f;

    double edge_length = 0.f;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const its_triangle v = its_triangle_vertices(its, i);
        edge_length += (v[1] - v[0]).cast<double>().norm() + 
                       (v[2] - v[0]).cast<double>().norm() +
                       (v[1] - v[2]).cast<double>().norm();
    }
    return float(edge_length / (3 * its.indices.size()));
}

std::vector<indexed_triangle_set> its_split(const indexed_triangle_set &its)
{
    return its_split<>(its);
}

size_t its_number_of_patches(const indexed_triangle_set &its)
{
    return its_number_of_patches<>(its);
}
size_t its_number_of_patches(const indexed_triangle_set &its, const std::vector<Vec3i32> &face_neighbors)
{
    return its_number_of_patches<>(ItsNeighborsWrapper{ its, face_neighbors });
}

bool its_is_splittable(const indexed_triangle_set &its)
{
    return its_is_splittable<>(its);
}
bool its_is_splittable(const indexed_triangle_set &its, const std::vector<Vec3i32> &face_neighbors)
{
    return its_is_splittable<>(ItsNeighborsWrapper{ its, face_neighbors });
}

size_t its_num_open_edges(const std::vector<Vec3i32> &face_neighbors)
{
    size_t num_open_edges = 0;
    for (Vec3i32 neighbors : face_neighbors)
        for (int n : neighbors)
            if (n < 0)
                ++ num_open_edges;
    return num_open_edges;
}

size_t its_num_open_edges(const indexed_triangle_set &its)
{
    return its_num_open_edges(its_face_neighbors(its));
}

void VertexFaceIndex::create(const indexed_triangle_set &its)
{
    m_vertex_to_face_start.assign(its.vertices.size() + 1, 0);
    for (auto &face : its.indices) {
        ++ m_vertex_to_face_start[face(0) + 1];
        ++ m_vertex_to_face_start[face(1) + 1];
        ++ m_vertex_to_face_start[face(2) + 1];
    }
    for (size_t i = 2; i < m_vertex_to_face_start.size(); ++ i)
        m_vertex_to_face_start[i] += m_vertex_to_face_start[i - 1];
    m_vertex_faces_all.assign(m_vertex_to_face_start.back(), 0);
    for (size_t face_idx = 0; face_idx < its.indices.size(); ++ face_idx) {
        auto &face = its.indices[face_idx];
        for (int i = 0; i < 3; ++ i)
            m_vertex_faces_all[m_vertex_to_face_start[face(i)] ++] = face_idx;
    }
    for (auto i = int(m_vertex_to_face_start.size()) - 1; i > 0; -- i)
        m_vertex_to_face_start[i] = m_vertex_to_face_start[i - 1];
    m_vertex_to_face_start.front() = 0;
}

std::vector<Vec3i32> its_face_neighbors(const indexed_triangle_set &its)
{
    return create_face_neighbors_index(ex_seq, its);
}

std::vector<Vec3i32> its_face_neighbors_par(const indexed_triangle_set &its)
{
    return create_face_neighbors_index(ex_tbb, its);
}

std::vector<Vec3f> its_face_normals(const indexed_triangle_set &its) 
{
    std::vector<Vec3f> normals;
    normals.reserve(its.indices.size());
    for (stl_triangle_vertex_indices face : its.indices)
        normals.push_back(its_face_normal(its, face));
    return normals;
}

#if BOOST_ENDIAN_LITTLE_BYTE
static inline void big_endian_reverse_quads(char*, size_t) {}
#else
static inline void big_endian_reverse_quads(char *buf, size_t cnt)
{
    for (size_t i = 0; i < cnt; i += 4) {
        std::swap(buf[i], buf[i+3]);
        std::swap(buf[i+1], buf[i+2]);
    }
}
#endif

bool its_write_stl_ascii(const char *file, const char *label, const std::vector<stl_triangle_vertex_indices> &indices, const std::vector<stl_vertex> &vertices)
{
    FILE *fp = boost::nowide::fopen(file, "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "its_write_stl_ascii: Couldn't open " << file << " for writing";
        return false;
    }

    fprintf(fp, "solid  %s\n", label);

    for (const stl_triangle_vertex_indices& face : indices) {
        Vec3f vertex[3] = { vertices[face(0)], vertices[face(1)], vertices[face(2)] };
        Vec3f normal    = (vertex[1] - vertex[0]).cross(vertex[2] - vertex[1]).normalized();
        fprintf(fp, "  facet normal % .8E % .8E % .8E\n", normal(0), normal(1), normal(2));
        fprintf(fp, "    outer loop\n");
        fprintf(fp, "      vertex % .8E % .8E % .8E\n", vertex[0](0), vertex[0](1), vertex[0](2));
        fprintf(fp, "      vertex % .8E % .8E % .8E\n", vertex[1](0), vertex[1](1), vertex[1](2));
        fprintf(fp, "      vertex % .8E % .8E % .8E\n", vertex[2](0), vertex[2](1), vertex[2](2));
        fprintf(fp, "    endloop\n");
        fprintf(fp, "  endfacet\n");
    }

    fprintf(fp, "endsolid  %s\n", label);
    fclose(fp);
    return true;
}

bool its_write_stl_binary(const char *file, const char *label, const std::vector<stl_triangle_vertex_indices> &indices, const std::vector<stl_vertex> &vertices)
{
    FILE *fp = boost::nowide::fopen(file, "wb");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "its_write_stl_binary: Couldn't open " << file << " for writing";
        return false;
    }

    {
        static constexpr const int header_size = 80;
        std::vector<char> header(header_size, 0);
        if (int header_len = std::min((label == nullptr) ? 0 : int(strlen(label)), header_size); header_len > 0)
            ::memcpy(header.data(), label, header_len);
        ::fwrite(header.data(), header_size, 1, fp);
    }

    uint32_t nfaces = indices.size();
    big_endian_reverse_quads(reinterpret_cast<char*>(&nfaces), 4);
    ::fwrite(&nfaces, 4, 1, fp);

    stl_facet f;
    f.extra[0] = 0;
    f.extra[1] = 0;
    for (const stl_triangle_vertex_indices& face : indices) {
        f.vertex[0] = vertices[face(0)];
        f.vertex[1] = vertices[face(1)];
        f.vertex[2] = vertices[face(2)];
        f.normal = (f.vertex[1] - f.vertex[0]).cross(f.vertex[2] - f.vertex[1]).normalized();
        big_endian_reverse_quads(reinterpret_cast<char*>(&f), 48);
        fwrite(&f, 50, 1, fp);
    }

    fclose(fp);
    return true;
}

}
