#pragma once
#include "libslic3r/Point.hpp"

#include "admesh/stl.h"

#include <vector>

namespace Slic3r {
class DynamicPrintConfig;
}

namespace Slic3r::Nonplanar {

struct OrientParams
{
    double layer_height       = 0.2;
    double outer_width        = 0.3;
    double first_layer_height = 0.2;
    double support_angle      = 30.;
    int    samples            = 200;
    int    refine             = 5;
};
OrientParams orient_params(const DynamicPrintConfig &config);

struct OrientScore
{
    Vec3d  up             = Vec3d::UnitZ();
    double flat_top       = 0.;
    double curved_top     = 0.;
    double steep_top      = 0.;
    double curved_stair   = 0., steep_stair = 0., under_stair = 0.;
    double curved_share   = 1.;
    double support_area   = 0.;
    double support_volume = 0.;
    double bed            = 0.;
    double footprint      = 0.;
    double height         = 0.;
    double time_s         = 0.;
    double defect         = 0.;
    double cost           = 0.;
    bool   stable         = false;
    bool   measured        = false;
    double printed_support = 0.;
};

constexpr double OrientDefectCost = 0.5;

OrientScore score_orientation(const indexed_triangle_set &its, const Vec3d &up, const OrientParams &params);
void        rescore(OrientScore &score);

bool        replaces(const OrientScore &candidate, const OrientScore &original);

struct OrientResult
{
    OrientScore              original;
    std::vector<OrientScore> ranked;
    OrientScore              chosen;
    bool                     rotated = false;
};
OrientResult auto_orient(const indexed_triangle_set &its, const OrientParams &params);

Matrix3d rotation_to_z(const Vec3d &up);

}
