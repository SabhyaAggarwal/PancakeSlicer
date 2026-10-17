#pragma once
#include "HeadEnvelope.hpp"
#include "HeightField.hpp"
#include "LayerField.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

#include <admesh/stl.h>

#include <functional>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

class StackLayerField final : public LayerField
{
public:
    StackLayerField() = default;
    static StackLayerField planar(const BoundingBoxf3 &bbox, double pitch = 2., double ds = 1.);

    double phi(const Vec3d &p) const override;
    double z_at(const Vec2d &xy, double level) const override;
    Vec3d  gradient(const Vec3d &p) const override;
    double identity_below() const override { return m_identity_below; }

    bool        empty() const { return m_z.empty(); }
    double       pitch() const { return m_pitch; }
    double       level_spacing() const { return m_ds; }
    int          levels() const { return m_nk; }
    BoundingBoxf xy_bbox() const;
    double       top_level() const { return (m_nk - 1) * m_ds; }

    int                       nx() const { return m_nx; }
    int                       ny() const { return m_ny; }
    const Vec2d              &origin() const { return m_origin; }
    std::vector<float>       &data() { return m_z; }
    const std::vector<float> &data() const { return m_z; }
    void reset(const Vec2d &origin, double pitch, int nx, int ny, double ds, int nk, double identity_below = 0.);

private:
    float  node(int ix, int iy, int k) const { return m_z[(size_t(k) * m_ny + iy) * m_nx + ix]; }
    double level_z(int k, double u, double v) const;

    Vec2d              m_origin { Vec2d::Zero() };
    double             m_pitch { 2. };
    double             m_ds { 1. };
    double             m_identity_below { 0. };
    int                m_nx { 0 }, m_ny { 0 }, m_nk { 0 };
    std::vector<float> m_z;
};

using ToolAxisFn = std::function<Vec3d(const Vec3d &p)>;

struct LayerFieldParams
{
    double layer_height       = 0.2;
    double min_layer_height   = 0.08;
    double max_layer_height   = 0.28;
    double flat_base_height   = 0.6;
    double max_slope          = 0.;
    double overhang_angle     = 55.;
    double support_angle      = 60.;
    double w_conform          = 50.;
    double w_overhang         = 1000.;
    double w_smooth_xy        = 1.;
    double w_smooth_s         = 1.;
    double hidden_top_weight  = 0.3;
    double conform_clear_radius = 0.;
    double pitch              = 0.;
    double level_spacing      = 1.;
    int    outer_iterations   = 6;
    const HeadEnvelope *head  = nullptr;
    ToolAxisFn          tool_axis;
    std::function<void()> throw_if_canceled;
};

struct SupportFeatures
{
    double area_height      = 0.;
    double perimeter_height = 0.;
    double region_height    = 0.;
    int    regions          = 0;
    static constexpr double k_area = 0., k_perimeter = 0.0844, k_region = 0.;
};

struct LayerFieldMetrics
{
    double curved_top_area      = 0.;
    double curved_top_in_cap    = 0.;
    double conformal_top        = 0.;
    double top_rms_angle        = 0.;
    double overhang_area        = 0.;
    double max_overhang         = 0.;
    double max_field_slope      = 0.;
    double field_slope_p99      = 0.;
    double slope_cap            = 0.;
    double min_thickness        = 0.;
    double max_thickness        = 0.;
    double head_violation_area  = 0.;
    double support_estimate     = 0.;
    SupportFeatures support;
    double axis_dev_mean = 0., axis_dev_p95 = 0., axis_within_3 = 0.;
    int    head_layers_checked  = 0;
};

struct LayerFieldReport
{
    LayerFieldMetrics planar;
    LayerFieldMetrics field;
    double            solve_seconds = 0.;
    int               columns = 0, levels = 0;
    std::string       log;
};

StackLayerField solve_layer_field(const indexed_triangle_set &mesh, const LayerFieldParams &params,
                                  LayerFieldReport *report = nullptr);

LayerFieldMetrics evaluate_layer_field(const indexed_triangle_set &mesh, const LayerField &field,
                                       const LayerFieldParams &params, double pitch = 0.5,
                                       int head_check_every = 0);

double layer_field_slope_cap(const LayerFieldParams &params);

}
