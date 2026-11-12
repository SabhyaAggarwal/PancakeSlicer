#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "libslic3r/Exception.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/HeightField.hpp"
#include "libslic3r/Nonplanar/SurfaceClip.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double Inf = std::numeric_limits<double>::infinity();

double deg(double rad) { return rad * 180. / M_PI; }

HeadEnvelope readme_head(double shoulder)
{
    HeadEnvelope head = envelope_from_side_profile({ { 0., 0.5, 0.5, 0.5, 0.5 }, { shoulder, 20., 20., 20., 20. } }, 80., Inf, 0., 0.);
    head.margin         = 0.05;
    head.lateral_margin = 0.;
    return head;
}

boost::filesystem::path bundled_heads_dir() { return boost::filesystem::path(std::string(PROFILES_DIR)) / ".." / "nonplanar" / "heads"; }

std::string read_file(const boost::filesystem::path &path)
{
    boost::nowide::ifstream in(path.string());
    std::stringstream       ss;
    ss << in.rdbuf();
    return ss.str();
}

HeadEnvelope random_head(std::mt19937 &rng, double pitch)
{
    std::uniform_real_distribution<double> u01(0., 1.);
    HeadEnvelope                           head;
    head.margin         = 0.3 * u01(rng);
    head.lateral_margin = 0.5 * u01(rng);
    const int n         = 1 + int(rng() % 6);
    while (int(head.boxes.size()) < n) {
        HeadBox box;
        box.name       = "box" + std::to_string(head.boxes.size());
        box.infinite_x = u01(rng) < 0.1;
        box.infinite_y = !box.infinite_x && u01(rng) < 0.1;
        const double cx = (u01(rng) - 0.5) * 30. * pitch, cy = (u01(rng) - 0.5) * 30. * pitch;
        const double hx = (0.5 + 8. * u01(rng)) * pitch, hy = (0.5 + 8. * u01(rng)) * pitch;
        box.min         = Vec3d(cx - hx, cy - hy, 0.4 + 6. * u01(rng));
        box.max         = Vec3d(cx + hx, cy + hy, box.min.z() + 10.);
        const double g  = head.lateral_margin;
        const bool   over_x = box.infinite_x || (box.min.x() - g <= 0. && 0. <= box.max.x() + g);
        const bool   over_y = box.infinite_y || (box.min.y() - g <= 0. && 0. <= box.max.y() + g);
        if (!(over_x && over_y))
            head.boxes.emplace_back(box);
    }
    return head;
}

HeightField random_field(std::mt19937 &rng, int n, double pitch, bool holes)
{
    std::uniform_real_distribution<double> u01(0., 1.);
    HeightField                            f(BoundingBoxf(Vec2d(-1., 2.), Vec2d(-1. + (n - 1) * pitch, 2. + (n - 1) * pitch)), pitch);
    const double                           ax = 2. * u01(rng), ay = 2. * u01(rng), kx = 0.3 * u01(rng), ky = 0.3 * u01(rng);
    for (int y = 0; y < f.ny(); ++y)
        for (int x = 0; x < f.nx(); ++x)
            f.at(x, y) = (holes && u01(rng) < 0.05) ? HeightField::NoData :
                                                      float(5. + ax * std::sin(kx * x) + ay * std::cos(ky * y) + 1.5 * u01(rng));
    return f;
}

std::vector<double> offset_table(const HeadEnvelope &head, int n, double pitch)
{
    const int           w = 2 * n - 1;
    std::vector<double> t(size_t(w) * w);
    for (int dy = -(n - 1); dy < n; ++dy)
        for (int dx = -(n - 1); dx < n; ++dx)
            t[size_t(dy + n - 1) * w + dx + n - 1] = (dx == 0 && dy == 0) ? Inf :
                                                                           head.underside_height(pitch * Vec2d(dx, dy)) - head.margin;
    return t;
}

