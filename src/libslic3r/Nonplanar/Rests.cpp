#include "Rests.hpp"

#include "ConformalShells.hpp"
#include "Multiplanar.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double Step = 0.25, CellXY = 1., CellZ = 0.5, OwnRun = 2.;

struct Sample
{
    Vec3f    p;
    float    along;
    float    width;
    float    length;
    uint32_t run;
    bool     support, sparse;
};
struct Probe
{
    Vec3d    p, axis;
    double   along, height;
    uint32_t run;
};

int64_t key(int x, int y, int z) { return (int64_t(x) * 73856093) ^ (int64_t(y) * 19349663) ^ (int64_t(z) * 83492791); }

class Walker
{
public:
    Walker(double bed, double layer, double spacing, bool rotary) : m_bed(bed), m_layer(layer), m_spacing(spacing), m_rotary(rotary) {}

    void path(const ExtrusionPath &path, double z)
    {
        if (path.mm3_per_mm <= 0. || path.is_force_no_extrusion() || path.polyline.points.size() < 2)
            return;
        const ExtrusionRole role = path.role();
        if (role == erIroning)
            return;
        const bool checked = role != erInternalInfill && role != erBridgeInfill && role != erInternalBridgeInfill &&
                             role != erSolidInfill && role != erSupportMaterialInterface && role != erSupportTransition;
        const bool surface = role == erTopSolidInfill || role == erBottomSurface;
        const auto         *conformal = dynamic_cast<const ExtrusionPathConformal *>(&path);
        const Points3      &pts       = path.polyline.points;
        const uint32_t      run       = m_runs++;
        double              along     = 0.;
        for (size_t i = 1; i < pts.size(); ++i) {
            double h = path.height, w = path.width;
            if (conformal != nullptr) {
                if (i < conformal->bead_height.size() && (conformal->bead_height[i - 1] <= 0.f || conformal->bead_height[i] <= 0.f))
                    continue;
                if (i < conformal->bead_height.size())
                    h = 0.5 * (conformal->bead_height[i - 1] + conformal->bead_height[i]);
                if (!conformal->bead_width.empty() && i < conformal->bead_width.size())
                    w = 0.5 * (conformal->bead_width[i - 1] + conformal->bead_width[i]);
            }
            Vec3d ax = Vec3d::UnitZ();
            if (m_rotary && conformal != nullptr && i < conformal->tool_axis.size() && conformal->tool_axis[i].squaredNorm() > 0.f)
                ax = conformal->tool_axis[i].cast<double>().normalized();
            auto at = [&](size_t j) -> Vec3d {
                return Vec3d(unscale<double>(pts[j].x()), unscale<double>(pts[j].y()),
                             conformal != nullptr || path.z_contoured ? z + unscale<double>(pts[j].z()) : z) - 0.5 * h * ax;
            };
            const Vec3d  a = at(i - 1), b = at(i);
            const double l = (b - a).norm();
            const int    n = std::max(1, int(std::ceil(l / Step)));
            for (int k = 0; k < n; ++k) {
                const Vec3d p = a + (b - a) * ((k + 0.5) / n);
                const double s = along + l * (k + 0.5) / n;
                const Probe probe{ p, ax, s, h, run };
                if (checked && !this->rests(probe, h, w) && !(surface && this->spans(probe)))
                    m_floating.push_back(m_samples.size());
                this->add(Sample{ p.cast<float>(), float(s), float(w), float(l / n), run, is_support(role), role == erInternalInfill });
            }
            along += l;
        }
    }
    void raised(const ExtrusionEntity &ee, double z)
    {
        if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&ee)) {
            for (const ExtrusionPath &p : loop->raised)
                this->path(p, z);
        } else if (const auto *coll = dynamic_cast<const ExtrusionEntityCollection *>(&ee)) {
            for (const ExtrusionEntity *e : coll->entities)
                this->raised(*e, z);
        }
    }
    void entity(const ExtrusionEntity &ee, double z)
    {
        std::vector<const ExtrusionPath *> paths;
        collect_paths(&ee, paths);
        for (const ExtrusionPath *p : paths)
            this->path(*p, z);
    }

    Floating floating() const
    {
        Floating out;
        for (size_t i : m_floating)
            (m_samples[i].support ? out.support : out.part) += m_samples[i].length;
        return out;
    }

