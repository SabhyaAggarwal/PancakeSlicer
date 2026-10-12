#include "ClearanceVerifier.hpp"
#include "GCodeProcessor.hpp"
#include "ToolKinematics.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintBase.hpp"
#include <sstream>
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace Slic3r {
namespace {
constexpr double Inf = std::numeric_limits<double>::infinity();
constexpr double ContactTolerance = 1e-5;

bool is_nozzle(const Nonplanar::HeadBox &box, const Nonplanar::HeadEnvelope &head)
{
    const Vec2d radius = box.min.head<2>().cwiseAbs().cwiseMax(box.max.head<2>().cwiseAbs());
    return box.nozzle || (!box.infinite_x && !box.infinite_y && box.min.z() == 0. &&
                          box.min.head<2>().maxCoeff() <= 0. && box.max.head<2>().minCoeff() >= 0. &&
                          radius.norm() <= head.tip_allowance.radius);
}

Vec3d move_tip(const GCodeProcessorResult::MoveVertex &move, bool rotary)
{
    return !rotary && move.has_joints ? move.joints.xyz : Vec3d(move.position.cast<double>());
}

Nonplanar::HeadBox prepared_box(Nonplanar::HeadBox box, const Nonplanar::HeadEnvelope &head)
{
    box.nozzle = is_nozzle(box, head);
    if (!box.nozzle) {
        box.min.head<2>().array() -= head.lateral_margin;
        box.max.head<2>().array() += head.lateral_margin;
        box.min.z() -= head.margin;
        box.max.z() += head.margin;
    } else if (!box.infinite_x && !box.infinite_y) {
        const Vec2d radius = box.min.head<2>().cwiseAbs().cwiseMax(box.max.head<2>().cwiseAbs());
        if (radius.norm() <= head.tip_allowance.radius)
            box.min.z() = std::max(box.min.z(), head.tip_allowance.height);
    }
    return box;
}

BoundingBoxf swept_rect(const Vec3d &a, const Vec3d &b, const Nonplanar::HeadBox &box)
{
    Vec2d lo = a.head<2>().cwiseMin(b.head<2>()) + box.min.head<2>();
    Vec2d hi = a.head<2>().cwiseMax(b.head<2>()) + box.max.head<2>();
    if (box.infinite_x) { lo.x() = -Inf; hi.x() = Inf; }
    if (box.infinite_y) { lo.y() = -Inf; hi.y() = Inf; }
    return BoundingBoxf(lo, hi);
}

struct Bead { std::array<Vec3d, 8> corners; Vec3d lo, hi; size_t run; };
using Cell = std::array<int, 3>;
class BeadGrid
{
public:
    std::vector<Bead> beads;
    std::map<Cell, std::vector<size_t>> grid;
    double material_radius = 0.;
    void deposit(const ToolPose &a, const ToolPose &b, double width, double height, size_t run)
    {
        Bead bead;
        bead.run = run;
        bead.lo = Vec3d::Constant(Inf);
        bead.hi = Vec3d::Constant(-Inf);
        for (int end = 0; end < 2; ++end) {
            const ToolPose &p = end == 0 ? a : b;
            Vec3d side = p.axis.cross(b.tip - a.tip);
            if (side.squaredNorm() < 1e-20) side = p.axis.unitOrthogonal();
            side.normalize();
            for (int sign = 0; sign < 2; ++sign)
                for (int depth = 0; depth < 2; ++depth) {
                    Vec3d &v = bead.corners[4 * end + 2 * sign + depth];
                    v = p.tip + (sign == 0 ? -0.5 : 0.5) * width * side - depth * height * p.axis;
                    bead.lo = bead.lo.cwiseMin(v);
                    bead.hi = bead.hi.cwiseMax(v);
                    material_radius = std::max(material_radius, v.norm());
                }
        }
        const size_t id = beads.size();
        beads.push_back(bead);
        visit(bead.lo, bead.hi, [&](const Cell &cell) { grid[cell].push_back(id); });
    }
    double clearance(const ToolPose &pose, const Matrix3d &frame, const Nonplanar::HeadEnvelope &head,
                     double inflation, size_t run, bool extruding, std::string &component) const
    {
        double best = Inf;
        std::vector<size_t> candidates;
        for (const auto &raw : head.boxes) {
            auto box = prepared_box(raw, head);
            if (box.min.z() > box.max.z()) continue;
            Vec3d lo = box.min - Vec3d::Constant(inflation), hi = box.max + Vec3d::Constant(inflation);
            Vec3d work_lo = Vec3d::Constant(Inf), work_hi = Vec3d::Constant(-Inf);
            if (box.infinite_x || box.infinite_y) {
                candidates.resize(beads.size());
                for (size_t i = 0; i < beads.size(); ++i) candidates[i] = i;
            } else {
                for (int k = 0; k < 8; ++k) {
                    const Vec3d v = pose.tip + frame.transpose() * Vec3d(k & 1 ? hi.x() : lo.x(), k & 2 ? hi.y() : lo.y(), k & 4 ? hi.z() : lo.z());
                    work_lo = work_lo.cwiseMin(v); work_hi = work_hi.cwiseMax(v);
                }
                candidates.clear();
                visit(work_lo, work_hi, [&](const Cell &cell) {
                    auto it = grid.find(cell);
                    if (it != grid.end()) candidates.insert(candidates.end(), it->second.begin(), it->second.end());
                });
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            }
            for (size_t id : candidates) {
                const Bead &bead = beads[id];
                if (box.nozzle && extruding && bead.run == run) continue;
                Vec3d bl = Vec3d::Constant(Inf), bh = Vec3d::Constant(-Inf);
                for (const Vec3d &v : bead.corners) {
                    const Vec3d q = frame * (v - pose.tip);
                    bl = bl.cwiseMin(q); bh = bh.cwiseMax(q);
                }
                double gap = -Inf;
                for (int axis = 0; axis < 3; ++axis) {
                    if ((axis == 0 && box.infinite_x) || (axis == 1 && box.infinite_y)) continue;
                    gap = std::max(gap, std::max(lo[axis] - bh[axis], bl[axis] - hi[axis]));
                }
                if (gap < best) { best = gap; component = box.name; }
            }
        }
        return best;
    }
private:
    template<class Fn> static void visit(const Vec3d &lo, const Vec3d &hi, Fn fn)
    {
        Cell a, b;
        for (int k = 0; k < 3; ++k) { a[k] = int(std::floor(lo[k] / 4.)); b[k] = int(std::floor(hi[k] / 4.)); }
        for (int z = a[2]; z <= b[2]; ++z)
            for (int y = a[1]; y <= b[1]; ++y)
                for (int x = a[0]; x <= b[0]; ++x) fn(Cell{ x, y, z });
    }
};

JointPose interpolate(const JointPose &a, const JointPose &b, double t)
{
    JointPose q;
    q.xyz = a.xyz + t * (b.xyz - a.xyz);
    for (int k = 0; k < 2; ++k) q.rot[k] = a.rot[k] + t * (b.rot[k] - a.rot[k]);
    return q;
}
}

MaterialColumns::MaterialColumns(const BoundingBoxf &bbox, double pitch) : m_bbox(bbox), m_pitch(pitch)
{
    if (!std::isfinite(pitch) || pitch <= 0. || !bbox.defined || !bbox.min.allFinite() || !bbox.max.allFinite())
        throw RuntimeError("Invalid clearance grid bounds or cell size.");
    const Vec2d shape = (bbox.max - bbox.min) / pitch;
    if (shape.minCoeff() < 0. || shape.maxCoeff() > 100000. || shape.x() * shape.y() > 100000000.)
        throw RuntimeError("Clearance grid is too large. Check the G-code coordinates or increase the clearance cell size.");
    m_nx = std::max(1, int(std::ceil(shape.x())) + 1);
    m_ny = std::max(1, int(std::ceil(shape.y())) + 1);
    m_tile = std::max(1, int(std::floor(4. / pitch)));
    m_tx = (m_nx + m_tile - 1) / m_tile;
    m_top.assign(size_t(m_nx) * m_ny, -Inf);
    m_tiles.assign(size_t(m_tx) * ((m_ny + m_tile - 1) / m_tile), -Inf);
}

void MaterialColumns::cells(const BoundingBoxf &rect, int &x0, int &y0, int &x1, int &y1) const
{
    auto bound = [&](double value, double origin, int count, bool upper) {
        const double cell = std::floor((value - origin) / m_pitch) + (upper ? 1. : 0.);
        return int(std::clamp(cell, 0., double(count)));
    };
    x0 = bound(rect.min.x(), m_bbox.min.x(), m_nx, false); y0 = bound(rect.min.y(), m_bbox.min.y(), m_ny, false);
    x1 = bound(rect.max.x(), m_bbox.min.x(), m_nx, true); y1 = bound(rect.max.y(), m_bbox.min.y(), m_ny, true);
}

void MaterialColumns::deposit(const Vec3d &a, const Vec3d &b, double width)
{
    const Vec2d r = Vec2d::Constant(0.5 * std::max(0., width));
    int x0, y0, x1, y1;
    cells(BoundingBoxf(a.head<2>().cwiseMin(b.head<2>()) - r, a.head<2>().cwiseMax(b.head<2>()) + r), x0, y0, x1, y1);
    const double z = std::max(a.z(), b.z());
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            double &top = m_top[size_t(y) * m_nx + x]; top = std::max(top, z);
            double &tile = m_tiles[size_t(y / m_tile) * m_tx + x / m_tile]; tile = std::max(tile, z);
        }
    if (x0 < x1 && y0 < y1) m_ceiling = std::max(m_ceiling, z);
}