HeightField brute_minorant(const HeightField &S, const HeadEnvelope &head)
{
    const int           n = S.nx(), w = 2 * n - 1;
    const auto          t = offset_table(head, n, S.pitch());
    HeightField         out = S;
    for (bool changed = true; changed;) {
        changed = false;
        for (int qy = 0; qy < n; ++qy)
            for (int qx = 0; qx < n; ++qx) {
                if (std::isnan(out.at(qx, qy)))
                    continue;
                double best = out.at(qx, qy);
                for (int py = 0; py < n; ++py)
                    for (int px = 0; px < n; ++px)
                        if (!std::isnan(out.at(px, py)))
                            best = std::min(best, out.at(px, py) + t[size_t(qy - py + n - 1) * w + qx - px + n - 1]);
                if (best < out.at(qx, qy) - 1e-6) {
                    out.at(qx, qy) = float(best);
                    changed        = true;
                }
            }
    }
    return out;
}

HeightField brute_safe_height(const HeightField &material, const HeadEnvelope &head)
{
    const int   n = material.nx(), w = 2 * n - 1;
    const auto  t = offset_table(head, n, material.pitch());
    HeightField out(material.bbox(), material.pitch(), -std::numeric_limits<float>::infinity());
    for (int py = 0; py < n; ++py)
        for (int px = 0; px < n; ++px) {
            double best = -Inf;
            for (int qy = 0; qy < n; ++qy)
                for (int qx = 0; qx < n; ++qx)
                    if (!std::isnan(material.at(qx, qy)))
                        best = std::max(best, material.at(qx, qy) - t[size_t(qy - py + n - 1) * w + qx - px + n - 1]);
            out.at(px, py) = float(best);
        }
    return out;
}

void require_fields_equal(const HeightField &a, const HeightField &b, double tol)
{
    REQUIRE(a.nx() == b.nx());
    REQUIRE(a.ny() == b.ny());
    int mismatches = 0;
    for (size_t i = 0; i < a.data().size(); ++i) {
        const float x = a.data()[i], y = b.data()[i];
        const bool  same = (std::isnan(x) && std::isnan(y)) || (std::isinf(x) && x == y) || std::abs(double(x) - double(y)) <= tol;
        if (!same && ++mismatches <= 3)
            UNSCOPED_INFO("node " << i << ": " << x << " vs " << y);
    }
    CHECK(mismatches == 0);
}

}

TEST_CASE("max_slope reproduces the README plane-clearance table", "[Nonplanar][HeadEnvelope]")
{
    struct Row { double gap, shoulder, expected_deg; };
    const Row row = GENERATE(Row { 0.1, 5., 5.71 }, Row { 0.1, 12., 5.71 }, Row { 0.2, 5., 14.44 }, Row { 0.2, 12., 16.70 },
                             Row { 0.3, 5., 14.71 }, Row { 0.3, 12., 26.57 });
    const HeadEnvelope head = readme_head(row.shoulder);
    for (double phi : { 0., 0.5 * M_PI, M_PI, 1.5 * M_PI })
        CHECK_THAT(deg(head.max_slope(phi, row.gap)), WithinAbs(row.expected_deg, 0.05));
}

TEST_CASE("A 45-degree stepped pyramid allows about 35.3 degrees on the diagonal", "[Nonplanar][HeadEnvelope]")
{
    std::vector<SideProfileRow> rows;
    for (int i = 1; i <= 200; ++i)
        rows.push_back({ 0.25 * i, 0.25 * i, 0.25 * i, 0.25 * i, 0.25 * i });
    HeadEnvelope head   = envelope_from_side_profile(rows, 80., Inf, 0., 0.);
    head.margin         = 0.;
    head.lateral_margin = 0.;
    CHECK_THAT(deg(head.max_slope(0., 0.)), WithinAbs(45., 0.05));
    CHECK_THAT(deg(head.max_slope(0.25 * M_PI, 0.)), WithinAbs(35.26, 0.05));
    CHECK_THAT(deg(head.max_slope(1.25 * M_PI, 0.)), WithinAbs(35.26, 0.05));
}

