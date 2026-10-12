#pragma once
#include "HeightField.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <functional>
#include <memory>
#include <vector>

struct indexed_triangle_set;

namespace Slic3r {
class DynamicPrintConfig;
}

namespace Slic3r::Nonplanar {

struct HeadEnvelope;

struct PatchPreviewParams
{
    std::shared_ptr<const DynamicPrintConfig> config;

    bool   banded             = false;
    int    shells             = 0;
    int    top_shells         = 0;
    double shell_height       = 0.2;
    double max_slope          = 0.;
    double max_error          = 0.05;
    double min_area           = 16.;
    double pitch              = 0.2;
    double first_layer_height = 0.2;
    double line_width         = 0.45;
    double wall_width         = 0.;
    double max_tilt           = 0.;

    bool operator==(const PatchPreviewParams &rhs) const;
    bool operator!=(const PatchPreviewParams &rhs) const { return !(*this == rhs); }
};

PatchPreviewParams patch_preview_params(const DynamicPrintConfig &config);

struct PatchPreview
{
    HeightField             surface;
    HeightField             conformal;
    std::vector<ExPolygons> patches;
};

PatchPreview preview_conformal_patches(const indexed_triangle_set &mesh, const HeadEnvelope &head,
                                       const PatchPreviewParams &params,
                                       const std::function<void()> &throw_if_canceled = {});

}
