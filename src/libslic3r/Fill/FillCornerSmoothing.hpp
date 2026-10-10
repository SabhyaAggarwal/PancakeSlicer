#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../Polyline.hpp"

namespace Slic3r {

inline double sanitize_smooth_factor(double smooth_factor)
{
    return std::isfinite(smooth_factor) ? std::clamp(smooth_factor, 0., 1.) : 0.;
}

using CornerFilter = std::function<bool(const Vec2d &from, const Vec2d &to)>;

class CornerSmoother
{
public:
    CornerSmoother(double smooth_factor, double tolerance, double max_corner_distance = 0.,
                   CornerFilter corner_filter = {})
        : m_corner_distance_ratio(0.5 * sanitize_smooth_factor(smooth_factor)), m_tolerance(tolerance),
          m_max_corner_distance(max_corner_distance), m_corner_filter(std::move(corner_filter))
    {}

    bool enabled() const { return m_corner_distance_ratio > 0.; }

    template<typename Emit> void push(const Vec2d &point, Emit &emit)
    {
        if (m_held == 0) {
            emit(point);
            m_window[m_held++] = point;
            return;
        }
        if (m_held > 1 && is_on_straight_run(m_window[m_held - 2], m_window[m_held - 1], point)) {
            m_window[m_held - 1] = point;
            return;
        }
        if (m_held < 3) {
            m_window[m_held++] = point;
            return;
        }
        emit_corner(m_window[0], m_window[1], m_window[2], emit);
        m_window[0] = m_window[1];
        m_window[1] = m_window[2];
        m_window[2] = point;
    }

    template<typename Emit> void flush(Emit &emit)
    {
        if (m_held > 2)
            emit_corner(m_window[0], m_window[1], m_window[2], emit);
        if (m_held > 1)
            emit(m_window[m_held - 1]);
        m_held = 0;
    }

private:
    template<typename Emit> void emit_corner(const Vec2d &previous, const Vec2d &corner, const Vec2d &next, Emit &emit)
    {
        round_corner(previous, corner, next);
        for (const Vec2d &corner_point : m_corner_points)
            emit(corner_point);
    }

    static bool is_on_straight_run(const Vec2d &previous, const Vec2d &vertex, const Vec2d &next);
    void round_corner(const Vec2d &previous, const Vec2d &corner, const Vec2d &next);
    const std::vector<Vec2d>& curve_coefficients(double corner_distance, const Vec2d &incoming, const Vec2d &outgoing);

    const double       m_corner_distance_ratio;
    const double       m_tolerance;
    const double       m_max_corner_distance;
    const CornerFilter m_corner_filter;
    std::vector<Vec2d> m_corner_points;
    std::vector<Vec2d> m_cached_coefficients;
    double             m_cached_distance { 0. };
    double             m_cached_cosine { 0. };
    bool               m_has_cached_coefficients { false };

    std::array<Vec2d, 3> m_window { Vec2d::Zero(), Vec2d::Zero(), Vec2d::Zero() };
    int                  m_held { 0 };
};

void smooth_polyline_corners(Polyline &polyline, double smooth_factor, double tolerance,
                             double max_corner_distance = 0., const CornerFilter &corner_filter = {});
void smooth_polylines_corners(Polylines &polylines, double smooth_factor, double tolerance,
                              double max_corner_distance = 0., const CornerFilter &corner_filter = {});
void smooth_polygons_corners(Polygons &polygons, double smooth_factor, double tolerance,
                             double max_corner_distance = 0., const CornerFilter &corner_filter = {});

}
