#include "ShellPaths.hpp"

#include "BeadModel.hpp"
#include "ConformalShells.hpp"
#include "HeightField.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

constexpr int    LineAngles = 12;
constexpr double AngleStep  = PI / LineAngles;

bool link_inside(const Point &a, const Point &b, const ExPolygons &area)
{
    if (a == b)
        return true;
    for (const Polyline &outside : diff_pl(Polylines{ Polyline(a, b) }, area))
        if (outside.length() > SCALED_EPSILON)
            return false;
    return true;
}

Polyline open_loop_at(const Polygon &loop, const Point &near)
{
    const Points &pts  = loop.points;
    const Vec2d   p    = near.cast<double>();
    size_t        best = 0;
    Vec2d         foot = pts.front().cast<double>();
    double        dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2d  a = pts[i].cast<double>(), b = pts[(i + 1) % pts.size()].cast<double>();
        const Vec2d  d = b - a;
        const double t = d.squaredNorm() > 0. ? std::clamp((p - a).dot(d) / d.squaredNorm(), 0., 1.) : 0.;
        const Vec2d  f = a + t * d;
        if (const double dd = (f - p).squaredNorm(); dd < dist) {
            dist = dd;
            best = i;
            foot = f;
        }
    }
    const Point start(coord_t(std::lround(foot.x())), coord_t(std::lround(foot.y())));
    Polyline    out;
    out.points.reserve(pts.size() + 2);
    out.points.emplace_back(start);
    for (size_t j = 1; j <= pts.size(); ++j)
        if (const Point &q = pts[(best + j) % pts.size()]; q != out.points.back())
            out.points.emplace_back(q);
    if (out.points.back() != start)
        out.points.emplace_back(start);
    return out;
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

Polyline glide_route(const Point &a, const Point &b, const ExPolygons &inside, const Polygons &rails, double reach, double max_length)
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
    const double target = bead_area(width, height) / std::sqrt(1. + s * s);
    const double a      = 1. - 0.25 * PI;
    const double disc   = width * width - 4. * a * target;
    return disc < 0. ? height : float((width - std::sqrt(disc)) / (2. * a));
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

