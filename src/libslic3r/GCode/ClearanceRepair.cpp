#include "ClearanceRepair.hpp"

#include "ClearanceVerifier.hpp"
#include "GCodeProcessor.hpp"
#include "ToolKinematics.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/HeapTrim.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Nonplanar/AutoMode.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/HeadBoxLabel.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

using MoveVertex = GCodeProcessorResult::MoveVertex;

constexpr double Inf = std::numeric_limits<double>::infinity();
constexpr double UniformRadius = 3.;
constexpr double UprightRadius = 1.5;

bool adaptive_top(const PrintObject &object, int patch)
{
    if (patch < 0)
        return false;
    const ConformalPatch &p = object.conformal().patches[size_t(patch)];
    return p.kind == PatchKind::Top && p.region_id >= 0 &&
           object.printing_region(size_t(p.region_id)).config().nonplanar_adaptive_bands.value;
}
constexpr double Extra = 0.05;
constexpr double CellSlack = 0.25;

bool physical(const MoveVertex &m)
{
    return !m.timing_only && m.type != EMoveType::Seam && m.type != EMoveType::Color_change && m.type != EMoveType::Tool_change &&
           m.type != EMoveType::Pause_Print && m.type != EMoveType::Custom_GCode;
}

Vec3d tip_of(const GCodeProcessorResult &result, size_t i)
{
    return result.has_joints() ? result.joints[i].xyz : Vec3d(result.moves[i].position.cast<double>());
}

struct Box { std::string name; Vec3d min, max; bool inf_x, inf_y, nozzle; };
std::vector<Box> verifier_boxes(const HeadEnvelope &head, bool allowance)
{
    std::vector<Box> out;
    for (const HeadBox &b : head.boxes) {
        const Vec2d radius = b.min.head<2>().cwiseAbs().cwiseMax(b.max.head<2>().cwiseAbs());
        const bool  nozzle = b.nozzle || (!b.infinite_x && !b.infinite_y && b.min.z() == 0. && b.min.head<2>().maxCoeff() <= 0. &&
                                         b.max.head<2>().minCoeff() >= 0. && radius.norm() <= head.tip_allowance.radius);
        Box box { b.name, b.min, b.max, b.infinite_x, b.infinite_y, nozzle };
        if (!nozzle) {
            box.min.head<2>().array() -= head.lateral_margin;
            box.max.head<2>().array() += head.lateral_margin;
            box.min.z() -= head.margin;
        } else if (allowance && !b.infinite_x && !b.infinite_y && radius.norm() <= head.tip_allowance.radius)
            box.min.z() = std::max({ box.min.z(), head.tip_allowance.height, 0.05 });
        if (box.min.z() <= box.max.z())
            out.emplace_back(std::move(box));
    }
    return out;
}

