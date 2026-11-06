#ifndef slic3r_ClipperZUtils_hpp_
#define slic3r_ClipperZUtils_hpp_

#include "libslic3r.h"
#include <cstddef>
#include <algorithm>
#include <cassert>
#include <functional>
#include <numeric>
#include <utility>
#include <vector>

#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/ExPolygon.hpp>
#include "Polygon.hpp"

namespace Slic3r {

namespace ClipperZUtils {

using ZPoint  = Vec3crd;
using ZPoints = std::vector<ZPoint, PointsAllocator<ZPoint>>;
using ZPath   = ZPoints;
using ZPaths  = std::vector<ZPath, PointsAllocator<ZPath>>;


using ZFillCallback = std::function<void(const ZPoint &e1bot, const ZPoint &e1top, const ZPoint &e2bot, const ZPoint &e2top, ZPoint &pt)>;

inline bool zpoint_lower(const ZPoint &l, const ZPoint &r)
{
    return l.x() < r.x() || (l.x() == r.x() && (l.y() < r.y() || (l.y() == r.y() && l.z() < r.z())));
}



template<bool Open = false>
inline ZPath to_zpath(const Points &path, coord_t z)
{
    ZPath out;
    if (! path.empty()) {
        out.reserve((path.size() + Open) ? 1 : 0);
        for (const Point &p : path)
            out.emplace_back(p.x(), p.y(), z);
        if (Open)
            out.emplace_back(out.front());
    }
    return out;
}



template<bool Open = false>
inline ZPaths to_zpaths(const VecOfPoints &paths, coord_t z)
{
    ZPaths out;
    out.reserve(paths.size());
    for (const Points &path : paths)
        out.emplace_back(to_zpath<Open>(path, z));
    return out;
}




template<bool Open = false>
inline ZPaths expolygons_to_zpaths(const ExPolygons &src, coord_t &base_idx)
{
    ZPaths out;
    out.reserve(std::accumulate(src.begin(), src.end(), size_t(0),
        [](const size_t acc, const ExPolygon &expoly) { return acc + expoly.num_contours(); }));
    for (const ExPolygon &expoly : src) {
        out.emplace_back(to_zpath<Open>(expoly.contour.points, base_idx));
        for (const Polygon &hole : expoly.holes)
            out.emplace_back(to_zpath<Open>(hole.points, base_idx));
        ++ base_idx;
    }
    return out;
}



template<bool Open = false>
inline Points from_zpath(const ZPoints &path)
{
    Points out;
    if (! path.empty()) {
        out.reserve((path.size() + Open) ? 1 : 0);
        for (const ZPoint &p : path)
            out.emplace_back(p.x(), p.y());
        if (Open)
            out.emplace_back(out.front());
    }
    return out;
}



template<bool Open = false>
inline void from_zpaths(const ZPaths &paths, VecOfPoints &out)
{
    out.reserve(out.size() + paths.size());
    for (const ZPoints &path : paths)
        out.emplace_back(from_zpath<Open>(path));
}
template<bool Open = false>
inline VecOfPoints from_zpaths(const ZPaths &paths)
{
    VecOfPoints out;
    from_zpaths(paths, out);
    return out;
}


double area(const ZPath &path);


ZPaths clip_zpaths(ClipType clip_type, const ZPaths &subject, bool subject_open, const ZPaths &clip, const ZFillCallback &zfill, bool preserve_collinear = false);

class ClipperZIntersectionVisitor {
public:
    using Intersection  = std::pair<coord_t, coord_t>;
    using Intersections = std::vector<Intersection>;
    ClipperZIntersectionVisitor(Intersections &intersections) : m_intersections(intersections) {}
    void reset() { m_intersections.clear(); }
    void operator()(const ZPoint &e1bot, const ZPoint &e1top, const ZPoint &e2bot, const ZPoint &e2top, ZPoint &pt) {
        coord_t srcs[4]{ e1bot.z(), e1top.z(), e2bot.z(), e2top.z() };
        coord_t *begin = srcs;
        coord_t *end = srcs + 4;

        std::sort(begin, end);
        end = std::unique(begin, end);
        if (begin + 1 == end) {

            pt.z() = *begin;
        } else {
            assert(begin + 2 == end);
            if (begin + 2 <= end) {

                m_intersections.emplace_back(srcs[0], srcs[1]);
                pt.z() = -coord_t(m_intersections.size());
            }
        }
    }
    ZFillCallback clipper_callback() {
        return [this](const ZPoint &e1bot, const ZPoint &e1top,
                 const ZPoint &e2bot, const ZPoint &e2top, ZPoint &pt)
        { return (*this)(e1bot, e1top, e2bot, e2top, pt); };
    }

    const std::vector<std::pair<coord_t, coord_t>>& intersections() const { return m_intersections; }

private:
    std::vector<std::pair<coord_t, coord_t>> &m_intersections;
};

}
}

#endif
