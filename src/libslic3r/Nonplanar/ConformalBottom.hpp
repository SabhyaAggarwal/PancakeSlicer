#pragma once
#include "HeightField.hpp"

#include <functional>

namespace Slic3r { class PrintObject; }
namespace Slic3r::sla { class IndexedMesh; }

namespace Slic3r::Nonplanar {

struct ConformalPatch;
struct ConformalResult;

HeightField sample_bottom_surface(const sla::IndexedMesh &mesh, const HeightField &ray_start, double z_offset,
                                  const std::function<void()> &throw_if_canceled = {});

void build_conformal_bottom(PrintObject &object, ConformalResult &result);

void contour_support_interface(PrintObject &object);

int patch_filament_id(const PrintObject &object, const ConformalPatch &patch);

bool patch_has_extrusions(const ConformalPatch &patch);

}