double MaterialColumns::top_in_rect(const BoundingBoxf &rect) const
{
    int x0, y0, x1, y1;
    cells(rect, x0, y0, x1, y1);
    if (x0 >= x1 || y0 >= y1) return -Inf;
    double best = -Inf;
    for (int ty = y0 / m_tile; ty <= (y1 - 1) / m_tile; ++ty)
        for (int tx = x0 / m_tile; tx <= (x1 - 1) / m_tile; ++tx) {
            const double tile = m_tiles[size_t(ty) * m_tx + tx];
            if (tile <= best) continue;
            const int ax = std::max(x0, tx * m_tile), bx = std::min(x1, (tx + 1) * m_tile);
            const int ay = std::max(y0, ty * m_tile), by = std::min(y1, (ty + 1) * m_tile);
            if (ax == tx * m_tile && bx == std::min(m_nx, (tx + 1) * m_tile) &&
                ay == ty * m_tile && by == std::min(m_ny, (ty + 1) * m_tile)) { best = tile; continue; }
            for (int y = ay; y < by; ++y)
                for (int x = ax; x < bx; ++x) best = std::max(best, m_top[size_t(y) * m_nx + x]);
        }
    return best;
}

double MaterialColumns::clearance(const Vec3d &tip, const Nonplanar::HeadEnvelope &head, std::string *component) const
{ return swept_clearance(tip, tip, head, component); }

