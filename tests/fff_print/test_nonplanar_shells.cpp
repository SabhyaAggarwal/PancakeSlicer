#include <catch2/catch_all.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalBottom.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include "test_helpers.hpp"
#include "test_nonplanar_heads.hpp"
#include "test_utils.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <tbb/global_control.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double DomeSphere = 60.;
constexpr double DomeRadius = 30.;
constexpr double DomeBase   = 10.;

double dome_z(double r)
{
    return DomeBase + std::sqrt(DomeSphere * DomeSphere - r * r) - std::sqrt(DomeSphere * DomeSphere - DomeRadius * DomeRadius);
}

TriangleMesh make_dome(int rings = 90, int segments = 256)
{
    indexed_triangle_set its;
    auto ring_vertex = [segments](int ring, int seg) { return 1 + (ring - 1) * segments + (seg % segments); };
    its.vertices.emplace_back(0.f, 0.f, float(dome_z(0.)));
    for (int i = 1; i <= rings; ++i)
        for (int j = 0; j < segments; ++j) {
            const double r = DomeRadius * i / rings, a = 2. * PI * j / segments;
            its.vertices.emplace_back(float(r * std::cos(a)), float(r * std::sin(a)), float(dome_z(r)));
        }
    const int bottom_center = int(its.vertices.size());
    its.vertices.emplace_back(0.f, 0.f, 0.f);
    const int bottom_ring = int(its.vertices.size());
    for (int j = 0; j < segments; ++j) {
        const double a = 2. * PI * j / segments;
        its.vertices.emplace_back(float(DomeRadius * std::cos(a)), float(DomeRadius * std::sin(a)), 0.f);
    }
    for (int j = 0; j < segments; ++j) {
        its.indices.emplace_back(0, ring_vertex(1, j), ring_vertex(1, j + 1));
        for (int i = 1; i < rings; ++i) {
            its.indices.emplace_back(ring_vertex(i, j), ring_vertex(i + 1, j), ring_vertex(i + 1, j + 1));
            its.indices.emplace_back(ring_vertex(i, j), ring_vertex(i + 1, j + 1), ring_vertex(i, j + 1));
        }
        const int top0 = ring_vertex(rings, j), top1 = ring_vertex(rings, j + 1);
        const int bot0 = bottom_ring + j, bot1 = bottom_ring + (j + 1) % segments;
        its.indices.emplace_back(top0, bot0, bot1);
        its.indices.emplace_back(top0, bot1, top1);
        its.indices.emplace_back(bottom_center, bot1, bot0);
    }
    return TriangleMesh(std::move(its));
}

constexpr double PillarX = DomeRadius + 4. + 3.;

TriangleMesh make_dome_pillar()
{
    TriangleMesh mesh   = make_dome();
    TriangleMesh pillar(its_make_cylinder(3., 40.));
    pillar.translate(float(PillarX), 0.f, 0.f);
    mesh.merge(pillar);
    return mesh;
}

DynamicPrintConfig mk4_config(int top_shells, const std::string &head = {}, const std::string &fallback = "zaa")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "nozzle_diameter", "0.4" },
        { "layer_height", "0.2" },
        { "initial_layer_print_height", "0.2" },
        { "line_width", "0.45" },
        { "wall_loops", "2" },
        { "top_shell_layers", "5" },
        { "bottom_shell_layers", "4" },
        { "sparse_infill_density", "15%" },
        { "skirt_loops", "0" },
        { "brim_type", "no_brim" },
        { "enable_support", "0" },
        { "nonplanar_mode", "manual" },
        { "nonplanar_top_shells", std::to_string(top_shells) },
        { "nonplanar_fallback", fallback },
        { "nonplanar_head", head },
        { "machine_end_gcode", ";MAX_LAYER_Z {max_layer_z}\n" },
    });
    return config;
}

std::string commands_only(const std::string &gcode)
{
    std::istringstream in(gcode);
    std::string        line, out;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find(';'));
        if (!line.empty())
            out += line + "\n";
    }
    return out;
}

Vec2d dome_center(const PrintObject &object) { return -unscale(object.center_offset()); }

bool inside(const ExPolygons &area, const Point &pt)
{
    return std::any_of(area.begin(), area.end(), [&pt](const ExPolygon &expoly) { return expoly.contains(pt); });
}

double footprint_area(const ConformalResult &result)
{
    double a = 0.;
    for (const ConformalPatch &patch : result.patches)
        a += area(patch.footprint);
    return a * SCALING_FACTOR * SCALING_FACTOR;
}

}

