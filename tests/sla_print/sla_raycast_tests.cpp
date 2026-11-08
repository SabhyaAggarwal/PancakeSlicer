#include <array>
#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Point.hpp"
#include <cstdlib>
#include <cmath>
#include <libslic3r/SLA/Hollowing.hpp>
#include <utility>


using namespace Slic3r;

TEST_CASE("Raycaster - find intersections of a line and cylinder")
{
    sla::DrainHole hole{Vec3f(0,0,0), Vec3f(0,0,1), 5, 10};
    std::array<std::pair<float, Vec3d>, 2> out;
    Vec3f s;
    Vec3f dir;

    s = {-1.f, 0, 5.f};
    dir = {1.f, 0, 0};
    hole.get_intersections(s, dir, out);
    REQUIRE(out[0].first == Catch::Approx(-4.f));
    REQUIRE(out[1].first == Catch::Approx(6.f));

    s = {0, 0, -1.f};
    dir = {0, 0, 1.f};
    hole.get_intersections(s, dir, out);
    REQUIRE(std::abs(out[0].first - 1.f) < 0.001f);
    REQUIRE(std::abs(out[1].first - 11.f) < 0.001f);

    s = {0, -1.f, -1.f};
    dir = {0, 1.f, 1.f};
    dir.normalize();
    hole.get_intersections(s, dir, out);
    REQUIRE(std::abs(out[0].first - std::sqrt(2.f)) < 0.001f);
    REQUIRE(std::abs(out[1].first - std::sqrt(72.f)) < 0.001f);
}

#ifdef SLIC3R_HOLE_RAYCASTER
TEST_CASE("Raycaster with loaded drillholes", "[sla_raycast]") 
{
    TriangleMesh cube = load_model("20mm_cube.obj");
    sla::HollowingConfig hcfg;
    std::unique_ptr<TriangleMesh> cube_inside = sla::generate_interior(cube, hcfg);
    REQUIRE(cube_inside);
    
    auto boxbb = cube.bounding_box();
    
    Vec3f center = boxbb.center().cast<float>();
    Vec3f p = {center.x(), 0., center.z()};
    Vec3f normal = {0.f, 1.f, 0.f};
    float radius = 5.f;
    float hole_length = 10.;
    sla::DrainHoles holes = { sla::DrainHole{p, normal, radius, hole_length} };
    
    cube.merge(*cube_inside);
    
    sla::IndexedMesh emesh{cube};
    emesh.load_holes(holes);
    
    Vec3d s = center.cast<double>();
    auto hit = emesh.query_ray_hit(s, {0, 1., 0.});
    REQUIRE(hit.distance() == Catch::Approx(boxbb.size().x() / 2 - hcfg.min_thickness));
    
    s.y() = hcfg.min_thickness / 2;
    hit = emesh.query_ray_hit(s, {0, 0., 1.});
    REQUIRE(hit.distance() == Catch::Approx(radius));

    s.y() = -1.;
    hit = emesh.query_ray_hit(s, {0, 1., 0.});
    REQUIRE(hit.distance() == Catch::Approx(boxbb.max.y() - hcfg.min_thickness - s.y()));
    
    s = center.cast<double>();
    s.z() = boxbb.max.z() - hcfg.min_thickness - 1.;
    hit = emesh.query_ray_hit(s, {0, 0., -1.});
    REQUIRE(hit.distance() == Catch::Approx(s.z() - boxbb.min.z() - hcfg.min_thickness));

    test_support_model_collision("20mm_cube.obj", {}, hcfg, holes);
}
#endif
