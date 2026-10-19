#include "SubLayerWalls.hpp"

#include "BeadModel.hpp"
#include "ConformalShells.hpp"

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double SampleStep  = 0.5;
constexpr double MinTilt     = 25.;
constexpr double MaxTilt     = 90.;
constexpr double BestTilt    = 47.5;
constexpr double MaxShift    = 0.25;
constexpr double MinRun      = 2.;
constexpr double MaxGap      = 1.;
constexpr double HostReach   = 1.;
constexpr double OtherClear  = 1.;
constexpr double PatchClear  = 3.;
constexpr double MinLayer    = 0.12;
constexpr double Simplify    = 0.005;
constexpr double TimeBudget  = 0.40;
constexpr double Overhead    = 1.3;

struct Sample
{
    Vec2d  p = Vec2d::Zero();
    Vec2d  n = Vec2d::Zero();
    double lo = 0., hi = 0.;
    double tilt = 90.;
    bool   valid = false;
    bool   sloped = false;
    double s = 0.;
};

struct Run
{
    size_t    first = 0, last = 0;
    Polyline3 raised;
    bool      attached = false;
};

struct Wall
{
    ExtrusionPaths     *owner = nullptr;
    size_t              index = 0;
    ExtrusionEntity   **slot  = nullptr;
    std::vector<Sample> samples;
    std::vector<Run>    runs;
    bool                hole = false;
};

struct LayerWalls
{
    std::vector<Wall>                   walls;
    std::vector<BoundingBox>            island_boxes;
    AABBTreeLines::LinesDistancer<Line> outline;
    AABBTreeLines::LinesDistancer<Line> holes;
};

using Admitted = std::array<std::array<bool, 91>, 2>;

size_t tilt_bin(const Sample &smp) { return size_t(std::clamp(int(std::lround(smp.tilt)), 0, 90)); }

bool candidate(const ExtrusionPath &path)
{
    return (path.role() == erExternalPerimeter || path.role() == erOverhangPerimeter) && !path.z_contoured &&
           !path.is_force_no_extrusion() && path.polyline.points.size() >= 2 && path.mm3_per_mm > 0. &&
           typeid(path) == typeid(ExtrusionPath);
}

bool enabled(const Layer &layer)
{
    if (layer.id() == 0 || layer.lower_layer == nullptr || layer.upper_layer == nullptr || layer.height < MinLayer - EPSILON)
        return false;
    for (const LayerRegion *region : layer.regions())
        if (region->region().config().nonplanar_sublayer_walls.value)
            return true;
    return false;
}

void collect(ExtrusionEntityCollection &c, std::vector<Wall> &out)
{
    for (ExtrusionEntity *&ee : c.entities) {
        ExtrusionPaths *owner = nullptr;
        if (auto *nested = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            collect(*nested, out);
            continue;
        }
        if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee))
            owner = &loop->paths;
        else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(ee))
            owner = &multi->paths;
        else if (auto *path = dynamic_cast<ExtrusionPath *>(ee); path != nullptr && candidate(*path)) {
            Wall w;
            w.slot = &ee;
            out.emplace_back(std::move(w));
            continue;
        }
        if (owner != nullptr)
            for (size_t i = 0; i < owner->size(); ++i)
                if (candidate((*owner)[i])) {
                    Wall w;
                    w.owner = owner;
                    w.index = i;
                    out.emplace_back(std::move(w));
                }
    }
}

const ExtrusionPath &wall_path(const Wall &w) { return w.slot ? *static_cast<const ExtrusionPath *>(*w.slot) : (*w.owner)[w.index]; }

