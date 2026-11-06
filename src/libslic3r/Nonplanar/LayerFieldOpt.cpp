#include "LayerFieldOpt.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Nonplanar/HeightField.hpp"
#include "libslic3r/Nonplanar/LayerField.hpp"
#include "libslic3r/Point.hpp"

#include "SurfaceClip.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {




void StackLayerField::reset(const Vec2d &origin, double pitch, int nx, int ny, double ds, int nk, double identity_below)
{
    m_identity_below = identity_below;
    m_origin = origin;
    m_pitch  = pitch;
    m_nx = nx; m_ny = ny; m_nk = nk;
    m_ds = ds;
    m_z.assign(size_t(nx) * ny * nk, 0.f);
    for (int k = 0; k < nk; ++k)
        std::fill(m_z.begin() + size_t(k) * nx * ny, m_z.begin() + size_t(k + 1) * nx * ny, float(k * ds));
}

StackLayerField StackLayerField::planar(const BoundingBoxf3 &bbox, double pitch, double ds)
{
    StackLayerField f;
    const Vec3d size = bbox.size();
    const int   nx = std::max(2, int(std::ceil(size.x() / pitch)) + 1);
    const int   ny = std::max(2, int(std::ceil(size.y() / pitch)) + 1);
    const int   nk = std::max(2, int(std::ceil(std::max(0., bbox.max.z()) / ds)) + 2);
    f.reset(Vec2d(bbox.min.x(), bbox.min.y()), pitch, nx, ny, ds, nk);
    return f;
}

BoundingBoxf StackLayerField::xy_bbox() const
{
    return BoundingBoxf(m_origin, m_origin + m_pitch * Vec2d(m_nx - 1, m_ny - 1));
}

double StackLayerField::level_z(int k, double u, double v) const
{
    const int    ix = std::clamp(int(std::floor(u)), 0, m_nx - 2), iy = std::clamp(int(std::floor(v)), 0, m_ny - 2);
    const double tx = std::clamp(u - ix, 0., 1.), ty = std::clamp(v - iy, 0., 1.);
    return (1. - ty) * ((1. - tx) * node(ix, iy, k) + tx * node(ix + 1, iy, k)) +
           ty * ((1. - tx) * node(ix, iy + 1, k) + tx * node(ix + 1, iy + 1, k));
}

double StackLayerField::z_at(const Vec2d &xy, double s) const
{
    if (this->empty())
        return s;
    if (s <= 0.)
        return s;
    const double u = (xy.x() - m_origin.x()) / m_pitch, v = (xy.y() - m_origin.y()) / m_pitch;
    const double ks = s / m_ds;
    if (ks >= m_nk - 1)
        return level_z(m_nk - 1, u, v) + (s - this->top_level());
    const int    k = int(std::floor(ks));
    const double f = ks - k;
    return (1. - f) * level_z(k, u, v) + f * level_z(k + 1, u, v);
}

double StackLayerField::phi(const Vec3d &p) const
{
    if (this->empty() || p.z() <= 0.)
        return p.z();
    const double u = (p.x() - m_origin.x()) / m_pitch, v = (p.y() - m_origin.y()) / m_pitch;
    const double ztop = level_z(m_nk - 1, u, v);
    if (p.z() >= ztop)
        return this->top_level() + (p.z() - ztop);

    int lo = 0, hi = m_nk - 1;
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        (level_z(mid, u, v) <= p.z() ? lo : hi) = mid;
    }
    const double z0 = level_z(lo, u, v), z1 = level_z(hi, u, v);
    const double f  = z1 > z0 ? std::clamp((p.z() - z0) / (z1 - z0), 0., 1.) : 0.;
    return (lo + f) * m_ds;
}

Vec3d StackLayerField::gradient(const Vec3d &p) const
{

    const double s = this->phi(p);
    const double e = 0.25 * m_pitch, es = 0.25 * m_ds;
    const Vec2d  xy(p.x(), p.y());
    const double zs = (this->z_at(xy, s + es) - this->z_at(xy, std::max(0., s - es))) / (s + es - std::max(0., s - es));
    const double zx = (this->z_at(xy + Vec2d(e, 0.), s) - this->z_at(xy - Vec2d(e, 0.), s)) / (2. * e);
    const double zy = (this->z_at(xy + Vec2d(0., e), s) - this->z_at(xy - Vec2d(0., e), s)) / (2. * e);
    const double inv = 1. / std::max(zs, 1e-6);
    return Vec3d(-zx * inv, -zy * inv, inv);
}




namespace {

struct Interval
{
    float lo, hi;
    Vec3f n_lo, n_hi;
};

struct Columns
{
    Vec2d origin;
    double pitch;
    int nx = 0, ny = 0;
    std::vector<std::vector<Interval>> col;

    const std::vector<Interval> &at(int ix, int iy) const { return col[size_t(iy) * nx + ix]; }
    Vec2d node(int ix, int iy) const { return origin + pitch * Vec2d(ix, iy); }
    bool  inside(int ix, int iy, double z) const
    {
        for (const Interval &it : at(ix, iy))
            if (z >= it.lo && z <= it.hi)
                return true;
        return false;
    }

    float top_below(int ix, int iy, double z) const
    {
        float best = std::numeric_limits<float>::quiet_NaN();
        for (const Interval &it : at(ix, iy))
            if (it.lo <= z)
                best = std::isnan(best) ? std::min(it.hi, float(z)) : std::max(best, std::min(it.hi, float(z)));
        return best;
    }
    bool overlaps(int ix, int iy, double z0, double z1) const
    {
        if (ix < 0 || iy < 0 || ix >= nx || iy >= ny)
            return false;
        for (const Interval &it : at(ix, iy))
            if (it.hi >= z0 && it.lo <= z1)
                return true;
        return false;
    }
};

Columns rasterize(const indexed_triangle_set &its, const Vec2d &origin, double pitch, int nx, int ny)
{
    struct Hit { float z; Vec3f n; };
    std::vector<std::vector<Hit>> hits(size_t(nx) * ny);
    for (const stl_triangle_vertex_indices &f : its.indices) {
        const Vec3f &a = its.vertices[f[0]], &b = its.vertices[f[1]], &c = its.vertices[f[2]];
        Vec3f n = (b - a).cross(c - a);
        const float len = n.norm();
        if (len <= 0.f || std::abs(n.z()) < 1e-7f * len)
            continue;
        n /= len;
        const double minx = std::min({ a.x(), b.x(), c.x() }), maxx = std::max({ a.x(), b.x(), c.x() });
        const double miny = std::min({ a.y(), b.y(), c.y() }), maxy = std::max({ a.y(), b.y(), c.y() });
        const int ix0 = std::max(0, int(std::ceil((minx - origin.x()) / pitch))), ix1 = std::min(nx - 1, int(std::floor((maxx - origin.x()) / pitch)));
        const int iy0 = std::max(0, int(std::ceil((miny - origin.y()) / pitch))), iy1 = std::min(ny - 1, int(std::floor((maxy - origin.y()) / pitch)));
        const double area = double(b.x() - a.x()) * (c.y() - a.y()) - double(b.y() - a.y()) * (c.x() - a.x());
        if (area == 0.)
            continue;
        for (int iy = iy0; iy <= iy1; ++iy)
            for (int ix = ix0; ix <= ix1; ++ix) {
                const double px = origin.x() + pitch * ix, py = origin.y() + pitch * iy;
                const double w0 = (double(b.x() - px) * (c.y() - py) - double(b.y() - py) * (c.x() - px)) / area;
                const double w1 = (double(c.x() - px) * (a.y() - py) - double(c.y() - py) * (a.x() - px)) / area;
                const double w2 = 1. - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9)
                    continue;
                hits[size_t(iy) * nx + ix].push_back({ float(w0 * a.z() + w1 * b.z() + w2 * c.z()), n });
            }
    }
    Columns cols { origin, pitch, nx, ny, std::vector<std::vector<Interval>>(size_t(nx) * ny) };
    tbb::parallel_for(tbb::blocked_range<size_t>(0, hits.size()), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t i = r.begin(); i < r.end(); ++i) {
            std::vector<Hit> &h = hits[i];
            std::sort(h.begin(), h.end(), [](const Hit &l, const Hit &r) { return l.z < r.z; });
            int depth = 0;
            Interval cur {};
            for (size_t j = 0; j < h.size(); ++j) {
                const bool down = h[j].n.z() < 0.f;

                if (j > 0 && (h[j - 1].n.z() < 0.f) == down && h[j].z - h[j - 1].z < 1e-4f)
                    continue;
                if (down) {
                    if (depth++ == 0) { cur.lo = h[j].z; cur.n_lo = h[j].n; }
                } else if (depth > 0 && --depth == 0) {
                    cur.hi = h[j].z; cur.n_hi = h[j].n;
                    if (cur.hi - cur.lo > 1e-3f)
                        cols.col[i].push_back(cur);
                }
            }
        }
    });
    return cols;
}

