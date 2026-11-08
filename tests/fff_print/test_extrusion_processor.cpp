#include <catch2/catch_all.hpp>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/ExtrusionProcessor.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include <cstddef>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <catch2/interfaces/catch_interfaces_capture.hpp>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

constexpr double caged_layer_height     = 0.2;
constexpr double caged_wall_width       = 0.42;
constexpr double caged_outer_wall_speed = 200.;
constexpr double caged_slow_speed       = 100.;
constexpr double dished_wall_gap     = 0.2;
constexpr double dished_layer_depth  = 0.03;
constexpr double dished_min_distance = 0.042;
constexpr float  dished_end_reading  = float(dished_wall_gap + 0.5 * caged_wall_width);
constexpr float  dished_mid_reading  = float(dished_end_reading + dished_layer_depth);
constexpr double dished_reading_tolerance = 0.5 * dished_layer_depth;

TriangleMesh caged_overhang_mesh()
{
    return TriangleMesh(
        {
            {5.0859987f, 10.167065f, 5.711731f}, {34.914257f, 10.167065f, 5.711731f},
            {34.914257f, 0.f, 15.878796f},       {5.0859995f, 0.f, 15.878796f},
            {0.f, 0.f, 0.f},                      {0.f, 0.f, 20.f},
            {0.f, 20.f, 20.f},                    {0.f, 20.f, 0.f},
            {40.f, 20.f, 20.f},                   {40.f, 20.f, 0.f},
            {40.f, 0.f, 20.f},                    {40.f, 0.f, 0.f},
            {34.914257f, 0.f, 0.f},               {5.0859995f, 0.f, 0.f},
            {34.914257f, 10.167065f, 0.f},        {5.0859995f, 10.167065f, 0.f},
        },
        {
            {0, 1, 2},   {0, 2, 3},   {4, 5, 6},   {4, 6, 7},   {7, 6, 8},   {7, 8, 9},
            {9, 8, 10},  {9, 10, 11}, {12, 11, 10}, {5, 4, 13}, {5, 13, 3},  {2, 12, 10},
            {5, 3, 2},   {10, 5, 2},  {9, 11, 12}, {9, 12, 14}, {13, 4, 7},  {9, 14, 15},
            {15, 13, 7}, {7, 9, 15},  {8, 6, 5},   {8, 5, 10},  {14, 1, 0},  {14, 0, 15},
            {2, 1, 14},  {2, 14, 12}, {15, 0, 3},  {15, 3, 13},
        });
}

constexpr double caged_box_depth      = 20.;
constexpr double caged_slope_face_sum = 15.878796;
constexpr double caged_slope_x_min = 5.0859995;
constexpr double caged_slope_x_max = 34.914257;
constexpr double caged_slope_span  = caged_slope_x_max - caged_slope_x_min;
constexpr double caged_slope_z_min = 5.711731;
constexpr double caged_slope_z_max = 15.878796;
constexpr double caged_span_z_min = caged_slope_z_min + caged_layer_height;

constexpr double caged_slope_wall_sum = caged_slope_face_sum + 0.5 * caged_layer_height + 0.5 * caged_wall_width;
constexpr double caged_back_wall_y = caged_box_depth - 0.5 * caged_wall_width;
constexpr double caged_front_wall_y = 0.5 * caged_wall_width;
constexpr double caged_wall_tolerance = 0.5 * caged_wall_width;

template<typename KeepLine> std::vector<double> outer_wall_feed_rates(const std::string& gcode, KeepLine keep_line)
{
    std::vector<double> feed_rates;
    bool outer_wall = false;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&feed_rates, &outer_wall, &keep_line](GCodeReader& self, const GCodeReader::GCodeLine& line) {
        const std::string_view comment = line.comment();
        if (comment.find("FEATURE:") != std::string_view::npos || comment.find("TYPE:") != std::string_view::npos)
            outer_wall = comment.find("Outer wall") != std::string_view::npos ||
                         comment.find("External perimeter") != std::string_view::npos;

        if (outer_wall && line.extruding(self) && line.dist_XY(self) > 1.0 && keep_line(self, line))
            feed_rates.push_back(line.new_F(self));
    });

    return feed_rates;
}

