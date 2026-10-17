#include "Report.hpp"

#include "AutoMode.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "HeightField.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace Slic3r::Nonplanar {

namespace {

const char *pattern_name(NonplanarTopPattern pattern)
{
    return pattern == NonplanarTopPattern::Contour ? "contour" : pattern == NonplanarTopPattern::Concentric ? "concentric" : "rectilinear";
}

bool is_underside(const std::string &reason) { return reason == "bottom" || boost::starts_with(reason, "overhang"); }

float surface_z(const HeightField &surface, const Vec2d &p)
{
    if (const float z = surface.sample(p); !std::isnan(z))
        return z;
    const Vec2d g  = (p - surface.origin()) / surface.pitch();
    const int   ix = int(std::floor(g.x())), iy = int(std::floor(g.y()));
    float       best = HeightField::NoData;
    double      best_d = std::numeric_limits<double>::max();
    for (int y = iy - 1; y <= iy + 2; ++y)
        for (int x = ix - 1; x <= ix + 2; ++x)
            if (x >= 0 && y >= 0 && x < surface.nx() && y < surface.ny() && !std::isnan(surface.at(x, y)))
                if (const double d = (surface.node(x, y) - p).squaredNorm(); d < best_d)
                    best_d = d, best = surface.at(x, y);
    return best;
}

}

std::string patch_kind_name(int kind)
{
    static const char *names[] = { "top", "bottom", "volume", "overhang", "chunk", "support interface", "bridge", "field" };
    return kind >= 0 && kind < int(std::size(names)) ? names[kind] : "patch";
}

namespace {
struct ReasonInfo
{
    const char *cls;
    const char *text;
};
const std::map<std::string, ReasonInfo> &reasons()
{
    static const std::map<std::string, ReasonInfo> map{
        { "flat", { "flat", L("flat: planar layers print it exactly") } },
        { "flat_ground", { "flat", L("flat ground: planar layers, Z contoured, print it exactly") } },
        { "flat_sliver", { "flat", L("a sliver beside flat ground, printed with it") } },
        { "steep", { "wall", L("steeper than a curved bead follows (atan(h/w)): planar layers, Z contoured") } },
        { "steep_strip", { "wall", L("a steep strip between curved tops: planar layers, Z contoured") } },
        { "narrow", { "wall", L("narrower than half a road: the contoured planar top measured better") } },
        { "outline", { "outline", L("outline between surface samples") } },
        { "near_bed", { "fallback", L("too close to the first layer for a shell") } },
        { "no_transition_layer", { "fallback", L("no planar layer under the first shell within the bead range") } },
        { "no_pieces", { "fallback", L("the bands laid no road there") } },
        { "shell_height", { "unreachable", L("shell layer too thick for the nozzle") } },
        { "edge", { "fallback", L("edge of a curved top: near the bed, too steep or narrower than a road") } },
        { "head", { "fallback", L("the print head cannot reach it") } },
        { "bottom", { "fallback", L("underside printed by planar bottom layers") } },
        { "overhang_self_supporting", { "wall", L("self-supporting underside: planar layers print it") } },
        { "overhang_unfit", { "wall", L("an overhang sliver under a road") } },
        { "overhang_roof", { "wall", L("flat roof: a bridge prints it") } },
        { "overhang_no_root", { "unreachable", L("overhang with nothing below to grow from") } },
        { "overhang_rootless", { "unreachable", L("overhang with nothing below to grow from") } },
        { "overhang_resting", { "unreachable", L("underside on or just above the bed") } },
        { "overhang_unsampled", { "unreachable", L("underside over solid material") } },
        { "overhang_supported", { "unreachable", L("overhang the curved layers cannot make printable: support prints it") } },
        { "overhang_small", { "fallback", L("overhang smaller than the minimum patch") } },
        { "overhang_no_beads", { "fallback", L("overhang no bead fits") } },
        { "overhang_no_pieces", { "fallback", L("the overhang laid no road there") } },
        { "overhang_narrow", { "wall", L("overhang narrower than a road") } },
        { "overhang_outline", { "outline", L("overhang outline between surface samples") } },
        { "overhang_rim", { "outline", L("overhang rim without a slope") } },
        { "untagged", { "fallback", L("no reason recorded") } },
    };
    return map;
}
}

std::string fallback_reason_text(const std::string &reason)
{
    const auto it = reasons().find(reason);
    return it == reasons().end() ? reason : it->second.text;
}

std::string fallback_class(const std::string &reason)
{
    const auto it = reasons().find(reason);
    return it == reasons().end() ? "fallback" : it->second.cls;
}

bool fallback_is_exact(const std::string &reason) { return fallback_class(reason) != "fallback"; }

bool fallback_hatched(const std::string &reason) { return fallback_class(reason) != "outline"; }

