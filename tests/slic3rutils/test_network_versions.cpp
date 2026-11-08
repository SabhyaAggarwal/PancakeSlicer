#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <vector>
#include "libslic3r/AppConfig.hpp"
#include <cstddef>

#include <catch2/catch_test_macros.hpp>
#include "slic3r/Utils/bambu_networking.hpp"

#include "plugin_test_utils.hpp"

using namespace Slic3r;
namespace fs = boost::filesystem;

namespace {

#if defined(_MSC_VER) || defined(_WIN32)
static const char* PLUGIN_PREFIX = "bambu_networking_";
static const char* PLUGIN_EXT    = ".dll";
#elif defined(__WXMAC__) || defined(__APPLE__)
static const char* PLUGIN_PREFIX = "libbambu_networking_";
static const char* PLUGIN_EXT    = ".dylib";
#else
static const char* PLUGIN_PREFIX = "libbambu_networking_";
static const char* PLUGIN_EXT    = ".so";
#endif

struct PluginFolderFixture
{
    ScopedDataDir data{"netver"};

    PluginFolderFixture()
    {
        fs::create_directories(data.dir / "plugins");
    }

    void add_plugin(const std::string& version)
    {
        boost::nowide::ofstream f((data.dir / "plugins" / (PLUGIN_PREFIX + version + PLUGIN_EXT)).string());
        f << "stub";
    }
};

int count_version(const std::vector<NetworkLibraryVersionInfo>& versions, const std::string& v)
{
    int n = 0;
    for (const auto& info : versions)
        if (info.version == v)
            ++n;
    return n;
}

}

TEST_CASE("Series and managed classification", "[NetworkVersions]")
{
    CHECK(network_plugin_series("02.08.01.53")      == "02.08.01");
    CHECK(network_plugin_series("02.08.01")         == "02.08.01");
    CHECK(network_plugin_series("02.08.01_custom")  == "02.08.01");
    CHECK(network_plugin_series("02.08.01.52-dev")  == "02.08.01");
    CHECK(network_plugin_series(BAMBU_NETWORK_AGENT_VERSION_LEGACY) == BAMBU_NETWORK_AGENT_VERSION_LEGACY);
    CHECK(network_plugin_series("").empty());

    CHECK(is_series_managed_version("02.08.01"));
    CHECK(is_series_managed_version("02.08.01.53"));
    CHECK_FALSE(is_series_managed_version("02.08.01_custom"));
    CHECK_FALSE(is_series_managed_version("02.08.01.52-dev"));
    CHECK_FALSE(is_series_managed_version(BAMBU_NETWORK_AGENT_VERSION_LEGACY));
    CHECK_FALSE(is_series_managed_version(""));
}

TEST_CASE_METHOD(PluginFolderFixture, "Managed builds fold into the series; customs are surfaced", "[NetworkVersions]")
{
    add_plugin("02.08.04.60");
    add_plugin("02.09.00.10");
    add_plugin("02.08.01.55");
    add_plugin("02.03.00.62");
    add_plugin("02.01.01.52");
    add_plugin("02.08.04_custom");
    add_plugin("02.08.04.52-dev");

    auto versions = get_all_available_versions();

    REQUIRE(count_version(versions, "02.08.04.60") == 0);
    REQUIRE(count_version(versions, "02.08.04")    == 1);
    REQUIRE(count_version(versions, "02.09.00.10") == 0);
    REQUIRE(count_version(versions, "02.08.01.55") == 0);
    REQUIRE(count_version(versions, "02.08.01")    == 0);
    REQUIRE(count_version(versions, "02.03.00.62") == 0);
    REQUIRE(count_version(versions, "02.03.00")    == 1);
    REQUIRE(count_version(versions, "02.01.01.52") == 0);
    REQUIRE(count_version(versions, "02.08.04_custom")  == 1);
    REQUIRE(count_version(versions, "02.08.04.52-dev")  == 1);

    REQUIRE(versions[0].version == "02.08.04");
    REQUIRE(versions[1].version == "02.08.04.52-dev");
    REQUIRE(versions[2].version == "02.08.04_custom");
    REQUIRE(versions[3].version == "02.03.00");
    REQUIRE(versions.back().version == BAMBU_NETWORK_AGENT_VERSION_LEGACY);

    REQUIRE(versions[3].suffix.empty());
    REQUIRE_FALSE(versions[3].is_latest);

    REQUIRE(versions[1].base_version == "02.08.04");
    REQUIRE_FALSE(versions[1].suffix.empty());
    REQUIRE(versions[2].base_version == "02.08.04");
    REQUIRE_FALSE(versions[2].suffix.empty());

    REQUIRE(versions[0].suffix.empty());
    REQUIRE(versions[0].is_latest);
    REQUIRE_FALSE(versions[1].is_latest);
    REQUIRE_FALSE(versions[2].is_latest);

    REQUIRE(std::string(get_latest_network_version()) == "02.08.04");
}

