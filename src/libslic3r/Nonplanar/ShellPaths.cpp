#include "ShellPaths.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Nonplanar/PatchTools.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <cstddef>

#include "AutoMode.hpp"

#include "BeadModel.hpp"
#include "ConformalShells.hpp"
#include "HeightField.hpp"
#include "Seams.hpp"
#include "SpiralTop.hpp"

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/libslic3r.h"

#include <Eigen/Eigenvalues>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {


constexpr int    LineAngles = 12;
constexpr double AngleStep  = PI / LineAngles;


bool link_inside(const Point &a, const Point &b, const AreaEdges &area)
{
    if (a == b)
        return true;

    if (const int side = area.classify(a, b); side >= 0)
        return side == 1 || !(Polyline(a, b).length() > SCALED_EPSILON);
    for (const Polyline &outside : diff_pl(Polylines{ Polyline(a, b) }, area.area()))
        if (outside.length() > SCALED_EPSILON)
            return false;
    return true;
}


std::pair<Point, size_t> project_on(const Polygon &rail, const Point &p)
{
    const Points &pts  = rail.points;
    const Vec2d   q    = p.cast<double>();
    size_t        best = 0;
    Vec2d         foot = pts.front().cast<double>();
    double        dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2d  a = pts[i].cast<double>(), d = pts[(i + 1) % pts.size()].cast<double>() - a;
        const double t = d.squaredNorm() > 0. ? std::clamp((q - a).dot(d) / d.squaredNorm(), 0., 1.) : 0.;
        if (const double dd = (a + t * d - q).squaredNorm(); dd < dist) {
            dist = dd;
            best = i;
            foot = a + t * d;
        }
    }
    return { Point(coord_t(std::lround(foot.x())), coord_t(std::lround(foot.y()))), best };
}


Polyline open_loop_at(const Polygon &loop, const Point &near)
{
    const Points &pts         = loop.points;
    const auto [start, best] = project_on(loop, near);
    Polyline     out;
    out.points.reserve(pts.size() + 2);
    out.points.emplace_back(start);
    for (size_t j = 1; j <= pts.size(); ++j)
        if (const Point &q = pts[(best + j) % pts.size()]; q != out.points.back())
            out.points.emplace_back(q);
    if (out.points.back() != start)
        out.points.emplace_back(start);
    return out;
}




Polyline glide_route(const Point &a, const Point &b, const AreaEdges &inside, const Polygons &rails, double reach, double max_length)
{
    if ((b - a).cast<double>().norm() <= max_length && link_inside(a, b, inside))
        return Polyline(a, b);
    Polyline best;
    double   best_length = max_length;
    for (const Polygon &rail : rails) {
        const auto [pa, ia] = project_on(rail, a);
        const auto [pb, ib] = project_on(rail, b);
        if ((pa - a).cast<double>().norm() > reach || (pb - b).cast<double>().norm() > reach)
            continue;
        const size_t n = rail.points.size();
        for (const bool forward : { true, false }) {
            Polyline route(a, pa);

            if (forward) {
                for (size_t i = (ia + 1) % n, k = 0; ia != ib && k < n; i = (i + 1) % n, ++k) {
                    route.points.emplace_back(rail.points[i]);
                    if (i == ib)
                        break;
                }
            } else {
                for (size_t i = ia, k = 0; ia != ib && k < n; i = (i + n - 1) % n, ++k) {
                    route.points.emplace_back(rail.points[i]);
                    if (i == (ib + 1) % n)
                        break;
                }
            }
            route.points.emplace_back(pb);
            route.points.emplace_back(b);
            route.remove_duplicate_points();
            if (const double length = route.length(); length < best_length && link_inside(a, pa, inside) && link_inside(pb, b, inside)) {
                best_length = length;
                best        = std::move(route);
            }
        }
    }
    return best;
}


Point crossing_near(const Polygon &loop, const Polyline &line, const Point &near)
{
    Point  best = near;
    double dist = std::numeric_limits<double>::max();
    for (const Line &edge : loop.lines())
        for (const Line &seg : line.lines()) {
            Point p;
            if (edge.intersection(seg, &p))
                if (const double d = (p - near).cast<double>().squaredNorm(); d < dist) {
                    dist = d;
                    best = p;
                }
        }
    return best;
}


void reverse_order(Polylines &polylines)
{
    std::reverse(polylines.begin(), polylines.end());
    for (Polyline &pl : polylines)
        pl.reverse();
}




class AcrossStretch
{
public:
    AcrossStretch(const BoundingBoxf &uv, const HeightField &surface, double angle, double step)
        : m_dir(std::cos(angle), std::sin(angle)), m_across(-std::sin(angle), std::cos(angle)), m_u0(uv.min.x()), m_v0(uv.min.y()), m_step(step)
    {
        m_nu = std::max(2, int(std::ceil((uv.max.x() - m_u0) / step)) + 1);
        m_nv = std::max(2, int(std::ceil((uv.max.y() - m_v0) / step)) + 1);
        m_w.assign(size_t(m_nu) * m_nv, 0.);
        for (int iu = 0; iu < m_nu; ++iu) {
            double w = m_v0;
            for (int iv = 0; iv < m_nv; ++iv) {
                m_w[size_t(iu) * m_nv + iv] = w;
                const Vec2d  g = surface.gradient(this->xy(m_u0 + iu * step, m_v0 + (iv + 0.5) * step));
                const double s = std::isnan(g.x()) || std::isnan(g.y()) ? 0. : g.dot(m_across);
                w += step * std::sqrt(1. + s * s);
            }
        }
    }

    Vec2d xy(double u, double v) const { return u * m_dir + v * m_across; }
    Vec2d uv(const Vec2d &p) const { return Vec2d(p.dot(m_dir), p.dot(m_across)); }

    double stretch(double u, double v) const
    {
        const double fu = std::clamp((u - m_u0) / m_step, 0., double(m_nu - 1));
        const int    iu = std::min(int(fu), m_nu - 2);
        const double t  = fu - iu;
        return (1. - t) * this->column(iu, v) + t * this->column(iu + 1, v);
    }

