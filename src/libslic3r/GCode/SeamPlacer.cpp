#include "SeamPlacer.hpp"

#include "libslic3r/Point.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "Polygon.hpp"
#include "libslic3r/PrintBase.hpp"
#include "PrintConfig.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
#include "tbb/parallel_reduce.h"
#include <atomic>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <igl/Hit.h>
#include <optional>
#include <cassert>
#include <functional>
#include <numeric>
#include <memory>
#include <random>
#include <algorithm>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Nonplanar/Multiplanar.hpp"

#include "libslic3r/Geometry/Curves.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/ShortEdgeCollapse.hpp"
#include "libslic3r/TriangleSetSampling.hpp"

#include "libslic3r/Utils.hpp"
#include "PreciseSeam.hpp"

#ifdef DEBUG_FILES
#include <boost/nowide/cstdio.hpp>
#include <SVG.hpp>
#endif

namespace Slic3r {

namespace SeamPlacerImpl {

template<typename T> int sgn(T val) {
  return int(T(0) < val) - int(val < T(0));
}

float gauss(float value, float mean_x_coord, float mean_value, float falloff_speed) {
  float shifted = value - mean_x_coord;
  float denominator = falloff_speed * shifted * shifted + 1.0f;
  float exponent = 1.0f / denominator;
  return mean_value * (std::exp(exponent) - 1.0f) / (std::exp(1.0f) - 1.0f);
}

float compute_angle_penalty(float ccw_angle) {
  return gauss(ccw_angle, 0.0f, 1.0f, 3.0f) +
         1.0f / (2 + std::exp(-ccw_angle));
}

class Frame {
public:
  Frame() {
    mX = Vec3f(1, 0, 0);
    mY = Vec3f(0, 1, 0);
    mZ = Vec3f(0, 0, 1);
  }

  Frame(const Vec3f &x, const Vec3f &y, const Vec3f &z) :
                                                          mX(x), mY(y), mZ(z) {
  }

  void set_from_z(const Vec3f &z) {
    mZ = z.normalized();
    Vec3f tmpZ = mZ;
    Vec3f tmpX = (std::abs(tmpZ.x()) > 0.99f) ? Vec3f(0, 1, 0) : Vec3f(1, 0, 0);
    mY = (tmpZ.cross(tmpX)).normalized();
    mX = mY.cross(tmpZ);
  }

  Vec3f to_world(const Vec3f &a) const {
    return a.x() * mX + a.y() * mY + a.z() * mZ;
  }

  Vec3f to_local(const Vec3f &a) const {
    return Vec3f(mX.dot(a), mY.dot(a), mZ.dot(a));
  }

  const Vec3f& binormal() const {
    return mX;
  }

  const Vec3f& tangent() const {
    return mY;
  }

  const Vec3f& normal() const {
    return mZ;
  }

private:
  Vec3f mX, mY, mZ;
};

Vec3f sample_sphere_uniform(const Vec2f &samples) {
  float term1 = 2.0f * float(PI) * samples.x();
  float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
  return {cos(term1) * term2, sin(term1) * term2,
          1.0f - 2.0f * samples.y()};
}

Vec3f sample_hemisphere_uniform(const Vec2f &samples) {
  float term1 = 2.0f * float(PI) * samples.x();
  float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
  return {cos(term1) * term2, sin(term1) * term2,
          abs(1.0f - 2.0f * samples.y())};
}

Vec3f sample_power_cosine_hemisphere(const Vec2f &samples, float power) {
  float term1 = 2.f * float(PI) * samples.x();
  float term2 = pow(samples.y(), 1.f / (power + 1.f));
  float term3 = sqrt(1.f - term2 * term2);

  return Vec3f(cos(term1) * term3, sin(term1) * term3, term2);
}

std::vector<float> raycast_visibility(const AABBTreeIndirect::Tree<3, float> &raycasting_tree,
                                      const indexed_triangle_set &triangles,
                                      const TriangleSetSamples &samples,
                                      size_t negative_volumes_start_index,
                                      SeamPosition seam_position = spAligned) {
  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: raycast visibility of " << samples.positions.size() << " samples over " << triangles.indices.size()
      << " triangles: end";

  float step_size = 1.0f / SeamPlacer::sqr_rays_per_sample_point;
  std::vector<Vec3f> precomputed_sample_directions(
      SeamPlacer::sqr_rays_per_sample_point * SeamPlacer::sqr_rays_per_sample_point);
  for (size_t x_idx = 0; x_idx < SeamPlacer::sqr_rays_per_sample_point; ++x_idx) {
    float sample_x = x_idx * step_size + step_size / 2.0;
    for (size_t y_idx = 0; y_idx < SeamPlacer::sqr_rays_per_sample_point; ++y_idx) {
      size_t dir_index = x_idx * SeamPlacer::sqr_rays_per_sample_point + y_idx;
      float sample_y = y_idx * step_size + step_size / 2.0;
      precomputed_sample_directions[dir_index] = sample_hemisphere_uniform( { sample_x, sample_y });
    }
  }

  bool model_contains_negative_parts = negative_volumes_start_index < triangles.indices.size();

  std::vector<float> result(samples.positions.size());
  tbb::parallel_for(tbb::blocked_range<size_t>(0, result.size()),
                    [&triangles, &precomputed_sample_directions, model_contains_negative_parts, negative_volumes_start_index,
                     &raycasting_tree, &result, &samples, seam_position](tbb::blocked_range<size_t> r) {
                      std::vector<igl::Hit<float>> hits;
                      for (size_t s_idx = r.begin(); s_idx < r.end(); ++s_idx) {
                        result[s_idx] = 1.0f;
                        constexpr float decrease_step = 1.0f
                                                        / (SeamPlacer::sqr_rays_per_sample_point * SeamPlacer::sqr_rays_per_sample_point);

                        const Vec3f &center = samples.positions[s_idx];
                        const Vec3f &normal = samples.normals[s_idx];
                        if (seam_position == spAlignedBack) {
                            const float front_adjustment = std::clamp((normal.dot(Vec3f(0.0f, -1.0f, 0.0f)) + 1.2f) * 0.5f, 0.0f, 1.0f);
                            result[s_idx] += front_adjustment;
                        }

                        Frame f;
                        f.set_from_z(normal);

                        for (const auto &dir : precomputed_sample_directions) {
                          Vec3f final_ray_dir = (f.to_world(dir));
                          if (!model_contains_negative_parts) {
                            igl::Hit<float> hitpoint;
                            Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                            Vec3d ray_origin_d = (center + normal * 0.01f).cast<double>();
                            bool hit = AABBTreeIndirect::intersect_ray_first_hit(triangles.vertices,
                                                                                 triangles.indices, raycasting_tree, ray_origin_d, final_ray_dir_d, hitpoint);
                            if (hit && its_face_normal(triangles, hitpoint.id).dot(final_ray_dir) <= 0) {
                              result[s_idx] -= decrease_step;
                            }
                          } else {
                            bool casting_from_negative_volume = samples.triangle_indices[s_idx]
                                                                >= negative_volumes_start_index;

                            Vec3d ray_origin_d = (center + normal * 0.01f).cast<double>();
                            if (casting_from_negative_volume) {
                              final_ray_dir = -1.0 * final_ray_dir;
                              ray_origin_d = (center - normal * 0.01f).cast<double>();
                            }
                            Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                            bool some_hit = AABBTreeIndirect::intersect_ray_all_hits(triangles.vertices,
                                                                                     triangles.indices, raycasting_tree,
                                                                                     ray_origin_d, final_ray_dir_d, hits);
                            if (some_hit) {
                              int counter = 0;
                              for (int hit_index = int(hits.size()) - 1; hit_index >= 0; --hit_index) {
                                Vec3f face_normal = its_face_normal(triangles, hits[hit_index].id);
                                if (hits[hit_index].id >= int(negative_volumes_start_index)) {
                                  counter -= sgn(face_normal.dot(final_ray_dir));
                                } else {
                                  counter += sgn(face_normal.dot(final_ray_dir));
                                }
                              }
                              if (counter == 0) {
                                result[s_idx] -= decrease_step;
                              }
                            }
                          }
                        }
                      }
                    });

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: raycast visibility of " << samples.positions.size() << " samples over " << triangles.indices.size()
      << " triangles: end";

  return result;
}

std::vector<float> calculate_polygon_angles_at_vertices(const Polygon &polygon, const std::vector<float> &lengths,
                                                        float min_arm_length) {
  std::vector<float> result(polygon.size());

  if (polygon.size() == 1) {
    result[0] = 0.0f;
  }

  size_t idx_prev = 0;
  size_t idx_curr = 0;
  size_t idx_next = 0;

  float distance_to_prev = 0;
  float distance_to_next = 0;

  while (distance_to_prev < min_arm_length) {
    idx_prev = Slic3r::prev_idx_modulo(idx_prev, polygon.size());
    distance_to_prev += lengths[idx_prev];
  }

  for (size_t _i = 0; _i < polygon.size(); ++_i) {
    while (distance_to_prev - lengths[idx_prev] > min_arm_length) {
      distance_to_prev -= lengths[idx_prev];
      idx_prev = Slic3r::next_idx_modulo(idx_prev, polygon.size());
    }

    while (distance_to_next < min_arm_length) {
      distance_to_next += lengths[idx_next];
      idx_next = Slic3r::next_idx_modulo(idx_next, polygon.size());
    }

    const Point &p0 = polygon.points[idx_prev];
    const Point &p1 = polygon.points[idx_curr];
    const Point &p2 = polygon.points[idx_next];
    result[idx_curr] = float(angle(p1 - p0, p2 - p1));

    float curr_distance = lengths[idx_curr];
    idx_curr++;
    distance_to_prev += curr_distance;
    distance_to_next -= curr_distance;
  }

  return result;
}

struct CoordinateFunctor {
  const std::vector<Vec3f> *coordinates;
  CoordinateFunctor(const std::vector<Vec3f> *coords) :
                                                        coordinates(coords) {
  }
  CoordinateFunctor() :
                        coordinates(nullptr) {
  }

