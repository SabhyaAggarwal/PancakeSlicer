#pragma once
#include "HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class PrintObject;
class ToolKinematics;
}

namespace Slic3r::Nonplanar {

struct ConformalResult;

struct HalfSpace
{
    Vec3d  normal { Vec3d::UnitZ() };
    double offset = 0.;
};

struct MultiplanarChunk
{
    int                    parent = -1;
    Vec3d                  build_dir { Vec3d::UnitZ() };
    Matrix3d               frame { Matrix3d::Identity() };
    double                 base = 0.;
    std::vector<HalfSpace> clips;
    int                    base_clip = -1;
    indexed_triangle_set   piece;
    double                 overhang_area = 0.;
};

struct MultiplanarParams
{
    double                layer_height       = 0.2;
    double                overhang_threshold = 0.25 * M_PI;
    int                   max_planes         = 3;
    HeadEnvelope          head;
    const ToolKinematics *kinematics         = nullptr;
    double                min_overhang_area  = 4.;
};

struct MultiplanarPlan
{
    std::vector<MultiplanarChunk> chunks;
    double                        residual_overhang_area = 0.;
    std::vector<std::string>      notes;
};

MultiplanarPlan plan_multiplanar(const indexed_triangle_set &mesh, const MultiplanarParams &params,
                                 const std::function<void()> &throw_if_canceled = {});

indexed_triangle_set clip_mesh(const indexed_triangle_set &mesh, const std::vector<HalfSpace> &clips,
                               const std::vector<double> &inset = {});

std::shared_ptr<const MultiplanarPlan> clip_multiplanar_slices(PrintObject &object);

void build_multiplanar_chunks(PrintObject &object, const MultiplanarPlan &plan, ConformalResult &result);

}
