#pragma once











#include "LayerField.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

#include <admesh/stl.h>

#include <utility>
#include <vector>

namespace Slic3r::Nonplanar::Native {


struct Piece
{
    Vec3d                           n { Vec3d::UnitZ() };
    Vec3d                           o { Vec3d::Zero() };
    ExPolygons                      domain;
    std::vector<std::vector<Vec3d>> loops;
};

struct Layer
{
    double             level = 0.;
    std::vector<Piece> pieces;
};

struct Params
{
    double max_lean      = 0.;
    double mesh_edge_max = 0.5;
    double eps           = 1e-6;
    bool   fixed_head    = true;
};


std::pair<Vec3d, Vec3d> frame(const Vec3d &n);


std::vector<Layer> extract_layers(const indexed_triangle_set &mesh, const LayerField &field, const std::vector<double> &levels,
                                  const Params &params);



Vec3d lift(const Piece &piece, const Vec2d &plane_pt, const LayerField &field, double level);



std::vector<Vec3d> lift_polyline(const Piece &piece, const Polyline &road, const LayerField &field, double level, double max_chord_err);



double bead_height(const LayerField &field, const Vec3d &p, double level_below, double level, const Vec3d &axis);


Vec3d tool_axis(const LayerField &field, const Vec3d &p, double tilt_cap);





std::vector<Vec3f> smooth_axes(const std::vector<Vec3d> &tips, const std::vector<Vec3f> &exact, double sigma, double max_dev);

std::vector<Vec3f> tube_axes(const std::vector<Vec3f> &exact, double tube, double tilt_cap, double rate_b, double rate_c);

}
