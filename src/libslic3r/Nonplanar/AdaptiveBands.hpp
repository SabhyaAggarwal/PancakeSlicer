#pragma once
#include "HeightField.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

#include <vector>

namespace Slic3r::Nonplanar {

constexpr double AdaptiveMinHeight = 0.2;
constexpr double AdaptiveMaxHeight = 0.75;
constexpr double AdaptiveMaxOuterHeight = 0.6;

double outer_bead_width(double height, double nozzle, double max_width);

double outer_residual(double width, double slope, double curvature);

struct AdaptiveStack
{
    double      total        = 0.;
    double      shell_height = 0.;
    double      max_height   = 0.;
    int         top_uniform  = 0;
    int         slots        = 0;
    int         below        = 0;
    HeightField outer;
    HeightField count;
    HeightField layers;
    std::vector<double>     levels;
    std::vector<ExPolygons> zones;
    double      dominant     = 0.;

    int shells() const { return slots + below; }
    bool layer(int k, const Vec2d &p, double &drop, double &bead, bool &deepest) const;
    double inner_height(double outer, int count) const;
    int    top_count(double outer) const;
    void   make_uniform(size_t i);
    double min_height() const;
    double nominal_drop(int k) const;
    void   update_zones();
};

AdaptiveStack adaptive_stack(const HeightField &surface, const HeightField &layers, double total, double shell_height, int below,
                             double nozzle, double max_width, double max_error);

}
