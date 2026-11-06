#include "LineSplit.hpp"

#include "AABBTreeLines.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ClipperZUtils.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "SVG.hpp"
#include "Utils.hpp"
#include <limits>
#include "libslic3r/libslic3r.h"
#include <algorithm>
#include <cstddef>
#include <cassert>
#include <vector>
#include <utility>



namespace Slic3r {
namespace Algorithm {

#ifdef DEBUG_SPLIT_LINE
static std::atomic<std::uint32_t> g_dbg_id = 0;
#endif


static constexpr auto CLIP_IDX = std::numeric_limits<coord_t>::max();

static void cb_split_line(const ClipperZUtils::ZPoint& e1bot,
                   const ClipperZUtils::ZPoint& e1top,
                   const ClipperZUtils::ZPoint& e2bot,
                   const ClipperZUtils::ZPoint& e2top,
                   ClipperZUtils::ZPoint&       pt)
{
    coord_t zs[4]{e1bot.z(), e1top.z(), e2bot.z(), e2top.z()};
    std::sort(zs, zs + 4);
    pt.z() = -(zs[0] + 1);
}

static bool is_src(const ClipperZUtils::ZPoint& p) { return p.z() >= 0 && p.z() != CLIP_IDX; }
static bool is_clip(const ClipperZUtils::ZPoint& p) { return p.z() == CLIP_IDX; }
static bool is_new(const ClipperZUtils::ZPoint& p) { return p.z() < 0; }
static size_t to_src_idx(const ClipperZUtils::ZPoint& p)
{
    assert(!is_clip(p));
    if (is_src(p)) {
        return p.z();
    } else {
        return -p.z() - 1;
    }
}

static Point to_point(const ClipperZUtils::ZPoint& p) { return {p.x(), p.y()}; }

using SplitNode = std::vector<ClipperZUtils::ZPath*>;


static bool point_on_line(const Point& p, const Line& l)
{

    const Vec2crd d1 = l.b - l.a;
    const Vec2crd d2 = p - l.a;
    if (d1.x() * d2.y() != d1.y() * d2.x()) {
        return false;
    }


    if (l.a.x() != l.b.x())
        return (p.x() > l.a.x()) == (p.x() < l.b.x());
    else
        return (p.y() > l.a.y()) == (p.y() < l.b.y());
}

SplittedLine do_split_line(const ClipperZUtils::ZPath& path, const ExPolygons& clip, bool closed)
{
    assert(path.size() > 1);
#ifdef DEBUG_SPLIT_LINE
    const auto  dbg_path_points = ClipperZUtils::from_zpath<false>(path);
    BoundingBox dbg_bbox = get_extents(clip);
    dbg_bbox.merge(get_extents(dbg_path_points));
    dbg_bbox.offset(scale_(1.));
    const std::uint32_t dbg_id = g_dbg_id++;
    {
        ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_input.svg", dbg_id).c_str(), dbg_bbox);
        svg.draw(clip, "red", 0.5);
        svg.draw_outline(clip, "red");
        svg.draw(Polyline{dbg_path_points});
        svg.draw(dbg_path_points);
        svg.Close();
    }
#endif

    ClipperZUtils::ZPaths intersections;

    {

        ClipperZUtils::ZPaths clip_path;
        for (const auto& exp : clip) {
            clip_path.emplace_back(ClipperZUtils::to_zpath<false>(exp.contour.points, CLIP_IDX));
            for (const Polygon& hole : exp.holes)
                clip_path.emplace_back(ClipperZUtils::to_zpath<false>(hole.points, CLIP_IDX));
        }

        intersections = ClipperZUtils::clip_zpaths(ctIntersection, ClipperZUtils::ZPaths{ path }, true, clip_path, cb_split_line, true);
    }
    if (intersections.empty()) {
        return {};
    }

#ifdef DEBUG_SPLIT_LINE
    {
        int i = 0;
        for (const auto& segment : intersections) {
            ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_seg_%d.svg", dbg_id, i).c_str(), dbg_bbox);
            svg.draw(clip, "red", 0.5);
            svg.draw_outline(clip, "red");
            const auto segment_points = ClipperZUtils::from_zpath<false>(segment);
            svg.draw(Polyline{segment_points});
            for (const ClipperZUtils::ZPoint& p : segment) {
                const auto z = p.z();
                if (is_new(p)) {
                    svg.draw(to_point(p), "yellow");
                } else if (is_clip(p)) {
                    svg.draw(to_point(p), "red");
                } else {
                    svg.draw(to_point(p), "black");
                }
            }
            svg.Close();
            i++;
        }
    }
#endif


    std::vector<SplitNode> split_chain;
    {



        AABBTreeLines::LinesDistancer<Line> aabb_tree;
        const auto                          resolve_clip_point = [&path, &aabb_tree](ClipperZUtils::ZPoint& zp) {
            if (!is_clip(zp)) {
                return;
            }

            if (aabb_tree.get_lines().empty()) {
                Lines lines;
                lines.reserve(path.size() - 1);
                for (auto it = path.begin() + 1; it != path.end(); ++it) {
                    lines.emplace_back(to_point(it[-1]), to_point(*it));
                }
                aabb_tree = AABBTreeLines::LinesDistancer(lines);
            }

            const Point p = to_point(zp);
            const auto possible_edges = aabb_tree.all_lines_in_radius(p, SCALED_EPSILON);
            assert(!possible_edges.empty());
            for (const size_t l : possible_edges) {

                const Line line(to_point(path[l]), to_point(path[l + 1]));
                if (p == line.a) {
                    zp.z() = path[l].z();
                    break;
                }
                if (p == line.b) {
                    zp.z() = path[l + 1].z();
                    break;
                }
                if (point_on_line(p, line)) {
                    zp.z() = -(path[l].z() + 1);
                    break;
                }
            }
            if (is_clip(zp)) {

                zp.z() = -(path[possible_edges[0]].z() + 1);
            }
        };

        split_chain.assign(path.size(), {});
        for (ClipperZUtils::ZPath& segment : intersections) {
            assert(segment.size() >= 2);

            std::for_each(segment.begin(), segment.end(), resolve_clip_point);


            std::sort(segment.begin(), segment.end(), [&path](const ClipperZUtils::ZPoint& a, const ClipperZUtils::ZPoint& b) -> bool {
                if (is_new(a) && is_new(b) && a.z() == b.z()) {

                    const auto src = to_point(path[-a.z() - 1]);
                    return (to_point(a) - src).squaredNorm() < (to_point(b) - src).squaredNorm();
                }
                const auto a_idx = to_src_idx(a);
                const auto b_idx = to_src_idx(b);
                if (a_idx == b_idx) {

                    return is_src(a);
                } else {
                    return a_idx < b_idx;
                }
            });


            ClipperZUtils::ZPoint& front = segment.front();
            const ClipperZUtils::ZPoint* previous_src_point = nullptr;
            if (is_src(front)) {




                auto& node = split_chain[front.z()];
                node.insert(node.begin(), &segment);

                previous_src_point = &front;
            } else if (is_new(front)) {
                const auto id = -front.z() - 1;
                const ClipperZUtils::ZPoint& src_p = path[id];
                const auto dist2 = (front - src_p).block<2, 1>(0,0).squaredNorm();

                auto& node = split_chain[id];
                auto it = std::find_if(node.begin(), node.end(), [dist2, &src_p](const ClipperZUtils::ZPath* p) {
                    const ClipperZUtils::ZPoint& p_front = p->front();
                    if (is_src(p_front)) {
                        return false;
                    }

                    const auto dist2_2 = (p_front - src_p).block<2, 1>(0, 0).squaredNorm();
                    return dist2_2 > dist2;
                });

                node.insert(it, &segment);

                previous_src_point = &src_p;
            } else {
                assert(false);
            }


            for (ClipperZUtils::ZPoint& p : segment) {
                assert(!is_new(p) || p == front || p == segment.back());
                if (is_src(p)) {
                    previous_src_point = &p;
                } else if (is_clip(p)) {

                    p.z() = -(previous_src_point->z() + 1);
                }
            }
        }
    }


    SplittedLine result;
    size_t       idx  = 0;
    while (idx < split_chain.size()) {
        const ClipperZUtils::ZPoint& p = path[idx];
        const auto& node = split_chain[idx];
        if (node.empty()) {
            result.emplace_back(to_point(p), false, idx);
            idx++;
        } else {
            if (!is_src(node.front()->front())) {
                if (result.empty() || result.back().get_src_index() != to_src_idx(p)) {


                    result.emplace_back(to_point(p), false, idx);
                }
            }
            for (const auto segment : node) {
                for (const ClipperZUtils::ZPoint& sp : *segment) {
                    assert(!is_clip(sp));
                    result.emplace_back(to_point(sp), true, sp.z());
                }
                result.back().clipped = false;
            }


            const auto back = result.back().src_idx;
            if (back < 0) {
                auto next_idx = -back - 1;
                if (next_idx == idx) {
                    next_idx++;
                } else if (split_chain[next_idx].empty()) {
                    next_idx++;
                }
                idx = next_idx;
            } else {
                result.pop_back();
                idx = back;
            }
        }
    }


#ifdef DEBUG_SPLIT_LINE
    {
        ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_result.svg", dbg_id).c_str(), dbg_bbox);
        svg.draw(clip, "red", 0.5);
        svg.draw_outline(clip, "red");
        for (auto it = result.begin() + 1; it != result.end(); ++it) {
            const auto& a = *(it - 1);
            const auto& b = *it;
            const bool  clipped = a.clipped;
            const Line  l(a.p, b.p);
            svg.draw(l, clipped ? "yellow" : "black");
        }
        svg.Close();
    }
#endif

    if (closed) {

        result.pop_back();
    }

    return result;
}

}
}
