#include "NativeLayers.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/libslic3r.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar::Native {

namespace {

using Edge = std::pair<int, int>;
inline Edge edge(int a, int b) { return a < b ? Edge(a, b) : Edge(b, a); }
struct EdgeHash
{
    size_t operator()(const Edge &e) const { return std::hash<uint64_t>()((uint64_t(uint32_t(e.first)) << 32) | uint32_t(e.second)); }
};

indexed_triangle_set refine(const indexed_triangle_set &in, double max_len)
{
    indexed_triangle_set its = in;
    const float          max2 = float(max_len * max_len);
    for (int pass = 0; pass < 24; ++pass) {
        std::unordered_map<Edge, int, EdgeHash> mid;
        for (const stl_triangle_vertex_indices &f : its.indices)
            for (int i = 0; i < 3; ++i) {
                const int a = f[i], b = f[(i + 1) % 3];
                if ((its.vertices[a] - its.vertices[b]).squaredNorm() > max2)
                    mid.emplace(edge(a, b), -1);
            }
        if (mid.empty())
            break;
        for (auto &[e, id] : mid) {
            id = int(its.vertices.size());
            its.vertices.push_back(0.5f * (its.vertices[e.first] + its.vertices[e.second]));
        }
        std::vector<stl_triangle_vertex_indices> out;
        out.reserve(its.indices.size() * 2);
        for (const stl_triangle_vertex_indices &f : its.indices) {
            int m[3];
            int n = 0;
            for (int i = 0; i < 3; ++i) {
                const auto it = mid.find(edge(f[i], f[(i + 1) % 3]));
                m[i]          = it == mid.end() ? -1 : it->second;
                n += m[i] >= 0;
            }
            if (n == 0) {
                out.push_back(f);
            } else if (n == 3) {
                out.emplace_back(f[0], m[0], m[2]);
                out.emplace_back(m[0], f[1], m[1]);
                out.emplace_back(m[2], m[1], f[2]);
                out.emplace_back(m[0], m[1], m[2]);
            } else {
                int r = 0;
                while (m[r] < 0)
                    ++r;
                const int v0 = f[r], v1 = f[(r + 1) % 3], v2 = f[(r + 2) % 3];
                const int m0 = m[r], m1 = m[(r + 1) % 3], m2 = m[(r + 2) % 3];
                if (n == 1) {
                    out.emplace_back(v0, m0, v2);
                    out.emplace_back(m0, v1, v2);
                } else if (m1 >= 0) {
                    out.emplace_back(v0, m0, v2);
                    out.emplace_back(m0, v1, m1);
                    out.emplace_back(m0, m1, v2);
                } else {
                    out.emplace_back(v0, m0, m2);
                    out.emplace_back(m0, v1, v2);
                    out.emplace_back(m0, v2, m2);
                }
            }
        }
        its.indices = std::move(out);
    }
    return its;
}

Vec3d edge_crossing(const LayerField &field, double level, Vec3d a, Vec3d b, double fa, double fb, double eps)
{
    for (int i = 0; i < 40; ++i) {
        const double t = std::clamp(fa / (fa - fb), 0.05, 0.95);
        const Vec3d  p = a + t * (b - a);
        if ((b - a).norm() < eps)
            return p;
        const double fp = field.phi(p) - level;
        if (std::abs(fp) < 1e-9)
            return p;
        if ((fp < 0.) == (fa < 0.)) {
            a  = p;
            fa = fp;
        } else {
            b  = p;
            fb = fp;
        }
    }
    return a + (fa / (fa - fb)) * (b - a);
}

std::vector<std::vector<Vec3d>> contours(const indexed_triangle_set &its, const std::vector<double> &phi, const LayerField &field,
                                         double level, double eps)
{
    struct Seg
    {
        Edge  from, to;
        Vec3d a, b;
    };
    auto f = [&](int v) {
        const double d = phi[size_t(v)] - level;
        return d == 0. ? 1e-12 : d;
    };
    std::unordered_map<Edge, Vec3d, EdgeHash> cross;
    auto crossing = [&](int a, int b) {
        const Edge e  = edge(a, b);
        auto       it = cross.find(e);
        if (it == cross.end())
            it = cross.emplace(e, edge_crossing(field, level, its.vertices[e.first].cast<double>(), its.vertices[e.second].cast<double>(),
                                                f(e.first), f(e.second), eps))
                     .first;
        return it->second;
    };
    std::unordered_map<Edge, Seg, EdgeHash> by_from;
    for (const stl_triangle_vertex_indices &t : its.indices) {
        int below = 0;
        for (int i = 0; i < 3; ++i)
            below += f(t[i]) < 0.;
        if (below == 0 || below == 3)
            continue;
        std::vector<Edge> es;
        for (int i = 0; i < 3; ++i)
            if ((f(t[i]) < 0.) != (f(t[(i + 1) % 3]) < 0.))
                es.push_back(edge(t[i], t[(i + 1) % 3]));
        Vec3d       p0 = crossing(es[0].first, es[0].second), p1 = crossing(es[1].first, es[1].second);
        const Vec3d v0 = its.vertices[t[0]].cast<double>(), v1 = its.vertices[t[1]].cast<double>(), v2 = its.vertices[t[2]].cast<double>();
        const Vec3d n  = (v1 - v0).cross(v2 - v0);
        const Vec3d g  = field.gradient(0.5 * (p0 + p1));
        Edge e0 = es[0], e1 = es[1];
        if ((p1 - p0).dot(g.cross(n)) < 0.) {
            std::swap(p0, p1);
            std::swap(e0, e1);
        }
        by_from.emplace(e0, Seg{ e0, e1, p0, p1 });
    }
    std::vector<std::vector<Vec3d>> loops;
    while (!by_from.empty()) {
        auto               it    = by_from.begin();
        const Edge         start = it->first;
        std::vector<Vec3d> loop;
        Edge               at = start;
        while (true) {
            auto s = by_from.find(at);
            if (s == by_from.end())
                break;
            loop.push_back(s->second.a);
            at = s->second.to;
            by_from.erase(s);
            if (at == start)
                break;
        }
        if (loop.size() >= 3)
            loops.push_back(std::move(loop));
    }
    return loops;
}

Polygon project(const std::vector<Vec3d> &loop, const Vec3d &o, const Vec3d &u, const Vec3d &v)
{
    Polygon p;
    p.points.reserve(loop.size());
    for (const Vec3d &q : loop)
        p.points.emplace_back(scaled<coord_t>((q - o).dot(u)), scaled<coord_t>((q - o).dot(v)));
    return p;
}

}