private:
    template<class Fn> bool any_near(const Probe &s, double reach, Fn &&fn) const
    {
        const int x0 = int(std::floor((s.p.x() - reach) / CellXY)), x1 = int(std::floor((s.p.x() + reach) / CellXY));
        const int y0 = int(std::floor((s.p.y() - reach) / CellXY)), y1 = int(std::floor((s.p.y() + reach) / CellXY));
        const int z0 = int(std::floor((s.p.z() - reach) / CellZ)), z1 = int(std::floor((s.p.z() + reach) / CellZ));
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if (auto it = m_cells.find(key(x, y, z)); it != m_cells.end())
                        for (uint32_t id : it->second)
                            if (fn(m_samples[id]))
                                return true;
        return false;
    }
    bool rests(const Probe &s, double h, double w) const
    {
        if (s.p.z() <= m_bed + 0.75 * m_layer)
            return true;
        constexpr double LateralSlack = 0.05, WidestBead = 1.5;
        const double deep = 2. * std::max(h, m_layer) + 0.1, own = 0.5 * w + h;
        const double side = std::max(own, 0.5 * (w + WidestBead) + LateralSlack);
        return this->any_near(s, std::sqrt(deep * deep + side * side), [&](const Sample &m) {
            const Vec3d  v     = m.p.cast<double>() - s.p;
            const double along = v.dot(s.axis);
            if (along > -0.01 || along < -deep || (v - along * s.axis).norm() > std::max(own, 0.5 * (w + m.width) + LateralSlack))
                return false;
            return m.run != s.run || s.along - m.along >= OwnRun;
        });
    }
    bool spans(const Probe &s) const
    {
        const double       deep = 2. * std::max(s.height, m_layer) + 0.1;
        std::vector<Vec3d> dirs;
        return this->any_near(s, std::sqrt(deep * deep + m_spacing * m_spacing), [&](const Sample &m) {
            if (!m.sparse)
                return false;
            const Vec3d  v     = m.p.cast<double>() - s.p;
            const double along = v.dot(s.axis);
            const Vec3d  u     = v - along * s.axis;
            const double n     = u.norm();
            if (along > -0.01 || along < -deep || n > m_spacing || n <= 1e-6)
                return false;
            for (const Vec3d &d : dirs)
                if (u.dot(d) < 0.)
                    return true;
            if (dirs.size() < 300)
                dirs.push_back(u / n);
            return false;
        });
    }
    void add(const Sample &s)
    {
        m_cells[key(int(std::floor(s.p.x() / CellXY)), int(std::floor(s.p.y() / CellXY)), int(std::floor(s.p.z() / CellZ)))].push_back(
            uint32_t(m_samples.size()));
        m_samples.push_back(s);
    }

    double                                             m_bed, m_layer, m_spacing;
    bool                                               m_rotary;
    uint32_t                                           m_runs = 0;
    std::vector<Sample>                                m_samples;
    std::vector<uint32_t>                              m_floating;
    std::unordered_map<int64_t, std::vector<uint32_t>> m_cells;
};

}

