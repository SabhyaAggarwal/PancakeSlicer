#pragma once
#include "FlowMod.hpp"
#include "HeightField.hpp"
#include "ShellBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r { class PrintObject; }
namespace Slic3r::sla { class IndexedMesh; }

namespace Slic3r::Nonplanar {

struct AutoPatchSettings;
class OrientationField;

enum class PatchKind : unsigned char { Top, Bottom, Volume, Overhang, Chunk, SupportInterface, Bridge, Field };

struct ConformalPatch
{
    int        id = -1;
    PatchKind  kind = PatchKind::Top;
    ExPolygons footprint;
    ExPolygons wall_band;
    HeightField surface;
    HeightField base;
    int        shells = 0;
    int        sparse = 0;
    bool       bridged_base = false;
    double     shell_height = 0.;
    double     z_min = 0., z_max = 0.;
    size_t     host_layer = 0;
    int        region_id = -1;
    int        band = -1;
    bool       from_support = false;
    std::shared_ptr<const AutoPatchSettings> automatic;
    std::shared_ptr<const OrientationField> field;

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
    std::string fallback_reason;
    std::string limiting_component;
    int    repaired_nodes = 0;
    double repair_lowering = 0.;
    ExPolygons                      fallback_shape;
    std::vector<std::vector<Vec3f>> fallback_hatch;
};

struct ObjectMesh;

struct ConformalResult
{
    std::vector<ConformalPatch>   patches;
    std::vector<PatchDiagnostics> diagnostics;
    std::vector<ExPolygons>       planar_exclusion;
    std::vector<ExPolygons>       bridged;
    std::vector<ExPolygons>       self_supported;
    FlowModStats                  flow_mod;
    std::vector<PatchBase>        base;
    bool                          layer_changes_filament = false;
    std::shared_ptr<ObjectMesh>   mesh;
};

HeightField sample_top_surface(const sla::IndexedMesh &mesh, double ray_z, double z_offset, const HeightField &like,
                               const std::function<void()> &throw_if_canceled = {},
                               const std::function<bool(const Vec2d &, double)> &solid_below = {});

constexpr double BandedTopWidth = 0.55;
constexpr double BandedCapWidth = 0.625;

double low_plane_top_radius(const PrintObject &object, double layer_height);

double low_plane_rim(const PrintObject &object);

ConformalResult build_conformal_shells(PrintObject &object);

}
