#pragma once
#include "HeadEnvelope.hpp"
#include "LayerField.hpp"
#include "LayerFieldOpt.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class PrintConfig;
class PrintObject;
class ToolKinematics;
}

namespace Slic3r::Nonplanar {

struct ConformalResult;

struct OrientationFieldParams
{
    double                layer_height       = 0.2;
    double                first_layer_height = 0.2;
    double                overhang_threshold = 0.25 * M_PI;
    double                max_tilt           = 55. * M_PI / 180.;
    HeadEnvelope          head;
    const ToolKinematics *kinematics         = nullptr;
    double                pitch              = 0.;
    bool                  conform_tops       = true;
};

struct OrientationFieldStats
{
    double planar_overhang_area = 0.;
    double overhang_area        = 0.;
    double planar_support_volume = 0.;
    double support_volume        = 0.;
    double planar_step_area      = 0.;
    double step_area             = 0.;
    double shell_step_area       = 0.;
    double max_tilt              = 0.;
    double min_thickness         = 1.;
    double max_thickness         = 1.;
    size_t head_conflicts        = 0;
    size_t nodes                 = 0;
    int    relax_rounds          = 0;
    double tilt_scale            = 1.;
    bool   solver_field          = false;
};

class OrientationField : public LayerField
{
public:
    double phi(const Vec3d &p) const override;
    Vec3d  gradient(const Vec3d &p) const override;
    double identity_below() const override { return m_bed_band; }
    Vec3d  orientation(const Vec3d &p) const;

    Vec3d    forward(const Vec3d &p) const;
    Matrix3d jacobian(const Vec3d &p) const;
    bool     inverse(const Vec3d &q, Vec3d &p) const;
    bool     inside(const Vec3d &p) const;

    double                       pitch() const { return m_pitch; }
    const OrientationFieldStats &stats() const { return m_stats; }
    double                       max_tilt() const { return m_stats.max_tilt; }
    std::vector<std::string>     notes;

    static std::shared_ptr<OrientationField> build(const indexed_triangle_set &mesh, const OrientationFieldParams &params,
                                                   const std::function<void()> &throw_if_canceled = {});

    indexed_triangle_set deform(const indexed_triangle_set &mesh, double max_edge) const;

private:
    Vec3d  m_origin { Vec3d::Zero() };
    double m_pitch = 1.;
    int    m_nx = 0, m_ny = 0, m_nz = 0;
    double m_bed_band = 0.;
    double m_tilt_budget = 0.5 * M_PI;
    std::vector<double>  m_phi, m_u, m_v;
    std::vector<Vec3f>   m_grad_phi, m_grad_u, m_grad_v;
    std::vector<uint8_t> m_inside;
    Vec3d                              m_inv_origin { Vec3d::Zero() };
    double                             m_inv_pitch = 1.;
    int                                m_inv_nx = 0, m_inv_ny = 0, m_inv_nz = 0;
    std::vector<std::vector<uint32_t>> m_inv_bins;
    OrientationFieldStats              m_stats;

    size_t node(int i, int j, int k) const { return (size_t(k) * size_t(m_ny) + size_t(j)) * size_t(m_nx) + size_t(i); }
    Vec3d  node_pos(int i, int j, int k) const { return m_origin + m_pitch * Vec3d(i, j, k); }
    void   locate(const Vec3d &p, int &i, int &j, int &k, Vec3d &t) const;
    template<class T> T trilinear(const std::vector<T> &f, const Vec3d &p) const;
    void   compute_gradients();
    void   build_inverse();
    friend struct OrientationFieldBuilder;
};

Vec3d settle_axis(const Vec3d &axis, double &plane, double none = 5. * M_PI / 180., double full = 20. * M_PI / 180.);

bool field_motion_supported(const PrintConfig &config, std::string *why = nullptr);

std::shared_ptr<const OrientationField> clip_field_slices(PrintObject &object);

void build_field_patch(PrintObject &object, const std::shared_ptr<const OrientationField> &field, ConformalResult &result);

}