TEST_CASE("underside_height is zero at the tip and infinite beyond reach", "[Nonplanar][HeadEnvelope]")
{
    HeadEnvelope head = readme_head(5.);
    CHECK_THAT(head.underside_height(Vec2d::Zero()), WithinAbs(0., 0.));
    CHECK_THAT(head.underside_height(Vec2d(10., 0.)), WithinAbs(5., 1e-12));
    const double r = head.reach();
    CHECK_THAT(r, WithinAbs(20. * std::sqrt(2.), 1e-9));
    CHECK(std::isinf(head.underside_height(Vec2d(r + 0.1, 0.))));
    CHECK(std::isinf(head.underside_height(Vec2d(0.75 * r, 0.75 * r))));
    CHECK(std::isinf(head.gantry_clearance()));

    HeadEnvelope with_gantry = envelope_from_side_profile({ { 0., 0.5, 0.5, 0.5, 0.5 } }, 80., 22., 8., 37.);
    CHECK_THAT(with_gantry.gantry_clearance(), WithinAbs(22., 1e-12));
    CHECK_THAT(with_gantry.underside_height(Vec2d(1000., 10.)), WithinAbs(22., 1e-12));
    CHECK(std::isinf(with_gantry.underside_height(Vec2d(1000., 0.))));
}

TEST_CASE("Lateral margin widens body boxes but not nozzle boxes", "[Nonplanar][HeadEnvelope]")
{
    HeadEnvelope head;
    head.margin         = 0.;
    head.lateral_margin = 1.;
    head.boxes.push_back({ "nozzle", Vec3d(-0.5, -0.5, 0.), Vec3d(0.5, 0.5, 5.), false, false, true });
    head.boxes.push_back({ "duct", Vec3d(-10., -10., 3.), Vec3d(10., 10., 20.), false, false, false });
    CHECK_THAT(head.underside_height(Vec2d(0.7, 0.)), WithinAbs(3., 1e-12));
    CHECK_THAT(head.underside_height(Vec2d(10.9, 0.)), WithinAbs(3., 1e-12));
    CHECK(std::isinf(head.underside_height(Vec2d(11.1, 0.))));
    CHECK_THAT(head.max_slope(0., 0.2), WithinAbs(std::atan(3.2 / 11.), 1e-12));
}

TEST_CASE("A head part beside the ray still limits the slope through its projection", "[Nonplanar][HeadEnvelope]")
{
    HeadEnvelope head;
    head.margin         = 0.;
    head.lateral_margin = 0.;
    head.boxes.push_back({ "duct", Vec3d(5., 3., 2.), Vec3d(15., 10., 20.), false, false, false });
    CHECK(std::isinf(head.underside_height(Vec2d(10., 0.))));
    CHECK_THAT(head.max_slope(0., 0.2), WithinAbs(std::atan(2.2 / 15.), 1e-12));
    CHECK_THAT(head.max_slope(0.25 * M_PI, 0.2), WithinAbs(std::atan(2.2 * std::sqrt(2.) / 25.), 1e-12));
    CHECK_THAT(head.max_slope(M_PI + 0.5 * M_PI * 0.9, 0.2), WithinAbs(0.5 * M_PI, 1e-12));
}

TEST_CASE("Every bundled head loads and round-trips through JSON", "[Nonplanar][HeadEnvelope]")
{
    namespace fs = boost::filesystem;
    const fs::path dir = bundled_heads_dir();
    REQUIRE(fs::is_directory(dir));
    std::vector<std::string> ids;
    for (fs::directory_iterator it(dir), end; it != end; ++it) {
        if (it->path().extension() != ".json")
            continue;
        INFO(it->path().string());
        const HeadEnvelope head = load_head_envelope(read_file(it->path()));
        CHECK(head.id == it->path().stem().string());
        CHECK_FALSE(head.empty());
        CHECK_FALSE(head.provenance.empty());
        CHECK_FALSE(head.calibrated);
        CHECK_THAT(head.underside_height(Vec2d::Zero()), WithinAbs(0., 0.));
        CHECK(load_head_envelope(save_head_envelope(head)) == head);
        for (double a : head.slope_map(16, 0.2)) {
            CHECK(a >= 0.);
            CHECK(a <= 0.5 * M_PI);
        }
        ids.push_back(head.id);
    }
    for (const char *id : { "prusa-mini", "prusa-mk4-nextruder", "bambu-a1", "bambu-x1-p1", "bambu-h2d", "bambu-h2s", "bambu-p2s", "bambu-x2d",
                            "prodcan3", "generic-conservative" })
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
}

