#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "libslic3r/Point.hpp"
#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

static constexpr int SUBDIVIDE_SAFETY_CAP = 16'000'000;

static constexpr double SUBDIVIDE_SHARP_ANGLE_DEG = 30.0;

static constexpr int SUBDIVIDE_MAX_ITERATIONS = 12;

static constexpr double SUBDIVIDE_GRADE = 1.0;

struct VertStore
{
    std::vector<Vec3d>  pos;
    std::vector<Vec3d>  nrm;
    std::vector<double> wgt;
    std::vector<int>    canon;

    size_t count() const { return pos.size(); }
    int    push(const Vec3d &p, const Vec3d &n)
    {
        const int idx = int(pos.size());
        pos.push_back(p);
        nrm.push_back(n);
        return idx;
    }
};

struct IndexedMesh
{
    VertStore         verts;
    std::vector<int>  indices;
    QuantizedPointMap pos_canon_map{ WELD_GRID_GEOMETRY, 256 };
    bool              has_canon = false;
};

using SubdivideProgressFn = std::function<bool(double fraction, size_t triangles, double longest_edge)>;
using SubdivideWithinFn = std::function<bool(const Vec3d &point, double radius)>;

struct SubdivideResult
{
    TriSoup geometry;
    std::vector<int> face_parent_id;
    bool             safety_cap_hit = false;
};

SubdivideResult subdivide(const TriSoup &geometry, double max_edge_length,
                          const std::vector<uint8_t> &face_excluded = {}, bool fast = false,
                          int safety_cap = SUBDIVIDE_SAFETY_CAP,
                          const SubdivideProgressFn &on_progress = {},
                          const SubdivideWithinFn &within = {});

IndexedMesh to_indexed(const TriSoup &geometry);
IndexedMesh to_indexed_fast(const TriSoup &geometry);
TriSoup     to_non_indexed(const VertStore &verts, const std::vector<int> &indices,
                           const std::vector<uint8_t> &face_excluded);

}
}
