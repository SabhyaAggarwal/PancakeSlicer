#include "IndexedMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "Concurrency.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include <igl/Hit.h>
#include <Eigen/Core>
#include <cstddef>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <Eigen/Geometry>
#include <cstdlib>
#include <functional>
#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include <limits>
#include <numeric>
#include <vector>

#ifdef SLIC3R_HOLE_RAYCASTER
#include <libslic3r/SLA/Hollowing.hpp>
#endif

namespace Slic3r {

namespace sla {

class IndexedMesh::AABBImpl {
private:
    AABBTreeIndirect::Tree3f m_tree;
    double                   m_triangle_ray_epsilon;

public:
    void init(const indexed_triangle_set &its, bool calculate_epsilon)
    {
        m_triangle_ray_epsilon = 0.000001;
        if (calculate_epsilon) {
            double l = its_average_edge_length(its);
            if (l > 0)
                m_triangle_ray_epsilon = 0.000001 * l * l;
        }
        m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
            its.vertices, its.indices);
    }

    void intersect_ray(const indexed_triangle_set &its,
                       const Vec3d &               s,
                       const Vec3d &               dir,
                       igl::Hit<float> &                  hit)
    {
        AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices,
                                                  m_tree, s, dir, hit, m_triangle_ray_epsilon);
    }

    void intersect_ray(const indexed_triangle_set &its,
                       const Vec3d &               s,
                       const Vec3d &               dir,
                       std::vector<igl::Hit<float>> &     hits)
    {
        AABBTreeIndirect::intersect_ray_all_hits(its.vertices, its.indices,
                                                 m_tree, s, dir, hits, m_triangle_ray_epsilon);
    }

    double squared_distance(const indexed_triangle_set & its,
                            const Vec3d &                point,
                            int &                        i,
                            Eigen::Matrix<double, 1, 3> &closest)
    {
        size_t idx_unsigned { 0 };
        Vec3d  closest_vec3d { Vec3d::Zero() };
        const double dist {
            AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
                its.vertices, its.indices, m_tree, point, idx_unsigned,
                closest_vec3d) };
        i       = int(idx_unsigned);
        closest = closest_vec3d;
        return dist;
    }
};

template<class M> void IndexedMesh::init(const M &mesh, bool calculate_epsilon)
{
    BoundingBoxf3 bb = bounding_box(mesh);
    m_ground_level += bb.min(Z);

    m_aabb->init(*m_tm, calculate_epsilon);
}

IndexedMesh::IndexedMesh(const indexed_triangle_set& tmesh, bool calculate_epsilon)
    : m_aabb(new AABBImpl()), m_tm(&tmesh)
{
    init(tmesh, calculate_epsilon);
}

IndexedMesh::IndexedMesh(const TriangleMesh &mesh, bool calculate_epsilon)
    : m_aabb(new AABBImpl()), m_tm(&mesh.its)
{
    init(mesh, calculate_epsilon);
}

IndexedMesh::~IndexedMesh() {}

IndexedMesh::IndexedMesh(const IndexedMesh &other):
    m_tm(other.m_tm), m_ground_level(other.m_ground_level),
    m_aabb( new AABBImpl(*other.m_aabb) ) {}


IndexedMesh &IndexedMesh::operator=(const IndexedMesh &other)
{
    m_tm = other.m_tm;
    m_ground_level = other.m_ground_level;
    m_aabb.reset(new AABBImpl(*other.m_aabb)); return *this;
}

IndexedMesh &IndexedMesh::operator=(IndexedMesh &&other) = default;

IndexedMesh::IndexedMesh(IndexedMesh &&other) = default;



const std::vector<Vec3f>& IndexedMesh::vertices() const
{
    return m_tm->vertices;
}



const std::vector<Vec3i32>& IndexedMesh::indices()  const
{
    return m_tm->indices;
}



const Vec3f& IndexedMesh::vertices(size_t idx) const
{
    return m_tm->vertices[idx];
}



const Vec3i32& IndexedMesh::indices(size_t idx) const
{
    return m_tm->indices[idx];
}


Vec3d IndexedMesh::normal_by_face_id(int face_id) const {

    return its_unnormalized_normal(*m_tm, face_id).cast<double>().normalized();
}


