#include <catch2/catch_all.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/HeightField.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>
#include <random>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;

namespace {

HeightField plane_field(double a, double b, double c)
{
    HeightField field(BoundingBoxf(Vec2d(-10., -10.), Vec2d(10., 10.)), 0.5);
    for (int iy = 0; iy < field.ny(); ++iy)
        for (int ix = 0; ix < field.nx(); ++ix) {
            const Vec2d p     = field.node(ix, iy);
            field.at(ix, iy) = float(a * p.x() + b * p.y() + c);
        }
    return field;
}

}

TEST_CASE("Height field sampling reproduces a plane", "[Nonplanar][SlicingSurface]")
{
    const double      a = 0.3, b = -0.2, c = 5.;
    const HeightField field = plane_field(a, b, c);
    REQUIRE(field.nx() == 41);
    REQUIRE(field.ny() == 41);

    std::mt19937                           rng(42);
    std::uniform_real_distribution<double> coord(-9.5, 9.5);
    for (int i = 0; i < 200; ++i) {
        const Vec2d p(coord(rng), coord(rng));
        CHECK_THAT(field.sample(p), WithinAbs(a * p.x() + b * p.y() + c, 1e-4));
        const Vec2d g = field.gradient(p);
        CHECK_THAT(g.x(), WithinAbs(a, 1e-4));
        CHECK_THAT(g.y(), WithinAbs(b, 1e-4));
    }
}

TEST_CASE("Height field sampling outside the grid or next to missing data gives no data", "[Nonplanar][SlicingSurface]")
{
    HeightField field = plane_field(0., 0., 1.);
    CHECK(std::isnan(field.sample(Vec2d(10.1, 0.))));
    CHECK(std::isnan(field.sample(Vec2d(0., -10.1))));

    field.at(20, 20) = HeightField::NoData;
    CHECK(std::isnan(field.sample(Vec2d(0.2, 0.2))));
    CHECK(std::isnan(field.sample(Vec2d(-0.2, 0.3))));
    CHECK_FALSE(std::isnan(field.sample(Vec2d(0.7, 0.7))));
    const Vec2d g = field.gradient(Vec2d(0.25, 0.25));
    CHECK(std::isnan(g.x()));
}

TEST_CASE("Sampled top surface of a sphere matches the analytic sphere", "[Nonplanar][SlicingSurface]")
{
    const double R  = 30.;
    const double fa = 0.03;
    TriangleMesh sphere(its_make_sphere(R, fa));
    sphere.translate(0., 0., float(R));
    sla::IndexedMesh mesh(sphere);

    const double      z_offset = 0.4;
    const HeightField like(BoundingBoxf(Vec2d(-35., -35.), Vec2d(35., 35.)), 0.5);
    const HeightField S = sample_top_surface(mesh, 2. * R + 1., z_offset, like);

    size_t hits = 0;
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix) {
            const Vec2d  p = S.node(ix, iy);
            const double r = p.norm();
            if (r > R) {
                CHECK(std::isnan(S.at(ix, iy)));
                continue;
            }
            if (r > 0.8 * R)
                continue;
            REQUIRE_FALSE(std::isnan(S.at(ix, iy)));
            const double analytic = R + std::sqrt(R * R - r * r) + z_offset;
            CHECK_THAT(S.at(ix, iy), WithinAbs(analytic, 0.01));
            ++hits;
        }
    CHECK(hits > 5000);
}
