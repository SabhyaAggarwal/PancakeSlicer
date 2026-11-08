#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_message.hpp>
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/Arachne/SkeletalTrapezoidation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"
#include "libslic3r/Feature/FuzzySkin/FuzzySkin.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Point.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include "libslic3r/libslic3r.h"
#include <vector>
#include <utility>
#include <math.h>
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Layer.hpp"

using namespace Slic3r;
using namespace Slic3r::Arachne;

namespace {

struct Segment {
    Point from;
    Point to;
    size_t inset_idx;
    
    Segment normalized() const {
        if (from < to || (from.x() == to.x() && from.y() < to.y())) {
            return *this;
        }
        return {to, from, inset_idx};
    }
    
    bool operator<(const Segment& other) const {
        auto a = normalized();
        auto b = other.normalized();
        if (a.inset_idx != b.inset_idx) return a.inset_idx < b.inset_idx;
        if (a.from != b.from) return a.from < b.from;
        return a.to < b.to;
    }
    
    bool operator==(const Segment& other) const {
        auto a = normalized();
        auto b = other.normalized();
        return a.inset_idx == b.inset_idx && a.from == b.from && a.to == b.to;
    }
};

bool points_approx_equal(const Point& a, const Point& b, coord_t tolerance) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

bool segments_approx_equal(const Segment& a, const Segment& b, coord_t tolerance) {
    if (a.inset_idx != b.inset_idx) return false;
    
    bool same_dir = points_approx_equal(a.from, b.from, tolerance) && 
                    points_approx_equal(a.to, b.to, tolerance);
    bool reverse_dir = points_approx_equal(a.from, b.to, tolerance) && 
                       points_approx_equal(a.to, b.from, tolerance);
    return same_dir || reverse_dir;
}

std::vector<Segment> extract_all_segments(const std::vector<VariableWidthLines>& toolpaths) {
    std::vector<Segment> segments;
    
    for (const auto& inset : toolpaths) {
        for (const auto& line : inset) {
            if (line.junctions.size() < 2) continue;
            
            for (size_t i = 0; i + 1 < line.junctions.size(); ++i) {
                segments.push_back({
                    line.junctions[i].p,
                    line.junctions[i + 1].p,
                    line.inset_idx
                });
            }
        }
    }
    
    return segments;
}

std::vector<std::pair<Segment, Segment>> find_duplicate_segments(
    const std::vector<Segment>& segments, 
    coord_t tolerance) 
{
    std::vector<std::pair<Segment, Segment>> duplicates;
    
    for (size_t i = 0; i < segments.size(); ++i) {
        for (size_t j = i + 1; j < segments.size(); ++j) {
            if (segments_approx_equal(segments[i], segments[j], tolerance)) {
                duplicates.emplace_back(segments[i], segments[j]);
            }
        }
    }
    
    return duplicates;
}

WallToolPathsParams make_bbl_x1c_028_params(int min_bead_width_percent) {
    constexpr double nozzle_diameter = 0.4;
    
    WallToolPathsParams params;
    params.min_bead_width = float(min_bead_width_percent / 100.0 * nozzle_diameter);
    params.min_feature_size = float(0.25 * nozzle_diameter);
    params.wall_transition_filter_deviation = float(0.25 * nozzle_diameter);
    params.wall_transition_length = float(1.0 * nozzle_diameter);
    params.wall_transition_angle = 10.0f;
    params.wall_distribution_count = 1;
    params.min_length_factor = 0.5f;
    params.is_top_or_bottom_layer = false;
    return params;
}

size_t run_arachne_test(int min_bead_width_percent) {
    constexpr double layer_height = 0.28;
    constexpr double ext_perimeter_width_mm = 0.42;
    constexpr double perimeter_width_mm = 0.45;
    
    constexpr double spacing_factor = 1.0 - 0.25 * M_PI;
    double ext_perimeter_spacing_mm = ext_perimeter_width_mm - layer_height * spacing_factor;
    double perimeter_spacing_mm = perimeter_width_mm - layer_height * spacing_factor;
    
    coord_t ext_perimeter_width = scaled<coord_t>(ext_perimeter_width_mm);
    coord_t ext_perimeter_spacing = scaled<coord_t>(ext_perimeter_spacing_mm);
    coord_t perimeter_spacing = scaled<coord_t>(perimeter_spacing_mm);
    
    coord_t bead_width_0 = ext_perimeter_spacing;
    coord_t bead_width_x = perimeter_spacing;
    size_t  inset_count  = 2;
    
    float precise_offset = -float(ext_perimeter_width - ext_perimeter_spacing);
    coord_t wall_0_inset = -coord_t(ext_perimeter_width / 2 - ext_perimeter_spacing / 2);
    
    auto params = make_bbl_x1c_028_params(min_bead_width_percent);
    
    Polygon outer_raw;
    outer_raw.points.emplace_back(Point::new_scale(0.0, 0.0));
    outer_raw.points.emplace_back(Point::new_scale(20.0, 0.0));
    outer_raw.points.emplace_back(Point::new_scale(20.0, 20.0));
    outer_raw.points.emplace_back(Point::new_scale(0.0, 20.0));
    
    Polygon inner_raw;
    inner_raw.points.emplace_back(Point::new_scale(0.5, 0.5));
    inner_raw.points.emplace_back(Point::new_scale(0.5, 19.5));
    inner_raw.points.emplace_back(Point::new_scale(19.5, 19.5));
    inner_raw.points.emplace_back(Point::new_scale(19.5, 0.5));
    
    ExPolygon input_expolygon;
    input_expolygon.contour = outer_raw;
    input_expolygon.holes.push_back(inner_raw);
    
    ExPolygons offset_result = offset_ex(input_expolygon, precise_offset);
    Polygons outline;
    for (const auto& expoly : offset_result) {
        outline.push_back(expoly.contour);
        for (const auto& hole : expoly.holes) {
            outline.push_back(hole);
        }
    }
    
    WallToolPaths wallToolPaths(outline, bead_width_0, bead_width_x, 
                                 inset_count, wall_0_inset, 
                                 layer_height, params);
    auto toolpaths = wallToolPaths.getToolPaths();
    
    auto all_segments = extract_all_segments(toolpaths);
    auto duplicates = find_duplicate_segments(all_segments, scaled<coord_t>(0.1));
    
    return duplicates.size();
}

}

