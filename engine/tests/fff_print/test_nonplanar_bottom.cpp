#include <catch2/catch_all.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalBottom.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"
#include "test_nonplanar_heads.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double ArchRadius     = 30.;
constexpr double ArchChordError = 0.017;

double arch_underside(double x) { return std::sqrt(ArchRadius * ArchRadius - x * x); }

DynamicPrintConfig arch_config(int bottom_shells)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "nozzle_diameter", "0.4" },
        { "layer_height", "0.2" },
        { "initial_layer_print_height", "0.2" },
        { "line_width", "0.45" },
        { "wall_loops", "2" },
        { "top_shell_layers", "4" },
        { "bottom_shell_layers", "4" },
        { "sparse_infill_density", "15%" },
        { "skirt_loops", "0" },
        { "brim_type", "no_brim" },
        { "enable_support", "1" },
        { "support_type", "normal(auto)" },
        { "support_top_z_distance", "0.2" },
        { "support_interface_top_layers", "2" },
        { "nonplanar_mode", "manual" },
        { "nonplanar_bottom_shells", std::to_string(bottom_shells) },
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

bool inside(const ExPolygons &area, const Point &pt)
{
    return std::any_of(area.begin(), area.end(), [&pt](const ExPolygon &expoly) { return expoly.contains(pt); });
}

void for_each_path(const ExtrusionEntityCollection &collection, const std::function<void(const ExtrusionPath &)> &fn)
{
    for (const ExtrusionEntity *ee : collection.entities) {
        if (const auto *sub = dynamic_cast<const ExtrusionEntityCollection *>(ee))
            for_each_path(*sub, fn);
        else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(ee))
            for (const ExtrusionPath &path : loop->paths)
                fn(path);
        else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(ee))
            for (const ExtrusionPath &path : multi->paths)
                fn(path);
        else if (const auto *path = dynamic_cast<const ExtrusionPath *>(ee))
            fn(*path);
    }
}

}
TEST_CASE("Curved bottom shells follow the arch underside over a contoured support top", "[Nonplanar][ConformalBottom]")
{
    Print                    print;
    Model                    model;
    const DynamicPrintConfig config = arch_config(2);
    init_print({ load_model("nonplanar/arch.obj") }, print, model, config);
    const std::string gcode = Test::gcode(print);

    const PrintObject     &object = *print.objects().front();
    const ConformalResult &result = object.conformal();
    std::vector<const ConformalPatch *> bottoms;
    for (const ConformalPatch &patch : result.patches)
        if (patch.kind == PatchKind::Bottom)
            bottoms.emplace_back(&patch);
    REQUIRE_FALSE(bottoms.empty());
    double footprint = 0.;
    for (const ConformalPatch *patch : bottoms)
        footprint += area(patch->footprint) * SCALING_FACTOR * SCALING_FACTOR;
    WARN(bottoms.size() << " bottom patch(es), footprint " << footprint << " mm^2");

    const double center_x = -unscale<double>(object.center_offset().x());
    const double max_error = config.opt_float("nonplanar_max_surface_error");
    const double gap       = config.opt_float("support_top_z_distance");
    const double pitch     = config.opt_float("nonplanar_surface_resolution");

    {
        size_t checked = 0;
        for (const ConformalPatch *patch : bottoms) {
            REQUIRE(patch->shell_paths.size() == 2);
            const double     host_z = object.layers()[patch->host_layer]->print_z;
            const ExPolygons inner  = offset_ex(patch->footprint, -float(scale_(2. * pitch)));
            for (const ExtrusionEntity *ee : patch->shell_paths.front().entities) {
                const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
                REQUIRE(path != nullptr);
                CHECK(path->role() == erBottomSurface);
                for (size_t i = 0; i < path->polyline.points.size(); ++i) {
                    const Point3 &pt = path->polyline.points[i];
                    if (!inside(inner, pt.to_point()))
                        continue;
                    const double underside = host_z + unscale<double>(pt.z()) - path->bead_height[i];
                    CHECK_THAT(underside, WithinAbs(arch_underside(unscale<double>(pt.x()) - center_x), max_error + ArchChordError + 0.01));
                    ++checked;
                }
            }
        }
        CHECK(checked > 10);
    }

    {
        size_t lifted = 0;
        for (const SupportLayer *layer : object.support_layers())
            for_each_path(layer->support_fills, [&](const ExtrusionPath &path) {
                for (const Point3 &pt : path.polyline.points)
                    for (const ConformalPatch *patch : bottoms) {
                        const float shell_bottom = patch->surface.sample(unscale(pt.to_point()));
                        if (std::isnan(shell_bottom) || !inside(offset_ex(patch->footprint, -float(scale_(2. * pitch))), pt.to_point()))
                            continue;
                        const double z = layer->print_z + unscale<double>(pt.z());
                        CHECK(z <= shell_bottom - gap + 0.02);
                        if (path.z_contoured && pt.z() != 0) {
                            CHECK_THAT(z, WithinAbs(shell_bottom - gap, 0.02));
                            ++lifted;
                        }
                    }
            });
        CHECK(lifted > 10);
    }

    {
        std::istringstream in(gcode);
        std::string        line;
        double             layer_z      = 0.;
        bool               planar_seen  = false;
        bool               in_block     = false;
        size_t             blocks_found = 0;
        while (std::getline(in, line)) {
            if (line.rfind(";Z:", 0) == 0) {
                layer_z     = std::stod(line.substr(3));
                planar_seen = false;
            } else if (line.rfind(";_NONPLANAR_END", 0) == 0)
                in_block = false;
            else if (!in_block && line.rfind(";TYPE:", 0) == 0 &&
                     (line.find("wall") != std::string::npos || line.find("infill") != std::string::npos))
                planar_seen = true;
            else if (int id = -1, shell = -1; std::sscanf(line.c_str(), ";_NONPLANAR_BEGIN patch=%d shell=%d", &id, &shell) == 2) {
                in_block                    = true;
                const ConformalPatch &patch = result.patches[size_t(id)];
                if (patch.kind != PatchKind::Bottom)
                    continue;
                CHECK_THAT(layer_z, WithinAbs(object.layers()[patch.host_layer]->print_z, 1e-3));
                CHECK_FALSE(planar_seen);
                ++blocks_found;
            }
        }
        size_t printing_passes = 0;
        for (const ConformalPatch *patch : bottoms)
            printing_passes += size_t(std::count_if(patch->shell_paths.begin(), patch->shell_paths.end(),
                                                     [](const ExtrusionEntityCollection &shell) { return pass_prints(shell); })) +
                               (pass_prints(patch->ironing) ? 1 : 0);
        CHECK(blocks_found == printing_passes);
    }
}