TEST_CASE("The bundled generic head matches the built-in one", "[Nonplanar][HeadEnvelope]")
{
    const HeadEnvelope file    = load_head_envelope(read_file(bundled_heads_dir() / "generic-conservative.json"));
    const HeadEnvelope &builtin = HeadLibrary::generic_conservative();
    CHECK(file.id == builtin.id);
    REQUIRE(file.boxes.size() == builtin.boxes.size());
    const auto a = file.slope_map(72, 0.2), b = builtin.slope_map(72, 0.2);
    for (size_t i = 0; i < a.size(); ++i)
        CHECK_THAT(a[i], WithinAbs(b[i], 1e-9));
    CHECK_THAT(file.gantry_clearance(), WithinAbs(builtin.gantry_clearance(), 1e-12));
}

TEST_CASE("HeadLibrary finds bundled heads and resolves unknown ids to the generic head", "[Nonplanar][HeadEnvelope]")
{
    const std::string previous = resources_dir();
    set_resources_dir((boost::filesystem::path(std::string(PROFILES_DIR)) / "..").string());
    HeadLibrary::instance().reload();
    const HeadEnvelope *mini = HeadLibrary::instance().find("prusa-mini");
    REQUIRE(mini != nullptr);
    CHECK(mini->id == "prusa-mini");
    CHECK(HeadLibrary::instance().find("no-such-head") == nullptr);
    const auto ids = HeadLibrary::instance().ids();
    CHECK(std::is_sorted(ids.begin(), ids.end()));
    CHECK(std::find(ids.begin(), ids.end(), "generic-conservative") != ids.end());
    set_resources_dir(previous);
    HeadLibrary::instance().reload();
}

TEST_CASE("Side-profile JSON becomes one box per row from its height to the top", "[Nonplanar][HeadEnvelope]")
{
    const HeadEnvelope head = load_head_envelope(R"({
        "schema": "orca.head_envelope.v1", "id": "t", "margin": 0.1, "lateral_margin": 0,
        "side_profile": [ { "height": 2.5, "xpos": 4, "xneg": 4, "ypos": 4, "yneg": 4 },
                          { "height": 0, "xpos": 0.5, "xneg": 0.5, "ypos": 0.5, "yneg": 0.5 } ],
        "side_profile_top": 80,
        "boxes": [ { "name": "x_gantry", "min": [0, 8, 22], "max": [0, 37, 74], "infinite_x": true } ],
        "future_field": 1 })");
    REQUIRE(head.boxes.size() == 3);
    CHECK_THAT(head.boxes[0].min.z(), WithinAbs(0., 1e-12));
    CHECK_THAT(head.boxes[1].min.z(), WithinAbs(2.5, 1e-12));
    CHECK_THAT(head.boxes[1].max.z(), WithinAbs(80., 1e-12));
    CHECK(head.boxes[2].infinite_x);
    CHECK_THAT(head.underside_height(Vec2d(3., -3.)), WithinAbs(2.5, 1e-12));
    CHECK_THAT(head.gantry_clearance(), WithinAbs(22., 1e-12));
}

TEST_CASE("An N-side profile row is covered by at most three boxes", "[Nonplanar][HeadEnvelope]")
{
    const int    sides = GENERATE(4, 8, 16);
    std::mt19937 rng(sides);
    std::vector<double> reach;
    for (int k = 0; k < sides; ++k)
        reach.push_back(5. + 20. * std::uniform_real_distribution<double>(0., 1.)(rng));
    HeadEnvelope head;
    head.lateral_margin = 0.;
    head.boxes          = boxes_from_n_side_profile({ { 3., reach } }, 50.);
    CHECK(head.boxes.size() <= 3);
    for (int k = 0; k < sides; ++k)
        for (double t = 0.; t <= 1.; t += 0.05) {
            const double a0 = 2. * M_PI * k / sides, a1 = 2. * M_PI * (k + 1) / sides;
            const Vec2d  p0 = reach[k] * Vec2d(std::cos(a0), std::sin(a0)), p1 = reach[(k + 1) % sides] * Vec2d(std::cos(a1), std::sin(a1));
            for (double s : { 0.999, 0.5, 0.1 })
                CHECK_THAT(head.underside_height(s * ((1. - t) * p0 + t * p1)), WithinAbs(3., 1e-12));
        }
}

