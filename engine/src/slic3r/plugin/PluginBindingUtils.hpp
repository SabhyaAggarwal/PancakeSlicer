#pragma once
#include "libslic3r/libslic3r.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include "libslic3r/Config.hpp"
#include "libslic3r/Point.hpp"
#include <pybind11/pytypes.h>
#include <pybind11/cast.h>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

static_assert(sizeof(Point)  == 2 * sizeof(coord_t), "Point must be 2 packed coord_t");
static_assert(sizeof(Point3) == 3 * sizeof(coord_t), "Point3 must be 3 packed coord_t");

template<typename Builder>
pybind11::object with_numpy(Builder&& build)
{
    namespace py = pybind11;
    try {
        return std::forward<Builder>(build)();
    } catch (py::error_already_set& err) {
        if (err.matches(PyExc_ImportError))
            throw py::import_error("numpy is required to access geometry/mesh arrays; "
                                   "add dependencies = [\"numpy\"] to your plugin metadata");
        throw;
    }
}

template<typename T, int N>
pybind11::array make_readonly_rows(pybind11::handle base, const T* data, pybind11::ssize_t rows)
{
    namespace py = pybind11;
    if (rows == 0 || data == nullptr) {
        py::array_t<T> empty(std::vector<py::ssize_t>{ 0, (py::ssize_t) N });
        empty.attr("setflags")(py::arg("write") = false);
        return std::move(empty);
    }
    py::array_t<T> arr(
        { rows, (py::ssize_t) N },
        { (py::ssize_t)(N * sizeof(T)), (py::ssize_t) sizeof(T) },
        data, base);
    arr.attr("setflags")(py::arg("write") = false);
    return std::move(arr);
}

template<typename T, int N>
pybind11::array make_writable_rows(pybind11::handle base, T* data, pybind11::ssize_t rows)
{
    namespace py = pybind11;
    if (rows == 0 || data == nullptr)
        return py::array_t<T>(std::vector<py::ssize_t>{ 0, (py::ssize_t) N });
    return py::array_t<T>(
        { rows, (py::ssize_t) N },
        { (py::ssize_t)(N * sizeof(T)), (py::ssize_t) sizeof(T) },
        data, base);
}

inline pybind11::object config_value_or_none(const ConfigBase& config, const std::string& key)
{
    if (!config.has(key))
        return pybind11::none();
    return pybind11::cast(config.opt_serialize(key));
}

inline pybind11::tuple vec3_to_tuple(const Vec3d& v)
{
    return pybind11::make_tuple(v.x(), v.y(), v.z());
}

inline pybind11::object mat4_to_numpy(const Transform3d& transform)
{
    namespace py = pybind11;
    return with_numpy([&] {
        py::array_t<double> array({ py::ssize_t(4), py::ssize_t(4) });
        auto                view   = array.mutable_unchecked<2>();
        const auto&         matrix = transform.matrix();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                view(i, j) = matrix(i, j);
        return py::object(std::move(array));
    });
}

}
