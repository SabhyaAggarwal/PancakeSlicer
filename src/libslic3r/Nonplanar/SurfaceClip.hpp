#pragma once
#include "HeightField.hpp"
#include "HeadEnvelope.hpp"
#include <functional>
#include <vector>

namespace Slic3r::Nonplanar {

HeightField head_lipschitz_minorant(const HeightField &S, const HeadEnvelope &head,
                                    int max_iterations = 4,
                                    const std::function<void()> &throw_if_canceled = {});

HeightField min_safe_tip_height(const HeightField &material, const HeadEnvelope &head,
                                const HeightField &like);

HeightField head_lipschitz_minorant(const HeightField &S, const HeadEnvelope &head,
                                    int max_iterations, const std::function<void()> &throw_if_canceled,
                                    std::vector<int> *limiting_components);
HeightField min_safe_tip_height(const HeightField &material, const HeadEnvelope &head,
                                const HeightField &like, std::vector<int> *limiting_components,
                                bool apply_tip_allowance = false);

HeightField slope_cap_minorant(const HeightField &S, double max_slope);

HeightField apply_surface_cap(const HeightField &S, const HeightField &cap);

}