struct Segment { size_t move; size_t run; Vec3d a, b; double half_width; };
class SegmentGrid
{
public:
    SegmentGrid(const GCodeProcessorResult &result, const std::vector<BoundingBoxf> *reach, const std::vector<size_t> &blamed)
    {
        std::unordered_set<int64_t> cells;
        if (reach != nullptr)
            for (const BoundingBoxf &r : *reach)
                visit(r, [&](int64_t key) { cells.insert(key); });
        std::unordered_set<size_t> blamed_moves(blamed.begin(), blamed.end());
        size_t last = 0, run = 0;
        for (size_t i = 1; i < result.moves.size(); ++i) {
            const MoveVertex &m = result.moves[i];
            if (!physical(m))
                continue;
            const Vec3d a = tip_of(result, last), b = tip_of(result, i);
            last = i;
            if (m.type != EMoveType::Extrude) {
                ++run;
                continue;
            }
            if (blamed_moves.count(i))
                m_run_of[i] = run;
            const Segment segment { i, run, a, b, 0.5 * m.width };
            bool in_reach = reach == nullptr;
            if (!in_reach)
                visit(bounds(segment), [&](int64_t key) { in_reach = in_reach || cells.count(key) > 0; });
            if (!in_reach)
                continue;
            const size_t id = m_segments.size();
            m_segments.push_back(segment);
            visit(bounds(segment), [&](int64_t key) { m_cells[key].push_back(id); });
        }
    }
    size_t run_of(size_t move) const { auto it = m_run_of.find(move); return it == m_run_of.end() ? size_t(-1) : it->second; }
    const std::vector<Segment> &segments() const { return m_segments; }
    template<class Fn> void query(const BoundingBoxf &rect, Fn fn) const
    {
        if (!rect.min.allFinite() || !rect.max.allFinite()) {
            for (const Segment &s : m_segments) fn(s);
            return;
        }
        std::vector<size_t> ids;
        visit(rect, [&](int64_t key) {
            if (auto it = m_cells.find(key); it != m_cells.end())
                ids.insert(ids.end(), it->second.begin(), it->second.end());
        });
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (size_t id : ids)
            if (bounds(m_segments[id]).overlap(rect))
                fn(m_segments[id]);
    }

private:
    static BoundingBoxf bounds(const Segment &s)
    {
        const Vec2d r = Vec2d::Constant(s.half_width);
        return BoundingBoxf(s.a.head<2>().cwiseMin(s.b.head<2>()) - r, s.a.head<2>().cwiseMax(s.b.head<2>()) + r);
    }
    template<class Fn> static void visit(const BoundingBoxf &r, Fn fn)
    {
        const int x0 = int(std::floor(r.min.x() / 2.)), x1 = int(std::floor(r.max.x() / 2.));
        const int y0 = int(std::floor(r.min.y() / 2.)), y1 = int(std::floor(r.max.y() / 2.));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) fn((int64_t(x) << 32) ^ int64_t(uint32_t(y)));
    }
    std::vector<Segment>                              m_segments;
    std::unordered_map<int64_t, std::vector<size_t>> m_cells;
    std::unordered_map<size_t, size_t>               m_run_of;
};

struct Blame
{
    const Segment *culprit = nullptr;
    double         depth   = -Inf;
    double         floor   = 0.;
    double         box_lift = 0.;
    BoundingBoxf   rect;
    Vec3d          tip { Vec3d::Zero() };
};

Blame blame(const GCodeProcessorResult &result, const SegmentGrid &grid, size_t move_id, size_t prev_id,
            const std::vector<Box> &boxes, const std::string &component)
{
    Blame best;
    const MoveVertex &m  = result.moves[move_id];
    const bool extruding = m.type == EMoveType::Extrude;
    const size_t run     = grid.run_of(move_id);
    const Vec3d a = tip_of(result, prev_id), b = tip_of(result, move_id);
    const int count = std::max(1, int(std::ceil((b - a).norm() / 0.25)));
    for (int j = 0; j < count; ++j) {
        const Vec3d p0 = a + double(j) / count * (b - a), p1 = a + double(j + 1) / count * (b - a);
        for (const Box &box : boxes) {
            if (!component.empty() && box.name != component)
                continue;
            BoundingBoxf rect(p0.head<2>().cwiseMin(p1.head<2>()) + box.min.head<2>() - Vec2d::Constant(CellSlack),
                              p0.head<2>().cwiseMax(p1.head<2>()) + box.max.head<2>() + Vec2d::Constant(CellSlack));
            if (box.inf_x) rect.min.x() = -Inf, rect.max.x() = Inf;
            if (box.inf_y) rect.min.y() = -Inf, rect.max.y() = Inf;
            const double floor = std::min(p0.z(), p1.z()) + box.min.z();
            grid.query(rect, [&](const Segment &s) {
                if (s.move >= move_id || (box.nozzle && extruding && s.run == run))
                    return;
                const double depth = std::max(s.a.z(), s.b.z()) - floor;
                if (depth > best.depth || (depth == best.depth && best.culprit && s.move > best.culprit->move))
                    best = Blame { &s, depth, floor, box.min.z(), rect, p1 };
            });
        }
    }
    return best;
}

struct Site
{
    PrintObject *object   = nullptr;
    int          patch    = -1;
    int          shell    = -1;
    Vec2d        xy { Vec2d::Zero() };
};