double slope_of(const Vec3f &n) { return std::acos(std::clamp(double(std::abs(n.z())), 0., 1.)); }

enum class Ceiling { Island, Bridge, Cantilever };




Ceiling classify_ceiling(const Columns &cols, int ix, int iy, double lo, Vec2d *away)
{
    const int R = int(std::ceil(40. / cols.pitch));
    int found[16];
    Vec2d acc = Vec2d::Zero();
    bool any = false;
    for (int a = 0; a < 16; ++a) {
        const Vec2d d(std::cos(a * PI / 8), std::sin(a * PI / 8));
        found[a] = 0;
        for (int r = 1; r <= R; ++r)
            if (cols.overlaps(ix + int(std::lround(r * d.x())), iy + int(std::lround(r * d.y())), lo - 1.0, lo - 0.1)) {
                found[a] = r;
                acc += d / r;
                any = true;
                break;
            }
    }
    if (!any)
        return Ceiling::Island;
    for (int a = 0; a < 16; ++a)
        for (int b = a + 6; b <= a + 10; ++b)
            if (found[a] && found[b % 16])
                return Ceiling::Bridge;
    if (away)
        *away = acc.squaredNorm() > 1e-12 ? Vec2d(-acc.normalized()) : Vec2d(1., 0.);
    return Ceiling::Cantilever;
}

}

namespace {



double deformed_overhang(const Vec3d &n, const Vec3d &g)
{
    const Vec3d nd(n.x() * g.z() - n.z() * g.x(), n.y() * g.z() - n.z() * g.y(), n.z());
    return std::asin(std::clamp(-nd.z() / nd.norm(), -1., 1.)) * 180. / PI;
}







SupportFeatures support_features(const indexed_triangle_set &mesh, const LayerField &field, const Columns &cols, double support_angle,
                                 double z_base)
{
    constexpr double band = 2.;
    const double     q = cols.pitch;
    std::vector<uint8_t> flagged(mesh.indices.size(), 0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, mesh.indices.size()), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t i = r.begin(); i < r.end(); ++i) {
            const stl_triangle_vertex_indices &f = mesh.indices[i];
            const Vec3d a = mesh.vertices[f[0]].cast<double>(), b = mesh.vertices[f[1]].cast<double>(), c = mesh.vertices[f[2]].cast<double>();
            Vec3d n = (b - a).cross(c - a);
            if (n.squaredNorm() <= 0.)
                continue;
            n.normalize();
            const Vec3d ctr = (a + b + c) / 3.;
            if (n.z() >= -0.05 || ctr.z() <= z_base)
                continue;
            const Vec3d g = field.gradient(ctr + Vec3d(0., 0., 0.02));
            if (deformed_overhang(n, g) <= support_angle)
                continue;
            const int ix = std::clamp(int(std::lround((ctr.x() - cols.origin.x()) / q)), 0, cols.nx - 1);
            const int iy = std::clamp(int(std::lround((ctr.y() - cols.origin.y()) / q)), 0, cols.ny - 1);
            if (std::hypot(n.x(), n.y()) < 0.2 && std::acos(std::clamp(g.normalized().z(), -1., 1.)) < 5. * PI / 180. &&
                classify_ceiling(cols, ix, iy, ctr.z(), nullptr) == Ceiling::Bridge)
                continue;
            flagged[i] = 1;
        }
    });

    std::map<int, std::vector<float>> bands;
    const size_t cells = size_t(cols.nx) * cols.ny;
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        if (!flagged[i])
            continue;
        const stl_triangle_vertex_indices &f = mesh.indices[i];
        const Vec3d v[3] = { mesh.vertices[f[0]].cast<double>(), mesh.vertices[f[1]].cast<double>(), mesh.vertices[f[2]].cast<double>() };
        const double area2 = (v[1].x() - v[0].x()) * (v[2].y() - v[0].y()) - (v[1].y() - v[0].y()) * (v[2].x() - v[0].x());
        auto mark = [&](const Vec3d &pt) {
            const int ix = int(std::lround((pt.x() - cols.origin.x()) / q)), iy = int(std::lround((pt.y() - cols.origin.y()) / q));
            if (ix < 0 || iy < 0 || ix >= cols.nx || iy >= cols.ny)
                return;
            const float below = cols.top_below(ix, iy, pt.z() - 0.3);
            const float hgt   = float(pt.z() - (std::isnan(below) ? 0. : below));
            std::vector<float> &g = bands[int(std::floor(pt.z() / band))];
            if (g.empty())
                g.assign(cells, 0.f);
            float &cell = g[size_t(iy) * cols.nx + ix];
            cell = std::max(cell, hgt);
        };
        mark((v[0] + v[1] + v[2]) / 3.);
        const double minx = std::min({ v[0].x(), v[1].x(), v[2].x() }), maxx = std::max({ v[0].x(), v[1].x(), v[2].x() });
        const double miny = std::min({ v[0].y(), v[1].y(), v[2].y() }), maxy = std::max({ v[0].y(), v[1].y(), v[2].y() });
        if (std::abs(area2) < 1e-12)
            continue;
        for (int iy = std::max(0, int(std::ceil((miny - cols.origin.y()) / q))); iy <= std::min(cols.ny - 1, int(std::floor((maxy - cols.origin.y()) / q))); ++iy)
            for (int ix = std::max(0, int(std::ceil((minx - cols.origin.x()) / q))); ix <= std::min(cols.nx - 1, int(std::floor((maxx - cols.origin.x()) / q))); ++ix) {
                const Vec2d pt = cols.node(ix, iy);
                const double w0 = ((v[1].x() - pt.x()) * (v[2].y() - pt.y()) - (v[1].y() - pt.y()) * (v[2].x() - pt.x())) / area2;
                const double w1 = ((v[2].x() - pt.x()) * (v[0].y() - pt.y()) - (v[2].y() - pt.y()) * (v[0].x() - pt.x())) / area2;
                const double w2 = 1. - w0 - w1;
                if (w0 >= 0. && w1 >= 0. && w2 >= 0.)
                    mark(Vec3d(pt.x(), pt.y(), w0 * v[0].z() + w1 * v[1].z() + w2 * v[2].z()));
            }
    }
    SupportFeatures out;
    std::vector<int> label(cells), stack;
    for (auto &[b, g] : bands) {
        std::fill(label.begin(), label.end(), -1);
        for (size_t start = 0; start < cells; ++start) {
            if (g[start] <= 0.f || label[start] >= 0)
                continue;
            double area_h = 0., h_sum = 0., perim = 0.;
            int    n = 0;
            stack.assign(1, int(start));
            label[start] = int(start);
            while (!stack.empty()) {
                const int c = stack.back();
                stack.pop_back();
                area_h += q * q * g[c];
                h_sum += g[c];
                ++n;
                const int cx = c % cols.nx, cy = c / cols.nx;
                const int nb[4][2] = { { cx + 1, cy }, { cx - 1, cy }, { cx, cy + 1 }, { cx, cy - 1 } };
                for (const auto &e : nb) {
                    if (e[0] < 0 || e[1] < 0 || e[0] >= cols.nx || e[1] >= cols.ny || g[size_t(e[1]) * cols.nx + e[0]] <= 0.f) {
                        perim += q;
                        continue;
                    }
                    const int m = e[1] * cols.nx + e[0];
                    if (label[m] < 0) {
                        label[m] = int(start);
                        stack.push_back(m);
                    }
                }
            }
            const double h_mean = h_sum / n;
            out.area_height += area_h;
            out.perimeter_height += perim * h_mean;
            out.region_height += h_mean;
            ++out.regions;
        }
    }
    return out;
}


