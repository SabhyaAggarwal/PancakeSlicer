#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

struct EdgeDefects
{
    size_t open = 0, non_manifold = 0, triangles = 0;
};

EdgeDefects count_edge_defects(const TriSoup &geometry, double quant = WELD_GRID_EXPORT);

size_t count_area_slivers(const TriSoup &geometry);

struct RepairOptions
{
    double weld_quant = WELD_GRID_EXPORT;

    double on_seg_tol = 0.02;

    int max_iters = 16;
};

TriSoup resolve_t_junctions(const TriSoup &geometry, const RepairOptions &opts = {},
                            std::vector<int> *face_color = nullptr);

}
}
