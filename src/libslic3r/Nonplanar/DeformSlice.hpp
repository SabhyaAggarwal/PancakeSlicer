#pragma once
#include "libslic3r/Point.hpp"
#include <utility>
#include "ConformalShells.hpp"
#include "HeightField.hpp"
#include "LayerField.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r { class PrintObject; }
namespace Slic3r::sla { class IndexedMesh; }

namespace Slic3r::Nonplanar {

struct HeadEnvelope;

struct MeshRays
{
    const sla::IndexedMesh *imesh = nullptr;
    double                  shift = 0.;
    BoundingBoxf3           bbox;
};

class ColumnScaleField : public LayerField
{
public:
    ColumnScaleField(double z0, HeightField rho) : m_z0(z0), m_rho(std::move(rho)) {}

    double phi(const Vec3d &p) const override { return p.z() <= m_z0 ? p.z() : m_z0 + (p.z() - m_z0) / this->rho(p.head<2>()); }
    double z_at(const Vec2d &xy, double level) const override { return level <= m_z0 ? level : m_z0 + (level - m_z0) * this->rho(xy); }
    Vec3d  gradient(const Vec3d &p) const override;
    double identity_below() const override { return m_z0; }

    double             rho(const Vec2d &xy) const;
    double             max_slope_near(const Vec2d &xy, double level) const override;

private:
    double      m_z0;
    HeightField m_rho;
};

struct DeformFieldParams
{
    double planar_top         = 0.6;
    double layer_height       = 0.2;
    double min_ratio          = 0.5;
    double max_ratio          = 1.5;
    double max_slope          = 0.5;
    double pitch              = 0.5;
    double blur               = 2.0;
    double z0_keep            = 0.97;
    double flat_share_limit   = 1.;
    double saddle_share_limit = 1.;
};

struct DeformFieldReport
{
    double z0            = 0.;
    double top_level     = 0.;
    double curved_area   = 0.;
    double aligned_area  = 0.;
    double max_layer_slope = 0.;
    double rho_min = 1., rho_max = 1.;
    double predicted_rms = 0.;
    double layer_factor  = 1.;
    double flat_share    = 0.;
    double saddle_share  = 0.;
};

std::unique_ptr<ColumnScaleField> build_column_scale_field(const indexed_triangle_set &mesh, const DeformFieldParams &params,
                                                           DeformFieldReport *report = nullptr, HeightField *top = nullptr,
                                                           const MeshRays *rays = nullptr);

indexed_triangle_set deform_mesh(const indexed_triangle_set &mesh, const LayerField &field, double max_edge);

double deform_band_height(const HeadEnvelope &head, double slope, double inflation = 0.);

bool deform_slice_requested(const PrintObject &object);
bool deform_slice_considered(const PrintObject &object);
bool choose_deform_slice(PrintObject &object, ConformalResult &result);

struct DeformSlice
{
    struct Planar
    {
        ExtrusionEntityCollection perimeters, fills, thin_fills;
        bool                      curved = false;
    };
    DeformFieldReport                    report;
    std::vector<std::unique_ptr<Planar>> planar;
    std::vector<ConformalPatch>          patches;
    std::shared_ptr<const LayerField>    field;
};
bool compute_deform_slice(const PrintObject &object, DeformSlice &out, std::string *why = nullptr,
                          double give_up_above = std::numeric_limits<double>::infinity(), const LayerField *field = nullptr,
                          double planar_time = 0., double time_budget = std::numeric_limits<double>::infinity(),
                          const std::function<bool(const DeformFieldReport &)> &admit = {}, const MeshRays *rays = nullptr,
                          bool host_walls = false);

DeformFieldReport measure_layer_field(const indexed_triangle_set &mesh, const LayerField &field, double layer_height,
                                      const HeightField *top = nullptr);

double print_time_proxy(const PrintObject &object, const ConformalResult *result, const DeformSlice *slice);

void apply_deform_slice(PrintObject &object, DeformSlice &&slice, ConformalResult &result);

bool build_deform_slice(PrintObject &object, ConformalResult &result, std::string *why = nullptr);

}
