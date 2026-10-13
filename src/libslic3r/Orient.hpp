#ifndef ORIENT_HPP
#define ORIENT_HPP

#include "TriangleMesh.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "Nonplanar/AutoOrient.hpp"
#include <string>
#include <functional>
#include <Eigen/Core>
#include <vector>

namespace Slic3r { class ModelInstance; }
namespace Slic3r { class ModelObject; }

namespace Slic3r {

namespace orientation {


static const constexpr int UNORIENTD = -1;

struct OrientMesh {
    TriangleMesh mesh;
    double overhang_angle = 30;
    double angle{ 0 };
    double angle_vertical{ 0 };
    Vec3d axis{ 0,0,1 };
    Vec3d axis_vertical{ 0,0,1 };
    Vec3d orientation{ 0,0,1 };
    Vec3d orientation_vertical{ -1,0,0 };
    Matrix3d rotation_matrix = Matrix3d::Identity();
    Matrix3d rotation_matrix_vertical = Matrix3d::Identity();
    Vec3d euler_angles = {0, 0, 0};
    Vec3d euler_angles_vertical = {0, 0, 0};
    Vec3d cooling_direction = {0, 0, 0};
    bool has_cooling_fan{false};
    bool nonplanar{false};
    Nonplanar::OrientParams nonplanar_params;

    std::string name;

    std::function<void(const OrientMesh&)> setter = nullptr;

    void apply() const { if (setter) setter(*this); }

};

struct OrientParams {
    float TAR_A               { 0.01f };
    float TAR_B               { 0.177f };
    float RELATIVE_F          { 6.610621027964314f };
    float CONTOUR_F           { 0.23228623269775997f };
    float BOTTOM_F            { 1.167152017941474f };
    float BOTTOM_HULL_F       { 0.1f };
    float TAR_C               { 0.24308070476924726f };
    float TAR_D               { 0.6284515508160871f };
    float TAR_E               { 0};
    float FIRST_LAY_H         { 0.2f};
    float VECTOR_TOL          { -0.0011163303070972383f };
    float NEGL_FACE_SIZE      { 0.1f };
    float ASCENT              { -0.5f };
    float PLAFOND_ADV         { 0.04079208948120519f };
    float CONTOUR_AMOUNT      { 0.0101472219892684f };
    float OV_H                { 1.0370178217794535f };
    float height_offset       { 2.7417608343142073f };
    float height_log          { 0.06442030687034085f };
    float height_log_k        { 0.3933594673063997f };
    float LAF_MAX             { 0.999f };
    float LAF_MIN             { 0.9703f };
    float TAR_LAF             { 0.01f };
    float TAR_PROJ_AREA       { 0.1f };
    float BOTTOM_MIN          { 0.1f };
    float BOTTOM_MAX          { 2000 };
    float height_to_bottom_hull_ratio_MIN { 1 };
    float BOTTOM_HULL_MAX     { 2000 };
    float APPERANCE_FACE_SUPP { 3 };

    float overhang_angle     { 60.f };
    bool  use_low_angle_face { true };
    bool  min_volume         { false };
    Eigen::Vector3f fun_dir  {};

    bool parallel { false };

    std::function<void(unsigned, std::string)> progressind {};

    std::function<bool(void)> stopcondition {};

    OrientParams() = default;
};

using OrientMeshs = std::vector<OrientMesh>;

void orient(OrientMeshs &items, const OrientMeshs &excludes, const OrientParams &params = {});

void orient(ModelObject* obj);

void orient(ModelInstance* instance);

void orient_for_cooling(TriangleMesh& mesh, const FanDirection& fan_dir);

}}

#endif
