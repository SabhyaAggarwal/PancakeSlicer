#pragma once
#include "HeadEnvelope.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <math.h>
#include <memory>
#include <vector>

namespace Slic3r {
class ToolKinematics;
}

namespace Slic3r::Nonplanar {

class OrientationField;



Matrix3d field_head_frame(const ToolKinematics *kinematics, const Vec3d &tip, const Vec3d &axis);




std::vector<Vec3d> head_samples(const HeadEnvelope &head, double step);


struct FieldRoad
{
    std::unique_ptr<ExtrusionPathConformal> path;
    std::vector<Vec3d> tip;
    std::vector<Vec3d> deformed;
    size_t             layer = 0;
    bool               shell = false;


    bool               by_height = false;
};

struct FieldOrderParams
{
    HeadEnvelope          head;
    const ToolKinematics *kinematics = nullptr;

    std::vector<double> layer_top;
    std::vector<double> layer_height;
    double              cell = 5.;
    double              max_tilt = M_PI / 3.;



    double              lean_max = 20. * M_PI / 180.;


    bool                planar_support = false;


    double              axis_blend = 0.;
};



struct FieldSupport
{
    std::vector<FieldRoad> roads;
    std::vector<double>    top, height;
};

struct FieldOrderStats
{
    size_t pieces = 0, support_edges = 0, head_edges = 0;
    size_t groups = 0;
    size_t layers = 0;
    size_t cycle_pieces = 0;
    size_t table_poses = 0;
    double floating_mm = 0.;
    size_t support_layers = 0, support_pieces = 0;
    size_t leaned_points = 0;
    size_t blocked_points = 0;
    int    split_passes = 0;
    size_t shell_roads = 0;
};

struct FieldOrder
{
    std::vector<ExtrusionEntityCollection> groups;
    std::vector<size_t>                    group_layers;
    std::vector<Vec3d>                     relax_points;


    std::vector<Vec3d>                     relax_dirs;
    std::vector<Vec3d>                     floating_points;
    FieldOrderStats                        stats;
};










FieldOrder order_field_roads(std::vector<FieldRoad> &roads, const OrientationField &field, const FieldOrderParams &params,
                             const std::function<FieldSupport(const std::vector<Vec3d> &)> &support = {},
                             const std::function<void()> &throw_if_canceled = {});





FieldOrder layer_order_field_roads(std::vector<FieldRoad> &roads, const FieldOrderParams &params);





double wall_floating_mm(const std::vector<ExPolygons> &slices, const std::vector<double> &heights, int wall_loops, double width);

}
