#pragma once

#include "Callbacks.hpp"
#include "libslic3r/Point.hpp"
#include "TriMesh.hpp"
#include "opencv2/core.hpp"
#include <cstddef>
#include <array>
#include <functional>
#include <opencv2/core/mat.hpp>
#include <string>
#include <vector>

namespace cv { class Mat; }

namespace Slic3r { namespace tex2color {

enum class MeshRepairDecision {
    Ask,
    ImportWithoutRepair,
    RepairAndImport
};

using MeshRepairCallback = std::function<bool(const indexed_triangle_set& mesh,
                                              indexed_triangle_set&       repaired_mesh,
                                              std::function<void(const char* message, unsigned progress)> progress_callback,
                                              std::function<bool()> cancel_callback,
                                              std::string* error_message)>;

struct TextureToColorSettings {
    std::size_t target_colors_num = 4;

    double smooth_weight = 0.5;
    std::size_t oversampling_iters = 0;
    std::size_t oversampling_min_face_count = 10000;
    std::size_t oversampling_max_face_count = 1000000;

    double max_color_distance = 25.0;
    std::size_t max_cluster_k = 32;

    MeshRepairDecision mesh_repair_decision = MeshRepairDecision::ImportWithoutRepair;

    bool* mesh_repair_decision_required = nullptr;

    MeshRepairCallback mesh_repair_callback;
};

bool TextureToColor(const TriMesh& texture_mesh, const std::vector<std::vector<Vec2f>>& uv_coords, const cv::Mat& texture, TriMesh& color_mesh,
                    std::vector<std::array<std::size_t, 3>>& face_colors, const TextureToColorSettings& settings = TextureToColorSettings(),
                    AlgoProgressCallback progress_callback = nullptr, AlgoCancelCallback cancel_callback = nullptr);

bool ClusterAndSmooth(const TriMesh& mesh,
                      const std::vector<std::array<std::size_t, 3>>& input_face_colors,
                      TriMesh& out_mesh,
                      std::vector<std::array<std::size_t, 3>>& out_face_colors,
                      const TextureToColorSettings& settings = TextureToColorSettings(),
                      AlgoProgressCallback progress_callback = nullptr,
                      AlgoCancelCallback cancel_callback = nullptr,
                      const std::vector<std::array<float, 4>>& vertex_colors = {});

}
}