class Locator
{
public:
    explicit Locator(Print &print) : m_print(print)
    {
        m_offset = Vec2d(print.get_plate_origin().head<2>()) + print.get_imex_slice_offset();
        m_z_offset = print.config().z_offset.value;
    }
    Vec2d to_object(const Vec3d &tip, const PrintObject &object, size_t instance, unsigned extruder) const
    {
        const Vec2d extruder_offset = extruder < m_print.config().extruder_offset.values.size() ?
                                          m_print.config().extruder_offset.values[extruder] : Vec2d::Zero();
        return tip.head<2>() + m_offset + extruder_offset - unscale(object.instances()[instance].shift);
    }
    double to_object_z(double z) const { return z - m_z_offset; }
    Vec3d from_preview(const MoveVertex &m) const
    {
        const Vec2d extruder_offset = m.extruder_id < m_print.config().extruder_offset.values.size() ?
                                          m_print.config().extruder_offset.values[m.extruder_id] : Vec2d::Zero();
        const Vec2d xy = m.position.head<2>().cast<double>() - Vec2d(m_print.get_plate_origin().head<2>()) - extruder_offset;
        return Vec3d(xy.x(), xy.y(), double(m.position.z()) + m_z_offset);
    }
    double to_gcode_z(double z) const { return z + m_z_offset; }
    PrintObject *object_at(const Vec3d &tip, unsigned extruder) const
    {
        for (PrintObject *object : m_print.objects())
            if (!object->conformal().patches.empty())
                for (size_t instance = 0; instance < object->instances().size(); ++instance)
                    if (unscaled(object->bounding_box()).contains(to_object(tip, *object, instance, extruder)))
                        return object;
        return nullptr;
    }
    Site locate(const GCodeProcessorResult &result, size_t move_id, const Vec3d &tip) const
    {
        Site best;
        const MoveVertex &m = result.moves[move_id];
        const int shell = result.state(m).nonplanar_patch_shell;
        if (shell == 0xFF)
            return best;
        double best_dz = 0.5;
        const double z = to_object_z(tip.z());
        for (PrintObject *object : m_print.objects())
            for (size_t instance = 0; instance < object->instances().size(); ++instance) {
                const Vec2d q = to_object(tip, *object, instance, m.extruder_id);
                for (const ConformalPatch &patch : object->conformal().patches) {
                    const float s = patch.surface.sample(q);
                    if (std::isnan(s) || shell > patch.shells)
                        continue;
                    const double expected = patch.kind == PatchKind::Top ? s - std::max(0, patch.shells - 1 - shell) * patch.shell_height :
                                                                           s + (shell + 1) * patch.shell_height;
                    if (const double dz = std::abs(z - expected); dz < best_dz)
                        best_dz = dz, best = Site { object, patch.id, shell, q };
                }
            }
        return best;
    }

private:
    Print &m_print;
    Vec2d  m_offset { Vec2d::Zero() };
    double m_z_offset = 0.;
};

}

ClearanceRepair::ClearanceRepair(Print &print) : m_print(print)
{
    m_max_passes = print.config().nonplanar_repair_iterations.value;
    print.m_clearance_repair = this;
}

ClearanceRepair::~ClearanceRepair() { m_print.m_clearance_repair = nullptr; }