  const float& operator()(size_t idx, size_t dim) const {
    return coordinates->operator [](idx)[dim];
  }
};

struct GlobalModelInfo {
  TriangleSetSamples mesh_samples;
  std::vector<float> mesh_samples_visibility;
  CoordinateFunctor mesh_samples_coordinate_functor;
  KDTreeIndirect<3, float, CoordinateFunctor> mesh_samples_tree { CoordinateFunctor { } };
  float mesh_samples_radius;

  indexed_triangle_set enforcers;
  indexed_triangle_set blockers;
  AABBTreeIndirect::Tree<3, float> enforcers_tree;
  AABBTreeIndirect::Tree<3, float> blockers_tree;

  std::vector<const ModelVolume*> precise_seam_strong_volumes;
  std::vector<const ModelVolume*> precise_seam_weak_volumes;

  PreciseSeam::ModifierRegionsCache precise_seam_slices;

  bool is_enforced(const Vec3f &position, float radius) const {
    if (enforcers.empty()) {
      return false;
    }
    float radius_sqr = radius * radius;
    return AABBTreeIndirect::is_any_triangle_in_radius(enforcers.vertices, enforcers.indices,
                                                       enforcers_tree, position, radius_sqr);
  }

  bool is_blocked(const Vec3f &position, float radius) const {
    if (blockers.empty()) {
      return false;
    }
    float radius_sqr = radius * radius;
    return AABBTreeIndirect::is_any_triangle_in_radius(blockers.vertices, blockers.indices,
                                                       blockers_tree, position, radius_sqr);
  }

  float calculate_point_visibility(const Vec3f &position) const {
    std::vector<size_t> points = find_nearby_points(mesh_samples_tree, position, mesh_samples_radius);
    if (points.empty()) {
      return 1.0f;
    }

    auto compute_dist_to_plane = [](const Vec3f &position, const Vec3f &plane_origin, const Vec3f &plane_normal) {
      Vec3f orig_to_point = position - plane_origin;
      return std::abs(orig_to_point.dot(plane_normal));
    };

    float total_weight = 0;
    float total_visibility = 0;
    for (size_t i = 0; i < points.size(); ++i) {
      size_t sample_idx = points[i];

      Vec3f sample_point = this->mesh_samples.positions[sample_idx];
      Vec3f sample_normal = this->mesh_samples.normals[sample_idx];

      float weight = mesh_samples_radius - compute_dist_to_plane(position, sample_point, sample_normal);
      weight += (mesh_samples_radius - (position - sample_point).norm());
      total_visibility += weight * mesh_samples_visibility[sample_idx];
      total_weight += weight;
    }

    return total_visibility / total_weight;

  }

#ifdef DEBUG_FILES
  void debug_export(const indexed_triangle_set &obj_mesh) const {

    indexed_triangle_set divided_mesh = obj_mesh;
    Slic3r::CNumericLocalesSetter locales_setter;

    {
      auto filename = debug_out_path("visiblity.obj");
      FILE *fp = boost::nowide::fopen(filename.c_str(), "w");
      if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error)
            << "stl_write_obj: Couldn't open " << filename << " for writing";
        return;
      }

      for (size_t i = 0; i < divided_mesh.vertices.size(); ++i) {
        float visibility = calculate_point_visibility(divided_mesh.vertices[i]);
        Vec3f color = value_to_rgbf(0.0f, 1.0f, visibility);
        fprintf(fp, "v %f %f %f  %f %f %f\n",
                divided_mesh.vertices[i](0), divided_mesh.vertices[i](1), divided_mesh.vertices[i](2),
                color(0), color(1), color(2));
      }
      for (size_t i = 0; i < divided_mesh.indices.size(); ++i)
        fprintf(fp, "f %d %d %d\n", divided_mesh.indices[i][0] + 1, divided_mesh.indices[i][1] + 1,
                divided_mesh.indices[i][2] + 1);
      fclose(fp);
    }

    {
      auto filename = debug_out_path("visiblity_samples.obj");
      FILE *fp = boost::nowide::fopen(filename.c_str(), "w");
      if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error)
            << "stl_write_obj: Couldn't open " << filename << " for writing";
        return;
      }

      for (size_t i = 0; i < mesh_samples.positions.size(); ++i) {
        float visibility = mesh_samples_visibility[i];
        Vec3f color = value_to_rgbf(0.0f, 1.0f, visibility);
        fprintf(fp, "v %f %f %f  %f %f %f\n",
                mesh_samples.positions[i](0), mesh_samples.positions[i](1), mesh_samples.positions[i](2),
                color(0), color(1), color(2));
      }
      fclose(fp);
    }

  }
#endif
}
;

Polygons extract_perimeter_polygons(const Layer *layer, std::vector<const LayerRegion*> &corresponding_regions_out,
                                   bool has_precise_seam_modifiers) {
  const auto projected = [](const ExtrusionEntity &entity) {
    if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity))
      return path->nonplanar_source;
    const ExtrusionPaths *paths = nullptr;
    if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity))
      paths = &loop->paths;
    else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity))
      paths = &multi->paths;
    return paths != nullptr && std::any_of(paths->begin(), paths->end(), [](const ExtrusionPath &path) { return path.nonplanar_source; });
  };
  Polygons polygons;
  for (const LayerRegion *layer_region : layer->regions()) {
    for (const ExtrusionEntity *ex_entity : layer_region->perimeters.entities) {
      if (ex_entity->is_collection()) {
        for (const ExtrusionEntity *perimeter : static_cast<const ExtrusionEntityCollection*>(ex_entity)->entities) {
          ExtrusionRole role = perimeter->role();
          if (perimeter->is_loop()) {
            for (const ExtrusionPath &path : static_cast<const ExtrusionLoop*>(perimeter)->paths) {
              if (path.role() == ExtrusionRole::erExternalPerimeter) {
                role = ExtrusionRole::erExternalPerimeter;
              }
            }
          }

          if (projected(*perimeter))
            continue;
          if (role == ExtrusionRole::erExternalPerimeter) {
            Points p;
            perimeter->collect_points(p);
            polygons.emplace_back(std::move(p));
            corresponding_regions_out.push_back(layer_region);
          }
        }
        if (polygons.empty()) {
          Points p;
          for (const ExtrusionEntity *perimeter : static_cast<const ExtrusionEntityCollection *>(ex_entity)->entities)
            if (!projected(*perimeter))
              perimeter->collect_points(p);
          if (p.empty())
            continue;
          polygons.emplace_back(std::move(p));
          corresponding_regions_out.push_back(layer_region);
        }
      } else {
        if (projected(*ex_entity))
          continue;
        Points p;
        ex_entity->collect_points(p);
        polygons.emplace_back(std::move(p));
        corresponding_regions_out.push_back(layer_region);
      }
    }
  }

  if (has_precise_seam_modifiers) {
    for (Polygon &polygon : polygons) {
      polygon.points.erase(std::unique(polygon.points.begin(), polygon.points.end()), polygon.points.end());
      while (polygon.size() > 1 && polygon.points.front() == polygon.points.back())
        polygon.points.pop_back();
    }
  }

  if (polygons.empty()) {
    polygons.emplace_back(Points{ { 0, 0 } });
    corresponding_regions_out.push_back(nullptr);
  }

  return polygons;
}

