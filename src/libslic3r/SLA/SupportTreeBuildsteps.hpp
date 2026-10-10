#ifndef SLASUPPORTTREEALGORITHM_H
#define SLASUPPORTTREEALGORITHM_H

#include "libslic3r/Point.hpp"
#include <cmath>
#include <array>
#include <cstddef>
#include "libslic3r/libslic3r.h"
#include "libslic3r/SLA/Concurrency.hpp"
#include <climits>
#include <cassert>
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <map>
#include <optional>

#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>
#include <utility>
#include <tuple>
#include <vector>
#include "libslic3r/MTUtils.hpp"

namespace Slic3r::sla { struct SupportPoint; }

namespace Slic3r {
namespace sla {

const double  D_SP = 0.1;

enum {
    X, Y, Z
};

inline Vec2d to_vec2(const Vec3d &v3) { return {v3(X), v3(Y)}; }

inline std::pair<double, double> dir_to_spheric(const Vec3d &n, double norm = 1.)
{
    double z       = n.z();
    double r       = norm;
    double polar   = std::acos(z / r);
    double azimuth = std::atan2(n(1), n(0));
    return {polar, azimuth};
}

inline Vec3d spheric_to_dir(double polar, double azimuth)
{
    return {std::cos(azimuth) * std::sin(polar),
            std::sin(azimuth) * std::sin(polar), std::cos(polar)};
}

inline Vec3d spheric_to_dir(const std::tuple<double, double> &v)
{
    auto [plr, azm] = v;
    return spheric_to_dir(plr, azm);
}

inline Vec3d spheric_to_dir(const std::pair<double, double> &v)
{
    return spheric_to_dir(v.first, v.second);
}

inline Vec3d spheric_to_dir(const std::array<double, 2> &v)
{
    return spheric_to_dir(v[0], v[1]);
}

template<size_t N>
class PointRing {
    std::array<double, N> m_phis;

    Vec3d a = {0, 1, 0}, b;
    double m_radius = 0.;

    static inline bool constexpr is_one(double val)
    {
        return std::abs(std::abs(val) - 1) < 1e-20;
    }

public:

    PointRing(const Vec3d &n)
    {
        m_phis = linspace_array<N>(0., 2 * PI);

        if(is_one(n(X)) || is_one(n(Y)) || is_one(n(Z))) {
            a = {n(Z), n(X), n(Y)};
            b = {n(Y), n(Z), n(X)};
        }
        else {
            a(Z) = -(n(Y)*a(Y)) / n(Z); a.normalize();
            b = a.cross(n);
        }
    }

    Vec3d get(size_t idx, const Vec3d src, double r) const
    {
        double phi = m_phis[idx];
        double sinphi = std::sin(phi);
        double cosphi = std::cos(phi);

        double rpscos = r * cosphi;
        double rpssin = r * sinphi;

        return {src(X) + rpscos * a(X) + rpssin * b(X),
                src(Y) + rpscos * a(Y) + rpssin * b(Y),
                src(Z) + rpscos * a(Z) + rpssin * b(Z)};
    }
};

inline Vec3d dirv(const Vec3d& startp, const Vec3d& endp) {
    return (endp - startp).normalized();
}

class PillarIndex {
    PointIndex m_index;
    using Mutex = ccr::BlockingMutex;
    mutable Mutex m_mutex;

public:

    template<class...Args> inline void guarded_insert(Args&&...args)
    {
        std::lock_guard<Mutex> lck(m_mutex);
        m_index.insert(std::forward<Args>(args)...);
    }

    template<class...Args>
    inline std::vector<PointIndexEl> guarded_query(Args&&...args) const
    {
        std::lock_guard<Mutex> lck(m_mutex);
        return m_index.query(std::forward<Args>(args)...);
    }

    template<class...Args> inline void insert(Args&&...args)
    {
        m_index.insert(std::forward<Args>(args)...);
    }

    template<class...Args>
    inline std::vector<PointIndexEl> query(Args&&...args) const
    {
        return m_index.query(std::forward<Args>(args)...);
    }

    template<class Fn> inline void foreach(Fn fn) { m_index.foreach(fn); }
    template<class Fn> inline void guarded_foreach(Fn fn)
    {
        std::lock_guard<Mutex> lck(m_mutex);
        m_index.foreach(fn);
    }