    double unstretch(double u, double w) const
    {
        double lo = m_v0 - m_step, hi = m_v0 + m_nv * m_step;
        for (int i = 0; i < 48; ++i) {
            const double mid = 0.5 * (lo + hi);
            (this->stretch(u, mid) < w ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

private:

    double column(int iu, double v) const
    {
        const double *w  = &m_w[size_t(iu) * m_nv];
        const double  fv = (v - m_v0) / m_step;
        if (fv <= 0.)
            return w[0] + (v - m_v0);
        if (fv >= m_nv - 1)
            return w[m_nv - 1] + (v - (m_v0 + (m_nv - 1) * m_step));
        const int iv = int(fv);
        return w[iv] + (fv - iv) * (w[iv + 1] - w[iv]);
    }

    Vec2d               m_dir, m_across;
    double              m_u0, m_v0, m_step;
    int                 m_nu = 0, m_nv = 0;
    std::vector<double> m_w;
};


template<typename Map> Points map_resampled(const Points &pts, bool closed, double step, const Map &map)
{
    Points out;
    const size_t n = closed ? pts.size() : pts.size() - 1;
    for (size_t i = 0; i < n; ++i) {
        const Vec2d a = unscale(pts[i]), b = unscale(pts[(i + 1) % pts.size()]);
        const int   k = std::max(1, int(std::ceil((b - a).norm() / step)));
        for (int j = 0; j < k; ++j) {
            const Vec2d q = map(a + (b - a) * (double(j) / k));
            out.emplace_back(Point::new_scale(q.x(), q.y()));
        }
    }
    if (!closed && !pts.empty()) {
        const Vec2d q = map(unscale(pts.back()));
        out.emplace_back(Point::new_scale(q.x(), q.y()));
    }
    return out;
}



constexpr int ContourDirectionBins = 12;

constexpr double ContourDirectionWindow = 2.;






Polylines fill_piece(const ExPolygons &piece, const ExPolygons &area, double angle, double spacing, const Flow &flow, bool monotonic,
                     double z)
{
    BoundingBox bb = get_extents(piece);
    bb.offset(scale_(2. * spacing));
    const ExPolygons grown = intersection_ex(area, ExPolygons{ ExPolygon(bb.polygon()) });
    return intersection_pl(fill_solid(grown, monotonic ? ipMonotonic : ipRectilinear, spacing, angle, monotonic, flow, z), piece);
}

}

Polylines fill_surface_spaced(const ExPolygons &area, const HeightField &surface, InfillPattern pattern, double spacing, double angle,
                              bool monotonic, const Flow &flow, double z)
{

    const double line = angle;
    const double step = surface.pitch();
    const Vec2d  dir(std::cos(line), std::sin(line)), across(-std::sin(line), std::cos(line));
    BoundingBoxf uv;
    for (const ExPolygon &expoly : area)
        for (const Point &pt : expoly.contour.points) {
            const Vec2d p = unscale(pt);
            uv.merge(Vec2d(p.dot(dir), p.dot(across)));
        }
    if (!uv.defined)
        return {};
    const AcrossStretch stretch(uv, surface, line, step);
    auto forward = [&stretch](const Vec2d &p) {
        const Vec2d q = stretch.uv(p);
        return stretch.xy(q.x(), stretch.stretch(q.x(), q.y()));
    };
    auto backward = [&stretch](const Vec2d &p) {
        const Vec2d q = stretch.uv(p);
        return stretch.xy(q.x(), stretch.unstretch(q.x(), q.y()));
    };
    ExPolygons stretched;
    for (const ExPolygon &expoly : area) {
        ExPolygon out;
        out.contour.points = map_resampled(expoly.contour.points, true, step, forward);
        for (const Polygon &hole : expoly.holes)
            out.holes.emplace_back(map_resampled(hole.points, true, step, forward));
        stretched.emplace_back(std::move(out));
    }
    Polylines lines = fill_solid(stretched, pattern, spacing, angle, monotonic, flow, z);
    for (Polyline &pl : lines)
        pl.points = map_resampled(pl.points, false, step, backward);
    return lines;
}

float surface_spaced_bead(float height, double width, const Vec2d &gradient, double angle)
{
    if (std::isnan(gradient.x()) || std::isnan(gradient.y()))
        return height;

    const double s      = gradient.dot(Vec2d(-std::sin(angle), std::cos(angle)));
    return float(bead_height_for_area(width, bead_area(width, height) / std::sqrt(1. + s * s)));
}

PathTilt path_tilt(const Polylines &polylines, const HeightField &surface, double step)
{
    PathTilt tilt;
    double   rise = 0., length = 0.;
    for (const Polyline &pl : polylines)
        for (size_t i = 0; i + 1 < pl.points.size(); ++i) {
            const Vec2d a = unscale(pl.points[i]), b = unscale(pl.points[i + 1]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / step)));
            Vec2d       p0 = a;
            float       z0 = surface.sample(a);
            for (int j = 1; j <= n; ++j) {
                const Vec2d  p1 = a + (b - a) * (double(j) / n);
                const float  z1 = surface.sample(p1);
                const double ds = (p1 - p0).norm();
                if (!std::isnan(z0) && !std::isnan(z1) && ds > 0.) {
                    const double dz = std::abs(double(z1) - double(z0));
                    tilt.max = std::max(tilt.max, dz / ds);
                    rise += dz;
                    length += ds;
                }
                p0 = p1;
                z0 = z1;
            }
        }
    tilt.mean = length > 0. ? rise / length : 0.;
    return tilt;
}