int outward_side(const Layer &layer, const LayerWalls &lw, const ExtrusionPath &path)
{
    const Points3 &pts   = path.polyline.points;
    int            votes = 0;
    for (size_t k = 1; k <= 3; ++k) {
        const size_t i = std::min(pts.size() - 2, (pts.size() - 1) * k / 4);
        const Vec2d  a = unscale(pts[i].to_point()), b = unscale(pts[i + 1].to_point());
        const Vec2d  t = b - a;
        if (t.squaredNorm() < 1e-8)
            continue;
        const Point q      = Point::new_scale(0.5 * (a + b) + double(path.width) * Vec2d(t.y(), -t.x()).normalized());
        bool        inside = false;
        for (size_t j = 0; j < layer.lslices.size() && !inside; ++j)
            inside = lw.island_boxes[j].contains(q) && layer.lslices[j].contains(q);
        votes += inside ? -1 : 1;
    }
    return votes > 0 ? 1 : votes < 0 ? -1 : 0;
}

void sample(const Layer &layer, const LayerWalls &lw, const LayerWalls &below, const LayerWalls &above, Wall &w)
{
    const ExtrusionPath &path = wall_path(w);
    const int            side = outward_side(layer, lw, path);
    if (side == 0)
        return;
    const Points3 &pts = path.polyline.points;
    double         s   = 0.;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2d  a = unscale(pts[i].to_point()), b = unscale(pts[i + 1].to_point());
        const double L = (b - a).norm();
        if (L < 1e-6)
            continue;
        const int n = std::max(1, int(std::ceil(L / SampleStep)));
        for (int j = w.samples.empty() ? 0 : 1; j <= n; ++j) {
            Sample smp;
            smp.p = a + (b - a) * (double(j) / n);
            smp.s = s + L * double(j) / n;
            w.samples.emplace_back(smp);
        }
        s += L;
    }
    const Layer &lower = *layer.lower_layer, &upper = *layer.upper_layer;
    const double h = layer.height, cap = MaxShift * path.width;
    const double mid = layer.print_z - 0.5 * h;
    const double f_lo = 0.25 * h / (mid - (lower.print_z - 0.5 * lower.height));
    const double f_hi = 0.25 * h / ((upper.print_z - 0.5 * upper.height) - mid);
    for (size_t i = 0; i < w.samples.size(); ++i) {
        const Vec2d t = w.samples[std::min(i + 1, w.samples.size() - 1)].p - w.samples[i == 0 ? 0 : i - 1].p;
        if (t.squaredNorm() < 1e-12)
            continue;
        Sample &smp = w.samples[i];
        smp.n       = Vec2d(t.y(), -t.x()).normalized() * double(side);
        const Point  edge = Point::new_scale(smp.p + 0.5 * double(path.width) * smp.n);
        const double lo   = -f_lo * unscale<double>(below.outline.distance_from_lines<true>(edge));
        const double hi   = -f_hi * unscale<double>(above.outline.distance_from_lines<true>(edge));
        if (!std::isfinite(lo) || !std::isfinite(hi))
            continue;
        const double step = lo - hi;
        smp.tilt  = Geometry::rad2deg(std::atan2(0.5 * h, std::abs(step)));
        smp.lo    = std::clamp(lo, -cap, cap);
        smp.hi    = std::clamp(hi, -cap, cap);
        smp.valid = smp.tilt >= MinTilt && smp.tilt <= MaxTilt;
    }
    const Point middle = pts[pts.size() / 2].to_point();
    w.hole = !lw.holes.get_lines().empty() &&
             std::abs(lw.holes.distance_from_lines<true>(middle)) <= std::abs(lw.outline.distance_from_lines<true>(middle)) + SCALED_EPSILON;
}