IndexedMesh::hit_result
IndexedMesh::query_ray_hit(const Vec3d &s, const Vec3d &dir) const
{
    assert(is_approx(dir.norm(), 1.));
    igl::Hit<float> hit{-1, -1, 0.f, 0.f, 0.f};
    hit.t = std::numeric_limits<float>::infinity();

#ifdef SLIC3R_HOLE_RAYCASTER
    if (! m_holes.empty()) {

        return filter_hits(query_ray_hits(s, dir));
    }
#endif

    m_aabb->intersect_ray(*m_tm, s, dir, hit);
    hit_result ret(*this);
    ret.m_t = double(hit.t);
    ret.m_dir = dir;
    ret.m_source = s;
    if(!std::isinf(hit.t) && !std::isnan(hit.t)) {
        ret.m_normal = this->normal_by_face_id(hit.id);
        ret.m_face_id = hit.id;
    }

    return ret;
}

std::vector<IndexedMesh::hit_result>
IndexedMesh::query_ray_hits(const Vec3d &s, const Vec3d &dir) const
{
    std::vector<IndexedMesh::hit_result> outs;
    std::vector<igl::Hit<float>> hits;
    m_aabb->intersect_ray(*m_tm, s, dir, hits);

    std::sort(hits.begin(), hits.end(),
              [](const igl::Hit<float>& a, const igl::Hit<float>& b) { return a.t < b.t; });

    outs.reserve(hits.size());
    for (const igl::Hit<float>& hit : hits) {
        outs.emplace_back(IndexedMesh::hit_result(*this));
        outs.back().m_t = double(hit.t);
        outs.back().m_dir = dir;
        outs.back().m_source = s;
        if(!std::isinf(hit.t) && !std::isnan(hit.t)) {
            outs.back().m_normal = this->normal_by_face_id(hit.id);
            outs.back().m_face_id = hit.id;
        }
    }

    return outs;
}


#ifdef SLIC3R_HOLE_RAYCASTER
IndexedMesh::hit_result IndexedMesh::filter_hits(
    const std::vector<IndexedMesh::hit_result>& object_hits) const
{
    assert(! m_holes.empty());
    hit_result out(*this);

    if (object_hits.empty())
        return out;

    const Vec3d& s = object_hits.front().source();
    const Vec3d& dir = object_hits.front().direction();

    struct HoleHit {
        HoleHit(float t_p, const Vec3d& normal_p, bool entry_p) :
            t(t_p), normal(normal_p), entry(entry_p) {}
        float t;
        Vec3d normal;
        bool entry;
    };
    std::vector<HoleHit> hole_isects;
    hole_isects.reserve(m_holes.size());

    auto sf = s.cast<float>();
    auto dirf = dir.cast<float>();

    for (const sla::DrainHole& hole : m_holes) {
        std::array<std::pair<float, Vec3d>, 2> isects;
        if (hole.get_intersections(sf, dirf, isects)) {
            if (isects[0].first > 0.f) hole_isects.emplace_back(isects[0].first, isects[0].second, true);
            if (isects[1].first > 0.f) hole_isects.emplace_back(isects[1].first, isects[1].second, false);
        }
    }

    std::sort(hole_isects.begin(), hole_isects.end(),
              [](const HoleHit& a, const HoleHit& b) { return a.t < b.t; });

    int hole_nested = 0;
    int object_nested = 0;
    for (int dry_run=1; dry_run>=0; --dry_run) {
        hole_nested = -hole_nested;
        object_nested = -object_nested;

        bool is_hole = false;
        bool is_entry = false;
        const HoleHit* next_hole_hit = hole_isects.empty() ? nullptr : &hole_isects.front();
        const hit_result* next_mesh_hit = &object_hits.front();

        while (next_hole_hit || next_mesh_hit) {
            if (next_hole_hit && next_mesh_hit)
                is_hole = (next_hole_hit->t < next_mesh_hit->m_t);
            else
                is_hole = next_hole_hit;
            is_entry = is_hole ? next_hole_hit->entry : ! next_mesh_hit->is_inside();

            if (! dry_run) {
                if (! is_hole && hole_nested == 0) {
                    return *next_mesh_hit;
                }
                if (is_hole && ! is_entry && object_nested != 0) {
                    out.m_t = next_hole_hit->t;
                    out.m_normal = next_hole_hit->normal;
                    out.m_source = s;
                    out.m_dir = dir;
                    return out;
                }
            }

            (is_hole ? hole_nested : object_nested) += (is_entry ? 1 : -1);

            if (is_hole && next_hole_hit++ == &hole_isects.back())
                next_hole_hit = nullptr;
            if (! is_hole && next_mesh_hit++ == &object_hits.back())
                next_mesh_hit = nullptr;
        }
    }

    return out;
}
#endif