std::pair<Vec3d, Vec3d> frame(const Vec3d &n)
{
    if (n.z() > 1. - 1e-12)
        return { Vec3d::UnitX(), Vec3d::UnitY() };
    Vec3d u = Vec3d::UnitZ().cross(n);
    u       = u.squaredNorm() < 1e-18 ? Vec3d::UnitX() : u.normalized();
    return { u, n.cross(u).normalized() };
}

std::vector<Layer> extract_layers(const indexed_triangle_set &mesh, const LayerField &field, const std::vector<double> &levels,
                                  const Params &params)
{
    const indexed_triangle_set its = refine(mesh, params.mesh_edge_max);
    std::vector<double>        phi(its.vertices.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, phi.size()), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t i = r.begin(); i < r.end(); ++i)
            phi[i] = field.phi(its.vertices[i].cast<double>());
    });
    std::vector<Layer> layers(levels.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, levels.size()), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t k = r.begin(); k < r.end(); ++k) {
            Layer &layer = layers[k];
            layer.level  = levels[k];
            std::vector<std::vector<Vec3d>> loops = contours(its, phi, field, levels[k], params.eps);
            if (loops.empty())
                continue;
            if (params.fixed_head) {
                Piece piece;
                Polygons polys;
                for (const std::vector<Vec3d> &l : loops)
                    polys.push_back(project(l, piece.o, Vec3d::UnitX(), Vec3d::UnitY()));
                piece.domain = union_ex(polys);
                piece.loops  = std::move(loops);
                layer.pieces.push_back(std::move(piece));
                continue;
            }
            auto mean_normal = [&field](const std::vector<const std::vector<Vec3d> *> &ls) {
                Vec3d g = Vec3d::Zero();
                for (const std::vector<Vec3d> *l : ls)
                    for (const Vec3d &q : *l) {
                        const Vec3d d = field.gradient(q);
                        if (d.squaredNorm() > 1e-24)
                            g += d.normalized();
                    }
                return g.squaredNorm() < 1e-24 ? Vec3d(Vec3d::UnitZ()) : Vec3d(g.normalized());
            };
            auto centroid = [](const std::vector<const std::vector<Vec3d> *> &ls) {
                Vec3d  c = Vec3d::Zero();
                size_t n = 0;
                for (const std::vector<Vec3d> *l : ls)
                    for (const Vec3d &q : *l) {
                        c += q;
                        ++n;
                    }
                return n ? Vec3d(c / double(n)) : Vec3d(Vec3d::Zero());
            };
            std::vector<const std::vector<Vec3d> *> all;
            for (const std::vector<Vec3d> &l : loops)
                all.push_back(&l);
            const Vec3d n0 = mean_normal(all), o0 = centroid(all);
            const auto [u0, v0] = frame(n0);
            Polygons polys;
            for (const std::vector<Vec3d> &l : loops)
                polys.push_back(project(l, o0, u0, v0));
            const ExPolygons comps = union_ex(polys);
            auto boundary_dist = [](const Polygon &b, const Point &p) {
                double d = std::numeric_limits<double>::max();
                for (size_t j = 0; j < b.points.size(); ++j)
                    d = std::min(d, Line(b.points[j], b.points[(j + 1) % b.points.size()]).distance_to(p));
                return d;
            };
            std::vector<std::vector<const std::vector<Vec3d> *>> members(comps.size());
            for (size_t i = 0; i < loops.size(); ++i) {
                const Point &p    = polys[i].points.front();
                size_t       best = 0;
                double       dmin = std::numeric_limits<double>::max();
                for (size_t c = 0; c < comps.size(); ++c) {
                    double d = boundary_dist(comps[c].contour, p);
                    for (const Polygon &h : comps[c].holes)
                        d = std::min(d, boundary_dist(h, p));
                    if (d < dmin) {
                        dmin = d;
                        best = c;
                    }
                }
                if (!comps.empty())
                    members[best].push_back(&loops[i]);
            }
            for (size_t c = 0; c < comps.size(); ++c) {
                if (members[c].empty())
                    continue;
                Piece piece;
                piece.n             = mean_normal(members[c]);
                piece.o             = centroid(members[c]);
                const auto [u, v]   = frame(piece.n);
                Polygons own;
                for (const std::vector<Vec3d> *l : members[c]) {
                    own.push_back(project(*l, piece.o, u, v));
                    piece.loops.push_back(*l);
                }
                piece.domain = union_ex(own);
                layer.pieces.push_back(std::move(piece));
            }
        }
    });
    return layers;
}

