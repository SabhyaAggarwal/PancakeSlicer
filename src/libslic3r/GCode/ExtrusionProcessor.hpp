#ifndef slic3r_ExtrusionProcessor_hpp_
#define slic3r_ExtrusionProcessor_hpp_



#include "../AABBTreeLines.hpp"

#include "../libslic3r.h"
#include "../ExtrusionEntity.hpp"
#include "../Layer.hpp"
#include "../Point.hpp"
#include "../SVG.hpp"
#include "../BoundingBox.hpp"
#include "../Polygon.hpp"
#include "../ClipperUtils.hpp"
#include "../Flow.hpp"
#include "../Config.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {

template<int Dim> struct ExtendedPoint
{
    Eigen::Matrix<double, Dim, 1, Eigen::DontAlign> position;
    float distance;
    float curvature;
};


struct NoKnownDistances
{
    template<typename P> const double *operator()(const P &) const { return nullptr; }
};

template<bool SCALED_INPUT, bool ADD_INTERSECTIONS, bool PREV_LAYER_BOUNDARY_OFFSET, bool SIGNED_DISTANCE, bool CURVATURE = true,
         typename POINTS, typename L, typename KNOWN_DISTANCES = NoKnownDistances>
std::vector<ExtendedPoint<L::Dim>> estimate_points_properties(const POINTS&                           input_points,
                                                              const AABBTreeLines::LinesDistancer<L>& unscaled_prev_layer,
                                                              float                                   flow_width,
                                                              float                                   max_line_length = -1.0f,
                                                              float                                   min_distance    = -1.0f,


                                                              const std::function<float(float)>&      distance_to_speed = {},


                                                              float                                   fan_overlap_threshold = -1.0f,

                                                              const KNOWN_DISTANCES&                  known_distance = KNOWN_DISTANCES{})
{
    bool   looped     = input_points.front() == input_points.back();
    std::function<size_t(size_t,size_t)> get_prev_index = [](size_t idx, size_t count) {
        if (idx > 0) {
            return idx - 1;
        } else
            return idx;
    };
    if (looped) {
        get_prev_index = [](size_t idx, size_t count) {
            if (idx == 0)
                idx = count;
            return --idx;
        };
    };
    std::function<size_t(size_t,size_t)> get_next_index = [](size_t idx, size_t size) {
        if (idx + 1 < size) {
            return idx + 1;
        } else
            return idx;
    };
    if (looped) {
        get_next_index = [](size_t idx, size_t count) {
            if (++idx == count)
                idx = 0;
            return idx;
        };
    };

    using P = typename POINTS::value_type;





    double min_spacing = flow_width*0.25;

    using AABBScalar = typename AABBTreeLines::LinesDistancer<L>::Scalar;
    using Vec        = Eigen::Matrix<double, L::Dim, 1, Eigen::DontAlign>;
    if (input_points.empty())
        return {};
    float boundary_offset = PREV_LAYER_BOUNDARY_OFFSET ? 0.5 * flow_width : 0.0f;
    auto  maybe_unscale   = [](const P& p) -> Vec { return SCALED_INPUT ? unscaled(p) : p.template cast<double>(); };

    using Distance = typename AABBTreeLines::LinesDistancer<L>::Floating;
    auto input_distance = [&unscaled_prev_layer, &known_distance](const P &input, const Vec &position) -> Distance {
        if (const double *known = known_distance(input))
            return Distance(*known);
        auto [distance, nearest_line, x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(
            position.template cast<AABBScalar>());
        return distance;
    };

    std::vector<ExtendedPoint<L::Dim>> points;
    points.reserve(input_points.size() * (ADD_INTERSECTIONS ? 1.5 : 1));

    {
        ExtendedPoint<L::Dim> start_point{maybe_unscale(input_points.front())};
        start_point.distance = input_distance(input_points.front(), start_point.position) + boundary_offset;
        points.push_back(start_point);
    }
    for (size_t i = 1; i < input_points.size(); i++) {
        ExtendedPoint<L::Dim> next_point{maybe_unscale(input_points[i])};
        next_point.distance = input_distance(input_points[i], next_point.position) + boundary_offset;


        if (ADD_INTERSECTIONS &&
            ((points.back().distance > boundary_offset + EPSILON) != (next_point.distance > boundary_offset + EPSILON))) {

            const ExtendedPoint<L::Dim>  prev_point    = points.back();
            auto                         intersections = unscaled_prev_layer.template intersections_with_line<true>(
                L{prev_point.position.template cast<AABBScalar>(), next_point.position.template cast<AABBScalar>()});
            for (const auto &intersection : intersections) {
                ExtendedPoint<L::Dim> p{};
                p.position = intersection.first.template cast<double>();
                p.distance = boundary_offset;

                if ((p.position - prev_point.position).norm() > min_spacing &&
                    (next_point.position - p.position).norm() > min_spacing) {
                    points.push_back(p);
                }
            }
        }
        points.push_back(next_point);
    }





    const float width_inv = 1.f / flow_width;

    auto fan_on = [fan_overlap_threshold, width_inv](float distance) {
        return fan_overlap_threshold >= 0.f && 1.f - distance * width_inv <= fan_overlap_threshold;
    };


    auto same_speed_and_fan = [&distance_to_speed, &fan_on](float a, float b) {
        return std::abs(distance_to_speed(a) - distance_to_speed(b)) <= 1.f && fan_on(a) == fan_on(b);
    };



    auto slower_or_cooled = [&distance_to_speed, &fan_on](float a, float b) {
        return distance_to_speed(a) < distance_to_speed(b) - 1.f || (fan_on(a) && !fan_on(b));
    };










    if (PREV_LAYER_BOUNDARY_OFFSET && ADD_INTERSECTIONS && distance_to_speed && (min_distance > 0 || fan_overlap_threshold >= 0.f)) {



        const double max_probe_spacing = std::max(2., 4. * min_spacing);

        constexpr int max_bisection_depth = 10;


        struct Subspan { double t0, t1; int depth; };

        std::vector<ExtendedPoint<L::Dim>>    sampled_points;
        std::vector<std::pair<double, float>> interior;
        std::vector<Subspan>                  pending;

        for (size_t point_idx = 0; point_idx + 1 < points.size(); ++point_idx) {
            const ExtendedPoint<L::Dim>& curr     = points[point_idx];
            const ExtendedPoint<L::Dim>& next     = points[point_idx + 1];
            const Vec                    step     = next.position - curr.position;
            const double                 line_len = step.norm();

            interior.clear();
            if (line_len >= max_probe_spacing)
                pending.push_back({0., 1., max_bisection_depth});

            while (!pending.empty()) {
                const Subspan subspan = pending.back();
                pending.pop_back();
                if (subspan.depth <= 0 || (subspan.t1 - subspan.t0) * line_len < max_probe_spacing)
                    continue;

                const double t = 0.5 * (subspan.t0 + subspan.t1);
                auto [distance, nearest_line, x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(
                    (curr.position + t * step).template cast<AABBScalar>());
                const float sampled = float(distance + boundary_offset);

                interior.emplace_back(t, sampled);
                pending.push_back({subspan.t0, t, subspan.depth - 1});
                pending.push_back({t, subspan.t1, subspan.depth - 1});
            }

            if (!interior.empty()) {
                std::sort(interior.begin(), interior.end(),
                          [](const std::pair<double, float>& l, const std::pair<double, float>& r) { return l.first < r.first; });



                size_t kept = 0;
                for (size_t i = 0; i < interior.size(); ++i) {
                    const float sample    = interior[i].second;
                    const bool  at_start  = kept == 0;
                    const bool  at_end    = i + 1 == interior.size();
                    const float before    = at_start ? curr.distance : interior[kept - 1].second;
                    const float after     = at_end ? next.distance : interior[i + 1].second;







                    const bool worth_before = !same_speed_and_fan(sample, before) && (!at_start || slower_or_cooled(sample, before));
                    const bool worth_after  = !same_speed_and_fan(sample, after) && (!at_end || slower_or_cooled(sample, after));
                    if (worth_before || worth_after)
                        interior[kept++] = interior[i];
                }
                interior.resize(kept);
            }

            if (!interior.empty() && sampled_points.empty()) {
                sampled_points.reserve(points.size() + 8);
                sampled_points.assign(points.begin(), points.begin() + point_idx + 1);
            }
            if (!sampled_points.empty()) {


                for (const auto& [t, distance] : interior)
                    sampled_points.push_back({curr.position + t * step, distance});
                sampled_points.push_back(next);
            }
        }
        if (!sampled_points.empty())
            points = std::move(sampled_points);
    }


    if (PREV_LAYER_BOUNDARY_OFFSET && ADD_INTERSECTIONS) {
        std::vector<ExtendedPoint<L::Dim>> new_points;
        new_points.reserve(points.size() * 2);
        new_points.push_back(points.front());
        for (int point_idx = 0; point_idx < int(points.size()) - 1; ++point_idx) {
            const ExtendedPoint<L::Dim>& curr = points[point_idx];
            const ExtendedPoint<L::Dim>& next = points[point_idx + 1];

            if ((curr.distance > -boundary_offset && curr.distance < boundary_offset + 2.0f) ||
                (next.distance > -boundary_offset && next.distance < boundary_offset + 2.0f)) {
                double line_len = (next.position - curr.position).norm();








                const bool split_line = distance_to_speed ?
                    line_len >= 2.f && (slower_or_cooled(curr.distance, 0.f) || slower_or_cooled(next.distance, 0.f)) :
                    line_len > 4.0f;
                if (split_line) {


                    double a0 = std::clamp((curr.distance + 3 * boundary_offset) / line_len, 0.0, 1.0);
                    double a1 = std::clamp(1.0f - (next.distance + 3 * boundary_offset) / line_len, 0.0, 1.0);
                    double t0 = std::min(a0, a1);
                    double t1 = std::max(a0, a1);





                    ExtendedPoint<L::Dim> cut[2]{};
                    bool                  keep[2] = {false, false};
                    for (int k = 0; k < 2; ++k) {
                        const double t = k == 0 ? t0 : t1;
                        if (k == 0 ? t >= 1.0 : t <= 0.0)
                            continue;
                        const Vec p                  = curr.position + t * (next.position - curr.position);
                        auto [p_dist, p_near_l, p_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(
                            p.template cast<AABBScalar>());
                        cut[k].position = p;
                        cut[k].distance = float(p_dist + boundary_offset);
                        keep[k] = (p - curr.position).norm() > min_spacing && (next.position - p).norm() > min_spacing;
                    }
                    if (distance_to_speed) {



                        if (keep[0])
                            keep[0] = !same_speed_and_fan(cut[0].distance, curr.distance) ||
                                      !same_speed_and_fan(cut[0].distance, keep[1] ? cut[1].distance : next.distance);
                        if (keep[1])
                            keep[1] = !same_speed_and_fan(cut[1].distance, keep[0] ? cut[0].distance : curr.distance) ||
                                      !same_speed_and_fan(cut[1].distance, next.distance);


                        if (keep[0] && keep[1] && (cut[1].position - cut[0].position).norm() <= min_spacing)
                            keep[1] = false;
                    }
                    for (int k = 0; k < 2; ++k)
                        if (keep[k])
                            new_points.push_back(cut[k]);
                }
            }
            new_points.push_back(next);
        }
        points = std::move(new_points);
    }


    if (max_line_length > 0) {
        std::vector<ExtendedPoint<L::Dim>> new_points;
        new_points.reserve(points.size() * 2);
        {
            for (size_t i = 0; i + 1 < points.size(); i++) {
                const ExtendedPoint<L::Dim>& curr = points[i];
                const ExtendedPoint<L::Dim>& next = points[i + 1];
                new_points.push_back(curr);
                double len             = (next.position - curr.position).squaredNorm();
                double t               = sqrt((max_line_length * max_line_length) / len);
                size_t new_point_count = 1.0 / t;
                for (size_t j = 1; j < new_point_count + 1; j++) {
                    Vec pos    = curr.position * (1.0 - j * t) + next.position * (j * t);
                    auto [p_dist, p_near_l,
                          p_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(pos.template cast<AABBScalar>());
                    ExtendedPoint<L::Dim> new_p{};
                    new_p.position = pos;
                    new_p.distance = float(p_dist + boundary_offset);


                    if ((pos - curr.position).norm() > min_spacing && (next.position - pos).norm() > min_spacing) {
                        new_points.push_back(new_p);
                    }
                }
            }
            new_points.push_back(points.back());
        }
        points = std::move(new_points);
    }

    if constexpr (!CURVATURE)
        return points;


    float accumulated_distance = 0;
    std::vector<float> distances_for_curvature(points.size());
    for (size_t point_idx = 0; point_idx < points.size(); ++point_idx) {
        const ExtendedPoint<L::Dim>& a = points[point_idx];
        const ExtendedPoint<L::Dim>& b = points[get_prev_index(point_idx, points.size())];

        distances_for_curvature[point_idx] = (b.position - a.position).norm();
        accumulated_distance += distances_for_curvature[point_idx];
    }

    if (accumulated_distance > EPSILON)
        for (float window_size : {3.0f, 9.0f, 16.0f}) {
            for (int point_idx = 0; point_idx < int(points.size()); ++point_idx) {
                ExtendedPoint<L::Dim>& current = points[point_idx];

                Vec back_position = current.position;
                {
                    size_t back_point_index = point_idx;
                    float  dist_backwards   = 0;
                    while (dist_backwards < window_size * 0.5 && back_point_index != get_prev_index(back_point_index, points.size())) {
                        float line_dist = distances_for_curvature[get_prev_index(back_point_index, points.size())];
                        if (dist_backwards + line_dist > window_size * 0.5) {
                            back_position = points[back_point_index].position +
                                            (window_size * 0.5 - dist_backwards) *
                                                (points[get_prev_index(back_point_index, points.size())].position -
                                                 points[back_point_index].position)
                                                    .normalized();
                            dist_backwards += window_size * 0.5 - dist_backwards + EPSILON;
                        } else {
                            dist_backwards += line_dist;
                            back_point_index = get_prev_index(back_point_index, points.size());
                        }
                    }
                }

                Vec front_position = current.position;
                {
                    size_t front_point_index = point_idx;
                    float  dist_forwards     = 0;
                    while (dist_forwards < window_size * 0.5 && front_point_index != get_next_index(front_point_index, points.size())) {
                        float line_dist = distances_for_curvature[front_point_index];
                        if (dist_forwards + line_dist > window_size * 0.5) {
                            front_position = points[front_point_index].position +
                                             (window_size * 0.5 - dist_forwards) *
                                                 (points[get_next_index(front_point_index, points.size())].position -
                                                  points[front_point_index].position)
                                                     .normalized();
                            dist_forwards += window_size * 0.5 - dist_forwards + EPSILON;
                        } else {
                            dist_forwards += line_dist;
                            front_point_index = get_next_index(front_point_index, points.size());
                        }
                    }
                }

                float new_curvature = angle((current.position - back_position).template head<2>(),
                                            (front_position - current.position).template head<2>()) /
                                      window_size;
                if (abs(current.curvature) < abs(new_curvature)) {
                    current.curvature = new_curvature;
                }
            }
        }

    return points;
}

struct Point3Hash
{
    size_t operator()(const Vec3crd &pt) const noexcept { return size_t(((89 * 31 + int64_t(pt.x())) * 31 + pt.y()) * 31 + pt.z()); }
};



struct PrecomputedOverhangLayer
{
    const PrintObject                                               *object{nullptr};
    const Layer                                                     *layer{nullptr};
    std::shared_ptr<const AABBTreeLines::LinesDistancer<Linef3>>     lower_boundaries;
    std::shared_ptr<const AABBTreeLines::LinesDistancer<CurledLine>> lower_curled_lines;
    std::unordered_map<Point3, double, Point3Hash>                   distances;
};



inline PrecomputedOverhangLayer precompute_overhang_layer(const PrintObject *object, const Layer &layer, bool curled_lines = true)
{
    PrecomputedOverhangLayer out{object, &layer,
                                 std::make_shared<const AABBTreeLines::LinesDistancer<Linef3>>(to_unscaled_linesf3(layer.lower_layer->lslices)),
                                 curled_lines ? std::make_shared<const AABBTreeLines::LinesDistancer<CurledLine>>(layer.lower_layer->curled_lines) :
                                                nullptr,
                                 {}};
    const AABBTreeLines::LinesDistancer<Linef3> &lower = *out.lower_boundaries;
    auto add_path = [&out, &lower](const ExtrusionPath &path) {
        if (!is_bridge(path.role()) && !is_perimeter(path.role()))
            return;
        for (const Point3 &point : path.polyline.points)
            if (auto [it, inserted] = out.distances.try_emplace(point, 0.); inserted) {
                const Eigen::Matrix<double, 3, 1, Eigen::DontAlign> position = unscaled(point);
                auto [distance, nearest_line, x] = lower.distance_from_lines_extra<true>(position.cast<double>());
                it->second = distance;
            }
    };
    for (const LayerRegion *region : layer.regions()) {
        for_each_extrusion_path(region->perimeters, add_path);
        for_each_extrusion_path(region->fills, add_path);
    }
    return out;
}

struct ProcessedPoint
{
    Point3 p;
    float speed = 1.0f;
    float overlap = 1.0f;
};




struct OverhangSpeedSections
{

    static ConfigOptionPercents overlap_levels() { return ConfigOptionPercents({90, 75, 50, 25, 13, 0}); }


    std::vector<std::pair<float, float>> sections;

    OverhangSpeedSections(const ConfigOptionPercents &overlaps, const ConfigOptionFloatsOrPercents &speeds, float ext_perimeter_speed,
                          float width)
    {
        const size_t count = std::min(overlaps.values.size(), speeds.values.size());
        for (size_t i = 0; i < count; i++) {
            float distance = width * (1.0 - (overlaps.get_at(i) / 100.0));
            float speed    = speeds.get_at(i).percent ? (ext_perimeter_speed * speeds.get_at(i).value / 100.0) : speeds.get_at(i).value;
            sections.push_back({distance, speed});
        }
        std::sort(sections.begin(), sections.end(), [](const std::pair<float, float> &a, const std::pair<float, float> &b) {
            if (a.first == b.first)
                return a.second > b.second;
            return a.first < b.first;
        });
        std::pair<float, float> last_section{INFINITY, 0};
        for (auto &section : sections) {
            if (section.first == last_section.first)
                section.second = last_section.second;
            else
                last_section = section;
        }
    }


    float slowdown_from(float original_speed) const
    {
        float smallest = std::numeric_limits<float>::infinity();
        bool  found    = false;
        for (const auto &section : sections)
            if (section.second <= original_speed && section.first < smallest) {
                smallest = section.first;
                found    = true;
            }
        return found ? smallest : -1.f;
    }


    float speed(float distance, float original_speed) const
    {
        float final_speed;
        if (distance <= sections.front().first) {
            final_speed = original_speed;
        } else if (distance >= sections.back().first) {
            final_speed = sections.back().second;
        } else {
            size_t section_idx = 0;
            while (distance > sections[section_idx + 1].first)
                section_idx++;
            float t = (distance - sections[section_idx].first) / (sections[section_idx + 1].first - sections[section_idx].first);
            t           = std::clamp(t, 0.0f, 1.0f);
            final_speed = (1.0f - t) * sections[section_idx].second + t * sections[section_idx + 1].second;
        }
        return round(final_speed);
    }
};

class ExtrusionQualityEstimator
{
    using Boundaries   = AABBTreeLines::LinesDistancer<Linef3>;
    using CurledLines  = AABBTreeLines::LinesDistancer<CurledLine>;
    std::unordered_map<const PrintObject*, std::shared_ptr<const Boundaries>>  prev_layer_boundaries;
    std::unordered_map<const PrintObject*, std::shared_ptr<const CurledLines>> prev_curled_extrusions;

    std::unordered_map<const PrintObject*, const Layer*>                      prev_layer_sources;
    std::unordered_map<const PrintObject*, const Layer*>                      last_prepared_layers;
    std::vector<PrecomputedOverhangLayer>                                     precomputed_layers;
    const PrintObject                                                        *current_object;

    const PrecomputedOverhangLayer *precomputed_for(const PrintObject *object) const
    {
        auto it = std::find_if(precomputed_layers.begin(), precomputed_layers.end(),
                               [object](const PrecomputedOverhangLayer &layer) { return layer.object == object; });
        return it == precomputed_layers.end() ? nullptr : &*it;
    }

    template<typename T> static const T &or_empty(const std::shared_ptr<const T> &tree)
    {
        static const T empty;
        return tree ? *tree : empty;
    }

public:
    void set_current_object(const PrintObject *object) { current_object = object; }


    void set_precomputed_layers(std::vector<PrecomputedOverhangLayer> &&layers) { precomputed_layers = std::move(layers); }


    void prepare_for_new_layer(const PrintObject * obj, const Layer *layer)
    {
        if (layer == nullptr) return;
        const PrintObject *object = obj;
        const Layer *prev = std::exchange(last_prepared_layers[object], layer);
        prev_layer_sources[object] = prev;
        const PrecomputedOverhangLayer *precomputed = precomputed_for(object);
        if (prev == nullptr) {
            prev_layer_boundaries[object]  = nullptr;
            prev_curled_extrusions[object] = nullptr;
        } else if (precomputed != nullptr && precomputed->layer == layer && layer->lower_layer == prev) {
            prev_layer_boundaries[object]  = precomputed->lower_boundaries;
            prev_curled_extrusions[object] = precomputed->lower_curled_lines;
        } else {
            prev_layer_boundaries[object]  = std::make_shared<const Boundaries>(to_unscaled_linesf3(prev->lslices));
            prev_curled_extrusions[object] = std::make_shared<const CurledLines>(prev->curled_lines);
        }
    }

    std::vector<ProcessedPoint> estimate_extrusion_quality(const ExtrusionPath                &path,
                                                           const ConfigOptionPercents         &overlaps,
                                                           const ConfigOptionFloatsOrPercents &speeds,
                                                           float                               ext_perimeter_speed,
                                                           float                               original_speed,
                                                           bool								   slowdown_for_curled_edges,


                                                           float                               fan_overlap_threshold = -1.0f)
    {
        const OverhangSpeedSections speed_sections(overlaps, speeds, ext_perimeter_speed, path.width);


        const float smallest_distance_with_lower_speed = speed_sections.slowdown_from(original_speed);


        auto calculate_speed = [&speed_sections, &original_speed](float distance) { return speed_sections.speed(distance, original_speed); };





        auto effective_speed = [&calculate_speed, original_speed](float distance) {
            return std::min(calculate_speed(distance), original_speed);
        };


        const std::unordered_map<Point3, double, Point3Hash> *known = nullptr;
        if (const PrecomputedOverhangLayer *precomputed = precomputed_for(current_object);
            precomputed != nullptr && precomputed->layer->lower_layer == prev_layer_sources[current_object])
            known = &precomputed->distances;
        const Boundaries  &prev_boundaries = or_empty(prev_layer_boundaries[current_object]);
        const CurledLines &prev_curled     = or_empty(prev_curled_extrusions[current_object]);
        auto known_distance = [known](const Point3 &point) -> const double * {
            if (known == nullptr)
                return nullptr;
            auto it = known->find(point);
            return it == known->end() ? nullptr : &it->second;
        };

        std::vector<ExtendedPoint<3>> extended_points =
            estimate_points_properties<true, true, true, true, false>(path.polyline.points, prev_boundaries, path.width, -1,
                                                               smallest_distance_with_lower_speed, effective_speed, fan_overlap_threshold,
                                                               known_distance);
        const auto width_inv = 1.0f / path.width;
        std::vector<ProcessedPoint> processed_points;
        processed_points.reserve(extended_points.size());
        for (size_t i = 0; i < extended_points.size(); i++) {
            const ExtendedPoint<3>& curr = extended_points[i];
            const ExtendedPoint<3>& next = extended_points[i + 1 < extended_points.size() ? i + 1 : i];

            float artificial_distance_to_curled_lines = 0.0;
            if(slowdown_for_curled_edges) {

                const double dist_limit = 10.0 * path.width;
				{
                    Vec3d middle       = 0.5 * (curr.position + next.position);
                    auto  line_indices = prev_curled.all_lines_in_radius(Point::new_scale(middle),
                                                                                                    scale_(dist_limit));
                    if (!line_indices.empty()) {
                        double len = (next.position - curr.position).norm();




                        if (len > 2) {
                            Vec2d dir   = Vec2d(next.position.head<2>() - curr.position.head<2>()) / len;
                            Vec2d right = Vec2d(-dir.y(), dir.x());

                            Polygon box_of_influence = {
                                scaled(Vec2d(curr.position.head<2>() + right * dist_limit)),
                                scaled(Vec2d(next.position.head<2>() + right * dist_limit)),
                                scaled(Vec2d(next.position.head<2>() - right * dist_limit)),
                                scaled(Vec2d(curr.position.head<2>() - right * dist_limit)),
                            };

                            double projected_lengths_sum = 0;
                            for (size_t idx : line_indices) {
                                const CurledLine& line   = prev_curled.get_line(idx);
                                Lines             inside = intersection_ln({{line.a, line.b}}, {box_of_influence});
                                if (inside.empty())
                                    continue;
                                double projected_length = abs(dir.dot(unscaled(Vec2d((inside.back().b - inside.back().a).cast<double>()))));
                                projected_lengths_sum += projected_length;
                            }
                            if (projected_lengths_sum < 0.4 * len) {
                                line_indices.clear();
                            }
                        }

                        for (size_t idx : line_indices) {
                            const CurledLine &line                 = prev_curled.get_line(idx);
                            float             distance_from_curled = unscaled(line_alg::distance_to(line, Point::new_scale(middle)));
                            float             dist                 = path.width * (1.0 - (distance_from_curled / dist_limit)) *
                                     (1.0 - (distance_from_curled / dist_limit)) *
                                     (line.curled_height / (path.height * 10.0f));
                            artificial_distance_to_curled_lines = std::max(artificial_distance_to_curled_lines, dist);
                        }
                    }
                }
            }

            float extrusion_speed = std::min(calculate_speed(curr.distance), calculate_speed(next.distance));


            extrusion_speed = std::min(extrusion_speed, original_speed);

            if(slowdown_for_curled_edges) {
                float curled_speed = calculate_speed(artificial_distance_to_curled_lines);
                extrusion_speed       = std::min(curled_speed, extrusion_speed);
            }

            float overlap = std::min(1 - (curr.distance+artificial_distance_to_curled_lines) * width_inv, 1 - (next.distance+artificial_distance_to_curled_lines) * width_inv);

            processed_points.push_back({Point3(scaled(curr.position)), extrusion_speed, overlap});
        }
        return processed_points;
    }
};

}

#endif
