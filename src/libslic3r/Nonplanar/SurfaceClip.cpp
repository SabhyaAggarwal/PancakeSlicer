#include "SurfaceClip.hpp"

#include "libslic3r/Exception.hpp"
#include "libslic3r/libslic3r.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr float FInf = std::numeric_limits<float>::infinity();

struct MinOp
{
    static constexpr float identity = FInf;
    static float apply(float a, float b) { return a < b ? a : b; }
};

struct MaxOp
{
    static constexpr float identity = -FInf;
    static float apply(float a, float b) { return a > b ? a : b; }
};

struct KernelBox
{
    int   i0 = 0, i1 = 0, j0 = 0, j1 = 0;
    bool  inf_x = false, inf_y = false;
    int   component = -1;
    float z = 0.f;

    bool same_window(const KernelBox &o) const
    {
        return inf_x == o.inf_x && inf_y == o.inf_y && (inf_x || (i0 == o.i0 && i1 == o.i1)) && (inf_y || (j0 == o.j0 && j1 == o.j1));
    }
};

std::vector<KernelBox> kernel_boxes(const HeadEnvelope &head, double pitch, bool apply_allowance)
{
    constexpr double       eps = 1e-6;
    std::vector<KernelBox> out;
    auto offset = [](double value) {
        const double limit = std::numeric_limits<int>::max();
        if (!std::isfinite(value) || value < -limit || value > limit)
            throw RuntimeError("Head envelope reach is too large for the non-planar grid. Check the head dimensions or increase the grid spacing.");
        return int(value);
    };
    for (size_t component = 0; component < head.boxes.size(); ++component) {
        const HeadBox &box = head.boxes[component];
        const double grow = box.nozzle ? 0. : head.lateral_margin;
        double       zmin = box.min.z();
        if (apply_allowance && box.nozzle) {
            const double r = std::hypot(std::max(std::abs(box.min.x()), std::abs(box.max.x())),
                                        std::max(std::abs(box.min.y()), std::abs(box.max.y())));
            if (r <= head.tip_allowance.radius)
                zmin = std::max(zmin, head.tip_allowance.height);
            if (zmin >= box.max.z())
                continue;
        }
        KernelBox k;
        k.component = int(component);
        k.inf_x = box.infinite_x;
        k.inf_y = box.infinite_y;
        k.i0    = k.inf_x ? 0 : offset(std::ceil((box.min.x() - grow) / pitch - eps));
        k.i1    = k.inf_x ? 0 : offset(std::floor((box.max.x() + grow) / pitch + eps));
        k.j0    = k.inf_y ? 0 : offset(std::ceil((box.min.y() - grow) / pitch - eps));
        k.j1    = k.inf_y ? 0 : offset(std::floor((box.max.y() + grow) / pitch + eps));
        if ((!k.inf_x && k.i0 > k.i1) || (!k.inf_y && k.j0 > k.j1))
            continue;
        const double height = zmin - (box.nozzle ? 0. : head.margin);
        if (!std::isfinite(height) || std::abs(height) > std::numeric_limits<float>::max())
            throw RuntimeError("Head envelope clearance height is too large. Check the head dimensions and margin.");
        k.z = float(height);
        auto it = std::find_if(out.begin(), out.end(), [&k](const KernelBox &o) { return o.same_window(k); });
        if (it == out.end())
            out.emplace_back(k);
        else if (k.z < it->z)
            *it = k;
    }
    return out;
}

template<class Op>
void sliding_lines(const float *in, int n, int lanes, int a, int b, float *out, std::vector<float> &g, std::vector<float> &h)
{
    a = std::max(a, -(n - 1));
    b = std::min(b, n - 1);
    if (a > b) {
        std::fill(out, out + size_t(n) * lanes, Op::identity);
        return;
    }
    const int64_t w = int64_t(b) - a + 1;
    const int64_t L = int64_t(n) + w - 1;
    g.resize(size_t(L) * lanes);
    h.resize(size_t(L) * lanes);
    auto src = [&](int64_t m, int lane) {
        const int64_t idx = m + a;
        return idx >= 0 && idx < n ? in[size_t(idx) * lanes + lane] : Op::identity;
    };
    for (int64_t m = 0; m < L; ++m)
        for (int l = 0; l < lanes; ++l)
            g[size_t(m) * lanes + l] = m % w == 0 ? src(m, l) : Op::apply(g[size_t(m - 1) * lanes + l], src(m, l));
    for (int64_t m = L - 1; m >= 0; --m)
        for (int l = 0; l < lanes; ++l)
            h[size_t(m) * lanes + l] = (m % w == w - 1 || m == L - 1) ? src(m, l) : Op::apply(h[size_t(m + 1) * lanes + l], src(m, l));
    for (int k = 0; k < n; ++k)
        for (int l = 0; l < lanes; ++l)
            out[size_t(k) * lanes + l] = Op::apply(h[size_t(k) * lanes + l], g[size_t(k + w - 1) * lanes + l]);
}

