#ifndef slic3r_SurfaceDrag_hpp_
#define slic3r_SurfaceDrag_hpp_

#include "libslic3r/Model.hpp"
#include <optional>
#include "libslic3r/Point.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "wx/event.h"
#include <functional>

namespace Slic3r {
class GLVolume;
class ModelVolume;
}

namespace Slic3r::GUI {
class GLCanvas3D;
class Selection;
class TransformationType;
struct Camera;

struct SurfaceDrag
{
    Vec2d mouse_offset;

    Transform3d world;

    Transform3d instance_inv;

    GLVolume *gl_volume;

    RaycastManager::AllowVolumes condition;

    std::optional<float> start_angle;

    std::optional<float> start_distance;

    bool exist_hit = true;

    Vec2d mouse_offset_without_sla_shift;
};

constexpr double UP_LIMIT = 0.9;

bool on_mouse_surface_drag(const wxMouseEvent         &mouse_event,
                           const Camera               &camera,
                           std::optional<SurfaceDrag> &surface_drag,
                           GLCanvas3D                 &canvas,
                           RaycastManager             &raycast_manager,
                           const std::optional<double>&up_limit = {});

std::optional<Vec3d> calc_surface_offset(const Selection &selection, RaycastManager &raycast_manager);

std::optional<float> calc_distance(const GLVolume &gl_volume, RaycastManager &raycaster, GLCanvas3D &canvas);
std::optional<float> calc_distance(const GLVolume &gl_volume, const RaycastManager &raycaster, 
    const RaycastManager::ISkip *condition, const std::optional<Slic3r::Transform3d>& fix);

std::optional<float> calc_angle(const Selection &selection);

Transform3d world_matrix_fixed(const GLVolume &gl_volume, const ModelObjectPtrs& objects);

Transform3d world_matrix_fixed(const Selection &selection);

void selection_transform(Selection &selection, const std::function<void()>& selection_transformation_fnc);

bool face_selected_volume_to_camera(const Camera &camera, GLCanvas3D &canvas, const std::optional<double> &wanted_up_limit = {});

void do_local_z_rotate(Selection &selection, double relative_angle);

void do_local_z_move(Selection &selection, double relative_move);

TransformationType get_drag_transformation_type(const Selection &selection);

void dragging_rotate_gizmo(double gizmo_angle, std::optional<float>& current_angle, std::optional<float> &start_angle, Selection &selection);

}
#endif