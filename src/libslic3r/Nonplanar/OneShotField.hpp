#pragma once
#include <chrono>
#include "FieldObjective.hpp"
#include "NpField.hpp"
#include "Rests.hpp"
#include "libslic3r/Point.hpp"

#include <admesh/stl.h>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct MultiplanarPlan;
struct MeasuredPrint;

struct OneShotState
{
    static constexpr double MaxTimeRatio = 1.15;
    static constexpr double ZOnlySpread  = 0.05;

    std::shared_ptr<const NpField> solved;
    struct RawSolve
    {
        NpField              field;
        std::vector<NpField> time_cut;
        double               cap_scale;
        bool                 anchor;
    };
    std::shared_ptr<const RawSolve> raw;
    bool                           z_only = false;
    std::string                    decision;
    std::string                    student = "none";
    int                            polish_k = 0;
    double                         max_time_ratio_override = -1.;
    std::shared_ptr<const MultiplanarPlan> route;
    std::string                    route_log;
    int                            plinth_layers = 0;
    bool                           plinth_applied = false;
    int                            plinth_user_layers = 0;
    double                         plinth_user_expansion = 0., plinth_user_first_expansion = 0.;
    bool                           injected = false;
    double                         measured_ratio   = std::numeric_limits<double>::quiet_NaN();
    int                            measured_repicks = 0;
    bool                           measured_anchor  = false;
    double                         measured_seconds = 0.;
    std::chrono::steady_clock::time_point measure_deadline {};
    double                         measure_work_left = std::numeric_limits<double>::quiet_NaN();
    double                         cap_scale        = 1.;
    std::string                    time_cut_note;
    enum class Offer { Body, NormalSupport, Control, Auto };
    Offer                          offer            = Offer::Body;
    double                         measured_floating = std::numeric_limits<double>::quiet_NaN();
    double                         control_floating  = std::numeric_limits<double>::quiet_NaN();
    std::string                    auto_note;
    std::string                    band_note;
    bool                           auto_weighed = false;
    std::shared_ptr<const MeasuredPrint> auto_measured;
    bool                           auto_floats = false;
    bool                           auto_planar = false;
    double                         planar_air  = -1.;

    double max_time_ratio() const { return max_time_ratio_override > 0. ? max_time_ratio_override : MaxTimeRatio; }
    double measured_room() const { return max_time_ratio() / measured_ratio - 1.; }
    double time_ratio() const;
    double time_cap() const;
    bool combine_infill() const;
    std::string offers() const;
    std::string floating_decision() const;

    bool field() const { return solved != nullptr; }
    bool field_path() const { return field() && !z_only; }
};

bool oneshot_requested(const PrintObject &object);
constexpr double MeasureWorkBudget   = 15000.;
constexpr double MeasureWorkFacets   = 50000.;
constexpr double MeasureWorkMaxScale = 10.;
constexpr double MeasureSafetyNet    = 7200.;
double                               *measure_work(const PrintObject &object, OneShotState &state);
std::chrono::steady_clock::time_point measure_deadline(OneShotState &state);

bool take_injected_oneshot(OneShotState &state);

constexpr int MaxRepicks = 2;
void measured_export_check(PrintObject &object, OneShotState &state, const std::function<void()> &resolve);
bool measured_repick(const PrintObject &object, OneShotState &state, double ratio, const Floating &floating, bool support);
bool band_control(const PrintObject &object, OneShotState &state);
bool control_past_cap(const PrintObject &object, OneShotState &state);
bool body_earns_control(const PrintObject &object, OneShotState &state, const Floating &floating);
bool past_floating_bar(const PrintObject &object, OneShotState &state, const Floating &floating);
bool auto_offer(const PrintObject &object, OneShotState &state, double t_ps, double pick_rms, double pick_time);

double in_plane_spread(const NpField &field);

std::vector<double> layer_profile(const NpField &field, double first_layer, double top, double min_h, double max_h,
                                  const std::vector<std::pair<double, double>> &planar_bands = {}, double planar_h = 0.);

std::vector<std::pair<double, double>> supported_underside_bands(const indexed_triangle_set &mesh, int threshold_deg, double band);

ObjectiveGeometry objective_geometry(const indexed_triangle_set &mesh, int wall_loops, int top_shell_layers, double line_width,
                                     double layer_height, const std::function<bool(const std::array<Vec3d, 3> &)> &unsampled = {});

ObjectiveMachine objective_machine(const PrintObject &object, const ObjectiveGeometry &geom, double tilt_max);

enum class OneShotAbort { None, CpuTime, WallTime, Infeasible };
struct OneShotAbortReason
{
    OneShotAbort kind = OneShotAbort::None;
    std::string  what;
};

NpField one_shot_field(const PrintObject &object, const indexed_triangle_set &mesh, double tilt_max, const std::function<void()> &cancel,
                       std::string &student, int &polish_k, OneShotAbortReason &abort, std::vector<NpField> *time_cut = nullptr,
                       const std::function<bool(const std::array<Vec3d, 3> &)> &unsampled = {});

void choose_route(PrintObject &object, OneShotState &state);

bool admit_time_cut(const PrintObject &object, OneShotState &state, std::vector<NpField> &cuts);
bool admit_lean_offer(const PrintObject &object, OneShotState &state, std::vector<NpField> &cuts);
inline const std::string FieldSolverOffer = "field solver ";

double max_time_ratio_override();

}