double top_rms_angle(const LayerField &field, const Columns &cols)
{
    double sq = 0., area = 0.;
    for (int iy = 0; iy < cols.ny; ++iy)
        for (int ix = 0; ix < cols.nx; ++ix) {
            const std::vector<Interval> &c = cols.at(ix, iy);
            if (c.empty())
                continue;
            const Interval &it = c.back();
            const double    slope = slope_of(it.n_hi);
            if (it.n_hi.z() <= 0.f || slope < 2. * PI / 180. || slope > 80. * PI / 180.)
                continue;
            const Vec2d  xy = cols.node(ix, iy);
            const Vec3d  g  = field.gradient(Vec3d(xy.x(), xy.y(), it.hi - 0.02)).normalized();
            const double a  = std::acos(std::clamp(g.dot(it.n_hi.cast<double>()), -1., 1.));
            const double w  = 1. / it.n_hi.z();
            sq += w * a * a;
            area += w;
        }
    return area > 0. ? std::sqrt(sq / area) * 180. / PI : 0.;
}

double estimate_support(const SupportFeatures &f)
{
    return f.area_height * SupportFeatures::k_area + f.perimeter_height * SupportFeatures::k_perimeter +
           f.region_height * SupportFeatures::k_region;
}

double estimate_support(const indexed_triangle_set &mesh, const LayerField &field, const Columns &cols, double support_angle,
                        double z_base)
{
    return estimate_support(support_features(mesh, field, cols, support_angle, z_base));
}

}

double layer_field_slope_cap(const LayerFieldParams &params)
{



    double cap = params.max_slope > 0. ? params.max_slope : 45. * PI / 180.;
    if (params.head && !params.head->empty()) {
        const double reach = std::max(params.head->tip_allowance.radius, 2. * params.head->tip_flat_radius);
        for (int i = 0; i < 32; ++i)
            cap = std::min(cap, params.head->max_slope(2. * PI * i / 32, params.layer_height, reach));
    }
    return cap;
}




namespace {

using Clock = std::chrono::steady_clock;


struct Term
{
    static constexpr int N = 16;
    size_t idx[N];
    float  c[N];
    int    n;
    float  rhs, w;
    float  value(const std::vector<float> &x) const
    {
        float v = 0.f;
        for (int i = 0; i < n; ++i)
            v += c[i] * x[idx[i]];
        return v;
    }
};


void add_bilinear(Term &t, int nx, int ny, const Vec2d &origin, double pitch, int k, const Vec2d &xy, float coef)
{
    const double u = (xy.x() - origin.x()) / pitch, v = (xy.y() - origin.y()) / pitch;
    const int    ix = std::clamp(int(std::floor(u)), 0, nx - 2), iy = std::clamp(int(std::floor(v)), 0, ny - 2);
    const float  tx = float(std::clamp(u - ix, 0., 1.)), ty = float(std::clamp(v - iy, 0., 1.));
    const size_t base = (size_t(k) * ny + iy) * nx + ix;
    const size_t id[4] = { base, base + 1, base + nx, base + nx + 1 };
    const float  w[4]  = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
    for (int i = 0; i < 4; ++i) {
        assert(t.n < Term::N);
        t.idx[t.n] = id[i];
        t.c[t.n++] = coef * w[i];
    }
}


double bilinear_level(const std::vector<float> &f, int nx, int ny, const Vec2d &origin, double pitch, int k, const Vec2d &xy)
{
    const double u = (xy.x() - origin.x()) / pitch, v = (xy.y() - origin.y()) / pitch;
    const int    ix = std::clamp(int(std::floor(u)), 0, nx - 2), iy = std::clamp(int(std::floor(v)), 0, ny - 2);
    const double tx = std::clamp(u - ix, 0., 1.), ty = std::clamp(v - iy, 0., 1.);
    const size_t b  = (size_t(k) * ny + iy) * nx + ix;
    return (1 - ty) * ((1 - tx) * f[b] + tx * f[b + 1]) + ty * ((1 - tx) * f[b + nx] + tx * f[b + nx + 1]);
}

struct Problem
{
    int nx, ny, nk;
    double pitch, ds;
    float w_xy, w_s, w_reg;
    std::vector<uint8_t> fixed;
    std::vector<float>   rho, anchor;


    std::vector<float>   ref, inv_az;


    std::vector<float>   xy_w;
    float spacing(size_t i) const { return inv_az.empty() ? 1.f : inv_az[i]; }
    float wxy(size_t i, size_t j) const { return xy_w.empty() ? w_xy : w_xy * std::min(xy_w[i], xy_w[j]); }
    std::vector<Term>    terms;

    size_t n() const { return size_t(nx) * ny * nk; }
    size_t id(int ix, int iy, int k) const { return (size_t(k) * ny + iy) * nx + ix; }


    void apply(const std::vector<float> &x, std::vector<float> &y) const
    {
        const size_t layer = size_t(nx) * ny;
        tbb::parallel_for(tbb::blocked_range<int>(0, nk), [&](const tbb::blocked_range<int> &r) {
            for (int k = r.begin(); k < r.end(); ++k)
                for (int iy = 0; iy < ny; ++iy)
                    for (int ix = 0; ix < nx; ++ix) {
                        const size_t i = id(ix, iy, k);
                        const float  xi = x[i];
                        float        acc = (w_reg + rho[i]) * xi;
                        if (ix > 0)      acc += wxy(i, i - 1) * (xi - x[i - 1]);
                        if (ix < nx - 1) acc += wxy(i, i + 1) * (xi - x[i + 1]);
                        if (iy > 0)      acc += wxy(i, i - nx) * (xi - x[i - nx]);
                        if (iy < ny - 1) acc += wxy(i, i + nx) * (xi - x[i + nx]);
                        if (k > 0)       acc += w_s * (xi - x[i - layer]);
                        if (k < nk - 1)  acc += w_s * (xi - x[i + layer]);
                        y[i] = acc;
                    }
        });
        for (const Term &t : terms) {
            const float v = t.w * t.value(x);
            for (int i = 0; i < t.n; ++i)
                y[t.idx[i]] += t.c[i] * v;
        }
    }

    void rhs(std::vector<float> &b) const
    {
        const size_t layer = size_t(nx) * ny;
        b.assign(n(), 0.f);
        for (int k = 0; k < nk; ++k)
            for (size_t j = 0; j < layer; ++j) {
                const size_t i = size_t(k) * layer + j;
                float acc = w_reg * float(k * ds) + rho[i] * anchor[i];

                if (k > 0)      acc += w_s * float(ds) * spacing(i);
                if (k < nk - 1) acc -= w_s * float(ds) * spacing(i + layer);

                if (!ref.empty()) {
                    const int ix = int(j % nx), iy = int(j / nx);
                    if (ix > 0)      acc += wxy(i, i - 1) * (ref[i] - ref[i - 1]);
                    if (ix < nx - 1) acc += wxy(i, i + 1) * (ref[i] - ref[i + 1]);
                    if (iy > 0)      acc += wxy(i, i - nx) * (ref[i] - ref[i - nx]);
                    if (iy < ny - 1) acc += wxy(i, i + nx) * (ref[i] - ref[i + nx]);
                }
                b[i] = acc;
            }
        for (const Term &t : terms) {
            for (int i = 0; i < t.n; ++i)
                b[t.idx[i]] += t.w * t.c[i] * t.rhs;
        }
    }

