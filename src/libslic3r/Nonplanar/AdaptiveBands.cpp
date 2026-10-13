#include "AdaptiveBands.hpp"

#include "PatchTools.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>

namespace Slic3r::Nonplanar {

namespace {

constexpr double LevelFractions[] = { 0.2, 0.25, 0.3, 0.45, 0.6 };
constexpr double ZoneRadius = 1.;

HeightField smoothed(const HeightField &f)
{
    HeightField out = f;
    for (int iy = 0; iy < f.ny(); ++iy)
        for (int ix = 0; ix < f.nx(); ++ix) {
            if (std::isnan(f.at(ix, iy)))
                continue;
            double sum = 0.;
            int    n   = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = ix + dx, y = iy + dy;
                    if (x >= 0 && y >= 0 && x < f.nx() && y < f.ny() && !std::isnan(f.at(x, y))) {
                        sum += f.at(x, y);
                        ++n;
                    }
                }
            out.at(ix, iy) = float(sum / n);
        }
    return out;
}

double diff1(const HeightField &f, int ix, int iy, int dx, int dy)
{
    auto at = [&f](int x, int y) {
        return x < 0 || y < 0 || x >= f.nx() || y >= f.ny() ? std::numeric_limits<double>::quiet_NaN() : double(f.at(x, y));
    };
    const double c = at(ix, iy), a = at(ix + dx, iy + dy), b = at(ix - dx, iy - dy);
    if (!std::isnan(a) && !std::isnan(b))
        return (a - b) / (2. * f.pitch());
    if (!std::isnan(a))
        return (a - c) / f.pitch();
    if (!std::isnan(b))
        return (c - b) / f.pitch();
    return 0.;
}

double diff2(const HeightField &f, int ix, int iy, int dx, int dy)
{
    auto at = [&f](int x, int y) {
        return x < 0 || y < 0 || x >= f.nx() || y >= f.ny() ? std::numeric_limits<double>::quiet_NaN() : double(f.at(x, y));
    };
    const double c = at(ix, iy), a = at(ix + dx, iy + dy), b = at(ix - dx, iy - dy);
    return std::isnan(a) || std::isnan(b) ? 0. : (a - 2. * c + b) / (f.pitch() * f.pitch() * (dx * dx + dy * dy));
}

void filter_levels(std::vector<int> &lv, int nx, int ny, int r, bool take_min)
{
    auto pick = [take_min](int a, int b) { return a < 0 ? b : b < 0 ? a : take_min ? std::min(a, b) : std::max(a, b); };
    std::vector<int> tmp(lv.size(), -1);
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            if (lv[size_t(iy) * nx + ix] < 0)
                continue;
            int v = -1;
            for (int x = std::max(0, ix - r); x <= std::min(nx - 1, ix + r); ++x)
                v = pick(v, lv[size_t(iy) * nx + x]);
            tmp[size_t(iy) * nx + ix] = v;
        }
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            if (lv[size_t(iy) * nx + ix] < 0)
                continue;
            int v = -1;
            for (int y = std::max(0, iy - r); y <= std::min(ny - 1, iy + r); ++y)
                v = pick(v, tmp[size_t(y) * nx + ix]);
            lv[size_t(iy) * nx + ix] = v;
        }
}

}

double outer_bead_width(double height, double nozzle, double max_width)
{
    return std::clamp(2.5 * height, 0.5 * nozzle, std::max(max_width, height));
}

double outer_residual(double width, double slope, double curvature)
{
    return 0.5 * width * slope + 0.125 * curvature * width * width;
}

int AdaptiveStack::top_count(double h0) const
{
    const double rest = total - h0;
    return rest < EPSILON ? 1 : 1 + int(std::ceil(rest / max_height - 1e-9));
}

double AdaptiveStack::inner_height(double h0, int n) const
{
    return n > 1 ? (total - h0) / (n - 1) : 0.;
}

bool AdaptiveStack::layer(int k, const Vec2d &p, double &drop, double &bead, bool &deepest) const
{
    const Vec2d o  = (p - outer.origin()) / outer.pitch();
    const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
    const int   a  = this->shells() - k;
    if (ix < 0 || iy < 0 || ix >= outer.nx() || iy >= outer.ny() || a < 0 || a >= this->shells())
        return false;
    const float h0 = outer.at(ix, iy);
    if (std::isnan(h0))
        return false;
    int infill = below;
    if (!layers.empty()) {
        const float m = layers.at(ix, iy);
        if (std::isnan(m))
            return false;
        infill = std::clamp(int(m) - top_uniform, 0, below);
    }
    const int n = int(count.at(ix, iy));
    if (a == 0) {
        drop    = 0.;
        bead    = h0;
        deepest = n == 1 && infill == 0;
        return true;
    }
    if (a < slots) {
        const int r = a - (slots - n);
        if (r < 1)
            return false;
        bead    = inner_height(h0, n);
        drop    = h0 + (r - 1) * bead;
        deepest = r == n - 1 && infill == 0;
        return true;
    }
    const int i = a - slots;
    if (i >= infill)
        return false;
    drop    = total + i * shell_height;
    bead    = shell_height;
    deepest = i == infill - 1;
    return true;
}

void AdaptiveStack::make_uniform(size_t i)
{
    outer.data()[i] = float(shell_height);
    count.data()[i] = float(top_uniform);
    slots           = std::max(slots, top_uniform);
}

