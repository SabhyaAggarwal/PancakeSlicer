#include "Seams.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

Points resample_closed(const Polygon &loop, double step)
{
    Points        out;
    const Points &pts  = loop.points;
    double        pos  = 0.;
    double        next = 0.;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2d  a = pts[i].cast<double>(), b = pts[(i + 1) % pts.size()].cast<double>();
        const double l = (b - a).norm();
        for (; next < pos + l; next += step)
            out.emplace_back((a + (b - a) * ((next - pos) / l)).cast<coord_t>());
        pos += l;
    }
    if (out.empty() && !pts.empty())
        out.emplace_back(pts.front());
    return out;
}

double dist2_to_segment(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    const Vec2d  d = b - a;
    const double t = d.squaredNorm() > 0. ? std::clamp((p - a).dot(d) / d.squaredNorm(), 0., 1.) : 0.;
    return (a + t * d - p).squaredNorm();
}

bool near_facet(const std::vector<SeamPaint::Facet> &facets, const Point &p, double z, double reach)
{
    const Vec2d q = p.cast<double>();
    for (const SeamPaint::Facet &f : facets) {
        if (z < f.z_min - 2. || z > f.z_max + 2.)
            continue;
        if (f.xy.contains(p))
            return true;
        const Points &v = f.xy.points;
        for (size_t i = 0; i < v.size(); ++i)
            if (dist2_to_segment(q, v[i].cast<double>(), v[(i + 1) % v.size()].cast<double>()) <= reach * reach)
                return true;
    }
    return false;
}

}

namespace {

bool segments_cross(const Point &a, const Point &b, const Point &c, const Point &d)
{
    auto orient = [](const Point &p, const Point &q, const Point &r) {
        const double v = double(q.x() - p.x()) * double(r.y() - p.y()) - double(q.y() - p.y()) * double(r.x() - p.x());
        return (v > 0.) - (v < 0.);
    };
    return orient(a, b, c) * orient(a, b, d) < 0 && orient(c, d, a) * orient(c, d, b) < 0;
}

}

template<typename Fn> bool RoadGrid::visit(const Point &a, const Point &b, Fn &&fn) const
{
    const int64_t x0 = std::min(a.x(), b.x()) / m_cell - 1, x1 = std::max(a.x(), b.x()) / m_cell + 1;
    const int64_t y0 = std::min(a.y(), b.y()) / m_cell - 1, y1 = std::max(a.y(), b.y()) / m_cell + 1;
    for (int64_t ix = x0; ix <= x1; ++ix)
        for (int64_t iy = y0; iy <= y1; ++iy)
            if (fn((uint64_t(uint32_t(ix)) << 32) | uint32_t(iy)))
                return true;
    return false;
}

void RoadGrid::add(const Point &a, const Point &b)
{
    if (a != b)
        this->visit(a, b, [&](uint64_t key) { m_cells[key].emplace_back(a, b); return false; });
}

void RoadGrid::add(const Polyline &road)
{
    for (size_t i = 0; i + 1 < road.points.size(); ++i)
        this->add(road.points[i], road.points[i + 1]);
}

void RoadGrid::add(const ExtrusionPathConformal &path)
{
    const Points3 &pts = path.polyline.points;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        if (path.bead_height[i] > 0.f && path.bead_height[i + 1] > 0.f)
            this->add(pts[i].to_point(), pts[i + 1].to_point());
}

bool RoadGrid::crosses(const Point &a, const Point &b) const
{
    return this->visit(a, b, [&](uint64_t key) {
        const auto it = m_cells.find(key);
        if (it != m_cells.end())
            for (const auto &[c, d] : it->second)
                if (segments_cross(a, b, c, d))
                    return true;
        return false;
    });
}

bool RoadGrid::crosses(const Polyline &route) const
{
    for (size_t i = 0; i + 1 < route.points.size(); ++i)
        if (this->crosses(route.points[i], route.points[i + 1]))
            return true;
    return false;
}