    PointIndex guarded_clone()
    {
        std::lock_guard<Mutex> lck(m_mutex);
        return m_index;
    }
};

template<class I, class DoubleI = IntegerOnly<I>>
IntegerOnly<DoubleI> pairhash(I a, I b)
{
    using std::ceil; using std::log2; using std::max; using std::min;
    static const auto constexpr Ibits = int(sizeof(I) * CHAR_BIT);
    static const auto constexpr DoubleIbits = int(sizeof(DoubleI) * CHAR_BIT);
    static const auto constexpr shift = DoubleIbits / 2 < Ibits ? Ibits / 2 : Ibits;

    I g = min(a, b), l = max(a, b);

    assert((g ? (ceil(log2(g))) : 0) <= shift);
    assert((l ? (ceil(log2(l))) : 0) <= shift);

    return (DoubleI(g) << shift) + l;
}

class SupportTreeBuildsteps {
    const SupportTreeConfig& m_cfg;
    const IndexedMesh& m_mesh;
    const std::vector<SupportPoint>& m_support_pts;

    using PtIndices = std::vector<unsigned>;

    PtIndices m_iheads;
    PtIndices m_iheads_onmodel;
    PtIndices m_iheadless;
    
    std::map<unsigned, IndexedMesh::hit_result> m_head_to_ground_scans;

    PointSet  m_support_nmls;

    std::vector<PtIndices> m_pillar_clusters;

    SupportTreeBuilder& m_builder;

    PointSet m_points;

    ThrowOnCancel m_thr;

    PillarIndex m_pillar_index;

    ccr::BlockingMutex m_bridge_mutex;

    inline IndexedMesh::hit_result ray_mesh_intersect(const Vec3d& s, 
                                                      const Vec3d& dir)
    {
        return m_mesh.query_ray_hit(s, dir);
    }

    IndexedMesh::hit_result pinhead_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r_pin,
        double r_back,
        double width,
        double safety_d);

    IndexedMesh::hit_result pinhead_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r_pin,
        double r_back,
        double width)
    {
        return pinhead_mesh_intersect(s, dir, r_pin, r_back, width,
                                      r_back * m_cfg.safety_distance_mm /
                                          m_cfg.head_back_radius_mm);
    }

    IndexedMesh::hit_result bridge_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r,
        double safety_d);

    IndexedMesh::hit_result bridge_mesh_intersect(
        const Vec3d& s,
        const Vec3d& dir,
        double r)
    {
        return bridge_mesh_intersect(s, dir, r,
                                     r * m_cfg.safety_distance_mm /
                                         m_cfg.head_back_radius_mm);
    }
    
    template<class...Args>
    inline double bridge_mesh_distance(Args&&...args) {
        return bridge_mesh_intersect(std::forward<Args>(args)...).distance();
    }

    bool interconnect(const Pillar& pillar, const Pillar& nextpillar);

    bool connect_to_nearpillar(const Head& head, long nearpillar_id);
    
    bool connect_to_ground(Head& head, const Vec3d &dir);
    inline bool connect_to_ground(Head& head);
    
    bool connect_to_model_body(Head &head);

    bool search_pillar_and_connect(const Head& source);
    
    bool create_ground_pillar(const Vec3d &jp,
                              const Vec3d &sourcedir,
                              double       radius,
                              long         head_id = SupportTreeNode::ID_UNSET);

    void add_pillar_base(long pid)
    {
        m_builder.add_pillar_base(pid, m_cfg.base_height_mm, m_cfg.base_radius_mm);
    }

    std::optional<DiffBridge> search_widening_path(const Vec3d &jp,
                                                   const Vec3d &dir,
                                                   double       radius,
                                                   double       new_radius);

public:
    SupportTreeBuildsteps(SupportTreeBuilder & builder, const SupportableMesh &sm);

    void filter();

    void add_pinheads();

    void classify();

    void routing_to_ground();

    void routing_to_model();

    void interconnect_pillars();

    inline void merge_result() { m_builder.merged_mesh(); }

    static bool execute(SupportTreeBuilder & builder, const SupportableMesh &sm);
};

}
}

#endif