void process_perimeter_polygon(const Polygon &orig_polygon, float z_coord, const LayerRegion *region,
                               const GlobalModelInfo &global_model_info, PrintObjectSeamData::LayerSeams &result,
                               PreciseSeam::PreciseSeamWarnings* warnings = nullptr) {
  if (orig_polygon.size() == 0) {
    return;
  }
  Polygon polygon = orig_polygon;
  bool was_clockwise = polygon.make_counter_clockwise();

  const Layer* layer = region ? region->layer() : nullptr;

  const auto& strong_volumes = global_model_info.precise_seam_strong_volumes;
  const auto& weak_volumes = global_model_info.precise_seam_weak_volumes;

  std::optional<Point> seam_point;
  std::vector<PreciseSeam::WeakModifierSegment> weak_segments;
  if (layer != nullptr && (!strong_volumes.empty() || !weak_volumes.empty())) {
    const PreciseSeam::PreparedPerimeter prepared(polygon);
    seam_point = PreciseSeam::insert_strong_seam_point(
        strong_volumes, polygon, prepared, layer, global_model_info.precise_seam_slices, warnings);
    if (!seam_point.has_value())
      weak_segments = PreciseSeam::collect_weak_modifier_segments(
          weak_volumes, polygon, prepared, layer, global_model_info.precise_seam_slices, warnings);
  }

  std::optional<Vec3f> inserted_seam_position;
  if (seam_point.has_value()) {
    Vec2f unscaled_p = unscale(seam_point.value()).cast<float>();
    inserted_seam_position = Vec3f(unscaled_p.x(), unscaled_p.y(), z_coord);
  }

  float angle_arm_len = region != nullptr ? region->flow(FlowRole::frExternalPerimeter).nozzle_diameter() : 0.5f;

  std::vector<float> lengths { };
  for (size_t point_idx = 0; point_idx < polygon.size() - 1; ++point_idx) {
    lengths.push_back((unscale(polygon[point_idx]) - unscale(polygon[point_idx + 1])).norm());
  }
  lengths.push_back(std::max((unscale(polygon[0]) - unscale(polygon[polygon.size() - 1])).norm(), 0.1));
  std::vector<float> polygon_angles = calculate_polygon_angles_at_vertices(polygon, lengths,
                                                                           angle_arm_len);

  result.perimeters.push_back( { });
  Perimeter &perimeter = result.perimeters.back();

  std::queue<Vec3f> orig_polygon_points { };
  for (size_t index = 0; index < polygon.size(); ++index) {
    Vec2f unscaled_p = unscale(polygon[index]).cast<float>();
    orig_polygon_points.emplace(unscaled_p.x(), unscaled_p.y(), z_coord);
  }
  Vec3f first = orig_polygon_points.front();
  std::queue<Vec3f> oversampled_points { };
  size_t orig_angle_index = 0;
  perimeter.start_index = result.points.size();
  perimeter.flow_width = region != nullptr ? region->flow(FlowRole::frExternalPerimeter).width() : 0.0f;
  bool some_point_enforced = false;
  while (!orig_polygon_points.empty() || !oversampled_points.empty()) {
    EnforcedBlockedSeamPoint type = EnforcedBlockedSeamPoint::Neutral;
    Vec3f position;
    float local_ccw_angle = 0;
    bool orig_point = false;
    if (!oversampled_points.empty()) {
      position = oversampled_points.front();
      oversampled_points.pop();
    } else {
      position = orig_polygon_points.front();
      orig_polygon_points.pop();
      local_ccw_angle = was_clockwise ? -polygon_angles[orig_angle_index] : polygon_angles[orig_angle_index];
      orig_angle_index++;
      orig_point = true;
    }

    if (global_model_info.is_enforced(position, perimeter.flow_width)) {
      type = EnforcedBlockedSeamPoint::Enforced;
    }

    if (global_model_info.is_blocked(position, perimeter.flow_width)) {
      type = EnforcedBlockedSeamPoint::Blocked;
    }
    some_point_enforced = some_point_enforced || type == EnforcedBlockedSeamPoint::Enforced;

    if (orig_point) {
      Vec3f pos_of_next = orig_polygon_points.empty() ? first : orig_polygon_points.front();
      float distance_to_next = (position - pos_of_next).norm();
      if (global_model_info.is_enforced(position, distance_to_next)) {
        Vec3f vec_to_next = (pos_of_next - position).normalized();
        float step_size = SeamPlacer::enforcer_oversampling_distance;
        float step = step_size;
        while (step < distance_to_next) {
          oversampled_points.push(position + vec_to_next * step);
          step += step_size;
        }
      }
    }

    result.points.emplace_back(position, perimeter, local_ccw_angle, type);
  }

  perimeter.end_index = result.points.size();

  if (!inserted_seam_position.has_value() && !weak_segments.empty()) {
    PreciseSeam::apply_weak_modifiers_to_perimeter(
        weak_segments, result, perimeter, some_point_enforced);
  }

  if (some_point_enforced) {
    size_t perimeter_size = perimeter.end_index - perimeter.start_index;
    const auto next_index = [&](size_t idx) {
      return perimeter.start_index + Slic3r::next_idx_modulo(idx - perimeter.start_index, perimeter_size);
    };

    std::vector<size_t> patches_starts_ends;
    for (size_t i = perimeter.start_index; i < perimeter.end_index; ++i) {
      if (result.points[i].type != EnforcedBlockedSeamPoint::Enforced &&
          result.points[next_index(i)].type == EnforcedBlockedSeamPoint::Enforced) {
        patches_starts_ends.push_back(next_index(i));
      }
      if (result.points[i].type == EnforcedBlockedSeamPoint::Enforced &&
          result.points[next_index(i)].type != EnforcedBlockedSeamPoint::Enforced) {
        patches_starts_ends.push_back(next_index(i));
      }
    }
    if (!patches_starts_ends.empty()) {
      assert(patches_starts_ends.size() % 2 == 0);
      bool start_on_second = false;
      if (result.points[patches_starts_ends[0]].type != EnforcedBlockedSeamPoint::Enforced) {
        start_on_second = true;
        patches_starts_ends.push_back(patches_starts_ends[0]);
      }
      std::pair<size_t, size_t> longest_patch { 0, 0 };
      auto patch_len = [perimeter_size](const std::pair<size_t, size_t> &start_end) {
        if (start_end.second < start_end.first) {
          return perimeter_size - (start_end.first - start_end.second);
        } else {
          return start_end.second - start_end.first;
        }
      };
      for (size_t patch_idx = start_on_second ? 1 : 0; patch_idx < patches_starts_ends.size(); patch_idx += 2) {
        std::pair<size_t, size_t> current_patch { patches_starts_ends[patch_idx], patches_starts_ends[patch_idx
                                                                                                    + 1] };
        if (patch_len(longest_patch) < patch_len(current_patch)) {
          longest_patch = current_patch;
        }
      }
      std::vector<size_t> viable_points_indices;
      std::vector<size_t> large_angle_points_indices;
      for (size_t point_idx = longest_patch.first; point_idx != longest_patch.second;
           point_idx = next_index(point_idx)) {
        viable_points_indices.push_back(point_idx);
        if (std::abs(result.points[point_idx].local_ccw_angle)
            > SeamPlacer::sharp_angle_snapping_threshold) {
          large_angle_points_indices.push_back(point_idx);
        }
      }
      assert(viable_points_indices.size() > 0);
      if (large_angle_points_indices.empty()) {
        size_t central_idx = viable_points_indices[viable_points_indices.size() / 2];
        result.points[central_idx].central_enforcer = true;
      } else {
        size_t central_idx = large_angle_points_indices.size() / 2;
        result.points[large_angle_points_indices[central_idx]].central_enforcer = true;
      }
    }
  }

  if (inserted_seam_position.has_value()) {
    for (size_t i = perimeter.start_index; i < perimeter.end_index; ++i) {
      if (result.points[i].position == inserted_seam_position.value()) {
        result.points[i].type = EnforcedBlockedSeamPoint::Enforced;
        result.points[i].central_enforcer = true;
        perimeter.precise_seam_point = inserted_seam_position;
        perimeter.precise_seam_index = i;
      } else {
        result.points[i].type = EnforcedBlockedSeamPoint::Blocked;
        result.points[i].central_enforcer = false;
      }
    }
  }

}