SeamPaint seam_paint(const PrintObject &object)
{
    SeamPaint         out;
    const Transform3d trafo    = object.trafo_sliced();
    const double      z_offset = object.slicing_parameters().object_print_z_min;
    for (const ModelVolume *mv : object.model_object()->volumes) {
        if (!mv->is_model_part())
            continue;
        const Transform3d t = trafo * mv->get_matrix();
        for (const EnforcerBlockerType type : { EnforcerBlockerType::ENFORCER, EnforcerBlockerType::BLOCKER }) {
            const indexed_triangle_set its = mv->seam_facets.get_facets_strict(*mv, type);
            for (const stl_triangle_vertex_indices &tri : its.indices) {
                SeamPaint::Facet facet;
                facet.z_min = std::numeric_limits<float>::max();
                facet.z_max = std::numeric_limits<float>::lowest();
                for (int i = 0; i < 3; ++i) {
                    const Vec3d v = t * its.vertices[size_t(tri[i])].cast<double>();
                    facet.xy.points.emplace_back(Point::new_scale(v.x(), v.y()));
                    facet.z_min = std::min(facet.z_min, float(v.z() + z_offset));
                    facet.z_max = std::max(facet.z_max, float(v.z() + z_offset));
                }
                (type == EnforcerBlockerType::ENFORCER ? out.enforcers : out.blockers).emplace_back(std::move(facet));
            }
        }
    }
    return out;
}

Point shell_seam(const ExPolygons &area, const HeightField &surface, SeamPosition seam, unsigned salt, const SeamPaint *paint, double reach)
{
    struct Candidate
    {
        Point  p;
        double z, y, concave;
    };
    std::vector<Candidate> cands;
    const double           step   = scale_(0.5);
    const int              window = 4;
    for (const ExPolygon &expoly : area) {
        Polygon contour = expoly.contour;
        contour.make_counter_clockwise();
        const Points pts = resample_closed(contour, step);
        const int    n   = int(pts.size());
        for (int i = 0; i < n; ++i) {
            const Vec2d  back  = pts[size_t((i - window % n + n) % n)].cast<double>();
            const Vec2d  here  = pts[size_t(i)].cast<double>();
            const Vec2d  ahead = pts[size_t((i + window) % n)].cast<double>();
            const Vec2d  d1 = here - back, d2 = ahead - here;
            const double turn = n >= 3 ? std::atan2(d1.x() * d2.y() - d1.y() * d2.x(), d1.dot(d2)) : 0.;
            const float  s = surface.sample(unscale(pts[size_t(i)]));
            cands.push_back({ pts[size_t(i)], std::isnan(s) ? std::numeric_limits<double>::max() : double(s),
                              unscale<double>(pts[size_t(i)].y()), -turn });
        }
    }
    if (cands.empty())
        return Point(0, 0);

    if (paint != nullptr && !paint->empty()) {
        const double r = scale_(reach);
        std::vector<Candidate> enforced, free;
        for (const Candidate &c : cands) {
            if (near_facet(paint->enforcers, c.p, c.z, r))
                enforced.emplace_back(c);
            else if (!near_facet(paint->blockers, c.p, c.z, r))
                free.emplace_back(c);
        }
        if (!enforced.empty())
            cands = std::move(enforced);
        else if (!free.empty())
            cands = std::move(free);
    }

    if (seam == spRandom) {
        const unsigned h = (salt + 1u) * 2654435761u;
        return cands[size_t(h % cands.size())].p;
    }
    auto lowest_then_rear = [](const Candidate &a, const Candidate &b) {
        return a.z < b.z - 0.02 || (a.z <= b.z + 0.02 && a.y > b.y);
    };
    if (seam == spRear || seam == spAlignedBack)
        return std::min_element(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) {
                   return a.y > b.y + 0.05 || (a.y >= b.y - 0.05 && a.z < b.z);
               })->p;
    const double deepest = std::max_element(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) {
                               return a.concave < b.concave;
                           })->concave;
    constexpr double Corner = 0.52;
    if (deepest >= Corner) {
        std::vector<Candidate> corners;
        for (const Candidate &c : cands)
            if (c.concave >= 0.8 * deepest)
                corners.emplace_back(c);
        return std::min_element(corners.begin(), corners.end(), lowest_then_rear)->p;
    }
    return std::min_element(cands.begin(), cands.end(), lowest_then_rear)->p;
}

