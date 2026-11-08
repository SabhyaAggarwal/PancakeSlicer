#include <catch2/catch_all.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalBottom.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;

namespace {

DynamicPrintConfig dome_config(bool ironing)
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
        { "nonplanar_top_shells", "3" },
        { "nonplanar_ironing", ironing ? "1" : "0" },
        { "ironing_flow", "10%" },
    });
    return config;
}

}

TEST_CASE("Conformal ironing runs on the dome's top shell", "[Nonplanar][ConformalIroning]")
{
    Print                    print;
    Model                    model;
    init_print({ load_model("nonplanar/dome.obj") }, print, model, dome_config(true));
    const std::string gcode = Test::gcode(print);

    const PrintObject     &object = *print.objects().front();
    const ConformalResult &result = object.conformal();
    REQUIRE_FALSE(result.patches.empty());
    size_t ironed = 0;
    for (const ConformalPatch &patch : result.patches) {
        ironed += !patch.ironing.empty();
        for (const ExtrusionEntity *ee : patch.ironing.entities) {
            const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
            REQUIRE(path != nullptr);
            CHECK(path->role() == erIroning);
            CHECK(path->shell == patch.shells);
        }
    }
    CHECK(ironed > 0);

    const Vec2d        shift = unscale(object.instances().front().shift);
    std::istringstream in(gcode);
    std::string        line;
    std::map<int, int> last_shell;
    int                patch_id = -1, shell = -1;
    double             x = 0., y = 0., z = 0.;
    size_t             checked = 0;
    while (std::getline(in, line)) {
        if (std::sscanf(line.c_str(), ";_NONPLANAR_BEGIN patch=%d shell=%d", &patch_id, &shell) == 2) {
            const ConformalPatch &patch = result.patches[size_t(patch_id)];
            if (shell == patch.shells) {
                int top = -1;
                for (int k = 0; k < patch.shells; ++k)
                    if (pass_prints(patch.shell_paths[size_t(k)]))
                        top = k;
                const auto seen = last_shell.find(patch_id);
                CHECK((seen == last_shell.end() ? -1 : seen->second) == top);
            }
            last_shell[patch_id] = shell;
            continue;
        }
        if (line.rfind(";_NONPLANAR_END", 0) == 0) {
            patch_id = -1;
            continue;
        }
        if (line.rfind("G1 ", 0) != 0)
            continue;
        const std::string cmd = line.substr(0, line.find(';'));
        bool              extrudes = false;
        std::istringstream words(cmd);
        std::string        word;
        while (words >> word) {
            if (word[0] == 'X')
                x = std::stod(word.substr(1));
            else if (word[0] == 'Y')
                y = std::stod(word.substr(1));
            else if (word[0] == 'Z')
                z = std::stod(word.substr(1));
            else if (word[0] == 'E')
                extrudes = std::stod(word.substr(1)) > 0.;
        }
        if (patch_id < 0 || !extrudes)
            continue;
        const ConformalPatch &patch = result.patches[size_t(patch_id)];
        if (shell != patch.shells)
            continue;
        const float top = patch.surface.sample(Vec2d(x, y) - shift);
        if (std::isnan(top))
            continue;
        CHECK_THAT(z, WithinAbs(top, 0.015));
        ++checked;
    }
    CHECK(checked > 10);
    for (const ConformalPatch &patch : result.patches)
        if (!patch.ironing.empty())
            CHECK(last_shell[patch.id] == patch.shells);
}

TEST_CASE("Conformal ironing off leaves the curved shells unchanged", "[Nonplanar][ConformalIroning]")
{
    Print print;
    Model model;
    init_print({ load_model("nonplanar/dome.obj") }, print, model, dome_config(false));
    print.process();
    const ConformalResult &result = print.objects().front()->conformal();
    REQUIRE_FALSE(result.patches.empty());
    for (const ConformalPatch &patch : result.patches)
        CHECK(patch.ironing.empty());
}
