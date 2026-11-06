#ifndef slic3r_SketchInference_hpp_
#define slic3r_SketchInference_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include <math.h>
#include <vector>
#include <optional>
#include <cmath>

namespace Slic3r {







struct InferenceSnap {
    enum class Kind { None, Endpoint, Center, Midpoint, OnEdge, Origin };
    Kind            kind{Kind::None};
    int             entity{-1};
    SketchPointRole role{SketchPointRole::P0};
    Vec2d           point{0, 0};

    bool snapped() const { return kind != Kind::None; }
};





InferenceSnap infer_point_snap(const std::vector<SketchEntity>& entities,
                               const Vec2d& query, double tol,
                               bool include_origin = true);







inline constexpr double kSketchInferAngleTol = 3.0 * M_PI / 180.0;

std::optional<SketchConstraintType>
infer_axis_constraint(const Vec2d& anchor, const Vec2d& tip, double ang_tol_rad = kSketchInferAngleTol);







std::vector<SketchEntityConstraintDef>
infer_relations(const std::vector<SketchEntity>& entities, int new_ei,
                double ang_tol_rad = kSketchInferAngleTol,
                double len_tol_frac = 0.01);

}

#endif
