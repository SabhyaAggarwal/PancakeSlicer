#include "CurvedTopsBudget.hpp"
#include "FirmwareTime.hpp"
#include "MultiplanarConfirm.hpp"
#include "TopSurfaceGrid.hpp"
#include "CurvedTopsSurface.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {
namespace {
constexpr double Inf = std::numeric_limits<double>::infinity();
struct Control {
    std::unique_ptr<TopSurfaceGrid> grid;
    std::vector<double> flat_bead;
    std::vector<int> region;
    std::map<int, double> planar_times;
    double seconds = 0., object_seconds = 0., outside_seconds = 0.;
    CurvedTopBudgetReport report;
    bool attempted = false, valid = false;
};
int cell_at(const TopSurfaceGrid &grid, const Vec2d &p)
{
    const Vec2d q = (p - grid.origin) / TopSurfaceGrid::pitch;
    const int x = int(std::floor(q.x())), y = int(std::floor(q.y()));
    return x >= 0 && y >= 0 && x < grid.nx && y < grid.ny ? y * grid.nx + x : -1;
}
std::map<int, double> patch_times(const FirmwareTime &time, const Control &control, int object_id,
                                const PrintObject &object, bool planar)
{
    std::map<int, double> sums;
    if (!control.grid) return sums;
    const Vec2d first_shift = unscale(object.instances().front().shift);
    for (const auto &m : time.moves) {
        if (m.object != object_id && !(object_id == 0 && planar && m.patch >= 0)) continue;
        int id = planar ? -1 : m.patch;
        if (id < 0) {
            const Vec3d mid = .5 * (m.from + m.to);
            for (const auto &instance : object.instances()) {
                const Vec2d p = mid.head<2>() - unscale(instance.shift) + first_shift;
                const int k = cell_at(*control.grid, p);
                if (k < 0 || control.region[size_t(k)] < 0) continue;
                if (mid.z() < control.grid->mesh[size_t(k)] - 1. || mid.z() > control.grid->mesh[size_t(k)] + 1.) continue;
                id = control.region[size_t(k)]; break;
            }
        }
        if (id >= 0) sums[id] += m.seconds;
    }
    if (!planar && !object.instances().empty())
        for (auto &[id, seconds] : sums) seconds /= object.instances().size();
    return sums;
}
void build_control(Control &control, Print &print, const PrintObject &object)
{
    control.attempted = true;
    const boost::filesystem::path file = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("np-ct-budget-%%%%-%%%%.gcode");
    struct Remove {
        boost::filesystem::path path;
        ~Remove() { boost::system::error_code ec; boost::filesystem::remove(path, ec); }
    } remove{file};
    GCodeProcessorResult planar;
    const double measured = measured_planar_export(object, planar, file.string());
    if (!(std::isfinite(measured) && measured > 0.)) { control.report.reason = "planar control could not be measured"; return; }
    const FirmwareTime fw = firmware_time(file.string(), print.config(), print.is_BBL_printer(), [&print] {
        if (print.canceled()) throw CanceledException();
    });
    if (!fw.valid) { control.report.reason = "firmware timing is unavailable for the planar control"; return; }
    TriangleMesh mesh = object.model_object()->raw_mesh();
    mesh.transform(object.instances().front().model_instance->get_matrix());
    control.grid = std::make_unique<TopSurfaceGrid>(mesh.its);
    control.grid->deposit(planar);
    control.flat_bead = std::move(control.grid->bead);
    control.region.assign(control.grid->mesh.size(), -1);
    const Vec2d shift = unscale(object.instances().front().shift);
    std::map<int, ExPolygons> footprints;
    std::map<int, std::pair<double, double>> heights;
    for (const auto &surface : object.nonplanar_surfaces()) {
        footprints.emplace(surface.stable_id, surface.horizontal_projection());
        heights.emplace(surface.stable_id, std::make_pair(surface.stats.min.z, surface.stats.max.z));
    }
    for (const auto &[id, footprint] : footprints) {
        const BoundingBox box = get_extents(footprint);
        control.grid->visit(unscale(box.min) + shift, unscale(box.max) + shift, [&](size_t k, const Vec2d &p) {
            const auto &z = heights.at(id);
            if (control.grid->mesh[k] < z.first - .001 || control.grid->mesh[k] > z.second + .001) return;
            if (control.region[k] < 0 && std::any_of(footprint.begin(), footprint.end(), [&](const ExPolygon &poly) { return poly.contains(Point::new_scale(p - shift)); })) control.region[k] = id;
        });
    }
    control.seconds = fw.seconds;
    for (const auto &m : fw.moves) (m.object == 0 ? control.object_seconds : control.outside_seconds) += m.seconds;
    control.planar_times = patch_times(fw, control, 0, object, true);
    control.valid = true;
}
}