double AdaptiveStack::min_height() const
{
    double h = shell_height;
    for (size_t i = 0; i < outer.data().size(); ++i)
        if (const float h0 = outer.data()[i]; !std::isnan(h0)) {
            h = std::min(h, double(h0));
            if (const int n = int(count.data()[i]); n > 1)
                h = std::min(h, this->inner_height(h0, n));
        }
    return h;
}

double AdaptiveStack::nominal_drop(int k) const
{
    const int a = this->shells() - k;
    if (a >= slots)
        return total + (a - slots) * shell_height;
    const int n = this->top_count(dominant);
    const int r = std::max(1, a - (slots - n));
    return a == 0 ? 0. : dominant + (r - 1) * this->inner_height(dominant, n);
}

void AdaptiveStack::update_zones()
{
    std::map<float, Mask> masks;
    std::map<float, size_t> nodes;
    for (size_t i = 0; i < outer.data().size(); ++i)
        if (const float h0 = outer.data()[i]; !std::isnan(h0)) {
            Mask &m = masks[h0];
            m.resize(outer.data().size(), 0);
            m[i] = 1;
            ++nodes[h0];
        }
    levels.clear();
    zones.clear();
    size_t most = 0;
    for (const auto &[h0, mask] : masks) {
        levels.emplace_back(h0);
        const float     r   = float(scale_(2. * outer.pitch()));
        const double    tol = scale_(0.01);
        ExPolygons      z   = mask_to_expolygons(mask, outer);
        z = offset_ex(offset_ex(z, r, jtRound, tol), -r, jtRound, tol);
        z = offset_ex(offset_ex(z, -r, jtRound, tol), r, jtRound, tol);
        zones.emplace_back(expolygons_simplify(z, scale_(0.02)));
        if (nodes[h0] > most) {
            most     = nodes[h0];
            dominant = h0;
        }
    }
}

AdaptiveStack adaptive_stack(const HeightField &surface, const HeightField &layers, double total, double shell_height, int below,
                             double nozzle, double max_width, double max_error)
{
    AdaptiveStack st;
    st.total        = total;
    st.shell_height = shell_height;
    st.max_height   = AdaptiveMaxHeight * nozzle;
    st.top_uniform  = std::max(1, int(std::lround(total / shell_height)));
    st.below        = below;
    st.layers       = layers;

    const double     h_lo = AdaptiveMinHeight * nozzle;
    std::vector<double> cand;
    for (double f : LevelFractions)
        if (const double h = f * nozzle; f <= AdaptiveMaxOuterHeight + EPSILON && h <= total + EPSILON &&
                                          (total - h < EPSILON || total - h >= h_lo - EPSILON))
            cand.emplace_back(h);
    if (total <= AdaptiveMaxOuterHeight * nozzle + EPSILON)
        cand.emplace_back(total);
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end(), [](double a, double b) { return std::abs(a - b) < EPSILON; }), cand.end());
    if (cand.empty())
        cand.emplace_back(total);
    std::vector<double> width(cand.size());
    for (size_t j = 0; j < cand.size(); ++j)
        width[j] = outer_bead_width(cand[j], nozzle, max_width);

    const HeightField S  = smoothed(surface);
    const int         nx = S.nx(), ny = S.ny();
    std::vector<int>  lv(size_t(nx) * ny, -1);
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            if (std::isnan(S.at(ix, iy)))
                continue;
            const double gx = diff1(S, ix, iy, 1, 0), gy = diff1(S, ix, iy, 0, 1);
            const double hxx = diff2(S, ix, iy, 1, 0), hyy = diff2(S, ix, iy, 0, 1);
            const double hxy = 0.5 * (diff2(S, ix, iy, 1, 1) - diff2(S, ix, iy, 1, -1));
            const double kappa = 0.5 * std::abs(hxx + hyy) + std::sqrt(0.25 * (hxx - hyy) * (hxx - hyy) + hxy * hxy);
            const double slope = std::hypot(gx, gy);
            int          best = -1, least = 0;
            double       least_e = std::numeric_limits<double>::max();
            for (int j = int(cand.size()) - 1; j >= 0; --j) {
                const double e = outer_residual(width[size_t(j)], slope, kappa);
                if (e <= max_error) {
                    best = j;
                    break;
                }
                if (e < least_e) {
                    least_e = e;
                    least   = j;
                }
            }
            lv[size_t(iy) * nx + ix] = best >= 0 ? best : least;
        }
    const int r = std::max(1, int(std::ceil(ZoneRadius / S.pitch())));
    filter_levels(lv, nx, ny, r, true);
    filter_levels(lv, nx, ny, r, false);

    st.outer = surface;
    st.count = surface;
    for (size_t i = 0; i < lv.size(); ++i)
        if (lv[i] >= 0) {
            const double h0 = cand[size_t(lv[i])];
            st.outer.data()[i] = float(h0);
            st.count.data()[i] = float(st.top_count(h0));
            st.slots           = std::max(st.slots, st.top_count(h0));
        }
    pad_lowest(st.outer, 3);
    for (size_t i = 0; i < st.outer.data().size(); ++i)
        if (const float h0 = st.outer.data()[i]; !std::isnan(h0) && std::isnan(st.count.data()[i]))
            st.count.data()[i] = float(st.top_count(h0));
    st.update_zones();
    return st;
}

}
