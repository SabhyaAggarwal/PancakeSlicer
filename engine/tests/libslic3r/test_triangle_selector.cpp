#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include "libslic3r/libslic3r.h"
#include <vector>
#include <cstddef>
#include <string>

using namespace Slic3r;

static TriangleMesh test_mesh() { return make_sphere(5., 2 * PI / 24); }

static int nibble_at(const std::vector<bool> &bitstream, size_t nibble_idx)
{
    int n = 0;
    for (size_t bit = 0; bit < 4; ++bit)
        n |= int(bitstream[nibble_idx * 4 + bit]) << bit;
    return n;
}

TEST_CASE("Every extruder state survives a serialize/deserialize round trip", "[TriangleSelector]")
{
    const TriangleMesh mesh      = test_mesh();
    const int          max_state = int(EnforcerBlockerType::ExtruderMax);
    REQUIRE(int(mesh.its.indices.size()) >= max_state);

    TriangleSelector selector(mesh);
    for (int state = 1; state <= max_state; ++state)
        selector.set_facet(state - 1, EnforcerBlockerType(state));

    TriangleSelector restored(mesh);
    restored.deserialize(selector.serialize());

    for (int state = 1; state <= max_state; ++state) {
        INFO("Extruder " << state);
        REQUIRE(restored.has_facets(EnforcerBlockerType(state)));
        REQUIRE(restored.num_facets(EnforcerBlockerType(state)) == 1);
    }
}

TEST_CASE("Serialized data reports the extruder states it uses", "[TriangleSelector]")
{
    const TriangleMesh mesh = test_mesh();
    TriangleSelector   selector(mesh);
    selector.set_facet(0, EnforcerBlockerType::Extruder16);
    selector.set_facet(1, EnforcerBlockerType::Extruder32);

    const TriangleSelector::TriangleSplittingData data = selector.serialize();

    REQUIRE(data.used_states.size() == size_t(EnforcerBlockerType::ExtruderMax) + 1);
    REQUIRE(data.used_states[size_t(EnforcerBlockerType::Extruder16)]);
    REQUIRE(data.used_states[size_t(EnforcerBlockerType::Extruder32)]);
    REQUIRE_FALSE(data.used_states[size_t(EnforcerBlockerType::Extruder17)]);

    SECTION("used_states recomputed from the bitstream agrees") {
        TriangleSelector::TriangleSplittingData recomputed = data;
        recomputed.reset_used_states();
        recomputed.update_used_states(0);
        REQUIRE(recomputed.used_states == data.used_states);
    }

    SECTION("has_facets on the raw data agrees") {
        REQUIRE(TriangleSelector::has_facets(data, EnforcerBlockerType::Extruder32));
        REQUIRE_FALSE(TriangleSelector::has_facets(data, EnforcerBlockerType::Extruder17));
    }
}

TEST_CASE("Extruder states up to 17 keep the single-nibble encoding", "[TriangleSelector]")
{
    const int state = GENERATE(3, 8, 16, 17);

    TriangleSelector selector(test_mesh());
    selector.set_facet(0, EnforcerBlockerType(state));
    const std::vector<bool> bitstream = selector.serialize().bitstream;

    INFO("Extruder " << state);
    REQUIRE(bitstream.size() == 8);
    REQUIRE(nibble_at(bitstream, 0) == 0b1100);
    REQUIRE(nibble_at(bitstream, 1) == state - 3);
}

TEST_CASE("Extruder states above 17 are encoded in a second nibble", "[TriangleSelector]")
{
    const int state = GENERATE(18, 25, 32);

    TriangleSelector selector(test_mesh());
    selector.set_facet(0, EnforcerBlockerType(state));
    const std::vector<bool> bitstream = selector.serialize().bitstream;

    INFO("Extruder " << state);
    REQUIRE(bitstream.size() == 12);
    REQUIRE(nibble_at(bitstream, 0) == 0b1100);
    REQUIRE(nibble_at(bitstream, 1) == 0b1111);
    REQUIRE(nibble_at(bitstream, 2) == state - 18);
}

