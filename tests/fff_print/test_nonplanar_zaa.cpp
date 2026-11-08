#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinRel;

namespace {

constexpr double wedge_size        = 20.;
constexpr double wedge_low_z       = 3.;
constexpr double wedge_slope_deg   = 20.;
constexpr double zaa_layer_height  = 0.2;

TriangleMesh sloped_wedge(double slope_deg = wedge_slope_deg)
{
    indexed_triangle_set its = its_make_cube(wedge_size, wedge_size, 1.);
    const double tan_slope = std::tan(slope_deg * M_PI / 180.);
    for (stl_vertex &v : its.vertices)
        if (v.z() > 0.5f)
            v.z() = float(wedge_low_z + v.x() * tan_slope);
    return TriangleMesh(its);
}

DynamicPrintConfig zaa_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"layer_height", zaa_layer_height},
        {"initial_layer_print_height", zaa_layer_height},
        {"zaa_enabled", 1},
        {"nonplanar_mode", "manual"},
        {"top_shell_layers", 3},
        {"wall_loops", 0},
        {"sparse_infill_density", "15%"},
        {"solid_infill_direction", 0},
        {"top_surface_pattern", "monotonic"},
        {"use_relative_e_distances", 1},
        {"ironing_type", "no ironing"},
    });
    return config;
}

struct TopMove
{
    double xy;
    double dz;
    double offset;
    double e;
    Vec2d  end;
};

std::vector<TopMove> top_surface_moves(const std::string &gcode, const DynamicPrintConfig &config)
{
    std::vector<TopMove> moves;
    bool   top_surface = false;
    double layer_z     = 0.;
    GCodeReader parser;
    parser.apply_config(config);
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string_view comment = line.comment();
        if (comment.find("TYPE:") != std::string_view::npos || comment.find("FEATURE:") != std::string_view::npos)
            top_surface = comment.find("Top surface") != std::string_view::npos;
        for (std::string_view tag : {std::string_view("Z:"), std::string_view(" Z_HEIGHT: ")})
            if (comment.substr(0, tag.size()) == tag)
                layer_z = std::stod(std::string(comment.substr(tag.size())));
        if (top_surface && line.extruding(self) && line.dist_XY(self) > 0.)
            moves.push_back({line.dist_XY(self), line.dist_Z(self), line.new_Z(self) - layer_z, line.dist_E(self),
                             Vec2d(line.new_X(self), line.new_Y(self))});
    });
    return moves;
}

}

TEST_CASE("Z anti-aliasing charges filament over the XY length of a sloped move", "[Nonplanar][ContourZ]")
{
    const DynamicPrintConfig config = zaa_config();
    const std::string raw_gcode = Test::slice({sloped_wedge()}, config);
    const std::vector<TopMove> moves = top_surface_moves(raw_gcode, config);

    std::vector<double> flat_ratios;
    double sloped_e = 0., sloped_expected_xy = 0., sloped_expected_3d = 0.;
    for (const TopMove &m : moves) {
        if (m.xy < 0.1 || std::abs(m.offset) < EPSILON)
            continue;
        const double scale = (zaa_layer_height + m.offset) / zaa_layer_height;
        if (std::abs(m.dz) < 1e-4) {
            flat_ratios.push_back(m.e / (m.xy * scale));
        } else if (std::abs(std::abs(m.dz) / m.xy - std::tan(wedge_slope_deg * M_PI / 180.)) < 0.01) {
            sloped_e += m.e;
            sloped_expected_xy += m.xy * scale;
            sloped_expected_3d += std::hypot(m.xy, m.dz) * scale;
        }
    }
    REQUIRE(flat_ratios.size() > 20);
    REQUIRE(sloped_expected_xy > 10.);
    std::nth_element(flat_ratios.begin(), flat_ratios.begin() + flat_ratios.size() / 2, flat_ratios.end());
    const double planar_e_per_mm = flat_ratios[flat_ratios.size() / 2];

    CAPTURE(planar_e_per_mm, sloped_expected_3d / sloped_expected_xy);
    CHECK_THAT(sloped_expected_3d / sloped_expected_xy,
               WithinRel(1. / std::cos(wedge_slope_deg * M_PI / 180.), 0.005));
    CHECK_THAT(sloped_e / sloped_expected_xy, WithinRel(planar_e_per_mm, 0.01));
}

namespace {

std::vector<size_t> contoured_moves_per_instance(const std::string &gcode, const DynamicPrintConfig &config,
                                                 const std::vector<Vec2d> &instance_centres)
{
    std::vector<size_t> counts(instance_centres.size(), 0);
    for (const TopMove &m : top_surface_moves(gcode, config)) {
        if (std::abs(m.offset) < EPSILON)
            continue;
        size_t best = 0;
        for (size_t i = 1; i < instance_centres.size(); ++i)
            if ((m.end - instance_centres[i]).squaredNorm() < (m.end - instance_centres[best]).squaredNorm())
                best = i;
        ++counts[best];
    }
    return counts;
}

std::vector<Vec2d> instance_centres(const Model &model)
{
    std::vector<Vec2d> centres;
    for (const ModelObject *object : model.objects)
        for (size_t i = 0; i < object->instances.size(); ++i) {
            const BoundingBoxf3 bb = object->instance_bounding_box(i);
            centres.emplace_back(bb.center().head<2>());
        }
    return centres;
}

}