TEST_CASE_METHOD(PluginFolderFixture, "Only the loaded series is marked installed", "[NetworkVersions]")
{
    add_plugin("02.08.04.60");
    add_plugin("02.08.04_custom");

    {
        auto versions = get_all_available_versions("02.08.04.60");
        int marked = 0;
        for (const auto& info : versions)
            if (info.is_loaded) { ++marked; REQUIRE(info.version == "02.08.04"); }
        REQUIRE(marked == 1);
    }

    {
        add_plugin("02.03.00.62");
        auto versions = get_all_available_versions("02.03.00.62");
        int marked = 0;
        for (const auto& info : versions)
            if (info.is_loaded) { ++marked; REQUIRE(info.version == "02.03.00"); }
        REQUIRE(marked == 1);
    }

    {
        auto versions = get_all_available_versions("02.08.04_custom");
        int marked = 0;
        for (const auto& info : versions)
            if (info.is_loaded) { ++marked; REQUIRE(info.version == "02.08.04_custom"); }
        REQUIRE(marked == 1);
    }

    for (const auto& info : get_all_available_versions(""))
        REQUIRE_FALSE(info.is_loaded);
}

TEST_CASE("Only whitelisted series pass the load gate", "[NetworkVersions]")
{
    REQUIRE(is_supported_network_version("02.08.04"));
    REQUIRE(is_supported_network_version("02.08.04.52"));
    REQUIRE(is_supported_network_version("02.08.04.60"));
    REQUIRE(is_supported_network_version("02.08.04_custom"));
    REQUIRE(is_supported_network_version("02.08.04.52-dev"));
    REQUIRE(is_supported_network_version("02.03.00"));
    REQUIRE(is_supported_network_version("02.03.00.62"));
    REQUIRE(is_supported_network_version("02.03.00.70"));
    REQUIRE(is_supported_network_version("02.03.00_custom"));
    REQUIRE(is_supported_network_version(BAMBU_NETWORK_AGENT_VERSION_LEGACY));

    REQUIRE_FALSE(is_supported_network_version("02.08.01"));
    REQUIRE_FALSE(is_supported_network_version("02.08.01.55"));
    REQUIRE_FALSE(is_supported_network_version("02.08.01_custom"));
    REQUIRE_FALSE(is_supported_network_version("02.01.01.52"));
    REQUIRE_FALSE(is_supported_network_version("02.00.02.50"));

    REQUIRE_FALSE(is_supported_network_version("02.03.01.51"));

    REQUIRE_FALSE(is_supported_network_version("02.09.00.10"));
    std::string legacy = BAMBU_NETWORK_AGENT_VERSION_LEGACY;
    std::string legacy_sibling = legacy.substr(0, 9) + (legacy.substr(9) == "99" ? "98" : "99");
    REQUIRE_FALSE(is_supported_network_version(legacy_sibling));
    REQUIRE_FALSE(is_supported_network_version(""));
    REQUIRE_FALSE(is_supported_network_version("02.08"));
}

TEST_CASE("Each version resolves to the ABI generation that can call it", "[NetworkVersions]")
{
    CHECK(network_plugin_abi("02.08.04")        == NetworkAbi::Current);
    CHECK(network_plugin_abi("02.08.04.60")     == NetworkAbi::Current);
    CHECK(network_plugin_abi("02.08.04.52-dev") == NetworkAbi::Current);
    CHECK(network_plugin_abi("02.03.00")        == NetworkAbi::V0203);
    CHECK(network_plugin_abi("02.03.00.62")     == NetworkAbi::V0203);
    CHECK(network_plugin_abi("02.03.00_custom") == NetworkAbi::V0203);
    CHECK(network_plugin_abi(BAMBU_NETWORK_AGENT_VERSION_LEGACY) == NetworkAbi::Legacy);

    CHECK(network_plugin_abi("02.08.01.55") == NetworkAbi::Unsupported);
    CHECK(network_plugin_abi("02.01.01.52") == NetworkAbi::Unsupported);
    CHECK(network_plugin_abi("02.00.02.50") == NetworkAbi::Unsupported);
    CHECK(network_plugin_abi("02.09.00.10") == NetworkAbi::Unsupported);
    CHECK(network_plugin_abi("")            == NetworkAbi::Unsupported);

    for (size_t i = 0; i < AVAILABLE_NETWORK_VERSIONS_COUNT; ++i)
        CHECK(AVAILABLE_NETWORK_VERSIONS[i].abi != NetworkAbi::Unsupported);
}

TEST_CASE_METHOD(PluginFolderFixture, "Legacy series never adopts discovered builds", "[NetworkVersions]")
{
    std::string legacy = BAMBU_NETWORK_AGENT_VERSION_LEGACY;
    std::string legacy_sibling = legacy.substr(0, 9) + (legacy.substr(9) == "99" ? "98" : "99");
    add_plugin(legacy_sibling);

    auto versions = get_all_available_versions();

    REQUIRE(count_version(versions, legacy_sibling) == 0);
    REQUIRE(count_version(versions, legacy) == 1);

    for (const auto& info : versions) {
        if (info.version == "02.08.04") {
            REQUIRE(info.is_latest);
            REQUIRE_FALSE(info.is_loaded);
        }
    }
}
