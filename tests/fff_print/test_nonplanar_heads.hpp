#pragma once
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace Slic3r::Test {

inline std::string cone30_head_json()
{
    std::ostringstream out;
    out << R"({ "schema": "orca.head_envelope.v1", "id": "test-cone30", "name": "Test 30-degree cone", "provenance": "test",
                "nozzle": { "tip_flat_radius": 0.5, "allowance_radius": 0.75, "allowance_height": 0.1 },
                "margin": 0.05, "lateral_margin": 0.0, "side_profile_top": 60.0, "side_profile": [)";
    for (int i = 1; i <= 40; ++i) {
        const double r = 0.5 * (i + 1) / std::tan(PI / 6.);
        out << (i > 1 ? "," : "") << "{\"height\":" << 0.5 * i << ",\"xpos\":" << r << ",\"xneg\":" << r << ",\"ypos\":" << r
            << ",\"yneg\":" << r << "}";
    }
    out << R"(], "boxes": [ { "name": "nozzle", "min": [-0.5, -0.5, 0.0], "max": [0.5, 0.5, 60.0], "nozzle": true } ] })";
    return out.str();
}

struct BundledHeads
{
    std::string             previous_resources = resources_dir();
    std::string             previous_data      = data_dir();
    boost::filesystem::path user_dir           = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("np_heads_%%%%%%%%");
    BundledHeads()
    {
        set_resources_dir((boost::filesystem::path(std::string(PROFILES_DIR)) / "..").string());
        boost::filesystem::create_directories(user_dir / "nonplanar" / "heads");
        std::ofstream((user_dir / "nonplanar" / "heads" / "test-cone30.json").string()) << cone30_head_json();
        set_data_dir(user_dir.string());
        Nonplanar::HeadLibrary::instance().reload();
    }
    ~BundledHeads()
    {
        set_resources_dir(previous_resources);
        set_data_dir(previous_data);
        Nonplanar::HeadLibrary::instance().reload();
        boost::system::error_code ec;
        boost::filesystem::remove_all(user_dir, ec);
    }
};

}