std::vector<Run> runs_of(std::vector<Sample> &samples)
{
    std::vector<Run> out;
    if (samples.size() < 2)
        return out;
    auto runs = [&samples](bool value) {
        std::vector<std::pair<size_t, size_t>> r;
        for (size_t i = 0; i < samples.size();) {
            if (samples[i].sloped != value) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j + 1 < samples.size() && samples[j + 1].sloped == value)
                ++j;
            r.emplace_back(i, j);
            i = j + 1;
        }
        return r;
    };
    for (const auto &[i, j] : runs(false))
        if (i > 0 && j + 1 < samples.size() && samples[j + 1].s - samples[i - 1].s <= MaxGap) {
            const Sample &a = samples[i - 1], &b = samples[j + 1];
            for (size_t k = i; k <= j; ++k) {
                const double t    = (samples[k].s - a.s) / std::max(1e-9, b.s - a.s);
                samples[k].lo     = a.lo + t * (b.lo - a.lo);
                samples[k].hi     = a.hi + t * (b.hi - a.hi);
                samples[k].n      = samples[k].n.squaredNorm() > 0. ? samples[k].n : a.n;
                samples[k].sloped = true;
            }
        }
    for (const auto &[i, j] : runs(true)) {
        if (samples[j].s - samples[i].s < MinRun)
            for (size_t k = i; k <= j; ++k)
                samples[k].sloped = false;
        else
            out.push_back(Run{ i, j, {}, false });
    }
    return out;
}

double stretch_time(double length, double speed, double accel)
{
    if (speed <= 0.)
        return 0.;
    if (accel <= 0. || length >= speed * speed / accel)
        return length / speed + (accel > 0. ? speed / accel : 0.);
    return 2. * std::sqrt(length / accel);
}

double polyline_time(const Polyline &pl, double speed, double accel)
{
    double t = 0., run = 0.;
    for (size_t i = 0; i + 1 < pl.points.size(); ++i) {
        const Vec2d a = unscale(pl.points[i]), b = unscale(pl.points[i + 1]);
        run += (b - a).norm();
        if (i + 2 < pl.points.size()) {
            const Vec2d c = unscale(pl.points[i + 2]);
            if ((b - a).normalized().dot((c - b).normalized()) < std::cos(Geometry::deg2rad(30.)))
                t += stretch_time(run, speed, accel), run = 0.;
        }
    }
    return t + stretch_time(run, speed, accel);
}

std::pair<double, double> estimated_times(const PrintObject &object)
{
    const PrintObjectConfig &oc    = object.config();
    const double             accel = oc.default_acceleration.get_at(0);
    auto                     a_or  = [accel](double a) { return a > 0. ? a : accel; };
    double                   total = 0., outer = 0.;
    for (const Layer *layer : object.layers())
        for (const LayerRegion *region : layer->regions()) {
            const PrintRegionConfig &c = region->region().config();
            auto motion = [&](ExtrusionRole role) -> std::pair<double, double> {
                switch (role) {
                case erExternalPerimeter: return { c.outer_wall_speed.get_at(0), a_or(oc.outer_wall_acceleration.get_at(0)) };
                case erPerimeter: return { c.inner_wall_speed.get_at(0), a_or(oc.inner_wall_acceleration.get_at(0)) };
                case erInternalInfill: return { c.sparse_infill_speed.get_at(0), accel };
                case erSolidInfill: return { c.internal_solid_infill_speed.get_at(0), accel };
                case erTopSolidInfill: return { c.top_surface_speed.get_at(0), a_or(oc.top_surface_acceleration.get_at(0)) };
                case erGapFill: return { c.gap_infill_speed.get_at(0), accel };
                case erOverhangPerimeter:
                case erBridgeInfill:
                case erInternalBridgeInfill: return { c.bridge_speed.get_at(0), accel };
                default: return { c.inner_wall_speed.get_at(0), accel };
                }
            };
            for (const ExtrusionEntityCollection *collection : { &region->perimeters, &region->fills })
                for (const ExtrusionEntity *ee : collection->flatten().entities) {
                    const auto [v, a] = motion(ee->role());
                    for (const Polyline &pl : ee->as_polylines()) {
                        const double t = polyline_time(pl, v, a);
                        total += t;
                        if (ee->role() == erExternalPerimeter)
                            outer += t;
                    }
                }
        }
    if (object.num_printing_regions() > 0)
        if (const double v = object.printing_region(0).config().top_surface_speed.get_at(0); v > 0.)
            for (const ConformalPatch &patch : object.conformal().patches)
                for (const ExtrusionEntityCollection &shell : patch.shell_paths)
                    for (const ExtrusionEntity *ee : shell.flatten().entities)
                        for (const Polyline &pl : ee->as_polylines())
                            total += polyline_time(pl, v, a_or(oc.top_surface_acceleration.get_at(0)));
    return { total, outer };
}

