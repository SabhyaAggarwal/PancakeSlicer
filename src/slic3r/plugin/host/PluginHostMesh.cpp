#include "PluginHostBindings.hpp"
#include "PluginHostMesh.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/cast.h>
#include "slic3r/plugin/PluginBindingUtils.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Point.hpp"
#include <cstddef>
#include <pybind11/numpy.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <pybind11/detail/common.h>
#include <pybind11/detail/descr.h>

namespace py = pybind11;

namespace Slic3r {
namespace {

static_assert(sizeof(stl_vertex) == 3 * sizeof(float),
              "stl_vertex must be a packed float[3] for zero-copy numpy export");
static_assert(sizeof(stl_triangle_vertex_indices) == 3 * sizeof(std::int32_t),
              "triangle index must be a packed int32[3] for zero-copy numpy export");

}

void host_bindings::register_mesh(py::module_& host)
{
    py::class_<TriangleMesh, std::shared_ptr<TriangleMesh>>(host, "TriangleMesh",
        "Immutable snapshot of a ModelVolume's mesh in local (untransformed) coordinates, mm.")
        .def("vertex_count", [](const TriangleMesh& mesh) { return mesh.its.vertices.size(); })
        .def("triangle_count", [](const TriangleMesh& mesh) { return mesh.its.indices.size(); })
        .def("facets_count", [](const TriangleMesh& mesh) { return mesh.its.indices.size(); })
        .def("is_empty", [](const TriangleMesh& mesh) { return mesh.its.indices.empty(); })
        .def("vertices", [](py::object self) {
            const TriangleMesh& mesh = self.cast<const TriangleMesh&>();
            return with_numpy([&] {
                const std::vector<stl_vertex>& vertices = mesh.its.vertices;
                return py::object(make_readonly_rows<float, 3>(
                    self, vertices.empty() ? nullptr : vertices.front().data(),
                    static_cast<py::ssize_t>(vertices.size())));
            });
        }, "Read-only zero-copy (N, 3) float32 ndarray of vertex positions (local mm). Requires numpy.")
        .def("triangles", [](py::object self) {
            const TriangleMesh& mesh = self.cast<const TriangleMesh&>();
            return with_numpy([&] {
                const std::vector<stl_triangle_vertex_indices>& indices = mesh.its.indices;
                return py::object(make_readonly_rows<std::int32_t, 3>(
                    self, indices.empty() ? nullptr : indices.front().data(),
                    static_cast<py::ssize_t>(indices.size())));
            });
        }, "Read-only zero-copy (M, 3) int32 ndarray of triangle vertex indices. Requires numpy.")
        .def("face_normals", [](const TriangleMesh& mesh) {
            return with_numpy([&] {
                std::vector<Vec3f> normals = its_face_normals(mesh.its);
                py::array_t<float> array({ static_cast<py::ssize_t>(normals.size()), py::ssize_t(3) });
                if (!normals.empty()) {
                    auto view = array.mutable_unchecked<2>();
                    for (size_t i = 0; i < normals.size(); ++i) {
                        view(i, 0) = normals[i].x();
                        view(i, 1) = normals[i].y();
                        view(i, 2) = normals[i].z();
                    }
                }
                return py::object(std::move(array));
            });
        }, "Per-triangle normalized normals as an (M, 3) float32 ndarray (copy). Requires numpy.")
        .def("vertex", [](const TriangleMesh& mesh, size_t index) {
            const std::vector<stl_vertex>& vertices = mesh.its.vertices;
            if (index >= vertices.size())
                throw py::index_error("vertex index out of range");
            const stl_vertex& vertex = vertices[index];
            return py::make_tuple(vertex.x(), vertex.y(), vertex.z());
        })
        .def("triangle", [](const TriangleMesh& mesh, size_t index) {
            const std::vector<stl_triangle_vertex_indices>& indices = mesh.its.indices;
            if (index >= indices.size())
                throw py::index_error("triangle index out of range");
            const stl_triangle_vertex_indices& triangle = indices[index];
            return py::make_tuple(triangle[0], triangle[1], triangle[2]);
        })
        .def("volume", [](const TriangleMesh& mesh) { return mesh.stats().volume; })
        .def("bounding_box", [](const TriangleMesh& mesh) { return bbox_from_stats(mesh.stats()); })
        .def("is_manifold", [](const TriangleMesh& mesh) { return mesh.stats().manifold(); });
}

}
