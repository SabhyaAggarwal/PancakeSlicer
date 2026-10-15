#pragma once
#include "FlowMod.hpp"
#include "HeightField.hpp"
#include "ShellBase.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Point.hpp"
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Slic3r { class PrintObject; }
namespace Slic3r::sla { class IndexedMesh; }

namespace Slic3r::Nonplanar {

struct AutoPatchSettings;
class OrientationField;

enum class PatchKind : unsigned char { Top, Bottom, Volume, Overhang, Chunk, SupportInterface, Bridge, Field };

class PatchField
{
public:
    PatchField() = default;
    PatchField(HeightField field) : m_field(std::make_shared<HeightField>(std::move(field))) {}
    PatchField &operator=(HeightField field)
    {
        m_field = std::make_shared<HeightField>(std::move(field));
        return *this;
    }

    const HeightField &get() const { return m_field ? *m_field : empty_field(); }
    operator const HeightField &() const { return this->get(); }
    HeightField &edit()
    {
        if (!m_field)
            m_field = std::make_shared<HeightField>();
        else if (m_field.use_count() > 1)
            m_field = std::make_shared<HeightField>(*m_field);
        return *m_field;
    }

    int                       nx() const { return this->get().nx(); }
    int                       ny() const { return this->get().ny(); }
    double                    pitch() const { return this->get().pitch(); }
    const Vec2d              &origin() const { return this->get().origin(); }
    BoundingBoxf              bbox() const { return this->get().bbox(); }
    bool                      empty() const { return this->get().empty(); }
    float                     at(int ix, int iy) const { return this->get().at(ix, iy); }
    Vec2d                     node(int ix, int iy) const { return this->get().node(ix, iy); }
    float                     sample(const Vec2d &p) const { return this->get().sample(p); }
    Vec2d                     gradient(const Vec2d &p) const { return this->get().gradient(p); }
    bool                      has_data(const Vec2d &p) const { return this->get().has_data(p); }
    const std::vector<float> &data() const { return this->get().data(); }

private:
    static const HeightField &empty_field()
    {
        static const HeightField empty;
        return empty;
    }
    std::shared_ptr<HeightField> m_field;
};

struct ConformalPatch
{
    int        id = -1;
    PatchKind  kind = PatchKind::Top;
    ExPolygons footprint;
    ExPolygons wall_band;
    PatchField surface;
    PatchField base;
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
static_assert(std::is_nothrow_move_constructible_v<ConformalPatch>);

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
