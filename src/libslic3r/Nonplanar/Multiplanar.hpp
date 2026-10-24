#pragma once
#include <math.h>
#include "HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class DynamicPrintConfig;
class ExtrusionEntity;
class ExtrusionPath;
class PrintConfig;
class PrintObject;
class ToolKinematics;
struct JointPose;
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
    double                 step_area     = 0.;
};

struct MultiplanarParams
{
    double                layer_height       = 0.2;
    double                first_layer_height = 0.2;
    double                overhang_threshold = 0.25 * M_PI;
    int                   max_planes         = 3;
    HeadEnvelope          head;
    const ToolKinematics *kinematics         = nullptr;
    double                min_overhang_area  = 4.;
    Polygons              plates;
};

struct MultiplanarCut
{
    int              parent = 0;
    HalfSpace        parent_clip;
    MultiplanarChunk chunk;
};

struct MultiplanarPlan
{
    std::vector<MultiplanarChunk> chunks;
    std::vector<MultiplanarCut>   cuts;
    double                        residual_overhang_area = 0.;
    double                        step_area              = 0.;
    double                        planar_step_area       = 0.;
    double                        planar_overhang_area   = 0.;
    std::vector<std::string>      notes;
    std::string                   auto_reason;
};

MultiplanarPlan plan_multiplanar(const indexed_triangle_set &mesh, const MultiplanarParams &params,
                                 const std::function<void()> &throw_if_canceled = {});

std::vector<int> multiplanar_cut_parents(const MultiplanarPlan &plan);
MultiplanarPlan multiplanar_subset(const MultiplanarPlan &plan, const std::vector<bool> &keep);
Matrix3d chunk_frame(const ToolKinematics &kinematics, const JointPose &joints, const Vec3d &dir);

std::string chunk_head_hit(const indexed_triangle_set &mesh, const MultiplanarPlan &plan, size_t ci, const MultiplanarParams &params);

indexed_triangle_set clip_mesh(const indexed_triangle_set &mesh, const std::vector<HalfSpace> &clips,
                               const std::vector<double> &inset = {});

indexed_triangle_set chunk_piece(const indexed_triangle_set &mesh, const MultiplanarChunk &chunk, double layer_height,
                                 double line_width = 0., const std::vector<bool> &later = {});

int printable_layers(const indexed_triangle_set &piece, const MultiplanarChunk &chunk, double layer_height);

bool full_orientation(const PrintConfig &config);
indexed_triangle_set slicing_frame_mesh(const PrintObject &object);
DynamicPrintConfig chunk_print_config(const PrintObject &object);
void collect_paths(const ExtrusionEntity *ee, std::vector<const ExtrusionPath *> &out);

MultiplanarParams multiplanar_params(const PrintObject &object, const ToolKinematics *kinematics);
MultiplanarPlan   object_multiplanar_plan(const PrintObject &object, const indexed_triangle_set &mesh, const MultiplanarParams &params);

std::shared_ptr<const MultiplanarPlan> clip_multiplanar_slices(PrintObject &object);

void build_multiplanar_chunks(PrintObject &object, const MultiplanarPlan &plan, ConformalResult &result);

}