void compute_global_occlusion(GlobalModelInfo &result, const PrintObject *po,
                              std::function<void(void)> throw_if_canceled,
                              SeamPosition seam_position = spAligned) {
  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: gather occlusion meshes: start";
  auto obj_transform = po->trafo_sliced();
  indexed_triangle_set triangle_set;
  indexed_triangle_set negative_volumes_set;
  for (const ModelVolume *model_volume : po->model_object()->volumes) {
    if (model_volume->type() == ModelVolumeType::MODEL_PART
        || model_volume->type() == ModelVolumeType::NEGATIVE_VOLUME) {
      auto model_transformation = model_volume->get_matrix();
      indexed_triangle_set model_its = model_volume->mesh().its;
      its_transform(model_its, model_transformation, true);
      if (model_volume->type() == ModelVolumeType::MODEL_PART) {
        its_merge(triangle_set, model_its);
      } else {
        its_merge(negative_volumes_set, model_its);
      }
    }
  }
  throw_if_canceled();

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: gather occlusion meshes: end";

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: decimate: start";
  its_short_edge_collpase(triangle_set, SeamPlacer::fast_decimation_triangle_count_target);
  its_short_edge_collpase(negative_volumes_set, SeamPlacer::fast_decimation_triangle_count_target);

  size_t negative_volumes_start_index = triangle_set.indices.size();
  its_merge(triangle_set, negative_volumes_set);
  its_transform(triangle_set, obj_transform, true);
  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: decimate: end";

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: Compute visibility sample points: start";

  result.mesh_samples = sample_its_uniform_parallel(SeamPlacer::raycasting_visibility_samples_count,
                                                    triangle_set);
  result.mesh_samples_coordinate_functor = CoordinateFunctor(&result.mesh_samples.positions);
  result.mesh_samples_tree = KDTreeIndirect<3, float, CoordinateFunctor>(result.mesh_samples_coordinate_functor,
                                                                         result.mesh_samples.positions.size());

  float probability = 0.9f;
  float samples = 4;
  float density = SeamPlacer::raycasting_visibility_samples_count / result.mesh_samples.total_area;
  float search_area = samples / (-logf(probability) * density);
  float search_radius = sqrt(search_area / PI);
  result.mesh_samples_radius = search_radius;

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: Compute visiblity sample points: end";
  throw_if_canceled();

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: Mesh sample raidus: " << result.mesh_samples_radius;

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: build AABB tree: start";
  auto raycasting_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(triangle_set.vertices,
                                                                                     triangle_set.indices);

  throw_if_canceled();
  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: build AABB tree: end";
  result.mesh_samples_visibility = raycast_visibility(raycasting_tree, triangle_set, result.mesh_samples,
                                                      negative_volumes_start_index, seam_position);
  throw_if_canceled();
#ifdef DEBUG_FILES
  result.debug_export(triangle_set);
#endif
}

void gather_enforcers_blockers(GlobalModelInfo &result, const PrintObject *po) {
  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: build AABB trees for raycasting enforcers/blockers: start";

  auto obj_transform = po->trafo_sliced();

  for (const ModelVolume *mv : po->model_object()->volumes) {
    if (!mv->is_model_part() && !mv->is_negative_volume())
      continue;
    if (mv->is_seam_painted()) {
      auto model_transformation = obj_transform * mv->get_matrix();

      indexed_triangle_set enforcers = mv->seam_facets.get_facets(*mv, EnforcerBlockerType::ENFORCER);
      its_transform(enforcers, model_transformation, true);
      its_merge(result.enforcers, enforcers);

      indexed_triangle_set blockers = mv->seam_facets.get_facets(*mv, EnforcerBlockerType::BLOCKER);
      its_transform(blockers, model_transformation, true);
      its_merge(result.blockers, blockers);
    }
  }
  its_merge(result.enforcers, Nonplanar::route_cut_enforcers(*po));

  result.enforcers_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(result.enforcers.vertices,
                                                                                      result.enforcers.indices);
  result.blockers_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(result.blockers.vertices,
                                                                                     result.blockers.indices);

  BOOST_LOG_TRIVIAL(debug)
      << "SeamPlacer: build AABB trees for raycasting enforcers/blockers: end";
}

struct SeamComparator {
  SeamPosition setup;
  float angle_importance;
  explicit SeamComparator(SeamPosition setup) :
                                                setup(setup) {
    angle_importance =
        setup == spNearest ? SeamPlacer::angle_importance_nearest : SeamPlacer::angle_importance_aligned;
  }

  bool is_first_better(const SeamCandidate &a, const SeamCandidate &b, const Vec2f &preffered_location = Vec2f { 0.0f,
                                                                                                               0.0f }) const {
    if ((setup == SeamPosition::spAligned || setup == SeamPosition::spAlignedBack) && a.central_enforcer != b.central_enforcer) {
      return a.central_enforcer;
    }

    if (a.type != b.type) {
      return a.type > b.type;
    }

    if (a.overhang > 0.0f || b.overhang > 0.0f) {
      return a.overhang < b.overhang;
    }

    if (a.embedded_distance < -0.5f && b.embedded_distance > -0.5f) {
      return true;
    }
    if (b.embedded_distance < -0.5f && a.embedded_distance > -0.5f) {
      return false;
    }

    if (setup == SeamPosition::spRear && a.position.y() != b.position.y()) {
      return a.position.y() > b.position.y();
    }

    float distance_penalty_a = 0.0f;
    float distance_penalty_b = 0.0f;
    if (setup == spNearest) {
      distance_penalty_a = 1.0f - gauss((a.position.head<2>() - preffered_location).norm(), 0.0f, 1.0f, 0.005f);
      distance_penalty_b = 1.0f - gauss((b.position.head<2>() - preffered_location).norm(), 0.0f, 1.0f, 0.005f);
    }

    float penalty_a = a.overhang + a.visibility +
                      angle_importance * compute_angle_penalty(a.local_ccw_angle)
                      + distance_penalty_a;
    float penalty_b = b.overhang + b.visibility +
                      angle_importance * compute_angle_penalty(b.local_ccw_angle)
                      + distance_penalty_b;

    return penalty_a < penalty_b;
  }

  bool is_first_not_much_worse(const SeamCandidate &a, const SeamCandidate &b) const {
    if ((setup == SeamPosition::spAligned || setup == SeamPosition::spAlignedBack) && a.central_enforcer != b.central_enforcer) {
      return a.central_enforcer;
    }

    if (a.type == EnforcedBlockedSeamPoint::Enforced) {
      return true;
    }

    if (a.type == EnforcedBlockedSeamPoint::Blocked) {
      return false;
    }

    if (a.type != b.type) {
      return a.type > b.type;
    }

    if ((a.overhang > 0.0f || b.overhang > 0.0f)
        && abs(a.overhang - b.overhang) > (0.1f * a.perimeter.flow_width)) {
      return a.overhang < b.overhang;
    }

    if (a.embedded_distance < -0.5f && b.embedded_distance > -0.5f) {
      return true;
    }
    if (b.embedded_distance < -0.5f && a.embedded_distance > -0.5f) {
      return false;
    }

    if (setup == SeamPosition::spRandom) {
      return true;
    }

    if (setup == SeamPosition::spRear) {
      return a.position.y() + SeamPlacer::seam_align_score_tolerance * 5.0f > b.position.y();
    }

    float penalty_a = a.overhang + a.visibility
                      + angle_importance * compute_angle_penalty(a.local_ccw_angle);
    float penalty_b = b.overhang + b.visibility +
                      angle_importance * compute_angle_penalty(b.local_ccw_angle);

    return penalty_a <= penalty_b || penalty_a - penalty_b < SeamPlacer::seam_align_score_tolerance;
  }

  bool are_similar(const SeamCandidate &a, const SeamCandidate &b) const {
    return is_first_not_much_worse(a, b) && is_first_not_much_worse(b, a);
  }
};

#ifdef DEBUG_FILES
void debug_export_points(const std::vector<PrintObjectSeamData::LayerSeams> &layers,
                         const BoundingBox &bounding_box, const SeamComparator &comparator) {
  for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
    std::string angles_file_name = debug_out_path(
        ("angles_" + std::to_string(layer_idx) + ".svg").c_str());
    SVG angles_svg { angles_file_name, bounding_box };
    float min_vis = 0;
    float max_vis = min_vis;

    float min_weight = std::numeric_limits<float>::min();
    float max_weight = min_weight;

    for (const SeamCandidate &point : layers[layer_idx].points) {
      Vec3i32 color = value_to_rgbi(-PI, PI, point.local_ccw_angle);
      std::string fill = "rgb(" + std::to_string(color.x()) + "," + std::to_string(color.y()) + ","
                         + std::to_string(color.z()) + ")";
      angles_svg.draw(scaled(Vec2f(point.position.head<2>())), fill);
      min_vis = std::min(min_vis, point.visibility);
      max_vis = std::max(max_vis, point.visibility);

      min_weight = std::min(min_weight, -compute_angle_penalty(point.local_ccw_angle));
      max_weight = std::max(max_weight, -compute_angle_penalty(point.local_ccw_angle));

    }

    std::string visiblity_file_name = debug_out_path(
        ("visibility_" + std::to_string(layer_idx) + ".svg").c_str());
    SVG visibility_svg { visiblity_file_name, bounding_box };
    std::string weights_file_name = debug_out_path(
        ("weight_" + std::to_string(layer_idx) + ".svg").c_str());
    SVG weight_svg { weights_file_name, bounding_box };
    std::string overhangs_file_name = debug_out_path(
        ("overhang_" + std::to_string(layer_idx) + ".svg").c_str());
    SVG overhangs_svg { overhangs_file_name, bounding_box };

    for (const SeamCandidate &point : layers[layer_idx].points) {
      Vec3i32 color = value_to_rgbi(min_vis, max_vis, point.visibility);
      std::string visibility_fill = "rgb(" + std::to_string(color.x()) + "," + std::to_string(color.y()) + ","
                                    + std::to_string(color.z()) + ")";
      visibility_svg.draw(scaled(Vec2f(point.position.head<2>())), visibility_fill);

      Vec3i32 weight_color = value_to_rgbi(min_weight, max_weight,
                                         -compute_angle_penalty(point.local_ccw_angle));
      std::string weight_fill = "rgb(" + std::to_string(weight_color.x()) + "," + std::to_string(weight_color.y())
                                + ","
                                + std::to_string(weight_color.z()) + ")";
      weight_svg.draw(scaled(Vec2f(point.position.head<2>())), weight_fill);

      Vec3i32 overhang_color = value_to_rgbi(-0.5, 0.5, std::clamp(point.overhang, -0.5f, 0.5f));
      std::string overhang_fill = "rgb(" + std::to_string(overhang_color.x()) + ","
                                  + std::to_string(overhang_color.y())
                                  + ","
                                  + std::to_string(overhang_color.z()) + ")";
      overhangs_svg.draw(scaled(Vec2f(point.position.head<2>())), overhang_fill);
    }
  }
}
#endif

