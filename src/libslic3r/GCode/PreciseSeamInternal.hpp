#pragma once

#include "PreciseSeam.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include <cstddef>
#include <vector>

namespace Slic3r::PreciseSeam::detail {

struct ClippedEdgeInterval {
    size_t edge;
    double begin;
    double end;
    Point first;
    Point last;
};

struct FragmentBindingFailure {
    size_t pair_index = 0;
    const char *reason = "empty fragment";
};

bool append_projected_fragment(const Polyline &fragment, const Polygon &perimeter,
                               std::vector<ClippedEdgeInterval> &intervals,
                               FragmentBindingFailure &failure);

bool bind_fragment(const Polyline &fragment, const Polygon &perimeter,
                   std::vector<ClippedEdgeInterval> &intervals, FragmentBindingFailure &failure);

bool append_fragment(const Polyline &fragment, const Polygon &perimeter,
                     std::vector<ClippedEdgeInterval> &intervals,
                     const ExtractionContext &context, size_t fragment_index);

}
