#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct indexed_triangle_set;

namespace Slic3r {

class TriangleMesh;
class ModelVolume;

struct TextureImage {
    int width  = 0;
    int height = 0;
    int channels = 4;
    std::vector<unsigned char> data;
};

struct TexturedMesh {
    std::vector<std::array<float,3>>  vertices;
    std::vector<std::array<int,3>>    indices;
    std::vector<std::array<float,2>>  uvs;
    std::vector<TextureImage>         textures;
    std::vector<int>                  material_ids;

    std::vector<int>                  material_texture_map;

    std::vector<std::array<float,4>>  material_colors;



    std::vector<std::array<float,2>>  uv_coords;
    std::vector<std::array<int,3>>    uv_indices;

    bool has_face_uvs() const { return !uv_indices.empty() && !uv_coords.empty(); }





    std::vector<std::array<std::size_t,3>> precomputed_face_colors;





    std::vector<std::array<float,4>> precomputed_vertex_colors;
};

struct PaintedMesh {
    std::vector<std::array<float,3>>            vertices;
    std::vector<std::array<int,3>>              indices;
    std::vector<std::array<std::size_t,3>>      face_colors;
    std::vector<std::array<std::size_t,3>>      cluster_colors;
};

using PaintProgressCallback = std::function<void(int percent, const char* message)>;
using PaintCancelCallback   = std::function<bool()>;
using PaintMeshRepairCallback = std::function<bool(const indexed_triangle_set& mesh,
                                                   indexed_triangle_set&       repaired_mesh,
                                                   std::function<void(const char* message, unsigned progress)> progress_callback,
                                                   std::function<bool()> cancel_callback,
                                                   std::string* error_message)>;

struct TexturePaintingSettings {
    std::size_t target_colors_num  = 4;
    double      smooth_weight      = 0.5;
    std::size_t oversampling_iters = 0;
    enum class MeshRepairDecision {
        Ask,
        ImportWithoutRepair,
        RepairAndImport
    };
    MeshRepairDecision mesh_repair_decision = MeshRepairDecision::ImportWithoutRepair;
    bool* mesh_repair_decision_required = nullptr;
    PaintMeshRepairCallback mesh_repair_callback;
};

struct FilamentMatch {
    int    cluster_index   = -1;
    int    filament_index  = -1;
    double delta_e         = 0.0;
    std::array<std::size_t,3> cluster_color   = {0,0,0};
    std::array<float,4>       filament_color  = {0,0,0,1};
};

bool texture_to_painting(
    const TexturedMesh& textured,
    PaintedMesh& painted,
    const TexturePaintingSettings& settings = {},
    PaintProgressCallback progress = nullptr,
    PaintCancelCallback cancel = nullptr);



bool face_colors_to_painting(
    const TexturedMesh& mesh,
    PaintedMesh& painted,
    const TexturePaintingSettings& settings = {},
    PaintProgressCallback progress = nullptr,
    PaintCancelCallback cancel = nullptr);


std::vector<FilamentMatch> match_clusters_to_filaments(
    const std::vector<std::array<std::size_t,3>>& cluster_colors,
    const std::vector<std::array<float,4>>& filament_colors,
    const std::vector<std::string>& filament_names);

double compute_delta_e(
    const std::array<std::size_t,3>& rgb1,
    const std::array<float,4>& rgba2);

bool apply_painted_mesh_to_volume(
    const PaintedMesh& painted,
    const std::vector<FilamentMatch>& matches,
    ModelVolume& volume);



bool decode_texture_to_pixels(
    const TextureImage& img,
    std::vector<unsigned char>& out_pixels,
    int& out_w, int& out_h);




bool sample_original_face_colors(
    const TexturedMesh& textured,
    std::vector<std::array<std::size_t,3>>& out_face_colors);

}
