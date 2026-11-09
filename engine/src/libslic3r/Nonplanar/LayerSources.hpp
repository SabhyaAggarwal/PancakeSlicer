#pragma once
#include "HeightField.hpp"
#include "LayerField.hpp"
#include "libslic3r/Point.hpp"

#include <memory>
#include <string>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

class OffsetLayerField : public LayerField
{
public:
    OffsetLayerField(HeightField S, double z0, double ramp);

    double phi(const Vec3d &p) const override;
    double z_at(const Vec2d &xy, double level) const override;
    Vec3d  gradient(const Vec3d &p) const override;
    double identity_below() const override { return m_z0; }
    double max_slope_near(const Vec2d &xy, double level) const override;

    double s(const Vec2d &xy) const;
    double ramp() const { return m_ramp; }

private:
    HeightField m_S;
    double      m_z0, m_ramp;
};

std::unique_ptr<LayerField> build_layer_source(const PrintObject &object, std::string *why = nullptr);

std::unique_ptr<LayerField> build_smooth_morph(const PrintObject &object, std::string *limit = nullptr);

}