Polyline seam_line(const HeightField &surface, const Point &seam, const ExPolygons &area)
{
    Polyline out;
    out.points.emplace_back(seam);
    const double step = surface.pitch();
    Vec2d        p    = unscale(seam);
    float        z    = surface.sample(p);
    if (std::isnan(z))
        return out;
    for (int i = 0; i < 100000; ++i) {
        const Vec2d g = surface.gradient(p);
        if (std::isnan(g.x()) || std::isnan(g.y()) || g.norm() < 1e-3)
            break;
        const Vec2d q  = p + step * g.normalized();
        const float zq = surface.sample(q);
        const Point pq = Point::new_scale(q.x(), q.y());
        if (std::isnan(zq) || zq <= z ||
            (i > 0 && std::none_of(area.begin(), area.end(), [&pq](const ExPolygon &e) { return e.contains(pq); })))
            break;
        p = q;
        z = zq;
        out.points.emplace_back(pq);
    }
    return out;
}

void order_band_pieces(ExtrusionEntitiesPtr &pieces, Point &cursor, RoadGrid &band_roads, const GlideZ &glide_z, double max_glide,
                       double step, bool hidden)
{
    std::vector<ExtrusionPathConformal *> todo;
    for (ExtrusionEntity *ee : pieces)
        todo.emplace_back(static_cast<ExtrusionPathConformal *>(ee));
    std::vector<bool>  used(todo.size(), false);
    ExtrusionEntitiesPtr out;
    ExtrusionPathConformal *current = nullptr;
    RoadGrid           roads;
    const double       reach = scale_(max_glide);
    constexpr size_t   Tries = 8;

    struct End
    {
        double d;
        size_t i;
        bool   reversed;
    };
    std::vector<End> ends;
    auto glide_to = [&](const Point3 &a, const Point3 &b, Points3 &route) {
        route.clear();
        const Vec2d  pa = unscale(a.to_point()), pb = unscale(b.to_point());
        const double l  = (pb - pa).norm();
        if (l > max_glide || (!hidden && roads.crosses(a.to_point(), b.to_point())))
            return false;
        const int k = std::max(1, int(std::ceil(l / step)));
        for (int j = 1; j < k; ++j) {
            const Vec2d p = pa + (pb - pa) * (double(j) / k);
            double      z = 0.;
            if (!glide_z(p, z))
                return false;
            route.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(z)));
        }
        return true;
    };
    auto mergeable = [](const ExtrusionPathConformal &a, const ExtrusionPathConformal &b) {
        return a.role() == b.role() && a.width == b.width && a.mm3_per_mm == b.mm3_per_mm && a.tool_axis.empty() && b.tool_axis.empty();
    };

    Points3 route;
    for (size_t left = todo.size(); left > 0; --left) {
        ends.clear();
        for (size_t i = 0; i < todo.size(); ++i)
            if (!used[i]) {
                ends.push_back({ (todo[i]->first_point() - cursor).cast<double>().norm(), i, false });
                if (todo[i]->can_reverse())
                    ends.push_back({ (todo[i]->last_point() - cursor).cast<double>().norm(), i, true });
            }
        const size_t head = std::min(ends.size(), Tries);
        std::partial_sort(ends.begin(), ends.begin() + head, ends.end(), [](const End &a, const End &b) { return a.d < b.d; });

        int  pick  = -1;
        bool glide = false;
        if (current != nullptr)
            for (size_t k = 0; k < head && ends[k].d <= reach; ++k) {
                ExtrusionPathConformal &cand = *todo[ends[k].i];
                if (!mergeable(*current, cand))
                    continue;
                const Point3 b = ends[k].reversed ? cand.polyline.points.back() : cand.polyline.points.front();
                if (glide_to(current->polyline.points.back(), b, route)) {
                    pick  = int(k);
                    glide = true;
                    break;
                }
            }
        if (pick < 0) {
            for (size_t k = 0; k < head && pick < 0; ++k) {
                const ExtrusionPathConformal &cand = *todo[ends[k].i];
                if (!band_roads.crosses(cursor, ends[k].reversed ? cand.last_point() : cand.first_point()))
                    pick = int(k);
            }
            if (pick < 0)
                pick = 0;
        }

        const End &e = ends[size_t(pick)];
        used[e.i]    = true;
        ExtrusionPathConformal *path = todo[e.i];
        if (e.reversed)
            path->reverse();
        {
            Points3            &pts = path->polyline.points;
            std::vector<float> &h   = path->bead_height;
            for (size_t i = 0; !hidden && path->tool_axis.empty() && i + 1 < pts.size(); ++i)
                if (h[i] == 0.f && h[i + 1] == 0.f && roads.crosses(pts[i].to_point(), pts[i + 1].to_point())) {
                    size_t g0 = i, g1 = i + 1;
                    while (g0 > 0 && h[g0 - 1] == 0.f)
                        --g0;
                    while (g1 + 1 < pts.size() && h[g1 + 1] == 0.f)
                        ++g1;
                    Points3            tail(pts.begin() + g1, pts.end());
                    std::vector<float> tail_h(h.begin() + g1, h.end());
                    if (g0 < 2) {
                        pts = std::move(tail);
                        h   = std::move(tail_h);
                        i   = size_t(-1);
                        continue;
                    }
                    pts.resize(g0);
                    h.resize(g0);
                    if (tail.size() >= 2) {
                        auto *rest     = new ExtrusionPathConformal(Polyline3(std::move(tail)), *path, std::move(tail_h));
                        rest->patch_id = path->patch_id;
                        rest->shell    = path->shell;
                        todo.emplace_back(rest);
                        used.emplace_back(false);
                        ++left;
                    }
                    break;
                }
        }
        roads.add(*path);
        band_roads.add(*path);
        cursor = path->last_point();
        if (!glide) {
            out.emplace_back(path);
            current = path;
            continue;
        }
        Points3            &pts     = current->polyline.points;
        std::vector<float> &heights = current->bead_height;
        const Point3        a       = pts.back();
        const Point3        b       = path->polyline.points.front();
        if (a != b) {
            pts.emplace_back(a);
            heights.emplace_back(0.f);
            for (const Point3 &q : route) {
                pts.emplace_back(q);
                heights.emplace_back(0.f);
            }
            pts.emplace_back(b);
            heights.emplace_back(0.f);
        }
        const bool same = a == b && heights.back() == path->bead_height.front();
        pts.insert(pts.end(), path->polyline.points.begin() + (same ? 1 : 0), path->polyline.points.end());
        heights.insert(heights.end(), path->bead_height.begin() + (same ? 1 : 0), path->bead_height.end());
        delete path;
    }
    pieces = std::move(out);
}

bool glide_from(ExtrusionPathConformal &path, const Point3 &from, const GlideZ &glide_z, double max_glide, double step)
{
    if (path.polyline.points.empty() || !path.tool_axis.empty())
        return false;
    const Point3 to = path.polyline.points.front();
    const Vec2d  pa = unscale(from.to_point()), pb = unscale(to.to_point());
    const double l  = (pb - pa).norm();
    if (l > max_glide)
        return false;
    Points3      pts{ from };
    const int    k = std::max(1, int(std::ceil(l / step)));
    for (int j = 0; j < k; ++j) {
        const Vec2d p = pa + (pb - pa) * (double(j) / k);
        double      z = 0.;
        if (!glide_z(p, z))
            return false;
        const Point3 q(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(z)));
        if (j == 0 && q.z() < from.z())
            return false;
        if (q != pts.back())
            pts.emplace_back(q);
    }
    pts.emplace_back(to);
    path.polyline.points.insert(path.polyline.points.begin(), pts.begin(), pts.end());
    path.bead_height.insert(path.bead_height.begin(), pts.size(), 0.f);
    return true;
}

}
