#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include "libslic3r/PrintConfig.hpp"
#include <cstddef>
#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "test_helpers.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

const double ledge_z = 5.0;

const double first_layer_z = 0.2;

TriangleMesh step_with_ledge()
{
    TriangleMesh m = Slic3r::Test::mesh(TestMesh::step);
    m.scale(Vec3f(3.f, 3.f, 1.f));
    return m;
}

DynamicPrintConfig base_config(const char *wall_generator)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_generator",             wall_generator },
        { "layer_height",               0.2 },
        { "initial_layer_print_height", 0.2 },
        { "wall_loops",                 3 },
        { "sparse_infill_density",      "15%" },
        { "top_shell_layers",           3 },
        { "bottom_shell_layers",        3 },
        { "top_surface_density",        "100%" },
        { "top_surface_expansion",      0.0 },
        { "only_one_wall_top",          false },
        { "only_one_wall_first_layer",  false },
        { "min_width_top_surface",      0.0 },
    });
    return config;
}

double collection_length(const ExtrusionEntityCollection &coll)
{
    double len = 0.;
    for (const ExtrusionEntity *entity : coll.flatten().entities)
        if (! entity->is_collection())
            len += entity->length();
    return len;
}

struct SliceLengths {
    std::vector<double> perimeters;
    std::vector<double> fills;
};

SliceLengths slice_lengths(const Print &print)
{
    SliceLengths out;
    for (const Layer *layer : print.objects().front()->layers()) {
        double perimeters = 0., fills = 0.;
        for (const LayerRegion *region : layer->regions()) {
            perimeters += collection_length(region->perimeters);
            fills      += collection_length(region->fills);
        }
        out.perimeters.push_back(perimeters);
        out.fills.push_back(fills);
    }
    return out;
}

double perimeter_length_at(const Print &print, double print_z)
{
    for (const Layer *layer : print.objects().front()->layers())
        if (std::abs(layer->print_z - print_z) < 1e-4) {
            double len = 0.;
            for (const LayerRegion *region : layer->regions())
                len += collection_length(region->perimeters);
            return len;
        }
    return 0.;
}

double max_difference(const std::vector<double> &a, const std::vector<double> &b)
{
    if (a.size() != b.size() || a.empty())
        return -1.;
    double worst = 0.;
    for (size_t i = 0; i < a.size(); ++ i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

}
TEST_CASE("Top surface expansion only acts where there is a top fill", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto lengths_for = [wall_generator](int top_shell_layers, const char *top_surface_density, double expansion) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "top_shell_layers",      top_shell_layers },
            { "top_surface_density",   top_surface_density },
            { "top_surface_expansion", expansion },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return slice_lengths(print);
    };

    SECTION("no top shell layers") {
        const SliceLengths off = lengths_for(0, "100%", 0.0);
        const SliceLengths on  = lengths_for(0, "100%", 2.0);
        REQUIRE(off.perimeters.size() == on.perimeters.size());
        CHECK_THAT(max_difference(off.perimeters, on.perimeters), Catch::Matchers::WithinAbs(0., 1.0));
        CHECK_THAT(max_difference(off.fills,      on.fills),      Catch::Matchers::WithinAbs(0., 1.0));
    }

    SECTION("zero top surface density") {
        const SliceLengths off = lengths_for(3, "0%", 0.0);
        const SliceLengths on  = lengths_for(3, "0%", 2.0);
        REQUIRE(off.perimeters.size() == on.perimeters.size());
        CHECK_THAT(max_difference(off.perimeters, on.perimeters), Catch::Matchers::WithinAbs(0., 1.0));
        CHECK_THAT(max_difference(off.fills,      on.fills),      Catch::Matchers::WithinAbs(0., 1.0));
    }

    SECTION("with a top fill the same expansion does change the slice") {
        const SliceLengths off = lengths_for(3, "100%", 0.0);
        const SliceLengths on  = lengths_for(3, "100%", 2.0);
        REQUIRE(off.fills.size() == on.fills.size());
        CHECK(max_difference(off.fills, on.fills) > scale_(0.5));
    }
}