TEST_CASE("Malformed head JSON names the offending field", "[Nonplanar][HeadEnvelope]")
{
    CHECK_THROWS_WITH(load_head_envelope(R"({"schema": "orca.head_envelope.v2", "id": "x"})"), Catch::Matchers::ContainsSubstring("schema"));
    CHECK_THROWS_WITH(load_head_envelope(R"({"schema": "orca.head_envelope.v1", "id": "x", "boxes": [{"min": [0, 0], "max": [1, 1, 1]}]})"),
                      Catch::Matchers::ContainsSubstring("boxes[0].min"));
    CHECK_THROWS_WITH(load_head_envelope(R"({"schema": "orca.head_envelope.v1", "id": "x", "margin": "a", "boxes": []})"),
                      Catch::Matchers::ContainsSubstring("margin"));
    CHECK_THROWS_AS(load_head_envelope("{"), RuntimeError);
}

TEST_CASE("head_lipschitz_minorant agrees with brute force", "[Nonplanar][HeadEnvelope]")
{
    const int    seed = GENERATE(range(1, 9));
    std::mt19937 rng(seed);
    const double pitch = 0.5;
    const HeightField  S    = random_field(rng, 64, pitch, seed % 2 == 0);
    const HeadEnvelope head = random_head(rng, pitch);
    const HeightField  fast = head_lipschitz_minorant(S, head, 1000);
    require_fields_equal(fast, brute_minorant(S, head), 1e-3);
    for (size_t i = 0; i < S.data().size(); ++i)
        if (!std::isnan(S.data()[i]))
            REQUIRE(fast.data()[i] <= S.data()[i]);
    require_fields_equal(head_lipschitz_minorant(fast, head, 1000), fast, 0.);
}

TEST_CASE("min_safe_tip_height agrees with brute force", "[Nonplanar][HeadEnvelope]")
{
    const int    seed = GENERATE(range(1, 9));
    std::mt19937 rng(100 + seed);
    const double pitch = 0.5;
    const HeightField  material = random_field(rng, 64, pitch, true);
    const HeadEnvelope head     = random_head(rng, pitch);
    const HeightField  like(material.bbox(), pitch, 0.f);
    require_fields_equal(min_safe_tip_height(material, head, like), brute_safe_height(material, head), 1e-4);
}

TEST_CASE("min_safe_tip_height sees material outside the queried grid", "[Nonplanar][HeadEnvelope]")
{
    HeadEnvelope head;
    head.margin         = 0.5;
    head.lateral_margin = 0.;
    head.boxes.push_back({ "duct", Vec3d(-10., -10., 3.), Vec3d(10., 10., 20.), false, false, false });
    HeightField material(BoundingBoxf(Vec2d(9., 0.), Vec2d(10., 1.)), 0.5, 10.f);
    HeightField like(BoundingBoxf(Vec2d(-20., 0.), Vec2d(4., 4.)), 0.5, 0.f);
    const HeightField safe = min_safe_tip_height(material, head, like);
    CHECK_THAT(safe.at(40, 0), WithinAbs(10. - 2.5, 1e-5));
    CHECK_THAT(safe.at(48, 8), WithinAbs(10. - 2.5, 1e-5));
    CHECK(std::isinf(safe.at(0, 0)));
    CHECK(safe.at(0, 0) < 0.f);
}

