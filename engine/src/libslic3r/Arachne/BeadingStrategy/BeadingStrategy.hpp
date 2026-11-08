// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef BEADING_STRATEGY_H
#define BEADING_STRATEGY_H

#include <math.h>
#include <memory>
#include <string>
#include <vector>
#include <cmath>

#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne
{

template<typename T> constexpr T pi_div(const T div) { return static_cast<T>(M_PI) / div; }

class BeadingStrategy
{
public:
    struct Beading
    {
        coord_t total_thickness;
        std::vector<coord_t> bead_widths;
        std::vector<coord_t> toolpath_locations;
        coord_t left_over;
    };

    BeadingStrategy(coord_t optimal_width, double wall_split_middle_threshold, double wall_add_middle_threshold, coord_t default_transition_length, float transitioning_angle = pi_div(3));

    BeadingStrategy(const BeadingStrategy &other);

    virtual ~BeadingStrategy() = default;

    virtual Beading compute(coord_t thickness, coord_t bead_count) const = 0;

    virtual coord_t getOptimalThickness(coord_t bead_count) const;

    virtual coord_t getTransitionThickness(coord_t lower_bead_count) const;

    virtual coord_t getOptimalBeadCount(coord_t thickness) const = 0;

    virtual coord_t getTransitioningLength(coord_t lower_bead_count) const;

    virtual float getTransitionAnchorPos(coord_t lower_bead_count) const;

    virtual std::vector<coord_t> getNonlinearThicknesses(coord_t lower_bead_count) const;

    virtual std::string toString() const;

    double  getSplitMiddleThreshold() const;
    double  getTransitioningAngle() const;

protected:
    std::string name;

    coord_t optimal_width;

    double  wall_split_middle_threshold;

    double  wall_add_middle_threshold;

    coord_t default_transition_length;
    double  transitioning_angle;
};

using BeadingStrategyPtr = std::unique_ptr<BeadingStrategy>;

}
#endif