TEST_CASE("Extruder states match the CONST_FILAMENTS hex encoding", "[TriangleSelector]")
{
    struct Case { const char *hex; int state; };
    const auto c = GENERATE(values<Case>({
        {"8", 2}, {"0C", 3}, {"DC", 16}, {"EC", 17}, {"0FC", 18}, {"EFC", 32},
    }));

    const std::string hex = c.hex;
    std::vector<bool> bitstream;
    for (auto it = hex.rbegin(); it != hex.rend(); ++it) {
        const int nibble = *it >= 'A' ? (*it - 'A' + 10) : (*it - '0');
        for (int bit = 0; bit < 4; ++bit)
            bitstream.push_back((nibble >> bit) & 1);
    }

    TriangleSelector::TriangleSplittingData data;
    data.triangles_to_split.emplace_back(0, 0);
    data.bitstream = bitstream;

    INFO("Hex " << c.hex << " -> extruder " << c.state);
    REQUIRE(TriangleSelector::has_facets(data, EnforcerBlockerType(c.state)));
}

static std::vector<bool> pack_nibbles(const std::vector<int> &nibbles)
{
    std::vector<bool> bitstream;
    for (const int nibble : nibbles)
        for (int bit = 0; bit < 4; ++bit)
            bitstream.push_back((nibble >> bit) & 1);
    return bitstream;
}

TEST_CASE("A valid paint stream with nested splits round-trips bit for bit", "[TriangleSelector]")
{
    const TriangleMesh mesh = test_mesh();

    TriangleSelector::TriangleSplittingData data;
    data.triangles_to_split.emplace_back(0, 0);
    const std::vector<int> triangle_0 = {0b0011,
                                         0b1001, 0b1000, 0b0100,
                                         0b0110, 0b1100, 0b1111, 20 - 18, 0b0000, 0b1100, 8 - 3,
                                         0b1000,
                                         0b0100};
    data.bitstream = pack_nibbles(triangle_0);
    data.triangles_to_split.emplace_back(5, int(data.bitstream.size()));
    const std::vector<bool> triangle_5 = pack_nibbles({0b1100, 3 - 3});
    data.bitstream.insert(data.bitstream.end(), triangle_5.begin(), triangle_5.end());
    data.reset_used_states();
    REQUIRE(data.update_used_states(0));

    TriangleSelector restored(mesh);
    restored.deserialize(data);

    REQUIRE(restored.num_facets(EnforcerBlockerType::Extruder20) == 1);
    REQUIRE(restored.num_facets(EnforcerBlockerType::Extruder3) == 1);
    REQUIRE(restored.serialize() == data);
}

TEST_CASE("A truncated or malformed paint stream drops only the damaged triangle", "[TriangleSelector][Regression]")
{
    struct Case { const char *name; std::vector<int> nibbles; };
    const auto c = GENERATE(values<Case>({
        {"three-side split missing two children", {0b0011, 0b1000, 0b1000}},
        {"leaf missing its state nibble",         {0b1100}},
        {"leaf missing its second state nibble",  {0b1100, 0b1111}},
        {"splits nested past the end",            {0b0011, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF,
                                                    0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF}},
        {"one-side split of the nonexistent side 3", {0b1101, 0b1000, 0b1000}},
    }));
    INFO(c.name);

    const TriangleMesh mesh = test_mesh();
    TriangleSelector   intact(mesh);
    intact.set_facet(0, EnforcerBlockerType::Extruder2);

    TriangleSelector::TriangleSplittingData data = intact.serialize();
    data.triangles_to_split.emplace_back(1, int(data.bitstream.size()));
    const std::vector<bool> damaged = pack_nibbles(c.nibbles);
    data.bitstream.insert(data.bitstream.end(), damaged.begin(), damaged.end());

    TriangleSelector restored(mesh);
    REQUIRE_NOTHROW(restored.deserialize(data));
    REQUIRE(restored.serialize() == intact.serialize());

    REQUIRE_NOTHROW(TriangleSelector::has_facets(data, EnforcerBlockerType::Extruder3));

    TriangleSelector::TriangleSplittingData recomputed = data;
    recomputed.reset_used_states();
    REQUIRE_FALSE(recomputed.update_used_states(0));
    REQUIRE(std::none_of(recomputed.used_states.begin(), recomputed.used_states.end(), [](bool used) { return used; }));
}