template<class Op>
std::vector<float> rect_sliding(const std::vector<float> &in, int nx, int ny, int a, int b, bool inf_x, int c, int d, bool inf_y)
{
    std::vector<float> tmp(in.size()), out(in.size());
    if (inf_y)
        c = -(ny - 1), d = ny - 1;
    if (inf_x)
        a = -(nx - 1), b = nx - 1;
    constexpr int block = 64;
    tbb::parallel_for(tbb::blocked_range<int>(0, int((int64_t(nx) + block - 1) / block)), [&](const tbb::blocked_range<int> &range) {
        std::vector<float> lane_in, lane_out, g, h;
        for (int blk = range.begin(); blk < range.end(); ++blk) {
            const int x0 = blk * block, lanes = std::min(block, nx - x0);
            lane_in.resize(size_t(ny) * lanes);
            lane_out.resize(size_t(ny) * lanes);
            for (int y = 0; y < ny; ++y)
                std::copy_n(&in[size_t(y) * nx + x0], lanes, &lane_in[size_t(y) * lanes]);
            sliding_lines<Op>(lane_in.data(), ny, lanes, c, d, lane_out.data(), g, h);
            for (int y = 0; y < ny; ++y)
                std::copy_n(&lane_out[size_t(y) * lanes], lanes, &tmp[size_t(y) * nx + x0]);
        }
    });
    tbb::parallel_for(tbb::blocked_range<int>(0, ny), [&](const tbb::blocked_range<int> &range) {
        std::vector<float> g, h;
        for (int y = range.begin(); y < range.end(); ++y)
            sliding_lines<Op>(&tmp[size_t(y) * nx], nx, 1, a, b, &out[size_t(y) * nx], g, h);
    });
    return out;
}

bool minorant_pass(std::vector<float> &cur, int nx, int ny, const std::vector<KernelBox> &boxes, std::vector<int> *limiting_components)
{
    bool changed = false;
    for (const KernelBox &k : boxes) {
        std::vector<float> src(cur.size());
        std::transform(cur.begin(), cur.end(), src.begin(), [](float v) { return std::isnan(v) ? FInf : v; });
        const std::vector<float> w = rect_sliding<MinOp>(src, nx, ny, -k.i1, -k.i0, k.inf_x, -k.j1, -k.j0, k.inf_y);
        const bool  self = (k.inf_x || (k.i0 <= 0 && 0 <= k.i1)) && (k.inf_y || (k.j0 <= 0 && 0 <= k.j1));
        const float z    = self ? std::max(k.z, 0.f) : k.z;
        for (size_t i = 0; i < cur.size(); ++i)
            if (!std::isnan(cur[i]) && w[i] + z < cur[i]) {
                cur[i]  = w[i] + z;
                changed = true;
                if (limiting_components)
                    (*limiting_components)[i] = k.component;
            }
    }
    return changed;
}

}

HeightField head_lipschitz_minorant(const HeightField &S, const HeadEnvelope &head, int max_iterations,
                                    const std::function<void()> &throw_if_canceled)
{
    return head_lipschitz_minorant(S, head, max_iterations, throw_if_canceled, nullptr);
}

HeightField head_lipschitz_minorant(const HeightField &S, const HeadEnvelope &head, int max_iterations,
                                    const std::function<void()> &throw_if_canceled, std::vector<int> *limiting_components)
{
    HeightField out = S;
    if (limiting_components)
        limiting_components->assign(S.data().size(), -1);
    if (S.empty())
        return out;
    const std::vector<KernelBox> boxes = kernel_boxes(head, S.pitch(), true);
    for (int it = 0; it < max_iterations; ++it) {
        if (throw_if_canceled)
            throw_if_canceled();
        if (!minorant_pass(out.data(), S.nx(), S.ny(), boxes, limiting_components))
            break;
    }
    return out;
}

HeightField min_safe_tip_height(const HeightField &material, const HeadEnvelope &head, const HeightField &like)
{
    return min_safe_tip_height(material, head, like, nullptr, false);
}

