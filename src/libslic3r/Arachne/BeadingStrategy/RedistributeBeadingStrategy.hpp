//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef REDISTRIBUTE_DISTRIBUTED_BEADING_STRATEGY_H
#define REDISTRIBUTE_DISTRIBUTED_BEADING_STRATEGY_H

#include <string>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne
{
    class RedistributeBeadingStrategy : public BeadingStrategy
    {
    public:
        RedistributeBeadingStrategy(coord_t optimal_width_outer, double minimum_variable_line_ratio, BeadingStrategyPtr parent);

        ~RedistributeBeadingStrategy() override = default;

        Beading compute(coord_t thickness, coord_t bead_count) const override;

        coord_t getOptimalThickness(coord_t bead_count) const override;
        coord_t getTransitionThickness(coord_t lower_bead_count) const override;
        coord_t getOptimalBeadCount(coord_t thickness) const override;
        coord_t getTransitioningLength(coord_t lower_bead_count) const override;
        float getTransitionAnchorPos(coord_t lower_bead_count) const override;

        std::string toString() const override;

    protected:
        BeadingStrategyPtr parent;
        coord_t optimal_width_outer;
        double minimum_variable_line_ratio;
    };

}
#endif