void ClearanceRepair::check_export(Print &print, GCodeProcessorResult &result)
{
    trim_free_memory();
    ClearanceRepair *self = print.m_clearance_repair;
    const auto       mode = print.config().nonplanar_collision_check.value;
    if (print.config().nonplanar_kinematics.value != RotaryLayout::None) {
        if (self != nullptr && result.nonplanar && effective_collision_check(print) != NonplanarCollisionCheck::Off &&
            self->m_pass < self->m_max_passes) {
            ClearanceParams params;
            params.head = resolve_head_envelope(print.config());
            ClearanceReport report = verify_clearance(result, params, *make_tool_kinematics(print.config()),
                                                      [&print] { print.throw_if_canceled(); });
            if (self->m_pass == 0)
                self->m_first_hits = report.hit_moves;
            self->m_planned.clear();
            if (report.hit_moves == 0 || self->plan_upright(result, report)) {
                if (report.hit_moves == 0 && self->m_pass > 0)
                    print.active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL, self->summary(0));
                result.clearance_report = std::move(report);
                result.clearance        = result.clearance_report.per_move;
                return;
            }
        }
        if (self != nullptr)
            self->m_planned.clear();
        check_export_clearance(print, result);
        return;
    }
    if (self == nullptr || !result.nonplanar || mode == NonplanarCollisionCheck::Off) {
        check_export_clearance(print, result);
        return;
    }
    ClearanceParams params;
    params.head = resolve_head_envelope(print.config());
    ClearanceReport report = verify_clearance(result, params, ThreeAxisKinematics(), [&print] { print.throw_if_canceled(); });
    const size_t hits = report.hit_moves;
    self->m_planned.clear();
    const bool stalled = self->m_pass > 0 && hits == self->m_last_hits && report.min_clearance == self->m_last_min;
    self->m_last_hits  = hits;
    self->m_last_min   = report.min_clearance;
    if (hits > 0 && !stalled && self->plan(result, report, params.head) && self->m_pass < self->m_max_passes) {
        result.clearance_report = std::move(report);
        result.clearance        = result.clearance_report.per_move;
        return;
    }
    self->m_planned.clear();
    size_t conformal_hits = 0;
    for (size_t i = 0; i < result.moves.size(); ++i)
        if (report.per_move[i] < 0.f && result.state(result.moves[i]).nonplanar_patch_shell != 0xFF && result.moves[i].type == EMoveType::Extrude)
            ++conformal_hits;
    result.clearance_report = std::move(report);
    result.clearance        = result.clearance_report.per_move;
    if (self->m_pass == 0 && hits == 0)
        return;
    std::string message = self->summary(hits);
    if (conformal_hits > 0) {
        const auto worst = std::find_if(result.clearance_report.worst.begin(), result.clearance_report.worst.end(), [&result](const ClearanceHit &hit) {
            return result.state(result.moves[hit.move_id]).nonplanar_patch_shell != 0xFF && result.moves[hit.move_id].type == EMoveType::Extrude;
        });
        message += " " + (worst == result.clearance_report.worst.end() ?
                              Slic3r::format(_u8L("Curved moves that still hit printed material: %1%."), conformal_hits) :
                              Slic3r::format(_u8L("Curved moves that still hit printed material: %1% (worst %2$.3f mm, %3%, G-code line %4%)."),
                                             conformal_hits, worst->penetration, head_box_label(worst->component), worst->gcode_line));
    }
    print.active_step_add_warning(conformal_hits > 0 ? PrintStateBase::WarningLevel::CRITICAL : PrintStateBase::WarningLevel::NON_CRITICAL, message);
    if (mode == NonplanarCollisionCheck::Block && conformal_hits > 0)
        throw RuntimeError(message + " " + _u8L("Export blocked by the head clearance check. Review the path or set the check to Warn to export it."));
}

