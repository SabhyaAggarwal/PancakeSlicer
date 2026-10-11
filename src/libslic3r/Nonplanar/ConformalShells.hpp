#pragma once
#include "HeightField.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include <functional>
#include <string>
#include <vector>

namespace Slic3r { class PrintObject; }
namespace Slic3r::sla { class IndexedMesh; }

namespace Slic3r::Nonplanar {

enum class PatchKind : unsigned char { Top, Bottom, Volume, Overhang, Chunk };

struct ConformalPatch
{
    int        id = -1;
    PatchKind  kind = PatchKind::Top;
    ExPolygons footprint;
    ExPolygons wall_band;
    HeightField surface;
    HeightField base;
    int        shells = 0;
    double     shell_height = 0.;
    double     z_min = 0., z_max = 0.;
    size_t     host_layer = 0;
    int        region_id = -1;
    int        band = -1;
    std::vector<ExtrusionEntityCollection> shell_paths;
    ExtrusionEntityCollection              ironing;
};

struct PatchDiagnostics
{
    int    patch_id = -1;
    double max_surface_error = 0.;
    double rms_surface_error = 0.;
    double max_slope_deg = 0.;
    double fallback_area = 0.;
    std::string limiting_component;
};

struct ConformalResult
{
    std::vector<ConformalPatch>   patches;
    std::vector<PatchDiagnostics> diagnostics;
    std::vector<ExPolygons>       planar_exclusion;
};

HeightField sample_top_surface(const sla::IndexedMesh &mesh, double ray_z, double z_offset, const HeightField &like,
                               const std::function<void()> &throw_if_canceled = {});

ConformalResult build_conformal_shells(PrintObject &object);

}