Floating floating_road(const PrintObject &object, const ConformalResult *result)
{
    const auto layers  = object.layers();
    const auto support = object.support_layers();
    if (layers.empty())
        return {};
    const Layer *first = layers.front();
    double       bed   = first->print_z - 0.5 * first->height;
    if (!support.empty())
        bed = std::min(bed, support.front()->print_z - 0.5 * support.front()->height);
    const PrintRegionConfig &rc      = object.printing_region(0).config();
    const double             nozzle  = object.print()->config().nozzle_diameter.get_at(0);
    const double             density = 0.01 * rc.sparse_infill_density.value;
    const double             spacing = density > 0. ? 2. * rc.sparse_infill_line_width.get_abs_value(nozzle) / density : 6.;
    Walker walker(bed, object.config().layer_height.value, spacing, make_tool_kinematics(object.print()->config())->rotary_count() > 0);
    struct Event { double z; int kind; size_t index; };
    std::vector<Event> events;
    for (size_t i = 0; i < support.size(); ++i)
        events.push_back({ support[i]->print_z, 0, i });
    for (size_t i = 0; i < layers.size(); ++i)
        events.push_back({ layers[i]->print_z, 1, i });
    std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) { return a.z < b.z || (a.z == b.z && a.kind < b.kind); });
    auto patches = [&](const std::vector<int> &ids, double z) {
        if (result == nullptr)
            return;
        for (int id : ids)
            if (id >= 0 && size_t(id) < result->patches.size())
                for (const ExtrusionEntityCollection &shell : result->patches[size_t(id)].shell_paths)
                    walker.entity(shell, z);
    };
    for (const Event &e : events) {
        if (e.kind == 0) {
            walker.entity(support[e.index]->support_fills, e.z);
            continue;
        }
        const Layer *layer = layers[e.index];
        patches(layer->conformal_patches_before, e.z);
        for (const LayerRegion *layerm : layer->regions()) {
            walker.entity(layerm->perimeters, e.z);
            walker.entity(layerm->fills, e.z);
        }
        patches(layer->conformal_patches_after, e.z);
        for (const LayerRegion *layerm : layer->regions())
            walker.raised(layerm->perimeters, e.z);
    }
    return walker.floating();
}

bool floats_past_bar(const Floating &print, const Floating &control)
{
    constexpr double Share = 1.05, Slack = 2.;
    constexpr double MaterialExcess = 10.;
    return print.part > Share * std::max(control.part, 0.) + Slack + MaterialExcess;
}

Floating planar_control_floating(const PrintObject &object, bool support)
{
    DynamicPrintConfig config = object.print()->full_print_config();
    config.apply(object.config(), true);
    config.apply(object.printing_region(0).config(), true);
    std::vector<std::pair<const char *, const char *>> control = { { "nonplanar_mode", "off" }, { "enable_support", "0" } };
    if (support)
        control = { { "nonplanar_mode", "off" },     { "enable_support", "1" },
                    { "support_type", "tree(auto)" }, { "support_style", "tree_slim" },
                    { "support_critical_regions_only", "0" } };
    Model        model;
    ModelObject *copy = model.add_object(*object.model_object());
    DynamicPrintConfig &copy_config = const_cast<DynamicPrintConfig &>(copy->config.get());
    for (const auto &[key, value] : control) {
        config.set_deserialize_strict(key, value);
        if (copy_config.has(key))
            copy_config.set_deserialize_strict(key, value);
    }
    copy_config.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonplanarMode>(NonplanarMode::Off));
    copy->clear_instances();
    copy->add_instance(*object.model_object()->instances.front());
    static thread_local std::pair<std::string, Floating> last;
    std::string key;
    for (const DynamicPrintConfig *c : std::initializer_list<const DynamicPrintConfig *>{ &config, &copy->config.get() })
        for (const std::string &opt : c->keys())
            key += opt + '=' + c->opt_serialize(opt) + '\n';
    for (const ModelVolume *volume : copy->volumes) {
        const indexed_triangle_set &its = volume->mesh().its;
        key.append(reinterpret_cast<const char *>(its.vertices.data()), its.vertices.size() * sizeof(stl_vertex));
        key.append(reinterpret_cast<const char *>(its.indices.data()), its.indices.size() * sizeof(stl_triangle_vertex_indices));
        key.append(reinterpret_cast<const char *>(volume->get_matrix().data()), 16 * sizeof(double));
        key += std::to_string(int(volume->type()));
    }
    key.append(reinterpret_cast<const char *>(copy->instances.front()->get_matrix().data()), 16 * sizeof(double));
    if (last.first == key)
        return last.second;
    Print print;
    try {
        print.apply(model, config);
        print.set_status_silent();
        print.process();
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(warning) << "Auto floating " << object.model_object()->name << ": the planar control failed: " << ex.what();
        return { -1., -1. };
    }
    if (print.objects().empty())
        return { -1., -1. };
    last = { std::move(key), floating_road(*print.objects().front(), nullptr) };
    return last.second;
}

}
