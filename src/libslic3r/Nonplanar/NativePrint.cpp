#include "NativePrint.hpp"

#include "ConformalShells.hpp"
#include "FieldChart.hpp"
#include "FieldOrder.hpp"
#include "HeadEnvelope.hpp"
#include "Multiplanar.hpp"
#include "NativeLayers.hpp"
#include "NpField.hpp"
#include "OneShotField.hpp"
#include "OrientationField.hpp"
#include "PatchTools.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ConformalEmit.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/format.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

std::mutex                                     g_override_mutex;
std::map<std::string, std::vector<ExPolygons>> g_overrides;

double tilt_budget(const ToolKinematics &kinematics)
{
    double tilt = 0.5 * M_PI;
    for (int s = 0; s < 72; ++s)
        tilt = std::min(tilt, kinematics.max_tilt(2. * M_PI * s / 72.));
    return tilt;
}

class NativeChart : public FieldChart
{
public:
    NativeChart(std::shared_ptr<const LayerField> field, double tilt_cap) : m_field(std::move(field)), m_tilt_cap(tilt_cap) {}
    Vec3d forward(const Vec3d &p) const override { return Vec3d(p.x(), p.y(), m_field->phi(p)); }
    bool  inverse(const Vec3d &q, Vec3d &p) const override
    {
        p = Vec3d(q.x(), q.y(), m_field->z_at(q.head<2>(), q.z()));
        return std::isfinite(p.z());
    }
    Vec3d orientation(const Vec3d &p) const override { return Native::tool_axis(*m_field, p, m_tilt_cap); }
    bool  given() const override { return true; }

private:
    std::shared_ptr<const LayerField> m_field;
    double                            m_tilt_cap;
};

void flatten(const ExtrusionEntity *ee, std::vector<const ExtrusionPath *> &out)
{
    if (const auto *c = dynamic_cast<const ExtrusionEntityCollection *>(ee)) {
        for (const ExtrusionEntity *e : c->entities)
            flatten(e, out);
    } else if (const auto *l = dynamic_cast<const ExtrusionLoop *>(ee)) {
        for (const ExtrusionPath &p : l->paths)
            out.push_back(&p);
    } else if (const auto *m = dynamic_cast<const ExtrusionMultiPath *>(ee)) {
        for (const ExtrusionPath &p : m->paths)
            out.push_back(&p);
    } else if (const auto *p = dynamic_cast<const ExtrusionPath *>(ee)) {
        out.push_back(p);
    }
}

}

bool native_requested()
{
    const char *v = std::getenv("NP_NATIVE");
    return v != nullptr && *v != 0 && *v != '0';
}

std::shared_ptr<const LayerField> native_field(PrintObject &object)
{
    const NonplanarMode mode = object.config().nonplanar_mode.value;
    if (!object.config().nonplanar_atom_toolpath.value.empty())
        return {};
    if (mode == NonplanarMode::OneShot) {
        const OneShotState *state = std::as_const(object).oneshot_state();
        if (state == nullptr || !state->field() || state->z_only)
            return {};
        return std::make_shared<const NpLayerField>(*state->solved);
    }
    const std::string &path = object.config().nonplanar_field_file.value;
    if (mode != NonplanarMode::Field || path.empty())
        return {};
    BoundingBoxf3 box;
    for (const stl_vertex &v : slicing_frame_mesh(object).vertices)
        box.merge(v.cast<double>());
    try {
        return std::make_shared<const NpLayerField>(load_npfield_for(path, box, plate_offset(object)));
    } catch (const RuntimeError &ex) {
        throw Slic3r::SlicingError(ex.what());
    }
}

NativeSliceOverride::NativeSliceOverride(std::string name, std::vector<ExPolygons> slices) : m_name(std::move(name))
{
    std::lock_guard<std::mutex> lock(g_override_mutex);
    g_overrides[m_name] = std::move(slices);
}

NativeSliceOverride::~NativeSliceOverride()
{
    std::lock_guard<std::mutex> lock(g_override_mutex);
    g_overrides.erase(m_name);
}

