#ifndef PRUSASLICER_AABBMESH_H
#define PRUSASLICER_AABBMESH_H

#include <cstddef>
#include <limits>
#include <cmath>
#include <cassert>
#include <memory>
#include <vector>

#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>

#ifdef SLIC3R_HOLE_RAYCASTER
  #include "libslic3r/SLA/Hollowing.hpp"
#endif

struct indexed_triangle_set;

namespace Slic3r {

class TriangleMesh;

class AABBMesh {
    class AABBImpl;

    const indexed_triangle_set* m_tm;

    std::unique_ptr<AABBImpl> m_aabb;
    VertexFaceIndex m_vfidx;
    std::vector<Vec3i32> m_fnidx;

#ifdef SLIC3R_HOLE_RAYCASTER
    std::vector<sla::DrainHole> m_holes;
#endif

    template<class M> void init(const M &mesh, bool calculate_epsilon);

public:

    explicit AABBMesh(const indexed_triangle_set &tmesh, bool calculate_epsilon = false);
    explicit AABBMesh(const TriangleMesh &mesh, bool calculate_epsilon = false);
    
    AABBMesh(const AABBMesh& other);
    AABBMesh& operator=(const AABBMesh&);

    AABBMesh(AABBMesh &&other);
    AABBMesh& operator=(AABBMesh &&other);

    ~AABBMesh();

    const std::vector<Vec3f>& vertices() const;
    const std::vector<Vec3i32>& indices()  const;
    const Vec3f& vertices(size_t idx) const;
    const Vec3i32& indices(size_t idx) const;

    class hit_result {
        double m_t = infty();
        int m_face_id = -1;
        const AABBMesh *m_mesh = nullptr;
        Vec3d m_dir    = Vec3d::Zero();
        Vec3d m_source = Vec3d::Zero();
        Vec3d m_normal = Vec3d::Zero();
        friend class AABBMesh;
        
        explicit inline hit_result(const AABBMesh& em): m_mesh(&em) {}
    public:
        static inline constexpr double infty() { return std::numeric_limits<double>::infinity(); }
        
        explicit inline hit_result(double val = infty()) : m_t(val) {}
        
        inline double distance() const { return m_t; }
        inline const Vec3d& direction() const { return m_dir; }
        inline const Vec3d& source() const { return m_source; }
        inline Vec3d position() const { return m_source + m_dir * m_t; }
        inline int face() const { return m_face_id; }
        inline bool is_valid() const { return m_mesh != nullptr; }
        inline bool is_hit() const { return m_face_id >= 0 && !std::isinf(m_t); }

        inline const Vec3d& normal() const {
            assert(is_valid());
            return m_normal;
        }

        inline bool is_inside() const {
            return is_hit() && normal().dot(m_dir) > 0;
        }
    };

#ifdef SLIC3R_HOLE_RAYCASTER
    void load_holes(const std::vector<sla::DrainHole>& holes) {
        m_holes = holes;
    }

    hit_result filter_hits(const std::vector<AABBMesh::hit_result>& obj_hits) const;
#endif

    hit_result query_ray_hit(const Vec3d &s, const Vec3d &dir) const;
    
    std::vector<hit_result> query_ray_hits(const Vec3d &s, const Vec3d &dir) const;

    double squared_distance(const Vec3d& p, int& i, Vec3d& c) const;
    inline double squared_distance(const Vec3d &p) const
    {
        int   i;
        Vec3d c;
        return squared_distance(p, i, c);
    }

    Vec3d normal_by_face_id(int face_id) const;

    const indexed_triangle_set * get_triangle_mesh() const { return m_tm; }

    const VertexFaceIndex &vertex_face_index() const { return m_vfidx; }
    const std::vector<Vec3i32> &face_neighbor_index() const { return m_fnidx; }
};


}

#endif