Polylines contour_fill(const HeightField &surface, const ExPolygons &area, double drop, double spacing, const Flow &flow, bool monotonic,
                       double z, double cross, double strip_height)
{
    const int nx = surface.nx(), ny = surface.ny();

    std::vector<double> sxx(size_t(nx + 1) * (ny + 1), 0.), sxy(sxx.size(), 0.), syy(sxx.size(), 0.);
    auto at = [nx](std::vector<double> &v, int ix, int iy) -> double & { return v[size_t(iy) * (nx + 1) + ix]; };
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            Vec2d g = surface.gradient(surface.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y()))
                g = Vec2d::Zero();
            at(sxx, ix + 1, iy + 1) = g.x() * g.x() + at(sxx, ix, iy + 1) + at(sxx, ix + 1, iy) - at(sxx, ix, iy);
            at(sxy, ix + 1, iy + 1) = g.x() * g.y() + at(sxy, ix, iy + 1) + at(sxy, ix + 1, iy) - at(sxy, ix, iy);
            at(syy, ix + 1, iy + 1) = g.y() * g.y() + at(syy, ix, iy + 1) + at(syy, ix + 1, iy) - at(syy, ix, iy);
        }
    const int        half = std::max(1, int(std::lround(0.5 * ContourDirectionWindow / surface.pitch())));
    std::vector<int> direction(size_t(nx) * ny, 0);
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            const int x0 = std::max(0, ix - half), x1 = std::min(nx, ix + half + 1), y0 = std::max(0, iy - half), y1 = std::min(ny, iy + half + 1);
            auto box = [&](std::vector<double> &v) { return at(v, x1, y1) - at(v, x0, y1) - at(v, x1, y0) + at(v, x0, y0); };
            const double jxx = box(sxx), jxy = box(sxy), jyy = box(syy);
            if (jxx + jyy > 1e-12) {

                const double contour = std::fmod(0.5 * std::atan2(2. * jxy, jxx - jyy) + 1.5 * PI, PI);
                direction[size_t(iy) * nx + ix] = std::min(ContourDirectionBins - 1, int(contour / PI * ContourDirectionBins));
            }
        }

    RunGroups strips;
    for (size_t n = 0; n < surface.data().size(); ++n)
        if (const float s = surface.data()[n]; !std::isnan(s))
            strips.add(std::isfinite(strip_height) ? int(std::floor((s - drop) / strip_height)) : 0, int(n % nx), int(n / nx));
    Polylines out;
    for (const auto &[strip, runs] : strips.groups())

        for (const ExPolygon &piece : intersection_ex(area, runs_to_expolygons(runs, surface))) {

            const BoundingBox bb  = get_extents(piece);
            const Vec2d       lo  = (unscale(bb.min) - surface.origin()) / surface.pitch();
            const Vec2d       hi  = (unscale(bb.max) - surface.origin()) / surface.pitch();
            const int         ix0 = std::clamp(int(std::floor(lo.x())), 0, nx - 1), iy0 = std::clamp(int(std::floor(lo.y())), 0, ny - 1);
            const int         ix1 = std::clamp(int(std::ceil(hi.x())), 0, nx - 1), iy1 = std::clamp(int(std::ceil(hi.y())), 0, ny - 1);
            HeightField       local  = crop(surface, ix0, iy0, ix1, iy1);
            const Mask        inside = rasterize({ piece }, local);
            for (size_t n = 0; n < inside.size(); ++n)
                if (!inside[n])
                    local.data()[n] = HeightField::NoData;
            double angle = 0.;
            if (contour_line_angle(local, angle, 0.5)) {
                append(out, fill_piece({ piece }, area, angle + cross, spacing, flow, monotonic, z));
                continue;
            }
            std::vector<Mask> classes(ContourDirectionBins);
            for (int iy = 0; iy < local.ny(); ++iy)
                for (int ix = 0; ix < local.nx(); ++ix)
                    if (inside[size_t(iy) * local.nx() + ix]) {
                        Mask &m = classes[size_t(direction[size_t(iy0 + iy) * nx + ix0 + ix])];
                        m.resize(inside.size(), 0);
                        m[size_t(iy) * local.nx() + ix] = 1;
                    }
            for (int bin = 0; bin < ContourDirectionBins; ++bin)
                if (!classes[size_t(bin)].empty())
                    append(out, fill_piece(intersection_ex(ExPolygons{ piece }, mask_to_expolygons(classes[size_t(bin)], local)), area,
                                           (bin + 0.5) * PI / ContourDirectionBins + cross, spacing, flow, monotonic, z));
        }
    return out;
}

Polylines chain_nearest(Polylines &&polylines, const Point &start)
{
    Polylines out;
    out.reserve(polylines.size());
    polylines.erase(std::remove_if(polylines.begin(), polylines.end(), [](const Polyline &pl) { return !pl.is_valid(); }), polylines.end());
    const size_t n = polylines.size();
    if (n == 0)
        return out;


    BoundingBox bb;
    for (const Polyline &pl : polylines) {
        bb.merge(pl.first_point());
        bb.merge(pl.last_point());
    }
    const Vec2d   size  = (bb.max - bb.min).cast<double>();
    const coord_t cell  = std::max<coord_t>(coord_t(scale_(0.5)), coord_t(std::sqrt(std::max(1., size.x() * size.y() / double(2 * n)))));
    const int     gx    = int(size.x() / cell) + 1, gy = int(size.y() / cell) + 1;
    std::vector<std::vector<size_t>> grid(size_t(gx) * gy);
    auto end_point = [&polylines](size_t e) -> const Point & { return e % 2 == 0 ? polylines[e / 2].first_point() : polylines[e / 2].last_point(); };
    auto cell_of   = [&](const Point &p, int &ix, int &iy) {
        ix = std::clamp(int((p.x() - bb.min.x()) / cell), 0, gx - 1);
        iy = std::clamp(int((p.y() - bb.min.y()) / cell), 0, gy - 1);
    };
    for (size_t e = 0; e < 2 * n; ++e) {
        int ix, iy;
        cell_of(end_point(e), ix, iy);
        grid[size_t(iy) * gx + ix].emplace_back(e);
    }
    std::vector<bool> used(n, false);
    Point             cursor = start;
    for (size_t left = n; left > 0; --left) {
        int cx, cy;
        cell_of(cursor, cx, cy);
        size_t best = size_t(-1);
        double best_d = std::numeric_limits<double>::max();

        for (int r = 0; r <= std::max(gx, gy); ++r) {
            if (best != size_t(-1) && double(r - 1) * double(cell) > std::sqrt(best_d))
                break;
            for (int iy = cy - r; iy <= cy + r; ++iy)
                for (int ix = cx - r; ix <= cx + r; ++ix) {
                    if (ix < 0 || iy < 0 || ix >= gx || iy >= gy || (std::abs(ix - cx) != r && std::abs(iy - cy) != r))
                        continue;
                    std::vector<size_t> &bucket = grid[size_t(iy) * gx + ix];

                    bucket.erase(std::remove_if(bucket.begin(), bucket.end(), [&used](size_t e) { return used[e / 2]; }), bucket.end());
                    for (const size_t e : bucket)
                        if (const double d = (end_point(e) - cursor).cast<double>().squaredNorm(); d < best_d) {
                            best_d = d;
                            best   = e;
                        }
                }
        }
        used[best / 2] = true;
        out.emplace_back(std::move(polylines[best / 2]));
        if (best % 2 == 1)
            out.back().reverse();
        cursor = out.back().last_point();
    }
    return out;
}

