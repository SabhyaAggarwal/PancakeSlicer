#pragma once

#include "libslic3r/Point.hpp"
#include "TriMesh.hpp"
#include <array>
#include <cstddef>
#include <vector>
#include <functional>

namespace Slic3r { namespace tex2color {

namespace color_utils {
struct ClusterParameters;

typedef std::array<std::size_t, 3> Color;
typedef std::vector<Color> ColorList;
typedef std::array<double, 3> ColorDouble;
typedef std::array<std::size_t, 3> RGB;

enum struct ColorDifferenceMethod : std::size_t {
    RGB = 0,
    Lab = 1
};

struct ClusterParameters {
    ColorDifferenceMethod color_difference_method = ColorDifferenceMethod::Lab;

    double max_color_distance = 25;

    std::size_t cluster_k = 10;

    std::size_t max_cluster_k = 32;

    std::size_t max_iter = 50;

    std::function<bool()> cancel_callback;
};

struct SmoothParameters {
    double smooth_weight = 0.5;
};

double calc_rgb_color_difference_by_squared_rgb(const RGB& rgb1, const RGB& rgb2);

double calc_rgb_color_difference_by_squared_rgb_double(const ColorDouble& c1, const ColorDouble& c2);

double calc_rgb_color_difference_by_ciede2000(const RGB& rgb1, const RGB& rgb2);

double calc_rgb_color_difference_by_ciede2000_srgb01(const ColorDouble& rgb1, const ColorDouble& rgb2);

std::vector<Color> cluster_k_means(const std::vector<Color>& colors, const ClusterParameters& cluster_parameters);

std::vector<Color> cluster_adaptive(const std::vector<Color>& colors, const ClusterParameters& cluster_parameters);

std::vector<Color> cluster_to_specified_colors(const std::vector<Color>& colors, const std::vector<Color>& specified_colors);

bool remesh_mesh(TriMesh& mesh, std::vector<std::size_t>& face_labels, double target_edge_length_ratio);

bool is_closed(const TriMesh& tri_mesh);

bool smooth_region(TriMesh& tri_mesh, std::vector<std::array<std::size_t, 3>>& face_labels, const SmoothParameters& smooth_parameters = SmoothParameters());

bool smooth_region(TriMesh& tri_mesh, std::vector<std::size_t>& face_labels, const SmoothParameters& smooth_parameters = SmoothParameters());

bool get_components(const TriMesh& mesh, const std::vector<Vec2f>& vertex_uvs, std::vector<TriMesh>& component_meshes,
                    std::vector<std::vector<Vec2f>>& component_vertex_uvs);

bool calc_nearest_color_id(const std::vector<RGB>& colors, const RGB& color, std::size_t& nearest_color_id);

bool mesh_cluster(const TriMesh& mesh, const std::vector<RGB>& cluster_centers, std::vector<RGB>& map_face_to_rgb,
                  std::vector<std::size_t>& map_face_to_cluster_id);

}

}
}