HeightField min_safe_tip_height(const HeightField &material, const HeadEnvelope &head, const HeightField &like,
                                std::vector<int> *limiting_components, bool apply_tip_allowance)
{
    HeightField out = like;
    if (limiting_components)
        limiting_components->assign(like.data().size(), -1);
    if (like.empty())
        return out;
    const double pitch = like.pitch();
    int ox = 0, oy = 0, nx = like.nx(), ny = like.ny();
    if (!material.empty()) {
        const Vec2d off = (material.origin() - like.origin()) / pitch;
        const int   mx = int(std::lround(off.x())), my = int(std::lround(off.y()));
        ox = std::min(0, mx);
        oy = std::min(0, my);
        nx = std::max(like.nx(), mx + material.nx()) - ox;
        ny = std::max(like.ny(), my + material.ny()) - oy;
    }
    std::vector<float> src(size_t(nx) * ny, -FInf);
    if (!material.empty()) {
        const Vec2d off = (material.origin() - like.origin()) / pitch;
        const int   mx = int(std::lround(off.x())) - ox, my = int(std::lround(off.y())) - oy;
        for (int y = 0; y < material.ny(); ++y)
            for (int x = 0; x < material.nx(); ++x) {
                const float v = material.at(x, y);
                if (!std::isnan(v))
                    src[size_t(y + my) * nx + x + mx] = v;
            }
    }
    std::vector<float> best(src.size(), -FInf);
    std::vector<int> binding(limiting_components ? src.size() : 0, -1);
    for (const KernelBox &k : kernel_boxes(head, pitch, apply_tip_allowance)) {
        const std::vector<float> w = rect_sliding<MaxOp>(src, nx, ny, k.i0, k.i1, k.inf_x, k.j0, k.j1, k.inf_y);
        for (size_t i = 0; i < best.size(); ++i)
            if (w[i] - k.z > best[i] ||
                (limiting_components && std::isfinite(best[i]) && w[i] - k.z == best[i] && k.component < binding[i])) {
                best[i] = w[i] - k.z;
                if (limiting_components)
                    binding[i] = k.component;
            }
    }
    for (int y = 0; y < like.ny(); ++y)
        for (int x = 0; x < like.nx(); ++x) {
            const size_t i = size_t(y - oy) * nx + x - ox;
            out.at(x, y) = std::isnan(like.at(x, y)) ? HeightField::NoData : best[i];
            if (limiting_components && !std::isnan(like.at(x, y)))
                (*limiting_components)[size_t(y) * like.nx() + x] = binding[i];
        }
    return out;
}

HeightField slope_cap_minorant(const HeightField &S, double max_slope)
{
    HeightField out = S;
    if (S.empty() || max_slope >= 0.5 * PI)
        return out;
    const double t     = std::tan(std::max(0., max_slope)) * std::cos(0.5 * std::atan(0.5));
    const int    nx    = S.nx(), ny = S.ny();
    static const int steps[][2] = { { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { -2, -1 }, { -1, -2 }, { 1, -2 }, { 2, -1 } };
    float            weight[8];
    for (int s = 0; s < 8; ++s)
        weight[s] = float(t * S.pitch() * std::hypot(steps[s][0], steps[s][1]));
    std::vector<float> &z = out.data();
    auto relax = [&](int x, int y, int sx, int sy, float w) {
        const int px = x + sx, py = y + sy;
        if (px < 0 || py < 0 || px >= nx || py >= ny)
            return false;
        float &zq = z[size_t(y) * nx + x];
        const float zp = z[size_t(py) * nx + px];
        if (std::isnan(zq) || std::isnan(zp) || zp + w >= zq)
            return false;
        zq = zp + w;
        return true;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                for (int s = 0; s < 8; ++s)
                    changed |= relax(x, y, steps[s][0], steps[s][1], weight[s]);
        for (int y = ny - 1; y >= 0; --y)
            for (int x = nx - 1; x >= 0; --x)
                for (int s = 0; s < 8; ++s)
                    changed |= relax(x, y, -steps[s][0], -steps[s][1], weight[s]);
    }
    return out;
}

HeightField apply_surface_cap(const HeightField &S, const HeightField &cap)
{
    assert(S.nx() == cap.nx() && S.ny() == cap.ny());
    HeightField out = S;
    for (size_t i = 0; i < out.data().size(); ++i)
        if (!std::isnan(cap.data()[i]) && !std::isnan(out.data()[i]))
            out.data()[i] = std::min(out.data()[i], cap.data()[i]);
    return out;
}

}