    void diagonal(std::vector<float> &d) const
    {
        d.assign(n(), 0.f);
        for (int k = 0; k < nk; ++k)
            for (int iy = 0; iy < ny; ++iy)
                for (int ix = 0; ix < nx; ++ix) {
                    const size_t i = id(ix, iy, k);
                    d[i] = w_reg + rho[i] + w_s * float((k > 0) + (k < nk - 1));
                    if (ix > 0)      d[i] += wxy(i, i - 1);
                    if (ix < nx - 1) d[i] += wxy(i, i + 1);
                    if (iy > 0)      d[i] += wxy(i, i - nx);
                    if (iy < ny - 1) d[i] += wxy(i, i + nx);
                }
        for (const Term &t : terms) {
            for (int i = 0; i < t.n; ++i)
                d[t.idx[i]] += t.w * t.c[i] * t.c[i];
        }
    }
};


int solve_cg(const Problem &P, std::vector<float> &x, int max_iter, double tol)
{
    const size_t n = P.n();
    std::vector<float> b, d, r(n), z(n), p(n), q(n);
    P.rhs(b);
    P.diagonal(d);
    P.apply(x, q);
    auto dot = [&](const std::vector<float> &u, const std::vector<float> &v) {
        double s = 0.;
        for (size_t i = 0; i < n; ++i)
            s += double(u[i]) * v[i];
        return s;
    };
    for (size_t i = 0; i < n; ++i) {
        r[i] = P.fixed[i] ? 0.f : b[i] - q[i];
        z[i] = r[i] / d[i];
        p[i] = z[i];
    }
    double rz = dot(r, z);
    const double r0 = std::sqrt(dot(r, r));
    if (r0 == 0.)
        return 0;
    int it = 0;
    for (; it < max_iter; ++it) {
        P.apply(p, q);
        for (size_t i = 0; i < n; ++i)
            if (P.fixed[i])
                q[i] = 0.f;
        const double pq = dot(p, q);
        if (pq <= 0.)
            break;
        const float alpha = float(rz / pq);
        for (size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * q[i];
            z[i] = r[i] / d[i];
        }
        if (std::sqrt(dot(r, r)) < tol * r0)
            break;
        const double rz1 = dot(r, z);
        const float  beta = float(rz1 / rz);
        rz = rz1;
        for (size_t i = 0; i < n; ++i)
            p[i] = z[i] + beta * p[i];
    }
    return it;
}

struct TopSample { int ix, iy; float T; float w; float slope; float s_star; };








struct DownSample { Vec2d c; float zb; Vec2d u; float limit; float w; float az = 1.f; };





void project(std::vector<float> &z, const Problem &P, const Columns &cols, const LayerFieldParams &params,
             double cap, int k_fixed)
{
    const int    nx = P.nx, ny = P.ny;
    const size_t layer = size_t(nx) * ny;
    const float  d_min = float(P.ds * params.min_layer_height / params.layer_height);
    const float  d_max = float(P.ds * params.max_layer_height / params.layer_height);
    const BoundingBoxf bb(cols.origin, cols.origin + cols.pitch * Vec2d(nx - 1, ny - 1));
    HeightField hf(bb, cols.pitch);
    assert(hf.nx() == nx && hf.ny() == ny);
    std::vector<float> L(layer), U(layer);
    for (int k = k_fixed + 1; k < P.nk; ++k) {
        float *zk = z.data() + size_t(k) * layer;
        const float *zp = zk - layer;

        const float *ref = P.ref.empty() ? nullptr : P.ref.data() + size_t(k) * layer;
        for (size_t j = 0; j < layer; ++j) {
            const float sp = P.spacing(size_t(k) * layer + j);


            L[j]  = zp[j] + (P.ref.empty() ? d_min * sp : 0.f);
            U[j]  = zp[j] + d_max * sp;
            zk[j] = std::clamp(zk[j], L[j], U[j]);
        }


        for (size_t j = 0; j < layer; ++j)
            hf.data()[j] = ref && zk[j] - zp[j] < 0.25f * d_min ? HeightField::NoData : zk[j] - (ref ? ref[j] : 0.f);
        const HeightField lo = slope_cap_minorant(hf, cap);
        for (float &v : hf.data())
            v = -v;
        const HeightField hi = slope_cap_minorant(hf, cap);
        for (size_t j = 0; j < layer; ++j)
            if (!std::isnan(lo.data()[j]))
                zk[j] = std::clamp(0.5f * (lo.data()[j] - hi.data()[j]) + (ref ? ref[j] : 0.f), L[j], U[j]);
        for (size_t j = 0; j < layer; ++j)
            zk[j] = std::max(zk[j], L[j]);
        if (params.throw_if_canceled && (k & 15) == 0)
            params.throw_if_canceled();
    }
}


void box_blur(std::vector<float> &a, int nx, int ny, int r)
{
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> t(a.size());
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix) {
                float acc = 0.f;
                for (int d = -r; d <= r; ++d)
                    acc += pass == 0 ? a[size_t(iy) * nx + std::clamp(ix + d, 0, nx - 1)] : a[size_t(std::clamp(iy + d, 0, ny - 1)) * nx + ix];
                t[size_t(iy) * nx + ix] = acc / float(2 * r + 1);
            }
        a.swap(t);
    }
}


void zero_around(std::vector<float> &a, const std::vector<uint8_t> &mark, int nx, int ny, int r)
{
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix)
            if (mark[size_t(iy) * nx + ix])
                for (int jy = std::max(0, iy - r); jy <= std::min(ny - 1, iy + r); ++jy)
                    for (int jx = std::max(0, ix - r); jx <= std::min(nx - 1, ix + r); ++jx)
                        a[size_t(jy) * nx + jx] = 0.f;
}

}



