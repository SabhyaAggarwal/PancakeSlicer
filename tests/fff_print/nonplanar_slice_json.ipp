#pragma once
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/AutoMode.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
double process_cpu_seconds()
{
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("cannot read process CPU time");
    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return double(k.QuadPart + u.QuadPart) * 1e-7;
#else
    return double(std::clock()) / CLOCKS_PER_SEC;
#endif
}

using Json = nlohmann::json;

std::string file_sha1(const std::string &path)
{
    boost::nowide::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot hash " + path);
    boost::uuids::detail::sha1 hash;
    std::array<char, 65536> buffer;
    while (in) {
        in.read(buffer.data(), buffer.size());
        hash.process_bytes(buffer.data(), size_t(in.gcount()));
    }
    if (!in.eof()) throw std::runtime_error("cannot read " + path);
    boost::uuids::detail::sha1::digest_type digest;
    hash.get_digest(digest);
    std::ostringstream out;
    for (const auto word : digest)
        out << std::hex << std::setfill('0') << std::setw(int(2 * sizeof(word))) << static_cast<unsigned long>(word);
    return out.str();
}

std::string first_line(const fs::path &path)
{
    boost::nowide::ifstream in(path.string());
    std::string line;
    std::getline(in, line);
    return line;
}

Json git_sha(const std::string &ref)
{
    const fs::path root = fs::path(PROFILES_DIR).parent_path().parent_path();
    fs::path git = root / ".git";
    if (fs::is_regular_file(git)) {
        const std::string line = first_line(git);
        if (line.rfind("gitdir: ", 0) != 0) return nullptr;
        const fs::path directory(line.substr(8));
        git = directory.is_absolute() ? directory : root / directory;
    }
    fs::path common = git;
    if (fs::exists(git / "commondir")) {
        const fs::path directory(first_line(git / "commondir"));
        common = directory.is_absolute() ? directory : git / directory;
    }
    const auto packed_ref = [&](const std::string &name) {
        boost::nowide::ifstream packed((common / "packed-refs").string());
        std::string line;
        while (std::getline(packed, line))
            if (line.size() > 41 && line.substr(41) == name) return line.substr(0, 40);
        return std::string();
    };
    std::string value = first_line((ref == "HEAD" ? git : common) / ref);
    if (value.rfind("ref: ", 0) == 0) {
        const std::string target = value.substr(5);
        value = first_line(common / target);
        if (value.empty()) value = packed_ref(target);
    } else if (value.empty() && ref != "HEAD") value = packed_ref(ref);
    return value.size() == 40 ? Json(value) : Json(nullptr);
}

template<class T> std::string text_value(const T &value)
{
    std::ostringstream out;
    out.copyfmt(std::cout);
    out << value;
    return out.str();
}

double text_number(double value)
{
    return std::stod(text_value(value));
}

void slice_json_record(Json &record, const Args &args)
{
    record = {{"schema", "orca.nonplanar_slice.v1"}, {"status", "started"},
              {"inputs", {{"printer", args.printer}, {"process", args.process}, {"filaments", args.filaments},
                          {"vendor", args.vendor}, {"overrides", args.overrides}, {"mode", nullptr}, {"head", nullptr},
                          {"source_sha", git_sha("HEAD")}, {"trunk_sha", git_sha("refs/heads/nonplanar")}}},
              {"results", {{"patches", Json::array()}}}, {"objects", Json::array()},
              {"totals", {{"whole_top_rms_mm", nullptr}, {"zaa_rms_mm", nullptr}, {"planar_rms_mm", nullptr},
                          {"aborts", Json::array()}, {"exceptions", Json::array()}, {"coverage", nullptr}, {"gap_cells", nullptr},
                          {"ironed_area_mm2", nullptr}, {"estimated_time_s", nullptr}, {"cpu_seconds", nullptr},
                          {"gcode_bytes", nullptr}, {"retractions", nullptr}, {"travel_length_mm", nullptr},
                          {"verifier", {{"ran", false}, {"hit_moves", nullptr}, {"machine_hit_moves", nullptr},
                                        {"first_hits", Json::array()}, {"min_clearance_mm", nullptr}, {"omissions", Json::array()}}}}},
              {"measurement", {{"pitch_mm", 0.25}, {"inset_mm", 2.}, {"wall_slope_deg", 70.}, {"gap_threshold_mm", 1.}}},
              {"tool", Json::object()}, {"text_summary", Json::object()}};
    auto &meshes = record["inputs"]["meshes"];
    meshes = Json::array();
    meshes.push_back({{"file", args.model}, {"sha1", nullptr}});
    for (const auto &part : args.parts)
        meshes.push_back({{"file", part.first}, {"sha1", nullptr}, {"extruder", part.second}});
    for (auto &mesh : meshes) mesh["sha1"] = file_sha1(mesh["file"].get<std::string>());
}