void pick_seam_point(std::vector<SeamCandidate> &perimeter_points, size_t start_index,
                     const SeamComparator &comparator) {
  size_t end_index = perimeter_points[start_index].perimeter.end_index;

  size_t seam_index = start_index;
  for (size_t index = start_index; index < end_index; ++index) {
    if (comparator.is_first_better(perimeter_points[index], perimeter_points[seam_index])) {
      seam_index = index;
    }
  }
  perimeter_points[start_index].perimeter.seam_index = seam_index;
}

size_t pick_nearest_seam_point_index(const std::vector<SeamCandidate> &perimeter_points, size_t start_index,
                                     const Vec2f &preffered_location) {
  size_t end_index = perimeter_points[start_index].perimeter.end_index;
  SeamComparator comparator { spNearest };

  size_t seam_index = start_index;
  for (size_t index = start_index; index < end_index; ++index) {
    if (comparator.is_first_better(perimeter_points[index], perimeter_points[seam_index], preffered_location)) {
      seam_index = index;
    }
  }
  return seam_index;
}

void pick_random_seam_point(const std::vector<SeamCandidate> &perimeter_points, size_t start_index) {
  SeamComparator comparator { spRandom };

  size_t viable_example_index = start_index;
  size_t end_index = perimeter_points[start_index].perimeter.end_index;
  struct Viable {
    size_t index;
    float edge_length;
    Vec3f edge;
  };
  std::vector<Viable> viables;

  const Vec3f pseudornd_seed = perimeter_points[viable_example_index].position;
  float rand = std::abs(sin(pseudornd_seed.dot(Vec3f(12.9898f,78.233f, 133.3333f))) * 43758.5453f);
  rand = rand - (int) rand;

  for (size_t index = start_index; index < end_index; ++index) {
    if (comparator.are_similar(perimeter_points[index], perimeter_points[viable_example_index])) {
      Vec3f edge_to_next { perimeter_points[index == end_index - 1 ? start_index : index + 1].position
                         - perimeter_points[index].position };
      float dist_to_next = edge_to_next.norm();
      viables.push_back( { index, dist_to_next, edge_to_next });
    } else if (comparator.is_first_not_much_worse(perimeter_points[viable_example_index],
                                                  perimeter_points[index])) {
    } else {
      viable_example_index = index;
      viables.clear();

      Vec3f edge_to_next = (perimeter_points[index == end_index - 1 ? start_index : index + 1].position
                            - perimeter_points[index].position);
      float dist_to_next = edge_to_next.norm();
      viables.push_back( { index, dist_to_next, edge_to_next });
    }
  }

  float len_sum = std::accumulate(viables.begin(), viables.end(), 0.0f, [](const float acc, const Viable &v) {
    return acc + v.edge_length;
  });
  float picked_len = len_sum * rand;

  size_t point_idx = 0;
  while (picked_len - viables[point_idx].edge_length > 0) {
    picked_len = picked_len - viables[point_idx].edge_length;
    point_idx++;
  }

  Perimeter &perimeter = perimeter_points[start_index].perimeter;
  perimeter.seam_index = viables[point_idx].index;
  perimeter.final_seam_position = perimeter_points[perimeter.seam_index].position
                                  + viables[point_idx].edge.normalized() * picked_len;
  perimeter.finalized = true;
}

}
void SeamPlacer::gather_seam_candidates(const PrintObject *po, const SeamPlacerImpl::GlobalModelInfo &global_model_info,
                                        PreciseSeam::PreciseSeamWarnings* warnings) {
  using namespace SeamPlacerImpl;
  PrintObjectSeamData &seam_data = m_seam_per_object.emplace(po, PrintObjectSeamData { }).first->second;
  seam_data.layers.resize(po->layer_count());

  tbb::parallel_for(tbb::blocked_range<size_t>(0, po->layers().size()),
                    [po, &global_model_info, &seam_data, warnings]
                    (tbb::blocked_range<size_t> r) {
                      for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                        PrintObjectSeamData::LayerSeams &layer_seams = seam_data.layers[layer_idx];
                        const Layer *layer = po->get_layer(layer_idx);
                        auto unscaled_z = layer->slice_z;
                        std::vector<const LayerRegion*> regions;
                        const bool has_precise_seam_modifiers = !global_model_info.precise_seam_strong_volumes.empty() ||
                                                               !global_model_info.precise_seam_weak_volumes.empty();
                        Polygons polygons = extract_perimeter_polygons(layer, regions, has_precise_seam_modifiers);
                        for (size_t poly_index = 0; poly_index < polygons.size(); ++poly_index) {
                          process_perimeter_polygon(polygons[poly_index], unscaled_z,
                                                    regions[poly_index], global_model_info, layer_seams,
                                                    warnings);
                        }
                        auto functor = SeamCandidateCoordinateFunctor { layer_seams.points };
                        seam_data.layers[layer_idx].points_tree =
                            std::make_unique<PrintObjectSeamData::SeamCandidatesTree>(functor,
                                                                                      layer_seams.points.size());
                      }
                    }
  );
}

void SeamPlacer::calculate_candidates_visibility(const PrintObject *po,
                                                 const SeamPlacerImpl::GlobalModelInfo &global_model_info) {

  std::vector<PrintObjectSeamData::LayerSeams> &layers = m_seam_per_object[po].layers;
  tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()),
                    [&layers, &global_model_info](tbb::blocked_range<size_t> r) {
                      for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                        for (auto &perimeter_point : layers[layer_idx].points) {
                          perimeter_point.visibility = global_model_info.calculate_point_visibility(
                              perimeter_point.position);
                        }
                      }
                    });
}

void SeamPlacer::calculate_overhangs_and_layer_embedding(const PrintObject *po) {
  using namespace SeamPlacerImpl;
  using PerimeterDistancer = AABBTreeLines::LinesDistancer<Linef>;

  std::vector<PrintObjectSeamData::LayerSeams> &layers = m_seam_per_object[po].layers;
  tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()),
                    [po, &layers](tbb::blocked_range<size_t> r) {
                      std::unique_ptr<PerimeterDistancer> prev_layer_distancer;
                      if (r.begin() > 0) {
                        prev_layer_distancer = std::make_unique<PerimeterDistancer>(to_unscaled_linesf(po->layers()[r.begin() - 1]->lslices));
                      }

                      for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                        size_t regions_with_perimeter = 0;
                        for (const LayerRegion *region : po->layers()[layer_idx]->regions()) {
                          if (region->perimeters.entities.size() > 0) {
                            regions_with_perimeter++;
                          }
                        };
                        bool should_compute_layer_embedding = regions_with_perimeter > 1;
                        std::unique_ptr<PerimeterDistancer> current_layer_distancer        = std::make_unique<PerimeterDistancer>(
                            to_unscaled_linesf(po->layers()[layer_idx]->lslices));

                        auto& layer_seams = layers[layer_idx];
                        for (SeamCandidate &perimeter_point : layer_seams.points) {
                          Vec2f point = Vec2f { perimeter_point.position.head<2>() };
                          if (prev_layer_distancer.get() != nullptr) {
                            const auto _dist = prev_layer_distancer->distance_from_lines<true>(point.cast<double>());
                            perimeter_point.overhang = _dist
                                                       + 0.65f * perimeter_point.perimeter.flow_width
                                                       - tan(SeamPlacer::overhang_angle_threshold)
                                                             * po->layers()[layer_idx]->height;
                            perimeter_point.overhang =
                                perimeter_point.overhang < 0.0f ? 0.0f : perimeter_point.overhang;
                            perimeter_point.unsupported_dist = _dist + 0.4f * perimeter_point.perimeter.flow_width;
                          }

                          if (should_compute_layer_embedding) {
                            perimeter_point.embedded_distance = current_layer_distancer->distance_from_lines<true>(point.cast<double>())
                                                                + 0.65f * perimeter_point.perimeter.flow_width;
                          }
                        }

                        prev_layer_distancer.swap(current_layer_distancer);
                      }
                    }
  );
}

