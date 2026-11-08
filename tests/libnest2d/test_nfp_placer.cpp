#include <catch2/catch_all.hpp>
#include "libnest2d/libnest2d.hpp"
#include <vector>
#include <cstddef>
#include "libnest2d/geometry_traits.hpp"
#include "libnest2d/common.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "libnest2d_test_utils.hpp"
#include "libnest2d/backends/libslic3r/geometries.hpp"
#include "libnest2d/nester.hpp"
#include "libnest2d/placers/nfpplacer.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

using namespace libnest2d;

namespace {

struct NfpPlacerFixture {
    using Cfg = NfpPlacer::Config;
    Box bin{250000000, 210000000};

    NfpPlacer placer_with(Cfg cfg = {}) const {
        cfg.parallel = false;
        NfpPlacer p{bin};
        p.configure(cfg);
        return p;
    }

    static bool place(NfpPlacer &p, Item &item) {
        auto res = p.pack(item);
        if (res) p.accept(res);
        return bool(res);
    }

    static void place_all(NfpPlacer &p, std::vector<RectangleItem> &items) {
        for (size_t i = 0; i < items.size(); ++i) {
            INFO("packing item " << i);
            REQUIRE(place(p, items[i]));
        }
    }

    void require_disjoint_in_bin(std::vector<RectangleItem> &items) const {
        for (size_t i = 0; i < items.size(); ++i) {
            REQUIRE(sl::isInside(items[i].boundingBox(), bin));
            for (size_t j = i + 1; j < items.size(); ++j) {
                const bool overlaps = Item::intersects(items[i], items[j]) &&
                                      !Item::touches(items[i], items[j]);
                INFO("items " << i << " and " << j);
                REQUIRE_FALSE(overlaps);
            }
        }
    }

    static std::vector<RectangleItem> squares(size_t n, Coord side) {
        return std::vector<RectangleItem>(n, RectangleItem{side, side});
    }
};

}

TEST_CASE_METHOD(NfpPlacerFixture, "NfpPlacer places a single item inside the bin", "[Nesting][Placer]") {
    RectangleItem item{100000000, 100000000};
    NfpPlacer placer = placer_with();

    REQUIRE(place(placer, item));
    REQUIRE(placer.getItems().size() == 1u);
    REQUIRE(sl::isInside(item.boundingBox(), bin));
}

TEST_CASE_METHOD(NfpPlacerFixture, "NfpPlacer rejects an item larger than the bin", "[Nesting][Placer]") {
    NfpPlacer placer = placer_with();
    RectangleItem big{300000000, 300000000};

    auto res = placer.pack(big);
    REQUIRE_FALSE(bool(res));
    REQUIRE(placer.getItems().empty());
}

TEST_CASE_METHOD(NfpPlacerFixture, "NfpPlacer positions the first item for any starting point", "[Nesting][Placer]") {
    using A = Cfg::Alignment;
    auto start = GENERATE(A::CENTER, A::BOTTOM_LEFT, A::BOTTOM_RIGHT,
                          A::TOP_LEFT, A::TOP_RIGHT, A::USER_DEFINED, A::DONT_ALIGN);
    CAPTURE(int(start));

    Cfg cfg;
    cfg.starting_point  = start;
    cfg.best_object_pos = bin.center();
    NfpPlacer placer = placer_with(cfg);

    RectangleItem item{100000000, 100000000};
    auto res = placer.pack(item);
    REQUIRE(bool(res));
    REQUIRE(sl::isInside(item.boundingBox(), bin));
}

TEST_CASE_METHOD(NfpPlacerFixture, "NfpPlacer packs many items without overlap", "[Nesting][Placer]") {
    auto items = squares(GENERATE(2u, 6u, 9u), 60000000);
    NfpPlacer placer = placer_with();

    place_all(placer, items);
    REQUIRE(placer.getItems().size() == items.size());
    require_disjoint_in_bin(items);
}

TEST_CASE_METHOD(NfpPlacerFixture, "NfpPlacer evaluates the rotation candidates", "[Nesting][Placer]") {
    std::vector<RectangleItem> rects = {
        {180000000, 40000000}, {180000000, 40000000}, {180000000, 40000000}};

    Cfg cfg;
    cfg.rotations = {0.0, Pi / 2.0};
    NfpPlacer placer = placer_with(cfg);

    place_all(placer, rects);
    require_disjoint_in_bin(rects);
}

TEST_CASE_METHOD(NfpPlacerFixture, "NfpPlacer's final alignment keeps the pile clear of a fixed obstacle", "[Nesting][Placer]") {
    RectangleItem obstacle{80000000, 80000000};
    obstacle.translation({-40000000, -40000000});
    obstacle.markAsFixedInBin(0);
    obstacle.is_virt_object = true;

    auto items = squares(4, 30000000);
    {
        NfpPlacer placer = placer_with();
        NfpPlacer::ItemGroup fixed;
        fixed.emplace_back(obstacle);
        placer.preload(fixed);
        place_all(placer, items);
    }

    for (size_t i = 0; i < items.size(); ++i) {
        INFO("item " << i);
        const bool overlaps = Item::intersects(items[i], obstacle) &&
                              !Item::touches(items[i], obstacle);
        REQUIRE_FALSE(overlaps);
    }
}