struct SliceGrid
{
    static constexpr double pitch = 0.25;
    Vec2d origin;
    int nx, ny;
    std::vector<double> mesh, normal, bead;
    std::vector<unsigned char> conformal, ironed, eligible;
    explicit SliceGrid(const indexed_triangle_set &its)
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
        for (const auto &f : its.indices) {
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
                if (z > mesh[k]) { mesh[k] = z; normal[k] = d / n.norm(); }
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
                if (z > bead[k]) { bead[k] = z; conformal[k] = m.nonplanar_patch_shell != 0xFF; }
            });
        }
    }
    template<class F> Json measure(F accepts) const
    {
        double squared = 0.;
        size_t cells = 0, finite = 0, gaps = 0, covered = 0, iron = 0;
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
            const size_t k = size_t(y) * nx + x;
            if (!eligible[k] || !accepts(origin + pitch * Vec2d(x + 0.5, y + 0.5))) continue;
            ++cells;
            const double error = bead[k] - mesh[k];
            if (std::isfinite(error)) { squared += error * error; ++finite; }
            if (error < -1.) ++gaps;
            if (conformal[k] && std::abs(error) <= 1.) ++covered;
            iron += ironed[k];
        }
        return {{"rms_vs_mesh_mm", finite ? Json(std::sqrt(squared / finite)) : Json(nullptr)},
                {"coverage", cells ? Json(double(covered) / cells) : Json(nullptr)},
                {"gap_cells", gaps}, {"ironed_area_mm2", iron * pitch * pitch}, {"eval_cells", cells},
                {"finite_cells", finite}};
    }
};

