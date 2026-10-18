#include "MultiplanarConfirm.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <cstddef>
#include <math.h>

#include "AutoMode.hpp"
#include "Multiplanar.hpp"
#include "PatchTools.hpp"

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

double steep_top_share(const indexed_triangle_set &mesh)
{
    double up = 0., steep = 0.;
    for (const stl_triangle_vertex_indices &t : mesh.indices) {
        const Vec3d  n = (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cast<double>().cross((mesh.vertices[t[2]] - mesh.vertices[t[0]]).cast<double>());
        const double a = n.norm();
        if (a <= 0. || n.z() <= std::cos(80. * M_PI / 180.) * a)
            continue;
        up += a;
        if (n.z() < std::cos(45. * M_PI / 180.) * a)
            steep += a;
    }
    return up > 0. ? steep / up : 0.;
}

double thin_top_share(const indexed_triangle_set &mesh, double thickness)
{
    if (mesh.indices.empty() || thickness <= 0.)
        return 0.;
    const AABBMesh tree(mesh);
    const size_t   stride = std::max<size_t>(1, mesh.indices.size() / 4000);
    double         up = 0., thin = 0.;
    for (size_t i = 0; i < mesh.indices.size(); i += stride) {
        const stl_triangle_vertex_indices &t = mesh.indices[i];
        const Vec3d a = mesh.vertices[t[0]].cast<double>(), b = mesh.vertices[t[1]].cast<double>(), c = mesh.vertices[t[2]].cast<double>();
        Vec3d       n = (b - a).cross(c - a);
        const double area = n.norm();
        if (area <= 0. || n.z() <= std::cos(80. * M_PI / 180.) * area)
            continue;
        n /= area;
        up += area;
        const Vec3d centroid = (a + b + c) / 3.;
        const auto  hit      = tree.query_ray_hit(centroid - 1e-3 * n, -n);
        if (hit.is_hit() && hit.distance() + 1e-3 <= thickness)
            thin += area;
    }
    return up > 0. ? thin / up : 0.;
}

namespace {

struct Bead
{
    Vec3d a, b, axis;
    double width, height;
};

double measured_slice(const PrintObject &object, bool chunks, std::vector<Bead> &beads)
{
    DynamicPrintConfig config = object.print()->full_print_config();
    config.apply(object.config(), true);
    config.apply(object.printing_region(0).config(), true);
    config.set_deserialize_strict("nonplanar_mode", chunks ? "multiplanar" : "off");
    if (chunks) {
        config.set_deserialize_strict("nonplanar_top_shells", "0");
        config.set_deserialize_strict("nonplanar_bottom_shells", "0");
    }
    Model        model;
    ModelObject *copy = model.add_object(*object.model_object());
    copy->clear_instances();
    copy->add_instance(*object.model_object()->instances.front());
    Print                print;
    GCodeProcessorResult result;
    const boost::filesystem::path gcode = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("np-mf-%%%%-%%%%.gcode");
    boost::system::error_code     ec;
    try {
        print.apply(model, config);
        print.set_status_silent();
        print.process();
        print.export_gcode(gcode.string(), &result, nullptr);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(warning) << "Multiplanar check: a measured slice failed: " << ex.what();
        boost::filesystem::remove(gcode, ec);
        return std::numeric_limits<double>::quiet_NaN();
    }
    boost::filesystem::remove(gcode, ec);
    const auto kinematics = make_tool_kinematics(print.config());
    const bool rotary     = kinematics->rotary_count() > 0;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        const ExtrusionRole r = m.extrusion_role;
        if (m.type != EMoveType::Extrude || r == erSkirt || r == erBrim || r == erWipeTower || r == erCustom || r == erNone ||
            r == erSupportMaterial || r == erSupportMaterialInterface || r == erSupportTransition)
            continue;
        beads.push_back({ result.moves[i - 1].position.cast<double>(), m.position.cast<double>(),
                          rotary && result.has_joints() ? kinematics->forward(result.joints[i]).axis : Vec3d::UnitZ(), double(m.width), double(m.height) });
    }
    return result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time;
}

double normal_rms(indexed_triangle_set mesh, const std::vector<Bead> &beads)
{
    using Key = int64_t;
    auto key = [](const Vec3d &p, double cell) {
        const auto c = [cell](double x) { return Key(std::floor(x / cell)) & 0x1FFFFF; };
        return (c(p.x()) << 42) | (c(p.y()) << 21) | c(p.z());
    };
    if (beads.empty() || mesh.indices.empty())
        return std::numeric_limits<double>::quiet_NaN();
    BoundingBoxf3 mbox, bbox;
    for (const stl_vertex &v : mesh.vertices)
        mbox.merge(v.cast<double>());
    for (const Bead &b : beads) {
        bbox.merge(Vec3d(b.a.x(), b.a.y(), 0.));
        bbox.merge(Vec3d(b.b.x(), b.b.y(), 0.));
    }
    const Vec3d shift(bbox.center().x() - mbox.center().x(), bbox.center().y() - mbox.center().y(), -mbox.min.z());
    for (stl_vertex &v : mesh.vertices)
        v += shift.cast<float>();

    std::vector<Vec3d> normals(mesh.indices.size());
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        const Vec3d a = mesh.vertices[mesh.indices[i][0]].cast<double>();
        const Vec3d n = (mesh.vertices[mesh.indices[i][1]].cast<double>() - a).cross(mesh.vertices[mesh.indices[i][2]].cast<double>() - a);
        normals[i]    = n;
    }
    const std::vector<Vec3i32>                   neighbours = its_face_neighbors(mesh);
    std::unordered_map<Key, std::vector<Vec3d>> sharp;
    for (size_t i = 0; i < mesh.indices.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const int j = neighbours[i][k];
            if (j < int(i) || normals[i].norm() <= 0. || normals[size_t(j)].norm() <= 0. ||
                normals[i].normalized().dot(normals[size_t(j)].normalized()) >= std::cos(30. * M_PI / 180.))
                continue;
            const Vec3d a = mesh.vertices[mesh.indices[i][k]].cast<double>(), b = mesh.vertices[mesh.indices[i][(k + 1) % 3]].cast<double>();
            const int   n = std::max(1, int(std::ceil((b - a).norm() / 0.25)));
            for (int s = 0; s <= n; ++s) {
                const Vec3d p = a + (double(s) / n) * (b - a);
                sharp[key(p, 1.)].push_back(p);
            }
        }
    auto near_sharp = [&](const Vec3d &p) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    if (auto it = sharp.find(key(p + Vec3d(dx, dy, dz), 1.)); it != sharp.end())
                        for (const Vec3d &q : it->second)
                            if ((q - p).squaredNorm() <= 1.)
                                return true;
        return false;
    };
    std::vector<std::pair<Vec3d, Vec3d>>   samples;
    std::mt19937                           rng(1);
    std::uniform_real_distribution<double> u01(0., 1.);
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        const double area2 = normals[i].norm();
        if (area2 <= 0. || normals[i].z() <= std::cos(75. * M_PI / 180.) * area2)
            continue;
        const Vec3d n = normals[i] / area2;
        const Vec3d a = mesh.vertices[mesh.indices[i][0]].cast<double>(), b = mesh.vertices[mesh.indices[i][1]].cast<double>(),
                    c = mesh.vertices[mesh.indices[i][2]].cast<double>();
        const int count = std::min(2000, int(std::ceil(0.5 * area2 / 0.09)));
        for (int k = 0; k < count; ++k) {
            double r1 = u01(rng), r2 = u01(rng);
            if (r1 + r2 > 1.)
                r1 = 1. - r1, r2 = 1. - r2;
            const Vec3d p = a + r1 * (b - a) + r2 * (c - a);
            if (! near_sharp(p))
                samples.emplace_back(p, n);
        }
    }
    if (samples.size() > 20000) {
        std::vector<std::pair<Vec3d, Vec3d>> kept;
        const double                         step = double(samples.size()) / 20000.;
        for (double k = 0.; k < double(samples.size()); k += step)
            kept.push_back(samples[size_t(k)]);
        samples.swap(kept);
    }
    if (samples.empty())
        return std::numeric_limits<double>::quiet_NaN();
    std::unordered_map<Key, std::vector<size_t>> near_cells;
    for (size_t i = 0; i < samples.size(); ++i)
        near_cells[key(samples[i].first, 1.2)].push_back(i);
    auto near_surface = [&](const Vec3d &p) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    if (auto it = near_cells.find(key(p + 1.2 * Vec3d(dx, dy, dz), 1.2)); it != near_cells.end())
                        for (size_t i : it->second)
                            if ((samples[i].first - p).squaredNorm() <= 1.44)
                                return true;
        return false;
    };
    constexpr double                              Cell = 0.5;
    std::unordered_map<Key, std::vector<Vec3d>> material;
    for (const Bead &bead : beads) {
        const Vec3d  d   = bead.b - bead.a;
        const double len = d.norm();
        if (len < EPSILON)
            continue;
        Vec3d side = bead.axis.cross(d / len);
        if (side.norm() < 1e-9)
            continue;
        side.normalize();
        const double h     = bead.height > 0. ? bead.height : 0.2;
        const int    steps = std::max(1, int(std::ceil(len / 0.1)));
        for (int s = 0; s < steps; ++s) {
            const Vec3d p = bead.a + ((s + 0.5) / steps) * d;
            if (! near_surface(p))
                continue;
            for (double u : { -1., -0.5, 0., 0.5, 1. })
                for (double v : { 0., 0.5, 1. }) {
                    const Vec3d q = p + 0.5 * u * bead.width * side - v * h * bead.axis;
                    material[key(q, Cell)].push_back(q);
                }
        }
    }
    double sum   = 0.;
    size_t count = 0;
    for (const auto &[s, n] : samples) {
        double best = -std::numeric_limits<double>::infinity();
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 2; ++dy)
                for (int dz = -2; dz <= 2; ++dz) {
                    auto it = material.find(key(s + Cell * Vec3d(dx, dy, dz), Cell));
                    if (it == material.end())
                        continue;
                    for (const Vec3d &q : it->second) {
                        const Vec3d  rel   = q - s;
                        const double along = rel.dot(n);
                        if ((rel - along * n).squaredNorm() <= 0.15 * 0.15 && rel.squaredNorm() <= 0.81)
                            best = std::max(best, along);
                    }
                }
        if (std::isfinite(best) && std::abs(best) < 0.5) {
            sum += best * best;
            ++count;
        }
    }
    return count > 0 ? std::sqrt(sum / double(count)) : std::numeric_limits<double>::quiet_NaN();
}

