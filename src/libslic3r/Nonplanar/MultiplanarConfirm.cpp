#include "MultiplanarConfirm.hpp"
#include "RotaryRouter.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <cstddef>
#include <math.h>

#include "AutoMode.hpp"
#include "Multiplanar.hpp"
#include "PatchTools.hpp"
#include "Rests.hpp"

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/ClearanceRepair.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

double steep_top_share(const indexed_triangle_set &mesh)
{
    double up = 0., steep = 0.;
    for (const stl_triangle_vertex_indices &t : mesh.indices) {
        const Vec3d  n = (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cast<double>().cross((mesh.vertices[t[2]] - mesh.vertices[t[0]]).cast<double>());
        const double a = n.norm();
        if (a <= 0. || n.z() <= std::cos(80. * M_PI / 180.) * a)
            continue;
        up += a;
        if (n.z() < std::cos(45. * M_PI / 180.) * a)
            steep += a;
    }
    return up > 0. ? steep / up : 0.;
}

double thin_top_share(const indexed_triangle_set &mesh, double thickness)
{
    if (mesh.indices.empty() || thickness <= 0.)
        return 0.;
    const AABBMesh tree(mesh);
    const size_t   stride = std::max<size_t>(1, mesh.indices.size() / 4000);
    double         up = 0., thin = 0.;
    for (size_t i = 0; i < mesh.indices.size(); i += stride) {
        const stl_triangle_vertex_indices &t = mesh.indices[i];
        const Vec3d a = mesh.vertices[t[0]].cast<double>(), b = mesh.vertices[t[1]].cast<double>(), c = mesh.vertices[t[2]].cast<double>();
        Vec3d       n = (b - a).cross(c - a);
        const double area = n.norm();
        if (area <= 0. || n.z() <= std::cos(80. * M_PI / 180.) * area)
            continue;
        n /= area;
        up += area;
        const Vec3d centroid = (a + b + c) / 3.;
        const auto  hit      = tree.query_ray_hit(centroid - 1e-3 * n, -n);
        if (hit.is_hit() && hit.distance() + 1e-3 <= thickness)
            thin += area;
    }
    return up > 0. ? thin / up : 0.;
}

namespace {
thread_local MeasureClock::time_point measure_deadline = MeasureClock::time_point::max();
}

MeasureDeadline::MeasureDeadline(MeasureClock::time_point at) : m_previous(measure_deadline) { measure_deadline = std::min(measure_deadline, at); }
MeasureDeadline::~MeasureDeadline() { measure_deadline = m_previous; }

bool measure_deadline_passed() { return measure_deadline != MeasureClock::time_point::max() && MeasureClock::now() >= measure_deadline; }

namespace {
thread_local double *measure_work_left = nullptr;
}

MeasureWork::MeasureWork(double *left) : m_previous(measure_work_left) { measure_work_left = left; }
MeasureWork::~MeasureWork() { measure_work_left = m_previous; }

bool measure_work_spent() { return measure_work_left != nullptr && *measure_work_left <= 0.; }
bool measure_work_active() { return measure_work_left != nullptr; }

DeadlineWatch::DeadlineWatch(PrintBase &print)
{
    if (measure_deadline == MeasureClock::time_point::max())
        return;
    m_watch = std::thread([this, &print, at = measure_deadline] {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_done.wait_until(lock, at, [this] { return m_finished; })) {
            BOOST_LOG_TRIVIAL(error) << "One-shot measurement: past the wall-clock safety net, the private slice is canceled (unmeasured): "
                                    "the work budget should have bound first";
            print.cancel();
        }
    });
}

DeadlineWatch::~DeadlineWatch()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
    }
    m_done.notify_all();
    if (m_watch.joinable())
        m_watch.join();
}

namespace {

thread_local unsigned rotary_measurement_depth = 0;
thread_local NonplanarMode rotary_measurement_mode = NonplanarMode::Off;
thread_local double rotary_measurement_time_cap = std::numeric_limits<double>::infinity();
thread_local bool rotary_measurement_unchecked = false;
struct RotaryMeasurement
{
    const NonplanarMode previous = rotary_measurement_mode;
    explicit RotaryMeasurement(NonplanarMode mode) { ++rotary_measurement_depth; rotary_measurement_mode = mode; }
    ~RotaryMeasurement() { rotary_measurement_mode = previous; --rotary_measurement_depth; }
};

struct Bead
{
    Vec3d a, b, axis;
    double width, height;
};

void append_beads(const GCodeProcessorResult &result, const PrintConfig &config, std::vector<Bead> &beads)
{
    const auto kinematics = make_tool_kinematics(config);
    const bool rotary     = kinematics->rotary_count() > 0;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &m = result.moves[i];
        const ExtrusionRole r = m.extrusion_role;
        if (m.type != EMoveType::Extrude || r == erSkirt || r == erBrim || r == erWipeTower || r == erCustom || r == erNone ||
            r == erSupportMaterial || r == erSupportMaterialInterface || r == erSupportTransition)
            continue;
        beads.push_back({ result.moves[i - 1].position.cast<double>(), m.position.cast<double>(),
                          rotary && result.has_joints() ? kinematics->forward(result.joints[i]).axis : Vec3d::UnitZ(), double(m.width), double(m.height) });
    }
}