std::vector<ShellFill> plan_shell_fills(const ConformalPatch &patch, const PrintRegionConfig &config, double spacing)
{
    const int                 n       = patch.shells;
    std::vector<ShellFill>    fills(size_t(std::max(0, n)));

    const NonplanarTopPattern pattern = top_pattern(patch, config);
    const double              user    = patch.automatic ? -1. : config.nonplanar_top_direction.value;


    auto spirals = [&fills, n]() {
        for (int k = 1; k <= n; ++k) {
            fills[size_t(k - 1)].spiral  = true;
            fills[size_t(k - 1)].outward = (n - k) % 2 == 1;
        }
    };
    if (n <= 0)
        return fills;
    if (pattern == NonplanarTopPattern::Concentric) {
        spirals();
        return fills;
    }
    if (pattern == NonplanarTopPattern::Rectilinear) {
        const double angle = Geometry::deg2rad(user >= 0. ? user : config.solid_infill_direction.value);
        for (int k = 1; k <= n; ++k)
            fills[size_t(k - 1)].angle = angle + (n - k) * 0.5 * PI;
        return fills;
    }



    const ExPolygons area = diff_ex(patch.footprint, patch.wall_band);
    const double     step = patch.surface.pitch();
    const Flow       flow(float(spacing), float(patch.shell_height), float(spacing));
    std::array<PathTilt, LineAngles> lines;
    for (int i = 0; i < LineAngles; ++i)
        lines[size_t(i)] = path_tilt(fill_solid(area, ipRectilinear, spacing, i * AngleStep, false, flow, 0.), patch.surface, step);
    const PathTilt spiral = path_tilt(fill_spiral(area, spacing, Point(0, 0)), patch.surface, step);
    auto better = [](const PathTilt &a, const PathTilt &b) {

        return a.mean < b.mean - 0.005 || (a.mean <= b.mean + 0.005 && a.max < b.max);
    };
    int best = 0;
    for (int i = 1; i < LineAngles; ++i)
        if (better(lines[size_t(i)], lines[size_t(best)]))
            best = i;
    if (better(spiral, lines[size_t(best)]))
        spirals();
    else {

        const double bound = lines[size_t(best)].mean + std::tan(Geometry::deg2rad(5.));
        int          cross = 3;
        for (int c : { 6, 4 })
            if (std::max(lines[size_t((best + c) % LineAngles)].mean, lines[size_t((best - c + LineAngles) % LineAngles)].mean) <= bound) {
                cross = c;
                break;
            }
        for (int k = 1; k <= n; ++k) {
            const int j = n - k;

            const int offset = j == 0 ? 0 : cross == 6 ? (j % 2) * cross : (j % 2 == 1 ? cross : -cross);
            fills[size_t(k - 1)].angle = (best + offset) * AngleStep;
        }
    }

    if (user >= 0.) {
        fills.back().spiral  = false;
        fills.back().outward = false;
        fills.back().angle   = Geometry::deg2rad(user);
    }
    return fills;
}

ThickPolylines gap_fill(const ExPolygons &area, const Polygons &covered, double spacing)
{
    ThickPolylines out;
    for (ExPolygon &gap : opening_ex(diff_ex(area, covered), float(scale_(0.125 * spacing)))) {
        gap.douglas_peucker(scale_(0.01));
        const size_t n = out.size();
        gap.medial_axis(scale_(0.25 * spacing), scale_(2. * spacing), &out);


        if (out.size() == n && gap.area() > sqr(scale_(0.5 * spacing))) {
            const BoundingBox bb   = get_extents(gap.contour);
            const Point       c    = bb.center();
            const bool        wide = bb.size().x() >= bb.size().y();
            const coord_t     half = std::max<coord_t>(coord_t(scale_(0.125 * spacing)), (wide ? bb.size().x() : bb.size().y()) / 2 - coord_t(scale_(0.5 * spacing)));
            ThickPolyline     pl;
            pl.points = wide ? Points{ Point(c.x() - half, c.y()), Point(c.x() + half, c.y()) } : Points{ Point(c.x(), c.y() - half), Point(c.x(), c.y() + half) };
            pl.width.assign(2, std::min(double(wide ? bb.size().y() : bb.size().x()), gap.area() / double(2 * half)));
            out.emplace_back(std::move(pl));
        }
    }
    return out;
}

