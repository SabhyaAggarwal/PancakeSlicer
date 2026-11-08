#include <catch2/catch_all.hpp>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

#include "libslic3r/BeltBrim.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

static ExPolygon make_box(coord_t x0, coord_t y0, coord_t x1, coord_t y1)
{
    ExPolygon out;
    out.contour.points = { Point(x0, y0), Point(x1, y0), Point(x1, y1), Point(x0, y1) };
    return out;
}

static void add_hole(ExPolygon &ex, coord_t x0, coord_t y0, coord_t x1, coord_t y1)
{
    Polygon hole;
    hole.points = { Point(x0, y0), Point(x0, y1), Point(x1, y1), Point(x1, y0) };
    ex.holes.emplace_back(std::move(hole));
}

SCENARIO("sweep_ex sweeps a box", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    GIVEN("a 10x10 mm box") {
        const ExPolygons src { make_box(0, 0, 10 * mm, 10 * mm) };

        WHEN("swept 5 mm along -Y") {
            const ExPolygons out = sweep_ex(src, Point(0, -5 * mm));
            THEN("it becomes one 10x15 mm box") {
                REQUIRE(out.size() == 1);
                REQUIRE(out.front().holes.empty());
                const BoundingBox bb = get_extents(out);
                CHECK(bb.min.y() == -5 * mm);
                CHECK(bb.max.y() == 10 * mm);
                CHECK(bb.min.x() == 0);
                CHECK(bb.max.x() == 10 * mm);
                CHECK_THAT(unscale<double>(unscale<double>(out.front().area())),
                           Catch::Matchers::WithinRel(150., 1e-6));
            }
        }

        WHEN("swept by a zero vector") {
            THEN("it is unchanged") {
                const ExPolygons out = sweep_ex(src, Point(0, 0));
                REQUIRE(out.size() == 1);
                CHECK(out.front().area() == src.front().area());
            }
        }

        WHEN("swept diagonally") {
            const ExPolygons out = sweep_ex(src, Point(3 * mm, 3 * mm));
            THEN("area grows by |t| times the perpendicular width") {
                const double expected = 100. + std::sqrt(2. * 9.) * (10. * std::sqrt(2.));
                CHECK_THAT(unscale<double>(unscale<double>(out.front().area())),
                           Catch::Matchers::WithinRel(expected, 1e-6));
            }
        }
    }
}

SCENARIO("sweep_ex closes holes narrower than the sweep", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    GIVEN("a 20x20 mm box with a hole 2 mm tall in the sweep direction") {
        ExPolygon ex = make_box(0, 0, 20 * mm, 20 * mm);
        add_hole(ex, 5 * mm, 9 * mm, 15 * mm, 11 * mm);
        WHEN("swept 5 mm along -Y") {
            const ExPolygons out = sweep_ex(ExPolygons{ ex }, Point(0, -5 * mm));
            THEN("the hole is filled in") {
                REQUIRE(out.size() == 1);
                CHECK(out.front().holes.empty());
            }
        }
    }
    GIVEN("a 20x20 mm box with a hole 12 mm tall in the sweep direction") {
        ExPolygon ex = make_box(0, 0, 20 * mm, 20 * mm);
        add_hole(ex, 5 * mm, 4 * mm, 15 * mm, 16 * mm);
        WHEN("swept 5 mm along -Y") {
            const ExPolygons out = sweep_ex(ExPolygons{ ex }, Point(0, -5 * mm));
            THEN("the hole survives, shrunk by the sweep") {
                REQUIRE(out.size() == 1);
                REQUIRE(out.front().holes.size() == 1);
                const BoundingBox hb = get_extents(out.front().holes.front());
                CHECK(hb.max.y() - hb.min.y() == 7 * mm);
            }
        }
    }
    GIVEN("a box whose edges are parallel to the sweep vector") {
        const ExPolygons src { make_box(0, 0, 10 * mm, 10 * mm) };
        WHEN("swept along +X") {
            const ExPolygons out = sweep_ex(src, Point(4 * mm, 0));
            THEN("the result is the expected rectangle") {
                REQUIRE(out.size() == 1);
                const BoundingBox bb = get_extents(out);
                CHECK(bb.min.x() == 0);
                CHECK(bb.max.x() == 14 * mm);
            }
        }
    }
    GIVEN("a reversed (clockwise) contour") {
        ExPolygon ex = make_box(0, 0, 10 * mm, 10 * mm);
        ex.contour.reverse();
        WHEN("swept") {
            const ExPolygons out = sweep_ex(ExPolygons{ ex }, Point(0, -5 * mm));
            THEN("material is still produced") {
                REQUIRE(! out.empty());
                CHECK(get_extents(out).min.y() == -5 * mm);
            }
        }
    }
}