double MaterialColumns::swept_clearance(const Vec3d &a, const Vec3d &b, const Nonplanar::HeadEnvelope &head, std::string *component) const
{
    double best = Inf;
    if (component) component->clear();
    if (m_ceiling == -Inf) return best;
    for (const auto &raw : head.boxes) {
        const auto box = prepared_box(raw, head);
        if (box.min.z() > box.max.z()) continue;
        const double floor = std::min(a.z(), b.z()) + box.min.z();
        if (floor - m_ceiling >= best) continue;
        const double gap = floor - top_in_rect(swept_rect(a, b, box));
        if (gap < best) { best = gap; if (component) *component = box.name; }
    }
    return best;
}

double safe_travel_z(const MaterialColumns &columns, const Nonplanar::HeadEnvelope &head, const Vec2d &a, const Vec2d &b, double hop)
{
    auto travel_head = head;
    for (auto &box : travel_head.boxes) box.nozzle = is_nozzle(box, head);
    travel_head.tip_allowance = {};
    const double gap = columns.swept_clearance(Vec3d(a.x(), a.y(), 0.), Vec3d(b.x(), b.y(), 0.), travel_head);
    return -gap + std::max(0., hop);
}

ClearanceReport verify_clearance(const GCodeProcessorResult &result, const ClearanceParams &params,
                                 const ToolKinematics &kinematics, const std::function<void()> &throw_if_canceled)
{
    if (!std::isfinite(params.pitch) || params.pitch <= 0. || !std::isfinite(params.sweep) || params.sweep <= 0.)
        throw RuntimeError("Clearance cell and sweep sizes must be positive finite lengths.");
    ClearanceReport report;
    report.ran = true;
    report.per_move.assign(result.moves.size(), float(Inf));
    report.omissions = { "Bed and machine self-collision are not modelled.", "Support stability and thermal effects are not modelled.",
                         "Nozzle contact with the current uninterrupted extrusion run is excluded." };
    if (!params.head.calibrated) report.omissions.emplace_back("Head geometry has not been calibrated on this printer.");
    if (result.moves.empty()) return report;
    BoundingBoxf bounds;
    double max_width = 0.;
    const bool rotary = kinematics.rotary_count() != 0;
    for (const auto &move : result.moves) {
        if (!move.position.allFinite()) throw RuntimeError("Clearance verification found a non-finite G-code position.");
        bounds.merge(Vec2d(move_tip(move, rotary).head<2>()));
        max_width = std::max(max_width, double(move.width));
    }
    bounds.min.array() -= max_width + params.pitch; bounds.max.array() += max_width + params.pitch;
    MaterialColumns columns(bounds, params.pitch), previous(bounds, params.pitch);
    Nonplanar::HeadEnvelope body = params.head, nozzle = params.head;
    for (auto &box : body.boxes) box.nozzle = is_nozzle(box, params.head);
    nozzle = body;
    body.boxes.erase(std::remove_if(body.boxes.begin(), body.boxes.end(), [](const auto &b) { return b.nozzle; }), body.boxes.end());
    nozzle.boxes.erase(std::remove_if(nozzle.boxes.begin(), nozzle.boxes.end(), [](const auto &b) { return !b.nozzle; }), nozzle.boxes.end());
    struct Pending { Vec3d a, b; double width; };
    std::vector<Pending> pending;
    BeadGrid beads;
    size_t run = 0;
    size_t last_index = 0;
    double head_lever = 0.;
    bool infinite_head = false;
    for (const auto &raw : params.head.boxes) {
        const auto box = prepared_box(raw, params.head);
        head_lever = std::max(head_lever, box.min.cwiseAbs().cwiseMax(box.max.cwiseAbs()).norm());
        infinite_head |= box.infinite_x || box.infinite_y;
    }
    for (size_t i = 1; i < result.moves.size(); ++i) {
        if (throw_if_canceled) throw_if_canceled();
        const auto &move = result.moves[i];
        if (move.type == EMoveType::Seam || move.type == EMoveType::Color_change || move.type == EMoveType::Tool_change ||
            move.type == EMoveType::Pause_Print || move.type == EMoveType::Custom_GCode) continue;
        const auto &last = result.moves[last_index];
        last_index = i;
        const bool extruding = move.type == EMoveType::Extrude;
        if (!extruding && move.type != EMoveType::Retract && move.type != EMoveType::Unretract &&
            (move_tip(move, rotary) - move_tip(last, rotary)).squaredNorm() == 0. && move.joints.rot == last.joints.rot) continue;
        if (!extruding) {
            ++run;
            for (const auto &p : pending) previous.deposit(p.a, p.b, p.width);
            pending.clear();
        }
        const Vec3d a = move_tip(last, rotary), b = move_tip(move, rotary);
        const bool straight_lift = !extruding && a.head<2>() == b.head<2>() && b.z() >= a.z() && move.joints.rot == last.joints.rot;
        double displacement = (b - a).norm();
        if (rotary) {
            const double angle = (std::abs(move.joints.rot[0] - last.joints.rot[0]) + std::abs(move.joints.rot[1] - last.joints.rot[1])) * M_PI / 180.;
            double lever = std::max(a.norm(), b.norm()) + head_lever;
            if (infinite_head) lever += beads.material_radius;
            if (const auto *kin = dynamic_cast<const RotaryKinematics *>(&kinematics))
                lever += kin->geometry().pivot.norm() + kin->geometry().tool_mount.norm() + kin->geometry().tool_length + std::abs(kin->geometry().table_height);
            displacement = (move.joints.xyz - last.joints.xyz).norm() + angle * lever;
        }
        if (!std::isfinite(displacement) || displacement / params.sweep > 50000000.)
            throw RuntimeError("Clearance sweep is too large. Check the G-code coordinates and rotary angles.");
        const int count = std::max(1, int(std::ceil(displacement / params.sweep)));
        double best = Inf;
        ClearanceHit hit;
        hit.move_id = i; hit.gcode_line = move.gcode_id; hit.layer = move.layer_id;
        auto record = [&](double gap, const std::string &component, const Vec3d &tip) {
            if (gap < best) { best = gap; hit.component = component; hit.tip = tip; }
        };
        bool lands_on_extrusion = false;
        if (!extruding && a.head<2>() == b.head<2>() && b.z() < a.z() && move.joints.rot == last.joints.rot)
            for (size_t k = i + 1; k < result.moves.size(); ++k) {
                const EMoveType type = result.moves[k].type;
                if (type == EMoveType::Extrude || ((type == EMoveType::Travel || type == EMoveType::Wipe) &&
                    (move_tip(result.moves[k], rotary) != b || result.moves[k].joints.rot != move.joints.rot))) {
                    lands_on_extrusion = type == EMoveType::Extrude;
                    break;
                }
            }
        auto contact_head = nozzle;
        if (!extruding && !straight_lift && !lands_on_extrusion && move.nonplanar_patch_shell == 0xFF) contact_head.tip_allowance = {};
        else contact_head.tip_allowance.height = std::max(contact_head.tip_allowance.height, params.bead_eps);
        for (int j = 0; j < count; ++j) {
            if ((j & 1023) == 0 && throw_if_canceled) throw_if_canceled();
            const double t0 = double(j) / count, t1 = double(j + 1) / count;
            const Vec3d p0 = a + t0 * (b - a), p1 = a + t1 * (b - a);
            std::string component;
            if (rotary) {
                const JointPose q = interpolate(last.joints, move.joints, 0.5 * (t0 + t1));
                const ToolPose pose = kinematics.forward(q);
                record(beads.clearance(pose, kinematics.head_frame(q), params.head, displacement / (2. * count), run, extruding, component), component, pose.tip);
            } else {
                const double body_gap = columns.swept_clearance(p0, p1, body, &component);
                record(body_gap, component, p1);
                double nozzle_gap = (extruding ? previous : columns).swept_clearance(p0, p1, contact_head, &component);
                if (nozzle_gap < -ContactTolerance && nozzle_gap >= -params.pitch) {
                    auto swept_head = contact_head;
                    const Vec2d half = 0.5 * (p1.head<2>() - p0.head<2>()).cwiseAbs();
                    for (auto &box : swept_head.boxes) {
                        box = prepared_box(box, contact_head);
                        box.min.head<2>() -= half; box.max.head<2>() += half;
                        box.max.z() += std::abs(p1.z() - p0.z());
                    }
                    swept_head.tip_allowance = {};
                    const Vec2d middle = 0.5 * (p0.head<2>() + p1.head<2>());
                    const ToolPose pose { Vec3d(middle.x(), middle.y(), std::min(p0.z(), p1.z())), Vec3d::UnitZ() };
                    nozzle_gap = std::max(nozzle_gap, beads.clearance(pose, Matrix3d::Identity(), swept_head, 0., run, extruding, component));
                }
                record(nozzle_gap, component, p1);
            }
        }
        if (extruding) {
            if (!rotary && !nozzle.boxes.empty())
                beads.deposit({ a, Vec3d::UnitZ() }, { b, Vec3d::UnitZ() }, move.width, move.height, run);
            for (int j = 0; j < count; ++j) {
                const double t0 = double(j) / count, t1 = double(j + 1) / count;
                const Vec3d p0 = a + t0 * (b - a), p1 = a + t1 * (b - a);
                if (rotary) beads.deposit(kinematics.forward(interpolate(last.joints, move.joints, t0)),
                                          kinematics.forward(interpolate(last.joints, move.joints, t1)), move.width, move.height, run);
                else {
                    columns.deposit(p0, p1, move.width);
                    if (!nozzle.boxes.empty()) pending.push_back({ p0, p1, move.width });
                }
            }
        }
        report.per_move[i] = float(best);
        report.min_clearance = std::min(report.min_clearance, best);
        if (best < -ContactTolerance) {
            ++report.hit_moves;
            hit.penetration = -best;
            report.worst.push_back(std::move(hit));
            std::sort(report.worst.begin(), report.worst.end(), [](const auto &x, const auto &y) { return x.penetration > y.penetration; });
            if (report.worst.size() > 256) report.worst.resize(256);
        }
    }
    return report;
}
void check_export_clearance(Print &print, GCodeProcessorResult &result)
{
    const auto mode = print.config().nonplanar_collision_check.value;
    if (!result.nonplanar || mode == NonplanarCollisionCheck::Off) return;
    ClearanceParams params;
    params.head = Nonplanar::resolve_head_envelope(print.config());
    auto kinematics = make_tool_kinematics(print.config());
    result.clearance_report = verify_clearance(result, params, *kinematics, [&print] { if (print.canceled()) throw CanceledException(); });
    result.clearance = result.clearance_report.per_move;
    if (result.clearance_report.hit_moves == 0) return;
    const auto &hit = result.clearance_report.worst.front();
    std::ostringstream message;
    message << result.clearance_report.hit_moves << " moves would hit printed material (worst: " << hit.penetration
            << " mm, " << hit.component << ", layer " << hit.layer + 1 << ", line " << hit.gcode_line << ").";
    print.active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL, message.str());
    if (mode == NonplanarCollisionCheck::Block)
        throw RuntimeError(message.str() + " Export blocked by non-planar collision checking. Review the path or select warn to export it.");
}
}