Polylines fill_spiral(const ExPolygons &area, double spacing, const Point &seam, const Polyline *seam_line, double ring_spacing, int first_ring)
{
    const coord_t step = coord_t(scale_(spacing));
    if (step <= 0)
        return {};






    const coord_t chain = ring_spacing > 0. ? coord_t(scale_(ring_spacing)) : step;
    const int     every = std::max(1, int(std::lround(double(step) / double(chain))));
    const double  arc   = scale_(0.01);


    auto inward = [chain, arc](const ExPolygons &level, int rings) {
        ExPolygons out = level;
        for (int i = 0; i < rings && !out.empty(); ++i)
            out = expolygons_simplify(offset2_ex(out, -1.5f * float(chain), 0.5f * float(chain), jtRound, arc), scale_(0.005));
        return out;
    };
    Polygons   loops;
    Polylines  centres;
    ExPolygons innermost;
    ExPolygons level = inward(expolygons_simplify(offset_ex(area, -0.5f * float(chain), jtRound, arc), scale_(0.005)), first_ring);
    while (!level.empty()) {
        append(loops, to_polygons(level));
        ExPolygons next = inward(level, every);




        for (const ExPolygon &piece : level)
            if (intersection_ex(ExPolygons{ piece }, next).empty()) {
                innermost.emplace_back(piece);
                if (!offset_ex(piece, -0.75f * float(step)).empty())
                    append(centres, piece.medial_axis(0.5 * double(step), 3.5 * double(step)));
            }
        level = std::move(next);
    }
    loops = union_pt_chained_outside_in(loops);





    Polyline line = seam_line != nullptr && seam_line->is_valid() ? *seam_line : Polyline(seam, seam);
    if (!innermost.empty()) {
        Vec2d  c    = Vec2d::Zero();
        double area = 0.;
        for (const ExPolygon &e : innermost) {
            const double a = e.area();
            c += a * e.contour.centroid().cast<double>();
            area += a;
        }
        if (area > 0.)
            line.points.emplace_back((c / area).cast<coord_t>());
    }
    Polylines opened;
    Polygons  rings;
    Point     pos = seam;
    for (const Polygon &loop : loops) {
        if (loop.points.size() < 3)
            continue;
        Polyline pl = open_loop_at(loop, opened.empty() ? pos : crossing_near(loop, line, pos));
        Polyline short_of = pl;
        short_of.clip_end(double(step));
        if (!short_of.is_valid())
            continue;
        pos = pl.first_point();
        opened.emplace_back(std::move(pl));
        rings.emplace_back(loop);
    }










    const Polygons edges = to_polygons(area);
    auto           along_edge = [&](const Point &p) {
        return first_ring == 0 && std::any_of(edges.begin(), edges.end(), [&](const Polygon &edge) {
                   return edge.size() >= 3 && (project_on(edge, p).first - p).cast<double>().norm() < double(chain);
               });
    };
    Polylines out;
    for (size_t k = 0; k < opened.size(); ++k) {
        Polyline pl = opened[k];
        const double length = pl.length();
        const double shear  = std::clamp(0.125 * length, double(step), scale_(RingStepLength));
        bool         joined = false;
        Polyline     body   = pl;
        body.clip_end(shear);
        if (k + 1 < opened.size() && shear > double(step) && body.is_valid() && !along_edge(body.last_point())) {
            Polyline tail = pl;
            tail.reverse();
            tail.clip_end(length - shear);
            tail.reverse();
            Points blended;
            bool   parallel = body.is_valid() && tail.is_valid();
            double done     = 0.;
            for (size_t i = 0; parallel && i + 1 < tail.points.size(); ++i) {
                const Vec2d  a = tail.points[i].cast<double>(), b = tail.points[i + 1].cast<double>();
                const double l = (b - a).norm();
                const int    n = std::max(1, int(std::ceil(l / (0.25 * double(step)))));
                for (int j = (i == 0 ? 0 : 1); j <= n; ++j) {
                    const Point  q(((a + (b - a) * (double(j) / n))).cast<coord_t>());
                    const Point  f = project_on(rings[k + 1], q).first;
                    const double d = (f - q).cast<double>().norm();
                    if (d < 0.5 * double(step) || d > 1.5 * double(step)) {
                        parallel = false;
                        break;
                    }
                    const double t = std::min(1., (done + l * double(j) / n) / shear);
                    blended.emplace_back((q.cast<double>() + t * (f - q).cast<double>()).cast<coord_t>());
                }
                done += l;
            }



            const Vec2d miss = blended.empty() ? Vec2d(Vec2d::Zero()) : Vec2d((opened[k + 1].first_point() - blended.back()).cast<double>());
            if (parallel && !blended.empty() && miss.norm() <= 1.5 * double(step)) {
                for (size_t i = 0; i < blended.size(); ++i)
                    blended[i] += (miss * (double(i) / double(std::max<size_t>(1, blended.size() - 1)))).cast<coord_t>();
                blended.back() = opened[k + 1].first_point();
                body.points.insert(body.points.end(), blended.begin() + (blended.front() == body.last_point() ? 1 : 0), blended.end());
                pl     = std::move(body);
                joined = true;
            }
        }


        if (!joined)
            pl.clip_end(double(step));
        out.emplace_back(std::move(pl));
    }
    if (!out.empty())
        pos = out.back().last_point();
    append(out, chain_polylines(std::move(centres), &pos));









    if (!innermost.empty()) {


        BoundingBox box = get_extents(innermost);
        box.offset(step);
        auto reaches = [&box](const Point &a, const Point &b) {
            Line l(a, b);
            return l.clip_with_bbox(box);
        };
        Polylines near;
        for (size_t i = 0; i < out.size(); ++i) {
            const Points &pts = out[i].points;
            for (size_t j = 0; j + 1 < pts.size(); ++j)
                if (reaches(pts[j], pts[j + 1])) {
                    near.emplace_back(out[i]);
                    break;
                }
            if (i + 1 < out.size() && (out[i + 1].first_point() - out[i].last_point()).cast<double>().norm() <= 2. * double(step) &&
                reaches(out[i].last_point(), out[i + 1].first_point()))
                near.emplace_back(out[i].last_point(), out[i + 1].first_point());
        }

        const float  eps      = float(scale_(0.005));
        ExPolygons   holes    = offset_ex(offset_ex(expolygons_simplify(diff_ex(innermost, offset(near, 0.5f * float(step))), scale_(0.01)), -eps), eps);
        const double min_hole = 0.25 * double(step) * double(step);
        Point        at       = out.empty() ? pos : out.back().last_point();
        for (int pass = 0; pass < 3 && !holes.empty(); ++pass) {
            Polylines fill;
            for (const ExPolygon &hole : holes) {
                if (hole.area() < min_hole)
                    continue;
                if (const ExPolygons ring = offset_ex(hole, -0.5f * float(step)); !ring.empty()) {
                    for (const Polygon &loop : to_polygons(ring))
                        if (loop.points.size() >= 3) {
                            Polyline pl = open_loop_at(loop, at);
                            pl.clip_end(0.2 * double(step));
                            if (pl.is_valid())
                                fill.emplace_back(std::move(pl));
                        }
                    continue;
                }


                const Points &pts = hole.contour.points;
                Vec2d         c   = Vec2d::Zero();
                for (const Point &q : pts)
                    c += q.cast<double>();
                c /= double(pts.size());
                Matrix2d cov = Matrix2d::Zero();
                for (const Point &q : pts)
                    cov += (q.cast<double>() - c) * (q.cast<double>() - c).transpose();
                const Vec2d d  = Eigen::SelfAdjointEigenSolver<Matrix2d>(cov).eigenvectors().col(1);
                double      lo = 0., hi = 0.;
                for (const Point &q : pts) {
                    lo = std::min(lo, (q.cast<double>() - c).dot(d));
                    hi = std::max(hi, (q.cast<double>() - c).dot(d));
                }
                lo = std::min(lo + 0.5 * double(step), -0.25 * double(step));
                hi = std::max(hi - 0.5 * double(step), 0.25 * double(step));
                fill.emplace_back(Point((c + lo * d).cast<coord_t>()), Point((c + hi * d).cast<coord_t>()));
            }
            Polylines kept;
            for (Polyline &pl : chain_polylines(std::move(fill), &at))
                if ((pl.first_point() - at).cast<double>().norm() <= 2. * double(step)) {
                    at = pl.last_point();
                    kept.emplace_back(std::move(pl));
                }
            if (kept.empty())
                break;
            holes = diff_ex(holes, offset(kept, 0.5f * float(step)));
            append(out, std::move(kept));
        }
    }
    return out;
}