TEST_CASE("Top surface density does not affect a slice without a top shell", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto lengths_for = [wall_generator](const char *top_surface_density) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "top_shell_layers",    0 },
            { "only_one_wall_top",   true },
            { "top_surface_density", top_surface_density },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return slice_lengths(print);
    };

    const SliceLengths solid = lengths_for("100%");
    const SliceLengths none  = lengths_for("0%");
    REQUIRE(solid.perimeters.size() == none.perimeters.size());
    CHECK_THAT(max_difference(solid.perimeters, none.perimeters), Catch::Matchers::WithinAbs(0., 1.0));
    CHECK_THAT(max_difference(solid.fills,      none.fills),      Catch::Matchers::WithinAbs(0., 1.0));
}

TEST_CASE("Only one wall on top surfaces drops inner walls only where a top fill replaces them", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto ledge_perimeters_for = [wall_generator](bool only_one_wall_top, const char *top_surface_density, double expansion) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "only_one_wall_top",     only_one_wall_top },
            { "top_surface_density",   top_surface_density },
            { "top_surface_expansion", expansion },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return perimeter_length_at(print, ledge_z);
    };

    const double plain              = ledge_perimeters_for(false, "100%", 2.0);
    const double one_wall           = ledge_perimeters_for(true,  "100%", 2.0);
    const double one_wall_no_fill   = ledge_perimeters_for(true,  "0%",   2.0);
    const double one_wall_no_expand = ledge_perimeters_for(true,  "100%", 0.0);

    REQUIRE(plain > 0.);
    CHECK(one_wall < plain);
    CHECK(one_wall_no_fill > one_wall);
    CHECK(one_wall_no_fill < plain);
    CHECK(one_wall_no_expand > one_wall);
    CHECK(one_wall_no_expand < plain);
}

TEST_CASE("Only one wall on the first layer needs a bottom shell", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto first_layer_perimeters_for = [wall_generator](bool only_one_wall_first_layer, int bottom_shell_layers) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "only_one_wall_first_layer", only_one_wall_first_layer },
            { "bottom_shell_layers",       bottom_shell_layers },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return perimeter_length_at(print, first_layer_z);
    };

    const double plain             = first_layer_perimeters_for(false, 3);
    const double one_wall          = first_layer_perimeters_for(true,  3);
    const double plain_no_shell    = first_layer_perimeters_for(false, 0);
    const double one_wall_no_shell = first_layer_perimeters_for(true,  0);

    REQUIRE(plain > 0.);
    CHECK(one_wall < plain);
    CHECK_THAT(one_wall_no_shell, Catch::Matchers::WithinAbs(plain_no_shell, 1.0));
}