double measured_slice(const PrintObject &object, NonplanarMode mode, std::vector<Bead> &beads,
                      const std::vector<std::pair<std::string, std::string>> &overrides = {}, const std::string &keep_as = {},
                      bool verify = true, MeasuredPrint *measured = nullptr)
{
    DynamicPrintConfig config = object.print()->full_print_config();
    config.apply(object.config(), true);
    config.apply(object.printing_region(0).config(), true);
    config.set_deserialize_strict("nonplanar_mode", mode == NonplanarMode::Auto ? "auto" :
                                  mode == NonplanarMode::Multiplanar ? "multiplanar" :
                                  mode == NonplanarMode::OneShot ? "oneshot" : "off");
    if (mode == NonplanarMode::Multiplanar) {
        config.set_deserialize_strict("nonplanar_top_shells", "0");
        config.set_deserialize_strict("nonplanar_bottom_shells", "0");
    }
    Model        model;
    ModelObject *copy = model.add_object(*object.model_object());
    DynamicPrintConfig &copy_config = const_cast<DynamicPrintConfig &>(copy->config.get());
    copy_config.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonplanarMode>(mode));
    for (const auto &[key, value] : overrides) {
        config.set_deserialize_strict(key, value);
        if (copy_config.has(key))
            copy_config.set_deserialize_strict(key, value);
    }
    copy->clear_instances();
    copy->add_instance(*object.model_object()->instances.front());
    static std::mutex                                                          planar_mutex;
    static std::map<std::string, std::pair<std::pair<double, std::vector<Bead>>, size_t>> planar_memo;
    static size_t                                                              planar_tick = 0;
    constexpr size_t                                                           PlanarMemo  = 32;
    std::string key;
    if (mode == NonplanarMode::Off) {
        auto planar_reads = [](const std::string &opt) { return opt.rfind("nonplanar_", 0) != 0 || opt == "nonplanar_kinematics"; };
        for (const std::string &opt : config.keys())
            if (planar_reads(opt))
                key += opt + '=' + config.opt_serialize(opt) + '\n';
        for (const std::string &opt : copy->config.get().keys())
            if (planar_reads(opt))
                key += opt + '=' + copy->config.get().opt_serialize(opt) + '\n';
        for (const ModelVolume *volume : copy->volumes) {
            const indexed_triangle_set &its = volume->mesh().its;
            key.append(reinterpret_cast<const char *>(its.vertices.data()), its.vertices.size() * sizeof(stl_vertex));
            key.append(reinterpret_cast<const char *>(its.indices.data()), its.indices.size() * sizeof(stl_triangle_vertex_indices));
            const Transform3d &m = volume->get_matrix();
            key.append(reinterpret_cast<const char *>(m.data()), 16 * sizeof(double));
            key += std::to_string(int(volume->type()));
        }
        const Transform3d &m = copy->instances.front()->get_matrix();
        key.append(reinterpret_cast<const char *>(m.data()), 16 * sizeof(double));
        const std::lock_guard<std::mutex> lock(planar_mutex);
        if (const auto it = planar_memo.find(key); it != planar_memo.end()) {
            it->second.second = ++planar_tick;
            beads             = it->second.first.second;
            return it->second.first.first;
        }
    }
    if (measure_work_spent()) {
        BOOST_LOG_TRIVIAL(info) << "One-shot measurement: its work budget is spent, a private slice is not started (unmeasured)";
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (measure_deadline_passed()) {
        BOOST_LOG_TRIVIAL(error) << "One-shot measurement: past the wall-clock safety net, a private slice is not started (unmeasured): "
                                    "the work budget should have bound first";
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto           t_slice = MeasureClock::now();
    Print                print;
    GCodeProcessorResult result;
    const boost::filesystem::path gcode = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("np-mf-%%%%-%%%%.gcode");
    boost::system::error_code     ec;
    try {
        const DeadlineWatch watch(print);
        const RotaryMeasurement measurement(measured != nullptr && measured->as_printed ? NonplanarMode::Off : mode);
        const bool              unchecked = rotary_measurement_unchecked;
        rotary_measurement_unchecked      = !verify;
        struct Restore
        {
            bool value;
            ~Restore() { rotary_measurement_unchecked = value; }
        } restore { unchecked };
        print.apply(model, config);
        print.set_status_silent();
        print.process();
        print.export_gcode(gcode.string(), &result, nullptr);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(warning) << "Multiplanar check: a measured slice failed: " << ex.what();
        boost::filesystem::remove(gcode, ec);
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (measure_deadline_passed()) {
        boost::filesystem::remove(gcode, ec);
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (measured != nullptr && !print.objects().empty()) {
        const PrintObject &printed = *print.objects().front();
        measured->floating         = floating_road(result, printed);
        measured->support          = std::any_of(printed.support_layers().begin(), printed.support_layers().end(),
                                                 [](const SupportLayer *layer) { return !layer->support_fills.empty(); });
        if (measured->count_contacts && result.nonplanar)
            measured->contacts = ClearanceRepair::curved_contacts(print, result);
        {
            const double d    = print.config().filament_diameter.get_at(0);
            double       fil  = 0.;
            for (const auto &mv : result.moves)
                if (mv.type == EMoveType::Extrude && is_support(mv.extrusion_role) && mv.delta_extruder > 0.f)
                    fil += double(mv.delta_extruder);
            measured->support_mm3 = fil * M_PI * 0.25 * d * d;
        }
    }
    if (!keep_as.empty())
        boost::filesystem::copy_file(gcode, keep_as, boost::filesystem::copy_options::overwrite_existing, ec);
    boost::filesystem::remove(gcode, ec);
    append_beads(result, print.config(), beads);
    const double time = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    if (measure_work_left != nullptr && std::isfinite(time))
        *measure_work_left -= time;
    BOOST_LOG_TRIVIAL(info) << "One-shot measurement: " << (mode == NonplanarMode::Off ? "off" : mode == NonplanarMode::Auto ? "auto" :
                                                         mode == NonplanarMode::OneShot ? "oneshot" : "multiplanar")
                            << " slice, estimated " << time << " s, " << std::chrono::duration<double>(MeasureClock::now() - t_slice).count()
                            << " s wall, work left " << (measure_work_left != nullptr ? *measure_work_left : -1.);
    if (mode == NonplanarMode::Off) {
        const std::lock_guard<std::mutex> lock(planar_mutex);
        if (planar_memo.size() >= PlanarMemo) {
            std::vector<std::pair<size_t, std::string>> age;
            for (const auto &[k, v] : planar_memo)
                age.emplace_back(v.second, k);
            std::sort(age.begin(), age.end());
            for (size_t i = 0; i < age.size() / 2; ++i)
                planar_memo.erase(age[i].second);
        }
        planar_memo[std::move(key)] = { { time, beads }, ++planar_tick };
    }
    return time;
}

double normal_rms(indexed_triangle_set mesh, const std::vector<Bead> &beads)
{
    using Key = int64_t;
    auto key = [](const Vec3d &p, double cell) {
        const auto c = [cell](double x) { return Key(std::floor(x / cell)) & 0x1FFFFF; };
        return (c(p.x()) << 42) | (c(p.y()) << 21) | c(p.z());
    };
    if (beads.empty() || mesh.indices.empty())
        return std::numeric_limits<double>::quiet_NaN();
    BoundingBoxf3 mbox, bbox;
    for (const stl_vertex &v : mesh.vertices)
        mbox.merge(v.cast<double>());
    for (const Bead &b : beads) {
        bbox.merge(Vec3d(b.a.x(), b.a.y(), 0.));
        bbox.merge(Vec3d(b.b.x(), b.b.y(), 0.));
    }
    const Vec3d shift(bbox.center().x() - mbox.center().x(), bbox.center().y() - mbox.center().y(), -mbox.min.z());
    for (stl_vertex &v : mesh.vertices)
        v += shift.cast<float>();

    std::vector<Vec3d> normals(mesh.indices.size());
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        const Vec3d a = mesh.vertices[mesh.indices[i][0]].cast<double>();
        const Vec3d n = (mesh.vertices[mesh.indices[i][1]].cast<double>() - a).cross(mesh.vertices[mesh.indices[i][2]].cast<double>() - a);
        normals[i]    = n;
    }
    const std::vector<Vec3i32>                   neighbours = its_face_neighbors(mesh);
    std::unordered_map<Key, std::vector<Vec3d>> sharp;
    for (size_t i = 0; i < mesh.indices.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const int j = neighbours[i][k];
            if (j < int(i) || normals[i].norm() <= 0. || normals[size_t(j)].norm() <= 0. ||
                normals[i].normalized().dot(normals[size_t(j)].normalized()) >= std::cos(30. * M_PI / 180.))
                continue;
            const Vec3d a = mesh.vertices[mesh.indices[i][k]].cast<double>(), b = mesh.vertices[mesh.indices[i][(k + 1) % 3]].cast<double>();
            const int   n = std::max(1, int(std::ceil((b - a).norm() / 0.25)));
            for (int s = 0; s <= n; ++s) {
                const Vec3d p = a + (double(s) / n) * (b - a);
                sharp[key(p, 1.)].push_back(p);
            }
        }
    auto near_sharp = [&](const Vec3d &p) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    if (auto it = sharp.find(key(p + Vec3d(dx, dy, dz), 1.)); it != sharp.end())
                        for (const Vec3d &q : it->second)
                            if ((q - p).squaredNorm() <= 1.)
                                return true;
        return false;
    };
    std::vector<std::pair<Vec3d, Vec3d>>   samples;
    std::mt19937                           rng(1);
    std::uniform_real_distribution<double> u01(0., 1.);
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        const double area2 = normals[i].norm();
        if (area2 <= 0. || normals[i].z() <= std::cos(75. * M_PI / 180.) * area2)
            continue;
        const Vec3d n = normals[i] / area2;
        const Vec3d a = mesh.vertices[mesh.indices[i][0]].cast<double>(), b = mesh.vertices[mesh.indices[i][1]].cast<double>(),
                    c = mesh.vertices[mesh.indices[i][2]].cast<double>();
        const int count = std::min(2000, int(std::ceil(0.5 * area2 / 0.09)));
        for (int k = 0; k < count; ++k) {
            double r1 = u01(rng), r2 = u01(rng);
            if (r1 + r2 > 1.)
                r1 = 1. - r1, r2 = 1. - r2;
            const Vec3d p = a + r1 * (b - a) + r2 * (c - a);
            if (! near_sharp(p))
                samples.emplace_back(p, n);
        }
    }
    if (samples.size() > 20000) {
        std::vector<std::pair<Vec3d, Vec3d>> kept;
        const double                         step = double(samples.size()) / 20000.;
        for (double k = 0.; k < double(samples.size()); k += step)
            kept.push_back(samples[size_t(k)]);
        samples.swap(kept);
    }
    if (samples.empty())
        return std::numeric_limits<double>::quiet_NaN();
    std::unordered_map<Key, std::vector<size_t>> near_cells;
    for (size_t i = 0; i < samples.size(); ++i)
        near_cells[key(samples[i].first, 1.2)].push_back(i);
    auto near_surface = [&](const Vec3d &p) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    if (auto it = near_cells.find(key(p + 1.2 * Vec3d(dx, dy, dz), 1.2)); it != near_cells.end())
                        for (size_t i : it->second)
                            if ((samples[i].first - p).squaredNorm() <= 1.44)
                                return true;
        return false;
    };
    constexpr double                              Cell = 0.5;
    std::unordered_map<Key, std::vector<Vec3d>> material;
    for (const Bead &bead : beads) {
        const Vec3d  d   = bead.b - bead.a;
        const double len = d.norm();
        if (len < EPSILON)
            continue;
        Vec3d side = bead.axis.cross(d / len);
        if (side.norm() < 1e-9)
            continue;
        side.normalize();
        const double h     = bead.height > 0. ? bead.height : 0.2;
        const int    steps = std::max(1, int(std::ceil(len / 0.1)));
        for (int s = 0; s < steps; ++s) {
            const Vec3d p = bead.a + ((s + 0.5) / steps) * d;
            if (! near_surface(p))
                continue;
            for (double u : { -1., -0.5, 0., 0.5, 1. })
                for (double v : { 0., 0.5, 1. }) {
                    const Vec3d q = p + 0.5 * u * bead.width * side - v * h * bead.axis;
                    material[key(q, Cell)].push_back(q);
                }
        }
    }
    double sum   = 0.;
    size_t count = 0;
    for (const auto &[s, n] : samples) {
        double best = -std::numeric_limits<double>::infinity();
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 2; ++dy)
                for (int dz = -2; dz <= 2; ++dz) {
                    auto it = material.find(key(s + Cell * Vec3d(dx, dy, dz), Cell));
                    if (it == material.end())
                        continue;
                    for (const Vec3d &q : it->second) {
                        const Vec3d  rel   = q - s;
                        const double along = rel.dot(n);
                        if ((rel - along * n).squaredNorm() <= 0.15 * 0.15 && rel.squaredNorm() <= 0.81)
                            best = std::max(best, along);
                    }
                }
        if (std::isfinite(best) && std::abs(best) < 0.5) {
            sum += best * best;
            ++count;
        }
    }
    return count > 0 ? std::sqrt(sum / double(count)) : std::numeric_limits<double>::quiet_NaN();
}