struct CurvedTopsBudget::Impl { std::map<const PrintObject *, Control> controls; };
CurvedTopsBudget::CurvedTopsBudget() : m_impl(std::make_unique<Impl>()) {}
CurvedTopsBudget::~CurvedTopsBudget() = default;
CurvedTopBudgetReport CurvedTopsBudget::report(const PrintObject &object) const
{
    const auto it = m_impl->controls.find(&object);
    return it == m_impl->controls.end() ? CurvedTopBudgetReport{} : it->second.report;
}
std::vector<std::pair<PrintObject *, int>> CurvedTopsBudget::reject(Print &print, const GCodeProcessorResult &result)
{
    std::vector<std::pair<PrintObject *, int>> rejected;
    if (!firmware_time_supported(print.config(), print.is_BBL_printer())) return rejected;
    if (std::none_of(print.objects().begin(), print.objects().end(), [](const PrintObject *o) {
        return o->config().nonplanar_mode.value == NonplanarMode::CurvedTops && curved_tops_stats(*o).kept > 0;
    }) && m_impl->controls.empty()) return rejected;
    const FirmwareTime fw = firmware_time(result.filename, print.config(), print.is_BBL_printer(), [&print] {
        if (print.canceled()) throw CanceledException();
    });
    size_t instances = 0;
    for (const auto *object : print.objects()) instances += object->instances().size();
    double outside = 0.;
    std::map<int, double> object_times;
    for (const auto &m : fw.moves) {
        if (m.object < 0) outside += m.seconds;
        else object_times[m.object] += m.seconds;
    }
    for (size_t oi = 0; oi < print.objects().size(); ++oi) {
        PrintObject &object = *print.get_object(oi);
        if (object.config().nonplanar_mode.value != NonplanarMode::CurvedTops || object.instances().empty()) continue;
        auto old = m_impl->controls.find(&object);
        if (old == m_impl->controls.end() && curved_tops_stats(object).kept == 0) continue;
        Control &c = m_impl->controls[&object];
        if (!c.attempted) build_control(c, print, object);
        auto &report = c.report;
        const bool measured = fw.valid && c.valid;
        report.measured = measured;
        report.planar_s = c.seconds;
        report.candidate_s = fw.seconds;
        if (print.objects().size() > 1 || object.instances().size() > 1) {
            const double shared = instances > 0 ? outside / instances : 0.;
            report.candidate_s = object_times[int(oi)] / object.instances().size() + shared;
            report.planar_s = c.object_seconds + std::min(c.outside_seconds, shared);
        }
        report.time_budget_percent = object.config().nonplanar_time_budget.value;
        report.limit_s = (1. + .01 * report.time_budget_percent) * report.planar_s;
        report.within_budget = measured && report.candidate_s <= report.limit_s;
        std::set<int> active;
        std::map<int, std::pair<double, double>> heights;
        for (const auto &m : fw.moves) {
            if (!m.extruding || m.patch < 0 || (m.object != int(oi) && print.objects().size() != 1)) continue;
            const double lo = std::min(m.from.z(), m.to.z()), hi = std::max(m.from.z(), m.to.z());
            auto [it, inserted] = heights.emplace(m.patch, std::make_pair(lo, hi));
            if (!inserted) { it->second.first = std::min(it->second.first, lo); it->second.second = std::max(it->second.second, hi); }
        }
        for (const auto &[id, height] : heights)
            if (height.second - height.first >= .03 - .001) active.insert(id);
        if (!fw.valid) for (const auto &surface : object.nonplanar_surfaces()) active.insert(surface.stable_id);
        if (active.empty()) continue;
        std::map<int, CurvedTopBudgetPatch> patches;
        for (int id : active) { auto &p = patches[id]; p.id = id; }
        if (measured) {
            const auto times = patch_times(fw, c, int(oi), object, false);
            auto &grid = *c.grid;
            grid.bead.assign(grid.mesh.size(), -Inf);
            std::fill(grid.conformal.begin(), grid.conformal.end(), 0);
            std::fill(grid.ironed.begin(), grid.ironed.end(), 0);
            grid.deposit(result);
            struct Error { double flat = 0., curve = 0.; size_t count = 0; } whole;
            std::map<int, Error> errors;
            for (size_t k = 0; k < grid.mesh.size(); ++k) {
                if (!grid.eligible[k] || !std::isfinite(c.flat_bead[k]) || !std::isfinite(grid.bead[k])) continue;
                const double flat = c.flat_bead[k] - grid.mesh[k], curve = grid.bead[k] - grid.mesh[k];
                whole.flat += flat * flat; whole.curve += curve * curve; ++whole.count;
                if (!active.count(c.region[k])) continue;
                auto &e = errors[c.region[k]];
                e.flat += flat * flat; e.curve += curve * curve; ++e.count;
            }
            for (auto &[id, p] : patches) {
                const auto t = times.find(id); p.curved_s = t == times.end() ? 0. : t->second;
                const auto pt = c.planar_times.find(id); p.planar_s = pt == c.planar_times.end() ? 0. : pt->second;
                p.cost_s = std::max(0., p.curved_s - p.planar_s);
                const auto e = errors.find(id);
                if (e != errors.end() && e->second.count > 0 && whole.count > 0) {
                    p.samples = e->second.count;
                    p.planar_rms_mm = std::sqrt(e->second.flat / p.samples);
                    p.curved_rms_mm = std::sqrt(e->second.curve / p.samples);
                    p.rms_gain_mm = std::sqrt(std::max(0., whole.curve + e->second.flat - e->second.curve) / whole.count) -
                                    std::sqrt(whole.curve / whole.count);
                }
            }
        }
        int worst = -1, worst_quality = -1;
        double worst_score = -1., worst_quality_loss = 0.;
        for (const auto &[id, p] : patches) {
            const double score = p.samples > 0 && p.rms_gain_mm > 0. ? p.cost_s / p.rms_gain_mm : Inf;
            BOOST_LOG_TRIVIAL(info) << std::setprecision(12) << "Curved top budget patch: " << object.model_object()->name
                << " id " << id << " cost_s " << p.cost_s << " rms_gain_mm " << p.rms_gain_mm << " score " << score;
            if (score > worst_score) { worst = id; worst_score = score; }
            const double quality_loss = p.samples * (p.curved_rms_mm * p.curved_rms_mm -
                                                     p.planar_rms_mm * p.planar_rms_mm);
            if (measured && p.samples > 0 && p.curved_rms_mm > p.planar_rms_mm + 1e-9 &&
                quality_loss > worst_quality_loss) {
                worst_quality = id;
                worst_quality_loss = quality_loss;
            }
        }
        const int drop = worst_quality >= 0 ? worst_quality : !report.within_budget ? worst : -1;
        if (drop >= 0) {
            patches[drop].dropped = true;
            rejected.emplace_back(&object, drop);
            report.reason = worst_quality >= 0 ? "curved patch top RMS exceeds the planar control" :
                measured ? "curved patch exceeds the process firmware time budget" :
                report.reason.empty() ? "firmware time could not be measured" : report.reason;
        }
        std::map<int, CurvedTopBudgetPatch> records;
        for (const auto &p : report.patches) if (p.dropped) records[p.id] = p;
        for (const auto &[id, p] : patches) records[id] = p;
        report.patches.clear();
        for (const auto &[id, p] : records) report.patches.push_back(p);
        BOOST_LOG_TRIVIAL(warning) << std::setprecision(12) << "Curved top budget: " << object.model_object()->name << " firmware " << report.candidate_s
            << " s planar " << report.planar_s << " s cap " << report.limit_s << " s drop "
            << drop << " score " << worst_score << " reason " << report.reason;
    }
    return rejected;
}
}
