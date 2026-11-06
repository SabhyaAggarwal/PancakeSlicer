#ifndef slic3r_TriangleMeshSlicer_hpp_
#define slic3r_TriangleMeshSlicer_hpp_

#include <cstdint>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>
#include <admesh/stl.h>

#include "Point.hpp"
#include "Polygon.hpp"
#include "ExPolygon.hpp"

namespace Slic3r {

struct MeshSlicingParams
{
    enum class SlicingMode : uint32_t {


        Regular,


        EvenOdd,


        Positive,


        PositiveLargestContour,
    };

    SlicingMode   mode { SlicingMode::Regular };


    size_t        slicing_mode_normal_below_layer { 0 };

    SlicingMode   mode_below { SlicingMode::Regular };

    Transform3d   trafo { Transform3d::Identity() };
};

struct MeshSlicingParamsEx : public MeshSlicingParams
{

    float         closing_radius { 0 };

    float         extra_offset { 0 };


    double        resolution { 0 };
};












std::vector<Polygons>           slice_mesh(
    const indexed_triangle_set       &mesh,
    const std::vector<float>         &zs,
    const MeshSlicingParams          &params,
    std::function<void()>             throw_on_cancel = []{});


Polygons                        slice_mesh(
    const indexed_triangle_set       &mesh,
    const float                       plane_z,
    const MeshSlicingParams          &params);

std::vector<ExPolygons>         slice_mesh_ex(
    const indexed_triangle_set       &mesh,
    const std::vector<float>         &zs,
    const MeshSlicingParamsEx        &params,
    std::function<void()>             throw_on_cancel = []{});

inline std::vector<ExPolygons>  slice_mesh_ex(
    const indexed_triangle_set       &mesh,
    const std::vector<float>         &zs,
    std::function<void()>             throw_on_cancel = []{})
{
    return slice_mesh_ex(mesh, zs, MeshSlicingParamsEx{}, throw_on_cancel);
}

inline std::vector<ExPolygons>  slice_mesh_ex(
    const indexed_triangle_set       &mesh,
    const std::vector<float>         &zs,
    float                             closing_radius,
    std::function<void()>             throw_on_cancel = []{})
{
    MeshSlicingParamsEx params;
    params.closing_radius = closing_radius;
    return slice_mesh_ex(mesh, zs, params, throw_on_cancel);
}







void slice_mesh_slabs(
    const indexed_triangle_set       &mesh,

    const std::vector<float>         &zs,
    const Transform3d                &trafo,
    std::vector<Polygons>            *out_top,
    std::vector<Polygons>            *out_bottom,
    std::vector<std::pair<Vec3f, Vec3f>>   *vertical_points,
    std::function<void()>             throw_on_cancel);


void project_mesh(
    const indexed_triangle_set       &mesh,
    const Transform3d                &trafo,
    Polygons                         *out_top,
    Polygons                         *out_bottom,
    std::function<void()>             throw_on_cancel);


Polygons project_mesh(
    const indexed_triangle_set       &mesh,
    const Transform3d                &trafo,
    std::function<void()>             throw_on_cancel);

void cut_mesh(
    const indexed_triangle_set      &mesh,
    float                            z,
    indexed_triangle_set            *upper,
    indexed_triangle_set            *lower,
    bool                             triangulate_caps = true);

}

#endif
