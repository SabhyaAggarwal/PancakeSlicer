#ifndef slic3r_NormalUtils_hpp_
#define slic3r_NormalUtils_hpp_

#include "Point.hpp"
#include "Model.hpp"
#include <vector>

namespace Slic3r {

class NormalUtils
{
public:
    using Normal = Vec3f;
    using Normals = std::vector<Normal>;
    NormalUtils() = delete;

    enum class VertexNormalType {
        AverageNeighbor,
        AngleWeighted,
        NelsonMaxWeighted
    };

    static Normal create_triangle_normal(
        const stl_triangle_vertex_indices &indices,
        const std::vector<stl_vertex> &    vertices);

    static Normals create_triangle_normals(const indexed_triangle_set &its);

    static Normals create_normals(
        const indexed_triangle_set &its,
        VertexNormalType type = VertexNormalType::NelsonMaxWeighted);
    static Normals create_normals_average_neighbor(const indexed_triangle_set &its);
    static Normals create_normals_angle_weighted(const indexed_triangle_set &its);
    static Normals create_normals_nelson_weighted(const indexed_triangle_set &its);

    static float indice_angle(int                            i,
                              const Vec3i32 &                indice,
                              const std::vector<stl_vertex> &vertices);
};

}
#endif
