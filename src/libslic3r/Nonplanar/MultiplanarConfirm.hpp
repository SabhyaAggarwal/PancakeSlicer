#pragma once
#include "Rests.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
class PrintObject;
struct GCodeProcessorResult;
}

namespace Slic3r::Nonplanar {

struct MultiplanarPlan;

double steep_top_share(const indexed_triangle_set &mesh);
double thin_top_share(const indexed_triangle_set &mesh, double thickness);

bool rotary_auto_measurement();
bool rotary_measurement_unverified();
bool rotary_measurement_too_slow(double estimated_time);

bool auto_rotary_requested(const PrintObject &object);

constexpr double RotaryMaxTimeRatio = 2.5;

struct MeasuredPrint
{
    double   time = std::numeric_limits<double>::quiet_NaN();
    Floating floating { -1., -1. };
    bool     support = false;
    double   rms     = std::numeric_limits<double>::quiet_NaN();
    bool     as_printed = false;
};
double measured_time(const PrintObject &object, NonplanarMode mode, const std::vector<std::pair<std::string, std::string>> &overrides = {},
                     const std::string &keep_as = {}, bool verify = true, MeasuredPrint *measured = nullptr);

double export_normal_rms(const PrintObject &object, const GCodeProcessorResult &result);

double planar_support_time(const PrintObject &object);

std::string auto_rotary_planar(const PrintObject &object);

bool auto_rotary_candidate(const PrintObject &object, const indexed_triangle_set &mesh);
std::string auto_rotary_chunks(const PrintObject &object, const indexed_triangle_set &mesh, const MultiplanarPlan &plan);

}
