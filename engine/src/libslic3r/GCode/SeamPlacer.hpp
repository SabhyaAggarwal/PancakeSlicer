#ifndef libslic3r_SeamPlacer_hpp_
#define libslic3r_SeamPlacer_hpp_

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <memory>
#include <atomic>
#include <string>

#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/KDTreeIndirect.hpp"

namespace Slic3r {

class PrintObject;
namespace PreciseSeam { struct PreciseSeamWarnings; }
class ExtrusionLoop;
class Print;
class Layer;

namespace EdgeGrid {
class Grid;
}

namespace SeamPlacerImpl {


struct GlobalModelInfo;
struct SeamComparator;

enum class EnforcedBlockedSeamPoint {
  Blocked = 0,
  Neutral = 1,
  Enforced = 2,
};

struct Perimeter {
  size_t start_index{};
  size_t end_index{};
  size_t seam_index{};
  float flow_width{};

  bool finalized = false;
  Vec3f final_seam_position = Vec3f::Zero();

  std::optional<Vec3f> precise_seam_point;
  size_t precise_seam_index{};
};

struct SeamCandidate {
  SeamCandidate(const Vec3f &pos, Perimeter &perimeter,
                float local_ccw_angle,
                EnforcedBlockedSeamPoint type) :
                                                 position(pos), perimeter(perimeter), visibility(0.0f), overhang(0.0f), embedded_distance(0.0f), local_ccw_angle(
                                                                                                                                                     local_ccw_angle), type(type), central_enforcer(false) {
  }
  const Vec3f position;
  Perimeter &perimeter;
  float visibility;
  float overhang;
  float unsupported_dist = 0.f;
  float embedded_distance;
  float local_ccw_angle;
  EnforcedBlockedSeamPoint type;
  bool central_enforcer;
};

struct SeamCandidateCoordinateFunctor {
  SeamCandidateCoordinateFunctor(const std::vector<SeamCandidate> &seam_candidates) :
                                                                                      seam_candidates(seam_candidates) {
  }
  const std::vector<SeamCandidate> &seam_candidates;
  float operator()(size_t index, size_t dim) const {
    return seam_candidates[index].position[dim];
  }
};
}

struct PrintObjectSeamData
{
  using SeamCandidatesTree = KDTreeIndirect<3, float, SeamPlacerImpl::SeamCandidateCoordinateFunctor>;

  struct LayerSeams
  {
    Slic3r::deque<SeamPlacerImpl::Perimeter> perimeters;
    std::vector<SeamPlacerImpl::SeamCandidate> points;
    std::unique_ptr<SeamCandidatesTree> points_tree;
  };
  std::vector<LayerSeams> layers;
  bool has_precise_seam_strong_volumes = false;

  void clear()
  {
    layers.clear();
  }
};

class SeamPlacer {
public:
  static constexpr size_t raycasting_visibility_samples_count = 30000;
  static constexpr size_t fast_decimation_triangle_count_target = 16000;
  static constexpr size_t sqr_rays_per_sample_point = 5;

  static constexpr float sharp_angle_snapping_threshold = 55.0f * float(PI) / 180.0f;
  static constexpr float overhang_angle_threshold = 45.0f * float(PI) / 180.0f;

  static constexpr float angle_importance_aligned = 0.6f;
  static constexpr float angle_importance_nearest = 1.0f;
  static constexpr float enforcer_oversampling_distance = 0.2f;

  static constexpr float seam_align_score_tolerance = 0.3f;
  static constexpr float seam_align_tolerable_dist_factor = 4.0f;
  static constexpr size_t seam_align_minimum_string_seams = 6;
  static constexpr size_t seam_align_mm_per_segment = 4.0f;

  std::unordered_map<const PrintObject*, PrintObjectSeamData> m_seam_per_object;

  void init(const Print &print, std::function<void(void)> throw_if_canceled_func);

  const std::string &precise_seam_warning() const { return m_precise_seam_warning; }

  void place_seam(const Layer *layer, ExtrusionLoop &loop, const Point &last_pos, float& overhang) const;
private:
  std::string m_precise_seam_warning;

  void gather_seam_candidates(const PrintObject *po, const SeamPlacerImpl::GlobalModelInfo &global_model_info,
                              PreciseSeam::PreciseSeamWarnings* warnings = nullptr);
  void calculate_candidates_visibility(const PrintObject *po,
                                       const SeamPlacerImpl::GlobalModelInfo &global_model_info);
  void calculate_overhangs_and_layer_embedding(const PrintObject *po);
  void align_seam_points(const PrintObject *po, const SeamPlacerImpl::SeamComparator &comparator);
  std::vector<std::pair<size_t, size_t>> find_seam_string(const PrintObject *po,
                                                          std::pair<size_t, size_t> start_seam,
                                                          const SeamPlacerImpl::SeamComparator &comparator) const;
  std::optional<std::pair<size_t, size_t>> find_next_seam_in_layer(
      const std::vector<PrintObjectSeamData::LayerSeams> &layers,
      const Vec3f& projected_position,
      const size_t layer_idx, const float max_distance,
      const SeamPlacerImpl::SeamComparator &comparator) const;
};

}

#endif
