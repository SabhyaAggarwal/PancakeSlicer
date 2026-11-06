#pragma once

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Slic3r::Nonplanar {



class TopSurfaceGrid
{
public:
    using Json = nlohmann::json;
    static constexpr double pitch = 0.25;
    Vec2d origin;
    int nx, ny;
    std::vector<double> mesh, normal, bead;
    std::vector<unsigned char> conformal, ironed, eligible;
    std::vector<uint16_t> owner;
    explicit TopSurfaceGrid(const indexed_triangle_set &its, const std::vector<uint16_t> &face_owner = {})
    {
        BoundingBoxf box;
        for (const auto &v : its.vertices) box.merge(Vec2d(v.head<2>().cast<double>()));
        origin = box.min;
        nx = std::max(1, int(std::ceil(box.size().x() / pitch)));
        ny = std::max(1, int(std::ceil(box.size().y() / pitch)));
        const size_t cells = size_t(nx) * ny;
        mesh.assign(cells, -std::numeric_limits<double>::infinity());
        normal.assign(cells, 0.);
        bead = mesh;
        conformal.assign(cells, 0); ironed.assign(cells, 0); eligible.assign(cells, 0);
        if (!face_owner.empty()) owner.assign(cells, 65535);
        for (size_t face = 0; face < its.indices.size(); ++face) {
            const auto &f = its.indices[face];
            const Vec3d a = its.vertices[f[0]].cast<double>(), b = its.vertices[f[1]].cast<double>(), c = its.vertices[f[2]].cast<double>();
            const Vec3d n = (b - a).cross(c - a);
            const double d = n.z();
            if (d <= 1e-12) continue;
            visit(a.head<2>().cwiseMin(b.head<2>()).cwiseMin(c.head<2>()),
                  a.head<2>().cwiseMax(b.head<2>()).cwiseMax(c.head<2>()), [&](size_t k, const Vec2d &p) {
                const double u = ((b.x() - p.x()) * (c.y() - p.y()) - (b.y() - p.y()) * (c.x() - p.x())) / d;
                const double v = ((c.x() - p.x()) * (a.y() - p.y()) - (c.y() - p.y()) * (a.x() - p.x())) / d;
                if (u < -1e-9 || v < -1e-9 || u + v > 1. + 1e-9) return;
                const double z = u * a.z() + v * b.z() + (1. - u - v) * c.z();
                if (z > mesh[k]) {
                    mesh[k] = z; normal[k] = d / n.norm();
                    if (!owner.empty()) owner[k] = face_owner[face];
                }
            });
        }

        const int inset = 8;
        for (int y = inset; y < ny - inset; ++y) for (int x = inset; x < nx - inset; ++x) {
            bool ok = true;
            for (int dy = -inset; ok && dy <= inset; ++dy) for (int dx = -inset; dx <= inset; ++dx) {
                if (dx * dx + dy * dy > inset * inset) continue;
                const size_t k = size_t(y + dy) * nx + x + dx;
                if (!std::isfinite(mesh[k]) || normal[k] < std::cos(70. * 3.14159265358979323846 / 180.)) { ok = false; break; }
            }
            eligible[size_t(y) * nx + x] = ok;
        }
    }
    template<class F> void visit(const Vec2d &lo, const Vec2d &hi, F fn)
    {
        const int x0 = std::max(0, int(std::floor((lo.x() - origin.x()) / pitch))),
                  x1 = std::min(nx - 1, int(std::floor((hi.x() - origin.x()) / pitch))),
                  y0 = std::max(0, int(std::floor((lo.y() - origin.y()) / pitch))),
                  y1 = std::min(ny - 1, int(std::floor((hi.y() - origin.y()) / pitch)));
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            fn(size_t(y) * nx + x, origin + pitch * Vec2d(x + 0.5, y + 0.5));
    }
    void deposit(const GCodeProcessorResult &result)
    {
        for (size_t i = 1; i < result.moves.size(); ++i) {
            const auto &m = result.moves[i];
            if (m.type != EMoveType::Extrude || m.extrusion_role == erCustom || m.extrusion_role == erSkirt ||
                m.extrusion_role == erBrim || m.extrusion_role == erNone || m.extrusion_role == erWipeTower || is_support(m.extrusion_role)) continue;
            const Vec3d a = result.moves[i - 1].position.cast<double>(), b = m.position.cast<double>();
            const Vec2d aa = a.head<2>(), bb = b.head<2>(), d = bb - aa;
            const double radius = m.width / 2.;
            visit(aa.cwiseMin(bb) - Vec2d::Constant(radius), aa.cwiseMax(bb) + Vec2d::Constant(radius),
                  [&](size_t k, const Vec2d &p) {
                const double t = d.squaredNorm() > 1e-12 ? std::clamp((p - aa).dot(d) / d.squaredNorm(), 0., 1.) : 0.;
                if ((p - aa - t * d).squaredNorm() > radius * radius) return;
                const double z = a.z() + t * (b.z() - a.z());
                if (m.extrusion_role == erIroning) { if (std::abs(z - mesh[k]) <= 1.) ironed[k] = 1; return; }
                if (z > bead[k]) { bead[k] = z; conformal[k] = result.state(m).nonplanar_patch_shell != 0xFF; }
            });
        }
    }
    template<class F> Json measure(F accepts) const
    {
        double squared = 0., squared_conformal = 0.;
        size_t cells = 0, finite = 0, gaps = 0, covered = 0, iron = 0;
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
            const size_t k = size_t(y) * nx + x;
            if (!eligible[k] || !accepts(origin + pitch * Vec2d(x + 0.5, y + 0.5))) continue;
            ++cells;
            const double error = bead[k] - mesh[k];
            if (std::isfinite(error)) { squared += error * error; ++finite; }
            if (error < -1.) ++gaps;
            if (conformal[k] && std::abs(error) <= 1.) { ++covered; squared_conformal += error * error; }
            iron += ironed[k];
        }
        return {{"rms_vs_mesh_mm", finite ? Json(std::sqrt(squared / finite)) : Json(nullptr)},
                {"coverage", cells ? Json(double(covered) / cells) : Json(nullptr)},

                {"rms_covered_mm", covered ? Json(std::sqrt(squared_conformal / covered)) : Json(nullptr)},
                {"rms_uncovered_mm", finite > covered ? Json(std::sqrt(std::max(0., squared - squared_conformal) / (finite - covered))) : Json(nullptr)},
                {"gap_cells", gaps}, {"ironed_area_mm2", iron * pitch * pitch}, {"eval_cells", cells},
                {"finite_cells", finite}};
    }
};

}