std::optional<std::pair<size_t, size_t>> SeamPlacer::find_next_seam_in_layer(
    const std::vector<PrintObjectSeamData::LayerSeams> &layers,
    const Vec3f &projected_position,
    const size_t layer_idx, const float max_distance,
    const SeamPlacerImpl::SeamComparator &comparator) const {
  using namespace SeamPlacerImpl;
  std::vector<size_t> nearby_points_indices = find_nearby_points(*layers[layer_idx].points_tree, projected_position,
                                                                 max_distance);

  if (nearby_points_indices.empty()) {
    return {};
  }

  size_t best_nearby_point_index = nearby_points_indices[0];
  size_t nearest_point_index = nearby_points_indices[0];

  for (const size_t &nearby_point_index : nearby_points_indices) {
    const SeamCandidate &point = layers[layer_idx].points[nearby_point_index];
    if (point.perimeter.finalized) {
      continue;
    }
    if (comparator.is_first_better(point, layers[layer_idx].points[best_nearby_point_index],
                                   projected_position.head<2>())
        || layers[layer_idx].points[best_nearby_point_index].perimeter.finalized) {
      best_nearby_point_index = nearby_point_index;
    }
    if ((point.position - projected_position).squaredNorm()
            < (layers[layer_idx].points[nearest_point_index].position - projected_position).squaredNorm()
        || layers[layer_idx].points[nearest_point_index].perimeter.finalized) {
      nearest_point_index = nearby_point_index;
    }
  }

  const SeamCandidate &best_nearby_point = layers[layer_idx].points[best_nearby_point_index];
  const SeamCandidate &nearest_point = layers[layer_idx].points[nearest_point_index];

  if (nearest_point.perimeter.finalized) {
    return {};
  }

  const SeamCandidate &next_layer_seam = layers[layer_idx].points[nearest_point.perimeter.seam_index];

  if (next_layer_seam.central_enforcer
      && (next_layer_seam.position - projected_position).squaredNorm()
             < sqr(3 * max_distance)) {
    return {std::pair<size_t, size_t> {layer_idx, nearest_point.perimeter.seam_index}};
  }

  if (comparator.is_first_not_much_worse(nearest_point, next_layer_seam)) {
    return {std::pair<size_t, size_t> {layer_idx, nearest_point_index}};
  }
  if (comparator.is_first_not_much_worse(best_nearby_point, next_layer_seam)) {
    return {std::pair<size_t, size_t> {layer_idx, best_nearby_point_index}};
  }

  return {};
}

std::vector<std::pair<size_t, size_t>> SeamPlacer::find_seam_string(const PrintObject *po,
                                                                    std::pair<size_t, size_t> start_seam, const SeamPlacerImpl::SeamComparator &comparator) const {
  const std::vector<PrintObjectSeamData::LayerSeams> &layers = m_seam_per_object.find(po)->second.layers;
  int layer_idx = start_seam.first;

  int next_layer = layer_idx + 1;
  int step = 1;
  std::pair<size_t, size_t> prev_point_index = start_seam;
  std::vector<std::pair<size_t, size_t>> seam_string { start_seam };

  auto reverse_lookup_direction = [&]() {
    step = -1;
    prev_point_index = start_seam;
    next_layer = layer_idx - 1;
  };

  while (next_layer >= 0) {
    if (next_layer >= int(layers.size())) {
      reverse_lookup_direction();
      if (next_layer < 0) {
        break;
      }
    }
    float max_distance = SeamPlacer::seam_align_tolerable_dist_factor *
                         layers[start_seam.first].points[start_seam.second].perimeter.flow_width;
    Vec3f prev_position = layers[prev_point_index.first].points[prev_point_index.second].position;
    Vec3f projected_position = prev_position;
    projected_position.z() = float(po->get_layer(next_layer)->slice_z);

    std::optional<std::pair<size_t, size_t>> maybe_next_seam = find_next_seam_in_layer(layers, projected_position,
                                                                                       next_layer,
                                                                                       max_distance, comparator);

    if (maybe_next_seam.has_value()) {
      seam_string.push_back(maybe_next_seam.operator*());
      prev_point_index = seam_string.back();
    } else {
      if (step == 1) {
        reverse_lookup_direction();
        if (next_layer < 0) {
          break;
        }
      } else {
        break;
      }
    }
    next_layer += step;
  }
  return seam_string;
}

void SeamPlacer::align_seam_points(const PrintObject *po, const SeamPlacerImpl::SeamComparator &comparator) {
  using namespace SeamPlacerImpl;

#ifdef DEBUG_FILES
  Slic3r::CNumericLocalesSetter locales_setter;
  auto clusters_f = debug_out_path("seam_clusters.obj");
  FILE *clusters = boost::nowide::fopen(clusters_f.c_str(), "w");
  if (clusters == nullptr) {
    BOOST_LOG_TRIVIAL(error)
        << "stl_write_obj: Couldn't open " << clusters_f << " for writing";
    return;
  }
  auto aligned_f = debug_out_path("aligned_clusters.obj");
  FILE *aligns = boost::nowide::fopen(aligned_f.c_str(), "w");
  if (aligns == nullptr) {
    BOOST_LOG_TRIVIAL(error)
        << "stl_write_obj: Couldn't open " << clusters_f << " for writing";
    return;
  }
#endif

  const std::vector<PrintObjectSeamData::LayerSeams> &layers = m_seam_per_object[po].layers;
  std::vector<std::pair<size_t, size_t>> seams;
  for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
    const std::vector<SeamCandidate> &layer_perimeter_points = layers[layer_idx].points;
    size_t current_point_index = 0;
    while (current_point_index < layer_perimeter_points.size()) {
      seams.emplace_back(layer_idx, layer_perimeter_points[current_point_index].perimeter.seam_index);
      current_point_index = layer_perimeter_points[current_point_index].perimeter.end_index;
    }
  }

  std::stable_sort(seams.begin(), seams.end(),
                   [&comparator, &layers](const std::pair<size_t, size_t> &left,
                                          const std::pair<size_t, size_t> &right) {
                     return comparator.is_first_better(layers[left.first].points[left.second],
                                                       layers[right.first].points[right.second]);
                   }
  );

  std::vector<std::pair<size_t, size_t>> seam_string;
  std::vector<std::pair<size_t, size_t>> alternative_seam_string;
  std::vector<Vec2f> observations;
  std::vector<float> observation_points;
  std::vector<float> weights;

  int global_index = 0;
  while (global_index < int(seams.size())) {
    size_t layer_idx = seams[global_index].first;
    size_t seam_index = seams[global_index].second;
    global_index++;
    const std::vector<SeamCandidate> &layer_perimeter_points = layers[layer_idx].points;
    if (layer_perimeter_points[seam_index].perimeter.finalized) {
      continue;
    } else {
      seam_string = this->find_seam_string(po, { layer_idx, seam_index }, comparator);
      size_t step_size = 1 + seam_string.size() / 20;
      for (size_t alternative_start = 0; alternative_start < seam_string.size(); alternative_start += step_size) {
        size_t start_layer_idx = seam_string[alternative_start].first;
        size_t seam_idx =
            layers[start_layer_idx].points[seam_string[alternative_start].second].perimeter.seam_index;
        alternative_seam_string = this->find_seam_string(po,
                                                         std::pair<size_t, size_t>(start_layer_idx, seam_idx), comparator);
        if (alternative_seam_string.size() > seam_string.size()) {
          seam_string = std::move(alternative_seam_string);
        }
      }
      if (seam_string.size() < seam_align_minimum_string_seams) {
        continue;
      }

      std::sort(seam_string.begin(), seam_string.end(),
                [](const std::pair<size_t, size_t> &left, const std::pair<size_t, size_t> &right) {
                  return left.first < right.first;
                });

      global_index--;

      observations.resize(seam_string.size());
      observation_points.resize(seam_string.size());
      weights.resize(seam_string.size());

      auto angle_3d = [](const Vec3f& a, const Vec3f& b){
        return std::abs(acosf(a.normalized().dot(b.normalized())));
      };

      auto angle_weight = [](float angle){
        return 1.0f / (0.1f + compute_angle_penalty(angle));
      };

      float total_length = 0.0f;
      Vec3f last_point_pos = layers[seam_string[0].first].points[seam_string[0].second].position;
      for (size_t index = 0; index < seam_string.size(); ++index) {
        const SeamCandidate &current = layers[seam_string[index].first].points[seam_string[index].second];
        float layer_angle = 0.0f;
        if (index > 0 && index < seam_string.size() - 1) {
          layer_angle = angle_3d(
              current.position
                  - layers[seam_string[index - 1].first].points[seam_string[index - 1].second].position,
              layers[seam_string[index + 1].first].points[seam_string[index + 1].second].position
                  - current.position
          );
        }
        observations[index] = current.position.head<2>();
        observation_points[index] = current.position.z();
        weights[index] = angle_weight(current.local_ccw_angle);
        float curling_influence = layer_angle > 2.0 * std::abs(current.local_ccw_angle) ? -0.8f : 1.0f;
        if (current.type == EnforcedBlockedSeamPoint::Enforced) {
          curling_influence = 1.0f;
          weights[index] += 3.0f;
        }
        total_length += curling_influence * (last_point_pos - current.position).norm();
        last_point_pos = current.position;
      }

      if (comparator.setup == spRear) {
        total_length *= 0.3f;
      }

      size_t number_of_segments = std::max(size_t(1),
                                           size_t(std::max(0.0f,total_length) / SeamPlacer::seam_align_mm_per_segment));
      auto curve = Geometry::fit_cubic_bspline(observations, observation_points, weights, number_of_segments);

      for (size_t index = 0; index < seam_string.size(); ++index) {
        const auto &pair = seam_string[index];
        float t = std::min(1.0f, std::pow(std::abs(layers[pair.first].points[pair.second].local_ccw_angle)
                                              / SeamPlacer::sharp_angle_snapping_threshold, 3.0f));
        if (layers[pair.first].points[pair.second].type == EnforcedBlockedSeamPoint::Enforced){
          t = std::max(0.4f, t);
        }

        Vec3f current_pos = layers[pair.first].points[pair.second].position;
        Vec2f fitted_pos = curve.get_fitted_value(current_pos.z());

        Vec3f final_position = t * current_pos + (1.0f - t) * to_3d(fitted_pos, current_pos.z());

        Perimeter &perimeter = layers[pair.first].points[pair.second].perimeter;
        perimeter.seam_index = pair.second;
        perimeter.final_seam_position = final_position;
        perimeter.finalized = true;
      }

#ifdef DEBUG_FILES
      auto randf = []() {
        return float(rand()) / float(RAND_MAX);
      };
      Vec3f color { randf(), randf(), randf() };
      for (size_t i = 0; i < seam_string.size(); ++i) {
        auto orig_seam = layers[seam_string[i].first].points[seam_string[i].second];
        fprintf(clusters, "v %f %f %f %f %f %f \n", orig_seam.position[0],
                orig_seam.position[1],
                orig_seam.position[2], color[0], color[1],
                color[2]);
      }

      color = Vec3f { randf(), randf(), randf() };
      for (size_t i = 0; i < seam_string.size(); ++i) {
        const Perimeter &perimeter = layers[seam_string[i].first].points[seam_string[i].second].perimeter;
        fprintf(aligns, "v %f %f %f %f %f %f \n", perimeter.final_seam_position[0],
                perimeter.final_seam_position[1],
                perimeter.final_seam_position[2], color[0], color[1],
                color[2]);
      }
#endif
    }
  }

