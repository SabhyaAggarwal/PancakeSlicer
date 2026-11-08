#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <vector>
#include <cstddef>
#include <math.h>
#include "libslic3r/libslic3r.h"
#include <cmath>
#include <cstdint>
#include <limits>

#define SLIC3R_TEST_HARNESS

#include "libslic3r/Point.hpp"
#include "libslic3r/GCode/OrderingStrategies.hpp"
#include "libslic3r/Geometry.hpp"

#include <algorithm>
#include <unordered_set>
#include <catch2/catch_approx.hpp>

using namespace Slic3r;

static double euclidean_path_length(const std::vector<size_t>& path, const Points& centers)
{
    return tsp_cycle_path_length(path, centers);
}

static bool has_crossings(const std::vector<size_t>& path, const Points& centers)
{
    size_t pn = path.size();
    if (pn < 4) return false;
    for (size_t i = 0; i < pn; ++i) {
        size_t i_next = (i + 1) % pn;
        for (size_t j = i + 2; j < pn; ++j) {
            if (j == i_next) continue;
            if (j == (pn - 1) && i == 0) continue;
            size_t j_next = (j + 1) % pn;
            if (Geometry::segments_intersect(
                    centers[path[i]], centers[path[i_next]],
                    centers[path[j]], centers[path[j_next]])) {
                return true;
            }
        }
    }
    return false;
}

static bool is_permutation(const std::vector<size_t>& path, size_t n)
{
    if (path.size() != n) return false;
    std::unordered_set<size_t> seen(path.begin(), path.end());
    for (size_t i = 0; i < n; ++i) {
        if (seen.count(i) != 1) return false;
    }
    return true;
}

static Points make_grid_4x4()
{
    Points pts;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            pts.emplace_back(100000 * col, 100000 * row);
    return pts;
}

static Points make_linear_5()
{
    Points pts;
    for (int i = 0; i < 5; ++i)
        pts.emplace_back(100000 * i, 0);
    return pts;
}

static Points make_ring_8()
{
    Points pts;
    constexpr double R = 100000.0;
    for (int i = 0; i < 8; ++i) {
        double angle = 2.0 * M_PI * i / 8.0;
        pts.emplace_back(static_cast<coord_t>(R * std::cos(angle)),
                         static_cast<coord_t>(R * std::sin(angle)));
    }
    return pts;
}

static Points make_random_16()
{
    Points pts;
    for (int i = 0; i < 16; ++i) {
        uint32_t h = static_cast<uint32_t>(i * 2654435761u);
        coord_t x = static_cast<coord_t>((h >> 16) & 0xFFFF) * 10;
        coord_t y = static_cast<coord_t>(h & 0xFFFF) * 10;
        pts.emplace_back(x, y);
    }
    return pts;
}

TEST_CASE("tsp_2opt_improve reduces path length", "[TSPPostProcessing]") {
    Points centers = make_random_16();
    std::vector<size_t> path(centers.size());
    for (size_t i = 0; i < path.size(); ++i) path[i] = i;
    std::reverse(path.begin(), path.end() - path.size() / 2);

    double before = euclidean_path_length(path, centers);
    tsp_2opt_improve(path, centers);
    double after = euclidean_path_length(path, centers);

    REQUIRE(is_permutation(path, centers.size()));
    CHECK(after <= before);
}

TEST_CASE("tsp_remove_crossings eliminates crossings", "[TSPPostProcessing]") {
    Points centers = make_random_16();
    std::vector<size_t> path(centers.size());
    for (size_t i = 0; i < path.size(); ++i) path[i] = i;
    if (path.size() >= 4) {
        std::reverse(path.begin() + 1, path.end() - 1);
    }

    tsp_remove_crossings(path, centers);
    CHECK(!has_crossings(path, centers));
    REQUIRE(is_permutation(path, centers.size()));
}



TEST_CASE("tsp_rotate_minimize_closing shortens closing edge", "[TSPPostProcessing]") {
    Points centers = make_random_16();
    std::vector<size_t> path(centers.size());
    for (size_t i = 0; i < path.size(); ++i) path[i] = i;

    size_t pn = path.size();
    double min_closing2 = std::numeric_limits<double>::max();
    for (size_t start = 0; start < pn; ++start) {
        size_t last = (start + pn - 1) % pn;
        double d2 = (centers[path[start]].cast<double>() - centers[path[last]].cast<double>()).squaredNorm();
        if (d2 < min_closing2) min_closing2 = d2;
    }

    tsp_rotate_minimize_closing(path, centers);

    double actual_closing2 = (centers[path.front()].cast<double>() - centers[path.back()].cast<double>()).squaredNorm();
    CHECK(actual_closing2 == min_closing2);
    REQUIRE(is_permutation(path, centers.size()));
}