std::vector<double> caged_slope_feed_rates(const std::string& gcode)
{
    return outer_wall_feed_rates(gcode, [](const GCodeReader& self, const GCodeReader::GCodeLine& line) {
        const double z = line.new_Z(self);
        return z > caged_span_z_min && z < caged_slope_z_max &&
               line.dist_XY(self) > 0.5 * caged_slope_span &&
               std::abs(self.y() + z - caged_slope_wall_sum) < caged_wall_tolerance &&
               std::abs(line.new_Y(self) + z - caged_slope_wall_sum) < caged_wall_tolerance;
    });
}

std::vector<double> back_wall_feed_rates(const std::string& gcode)
{
    return outer_wall_feed_rates(gcode, [](const GCodeReader& self, const GCodeReader::GCodeLine& line) {
        return line.new_Z(self) > 1.5 * caged_layer_height &&
               std::abs(self.y() - caged_back_wall_y) < caged_wall_tolerance &&
               std::abs(line.new_Y(self) - caged_back_wall_y) < caged_wall_tolerance;
    });
}

const double caged_layer_above_slope_z = std::ceil(caged_slope_z_max / caged_layer_height) * caged_layer_height;

std::vector<double> cage_shoulder_feed_rates(const std::string& gcode)
{
    return outer_wall_feed_rates(gcode, [](const GCodeReader& self, const GCodeReader::GCodeLine& line) {
        return std::abs(line.new_Z(self) - caged_layer_above_slope_z) < 0.5 * caged_layer_height &&
               std::abs(self.y() - caged_front_wall_y) < caged_wall_tolerance &&
               std::abs(line.new_Y(self) - caged_front_wall_y) < caged_wall_tolerance &&
               (std::max(self.x(), line.new_X(self)) <= caged_slope_x_min ||
                std::min(self.x(), line.new_X(self)) >= caged_slope_x_max);
    });
}

std::vector<ExtendedPoint<2>> sampled_wall_over_dished_layer(const std::function<float(float)>& distance_to_speed)
{
    const AABBTreeLines::LinesDistancer<Linef> prev_layer(std::vector<Linef>{
        {{0., 0.}, {20., -dished_layer_depth}},
        {{20., -dished_layer_depth}, {40., 0.}},
        {{40., 0.}, {40., -10.}},
        {{40., -10.}, {0., -10.}},
        {{0., -10.}, {0., 0.}},
    });
    const Points wall{Point::new_scale(0., dished_wall_gap), Point::new_scale(40., dished_wall_gap)};

    return estimate_points_properties<true, true, true, true>(wall, prev_layer, caged_wall_width, -1.f,
                                                              dished_min_distance, distance_to_speed);
}

constexpr double narrow_pocket_wall_length = 40.;
constexpr double narrow_pocket_width       = 2.;
constexpr double narrow_pocket_depth       = 0.3;

std::vector<ExtendedPoint<2>> sampled_wall_over_narrow_pocket(
    double pocket_center, const std::function<float(float)>& distance_to_speed)
{
    const double pocket_left  = pocket_center - 0.5 * narrow_pocket_width;
    const double pocket_right = pocket_center + 0.5 * narrow_pocket_width;
    const AABBTreeLines::LinesDistancer<Linef> prev_layer(std::vector<Linef>{
        {{0., 0.}, {pocket_left, 0.}},
        {{pocket_left, 0.}, {pocket_left, -narrow_pocket_depth}},
        {{pocket_left, -narrow_pocket_depth}, {pocket_right, -narrow_pocket_depth}},
        {{pocket_right, -narrow_pocket_depth}, {pocket_right, 0.}},
        {{pocket_right, 0.}, {narrow_pocket_wall_length, 0.}},
        {{narrow_pocket_wall_length, 0.}, {narrow_pocket_wall_length, -10.}},
        {{narrow_pocket_wall_length, -10.}, {0., -10.}},
        {{0., -10.}, {0., 0.}},
    });
    const double wall_y = -0.5 * caged_wall_width;
    const Points wall{Point::new_scale(0., wall_y), Point::new_scale(narrow_pocket_wall_length, wall_y)};

    return estimate_points_properties<true, true, true, true>(wall, prev_layer, caged_wall_width, -1.f,
                                                               dished_min_distance, distance_to_speed);
}