SCENARIO("Belt flattening round-trips and rescales only the shear axis", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    const double  shear     = GENERATE(0.1, 0.5, 1.0, 3.0);
    const int     from_axis = GENERATE(0, 1);
    DYNAMIC_SECTION("shear " << shear << " axis " << from_axis) {
        const BeltBrimFrame frame { shear, from_axis };

        ExPolygon ex;
        ex.contour.points = { Point(0, 0), Point(20 * mm, 0), Point(20 * mm, 6 * mm),
                              Point(6 * mm, 6 * mm), Point(6 * mm, 20 * mm), Point(0, 20 * mm) };
        add_hole(ex, 2 * mm, 2 * mm, 4 * mm, 4 * mm);
        const ExPolygons src { ex };

        const ExPolygons round_tripped = belt_unflatten(belt_flatten(src, frame), frame);
        REQUIRE(round_tripped.size() == src.size());
        REQUIRE(round_tripped.front().holes.size() == src.front().holes.size());
        for (size_t c = 0; c < src.front().num_contours(); ++ c) {
            const Points &a = src.front().contour_or_hole(c).points;
            const Points &b = round_tripped.front().contour_or_hole(c).points;
            REQUIRE(a.size() == b.size());
            for (size_t i = 0; i < a.size(); ++ i) {
                CHECK(std::abs(a[i].x() - b[i].x()) <= 2);
                CHECK(std::abs(a[i].y() - b[i].y()) <= 2);
                if (from_axis == 0)
                    CHECK(a[i].y() == b[i].y());
                else
                    CHECK(a[i].x() == b[i].x());
            }
        }
    }
}

SCENARIO("Flattening makes shear-axis distances true on-belt distances", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    const double  shear     = GENERATE(0.1, 0.5, 1.0, 3.0);
    const int     from_axis = GENERATE(0, 1);
    DYNAMIC_SECTION("shear " << shear << " axis " << from_axis) {
        const BeltBrimFrame frame { shear, from_axis };
        const double stretch = std::sqrt(1. + shear * shear);
        CHECK_THAT(frame.u_stretch(), Catch::Matchers::WithinRel(stretch, 1e-12));
        CHECK_THAT(frame.cos_tilt() * frame.u_stretch(), Catch::Matchers::WithinRel(1., 1e-12));

        ExPolygon seg = make_box(0, 0, 1 * mm, 1 * mm);
        const BoundingBox flat = get_extents(belt_flatten(ExPolygons{ seg }, frame));
        const coord_t span_u = from_axis == 0 ? flat.max.x() - flat.min.x()
                                              : flat.max.y() - flat.min.y();
        CHECK_THAT(unscale<double>(span_u), Catch::Matchers::WithinRel(stretch, 1e-5));
    }
}

SCENARIO("belt_brim_line_positions walks an exact lattice", "[BeltBrim]") {
    const coord_t pitch  = 420;
    const coord_t anchor = 1000;

    GIVEN("a band narrower than the pitch containing no lattice point") {
        const std::vector<coord_t> us = belt_brim_line_positions(anchor + 100, anchor + 300, pitch, anchor);
        THEN("nothing is emitted") { CHECK(us.empty()); }
    }
    GIVEN("a band containing exactly one lattice point") {
        const std::vector<coord_t> us = belt_brim_line_positions(anchor - 10, anchor + 10, pitch, anchor);
        THEN("that point is emitted") {
            REQUIRE(us.size() == 1);
            CHECK(us.front() == anchor);
        }
    }
    GIVEN("a wide band, as at a shallow belt tilt") {
        const std::vector<coord_t> us = belt_brim_line_positions(anchor, anchor + 5 * pitch, pitch, anchor);
        THEN("several lines are emitted at exactly the pitch") {
            REQUIRE(us.size() == 5);
            for (size_t i = 1; i < us.size(); ++ i)
                CHECK(us[i] - us[i - 1] == pitch);
        }
    }
    GIVEN("two adjacent bands sharing a boundary") {
        const coord_t bound = anchor + 2 * pitch;
        const std::vector<coord_t> lower = belt_brim_line_positions(anchor, bound, pitch, anchor);
        const std::vector<coord_t> upper = belt_brim_line_positions(bound, bound + 2 * pitch, pitch, anchor);
        THEN("the boundary point appears exactly once, in the upper band") {
            CHECK(std::count(lower.begin(), lower.end(), bound) == 0);
            CHECK(std::count(upper.begin(), upper.end(), bound) == 1);
            CHECK(lower.size() == 2);
            CHECK(upper.size() == 2);
        }
    }
    GIVEN("a lattice anchored below zero") {
        THEN("negative lattice points are handled") {
            const std::vector<coord_t> us = belt_brim_line_positions(-3 * pitch, -pitch, pitch, 0);
            REQUIRE(us.size() == 2);
            CHECK(us.front() == -3 * pitch);
            CHECK(us.back() == -2 * pitch);
        }
    }
    GIVEN("a degenerate pitch or band") {
        THEN("nothing is emitted rather than looping forever") {
            CHECK(belt_brim_line_positions(0, 1000, 0, 0).empty());
            CHECK(belt_brim_line_positions(1000, 1000, pitch, 0).empty());
            CHECK(belt_brim_line_positions(1000, 500, pitch, 0).empty());
        }
    }
}