#ifdef DEBUG_FILES
  fclose(clusters);
  fclose(aligns);
#endif

}

void SeamPlacer::init(const Print &print, std::function<void(void)> throw_if_canceled_func) {
  using namespace SeamPlacerImpl;
  m_seam_per_object.clear();
  m_precise_seam_warning.clear();

  PreciseSeam::PreciseSeamWarnings precise_seam_warnings;

  for (const PrintObject *po : print.objects()) {
    throw_if_canceled_func();
    SeamPosition configured_seam_preference = po->config().seam_position.value;
    SeamComparator comparator { configured_seam_preference };

    {
      GlobalModelInfo global_model_info { };
      gather_enforcers_blockers(global_model_info, po);
      PreciseSeam::init_precise_seam_data(
          global_model_info.precise_seam_strong_volumes,
          global_model_info.precise_seam_weak_volumes,
          m_seam_per_object[po].has_precise_seam_strong_volumes,
          po->model_object());

      for (const ModelVolume* vol : global_model_info.precise_seam_strong_volumes)
          global_model_info.precise_seam_slices[vol] = PreciseSeam::prepare_modifier_slices(po->slice_single_volume_regions(vol));
      for (const ModelVolume* vol : global_model_info.precise_seam_weak_volumes)
          global_model_info.precise_seam_slices[vol] = PreciseSeam::prepare_modifier_slices(po->slice_single_volume_regions(vol));
      for (const ModelVolume* vol : global_model_info.precise_seam_strong_volumes)
          precise_seam_warnings.modifier_usage.try_emplace(vol);
      for (const ModelVolume* vol : global_model_info.precise_seam_weak_volumes)
          precise_seam_warnings.modifier_usage.try_emplace(vol);

      throw_if_canceled_func();
      if (configured_seam_preference == spAligned || configured_seam_preference == spNearest || configured_seam_preference == spAlignedBack) {
        compute_global_occlusion(global_model_info, po, throw_if_canceled_func, configured_seam_preference);
      }
      throw_if_canceled_func();

      BOOST_LOG_TRIVIAL(debug)
          << "SeamPlacer: gather_seam_candidates: start";
      gather_seam_candidates(po, global_model_info, &precise_seam_warnings);
      BOOST_LOG_TRIVIAL(debug)
          << "SeamPlacer: gather_seam_candidates: end";
      throw_if_canceled_func();
      if (configured_seam_preference == spAligned || configured_seam_preference == spNearest || configured_seam_preference == spAlignedBack) {
        BOOST_LOG_TRIVIAL(debug)
            << "SeamPlacer: calculate_candidates_visibility : start";
        calculate_candidates_visibility(po, global_model_info);
        BOOST_LOG_TRIVIAL(debug)
            << "SeamPlacer: calculate_candidates_visibility : end";
      }
    }
    throw_if_canceled_func();
    BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: calculate_overhangs and layer embdedding : start";
    calculate_overhangs_and_layer_embedding(po);
    BOOST_LOG_TRIVIAL(debug)
        << "SeamPlacer: calculate_overhangs and layer embdedding: end";
    throw_if_canceled_func();
    if (configured_seam_preference != spNearest) {
      BOOST_LOG_TRIVIAL(debug)
          << "SeamPlacer: pick_seam_point : start";
      std::vector<PrintObjectSeamData::LayerSeams> &layers = m_seam_per_object[po].layers;
      tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()),
                        [&layers, configured_seam_preference, comparator](tbb::blocked_range<size_t> r) {
                          for (size_t layer_idx = r.begin(); layer_idx < r.end(); ++layer_idx) {
                            std::vector<SeamCandidate> &layer_perimeter_points = layers[layer_idx].points;
                            for (size_t current = 0; current < layer_perimeter_points.size();
                                 current = layer_perimeter_points[current].perimeter.end_index)
                              if (configured_seam_preference == spRandom)
                                pick_random_seam_point(layer_perimeter_points, current);
                              else
                                pick_seam_point(layer_perimeter_points, current, comparator);
                          }
                        });
      BOOST_LOG_TRIVIAL(debug)
          << "SeamPlacer: pick_seam_point : end";
    }
    throw_if_canceled_func();
    if (configured_seam_preference == spAligned || configured_seam_preference == spRear || configured_seam_preference == spAlignedBack) {
      BOOST_LOG_TRIVIAL(debug)
          << "SeamPlacer: align_seam_points : start";
      align_seam_points(po, comparator);
      BOOST_LOG_TRIVIAL(debug)
          << "SeamPlacer: align_seam_points : end";
    }

    if (m_seam_per_object[po].has_precise_seam_strong_volumes) {
      PreciseSeam::restore_precise_seam_positions(m_seam_per_object[po].layers);
    }

#ifdef DEBUG_FILES
    debug_export_points(m_seam_per_object[po].layers, po->bounding_box(), comparator);
