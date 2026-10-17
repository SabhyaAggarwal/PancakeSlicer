#include "FlowMod.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>

namespace Slic3r::Nonplanar {

void FlowModStats::merge(const FlowModStats &rhs)
{
    points += rhs.points;
    max_clamped += rhs.max_clamped;
    min_clamped += rhs.min_clamped;
    dragged += rhs.dragged;
    contact += rhs.contact;
    min_gap  = std::min(min_gap, rhs.min_gap);
    max_gap  = std::max(max_gap, rhs.max_gap);
    min_laid = std::min(min_laid, rhs.min_laid);
    max_laid = std::max(max_laid, rhs.max_laid);
}

ShellDeficit::ShellDeficit(const HeightField &like) : value(like), distance(like.data().size(), std::numeric_limits<float>::infinity())
{
    std::fill(value.data().begin(), value.data().end(), 0.f);
}

float ShellDeficit::sample(const Vec2d &p) const
{
    const float v = value.sample(p);
    return std::isnan(v) ? 0.f : v;
}

namespace {

void deposit_deficit(ShellDeficit &field, const Vec2d &a, const Vec2d &b, double da, double db, double half_width)
{
    const HeightField &g     = field.value;
    const double       pitch = g.pitch();
    const Vec2d        lo    = a.cwiseMin(b) - Vec2d(half_width, half_width), hi = a.cwiseMax(b) + Vec2d(half_width, half_width);
    const int ix0 = std::max(0, int(std::ceil((lo.x() - g.origin().x()) / pitch))), ix1 = std::min(g.nx() - 1, int(std::floor((hi.x() - g.origin().x()) / pitch)));
    const int iy0 = std::max(0, int(std::ceil((lo.y() - g.origin().y()) / pitch))), iy1 = std::min(g.ny() - 1, int(std::floor((hi.y() - g.origin().y()) / pitch)));
    const Vec2d  ab  = b - a;
    const double len = ab.squaredNorm();
    for (int iy = iy0; iy <= iy1; ++iy)
        for (int ix = ix0; ix <= ix1; ++ix) {
            const Vec2d  p = g.node(ix, iy);
            const double t = len > 0. ? std::clamp((p - a).dot(ab) / len, 0., 1.) : 0.;
            const float  d = float((a + t * ab - p).norm());
            const size_t n = size_t(iy) * g.nx() + ix;
            if (d <= half_width && d < field.distance[n]) {
                field.distance[n]    = d;
                field.value.data()[n] = float((1. - t) * da + t * db);
            }
        }
}

void mark_simplified(const Points3 &pts, const std::vector<float> &heights, const std::vector<float> &widths,
                     size_t first, size_t last, double tolerance, std::vector<char> &keep)
{
    const double tol = scale_(tolerance);
    std::vector<std::pair<size_t, size_t>> stack { { first, last } };
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        const Vec3d  pa = pts[a].cast<double>(), ab = pts[b].cast<double>() - pa;
        const double len = ab.squaredNorm();
        double       worst = 0.;
        size_t       worst_idx = a;
        for (size_t i = a + 1; i < b; ++i) {
            const double t  = len > 0. ? std::clamp((pts[i].cast<double>() - pa).dot(ab) / len, 0., 1.) : 0.;
            double       d  = std::max((pa + t * ab - pts[i].cast<double>()).norm(),
                                       std::abs(scale_(heights[i] - (1. - t) * heights[a] - t * heights[b])));
            if (!widths.empty())
                d = std::max(d, std::abs(scale_(widths[i] - (1. - t) * widths[a] - t * widths[b])));
            if (d > worst) {
                worst     = d;
                worst_idx = i;
            }
        }
        if (worst > tol) {
            keep[worst_idx] = 1;
            stack.emplace_back(a, worst_idx);
            stack.emplace_back(worst_idx, b);
        }
    }
}

}