SCENARIO("belt_brim_region reduces to the plate brim without an apron", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    const BeltBrimFrame frame { 1.0, 1 };
    const ExPolygons footprint { make_box(0, 0, 20 * mm, 20 * mm) };
    const coord_t width = 3 * mm;
    const coord_t gap   = 1 * mm;

    GIVEN("outer brim, no apron") {
        const ExPolygons region = belt_brim_region(footprint, true, false, width, gap, 0, 0, frame);
        THEN("it matches the plate brim ring built from the same offsets") {
            const ExPolygons inner = offset_ex(Polygons{ footprint.front().contour }, float(gap), jtRound, SCALED_RESOLUTION);
            const ExPolygons outer = offset_ex(inner, float(width), jtRound, SCALED_RESOLUTION);
            const ExPolygons expect = diff_ex(outer, inner);
            CHECK_THAT(unscale<double>(unscale<double>(area(region))),
                       Catch::Matchers::WithinRel(unscale<double>(unscale<double>(area(expect))), 1e-6));
        }
    }
    GIVEN("no outer and no inner brim") {
        THEN("the region is empty") {
            CHECK(belt_brim_region(footprint, false, false, width, gap, 5 * mm, 0, frame).empty());
        }
    }
    GIVEN("an apron but no brim width") {
        const ExPolygons region = belt_brim_region(footprint, true, false, 0, 0, 5 * mm, 0, frame);
        THEN("brim appears only downhill of the footprint") {
            REQUIRE(! region.empty());
            const BoundingBox rb = get_extents(region);
            CHECK(rb.min.y() < 0);
            CHECK(rb.max.y() <= 0 + 2);
        }
    }
}

SCENARIO("belt_brim_region builds an inner ring inside a hole", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    const BeltBrimFrame frame { 1.0, 1 };
    const coord_t width = 3 * mm;
    const coord_t gap   = 1 * mm;

    GIVEN("a 40x40 mm washer with a 20 mm square hole") {
        ExPolygon washer = make_box(0, 0, 40 * mm, 40 * mm);
        add_hole(washer, 10 * mm, 10 * mm, 30 * mm, 30 * mm);
        const ExPolygons footprint { washer };
        const BoundingBox hole_bb = get_extents(washer.holes.front());

        WHEN("an inner-only brim is requested") {
            const ExPolygons region = belt_brim_region(footprint, false, true, width, gap, 0, 0, frame);
            THEN("a non-empty ring is produced strictly inside the hole") {
                REQUIRE(! region.empty());
                CHECK(area(region) > 0);
                const BoundingBox rb = get_extents(region);
                CHECK(rb.min.x() >= hole_bb.min.x());
                CHECK(rb.min.y() >= hole_bb.min.y());
                CHECK(rb.max.x() <= hole_bb.max.x());
                CHECK(rb.max.y() <= hole_bb.max.y());
            }
        }
        WHEN("no inner brim is requested") {
            THEN("the hole contributes nothing") {
                CHECK(belt_brim_region(footprint, false, false, width, gap, 0, 0, frame).empty());
            }
        }
    }
}