double rounded_normal_rms(const indexed_triangle_set &mesh, const std::vector<Bead> &beads)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    if (mesh.indices.empty() || beads.empty())
        return missing;
    using Key = std::uint64_t;
    auto key = [](const Vec3d &p, double cell) {
        const auto coordinate = [cell](double x) { return std::uint64_t(std::int64_t(std::floor(x / cell))) & 0x1FFFFF; };
        return (coordinate(p.x()) << 42) | (coordinate(p.y()) << 21) | coordinate(p.z());
    };
    struct Facet { Vec3d center, normal; double area2; };
    std::vector<Facet> facets;
    std::unordered_map<Key, std::vector<size_t>> faces;
    facets.reserve(mesh.indices.size());
    for (const auto &tri : mesh.indices) {
        const Vec3d a = mesh.vertices[tri[0]].cast<double>(), b = mesh.vertices[tri[1]].cast<double>(), c = mesh.vertices[tri[2]].cast<double>();
        const Vec3d cross = (b - a).cross(c - a);
        const double area2 = cross.norm();
        Vec3d normal = Vec3d::Zero();
        if (area2 > 0.)
            normal = cross / area2;
        facets.push_back({ (a + b + c) / 3., normal, area2 });
        faces[key(facets.back().center, 1.)].push_back(facets.size() - 1);
    }
    const double sharp_cos = std::cos(30. * M_PI / 180.);
    auto near_edge = [&](const Vec3d &p, const Vec3d &normal) {
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    if (auto it = faces.find(key(p + Vec3d(x, y, z), 1.)); it != faces.end())
                        for (size_t i : it->second)
                            if ((facets[i].center - p).squaredNorm() <= 1. && facets[i].normal.dot(normal) < sharp_cos)
                                return true;
        return false;
    };
    std::vector<std::pair<Vec3d, Vec3d>> samples;
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> unit(0., 1.);
    const double up_cos = std::cos(75. * M_PI / 180.);
    for (size_t i = 0; i < facets.size(); ++i) {
        if (facets[i].area2 <= 2e-6 || facets[i].normal.z() <= up_cos)
            continue;
        const auto &tri = mesh.indices[i];
        const Vec3d a = mesh.vertices[tri[0]].cast<double>(), b = mesh.vertices[tri[1]].cast<double>(), c = mesh.vertices[tri[2]].cast<double>();
        const int count = int(std::min(2000., std::ceil(0.5 * facets[i].area2 / 0.09)));
        for (int j = 0; j < count; ++j) {
            double u = unit(rng), v = unit(rng);
            if (u + v > 1.) { u = 1. - u; v = 1. - v; }
            const Vec3d p = a + u * (b - a) + v * (c - a);
            if (!near_edge(p, facets[i].normal))
                samples.emplace_back(p, facets[i].normal);
        }
    }
    if (samples.size() > 20000) {
        std::shuffle(samples.begin(), samples.end(), rng);
        samples.resize(20000);
    }
    if (samples.empty())
        return missing;
    BoundingBoxf3 mesh_box, bead_box;
    for (const auto &v : mesh.vertices)
        mesh_box.merge(v.cast<double>());
    for (const Bead &b : beads) {
        bead_box.merge(Vec3d(b.a.x(), b.a.y(), 0.));
        bead_box.merge(Vec3d(b.b.x(), b.b.y(), 0.));
    }
    const Vec3d shift(bead_box.center().x() - mesh_box.center().x(), bead_box.center().y() - mesh_box.center().y(), -mesh_box.min.z());
    for (auto &sample : samples)
        sample.first += shift;

    struct RoundedBead {
        Vec3d a, delta, axis, across;
        double across2 = 0., radius = 0., half = 0.;
        bool valid = false;
    };
    std::vector<RoundedBead> shape(beads.size());
    double max_width = 0.;
    for (size_t i = 0; i < beads.size(); ++i) {
        const Bead &b = beads[i];
        if (b.width <= 0. || b.axis.squaredNorm() < 1e-18)
            continue;
        RoundedBead &s = shape[i];
        s.a = b.a;
        s.delta = b.b - b.a;
        s.axis = b.axis.normalized();
        s.across = s.delta - s.delta.dot(s.axis) * s.axis;
        s.across2 = std::max(s.across.squaredNorm(), 1e-18);
        const double height = std::min(b.width, std::max(0.02, b.height > 0. ? b.height : 0.2));
        s.half = 0.5 * height;
        s.radius = 0.5 * std::max(0., b.width - height);
        s.valid = true;
        max_width = std::max(max_width, b.width);
    }
    constexpr double range = 0.6;
    const double reach = range + 0.5 * max_width + 0.2, reach2 = reach * reach;
    std::unordered_map<Key, std::vector<size_t>> surface;
    for (size_t i = 0; i < samples.size(); ++i)
        surface[key(samples[i].first, reach)].push_back(i);
    auto near_surface = [&](const Vec3d &p) {
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    if (auto it = surface.find(key(p + reach * Vec3d(x, y, z), reach)); it != surface.end())
                        for (size_t i : it->second)
                            if ((samples[i].first - p).squaredNorm() <= reach2)
                                return true;
        return false;
    };
    std::unordered_map<Key, std::vector<std::pair<Vec3d, size_t>>> roads;
    for (size_t i = 0; i < shape.size(); ++i) {
        const RoundedBead &s = shape[i];
        if (!s.valid)
            continue;
        const int steps = std::max(1, int(std::ceil(s.delta.norm() / 0.4)));
        for (int j = 0; j <= steps; ++j) {
            const Vec3d p = s.a + (double(j) / steps) * s.delta;
            if (near_surface(p))
                roads[key(p, reach)].emplace_back(p, i);
        }
    }
    auto ray_interval = [&](const Vec3d &p, const Vec3d &normal, const RoundedBead &b, std::pair<double, double> &out) {
        const Vec3d d1 = 2. * range * normal, r = p - range * normal - b.a + b.half * b.axis;
        const double a = d1.squaredNorm(), e = b.delta.squaredNorm(), f = b.delta.dot(r), c = d1.dot(r), d = d1.dot(b.delta);
        const double denom = a * e - d * d;
        double u = denom > 1e-12 ? std::clamp((d * f - c * e) / denom, 0., 1.) : 0.;
        double v = e > 1e-12 ? (d * u + f) / e : 0.;
        if (v < 0.) u = a > 1e-12 ? std::clamp(-c / a, 0., 1.) : 0.;
        else if (v > 1.) u = a > 1e-12 ? std::clamp((d - c) / a, 0., 1.) : 0.;
        v = std::clamp(v, 0., 1.);
        const double radius = b.radius + b.half + 1e-6;
        if ((r + u * d1 - v * b.delta).squaredNorm() > radius * radius)
            return false;
        const Vec3d origin = p - b.a + b.half * b.axis;
        auto distance2 = [&](double t) {
            const Vec3d x = origin + t * normal;
            const Vec3d xp = x - x.dot(b.axis) * b.axis;
            const double along = std::clamp(xp.dot(b.across) / b.across2, 0., 1.);
            const Vec3d y = x - along * b.delta;
            const double yn = y.dot(b.axis);
            const double outside = std::max(0., (y - yn * b.axis).norm() - b.radius);
            return outside * outside + yn * yn;
        };
        constexpr double golden = 0.6180339887498948482;
        double left = -range, right = range;
        for (int i = 0; i < 24; ++i) {
            const double c0 = right - golden * (right - left), c1 = left + golden * (right - left);
            if (distance2(c0) < distance2(c1)) right = c1;
            else left = c0;
        }
        const double best = 0.5 * (left + right), half2 = b.half * b.half;
        if (!(distance2(best) <= half2))
            return false;
        auto boundary = [&](double end) {
            double inside = best;
            for (int i = 0; i < 24; ++i) {
                const double mid = 0.5 * (inside + end);
                if (distance2(mid) <= half2) inside = mid;
                else end = mid;
            }
            return distance2(end) <= half2 ? end : inside;
        };
        out = { boundary(-range), boundary(range) };
        return true;
    };
    std::vector<size_t> seen(beads.size(), std::numeric_limits<size_t>::max());
    std::vector<std::pair<double, double>> intervals;
    intervals.reserve(64);
    double sum = 0.;
    size_t count = 0;
    for (size_t si = 0; si < samples.size(); ++si) {
        const auto &[p, normal] = samples[si];
        intervals.clear();
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    if (auto it = roads.find(key(p + reach * Vec3d(x, y, z), reach)); it != roads.end())
                        for (const auto &[road, bi] : it->second) {
                            if ((road - p).squaredNorm() > reach2 || seen[bi] == si)
                                continue;
                            seen[bi] = si;
                            std::pair<double, double> interval;
                            if (ray_interval(p, normal, shape[bi], interval))
                                intervals.push_back(interval);
                        }
        if (intervals.empty())
            continue;
        std::sort(intervals.begin(), intervals.end());
        double lo = intervals.front().first, hi = intervals.front().second, nearest = std::numeric_limits<double>::infinity(), error = missing;
        auto finish_run = [&] {
            const double distance = lo <= 0. && hi >= 0. ? 0. : std::min(std::abs(lo), std::abs(hi));
            if (distance < nearest) { nearest = distance; error = hi; }
        };
        for (size_t i = 1; i < intervals.size(); ++i) {
            if (intervals[i].first <= hi + 1e-9) hi = std::max(hi, intervals[i].second);
            else { finish_run(); lo = intervals[i].first; hi = intervals[i].second; }
        }
        finish_run();
        if (std::isfinite(error) && std::abs(error) < 0.5) { sum += error * error; ++count; }
    }
    return count > 0 && 2 * count >= samples.size() ? std::sqrt(sum / double(count)) : missing;
}