static StackLayerField solve_single(const indexed_triangle_set &mesh, const BoundingBoxf3 &bb, const LayerFieldParams &params,
                                    LayerFieldReport *report, StackLayerField *blended = nullptr, StackLayerField *frozen = nullptr)
{
    const auto t0 = Clock::now();
    std::ostringstream log;
    const double extent = std::max(bb.size().x(), bb.size().y());

    const double p      = params.pitch > 0. ? params.pitch :
                          params.tool_axis   ? std::clamp(extent / 96., 0.5, 3.) : std::clamp(extent / 64., 1., 3.);
    const double ds     = params.level_spacing;
    const double h      = params.layer_height;
    const double cap    = layer_field_slope_cap(params);

    const double cap_grid = cap - PI / 180.;
    const double margin = 2. * p;
    const Vec2d  origin(bb.min.x() - margin, bb.min.y() - margin);
    const int    nx = int(std::ceil((bb.size().x() + 2. * margin) / p)) + 1;
    const int    ny = int(std::ceil((bb.size().y() + 2. * margin) / p)) + 1;
    const Columns cols = rasterize(mesh, origin, p, nx, ny);
    const double s_flat = params.flat_base_height;


    std::vector<TopSample>  tops;
    std::vector<DownSample> downs;
    std::vector<std::vector<int>> top_of(size_t(nx) * ny);

    const double cot_oh = 1. / std::tan((params.overhang_angle - 3.) * PI / 180.);
    const double r_max  = params.max_layer_height / params.layer_height;


    const bool rotary = bool(params.tool_axis);
    auto axis_at = [&](const Vec3d &pt) -> Vec3d {
        if (!rotary)
            return Vec3d::UnitZ();
        Vec3d a = params.tool_axis(pt);
        if (a.squaredNorm() < 1e-12)
            return Vec3d::UnitZ();
        a.normalize();
        if (a.z() < 0.)
            a = -a;
        const double min_z = std::cos(75. * PI / 180.);
        if (a.z() < min_z) {
            const Vec2d xy = a.head<2>().normalized() * std::sin(75. * PI / 180.);
            a = Vec3d(xy.x(), xy.y(), min_z);
        }
        return a;
    };
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            const std::vector<Interval> &c = cols.at(ix, iy);
            for (size_t j = 0; j < c.size(); ++j) {
                const Interval &it = c[j];
                const Vec2d  xy    = cols.node(ix, iy);

                const double slope = rotary ? std::acos(std::clamp(axis_at(Vec3d(xy.x(), xy.y(), it.hi)).dot(it.n_hi.cast<double>()), -1., 1.)) :
                                              slope_of(it.n_hi);
                if (it.hi > s_flat && it.n_hi.z() > 0.34f) {
                    const double vis = j + 1 == c.size() ? 1. : params.hidden_top_weight;

                    const double wcap = slope <= cap ? 1. : 0.2;
                    top_of[size_t(iy) * nx + ix].push_back(int(tops.size()));
                    tops.push_back({ ix, iy, it.hi, float(vis * wcap), float(slope), it.hi });
                } else
                    top_of[size_t(iy) * nx + ix].push_back(-1);
            }
        }


    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d a = mesh.vertices[f[0]].cast<double>(), b = mesh.vertices[f[1]].cast<double>(), c = mesh.vertices[f[2]].cast<double>();
        Vec3d n = (b - a).cross(c - a);
        const double area = 0.5 * n.norm();
        if (area <= 0.)
            continue;
        n.normalize();
        const Vec3d ctr = (a + b + c) / 3.;


        const Vec3d  ax  = axis_at(ctr);
        const double na  = n.dot(ax);
        if (na >= -0.05 || ctr.z() <= s_flat + 0.05)
            continue;
        const Vec3d  n_perp = n - na * ax;
        const double h      = rotary ? n_perp.norm() : std::hypot(n.x(), n.y());
        const double limit  = h / -na;

        if (limit >= std::tan(cap) + r_max * cot_oh)
            continue;
        const float w = float(std::clamp(area / (p * p), 0.25, 4.));
        Vec2d u = Vec2d::Zero();
        if (h >= 0.2 && n_perp.head<2>().norm() > 1e-6)
            u = n_perp.head<2>().normalized();
        else {
            const int ix = std::clamp(int(std::lround((ctr.x() - origin.x()) / p)), 0, nx - 1);
            const int iy = std::clamp(int(std::lround((ctr.y() - origin.y()) / p)), 0, ny - 1);
            const Ceiling kind = classify_ceiling(cols, ix, iy, ctr.z(), &u);
            if (kind == Ceiling::Island)
                continue;
            if (kind == Ceiling::Bridge)
                u = Vec2d::Zero();
        }
        downs.push_back({ Vec2d(ctr.x(), ctr.y()), float(ctr.z()), u, float(limit), w, float(ax.z()) });
    }




    {
        const double a_lo = h / (0.95 * params.max_layer_height), a_hi = h / (1.05 * params.min_layer_height);
        std::vector<std::array<int, 4>> nbr(tops.size());
        std::vector<std::pair<float, float>> bounds(tops.size());
        for (size_t i = 0; i < tops.size(); ++i) {
            const TopSample &t = tops[i];
            static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; ++d) {
                nbr[i][d] = -1;
                const int jx = t.ix + dx[d], jy = t.iy + dy[d];
                if (jx < 0 || jy < 0 || jx >= nx || jy >= ny)
                    continue;
                float best = float(3. * p);
                for (int ti : top_of[size_t(jy) * nx + jx])
                    if (ti >= 0 && std::abs(tops[ti].T - t.T) < best) {
                        best = std::abs(tops[ti].T - t.T);
                        nbr[i][d] = ti;
                    }
            }
            const double base = t.T - s_flat;


            const double az = rotary ? axis_at(Vec3d(cols.node(t.ix, t.iy).x(), cols.node(t.ix, t.iy).y(), t.T)).z() : 1.;
            bounds[i] = { float(s_flat + base * a_lo * az), float(s_flat + base * a_hi) };
        }


        const float lambda = 2e-4f, omega = 1.8f;
        for (int it = 0; it < 20000; ++it) {
            float change = 0.f;
            for (size_t i = 0; i < tops.size(); ++i) {
                float acc = lambda * tops[i].T, wsum = lambda;
                for (int j : nbr[i])
                    if (j >= 0) { acc += tops[j].s_star; wsum += 1.f; }
                const float v = std::clamp(tops[i].s_star + omega * (acc / wsum - tops[i].s_star), bounds[i].first, bounds[i].second);
                change = std::max(change, std::abs(v - tops[i].s_star));
                tops[i].s_star = v;
            }
            if (change < 1e-5f)
                break;
        }
    }


    double s_max = bb.max.z();
    for (const TopSample &t : tops)
        s_max = std::max(s_max, double(t.s_star));
    const int nk = int(std::ceil(s_max / ds)) + 4;
    StackLayerField field;
    field.reset(origin, p, nx, ny, ds, nk, std::ceil(s_flat / ds - 1e-9) * ds);
    std::vector<float> &z = field.data();

    Problem P;
    P.nx = nx; P.ny = ny; P.nk = nk; P.pitch = p; P.ds = ds;
    P.w_xy  = float(params.w_smooth_xy * ds);


    P.w_s   = float(params.w_smooth_s * p * p / ds * (params.tool_axis ? 0.02 : 1.));
    const float stencil = 4.f * P.w_xy + 2.f * P.w_s;

    P.w_reg = (params.tool_axis ? 1e-6f : 1e-3f) * stencil;
    P.fixed.assign(P.n(), 0);
    P.rho.assign(P.n(), 0.f);
    P.anchor.assign(P.n(), 0.f);

    const int k_fixed = std::max(0, int(std::ceil(s_flat / ds - 1e-9)));
    for (int k = 0; k <= k_fixed && k < nk; ++k)
        std::fill(P.fixed.begin() + P.id(0, 0, k), P.fixed.begin() + P.id(0, 0, k + 1), 1);


    std::vector<float> clear_below;
    if (params.conform_clear_radius > 0.) {
        clear_below.assign(size_t(nx) * ny, -1e9f);
        const int r = int(std::ceil(params.conform_clear_radius / p));
        for (const DownSample &d : downs) {
            const int cx = int(std::lround((d.c.x() - origin.x()) / p)), cy = int(std::lround((d.c.y() - origin.y()) / p));
            for (int iy = std::max(0, cy - r); iy <= std::min(ny - 1, cy + r); ++iy)
                for (int ix = std::max(0, cx - r); ix <= std::min(nx - 1, cx + r); ++ix)
                    if ((ix - cx) * (ix - cx) + (iy - cy) * (iy - cy) <= r * r)
                        clear_below[size_t(iy) * nx + ix] = std::max(clear_below[size_t(iy) * nx + ix], d.zb);
        }
    }
    std::vector<Term> conform;
    for (const TopSample &t : tops) {
        if (!clear_below.empty()) {
            const float zb = clear_below[size_t(t.iy) * nx + t.ix];
            if (zb < t.T && t.T - zb < 20.f)
                continue;
        }
        const double u = t.s_star / ds;
        const int    k = std::min(int(std::floor(u)), nk - 2);
        const float  f = float(u - k);
        Term c {};
        c.idx[0] = P.id(t.ix, t.iy, k);
        c.idx[1] = P.id(t.ix, t.iy, k + 1);
        c.c[0] = 1.f - f;
        c.c[1] = f;
        c.n = 2;
        c.rhs = t.T;
        c.w = float(params.w_conform) * stencil * t.w;
        conform.push_back(c);
    }

    const auto t_prep = Clock::now();
    int cg_total = 0;




    auto update_reference = [&](int iterations) {
        const size_t layer = size_t(nx) * ny;
        if (P.ref.empty()) {
            P.ref.assign(P.n(), 0.f);
            P.inv_az.assign(P.n(), 1.f);
        }
        std::vector<float> gx(layer), gy(layer), wn(layer);
        for (int k = k_fixed + 1; k < nk; ++k) {
            float *ref = P.ref.data() + size_t(k) * layer;
            for (int iy = 0; iy < ny; ++iy)
                for (int ix = 0; ix < nx; ++ix) {
                    const size_t j = size_t(iy) * nx + ix;
                    const Vec2d  xy = cols.node(ix, iy);
                    const Vec3d  a  = axis_at(Vec3d(xy.x(), xy.y(), z[size_t(k) * layer + j]));
                    gx[j] = float(-a.x() / a.z());
                    gy[j] = float(-a.y() / a.z());

                    const double zk = z[size_t(k) * layer + j];
                    wn[j] = cols.overlaps(ix, iy, zk - ds, zk + ds) ? 1.f : 0.01f;
                    P.inv_az[size_t(k) * layer + j] = float(1. / a.z());
                }
            const float pp = float(p), omega = 1.9f;
            for (int it = 0; it < iterations; ++it)
                for (int iy = 0; iy < ny; ++iy)
                    for (int ix = 0; ix < nx; ++ix) {
                        const size_t j = size_t(iy) * nx + ix;
                        float acc = 0.f, deg = 0.f;
                        auto edge = [&](size_t i, float g) {
                            const float w = std::max(wn[i], wn[j]);
                            acc += w * (ref[i] + g * pp);
                            deg += w;
                        };
                        if (ix > 0)      edge(j - 1, 0.5f * (gx[j - 1] + gx[j]));
                        if (ix < nx - 1) edge(j + 1, -0.5f * (gx[j + 1] + gx[j]));
                        if (iy > 0)      edge(j - nx, 0.5f * (gy[j - nx] + gy[j]));
                        if (iy < ny - 1) edge(j + nx, -0.5f * (gy[j + nx] + gy[j]));
                        ref[j] += omega * (acc / deg - ref[j]);
                    }
        }
    };
    if (rotary) {


        update_reference(6 * std::max(nx, ny));
        const size_t layer = size_t(nx) * ny;
        for (int k = k_fixed + 1; k < nk; ++k) {
            const float *ref = P.ref.data() + size_t(k) * layer;
            const float  lo  = *std::min_element(ref, ref + layer);
            for (size_t j = 0; j < layer; ++j)
                z[size_t(k) * layer + j] = std::max(z[size_t(k - 1) * layer + j], float(k * ds) + ref[j] - lo);
        }
        update_reference(2 * std::max(nx, ny));
    }
    const float d_min_level = float(ds * params.min_layer_height / params.layer_height);
    auto update_collapsed = [&]() {
        const size_t layer = size_t(nx) * ny;
        P.xy_w.assign(P.n(), 1.f);
        for (int k = k_fixed + 1; k < nk; ++k)
            for (size_t j = 0; j < layer; ++j)
                if (z[size_t(k) * layer + j] - z[size_t(k - 1) * layer + j] < 0.25f * d_min_level)
                    P.xy_w[size_t(k) * layer + j] = 0.01f;
    };
    for (int outer = 0; outer < params.outer_iterations; ++outer) {
        if (rotary && outer > 0) {
            update_reference(2 * std::max(nx, ny));
            update_collapsed();
        }
        P.terms = conform;

        int active = 0;
        const float w_oh = float(params.w_overhang) * stencil;
        for (const DownSample &d : downs) {
            const double s = field.phi(Vec3d(d.c.x(), d.c.y(), d.zb + 0.05));
            const int    k = std::clamp(int(std::lround(s / ds)), k_fixed, nk - 2);

            const float  w = w_oh * d.w * float(1 << outer);
            if (d.u.squaredNorm() == 0.) {

                for (const Vec2d e : { Vec2d(p, 0.), Vec2d(0., p) }) {
                    Term t {};
                    add_bilinear(t, nx, ny, origin, p, k, d.c + e, 1.f);
                    add_bilinear(t, nx, ny, origin, p, k, d.c - e, -1.f);
                    t.rhs = 0.f;
                    t.w   = w;
                    P.terms.push_back(t);
                }
                ++active;
                continue;
            }

            Term t {};
            const float inv2d = float(0.5 / p), cs = float(cot_oh / ds) * d.az;
            add_bilinear(t, nx, ny, origin, p, k, d.c + p * d.u, inv2d);
            add_bilinear(t, nx, ny, origin, p, k, d.c - p * d.u, -inv2d);
            add_bilinear(t, nx, ny, origin, p, k + 1, d.c, cs);
            add_bilinear(t, nx, ny, origin, p, k, d.c, -cs);

            t.rhs = d.limit + (P.ref.empty() ? 0.f :
                                   inv2d * float(bilinear_level(P.ref, nx, ny, origin, p, k, d.c + p * d.u) -
                                                 bilinear_level(P.ref, nx, ny, origin, p, k, d.c - p * d.u)));
            t.w   = w;
            if (t.value(z) > t.rhs) {
                P.terms.push_back(t);
                ++active;
            }
        }

        cg_total += solve_cg(P, z, rotary ? 600 : 300, rotary ? 1e-6 : 1e-4);
        std::vector<float> before = z;
        project(z, P, cols, params, cap_grid, k_fixed);

        const float rho = 0.05f * stencil * float(std::pow(3., outer));
        size_t bound = 0;
        for (size_t i = 0; i < z.size(); ++i) {
            if (std::abs(before[i] - z[i]) > 1e-3f) {


                if (!rotary)
                    P.rho[i] = std::max(P.rho[i], rho);
                ++bound;
            }
            P.anchor[i] = z[i];
        }
        log << "outer " << outer << ": overhang terms " << active << ", projection bound " << bound << " nodes\n";
        if (params.throw_if_canceled)
            params.throw_if_canceled();
    }







    const size_t layer = size_t(nx) * ny;

    struct Facet { Vec3d n, ctr; int ix, iy, reach; bool bridge; };
    std::vector<Facet> fine, supported;
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d a = mesh.vertices[f[0]].cast<double>(), b = mesh.vertices[f[1]].cast<double>(), c = mesh.vertices[f[2]].cast<double>();
        Vec3d n = (b - a).cross(c - a);
        if (n.squaredNorm() <= 0.)
            continue;
        n.normalize();
        const Vec3d ctr = (a + b + c) / 3.;
        if (n.z() >= -0.05 || ctr.z() <= 0.05)
            continue;
        const int ix = std::clamp(int(std::lround((ctr.x() - origin.x()) / p)), 0, nx - 1);
        const int iy = std::clamp(int(std::lround((ctr.y() - origin.y()) / p)), 0, ny - 1);
        const bool bridge = std::hypot(n.x(), n.y()) < 0.2 && classify_ceiling(cols, ix, iy, ctr.z(), nullptr) == Ceiling::Bridge;

        const int reach = int(std::ceil(std::max({ (a - ctr).head<2>().norm(), (b - ctr).head<2>().norm(), (c - ctr).head<2>().norm() }) / p));



        if (std::asin(std::min(1., -n.z())) * 180. / PI > params.support_angle - 10.)
            supported.push_back({ n, ctr, ix, iy, reach, bridge });
        else if (ctr.z() > s_flat + 0.05)
            fine.push_back({ n, ctr, ix, iy, reach, bridge });
    }
    const std::vector<float> solved = z;
    auto blend = [&](bool freeze, StackLayerField &out) {
        const int r = int(std::ceil(5. / p));
        std::vector<float> alpha(layer, 1.f);
        std::vector<uint8_t> hard(layer, 0);
        if (freeze) {




            std::vector<uint8_t> still(layer, 0);
            for (const Facet &f : supported)
                if (f.bridge || deformed_overhang(f.n, field.gradient(f.ctr + Vec3d(0., 0., 0.02))) > params.support_angle - 10.)
                    for (int jy = std::max(0, f.iy - f.reach); jy <= std::min(ny - 1, f.iy + f.reach); ++jy)
                        for (int jx = std::max(0, f.ix - f.reach); jx <= std::min(nx - 1, f.ix + f.reach); ++jx)
                            still[size_t(jy) * nx + jx] = 1;
            std::vector<float> a0(layer, 1.f);
            zero_around(a0, still, nx, ny, int(std::ceil(7. / p)));
            box_blur(a0, nx, ny, int(std::ceil(4. / p)));
            std::vector<float> h(layer, 1.f);
            zero_around(h, still, nx, ny, int(std::ceil(3. / p)));
            for (size_t j = 0; j < layer; ++j) {
                alpha[j] = a0[j];
                hard[j]  = h[j] == 0.f;
            }
        }
        constexpr int rounds = 8;
        for (int round = 0; round < rounds; ++round) {
            if (round > 0 || freeze) {
                for (int k = 0; k < nk; ++k)
                    for (size_t j = 0; j < layer; ++j)
                        z[size_t(k) * layer + j] = alpha[j] * solved[size_t(k) * layer + j] + (1.f - alpha[j]) * float(k * ds);
                project(z, P, cols, params, cap_grid, k_fixed);
                for (int k = 0; k < nk; ++k)
                    for (size_t j = 0; j < layer; ++j)
                        if (hard[j])
                            z[size_t(k) * layer + j] = float(k * ds);
            }
            if (round == rounds - 1)
                break;
            std::vector<uint8_t> harm(layer, 0);
            bool any = false;
            for (const Facet &f : fine) {
                const Vec3d g = field.gradient(f.ctr + Vec3d(0., 0., 0.02));
                const bool worse = f.bridge ? std::acos(std::clamp(g.normalized().z(), -1., 1.)) > 5. * PI / 180. :
                                              deformed_overhang(f.n, g) > params.support_angle;
                if (worse) {
                    harm[size_t(f.iy) * nx + f.ix] = 1;
                    any = true;
                }
            }
            if (!any)
                break;
            std::vector<float> a0 = alpha;
            zero_around(a0, harm, nx, ny, r);
            box_blur(a0, nx, ny, r);
            for (size_t j = 0; j < layer; ++j)
                alpha[j] = std::min(alpha[j], a0[j]);
            log << (freeze ? "freeze" : "do no harm") << " round " << round << ": min weight "
                << *std::min_element(alpha.begin(), alpha.end()) << "\n";
        }
        out = field;
        z   = solved;
    };
    if (blended)
        blend(false, *blended);
    if (frozen)
        blend(true, *frozen);
    const auto t1 = Clock::now();
    if (report) {
        report->solve_seconds = std::chrono::duration<double>(t1 - t0).count();
        report->columns = nx * ny;
        report->levels = nk;
        std::ostringstream head;
        head << "grid " << nx << " x " << ny << " x " << nk << " (pitch " << p << ", ds " << ds << "), " << tops.size()
             << " top samples, " << downs.size() << " overhang samples, cap " << cap * 180. / PI
             << " deg; prep " << std::chrono::duration<double>(t_prep - t0).count() << " s, " << cg_total << " CG iterations\n";
        report->log = head.str() + log.str();
    }
    return field;
}

