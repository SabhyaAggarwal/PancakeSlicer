#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

#include "libslic3r.h"
#include <admesh/stl.h>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <array>
#include <cereal/specialize.hpp>
#include <functional>
#include <utility>
#include <vector>
#include "BoundingBox.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "Format/STL.hpp"
namespace Slic3r {

class TriangleMesh;
class TriangleMeshSlicer;
struct Groove;
struct RepairedMeshErrors {
    int           edges_fixed               = 0;
    int           degenerate_facets         = 0;
    int           facets_removed            = 0;
    int           facets_reversed           = 0;
    int           backwards_edges           = 0;

    void clear() { *this = RepairedMeshErrors(); }

    void merge(const RepairedMeshErrors& rhs) {
        this->edges_fixed         += rhs.edges_fixed;
        this->degenerate_facets   += rhs.degenerate_facets;
        this->facets_removed      += rhs.facets_removed;
        this->facets_reversed     += rhs.facets_reversed;
        this->backwards_edges     += rhs.backwards_edges;
    }

    bool repaired() const { return degenerate_facets > 0 || edges_fixed > 0 || facets_removed > 0 || facets_reversed > 0 || backwards_edges > 0; }
};

struct TriangleMeshStats {
    uint32_t      number_of_facets          = 0;
    stl_vertex    max                       = stl_vertex::Zero();
    stl_vertex    min                       = stl_vertex::Zero();
    stl_vertex    size                      = stl_vertex::Zero();
    float         volume                    = -1.f;
    int           number_of_parts           = 0;

    int           open_edges                = 0;

    RepairedMeshErrors repaired_errors;

    void clear() { *this = TriangleMeshStats(); }

    TriangleMeshStats merge(const TriangleMeshStats &rhs) const {
      if (this->number_of_facets == 0)
        return rhs;
      else if (rhs.number_of_facets == 0)
        return *this;
      else {
        TriangleMeshStats out;
        out.number_of_facets        = this->number_of_facets + rhs.number_of_facets;
        out.min                     = this->min.cwiseMin(rhs.min);
        out.max                     = this->max.cwiseMax(rhs.max);
        out.size                    = out.max - out.min;
        out.number_of_parts         = this->number_of_parts     + rhs.number_of_parts;
        out.open_edges              = this->open_edges          + rhs.open_edges;
        out.volume                  = this->volume              + rhs.volume;
        out.repaired_errors.merge(rhs.repaired_errors);
        return out;
      }
    }

    bool manifold() const { return open_edges == 0; }
    bool repaired() const { return repaired_errors.repaired(); }
};

class TriangleMesh
{
public:
    TriangleMesh() = default;
    TriangleMesh(const std::vector<Vec3f> &vertices, const std::vector<Vec3i32> &faces);
    TriangleMesh(std::vector<Vec3f> &&vertices, const std::vector<Vec3i32> &&faces);
    explicit TriangleMesh(const indexed_triangle_set &M);
    explicit TriangleMesh(indexed_triangle_set &&M, const RepairedMeshErrors& repaired_errors = RepairedMeshErrors());
    void clear() { this->its.clear(); this->m_stats.clear(); }
    bool from_stl(stl_file& stl, bool repair = true);
    bool  ReadSTLFile(const char *input_file, bool repair = true, ImportstlProgressFn stlFn = nullptr, int custom_header_length = 80);
    bool write_ascii(const char* output_file) const;
    bool write_binary(const char* output_file) const;
    float volume();
    void WriteOBJFile(const char* output_file) const;
    void scale(float factor);
    void scale(const Vec3f &versor);
    void translate(float x, float y, float z);
    void translate(const Vec3f &displacement);
    void rotate(float angle, const Axis &axis);
    void rotate(float angle, const Vec3d& axis);
    void rotate_x(float angle) { this->rotate(angle, X); }
    void rotate_y(float angle) { this->rotate(angle, Y); }
    void rotate_z(float angle) { this->rotate(angle, Z); }
    void mirror(const Axis axis);
    void mirror_x() { this->mirror(X); }
    void mirror_y() { this->mirror(Y); }
    void mirror_z() { this->mirror(Z); }
    void transform(const Transform3d& t, bool fix_left_handed = false);
    void transform(const Matrix3d& t, bool fix_left_handed = false);
    void flip_triangles();
    void align_to_origin();
    void rotate(double angle, Point* center);
    std::vector<TriangleMesh> split() const;
    void merge(const TriangleMesh &mesh);
    ExPolygons horizontal_projection() const;
    Polygon convex_hull() const;
    BoundingBoxf3 bounding_box() const;
    BoundingBoxf3 transformed_bounding_box(const Transform3d &trafo) const;
    BoundingBoxf3 transformed_bounding_box(const Transform3d& trafo, double world_min_z) const;
    Vec3d size() const { return m_stats.size.cast<double>(); }
    Vec3d center() const { return this->bounding_box().center(); }
    TriangleMesh convex_hull_3d() const;
    std::vector<ExPolygons> slice(const std::vector<double>& z) const;
    size_t facets_count() const { assert(m_stats.number_of_facets == this->its.indices.size()); return m_stats.number_of_facets; }
    bool   empty() const { return this->facets_count() == 0; }
    bool   repaired() const;
    bool   is_splittable() const;
    size_t memsize() const;