bool ClearanceRepair::plan(const GCodeProcessorResult &result, const ClearanceReport &report, const HeadEnvelope &head)
{
    const std::vector<Box> contact_boxes = verifier_boxes(head, true), travel_boxes = verifier_boxes(head, false);
    auto previous_physical = [&](size_t i) { while (i > 0 && !physical(result.moves[--i])) {} return i; };
    auto next_extrusion    = [&](size_t i) {
        while (++i < result.moves.size())
            if (!result.moves[i].timing_only && result.moves[i].type == EMoveType::Extrude) return i;
        return size_t(0);
    };
    auto previous_extrusion = [&](size_t i) {
        while (i > 0)
            if (const auto &move = result.moves[--i]; !move.timing_only && move.type == EMoveType::Extrude) return i;
        return size_t(0);
    };
    std::vector<BoundingBoxf> reach;
    std::vector<size_t>       blamed;
    bool                      unbounded = false;
    for (const ClearanceHit &hit : report.worst)
        for (const size_t id : { hit.move_id, next_extrusion(hit.move_id) }) {
            if (id == 0)
                continue;
            blamed.push_back(id);
            const Vec3d  a = tip_of(result, previous_physical(id)), b = tip_of(result, id);
            BoundingBoxf r(a.head<2>().cwiseMin(b.head<2>()), a.head<2>().cwiseMax(b.head<2>()));
            Vec2d        lo = Vec2d::Zero(), hi = Vec2d::Zero();
            for (const std::vector<Box> *list : { &contact_boxes, &travel_boxes })
                for (const Box &box : *list)
                    if (hit.component.empty() || box.name == hit.component) {
                        unbounded = unbounded || box.inf_x || box.inf_y;
                        lo = lo.cwiseMin(box.min.head<2>());
                        hi = hi.cwiseMax(box.max.head<2>());
                    }
            r.min += lo - Vec2d::Constant(CellSlack);
            r.max += hi + Vec2d::Constant(CellSlack);
            reach.push_back(r);
        }
    const SegmentGrid      grid(result, unbounded ? nullptr : &reach, blamed);
    if (m_pass == 0)
        m_first_hits = report.hit_moves;
    const Locator          locator(m_print);
    auto hints_of = [this](PrintObject *object) -> RepairHints & {
        for (Planned &p : m_planned)
            if (p.object == object)
                return p.hints;
        m_planned.push_back({ object, {} });
        return m_planned.back().hints;
    };

    size_t caps = 0, bands = 0, drops = 0, lifts = 0, unrepaired = 0;
    std::map<std::string, size_t> unrepaired_by_cause;
    for (const ClearanceHit &hit : report.worst) {
        size_t move_id = hit.move_id;
        const MoveVertex *m = &result.moves[move_id];
        const bool extruding = m->type == EMoveType::Extrude;
        const size_t prev = previous_physical(move_id);
        const Vec3d  a = tip_of(result, prev), b = tip_of(result, move_id);
        const double penetration = hit.penetration;

        if (!extruding) {
            const size_t next = next_extrusion(move_id);
            const bool vertical = (b.head<2>() - a.head<2>()).norm() < 1e-3;
            if (vertical && next != 0 && result.state(result.moves[next]).nonplanar_patch_shell != 0xFF) {
                move_id = next;
                m = &result.moves[move_id];
            } else {
                const size_t last = previous_extrusion(move_id);
                Site site = locator.locate(result, last, tip_of(result, last));
                if (site.object == nullptr && next != 0)
                    site = locator.locate(result, next, tip_of(result, next));
                if (site.object == nullptr)
                    site.object = locator.object_at(b, m->extruder_id);
                if (site.object != nullptr) {
                    RepairHints &h = hints_of(site.object);
                    if (adaptive_top(*site.object, site.patch))
                        h.uniform_beads.push_back({ site.xy, UniformRadius });
                    const double base = site.object->nonplanar_repair() ? site.object->nonplanar_repair()->travel_lift : 0.;
                    h.travel_lift = std::max(h.travel_lift, base + penetration + Extra);
                    const auto &layers = site.object->layers();
                    const double z     = locator.to_object_z(b.z());
                    const auto   it    = std::lower_bound(layers.begin(), layers.end(), z - EPSILON,
                                                          [](const Layer *layer, double v) { return layer->print_z < v; });
                    if (it != layers.end())
                        h.travel_layers.push_back((*it)->id());
                    ++lifts;
                } else {
                    ++unrepaired, ++unrepaired_by_cause["planar travel"];
                }
                continue;
            }
        }

        const Site site = locator.locate(result, move_id, tip_of(result, move_id));
        if (site.object != nullptr) {
            const ConformalPatch &patch = site.object->conformal().patches[size_t(site.patch)];
            if (site.shell == patch.shells) {
                hints_of(site.object).ironing_drops.push_back({ site.xy, std::max(1., 2. * double(m->width)) });
                ++drops;
                continue;
            }
        }

        const Blame what = blame(result, grid, move_id, previous_physical(move_id), m->type == EMoveType::Extrude || site.object ? contact_boxes : travel_boxes,
                                 hit.component);
        if (what.culprit == nullptr) {
            ++unrepaired, ++unrepaired_by_cause["no material found"];
            continue;
        }
        const Site        source  = locator.locate(result, what.culprit->move, what.culprit->b);
        if (source.object != nullptr && source.object->conformal().patches[size_t(source.patch)].kind == PatchKind::Top) {
            const ConformalPatch &patch = source.object->conformal().patches[size_t(source.patch)];
            const double drop = std::max(0, patch.shells - 1 - std::min(source.shell, patch.shells - 1)) * patch.shell_height;
            const double cap  = locator.to_object_z(what.floor - 2. * Extra) + drop;
            RepairHints &h    = hints_of(source.object);
            if (adaptive_top(*source.object, source.patch))
                h.uniform_beads.push_back({ source.xy, UniformRadius });
            if (site.object == source.object && adaptive_top(*site.object, site.patch))
                h.uniform_beads.push_back({ site.xy, UniformRadius });
            const Vec3d sa = what.culprit->a, sb = what.culprit->b;
            const int   n  = std::max(1, int(std::ceil((sb - sa).head<2>().norm() / 0.2)));
            const double radius = what.culprit->half_width + patch.surface.pitch();
            Vec3d nearest = sb;
            bool  inside  = false;
            for (int k = 0; k <= n; ++k) {
                const Vec3d p = sa + double(k) / n * (sb - sa);
                if (what.rect.contains(Vec2d(p.head<2>())))
                    h.surface_caps.push_back({ source.xy + (p - sb).head<2>(), radius, cap }), inside = true;
                else if ((p.head<2>() - what.rect.center()).norm() < (nearest.head<2>() - what.rect.center()).norm())
                    nearest = p;
            }
            if (!inside)
                h.surface_caps.push_back({ source.xy + (nearest - sb).head<2>(), radius, cap });
            ++caps;
        } else if (site.object != nullptr && site.object->conformal().patches[size_t(site.patch)].kind == PatchKind::Top) {
            double layer_height = 0.;
            for (const Layer *layer : site.object->layers())
                layer_height = std::max(layer_height, layer->height);
            const double height = what.box_lift - Extra - layer_height;
            if (height < layer_height) {
                ++unrepaired, ++unrepaired_by_cause["material beside the nozzle"];
                continue;
            }
            hints_of(site.object).order_bands.push_back({ site.xy, height });
            if (adaptive_top(*site.object, site.patch))
                hints_of(site.object).uniform_beads.push_back({ site.xy, UniformRadius });
            ++bands;
        } else {
            ++unrepaired, ++unrepaired_by_cause[extruding ? "planar extrusion" : "travel"];
        }
    }

    std::ostringstream action;
    action << "pass " << m_pass + 1 << ": " << report.hit_moves << " hits";
    if (caps) action << ", " << caps << " lowered";
    if (bands) action << ", " << bands << " re-banded";
    if (lifts) action << ", " << lifts << " travel lifted";
    if (drops) action << ", " << drops << " ironing dropped";
    for (const auto &[cause, n] : unrepaired_by_cause) action << ", " << n << " unrepaired (" << cause << ")";
    if (report.machine_hit_moves) action << ", " << report.machine_hit_moves << " in start/end G-code (not repairable)";
    m_actions.emplace_back(action.str());
    BOOST_LOG_TRIVIAL(info) << "Non-planar repair " << m_actions.back();
    return caps + bands + lifts + drops > 0;
}