void record_fallback_shape(PatchDiagnostics &diag, ExPolygons shape, const HeightField &surface)
{
    shape = opening_ex(shape, float(scale_(0.05)));
    if (shape.empty() || surface.empty())
        return;
    const BoundingBox bb   = get_extents(shape);
    const coord_t     step = scaled<coord_t>(1.5);
    Polylines         lines;
    for (coord_t c = bb.min.x() - bb.max.y(); c <= bb.max.x() - bb.min.y(); c += step)
        lines.emplace_back(Point(bb.min.y() + c, bb.min.y()), Point(bb.max.y() + c, bb.max.y()));
    lines = intersection_pl(lines, shape);
    for (const ExPolygon &expoly : shape)
        for (const Polygon &polygon : to_polygons(expoly))
            lines.emplace_back(polygon.split_at_first_point());
    const double sample = 0.5;
    for (const Polyline &line : lines) {
        std::vector<Vec3f> draped;
        auto flush = [&] {
            if (draped.size() > 1)
                diag.fallback_hatch.emplace_back(std::move(draped));
            draped.clear();
        };
        auto add = [&](const Vec2d &p) {
            const float z = surface_z(surface, p);
            if (std::isnan(z) || (!draped.empty() && std::abs(z - draped.back().z()) > 2. * sample))
                flush();
            if (!std::isnan(z))
                draped.emplace_back(float(p.x()), float(p.y()), z);
        };
        for (size_t i = 0; i + 1 < line.points.size(); ++i) {
            const Vec2d a = unscale(line.points[i]), b = unscale(line.points[i + 1]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / sample)));
            for (int k = 0; k < n; ++k)
                add(a + (b - a) * (double(k) / n));
        }
        if (!line.points.empty())
            add(unscale(line.points.back()));
        flush();
    }
    diag.fallback_shape = std::move(shape);
}

NonplanarReport make_report(const Print &print, const GCodeProcessorResult &result, bool planar_too)
{
    NonplanarReport report;
    if (!planar_too && std::all_of(print.objects().begin(), print.objects().end(), [](const PrintObject *object) {
            return object->conformal().patches.empty() && object->conformal().diagnostics.empty();
        }))
        return report;
    std::string     head_warning;
    report.head = resolve_head_envelope(print.config(), &head_warning).id;
    for (const PrintObject *object : print.objects()) {
        const ConformalResult &conformal = object->conformal();
        const size_t  index = report.objects.size();
        ReportObject &out   = report.objects.emplace_back();
        out.name            = object->model_object()->name;
        out.model_object_id = object->model_object()->id().id;
        out.mode            = object->config().nonplanar_mode.serialize();
        for (size_t r = 0; r < object->num_printing_regions(); ++r) {
            const PrintRegionConfig &config = object->printing_region(r).config();
            std::string              keys;
            for (const char *key : { "top_shell_layers", "top_shell_thickness", "top_surface_pattern", "nonplanar_top_shells", "nonplanar_top_pattern",
                                     "sparse_infill_density", "wall_loops", "fuzzy_skin", "ironing_type" })
                keys += std::string(keys.empty() ? "" : " ") + key + "=" + config.opt_serialize(key);
            out.regions.emplace_back(std::move(keys));
        }
        for (const ConformalPatch &patch : conformal.patches) {
            const bool top = patch.kind == PatchKind::Top;
            ++(top ? out.top_patches : out.other_patches);
            ReportPatch &rp = report.patches.emplace_back();
            rp.object       = index;
            rp.id           = patch.id;
            rp.kind         = int(patch.kind);
            rp.top          = top;
            rp.region       = patch.region_id;
            rp.band         = patch.band;
            rp.shells       = patch.shells;
            rp.area         = area(patch.footprint) * SCALING_FACTOR * SCALING_FACTOR;
            const BoundingBox box = get_extents(patch.footprint);
            rp.center = unscale(box.center());
            for (const PrintInstance &instance : object->instances()) {
                BoundingBox shifted = box;
                shifted.translate(instance.shift);
                rp.boxes.emplace_back(shifted);
            }
            if (!top)
                continue;
            out.bands = std::max(out.bands, size_t(patch.band + 1));
            out.curved_top += rp.area;
            rp.pattern = pattern_name(top_pattern(patch, object->printing_region(size_t(patch.region_id)).config()));
            rp.ironing = !patch.ironing.entities.empty();
            rp.explain = explain_top_patch(*object, patch);
            while (!rp.explain.empty() && rp.explain.back() == '\n')
                rp.explain.pop_back();
            rp.summary = describe_top_patch(*object, patch);
        }
        for (const PatchDiagnostics &diag : conformal.diagnostics) {
            if (diag.fallback_area <= 0.)
                continue;
            ReportFallback &fb = report.fallbacks.emplace_back();
            fb.object          = index;
            fb.patch           = diag.patch_id;
            fb.reason          = diag.fallback_reason.empty() ? "untagged" : diag.fallback_reason;
            fb.area            = diag.fallback_area;
            fb.top             = !is_underside(fb.reason);
            fb.cls             = fallback_class(fb.reason);
            if (fb.top)
                (fallback_is_exact(fb.reason) ? out.flat_top : out.planar_top) += fb.area;
            for (const PrintInstance &instance : object->instances()) {
                for (ExPolygon expoly : diag.fallback_shape) {
                    expoly.translate(instance.shift);
                    fb.shape.emplace_back(std::move(expoly));
                }
                const Vec3f shift(float(unscale<double>(instance.shift.x())), float(unscale<double>(instance.shift.y())), 0.f);
                for (std::vector<Vec3f> line : diag.fallback_hatch) {
                    for (Vec3f &p : line)
                        p += shift;
                    fb.hatch.emplace_back(std::move(line));
                }
            }
        }
        ExPolygons supported;
        for (const ExPolygons &layer : conformal.self_supported)
            expolygons_append(supported, layer);
        out.support_free = area(union_ex(supported)) * SCALING_FACTOR * SCALING_FACTOR;
        out.warnings     = object_warnings(*object, out);
        if (index == 0 && !head_warning.empty())
            out.warnings.insert(out.warnings.begin(), head_warning);
    }
    if (report.empty())
        return report;
    const auto &report_clearance = result.clearance_report;
    report.verified     = report_clearance.ran;
    report.hits         = report_clearance.hit_moves;
    report.machine_hits = report_clearance.machine_hit_moves;
    const size_t normal = size_t(PrintEstimatedStatistics::ETimeMode::Normal);
    report.time_s       = result.print_statistics.modes[normal].time;
    for (const GCodeProcessorResult::MoveVertex &move : result.moves)
        if (result.state(move).nonplanar_patch_shell != 0xFF)
            report.curved_time_s += move.time[normal];
    return report;
}

