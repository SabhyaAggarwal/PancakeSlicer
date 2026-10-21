#pragma once

#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <math.h>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

inline constexpr const char *ObjectiveVersion = "J v1.5.2";
inline constexpr double      ObjectiveVersionNumber = 1.52;

struct ObjectiveGeometry
{
    std::vector<Vec3d>    p, n;
    std::vector<double>   a;
    std::vector<int8_t>   cls;
    Vec3d                 origin { Vec3d::Zero() }, spacing { Vec3d::Ones() };
    std::array<size_t, 3> dims { 0, 0, 0 };
    std::vector<uint8_t>  occ;
    std::vector<float>    shell;

    size_t index(size_t i, size_t j, size_t k) const { return (i * dims[1] + j) * dims[2] + k; }
    size_t nodes() const { return dims[0] * dims[1] * dims[2]; }
};

struct HeadSamples
{
    Vec3d              centre;
    double             radius = 0.;
    std::vector<Vec3d> points;
};

struct ObjectiveCover
{
    double shell_rms = -1., shell_slope_cap = 38.66 * M_PI / 180.;
    double skin_rms = -1., skin_tilt_lo = 39. * M_PI / 180., skin_tilt_hi = 80. * M_PI / 180., skin_min_nz = -0.1, skin_time = 0.17;
    int    skin_classes = 0;
};

struct ObjectiveMachine
{
    double h = 0.2, first_layer = 0.2;
    double thickness_min = 0.1, thickness_max = 0.32;
    double printable_overhang = 45. * M_PI / 180.;
    bool   rotary = true;
    double tilt_max = 55. * M_PI / 180.;
    std::array<double, 24> slope_cap {};
    ObjectiveCover cover, cover_planar;
    double line_width = 0.4, infill_density = 0.15, shell_speed = 70., sparse_speed = 80., max_volumetric = 12.;
    double b_rate = 0.5, c_rate = 1.;
    bool   c_continuous = false;
    double road_factor = 1.;
    double min_layer_time = 8.;
    double travel_speed = 80.;
    double plate_radius = 1e9;
    double time_budget = std::numeric_limits<double>::infinity();
    std::vector<HeadSamples> head;
};

std::vector<HeadSamples> head_points(const HeadEnvelope &head, double pitch, double clip);

struct ObjectiveWeights
{
    double sup = 27.6, up = 1., wall = 1., down = 1., time = 0.5;
};

struct ObjectiveTerms
{
    double J = 0., S = 0., S_smooth = 0., E = 0., E_up = 0., E_wall = 0., E_down = 0., rms = 0., rms_up = 0., rms_wall = 0., rms_down = 0.;
    double T = 0., T_roads = 0., T_minlayer = 0., T_travel = 0., n_travel = 0., hop_mm = 0., island_layers = 0., road_pieces = 0., T_axis = 0., T_unwind = 0.,
           T_skin = 0., layers = 0., road_mm = 0.;
    int    H_thickness = 0, H_slope = 0, H_head = 0, H_head_probes = 0, H_bed = 0, H_time = 0;
    bool   feasible() const { return H_thickness == 0 && H_slope == 0 && H_head == 0 && H_bed == 0 && H_time == 0; }
    std::vector<std::pair<std::string, double>> named() const;
};

struct HeadPair
{
    size_t node;
    Vec3d  q;
};

double planar_support_time(const ObjectiveGeometry &geom, const ObjectiveMachine &machine);

ObjectiveTerms evaluate_objective(const ObjectiveGeometry &geom, const ObjectiveMachine &machine, const ObjectiveWeights &w,
                                  const std::vector<float> &phi, bool hard = true, std::vector<HeadPair> *pairs = nullptr);

struct ObjectiveParams
{
    int    iterations       = 40;
    int    start_iterations = 10;
    double start_tilt       = 35. * M_PI / 180.;
    bool   multi_start      = true;
    bool   warm_adaptive    = true;
    int    lottery_iterations = 3;
    int    lottery_keep       = 2;
    int    max_starts       = -1;
    double budget           = 0.;
    int    local_rounds     = 2;
    double local_tilt       = 45. * M_PI / 180.;
    double local_radius     = 8.;
    double axis_softness    = 0.1;
    double trust            = 0.5;
    double tilt_sparsity    = 1.;
    double roughness        = 0.14;
    int    level_steps      = 8;
    bool   planar_adaptive  = false;
    double level_ramp       = 2.;
    int    level_rounds     = 3;
};

ObjectiveTerms solve_objective(const ObjectiveGeometry &geom, const ObjectiveMachine &machine, const ObjectiveWeights &w,
                               const ObjectiveParams &params, std::vector<float> &phi, const std::function<void()> &cancel,
                               const std::function<void(const std::string &)> &log);

}