std::vector<Polylines> shell_polylines(const ExPolygons &area, const ShellFill &fill, InfillPattern line_pattern, double spacing,
                                       bool monotonic, const Flow &flow, double z, const Point &start, double max_glide,
                                       const HeightField *surface_spaced, const Polyline *seam_line, HelixRamp *helix_ramp)
{
    Polylines paths;
    if (fill.spiral) {
        if (Polyline helix = fill.helix ? fill_helix(area, spacing, start, false, helix_ramp, TopHelixMinRings) : Polyline(); helix.is_valid())
            paths.emplace_back(std::move(helix));
        else
            paths = fill_spiral(area, spacing, start, seam_line, fill.ring_spacing, fill.first_ring);
        if (fill.outward)
            reverse_order(paths);
    } else {





        const ExPolygons inner  = fill.outline ? offset_ex(area, -float(scale_(spacing))) : area;
        const bool       seamed = fill.outline && seam_line != nullptr && seam_line->is_valid();
        Polylines        outline;
        auto             open_outline = [&](const Point &at) {
            for (const Polygon &loop : fill.outline ? to_polygons(offset_ex(area, -0.5f * float(scale_(spacing)))) : Polygons()) {
                Polyline pl = open_loop_at(loop, at);

                pl.clip_end(0.2 * scale_(spacing));
                if (pl.is_valid())
                    outline.emplace_back(std::move(pl));
            }

            std::stable_sort(outline.begin(), outline.end(), [&at](const Polyline &l, const Polyline &r) {
                return (l.first_point() - at).cast<double>().norm() > (r.first_point() - at).cast<double>().norm();
            });
        };
        if (seamed)
            open_outline(seam_line->first_point());
        const Point from = seamed && !outline.empty() ? outline.back().last_point() : start;
        paths = surface_spaced != nullptr ?
                    fill_surface_spaced(inner, *surface_spaced, line_pattern, spacing, fill.angle, monotonic, flow, z) :
                    fill_solid(inner, line_pattern, spacing, fill.angle, monotonic, flow, z);
        if (monotonic) {

            if (!paths.empty() && (paths.back().last_point() - from).cast<double>().norm() <
                                      (paths.front().first_point() - from).cast<double>().norm())
                reverse_order(paths);
        } else

            paths = chain_nearest(std::move(paths), from);
        if (!seamed)
            open_outline(paths.empty() ? start : paths.front().first_point());
        paths.insert(paths.begin(), std::make_move_iterator(outline.begin()), std::make_move_iterator(outline.end()));
    }
    return link_paths(std::move(paths), area, spacing, max_glide);
}


constexpr double TaperStep = 0.25;

void taper_converging(const Polylines &roads, std::vector<std::vector<float>> &widths, const Polylines &others, float to_width,
                      double spacing)
{
    std::vector<Linef>  lines;
    std::vector<size_t> owner;
    std::vector<double> arc0;
    std::vector<double> length;
    auto add = [&](const Polyline &pl, size_t id) {
        double arc = 0.;
        for (size_t j = 0; j + 1 < pl.points.size(); ++j) {
            const Linef l(unscale(pl.points[j]), unscale(pl.points[j + 1]));
            lines.emplace_back(l);
            owner.emplace_back(id);
            arc0.emplace_back(arc);
            arc += (l.b - l.a).norm();
        }
        length.resize(lines.size(), arc);
    };
    for (size_t r = 0; r < roads.size(); ++r)
        add(roads[r], r);
    for (size_t k = 0; k < others.size(); ++k)
        add(others[k], roads.size() + k);
    if (lines.empty())
        return;
    const AABBTreeLines::LinesDistancer<Linef> tree(lines);
    const double parallel = std::cos(Geometry::deg2rad(30.));

    auto room = [&](size_t r, const Vec2d &p, const Vec2d &t, double arc, double s) {
        const Vec2d n(-t.y(), t.x());
        double      left = std::numeric_limits<double>::max(), right = left;
        for (const size_t i : tree.all_lines_in_radius(p, 1.25 * s)) {
            const Linef &l = lines[i];
            const Vec2d  d = l.b - l.a;
            const double len = d.norm();
            if (len < EPSILON || std::abs(d.dot(t)) < parallel * len)
                continue;
            const double u = std::clamp((p - l.a).dot(d) / (len * len), 0., 1.);
            const Vec2d  v = l.a + u * d - p;

            const double at_line = arc0[i] + u * len;
            if (owner[i] == r && std::abs(at_line - arc) < 3. * s)
                continue;
            const double lateral = v.dot(n);

            if (std::abs(v.dot(t)) > 0.5 * std::abs(lateral) + 0.02)
                continue;


            if (owner[i] != r && std::min(at_line, length[i] - at_line) < TaperEnds)
                return 1.;
            if (lateral > 0.)
                left = std::min(left, lateral);
            else
                right = std::min(right, -lateral);
        }


        const auto   side = [s](double d) { return d == std::numeric_limits<double>::max() ? TaperMissing * s : std::min(d, 1.25 * s); };
        const double mean = 0.5 * (side(left) + side(right)) / s;
        return mean < TaperFrom ? std::max(mean, TaperFloor) : 1.;
    };
    const auto own_spacing = [&](float width) { return std::clamp(double(width - to_width), EPSILON, spacing); };
    tbb::parallel_for(tbb::blocked_range<size_t>(0, roads.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t r = range.begin(); r < range.end(); ++r) {
            const Points             &pts = roads[r].points;
            const std::vector<float> &w   = widths[r];
            if (pts.size() < 2)
                continue;

            std::vector<Vec2d>  p, heading;
            std::vector<double> arc, share;
            std::vector<float>  width;
            std::vector<size_t> vertex;
            double              at = 0.;
            for (size_t j = 0; j + 1 < pts.size(); ++j) {
                const Vec2d  a = unscale(pts[j]), b = unscale(pts[j + 1]);
                const double len = (b - a).norm();
                const Vec2d  t   = len > EPSILON ? Vec2d((b - a) / len) : Vec2d(1., 0.);
                const int    n   = std::max(1, int(std::ceil(len / TaperStep)));
                vertex.emplace_back(p.size());
                for (int k = 0; k < n; ++k) {
                    const double f = double(k) / n;
                    p.emplace_back(a + f * (b - a));
                    heading.emplace_back(t);
                    arc.emplace_back(at + f * len);
                    width.emplace_back(float(w[j] + f * (w[j + 1] - w[j])));
                }
                at += len;
            }
            vertex.emplace_back(p.size());
            p.emplace_back(unscale(pts.back()));
            heading.emplace_back(heading.back());
            arc.emplace_back(at);
            width.emplace_back(w.back());

            share.reserve(p.size());
            for (size_t i = 0; i < p.size(); ++i)
                share.emplace_back(std::min(arc[i], at - arc[i]) < TaperEnds ? 1. :
                                                                                room(r, p[i], heading[i], arc[i], own_spacing(width[i])));
            if (std::all_of(share.begin(), share.end(), [](double s) { return s >= 1.; }))
                continue;


            for (size_t j = 0, lo = 0, hi = 0; j < vertex.size(); ++j) {
                const size_t i = vertex[j];
                while (arc[lo] < arc[i] - 0.5 * TaperSmooth)
                    ++lo;
                while (hi + 1 < share.size() && arc[hi + 1] <= arc[i] + 0.5 * TaperSmooth)
                    ++hi;
                double sum = 0., weight = 0.;
                for (size_t k = lo; k <= std::max(hi, i); ++k) {
                    const double dl = 0.5 * ((k + 1 < share.size() ? arc[k + 1] : arc[k]) - (k > 0 ? arc[k - 1] : arc[k])) + 1e-6;
                    sum += share[k] * dl;
                    weight += dl;
                }
                widths[r][j] = float(width[i] - (1. - std::min(1., sum / weight)) * own_spacing(width[i]));
            }
        }
    });
}