struct ChunkCheck
{
    double chunks_rms = std::numeric_limits<double>::quiet_NaN(), planar_rms = std::numeric_limits<double>::quiet_NaN();
    double chunks_time = std::numeric_limits<double>::quiet_NaN(), planar_time = std::numeric_limits<double>::quiet_NaN();
};
ChunkCheck check_chunks(const PrintObject &object)
{
    ChunkCheck        check;
    std::vector<Bead> planar, chunks;
    check.planar_time = measured_slice(object, NonplanarMode::Off, planar);
    throw_if_canceled(object);
    {
        const double previous = rotary_measurement_time_cap;
        rotary_measurement_time_cap = 1.1 * multiplanar_time_limit(check.planar_time);
        check.chunks_time = measured_slice(object, NonplanarMode::Multiplanar, chunks);
        rotary_measurement_time_cap = previous;
    }
    TriangleMesh mesh = object.model_object()->raw_mesh();
    mesh.transform(object.model_object()->instances.front()->get_matrix());
    check.planar_rms = normal_rms(mesh.its, planar);
    check.chunks_rms = normal_rms(mesh.its, chunks);
    return check;
}

}

bool rotary_measurement_too_slow(double estimated_time)
{
    return rotary_measurement_depth > 0 && estimated_time >= rotary_measurement_time_cap;
}