struct ChunkCheck
{
    double chunks_rms = std::numeric_limits<double>::quiet_NaN(), planar_rms = std::numeric_limits<double>::quiet_NaN();
    double chunks_time = std::numeric_limits<double>::quiet_NaN(), planar_time = std::numeric_limits<double>::quiet_NaN();
};
ChunkCheck check_chunks(const PrintObject &object)
{
    ChunkCheck        check;
    std::vector<Bead> planar, chunks;
    check.planar_time = measured_slice(object, false, planar);
    throw_if_canceled(object);
    check.chunks_time = measured_slice(object, true, chunks);
    TriangleMesh mesh = object.model_object()->raw_mesh();
    mesh.transform(object.model_object()->instances.front()->get_matrix());
    check.planar_rms = normal_rms(mesh.its, planar);
    check.chunks_rms = normal_rms(mesh.its, chunks);
    return check;
}

}

bool auto_rotary_candidate(const PrintObject &object, const indexed_triangle_set &mesh)
{
    return auto_multiplanar_candidate(steep_top_share(mesh),
                                      thin_top_share(mesh, full_conformal_thickness(object.printing_region(0).config(), object)));
}

std::string auto_rotary_chunks(const PrintObject &object, const indexed_triangle_set &mesh, const MultiplanarPlan &plan)
{
    if (! auto_multiplanar_chunks(plan))
        return {};
    const std::string &name  = object.model_object()->name;
    const ChunkCheck   check = check_chunks(object);
    std::ostringstream measured;
    measured << "RMS along the normal " << check.chunks_rms << " mm against " << check.planar_rms << " mm planar, estimated time "
             << check.chunks_time / 60. << " against " << check.planar_time / 60. << " min";
    if (! auto_multiplanar_confirmed(check.chunks_rms, check.planar_rms, check.chunks_time, check.planar_time)) {
        BOOST_LOG_TRIVIAL(warning) << "Auto mode keeps curved shells for " << name << ": its chunks measure " << measured.str()
                                   << " (MULTIPLANAR.md \"Auto\").";
        return {};
    }
    const double thickness = full_conformal_thickness(object.printing_region(0).config(), object);
    const double steep = steep_top_share(mesh), thin = thin_top_share(mesh, thickness);
    std::ostringstream why;
    why << "Auto mode prints " << name << " in " << plan.chunks.size() << " tilted chunks: ";
    if (steep >= thin)
        why << int(std::lround(100. * steep)) << " % of its top is steeper than 45 deg";
    else
        why << int(std::lround(100. * thin)) << " % of its top is a wall thinner than " << thickness << " mm";
    why << ", and the chunks lay " << int(std::lround(100. * (1. - plan.step_area / std::max(plan.planar_step_area, EPSILON))))
        << " % of its stair-steps flat; measured: " << measured.str() << " (MULTIPLANAR.md \"Auto\").";
    BOOST_LOG_TRIVIAL(warning) << why.str();
    return why.str();
}

}