std::vector<Polylines> link_paths(Polylines &&paths, const ExPolygons &area, double spacing, double max_glide)
{

    const ExPolygons inside_area  = offset_ex(area, float(scale_(0.25 * spacing)));
    const AreaEdges  inside(inside_area);
    const double     extrude_link = scale_(2. * spacing);
    const double     glide_link   = scale_(max_glide);


    const Polygons   rails        = to_polygons(offset_ex(area, -float(scale_(spacing))));


    RoadGrid               roads;
    std::vector<Polylines> out;
    for (Polyline &pl : paths) {
        if (!pl.is_valid())
            continue;
        if (!out.empty()) {
            Polyline     &last = out.back().back();
            const Point   a    = last.last_point(), b = pl.first_point();
            const double  d    = (b - a).cast<double>().norm();
            if (d <= extrude_link && link_inside(a, b, inside) && !roads.crosses(a, b)) {

                roads.add(a, b);
                roads.add(pl);
                last.points.insert(last.points.end(), pl.points.begin() + (a == b ? 1 : 0), pl.points.end());
                continue;
            }
            if (Polyline route = glide_route(a, b, inside, rails, 3. * scale_(spacing), glide_link); route.is_valid() && !roads.crosses(route)) {
                roads.add(pl);
                out.back().emplace_back(std::move(route));
                out.back().emplace_back(std::move(pl));
                continue;
            }
        }
        roads.add(pl);
        out.emplace_back(Polylines{ std::move(pl) });
    }
    return out;
}

Polygons linked_cover(const std::vector<Polylines> &linked, double width)
{
    Polylines runs;
    for (const Polylines &chain : linked)
        for (size_t i = 0; i < chain.size(); i += 2)
            runs.emplace_back(chain[i]);
    return offset(runs, float(scale_(0.5 * width)), jtRound, scale_(0.01), etOpenRound);
}

bool cover_has_gaps(const ExPolygons &inner, const std::vector<Polylines> &linked, double width, const Polylines &variable,
                    const std::vector<std::vector<float>> &widths, double spacing)
{
    if (inner.empty())
        return false;
    const double pitch = 0.15 * spacing;
    BoundingBoxf bb;
    for (const ExPolygon &e : inner) {
        const BoundingBox b = get_extents(e);
        bb.merge(unscale(b.min));
        bb.merge(unscale(b.max));
    }

    const HeightField grid = HeightField::layout(bb, pitch);
    const int         nx = grid.nx(), ny = grid.ny();
    Mask              open = rasterize(inner, grid);
    auto cover = [&](const Point &pa, const Point &pb, double r) {
        const Vec2d  a = unscale(pa), b = unscale(pb), d = b - a;
        const double l2 = d.squaredNorm();
        const int    ix0 = std::max(0, int(std::floor((std::min(a.x(), b.x()) - r - bb.min.x()) / pitch)));
        const int    ix1 = std::min(nx - 1, int(std::ceil((std::max(a.x(), b.x()) + r - bb.min.x()) / pitch)));
        const int    iy0 = std::max(0, int(std::floor((std::min(a.y(), b.y()) - r - bb.min.y()) / pitch)));
        const int    iy1 = std::min(ny - 1, int(std::ceil((std::max(a.y(), b.y()) + r - bb.min.y()) / pitch)));
        for (int iy = iy0; iy <= iy1; ++iy)
            for (int ix = ix0; ix <= ix1; ++ix) {
                unsigned char &c = open[size_t(iy) * nx + ix];
                if (!c)
                    continue;
                const Vec2d  q = grid.node(ix, iy) - a;
                const double t = l2 > 0. ? std::clamp(q.dot(d) / l2, 0., 1.) : 0.;
                if ((q - t * d).squaredNorm() <= r * r)
                    c = 0;
            }
    };
    for (const Polylines &chain : linked)
        for (size_t i = 0; i < chain.size(); i += 2)
            for (size_t j = 0; j + 1 < chain[i].points.size(); ++j)
                cover(chain[i].points[j], chain[i].points[j + 1], 0.5 * width);
    for (size_t i = 0; i < variable.size(); ++i)
        for (size_t j = 0; j + 1 < variable[i].points.size(); ++j)
            cover(variable[i].points[j], variable[i].points[j + 1], 0.5 * std::min(widths[i][j], widths[i][j + 1]));

    for (int iy = 1; iy + 1 < ny; ++iy)
        for (int ix = 1; ix + 1 < nx; ++ix) {
            const size_t n = size_t(iy) * nx + ix;
            if (open[n] && open[n - 1] && open[n + 1] && open[n - size_t(nx)] && open[n + size_t(nx)])
                return true;
        }
    return false;
}


constexpr double PinholeWidth = 0.4;