namespace {

const double tab_top_z = 5.0;

const double narrow_wall = 1.186;

Print &tube_with_tab(Print &print, Model &model, const DynamicPrintConfig &config)
{
    ModelObject *object = model.add_object();
    object->name = "tube_with_tab.stl";
    object->add_volume(make_cube(20., 30., 10.), ModelVolumeType::MODEL_PART, false);
    TriangleMesh tab = make_cube(20., 8.5, 5.);
    tab.translate(0.f, -8.f, 0.f);
    object->add_volume(std::move(tab), ModelVolumeType::MODEL_PART, false);
    TriangleMesh bore = make_cube(20. - 2. * narrow_wall, 30. - 2. * narrow_wall, 12.);
    bore.translate(float(narrow_wall), float(narrow_wall), -1.f);
    object->add_volume(std::move(bore), ModelVolumeType::NEGATIVE_VOLUME, false);
    object->add_instance();
    object->ensure_on_bed();

    print.auto_assign_extruders(object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    return print;
}

DynamicPrintConfig narrow_wall_config(bool only_one_wall_top, double top_surface_expansion)
{
    DynamicPrintConfig config = base_config("arachne");
    config.set_deserialize_strict({
        { "wall_loops",            2 },
        { "nozzle_diameter",       "0.4" },
        { "line_width",            0.42 },
        { "outer_wall_line_width", 0.42 },
        { "inner_wall_line_width", 0.45 },
        { "min_bead_width",        "85%" },
        { "precise_outer_wall",    true },
        { "wall_sequence",         "inner wall/outer wall" },
        { "only_one_wall_top",     only_one_wall_top },
        { "top_surface_expansion", top_surface_expansion },
    });
    return config;
}

double far_wall_inner_wall_length(const Print &print, double print_z)
{
    for (const Layer *layer : print.objects().front()->layers()) {
        if (std::abs(layer->print_z - print_z) > EPSILON)
            continue;
        BoundingBox band = get_extents(layer->lslices);
        band.min.y()     = band.max.y() - scaled<coord_t>(3.);

        Polylines inner_walls;
        auto      collect = [&inner_walls](const ExtrusionPaths &paths) {
            for (const ExtrusionPath &path : paths)
                if (path.role() == erPerimeter)
                    inner_walls.emplace_back(path.as_polyline());
        };
        for (const LayerRegion *region : layer->regions()) {
            const ExtrusionEntityCollection walls = region->perimeters.flatten();
            for (const ExtrusionEntity *entity : walls.entities) {
                if (const auto *loop = dynamic_cast<const ExtrusionLoop*>(entity))
                    collect(loop->paths);
                else if (const auto *multi_path = dynamic_cast<const ExtrusionMultiPath*>(entity))
                    collect(multi_path->paths);
                else if (const auto *path = dynamic_cast<const ExtrusionPath*>(entity))
                    collect({ *path });
            }
        }
        return unscaled<double>(total_length(intersection_pl(inner_walls, band.polygon())));
    }
    return 0.;
}

}
TEST_CASE("Only one wall on top surfaces keeps the inner walls of narrow walls away from the top surface", "[Perimeters]")
{
    const double top_surface_expansion = GENERATE(0.0, 2.0);
    CAPTURE(top_surface_expansion);

    struct TabTopLayer {
        double perimeters;
        double far_wall_inner_walls;
    };
    auto tab_top_layer_for = [top_surface_expansion](bool only_one_wall_top) {
        Print print;
        Model model;
        tube_with_tab(print, model, narrow_wall_config(only_one_wall_top, top_surface_expansion));
        print.process();
        REQUIRE_FALSE(print.objects().empty());
        return TabTopLayer{ perimeter_length_at(print, tab_top_z), far_wall_inner_wall_length(print, tab_top_z) };
    };

    const TabTopLayer plain    = tab_top_layer_for(false);
    const TabTopLayer one_wall = tab_top_layer_for(true);

    REQUIRE(plain.far_wall_inner_walls > 10.);
    CHECK(one_wall.perimeters < plain.perimeters);
    CHECK_THAT(one_wall.far_wall_inner_walls, Catch::Matchers::WithinAbs(plain.far_wall_inner_walls, 1.0));
}

namespace {

const double cavity_ceiling_z = 6.2;

TriangleMesh flared_cone()
{
    TriangleMesh cone = make_cone(20., 4.);
    cone.mirror(Z);
    cone.translate(0., 0., 4.);
    return cone;
}

Print &box_over_cavity(Print &print, Model &model, const DynamicPrintConfig &config)
{
    ModelObject *object = model.add_object();
    object->name = "box_over_cavity.stl";
    object->add_volume(make_cube(30., 30., 8.), ModelVolumeType::MODEL_PART, false);
    TriangleMesh cavity = make_cube(20., 20., 4.);
    cavity.translate(5.f, 5.f, 2.f);
    object->add_volume(std::move(cavity), ModelVolumeType::NEGATIVE_VOLUME, false);
    TriangleMesh hole = make_cube(4., 4., 6.);
    hole.translate(13.f, 13.f, 5.f);
    object->add_volume(std::move(hole), ModelVolumeType::NEGATIVE_VOLUME, false);
    object->add_instance();
    object->ensure_on_bed();

    print.auto_assign_extruders(object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    return print;
}

DynamicPrintConfig unsupported_walls_config(const char *wall_generator, bool unsupported_wall_last)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_generator",             wall_generator },
        { "layer_height",               0.2 },
        { "initial_layer_print_height", 0.2 },
        { "wall_loops",                 3 },
        { "detect_overhang_wall",       true },
        { "wall_sequence",              "outer wall/inner wall" },
        { "is_infill_first",            false },
        { "sparse_infill_density",      "15%" },
        { "unsupported_wall_last",      unsupported_wall_last },
        { "gcode_comments",             true },
    });
    return config;
}

bool unsupported_loop(const ExtrusionEntity *entity)
{
    if (! entity->is_loop())
        return false;
    const ExtrusionPaths &paths = static_cast<const ExtrusionLoop *>(entity)->paths;
    return ! paths.empty() && std::all_of(paths.begin(), paths.end(),
                                          [](const ExtrusionPath &path) { return path.role() == erOverhangPerimeter; });
}