StackLayerField solve_layer_field(const indexed_triangle_set &mesh, const LayerFieldParams &params, LayerFieldReport *report)
{
    const auto t0 = Clock::now();
    BoundingBoxf3 bb;
    for (const Vec3f &v : mesh.vertices)
        bb.merge(v.cast<double>());
    if (!bb.defined || bb.max.z() <= 0.)
        return StackLayerField::planar(BoundingBoxf3(Vec3d::Zero(), Vec3d::Ones()));
    if (params.tool_axis) {


        StackLayerField f = solve_single(mesh, bb, params, report);
        if (report) {
            report->solve_seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            report->log = "rotary: layers normal to the tool axis\n" + report->log;
        }
        return f;
    }











    const std::pair<double, double> ladder[] = { { 1., 0. }, { 1., 4. }, { 0., 0. } };
    constexpr int n_solve = int(sizeof(ladder) / sizeof(ladder[0])), n = 3 * n_solve;
    std::vector<StackLayerField>  fields(n);
    std::vector<LayerFieldReport> reports(n);
    tbb::parallel_for(0, n_solve, [&](int i) {
        LayerFieldParams pi = params;
        pi.w_conform *= ladder[i].first;
        pi.conform_clear_radius = ladder[i].second;
        fields[3 * i] = solve_single(mesh, bb, pi, &reports[3 * i], &fields[3 * i + 1], &fields[3 * i + 2]);
        reports[3 * i + 1] = reports[3 * i + 2] = reports[3 * i];
    });
    const double q = std::clamp(std::max(bb.size().x(), bb.size().y()) / 128., 0.5, 1.5);
    const Columns cols = rasterize(mesh, Vec2d(bb.min.x(), bb.min.y()), q, int(std::ceil(bb.size().x() / q)) + 1,
                                   int(std::ceil(bb.size().y() / q)) + 1);
    const StackLayerField planar = StackLayerField::planar(bb);
    std::vector<SupportFeatures> feat(n + 1);
    tbb::parallel_for(0, n + 1, [&](int i) {
        feat[i] = support_features(mesh, i < n ? fields[i] : planar, cols, params.support_angle, params.flat_base_height);
    });
    const SupportFeatures &fp = feat[n];
    auto same_or_less = [&](const SupportFeatures &f) {
        return f.area_height <= fp.area_height + 1e-3 && f.perimeter_height <= fp.perimeter_height + 1e-3 &&
               f.region_height <= fp.region_height + 1e-3;
    };
    auto clearly_less = [&](const SupportFeatures &f) {
        return fp.perimeter_height > 0. && f.perimeter_height <= 0.85 * fp.perimeter_height && f.area_height <= 0.85 * fp.area_height &&
               f.region_height <= 1.2 * fp.region_height;
    };

    auto halves = [&](const SupportFeatures &f) {
        return fp.perimeter_height > 0. && f.perimeter_height <= 0.5 * fp.perimeter_height && f.area_height <= 0.5 * fp.area_height &&
               f.region_height <= fp.region_height;
    };
    int pick = n;
    for (int i = 0; i < n; ++i)
        if (i % 3 != 2 && halves(feat[i]) && (pick == n || feat[i].perimeter_height < feat[pick].perimeter_height))
            pick = i;
    for (int i = 2; pick == n && i < n; i += 3)
        if (clearly_less(feat[i]) && (pick == n || feat[i].perimeter_height < feat[pick].perimeter_height))
            pick = i;
    for (int i = 2; pick == n && i < n; i += 3)
        if (same_or_less(feat[i]))
            pick = i;


    if (pick != n && pick % 3 == 2 && top_rms_angle(fields[pick], cols) > top_rms_angle(planar, cols) - 2.)
        pick = n;
    std::vector<double> est(n + 1);
    for (int i = 0; i <= n; ++i)
        est[i] = estimate_support(feat[i]);
    std::ostringstream log;
    log << "support estimate planar " << est[n] << " (P " << fp.perimeter_height << ")";
    auto name = [&](int i) {
        return i == n ? std::string("planar") :
                        "conformity x" + std::to_string(ladder[i / 3].first).substr(0, 3) + " clear " +
                            std::to_string(ladder[i / 3].second).substr(0, 3) + (i % 3 == 1 ? " blended" : i % 3 == 2 ? " frozen" : "");
    };
    for (int i = 0; i < n; ++i)
        log << ", " << name(i) << " " << est[i] << " (P " << feat[i].perimeter_height << ")";
    log << "; chose " << name(pick) << "\n";
    if (report) {
        *report = reports[pick == n ? 0 : pick];
        report->solve_seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        report->log = log.str() + report->log;
    }
    BOOST_LOG_TRIVIAL(debug) << "layer field: " << log.str();
    if (pick == n) {

        StackLayerField f = fields[0];
        f.reset(f.origin(), f.pitch(), f.nx(), f.ny(), f.level_spacing(), f.levels(), f.identity_below());
        return f;
    }
    return std::move(fields[pick]);
}




