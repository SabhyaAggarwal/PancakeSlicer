#include "MutablePolygon.hpp"
#include "Point.hpp"
#include "libslic3r.h"
#include <cmath>
#include <cstdint>
#include <cassert>
#include <utility>

namespace Slic3r {

void remove_duplicates(MutablePolygon &polygon)
{
    if (! polygon.empty()) {
        auto begin = polygon.begin();
        auto it    = begin;
        for (++ it; it != begin;) {
            auto prev = it.prev();
            if (*prev == *it)
                it = it.remove();
            else
                ++ it;
        }
    }
}

void remove_duplicates(MutablePolygon &polygon, double eps)
{
    if (! polygon.empty()) {
        auto eps2 = eps * eps;
        auto begin = polygon.begin();
        auto it = begin;
        for (++ it; it != begin;) {
            auto prev = it.prev();
            if ((*it - *prev).cast<double>().squaredNorm() < eps2)
                it = it.remove();
            else
                ++ it;
        }
    }
}

void remove_duplicates(MutablePolygon& polygon, coord_t scaled_eps, const double max_angle)
{
    if (polygon.size() >= 3) {
        auto cos_max_angle_2 = Slic3r::sqr<double>(cos(max_angle));
        auto scaled_eps_sqr = Slic3r::sqr<int64_t>(scaled_eps);
        auto begin = polygon.begin();
        auto it = begin;
        for (++it; it != begin;) {
            auto    prev = it.prev();
            auto    next = it.next();
            Vec2i64 v1 = (*it - *prev).cast<int64_t>();
            int64_t v1_sqr_norm = v1.squaredNorm();
            if (v1_sqr_norm < scaled_eps_sqr) {
                if (Vec2i64 v2 = (*next - *prev).cast<int64_t>();
                    Slic3r::sqr<double>(double(v1.dot(v2))) > cos_max_angle_2 * double(v1_sqr_norm) * double(v2.squaredNorm())) {
                    it = it.remove();
                    continue;
                }
            }
            it = next;
        }
    }

    if (polygon.size() < 3)
        polygon.clear();
}

static bool clip_narrow_corner(
    const Vec2i64               p1, 
    MutablePolygon::iterator   &it0, 
    MutablePolygon::iterator   &it2,
    MutablePolygon::range      &unprocessed_range,
    int64_t                     dist2_current, 
    const int64_t               shortcut_length)
{
    MutablePolygon &polygon = it0.polygon();
    assert(polygon.size() >= 2);

    const int64_t shortcut_length2 = sqr(shortcut_length);

    enum Status {
        Free,
        Blocked,
        Far,
    };
    Status  forward  = Free;
    Status  backward = Free;

    Vec2i64 p0 = it0->cast<int64_t>();
    Vec2i64 p2 = it2->cast<int64_t>();
    Vec2i64 p02;
    Vec2i64 p22;
    int64_t dist2_next = 0;

    while (polygon.size() >= 3) {
        assert(dist2_current <= shortcut_length2);
        if (forward == Far && backward == Far) {
            p02 = it0.prev()->cast<int64_t>();
            p22 = it2.next()->cast<int64_t>();
            auto d2 = (p22 - p02).squaredNorm();
            if (d2 <= shortcut_length2) {
                it0 = unprocessed_range.remove_back(it0).prev();
                it2 = unprocessed_range.remove_front(it2);
                if (polygon.size() <= 2)
                    return true;
                forward       = Free;
                backward      = Free;
                dist2_current = d2;
                p0            = p02;
                p2            = p22;
            } else {
                dist2_next    = d2;
                break;
            }
        } else if (forward != Free && backward != Free)
            break;
        if (forward == Free && (backward != Free || (p2 - p1).squaredNorm() < (p0 - p1).cast<int64_t>().squaredNorm())) {
            p22 = it2.next()->cast<int64_t>();
            if (cross2(p2 - p0, p22 - p0) > 0)
                forward = Blocked;
            else {
                auto d2 = (p22 - p0).squaredNorm();
                if (d2 > shortcut_length2) {
                    forward    = Far;
                    dist2_next = d2;
                } else {
                    forward    = Free;
                    it2        = unprocessed_range.remove_front(it2);
                    p2         = p22;
                    dist2_current = d2;
                }
            }
        } else {
            assert(backward == Free);
            p02 = it0.prev()->cast<int64_t>();
            if (cross2(p02 - p2, p0 - p2) > 0)
                backward = Blocked;
            else {
                auto d2 = (p2 - p02).squaredNorm();
                if (d2 > shortcut_length2) {
                    backward   = Far;
                    dist2_next = d2;
                } else {
                    backward   = Free;
                    it0        = unprocessed_range.remove_back(it0).prev();
                    p0         = p02;
                    dist2_current = d2;
                }
            }
        }
    }

    assert(dist2_current <= shortcut_length2);
    assert(polygon.size() >= 2);
    assert(polygon.size() == 2 || forward  == Blocked || forward  == Far);
    assert(polygon.size() == 2 || backward == Blocked || backward == Far);

    if (polygon.size() <= 3) {
#ifndef NDEBUG
        bool blocked = forward == Blocked || backward == Blocked;
        assert(polygon.size() < 3 || 
            blocked ||
            (forward == Far && backward == Far));
        if (polygon.size() == 3) {
            p02 = it0.prev()->cast<int64_t>();
            p22 = it2.next()->cast<int64_t>();
            assert(p02 == p22);
            auto orient1 = cross2(p02 - p2, p0 - p2);
            auto orient2 = cross2(p2 - p0, p22 - p0);
            assert(orient1 > 0 == blocked);
            assert(orient2 > 0 == blocked);
        }
#endif
        if (polygon.size() < 3 || (forward == Far && backward == Far)) {
            polygon.clear();
        } else {
            assert(forward == Blocked || backward == Blocked);
        }
        return true;
    }

    assert(dist2_current <= shortcut_length2);
    if ((forward == Blocked && backward == Blocked) || dist2_current > sqr(shortcut_length - int64_t(SCALED_EPSILON))) {
    } else if (dist2_next < sqr(shortcut_length - int64_t(SCALED_EPSILON))) {
        if (forward == Far)
            it0 = unprocessed_range.remove_back(it0).prev();
        if (backward == Far)
            it2 = unprocessed_range.remove_front(it2);
        if (polygon.size() <= 2)
            return true;
    } else if (forward == Blocked || backward == Blocked) {
        assert(forward == Far || backward == Far);
        if (forward == Far) {
            std::swap(p0,  p2);
            std::swap(p02, p22);
        }
#ifndef NDEBUG
        auto dfar2 = (p02 - p2).squaredNorm();
        assert(dfar2 >= shortcut_length2);
#endif
        const Vec2d     v = (p02 - p0).cast<double>();
        const Vec2d     d = (p0 - p2).cast<double>();
        const double    a = v.squaredNorm();
        const double    b = 2. * double(d.dot(v));
        double          u = b * b - 4. * a * (d.squaredNorm() - shortcut_length2);
        assert(u > 0.);
        u = sqrt(u);
        double t = (- b + u) / (2. * a);
        assert(t > 0. && t < 1.);
        (backward == Far ? *it2 : *it0) += (v.cast<double>() * t).cast<coord_t>();
    } else {
        assert(forward == Far && backward == Far);
        assert(dist2_next > shortcut_length2);
        const double dcurrent = sqrt(double(dist2_current));
        double t = (shortcut_length - dcurrent) / (sqrt(double(dist2_next)) - dcurrent);
        assert(t > 0. && t < 1.);
        *it0 += ((p02 - p0).cast<double>() * t).cast<coord_t>();
        *it2 += ((p22 - p2).cast<double>() * t).cast<coord_t>();
    }
    return false;
}

void smooth_outward(MutablePolygon &polygon, coord_t clip_dist_scaled)
{
    remove_duplicates(polygon, scaled<double>(0.01));

    const auto clip_dist_scaled2    = sqr<int64_t>(clip_dist_scaled);
    const auto clip_dist_scaled2eps = sqr(clip_dist_scaled + int64_t(SCALED_EPSILON));
    const auto foot_dist_min2       = sqr(SCALED_EPSILON);

    MutablePolygon::range unprocessed_range(polygon);
    while (! unprocessed_range.empty() && polygon.size() > 2) {
        auto          it1  = unprocessed_range.process_next();
        auto          it0  = it1.prev();
        auto          it2  = it1.next();
        const Point   p0   = *it0;
        const Point   p1   = *it1;
        const Point   p2   = *it2;
        const Vec2i64 v1   = (p0 - p1).cast<int64_t>();
        const Vec2i64 v2   = (p2 - p1).cast<int64_t>();
        if (cross2(v1, v2) > 0) {
            int64_t dot  = v1.dot(v2);
            auto    l2v1 = double(v1.squaredNorm());
            auto    l2v2 = double(v2.squaredNorm());
            if (dot > 0 || Slic3r::sqr(double(dot)) * 2. < l2v1 * l2v2) {
                Vec2i64 v02   = (p2 - p0).cast<int64_t>();
                int64_t l2v02 = v02.squaredNorm();
                it1.remove();
                if (l2v02 < clip_dist_scaled2) {
                    if (clip_narrow_corner(p1.cast<int64_t>(), it0, it2, unprocessed_range, l2v02, clip_dist_scaled))
                        return;
                } else {
                    if (l2v02 > clip_dist_scaled2eps) {
                        Vec2d  v1d  = v1.cast<double>();
                        Vec2d  v2d  = v2.cast<double>();
                        bool   swap = l2v1 > l2v2;
                        if (swap) {
                            std::swap(v1d, v2d);
                            std::swap(l2v1, l2v2);
                        }
                        double lv1  = sqrt(l2v1);
                        double lv2  = sqrt(l2v2);
                        Vec2d  bisector   = v1d / lv1 + v2d / lv2;
                        double l2bisector = bisector.squaredNorm();
                        double d2         = l2v1 - sqr(v1d.dot(bisector)) / l2bisector;
                        if (d2 < foot_dist_min2) {
                        } else if (d2 < 0.25 * clip_dist_scaled2 + SCALED_EPSILON) {
                            const double    b = - 2. * v1d.cast<double>().dot(v2d);
                            double          u = b * b - 4. * l2v2 * (double(l2v1) - clip_dist_scaled2);
                            assert(u > 0.);
                            double          t = (- b + sqrt(u)) / (2. * l2v2);
                            assert(t > 0. && t < 1.);
                            Point           pt_new = p1 + (t * v2d).cast<coord_t>();
#ifndef NDEBUG
                            double d2new = (pt_new - (swap ? p2 : p0)).cast<double>().squaredNorm();
                            assert(std::abs(d2new - clip_dist_scaled2) < 1e-5 * clip_dist_scaled2);
#endif
                            it2.insert(pt_new);
                        } else {
                            double t  = sqrt(0.25 * clip_dist_scaled2 / d2);
                            double t2 = t * lv1 / lv2;
                            assert(t  > 0. && t  < 1.);
                            assert(t2 > 0. && t2 < 1.);
                            Point  p0 = p1 + (v1d * t ).cast<coord_t>();
                            Point  p2 = p1 + (v2d * t2).cast<coord_t>();
                            if (swap)
                                std::swap(p0, p2);
                            it2.insert(p2).insert(p0);
                        }
                    } else {
                        assert(l2v02 >= clip_dist_scaled2 && l2v02 <= clip_dist_scaled2eps);
                    }
                }
                it1 = it2;
            } else
                ++ it1;
        } else
            ++ it1;
    }

    if (polygon.size() == 3) {
        const Point   p0   = *polygon.begin().prev();
        const Point   p1   = *polygon.begin();
        const Point   p2   = *polygon.begin().next();
        Vec2i64 v1   = (p0 - p1).cast<int64_t>();
        Vec2i64 v2   = (p2 - p1).cast<int64_t>();
        if (cross2(v1, v2) > 0) {
            const Vec2i64 v3 = (p2 - p0).cast<int64_t>();
            int64_t l12 = v1.squaredNorm();
            int64_t l22 = v2.squaredNorm();
            int64_t l32 = v3.squaredNorm();
            if (l22 > l12 && l22 > l32) {
                std::swap(v1,  v2);
                std::swap(l12, l22);
            } else if (l32 > l12 && l32 > l22) {
                v1  = v3;
                l12 = l32;
            }
            auto h2 = l22 - sqr(double(v1.dot(v2))) / double(l12);
            if (h2 < clip_dist_scaled2)
                polygon.clear();
        }
    } else if (polygon.size() < 3)
        polygon.clear();
}

}