    size_t release_optional() { return 0; }
    void   restore_optional() {}

    const TriangleMeshStats& stats() const { return m_stats; }

    void set_init_shift(const Vec3d &offset) { m_init_shift = offset; }
    Vec3d get_init_shift() const { return m_init_shift; }

    indexed_triangle_set its;

private:
    TriangleMeshStats m_stats;
    Vec3d m_init_shift {0.0, 0.0, 0.0};
};

struct VertexFaceIndex
{
public:
    using iterator = std::vector<size_t>::const_iterator;

    VertexFaceIndex(const indexed_triangle_set &its) { this->create(its); }
    VertexFaceIndex() {}

    void create(const indexed_triangle_set &its);
    void clear() { m_vertex_to_face_start.clear(); m_vertex_faces_all.clear(); }

    iterator begin(size_t vertex_id) const throw() { return m_vertex_faces_all.begin() + m_vertex_to_face_start[vertex_id]; }
    iterator end  (size_t vertex_id) const throw() { return m_vertex_faces_all.begin() + m_vertex_to_face_start[vertex_id + 1]; }
    size_t   count(size_t vertex_id) const throw() { return m_vertex_to_face_start[vertex_id + 1] - m_vertex_to_face_start[vertex_id]; }

    const Range<iterator> operator[](size_t vertex_id) const { return {begin(vertex_id), end(vertex_id)}; }

private:
    std::vector<size_t>     m_vertex_to_face_start;
    std::vector<size_t>     m_vertex_faces_all;
};

std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its);
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its, std::function<void()> throw_on_cancel_callback);
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its, const std::vector<bool> &face_mask);
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set &its, std::vector<Vec3i32> &face_neighbors, bool assign_unbound_edges = false, int *num_edges = nullptr);

std::vector<Vec3i32> its_face_neighbors(const indexed_triangle_set &its);
std::vector<Vec3i32> its_face_neighbors_par(const indexed_triangle_set &its);

void its_flip_triangles(indexed_triangle_set &its);

int its_merge_vertices(indexed_triangle_set &its, bool shrink_to_fit = true);

int its_remove_degenerate_faces(indexed_triangle_set &its, bool shrink_to_fit = true);

int its_compactify_vertices(indexed_triangle_set &its, bool shrink_to_fit = true);

bool its_store_triangle(const indexed_triangle_set &its, const char *obj_filename, size_t triangle_index);
bool its_store_triangles(const indexed_triangle_set &its, const char *obj_filename, const std::vector<size_t>& triangles);

std::vector<indexed_triangle_set> its_split(const indexed_triangle_set &its);
std::vector<indexed_triangle_set> its_split(const indexed_triangle_set &its, std::vector<Vec3i32> &face_neighbors);

size_t its_number_of_patches(const indexed_triangle_set &its);
size_t its_number_of_patches(const indexed_triangle_set &its, const std::vector<Vec3i32> &face_neighbors);
bool its_is_splittable(const indexed_triangle_set &its);
bool its_is_splittable(const indexed_triangle_set &its, const std::vector<Vec3i32> &face_neighbors);

size_t its_num_open_edges(const indexed_triangle_set &its);
size_t its_num_open_edges(const std::vector<Vec3i32> &face_neighbors);

void its_shrink_to_fit(indexed_triangle_set &its);

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Matrix3f &m, const float z, Points &all_pts);
void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Transform3f &t, const float z, Points &all_pts);

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Matrix3f &m, const float z);
Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Transform3f &t, const float z);