constexpr double stepped_wall_inset = 0.5 * caged_wall_width;
constexpr double stepped_end_gap    = stepped_wall_inset - caged_layer_height;
constexpr double stepped_wall_span  = 30.;

std::vector<ExtendedPoint<2>> sampled_wall_between_growing_corners(const std::function<float(float)>& distance_to_speed)
{
    const AABBTreeLines::LinesDistancer<Linef> prev_layer(std::vector<Linef>{
        {{0., 0.}, {32., 0.}},
        {{32., 0.}, {32., -stepped_wall_span}},
        {{32., -stepped_wall_span}, {0., -stepped_wall_span}},
        {{0., -stepped_wall_span}, {0., 0.}},
    });
    const Points wall{Point::new_scale(stepped_wall_inset, -stepped_end_gap),
                      Point::new_scale(stepped_wall_inset, stepped_end_gap - stepped_wall_span)};

    return estimate_points_properties<true, true, true, true>(wall, prev_layer, caged_wall_width, -1.f,
                                                              dished_min_distance, distance_to_speed);
}

double slowed_length(const std::vector<ExtendedPoint<2>>& points, const std::function<float(float)>& distance_to_speed)
{
    double length = 0.;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (std::min(distance_to_speed(points[i].distance), distance_to_speed(points[i + 1].distance)) < distance_to_speed(0.f))
            length += (points[i + 1].position - points[i].position).norm();
    return length;
}

float furthest_reading(const std::vector<ExtendedPoint<2>>& points)
{
    return std::max_element(points.begin(), points.end(), [](const ExtendedPoint<2>& l, const ExtendedPoint<2>& r) {
               return l.distance < r.distance;
           })->distance;
}

constexpr double edge_run_length = 64.;
constexpr double edge_step       = 0.384;
constexpr double edge_wall_end_past  = edge_step - 0.5 * caged_wall_width;
constexpr double edge_wall_end_short = 0.05;
constexpr double edge_affected_length = 3. * caged_wall_width;

std::vector<ExtendedPoint<2>> sampled_wall_along_edge(double                              wall_end_x,
                                                      const std::function<float(float)>& distance_to_speed,
                                                      float                               min_distance,
                                                      float                               fan_overlap_threshold)
{
    const AABBTreeLines::LinesDistancer<Linef> prev_layer(std::vector<Linef>{
        {{0., 0.}, {edge_run_length + 10., 0.}},
        {{edge_run_length + 10., 0.}, {edge_run_length + 10., -10.}},
        {{edge_run_length + 10., -10.}, {0., -10.}},
        {{0., -10.}, {0., 0.}},
    });
    const double wall_y = -0.5 * caged_wall_width;
    const Points wall{Point::new_scale(edge_run_length, wall_y), Point::new_scale(wall_end_x, wall_y)};

    return estimate_points_properties<true, true, true, true>(wall, prev_layer, caged_wall_width, -1.f, min_distance,
                                                              distance_to_speed, fan_overlap_threshold);
}

double cooled_length(const std::vector<ExtendedPoint<2>>& points, float fan_overlap_threshold)
{
    double length = 0.;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (1.f - std::max(points[i].distance, points[i + 1].distance) / float(caged_wall_width) <= fan_overlap_threshold)
            length += (points[i + 1].position - points[i].position).norm();
    return length;
}