bool rotary_measurement_unverified() { return rotary_measurement_depth > 0 && rotary_measurement_unchecked; }

bool rotary_auto_measurement()
{
    return rotary_measurement_depth > 0 && rotary_measurement_mode == NonplanarMode::Auto;
}

bool auto_rotary_requested(const PrintObject &object)
{
    if (rotary_measurement_depth > 0)
        return false;
    const auto *mode = object.model_object()->config.get().option<ConfigOptionEnum<NonplanarMode>>("nonplanar_mode");
    return (mode ? mode->value : object.print()->default_object_config().nonplanar_mode.value) == NonplanarMode::Auto &&
           make_tool_kinematics(object.print()->config())->rotary_count() > 0;
}

std::string auto_rotary_planar(const PrintObject &object)
{
    if (rotary_measurement_depth > 0 || object.config().nonplanar_mode.value != NonplanarMode::Auto ||
        !object.config().nonplanar_atom_toolpath.value.empty())
        return {};
    if (object.print()->config().spiral_mode.value)
        return {};
    const auto kinematics = make_tool_kinematics(object.print()->config());
    if (kinematics->rotary_count() == 0 || full_orientation(object.print()->config()))
        return {};
    if (rotary_route_claimed(object))
        return {};

    TriangleMesh mesh = object.model_object()->raw_mesh();
    mesh.transform(object.model_object()->instances.front()->get_matrix());
    if (const RoutePrediction prediction = predict_rotary_route(mesh.its, object); prediction.planar >= PlanarEarlyThreshold) {
        std::ostringstream why;
        why << "Auto mode prints planar layers for " << object.model_object()->name
            << ": no surface of it changes in curved layers (predicted, P = " << prediction.planar << ").";
        BOOST_LOG_TRIVIAL(info) << why.str();
        return why.str();
    }

    std::vector<Bead> planar, candidate;
    const double planar_time = measured_slice(object, NonplanarMode::Off, planar);
    throw_if_canceled(object);
    const double planar_rms = rounded_normal_rms(mesh.its, planar);
    if (!std::isfinite(planar_time) || planar_time <= 0.)
        return {};
    constexpr double RotaryGainPerTime = 0.5;
    const double candidate_time = measured_slice(object, NonplanarMode::Auto, candidate);
    throw_if_canceled(object);
    const double candidate_rms = rounded_normal_rms(mesh.its, candidate);
    if (!std::isfinite(planar_rms) || (std::isfinite(candidate_time) && candidate_time > 0. && !std::isfinite(candidate_rms))) {
        if (!(std::isfinite(candidate_time) && candidate_time > RotaryMaxTimeRatio * planar_time))
            return {};
        std::ostringstream why;
        why << "Auto mode prints planar layers for " << object.model_object()->name << ": estimated time " << candidate_time / 60.
            << " against " << planar_time / 60. << " min planar, past " << RotaryMaxTimeRatio << " x (its error did not measure).";
        BOOST_LOG_TRIVIAL(warning) << "Rotary auto decision " << object.model_object()->name << ": gain nan time_ratio "
                                   << candidate_time / planar_time << " keep false gain_only_keep true";
        return why.str();
    }
    const double ratio = candidate_time / planar_time;
    const double gain = planar_rms > 0. ? 1. - candidate_rms / planar_rms : 0.;
    const double gain_per_time = gain / std::max(1., ratio);
    const bool measured = std::isfinite(candidate_rms) && std::isfinite(candidate_time) && candidate_time > 0.;
    const bool earns = gain_per_time > 0.;
    const bool keep  = measured && earns && ratio <= RotaryMaxTimeRatio && gain >= RotaryGainPerTime * std::max(0., ratio - 1.);
    std::ostringstream why;
    why << "Auto mode " << (keep ? "keeps curved layers for " : "prints planar layers for ")
        << object.model_object()->name << ": measured RMS along the normal " << candidate_rms
        << " mm against " << planar_rms << " mm planar, estimated time " << candidate_time / 60.
        << " against " << planar_time / 60. << " min, gain per relative time " << gain_per_time << ".";
    BOOST_LOG_TRIVIAL(warning) << "Rotary auto decision " << object.model_object()->name << ": gain " << gain << " time_ratio " << ratio
                               << " keep " << keep << " gain_only_keep " << (measured && earns);
    BOOST_LOG_TRIVIAL(info) << why.str();
    return keep ? std::string() : why.str();
}