TEST_CASE("The tip allowance keeps the nozzle land from flattening a gentle slope", "[Nonplanar][HeadEnvelope]")
{
    HeadEnvelope head;
    head.margin         = 0.3;
    head.lateral_margin = 0.;
    head.tip_allowance  = { 0.75, 0.1 };
    head.boxes.push_back({ "land", Vec3d(-0.5, -0.5, 0.), Vec3d(0.5, 0.5, 2.), false, false, true });
    head.boxes.push_back({ "duct", Vec3d(-20., -20., 3.), Vec3d(20., 20., 30.), false, false, false });
    const double pitch = 0.2;
    auto plane = [&](double slope_deg) {
        HeightField f(BoundingBoxf(Vec2d(0., 0.), Vec2d(40., 4.)), pitch);
        for (int y = 0; y < f.ny(); ++y)
            for (int x = 0; x < f.nx(); ++x)
                f.at(x, y) = float(f.node(x, y).x() * std::tan(slope_deg * M_PI / 180.));
        return f;
    };
    const HeightField gentle = plane(5.), steep = plane(10.);
    require_fields_equal(head_lipschitz_minorant(gentle, head, 100), gentle, 0.);
    const HeightField clipped = head_lipschitz_minorant(steep, head, 100);
    CHECK(clipped.at(clipped.nx() - 1, 0) < steep.at(steep.nx() - 1, 0) - 1.);
    head.tip_allowance = {};
    const HeightField flat = head_lipschitz_minorant(gentle, head, 1000);
    CHECK_THAT(flat.at(flat.nx() - 1, 0), WithinAbs(0., 1e-5));
}

TEST_CASE("slope_cap_minorant caps every slope and stays close to the exact cap", "[Nonplanar][HeadEnvelope]")
{
    const int    seed = GENERATE(range(1, 5));
    std::mt19937 rng(200 + seed);
    const double pitch = 0.5, alpha = (10. + 5. * seed) * M_PI / 180., t = std::tan(alpha);
    const HeightField S   = random_field(rng, 32, pitch, false);
    const HeightField cap = slope_cap_minorant(S, alpha);
    const int         n   = S.nx();
    const double      t2  = t * std::cos(0.5 * std::atan(0.5));
    auto exact_reduced = [&](int qx, int qy) {
        double best = S.at(qx, qy);
        for (int py = 0; py < n; ++py)
            for (int px = 0; px < n; ++px)
                best = std::min(best, S.at(px, py) + t2 * pitch * std::hypot(qx - px, qy - py));
        return best;
    };
    for (int qy = 0; qy < n; ++qy)
        for (int qx = 0; qx < n; ++qx) {
            REQUIRE(cap.at(qx, qy) <= S.at(qx, qy));
            double exact = S.at(qx, qy);
            for (int py = 0; py < n; ++py)
                for (int px = 0; px < n; ++px) {
                    const double d = pitch * std::hypot(qx - px, qy - py);
                    REQUIRE(cap.at(qx, qy) - cap.at(px, py) <= t * d + 1e-4);
                    exact = std::min(exact, S.at(px, py) + t * d);
                }
            REQUIRE(cap.at(qx, qy) <= exact + 1e-4);
            REQUIRE(cap.at(qx, qy) >= exact_reduced(qx, qy) - 1e-4);
        }
}

TEST_CASE("Box kernels on 1000 x 1000 nodes with 20 boxes", "[Nonplanar][HeadEnvelope][Benchmark][.]")
{
    std::mt19937 rng(7);
    const double pitch = 0.2;
    HeightField  S     = random_field(rng, 1000, pitch, false);
    HeadEnvelope head;
    while (head.boxes.size() < 20) {
        const HeadEnvelope more = random_head(rng, pitch * 10.);
        head.boxes.insert(head.boxes.end(), more.boxes.begin(), more.boxes.end());
    }
    head.boxes.resize(20);
    const auto t0 = std::chrono::steady_clock::now();
    const HeightField one_pass = head_lipschitz_minorant(S, head, 1);
    const auto t1 = std::chrono::steady_clock::now();
    const HeightField safe = min_safe_tip_height(S, head, S);
    const auto t2 = std::chrono::steady_clock::now();
    const HeightField fixed = head_lipschitz_minorant(S, head, 1000);
    const auto t3 = std::chrono::steady_clock::now();
    WARN("minorant, one pass: " << std::chrono::duration<double>(t1 - t0).count() << " s; to the fixed point: "
                                << std::chrono::duration<double>(t3 - t2).count() << " s; min_safe_tip_height: "
                                << std::chrono::duration<double>(t2 - t1).count() << " s");
    CHECK(one_pass.nx() == 1000);
    CHECK(safe.nx() == 1000);
}