std::vector<std::vector<const ExtrusionLoop*>> wall_islands(const Print &print)
{
    std::vector<std::vector<const ExtrusionLoop*>> islands;
    for (const Layer *layer : print.objects().front()->layers())
        for (const LayerRegion *region : layer->regions())
            for (const ExtrusionEntity *island : region->perimeters.entities) {
                std::vector<const ExtrusionLoop*> loops;
                for (const ExtrusionEntity *entity : static_cast<const ExtrusionEntityCollection*>(island)->entities)
                    if (entity->is_loop())
                        loops.push_back(static_cast<const ExtrusionLoop*>(entity));
                islands.push_back(std::move(loops));
            }
    return islands;
}

int islands_with_a_supported_loop_last(const Print &print)
{
    int count = 0;
    for (const std::vector<const ExtrusionLoop*> &loops : wall_islands(print)) {
        bool seen_unsupported = false;
        for (const ExtrusionLoop *loop : loops) {
            if (unsupported_loop(loop))
                seen_unsupported = true;
            else if (seen_unsupported) {
                ++ count;
                break;
            }
        }
    }
    return count;
}

std::vector<const ExtrusionLoop*> unsupported_loops(const Print &print, double print_z = -1.)
{
    std::vector<const ExtrusionLoop*> loops;
    for (const Layer *layer : print.objects().front()->layers()) {
        if (print_z >= 0. && std::abs(layer->print_z - print_z) > EPSILON)
            continue;
        for (const LayerRegion *region : layer->regions())
            for (const ExtrusionEntity *island : region->perimeters.entities)
                for (const ExtrusionEntity *entity : static_cast<const ExtrusionEntityCollection*>(island)->entities)
                    if (unsupported_loop(entity))
                        loops.push_back(static_cast<const ExtrusionLoop*>(entity));
    }
    return loops;
}

int loops_held_back_for_infill(const std::vector<const ExtrusionLoop*> &loops)
{
    return int(std::count_if(loops.begin(), loops.end(), [](const ExtrusionLoop *loop) { return loop->print_after_infill; }));
}

std::string layer_gcode(const std::string &gcode, double print_z)
{
    std::string out;
    GCodeReader reader;
    reader.parse_buffer(gcode, [&out, print_z](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (std::abs(self.z() - print_z) < EPSILON)
            out += line.raw() + "\n";
    });
    return out;
}

}
TEST_CASE("Unsupported wall loops are extruded after the walls that anchor them", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto slice_cone = [wall_generator](bool unsupported_wall_last, Print &print) {
        init_and_process_print({ flared_cone() }, print, unsupported_walls_config(wall_generator, unsupported_wall_last));
        REQUIRE_FALSE(print.objects().empty());
    };

    Print on;
    slice_cone(true, on);
    REQUIRE(unsupported_loops(on).size() > 0);
    CHECK(islands_with_a_supported_loop_last(on) == 0);

    SECTION("the held back loops run innermost first") {
        for (const std::vector<const ExtrusionLoop*> &loops : wall_islands(on)) {
            int previous_inset = std::numeric_limits<int>::max();
            for (const ExtrusionLoop *loop : loops)
                if (unsupported_loop(loop)) {
                    CHECK(loop->inset_idx <= previous_inset);
                    previous_inset = loop->inset_idx;
                }
        }
    }

    SECTION("switched off, the configured wall order is left alone") {
        Print off;
        slice_cone(false, off);
        REQUIRE(unsupported_loops(off).size() == unsupported_loops(on).size());
        CHECK(islands_with_a_supported_loop_last(off) > 0);
    }
}

TEST_CASE("A wall loop out of reach of the layer below waits for the infill", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    Print print;
    Model model;
    box_over_cavity(print, model, unsupported_walls_config(wall_generator, true));
    print.process();

    const std::vector<const ExtrusionLoop*> hole_loops = unsupported_loops(print, cavity_ceiling_z);
    REQUIRE(hole_loops.size() > 0);
    CHECK(loops_held_back_for_infill(hole_loops) == int(hole_loops.size()));

    SECTION("a loop alongside a supported wall is not held back") {
        Print cone;
        init_and_process_print({ flared_cone() }, cone, unsupported_walls_config(wall_generator, true));
        const std::vector<const ExtrusionLoop*> loops = unsupported_loops(cone);
        REQUIRE(loops.size() > 0);
        CHECK(loops_held_back_for_infill(loops) == 0);
    }

    SECTION("switched off, no loop is held back") {
        Print off;
        Model off_model;
        box_over_cavity(off, off_model, unsupported_walls_config(wall_generator, false));
        off.process();
        const std::vector<const ExtrusionLoop*> loops = unsupported_loops(off, cavity_ceiling_z);
        REQUIRE(loops.size() == hole_loops.size());
        CHECK(loops_held_back_for_infill(loops) == 0);
    }
}