void slice_json_results(Json &record, const Args &args, const Print &print, const GCodeProcessorResult &result,
                        const Model &model, const DynamicPrintConfig &config)
{
    Json &tool = record["tool"], &totals = record["totals"];
    size_t line = 0;
    const auto vz = max_vz(result, &line);
    const auto &normal = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
    tool["estimated_time_s"] = text_number(normal.time);
    tool["max_vz_commanded"] = text_number(vz.first);
    tool["max_vz_planned"] = text_number(vz.second);
    tool["machine_max_speed_z"] = text_number(config.opt<ConfigOptionFloats>("machine_max_speed_z")->get_at(0));
    const auto &clearance = result.clearance_report;
    tool["clearance_checked"] = int(clearance.ran);
    if (clearance.ran) {
        tool["clearance_hit_moves"] = clearance.hit_moves;
        tool["clearance_min_mm"] = std::isfinite(clearance.min_clearance) ? Json(text_number(clearance.min_clearance)) : Json("inf");
    }
    totals["estimated_time_s"] = normal.time;
    totals["gcode_bytes"] = fs::file_size(args.output);
    totals["retractions"] = 0;
    double travel = 0.;
    Json hits = Json::array();
    size_t first_print = result.moves.size(), last_print = 0;
    for (size_t i = 0; i < result.moves.size(); ++i)
        if (result.moves[i].type == EMoveType::Extrude && result.moves[i].extrusion_role != erCustom && result.moves[i].extrusion_role != erNone) {
            first_print = std::min(first_print, i); last_print = i;
        }
    bool turned = false;
    for (const auto &m : result.moves) turned |= m.has_joints && (m.joints.rot[0] != 0. || m.joints.rot[1] != 0.);
    for (size_t i = 0; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        if (m.type == EMoveType::Retract) totals["retractions"] = totals["retractions"].get<size_t>() + 1;
        if (m.type == EMoveType::Travel && i > 0) travel += (m.position - result.moves[i - 1].position).norm();
        if (hits.size() < 10 && i < clearance.per_move.size() && clearance.per_move[i] < -1e-5 &&
            !(m.extrusion_role == erCustom && (i < first_print || i > last_print))) {
            const Vec3d tip = !turned && m.has_joints ? m.joints.xyz : Vec3d(m.position.cast<double>());
            Json hit = {{"move_id", i}, {"gcode_line", m.gcode_id}, {"tip_mm", {tip.x(), tip.y(), tip.z()}},
                        {"penetration_mm", -clearance.per_move[i]}, {"component", nullptr}};
            for (const auto &worst : clearance.worst) if (worst.move_id == i) {
                hit["component"] = worst.component;
                hit["tip_mm"] = {worst.tip.x(), worst.tip.y(), worst.tip.z()};
            }
            hits.push_back(std::move(hit));
        }
    }
    totals["travel_length_mm"] = travel;
    totals["verifier"] = {{"ran", clearance.ran}, {"hit_moves", clearance.hit_moves},
                          {"machine_hit_moves", clearance.machine_hit_moves}, {"first_hits", hits},
                          {"min_clearance_mm", clearance.min_clearance}, {"omissions", clearance.omissions}};
    tool["fallback"] = Json::array();
    SliceGrid grid(plate_mesh(model));
    grid.deposit(result);
    const Json quality = grid.measure([](const Vec2d &) { return true; });
    totals["whole_top_rms_mm"] = quality["rms_vs_mesh_mm"];
    totals["coverage"] = quality["coverage"];
    totals["gap_cells"] = quality["gap_cells"];
    totals["ironed_area_mm2"] = quality["ironed_area_mm2"];
    totals["eval_cells"] = quality["eval_cells"];
    totals["finite_cells"] = quality["finite_cells"];
    const bool control = std::all_of(print.objects().begin(), print.objects().end(), [](const PrintObject *object) {
        return object->config().nonplanar_mode.value == NonplanarMode::Off;
    });
    if (control)
        totals[config.opt_bool("zaa_enabled") ? "zaa_rms_mm" : "planar_rms_mm"] = totals["whole_top_rms_mm"];
    size_t object_id = 0;
    for (const PrintObject *object : print.objects()) {
        Json entry = {{"id", object_id}, {"name", object->model_object()->name}, {"mode", object->config().nonplanar_mode.serialize()}, {"instances", Json::array()}, {"patches", Json::array()}};
        size_t top = 0, bands = 0;
        double footprint = 0.;
        for (const auto &instance : object->instances())
            entry["instances"].push_back({{"id", instance.id}, {"shift_mm", {unscale<double>(instance.shift.x()), unscale<double>(instance.shift.y())}}});
        const auto in_footprint = [&](const Vec2d &p, const ExPolygons &polygons) {
            for (const auto &instance : object->instances()) {
                const Vec2d local = p - Vec2d(unscale<double>(instance.shift.x()), unscale<double>(instance.shift.y()));
                const Point node = Point::new_scale(local);
                if (std::any_of(polygons.begin(), polygons.end(), [&node](const ExPolygon &polygon) { return polygon.contains(node); }))
                    return true;
            }
            return false;
        };
        for (const auto &patch : object->conformal().patches) {
            const double a = area(patch.footprint) * SCALING_FACTOR * SCALING_FACTOR;
            Json p = grid.measure([&](const Vec2d &xy) { return in_footprint(xy, patch.footprint); });
            static const char *kinds[] = {"top", "bottom", "volume", "overhang", "chunk", "support-interface", "bridge"};
            p.update({{"id", patch.id}, {"object_id", object_id}, {"kind", kinds[size_t(patch.kind)]},
                      {"area_mm2", a}, {"mode_chosen", patch.kind == Nonplanar::PatchKind::Chunk ? "multiplanar" : "conformal"},
                      {"band_count", patch.band < 0 ? 0 : 1}, {"band", patch.band}, {"shells", patch.shells}, {"selection", patch.automatic ? "auto" : "configured"},
                      {"reason_tags", Json::array()}, {"surface_rms_mm", nullptr}});
            for (const auto &diag : object->conformal().diagnostics) if (diag.patch_id == patch.id) {
                p["surface_rms_mm"] = diag.rms_surface_error;
                if (!diag.fallback_reason.empty()) p["reason_tags"].push_back(diag.fallback_reason);
            }
            entry["patches"].push_back(p);
            record["results"]["patches"].push_back(std::move(p));
            if (patch.kind == Nonplanar::PatchKind::Top) { ++top; bands = std::max(bands, size_t(patch.band + 1)); footprint += a; }
        }
        bool fallback_zaa = config.opt_bool("zaa_enabled");
        for (const PrintRegion &region : object->all_regions())
            fallback_zaa = fallback_zaa || Nonplanar::zaa_fallback(region.config(), object->config());
        for (const auto &diag : object->conformal().diagnostics) if (diag.fallback_area > 0.) {
            const std::string reason = diag.fallback_reason.empty() ? "untagged" : diag.fallback_reason;
            tool["fallback"].push_back({{"reason", reason}, {"area_mm2", text_number(diag.fallback_area)}, {"patch", diag.patch_id}});
            Json fallback = {{"id", diag.patch_id}, {"object_id", object_id}, {"area_mm2", diag.fallback_area},
                             {"mode_chosen", fallback_zaa ? "zaa" : "planar"},
                             {"kind", "fallback"}, {"reason_tags", {reason}}, {"coverage", 0.}, {"rms_vs_mesh_mm", nullptr},
                             {"gap_cells", nullptr}, {"ironed_area_mm2", 0.}, {"band_count", 0}};
            entry["patches"].push_back(fallback);
            record["results"]["patches"].push_back(std::move(fallback));
        }
        tool["nonplanar_top_patches"] = top;
        tool["nonplanar_bands"] = bands;
        tool["nonplanar_top_footprint_mm2"] = text_number(footprint);
        if (print.objects().size() == 1) {
            entry["totals"] = quality;
        } else {
            const indexed_triangle_set its = object->model_object()->raw_indexed_triangle_set();
            indexed_triangle_set object_mesh;
            for (const auto &instance : object->instances()) {
                indexed_triangle_set copy = its;
                its_transform(copy, instance.model_instance->get_matrix());
                its_merge(object_mesh, copy);
            }
            SliceGrid object_grid(object_mesh);
            object_grid.deposit(result);
            entry["totals"] = object_grid.measure([](const Vec2d &) { return true; });
        }
        record["objects"].push_back(std::move(entry));
        ++object_id;
    }
}

void write_slice_json(const std::string &path, Json &record)
{
    record["totals"]["cpu_seconds"] = process_cpu_seconds();
    const fs::path destination(path);
    const fs::path temporary = destination.parent_path() / fs::unique_path(destination.filename().string() + ".%%%%%%.tmp");
    try {
        boost::nowide::ofstream out(temporary.string(), std::ios::binary | std::ios::trunc);
        out << record.dump(2) << '\n';
        out.close();
        if (!out) throw std::runtime_error("cannot write slice JSON " + path);
        fs::rename(temporary, destination);
    } catch (...) {
        boost::system::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
}