std::vector<ShellFill> plan_shell_fills(const ConformalPatch &patch, const PrintRegionConfig &config, double spacing)
{
    const int                 n       = patch.shells;
    std::vector<ShellFill>    fills(size_t(std::max(0, n)));
    const NonplanarTopPattern pattern = config.nonplanar_top_pattern.value;
    const double              user    = config.nonplanar_top_direction.value;
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

Point shell_seam(const ExPolygons &area, const HeightField &surface, SeamPosition seam, unsigned salt)
{
    Points outline;
    for (const ExPolygon &expoly : area)
        append(outline, expoly.contour.points);
    if (outline.empty())
        return Point(0, 0);
    if (seam == spRandom) {
        const unsigned h = (salt + 1u) * 2654435761u;
        return outline[size_t(h % outline.size())];
    }
    const bool rear   = seam == spRear || seam == spAlignedBack;
    size_t     best   = 0;
    double     best_z = std::numeric_limits<double>::max(), best_y = -std::numeric_limits<double>::max();
    for (size_t i = 0; i < outline.size(); ++i) {
        const float  s = surface.sample(unscale(outline[i]));
        const double z = std::isnan(s) ? std::numeric_limits<double>::max() : double(s);
        const double y = unscale<double>(outline[i].y());
        const bool   take = rear ? y > best_y + 0.01 || (y >= best_y - 0.01 && z < best_z) :
                                   z < best_z - 0.02 || (z <= best_z + 0.02 && y > best_y);
        if (take) {
            best   = i;
            best_z = z;
            best_y = y;
        }
    }
    return outline[best];
}

Polylines fill_spiral(const ExPolygons &area, double spacing, const Point &seam)
{
    const coord_t step = coord_t(scale_(spacing));
    if (step <= 0)
        return {};
    Polygons   loops;
    ExPolygons level = offset_ex(area, -0.5f * float(step));
    while (!level.empty()) {
        append(loops, to_polygons(level));
        level = offset2_ex(level, -float(step) - 0.5f * float(step), 0.5f * float(step));
    }
    loops = union_pt_chained_outside_in(loops);
    Polylines out;
    Point     pos = seam;
    for (const Polygon &loop : loops) {
        if (loop.points.size() < 3)
            continue;
        Polyline pl = open_loop_at(loop, pos);
        pl.clip_end(double(step));
        if (!pl.is_valid())
            continue;
        pos = pl.last_point();
        out.emplace_back(std::move(pl));
    }
    return out;
}

std::vector<Polylines> shell_polylines(const ExPolygons &area, const ShellFill &fill, InfillPattern line_pattern, double spacing,
                                       bool monotonic, const Flow &flow, double z, const Point &start, double max_glide,
                                       const HeightField *surface_spaced)
{
    Polylines paths;
    if (fill.spiral) {
        paths = fill_spiral(area, spacing, start);
        if (fill.outward)
            reverse_order(paths);
    } else {
        const ExPolygons inner = offset_ex(area, -float(scale_(spacing)));
        paths = surface_spaced != nullptr ?
                    fill_surface_spaced(inner, *surface_spaced, line_pattern, spacing, fill.angle, monotonic, flow, z) :
                    fill_solid(inner, line_pattern, spacing, fill.angle, monotonic, flow, z);
        if (monotonic) {
            if (!paths.empty() && (paths.back().last_point() - start).cast<double>().norm() <
                                      (paths.front().first_point() - start).cast<double>().norm())
                reverse_order(paths);
        } else
            paths = chain_polylines(std::move(paths), &start);
        const Point lines_start = paths.empty() ? start : paths.front().first_point();
        Polylines   outline;
        for (const Polygon &loop : to_polygons(offset_ex(area, -0.5f * float(scale_(spacing))))) {
            Polyline pl = open_loop_at(loop, lines_start);
            pl.clip_end(0.2 * scale_(spacing));
            if (pl.is_valid())
                outline.emplace_back(std::move(pl));
        }
        std::stable_sort(outline.begin(), outline.end(), [&lines_start](const Polyline &l, const Polyline &r) {
            return (l.first_point() - lines_start).cast<double>().norm() > (r.first_point() - lines_start).cast<double>().norm();
        });
        paths.insert(paths.begin(), std::make_move_iterator(outline.begin()), std::make_move_iterator(outline.end()));
    }

    const ExPolygons inside       = offset_ex(area, float(scale_(0.25 * spacing)));
    const double     extrude_link = scale_(2. * spacing);
    const double     glide_link   = scale_(max_glide);
    const Polygons   rails        = to_polygons(offset_ex(area, -float(scale_(0.25 * spacing))));
    std::vector<Polylines> out;
    for (Polyline &pl : paths) {
        if (!pl.is_valid())
            continue;
        if (!out.empty()) {
            Polyline     &last = out.back().back();
            const Point   a    = last.last_point(), b = pl.first_point();
            const double  d    = (b - a).cast<double>().norm();
            if (d <= extrude_link && link_inside(a, b, inside)) {
                last.points.insert(last.points.end(), pl.points.begin() + (a == b ? 1 : 0), pl.points.end());
                continue;
            }
            if (Polyline route = glide_route(a, b, inside, rails, 3. * scale_(spacing), glide_link); route.is_valid()) {
                out.back().emplace_back(std::move(route));
                out.back().emplace_back(std::move(pl));
                continue;
            }
        }
        out.emplace_back(Polylines{ std::move(pl) });
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
    const Polyline route = glide_route(from.to_point(), path.polyline.points.front().to_point(), area,
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
    if (!path.tool_axis.empty())
        path.tool_axis.insert(path.tool_axis.begin(), pts.size(), path.tool_axis.front());
    return true;
}

}
