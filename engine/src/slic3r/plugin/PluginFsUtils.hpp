#pragma once

#include "PluginDescriptor.hpp"
#include <pybind11/pytypes.h>

#include <nlohmann/json.hpp>
#include <pybind11/pybind11.h>

#include <boost/filesystem/path.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <pybind11/cast.h>

#define PLUGIN_SUBSCRIBED_DIR "_subscribed"
#define PLUGIN_DATA_DIR "plugin_data"

namespace Slic3r {

extern const char* const INSTALL_STATE_FILE;

inline constexpr int kMaxJsonConversionDepth = 200;

inline pybind11::object json_to_py(const nlohmann::json& j, int depth = 0)
{
    namespace py = pybind11;
    using json   = nlohmann::json;

    if (depth > kMaxJsonConversionDepth)
        throw py::value_error("Plugin JSON value nested too deeply");

    switch (j.type()) {
    case json::value_t::null:            return py::none();
    case json::value_t::boolean:         return py::bool_(j.get<bool>());
    case json::value_t::number_integer:  return py::int_(j.get<std::int64_t>());
    case json::value_t::number_unsigned: return py::int_(j.get<std::uint64_t>());
    case json::value_t::number_float:    return py::float_(j.get<double>());
    case json::value_t::string:          return py::str(j.get<std::string>());
    case json::value_t::array: {
        py::list lst;
        for (const auto& e : j)
            lst.append(json_to_py(e, depth + 1));
        return lst;
    }
    case json::value_t::object: {
        py::dict d;
        for (auto it = j.begin(); it != j.end(); ++it)
            d[py::str(it.key())] = json_to_py(it.value(), depth + 1);
        return d;
    }
    default: return py::none();
    }
}

inline nlohmann::json py_to_json(const pybind11::handle& o, int depth = 0)
{
    namespace py = pybind11;
    using json   = nlohmann::json;

    if (depth > kMaxJsonConversionDepth)
        throw py::value_error("Plugin value nested too deeply (possible cycle)");

    if (o.is_none())
        return json(nullptr);
    if (py::isinstance<py::bool_>(o))
        return o.cast<bool>();
    if (py::isinstance<py::int_>(o))
        return o.cast<std::int64_t>();
    if (py::isinstance<py::float_>(o))
        return o.cast<double>();
    if (py::isinstance<py::str>(o))
        return o.cast<std::string>();
    if (py::isinstance<py::bytes>(o))
        return o.cast<std::string>();
    if (py::isinstance<py::dict>(o)) {
        json obj = json::object();
        for (auto item : py::reinterpret_borrow<py::dict>(o))
            obj[py::str(item.first).cast<std::string>()] = py_to_json(item.second, depth + 1);
        return obj;
    }
    if (py::isinstance<py::list>(o) || py::isinstance<py::tuple>(o)) {
        json arr = json::array();
        for (auto e : o)
            arr.push_back(py_to_json(e, depth + 1));
        return arr;
    }
    return py::str(o).cast<std::string>();
}

struct PluginPermissions
{
    std::vector<std::string> fs_read;
    std::vector<std::string> fs_readwrite;
    std::vector<std::string> network_http;
    std::vector<std::string> network_socket;
    std::vector<std::string> process;
    std::vector<std::string> threading;
};

struct PluginInstallState {
    std::string installed_from;
    std::string installed_version;
    std::string plugin_name;
    std::string cloud_uuid;

    PluginPermissions permissions;

    bool enabled = true;
    std::vector<std::pair<std::string, bool>> capabilities;
};

std::string get_cloud_plugin_dir(const std::string& user_id);

std::string get_orca_plugins_dir();

boost::filesystem::path resolve_plugin_root_from_descriptor(const PluginDescriptor& descriptor);

bool is_plugin_root_allowed(const boost::filesystem::path& candidate_root,
                            const std::vector<std::string>& allowed_dirs);

bool resolve_allowed_plugin_root(const PluginDescriptor& descriptor,
                                 const std::vector<std::string>& allowed_dirs,
                                 const std::string& out_of_scope_error,
                                 boost::filesystem::path& resolved_root,
                                 std::string& error);

bool delete_plugin_root(const boost::filesystem::path& resolved_root,
                        const std::string& plugin_id,
                        std::string& error);

std::vector<std::string> get_plugin_directories(const std::string& cloud_user_id);

std::vector<PluginDescriptor> discover_plugin_packages(const std::vector<std::string>& dirs, std::string& error);

bool is_ignored_plugin_directory(const boost::filesystem::path& path);
bool is_safe_relative_path(const boost::filesystem::path& path);
bool is_valid_plugin_id(const std::string& id);
bool extract_zip_to_directory(const boost::filesystem::path& zip_path, const boost::filesystem::path& destination, std::string& error);

bool read_python_plugin_metadata(const boost::filesystem::path& py_path, PluginDescriptor& descriptor, std::string& error);

bool read_wheel_plugin_metadata(const boost::filesystem::path& whl_path, PluginDescriptor& descriptor, std::string& error);

boost::filesystem::path find_installed_plugin_entry(const boost::filesystem::path& plugin_dir, std::string& error);

bool write_install_state(const boost::filesystem::path& plugin_dir, const PluginInstallState& state);
bool write_install_state(const boost::filesystem::path& plugin_dir, const PluginDescriptor& entry, bool enabled,
                         const std::vector<std::pair<std::string, bool>>& capabilities);
bool write_install_state(const boost::filesystem::path& plugin_dir, const PluginDescriptor& entry);

bool read_install_state(const boost::filesystem::path& plugin_dir, PluginDescriptor& entry);
bool read_install_state(const boost::filesystem::path& plugin_dir, PluginInstallState& out);

}