TEST_CASE("Arachne wall generation - 50% min_bead_width", "[Arachne]") {
    size_t duplicates = run_arachne_test(50);
    REQUIRE(duplicates == 0);
}

TEST_CASE("Arachne wall generation - 60% min_bead_width", "[Arachne]") {
    size_t duplicates = run_arachne_test(60);
    REQUIRE(duplicates == 0);
}

TEST_CASE("Arachne widening keeps two beads in transition band (#14376)", "[Arachne]") {

    const double outer_mm = 0.42, inner_mm = 0.45, min_bead_mm = 0.34;

    const coord_t outer_width = scaled<coord_t>(outer_mm);
    const coord_t inner_width = scaled<coord_t>(inner_mm);
    const coord_t min_bead_width = scaled<coord_t>(min_bead_mm);
    const coord_t min_feature_size = scaled<coord_t>(0.10);
    const coord_t transition_length = scaled<coord_t>(0.40);
    const coord_t max_bead_count = 4;
    const double split_middle_threshold = std::clamp(2.0 * min_bead_mm / outer_mm - 1.0, 0.01, 0.99);
    const double add_middle_threshold = std::clamp(min_bead_mm / inner_mm, 0.01, 0.99);

    auto strategy = BeadingStrategyFactory::makeStrategy(
        outer_width, inner_width, transition_length,
         float(M_PI / 4.0),  true,
        min_bead_width, min_feature_size,
        split_middle_threshold, add_middle_threshold,
        max_bead_count,  0,  1);

    const coord_t thickness = scaled<coord_t>(0.50);
    REQUIRE(thickness > inner_width);
    REQUIRE(thickness < strategy->getTransitionThickness(1));

    const BeadingStrategy::Beading beading = strategy->compute(thickness, 2);
    REQUIRE(beading.bead_widths.size() == 2);

    for (const coord_t w : beading.bead_widths)
        CHECK(w <= inner_width);
}