const std::vector<ExPolygons> *NativeSliceOverride::find(const std::string &name)
{
    std::lock_guard<std::mutex> lock(g_override_mutex);
    const auto                  it = g_overrides.find(name);
    return it == g_overrides.end() ? nullptr : &it->second;
}

double adaptive_level_ratio()
{
    static const double r = [] {
        const char *v = std::getenv("NP_NATIVE_ADAPTIVE");
        return v == nullptr ? 0. : std::clamp(std::atof(v), 1., 2.);
    }();
    return r;
}

std::vector<double> adaptive_levels(const indexed_triangle_set &mesh, const LayerField &field, double first, double top, double h, double t_max)
{
    constexpr double Bin = 0.02;
    const size_t     nb  = size_t(std::max(1., std::ceil((top - first) / Bin))) + 2;
    std::vector<double> allow(nb, t_max * h);
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d a = mesh.vertices[size_t(f[0])].cast<double>(), b = mesh.vertices[size_t(f[1])].cast<double>(),
                    c = mesh.vertices[size_t(f[2])].cast<double>();
        Vec3d n = (b - a).cross(c - a);
        if (n.squaredNorm() < 1e-18)
            continue;
        n.normalize();
        Vec3d g = field.gradient((a + b + c) / 3.);
        if (g.squaredNorm() < 1e-18)
            continue;
        g.normalize();
        const double ng = std::abs(n.dot(g));
        const double t  = h * (ng >= 0.5 ? 1. : ng <= 0.25 ? t_max : 1. + (t_max - 1.) * (0.5 - ng) / 0.25);
        const double pa = field.phi(a), pb = field.phi(b), pc = field.phi(c);
        const double lo = std::min({ pa, pb, pc }), hi = std::max({ pa, pb, pc });
        const size_t i0 = size_t(std::clamp((lo - first) / Bin, 0., double(nb - 1)));
        const size_t i1 = size_t(std::clamp((hi - first) / Bin, 0., double(nb - 1)));
        for (size_t i = i0; i <= i1; ++i)
            allow[i] = std::min(allow[i], t);
    }
    std::vector<double> tops{ first };
    double              prev = h;
    while (tops.back() < top - 1e-6) {
        const double phi = tops.back();
        double       t   = std::min(t_max * h, 1.15 * prev);
        for (int it = 0; it < 8; ++it) {
            double       least = t;
            const size_t i0    = size_t(std::clamp((phi - first) / Bin, 0., double(nb - 1)));
            const size_t i1    = size_t(std::clamp((phi + t - first) / Bin, 0., double(nb - 1)));
            for (size_t i = i0; i <= i1; ++i)
                least = std::min(least, allow[i]);
            if (least >= t - 1e-9)
                break;
            t = std::max(h, least);
        }
        if (phi + t > top - 0.5 * h)
            t = std::max(top - phi, 0.5 * h);
        tops.push_back(phi + t);
        prev = t;
    }
    BOOST_LOG_TRIVIAL(info) << "Native adaptive levels: " << tops.size() << " levels to " << tops.back() << " (uniform " << h << ": "
                            << size_t(std::ceil((top - first) / h - 1e-6) + 1.) << ")";
    return tops;
}