DynamicPrintConfig caged_overhang_config(const char* wall_generator){
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.4"},
        {"initial_layer_print_height", caged_layer_height},
        {"layer_height", caged_layer_height},
        {"line_width", caged_wall_width},
        {"outer_wall_line_width", caged_wall_width},
        {"inner_wall_line_width", "0.45"},
        {"wall_loops", "2"},
        {"wall_generator", wall_generator},
        {"wall_sequence", "inner wall/outer wall"},
        {"sparse_infill_density", "15%"},
        {"detect_overhang_wall", "1"},
        {"enable_overhang_speed", "1"},
        {"slowdown_for_curled_perimeters", "0"},
        {"zaa_enabled", "0"},
        {"outer_wall_speed", caged_outer_wall_speed},
        {"inner_wall_speed", "300"},
        {"overhang_1_4_speed", "0"},
        {"overhang_2_4_speed", "50"},
        {"overhang_3_4_speed", "30"},
        {"overhang_4_4_speed", "10"},
        {"bridge_speed", "50"},
        {"filament_max_volumetric_speed", "22"},
        {"slow_down_for_layer_cooling", "0"},
        {"slow_down_layers", "0"},
    });
    return config;
}

std::string caged_overhang_gcode(const char* wall_generator)
{
    Print print;
    Model model;
    init_print(std::vector<TriangleMesh>{caged_overhang_mesh()}, print, model, caged_overhang_config(wall_generator), nullptr,
               false);
    return gcode(print);
}

void info_feed_rates(const char* span, const std::vector<double>& feed_rates)
{
    UNSCOPED_INFO("matched " << feed_rates.size() << " " << span << " moves");
    if (!feed_rates.empty()) {
        const auto extremes = std::minmax_element(feed_rates.begin(), feed_rates.end());
        UNSCOPED_INFO("slowest " << *extremes.first / MM_PER_MIN << " mm/s, fastest " << *extremes.second / MM_PER_MIN << " mm/s");
    }
}

}
TEST_CASE("Caged external overhangs are slowed along their span", "[ExtrusionProcessor][Regression]")
{
    const char* wall_generator = GENERATE("classic", "arachne");
    INFO("wall generator: " << wall_generator);

    const std::vector<double> feed_rates = caged_slope_feed_rates(caged_overhang_gcode(wall_generator));
    info_feed_rates("caged slope", feed_rates);

    REQUIRE_FALSE(feed_rates.empty());

    const double fastest = *std::max_element(feed_rates.begin(), feed_rates.end());
    REQUIRE(fastest < caged_slow_speed * MM_PER_MIN);
}

TEST_CASE("Supported vertical walls keep their normal speed", "[ExtrusionProcessor][Regression]")
{
    const char* wall_generator = GENERATE("classic", "arachne");
    INFO("wall generator: " << wall_generator);

    const std::vector<double> feed_rates = back_wall_feed_rates(caged_overhang_gcode(wall_generator));
    info_feed_rates("back wall", feed_rates);

    REQUIRE_FALSE(feed_rates.empty());

    const double slowest = *std::min_element(feed_rates.begin(), feed_rates.end());
    REQUIRE(slowest >= caged_slow_speed * MM_PER_MIN);
}

TEST_CASE("Wall sections beside a caged overhang keep their normal speed", "[ExtrusionProcessor][Regression]")
{
    const char* wall_generator = GENERATE("classic", "arachne");
    INFO("wall generator: " << wall_generator);

    const std::vector<double> feed_rates = cage_shoulder_feed_rates(caged_overhang_gcode(wall_generator));
    info_feed_rates("cage shoulder", feed_rates);

    REQUIRE_FALSE(feed_rates.empty());

    const double slowest = *std::min_element(feed_rates.begin(), feed_rates.end());
    REQUIRE_THAT(slowest / MM_PER_MIN, Catch::Matchers::WithinRel(caged_outer_wall_speed, 0.01));
}

