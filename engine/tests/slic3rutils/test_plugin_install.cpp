#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <ios>
#include <slic3r/plugin/PluginLoader.hpp>
#include <slic3r/plugin/PluginDescriptor.hpp>
#include <slic3r/plugin/PluginFsUtils.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "plugin_test_utils.hpp"

#include <boost/filesystem.hpp>

#include <fstream>
#include <string>

using namespace Slic3r;
namespace fs = boost::filesystem;

namespace {

fs::path write_py_file(const fs::path& dir, const std::string& filename, const std::string& contents)
{
    fs::create_directories(dir);
    const fs::path p = dir / filename;
    std::ofstream out(p.string(), std::ios::binary);
    out << contents;
    return p;
}

}

TEST_CASE("install_plugin rejects a cloud UUID containing path traversal", "[PluginInstall]")
{
    ScopedDataDir data_dir_guard("cor2");

    const fs::path py = write_py_file(data_dir_guard.dir / "src", "evil.py", "print('hi')\n");



    PluginDescriptor descriptor;
    descriptor.cloud = CloudPluginState{"../../escape", true, false, false, false};

    std::string error;
    const bool installed = plugin_loader::install_plugin(py, "test-user", descriptor, error);

    REQUIRE_FALSE(installed);
    CHECK_THAT(error, Catch::Matchers::ContainsSubstring("valid identifier"));
}

TEST_CASE("install_plugin rejects a side-loaded .py with no PEP 723 metadata", "[PluginInstall]")
{
    ScopedDataDir data_dir_guard("cor3-bad");

    const fs::path py = write_py_file(data_dir_guard.dir / "src", "nameless.py", "print('no metadata here')\n");


    PluginDescriptor descriptor;
    std::string error;
    const bool installed = plugin_loader::install_plugin(py, "", descriptor, error);

    REQUIRE_FALSE(installed);
    CHECK_THAT(error, Catch::Matchers::ContainsSubstring("PEP 723"));
}

TEST_CASE("install_plugin accepts a side-loaded .py with complete PEP 723 metadata", "[PluginInstall]")
{
    ScopedDataDir data_dir_guard("cor3-good");

    const std::string contents =
        "# /// script\n"
        "# requires-python = \">=3.12\"\n"
        "#\n"
        "# [tool.orcaslicer.plugin]\n"
        "# name = \"Test Plugin\"\n"
        "# type = \"script\"\n"
        "# ///\n"
        "print('ok')\n";
    const fs::path py = write_py_file(data_dir_guard.dir / "src", "good.py", contents);


    PluginDescriptor descriptor;
    std::string error;
    const bool installed = plugin_loader::install_plugin(py, "", descriptor, error);

    REQUIRE(installed);
    CHECK(error.empty());
}

TEST_CASE("install-state sidecar is the source of truth for a cloud plugin's installed version", "[PluginInstall]")
{
    ScopedDataDir data_dir_guard("installed-version");

    const fs::path plugin_dir = data_dir_guard.dir / "plugin";
    fs::create_directories(plugin_dir);

    PluginDescriptor descriptor;
    descriptor.name             = "Versioned Plugin";
    descriptor.version          = "1.0.0";
    descriptor.installed_version = "1.2.0";
    descriptor.cloud            = CloudPluginState{"uuid-1", true, false, false, false};

    REQUIRE(write_install_state(plugin_dir, descriptor));

    PluginInstallState state;
    REQUIRE(read_install_state(plugin_dir, state));
    CHECK(state.installed_version == "1.2.0");

    state.permissions.fs_read       = {"/path/to/read"};
    state.permissions.fs_readwrite  = {"/path/to/readwrite"};
    state.permissions.network_http  = {"https://api.example.com"};
    state.permissions.network_socket = {"192.168.45.6:443"};
    state.permissions.process        = {"/usr/bin/curl"};
    state.permissions.threading      = {"thread"};
    REQUIRE(write_install_state(plugin_dir, state));

    PluginInstallState persisted;
    REQUIRE(read_install_state(plugin_dir, persisted));
    CHECK(persisted.permissions.fs_read == state.permissions.fs_read);
    CHECK(persisted.permissions.fs_readwrite == state.permissions.fs_readwrite);
    CHECK(persisted.permissions.network_http == state.permissions.network_http);
    CHECK(persisted.permissions.network_socket == state.permissions.network_socket);
    CHECK(persisted.permissions.process == state.permissions.process);
    CHECK(persisted.permissions.threading == state.permissions.threading);

    PluginDescriptor scanned;
    scanned.version = "1.0.0";
    read_install_state(plugin_dir, scanned);
    CHECK(scanned.installed_version == "1.2.0");
}

TEST_CASE("a local plugin's installed version follows its entry file header", "[PluginInstall]")
{
    ScopedDataDir data_dir_guard("local-installed-version");

    const fs::path plugin_dir = data_dir_guard.dir / "plugin";
    fs::create_directories(plugin_dir);

    PluginInstallState state;
    state.installed_from    = "local";
    state.installed_version = "1.0.0";
    REQUIRE(write_install_state(plugin_dir, state));

    PluginDescriptor scanned;
    scanned.version = "1.1.0";
    REQUIRE(read_install_state(plugin_dir, scanned));
    CHECK(scanned.installed_version == "1.1.0");
}

TEST_CASE("an unsubscribed cloud plugin follows its header version", "[PluginInstall]")
{
    ScopedDataDir data_dir_guard("unsubscribed-installed-version");

    const fs::path plugin_dir = data_dir_guard.dir / "plugin";
    fs::create_directories(plugin_dir);

    PluginInstallState state;
    state.installed_from    = "local";
    state.installed_version = "1.2.0";
    REQUIRE(write_install_state(plugin_dir, state));

    PluginDescriptor scanned;
    scanned.version = "1.0.0";
    REQUIRE(read_install_state(plugin_dir, scanned));
    CHECK(scanned.installed_version == "1.0.0");
}