SCENARIO("Leading-edge-only retains the downhill half of the brim region", "[BeltBrim]") {
    const coord_t mm    = scale_(1.);
    const double  shear = GENERATE(1.0, -1.0);
    DYNAMIC_SECTION("shear " << shear) {
        const BeltBrimFrame frame { shear, 1 };
        CHECK((frame.downhill_sign() < 0) == (frame.shear > 0.));

        const ExPolygons region { make_box(0, 0, 20 * mm, 20 * mm) };
        const coordf_t    u_cut = 8.;

        const ExPolygons kept = belt_brim_clip_leading_edge(region, frame, u_cut);

        REQUIRE(! kept.empty());
        const BoundingBox kb = get_extents(kept);
        if (frame.shear > 0.) {
            CHECK(kb.max.y() <= 8 * mm + 2);
            CHECK(kb.min.y() <  8 * mm);
        } else {
            CHECK(kb.min.y() >= 8 * mm - 2);
            CHECK(kb.max.y() >  8 * mm);
        }
        CHECK_THAT(area(kept), Catch::Matchers::WithinRel(area(region) * (frame.shear > 0. ? 8. / 20. : 12. / 20.), 0.01));
        CHECK(kb.min.x() == 0);
        CHECK(kb.max.x() == 20 * mm);
    }
    WHEN("the cut lies beyond the region") {
        const BeltBrimFrame frame { 1.0, 1 };
        const ExPolygons region { make_box(0, 0, 20 * mm, 20 * mm) };
        THEN("a cut past the uphill end keeps everything") {
            CHECK_THAT(area(belt_brim_clip_leading_edge(region, frame, 30.)), Catch::Matchers::WithinRel(area(region), 0.001));
        }
        THEN("a cut before the downhill end keeps nothing") {
            CHECK(belt_brim_clip_leading_edge(region, frame, -5.).empty());
        }
        THEN("an empty region stays empty") {
            CHECK(belt_brim_clip_leading_edge(ExPolygons{}, frame, 8.).empty());
        }
    }
}

SCENARIO("The apron follows the sign of the shear", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    const ExPolygons footprint { make_box(0, 0, 20 * mm, 20 * mm) };

    const ExPolygons pos = belt_brim_region(footprint, true, false, 0, 0, 5 * mm, 0, BeltBrimFrame{ 1.0, 1 });
    const ExPolygons neg = belt_brim_region(footprint, true, false, 0, 0, 5 * mm, 0, BeltBrimFrame{ -1.0, 1 });
    REQUIRE(! pos.empty());
    REQUIRE(! neg.empty());
    CHECK(get_extents(pos).min.y() < 0);
    CHECK(get_extents(neg).max.y() > 20 * mm);
}

SCENARIO("Outer belt brim does not fill the space between contact islands", "[BeltBrim]") {
    const coord_t mm = scale_(1.);
    const BeltBrimFrame frame { 1.0, 1 };
    const coord_t width = 3 * mm;

    GIVEN("two contact islands 4 mm apart, closer than 2 x brim width") {
        const ExPolygons footprint {
            make_box(0,        0, 10 * mm, 10 * mm),
            make_box(14 * mm,  0, 24 * mm, 10 * mm),
        };
        const ExPolygons region = belt_brim_region(footprint, true, false, width, 0, 0, 0, frame);
        THEN("no brim is placed in the gap between them") {
            REQUIRE(! region.empty());
            for (const coord_t x : { 12 * mm, coord_t(10.5 * mm), coord_t(13.5 * mm) }) {
                const Point probe(x, 5 * mm);
                bool covered = false;
                for (const ExPolygon &ex : region)
                    if (ex.contains(probe)) { covered = true; break; }
                CHECK(! covered);
            }
        }
        THEN("brim is still placed outside the pair") {
            bool outside_covered = false;
            const Point probe(-1 * mm, 5 * mm);
            for (const ExPolygon &ex : region)
                if (ex.contains(probe)) { outside_covered = true; break; }
            CHECK(outside_covered);
        }
    }

    GIVEN("an apron on a fragmented footprint") {
        const ExPolygons footprint {
            make_box(0,        0, 10 * mm, 10 * mm),
            make_box(14 * mm,  0, 24 * mm, 10 * mm),
        };
        const ExPolygons region = belt_brim_region(footprint, true, false, width, 0, 5 * mm, 0, frame);
        THEN("the apron still reaches downhill") {
            REQUIRE(! region.empty());
            CHECK(get_extents(region).min.y() <= -5 * mm);
        }
    }
}