TEST_CASE("An overhang reading is kept whenever it changes the speed", "[ExtrusionProcessor][Regression]")
{
    const std::vector<ExtendedPoint<2>> points =
        sampled_wall_over_dished_layer([](float distance) { return std::round(200.f - 400.f * distance); });

    REQUIRE_THAT(furthest_reading(points), Catch::Matchers::WithinAbs(dished_mid_reading, dished_reading_tolerance));
}

TEST_CASE("An overhang reading is dropped when the speed is unchanged", "[ExtrusionProcessor]")
{
    const std::vector<ExtendedPoint<2>> points = sampled_wall_over_dished_layer([](float) { return 50.f; });

    REQUIRE_THAT(furthest_reading(points), Catch::Matchers::WithinAbs(dished_end_reading, dished_reading_tolerance));
}

TEST_CASE("Coarse probing detects an unsupported pocket away from the wall midpoint",
          "[ExtrusionProcessor][Regression]")
{
    const std::function<float(float)> distance_to_speed = [](float distance) { return distance <= 0.2f ? 100.f : 50.f; };
    const std::vector<ExtendedPoint<2>> points =
        sampled_wall_over_narrow_pocket(0.25 * narrow_pocket_wall_length, distance_to_speed);
    const double slowed = slowed_length(points, distance_to_speed);

    REQUIRE(slowed > 0.);
    REQUIRE(slowed < 5.);
}

TEST_CASE("Coarse probing brackets a narrow slowdown at the wall midpoint",
          "[ExtrusionProcessor][Regression]")
{
    const std::function<float(float)> distance_to_speed = [](float distance) { return distance <= 0.2f ? 100.f : 50.f; };
    const std::vector<ExtendedPoint<2>> points =
        sampled_wall_over_narrow_pocket(0.5 * narrow_pocket_wall_length, distance_to_speed);
    const double slowed = slowed_length(points, distance_to_speed);

    REQUIRE(slowed > 0.);
    REQUIRE(slowed < 5.);
}

TEST_CASE("A supported wall between overhanging corners is slowed no further than its ends require",
          "[ExtrusionProcessor][Regression]")
{
    const std::function<float(float)> distance_to_speed = [](float distance) {
        return std::round(float(caged_outer_wall_speed) - 400.f * distance);
    };

    const double sampled   = slowed_length(sampled_wall_between_growing_corners(distance_to_speed), distance_to_speed);
    const double unsampled = slowed_length(sampled_wall_between_growing_corners({}), distance_to_speed);

    REQUIRE(unsampled > 0.);
    REQUIRE(sampled <= unsampled);
}

TEST_CASE("A wall stepping past the previous layer is slowed and cooled only beside the step", "[ExtrusionProcessor][Regression]")
{
    const float crossing_reading = 0.5f * float(caged_wall_width);
    const std::function<float(float)> distance_to_speed = [crossing_reading](float distance) {
        return distance < crossing_reading ? 70.f : 15.f;
    };
    const float fan_overlap_threshold = 0.75f;

    const std::vector<ExtendedPoint<2>> points = sampled_wall_along_edge(-edge_wall_end_past, distance_to_speed, crossing_reading,
                                                                         fan_overlap_threshold);
    const double slowed = slowed_length(points, distance_to_speed);
    const double cooled = cooled_length(points, fan_overlap_threshold);

    REQUIRE(slowed > 0.);
    REQUIRE(cooled > 0.);
    REQUIRE(slowed < edge_affected_length);
    REQUIRE(cooled < edge_affected_length);
}

TEST_CASE("A wall is split where only the overhang fan changes", "[ExtrusionProcessor][Regression]")
{
    const float crossing_reading = 0.5f * float(caged_wall_width);
    const std::function<float(float)> distance_to_speed = [crossing_reading](float distance) {
        return distance < crossing_reading ? 70.f : 15.f;
    };
    const float fan_overlap_threshold = 0.75f;

    const std::vector<ExtendedPoint<2>> points = sampled_wall_along_edge(edge_wall_end_short, distance_to_speed, crossing_reading,
                                                                         fan_overlap_threshold);
    const double cooled = cooled_length(points, fan_overlap_threshold);

    REQUIRE_THAT(slowed_length(points, distance_to_speed), Catch::Matchers::WithinAbs(0., 1e-9));
    REQUIRE(cooled > 0.);
    REQUIRE(cooled < edge_affected_length);
}