bool ClearanceRepair::plan_upright(const GCodeProcessorResult &result, const ClearanceReport &report)
{
    const Locator locator(m_print);
    const double  radius = UprightRadius * double(m_pass + 1);
    size_t        discs  = 0;
    for (size_t i = 1; i < result.moves.size() && i < report.per_move.size(); ++i) {
        const MoveVertex &m = result.moves[i];
        if (report.per_move[i] >= 0.f || result.state(m).nonplanar_patch_shell == 0xFF)
            continue;
        const Vec3d tip  = locator.from_preview(m);
        const Site  site = locator.locate(result, i, tip);
        if (site.object == nullptr)
            continue;
        std::vector<RepairHints::Disc> *upright = nullptr;
        for (Planned &p : m_planned)
            if (p.object == site.object)
                upright = &p.hints.upright;
        if (upright == nullptr) {
            m_planned.push_back({ site.object, {} });
            upright = &m_planned.back().hints.upright;
        }
        size_t previous = i;
        while (previous > 0 && !physical(result.moves[--previous])) {}
        const Vec2d from = site.xy + (result.moves[previous].position - m.position).head<2>().cast<double>();
        for (const Vec2d &p : { from, site.xy })
            if (std::none_of(upright->begin(), upright->end(), [&p, radius](const RepairHints::Disc &d) { return (d.p - p).norm() < 0.5 * radius; }))
                upright->push_back({ p, radius }), ++discs;
    }
    if (discs > 0)
        m_actions.push_back(std::to_string(discs) + " spots printed upright");
    return discs > 0;
}

