#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "TextureBakeIndex.hpp"

namespace Slic3r {
namespace TextureBake {

struct RegularizeOptions
{
    double aspect_threshold = 5.0;

    double slack            = 3.0;
    double aggressive_slack = 8.0;

    double extreme_sliver_aspect = 8.0;

    double max_normal_delta_cos        = 0.965925826289;
    double aggressive_normal_delta_cos = 0.906307787037;
    double sharp_edge_cos = 0.866025403784;

    int maxrounds = 8;

    bool preserve_excluded = false;
};

struct RegularizeRejectStats
{
    size_t frozen = 0, wing_count = 0, link_condition = 0, edge_cap = 0, normal_change = 0,
           degenerate = 0, folded_apex = 0;
};

struct RegularizeResult
{
    TriSoup               geometry;
    std::vector<int>      face_parent_id;
    size_t                collapse_count = 0;
    RegularizeRejectStats reject_stats;
};

RegularizeResult regularize_mesh(const TriSoup &geometry, const std::vector<int> &face_parent_id,
                                 double max_edge_length, const RegularizeOptions &opts = {});

}
}