TEST_CASE("A wall is left whole where neither its speed nor its cooling changes", "[ExtrusionProcessor]")
{
    const std::function<float(float)> distance_to_speed = [](float) { return 70.f; };
    const float fan_overlap_threshold = 0.05f;

    const std::vector<ExtendedPoint<2>> points = sampled_wall_along_edge(-edge_wall_end_past, distance_to_speed, -1.f,
                                                                         fan_overlap_threshold);

    REQUIRE(points.size() == 3);
}

namespace {

struct SlicedCage
{
    Print              print;
    Model              model;
    const PrintObject *object{nullptr};
    const Layer       *layer{nullptr};

    explicit SlicedCage(const DynamicPrintConfig &config = caged_overhang_config("classic"))
    {
        init_print(std::vector<TriangleMesh>{caged_overhang_mesh()}, print, model, config, nullptr, false);
        print.process();
        object = print.objects().front();
        layer  = object->get_layer(int(std::lround((caged_slope_z_min + caged_slope_z_max) / 2. / caged_layer_height)));
    }
};

using Walls = std::vector<std::vector<ProcessedPoint>>;

bool has_curled_lines(const PrintObject &object)
{
    const auto layers = object.layers();
    return std::any_of(layers.begin(), layers.end(), [](const Layer *layer) { return !layer->curled_lines.empty(); });
}

Walls estimate_walls(ExtrusionQualityEstimator &estimator, const PrintObject *object, const Layer &layer)
{
    const ConfigOptionPercents         overlaps({90, 75, 50, 25, 13, 0});
    const ConfigOptionFloatsOrPercents speeds({FloatOrPercent{100, true}, FloatOrPercent{50, true}, FloatOrPercent{30, true},
                                               FloatOrPercent{20, true}, FloatOrPercent{10, true}, FloatOrPercent{5, true}});
    Walls walls;
    estimator.set_current_object(object);
    for (const LayerRegion *region : layer.regions())
        for_each_extrusion_path(region->perimeters, [&](const ExtrusionPath &path) {
            if (is_perimeter(path.role()))
                walls.push_back(estimator.estimate_extrusion_quality(path, overlaps, speeds, caged_outer_wall_speed, caged_outer_wall_speed,
                                                                     true, 0.5f));
        });
    return walls;
}

uint32_t float_bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool same_point(const ProcessedPoint &a, const ProcessedPoint &b)
{
    return a.p == b.p && float_bits(a.speed) == float_bits(b.speed) && float_bits(a.overlap) == float_bits(b.overlap);
}

void check_identical(const Walls &actual, const Walls &expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t wall = 0; wall < actual.size(); ++wall) {
        INFO("wall " << wall);
        REQUIRE(actual[wall].size() == expected[wall].size());
        for (size_t i = 0; i < actual[wall].size(); ++i) {
            const ProcessedPoint &a = actual[wall][i];
            const ProcessedPoint &e = expected[wall][i];
            INFO("point " << i << ": speed " << a.speed << " vs " << e.speed << ", overlap " << a.overlap << " vs " << e.overlap);
            CHECK(a.p == e.p);
            CHECK(float_bits(a.speed) == float_bits(e.speed));
            CHECK(float_bits(a.overlap) == float_bits(e.overlap));
        }
    }
}

bool any_difference(const Walls &a, const Walls &b)
{
    return !std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const std::vector<ProcessedPoint> &wa, const std::vector<ProcessedPoint> &wb) {
        return std::equal(wa.begin(), wa.end(), wb.begin(), wb.end(), same_point);
    });
}

