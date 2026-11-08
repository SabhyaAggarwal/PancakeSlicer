#pragma once

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "FieldOrder.hpp"
#include "HeadEnvelope.hpp"
#include "LayerField.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct ConformalLayersParams
{
    double max_angle    = 30.;
    int    shells       = 5;
    double layer_height = 0.2;
    double first_layer  = 0.2;
    int    flat_layers  = 3;
    double pitch        = 0.25;
    double min_area     = 20.;
};

struct ConformalTop
{
    double      area_mm2 = 0.;
    double      z_min = 0., z_max = 0.;
    double      level = 0.;
    double      ramp  = 0.;
    std::string fallback;
};

class ConformalLayerField final : public LayerField
{
public:
    double phi(const Vec3d &p) const override;
    double z_at(const Vec2d &xy, double level) const override;
    double identity_below() const override { return m_identity_below; }

    std::vector<ConformalTop> tops;

private:
    friend std::shared_ptr<const ConformalLayerField> build_conformal_layer_field(const indexed_triangle_set &, const ConformalLayersParams &);
    struct Column { double T = 0., c = 1., L = 0., zb = 0., d = 0., omega = 0.; };
    double column_phi(const Column &col, double z) const;
    double column_z(const Column &col, double level) const;
    Column        column(const Vec2d &xy) const;

    Vec2d               m_origin { Vec2d::Zero() };
    double              m_pitch = 0.25;
    size_t              m_nx = 0, m_ny = 0;
    double              m_shell_depth = 0.;
    double              m_identity_below = 0.;
    std::vector<Column> m_cols;
};

std::shared_ptr<const ConformalLayerField> build_conformal_layer_field(const indexed_triangle_set &mesh, const ConformalLayersParams &params);

std::shared_ptr<const ConformalLayerField> conformal_layers_field(const PrintObject &object, std::string *summary = nullptr);

double conformal_band_height(const HeadEnvelope &head, double layer_height);

std::string contour_solid_angles(const LayerField &field, const std::vector<ExPolygons> &domains, const std::vector<double> &levels,
                                 double flat_angle);

size_t layer_order_head_conflicts(const std::vector<FieldRoad> &roads, size_t layers, const HeadEnvelope &head);

struct BandOrderStats
{
    double band = 0., offset = 0.;
    size_t bands = 0, groups = 0, cut_roads = 0;
};

FieldOrder band_order_roads(std::vector<FieldRoad> &roads, size_t layers, double band, BandOrderStats *stats = nullptr);

}