#endif
  }

  {
      const unsigned failed_types = precise_seam_warnings.failed_types.load(std::memory_order_relaxed);
      const unsigned mi = precise_seam_warnings.multiple_intersections.load(std::memory_order_relaxed);
      const unsigned fc = precise_seam_warnings.full_containment.load(std::memory_order_relaxed);
      const size_t failed = precise_seam_warnings.failed_fragments.load(std::memory_order_relaxed);
      if (failed > PreciseSeam::failed_fragment_log_limit)
          BOOST_LOG_TRIVIAL(warning) << "[PreciseSeamIntersectionFailed] " << failed
              << " fragments discarded; first " << PreciseSeam::failed_fragment_log_limit
              << " logged (parallel processing order), " << (failed - PreciseSeam::failed_fragment_log_limit)
              << " omitted";
      const size_t recovered = precise_seam_warnings.recovered_fragments.load(std::memory_order_relaxed);
      if (recovered > PreciseSeam::failed_fragment_log_limit)
          BOOST_LOG_TRIVIAL(warning) << "[PreciseSeamFragmentRecovered] " << recovered
              << " fragments recovered; first " << PreciseSeam::failed_fragment_log_limit
              << " logged (parallel processing order), " << (recovered - PreciseSeam::failed_fragment_log_limit)
              << " omitted";
      const auto type_list = [](unsigned mask) {
          const std::pair<ModelVolumeType, std::string> types[] = {
              {ModelVolumeType::PRECISE_SEAM_CENTER, _u8L("Seam Center")},
              {ModelVolumeType::PRECISE_SEAM_LEFT, _u8L("Seam Left")},
              {ModelVolumeType::PRECISE_SEAM_RIGHT, _u8L("Seam Right")},
              {ModelVolumeType::PRECISE_SEAM_ENFORCED, _u8L("Seam Enforced")},
              {ModelVolumeType::PRECISE_SEAM_BLOCKED, _u8L("Seam Blocked")},
              {ModelVolumeType::PRECISE_SEAM_NEUTRAL, _u8L("Seam Neutral")}};
          std::string list;
          for (const auto &[type, name] : types)
              if (mask & PreciseSeam::PreciseSeamWarnings::type_bit(type))
                  list += (list.empty() ? "" : ", ") + name;
          return list;
      };
      std::vector<std::string> parts;
      if (failed_types != 0)
          parts.push_back((boost::format(_u8L("failed to process some intersections (%1%)")) % type_list(failed_types)).str());
      if (mi != 0)
          parts.push_back((boost::format(_u8L("multiple intersections with a perimeter, only one was used (%1%)")) % type_list(mi)).str());
      if (fc != 0)
          parts.push_back((boost::format(_u8L("a perimeter is fully inside a modifier, the modifier was not applied to it (%1%)")) % type_list(fc)).str());
      std::vector<const ModelVolume*> no_effect;
      for (const PrintObject *po : print.objects())
          for (const ModelVolume *volume : po->model_object()->volumes) {
              const auto it = precise_seam_warnings.modifier_usage.find(volume);
              if (it != precise_seam_warnings.modifier_usage.end() &&
                  it->second.checked.load(std::memory_order_relaxed) &&
                  !it->second.reached.load(std::memory_order_relaxed) &&
                  std::find(no_effect.begin(), no_effect.end(), volume) == no_effect.end())
                  no_effect.push_back(volume);
          }
      for (const ModelVolume *volume : no_effect)
          BOOST_LOG_TRIVIAL(warning) << "[PreciseSeamNoEffect] object=\"" << volume->get_object()->name
              << "\" modifier=\"" << volume->name << "\"";
      if (!no_effect.empty()) {
          const ModelVolume *first = no_effect.front();
          if (no_effect.size() == 1)
              parts.push_back((boost::format(_u8L("modifier \"%1%\" of \"%2%\" had no effect on the seam (it might not reach the centerline of the printed perimeter)"))
                  % first->name % first->get_object()->name).str());
          else
              parts.push_back((boost::format(_u8L("modifier \"%1%\" of \"%2%\" (%3% in total) had no effect on the seam (it might not reach the centerline of the printed perimeter)"))
                  % first->name % first->get_object()->name % no_effect.size()).str());
      }
      if (!parts.empty()) {
          std::string warning_text = _u8L("Precise Seam") + ": ";
          for (size_t i = 0; i < parts.size(); ++i) {
              if (i > 0) warning_text += "; ";
              warning_text += parts[i];
          }
          warning_text += ". ";
          warning_text += _u8L("Seam placement may differ from expected.");
          m_precise_seam_warning = std::move(warning_text);
      }
  }
}

void SeamPlacer::place_seam(const Layer *layer, ExtrusionLoop &loop,
                            const Point &last_pos, float& overhang) const {
  using namespace SeamPlacerImpl;
  const PrintObject *po = layer->object();
  assert(dynamic_cast<const SupportLayer*>(layer) == nullptr);
  assert(layer->id() >= po->slicing_parameters().raft_layers());
  const size_t layer_index = layer->id() - po->slicing_parameters().raft_layers();
  const double unscaled_z = layer->slice_z;

  auto get_next_loop_point = [loop](ExtrusionLoop::ClosestPathPoint current) {
    current.segment_idx += 1;
    if (current.segment_idx >= loop.paths[current.path_idx].polyline.points.size()) {
      current.path_idx = next_idx_modulo(current.path_idx, loop.paths.size());
      current.segment_idx = 0;
    }
    current.foot_pt = loop.paths[current.path_idx].polyline.points[current.segment_idx].to_point();
    return current;
  };

  const PrintObjectSeamData::LayerSeams &layer_perimeters =
      m_seam_per_object.find(layer->object())->second.layers[layer_index];

  size_t closest_perimeter_point_index = 0;
  {
    Perimeter *closest_perimeter = nullptr;
    ExtrusionLoop::ClosestPathPoint closest_point{0, 0, loop.paths[0].polyline.points[0].to_point()};
    size_t points_count = std::accumulate(loop.paths.begin(), loop.paths.end(), 0, [](size_t acc,const ExtrusionPath& p) {
      return acc + p.polyline.points.size();
    });
    for (size_t i = 0; i < points_count; ++i) {
      Vec2f unscaled_p = unscaled<float>(closest_point.foot_pt);
      closest_perimeter_point_index = find_closest_point(*layer_perimeters.points_tree.get(),
                                                         to_3d(unscaled_p, float(unscaled_z)));
      if (closest_perimeter != &layer_perimeters.points[closest_perimeter_point_index].perimeter) {
        closest_perimeter = &layer_perimeters.points[closest_perimeter_point_index].perimeter;
        closest_point = get_next_loop_point(closest_point);
      } else {
        break;
      }
    }
  }

  Vec3f seam_position;
  size_t seam_index;
  if (const Perimeter &perimeter = layer_perimeters.points[closest_perimeter_point_index].perimeter;
      perimeter.finalized) {
    seam_position = perimeter.final_seam_position;
    seam_index = perimeter.seam_index;
  } else {
    seam_index =
        po->config().seam_position == spNearest ?
                                                pick_nearest_seam_point_index(layer_perimeters.points, perimeter.start_index,
                                                                              unscaled<float>(last_pos)) :
                                                perimeter.seam_index;
    seam_position = layer_perimeters.points[seam_index].position;
  }

  Point seam_point = Point::new_scale(seam_position.x(), seam_position.y());
  overhang = layer_perimeters.points[seam_index].unsupported_dist;

  if (loop.role() == ExtrusionRole::erPerimeter) {
    const SeamCandidate &perimeter_point = layer_perimeters.points[seam_index];
    ExtrusionLoop::ClosestPathPoint projected_point = loop.get_closest_path_and_point(seam_point, false);
    float depth = (float) unscale(Point(seam_point - projected_point.foot_pt)).norm();
    float beta_angle = cos(perimeter_point.local_ccw_angle / 2.0f);
    size_t index_of_prev =
        seam_index == perimeter_point.perimeter.start_index ?
                                                            perimeter_point.perimeter.end_index - 1 :
                                                            seam_index - 1;
    size_t index_of_next =
        seam_index == perimeter_point.perimeter.end_index - 1 ?
                                                              perimeter_point.perimeter.start_index :
                                                              seam_index + 1;

    if ((seam_position - perimeter_point.position).squaredNorm() < depth &&
        perimeter_point.local_ccw_angle < -EPSILON
    ) {
      Vec2f dir_to_middle =
          ((perimeter_point.position - layer_perimeters.points[index_of_prev].position).head<2>().normalized()
           + (perimeter_point.position - layer_perimeters.points[index_of_next].position).head<2>().normalized())
          * 0.5;
      depth = 1.4142 * depth / beta_angle;
      Vec2f final_pos = perimeter_point.position.head<2>() + depth * dir_to_middle;
      projected_point = loop.get_closest_path_and_point(Point::new_scale(final_pos.x(), final_pos.y()), false);
    } else {
      depth = depth * beta_angle / 1.4142;
    }

    seam_point = projected_point.foot_pt;

    if (po->config().staggered_inner_seams && loop.length() > 0.0) {
      depth = std::max(loop.paths[projected_point.path_idx].width, depth);

      while (depth > 0.0f) {
        auto next_point = get_next_loop_point(projected_point);
        Vec2f a = unscale(projected_point.foot_pt).cast<float>();
        Vec2f b = unscale(next_point.foot_pt).cast<float>();
        float dist = (a - b).norm();
        if (dist > depth) {
          Vec2f final_pos = a + (b - a) * depth / dist;
          next_point.foot_pt = Point::new_scale(final_pos.x(), final_pos.y());
        }
        depth -= dist;
        projected_point = next_point;
      }
      seam_point = projected_point.foot_pt;
    }
  }

  if (!loop.split_at_vertex(seam_point, scaled<double>(0.0015))) {
    loop.split_at(seam_point, true);
  }

}

}
