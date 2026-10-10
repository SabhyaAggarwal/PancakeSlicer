#pragma once

#include "Point.hpp"

#include <cstddef>
#include <vector>

namespace Slic3r {

class ModelObject;

struct LayOnFacePlane
{
    Vec3d       normal;
    Vec3d       center;
    float       area;
    Pointf3s    outline;
    Transform3d to_plane_frame;
};

std::vector<LayOnFacePlane> lay_on_face_planes(const ModelObject &object, const Transform3d &instance_matrix_no_offset);

int find_largest_plane(const std::vector<LayOnFacePlane> &planes);

int find_plane_by_normal(const std::vector<LayOnFacePlane> &planes, const Vec3d &direction);

int find_plane_at_point(const std::vector<LayOnFacePlane> &planes, const Transform3d &instance_matrix_no_offset,
                        const Vec3d &point, double tolerance);

void lay_on_face(ModelObject &object, size_t instance_idx, const Vec3d &normal);

}