bool auto_rotary_candidate(const PrintObject &object, const indexed_triangle_set &mesh)
{
    if (rotary_route_claimed(object) || rotary_route_appends(object))
        return false;
    return auto_multiplanar_candidate(steep_top_share(mesh),
                                      thin_top_share(mesh, full_conformal_thickness(object.printing_region(0).config(), object)));
}

std::string auto_rotary_chunks(const PrintObject &object, const indexed_triangle_set &mesh, const MultiplanarPlan &plan)
{
    if (! auto_multiplanar_chunks(plan))
        return {};
    const std::string &name  = object.model_object()->name;
    const ChunkCheck   check = check_chunks(object);
    std::ostringstream measured;
    measured << "RMS along the normal " << check.chunks_rms << " mm against " << check.planar_rms << " mm planar, estimated time "
             << check.chunks_time / 60. << " against " << check.planar_time / 60. << " min";
    if (! auto_multiplanar_confirmed(check.chunks_rms, check.planar_rms, check.chunks_time, check.planar_time)) {
        BOOST_LOG_TRIVIAL(warning) << "Auto mode keeps curved shells for " << name << ": its chunks measure " << measured.str()
                                   << " (MULTIPLANAR.md \"Auto\").";
        return {};
    }
    const double thickness = full_conformal_thickness(object.printing_region(0).config(), object);
    const double steep = steep_top_share(mesh), thin = thin_top_share(mesh, thickness);
    std::ostringstream why;
    why << "Auto mode prints " << name << " in " << plan.chunks.size() << " tilted chunks: ";
    if (steep >= thin)
        why << int(std::lround(100. * steep)) << " % of its top is steeper than 45 deg";
    else
        why << int(std::lround(100. * thin)) << " % of its top is a wall thinner than " << thickness << " mm";
    why << ", and the chunks lay " << int(std::lround(100. * (1. - plan.step_area / std::max(plan.planar_step_area, EPSILON))))
        << " % of its stair-steps flat; measured: " << measured.str() << " (MULTIPLANAR.md \"Auto\").";
    BOOST_LOG_TRIVIAL(warning) << why.str();
    return why.str();
}

double measured_time(const PrintObject &object, NonplanarMode mode, const std::vector<std::pair<std::string, std::string>> &overrides,
                     const std::string &keep_as, bool verify, MeasuredPrint *measured)
{
    std::vector<Bead> beads;
    const double      time = measured_slice(object, mode, beads, overrides, keep_as, verify, mode == NonplanarMode::Off ? nullptr : measured);
    if (measured != nullptr) {
        measured->time = time;
        TriangleMesh mesh = object.model_object()->raw_mesh();
        mesh.transform(object.model_object()->instances.front()->get_matrix());
        measured->rms = rounded_normal_rms(mesh.its, beads);
    }
    return time;
}

double export_normal_rms(const PrintObject &object, const GCodeProcessorResult &result)
{
    std::vector<Bead> beads;
    append_beads(result, object.print()->config(), beads);
    TriangleMesh mesh = object.model_object()->raw_mesh();
    mesh.transform(object.model_object()->instances.front()->get_matrix());
    return rounded_normal_rms(mesh.its, beads);
}

double planar_support_time(const PrintObject &object)
{
    std::vector<Bead> beads;
    return measured_slice(object, NonplanarMode::Off, beads, { { "enable_support", "1" }, { "support_type", "tree(auto)" } });
}

}