TEST_CASE("Curved top shells off leave the G-code unchanged whatever the other non-planar settings", "[Nonplanar][ConformalShells]")
{
    const std::string reference = slice({ make_dome() }, mk4_config(0));
    DynamicPrintConfig config = mk4_config(0);
    config.set_deserialize_strict({
        { "nonplanar_max_slope", "20" },
        { "nonplanar_surface_resolution", "0.3" },
        { "nonplanar_min_patch_area", "4" },
        { "nonplanar_shell_layer_height", "0.15" },
    });
    const std::string other = slice({ make_dome() }, config);
    CHECK(commands_only(other) == commands_only(reference));
    CHECK(reference.find(";_NONPLANAR_BEGIN") == std::string::npos);
}

TEST_CASE("Three curved top shells follow the dome", "[Nonplanar][ConformalShells]")
{
    Print  print;
    Model  model;
    DynamicPrintConfig config = mk4_config(3, {}, "none");
    config.set_deserialize_strict({ { "nonplanar_infill_depth", "0" } });
    init_print({ make_dome() }, print, model, config);
    const auto        t0    = std::chrono::steady_clock::now();
    const std::string gcode = Test::gcode(print);
    WARN("Dome slice + export: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s");

    const PrintObject     &object = *print.objects().front();
    const ConformalResult &result = object.conformal();
    REQUIRE_FALSE(result.patches.empty());
    for (const PatchDiagnostics &diag : result.diagnostics)
        WARN("patch " << diag.patch_id << ": max error " << diag.max_surface_error << " mm, rms " << diag.rms_surface_error
                      << " mm, max slope " << diag.max_slope_deg << " deg, fallback " << diag.fallback_area << " mm^2");
    WARN("Footprint " << footprint_area(result) << " mm^2 of " << PI * DomeRadius * DomeRadius << " mm^2");

    {
        std::map<int, std::multiset<int>> blocks;
        std::istringstream                in(gcode);
        std::string                       line;
        while (std::getline(in, line))
            if (line.rfind(";_NONPLANAR_BEGIN", 0) == 0) {
                int patch = -1, shell = -1;
                REQUIRE(std::sscanf(line.c_str(), ";_NONPLANAR_BEGIN patch=%d shell=%d", &patch, &shell) == 2);
                blocks[patch].insert(shell);
            }
        REQUIRE(blocks.size() == size_t(std::count_if(result.patches.begin(), result.patches.end(), patch_has_extrusions)));
        for (const auto &[patch, shells] : blocks) {
            std::multiset<int> printed;
            for (size_t k = 0; k < result.patches[size_t(patch)].shell_paths.size(); ++k)
                if (pass_prints(result.patches[size_t(patch)].shell_paths[k]))
                    printed.insert(int(k));
            CHECK(shells == printed);
        }
    }

    {
        const double tolerance = config.opt_float("nonplanar_max_surface_error") + 0.01;
        const Vec2d  center    = dome_center(object);
        size_t       checked   = 0;
        ExPolygons footprints;
        for (const ConformalPatch &patch : result.patches)
            append(footprints, patch.footprint);
        const ExPolygons inner = offset_ex(union_ex(footprints), -float(scale_(2. * config.opt_float("nonplanar_surface_resolution"))));
        for (const ConformalPatch &patch : result.patches) {
            REQUIRE(patch.shell_paths.size() == 3);
            const double host_z = object.layers()[patch.host_layer]->print_z;
            for (const ExtrusionEntity *ee : patch.shell_paths.back().entities) {
                const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
                REQUIRE(path != nullptr);
                CHECK(path->role() == erTopSolidInfill);
                for (size_t i = 0; i < path->polyline.points.size(); ++i) {
                    const Point3 &pt = path->polyline.points[i];
                    if (path->bead_height[i] == 0.f || !inside(inner, pt.to_point()))
                        continue;
                    const double r = (unscale(pt.to_point()) - center).norm();
                    CHECK_THAT(host_z + unscale<double>(pt.z()), WithinAbs(dome_z(r), tolerance));
                    ++checked;
                }
            }
        }
        CHECK(checked > 10);
    }

    {
        const double h_min = 0.5 * config.opt_float("layer_height");
        for (const ConformalPatch &patch : result.patches) {
            const ExPolygons shell_area = offset_ex(diff_ex(patch.footprint, patch.wall_band), -float(scale_(0.05 + 0.5 * 0.45)));
            for (size_t i = 0; i <= patch.host_layer; ++i) {
                const Layer *layer = object.layers()[i];
                Polylines    polylines;
                for (const LayerRegion *layerm : layer->regions()) {
                    layerm->fills.collect_polylines(polylines);
                    layerm->perimeters.collect_polylines(polylines);
                }
                for (const Polyline &pl : polylines)
                    for (const Point &pt : pl.points) {
                        if (!inside(shell_area, pt))
                            continue;
                        const float base = patch.base.sample(unscale(pt));
                        if (std::isnan(base))
                            continue;
                        CHECK(layer->print_z <= base + config.opt_float("layer_height") - h_min + 0.1);
                    }
            }
        }
    }

    {
        double conformal_top = 0.;
        for (const ConformalPatch &patch : result.patches)
            conformal_top = std::max(conformal_top, patch.z_max);
        const size_t pos = gcode.find(";MAX_LAYER_Z ");
        REQUIRE(pos != std::string::npos);
        CHECK(std::stod(gcode.substr(pos + 13)) >= conformal_top - 1e-3);

        std::istringstream in(gcode);
        std::string        line;
        bool               inside = false;
        double             top_z  = 0.;
        while (std::getline(in, line)) {
            if (line.rfind(";_NONPLANAR_BEGIN", 0) == 0)
                inside = true;
            else if (line.rfind(";_NONPLANAR_END", 0) == 0)
                inside = false;
            else if (inside && line.rfind("G1 ", 0) == 0)
                if (const size_t z = line.find(" Z"); z != std::string::npos && line.find(" E") != std::string::npos)
                    top_z = std::max(top_z, std::stod(line.substr(z + 2)));
        }
        CHECK(top_z > 0.);
        CHECK(top_z <= std::stod(gcode.substr(pos + 13)) + 1e-3);
    }
}