TEST_CASE("Z anti-aliasing contours every instance of an object", "[Nonplanar][ContourZ]")
{
    const DynamicPrintConfig config = zaa_config();
    Print print;
    Model model;
    Test::init_print({sloped_wedge()}, print, model, config, nullptr, true, 2);
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.objects().front()->instances().size() == 2);

    std::string gcode;
    REQUIRE_NOTHROW(gcode = Test::gcode(print));
    const std::vector<size_t> counts = contoured_moves_per_instance(gcode, config, instance_centres(model));
    CAPTURE(counts[0], counts[1]);
    CHECK(counts[0] > 50);
    CHECK(counts[0] == counts[1]);
}

TEST_CASE("Z anti-aliasing contours a rotated instance against its own geometry", "[Nonplanar][ContourZ]")
{
    const DynamicPrintConfig config = zaa_config();
    Print print;
    Model model;
    Test::init_print({sloped_wedge()}, print, model, config, nullptr, true, 2);
    model.objects.front()->instances[1]->set_rotation(Vec3d(0., 0., 0.5 * M_PI));
    model.objects.front()->ensure_on_bed();
    print.apply(model, print.full_print_config());
    REQUIRE(print.objects().size() == 2);

    std::string gcode;
    REQUIRE_NOTHROW(gcode = Test::gcode(print));
    const std::vector<size_t> counts = contoured_moves_per_instance(gcode, config, instance_centres(model));
    CAPTURE(counts[0], counts[1]);
    CHECK(counts[0] > 50);
    CHECK_THAT(double(counts[1]), WithinRel(double(counts[0]), 0.5));
}

namespace {

struct ContouredRoles
{
    size_t top = 0;
    size_t other = 0;
};

void count_contoured_roles(const ExtrusionEntity &entity, ContouredRoles &counts)
{
    if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
        for (const ExtrusionEntity *child : collection->entities)
            count_contoured_roles(*child, counts);
    } else if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity)) {
        if (path->z_contoured) {
            if (path->role() == erTopSolidInfill)
                ++counts.top;
            else
                ++counts.other;
        }
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity)) {
        for (const ExtrusionPath &path : loop->paths)
            count_contoured_roles(path, counts);
    } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
        for (const ExtrusionPath &path : multipath->paths)
            count_contoured_roles(path, counts);
    }
}

}

TEST_CASE("ZAA fallback contours only top roles when curved top shells are requested", "[Nonplanar][ContourZ]")
{
    const auto [shells, fallback, expected] = GENERATE(table<int, std::string, bool>({
        {0, "zaa", false},
        {3, "planar", false},
        {3, "zaa", true},
    }));
    DynamicPrintConfig config = zaa_config();
    config.set_deserialize_strict({
        {"zaa_enabled", 0},
        {"wall_loops", 1},
        {"nonplanar_top_shells", shells},
        {"nonplanar_fallback", fallback},
        {"nonplanar_min_patch_area", 10000.},
        {"zaa_min_z", 0.19},
    });
    Print print;
    Test::init_and_process_print({sloped_wedge(5.)}, print, config);
    ContouredRoles counts;
    for (const Layer *layer : print.objects().front()->layers())
        for (const LayerRegion *region : layer->regions()) {
            count_contoured_roles(region->fills, counts);
            count_contoured_roles(region->perimeters, counts);
        }
    CAPTURE(shells, fallback, counts.top, counts.other);
    CHECK((counts.top > 0) == expected);
    CHECK(counts.other == 0);

    const auto moves = top_surface_moves(Test::gcode(print), config);
    size_t offset_moves = 0;
    for (const TopMove &move : moves) {
        if (std::abs(move.offset) < EPSILON)
            continue;
        ++offset_moves;
        CHECK(move.offset <= 0.0501);
        CHECK(move.offset >= -0.1501);
    }
    CHECK((offset_moves > 0) == expected);
}

TEST_CASE("Explicit ZAA still contours walls when the non-planar fallback is planar", "[Nonplanar][ContourZ]")
{
    DynamicPrintConfig config = zaa_config();
    config.set_deserialize_strict({{"wall_loops", 1}, {"nonplanar_top_shells", 3}, {"nonplanar_fallback", "planar"},
                                   {"nonplanar_min_patch_area", 10000.}});
    Print print;
    Test::init_and_process_print({sloped_wedge(5.)}, print, config);
    ContouredRoles counts;
    for (const Layer *layer : print.objects().front()->layers())
        for (const LayerRegion *region : layer->regions()) {
            count_contoured_roles(region->fills, counts);
            count_contoured_roles(region->perimeters, counts);
        }
    CHECK(counts.top > 0);
    CHECK(counts.other > 0);
}