void modulate_flow(ExtrusionPathConformal &path, const HeightField &surface, const ShellDeficit *below, ShellDeficit *deficit,
                   const FlowModParams &params, FlowModStats &stats)
{
    if (path.polyline.points.size() < 2 || !path.tool_axis.empty())
        return;

    Points3            pts;
    std::vector<float> h, widths;
    const bool         variable_width = path.bead_width.size() == path.polyline.points.size();
    {
        const Points3            &src = path.polyline.points;
        const std::vector<float> &bh  = path.bead_height;
        pts.reserve(src.size());
        h.reserve(src.size());
        if (variable_width)
            widths.reserve(src.size());
        for (size_t i = 0; i < src.size(); ++i) {
            if (i > 0 && bh[i - 1] > 0.f && bh[i] > 0.f) {
                const Vec3d a = src[i - 1].cast<double>(), b = src[i].cast<double>();
                const int   n = int(std::ceil(unscale<double>((b - a).norm()) / params.resample));
                for (int j = 1; j < n; ++j) {
                    const double t = double(j) / n;
                    const Vec3d  q = a + t * (b - a);
                    pts.emplace_back(coord_t(std::lround(q.x())), coord_t(std::lround(q.y())), coord_t(std::lround(q.z())));
                    h.emplace_back(float((1. - t) * bh[i - 1] + t * bh[i]));
                    if (variable_width)
                        widths.emplace_back(float((1. - t) * path.bead_width[i - 1] + t * path.bead_width[i]));
                }
            }
            pts.emplace_back(src[i]);
            h.emplace_back(bh[i]);
            if (variable_width)
                widths.emplace_back(path.bead_width[i]);
        }
    }

    const double       width = path.width;
    const size_t       n     = pts.size();
    std::vector<char>  keep(n, 1);
    std::vector<Vec2d> xy(n);
    std::vector<double> nz(n, 1.), gap(n, 0.), s(n, 0.);
    for (size_t i = 0; i < n; ++i)
        xy[i] = unscale(pts[i].to_point());

    for (size_t b = 0; b < n;) {
        if (h[b] <= 0.f) {
            ++b;
            continue;
        }
        size_t e = b;
        while (e + 1 < n && h[e + 1] > 0.f)
            ++e;
        if (e == b) {
            ++b;
            continue;
        }

        for (size_t i = b; i <= e; ++i) {
            s[i]    = i == b ? 0. : s[i - 1] + (xy[i] - xy[i - 1]).norm();
            Vec2d t = xy[std::min(i + 1, e)] - xy[i > b ? i - 1 : b];
            t       = t.squaredNorm() > 0. ? Vec2d(t.normalized()) : Vec2d(1., 0.);
            Vec2d g = surface.gradient(xy[i]);
            if (std::isnan(g.x()) || std::isnan(g.y()))
                g = Vec2d::Zero();
            nz[i]  = 1. / std::sqrt(1. + g.squaredNorm());
            gap[i] = std::max(0., double(h[i]) + (below != nullptr ? below->sample(xy[i]) : 0.));
            stats.dragged += params.land_radius * std::max(0., -g.dot(t)) > 0.01;
            stats.contact += gap[i] < 0.5 * width * std::abs(g.dot(Vec2d(-t.y(), t.x())));
            stats.min_gap = std::min(stats.min_gap, gap[i] * nz[i]);
            stats.max_gap = std::max(stats.max_gap, gap[i] * nz[i]);
        }

        std::vector<double> smooth(e - b + 1);
        const double        half = 0.5 * params.smoothing;
        size_t              lo = b, hi = b;
        double              sum = 0., wsum = 0.;
        auto weight = [&](size_t j) { return 0.5 * ((j > b ? s[j] - s[j - 1] : 0.) + (j < e ? s[j + 1] - s[j] : 0.)) + 1e-9; };
        for (size_t i = b; i <= e; ++i) {
            while (hi <= e && s[hi] <= s[i] + half) {
                sum += weight(hi) * gap[hi];
                wsum += weight(hi);
                ++hi;
            }
            while (s[lo] < s[i] - half) {
                sum -= weight(lo) * gap[lo];
                wsum -= weight(lo);
                ++lo;
            }
            smooth[i - b] = wsum > 0. ? sum / wsum : gap[i];
        }
        for (size_t i = b; i <= e; ++i) {
            double v = smooth[i - b];
            if (v * nz[i] < params.min_normal) {
                v = params.min_normal / nz[i];
                ++stats.min_clamped;
            }
            if (v > params.max_height) {
                v = params.max_height;
                ++stats.max_clamped;
            }
            h[i] = float(v);
            stats.min_laid = std::min(stats.min_laid, v * nz[i]);
            stats.max_laid = std::max(stats.max_laid, v * nz[i]);
            ++stats.points;
        }
        if (deficit != nullptr)
            for (size_t i = b; i < e; ++i)
                deposit_deficit(*deficit, xy[i], xy[i + 1], gap[i] - h[i], gap[i + 1] - h[i + 1], 0.5 * width);

        std::fill(keep.begin() + b + 1, keep.begin() + e, 0);
        mark_simplified(pts, h, widths, b, e, 0.01, keep);
        b = e + 1;
    }

    Points3            out_pts;
    std::vector<float> out_h, out_widths;
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) {
            out_pts.emplace_back(pts[i]);
            out_h.emplace_back(h[i]);
            if (variable_width)
                out_widths.emplace_back(widths[i]);
        }
    path.polyline.points = std::move(out_pts);
    path.bead_height     = std::move(out_h);
    path.bead_width      = std::move(out_widths);
}

}