Admitted admitted_tilts(const PrintObject &object, const std::vector<LayerWalls> &layers)
{
    std::array<std::array<double, 91>, 2> length{};
    for (const LayerWalls &lw : layers)
        for (const Wall &w : lw.walls)
            for (size_t i = 0; i + 1 < w.samples.size(); ++i)
                if (const Sample &smp = w.samples[i]; smp.valid)
                    length[w.hole][tilt_bin(smp)] += w.samples[i + 1].s - smp.s;
    const auto [total, outer] = estimated_times(object);
    double outer_length = 0.;
    for (const Layer *layer : object.layers())
        for (const LayerRegion *region : layer->regions())
            for (const ExtrusionEntity *ee : region->perimeters.flatten().entities)
                if (ee->role() == erExternalPerimeter)
                    outer_length += unscale<double>(ee->length());
    Admitted admitted{};
    if (outer <= 0. || outer_length <= 0.)
        return admitted;
    const double v_outer = outer_length / outer;
    std::vector<int> order;
    for (int bin = int(MinTilt); bin <= int(MaxTilt); ++bin)
        order.push_back(bin);
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return std::abs(a - BestTilt) < std::abs(b - BestTilt); });
    const double budget = TimeBudget * total;
    double       spent  = 0.;
    for (const bool hole : { false, true })
        for (int bin : order) {
            const double cost = Overhead * length[hole][size_t(bin)] / v_outer;
            if (spent + cost > budget)
                break;
            spent += cost;
            admitted[hole][size_t(bin)] = true;
        }
    BOOST_LOG_TRIVIAL(info) << "Sub-layer walls: " << object.model_object()->name << " estimated " << total << " s, outer walls "
                            << outer_length / std::max(v_outer, 1e-9) << " s at " << v_outer << " mm/s, half-beads " << spent
                            << " s of a " << budget << " s budget";
    return admitted;
}

void replace(const Layer &layer, const Wall &w, ExtrusionPaths &out)
{
    const ExtrusionPath &path    = wall_path(w);
    const double         h       = layer.height;
    const double         half_mm = path.mm3_per_mm * bead_area(path.width, 0.5 * h) / bead_area(path.width, h);
    const auto          &samples = w.samples;
    size_t               i       = 0;
    while (i < samples.size()) {
        size_t j = i;
        while (j + 1 < samples.size() && samples[j + 1].sloped == samples[i].sloped)
            ++j;
        const size_t last = std::min(j + 1, samples.size() - 1);
        Polyline3    pl;
        for (size_t k = i; k <= last; ++k) {
            const Point q = Point::new_scale(samples[k].p + (samples[i].sloped && samples[k].sloped ? samples[k].hi : 0.) * samples[k].n);
            pl.points.emplace_back(q.x(), q.y(), coord_t(0));
        }
        pl.simplify(scale_(Simplify));
        if (pl.points.size() >= 2) {
            ExtrusionPath piece(std::move(pl), path);
            if (samples[i].sloped) {
                piece.height     = float(0.5 * h);
                piece.mm3_per_mm = half_mm;
            }
            out.emplace_back(std::move(piece));
        }
        i = j + 1;
    }
}

std::vector<std::pair<ExtrusionLoop *, Polygon>> outer_loops(Layer &layer)
{
    std::vector<std::pair<ExtrusionLoop *, Polygon>> loops;
    for (LayerRegion *region : layer.regions()) {
        std::vector<ExtrusionEntity *> stack(region->perimeters.entities.begin(), region->perimeters.entities.end());
        while (!stack.empty()) {
            ExtrusionEntity *ee = stack.back();
            stack.pop_back();
            if (auto *c = dynamic_cast<ExtrusionEntityCollection *>(ee))
                stack.insert(stack.end(), c->entities.begin(), c->entities.end());
            else if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee);
                     loop != nullptr && std::any_of(loop->paths.begin(), loop->paths.end(), [](const ExtrusionPath &p) {
                         return p.role() == erExternalPerimeter || p.role() == erOverhangPerimeter;
                     }))
                loops.emplace_back(loop, loop->polygon());
        }
    }
    return loops;
}