TEST_CASE("Curved shells on a real head cover most of the dome", "[Nonplanar][ConformalShells]")
{
    BundledHeads heads;
    REQUIRE(HeadLibrary::instance().find("prusa-mk4-nextruder") != nullptr);
    Print print;
    Model model;
    init_print({ make_dome() }, print, model, mk4_config(3, "prusa-mk4-nextruder"));
    print.process();
    const ConformalResult &result = print.objects().front()->conformal();
    REQUIRE_FALSE(result.patches.empty());
    for (const PatchDiagnostics &diag : result.diagnostics)
        WARN("MK4 head, patch " << diag.patch_id << ": max error " << diag.max_surface_error << " mm, max slope " << diag.max_slope_deg
                                << " deg, fallback " << diag.fallback_area << " mm^2");
    const std::vector<double> limits    = HeadLibrary::instance().find("prusa-mk4-nextruder")->slope_map(72, 0.2);
    const double              alpha     = *std::min_element(limits.begin(), limits.end());
    const double              reachable = PI * std::pow(DomeSphere * std::sin(alpha), 2);
    WARN("MK4 head: plane limit " << Geometry::rad2deg(alpha) << " to "
                                  << Geometry::rad2deg(*std::max_element(limits.begin(), limits.end())) << " deg, footprint "
                                  << footprint_area(result) << " mm^2, slope-reachable " << reachable << " mm^2");
    CHECK(footprint_area(result) >= 0.9 * reachable);
}

TEST_CASE("Patch nodes the head would push into a neighbouring pillar are excluded", "[Nonplanar][ConformalShells]")
{
    BundledHeads heads;
    const std::string head_id = "prusa-mk4-nextruder";
    const HeadEnvelope *head = HeadLibrary::instance().find(head_id);
    REQUIRE(head != nullptr);

    Print alone_print, pillar_print;
    Model alone_model, pillar_model;
    init_print({ make_dome() }, alone_print, alone_model, mk4_config(3, head_id));
    alone_print.process();
    init_print({ make_dome_pillar() }, pillar_print, pillar_model, mk4_config(3, head_id));
    pillar_print.process();

    const PrintObject     &object = *pillar_print.objects().front();
    const ConformalResult &result = object.conformal();
    REQUIRE_FALSE(result.patches.empty());
    CHECK(footprint_area(result) <= footprint_area(alone_print.objects().front()->conformal()) + 1e-3);

    double z_top = 0.;
    for (const ConformalPatch &patch : result.patches)
        z_top = std::max(z_top, patch.z_max);
    const Vec2d pillar = dome_center(object) + Vec2d(PillarX, 0.);
    std::vector<Vec2d> pillar_points;
    for (double x = -3.; x <= 3.; x += 0.5)
        for (double y = -3.; y <= 3.; y += 0.5)
            if (x * x + y * y <= 9.)
                pillar_points.emplace_back(pillar + Vec2d(x, y));
    size_t checked = 0;
    for (const ConformalPatch &patch : result.patches) {
        const ExPolygons shell_area = diff_ex(patch.footprint, patch.wall_band);
        const HeightField &S1 = patch.surface;
        for (int iy = 0; iy < S1.ny(); ++iy)
            for (int ix = 0; ix < S1.nx(); ix += 2) {
                const Vec2d p = S1.node(ix, iy);
                if (std::isnan(S1.at(ix, iy)) || !inside(shell_area, Point::new_scale(p.x(), p.y())))
                    continue;
                double clearance = std::numeric_limits<double>::infinity();
                for (const Vec2d &q : pillar_points)
                    clearance = std::min(clearance, S1.at(ix, iy) + head->underside_height(q - p) - head->margin - z_top);
                CHECK(clearance >= -0.06);
                ++checked;
            }
    }
    CHECK(checked > 0);
}

