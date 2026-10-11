#include "Multiplanar.hpp"

#include "ConformalShells.hpp"
#include "PatchTools.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <Eigen/Geometry>
#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <limits>
#include <sstream>

namespace Slic3r::Nonplanar {

namespace {

constexpr double PlaneTolerance = 0.01;
constexpr double AzimuthStep    = 15. * M_PI / 180.;
constexpr double TiltStep       = 5. * M_PI / 180.;
constexpr double SlopeMargin    = 3. * M_PI / 180.;

struct Facets
{
    std::vector<Vec3d>   normal;
    std::vector<Vec3d>   centroid;
    std::vector<double>  area;
    std::vector<Vec3i32> neighbours;
};

Facets facets_of(const indexed_triangle_set &mesh)
{
    Facets f;
    const size_t n = mesh.indices.size();
    f.normal.resize(n);
    f.centroid.resize(n);
    f.area.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec3d a = mesh.vertices[mesh.indices[i][0]].cast<double>();
        const Vec3d b = mesh.vertices[mesh.indices[i][1]].cast<double>();
        const Vec3d c = mesh.vertices[mesh.indices[i][2]].cast<double>();
        const Vec3d cross = (b - a).cross(c - a);
        const double len  = cross.norm();
        f.area[i]     = 0.5 * len;
        f.normal[i]   = len > 0. ? Vec3d(cross / len) : Vec3d::Zero();
        f.centroid[i] = (a + b + c) / 3.;
    }
    f.neighbours = its_face_neighbors(mesh);
    return f;
}

bool inside(const std::vector<HalfSpace> &clips, const Vec3d &p)
{
    for (const HalfSpace &c : clips)
        if (c.normal.dot(p) > c.offset + 1e-9)
            return false;
    return true;
}

Vec3d direction(double tilt, double azimuth)
{
    return { std::sin(tilt) * std::cos(azimuth), std::sin(tilt) * std::sin(azimuth), std::cos(tilt) };
}

struct Frame
{
    Vec3d  dir { Vec3d::UnitZ() };
    double floor = 0.;
};

bool overhangs(const indexed_triangle_set &mesh, const Facets &f, size_t i, const Frame &frame, double sin_threshold,
               double layer_height)
{
    if (f.area[i] <= 0. || f.normal[i].dot(frame.dir) >= -sin_threshold)
        return false;
    double top = -std::numeric_limits<double>::infinity();
    for (int k = 0; k < 3; ++k)
        top = std::max(top, frame.dir.dot(mesh.vertices[mesh.indices[i][k]].cast<double>()));
    return top - frame.floor >= layer_height;
}

Matrix3d minimal_frame(const Vec3d &dir)
{
    return Eigen::Quaterniond::FromTwoVectors(dir, Vec3d::UnitZ()).toRotationMatrix();
}

double vertical_extent(const indexed_triangle_set &mesh, const Vec3d &dir, double base)
{
    double top = -std::numeric_limits<double>::infinity();
    for (const stl_vertex &v : mesh.vertices)
        top = std::max(top, dir.dot(v.cast<double>()));
    return top - base;
}

}

indexed_triangle_set clip_mesh(const indexed_triangle_set &mesh, const std::vector<HalfSpace> &clips,
                               const std::vector<double> &inset)
{
    indexed_triangle_set out = mesh;
    for (size_t i = 0; i < clips.size() && ! out.indices.empty(); ++i) {
        const HalfSpace &c = clips[i];
        const Matrix3d R = minimal_frame(c.normal);
        for (stl_vertex &v : out.vertices)
            v = (R * v.cast<double>()).cast<float>();
        indexed_triangle_set lower;
        cut_mesh(out, float(c.offset - (i < inset.size() ? inset[i] : 0.)), nullptr, &lower, true);
        for (stl_vertex &v : lower.vertices)
            v = (R.transpose() * v.cast<double>()).cast<float>();
        out = std::move(lower);
    }
    return out;
}