ExPolygons curved_passes(const PrintObject &object, const Layer &layer, const Layer &host, double clear)
{
    ExPolygons passes;
    for (const ConformalPatch &patch : object.conformal().patches)
        if (patch.z_min < layer.print_z + layer.height && patch.z_max > host.print_z - host.height)
            append(passes, patch.footprint);
    return offset_ex(passes, float(scale_(clear)));
}

}

void split_sublayer_walls(PrintObject &object)
{
    if (object.config().nonplanar_mode.value != NonplanarMode::Auto || object.object_extruders().size() > 1)
        return;
    const LayerPtrs &layers = object.layers();
    if (std::none_of(layers.begin(), layers.end(), [](const Layer *l) { return enabled(*l); }))
        return;
    std::vector<LayerWalls> walls(layers.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t k = range.begin(); k < range.end(); ++k) {
            for (const ExPolygon &island : layers[k]->lslices)
                walls[k].island_boxes.emplace_back(get_extents(island));
            walls[k].outline = AABBTreeLines::LinesDistancer<Line>(to_lines(layers[k]->lslices));
            Lines hole_lines;
            for (const ExPolygon &island : layers[k]->lslices)
                for (const Polygon &hole : island.holes)
                    append(hole_lines, hole.lines());
            walls[k].holes = AABBTreeLines::LinesDistancer<Line>(std::move(hole_lines));
        }
    });
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t k = range.begin(); k < range.end(); ++k) {
            Layer &layer = *layers[k];
            if (!enabled(layer))
                continue;
            for (LayerRegion *region : layer.regions())
                if (region->region().config().nonplanar_sublayer_walls.value)
                    for (ExtrusionEntity *ee : region->perimeters.entities)
                        if (auto *island = dynamic_cast<ExtrusionEntityCollection *>(ee))
                            collect(*island, walls[k].walls);
            for (Wall &w : walls[k].walls)
                sample(layer, walls[k], walls[k - 1], walls[k + 1], w);
        }
    });

    const Admitted admitted = admitted_tilts(object, walls);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t k = range.begin(); k < range.end(); ++k) {
            const ExPolygons  passes     = k > 0 ? curved_passes(object, *layers[k], *layers[k - 1], PatchClear + 2. * SampleStep) : ExPolygons();
            const BoundingBox passes_box = get_extents(passes);
            for (Wall &w : walls[k].walls) {
                for (Sample &smp : w.samples) {
                    smp.sloped = smp.valid && admitted[w.hole][tilt_bin(smp)];
                    if (const Point q = Point::new_scale(smp.p); smp.sloped && passes_box.contains(q))
                        smp.sloped = std::none_of(passes.begin(), passes.end(), [&q](const ExPolygon &e) { return e.contains(q); });
                }
                w.runs = runs_of(w.samples);
                const coord_t up = coord_t(scale_(0.5 * layers[k]->height));
                for (Run &run : w.runs) {
                    for (size_t i = run.first; i <= std::min(run.last + 1, w.samples.size() - 1); ++i) {
                        const Sample &smp = w.samples[i];
                        const Point   q   = Point::new_scale(smp.p + (smp.sloped ? smp.lo : 0.) * smp.n);
                        run.raised.points.emplace_back(q.x(), q.y(), up);
                    }
                    run.raised.simplify(scale_(Simplify));
                }
            }
        }
    });

    tbb::parallel_for(tbb::blocked_range<size_t>(1, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t k = range.begin(); k < range.end(); ++k) {
            if (walls[k].walls.empty())
                continue;
            Layer &host  = *layers[k - 1];
            auto   loops = outer_loops(host);
            if (loops.empty())
                continue;
            Lines               lines;
            std::vector<size_t> line_loop;
            for (size_t i = 0; i < loops.size(); ++i)
                for (const Line &l : loops[i].second.lines())
                    lines.emplace_back(l), line_loop.emplace_back(i);
            const AABBTreeLines::LinesDistancer<Line> nearest(std::move(lines));
            const ExPolygons passes = curved_passes(object, *layers[k], host, PatchClear);
            std::vector<ExPolygons> forbidden(host.lslices.size() + 1);
            for (size_t j = 0; j <= host.lslices.size(); ++j) {
                ExPolygons others = passes;
                for (size_t i = 0; i < host.lslices.size(); ++i)
                    if (i != j)
                        append(others, offset_ex(host.lslices[i], float(scale_(OtherClear))));
                forbidden[j] = union_ex(others);
            }
            for (Wall &w : walls[k].walls)
                for (Run &run : w.runs) {
                    const Points3 &pts = run.raised.points;
                    if (pts.size() < 2)
                        continue;
                    const auto [d_mid, line_mid, foot_mid] = nearest.distance_from_lines_extra<false>(pts[pts.size() / 2].to_point());
                    if (line_mid >= line_loop.size() || d_mid > scale_(HostReach))
                        continue;
                    const size_t host_loop = line_loop[line_mid];
                    auto near_loop = [&](const Point &q) {
                        const auto [d, line, p] = nearest.distance_from_lines_extra<false>(q);
                        return line < line_loop.size() && line_loop[line] == host_loop && d <= scale_(HostReach);
                    };
                    if (!near_loop(pts.front().to_point()) || !near_loop(pts.back().to_point()))
                        continue;
                    ExtrusionLoop *best = loops[host_loop].first;
                    size_t         own  = host.lslices.size();
                    const Point    on_loop = best->first_point();
                    for (size_t j = 0; j < host.lslices.size() && own == host.lslices.size(); ++j)
                        if (walls[k - 1].island_boxes[j].contains(on_loop) && host.lslices[j].contains(on_loop))
                            own = j;
                    if (!intersection_pl(Polylines{ run.raised.to_polyline() }, forbidden[own]).empty())
                        continue;
                    const ExtrusionPath &path = wall_path(w);
                    const double         h    = layers[k]->height;
                    ExtrusionPath        bead(run.raised, path);
                    bead.height      = float(0.5 * h);
                    bead.mm3_per_mm  = path.mm3_per_mm * bead_area(path.width, 0.5 * h) / bead_area(path.width, h);
                    bead.z_contoured = true;
                    best->raised.emplace_back(std::move(bead));
                    run.attached = true;
                }
        }
    });

    tbb::parallel_for(tbb::blocked_range<size_t>(1, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t k = range.begin(); k < range.end(); ++k) {
            std::map<ExtrusionPaths *, std::vector<Wall *>> by_owner;
            for (Wall &w : walls[k].walls) {
                for (Sample &smp : w.samples)
                    smp.sloped = false;
                bool any = false;
                for (const Run &run : w.runs)
                    if (run.attached) {
                        any = true;
                        for (size_t i = run.first; i <= run.last; ++i)
                            w.samples[i].sloped = true;
                    }
                if (!any)
                    continue;
                if (w.slot != nullptr) {
                    ExtrusionPaths out;
                    replace(*layers[k], w, out);
                    delete *w.slot;
                    *w.slot = new ExtrusionMultiPath(std::move(out));
                } else
                    by_owner[w.owner].push_back(&w);
            }
            for (auto &[owner, ws] : by_owner) {
                ExtrusionPaths out;
                size_t         next = 0;
                for (size_t i = 0; i < owner->size(); ++i) {
                    if (next < ws.size() && ws[next]->index == i)
                        replace(*layers[k], *ws[next++], out);
                    else
                        out.emplace_back(std::move((*owner)[i]));
                }
                *owner = std::move(out);
            }
        }
    });
}

}