TEST_CASE("Bottom patches split into bands whose support is printed before them", "[Nonplanar][ConformalBottom]")
{
    BundledHeads       heads;
    DynamicPrintConfig config = arch_config(2);
    config.set_deserialize_strict({ { "nonplanar_head", "test-cone30" } });
    Print print;
    Model model;
    init_print({ load_model("nonplanar/arch.obj") }, print, model, config);
    print.process();

    const PrintObject &object     = *print.objects().front();
    const double       max_relief = 2 * config.opt_float("layer_height") + config.opt_float("support_top_z_distance") -
                                    config.opt_float("nonplanar_max_surface_error");
    size_t             bands      = 0;
    for (const ConformalPatch &patch : object.conformal().patches) {
        if (patch.kind != PatchKind::Bottom)
            continue;
        ++bands;
        REQUIRE(patch.host_layer > 0);
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (int iy = 0; iy < patch.surface.ny(); ++iy)
            for (int ix = 0; ix < patch.surface.nx(); ++ix) {
                const Vec2d p = patch.surface.node(ix, iy);
                if (inside(patch.footprint, Point::new_scale(p.x(), p.y())) && !std::isnan(patch.surface.at(ix, iy))) {
                    lo = std::min(lo, patch.surface.at(ix, iy));
                    hi = std::max(hi, patch.surface.at(ix, iy));
                }
            }
        WARN("band " << patch.id << " S1 " << lo << ".." << hi << " area " << area(patch.footprint) * SCALING_FACTOR * SCALING_FACTOR);
        CHECK(hi - lo <= max_relief + 1e-3);
        const double before_host = object.layers()[patch.host_layer - 1]->print_z;
        for (const SupportLayer *layer : object.support_layers())
            for_each_path(layer->support_fills, [&](const ExtrusionPath &path) {
                for (const Point3 &pt : path.polyline.points)
                    if (inside(offset_ex(patch.footprint, -float(scale_(0.5))), pt.to_point()))
                        CHECK(layer->print_z <= before_host + EPSILON);
            });
    }
    CHECK(bands >= 2);
}

TEST_CASE("Curved bottom shells off leave the arch G-code unchanged", "[Nonplanar][ConformalBottom]")
{
    DynamicPrintConfig config = arch_config(0);
    const std::string  reference = slice({ load_model("nonplanar/arch.obj") }, config);
    CHECK(reference.find(";_NONPLANAR_BEGIN") == std::string::npos);
    config.set_deserialize_strict({ { "nonplanar_max_surface_error", "0.1" }, { "nonplanar_surface_resolution", "0.3" } });
    CHECK(commands_only(slice({ load_model("nonplanar/arch.obj") }, config)) == commands_only(reference));
}