Vec3d lift(const Piece &piece, const Vec2d &plane_pt, const LayerField &field, double level)
{
    if (piece.n.z() > 1. - 1e-12) {
        const Vec2d xy = plane_pt + piece.o.head<2>();
        return Vec3d(xy.x(), xy.y(), field.z_at(xy, level));
    }
    const auto [u, v] = frame(piece.n);
    const Vec3d q0    = piece.o + plane_pt.x() * u + plane_pt.y() * v;
    double t = 0., lo = -1e9, hi = 1e9;
    for (int i = 0; i < 50; ++i) {
        const Vec3d  q  = q0 + t * piece.n;
        const double fq = field.phi(q) - level;
        if (std::abs(fq) < 1e-7)
            return q;
        (fq < 0. ? lo : hi) = t;
        const double d  = field.gradient(q).dot(piece.n);
        double       tn = d > 1e-9 ? t - fq / d : (fq < 0. ? t + 0.5 : t - 0.5);
        if (!(tn > lo && tn < hi))
            tn = (lo > -1e8 && hi < 1e8) ? 0.5 * (lo + hi) : (fq < 0. ? t + 0.5 : t - 0.5);
        t = tn;
    }
    return q0 + t * piece.n;
}

std::vector<Vec3d> lift_polyline(const Piece &piece, const Polyline &road, const LayerField &field, double level, double max_chord_err)
{
    std::vector<Vec3d> out;
    if (road.points.empty())
        return out;
    auto plane = [](const Point &p) { return Vec2d(unscaled(p.x()), unscaled(p.y())); };
    std::function<void(const Vec2d &, const Vec3d &, const Vec2d &, const Vec3d &, int)> seg =
        [&](const Vec2d &a, const Vec3d &la, const Vec2d &b, const Vec3d &lb, int depth) {
            const Vec2d m  = 0.5 * (a + b);
            const Vec3d lm = lift(piece, m, field, level);
            if (depth < 12 && (lm - 0.5 * (la + lb)).norm() > max_chord_err) {
                seg(a, la, m, lm, depth + 1);
                seg(m, lm, b, lb, depth + 1);
            } else
                out.push_back(lb);
        };
    Vec2d a  = plane(road.points.front());
    Vec3d la = lift(piece, a, field, level);
    out.push_back(la);
    for (size_t i = 1; i < road.points.size(); ++i) {
        const Vec2d b  = plane(road.points[i]);
        const Vec3d lb = lift(piece, b, field, level);
        seg(a, la, b, lb, 0);
        a  = b;
        la = lb;
    }
    return out;
}

double bead_height(const LayerField &field, const Vec3d &p, double level_below, double level, const Vec3d &axis)
{
    if (axis.z() > 1. - 1e-12) {
        const Vec2d xy = p.head<2>();
        return field.z_at(xy, level) - field.z_at(xy, level_below);
    }
    double hi = 0., lo = -0.05;
    while (field.phi(p + lo * axis) > level_below && lo > -50.)
        lo *= 2.;
    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        (field.phi(p + mid * axis) > level_below ? hi : lo) = mid;
    }
    return -0.5 * (lo + hi);
}

Vec3d tool_axis(const LayerField &field, const Vec3d &p, double tilt_cap)
{
    if (tilt_cap <= 0.)
        return Vec3d::UnitZ();
    Vec3d g = field.gradient(p);
    if (g.squaredNorm() < 1e-24)
        return Vec3d::UnitZ();
    g.normalize();
    const double tilt = std::acos(std::clamp(g.z(), -1., 1.));
    if (tilt <= tilt_cap)
        return g;
    Vec3d h(g.x(), g.y(), 0.);
    if (h.squaredNorm() < 1e-24)
        return Vec3d::UnitZ();
    return std::cos(tilt_cap) * Vec3d::UnitZ() + std::sin(tilt_cap) * h.normalized();
}

}