bool any_slowed(const Walls &walls)
{
    return std::any_of(walls.begin(), walls.end(), [](const std::vector<ProcessedPoint> &wall) {
        return std::any_of(wall.begin(), wall.end(), [](const ProcessedPoint &point) { return point.speed < caged_outer_wall_speed; });
    });
}

}

TEST_CASE("Overhang data computed ahead of the generator gives the same wall speeds", "[ExtrusionProcessor]")
{
    const SlicedCage cage;
    REQUIRE(cage.layer->lower_layer != nullptr);

    ExtrusionQualityEstimator queried;
    queried.prepare_for_new_layer(cage.object, cage.layer->lower_layer);
    queried.prepare_for_new_layer(cage.object, cage.layer);
    const Walls expected = estimate_walls(queried, cage.object, *cage.layer);
    REQUIRE(any_slowed(expected));

    ExtrusionQualityEstimator precomputed;
    precomputed.prepare_for_new_layer(cage.object, cage.layer->lower_layer);
    precomputed.set_precomputed_layers({precompute_overhang_layer(cage.object, *cage.layer)});
    precomputed.prepare_for_new_layer(cage.object, cage.layer);
    check_identical(estimate_walls(precomputed, cage.object, *cage.layer), expected);
}

TEST_CASE("Overhang distances measured against another layer than the previous one are not used", "[ExtrusionProcessor]")
{
    const SlicedCage cage;
    const Layer     *two_below = cage.layer->lower_layer->lower_layer;
    REQUIRE(two_below != nullptr);

    ExtrusionQualityEstimator queried;
    queried.prepare_for_new_layer(cage.object, two_below);
    queried.prepare_for_new_layer(cage.object, cage.layer);
    const Walls expected = estimate_walls(queried, cage.object, *cage.layer);
    ExtrusionQualityEstimator one_below;
    one_below.prepare_for_new_layer(cage.object, cage.layer->lower_layer);
    one_below.prepare_for_new_layer(cage.object, cage.layer);
    REQUIRE(any_difference(estimate_walls(one_below, cage.object, *cage.layer), expected));

    ExtrusionQualityEstimator precomputed;
    precomputed.prepare_for_new_layer(cage.object, two_below);
    precomputed.set_precomputed_layers({precompute_overhang_layer(cage.object, *cage.layer)});
    precomputed.prepare_for_new_layer(cage.object, cage.layer);
    check_identical(estimate_walls(precomputed, cage.object, *cage.layer), expected);
}

TEST_CASE("Overhang data computed for another layer is not used", "[ExtrusionProcessor]")
{
    const SlicedCage cage;
    const Layer     *one_below = cage.layer->lower_layer;
    REQUIRE(one_below != nullptr);
    REQUIRE(one_below->lower_layer != nullptr);

    ExtrusionQualityEstimator queried;
    queried.prepare_for_new_layer(cage.object, one_below);
    queried.prepare_for_new_layer(cage.object, cage.layer);
    const Walls expected = estimate_walls(queried, cage.object, *cage.layer);
    ExtrusionQualityEstimator two_below;
    two_below.prepare_for_new_layer(cage.object, one_below->lower_layer);
    two_below.prepare_for_new_layer(cage.object, cage.layer);
    REQUIRE(any_difference(estimate_walls(two_below, cage.object, *cage.layer), expected));

    ExtrusionQualityEstimator precomputed;
    precomputed.set_precomputed_layers({precompute_overhang_layer(cage.object, *one_below)});
    precomputed.prepare_for_new_layer(cage.object, one_below);
    precomputed.prepare_for_new_layer(cage.object, cage.layer);
    check_identical(estimate_walls(precomputed, cage.object, *cage.layer), expected);
}

