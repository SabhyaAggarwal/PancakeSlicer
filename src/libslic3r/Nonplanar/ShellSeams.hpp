#pragma once
#include "libslic3r/GCode/ConformalEmit.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

#include <string>
#include <vector>

namespace Slic3r {

class ExtrusionPathConformal;
class GCodeWriter;
class HeightField;

namespace Nonplanar {

struct FlowShape
{
    double ramp  = 0.;
    double taper = 0.;
    double coast = 0.;
    bool   open_end = false;
    bool   scored   = false;
};
constexpr double StartRamp = 1.;
constexpr double EndTaper  = 0.5;
constexpr double EndCoast  = 0.5;
constexpr double CoastMinRun    = 5.;
constexpr double SoftTaperFloor = 0.02;

std::vector<ConformalPoint> shape_runs(const std::vector<ConformalPoint> &points, const FlowShape &shape, double min_piece,
                                       std::vector<double> &flow);

constexpr double StartTuck = 2.;
void orient_free_roads(std::vector<const ExtrusionPathConformal *> &order, std::vector<ExtrusionPathConformal> &storage,
                       double min_rise = 0.1);

double pressure_advance_scale(double height, double nominal_height);

struct PressureAdvance
{
    double      value = -1.;
    std::string command;
};
PressureAdvance parse_pressure_advance(const std::string &gcode);

class PressureAdvanceTracker
{
public:
    PressureAdvanceTracker(const GCodeWriter &writer, const PressureAdvance &base, double nominal_height)
        : m_writer(writer), m_base(base), m_nominal(nominal_height), m_height(nominal_height) {}
    void        update(double height, std::string &out);
    void        restore(std::string &out);
    bool        along_road() const { return m_along_road; }
    void        set_along_road(bool along) { m_along_road = along; }
    int         changes() const { return m_changes; }

private:
    std::string set(double value) const;

    const GCodeWriter &m_writer;
    PressureAdvance    m_base;
    double             m_nominal, m_height;
    bool               m_changed    = false;
    bool               m_along_road = false;
    int                m_changes    = 0;
};
constexpr double PressureAdvanceStep = 0.25;

size_t convex_seam(const std::vector<double> &rear, const std::vector<double> &curvature, const std::vector<double> &z);
double mean_bead_height(const std::vector<ConformalPoint> &points);
double meridian_curvature(const HeightField &surface, const Polyline &meridian, double length = 5.);

}
}
