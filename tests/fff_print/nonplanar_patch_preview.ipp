#pragma once
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/PatchPreview.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

nlohmann::json preview_footprint(const ExPolygons &expolys, const Point &shift)
{
    auto ring = [&shift](const Polygon &polygon) {
        nlohmann::json points = nlohmann::json::array();
        for (const Point &p : polygon.points)
            points.push_back({ unscale<double>(p.x() + shift.x()), unscale<double>(p.y() + shift.y()) });
        return points;
    };
    nlohmann::json out = nlohmann::json::array();
    for (const ExPolygon &expoly : expolys) {
        nlohmann::json holes = nlohmann::json::array();
        for (const Polygon &hole : expoly.holes)
            holes.push_back(ring(hole));
        out.push_back({ { "contour", ring(expoly.contour) }, { "holes", std::move(holes) } });
    }
    return out;
}

std::vector<float> preview_patch_lines(const Nonplanar::ConformalPatch &patch, const Point &shift, double pitch)
{
    constexpr double hatch_spacing = 1.5;
    std::vector<float> out;
    const Vec2d bed = unscale(shift);
    auto add = [&](const Polyline &polyline) {
        bool  open = false;
        Vec3f last;
        for (size_t i = 0; i + 1 < polyline.points.size(); ++i) {
            const Vec2d a = unscale(polyline.points[i]), b = unscale(polyline.points[i + 1]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / pitch)));
            for (int k = i == 0 ? 0 : 1; k <= n; ++k) {
                const Vec2d p = a + (b - a) * (double(k) / n);
                const float z = patch.surface.sample(p);
                if (std::isnan(z)) {
                    open = false;
                    continue;
                }
                const Vec3f v(float(p.x() + bed.x()), float(p.y() + bed.y()), z);
                if (open)
                    out.insert(out.end(), { last.x(), last.y(), last.z(), v.x(), v.y(), v.z() });
                last = v;
                open = true;
            }
        }
    };
    for (const ExPolygon &expoly : patch.footprint)
        for (const Polygon &poly : to_polygons(expoly))
            add(poly.split_at_first_point());
    ExPolygons turned = patch.footprint;
    for (ExPolygon &expoly : turned)
        expoly.rotate(-0.25 * PI);
    const BoundingBox bb   = get_extents(turned);
    const coord_t     step = scale_(hatch_spacing);
    Polylines         lines;
    for (coord_t y = bb.min.y() + step / 2; y < bb.max.y(); y += step)
        lines.emplace_back(Polyline(Point(bb.min.x() - step, y), Point(bb.max.x() + step, y)));
    for (Polyline &line : intersection_pl(lines, turned)) {
        line.rotate(0.25 * PI);
        add(line);
    }
    return out;
}

const char *preview_kind(Nonplanar::PatchKind kind)
{
    switch (kind) {
    case Nonplanar::PatchKind::Top: return "top";
    case Nonplanar::PatchKind::Bottom: return "bottom";
    case Nonplanar::PatchKind::Volume: return "volume";
    case Nonplanar::PatchKind::Overhang: return "overhang";
    case Nonplanar::PatchKind::Chunk: return "chunk";
    case Nonplanar::PatchKind::SupportInterface: return "support_interface";
    case Nonplanar::PatchKind::Bridge: return "bridge";
    case Nonplanar::PatchKind::Field: return "field";
    }
    return "unknown";
}

struct PreviewSummary
{
    double up = 0., flat = 0., curved = 0., overhang = 0., overhang_curved = 0.;
    std::array<double, size_t(Nonplanar::SlopeBand::Count)> bands{};
    void add(const PreviewSummary &other)
    {
        up += other.up; flat += other.flat; curved += other.curved;
        overhang += other.overhang; overhang_curved += other.overhang_curved;
        for (size_t i = 0; i < bands.size(); ++i)
            bands[i] += other.bands[i];
    }
    nlohmann::json to_json(bool sliced) const
    {
        auto measured = [sliced](double v) { return sliced ? nlohmann::json(v) : nlohmann::json(nullptr); };
        return { { "up_area_mm2", up },
                 { "band_area_mm2", { { "within", bands[0] }, { "near", bands[1] }, { "tilt", bands[2] }, { "beyond", bands[3] } } },
                 { "flat_area_mm2", flat },
                 { "curved_area_mm2", measured(curved) },
                 { "overhang_area_mm2", overhang },
                 { "overhang_curved_area_mm2", measured(overhang_curved) },
                 { "overhang_supported_area_mm2", measured(std::max(0., overhang - overhang_curved)) } };
    }
};