LayerFieldMetrics evaluate_layer_field(const indexed_triangle_set &mesh, const LayerField &field,
                                       const LayerFieldParams &params, double q, int head_check_every)
{
    LayerFieldMetrics m;
    BoundingBoxf3 bb;
    for (const Vec3f &v : mesh.vertices)
        bb.merge(v.cast<double>());
    if (!bb.defined)
        return m;
    const Vec2d origin(bb.min.x(), bb.min.y());
    const int   nx = int(std::ceil(bb.size().x() / q)) + 1, ny = int(std::ceil(bb.size().y() / q)) + 1;
    const Columns cols = rasterize(mesh, origin, q, nx, ny);
    const double cap = layer_field_slope_cap(params);
    m.slope_cap = cap;
    const double flat = 2. * PI / 180., conf = 3. * PI / 180.;
    double top_in_cap = 0., top_conf = 0., top_sq = 0.;
    std::vector<std::pair<double, double>> slopes;
    std::vector<std::pair<double, double>> oh;
    std::vector<double> axis_dev;
    m.min_thickness = 1e9;
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            const Vec2d xy = cols.node(ix, iy);
            for (const Interval &it : cols.at(ix, iy)) {
                if (it.n_hi.z() > 0.f) {
                    const double s = slope_of(it.n_hi);
                    if (s > flat && s < 80. * PI / 180.) {
                        const double area = q * q / it.n_hi.z();
                        const Vec3d g = field.gradient(Vec3d(xy.x(), xy.y(), it.hi - 0.02)).normalized();
                        const double ang = std::acos(std::clamp(g.dot(it.n_hi.cast<double>()), -1., 1.));
                        m.curved_top_area += area;
                        top_in_cap += s <= cap ? area : 0.;
                        top_conf += ang <= conf ? area : 0.;
                        top_sq += area * ang * ang;
                    }
                }
                if (it.n_lo.z() < -0.02f && it.lo > params.flat_base_height + 0.05) {
                    const double area = q * q / -it.n_lo.z();
                    const Vec3d g_raw = field.gradient(Vec3d(xy.x(), xy.y(), it.lo + 0.02));
                    const Vec3d g = g_raw.normalized();

                    const Vec3d nd(it.n_lo.x() * g_raw.z() - it.n_lo.z() * g_raw.x(), it.n_lo.y() * g_raw.z() - it.n_lo.z() * g_raw.y(), it.n_lo.z());
                    double ang = std::asin(std::clamp(-nd.z() / nd.norm(), -1., 1.)) * 180. / PI;

                    if (std::hypot(it.n_lo.x(), it.n_lo.y()) < 0.2 && ang > params.overhang_angle &&
                        std::acos(std::clamp(g.z(), -1., 1.)) < 5. * PI / 180. &&
                        classify_ceiling(cols, ix, iy, it.lo, nullptr) == Ceiling::Bridge)
                        ang = 0.;
                    oh.emplace_back(ang, area);
                    if (ang > params.overhang_angle)
                        m.overhang_area += area;
                }
                if ((ix & 1) == 0 && (iy & 1) == 0)
                    for (double zz = it.lo + 0.1; zz < it.hi - 0.05; zz += 0.5) {
                        const Vec3d g = field.gradient(Vec3d(xy.x(), xy.y(), zz));
                        if (params.tool_axis && zz > params.flat_base_height) {
                            const Vec3d a = params.tool_axis(Vec3d(xy.x(), xy.y(), zz)).normalized();
                            axis_dev.push_back(std::acos(std::clamp(std::abs(g.normalized().dot(a)), 0., 1.)));
                        }
                        slopes.emplace_back(std::atan2(std::hypot(g.x(), g.y()), g.z()), 1.);
                        if (zz > params.flat_base_height) {
                            const double th = params.layer_height / std::max(g.z(), 1e-9);
                            m.min_thickness = std::min(m.min_thickness, th);
                            m.max_thickness = std::max(m.max_thickness, th);
                        }
                    }
            }
        }
    if (m.curved_top_area > 0.) {
        m.curved_top_in_cap = top_in_cap / m.curved_top_area;
        m.conformal_top     = top_conf / m.curved_top_area;
        m.top_rms_angle     = std::sqrt(top_sq / m.curved_top_area) * 180. / PI;
    }
    if (!oh.empty()) {
        std::sort(oh.begin(), oh.end());
        m.max_overhang = oh.back().first;
    }
    if (!slopes.empty()) {
        std::sort(slopes.begin(), slopes.end());
        m.max_field_slope = slopes.back().first;
        m.field_slope_p99 = slopes[size_t(0.99 * (slopes.size() - 1))].first;
    }
    if (m.min_thickness > 1e8)
        m.min_thickness = 0.;
    if (!axis_dev.empty()) {
        std::sort(axis_dev.begin(), axis_dev.end());
        double sum = 0.;
        for (double a : axis_dev)
            sum += a;
        m.axis_dev_mean = sum / axis_dev.size() * 180. / PI;
        m.axis_dev_p95  = axis_dev[size_t(0.95 * (axis_dev.size() - 1))] * 180. / PI;
        m.axis_within_3 = double(std::lower_bound(axis_dev.begin(), axis_dev.end(), 3. * PI / 180.) - axis_dev.begin()) / axis_dev.size();
    }

    m.support          = support_features(mesh, field, cols, params.support_angle, params.flat_base_height);
    m.support_estimate = estimate_support(m.support);
    if (head_check_every > 0 && params.head && !params.head->empty()) {
        const double h = params.layer_height;
        double max_phi = 0.;
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix)
                for (const Interval &it : cols.at(ix, iy))
                    max_phi = std::max(max_phi, field.phi(Vec3d(cols.node(ix, iy).x(), cols.node(ix, iy).y(), it.hi)));
        const int n_layers = int(std::ceil(max_phi / h)) + 1;
        const BoundingBoxf hb(origin, origin + q * Vec2d(nx - 1, ny - 1));
        std::vector<int> layers;
        for (int i = 2; i < n_layers; i += head_check_every)
            layers.push_back(i);
        std::vector<double> viol(layers.size(), 0.);
        std::vector<char>   used(layers.size(), 0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &r) {
            for (size_t li = r.begin(); li < r.end(); ++li) {
                const double s = layers[li] * h;
                HeightField cur(hb, q), prev(hb, q);
                bool any = false;
                for (int iy = 0; iy < ny; ++iy)
                    for (int ix = 0; ix < nx; ++ix) {
                        const Vec2d xy = cols.node(ix, iy);
                        const double zc = field.z_at(xy, s);
                        cur.at(ix, iy)  = cols.inside(ix, iy, zc) ? float(zc) : HeightField::NoData;
                        any |= !std::isnan(cur.at(ix, iy));
                        prev.at(ix, iy) = cols.top_below(ix, iy, field.z_at(xy, s - h));
                    }
                if (!any)
                    continue;
                used[li] = 1;

                const HeightField safe = min_safe_tip_height(prev, *params.head, cur, true);
                double a = 0.;
                for (size_t j = 0; j < cur.data().size(); ++j)
                    if (!std::isnan(cur.data()[j]) && cur.data()[j] + 1e-3f < safe.data()[j])
                        a += q * q;
                viol[li] = a;
            }
        });
        for (size_t li = 0; li < layers.size(); ++li) {
            m.head_violation_area += viol[li];
            m.head_layers_checked += used[li];
        }
    }
    return m;
}

}