MultiplanarPlan plan_multiplanar(const indexed_triangle_set &mesh, const MultiplanarParams &params,
                                 const std::function<void()> &throw_if_canceled)
{
    MultiplanarPlan plan;
    const Facets    f          = facets_of(mesh);
    const double    sin_thresh = std::sin(params.overhang_threshold);
    const double    h          = params.layer_height;
    const bool      rotary     = params.kinematics != nullptr && params.kinematics->rotary_count() > 0;

    auto frame_of = [](const MultiplanarChunk &c) { return Frame{ c.build_dir, c.parent < 0 ? 0. : c.base }; };
    auto overhang_area = [&](const std::vector<HalfSpace> &clips, const Frame &frame) {
        double area = 0.;
        for (size_t i = 0; i < f.area.size(); ++i)
            if (inside(clips, f.centroid[i]) && overhangs(mesh, f, i, frame, sin_thresh, h))
                area += f.area[i];
        return area;
    };

    {
        MultiplanarChunk root;
        root.piece         = mesh;
        root.frame         = Matrix3d::Identity();
        root.overhang_area = overhang_area(root.clips, frame_of(root));
        plan.chunks.push_back(std::move(root));
    }

    const int           n_azimuth = int(std::lround(2. * M_PI / AzimuthStep));
    std::vector<double> budget(n_azimuth, 0.);
    for (int k = 0; k < n_azimuth; ++k) {
        const double psi = k * AzimuthStep;
        if (rotary) {
            budget[k] = params.kinematics->max_tilt(psi);
            continue;
        }
        const Vec2d u(std::cos(psi + M_PI), std::sin(psi + M_PI));
        double      lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const stl_vertex &v : mesh.vertices) {
            lo = std::min(lo, u.dot(v.head<2>().cast<double>()));
            hi = std::max(hi, u.dot(v.head<2>().cast<double>()));
        }
        const double width = std::max(hi - lo, EPSILON);
        double       tilt  = params.head.max_slope(psi + M_PI, h, width) - SlopeMargin;
        if (const double gantry = params.head.gantry_clearance(); std::isfinite(gantry))
            tilt = std::min(tilt, std::atan(std::max(0., gantry - params.head.margin) / width));
        budget[k] = std::max(0., tilt);
    }
    {
        std::ostringstream note;
        note << "tilt budget " << Geometry::rad2deg(*std::min_element(budget.begin(), budget.end())) << ".."
             << Geometry::rad2deg(*std::max_element(budget.begin(), budget.end())) << " deg";
        plan.notes.push_back(note.str());
    }
    if (*std::max_element(budget.begin(), budget.end()) < TiltStep - EPSILON) {
        plan.notes.emplace_back("no tilt budget: the head clears no tilted layer");
        plan.residual_overhang_area = plan.chunks.front().overhang_area;
        return plan;
    }

    std::vector<bool> skipped(f.area.size(), false);
    int               planes = 0;
    while (planes < params.max_planes) {
        if (throw_if_canceled)
            throw_if_canceled();
        int    chunk_idx = -1;
        size_t seed      = 0;
        for (size_t ci = 0; ci < plan.chunks.size() && chunk_idx < 0; ++ci) {
            const MultiplanarChunk &c = plan.chunks[ci];
            if (c.overhang_area < params.min_overhang_area)
                continue;
            double lowest = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < f.area.size(); ++i)
                if (! skipped[i] && inside(c.clips, f.centroid[i]) && overhangs(mesh, f, i, frame_of(c), sin_thresh, h) &&
                    c.build_dir.dot(f.centroid[i]) < lowest) {
                    lowest    = c.build_dir.dot(f.centroid[i]);
                    seed      = i;
                    chunk_idx = int(ci);
                }
        }
        if (chunk_idx < 0)
            break;
        const MultiplanarChunk &c = plan.chunks[size_t(chunk_idx)];

        std::vector<size_t> cluster { seed };
        std::vector<bool>   in_cluster(f.area.size(), false);
        in_cluster[seed]    = true;
        double cluster_area = 0.;
        for (size_t q = 0; q < cluster.size(); ++q) {
            cluster_area += f.area[cluster[q]];
            for (int k = 0; k < 3; ++k) {
                const int n = f.neighbours[cluster[q]][k];
                if (n >= 0 && ! in_cluster[size_t(n)] && inside(c.clips, f.centroid[size_t(n)]) &&
                    overhangs(mesh, f, size_t(n), frame_of(c), sin_thresh, h)) {
                    in_cluster[size_t(n)] = true;
                    cluster.push_back(size_t(n));
                }
            }
        }
        auto give_up = [&](const std::string &why) {
            for (size_t i : cluster)
                skipped[i] = true;
            std::ostringstream note;
            note << "overhang of " << cluster_area << " mm2 in chunk " << chunk_idx << " stays: " << why;
            plan.notes.push_back(note.str());
        };
        if (cluster_area < params.min_overhang_area) {
            give_up("smaller than the minimum area");
            continue;
        }

        struct Candidate
        {
            Vec3d  dir;
            double base, far_area, score;
        };
        std::vector<Candidate> candidates;
        const double           tilt_max = *std::max_element(budget.begin(), budget.end());
        for (double tilt = 0.; tilt <= tilt_max + EPSILON; tilt += TiltStep)
            for (int k = 0; k < (tilt == 0. ? 1 : n_azimuth); ++k) {
                if (tilt > budget[k] + EPSILON)
                    continue;
                const Vec3d dir  = direction(tilt, k * AzimuthStep);
                double      base = std::numeric_limits<double>::infinity();
                for (size_t i : cluster)
                    for (int v = 0; v < 3; ++v)
                        base = std::min(base, dir.dot(mesh.vertices[mesh.indices[i][v]].cast<double>()));
                std::vector<HalfSpace> far = c.clips, near = c.clips;
                far.push_back({ -dir, -base });
                near.push_back({ dir, base });
                const double far_area = overhang_area(far, { dir, base });
                const double score    = far_area + overhang_area(near, frame_of(c)) + 1e-3 * tilt;
                if (score < c.overhang_area - 0.5 * params.min_overhang_area)
                    candidates.push_back({ dir, base, far_area, score });
            }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
        if (candidates.empty()) {
            give_up("no build direction within the tilt budget removes it");
            continue;
        }

        std::string rejected = "no candidate passed validation";
        bool        accepted = false;
        for (size_t ci = 0; ci < std::min<size_t>(candidates.size(), 5) && ! accepted; ++ci) {
            const Candidate     &cand = candidates[ci];
            indexed_triangle_set far  = clip_mesh(c.piece, { { -cand.dir, -cand.base } });
            indexed_triangle_set near = clip_mesh(c.piece, { { cand.dir, cand.base } });
            if (near.indices.empty() || far.indices.empty() || vertical_extent(far, cand.dir, cand.base) < 3. * h) {
                rejected = "the chunk would be thinner than three layers";
                continue;
            }
            bool below = std::all_of(near.vertices.begin(), near.vertices.end(),
                                     [&](const stl_vertex &v) { return cand.dir.dot(v.cast<double>()) <= cand.base + PlaneTolerance; });
            for (int pi = 0; pi < chunk_idx && below; ++pi)
                below = std::all_of(plan.chunks[size_t(pi)].piece.vertices.begin(), plan.chunks[size_t(pi)].piece.vertices.end(),
                                    [&](const stl_vertex &v) { return cand.dir.dot(v.cast<double>()) <= cand.base + PlaneTolerance; });
            if (! below) {
                rejected = "material printed earlier would rise above the cut plane";
                continue;
            }
            BoundingBoxf3 box;
            for (const stl_vertex &v : far.vertices)
                box.merge(v.cast<double>());
            Matrix3d frame = minimal_frame(cand.dir);
            if (rotary) {
                ToolPose  pose { box.center(), cand.dir };
                JointPose joints;
                if (! params.kinematics->inverse(pose, nullptr, joints)) {
                    rejected = "the rotary axes cannot reach the build direction";
                    continue;
                }
                const Matrix3d head = params.kinematics->head_frame(joints);
                if ((head.row(2).transpose() - cand.dir).norm() < 1e-6)
                    frame = head;
                double lowest = 0.;
                for (const HeadBox &b : params.head.boxes)
                    if (! b.infinite_x && ! b.infinite_y)
                        for (int corner = 0; corner < 8; ++corner) {
                            const Vec3d p((corner & 1) ? b.max.x() : b.min.x(), (corner & 2) ? b.max.y() : b.min.y(),
                                          (corner & 4) ? b.max.z() : b.min.z());
                            lowest = std::min(lowest, (frame.transpose() * p).z());
                        }
                if (box.min.z() + lowest < 0.) {
                    rejected = "the tilted head would reach below the bed";
                    continue;
                }
            }

            MultiplanarChunk chunk;
            chunk.parent        = chunk_idx;
            chunk.build_dir     = cand.dir;
            chunk.frame         = frame;
            chunk.base          = cand.base;
            chunk.clips         = c.clips;
            chunk.base_clip     = int(chunk.clips.size());
            chunk.clips.push_back({ -cand.dir, -cand.base });
            chunk.piece         = std::move(far);
            chunk.overhang_area = cand.far_area;

            MultiplanarChunk &parent = plan.chunks[size_t(chunk_idx)];
            parent.clips.push_back({ cand.dir, cand.base });
            parent.piece         = std::move(near);
            parent.overhang_area = overhang_area(parent.clips, frame_of(parent));
            for (MultiplanarChunk &other : plan.chunks)
                if (other.parent > chunk_idx)
                    ++other.parent;
            plan.chunks.insert(plan.chunks.begin() + chunk_idx + 1, std::move(chunk));
            accepted = true;
            ++planes;
        }
        if (! accepted)
            give_up(rejected);
    }
    for (const MultiplanarChunk &c : plan.chunks)
        plan.residual_overhang_area += c.overhang_area;
    return plan;
}

