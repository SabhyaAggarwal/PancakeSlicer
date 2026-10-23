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
    static constexpr double MaxTimeRatio = 1.15;
    static constexpr double ZOnlySpread  = 0.05;

    std::shared_ptr<const NpField> solved;
    bool                           z_only = false;
    std::string                    decision;
    std::string                    student = "none";
    int                            polish_k = 0;
    double                         max_time_ratio_override = -1.;

    double max_time_ratio() const { return max_time_ratio_override > 0. ? max_time_ratio_override : MaxTimeRatio; }
    double time_ratio() const;
    double time_cap() const;

    bool field() const { return solved != nullptr; }
    bool field_path() const { return field() && !z_only; }
};

bool oneshot_requested(const PrintObject &object);

double in_plane_spread(const NpField &field);

std::vector<double> layer_profile(const NpField &field, double first_layer, double top, double min_h, double max_h);

ObjectiveGeometry objective_geometry(const indexed_triangle_set &mesh, int wall_loops, int top_shell_layers, double line_width,
                                     double layer_height);

ObjectiveMachine objective_machine(const PrintObject &object, const ObjectiveGeometry &geom, double tilt_max);

enum class OneShotAbort { None, CpuTime, WallTime, Infeasible };
struct OneShotAbortReason
{
    OneShotAbort kind = OneShotAbort::None;
    std::string  what;
};

NpField one_shot_field(const PrintObject &object, const indexed_triangle_set &mesh, double tilt_max, const std::function<void()> &cancel,
                       std::string &student, int &polish_k, OneShotAbortReason &abort);

double max_time_ratio_override();

}
