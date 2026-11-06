#pragma once







#include "LayerField.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct NpField
{
    Vec3d                origin { Vec3d::Zero() };
    Vec3d                spacing { Vec3d::Ones() };
    std::array<size_t, 3> dims { 0, 0, 0 };
    std::vector<float>   phi;
    double               h           = 0.2;
    double               first_layer = 0.2;
    std::string          machine, mesh_sha256, solver;
    std::string          proxy_version;
    std::optional<Vec3d> mesh_min;
    std::vector<std::pair<std::string, double>> terms;



    std::vector<double> layer_tops;


    std::vector<float> axis;

    size_t index(size_t i, size_t j, size_t k) const { return (i * dims[1] + j) * dims[2] + k; }
    Vec3d  node(size_t i, size_t j, size_t k) const { return origin + spacing.cwiseProduct(Vec3d(double(i), double(j), double(k))); }
};


NpField load_npfield(const std::string &path);
void    save_npfield(const std::string &path, const NpField &field);

NpField planar_npfield(const BoundingBoxf3 &box, double pitch, double h, double first_layer);

void    check_npfield_frame(const NpField &field, const BoundingBoxf3 &mesh_box, const std::string &path);



NpField load_npfield_for(const std::string &path, const BoundingBoxf3 &slicing_box, const Vec2d &plate_offset);
void    to_plate_frame(NpField &field, const Vec2d &plate_offset);




size_t  make_z_monotone(NpField &field);

Vec2d   plate_offset(const PrintObject &object);

class NpLayerField : public LayerField
{
public:
    explicit NpLayerField(NpField field);

    double phi(const Vec3d &p) const override;

    Vec3d  gradient(const Vec3d &p) const override;

    double identity_below() const override { return m_identity_below; }

    std::optional<Vec3d> given_axis(const Vec3d &p) const override;
    const NpField &file() const { return m_field; }

private:
    NpField m_field;
    double  m_identity_below = 0.;
    void    locate(const Vec3d &p, std::array<size_t, 3> &c, Vec3d &t) const;
};

}