namespace {

indexed_triangle_set slicing_frame_mesh(const PrintObject &object)
{
    double               ground = 0.;
    indexed_triangle_set its    = object.object_frame_mesh(ground).its;
    float                min_z  = std::numeric_limits<float>::max();
    for (const stl_vertex &v : its.vertices)
        min_z = std::min(min_z, v.z());
    const float shift = -(min_z + float(ground));
    for (stl_vertex &v : its.vertices)
        v.z() += shift;
    return its;
}

Polygon clip_convex(const Polygon &poly, const Vec2d &normal, double offset)
{
    Polygon out;
    for (size_t i = 0; i < poly.points.size(); ++i) {
        const Point &a  = poly.points[i];
        const Point &b  = poly.points[(i + 1) % poly.points.size()];
        const double da = normal.dot(unscale(a)) - offset;
        const double db = normal.dot(unscale(b)) - offset;
        if (da <= 0.)
            out.points.push_back(a);
        if ((da < 0.) != (db < 0.) && da != db) {
            const Vec2d q = a.cast<double>() + da / (da - db) * (b - a).cast<double>();
            out.points.emplace_back(q.x(), q.y());
        }
    }
    if (out.points.size() < 3)
        out.points.clear();
    return out;
}

void collect_paths(const ExtrusionEntity *ee, std::vector<const ExtrusionPath *> &out)
{
    if (const auto *coll = dynamic_cast<const ExtrusionEntityCollection *>(ee)) {
        for (const ExtrusionEntity *e : coll->entities)
            collect_paths(e, out);
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(ee)) {
        for (const ExtrusionPath &p : loop->paths)
            out.push_back(&p);
    } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(ee)) {
        for (const ExtrusionPath &p : multi->paths)
            out.push_back(&p);
    } else if (const auto *path = dynamic_cast<const ExtrusionPath *>(ee))
        out.push_back(path);
}