TEST_CASE("Loops waiting for the infill are extruded after it", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto ceiling_roles = [wall_generator](bool unsupported_wall_last) {
        Print print;
        Model model;
        box_over_cavity(print, model, unsupported_walls_config(wall_generator, unsupported_wall_last));
        const std::string layer = layer_gcode(gcode(print), cavity_ceiling_z);
        REQUIRE_FALSE(layer.empty());
        return role_sequence(layer, { "perimeter", "infill" });
    };

    CHECK(ceiling_roles(true)  == std::vector<std::string>{ "perimeter", "infill", "perimeter" });
    CHECK(ceiling_roles(false) == std::vector<std::string>{ "perimeter", "infill" });
}

namespace {

const double slab_first_layer_z = 5.2;

const double unprintable_rib = 0.08;
const double printable_rib   = 0.3;

Print &slab_over_rib(Print &print, Model &model, double rib_width, const DynamicPrintConfig &config)
{
    ModelObject *object = model.add_object();
    object->name = "slab_over_rib.stl";
    object->add_volume(make_cube(4., 5., 6.), ModelVolumeType::MODEL_PART, false);
    if (rib_width > 0.) {
        TriangleMesh rib = make_cube(20., rib_width, 5.);
        rib.translate(4.f, 0.f, 0.f);
        object->add_volume(std::move(rib), ModelVolumeType::MODEL_PART, false);
    }
    TriangleMesh slab = make_cube(20., 5., 1.);
    slab.translate(4.f, 0.f, 5.f);
    object->add_volume(std::move(slab), ModelVolumeType::MODEL_PART, false);
    object->add_instance();
    object->ensure_on_bed();

    print.auto_assign_extruders(object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    return print;
}

DynamicPrintConfig printable_rib_config(const char *wall_generator, bool detect_thin_wall)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_generator",                wall_generator },
        { "layer_height",                  0.2 },
        { "initial_layer_print_height",    0.2 },
        { "nozzle_diameter",               "0.4" },
        { "outer_wall_line_width",         0.42 },
        { "inner_wall_line_width",         0.45 },
        { "wall_loops",                    2 },
        { "detect_overhang_wall",          true },
        { "detect_thin_wall",              detect_thin_wall },
        { "min_feature_size",              "25%" },
        { "raft_layers",                   0 },
        { "extra_perimeters_on_overhangs", false },
        { "overhang_reverse",              false },
        { "only_one_wall_top",             false },
        { "only_one_wall_first_layer",     false },
        { "unsupported_wall_last",         false },
        { "sparse_infill_density",         "15%" },
    });
    return config;
}

double overhang_length_at(const Print &print, double print_z)
{
    double len = 0.;
    const auto add_entity = [&len](const ExtrusionEntity *entity, auto &&self) -> void {
        const auto add_paths = [&len](const ExtrusionPaths &paths) {
            for (const ExtrusionPath &path : paths)
                if (path.role() == erOverhangPerimeter)
                    len += path.length();
        };
        if (const auto *coll = dynamic_cast<const ExtrusionEntityCollection*>(entity)) {
            for (const ExtrusionEntity *child : coll->entities)
                self(child, self);
        } else if (const auto *loop = dynamic_cast<const ExtrusionLoop*>(entity)) {
            add_paths(loop->paths);
        } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath*>(entity)) {
            add_paths(multi->paths);
        } else if (const auto *path = dynamic_cast<const ExtrusionPath*>(entity)) {
            if (path->role() == erOverhangPerimeter)
                len += path->length();
        }
    };

    for (const Layer *layer : print.objects().front()->layers()) {
        if (std::abs(layer->print_z - print_z) > EPSILON)
            continue;
        for (const LayerRegion *region : layer->regions())
            add_entity(&region->perimeters, add_entity);
    }
    return len;
}

}
TEST_CASE("A lower layer sliver too thin to print does not support the wall above it", "[Perimeters]")
{
    const char *wall_generator   = GENERATE("classic", "arachne");
    const bool  detect_thin_wall = GENERATE(true, false);
    CAPTURE(wall_generator, detect_thin_wall);

    auto overhang_for = [wall_generator, detect_thin_wall](double rib_width) {
        Print print;
        Model model;
        slab_over_rib(print, model, rib_width, printable_rib_config(wall_generator, detect_thin_wall));
        print.process();
        REQUIRE_FALSE(print.objects().empty());
        return overhang_length_at(print, slab_first_layer_z);
    };

    const double no_rib      = overhang_for(0.);
    const double unprintable = overhang_for(unprintable_rib);
    const double printable   = overhang_for(printable_rib);

    REQUIRE(no_rib > scale_(30.));
    CHECK_THAT(unprintable, Catch::Matchers::WithinAbs(no_rib, scale_(1.)));
    CHECK(printable < no_rib - scale_(15.));
}

