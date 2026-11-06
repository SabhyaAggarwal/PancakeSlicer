#ifndef slic3r_CutSurface_hpp_
#define slic3r_CutSurface_hpp_

#include "libslic3r/BoundingBox.hpp"
#include <string>
#include <vector>
#include <admesh/stl.h>
#include "ExPolygon.hpp"

namespace Slic3r::Emboss { class IProject3d; }
namespace Slic3r::Emboss { class IProjection; }

namespace Slic3r{





struct SurfaceCut : public indexed_triangle_set
{

    using Index = unsigned int;
    using Contour = std::vector<Index>;
    using Contours = std::vector<Contour>;

    Contours contours;
};













SurfaceCut cut_surface(const ExPolygons                        &shapes,
                       const std::vector<indexed_triangle_set> &models,
                       const Emboss::IProjection               &projection,
                       float projection_ratio);







indexed_triangle_set cut2model(const SurfaceCut         &cut,
                               const Emboss::IProject3d &projection);









indexed_triangle_set its_cut_AoI(const indexed_triangle_set &its,
                                 const BoundingBox          &bb,
                                 const Emboss::IProjection  &projection);







indexed_triangle_set its_mask(const indexed_triangle_set &its, const std::vector<bool> &mask);

bool corefine_test(const std::string &model_path, const std::string &shape_path);

}
#endif
