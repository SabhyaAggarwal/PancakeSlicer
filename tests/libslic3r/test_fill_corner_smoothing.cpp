#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cmath>
#include "libslic3r/Point.hpp"
#include <cstddef>
#include "libslic3r/Polygon.hpp"
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include "libslic3r/Fill/FillCornerSmoothing.hpp"
#include "libslic3r/Polyline.hpp"

using namespace Slic3r;

namespace {

Polyline asymmetric_corner()
{
    return Polyline{ Point::new_scale(0., 0.), Point::new_scale(10., 0.), Point::new_scale(10., 100.) };
}

double max_turn_cosine(const Polyline &polyline)
{
    double sharpest = 1.;
    for (size_t i = 1; i + 1 < polyline.size(); ++i) {
        const Vec2d incoming = (polyline[i] - polyline[i - 1]).cast<double>().normalized();
        const Vec2d outgoing = (polyline[i + 1] - polyline[i]).cast<double>().normalized();
        sharpest = std::min(sharpest, incoming.dot(outgoing));
    }
    return sharpest;
}

bool contains(const Polyline &polyline, const Point &point)
{
    return std::find(polyline.points.begin(), polyline.points.end(), point) != polyline.points.end();
}

const double tolerance = scaled<double>(0.0125);

}

TEST_CASE("Corner smoothing replaces a sharp vertex by a curve", "[FillCornerSmoothing]")
{
    const Polyline sharp  = asymmetric_corner();
    Polyline       smooth = sharp;
    smooth_polyline_corners(smooth, 1., tolerance);

    REQUIRE(smooth.size() > sharp.size());
    REQUIRE(smooth.front() == sharp.front());
    REQUIRE(smooth.back() == sharp.back());
    REQUIRE(max_turn_cosine(sharp) < 0.1);
    REQUIRE(max_turn_cosine(smooth) > 0.9);
    REQUIRE(smooth.length() < sharp.length());
}

TEST_CASE("Corner smoothing keeps the path untouched at a zero factor", "[FillCornerSmoothing]")
{
    const Polyline sharp = asymmetric_corner();

    Polyline none = sharp;
    smooth_polyline_corners(none, 0., tolerance);
    REQUIRE(none.points == sharp.points);

    Polyline invalid = sharp;
    smooth_polyline_corners(invalid, std::numeric_limits<double>::quiet_NaN(), tolerance);
    REQUIRE(invalid.points == sharp.points);
}

TEST_CASE("Corner smoothing consumes at most half of the shorter leg", "[FillCornerSmoothing]")
{
    Polyline smooth = asymmetric_corner();
    smooth_polyline_corners(smooth, 1., tolerance);

    REQUIRE(contains(smooth, Point::new_scale(5., 0.)));
    REQUIRE(contains(smooth, Point::new_scale(10., 5.)));
    for (const Point &point : smooth.points) {
        REQUIRE(point.x() >= 0);
        REQUIRE(point.y() >= 0);
        REQUIRE(point.x() <= Point::new_scale(10., 0.).x());
        REQUIRE(point.y() <= Point::new_scale(0., 100.).y());
    }
}

TEST_CASE("Corner smoothing scales the curve with the factor", "[FillCornerSmoothing]")
{
    Polyline half = asymmetric_corner();
    smooth_polyline_corners(half, 0.5, tolerance);
    Polyline full = asymmetric_corner();
    smooth_polyline_corners(full, 1., tolerance);

    REQUIRE(contains(half, Point::new_scale(7.5, 0.)));
    REQUIRE(contains(full, Point::new_scale(5., 0.)));
    REQUIRE(full.length() < half.length());
}

TEST_CASE("Corner smoothing leaves hairpins sharp", "[FillCornerSmoothing]")
{
    Polyline hairpin{ Point::new_scale(0., 0.), Point::new_scale(10., 0.), Point::new_scale(0., 0.5) };
    const Polyline sharp = hairpin;
    smooth_polyline_corners(hairpin, 1., tolerance);
    REQUIRE(hairpin == sharp);
}

TEST_CASE("Corner smoothing follows the flattening tolerance", "[FillCornerSmoothing]")
{
    Polyline coarse = asymmetric_corner();
    smooth_polyline_corners(coarse, 1., scaled<double>(0.2));
    Polyline fine = asymmetric_corner();
    smooth_polyline_corners(fine, 1., scaled<double>(0.001));

    REQUIRE(fine.size() > coarse.size());
    REQUIRE(fine.front() == coarse.front());
    REQUIRE(fine.back() == coarse.back());
}

TEST_CASE("Corner smoothing emits no zero length segments", "[FillCornerSmoothing]")
{
    Polyline zigzag;
    for (int i = 0; i < 8; ++i)
        zigzag.points.emplace_back(Point::new_scale(i, i % 2 ? 1. : 0.));
    smooth_polyline_corners(zigzag, 1., tolerance);

    for (size_t i = 1; i < zigzag.size(); ++i)
        REQUIRE((zigzag[i] - zigzag[i - 1]).cast<double>().squaredNorm() > 0.);
}

TEST_CASE("Corner smoothing rounds every vertex of a polygon", "[FillCornerSmoothing]")
{
    const Polygon square{ Point::new_scale(0., 0.), Point::new_scale(10., 0.), Point::new_scale(10., 10.),
                          Point::new_scale(0., 10.) };
    Polygons smooth{ square };
    smooth_polygons_corners(smooth, 1., tolerance);
    const Polyline rounded = smooth.front().split_at_first_point();

    REQUIRE(smooth.front().size() > square.size());
    REQUIRE(max_turn_cosine(rounded) > 0.9);
    const Vec2d incoming = (rounded[rounded.size() - 1] - rounded[rounded.size() - 2]).cast<double>().normalized();
    const Vec2d outgoing = (rounded[1] - rounded[0]).cast<double>().normalized();
    REQUIRE(incoming.dot(outgoing) > 0.9);
    for (const Point &point : smooth.front().points) {
        REQUIRE(point.x() >= 0);
        REQUIRE(point.y() >= 0);
        REQUIRE(point.x() <= Point::new_scale(10., 0.).x());
        REQUIRE(point.y() <= Point::new_scale(0., 10.).y());
    }
}

TEST_CASE("Corner smoothing keeps the ends of a path that returns to its start", "[FillCornerSmoothing][Regression]")
{
    Polyline retrace{ Point::new_scale(0., 0.), Point::new_scale(10., 0.), Point::new_scale(10., 10.),
                      Point::new_scale(5., 10.), Point::new_scale(0., 0.) };
    const Polyline sharp = retrace;
    smooth_polyline_corners(retrace, 1., tolerance);

    REQUIRE(retrace.size() > sharp.size());
    REQUIRE(retrace.front() == sharp.front());
    REQUIRE(retrace.back() == sharp.back());
}

TEST_CASE("Corner smoothing ignores vertices splitting a straight leg", "[FillCornerSmoothing][Regression]")
{
    const Polyline plain{ Point::new_scale(0., 20.), Point::new_scale(10., 0.),
                          Point::new_scale(20., 0.), Point::new_scale(30., 20.) };
    Polyline split = plain;
    split.points.insert(split.points.begin() + 2, Point::new_scale(15., 0.));

    Polyline smooth_plain = plain;
    smooth_polyline_corners(smooth_plain, 1., tolerance);
    Polyline smooth_split = split;
    smooth_polyline_corners(smooth_split, 1., tolerance);

    REQUIRE(smooth_split.points == smooth_plain.points);
    REQUIRE(contains(smooth_plain, Point::new_scale(15., 0.)));
}