namespace {

DynamicPrintConfig fuzzy_skin_config(const char *wall_generator)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_generator",             wall_generator },
        { "layer_height",               0.2 },
        { "initial_layer_print_height", 0.2 },
        { "wall_loops",                 1 },
        { "fuzzy_skin",                 "external" },
        { "fuzzy_skin_noise_type",      "classic" },
        { "fuzzy_skin_thickness",       0.3 },
        { "fuzzy_skin_point_distance",  0.8 },
    });
    return config;
}

double mid_span_wall_spread(const Print &print, double print_z)
{
    for (const Layer *layer : print.objects().front()->layers()) {
        if (std::abs(layer->print_z - print_z) > 1e-4)
            continue;
        const BoundingBox bbox  = get_extents(layer->lslices);
        const coord_t     x_min = bbox.min.x() + bbox.size().x() / 5;
        const coord_t     x_max = bbox.max.x() - bbox.size().x() / 5;
        Points            points;
        for (const LayerRegion *region : layer->regions())
            region->perimeters.collect_points(points);
        coord_t spread = 0;
        for (const bool south : { true, false }) {
            coord_t lo = bbox.max.y(), hi = bbox.min.y();
            for (const Point &p : points)
                if (p.x() > x_min && p.x() < x_max && (p.y() < bbox.center().y()) == south) {
                    lo = std::min(lo, p.y());
                    hi = std::max(hi, p.y());
                }
            spread = std::max(spread, hi - lo);
        }
        return unscale<double>(spread);
    }
    return -1.;
}

}
TEST_CASE("Fuzzy skin leaves the walls of a bridge smooth", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    Print print;
    init_and_process_print({ TestMesh::bridge }, print, fuzzy_skin_config(wall_generator));
    REQUIRE_FALSE(print.objects().empty());

    CHECK(mid_span_wall_spread(print, 5.6) > 0.1);
    const double bridged = mid_span_wall_spread(print, 5.2);
    CHECK(bridged >= 0.);
    CHECK(bridged < 0.001);
}

TEST_CASE("Fuzzy skin leaves the walls over an empty layer smooth", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    TriangleMesh mesh     = make_cube(20., 20., 3.);
    TriangleMesh floating = make_cube(20., 20., 3.);
    floating.translate(0.f, 0.f, 5.f);
    mesh.merge(floating);

    Print print;
    init_and_process_print({ mesh }, print, fuzzy_skin_config(wall_generator));
    REQUIRE_FALSE(print.objects().empty());

    CHECK(mid_span_wall_spread(print, 5.6) > 0.1);
    const double floating_first_layer = mid_span_wall_spread(print, 5.2);
    CHECK(floating_first_layer >= 0.);
    CHECK(floating_first_layer < 0.001);
}