double IndexedMesh::squared_distance(const Vec3d &p, int& i, Vec3d& c) const {
    double sqdst = 0;
    Eigen::Matrix<double, 1, 3> pp = p;
    Eigen::Matrix<double, 1, 3> cc;
    sqdst = m_aabb->squared_distance(*m_tm, pp, i, cc);
    c = cc;
    return sqdst;
}


static bool point_on_edge(const Vec3d& p, const Vec3d& e1, const Vec3d& e2,
                          double eps = 0.05)
{
    using Line3D = Eigen::ParametrizedLine<double, 3>;

    auto line = Line3D::Through(e1, e2);
    double d = line.distance(p);
    return std::abs(d) < eps;
}

PointSet normals(const PointSet& points,
                 const IndexedMesh& mesh,
                 double eps,
                 std::function<void()> thr,
                 const std::vector<unsigned>& pt_indices)
{
    if (points.rows() == 0 || mesh.vertices().empty() || mesh.indices().empty())
        return {};

    std::vector<unsigned> range = pt_indices;
    if (range.empty()) {
        range.resize(size_t(points.rows()), 0);
        std::iota(range.begin(), range.end(), 0);
    }

    PointSet ret(range.size(), 3);

    ccr::for_each(size_t(0), range.size(),
        [&ret, &mesh, &points, thr, eps, &range](size_t ridx) {
            thr();
            unsigned el = range[ridx];
            auto  eidx   = Eigen::Index(el);
            int   faceid = 0;
            Vec3d p;

            mesh.squared_distance(points.row(eidx), faceid, p);

            auto trindex = mesh.indices(faceid);

            const Vec3d &p1 = mesh.vertices(trindex(0)).cast<double>();
            const Vec3d &p2 = mesh.vertices(trindex(1)).cast<double>();
            const Vec3d &p3 = mesh.vertices(trindex(2)).cast<double>();

            int ia = -1, ib = -1, ic = -1;

            if (std::abs((p - p1).norm()) < eps) {
                ic = trindex(0);
            } else if (std::abs((p - p2).norm()) < eps) {
                ic = trindex(1);
            } else if (std::abs((p - p3).norm()) < eps) {
                ic = trindex(2);
            } else if (point_on_edge(p, p1, p2, eps)) {
                ia = trindex(0);
                ib = trindex(1);
            } else if (point_on_edge(p, p2, p3, eps)) {
                ia = trindex(1);
                ib = trindex(2);
            } else if (point_on_edge(p, p1, p3, eps)) {
                ia = trindex(0);
                ib = trindex(2);
            }

            std::vector<size_t> neigh;
            if (ic >= 0) {
                for (size_t n = 0; n < mesh.indices().size(); ++n) {
                    thr();
                    Vec3i32 ni = mesh.indices(n);
                    if ((ni(X) == ic || ni(Y) == ic || ni(Z) == ic))
                        neigh.emplace_back(n);
                }
            } else if (ia >= 0 && ib >= 0) {
                for (size_t n = 0; n < mesh.indices().size(); ++n) {
                    thr();
                    Vec3i32 ni = mesh.indices(n);
                    if ((ni(X) == ia || ni(Y) == ia || ni(Z) == ia) &&
                        (ni(X) == ib || ni(Y) == ib || ni(Z) == ib))
                        neigh.emplace_back(n);
                }
            }

            std::vector<Vec3d> neighnorms;
            neighnorms.reserve(neigh.size());
            for (size_t &tri_id : neigh)
                neighnorms.emplace_back(mesh.normal_by_face_id(tri_id));

            std::sort(neighnorms.begin(), neighnorms.end(),
                      [](const Vec3d &v1, const Vec3d &v2) {
                          return v1.sum() < v2.sum();
                      });

            auto lend = std::unique(neighnorms.begin(), neighnorms.end(),
                                    [](const Vec3d &n1, const Vec3d &n2) {
                                        auto deq = [](double a, double b) {
                                            return std::abs(a - b) < 1e-3;
                                        };
                                        return deq(n1(X), n2(X)) &&
                                               deq(n1(Y), n2(Y)) &&
                                               deq(n1(Z), n2(Z));
                                    });

            if (!neighnorms.empty()) {
                Vec3d sumnorm(0, 0, 0);
                sumnorm = std::accumulate(neighnorms.begin(), lend, sumnorm);
                sumnorm.normalize();
                ret.row(long(ridx)) = sumnorm;
            } else {
                Eigen::Vector3d U   = p2 - p1;
                Eigen::Vector3d V   = p3 - p1;
                ret.row(long(ridx)) = U.cross(V).normalized();
            }
        });

    return ret;
}

}}
