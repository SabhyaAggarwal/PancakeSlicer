#ifndef slic3r_SketchSolver_hpp_
#define slic3r_SketchSolver_hpp_

#include <vector>

namespace Slic3r { enum class SketchPointRole; }
namespace Slic3r { struct SketchEntity; }
namespace Slic3r { struct SketchEntityConstraintDef; }

namespace Slic3r {

struct SketchSolveResult {
    bool             ok{false};
    int              dof{-1};
    int              result{0};
    std::vector<int> bad;
    std::vector<int> skipped;
};

SketchSolveResult sketch_solve(std::vector<SketchEntity>& entities,
                               const std::vector<SketchEntityConstraintDef>& constraints);

SketchSolveResult sketch_solve_drag(std::vector<SketchEntity>& entities,
                                    const std::vector<SketchEntityConstraintDef>& constraints,
                                    int dragged_ei, SketchPointRole dragged_role);

}

#endif
