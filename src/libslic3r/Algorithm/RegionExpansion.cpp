#include "RegionExpansion.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Polygon.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cassert>
#include <iterator>
#include <cstdint>
#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/ClipperZUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Utils.hpp>

#include <math.h>
#include <numeric>
#include <vector>
#include <utility>
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r {
namespace Algorithm {

inline double clipper_round_offset_error(double offset, double arc_tolerance)
{
    static constexpr const double def_arc_tolerance = 0.25;
    const double y =
        arc_tolerance <= 0 ?
            def_arc_tolerance :
            arc_tolerance > offset * def_arc_tolerance ?
                offset * def_arc_tolerance :
                arc_tolerance;
    double steps = std::min(M_PI / std::acos(1. - y / offset), offset * M_PI);
    return offset * (1. - cos(M_PI / steps));
}

RegionExpansionParameters RegionExpansionParameters::build(
    float                full_expansion,
    float                expansion_step,
    size_t               max_nr_expansion_steps)
{
    assert(full_expansion > 0);
    assert(expansion_step > 0);
    assert(max_nr_expansion_steps > 0);

    RegionExpansionParameters out;
    out.tiny_expansion = std::min(0.25f * full_expansion, scaled<float>(0.05f));
    size_t nsteps = size_t(ceil((full_expansion - out.tiny_expansion) / expansion_step));
    if (max_nr_expansion_steps > 0)
        nsteps = std::min(nsteps, max_nr_expansion_steps);
    assert(nsteps > 0);
    out.initial_step = (full_expansion - out.tiny_expansion) / nsteps;
    if (nsteps > 1 && 0.25 * out.initial_step < out.tiny_expansion) {
        nsteps       = std::max<size_t>(1, (floor((full_expansion - out.tiny_expansion) / (4. * out.tiny_expansion))));
        out.initial_step = (full_expansion - out.tiny_expansion) / nsteps;
    }
    if (0.25 * out.initial_step < out.tiny_expansion || nsteps == 1) {
        out.tiny_expansion = 0.2f * full_expansion;
        out.initial_step   = 0.8f * full_expansion;
    }
    out.other_step           = out.initial_step;
    out.num_other_steps      = nsteps - 1;

    out.arc_tolerance        = scaled<double>(0.1);

    out.max_inflation = (out.tiny_expansion + nsteps * out.initial_step) * 1.1;

    return out;
}

static ClipperZUtils::ZPaths expolygons_to_zpaths_expanded_opened(
    const ExPolygons &src, const float expansion, coord_t &base_idx)
{
    ClipperZUtils::ZPaths out;
    out.reserve(2 * std::accumulate(src.begin(), src.end(), size_t(0),
        [](const size_t acc, const ExPolygon &expoly) { return acc + expoly.num_contours(); }));
    for (const ExPolygon &expoly : src) {
        for (size_t icontour = 0; icontour < expoly.num_contours(); ++ icontour) {
            Polygon contour = expoly.contour_or_hole(icontour);
            if (! contour.is_counter_clockwise())
                contour.reverse();
            for (const Polygon &expanded : offset(contour, icontour == 0 ? expansion : -expansion, jtSquare))
                out.emplace_back(ClipperZUtils::to_zpath<true>(expanded.points, base_idx));
        }
        ++ base_idx;
    }
    return out;
}

static inline void merge_splits(ClipperZUtils::ZPaths &paths, std::vector<std::pair<ClipperZUtils::ZPoint, int>> &splits)
{
    for (auto it_path = paths.begin(); it_path != paths.end(); ) {
        ClipperZUtils::ZPath &path = *it_path;
        assert(path.size() >= 2);
        bool merged = false;
        if (path.size() >= 2) {
            const ClipperZUtils::ZPoint &front = path.front();
            const ClipperZUtils::ZPoint &back  = path.back();
            if (front.x() != back.x() || front.y() != back.y()) {
                auto find_end = [&splits](const ClipperZUtils::ZPoint &pt) -> std::pair<ClipperZUtils::ZPoint, int>* {
                    auto it = std::lower_bound(splits.begin(), splits.end(), pt,
                        [](const auto &l, const auto &r){ return ClipperZUtils::zpoint_lower(l.first, r); });
                    return it != splits.end() && it->first == pt ? &(*it) : nullptr;
                };
                auto *end = find_end(front);
                bool  end_front = true;
                if (! end) {
                    end_front = false;
                    end = find_end(back);
                }
                if (end) {
                    if (end->second == -1) {
                        end->second = int(it_path - paths.begin());
                    } else {
                        ClipperZUtils::ZPath &other_path = paths[end->second];
                        polylines_merge(other_path, other_path.front() == end->first, std::move(path), end_front);
                        if (std::next(it_path) == paths.end()) {
                            paths.pop_back();
                            break;
                        }
                        path = std::move(paths.back());
                        paths.pop_back();
                        merged = true;
                    }
                }
            }
        }
        if (! merged)
            ++ it_path;
    }
}

using AABBTreeBBoxes = AABBTreeIndirect::Tree<2, coord_t>;

static AABBTreeBBoxes build_aabb_tree_over_expolygons(const ExPolygons &expolygons) 
{
    std::vector<AABBTreeIndirect::BoundingBoxWrapper> bboxes;
    bboxes.reserve(expolygons.size());
    for (size_t i = 0; i < expolygons.size(); ++ i)
        bboxes.emplace_back(i, get_extents(expolygons[i].contour));
    AABBTreeBBoxes out;
    out.build_modify_input(bboxes);
    return out;
}

static int sample_in_expolygons(
    const AABBTreeBBoxes &aabb_tree, 
    const ExPolygons     &expolygons,
    const Point          &sample)
{
    int out = -1;
    AABBTreeIndirect::traverse(aabb_tree,
        [&sample](const AABBTreeBBoxes::Node &node) {
            return node.bbox.contains(sample);
        },
        [&expolygons, &sample, &out](const AABBTreeBBoxes::Node &node) {
            assert(node.is_leaf());
            assert(node.is_valid());
            if (expolygons[node.idx].contains(sample)) {
                out = int(node.idx);
                return false;
            }
            return true;
        });
    return out;
}

std::vector<WaveSeed> wave_seeds(
    const ExPolygons      &src,
    const ExPolygons      &boundary,
    float                  tiny_expansion,
    bool                   sorted)
{
    assert(tiny_expansion > 0);

    if (src.empty() || boundary.empty())
        return {};

    using Intersection  = ClipperZUtils::ClipperZIntersectionVisitor::Intersection;
    using Intersections = ClipperZUtils::ClipperZIntersectionVisitor::Intersections;

    ClipperZUtils::ZPaths segments;
    Intersections         intersections;

    coord_t               idx_boundary_begin = 1;
    coord_t               idx_boundary_end   = idx_boundary_begin;
    coord_t               idx_src_end;

    {
        ClipperZUtils::ClipperZIntersectionVisitor visitor(intersections);
        ClipperZUtils::ZPaths zboundary = ClipperZUtils::expolygons_to_zpaths(boundary, idx_boundary_end);
        std::vector<std::pair<ClipperZUtils::ZPoint, int>> zsrc_splits;
        idx_src_end = idx_boundary_end;
        ClipperZUtils::ZPaths zsrc = expolygons_to_zpaths_expanded_opened(src, tiny_expansion, idx_src_end);
        zsrc_splits.reserve(zsrc.size());
        for (const ClipperZUtils::ZPath &path : zsrc) {
            assert(path.size() >= 2);
            assert(path.front() == path.back());
            zsrc_splits.emplace_back(path.front(), -1);
        }
        std::sort(zsrc_splits.begin(), zsrc_splits.end(), [](const auto &l, const auto &r){ return ClipperZUtils::zpoint_lower(l.first, r.first); });
        segments = ClipperZUtils::clip_zpaths(ctIntersection, zsrc, true, zboundary, visitor.clipper_callback());
        merge_splits(segments, zsrc_splits);
    }

    AABBTreeBBoxes aabb_tree;

    WaveSeeds out;
    out.reserve(segments.size());
    int iseed = 0;
    for (const ClipperZUtils::ZPath &path : segments) {
        assert(path.size() >= 2);
        ClipperZUtils::ZPoint front = path.front();
        ClipperZUtils::ZPoint back  = path.back();
        assert((front == back && front.z() >= idx_boundary_end && front.z() < idx_src_end) || 
            (front.z() < 0 || back.z() < 0));

        if (front != back && front.z() >= 0 && back.z() >= 0) {
            coord_t src_z = -1, boundary_z = -1;
            for (const ClipperZUtils::ZPoint &point : path) {
                if (point.z() >= idx_boundary_end && point.z() < idx_src_end && src_z < 0)
                    src_z = point.z();
                else if (point.z() >= idx_boundary_begin && point.z() < idx_boundary_end && boundary_z < 0)
                    boundary_z = point.z();
                if (src_z >= 0 && boundary_z >= 0)
                    break;
            }
            if (src_z >= 0) {
                uint32_t src_id = uint32_t(src_z - idx_boundary_end);
                if (boundary_z >= 0) {
                    out.push_back({ src_id, uint32_t(boundary_z - 1), ClipperZUtils::from_zpath(path) });
                } else {
                    if (aabb_tree.empty())
                        aabb_tree = build_aabb_tree_over_expolygons(boundary);
                    int boundary_id = sample_in_expolygons(aabb_tree, boundary, Point(front.x(), front.y()));
                    if (boundary_id >= 0)
                        out.push_back({ src_id, uint32_t(boundary_id), ClipperZUtils::from_zpath(path) });
                }
                ++ iseed;
                continue;
            }
            continue;
        } else
        if (front == back && (front.z() < idx_boundary_end)) {
            bool found = false;
            for (const ClipperZUtils::ZPoint &point : path) {
                if (point.z() >= idx_boundary_end) {
                    front = point;
                    back = point;
                    found = true;
                    break;
                }
            }
            if (! found)
                continue;
        }

        const Intersection *intersection = nullptr;
        auto intersection_point_valid = [idx_boundary_end, idx_src_end](const Intersection &is) {
            return is.first >= 1 && is.first < idx_boundary_end &&
                   is.second >= idx_boundary_end && is.second < idx_src_end;
        };
        if (front.z() < 0) {
            const Intersection &is = intersections[- front.z() - 1];
            assert(intersection_point_valid(is));
            if (intersection_point_valid(is))
                intersection = &is;
        }
        if (! intersection && back.z() < 0) {
            const Intersection &is = intersections[- back.z() - 1];
            assert(intersection_point_valid(is));
            if (intersection_point_valid(is))
                intersection = &is;
        }
        if (intersection) {
            out.push_back({ uint32_t(intersection->second - idx_boundary_end), uint32_t(intersection->first - 1), ClipperZUtils::from_zpath(path) });
        } else {
            assert(front == back && front.z() >= idx_boundary_end && front.z() < idx_src_end);
            if (front != back || front.z() < idx_boundary_end || front.z() >= idx_src_end)
                continue;
            if (aabb_tree.empty())
                aabb_tree = build_aabb_tree_over_expolygons(boundary);
            int boundary_id = sample_in_expolygons(aabb_tree, boundary, Point(front.x(), front.y()));
            assert(boundary_id >= 0);
            if (boundary_id >= 0)
                out.push_back({ uint32_t(front.z() - idx_boundary_end), uint32_t(boundary_id), ClipperZUtils::from_zpath(path) });
        }
    }

    if (sorted)
        std::sort(out.begin(), out.end(), lower_by_boundary_and_src);
    return out;
}

static Polygons wavefront_initial(const VecOfPoints &polylines, float offset, double arc_tolerance)
{
    Polygons out;
    out.reserve(polylines.size());
    for (const Points &path : polylines) {
        assert(path.size() >= 2);
        append(out, Slic3r::offset(Polyline(path), offset, jtRound, arc_tolerance, path.front() == path.back() ? etClosedLine : etOpenRound));
    }
    return out;
}

static Polygons wavefront_step(const Polygons &polygons, float offset, double arc_tolerance)
{
    Polygons out;
    out.reserve(polygons.size());
    for (const Polygon &polygon : polygons)
        append(out, Slic3r::offset(polygon, offset, jtRound, arc_tolerance));
    return out;
}

static Polygons wavefront_clip(const Polygons &wavefront, const Polygons &clipping)
{
    return intersection(wavefront, clipping, pftPositive);
}

static Polygons propagate_wave_from_boundary(
    const VecOfPoints           &seed,
    const ExPolygon             &boundary,
    const float                  initial_step,
    const float                  other_step,
    const size_t                 num_other_steps,
    const float                  max_inflation,
    const double                 arc_tolerance)
{
    assert(! seed.empty() && seed.front().size() >= 2);
    Polygons clipping = ClipperUtils::clip_clipper_polygons_with_subject_bbox(boundary, get_extents<true>(seed).inflated(max_inflation));
    Polygons polygons = wavefront_clip(wavefront_initial(seed, initial_step, arc_tolerance), clipping);
    for (size_t ioffset = 0; ioffset < num_other_steps; ++ ioffset)
        polygons = wavefront_clip(wavefront_step(polygons, other_step, arc_tolerance), clipping);
    return polygons;
}

std::vector<RegionExpansion> propagate_waves(const WaveSeeds &seeds, const ExPolygons &boundary, const RegionExpansionParameters &params)
{
    std::vector<RegionExpansion> out;
    VecOfPoints                  paths;
    for (auto it_seed = seeds.begin(); it_seed != seeds.end();) {
        auto it = it_seed;
        paths.clear();
        for (; it != seeds.end() && it->boundary == it_seed->boundary && it->src == it_seed->src; ++ it)
            paths.emplace_back(it->path);
        RegionExpansion re;
        for (Polygon &polygon : propagate_wave_from_boundary(paths, boundary[it_seed->boundary], params.initial_step, params.other_step, params.num_other_steps, params.max_inflation, params.arc_tolerance))
            out.push_back({ std::move(polygon), it_seed->src, it_seed->boundary });
        it_seed = it;
    }

    return out;
}

std::vector<RegionExpansion> propagate_waves(const ExPolygons &src, const ExPolygons &boundary, const RegionExpansionParameters &params)
{
    return propagate_waves(wave_seeds(src, boundary, params.tiny_expansion, true), boundary, params);
}

std::vector<RegionExpansion> propagate_waves(const ExPolygons &src, const ExPolygons &boundary,
    float expansion, 
    float expansion_step,
    size_t max_nr_steps)
{
    return propagate_waves(src, boundary, RegionExpansionParameters::build(expansion, expansion_step, max_nr_steps));
}

std::vector<RegionExpansionEx> propagate_waves_ex(const WaveSeeds &seeds, const ExPolygons &boundary, const RegionExpansionParameters &params)
{
    std::vector<RegionExpansion> expanded = propagate_waves(seeds, boundary, params);
    assert(std::is_sorted(seeds.begin(), seeds.end(), [](const auto &l, const auto &r){ return l.boundary < r.boundary || (l.boundary == r.boundary && l.src < r.src); }));
    Polygons acc;
    std::vector<RegionExpansionEx> out;
    for (auto it = expanded.begin(); it != expanded.end(); ) {
        auto it2 = it;
        acc.clear();
        for (; it2 != expanded.end() && it2->boundary_id == it->boundary_id && it2->src_id == it->src_id; ++ it2)
            acc.emplace_back(std::move(it2->polygon));
        size_t size = it2 - it;
        if (size == 1)
            out.push_back({ ExPolygon{std::move(acc.front())}, it->src_id, it->boundary_id });
        else {
            ExPolygons expolys = union_ex(acc);
            reserve_more_power_of_2(out, expolys.size());
            for (ExPolygon &ex : expolys)
                out.push_back({ std::move(ex), it->src_id, it->boundary_id });
        }
        it = it2;
    }
    return out;
}

std::vector<RegionExpansionEx> propagate_waves_ex(
    const ExPolygons    &src,
    const ExPolygons    &boundary,
    float                full_expansion,
    float                expansion_step,
    size_t               max_nr_expansion_steps)
{
    auto params = RegionExpansionParameters::build(full_expansion, expansion_step, max_nr_expansion_steps);
    return propagate_waves_ex(wave_seeds(src, boundary, params.tiny_expansion, true), boundary, params);
}

std::vector<Polygons> expand_expolygons(const ExPolygons &src, const ExPolygons &boundary,
    float expansion, 
    float expansion_step,
    size_t max_nr_steps)
{
    std::vector<Polygons> out(src.size(), Polygons{});
    for (RegionExpansion &r : propagate_waves(src, boundary, expansion, expansion_step, max_nr_steps))
        out[r.src_id].emplace_back(std::move(r.polygon));
    return out;
}

std::vector<ExPolygon> merge_expansions_into_expolygons(ExPolygons &&src, std::vector<RegionExpansion> &&expanded)
{
    std::sort(expanded.begin(), expanded.end(), [](const auto &l, const auto &r) { return l.src_id < r.src_id; });
    uint32_t   last = 0;
    Polygons   acc;
    ExPolygons out;
    out.reserve(src.size());
    for (auto it = expanded.begin(); it != expanded.end();) {
        for (; last < it->src_id; ++ last)
            out.emplace_back(std::move(src[last]));
        acc.clear();
        assert(it->src_id == last);
        for (; it != expanded.end() && it->src_id == last; ++ it)
            acc.emplace_back(std::move(it->polygon));
        ExPolygon &src_ex = src[last ++];
        assert(! src_ex.contour.empty());
#if 0
        {
            static int iRun = 0;
            BoundingBox bbox = get_extents(acc);
            bbox.merge(get_extents(src_ex));
            SVG svg(debug_out_path("expand_merge_expolygons-failed-union=%d.svg", iRun ++).c_str(), bbox);
            svg.draw(acc);
            svg.draw_outline(acc, "black", scale_(0.05));
            svg.draw(src_ex, "red");
            svg.Close();
        }
#endif
        Point sample = src_ex.contour.front();
        append(acc, to_polygons(std::move(src_ex)));
        ExPolygons merged = union_safety_offset_ex(acc);
        if (merged.size() > 1) {
            auto aabb_tree = build_aabb_tree_over_expolygons(merged);
            int id = sample_in_expolygons(aabb_tree, merged, sample);
            assert(id != -1);
            if (id != -1)
                out.emplace_back(std::move(merged[id]));
        } else if (merged.size() == 1)
            out.emplace_back(std::move(merged.front()));
    }
    for (; last < uint32_t(src.size()); ++ last)
        out.emplace_back(std::move(src[last]));
    return out;
}

std::vector<ExPolygon> expand_merge_expolygons(ExPolygons &&src, const ExPolygons &boundary, const RegionExpansionParameters &params)
{
    std::vector<RegionExpansion> expanded = propagate_waves(src, boundary, params);
    return merge_expansions_into_expolygons(std::move(src), std::move(expanded));
}

}
}
