#pragma once




#include "HeightField.hpp"
#include "libslic3r/Point.hpp"

#include <cstddef>
#include <limits>
#include <vector>

namespace Slic3r {
class ExtrusionPathConformal;
}

namespace Slic3r::Nonplanar {


constexpr double FlowModMaxThickness = 0.75;


constexpr double FlowModMinThickness = 0.05;

constexpr double FlowModSmoothing = 2.5;

struct FlowModParams
{
    double land_radius = 0.5;
    double max_height  = 0.3;
    double min_normal  = FlowModMinThickness;
    double smoothing   = 1.;
    double resample    = 0.2;
};

struct FlowModStats
{
    size_t points          = 0;
    size_t max_clamped     = 0;
    size_t min_clamped     = 0;
    size_t dragged         = 0;


    size_t contact         = 0;

    double min_gap = std::numeric_limits<double>::infinity(), max_gap = 0.;
    double min_laid = std::numeric_limits<double>::infinity(), max_laid = 0.;

    void merge(const FlowModStats &rhs);
};



struct ShellDeficit
{
    HeightField        value;
    std::vector<float> distance;

    explicit ShellDeficit(const HeightField &like);
    float sample(const Vec2d &p) const;
};







void modulate_flow(ExtrusionPathConformal &path, const HeightField &surface, const ShellDeficit *below, ShellDeficit *deficit,
                   const FlowModParams &params, FlowModStats &stats);

}
