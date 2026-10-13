#include "ClearanceVerifier.hpp"
#include "GCodeProcessor.hpp"
#include "ToolKinematics.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Nonplanar/AutoMode.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintBase.hpp"
#include <sstream>
#include <Eigen/Geometry>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
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

using Corners = std::array<Vec3d, 8>;
Corners bead_corners(const ToolPose &a, const ToolPose &b, double width, double height)
{
    Corners corners;
    for (int end = 0; end < 2; ++end) {
        const ToolPose &p = end == 0 ? a : b;
        Vec3d side = p.axis.cross(b.tip - a.tip);
        if (side.squaredNorm() < 1e-20) side = p.axis.unitOrthogonal();
        side.normalize();
        for (int sign = 0; sign < 2; ++sign)
            for (int depth = 0; depth < 2; ++depth)
                corners[4 * end + 2 * sign + depth] = p.tip + (sign == 0 ? -0.5 : 0.5) * width * side - depth * height * p.axis;
    }
    return corners;
}
struct Bead { Corners corners; size_t run; };
struct UprightBead { uint32_t move, last, run; uint16_t piece, pieces; };
class BeadGrid
{
public:
    BeadGrid(const std::vector<GCodeProcessorResult::MoveVertex> &moves, bool rotary) : m_moves(moves), m_rotary(rotary) {}
    double material_radius = 0.;
    size_t size() const { return m_beads.size() + m_upright.size(); }
    void deposit(const ToolPose &a, const ToolPose &b, double width, double height, size_t run)
    {
        m_beads.push_back({ bead_corners(a, b, width, height), run });
        index(m_beads.back().corners);
    }
    void deposit_upright(size_t move, size_t last, size_t run, int piece = 0, int pieces = 1)
    {
        m_upright.push_back({ uint32_t(move), uint32_t(last), uint32_t(run), uint16_t(piece), uint16_t(pieces) });
        index(corners(size() - 1));
    }
    double clearance(const ToolPose &pose, const Matrix3d &frame, const Nonplanar::HeadEnvelope &head,
                     double inflation, size_t run, bool extruding, std::string &component, size_t limit) const
    {
        double best = Inf;
        std::vector<uint32_t> candidates;
        for (const auto &raw : head.boxes) {
            auto box = prepared_box(raw, head);
            if (box.min.z() > box.max.z()) continue;
            Vec3d lo = box.min - Vec3d::Constant(inflation), hi = box.max + Vec3d::Constant(inflation);
            Vec3d work_lo = Vec3d::Constant(Inf), work_hi = Vec3d::Constant(-Inf);
            if (box.infinite_x || box.infinite_y) {
                candidates.resize(limit);
                for (size_t i = 0; i < limit; ++i) candidates[i] = uint32_t(i);
            } else {
                for (int k = 0; k < 8; ++k) {
                    const Vec3d v = pose.tip + frame.transpose() * Vec3d(k & 1 ? hi.x() : lo.x(), k & 2 ? hi.y() : lo.y(), k & 4 ? hi.z() : lo.z());
                    work_lo = work_lo.cwiseMin(v); work_hi = work_hi.cwiseMax(v);
                }
                candidates.clear();
                visit(work_lo, work_hi, [&](uint64_t cell) {
                    auto it = m_grid.find(cell);
                    if (it != m_grid.end())
                        for (uint32_t id : it->second)
                            if (id < limit) candidates.push_back(id);
                });
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            }
            for (uint32_t id : candidates) {
                if (box.nozzle && extruding && bead_run(id) == run) continue;
                Vec3d bl = Vec3d::Constant(Inf), bh = Vec3d::Constant(-Inf);
                for (const Vec3d &v : corners(id)) {
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
    Corners corners(size_t id) const
    {
        if (!m_beads.empty()) return m_beads[id].corners;
        const UprightBead &bead = m_upright[id];
        const auto        &move = m_moves[bead.move];
        const Vec3d        a    = move_tip(m_moves[bead.last], m_rotary), b = move_tip(move, m_rotary);
        return bead_corners({ a + (double(bead.piece) / bead.pieces) * (b - a), Vec3d::UnitZ() },
                            { a + (double(bead.piece + 1) / bead.pieces) * (b - a), Vec3d::UnitZ() }, move.width, move.height);
    }
    size_t bead_run(size_t id) const { return m_beads.empty() ? m_upright[id].run : m_beads[id].run; }
    void index(const Corners &corners)
    {
        Vec3d lo = Vec3d::Constant(Inf), hi = Vec3d::Constant(-Inf);
        for (const Vec3d &v : corners) {
            lo = lo.cwiseMin(v); hi = hi.cwiseMax(v);
            material_radius = std::max(material_radius, v.norm());
        }
        const uint32_t id = uint32_t(size() - 1);
        visit(lo, hi, [&](uint64_t cell) { m_grid[cell].push_back(id); });
    }
    template<class Fn> static void visit(const Vec3d &lo, const Vec3d &hi, Fn fn)
    {
        std::array<int, 3> a, b;
        for (int k = 0; k < 3; ++k) { a[k] = int(std::floor(lo[k] / 4.)); b[k] = int(std::floor(hi[k] / 4.)); }
        for (int z = a[2]; z <= b[2]; ++z)
            for (int y = a[1]; y <= b[1]; ++y)
                for (int x = a[0]; x <= b[0]; ++x)
                    fn((uint64_t(uint32_t(x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(y) & 0x1FFFFF) << 21) | (uint32_t(z) & 0x1FFFFF));
    }
    const std::vector<GCodeProcessorResult::MoveVertex> &m_moves;
    bool                                                 m_rotary;
    std::vector<Bead>                                    m_beads;
    std::vector<UprightBead>                             m_upright;
    std::unordered_map<uint64_t, std::vector<uint32_t>>  m_grid;
};

class TiltedBeads
{
public:
    struct Prefix { uint32_t beads = 0; double ceiling = -Inf, radius = 0.; Vec3d half { Vec3d::Zero() }; };
    struct Scratch
    {
        std::vector<uint32_t> seen;
        uint32_t query = 0;
        std::vector<std::pair<double, const void *>> cells;
        std::vector<std::pair<Vec3d, Vec3d>> bands;
    };
    Prefix prefix() const { return m_prefix; }
    explicit TiltedBeads(const Nonplanar::HeadEnvelope &head)
    {
        Group nozzle;
        for (const auto &raw : head.boxes) {
            const auto box = prepared_box(raw, head);
            if (box.min.z() > box.max.z()) continue;
            const size_t id = m_boxes.size();
            m_boxes.push_back({ box.min, box.max, raw.name, box.nozzle, box.infinite_x, box.infinite_y });
            const bool finite_nozzle = box.nozzle && !box.infinite_x && !box.infinite_y;
            Group &group = finite_nozzle ? nozzle : m_groups.emplace_back();
            group.infinite = box.infinite_x || box.infinite_y;
            group.nozzle = box.nozzle;
            group.add(id, box.min, box.max, finite_nozzle);
        }
        if (!nozzle.bands.empty()) m_groups.insert(m_groups.begin(), std::move(nozzle));
    }
    void deposit(const ToolPose &a, const ToolPose &b, double width, double height, size_t run)
    {
        Road bead { bead_corners(a, b, width, std::min(height, width)), Vec3d::Constant(Inf), Vec3d::Constant(-Inf), run };
        for (const Vec3d &v : bead.corners) {
            bead.lo = bead.lo.cwiseMin(v); bead.hi = bead.hi.cwiseMax(v);
            m_prefix.radius = std::max(m_prefix.radius, v.norm());
        }
        m_prefix.ceiling = std::max(m_prefix.ceiling, bead.hi.z());
        m_prefix.half = m_prefix.half.cwiseMax(0.5 * (bead.hi - bead.lo));
        const uint32_t id = uint32_t(m_beads.size());
        m_beads.push_back(bead);
        ++m_prefix.beads;
        Axes axes;
        const Vec3d n = a.axis.normalized();
        Vec3d side = n.cross(b.tip - a.tip);
        side = side.squaredNorm() < 1e-20 ? n.unitOrthogonal() : side.normalized();
        axes.u.row(0) = n; axes.u.row(1) = side; axes.u.row(2) = side.cross(n);
        axes.lo = Vec3d::Constant(Inf); axes.hi = Vec3d::Constant(-Inf);
        for (const Vec3d &v : bead.corners) {
            const Vec3d p = axes.u * v;
            axes.lo = axes.lo.cwiseMin(p); axes.hi = axes.hi.cwiseMax(p);
        }
        m_axes.push_back(axes);
        if (m_chunks.empty() || m_chunks.back().run != run || m_chunks.back().count == ChunkBeads ||
            (m_chunks.back().hi.cwiseMax(bead.hi) - m_chunks.back().lo.cwiseMin(bead.lo)).maxCoeff() > ChunkSize + (bead.hi - bead.lo).maxCoeff()) {
            m_chunks.push_back({ bead.lo, bead.hi, id, 0, run });
        }
        Chunk &chunk = m_chunks.back();
        ++chunk.count;
        chunk.lo = chunk.lo.cwiseMin(bead.lo);
        chunk.hi = chunk.hi.cwiseMax(bead.hi);
        const uint32_t chunk_id = uint32_t(m_chunks.size() - 1);
        for (Grid *grid : { &m_fine, &m_coarse })
            visit(*grid, bead.lo, bead.hi, [&](Slot &cell) {
                cell.lo = cell.lo.cwiseMin(bead.lo);
                cell.hi = cell.hi.cwiseMax(bead.hi);
                if (cell.chunks.empty() || cell.chunks.back() != chunk_id) {
                    if (cell.blocks.empty() || cell.blocks.back().end - cell.blocks.back().begin == BlockChunks)
                        cell.blocks.push_back({ Vec3d::Constant(Inf), Vec3d::Constant(-Inf), uint32_t(cell.chunks.size()), uint32_t(cell.chunks.size()) });
                    cell.chunks.push_back(chunk_id);
                    ++cell.blocks.back().end;
                }
                Block &block = cell.blocks.back();
                block.lo = block.lo.cwiseMin(bead.lo);
                block.hi = block.hi.cwiseMax(bead.hi);
            });
    }
    double clearance(const Prefix &laid, Scratch &scratch, const ToolPose &pose, const Matrix3d &frame, const Vec3d &sweep, double turn,
                     size_t run, bool extruding, double cutoff, std::string &component) const
    {
        double best = cutoff;
        size_t best_box = std::numeric_limits<size_t>::max();
        if (laid.beads == 0) return Inf;
        scratch.seen.resize(m_chunks.size(), 0);
        const Matrix3d to_work = frame.transpose(), abs_frame = frame.cwiseAbs();
        const Vec3d slack = (abs_frame.transpose() * abs_frame - Matrix3d::Identity()) * laid.half;
        auto bound_head = [&](const Vec3d &bl, const Vec3d &bh, const Box *axes, double limit) {
            double g = Inf;
            for (const auto &[band_lo, band_hi] : scratch.bands) {
                g = std::min(g, axes ? gap(band_lo, band_hi, bl, bh, axes) : (band_lo - bh).cwiseMax(bl - band_hi).maxCoeff());
                if (g <= limit) break;
            }
            return g;
        };
        auto bound = [&](const Vec3d &lo, const Vec3d &hi, const Box *axes, double limit = -Inf) {
            const Vec3d centre = frame * (0.5 * (lo + hi) - pose.tip), half = abs_frame * (0.5 * (hi - lo));
            return bound_head(centre - half, centre + half, axes, limit);
        };
        for (const Group &group : m_groups) {
            const double reach = group.infinite ? laid.radius + pose.tip.norm() : group.radius;
            const Vec3d grow = sweep + Vec3d::Constant(0.5 * turn * reach);
            const Vec3d glo = group.lo - grow, ghi = group.hi + grow;
            const Box *axes = group.infinite ? &m_boxes[group.bands.front().boxes.front()] : nullptr;
            const bool own_run_clear = group.nozzle && extruding;
            scratch.bands.clear();
            for (const Band &band : group.bands) scratch.bands.emplace_back(band.lo - grow, band.hi + grow);
            Vec3d work_lo = Vec3d::Constant(-Inf), work_hi = Vec3d::Constant(Inf);
            auto measure = [&](uint32_t chunk_id) {
                const Chunk &chunk = m_chunks[chunk_id];
                if (chunk.first >= laid.beads || (own_run_clear && chunk.run == run)) return;
                if (!((chunk.lo.array() <= work_hi.array()).all() && (chunk.hi.array() >= work_lo.array()).all()) ||
                    bound(chunk.lo, chunk.hi, axes, best) > best) return;
                for (uint32_t id = chunk.first; id < std::min(chunk.first + chunk.count, laid.beads); ++id) {
                    const Road &bead = m_beads[id];
                    if (!((bead.lo.array() <= work_hi.array()).all() && (bead.hi.array() >= work_lo.array()).all())) continue;
                    {
                        const Axes &road = m_axes[id];
                        const Matrix3d turn_road = frame * road.u.transpose();
                        const Vec3d centre = turn_road * (0.5 * (road.lo + road.hi)) - frame * pose.tip;
                        const Vec3d half = turn_road.cwiseAbs() * (0.5 * (road.hi - road.lo));
                        if (bound_head(centre - half, centre + half, axes, best) > best) continue;
                    }
                    Vec3d bl = Vec3d::Constant(Inf), bh = Vec3d::Constant(-Inf);
                    for (const Vec3d &v : bead.corners) {
                        const Vec3d q = frame * (v - pose.tip);
                        bl = bl.cwiseMin(q); bh = bh.cwiseMax(q);
                    }
                    for (size_t b = 0; b < group.bands.size(); ++b) {
                        const auto &[band_lo, band_hi] = scratch.bands[b];
                        if (gap(band_lo, band_hi, bl, bh, axes) > best) continue;
                        for (size_t index : group.bands[b].boxes) {
                            const Box &box = m_boxes[index];
                            if (box.nozzle && extruding && bead.run == run) continue;
                            double g = gap(box.lo - grow, box.hi + grow, bl, bh, &box);
                            if ((g < best || (g == best && index < best_box)) && !box.infinite_x && !box.infinite_y) {
                                const Axes &road = m_axes[id];
                                const Vec3d centre = 0.5 * (box.lo + box.hi), half = 0.5 * (box.hi - box.lo) + grow;
                                const Vec3d tip = road.u * pose.tip;
                                for (int k = 0; k < 3; ++k) {
                                    const Vec3d u = frame * road.u.row(k).transpose();
                                    const double c = u.dot(centre), r = u.cwiseAbs().dot(half);
                                    g = std::max(g, std::max(road.lo[k] - tip[k] - (c + r), c - r - (road.hi[k] - tip[k])));
                                }
                            }
                            if (g < best || (g == best && index < best_box)) { best = g; best_box = index; }
                        }
                    }
                }
            };
            if (group.infinite) {
                for (uint32_t id = 0; id < m_chunks.size() && m_chunks[id].first < laid.beads; ++id) measure(id);
                continue;
            }
            work_lo = Vec3d::Constant(Inf); work_hi = Vec3d::Constant(-Inf);
            for (int k = 0; k < 8; ++k) {
                const Vec3d v = pose.tip + to_work * Vec3d(k & 1 ? ghi.x() : glo.x(), k & 2 ? ghi.y() : glo.y(), k & 4 ? ghi.z() : glo.z());
                work_lo = work_lo.cwiseMin(v); work_hi = work_hi.cwiseMax(v);
            }
            work_lo -= slack; work_hi += slack;
            if (work_lo.z() > laid.ceiling) continue;
            const uint32_t query = ++scratch.query;
            const Vec3d extent = (work_hi - work_lo) / FineCell;
            const Grid &grid = (extent.x() + 2.) * (extent.y() + 2.) * (extent.z() + 2.) <= 4096. ? m_fine : m_coarse;
            scratch.cells.clear();
            visit_find(grid, work_lo, work_hi, [&](const Slot &cell) {
                if ((cell.lo.array() <= work_hi.array()).all() && (cell.hi.array() >= work_lo.array()).all())
                    scratch.cells.emplace_back(bound(cell.lo, cell.hi, axes), &cell);
            });
            std::sort(scratch.cells.begin(), scratch.cells.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
            for (const auto &[cell_bound, address] : scratch.cells) {
                if (cell_bound > best) break;
                const Slot &cell = *static_cast<const Slot *>(address);
                for (const Block &block : cell.blocks) {
                    if (m_chunks[cell.chunks[block.begin]].first >= laid.beads) break;
                    if (bound(block.lo, block.hi, axes, best) > best) continue;
                    for (uint32_t k = block.begin; k < block.end; ++k)
                        if (const uint32_t id = cell.chunks[k]; scratch.seen[id] != query) { scratch.seen[id] = query; measure(id); }
                }
            }
        }
        if (best_box == std::numeric_limits<size_t>::max()) return Inf;
        component = m_boxes[best_box].name;
        return best;
    }
private:
    static constexpr double FineCell = 1., CoarseCell = 4., ChunkSize = 1.;
    static constexpr uint32_t ChunkBeads = 16, BlockChunks = 16;
    struct Box { Vec3d lo, hi; std::string name; bool nozzle, infinite_x, infinite_y; };
    struct Band { Vec3d lo { Vec3d::Constant(Inf) }, hi { Vec3d::Constant(-Inf) }; std::vector<size_t> boxes; };
    struct Group
    {
        Vec3d lo { Vec3d::Constant(Inf) }, hi { Vec3d::Constant(-Inf) };
        std::vector<Band> bands;
        bool infinite = false, nozzle = false;
        double radius = 0.;
        void add(size_t id, const Vec3d &min, const Vec3d &max, bool banded)
        {
            auto it = std::find_if(bands.begin(), bands.end(), [&](const Band &b) {
                return banded && b.lo.z() == min.z() && b.hi.z() == max.z(); });
            Band &band = it != bands.end() ? *it : bands.emplace_back();
            band.boxes.push_back(id);
            band.lo = band.lo.cwiseMin(min); band.hi = band.hi.cwiseMax(max);
            lo = lo.cwiseMin(min); hi = hi.cwiseMax(max);
            radius = std::max(radius, min.cwiseAbs().cwiseMax(max.cwiseAbs()).norm());
        }
    };
    struct Chunk { Vec3d lo, hi; uint32_t first, count; size_t run; };
    struct Block { Vec3d lo, hi; uint32_t begin, end; };
    struct Slot { Vec3d lo { Vec3d::Constant(Inf) }, hi { Vec3d::Constant(-Inf) }; std::vector<uint32_t> chunks; std::vector<Block> blocks; };
    struct Grid { double cell; std::unordered_map<uint64_t, Slot> cells; };
    static double gap(const Vec3d &lo, const Vec3d &hi, const Vec3d &bl, const Vec3d &bh, const Box *box)
    {
        if (!box || !(box->infinite_x || box->infinite_y)) return (lo - bh).cwiseMax(bl - hi).maxCoeff();
        double g = -Inf;
        for (int axis = 0; axis < 3; ++axis) {
            if (box && ((axis == 0 && box->infinite_x) || (axis == 1 && box->infinite_y))) continue;
            g = std::max(g, std::max(lo[axis] - bh[axis], bl[axis] - hi[axis]));
        }
        return g;
    }
    static uint64_t key(int x, int y, int z)
    { return (uint64_t(uint32_t(x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(y) & 0x1FFFFF) << 21) | uint64_t(uint32_t(z) & 0x1FFFFF); }
    template<class Fn> static void visit(Grid &grid, const Vec3d &lo, const Vec3d &hi, Fn fn)
    {
        for (int z = int(std::floor(lo.z() / grid.cell)); z <= int(std::floor(hi.z() / grid.cell)); ++z)
            for (int y = int(std::floor(lo.y() / grid.cell)); y <= int(std::floor(hi.y() / grid.cell)); ++y)
                for (int x = int(std::floor(lo.x() / grid.cell)); x <= int(std::floor(hi.x() / grid.cell)); ++x)
                    fn(grid.cells[key(x, y, z)]);
    }
    template<class Fn> static void visit_find(const Grid &grid, const Vec3d &lo, const Vec3d &hi, Fn fn)
    {
        for (int z = int(std::floor(lo.z() / grid.cell)); z <= int(std::floor(hi.z() / grid.cell)); ++z)
            for (int y = int(std::floor(lo.y() / grid.cell)); y <= int(std::floor(hi.y() / grid.cell)); ++y)
                for (int x = int(std::floor(lo.x() / grid.cell)); x <= int(std::floor(hi.x() / grid.cell)); ++x)
                    if (auto it = grid.cells.find(key(x, y, z)); it != grid.cells.end()) fn(it->second);
    }
    std::vector<Box> m_boxes;
    std::vector<Group> m_groups;
    struct Road { Corners corners; Vec3d lo, hi; size_t run; };
    std::vector<Road> m_beads;
    std::vector<Chunk> m_chunks;
    struct Axes { Matrix3d u; Vec3d lo, hi; };
    std::vector<Axes> m_axes;
    Grid m_fine { FineCell, {} }, m_coarse { CoarseCell, {} };
    Prefix m_prefix;
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
    m_tiles.assign(size_t(m_tx) * ((m_ny + m_tile - 1) / m_tile), -Inf);
    m_blocks.resize(m_tiles.size());
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

std::vector<double> MaterialColumns::tops(const BoundingBoxf &rect, Vec2d &origin, int &nx, double &pitch) const
{
    int x0, y0, x1, y1;
    cells(rect, x0, y0, x1, y1);
    pitch  = m_pitch;
    nx     = std::max(0, x1 - x0);
    origin = m_bbox.min + m_pitch * Vec2d(x0, y0);
    std::vector<double> out;
    if (x0 >= x1 || y0 >= y1)
        return out;
    out.reserve(size_t(nx) * size_t(y1 - y0));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            out.push_back(cell_top(x, y));
    return out;
}

void MaterialColumns::deposit(const Vec3d &a, const Vec3d &b, double width)
{
    const Vec2d r = Vec2d::Constant(0.5 * std::max(0., width));
    int x0, y0, x1, y1;
    cells(BoundingBoxf(a.head<2>().cwiseMin(b.head<2>()) - r, a.head<2>().cwiseMax(b.head<2>()) + r), x0, y0, x1, y1);
    const double z = std::max(a.z(), b.z());
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1;) {
            const int    tx = x / m_tile, end = std::min(x1, (tx + 1) * m_tile);
            const size_t t  = size_t(y / m_tile) * m_tx + tx;
            std::vector<double> &block = m_blocks[t];
            if (block.empty()) block.assign(size_t(m_tile) * m_tile, -Inf);
            double *row = block.data() + size_t(y % m_tile) * m_tile - tx * m_tile;
            for (; x < end; ++x) row[x] = std::max(row[x], z);
            m_tiles[t] = std::max(m_tiles[t], z);
        }
    if (x0 < x1 && y0 < y1) m_ceiling = std::max(m_ceiling, z);
}

double MaterialColumns::top_in_rect(const BoundingBoxf &rect) const { return top_above(rect, -Inf); }

double MaterialColumns::tile_bound(const BoundingBoxf &rect) const
{
    int x0, y0, x1, y1;
    cells(rect, x0, y0, x1, y1);
    double best = -Inf;
    if (x0 < x1 && y0 < y1)
        for (int ty = y0 / m_tile; ty <= (y1 - 1) / m_tile; ++ty)
            for (int tx = x0 / m_tile; tx <= (x1 - 1) / m_tile; ++tx) best = std::max(best, m_tiles[size_t(ty) * m_tx + tx]);
    return best;
}

double MaterialColumns::top_above(const BoundingBoxf &rect, double floor) const
{
    int x0, y0, x1, y1;
    cells(rect, x0, y0, x1, y1);
    if (x0 >= x1 || y0 >= y1) return floor;
    double best = floor;
    for (int ty = y0 / m_tile; ty <= (y1 - 1) / m_tile; ++ty)
        for (int tx = x0 / m_tile; tx <= (x1 - 1) / m_tile; ++tx) {
            const double tile = m_tiles[size_t(ty) * m_tx + tx];
            if (tile <= best) continue;
            const int ax = std::max(x0, tx * m_tile), bx = std::min(x1, (tx + 1) * m_tile);
            const int ay = std::max(y0, ty * m_tile), by = std::min(y1, (ty + 1) * m_tile);
            if (ax == tx * m_tile && bx == std::min(m_nx, (tx + 1) * m_tile) &&
                ay == ty * m_tile && by == std::min(m_ny, (ty + 1) * m_tile)) { best = tile; continue; }
            const double *block = m_blocks[size_t(ty) * m_tx + tx].data();
            for (int y = ay; y < by; ++y) {
                const double *row = block + size_t(y - ty * m_tile) * m_tile - tx * m_tile;
                for (int x = ax; x < bx; ++x) best = std::max(best, row[x]);
            }
        }
    return best;
}

double MaterialColumns::clearance(const Vec3d &tip, const Nonplanar::HeadEnvelope &head, std::string *component) const
{ return swept_clearance(tip, tip, head, component); }

MaterialColumns::PreparedHead MaterialColumns::prepare(const Nonplanar::HeadEnvelope &head)
{
    PreparedHead out;
    for (size_t i = 0; i < head.boxes.size(); ++i) {
        const auto box = prepared_box(head.boxes[i], head);
        if (box.min.z() > box.max.z()) continue;
        out.boxes.push_back({ box.min, box.max, box.infinite_x, box.infinite_y, i });
        if (!box.infinite_x && !box.infinite_y) {
            out.finite_min = out.finite_min.cwiseMin(box.min.head<2>());
            out.finite_max = out.finite_max.cwiseMax(box.max.head<2>());
        }
    }
    std::stable_sort(out.boxes.begin(), out.boxes.end(), [](const PreparedBox &l, const PreparedBox &r) { return l.min.z() < r.min.z(); });
    return out;
}

double MaterialColumns::swept_clearance(const Vec3d &a, const Vec3d &b, const Nonplanar::HeadEnvelope &head, std::string *component) const
{
    const PreparedHead prepared = prepare(head);
    int box = -1;
    const double gap = swept_clearance(a, b, prepared, Inf, &box);
    if (component) *component = box >= 0 ? head.boxes[prepared.boxes[size_t(box)].index].name : std::string();
    return gap;
}

double MaterialColumns::swept_clearance(const Vec3d &a, const Vec3d &b, const PreparedHead &head, double bound, int *box_index,
                                        int hint) const
{
    if (box_index) *box_index = -1;
    if (m_ceiling == -Inf) return bound;
    const std::vector<PreparedBox> &boxes = head.boxes;
    const double z  = std::min(a.z(), b.z());
    const Vec2d  ab_lo = a.head<2>().cwiseMin(b.head<2>()), ab_hi = a.head<2>().cwiseMax(b.head<2>());
    const double local = head.finite_min.x() <= head.finite_max.x() ?
        tile_bound(BoundingBoxf(ab_lo + head.finite_min, ab_hi + head.finite_max)) : -Inf;
    auto gap = [&](size_t i, double limit, double &g) {
        const PreparedBox &box   = boxes[i];
        const double       floor = z + box.min.z();
        if (floor - (box.infinite_x || box.infinite_y ? m_ceiling : local) > limit) return false;
        Vec2d lo = ab_lo + box.min.head<2>(), hi = ab_hi + box.max.head<2>();
        if (box.infinite_x) { lo.x() = -Inf; hi.x() = Inf; }
        if (box.infinite_y) { lo.y() = -Inf; hi.y() = Inf; }
        const double threshold = floor - limit - 1e-9;
        const double top       = top_above(BoundingBoxf(lo, hi), threshold);
        if (!(top > threshold)) return false;
        g = floor - top;
        return g <= limit;
    };
    double best  = bound;
    int    found = -1;
    if (double g; hint >= 0 && size_t(hint) < boxes.size() && gap(size_t(hint), best, g) && g < best) { best = g; found = hint; }
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (z + boxes[i].min.z() - m_ceiling > best) break;
        if (double g; int(i) != found && gap(i, best, g) &&
            (g < best || (found >= 0 && g == best && boxes[i].index < boxes[size_t(found)].index))) { best = g; found = int(i); }
    }
    if (box_index) *box_index = found;
    return found >= 0 ? best : bound;
}

double MaterialColumns::tilted_clearance(const Vec3d &tip, const Matrix3d &frame, const Nonplanar::HeadEnvelope &head) const
{
    double best = Inf;
    if (m_ceiling == -Inf) return best;
    const Vec3d up = frame.col(2);
    for (const auto &raw : head.boxes) {
        if (raw.infinite_x || raw.infinite_y) continue;
        const auto box = prepared_box(raw, head);
        if (box.min.z() > box.max.z()) continue;
        Vec3d lo = Vec3d::Constant(Inf), hi = Vec3d::Constant(-Inf);
        for (int k = 0; k < 8; ++k) {
            const Vec3d v = tip + frame.transpose() * Vec3d(k & 1 ? box.max.x() : box.min.x(), k & 2 ? box.max.y() : box.min.y(),
                                                            k & 4 ? box.max.z() : box.min.z());
            lo = lo.cwiseMin(v), hi = hi.cwiseMax(v);
        }
        if (lo.z() - m_ceiling >= best) continue;
        const BoundingBoxf rect(lo.head<2>(), hi.head<2>());
        if (! box.nozzle) {
            best = std::min(best, lo.z() - top_in_rect(rect));
            continue;
        }
        Vec3d bmin = box.min, bmax = box.max;
        bmin.head<2>().array() -= 0.71 * m_pitch;
        bmax.head<2>().array() += 0.71 * m_pitch;
        int x0, y0, x1, y1;
        cells(BoundingBoxf(rect.min - Vec2d::Constant(m_pitch), rect.max + Vec2d::Constant(m_pitch)), x0, y0, x1, y1);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const double top = cell_top(x, y);
                if (top == -Inf || lo.z() - top >= best) continue;
                const Vec3d m = frame * (Vec3d(m_bbox.min.x() + (x + 0.5) * m_pitch, m_bbox.min.y() + (y + 0.5) * m_pitch, top) - tip);
                double t0 = -Inf, t1 = Inf;
                bool   miss = false;
                for (int a = 0; a < 3 && ! miss; ++a) {
                    if (std::abs(up[a]) < 1e-12) {
                        miss = m[a] < bmin[a] || m[a] > bmax[a];
                        continue;
                    }
                    double p0 = (bmin[a] - m[a]) / up[a], p1 = (bmax[a] - m[a]) / up[a];
                    if (p0 > p1) std::swap(p0, p1);
                    t0 = std::max(t0, p0), t1 = std::min(t1, p1);
                }
                if (! miss && t0 <= t1)
                    best = std::min(best, t0);
            }
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
    bool turned = false;
    for (const auto &move : result.moves)
        turned |= move.has_joints && (move.joints.rot[0] != 0. || move.joints.rot[1] != 0.);
    const bool rotary = kinematics.rotary_count() != 0 && turned;
    for (const auto &move : result.moves) {
        if (!move.position.allFinite()) throw RuntimeError("Clearance verification found a non-finite G-code position.");
        bounds.merge(Vec2d(move_tip(move, rotary).head<2>()));
        max_width = std::max(max_width, double(move.width));
    }
    bounds.min.array() -= max_width + params.pitch; bounds.max.array() += max_width + params.pitch;
    Nonplanar::HeadEnvelope body = params.head, nozzle = params.head;
    for (auto &box : body.boxes) box.nozzle = is_nozzle(box, params.head);
    nozzle = body;
    body.boxes.erase(std::remove_if(body.boxes.begin(), body.boxes.end(), [](const auto &b) { return b.nozzle; }), body.boxes.end());
    nozzle.boxes.erase(std::remove_if(nozzle.boxes.begin(), nozzle.boxes.end(), [](const auto &b) { return !b.nozzle; }), nozzle.boxes.end());
    Nonplanar::HeadEnvelope contact_strict = nozzle, contact_allowed = nozzle;
    contact_strict.tip_allowance = {};
    contact_allowed.tip_allowance.height = std::max(contact_allowed.tip_allowance.height, params.bead_eps);
    const MaterialColumns::PreparedHead body_boxes = MaterialColumns::prepare(body), strict_boxes = MaterialColumns::prepare(contact_strict),
                                       allowed_boxes = MaterialColumns::prepare(contact_allowed);
    double head_lever = 0.;
    bool infinite_head = false;
    for (const auto &raw : params.head.boxes) {
        const auto box = prepared_box(raw, params.head);
        head_lever = std::max(head_lever, box.min.cwiseAbs().cwiseMax(box.max.cwiseAbs()).norm());
        infinite_head |= box.infinite_x || box.infinite_y;
    }
    size_t first_print = result.moves.size(), last_print = 0;
    for (size_t i = 0; i < result.moves.size(); ++i)
        if (const auto &mv = result.moves[i]; mv.type == EMoveType::Extrude && mv.extrusion_role != erCustom && mv.extrusion_role != erNone)
            first_print = std::min(first_print, i), last_print = i;

    struct Checked { uint32_t move, last, run, beads; bool extruding; };
    std::vector<Checked> checked;
    {
        size_t last_index = 0, run = 0;
        for (size_t i = 1; i < result.moves.size(); ++i) {
            const auto &move = result.moves[i];
            if (move.type == EMoveType::Seam || move.type == EMoveType::Color_change || move.type == EMoveType::Tool_change ||
                move.type == EMoveType::Pause_Print || move.type == EMoveType::Custom_GCode) continue;
            const auto &last = result.moves[last_index];
            const size_t from = last_index;
            last_index = i;
            const bool extruding = move.type == EMoveType::Extrude;
            if (!extruding && move.type != EMoveType::Retract && move.type != EMoveType::Unretract &&
                (move_tip(move, rotary) - move_tip(last, rotary)).squaredNorm() == 0. && move.joints.rot == last.joints.rot) continue;
            if (!extruding) ++run;
            checked.push_back({ uint32_t(i), uint32_t(from), uint32_t(run), 0, extruding });
        }
    }

    struct Pending { Vec3d a, b; double width; };
    struct Sweep
    {
        MaterialColumns columns, previous;
        std::vector<Pending> pending;
        int body_hint = -1, strict_hint = -1, allowed_hint = -1;
        void flush()
        {
            for (const auto &p : pending) previous.deposit(p.a, p.b, p.width);
            pending.clear();
        }
    };
    BeadGrid beads(result.moves, rotary);
    using Hits = std::vector<std::pair<size_t, ClearanceHit>>;
    std::vector<double> bests(checked.size(), Inf);
    TiltedBeads tilted(params.head);
    std::vector<TiltedBeads::Prefix> laid_before;
    auto step = [&](Sweep &sweep, BeadGrid &bead_grid, size_t k, Hits *hits, TiltedBeads::Scratch *scratch = nullptr) {
        const Checked &c = checked[k];
        if (throw_if_canceled) throw_if_canceled();
        const auto &move = result.moves[c.move], &last = result.moves[c.last];
        const bool extruding = c.extruding;
        const Vec3d a = move_tip(last, rotary), b = move_tip(move, rotary);
        const bool straight_lift = !extruding && a.head<2>() == b.head<2>() && b.z() >= a.z() && move.joints.rot == last.joints.rot;
        double displacement = (b - a).norm(), turn = 0., tip_lever = 0.;
        const TiltedBeads::Prefix &laid = rotary ? laid_before[k] : TiltedBeads::Prefix {};
        if (rotary) {
            const double angle = (std::abs(move.joints.rot[0] - last.joints.rot[0]) + std::abs(move.joints.rot[1] - last.joints.rot[1])) * M_PI / 180.;
            tip_lever = std::max(a.norm(), b.norm());
            if (const auto *kin = dynamic_cast<const RotaryKinematics *>(&kinematics))
                tip_lever += kin->geometry().pivot.norm() + kin->geometry().tool_mount.norm() + kin->geometry().tool_length + std::abs(kin->geometry().table_height);
            double lever = tip_lever + head_lever;
            if (infinite_head) lever += laid.radius;
            turn = angle;
            displacement = (move.joints.xyz - last.joints.xyz).norm() + angle * lever;
        }
        if (!std::isfinite(displacement) || displacement / params.sweep > 50000000.)
            throw RuntimeError("Clearance sweep is too large. Check the G-code coordinates and rotary angles.");
        const int count = std::max(1, int(std::ceil(displacement / params.sweep)));
        if (hits != nullptr) {
            double best = Inf;
            ClearanceHit hit;
            hit.move_id = c.move; hit.gcode_line = move.gcode_id; hit.layer = move.layer_id;
            auto record = [&](double gap, const std::string &component, const Vec3d &tip) {
                if (gap < best) { best = gap; hit.component = component; hit.tip = tip; }
            };
            bool lands_on_extrusion = false;
            if (!extruding && a.head<2>() == b.head<2>() && b.z() < a.z() && move.joints.rot == last.joints.rot)
                for (size_t k = size_t(c.move) + 1; k < result.moves.size(); ++k) {
                    const EMoveType type = result.moves[k].type;
                    if (type == EMoveType::Extrude || ((type == EMoveType::Travel || type == EMoveType::Wipe) &&
                        (move_tip(result.moves[k], rotary) != b || result.moves[k].joints.rot != move.joints.rot))) {
                        lands_on_extrusion = type == EMoveType::Extrude;
                        break;
                    }
                }
            const bool strict = !extruding && !straight_lift && !lands_on_extrusion && move.nonplanar_patch_shell == 0xFF;
            const Nonplanar::HeadEnvelope &contact_head = strict ? contact_strict : contact_allowed;
            const auto &contact_boxes = strict ? strict_boxes : allowed_boxes;
            const size_t bead_limit = rotary ? bead_grid.size() : c.beads;
            Vec3d sample_start = rotary ? kinematics.forward(last.joints).tip : a;
            for (int j = 0; j < count; ++j) {
                if ((j & 1023) == 0 && throw_if_canceled) throw_if_canceled();
                const double t0 = double(j) / count, t1 = double(j + 1) / count;
                const Vec3d p0 = a + t0 * (b - a), p1 = a + t1 * (b - a);
                std::string component;
                if (rotary) {
                    const JointPose q = interpolate(last.joints, move.joints, 0.5 * (t0 + t1));
                    const ToolPose pose = kinematics.forward(q);
                    const Matrix3d frame = kinematics.head_frame(q);
                    const Vec3d sample_end = kinematics.forward(interpolate(last.joints, move.joints, t1)).tip;
                    const double theta = turn / count, stride = (move.joints.xyz - last.joints.xyz).norm() / count;
                    const Vec3d grow = 0.5 * (frame * (sample_end - sample_start)).cwiseAbs() +
                                       Vec3d::Constant((theta * theta * tip_lever + 2. * theta * stride) / 8.);
                    sample_start = sample_end;
                    record(tilted.clearance(laid, *scratch, pose, frame, grow, theta, c.run, extruding, best, component), component, pose.tip);
                } else {
                    int index = -1;
                    const double body_gap = sweep.columns.swept_clearance(p0, p1, body_boxes, best, &index, sweep.body_hint);
                    if (index >= 0) { record(body_gap, body.boxes[body_boxes.boxes[size_t(index)].index].name, p1); sweep.body_hint = index; }
                    int &hint = strict ? sweep.strict_hint : sweep.allowed_hint;
                    double nozzle_gap = (extruding ? sweep.previous : sweep.columns).swept_clearance(p0, p1, contact_boxes, best, &index, hint);
                    if (index < 0) continue;
                    hint = index;
                    component = contact_head.boxes[contact_boxes.boxes[size_t(index)].index].name;
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
                        nozzle_gap = std::max(nozzle_gap, bead_grid.clearance(pose, Matrix3d::Identity(), swept_head, 0., c.run, extruding, component, bead_limit));
                    }
                    record(nozzle_gap, component, p1);
                }
            }
            bests[k] = best;
            if (best < -ContactTolerance) hits->emplace_back(k, std::move(hit));
        }
        if (extruding && move.extrusion_role != erCustom && !(rotary && hits != nullptr)) {
            for (int j = 0; j < count; ++j) {
                const double t0 = double(j) / count, t1 = double(j + 1) / count;
                const Vec3d p0 = a + t0 * (b - a), p1 = a + t1 * (b - a);
                if (rotary) tilted.deposit(kinematics.forward(interpolate(last.joints, move.joints, t0)),
                                           kinematics.forward(interpolate(last.joints, move.joints, t1)), move.width, move.height, c.run);
                else {
                    sweep.columns.deposit(p0, p1, move.width);
                    if (!nozzle.boxes.empty()) sweep.pending.push_back({ p0, p1, move.width });
                }
            }
        }
    };

    std::vector<Hits> hits;
    if (rotary) {
        Sweep sweep { MaterialColumns(bounds, params.pitch), MaterialColumns(bounds, params.pitch) };
        laid_before.resize(checked.size());
        for (size_t k = 0; k < checked.size(); ++k) {
            laid_before[k] = tilted.prefix();
            step(sweep, beads, k, nullptr);
        }
        const size_t target = std::max<size_t>(256, checked.size() / size_t(8 * tbb::this_task_arena::max_concurrency()));
        const size_t ranges = (checked.size() + target - 1) / target;
        hits.resize(ranges);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, ranges, 1), [&](const tbb::blocked_range<size_t> &range) {
            Sweep own { MaterialColumns(bounds, params.pitch), MaterialColumns(bounds, params.pitch) };
            TiltedBeads::Scratch scratch;
            for (size_t r = range.begin(); r < range.end(); ++r)
                for (size_t k = r * target; k < std::min(checked.size(), (r + 1) * target); ++k)
                    step(own, beads, k, &hits[r], &scratch);
        });
    } else {
        const size_t target = std::max<size_t>(512, checked.size() / size_t(2 * tbb::this_task_arena::max_concurrency()));
        std::vector<size_t> starts { 0 };
        for (size_t k = 1; k < checked.size(); ++k)
            if (!checked[k].extruding && k >= starts.back() + target) starts.push_back(k);
        starts.push_back(checked.size());
        std::vector<Sweep> copies;
        copies.reserve(starts.size() - 1);
        Sweep sweep { MaterialColumns(bounds, params.pitch), MaterialColumns(bounds, params.pitch) };
        for (size_t r = 0; r + 1 < starts.size(); ++r)
            for (size_t k = starts[r]; k < starts[r + 1]; ++k) {
                Checked &c = checked[k];
                if (!c.extruding) sweep.flush();
                if (k == starts[r]) copies.push_back(sweep);
                c.beads = uint32_t(beads.size());
                step(sweep, beads, k, nullptr);
                if (c.extruding && result.moves[c.move].extrusion_role != erCustom && !nozzle.boxes.empty()) {
                    const Vec3d a = move_tip(result.moves[c.last], rotary), b = move_tip(result.moves[c.move], rotary);
                    const int   samples = int(std::min(65535., std::max(1., std::ceil((b - a).norm() / params.sweep))));
                    const int   pieces  = std::clamp(int(std::ceil(std::abs(b.z() - a.z()) / std::max(params.bead_eps, EPSILON))), 1, samples);
                    for (int piece = 0; piece < pieces; ++piece)
                        beads.deposit_upright(c.move, c.last, c.run, piece, pieces);
                }
            }
        sweep = Sweep { MaterialColumns(bounds, params.pitch), MaterialColumns(bounds, params.pitch) };
        hits.resize(copies.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, copies.size(), 1), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t r = range.begin(); r < range.end(); ++r) {
                Sweep own = std::move(copies[r]);
                for (size_t k = starts[r]; k < starts[r + 1]; ++k) {
                    if (!checked[k].extruding && k != starts[r]) own.flush();
                    step(own, beads, k, &hits[r]);
                }
            }
        });
    }

    auto hit_it = hits.begin();
    size_t hit_at = 0;
    for (size_t k = 0; k < checked.size(); ++k) {
        const double best = bests[k];
        const size_t i    = checked[k].move;
        report.per_move[i] = float(best);
        ClearanceHit *hit = nullptr;
        if (best < -ContactTolerance) {
            while (hit_at == hit_it->size()) { ++hit_it; hit_at = 0; }
            hit = &(*hit_it)[hit_at++].second;
        }
        if (result.moves[i].extrusion_role == erCustom && (i < first_print || i > last_print)) {
            report.machine_hit_moves += hit != nullptr;
            continue;
        }
        report.min_clearance = std::min(report.min_clearance, best);
        if (hit != nullptr) {
            ++report.hit_moves;
            hit->penetration = -best;
            report.worst.push_back(std::move(*hit));
            std::sort(report.worst.begin(), report.worst.end(), [](const auto &x, const auto &y) { return x.penetration > y.penetration; });
            if (report.worst.size() > 256) report.worst.resize(256);
        }
    }
    return report;
}
void check_export_clearance(Print &print, GCodeProcessorResult &result)
{
    const auto mode = Nonplanar::effective_collision_check(print);
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
