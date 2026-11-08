#pragma once

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/TriangleMesh.hpp>

namespace Slic3r {

inline BoundingBoxf3 bbox_from_stats(const TriangleMeshStats& stats)
{
    if (stats.number_of_facets == 0)
        return BoundingBoxf3();
    return BoundingBoxf3(stats.min.cast<double>(), stats.max.cast<double>());
}

}
