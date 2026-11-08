#include <algorithm>
#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/CAD/SketchImport.hpp"
#include "libslic3r/Utils.hpp"
#include "test_utils.hpp"

#include <cstdlib>
#include <fstream>
#include "libslic3r/Point.hpp"
#include <string>

using namespace Slic3r;

static void write_square_svg(const std::string& path)
{
    std::ofstream f(path);
    f << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10mm\" height=\"10mm\" "
         "viewBox=\"0 0 10 10\">"
         "<path d=\"M0,0 L10,0 L10,10 L0,10 Z\" fill=\"#000000\"/></svg>";
    REQUIRE(f.good());
}

TEST_CASE("svg_to_regions parses a filled path into a region", "[SketchImport]")
{
    ScopedTemporaryFile square(".svg");
    write_square_svg(square.string());

    ImportRegions regs = svg_to_regions(square.string(), 1.0);
    REQUIRE(regs.size() >= 1);
    REQUIRE(regs[0].size() >= 1);
    REQUIRE(regs[0][0].size() >= 4);

    double hi = 0.0;
    for (const auto& region : regs)
        for (const auto& contour : region)
            for (const Vec2d& p : contour)
                hi = std::max(hi, std::max(std::abs(p.x()), std::abs(p.y())));
    REQUIRE(hi > 3.0);
    REQUIRE(hi < 8.0);
}

TEST_CASE("svg_to_regions rejects bad input gracefully", "[SketchImport]")
{
    ScopedTemporaryFile square(".svg");
    write_square_svg(square.string());
    ScopedTemporaryFile missing(".svg");

    REQUIRE(svg_to_regions("", 1.0).empty());
    REQUIRE(svg_to_regions(missing.string(), 1.0).empty());
    REQUIRE(svg_to_regions(square.string(), 0.0).empty());
}

TEST_CASE("transform_regions moves and scales independently", "[SketchImport]")
{
    ImportRegions r = {{ {Vec2d(-1,-1), Vec2d(1,-1), Vec2d(1,1), Vec2d(-1,1)} }};
    ImportRegions t = transform_regions(r, Vec2d(10, 20), 2.0, 3.0);
    REQUIRE(t.size() == 1);
    REQUIRE(t[0][0].size() == 4);
    REQUIRE_THAT(t[0][0][0].x(), Catch::Matchers::WithinAbs(8.0, 1e-9));
    REQUIRE_THAT(t[0][0][0].y(), Catch::Matchers::WithinAbs(17.0, 1e-9));
    REQUIRE_THAT(t[0][0][2].x(), Catch::Matchers::WithinAbs(12.0, 1e-9));
    REQUIRE_THAT(t[0][0][2].y(), Catch::Matchers::WithinAbs(23.0, 1e-9));
    ImportRegions id = transform_regions(r, Vec2d(0,0), 1.0, 1.0);
    REQUIRE_THAT(id[0][0][1].x(), Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("text_to_regions vectorizes glyphs with counters", "[SketchImport]")
{
    std::string font = resources_dir().empty()
                           ? std::string("resources/fonts/HarmonyOS_Sans_SC_Regular.ttf")
                           : resources_dir() + "/fonts/HarmonyOS_Sans_SC_Regular.ttf";
    {
        std::ifstream probe(font);
        if (!probe.good()) {
            SUCCEED("bundled font not reachable in this environment; covered live on :10");
            return;
        }
    }

    REQUIRE(text_to_regions("", 10.0, font).empty());
    REQUIRE(text_to_regions("A", 0.0, font).empty());

    ImportRegions a = text_to_regions("A", 12.0, font);
    REQUIRE(a.size() >= 1);
    bool has_hole = false;
    for (const auto& region : a)
        if (region.size() >= 2) has_hole = true;
    REQUIRE(has_hole);

    ImportRegions ab = text_to_regions("AB", 12.0, font);
    REQUIRE(ab.size() >= a.size());
}
