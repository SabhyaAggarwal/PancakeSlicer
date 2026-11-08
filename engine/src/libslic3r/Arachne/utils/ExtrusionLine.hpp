//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.


#ifndef UTILS_EXTRUSION_LINE_H
#define UTILS_EXTRUSION_LINE_H

#include "../../ClipperZUtils.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <cstddef>

#include "ExtrusionJunction.hpp"
#include "../../Polyline.hpp"
#include "../../Polygon.hpp"
#include "../../ExtrusionEntity.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"

namespace Slic3r {
class ThickPolyline;
class Flow;
}

namespace Slic3r::Arachne
{

inline coord_t colinear_vertex_tolerance() { return coord_t(SCALED_EPSILON); }

struct ExtrusionLine
{
    size_t inset_idx;

    bool is_odd;

    bool is_closed;

    size_t size() const { return junctions.size(); }

    bool empty() const { return junctions.empty(); }

    std::vector<ExtrusionJunction> junctions;

    ExtrusionLine(const size_t inset_idx, const bool is_odd);
    ExtrusionLine() : inset_idx(-1), is_odd(true), is_closed(false) {}
    ExtrusionLine(const ExtrusionLine &other) : inset_idx(other.inset_idx), is_odd(other.is_odd), is_closed(other.is_closed), junctions(other.junctions) {}

    ExtrusionLine &operator=(ExtrusionLine &&other)
    {
        junctions = std::move(other.junctions);
        inset_idx = other.inset_idx;
        is_odd    = other.is_odd;
        is_closed = other.is_closed;
        return *this;
    }

    ExtrusionLine &operator=(const ExtrusionLine &other)
    {
        junctions = other.junctions;
        inset_idx = other.inset_idx;
        is_odd    = other.is_odd;
        is_closed = other.is_closed;
        return *this;
    }

    std::vector<ExtrusionJunction>::const_iterator begin() const { return junctions.begin(); }
    std::vector<ExtrusionJunction>::const_iterator end() const { return junctions.end(); }
    std::vector<ExtrusionJunction>::const_reverse_iterator rbegin() const { return junctions.rbegin(); }
    std::vector<ExtrusionJunction>::const_reverse_iterator rend() const { return junctions.rend(); }
    std::vector<ExtrusionJunction>::const_reference front() const { return junctions.front(); }
    std::vector<ExtrusionJunction>::const_reference back() const { return junctions.back(); }
    const ExtrusionJunction &operator[](unsigned int index) const { return junctions[index]; }
    ExtrusionJunction &operator[](unsigned int index) { return junctions[index]; }
    std::vector<ExtrusionJunction>::iterator begin() { return junctions.begin(); }
    std::vector<ExtrusionJunction>::iterator end() { return junctions.end(); }
    std::vector<ExtrusionJunction>::reference front() { return junctions.front(); }
    std::vector<ExtrusionJunction>::reference back() { return junctions.back(); }

    template<typename... Args> void emplace_back(Args &&...args) { junctions.emplace_back(args...); }
    void remove(unsigned int index) { junctions.erase(junctions.begin() + index); }
    void insert(size_t index, const ExtrusionJunction &p) { junctions.insert(junctions.begin() + index, p); }

    template<class iterator>
    std::vector<ExtrusionJunction>::iterator insert(std::vector<ExtrusionJunction>::const_iterator pos, iterator first, iterator last)
    {
        return junctions.insert(pos, first, last);
    }

    void clear() { junctions.clear(); }
    void reverse() { std::reverse(junctions.begin(), junctions.end()); }

    int64_t getLength() const;
    int64_t polylineLength() const { return getLength(); }

    Polygon toPolygon() const
    {
        Polygon ret;
        for (const ExtrusionJunction &j : junctions)
            ret.points.emplace_back(j.p);

        return ret;
    }

    void simplify(int64_t smallest_line_segment_squared, int64_t allowed_error_distance_squared, int64_t maximum_extrusion_area_deviation);

    static int64_t calculateExtrusionAreaDeviationError(ExtrusionJunction A, ExtrusionJunction B, ExtrusionJunction C);

    bool is_contour() const;

    double area() const;
};

template<class PathType>
static inline Slic3r::ThickPolyline to_thick_polyline(const PathType &path)
{
    assert(path.size() >= 2);
    Slic3r::ThickPolyline out;
    out.points.emplace_back(path.front().x(), path.front().y());
    out.width.emplace_back(path.front().z());
    out.points.emplace_back(path[1].x(), path[1].y());
    out.width.emplace_back(path[1].z());

    auto it_prev = path.begin() + 1;
    for (auto it = path.begin() + 2; it != path.end(); ++it) {
        out.points.emplace_back(it->x(), it->y());
        out.width.emplace_back(it_prev->z());
        out.width.emplace_back(it->z());
        it_prev = it;
    }

    return out;
}

static inline Polygon to_polygon(const ExtrusionLine &line)
{
    Polygon out;
    assert(line.junctions.size() >= 3);
    assert(line.junctions.front().p == line.junctions.back().p);
    out.points.reserve(line.junctions.size() - 1);
    for (auto it = line.junctions.begin(); it != line.junctions.end() - 1; ++it)
        out.points.emplace_back(it->p);
    return out;
}

static Points to_points(const ExtrusionLine &extrusion_line)
{
    Points points;
    points.reserve(extrusion_line.junctions.size());
    for (const ExtrusionJunction &junction : extrusion_line.junctions)
        points.emplace_back(junction.p);
    return points;
}

#if 0
static BoundingBox get_extents(const ExtrusionLine &extrusion_line)
{
    BoundingBox bbox;
    for (const ExtrusionJunction &junction : extrusion_line.junctions)
        bbox.merge(junction.p);
    return bbox;
}

static BoundingBox get_extents(const std::vector<ExtrusionLine> &extrusion_lines)
{
    BoundingBox bbox;
    for (const ExtrusionLine &extrusion_line : extrusion_lines)
        bbox.merge(get_extents(extrusion_line));
    return bbox;
}

static BoundingBox get_extents(const std::vector<const ExtrusionLine *> &extrusion_lines)
{
    BoundingBox bbox;
    for (const ExtrusionLine *extrusion_line : extrusion_lines) {
        assert(extrusion_line != nullptr);
        bbox.merge(get_extents(*extrusion_line));
    }
    return bbox;
}

static std::vector<Points> to_points(const std::vector<const ExtrusionLine *> &extrusion_lines)
{
    std::vector<Points> points;
    for (const ExtrusionLine *extrusion_line : extrusion_lines) {
        assert(extrusion_line != nullptr);
        points.emplace_back(to_points(*extrusion_line));
    }
    return points;
}
#endif

using VariableWidthLines = std::vector<ExtrusionLine>;

}

namespace Slic3r {

void extrusion_paths_append(ExtrusionPaths &dst, const ClipperZUtils::ZPaths &extrusion_paths, const ExtrusionRole role, const Flow &flow);
void extrusion_paths_append(ExtrusionPaths &dst, const Arachne::ExtrusionLine &extrusion, const ExtrusionRole role, const Flow &flow);

}

#endif
