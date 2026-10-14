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
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
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

struct ColumnHash
{
    size_t operator()(const std::pair<uint64_t, uint64_t> &k) const { return size_t(k.first * 0x9E3779B97F4A7C15ull ^ (k.second + 0x632BE59BD9B4E019ull + (k.first << 6))); }
};

class IndexedMesh::AABBImpl {
private:
    AABBTreeIndirect::Tree3f m_tree;
    double                   m_triangle_ray_epsilon;

    struct Columns
    {
        static constexpr size_t Shards = 64;
        double base_z;
        double x0, y0, cell, margin;
        int    nx, ny;
        std::vector<uint32_t> offsets;
        std::vector<uint32_t> leaves;
        std::array<std::mutex, Shards> locks;
        std::array<std::unordered_map<std::pair<uint64_t, uint64_t>, std::shared_ptr<const std::vector<int>>, ColumnHash>, Shards> maps;
    };
    std::shared_ptr<Columns> m_columns;

    std::shared_ptr<const std::vector<int>> column(const indexed_triangle_set &its, double x, double y)
    {
        uint64_t kx, ky;
        std::memcpy(&kx, &x, sizeof x);
        std::memcpy(&ky, &y, sizeof y);
        const std::pair<uint64_t, uint64_t> key(kx, ky);
        const size_t shard = ColumnHash()(key) % Columns::Shards;
        {
            std::lock_guard<std::mutex> lock(m_columns->locks[shard]);
            if (auto it = m_columns->maps[shard].find(key); it != m_columns->maps[shard].end())
                return it->second;
        }
        auto faces = std::make_shared<std::vector<int>>();
        const Columns &c = *m_columns;
        const double gx = std::floor((x - c.x0) / c.cell), gy = std::floor((y - c.y0) / c.cell);
        if (gx >= -1. && gy >= -1. && gx <= double(c.nx) && gy <= double(c.ny)) {
            const size_t cell = size_t(std::clamp(int(gy), 0, c.ny - 1)) * size_t(c.nx) + size_t(std::clamp(int(gx), 0, c.nx - 1));
            const Vec3d origin(x, y, c.base_z), dir(Vec3d::UnitZ()), invdir(dir.cwiseInverse());
            for (uint32_t k = c.offsets[cell]; k < c.offsets[cell + 1]; ++k) {
                const auto &node = m_tree.node(c.leaves[k]);
                if (!AABBTreeIndirect::detail::ray_box_intersect_invdir(origin, invdir, node.bbox.cast<double>(), 0.,
                                                                        std::numeric_limits<double>::infinity()))
                    continue;
                const Vec3i32 &face = its.indices[node.idx];
                double t, u, v;
                if (AABBTreeIndirect::detail::intersect_triangle(origin, dir, its.vertices[face(0)], its.vertices[face(1)], its.vertices[face(2)],
                                                                 t, u, v, m_triangle_ray_epsilon) &&
                    t > 0.)
                    faces->push_back(int(node.idx));
            }
        }
        std::lock_guard<std::mutex> lock(m_columns->locks[shard]);
        return m_columns->maps[shard].emplace(key, std::move(faces)).first->second;
    }

    void column_hits(const indexed_triangle_set &its, const Vec3d &s, const Vec3d &dir, std::vector<igl::Hit<float>> &hits)
    {
        for (int id : *column(its, s.x(), s.y())) {
            const Vec3i32 &face = its.indices[size_t(id)];
            double t, u, v;
            if (AABBTreeIndirect::detail::intersect_triangle(s, dir, its.vertices[face(0)], its.vertices[face(1)], its.vertices[face(2)], t, u,
                                                             v, m_triangle_ray_epsilon) &&
                t > 0.)
                hits.push_back(igl::Hit<float>{ id, -1, float(u), float(v), float(t) });
        }
    }

public:
    bool columns_cover(const Vec3d &dir) const { return m_columns && dir.x() == 0. && dir.y() == 0. && std::abs(dir.z()) == 1.; }
    void enable_columns(const indexed_triangle_set &its)
    {
        if (m_columns) return;
        m_columns = std::make_shared<Columns>();
        float lo = std::numeric_limits<float>::max();
        for (const Vec3f &v : its.vertices) lo = std::min(lo, v.z());
        m_columns->base_z = double(lo) - 1.;
        build_column_grid(its);
    }