TEST_CASE("tsp_cycle_path_length is correct for triangle", "[TSPPostProcessing]") {
    Points pts;
    pts.emplace_back(0, 0);
    pts.emplace_back(100000, 0);
    pts.emplace_back(50000, 86602);

    std::vector<size_t> path = {0, 1, 2};
    double len = tsp_cycle_path_length(path, pts);
    REQUIRE(len > 290000);
    REQUIRE(len < 310000);
}

TEST_CASE("tsp_max_edge_length finds longest edge", "[TSPPostProcessing]") {
    Points pts;
    pts.emplace_back(0, 0);
    pts.emplace_back(100000, 0);
    pts.emplace_back(50000, 0);

    std::vector<size_t> path = {0, 1, 2};
    double mx = tsp_max_edge_length(path, pts);
    CHECK(mx == Catch::Approx(100000).margin(1));
}

TEST_CASE("snake_core handles empty input", "[Snake]") {
    Points centers;
    auto path = snake_core(centers);
    REQUIRE(path.empty());
}

TEST_CASE("snake_core handles single point", "[Snake]") {
    Points pts{{100, 200}};
    CHECK(snake_core(pts) == std::vector<size_t>{0});
}

TEST_CASE("snake_core handles two points", "[Snake]") {
    Points pts{{100, 200}, {300, 400}};
    auto p2 = snake_core(pts);

    REQUIRE(is_permutation(p2, 2));
}

TEST_CASE("snake produces good path on grid", "[Snake]") {
    Points centers = make_grid_4x4();
    auto path = snake_core(centers);

    REQUIRE(is_permutation(path, centers.size()));
    CHECK(!has_crossings(path, centers));
}

TEST_CASE("snake handles variable Y spacing", "[Snake]") {
    Points pts;
    pts.emplace_back(0, 0);       pts.emplace_back(100000, 0);
    pts.emplace_back(0, 50000);   pts.emplace_back(100000, 50000);
    pts.emplace_back(0, 100000);  pts.emplace_back(100000, 100000);
    pts.emplace_back(0, 1000000); pts.emplace_back(100000, 1000000);

    auto path = snake_core(pts);
    REQUIRE(is_permutation(path, pts.size()));
    CHECK(!has_crossings(path, pts));
}

TEST_CASE("snake handles all points on same Y", "[Snake]") {
    Points pts;
    for (int i = 0; i < 6; ++i)
        pts.emplace_back(100000 * i, 50000);

    auto path = snake_core(pts);
    REQUIRE(is_permutation(path, pts.size()));
}

TEST_CASE("snake_core handles collinear points", "[Snake]") {
    Points centers = make_linear_5();

    auto p2 = snake_core(centers);

    REQUIRE(is_permutation(p2, centers.size()));
}

TEST_CASE("snake_core produces valid paths on ring", "[Snake]") {
    Points centers = make_ring_8();

    auto p2 = snake_core(centers);

    REQUIRE(is_permutation(p2, centers.size()));
}

TEST_CASE("snake_core produces valid paths on random input", "[Snake]") {
    Points centers = make_random_16();

    auto p2 = snake_core(centers);

    REQUIRE(is_permutation(p2, centers.size()));
}

TEST_CASE("snake has no crossings on random input", "[Snake]") {
    Points centers = make_random_16();
    auto path = snake_core(centers);

    REQUIRE(is_permutation(path, centers.size()));
    CHECK(!has_crossings(path, centers));
}

TEST_CASE("snake_core handles duplicate points", "[Snake]") {
    Points pts;
    pts.emplace_back(100, 200);
    pts.emplace_back(100, 200);
    pts.emplace_back(300, 400);

    auto p2 = snake_core(pts);

    REQUIRE(p2.size() == pts.size());
}

TEST_CASE("snake_core handles three points", "[Snake]") {
    Points pts;
    pts.emplace_back(0, 0);
    pts.emplace_back(100000, 0);
    pts.emplace_back(50000, 86602);

    auto p2 = snake_core(pts);

    REQUIRE(is_permutation(p2, 3));
}
