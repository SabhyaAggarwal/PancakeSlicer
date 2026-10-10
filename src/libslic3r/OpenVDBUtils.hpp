#ifndef OPENVDBUTILS_HPP
#define OPENVDBUTILS_HPP

#include "Point.hpp"
#include <libslic3r/TriangleMesh.hpp>
#include <openvdb/Types.h>
#include <openvdb/math/Transform.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <openvdb/openvdb.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Slic3r {

inline Vec3f to_vec3f(const openvdb::Vec3s &v) { return Vec3f{v.x(), v.y(), v.z()}; }
inline Vec3d to_vec3d(const openvdb::Vec3s &v) { return to_vec3f(v).cast<double>(); }
inline Vec3i32 to_vec3i(const openvdb::Vec3I &v) { return Vec3i32{int(v[0]), int(v[1]), int(v[2])}; }

openvdb::FloatGrid::Ptr mesh_to_grid(const indexed_triangle_set &    mesh,
                                     const openvdb::math::Transform &tr = {},
                                     float voxel_scale                  = 1.f,
                                     float exteriorBandWidth = 3.0f,
                                     float interiorBandWidth = 3.0f,
                                     int   flags             = 0);

indexed_triangle_set grid_to_mesh(const openvdb::FloatGrid &grid,
                                  double                    isovalue   = 0.0,
                                  double                    adaptivity = 0.0,
                                  bool relaxDisorientedTriangles = true);

openvdb::FloatGrid::Ptr redistance_grid(const openvdb::FloatGrid &grid,
                                        double                    iso,
                                        double ext_range = 3.,
                                        double int_range = 3.);

}

#endif