    void build_column_grid(const indexed_triangle_set &its)
    {
        Columns &c = *m_columns;
        std::vector<uint32_t> order;
        if (!m_tree.empty()) {
            std::vector<size_t> stack { 0 };
            while (!stack.empty()) {
                const size_t i = stack.back();
                stack.pop_back();
                if (m_tree.node(i).is_leaf())
                    order.push_back(uint32_t(i));
                else
                    stack.push_back(2 * i + 2), stack.push_back(2 * i + 1);
            }
        }
        BoundingBoxf box;
        for (const Vec3f &v : its.vertices) box.merge(Vec2d(v.x(), v.y()));
        const Vec2d size = box.defined ? Vec2d(box.size()) : Vec2d::Zero();
        const double area = std::max(size.x() * size.y(), 1e-6);
        c.cell   = std::max({ std::sqrt(area / (0.5 * std::max<size_t>(order.size(), 1))), std::sqrt(area / double(1 << 20)), 1e-3 });
        c.margin = 1e-4;
        c.x0 = box.defined ? box.min.x() : 0., c.y0 = box.defined ? box.min.y() : 0.;
        c.nx = std::max(1, int(std::ceil(size.x() / c.cell))), c.ny = std::max(1, int(std::ceil(size.y() / c.cell)));
        auto for_cells = [&](uint32_t leaf, auto &&visit) {
            const Vec3i32 &f = its.indices[m_tree.node(leaf).idx];
            const Vec2d p[3] = { its.vertices[f(0)].head<2>().cast<double>(), its.vertices[f(1)].head<2>().cast<double>(),
                                 its.vertices[f(2)].head<2>().cast<double>() };
            const Vec2d lo = p[0].cwiseMin(p[1]).cwiseMin(p[2]), hi = p[0].cwiseMax(p[1]).cwiseMax(p[2]);
            const int ix0 = std::clamp(int(std::floor((lo.x() - c.margin - c.x0) / c.cell)), 0, c.nx - 1),
                      ix1 = std::clamp(int(std::floor((hi.x() + c.margin - c.x0) / c.cell)), 0, c.nx - 1),
                      iy0 = std::clamp(int(std::floor((lo.y() - c.margin - c.y0) / c.cell)), 0, c.ny - 1),
                      iy1 = std::clamp(int(std::floor((hi.y() + c.margin - c.y0) / c.cell)), 0, c.ny - 1);
            for (int iy = iy0; iy <= iy1; ++iy)
                for (int ix = ix0; ix <= ix1; ++ix) {
                    const Vec2d centre(c.x0 + (ix + 0.5) * c.cell, c.y0 + (iy + 0.5) * c.cell);
                    bool apart = false;
                    for (int e = 0; e < 3 && !apart; ++e) {
                        const Vec2d &a = p[e], &b = p[(e + 1) % 3], &r = p[(e + 2) % 3];
                        const Vec2d n(a.y() - b.y(), b.x() - a.x());
                        const double reach = 0.5 * c.cell * (std::abs(n.x()) + std::abs(n.y())) + c.margin * n.norm();
                        const double edge = n.dot(a), other = n.dot(r), mid = n.dot(centre);
                        apart = mid - reach > std::max(edge, other) || mid + reach < std::min(edge, other);
                    }
                    if (!apart) visit(size_t(iy) * size_t(c.nx) + size_t(ix));
                }
        };
        c.offsets.assign(size_t(c.nx) * size_t(c.ny) + 1, 0);
        for (uint32_t leaf : order) for_cells(leaf, [&](size_t cell) { ++c.offsets[cell + 1]; });
        std::partial_sum(c.offsets.begin(), c.offsets.end(), c.offsets.begin());
        c.leaves.resize(c.offsets.back());
        std::vector<uint32_t> fill(c.offsets.begin(), c.offsets.end() - 1);
        for (uint32_t leaf : order) for_cells(leaf, [&](size_t cell) { c.leaves[fill[cell]++] = leaf; });
    }

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
        if (columns_cover(dir)) {
            std::vector<igl::Hit<float>> hits;
            column_hits(its, s, dir, hits);
            for (const igl::Hit<float> &h : hits)
                if (h.t < hit.t) hit = h;
            return;
        }
        AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices,
                                                  m_tree, s, dir, hit, m_triangle_ray_epsilon);
    }

    void intersect_ray(const indexed_triangle_set &its,
                       const Vec3d &               s,
                       const Vec3d &               dir,
                       std::vector<igl::Hit<float>> &     hits)
    {
        if (columns_cover(dir)) {
            column_hits(its, s, dir, hits);
            std::sort(hits.begin(), hits.end(), [](const auto &l, const auto &r) { return l.t < r.t; });
            return;
        }
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

void IndexedMesh::enable_column_cache() { m_aabb->enable_columns(*m_tm); }

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