void build_native_patch(PrintObject &object, const std::shared_ptr<const LayerField> &field, ConformalResult &result)
{
    if (!field || object.layers().empty())
        return;
    const auto   t0      = std::chrono::steady_clock::now();
    const size_t host_id = object.layers().size() - 1;
    Layer       *host    = object.layers()[host_id];
    const double raft    = host->print_z - (host->slice_z + 0.5 * host->height);
    const double first   = object.print()->config().initial_layer_print_height.value;
    const double h       = object.config().layer_height.value;
    const std::unique_ptr<ToolKinematics> kinematics = make_tool_kinematics(object.print()->config());
    if (!kinematics)
        return;
    const double tilt_cap = std::max(0., tilt_budget(*kinematics) - Geometry::deg2rad(5.));

    const indexed_triangle_set mesh = slicing_frame_mesh(object);
    double                     top  = 0.;
    for (const stl_vertex &v : mesh.vertices)
        top = std::max(top, field->phi(v.cast<double>()));
    std::vector<double> tops, mids;
    if (const double t_max = adaptive_level_ratio(); t_max > 1.)
        tops = adaptive_levels(mesh, *field, first, top, h, t_max);
    else {
        const size_t K0 = size_t(std::max(1., std::ceil((top - first) / h - 1e-6) + 1.));
        for (size_t k = 0; k < K0; ++k)
            tops.push_back(first + double(k) * h);
    }
    const size_t K = tops.size();
    mids.resize(K);
    for (size_t k = 0; k < K; ++k)
        mids[k] = k == 0 ? 0.5 * first : 0.5 * (tops[k - 1] + tops[k]);
    Native::Params np;
    np.fixed_head    = true;
    np.mesh_edge_max = 0.5;
    const std::vector<Native::Layer> layers = Native::extract_layers(mesh, *field, mids, np);
    std::vector<ExPolygons>          domains(K);
    BoundingBox                      bbox;
    for (size_t k = 0; k < K; ++k)
        if (!layers[k].pieces.empty()) {
            domains[k] = layers[k].pieces.front().domain;
            bbox.merge(get_extents(domains[k]));
        }
    if (!bbox.defined)
        return;
    throw_if_canceled(object);

    const Point            c_scaled = bbox.center();
    const Vec2d            c        = unscale(c_scaled);
    std::vector<ExPolygons> shifted = domains;
    for (ExPolygons &d : shifted)
        for (ExPolygon &e : d)
            e.translate(-c_scaled.x(), -c_scaled.y());
    static std::atomic<int> serial { 0 };
    const std::string       name = object.model_object()->name + " native#" + std::to_string(serial++);
    DynamicPrintConfig      config = chunk_print_config(object);
    config.set_deserialize_strict("initial_layer_print_height", object.print()->config().opt_serialize("initial_layer_print_height"));
    config.set_deserialize_strict("ironing_type", "no ironing");
    for (const std::string &key : object.config().keys())
        if ((key.rfind("support_", 0) == 0 || key.rfind("tree_support", 0) == 0 || key == "enable_support") && config.has(key))
            config.set_key_value(key, object.config().option(key)->clone());
    config.set_deserialize_strict("independent_support_layer_height", "0");
    const double            height = first + double(K - 1) * h;
    TriangleMesh            box    = make_cube(unscale<double>(bbox.size().x()), unscale<double>(bbox.size().y()), height);
    box.translate(float(unscale<double>(bbox.min.x())), float(unscale<double>(bbox.min.y())), 0.f);
    Model        model;
    ModelObject *model_object = model.add_object();
    model_object->name        = name;
    model_object->add_volume(std::move(box));
    model_object->add_instance();
    const NativeSliceOverride registered(name, std::move(shifted));
    Print                     print;
    const NestedFieldSlice    nested;
    try {
        print.apply(model, config);
        const PrintBase *outer = object.print();
        print.set_status_callback([&print, outer](const PrintBase::SlicingStatus &) {
            if (outer->canceled())
                print.cancel();
        });
        print.process();
    } catch (const CanceledException &) {
        throw;
    } catch (const SlicingErrors &errors) {
        throw Slic3r::SlicingError(std::string(_u8L("Native curved layers: slicing failed: ")) + print.slicing_errors_message(errors));
    } catch (const std::exception &ex) {
        throw Slic3r::SlicingError(std::string(_u8L("Native curved layers: slicing failed: ")) + ex.what());
    }
    throw_if_canceled(object);
    if (print.objects().empty())
        return;
    const PrintObject &sliced = *print.objects().front();
    const Vec2d        center = unscale(sliced.center_offset());
    if ((center - c).norm() > 1e-3)
        BOOST_LOG_TRIVIAL(warning) << "Native layers " << object.model_object()->name << ": nested centre " << center.transpose()
                                   << " against the areas' " << c.transpose();
    const size_t n = std::min(K, sliced.layers().size());
    if (sliced.layers().size() != K)
        BOOST_LOG_TRIVIAL(warning) << "Native layers " << object.model_object()->name << ": " << sliced.layers().size()
                                   << " nested layers for " << K << " field layers";

    std::vector<FieldRoad>  roads;
    std::vector<ExPolygons> internal;
    Native::Piece           over_xy;
    size_t                  points = 0;
    std::vector<std::vector<const ExtrusionPath *>> support(n);
    size_t                                          support_paths = 0;
    for (const SupportLayer *layer : sliced.support_layers()) {
        const long k = std::lround((layer->print_z - first) / h);
        if (k >= 1 && size_t(k) < n) {
            flatten(&layer->support_fills, support[size_t(k)]);
            support_paths += support[size_t(k)].size();
        }
    }
    for (size_t k = 1; k < n; ++k) {
        const Layer &layer = *sliced.layers()[k];
        internal.push_back(domains[k]);
        std::vector<const ExtrusionPath *> paths = support[k];
        for (const LayerRegion *region : layer.regions()) {
            flatten(&region->perimeters, paths);
            flatten(&region->thin_fills, paths);
        }
        for (const LayerRegion *region : layer.regions())
            flatten(&region->fills, paths);
        for (const ExtrusionPath *path : paths) {
            if (path->polyline.points.size() < 2)
                continue;
            Polyline road;
            for (const Point3 &p : path->polyline.points)
                road.points.emplace_back(p.x() + c_scaled.x(), p.y() + c_scaled.y());
            std::vector<Vec3d> tips = Native::lift_polyline(over_xy, road, *field, tops[k], 0.02);
            if (tips.size() < 2)
                continue;
            for (Vec3d &q : tips)
                q.z() = std::max(q.z(), first + 0.5 * h);
            const size_t       m = tips.size();
            std::vector<float> heights(m), widths(m);
            std::vector<Vec3f> axes(m);
            Polyline3          polyline;
            static const bool  upright_support = [] { const char *v = std::getenv("NP_NATIVE_SUPPORT_UPRIGHT"); return !v || std::atoi(v) != 0; }();
            const bool         upright         = upright_support && is_support(path->role());
            for (size_t i = 0; i < m; ++i) {
                const Vec3d a = upright ? Vec3d::UnitZ() : Native::tool_axis(*field, tips[i], tilt_cap);
                axes[i]       = a.cast<float>();
                heights[i]    = float(std::clamp(Native::bead_height(*field, tips[i], tops[k - 1], tops[k], a), 0.5 * h, 1.6 * h));
                const Vec3d t = tips[std::min(i + 1, m - 1)] - tips[i > 0 ? i - 1 : 0];
                double      fac = 1.;
                const Vec3d g   = field->gradient(tips[i]);
                if (t.head<2>().squaredNorm() > 1e-12 && g.z() > 1e-9) {
                    const Vec2d td    = t.head<2>().normalized();
                    const Vec2d l(-td.y(), td.x());
                    const Vec2d slope = -g.head<2>() / g.z();
                    fac               = std::sqrt(1. + std::pow(slope.dot(l), 2));
                }
                widths[i] = float(path->width * std::clamp(fac, 0.6, 1.6));
                polyline.points.push_back(Point3::new_scale(tips[i].x(), tips[i].y(), tips[i].z() + raft - host->print_z));
            }
            const double             rate  = Geometry::deg2rad(is_infill(path->role()) && path->role() != erTopSolidInfill ? 3. : 5.);
            const std::vector<Vec3f> exact = axes;
            for (int pass = 0; pass < 2; ++pass)
                for (size_t q = 1; q < m; ++q) {
                    const size_t i = pass == 0 ? q : m - 1 - q, j = pass == 0 ? i - 1 : i + 1;
                    const Vec3d  a = axes[j].cast<double>(), b = axes[i].cast<double>();
                    const double allowed = rate * std::max((tips[i] - tips[j]).norm(), 1e-3);
                    const double angle   = std::acos(std::clamp(a.dot(b), -1., 1.));
                    if (angle > allowed)
                        axes[i] = (Eigen::Quaterniond::Identity().slerp(allowed / angle, Eigen::Quaterniond::FromTwoVectors(a, b)) * a).cast<float>();
                }
            const double max_lag = Geometry::deg2rad(10.);
            for (size_t i = 0; i < m; ++i) {
                const Vec3d  e = exact[i].cast<double>(), a = axes[i].cast<double>();
                const double off = std::acos(std::clamp(e.dot(a), -1., 1.));
                if (off > max_lag)
                    axes[i] = (Eigen::Quaterniond::Identity().slerp(1. - max_lag / off, Eigen::Quaterniond::FromTwoVectors(a, e)) * a).cast<float>();
            }
            FieldRoad r;
            r.path             = std::make_unique<ExtrusionPathConformal>(std::move(polyline), *path, std::move(heights), std::move(axes));
            r.path->bead_width = std::move(widths);
            r.deformed.reserve(m);
            for (const Vec3d &p : tips)
                r.deformed.emplace_back(p.x(), p.y(), mids[k]);
            r.tip   = std::move(tips);
            r.layer = k - 1;
            points += m;
            roads.push_back(std::move(r));
        }
    }
    FieldOrderParams op;
    op.head       = resolve_head_envelope(object.print()->config());
    op.kinematics = kinematics.get();
    op.max_tilt   = std::max(0., tilt_budget(*kinematics) - Geometry::deg2rad(1.));
    op.lean_max   = 0.;
    op.layer_top.assign(tops.begin() + 1, tops.begin() + std::ptrdiff_t(n));
    op.layer_height.resize(n - 1);
    for (size_t k = 1; k < n; ++k)
        op.layer_height[k - 1] = tops[k] - tops[k - 1];
    const char *dump_prefix = std::getenv("NP_NATIVE_DUMP_ROADS");
    auto dump = [&](const char *stage, const auto &visit) {
        if (dump_prefix == nullptr || *dump_prefix == '\0')
            return;
        const std::string path = std::string(dump_prefix) + "." +
                                 std::to_string(object.model_object()->id().id) + "." + stage + ".tsv";
        boost::nowide::ofstream out(path);
        if (!out)
            throw std::runtime_error("Cannot open NP_NATIVE_DUMP_ROADS file: " + path);
        out << std::setprecision(17)
            << "# native-road-v1 slicing frame, mm; ordered axes precede G-code settle/RTCP\n"
            << "group\troad\tpoint\tlayer\tphi_top\trole\tbase_width\tx\ty\tz"
               "\tax\tay\taz\tused_ax\tused_ay\tused_az\twidth\theight\tgx\tgy\tgz\n";
        auto write = [&](const ExtrusionPathConformal &path, size_t group, size_t road, size_t layer) {
            const bool upright = std::all_of(path.tool_axis.begin(), path.tool_axis.end(), [](const Vec3f &a) {
                return a.cast<double>().normalized().z() >= std::cos(Geometry::deg2rad(UprightRoadDeg));
            });
            for (size_t i = 0; i < path.polyline.points.size(); ++i) {
                const Point3 &point = path.polyline.points[i];
                const Vec3d p(unscale<double>(point.x()), unscale<double>(point.y()),
                              unscale<double>(point.z()) + host->print_z - raft);
                const Vec3d axis = i < path.tool_axis.size() ? Vec3d(path.tool_axis[i].cast<double>()) : Vec3d(Vec3d::UnitZ());
                const Vec3d used = upright ? Vec3d::UnitZ() : axis;
                const Vec3d g = field->gradient(p);
                const double width = i < path.bead_width.size() ? path.bead_width[i] : path.width;
                const double height = i < path.bead_height.size() ? path.bead_height[i] : path.height;
                out << group << '\t' << road << '\t' << i << '\t' << layer << '\t' << op.layer_top.at(layer)
                    << '\t' << int(path.role()) << '\t' << path.width
                    << '\t' << p.x() << '\t' << p.y() << '\t' << p.z()
                    << '\t' << axis.x() << '\t' << axis.y() << '\t' << axis.z()
                    << '\t' << used.x() << '\t' << used.y() << '\t' << used.z()
                    << '\t' << width << '\t' << height << '\t' << g.x() << '\t' << g.y() << '\t' << g.z() << '\n';
            }
        };
        visit(write);
        out.flush();
        if (!out)
            throw std::runtime_error("Cannot write NP_NATIVE_DUMP_ROADS file: " + path);
    };
    dump("raw", [&](const auto &write) {
        for (size_t ri = 0; ri < roads.size(); ++ri)
            if (roads[ri].path)
                write(*roads[ri].path, roads[ri].layer, ri, roads[ri].layer);
    });
    const size_t n_roads = roads.size();
    FieldOrder   order   = layer_order_field_roads(roads, op);

    dump("ordered", [&](const auto &write) {
        size_t ri = 0;
        for (size_t gi = 0; gi < order.groups.size(); ++gi)
            for (const ExtrusionEntity *entity : order.groups[gi].entities)
                write(*static_cast<const ExtrusionPathConformal *>(entity), gi, ri++, order.group_layers.at(gi));
    });

    ConformalPatch patch;
    patch.id           = int(result.patches.size());
    patch.kind         = PatchKind::Field;
    patch.shell_height = h;
    patch.host_layer   = host_id;
    patch.region_id    = 0;
    patch.field        = std::make_shared<const NativeChart>(field, tilt_cap);
    patch.z_min        = std::numeric_limits<double>::infinity();
    patch.z_max        = -std::numeric_limits<double>::infinity();
    for (size_t g = 0; g < order.groups.size(); ++g) {
        ExtrusionEntityCollection &group = order.groups[g];
        if (g < order.group_layers.size() && order.group_layers[g] < internal.size())
            patch.shell_internal.push_back(internal[order.group_layers[g]]);
        for (const ExtrusionEntity *ee : group.entities)
            for (const Point3 &p : static_cast<const ExtrusionPath *>(ee)->polyline.points) {
                const double z = unscale<double>(p.z()) + host->print_z;
                patch.z_min    = std::min(patch.z_min, z);
                patch.z_max    = std::max(patch.z_max, z);
            }
        patch.shell_paths.push_back(std::move(group));
    }
    object.set_native_summary(Slic3r::format("%1% layers, %2% roads, %3% support paths, tilt cap %4$.0f deg", K, n_roads, support_paths,
                                             Geometry::rad2deg(tilt_cap)));
    BOOST_LOG_TRIVIAL(info) << "Native layers " << object.model_object()->name << ": " << K << " layers to level " << top << ", "
                            << n_roads << " roads (" << support_paths << " support paths), " << points << " points, " << patch.shell_paths.size() << " groups, tilt cap "
                            << Geometry::rad2deg(tilt_cap) << " deg, "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s";
    for (size_t layer_id = 1; layer_id < object.layers().size(); ++layer_id) {
        Layer *layer = object.layers()[layer_id];
        layer->lslices.clear();
        layer->lslices_extrudable.clear();
        layer->lslices_bboxes.clear();
    }
    for (size_t layer_id = 1; layer_id < object.layers().size(); ++layer_id)
        for (LayerRegion *region : object.layers()[layer_id]->regions()) {
            region->slices.set(ExPolygons(), stInternal);
            region->perimeters.clear();
            region->thin_fills.clear();
            region->fills.clear();
            region->fill_surfaces.clear();
            region->fill_expolygons.clear();
        }
    if (patch.shell_paths.empty())
        return;
    for (size_t layer_id = 1; layer_id < object.layers().size(); ++layer_id)
        object.layers()[layer_id]->conformal_covered = true;
    patch.shells = int(patch.shell_paths.size());
    host->conformal_patches_after.push_back(patch.id);
    result.patches.push_back(std::move(patch));
}

}