bool ClearanceRepair::apply()
{
    if (m_planned.empty())
        return false;
    for (Planned &p : m_planned) {
        PrintObject &object = *p.object;
        if (!object.m_nonplanar_repair)
            object.m_nonplanar_repair = std::make_shared<RepairHints>();
        RepairHints &h = *object.m_nonplanar_repair;
        const bool regenerate = !p.hints.surface_caps.empty() || !p.hints.ironing_drops.empty() || !p.hints.order_bands.empty() ||
                                !p.hints.uniform_beads.empty();
        h.surface_caps.insert(h.surface_caps.end(), p.hints.surface_caps.begin(), p.hints.surface_caps.end());
        h.ironing_drops.insert(h.ironing_drops.end(), p.hints.ironing_drops.begin(), p.hints.ironing_drops.end());
        h.order_bands.insert(h.order_bands.end(), p.hints.order_bands.begin(), p.hints.order_bands.end());
        h.uniform_beads.insert(h.uniform_beads.end(), p.hints.uniform_beads.begin(), p.hints.uniform_beads.end());
        h.upright.insert(h.upright.end(), p.hints.upright.begin(), p.hints.upright.end());
        h.travel_lift = std::max(h.travel_lift, p.hints.travel_lift);
        h.travel_layers.insert(h.travel_layers.end(), p.hints.travel_layers.begin(), p.hints.travel_layers.end());
        std::sort(h.travel_layers.begin(), h.travel_layers.end());
        h.travel_layers.erase(std::unique(h.travel_layers.begin(), h.travel_layers.end()), h.travel_layers.end());
        if (regenerate)
            object.invalidate_step(posPerimeters);
        else
            m_print.invalidate_step(psGCodeExport);
    }
    m_planned.clear();
    ++m_pass;
    return true;
}

std::string ClearanceRepair::summary(size_t hits_now) const
{
    std::ostringstream detail;
    detail << std::setprecision(2);
    for (const PrintObject *object : m_print.objects()) {
        const RepairHints *hints = object->nonplanar_repair();
        if (hints == nullptr || m_pass == 0)
            continue;
        for (const PatchDiagnostics &diag : object->conformal().diagnostics)
            if (diag.repaired_nodes > 0)
                detail << "; " << diag.repaired_nodes << " nodes lowered by <= " << diag.repair_lowering << " mm on patch " << diag.patch_id;
        if (!hints->order_bands.empty()) {
            double height = Inf;
            for (const RepairHints::OrderBand &b : hints->order_bands) height = std::min(height, b.height);
            detail << "; patches of " << object->model_object()->name << " printed in bands <= " << height << " mm";
        }
        if (hints->travel_lift > 0.)
            detail << "; travel lifted " << hints->travel_lift << " mm";
        if (!hints->ironing_drops.empty())
            detail << "; " << hints->ironing_drops.size() << " ironing spots dropped";
        if (!hints->uniform_beads.empty())
            detail << "; " << hints->uniform_beads.size() << " spots at the uniform shell height";
    }
    for (const std::string &action : m_actions)
        detail << "; " << action;
    BOOST_LOG_TRIVIAL(info) << "Non-planar repair: " << m_first_hits << " hits -> " << hits_now << " after " << m_pass << " passes" << detail.str();
    return m_pass == 0 ? Slic3r::format(_u8L("Head clearance check: %1% moves would hit printed material, none repaired."), hits_now) :
                         Slic3r::format(_u8L("Clearance repair passes: %1%. Moves that would hit printed material: %2% before, %3% after."),
                                        m_pass, m_first_hits, hits_now);
}

}
