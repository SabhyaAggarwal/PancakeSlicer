#pragma once

#include <boost/filesystem/path.hpp>
#include <libslic3r/Utils.hpp>

#include <boost/filesystem.hpp>

#include <string>

#include "test_utils.hpp"

namespace Slic3r {

struct ScopedDataDir
{
    ScopedTemporaryDir        tmp;
    boost::filesystem::path   dir;
    std::string               previous;

    explicit ScopedDataDir(const std::string& tag)
        : tmp("orca-" + tag), dir(tmp.path()), previous(data_dir())
    {
        set_data_dir(dir.string());
    }

    ~ScopedDataDir() { set_data_dir(previous); }
    boost::filesystem::path plugins_dir() const { return dir / "orca_plugins"; }

    ScopedDataDir(const ScopedDataDir&)            = delete;
    ScopedDataDir& operator=(const ScopedDataDir&) = delete;
};

struct ScopedResourcesDir
{
    ScopedTemporaryDir      tmp;
    boost::filesystem::path dir;
    std::string             previous;

    explicit ScopedResourcesDir(const std::string& tag)
        : tmp("orca-" + tag), dir(tmp.path()), previous(resources_dir())
    {
        set_resources_dir(dir.string());
    }

    ~ScopedResourcesDir() { set_resources_dir(previous); }

    ScopedResourcesDir(const ScopedResourcesDir&)            = delete;
    ScopedResourcesDir& operator=(const ScopedResourcesDir&) = delete;
};

}