TEST_CASE("Precomputed overhang data has the curled-line tree exactly when a region slows down for curled perimeters", "[ExtrusionProcessor]")
{
    const bool         slowdown = GENERATE(false, true);
    DynamicPrintConfig config   = caged_overhang_config("classic");
    config.set_deserialize_strict("slowdown_for_curled_perimeters", slowdown ? "1" : "0");
    const SlicedCage cage(config);

    GCode::LayerToPrint layer;
    layer.object_layer    = cage.layer;
    layer.original_object = cage.object;
    const std::vector<PrecomputedOverhangLayer> precomputed = precompute_overhang_layers({layer}, false);
    REQUIRE(precomputed.size() == 1);
    CHECK((precomputed.front().lower_curled_lines != nullptr) == slowdown);
}

TEST_CASE("Curled walls are estimated only when overhang speed and the slowdown for curled perimeters are both on", "[ExtrusionProcessor]")
{
    const auto [overhang_speed, slowdown, estimated] = GENERATE(table<bool, bool, bool>({
        {true, true, true},
        {true, false, false},
        {false, true, false},
    }));
    DynamicPrintConfig config = caged_overhang_config("classic");
    config.set_deserialize_strict({{"enable_overhang_speed", overhang_speed ? "1" : "0"},
                                   {"slowdown_for_curled_perimeters", slowdown ? "1" : "0"}});
    const SlicedCage cage(config);

    CHECK(has_curled_lines(*cage.object) == estimated);
}

TEST_CASE("Curled walls are estimated when overhang speed and the slowdown for curled perimeters are on in different objects", "[ExtrusionProcessor]")
{
    const std::vector<std::vector<ConfigBase::SetDeserializeItem>> overrides{
        {{"enable_overhang_speed", "1"}, {"slowdown_for_curled_perimeters", "0"}},
        {{"enable_overhang_speed", "0"}, {"slowdown_for_curled_perimeters", "1"}},
    };
    Print print;
    Model model;
    init_print(std::vector<TriangleMesh>{caged_overhang_mesh(), caged_overhang_mesh()}, print, model, caged_overhang_config("classic"),
               &overrides);
    print.process();

    REQUIRE(print.objects().size() == 2);
    for (const PrintObject *object : print.objects())
        CHECK(has_curled_lines(*object));
}

TEST_CASE("Curled walls from an earlier slice are dropped once overhang speed is off", "[ExtrusionProcessor]")
{
    DynamicPrintConfig config = caged_overhang_config("classic");
    config.set_deserialize_strict("slowdown_for_curled_perimeters", "1");
    SlicedCage cage(config);
    const Layer *first_layer = cage.print.objects().front()->layers().front();
    REQUIRE(has_curled_lines(*cage.print.objects().front()));

    config.set_deserialize_strict("enable_overhang_speed", "0");
    cage.print.apply(cage.model, config);
    cage.print.process();
    REQUIRE(cage.print.objects().front()->layers().front() == first_layer);
    CHECK_FALSE(has_curled_lines(*cage.print.objects().front()));
}

TEST_CASE("Caged external overhangs are slowed when printed by object or through the pressure equalizer", "[ExtrusionProcessor]")
{
    const auto [key, value] = GENERATE(table<const char *, const char *>({
        {"print_sequence", "by object"},
        {"max_volumetric_extrusion_rate_slope", "10"},
    }));
    INFO(key << " = " << value);
    DynamicPrintConfig config = caged_overhang_config("classic");
    config.set_deserialize_strict(key, value);
    Print print;
    Model model;
    init_print(std::vector<TriangleMesh>{caged_overhang_mesh()}, print, model, config, nullptr, false);

    const std::vector<double> feed_rates = caged_slope_feed_rates(gcode(print));
    info_feed_rates("caged slope", feed_rates);
    REQUIRE_FALSE(feed_rates.empty());
    REQUIRE(*std::max_element(feed_rates.begin(), feed_rates.end()) < caged_slow_speed * MM_PER_MIN);
}

TEST_CASE("Benchmark caged overhang interior sampling", "[ExtrusionProcessor][!benchmark]"){
    const char* wall_generator = GENERATE("classic", "arachne");

    BENCHMARK(wall_generator)
    {
        return caged_overhang_gcode(wall_generator);
    };
}