const ReportPatch *NonplanarReport::find_patch(int id, const Point &pos) const
{
    const ReportPatch *best   = nullptr;
    double             best_d = std::numeric_limits<double>::max();
    for (const ReportPatch &patch : patches)
        if (patch.id == id)
            for (const BoundingBox &box : patch.boxes) {
                if (box.contains(pos))
                    return &patch;
                if (const double d = (box.center() - pos).cast<double>().squaredNorm(); d < best_d)
                    best_d = d, best = &patch;
            }
    return best;
}

std::string NonplanarReport::to_json(const std::map<size_t, std::string> &orientation) const
{
    using nlohmann::json;
    json j;
    j["version"] = 1;
    j["head"]    = head;
    j["clearance"] = verified ? json{ { "checked", true }, { "hits", hits }, { "machine_hits", machine_hits } } : json{ { "checked", false } };
    j["time_s"]        = time_s;
    j["curved_time_s"] = curved_time_s;
    json objects_json  = json::array();
    for (size_t i = 0; i < objects.size(); ++i) {
        const ReportObject &o = objects[i];
        const auto  note      = orientation.find(o.model_object_id);
        json        jo{ { "name", o.name }, { "mode", o.mode }, { "top_patches", o.top_patches }, { "other_patches", o.other_patches },
                        { "bands", o.bands }, { "curved_top_mm2", o.curved_top }, { "planar_top_mm2", o.planar_top },
                        { "flat_top_mm2", o.flat_top }, { "curved_share", o.curved_share() }, { "support_free_mm2", o.support_free },
                        { "orientation", note != orientation.end() ? note->second : o.orientation }, { "warnings", o.warnings } };
        json patches_json = json::array();
        for (const ReportPatch &p : patches)
            if (p.object == i)
            {
                json jp{ { "id", p.id }, { "kind", patch_kind_name(p.kind) }, { "region", p.region }, { "band", p.band },
                         { "shells", p.shells }, { "area_mm2", p.area } };
                if (p.top) {
                    jp["pattern"] = p.pattern;
                    jp["ironing"] = p.ironing;
                    jp["explain"] = p.explain;
                }
                patches_json.push_back(std::move(jp));
            }
        json fallbacks_json = json::array();
        for (const ReportFallback &f : fallbacks)
            if (f.object == i)
                fallbacks_json.push_back({ { "reason", f.reason }, { "class", f.cls }, { "why", fallback_reason_text(f.reason) }, { "area_mm2", f.area },
                                           { "top", f.top }, { "patch", f.patch } });
        jo["regions"]   = o.regions;
        jo["patches"]   = std::move(patches_json);
        jo["fallbacks"] = std::move(fallbacks_json);
        objects_json.push_back(std::move(jo));
    }
    j["objects"] = std::move(objects_json);
    return j.dump(1, ' ', false, json::error_handler_t::replace);
}

}