inline int its_triangle_vertex_index(const stl_triangle_vertex_indices &triangle_indices, int vertex_idx)
{
    return vertex_idx == triangle_indices[0] ? 0 :
           vertex_idx == triangle_indices[1] ? 1 :
           vertex_idx == triangle_indices[2] ? 2 : -1;
}

inline Vec2i32 its_triangle_edge(const stl_triangle_vertex_indices &triangle_indices, int edge_idx)
{
    int next_edge_idx = (edge_idx == 2) ? 0 : edge_idx + 1;
    return { triangle_indices[edge_idx], triangle_indices[next_edge_idx] };
}

inline int its_triangle_edge_index(const stl_triangle_vertex_indices &triangle_indices, const Vec2i32 &triangle_edge)
{
    return triangle_edge(0) == triangle_indices[0] && triangle_edge(1) == triangle_indices[1] ? 0 :
           triangle_edge(0) == triangle_indices[1] && triangle_edge(1) == triangle_indices[2] ? 1 :
           triangle_edge(0) == triangle_indices[2] && triangle_edge(1) == triangle_indices[0] ? 2 : -1;
}

inline bool its_triangle_vertex_the_same(const stl_triangle_vertex_indices &triangle_indices_1, const stl_triangle_vertex_indices &triangle_indices_2)
{
    bool ret = false;
    if (triangle_indices_1[0] == triangle_indices_2[0])
    {
        if ((triangle_indices_1[1] ==  triangle_indices_2[1])
            && (triangle_indices_1[2] ==  triangle_indices_2[2]))
            ret = true;
        else if ((triangle_indices_1[1] ==  triangle_indices_2[2])
            && (triangle_indices_1[2] ==  triangle_indices_2[1]))
            ret = true;
    }
    else if (triangle_indices_1[0] == triangle_indices_2[1])
    {
        if ((triangle_indices_1[1] ==  triangle_indices_2[0])
            && (triangle_indices_1[2] ==  triangle_indices_2[2]))
            ret = true;
        else if ((triangle_indices_1[1] ==  triangle_indices_2[2])
            && (triangle_indices_1[2] ==  triangle_indices_2[0]))
            ret = true;
    }
    else if (triangle_indices_1[0] == triangle_indices_2[2])
    {
        if ((triangle_indices_1[1] ==  triangle_indices_2[0])
            && (triangle_indices_1[2] ==  triangle_indices_2[1]))
            ret = true;
        else if ((triangle_indices_1[1] ==  triangle_indices_2[1])
            && (triangle_indices_1[2] ==  triangle_indices_2[0]))
            ret = true;
    }

    return ret;
}


using its_triangle = std::array<stl_vertex, 3>;

inline its_triangle its_triangle_vertices(const indexed_triangle_set &its,
                                          size_t                      face_id)
{
    return {its.vertices[its.indices[face_id](0)],
            its.vertices[its.indices[face_id](1)],
            its.vertices[its.indices[face_id](2)]};
}

inline stl_normal its_unnormalized_normal(const indexed_triangle_set &its,
                                          size_t                      face_id)
{
    its_triangle tri = its_triangle_vertices(its, face_id);
    return (tri[1] - tri[0]).cross(tri[2] - tri[0]);
}

float its_volume(const indexed_triangle_set &its);
struct MassProperties
{
    double   mass{ 0. };
    double   volume{ 0. };
    Vec3d    center{ Vec3d::Zero() };
    Matrix3d spread{ Matrix3d::Zero() };

    MassProperties transformed(const Transform3d &trafo) const;
};
MassProperties its_mass_properties(const indexed_triangle_set &its);
float its_average_edge_length(const indexed_triangle_set &its);

void its_merge(indexed_triangle_set &A, const indexed_triangle_set &B);
void its_merge(indexed_triangle_set &A, const std::vector<Vec3f> &triangles);
void its_merge(indexed_triangle_set &A, const Pointf3s &triangles);

std::vector<Vec3f> its_face_normals(const indexed_triangle_set &its);
inline Vec3f face_normal(const stl_vertex vertex[3]) { return  (vertex[1] - vertex[0]).cross(vertex[2] - vertex[1]).normalized(); }
inline Vec3f face_normal_normalized(const stl_vertex vertex[3]) { return  face_normal(vertex).normalized(); }
inline Vec3f its_face_normal(const indexed_triangle_set &its, const stl_triangle_vertex_indices face)
    { const stl_vertex vertices[3] { its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]] }; return face_normal_normalized(vertices); }
