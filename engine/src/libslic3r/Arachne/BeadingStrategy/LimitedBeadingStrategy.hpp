//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIMITED_BEADING_STRATEGY_H
#define LIMITED_BEADING_STRATEGY_H

#include <string>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne
{

class LimitedBeadingStrategy : public BeadingStrategy
{
public:
    LimitedBeadingStrategy(coord_t max_bead_count, BeadingStrategyPtr parent);
    
    ~LimitedBeadingStrategy() override = default;
    
    Beading compute(coord_t thickness, coord_t bead_count) const override;
    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    std::string toString() const override;
    
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;
    
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;

protected:
    const coord_t max_bead_count;
    const BeadingStrategyPtr parent;
};

}
#endif