TEST_CASE("Curved top shells follow the saddle", "[Nonplanar][ConformalShells]")
{
    BundledHeads heads;
    REQUIRE(HeadLibrary::instance().find("test-cone30") != nullptr);
    Print        print;
    Model        model;
    const DynamicPrintConfig config = mk4_config(3, "test-cone30", "none");
    init_print({ load_model("nonplanar/saddle.obj") }, print, model, config);
    print.process();
    const PrintObject     &object = *print.objects().front();
    const ConformalResult &result = object.conformal();
    REQUIRE_FALSE(result.patches.empty());
    WARN("Saddle footprint " << footprint_area(result) << " mm^2 of 3600 mm^2");

    const double tolerance = config.opt_float("nonplanar_max_surface_error") + 0.01;
    const Vec2d  origin    = dome_center(object);
    size_t       checked   = 0;
    ExPolygons footprints;
    for (const ConformalPatch &patch : result.patches)
        append(footprints, patch.footprint);
    const ExPolygons inner = offset_ex(union_ex(footprints), -float(scale_(0.4)));
    for (const ConformalPatch &patch : result.patches) {
        const double     host_z = object.layers()[patch.host_layer]->print_z;
        for (const ExtrusionEntity *ee : patch.shell_paths.back().entities) {
            const auto *path = static_cast<const ExtrusionPathConformal *>(ee);
            for (size_t i = 0; i < path->polyline.points.size(); ++i) {
                const Point3 &pt = path->polyline.points[i];
                if (path->bead_height[i] == 0.f || !inside(inner, pt.to_point()))
                    continue;
                const Vec2d p = unscale(pt.to_point()) - origin;
                CHECK_THAT(host_z + unscale<double>(pt.z()), WithinAbs(21.25 + (p.x() * p.x() - p.y() * p.y()) / 80., tolerance));
                ++checked;
            }
        }
    }
    CHECK(checked > 10);
}

TEST_CASE("Benchy patch extraction time", "[.][Nonplanar][ConformalShells][Benchmark]")
{
    const char *path = std::getenv("NONPLANAR_BENCHY");
    if (path == nullptr)
        SKIP("NONPLANAR_BENCHY is not set");
    BundledHeads heads;
    Print        print;
    Model        model;
    TriangleMesh benchy;
    REQUIRE(benchy.ReadSTLFile(path));
    const char *head = std::getenv("NONPLANAR_HEAD");
    init_print({ std::move(benchy) }, print, model, mk4_config(3, head != nullptr ? head : "prusa-mk4-nextruder"));
    const std::string gcode  = Test::gcode(print);
    size_t            blocks = 0;
    for (size_t pos = gcode.find(";_NONPLANAR_BEGIN"); pos != std::string::npos; pos = gcode.find(";_NONPLANAR_BEGIN", pos + 1))
        ++blocks;
    CHECK(blocks == 3 * print.objects().front()->conformal().patches.size());
    PrintObject &object = *const_cast<PrintObject *>(print.objects().front());
    tbb::global_control threads(tbb::global_control::max_allowed_parallelism, 8);
    const auto      t0     = std::chrono::steady_clock::now();
    ConformalResult result = build_conformal_shells(object);
    WARN("Benchy, 8 threads: " << result.patches.size() << " patches, " << footprint_area(result) << " mm^2, patch extraction and shells "
                               << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s");
}