void write_patch_preview(const Args &args, const Model &model, const DynamicPrintConfig &config)
{
    const auto start = std::chrono::steady_clock::now();
    DynamicPrintConfig auto_config = config;
    auto_config.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonplanarMode>(NonplanarMode::Auto));
    PrintConfig printer;
    printer.apply(auto_config, true);
    nlohmann::json out = { { "schema", "orca.nonplanar_patch_preview.v1" }, { "as_mode", "auto" },
                           { "bands", { "within", "near", "tilt", "beyond" } }, { "head", nullptr },
                           { "partial", false }, { "objects", nlohmann::json::array() } };
    auto write = [&]() {
        boost::nowide::ofstream file(args.patch_preview, std::ios::binary | std::ios::trunc);
        file << out.dump() << '\n';
        file.close();
        if (!file)
            throw std::runtime_error("cannot write " + args.patch_preview);
    };
    if (config.opt_string("nonplanar_head").empty())
        return write();
    const Nonplanar::HeadEnvelope head = Nonplanar::resolve_head_envelope(printer);
    out["head"] = { { "id", head.id }, { "name", head.name }, { "calibrated", head.calibrated } };

    const double overhang_angle = config.opt_int("support_threshold_angle") > 0 ? config.opt_int("support_threshold_angle") : 30.;
    std::vector<PreviewSummary> summaries(model.objects.size());
    std::vector<std::vector<Vec3d>> shifts(model.objects.size());
    for (size_t o = 0; o < model.objects.size(); ++o) {
        const ModelObject *object = model.objects[o];
        DynamicPrintConfig object_config = auto_config;
        object_config.apply(object->config.get(), true);
        object_config.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonplanarMode>(NonplanarMode::Auto));
        const Nonplanar::PatchPreviewParams params = Nonplanar::patch_preview_params(object_config);
        const std::vector<double>           limits = Nonplanar::preview_slope_limits(head, params);
        nlohmann::json entry = { { "index", o }, { "name", object->name }, { "instances", nlohmann::json::array() } };
        for (size_t i = 0; i < object->instances.size(); ++i) {
            const ModelInstance *instance = object->instances[i];
            indexed_triangle_set its;
            for (const ModelVolume *volume : object->volumes)
                if (volume->is_model_part()) {
                    indexed_triangle_set part = volume->mesh().its;
                    its_transform(part, instance->get_matrix() * volume->get_matrix());
                    its_merge(its, part);
                }
            const float bottom = its.vertices.empty() ? 0.f : std::min_element(its.vertices.begin(), its.vertices.end(),
                [](const Vec3f &a, const Vec3f &b) { return a.z() < b.z(); })->z();
            auto normal_of = [&its](const stl_triangle_vertex_indices &f) {
                const Vec3f &a = its.vertices[f[0]], &b = its.vertices[f[1]], &c = its.vertices[f[2]];
                return Vec3d((b - a).cross(c - a).cast<double>());
            };
            const size_t up = size_t(std::count_if(its.indices.begin(), its.indices.end(),
                [&](const stl_triangle_vertex_indices &f) { return normal_of(f).normalized().z() > 0.05; }));
            const bool decimated = up > args.preview_max_triangles;
            if (decimated) {
                const uint32_t target = uint32_t(double(its.indices.size()) * double(args.preview_max_triangles) / double(up));
                its_quadric_edge_collapse(its, target);
            }
            PreviewSummary summary;
            std::array<std::vector<float>, size_t(Nonplanar::SlopeBand::Count)> bands;
            for (const stl_triangle_vertex_indices &f : its.indices) {
                const Vec3d  cross = normal_of(f);
                const double norm  = cross.norm();
                if (!(norm > 0.))
                    continue;
                const Vec3d  n         = cross / norm;
                const double projected = 0.5 * std::abs(cross.z());
                if (n.z() < -std::cos(Geometry::deg2rad(overhang_angle))) {
                    const float low = std::max({ its.vertices[f[0]].z(), its.vertices[f[1]].z(), its.vertices[f[2]].z() });
                    if (low > bottom + 0.05f)
                        summary.overhang += projected;
                    continue;
                }
                if (!(n.z() > 0.05))
                    continue;
                const auto [slope, phi] = Nonplanar::face_slope(n);
                const auto band = Nonplanar::slope_band(slope, Nonplanar::slope_limit_at(limits, phi), params.max_tilt);
                summary.up += projected;
                summary.bands[size_t(band)] += projected;
                if (slope < Geometry::deg2rad(0.5))
                    summary.flat += projected;
                for (int k = 0; k < 3; ++k)
                    bands[size_t(band)].insert(bands[size_t(band)].end(),
                                               { its.vertices[f[k]].x(), its.vertices[f[k]].y(), its.vertices[f[k]].z() });
            }
            summaries[o].add(summary);
            nlohmann::json band_triangles = nlohmann::json::object();
            for (size_t b = 0; b < bands.size(); ++b)
                band_triangles[out["bands"][b].get<std::string>()] = base64_floats(bands[b]);
            std::vector<double> limits_deg;
            for (double limit : limits)
                limits_deg.push_back(Geometry::rad2deg(limit));
            entry["instances"].push_back({ { "index", i }, { "printable", instance->is_printable() }, { "slope_limit_deg", limits_deg },
                                           { "decimated", decimated }, { "band_triangles", std::move(band_triangles) },
                                           { "patches", nlohmann::json::array() }, { "planar_areas", nlohmann::json::array() } });
        }
        out["objects"].push_back(std::move(entry));
    }

    Model  copy  = model;
    clear_object_modes(copy);
    Print  print;
    bool   sliced = false;
    std::mutex              mutex;
    std::condition_variable done;
    bool                    finished = false;
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!done.wait_until(lock, start + std::chrono::duration<double>(args.preview_budget), [&] { return finished; }))
            print.cancel();
    });
    try {
        print.is_BBL_printer() = config.opt_string("printer_model").rfind("Bambu Lab", 0) == 0;
        print.set_status_silent();
        for (ModelObject *object : copy.objects)
            print.auto_assign_extruders(object);
        print.apply(copy, auto_config);
        if (const StringObjectException err = print.validate(); !err.string.empty())
            throw std::runtime_error("validate: " + err.string);
        print.process();
        sliced = true;
    } catch (const CanceledException &) {
    } catch (const std::exception &e) {
        out["error"] = e.what();
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
    }
    done.notify_one();
    watchdog.join();
    out["partial"] = !sliced;

    size_t rotary_chunks = 0;
    if (sliced)
        for (const PrintObject *object : print.objects()) {
            const ModelObjectPtrs &print_objects = print.model().objects;
            const auto o = size_t(std::find(print_objects.begin(), print_objects.end(), object->model_object()) - print_objects.begin());
            const ModelObject *model_object = print_objects[o];
            const Nonplanar::ConformalResult &conformal = object->conformal();
            const double pitch = std::max(0.05, object->config().nonplanar_surface_resolution.value);
            ExPolygons unsupported;
            for (const auto *layers : { &conformal.self_supported, &conformal.flat_printed, &conformal.bridged })
                for (const ExPolygons &layer : *layers)
                    append(unsupported, layer);
            const double overhang_curved = area(union_ex(unsupported)) * SCALING_FACTOR * SCALING_FACTOR;
            for (const PrintInstance &instance : object->instances()) {
                const auto i = size_t(std::find(model_object->instances.begin(), model_object->instances.end(), instance.model_instance) -
                                      model_object->instances.begin());
                nlohmann::json &entry = out["objects"][o]["instances"][i];
                for (const Nonplanar::ConformalPatch &patch : conformal.patches) {
                    if (patch.footprint.empty())
                        continue;
                    const double patch_area = area(patch.footprint) * SCALING_FACTOR * SCALING_FACTOR;
                    if (patch.kind == Nonplanar::PatchKind::Top)
                        summaries[o].curved += patch_area;
                    if (patch.kind == Nonplanar::PatchKind::Chunk)
                        ++rotary_chunks;
                    entry["patches"].push_back({ { "id", patch.id }, { "kind", preview_kind(patch.kind) }, { "area_mm2", patch_area },
                                                 { "z", { patch.z_min, patch.z_max } }, { "footprint", preview_footprint(patch.footprint, instance.shift) },
                                                 { "lines", base64_floats(preview_patch_lines(patch, instance.shift, pitch)) } });
                }
                for (const Nonplanar::PatchDiagnostics &diag : conformal.diagnostics)
                    if (diag.fallback_area > 0.)
                        entry["planar_areas"].push_back({ { "patch", diag.patch_id },
                                                          { "reason", diag.fallback_reason.empty() ? "untagged" : diag.fallback_reason },
                                                          { "area_mm2", diag.fallback_area },
                                                          { "footprint", preview_footprint(diag.fallback_shape, instance.shift) } });
                summaries[o].overhang_curved += overhang_curved;
            }
        }
    PreviewSummary total;
    for (size_t o = 0; o < summaries.size(); ++o) {
        out["objects"][o]["summary"] = summaries[o].to_json(sliced);
        total.add(summaries[o]);
    }
    out["summary"] = total.to_json(sliced);
    if (printer.nonplanar_kinematics.value != RotaryLayout::None)
        out["rotary"] = { { "multiplanar_chunks", sliced ? nlohmann::json(rotary_chunks) : nlohmann::json(nullptr) } };
    out["time_s"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    write();
}

}
