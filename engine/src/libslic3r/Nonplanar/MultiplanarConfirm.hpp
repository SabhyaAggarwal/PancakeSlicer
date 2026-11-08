#pragma once
#include "Rests.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r {
class Print;
class PrintObject;
struct GCodeProcessorResult;
}

namespace Slic3r::Nonplanar {

struct MultiplanarPlan;

double steep_top_share(const indexed_triangle_set &mesh);
double thin_top_share(const indexed_triangle_set &mesh, double thickness);

bool rotary_auto_measurement();
bool curved_planar_measurement();
bool rotary_measurement_unverified();
bool rotary_measurement_too_slow(double estimated_time);

bool auto_rotary_requested(const PrintObject &object);

constexpr double RotaryMaxTimeRatio = 2.5;

using MeasureClock = std::chrono::steady_clock;
class MeasureDeadline
{
public:
    explicit MeasureDeadline(MeasureClock::time_point at);
    ~MeasureDeadline();
    MeasureDeadline(const MeasureDeadline &) = delete;
    MeasureDeadline &operator=(const MeasureDeadline &) = delete;

private:
    MeasureClock::time_point m_previous;
};
bool measure_deadline_passed();
class MeasureWork
{
public:
    explicit MeasureWork(double *left);
    ~MeasureWork();
    MeasureWork(const MeasureWork &) = delete;
    MeasureWork &operator=(const MeasureWork &) = delete;

private:
    double *m_previous;
};
bool measure_work_spent();
bool measure_work_active();
class DeadlineWatch
{
public:
    explicit DeadlineWatch(PrintBase &print);
    ~DeadlineWatch();
    DeadlineWatch(const DeadlineWatch &) = delete;
    DeadlineWatch &operator=(const DeadlineWatch &) = delete;

private:
    std::mutex              m_mutex;
    std::condition_variable m_done;
    bool                    m_finished = false;
    std::thread             m_watch;
};

struct MeasuredPrint
{
    double   time = std::numeric_limits<double>::quiet_NaN();
    Floating floating { -1., -1. };
    bool     support = false;
    double   support_mm3 = std::numeric_limits<double>::quiet_NaN();
    double   rms     = std::numeric_limits<double>::quiet_NaN();
    bool     as_printed = false;
    bool     count_contacts = false;
    size_t   contacts       = 0;
};
double measured_control(const PrintObject &object, MeasuredPrint &measured);
double measured_planar_export(const PrintObject &object, GCodeProcessorResult &result);
double measured_planar_export(const PrintObject &object, GCodeProcessorResult &result, const std::string &keep_as);
double measured_walls_export(const PrintObject &object, GCodeProcessorResult &result, const std::string &keep_as);
double net_support_mm3(const Print &print, const GCodeProcessorResult &result);
const std::string &last_measured_failure();
double measured_time(const PrintObject &object, NonplanarMode mode, const std::vector<std::pair<std::string, std::string>> &overrides = {},
                     const std::string &keep_as = {}, bool verify = true, MeasuredPrint *measured = nullptr);

double export_normal_rms(const PrintObject &object, const GCodeProcessorResult &result, bool whole_surface = false);

double planar_support_time(const PrintObject &object);

std::string auto_rotary_planar(const PrintObject &object);

bool auto_rotary_candidate(const PrintObject &object, const indexed_triangle_set &mesh);
std::string auto_rotary_chunks(const PrintObject &object, const indexed_triangle_set &mesh, const MultiplanarPlan &plan);

}