DynamicPrintConfig chunk_print_config(const PrintObject &object)
{
    DynamicPrintConfig config = object.print()->full_print_config();
    config.apply(object.config(), true);
    config.apply(object.printing_region(0).config(), true);
    const auto set = [&config](const char *key, const std::string &value) {
        if (config.option(key) != nullptr)
            config.set_deserialize_strict(key, value);
    };
    set("nonplanar_mode", "conformal");
    set("nonplanar_top_shells", "0");
    set("nonplanar_bottom_shells", "0");
    set("nonplanar_full_conformal_max_thickness", "0");
    set("nonplanar_overhangs", "0");
    set("nonplanar_atom_toolpath", "");
    set("nonplanar_ironing", "0");
    set("nonplanar_kinematics", "none");
    set("zaa_enabled", "0");
    set("enable_support", "0");
    set("raft_layers", "0");
    set("brim_type", "no_brim");
    set("skirt_loops", "0");
    set("enable_prime_tower", "0");
    set("enable_arc_fitting", "0");
    set("seam_slope_type", "none");
    set("elefant_foot_compensation", "0");
    set("initial_layer_print_height", config.opt_serialize("layer_height"));
    set("initial_layer_line_width", config.opt_serialize("line_width"));
    return config;
}

}

std::shared_ptr<const MultiplanarPlan> clip_multiplanar_slices(PrintObject &object)
{
    if (object.config().nonplanar_mode.value != NonplanarMode::Multiplanar ||
        !object.config().nonplanar_atom_toolpath.value.empty())
        return {};
    const std::unique_ptr<ToolKinematics> kinematics = make_tool_kinematics(object.print()->config());
    MultiplanarParams params;
    params.layer_height       = object.config().layer_height.value;
    params.overhang_threshold = Geometry::deg2rad(object.config().multiplanar_overhang_threshold.value);
    params.max_planes         = object.config().multiplanar_max_planes.value;
    params.head               = resolve_head_envelope(object.print()->config());
    params.kinematics         = kinematics.get();
    auto plan = std::make_shared<MultiplanarPlan>(plan_multiplanar(slicing_frame_mesh(object), params,
                                                                   [&object]() { throw_if_canceled(object); }));
    for (const std::string &note : plan->notes)
        BOOST_LOG_TRIVIAL(info) << "Multiplanar " << object.model_object()->name << ": " << note;
    BOOST_LOG_TRIVIAL(info) << "Multiplanar " << object.model_object()->name << ": " << plan->chunks.size()
                            << " chunks, " << plan->residual_overhang_area << " mm2 of overhang left";

    const std::vector<HalfSpace> &clips = plan->chunks.front().clips;
    if (clips.empty())
        return plan;
    LayerPtrs &layers = object.layers();
    std::vector<char> emptied(layers.size(), 0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id) {
            Layer      *layer = layers[layer_id];
            BoundingBox box;
            for (const LayerRegion *region : layer->regions())
                for (const Surface &surface : region->slices.surfaces)
                    box.merge(get_extents(surface.expolygon));
            if (! box.defined)
                continue;
            box.offset(scale_(1.));
            Polygon window { box.min, Point(box.max.x(), box.min.y()), box.max, Point(box.min.x(), box.max.y()) };
            for (const HalfSpace &c : clips) {
                const double rhs = c.offset - std::abs(c.normal.z()) * 0.5 * layer->height - c.normal.z() * layer->slice_z;
                const Vec2d  n   = c.normal.head<2>();
                if (n.norm() < 1e-9) {
                    if (rhs < 0.)
                        window.clear();
                } else
                    window = clip_convex(window, n, rhs);
                if (window.empty())
                    break;
            }
            for (LayerRegion *region : layer->regions()) {
                ExPolygons slices = to_expolygons(std::move(region->slices.surfaces));
                region->slices.set(window.empty() ? ExPolygons() : intersection_ex(slices, Polygons { window }), stInternal);
            }
            emptied[layer_id] = layer->empty();
        }
    });
    for (size_t layer_id = 0; layer_id < layers.size(); ++layer_id)
        layers[layer_id]->conformal_covered = emptied[layer_id] != 0;
    return plan;
}

