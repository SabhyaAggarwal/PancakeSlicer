#pragma once
#include "FieldObjective.hpp"
#include "NpField.hpp"
#include "libslic3r/Point.hpp"

#include <admesh/stl.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct OneShotState
{
    enum class Stage { Reference, Field, Final };
    static constexpr int    MaxReemits   = 3;
    static constexpr double MaxTimeRatio = 1.15;
    static constexpr double ZOnlySpread  = 0.05;

    std::shared_ptr<const NpField> solved;
    bool                           z_only = false;
    Stage                          stage = Stage::Reference;
    double                         blend = 1.;
    bool                           planar = false;
    int                            reemits = 0;
    double                         planar_time = -1.;
    std::string                    decision;
    std::string                    student = "none";
    int                            polish_k = 0;

    bool field() const { return stage == Stage::Field || (stage == Stage::Final && !planar); }
    bool field_path() const { return field() && !z_only; }
};

bool oneshot_requested(const PrintObject &object);

double in_plane_spread(const NpField &field);

std::vector<double> layer_profile(const NpField &field, double blend, double first_layer, double top, double min_h, double max_h);

NpField blend_toward_planar(const NpField &field, double blend);

ObjectiveGeometry objective_geometry(const indexed_triangle_set &mesh, int wall_loops, int top_shell_layers, double line_width,
                                     double layer_height);

ObjectiveMachine objective_machine(const PrintObject &object, const ObjectiveGeometry &geom, double tilt_max);

NpField one_shot_field(const PrintObject &object, const indexed_triangle_set &mesh, double tilt_max, double budget,
                       const std::function<void()> &cancel, std::string &student, int &polish_k);

}