namespace {

Print &square_tube(Print &print, Model &model, const DynamicPrintConfig &config, double wall, double far_wall)
{
    ModelObject *object = model.add_object();
    object->name = "square_tube.stl";
    object->add_volume(make_cube(20., 20., 5.), ModelVolumeType::MODEL_PART, false);
    TriangleMesh cavity = make_cube(20. - 2. * wall, 20. - wall - far_wall, 5.);
    cavity.translate(float(wall), float(wall), 0.f);
    object->add_volume(std::move(cavity), ModelVolumeType::NEGATIVE_VOLUME, false);
    object->add_instance();
    object->ensure_on_bed();

    print.auto_assign_extruders(object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();
    return print;
}

DynamicPrintConfig hole_direction_config(int wall_loops, const char *wall_direction)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_generator",             "classic" },
        { "wall_direction",             wall_direction },
        { "wall_loops",                 wall_loops },
        { "layer_height",               0.2 },
        { "initial_layer_print_height", 0.2 },
        { "outer_wall_line_width",      0.42 },
        { "inner_wall_line_width",      0.45 },
        { "detect_thin_wall",           false },
        { "filter_out_gap_fill",        0 },
        { "top_shell_layers",           3 },
        { "bottom_shell_layers",        3 },
        { "only_one_wall_top",          true },
        { "only_one_wall_first_layer",  true },
        { "overhang_reverse",           false },
        { "sparse_infill_density",      "15%" },
    });
    return config;
}

struct WallDirections {
    std::vector<bool> contours_ccw;
    std::vector<bool> holes_ccw;
    int               inner_walls = 0;
    size_t            gap_fills   = 0;
};

std::vector<WallDirections> wall_directions(const Print &print)
{
    std::vector<WallDirections> out;
    for (const Layer *layer : print.objects().front()->layers()) {
        WallDirections &walls = out.emplace_back();
        for (const LayerRegion *region : layer->regions()) {
            walls.gap_fills += region->thin_fills.flatten().entities.size();
            for (const ExtrusionEntity *entity : region->perimeters.flatten().entities) {
                if (! entity->is_loop())
                    continue;
                const ExtrusionLoop *loop = static_cast<const ExtrusionLoop*>(entity);
                if (loop->inset_idx > 0)
                    ++ walls.inner_walls;
                else
                    (loop->loop_role() == elrHole ? walls.holes_ccw : walls.contours_ccw).push_back(loop->polygon().is_counter_clockwise());
            }
        }
    }
    return out;
}

}
TEST_CASE("Holes run against the wall direction", "[Perimeters]")
{
    const auto [wall, far_wall] = GENERATE(std::make_pair(7., 7.), std::make_pair(1., 1.), std::make_pair(1., 3.));
    const int   wall_loops      = GENERATE(1, 2);
    const char *wall_direction  = GENERATE("ccw", "cw");
    CAPTURE(wall, far_wall, wall_loops, wall_direction);

    Print print;
    Model model;
    square_tube(print, model, hole_direction_config(wall_loops, wall_direction), wall, far_wall);
    const std::vector<WallDirections> layers = wall_directions(print);
    REQUIRE(layers.size() == 25);
    if (wall < 2.)
        REQUIRE(layers[layers.size() / 2].gap_fills > 0);

    const bool ccw = std::string(wall_direction) == "ccw";
    for (size_t i = 0; i < layers.size(); ++ i) {
        CAPTURE(i);
        REQUIRE(layers[i].contours_ccw.size() == 1);
        REQUIRE(layers[i].holes_ccw.size() == 1);
        CHECK(layers[i].contours_ccw.front() == ccw);
        CHECK(layers[i].holes_ccw.front() == ! ccw);
    }
}

TEST_CASE("A hole whose outer wall touches the contour's runs with it", "[Perimeters]")
{
    const int   wall_loops     = GENERATE(1, 2);
    const char *wall_direction = GENERATE("ccw", "cw");
    CAPTURE(wall_loops, wall_direction);

    Print print;
    Model model;
    square_tube(print, model, hole_direction_config(wall_loops, wall_direction), 0.8, 0.8);
    const std::vector<WallDirections> layers = wall_directions(print);
    REQUIRE(layers.size() == 25);
    REQUIRE(layers[layers.size() / 2].gap_fills == 0);
    REQUIRE(layers[layers.size() / 2].inner_walls == 0);

    const bool ccw = std::string(wall_direction) == "ccw";
    for (size_t i = 0; i < layers.size(); ++ i) {
        CAPTURE(i);
        REQUIRE(layers[i].contours_ccw.size() == 1);
        REQUIRE(layers[i].holes_ccw.size() == 1);
        CHECK(layers[i].contours_ccw.front() == ccw);
        CHECK(layers[i].holes_ccw.front() == ccw);
    }
}