inline Vec3f its_face_normal(const indexed_triangle_set &its, const int face_idx)
    { return its_face_normal(its, its.indices[face_idx]); }

indexed_triangle_set    its_make_cube(double x, double y, double z);
indexed_triangle_set    its_make_prism(float width, float length, float height);
indexed_triangle_set    its_make_cylinder(double r, double h, double fa=(2*PI/180));
indexed_triangle_set    its_make_cone(double r, double h, double fa=(2*PI/180));
indexed_triangle_set    its_make_frustum(double r, double h, double fa=(2*PI/180));
indexed_triangle_set    its_make_torus(double r, double h, double fa);
indexed_triangle_set    its_make_frustum_dowel(double r, double h, int sectorCount);
indexed_triangle_set    its_make_pyramid(float base, float height);
indexed_triangle_set    its_make_sphere(double radius, double fa);
indexed_triangle_set    its_make_snap(double r, double h, float space_proportion = 0.25f, float bulge_proportion = 0.125f);
indexed_triangle_set    its_make_groove_plane(const Groove &cur_groove, float rotate_radius, std::vector<Vec3d> &cur_groove_vertices);

indexed_triangle_set        its_convex_hull(const std::vector<Vec3f> &pts);
inline indexed_triangle_set its_convex_hull(const indexed_triangle_set &its) { return its_convex_hull(its.vertices); }

inline TriangleMesh     make_cube(double x, double y, double z)                 { return TriangleMesh(its_make_cube(x, y, z)); }
inline TriangleMesh     make_prism(float width, float length, float height)     { return TriangleMesh(its_make_prism(width, length, height)); }
inline TriangleMesh     make_cylinder(double r, double h, double fa=(2*PI/180)) { return TriangleMesh{its_make_cylinder(r, h, fa)}; }
inline TriangleMesh     make_cone(double r, double h, double fa=(2*PI/180))     { return TriangleMesh(its_make_cone(r, h, fa)); }
inline TriangleMesh     make_pyramid(float base, float height)                  { return TriangleMesh(its_make_pyramid(base, height)); }
inline TriangleMesh     make_sphere(double rho, double fa=(2*PI/90))            { return TriangleMesh(its_make_sphere(rho, fa)); }
inline TriangleMesh     make_torus(double r, double h, double fa=(PI/60))       { return TriangleMesh(its_make_torus(r, h, fa)); }

bool        its_write_stl_ascii(const char *file, const char *label, const std::vector<stl_triangle_vertex_indices> &indices, const std::vector<stl_vertex> &vertices);
inline bool its_write_stl_ascii(const char *file, const char *label, const indexed_triangle_set &its) { return its_write_stl_ascii(file, label, its.indices, its.vertices); }
bool        its_write_stl_binary(const char *file, const char *label, const std::vector<stl_triangle_vertex_indices> &indices, const std::vector<stl_vertex> &vertices);
inline bool its_write_stl_binary(const char *file, const char *label, const indexed_triangle_set &its) { return its_write_stl_binary(file, label, its.indices, its.vertices); }

inline BoundingBoxf3 bounding_box(const TriangleMesh &m) { return m.bounding_box(); }
inline BoundingBoxf3 bounding_box(const indexed_triangle_set& its)
{
    if (its.vertices.empty())
        return {};

    Vec3f bmin = its.vertices.front(), bmax = its.vertices.front();

    for (const Vec3f &p : its.vertices) {
        bmin = p.cwiseMin(bmin);
        bmax = p.cwiseMax(bmax);
    }

    return {bmin.cast<double>(), bmax.cast<double>()};
}

}

#include <cereal/access.hpp>
namespace cereal {
    template <class Archive> struct specialize<Archive, Slic3r::TriangleMesh, cereal::specialization::non_member_load_save> {};
    template<class Archive> void load(Archive &archive, Slic3r::TriangleMesh &mesh) {
        archive.loadBinary(reinterpret_cast<char*>(const_cast<Slic3r::TriangleMeshStats*>(&mesh.stats())), sizeof(Slic3r::TriangleMeshStats));
        archive(mesh.its.indices, mesh.its.vertices);
    }
    template<class Archive> void save(Archive &archive, const Slic3r::TriangleMesh &mesh) {
        archive.saveBinary(reinterpret_cast<const char*>(&mesh.stats()), sizeof(Slic3r::TriangleMeshStats));
        archive(mesh.its.indices, mesh.its.vertices);
    }
}

#endif