Polylines pinhole_roads(const ExPolygons &area, Polygons cover, double width, double spacing)
{
    Polylines        out;
    const ExPolygons inner  = offset_ex(area, -float(scale_(0.5 * spacing)));
    const float      radius = float(scale_(0.5 * PinholeWidth * spacing));
    ExPolygons       last;

    for (int round = 0; round < 3 && !inner.empty(); ++round) {






        if (round > 0) {
            bool found = false;
            for (const ExPolygon &window : offset_ex(last, float(scale_(0.5 * width + 0.5)) + 8.f * radius)) {
                const BoundingBox box  = get_extents(window);
                const ExPolygons  gaps = diff_ex(intersection_ex(ClipperUtils::clip_clipper_polygons_with_subject_bbox(inner, box), ExPolygons{ window }),
                                                 ClipperUtils::clip_clipper_polygons_with_subject_bbox(cover, box));
                if (found = !opening_ex(gaps, radius).empty(); found)
                    break;
            }
            if (!found)
                break;
        }
        const ExPolygons holes = opening_ex(diff_ex(inner, cover), radius);
        if (holes.empty())
            break;
        Polylines roads;
        for (const ExPolygon &hole : holes) {
            Polylines axis;


            try {
                axis = hole.medial_axis(0., 2. * scale_(spacing));
            } catch (const Slic3r::LogicError &) {
                axis.clear();
            }
            if (axis.empty()) {

                const Points &pts = hole.contour.points;
                Vec2d         c   = Vec2d::Zero();
                for (const Point &p : pts)
                    c += p.cast<double>();
                c /= double(pts.size());
                double xx = 0., xy = 0., yy = 0.;
                for (const Point &p : pts) {
                    const Vec2d d = p.cast<double>() - c;
                    xx += d.x() * d.x();
                    xy += d.x() * d.y();
                    yy += d.y() * d.y();
                }
                const double a    = 0.5 * std::atan2(2. * xy, xx - yy);
                const double half = get_extents(hole).size().cast<double>().norm();
                const Vec2d  d(half * std::cos(a), half * std::sin(a));
                axis = intersection_pl(Polylines{ Polyline((c - d).cast<coord_t>(), (c + d).cast<coord_t>()) }, ExPolygons{ hole });
            }
            for (Polyline &pl : axis)
                if (pl.is_valid())
                    roads.emplace_back(std::move(pl));
        }
        if (roads.empty())
            break;
        last = holes;
        append(cover, offset(roads, float(scale_(0.5 * width)), jtRound, scale_(0.01), etOpenRound));
        append(out, std::move(roads));
    }
    return out;
}

void lift_linked(const std::vector<Polylines> &linked, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                 int patch_id, int shell, ExtrusionEntitiesPtr &out)
{
    const LiftFn glide_at = [&at](const Vec2d &p, double &z, float &bead) {
        const bool ok = at(p, z, bead);
        bead          = 0.f;
        return ok;
    };

    auto trim_glides = [](ExtrusionPathConformal &path) {
        size_t end = path.bead_height.size();
        while (end > 1 && path.bead_height[end - 1] == 0.f && path.bead_height[end - 2] == 0.f)
            --end;
        size_t begin = 0;
        while (begin + 1 < end && path.bead_height[begin] == 0.f && path.bead_height[begin + 1] == 0.f)
            ++begin;
        path.polyline.points.erase(path.polyline.points.begin() + end, path.polyline.points.end());
        path.polyline.points.erase(path.polyline.points.begin(), path.polyline.points.begin() + begin);
        path.bead_height.erase(path.bead_height.begin() + end, path.bead_height.end());
        path.bead_height.erase(path.bead_height.begin(), path.bead_height.begin() + begin);
    };
    auto is_glide = [](const ExtrusionPathConformal &path) {
        return std::all_of(path.bead_height.begin(), path.bead_height.end(), [](float h) { return h == 0.f; });
    };
    for (const Polylines &runs : linked) {
        ExtrusionEntitiesPtr pieces;
        for (size_t i = 0; i < runs.size(); ++i)
            lift_polylines({ runs[i] }, proto, i % 2 == 0 ? at : glide_at, host_z, resample, patch_id, shell, pieces);

        std::unique_ptr<ExtrusionPathConformal> current;
        auto flush = [&]() {
            if (current) {
                trim_glides(*current);
                if (!is_glide(*current))
                    out.emplace_back(current.release());
            }
            current.reset();
        };
        for (ExtrusionEntity *ee : pieces) {
            std::unique_ptr<ExtrusionPathConformal> piece(static_cast<ExtrusionPathConformal *>(ee));


            if (current && (current->polyline.points.back() - piece->polyline.points.front()).cast<double>().norm() <= SCALED_EPSILON) {


                const bool same = current->bead_height.back() == piece->bead_height.front();
                current->polyline.points.insert(current->polyline.points.end(), piece->polyline.points.begin() + (same ? 1 : 0),
                                                piece->polyline.points.end());
                current->bead_height.insert(current->bead_height.end(), piece->bead_height.begin() + (same ? 1 : 0),
                                            piece->bead_height.end());
                continue;
            }
            flush();
            current = std::move(piece);
        }
        flush();
    }
}

bool prepend_glide(ExtrusionPathConformal &path, const Point3 &from, const ExPolygons &area, const LiftFn &at, double host_z,
                   double resample, double max_glide)
{
    if (path.polyline.points.empty())
        return false;

    const Polyline route = glide_route(from.to_point(), path.polyline.points.front().to_point(), AreaEdges(area),
                                       to_polygons(offset_ex(area, -float(scale_(0.5)))), scale_(1.5), scale_(max_glide));
    if (!route.is_valid())
        return false;
    Points3            pts{ from };
    std::vector<float> heights{ 0.f };
    for (size_t i = 0; i + 1 < route.points.size(); ++i) {
        const Vec2d pa = unscale(route.points[i]), pb = unscale(route.points[i + 1]);
        const int   n  = std::max(1, int(std::ceil((pb - pa).norm() / resample)));
        for (int j = 0; j < n; ++j) {
            const Vec2d p = pa + (pb - pa) * (double(j) / n);
            double      z = 0.;
            float       bead = 0.f;
            if (!at(p, z, bead))
                return false;
            const Point3 q(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(z - host_z)));

            if (i == 0 && j == 0 && q.z() < from.z() - scale_(0.01))
                return false;
            if (q != pts.back()) {
                pts.emplace_back(q);
                heights.emplace_back(0.f);
            }
        }
    }

    pts.emplace_back(path.polyline.points.front());
    heights.emplace_back(0.f);
    path.polyline.points.insert(path.polyline.points.begin(), pts.begin(), pts.end());
    path.bead_height.insert(path.bead_height.begin(), heights.begin(), heights.end());
    if (!path.bead_width.empty())
        path.bead_width.insert(path.bead_width.begin(), pts.size(), path.bead_width.front());
    if (!path.tool_axis.empty())
        path.tool_axis.insert(path.tool_axis.begin(), pts.size(), path.tool_axis.front());
    return true;
}

}