namespace {
struct InterpolateProbe : SkeletalTrapezoidation {
    using SkeletalTrapezoidation::interpolate;
};
}
TEST_CASE("Beading interpolation tolerates a thicker side with fewer insets", "[Arachne][Regression]") {
    using Beading = BeadingStrategy::Beading;

    const coord_t w = scaled<coord_t>(0.42);
    Beading left;
    left.total_thickness = scaled<coord_t>(1.0);
    left.bead_widths = { w, w, w, w };
    left.toolpath_locations = { scaled<coord_t>(0.1), scaled<coord_t>(0.3), scaled<coord_t>(0.5), scaled<coord_t>(0.7) };
    left.left_over = 0;

    Beading right;
    right.total_thickness = scaled<coord_t>(2.0);
    right.bead_widths = { w, w };
    right.toolpath_locations = { scaled<coord_t>(0.1), scaled<coord_t>(0.3) };
    right.left_over = 0;

    const coord_t switching_radius = scaled<coord_t>(0.6);

    Beading result;
    REQUIRE_NOTHROW(result = InterpolateProbe::interpolate(left, 0.5, right, switching_radius));

    const Beading expected = InterpolateProbe::interpolate(left, 0.5, right);
    REQUIRE(result.toolpath_locations.size() == expected.toolpath_locations.size());
    REQUIRE(result.bead_widths.size() == expected.bead_widths.size());
    for (size_t i = 0; i < expected.toolpath_locations.size(); ++i) {
        CHECK(result.toolpath_locations[i] == expected.toolpath_locations[i]);
        CHECK(result.bead_widths[i] == expected.bead_widths[i]);
    }
}

namespace {

Arachne::ExtrusionJunctions square_loop(coord_t width)
{
    const coord_t s = scaled<coord_t>(20.);
    return {{Point(0, 0), width, 0}, {Point(s, 0), width, 0}, {Point(s, s), width, 0}, {Point(0, s), width, 0}, {Point(0, 0), width, 0}};
}

FuzzySkinConfig thick_fuzzy_config(FuzzySkinMode mode, NoiseType noise_type, double thickness_mm)
{
    FuzzySkinConfig cfg{};
    cfg.type              = FuzzySkinType::All;
    cfg.thickness         = scaled<coord_t>(thickness_mm);
    cfg.point_distance    = scaled<coord_t>(0.3);
    cfg.fuzzy_first_layer = true;
    cfg.noise_type        = noise_type;
    cfg.noise_scale       = 1.0;
    cfg.noise_octaves     = 4;
    cfg.noise_persistence = 0.5;
    cfg.mode              = mode;
    cfg.layer_id          = 5;
    return cfg;
}

}
TEST_CASE("Fuzzy skin extrusion width is floored at the minimum the flow accepts", "[Arachne][FuzzySkin]") {
    using namespace Slic3r::Feature::FuzzySkin;

    const double layer_height = GENERATE(0.08, 0.2, 0.28);
    const auto   mode         = GENERATE(FuzzySkinMode::Extrusion, FuzzySkinMode::Combined);
    const auto   noise_type   = GENERATE(NoiseType::Classic, NoiseType::Perlin, NoiseType::Billow, NoiseType::RidgedMulti, NoiseType::Voronoi);
    CAPTURE(layer_height, int(mode), int(noise_type));

    const double line_width_mm = 0.42;
    auto         loop          = square_loop(scaled<coord_t>(line_width_mm));
    fuzzy_extrusion_line(loop,  1.0, layer_height, thick_fuzzy_config(mode, noise_type, 3 * line_width_mm));

    REQUIRE(loop.size() > 100);

    const auto   narrowest    = std::min_element(loop.begin(), loop.end(), [](const auto& a, const auto& b) { return a.w < b.w; });
    const double narrowest_mm = unscaled<double>(narrowest->w);
    const double floor_mm     = layer_height * (1. - 0.25 * PI);
    CAPTURE(narrowest_mm, floor_mm);

    CHECK(narrowest_mm < line_width_mm);
    CHECK(narrowest_mm > floor_mm);
    CHECK_NOTHROW(Flow::rounded_rectangle_extrusion_spacing(float(narrowest_mm), float(layer_height)));
}

TEST_CASE("Fuzzy skin displacement mode leaves widths untouched", "[Arachne][FuzzySkin]") {
    using namespace Slic3r::Feature::FuzzySkin;

    const coord_t width = scaled<coord_t>(0.42);
    auto          loop  = square_loop(width);
    fuzzy_extrusion_line(loop,  1.0,  0.2, thick_fuzzy_config(FuzzySkinMode::Displacement, NoiseType::Classic, 1.26));

    REQUIRE(loop.size() > 100);
    CHECK(std::all_of(loop.begin(), loop.end(), [width](const auto& j) { return j.w == width; }));
}