void build_multiplanar_chunks(PrintObject &object, const MultiplanarPlan &plan, ConformalResult &result)
{
    if (plan.chunks.size() < 2 || object.layers().empty())
        return;
    const bool   rotary   = object.print()->config().nonplanar_kinematics.value != RotaryLayout::None;
    const double h        = object.config().layer_height.value;
    const size_t host_id  = object.layers().size() - 1;
    Layer       *host     = object.layers()[host_id];
    const double raft     = host->print_z - (host->slice_z + 0.5 * host->height);
    const DynamicPrintConfig  config = chunk_print_config(object);
    const indexed_triangle_set mesh  = slicing_frame_mesh(object);

    for (size_t ci = 1; ci < plan.chunks.size(); ++ci) {
        throw_if_canceled(object);
        const MultiplanarChunk &chunk = plan.chunks[ci];
        std::vector<double>     inset(chunk.clips.size(), 0.);
        for (size_t k = 0; k < chunk.clips.size(); ++k)
            if (int(k) != chunk.base_clip)
                inset[k] = std::abs(chunk.clips[k].normal.dot(chunk.build_dir)) * 0.5 * h;
        indexed_triangle_set piece = clip_mesh(mesh, chunk.clips, inset);
        if (piece.indices.empty())
            continue;
        for (stl_vertex &v : piece.vertices)
            v = (chunk.frame * v.cast<double>() - Vec3d(0., 0., chunk.base)).cast<float>();

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name        = object.model_object()->name + " chunk " + std::to_string(ci);
        model_object->add_volume(TriangleMesh(std::move(piece)));
        model_object->add_instance();
        Print print;
        try {
            print.apply(model, config);
            print.set_status_silent();
            print.process();
        } catch (const std::exception &ex) {
            throw Slic3r::SlicingError(std::string(_u8L("Multiplanar: slicing a chunk failed: ")) + ex.what());
        }
        if (print.objects().empty())
            continue;
        const PrintObject &sliced = *print.objects().front();
        const Vec2d        center = unscale(sliced.center_offset());
        const Matrix3d     to_object = chunk.frame.transpose();

        ConformalPatch patch;
        patch.id           = int(result.patches.size());
        patch.kind         = PatchKind::Chunk;
        patch.shell_height = h;
        patch.host_layer   = host_id;
        patch.region_id    = 0;
        patch.z_min        = std::numeric_limits<double>::infinity();
        patch.z_max        = -std::numeric_limits<double>::infinity();
        for (const Layer *layer : sliced.layers()) {
            std::vector<const ExtrusionPath *> paths;
            for (const LayerRegion *region : layer->regions()) {
                collect_paths(&region->perimeters, paths);
                collect_paths(&region->fills, paths);
            }
            ExtrusionEntityCollection shell;
            for (const ExtrusionPath *path : paths) {
                const Points3 &pts = path->polyline.points;
                if (pts.size() < 2)
                    continue;
                std::vector<Vec3d> tips(pts.size());
                for (size_t i = 0; i < pts.size(); ++i) {
                    const Vec3d in_chunk(unscale<double>(pts[i].x()) + center.x(), unscale<double>(pts[i].y()) + center.y(),
                                         layer->print_z + chunk.base);
                    tips[i] = to_object * in_chunk;
                    tips[i].z() += raft;
                    patch.z_min = std::min(patch.z_min, tips[i].z());
                    patch.z_max = std::max(patch.z_max, tips[i].z());
                }
                Polyline3          polyline;
                std::vector<float> bead_height(pts.size(), float(path->height));
                std::vector<Vec3f> tool_axis;
                polyline.points.reserve(pts.size());
                for (const Vec3d &t : tips)
                    polyline.points.push_back(Point3::new_scale(t.x(), t.y(), t.z() - host->print_z));
                if (rotary)
                    tool_axis.assign(pts.size(), chunk.build_dir.cast<float>());
                else
                    for (size_t i = 0; i < pts.size(); ++i) {
                        const Vec3d  d  = i + 1 < pts.size() ? Vec3d(tips[i + 1] - tips[i]) : Vec3d(tips[i] - tips[i - 1]);
                        const double xy = d.head<2>().norm();
                        bead_height[i]  = float(path->height * std::clamp(xy > EPSILON ? d.norm() / xy : 1., 1., 2.));
                    }
                shell.entities.push_back(new ExtrusionPathConformal(std::move(polyline), *path, std::move(bead_height), std::move(tool_axis)));
            }
            if (! shell.entities.empty())
                patch.shell_paths.push_back(std::move(shell));
        }
        if (patch.shell_paths.empty())
            continue;
        patch.shells = int(patch.shell_paths.size());
        host->conformal_patches_after.push_back(patch.id);
        result.patches.push_back(std::move(patch));
    }
}

}
