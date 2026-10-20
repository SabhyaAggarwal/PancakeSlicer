#include "Multiplanar.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <admesh/stl.h>
#include "libslic3r/libslic3r.h"
#include <cstddef>
#include <math.h>
#include "MultiplanarConfirm.hpp"

#include "ConformalIroning.hpp"
#include "MaterialRules.hpp"
#include "ConformalShells.hpp"
#include "PatchTools.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Line.hpp"
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
#include <tbb/parallel_reduce.h>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
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
constexpr double MinTilt        = 1. * M_PI / 180.;
constexpr double TopBinTilt     = 3. * M_PI / 180.;
constexpr double TopBand        = 6. * M_PI / 180.;
constexpr double TopSlack       = 10. * M_PI / 180.;
constexpr double StepWeight     = 0.5;
constexpr int    MinChunkLayers = 3;
constexpr size_t MaxValidated   = 24;
constexpr double SeamShare      = 0.05;

struct Score
{
    double overhang = 0.;
    double steps    = 0.;
};

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
    bool   bed   = true;
};

bool overhangs(const indexed_triangle_set &mesh, const Facets &f, size_t i, const Frame &frame, double sin_threshold,
               double layer_height)
{
    if (f.area[i] <= 0. || f.normal[i].dot(frame.dir) >= -sin_threshold)
        return false;
    double top = -std::numeric_limits<double>::infinity();
    for (int k = 0; k < 3; ++k)
        top = std::max(top, frame.dir.dot(mesh.vertices[mesh.indices[i][k]].cast<double>()));
    return ! frame.bed || top - frame.floor >= layer_height;
}

double step_area(const indexed_triangle_set &mesh, const Facets &f, size_t i, const Frame &frame, double h)
{
    const double c = f.normal[i].dot(frame.dir);
    if (f.area[i] <= 0. || c <= 0.)
        return 0.;
    std::array<double, 3> s;
    for (int k = 0; k < 3; ++k)
        s[k] = (frame.dir.dot(mesh.vertices[mesh.indices[i][k]].cast<double>()) - frame.floor) / h;
    const auto [lo, hi] = std::minmax_element(s.begin(), s.end());
    double     e2       = 1. / 12.;
    if (*hi - *lo < 1.) {
        const std::array<double, 7> t { s[0], s[1], s[2], 0.5 * (s[0] + s[1]), 0.5 * (s[1] + s[2]), 0.5 * (s[2] + s[0]),
                                        (s[0] + s[1] + s[2]) / 3. };
        e2 = 0.;
        for (double x : t)
            e2 += (x - std::round(x)) * (x - std::round(x)) / 7.;
    }
    return f.area[i] * c * c * e2 * 12.;
}

template<typename Accept>
void grow_cluster(const Facets &f, size_t seed, std::vector<size_t> &cluster, Accept accept, std::vector<bool> *seen = nullptr)
{
    std::vector<bool> local;
    if (seen == nullptr) {
        local.assign(f.area.size(), false);
        seen = &local;
    }
    cluster = { seed };
    (*seen)[seed] = true;
    for (size_t q = 0; q < cluster.size(); ++q)
        for (int k = 0; k < 3; ++k) {
            const int n = f.neighbours[cluster[q]][k];
            if (n >= 0 && ! (*seen)[size_t(n)] && accept(size_t(n))) {
                (*seen)[size_t(n)] = true;
                cluster.push_back(size_t(n));
            }
        }
}

Matrix3d minimal_frame(const Vec3d &dir)
{
    return Eigen::Quaterniond::FromTwoVectors(dir, Vec3d::UnitZ()).toRotationMatrix();
}

double head_drop(const HeadEnvelope &head, const Matrix3d &frame)
{
    double lowest = 0.;
    for (const HeadBox &b : head.boxes)
        if (! b.infinite_x && ! b.infinite_y)
            for (int corner = 0; corner < 8; ++corner) {
                const Vec3d p((corner & 1) ? b.max.x() : b.min.x(), (corner & 2) ? b.max.y() : b.min.y(),
                              (corner & 4) ? b.max.z() : b.min.z());
                lowest = std::min(lowest, (frame.transpose() * p).z());
            }
    return lowest;
}
double head_drop(const HeadEnvelope &head, const Vec3d &dir) { return head_drop(head, minimal_frame(dir)); }

double vertical_extent(const indexed_triangle_set &mesh, const Vec3d &dir, double base)
{
    double top = -std::numeric_limits<double>::infinity();
    for (const stl_vertex &v : mesh.vertices)
        top = std::max(top, dir.dot(v.cast<double>()));
    return top - base;
}

constexpr double PlateDepth    = 150.;
constexpr double ObstaclePitch = 0.5;
constexpr double MergeTolerance = 1.;
constexpr double NozzleZone    = 2.;
constexpr double NozzleBody    = 5.;
constexpr double SampleReach   = 0.35;
constexpr double SideCutLean   = 20. * M_PI / 180.;
constexpr double StubPenalty   = 10.;
constexpr double SideCutStep   = 4.;
constexpr double TravelHop     = 2.;

MaterialColumns::PreparedHead coarse_head(const HeadEnvelope &envelope)
{
    MaterialColumns::PreparedHead out;
    std::vector<MaterialColumns::PreparedBox> first;
    for (const MaterialColumns::PreparedBox &b : MaterialColumns::prepare(envelope).boxes) {
        if (b.infinite_x || b.infinite_y || b.max.z() <= NozzleZone)
            continue;
        size_t k = b.min.z() < NozzleBody ? first.size() : 0;
        for (; k < first.size(); ++k)
            if ((first[k].min.head<2>() - b.min.head<2>()).cwiseAbs().maxCoeff() <= MergeTolerance &&
                (first[k].max.head<2>() - b.max.head<2>()).cwiseAbs().maxCoeff() <= MergeTolerance &&
                out.boxes[k].max.z() >= b.min.z() - EPSILON)
                break;
        if (k < first.size() && first[k].min.z() >= NozzleBody) {
            out.boxes[k].min = out.boxes[k].min.cwiseMin(b.min);
            out.boxes[k].max = out.boxes[k].max.cwiseMax(b.max);
        } else {
            first.push_back(b);
            out.boxes.push_back(b);
        }
    }
    return out;
}

template<typename Fn> void sample_surface(const indexed_triangle_set &mesh, double pitch, Fn fn)
{
    for (const stl_triangle_vertex_indices &t : mesh.indices) {
        const Vec3d a = mesh.vertices[t[0]].cast<double>(), b = mesh.vertices[t[1]].cast<double>(), c = mesh.vertices[t[2]].cast<double>();
        const int   n = std::max(1, int(std::ceil(std::max({ (b - a).norm(), (c - b).norm(), (a - c).norm() }) / pitch)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n - i; ++j)
                fn(a + (b - a) * (double(i) / n) + (c - a) * (double(j) / n));
    }
}

void sliding_min(const std::vector<double> &in, std::vector<double> &out, size_t first, int n, int stride, int lo, int hi)
{
    std::vector<int> window;
    size_t           head = 0;
    int              next = 0;
    for (int i = 0; i < n; ++i) {
        for (; next < n && next <= i + hi; ++next) {
            const double v = in[first + size_t(next) * stride];
            while (window.size() > head && in[first + size_t(window.back()) * stride] >= v)
                window.pop_back();
            window.push_back(next);
        }
        while (window.size() > head && window[head] < i + lo)
            ++head;
        out[first + size_t(i) * stride] = window.size() > head && window[head] <= i + hi ? in[first + size_t(window[head]) * stride] :
                                                                                            std::numeric_limits<double>::infinity();
    }
}

struct HeadHit
{
    std::string what;
    bool        bed = false;
};
HeadHit head_blocked(const indexed_triangle_set &piece, const Matrix3d &frame, double h, const HeadEnvelope &envelope,
                     const MaterialColumns::PreparedHead &head, const std::vector<const indexed_triangle_set *> &earlier,
                     const Polygons &plates)
{
    constexpr double Inf = std::numeric_limits<double>::infinity();
    std::vector<Vec3d> tips;
    sample_surface(piece, ObstaclePitch, [&](const Vec3d &p) { tips.push_back(frame * p); });
    if (tips.empty())
        return {};
    BoundingBoxf3 box;
    for (const Vec3d &t : tips)
        box.merge(t);
    const double floor = box.min.z() + h, top = box.max.z() + TravelHop;
    double reach = 0.;
    for (const MaterialColumns::PreparedBox &b : head.boxes)
        if (! b.infinite_x && ! b.infinite_y)
            reach = std::max({ reach, b.min.head<2>().cwiseAbs().maxCoeff(), b.max.head<2>().cwiseAbs().maxCoeff() });
    reach += 2. * ObstaclePitch;
    const Vec2d origin = box.min.head<2>() - Vec2d::Constant(reach);
    const int   nx = int(std::ceil((box.max.x() - box.min.x() + 2. * reach) / ObstaclePitch)) + 1;
    const int   ny = int(std::ceil((box.max.y() - box.min.y() + 2. * reach) / ObstaclePitch)) + 1;
    auto cell = [&](const Vec2d &p, int &ix, int &iy) {
        ix = int(std::floor((p.x() - origin.x()) / ObstaclePitch));
        iy = int(std::floor((p.y() - origin.y()) / ObstaclePitch));
        return ix >= 0 && iy >= 0 && ix < nx && iy < ny;
    };
    std::vector<double> lowest(size_t(nx) * ny, Inf), neg_highest(size_t(nx) * ny, Inf);
    for (const Vec3d &t : tips) {
        int ix, iy;
        if (cell(t.head<2>(), ix, iy)) {
            const size_t c = size_t(iy) * nx + ix;
            lowest[c]      = std::min(lowest[c], std::max(t.z(), floor));
            neg_highest[c] = std::min(neg_highest[c], -std::max(t.z(), floor));
        }
    }

    std::vector<Vec3d> obstacles, where;
    auto add = [&](const Vec3d &p) {
        const Vec3d q = frame * p;
        int ix, iy;
        if (cell(q.head<2>(), ix, iy)) {
            obstacles.push_back(q);
            where.push_back(p);
        }
    };
    for (const indexed_triangle_set *mesh : earlier)
        sample_surface(*mesh, ObstaclePitch, add);
    const size_t first_plate = obstacles.size();
    for (const Polygon &plate : plates) {
        const BoundingBoxf outline(unscaled(get_extents(plate).min), unscaled(get_extents(plate).max));
        for (double y = outline.min.y(); y <= outline.max.y(); y += ObstaclePitch)
            for (double x = outline.min.x(); x <= outline.max.x(); x += ObstaclePitch)
                if (plate.contains(Point::new_scale(x, y))) {
                    add(Vec3d(x, y, 0.));
                    add(Vec3d(x, y, -PlateDepth));
                }
        for (const Line &edge : plate.lines()) {
            const Vec2d a = unscaled(edge.a), b = unscaled(edge.b);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / ObstaclePitch)));
            for (int i = 0; i <= n; ++i)
                for (double z = 0.; z >= -PlateDepth; z -= ObstaclePitch)
                    add(Vec3d(a.x() + (b.x() - a.x()) * i / n, a.y() + (b.y() - a.y()) * i / n, z));
        }
    }
    if (obstacles.empty())
        return {};
    std::vector<double> heights;
    for (const Vec3d &o : obstacles)
        heights.push_back(o.z());
    std::sort(heights.begin(), heights.end());

    std::vector<double> rows(lowest.size()), reach_min(lowest.size()), reach_neg_max(lowest.size());
    for (const MaterialColumns::PreparedBox &b : head.boxes) {
        const Vec3d lo(b.min.x() - SampleReach, b.min.y() - SampleReach, std::max(b.min.z(), NozzleZone)),
            hi = b.max + Vec3d::Constant(SampleReach);
        if (auto it = std::lower_bound(heights.begin(), heights.end(), floor + lo.z()); it == heights.end() || *it > top + hi.z())
            continue;
        const int x0 = int(std::floor(-hi.x() / ObstaclePitch)) - 1, x1 = int(std::ceil(-lo.x() / ObstaclePitch)) + 1;
        const int y0 = int(std::floor(-hi.y() / ObstaclePitch)) - 1, y1 = int(std::ceil(-lo.y() / ObstaclePitch)) + 1;
        for (int iy = 0; iy < ny; ++iy)
            sliding_min(lowest, rows, size_t(iy) * nx, nx, 1, x0, x1);
        for (int ix = 0; ix < nx; ++ix)
            sliding_min(rows, reach_min, size_t(ix), ny, nx, y0, y1);
        for (int iy = 0; iy < ny; ++iy)
            sliding_min(neg_highest, rows, size_t(iy) * nx, nx, 1, x0, x1);
        for (int ix = 0; ix < nx; ++ix)
            sliding_min(rows, reach_neg_max, size_t(ix), ny, nx, y0, y1);
        for (size_t k = 0; k < obstacles.size(); ++k) {
            const Vec3d &o = obstacles[k];
            int ix, iy;
            cell(o.head<2>(), ix, iy);
            const size_t c = size_t(iy) * nx + ix;
            if (reach_min[c] <= o.z() - lo.z() && TravelHop - reach_neg_max[c] >= o.z() - hi.z()) {
                std::ostringstream hit;
                hit << "the head (" << envelope.boxes[b.index].name << ") would meet " << (k < first_plate ? "material printed earlier" : "the bed")
                    << " at " << where[k].x() << ", " << where[k].y() << ", " << where[k].z();
                return { hit.str(), k >= first_plate };
            }
        }
    }
    return {};
}

}

indexed_triangle_set chunk_piece(const indexed_triangle_set &mesh, const MultiplanarChunk &chunk, double layer_height)
{
    std::vector<double> inset(chunk.clips.size(), 0.);
    for (size_t k = 0; k < chunk.clips.size(); ++k)
        if (int(k) != chunk.base_clip)
            inset[k] = std::abs(chunk.clips[k].normal.dot(chunk.build_dir)) * 0.5 * layer_height;
    return clip_mesh(mesh, chunk.clips, inset);
}

int printable_layers(const indexed_triangle_set &piece, const MultiplanarChunk &chunk, double layer_height)
{
    if (piece.indices.empty())
        return 0;
    indexed_triangle_set local = piece;
    float                top   = 0.f;
    for (stl_vertex &v : local.vertices) {
        v   = (chunk.frame * v.cast<double>() - Vec3d(0., 0., chunk.base)).cast<float>();
        top = std::max(top, v.z());
    }
    std::vector<float> zs;
    for (double z = 0.5 * layer_height; z < top; z += layer_height)
        zs.push_back(float(z));
    const double min_area = scale_(1.) * scale_(1.);
    int          layers   = 0;
    for (const ExPolygons &slice : slice_mesh_ex(local, zs))
        if (std::any_of(slice.begin(), slice.end(), [min_area](const ExPolygon &e) { return e.area() >= min_area; }))
            ++layers;
    return layers;
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
    const size_t    n_facets   = f.area.size();
    const double    sin_thresh = std::sin(0.5 * M_PI - params.overhang_threshold);
    const double    h          = params.layer_height;
    const bool      rotary     = params.kinematics != nullptr && params.kinematics->rotary_count() > 0;

    auto frame_of = [](const MultiplanarChunk &c) { return Frame{ c.build_dir, c.parent < 0 ? 0. : c.base, c.parent < 0 }; };
    auto evaluate = [&](const std::vector<HalfSpace> &clips, const Frame &frame) {
        return tbb::parallel_deterministic_reduce(
            tbb::blocked_range<size_t>(0, n_facets, 1024), Score{},
            [&](const tbb::blocked_range<size_t> &range, Score s) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                    if (inside(clips, f.centroid[i])) {
                        if (overhangs(mesh, f, i, frame, sin_thresh, h))
                            s.overhang += f.area[i];
                        else
                            s.steps += step_area(mesh, f, i, frame, h);
                    }
                return s;
            },
            [](Score a, const Score &b) { a.overhang += b.overhang; a.steps += b.steps; return a; });
    };
    auto set_score = [](MultiplanarChunk &c, const Score &s) {
        c.overhang_area = s.overhang;
        c.step_area     = s.steps;
    };

    double planar_score = 0.;
    {
        MultiplanarChunk root;
        root.piece = mesh;
        root.frame = Matrix3d::Identity();
        set_score(root, evaluate(root.clips, frame_of(root)));
        plan.planar_step_area     = root.step_area;
        plan.planar_overhang_area = root.overhang_area;
        planar_score          = root.overhang_area + StepWeight * root.step_area;
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
    auto budget_at = [&](double psi) {
        const double t = std::fmod(std::fmod(psi, 2. * M_PI) + 2. * M_PI, 2. * M_PI) / AzimuthStep;
        const int    k = int(std::floor(t)) % n_azimuth;
        return budget[k] + (t - std::floor(t)) * (budget[(k + 1) % n_azimuth] - budget[k]);
    };
    const double tilt_max = *std::max_element(budget.begin(), budget.end());
    {
        std::ostringstream note;
        note << "tilt budget " << Geometry::rad2deg(*std::min_element(budget.begin(), budget.end())) << ".."
             << Geometry::rad2deg(tilt_max) << " deg";
        plan.notes.push_back(note.str());
    }
    auto finish = [&]() {
        for (const MultiplanarChunk &c : plan.chunks) {
            plan.residual_overhang_area += c.overhang_area;
            plan.step_area += c.step_area;
        }
        return plan;
    };
    if (tilt_max < MinTilt) {
        plan.notes.emplace_back("no tilt budget: the head clears no tilted layer");
        return finish();
    }
    const bool full = rotary && layout_is_table(params.kinematics->layout()) && tilt_max > 0.5 * M_PI + EPSILON;
    const MaterialColumns::PreparedHead prepared_head = full ? coarse_head(params.head) : MaterialColumns::PreparedHead{};

    std::vector<bool> skipped(n_facets, false);
    std::vector<bool> tried(n_facets, false);

    enum class Goal { Overhang, Top };
    struct Work
    {
        Goal                goal      = Goal::Overhang;
        int                 chunk_idx = -1;
        std::vector<size_t> cluster;
        Vec3d               target = Vec3d::UnitZ();
    };
    struct Cut
    {
        MultiplanarChunk       chunk;
        std::vector<HalfSpace> parent_clips;
        indexed_triangle_set   parent_piece;
        double                 gain = 0.;
    };

    auto overhang_work = [&]() {
        Work   w;
        size_t seed = 0;
        for (size_t ci = 0; ci < plan.chunks.size() && w.chunk_idx < 0; ++ci) {
            const MultiplanarChunk &c = plan.chunks[ci];
            if (c.overhang_area < params.min_overhang_area)
                continue;
            double lowest = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < n_facets; ++i)
                if (! skipped[i] && inside(c.clips, f.centroid[i]) && overhangs(mesh, f, i, frame_of(c), sin_thresh, h) &&
                    c.build_dir.dot(f.centroid[i]) < lowest) {
                    lowest      = c.build_dir.dot(f.centroid[i]);
                    seed        = i;
                    w.chunk_idx = int(ci);
                }
        }
        if (w.chunk_idx >= 0) {
            const MultiplanarChunk &c = plan.chunks[size_t(w.chunk_idx)];
            grow_cluster(f, seed, w.cluster, [&](size_t n) { return inside(c.clips, f.centroid[n]) && overhangs(mesh, f, n, frame_of(c), sin_thresh, h); });
        }
        return w;
    };

    auto top_work = [&]() {
        Work   w;
        w.goal      = Goal::Top;
        double best = 0.;
        for (size_t ci = 0; ci < plan.chunks.size(); ++ci) {
            const MultiplanarChunk &c        = plan.chunks[ci];
            const Frame             frame    = frame_of(c);
            const Matrix3d          to_chunk = minimal_frame(c.build_dir);
            std::map<std::pair<int, int>, std::pair<double, Vec3d>> bins;
            std::vector<std::pair<int, int>> bin_of(n_facets, { -1, -1 });
            for (size_t i = 0; i < n_facets; ++i) {
                if (tried[i] || ! inside(c.clips, f.centroid[i]))
                    continue;
                const Vec3d  n    = to_chunk * f.normal[i];
                const double tilt = std::acos(std::clamp(n.z(), -1., 1.));
                if (tilt < MinTilt || tilt > tilt_max + TopSlack)
                    continue;
                const double a = step_area(mesh, f, i, frame, h);
                if (a <= 0.)
                    continue;
                const std::pair<int, int> key(int(std::lround(tilt / TopBinTilt)),
                                              int(std::lround(std::atan2(n.y(), n.x()) / AzimuthStep)) % n_azimuth);
                auto &bin = bins.try_emplace(key, 0., Vec3d::Zero()).first->second;
                bin.first += a;
                bin.second += f.area[i] * f.normal[i];
                bin_of[i] = key;
            }
            for (const auto &[key, bin] : bins) {
                if (bin.first <= best)
                    continue;
                const Vec3d       mean = bin.second.normalized();
                std::vector<bool> seen(n_facets, false);
                auto in_band = [&](size_t n) { return bin_of[n].first >= 0 && f.normal[n].dot(mean) >= std::cos(TopBand); };
                for (size_t i = 0; i < n_facets; ++i) {
                    if (bin_of[i] != key || seen[i])
                        continue;
                    std::vector<size_t> piece;
                    grow_cluster(f, i, piece, in_band, &seen);
                    double a      = 0.;
                    Vec3d  normal = Vec3d::Zero();
                    for (size_t k : piece) {
                        a += step_area(mesh, f, k, frame, h);
                        normal += f.area[k] * f.normal[k];
                    }
                    if (a > best && a >= params.min_overhang_area) {
                        best        = a;
                        w.chunk_idx = int(ci);
                        w.cluster   = std::move(piece);
                        w.target    = normal.normalized();
                    }
                }
            }
        }
        return w;
    };

    auto find_cut = [&](const Work &w, std::string &rejected) -> std::optional<Cut> {
        const MultiplanarChunk &c = plan.chunks[size_t(w.chunk_idx)];
        std::vector<Vec3d> dirs;
        for (double tilt = 0.; tilt <= tilt_max + EPSILON; tilt += TiltStep)
            for (int k = 0; k < (tilt == 0. ? 1 : n_azimuth); ++k)
                if (tilt <= budget[k] + EPSILON)
                    dirs.push_back(direction(tilt, k * AzimuthStep));
        if (w.goal == Goal::Top) {
            const double psi  = std::atan2(w.target.y(), w.target.x());
            const double tilt = std::acos(std::clamp(w.target.z(), -1., 1.));
            dirs.push_back(direction(std::min(tilt, budget_at(psi)), psi));
        }
        dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const Vec3d &d) { return d.dot(c.build_dir) > std::cos(MinTilt); }), dirs.end());

        struct Candidate
        {
            Vec3d  dir;
            double base;
            Score  far;
            double score;
            Vec3d  cut;
            double offset;
            double stub = 0.;
        };
        std::vector<Candidate> candidates;
        for (const Vec3d &dir : dirs) {
            double lo = std::numeric_limits<double>::infinity(), level = 0., weight = 0.;
            for (size_t i : w.cluster) {
                for (int v = 0; v < 3; ++v)
                    lo = std::min(lo, dir.dot(mesh.vertices[mesh.indices[i][v]].cast<double>()));
                level += f.area[i] * dir.dot(f.centroid[i]);
                weight += f.area[i];
            }
            level /= std::max(weight, EPSILON);
            if (w.goal == Goal::Overhang) {
                for (int k = 0; k < 4; ++k)
                    candidates.push_back({ dir, lo - 0.25 * k * h, {}, 0., dir, lo - 0.25 * k * h });
                if (full && dir.z() < 0.) {
                    double foot = -std::numeric_limits<double>::infinity();
                    for (const stl_vertex &v : c.piece.vertices)
                        if (v.z() < params.first_layer_height + h)
                            foot = std::max(foot, dir.dot(v.cast<double>()));
                    if (foot > lo)
                        for (int k = 0; k < 3; ++k)
                            candidates.push_back({ dir, foot + (k + 1) * h, {}, 0., dir, foot + (k + 1) * h });
                }
            }
            else {
                const double layers = std::ceil((level - lo) / h - EPSILON) + MinChunkLayers;
                for (int k = 0; k < 3; ++k)
                    candidates.push_back({ dir, level - (layers + k) * h, {}, 0., dir, level - (layers + k) * h });
            }
            const Vec3d side(dir.x(), dir.y(), 0.);
            if (full && w.goal == Goal::Overhang && std::abs(dir.z()) <= std::sin(SideCutLean)) {
                const Vec3d u    = side.normalized();
                double      lo_u = std::numeric_limits<double>::infinity();
                for (size_t i : w.cluster)
                    for (int v = 0; v < 3; ++v)
                        lo_u = std::min(lo_u, u.dot(mesh.vertices[mesh.indices[i][v]].cast<double>()));
                double holder = -std::numeric_limits<double>::infinity();
                for (const stl_vertex &v : c.piece.vertices)
                    if (v.z() < params.first_layer_height + h)
                        holder = std::max(holder, u.dot(v.cast<double>()));
                std::vector<double> offsets { lo_u, lo_u + 1., lo_u + 2., lo_u + 4., lo_u + 8. };
                if (holder > -std::numeric_limits<double>::infinity() && holder < lo_u)
                    for (double off : { 0.1, 0.5, 1., 2. })
                        offsets.push_back(holder + off);
                for (double offset : offsets) {
                    double base = std::numeric_limits<double>::infinity();
                    for (const stl_vertex &v : c.piece.vertices)
                        if (u.dot(v.cast<double>()) >= offset)
                            base = std::min(base, dir.dot(v.cast<double>()));
                    if (std::isfinite(base))
                        candidates.push_back({ dir, base, {}, 0., u, offset, std::isfinite(holder) ? std::max(0., offset - holder) : 0. });
                }
            }
        }
        const double floor_z = params.first_layer_height + h;
        const stl_vertex *summit = nullptr;
        if (full && w.goal == Goal::Overhang)
            for (const stl_vertex &v : c.piece.vertices)
                if (summit == nullptr || v.z() > summit->z())
                    summit = &v;
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate &cand) {
            if (summit != nullptr && cand.cut.dot(summit->cast<double>()) >= cand.offset)
                return true;
            double lowest = std::numeric_limits<double>::infinity();
            for (const stl_vertex &v : c.piece.vertices)
                if (cand.cut.dot(v.cast<double>()) >= cand.offset)
                    lowest = std::min(lowest, double(v.z()));
            return lowest < floor_z || (rotary && ! full && lowest + head_drop(params.head, cand.dir) < 0.);
        }), candidates.end());

        const double current = c.overhang_area + StepWeight * c.step_area;
        auto score = [&](Candidate &cand) {
            std::vector<HalfSpace> far = c.clips, near = c.clips;
            far.push_back({ -cand.cut, -cand.offset });
            near.push_back({ cand.cut, cand.offset });
            cand.far         = evaluate(far, { cand.dir, cand.base, false });
            const Score rest = evaluate(near, frame_of(c));
            cand.score       = cand.far.overhang + rest.overhang + StepWeight * (cand.far.steps + rest.steps) + 1e-3 * std::acos(cand.dir.z()) +
                         StubPenalty * cand.stub;
        };
        tbb::parallel_for(size_t(0), candidates.size(), [&](size_t k) { score(candidates[k]); });
        const double seam = std::max(w.goal == Goal::Overhang ? 0.5 * params.min_overhang_area : StepWeight * params.min_overhang_area,
                                     SeamShare * planar_score);
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate &cand) { return cand.score >= current - seam; }),
                         candidates.end());
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
        if (candidates.empty()) {
            rejected = "no build direction within the tilt budget improves it";
            return std::nullopt;
        }

        rejected = "no candidate passed validation";
        const size_t max_validated = full ? 160 : MaxValidated;
        struct Blocked { Vec3d dir, cut; double offset; };
        std::vector<Blocked>                  blocked;
        std::vector<std::pair<Vec3d, double>> tried_cuts;
        for (size_t ci = 0; ci < std::min(candidates.size(), max_validated); ++ci) {
            const Candidate      cand = candidates[ci];
            if (std::any_of(blocked.begin(), blocked.end(), [&](const Blocked &b) {
                    return b.dir.dot(cand.dir) > 1. - 1e-9 && b.cut.dot(cand.cut) > 1. - 1e-9 && cand.offset <= b.offset + EPSILON; })) {
                candidates.erase(candidates.begin() + ci--);
                continue;
            }
            indexed_triangle_set far  = clip_mesh(c.piece, { { -cand.cut, -cand.offset } });
            indexed_triangle_set near = clip_mesh(c.piece, { { cand.cut, cand.offset } });
            double base = cand.base;
            if (cand.cut != cand.dir) {
                base = std::numeric_limits<double>::infinity();
                for (const stl_vertex &v : far.vertices)
                    base = std::min(base, cand.dir.dot(v.cast<double>()));
            }
            if (near.indices.empty() || far.indices.empty() || vertical_extent(far, cand.dir, base) < MinChunkLayers * h) {
                rejected = "the chunk would be thinner than three layers";
                continue;
            }
            bool below = std::all_of(near.vertices.begin(), near.vertices.end(),
                                     [&](const stl_vertex &v) { return cand.cut.dot(v.cast<double>()) <= cand.offset + PlaneTolerance; });
            for (int pi = 0; pi < w.chunk_idx && below; ++pi)
                below = std::all_of(plan.chunks[size_t(pi)].piece.vertices.begin(), plan.chunks[size_t(pi)].piece.vertices.end(),
                                    [&](const stl_vertex &v) { return cand.cut.dot(v.cast<double>()) <= cand.offset + PlaneTolerance; });
            if (! below) {
                rejected = "material printed earlier would rise above the cut plane";
                continue;
            }
            BoundingBoxf3 box;
            for (const stl_vertex &v : far.vertices)
                box.merge(v.cast<double>());
            if (box.min.z() < floor_z) {
                rejected = "the chunk would reach the root's first layers";
                continue;
            }
            Matrix3d frame = minimal_frame(cand.dir);
            if (rotary) {
                ToolPose  pose { box.center(), cand.dir };
                JointPose joints;
                if (! params.kinematics->inverse(pose, nullptr, joints)) {
                    rejected = "the rotary axes cannot reach the build direction";
                    continue;
                }
                const Matrix3d head = params.kinematics->head_frame(joints);
                if ((head.row(2).transpose() - cand.dir).norm() < 1e-6 && head.determinant() > 0.)
                    frame = head;
                if (! full && box.min.z() + head_drop(params.head, frame) < 0.) {
                    rejected = "the tilted head would reach below the bed";
                    continue;
                }
            }
            if (full) {
                std::vector<const indexed_triangle_set *> earlier { &near };
                for (int pi = 0; pi < w.chunk_idx; ++pi)
                    earlier.push_back(&plan.chunks[size_t(pi)].piece);
                if (const HeadHit hit = head_blocked(far, frame, h, params.head, prepared_head, earlier, params.plates); ! hit.what.empty()) {
                    blocked.push_back({ cand.dir, cand.cut, cand.offset });
                    if (cand.cut != cand.dir) {
                        const double tilt = std::acos(std::clamp(cand.dir.z(), -1., 1.)) - 2. * TiltStep;
                        for (const bool tip_back : { false, true }) {
                            Candidate next = cand;
                            if (tip_back) {
                                if (! hit.bed || std::abs(0.5 * M_PI - tilt) > SideCutLean + EPSILON)
                                    continue;
                                next.dir = direction(tilt, std::atan2(cand.cut.y(), cand.cut.x()));
                            } else {
                                next.offset += SideCutStep;
                                next.stub += SideCutStep;
                            }
                            next.base = std::numeric_limits<double>::infinity();
                            for (const stl_vertex &v : c.piece.vertices)
                                if (next.cut.dot(v.cast<double>()) >= next.offset)
                                    next.base = std::min(next.base, next.dir.dot(v.cast<double>()));
                            const bool seen = std::any_of(tried_cuts.begin(), tried_cuts.end(), [&](const auto &t) {
                                return t.first.dot(next.dir) > 1. - 1e-9 && std::abs(t.second - next.offset) < EPSILON; });
                            if (! std::isfinite(next.base) || seen)
                                continue;
                            tried_cuts.emplace_back(next.dir, next.offset);
                            score(next);
                            if (next.score < current - seam)
                                candidates.insert(candidates.begin() + ci + 1, next);
                        }
                    }
                    std::ostringstream why;
                    why << hit.what << " (build direction " << std::lround(Geometry::rad2deg(std::acos(std::clamp(cand.dir.z(), -1., 1.))))
                        << " deg from vertical toward " << std::lround(Geometry::rad2deg(std::atan2(cand.dir.y(), cand.dir.x()))) << " deg, "
                        << (cand.cut == cand.dir ? "base plane" : "side cut") << ")";
                    rejected = why.str();
                    continue;
                }
            }

            Cut cut;
            MultiplanarChunk &chunk = cut.chunk;
            chunk.parent    = w.chunk_idx;
            chunk.build_dir = cand.dir;
            chunk.frame     = frame;
            chunk.base      = base;
            chunk.clips     = c.clips;
            chunk.base_clip = int(chunk.clips.size());
            chunk.clips.push_back({ -cand.cut, -cand.offset });
            chunk.piece     = std::move(far);
            set_score(chunk, cand.far);

            MultiplanarChunk rest = c;
            rest.clips.push_back({ cand.cut, cand.offset });
            if (printable_layers(chunk_piece(mesh, chunk, h), chunk, h) < MinChunkLayers ||
                printable_layers(chunk_piece(mesh, rest, h), rest, h) < MinChunkLayers) {
                rejected = "a side of the cut would slice into fewer than three layers";
                continue;
            }
            cut.parent_clips = std::move(rest.clips);
            cut.parent_piece = std::move(near);
            cut.gain         = current - cand.score;
            return cut;
        }
        return std::nullopt;
    };

    auto give_up = [&](const Work &w, const std::string &why) {
        double area = 0.;
        for (size_t i : w.cluster) {
            (w.goal == Goal::Overhang ? skipped : tried)[i] = true;
            area += f.area[i];
        }
        std::ostringstream note;
        note << (w.goal == Goal::Overhang ? "overhang" : "stair-stepped top") << " of " << area << " mm2 in chunk " << w.chunk_idx
             << " stays: " << why;
        plan.notes.push_back(note.str());
    };

    int planes = 0;
    constexpr size_t MaxGivenUp = 64;
    size_t searched_given_up = 0;
    while (planes < params.max_planes) {
        if (throw_if_canceled)
            throw_if_canceled();
        if ((full ? searched_given_up : plan.notes.size()) > MaxGivenUp) {
            plan.notes.push_back("search stopped after " + std::to_string(MaxGivenUp) + " seeds stayed");
            break;
        }
        std::array<Work, 2>              work { overhang_work(), {} };
        if (! full || work[0].chunk_idx < 0)
            work[1] = top_work();
        std::array<std::optional<Cut>, 2> cuts;
        for (size_t k = 0; k < work.size(); ++k) {
            Work &w = work[k];
            if (w.chunk_idx < 0)
                continue;
            double area = 0.;
            for (size_t i : w.cluster)
                area += f.area[i];
            std::string rejected;
            const bool too_small = w.goal == Goal::Overhang && area < params.min_overhang_area;
            if (too_small)
                rejected = "smaller than the minimum area";
            else
                cuts[k] = find_cut(w, rejected);
            if (! cuts[k]) {
                give_up(w, rejected);
                if (! too_small)
                    ++searched_given_up;
            }
        }
        if (! cuts[0] && ! cuts[1]) {
            if (work[0].chunk_idx < 0 && work[1].chunk_idx < 0)
                break;
            continue;
        }
        const size_t k   = ! cuts[1] || (cuts[0] && cuts[0]->gain >= cuts[1]->gain) ? 0 : 1;
        Cut         &cut = *cuts[k];
        const int    idx = cut.chunk.parent;
        MultiplanarChunk &parent = plan.chunks[size_t(idx)];
        parent.clips = std::move(cut.parent_clips);
        parent.piece = std::move(cut.parent_piece);
        set_score(parent, evaluate(parent.clips, frame_of(parent)));
        for (MultiplanarChunk &other : plan.chunks)
            if (other.parent > idx)
                ++other.parent;
        plan.chunks.insert(plan.chunks.begin() + idx + 1, std::move(cut.chunk));
        if (work[k].goal == Goal::Top)
            for (size_t i : work[k].cluster)
                tried[i] = true;
        ++planes;
    }
    return finish();
}

bool full_orientation(const PrintConfig &config)
{
    return layout_is_table(config.nonplanar_kinematics.value) && config.nonplanar_max_tilt.value > 90.;
}

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

namespace {

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
    set("nonplanar_mode", "manual");
    set("nonplanar_top_shells", "0");
    set("nonplanar_bottom_shells", "0");
    set("nonplanar_full_conformal_max_thickness", "0");
    set("nonplanar_overhangs", "0");
    set("nonplanar_bridges", "0");
    set("nonplanar_atom_toolpath", "");
    set("nonplanar_ironing", "0");
    const PrintRegionConfig &region0 = object.printing_region(0).config();
    const int                top_id  = std::max(1, region0.top_surface_filament_id.value);
    if (conformal_ironing_enabled(region0) && region0.ironing_type.value == IroningType::NoIroning &&
        road_material(object.print()->config().filament_type.get_at(size_t(top_id - 1))) != RoadMaterial::Flexible)
        set("ironing_type", "top");
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

std::shared_ptr<const MultiplanarPlan> clip_multiplanar_slices(PrintObject &object)
{
    const NonplanarMode mode = object.config().nonplanar_mode.value;
    if ((mode != NonplanarMode::Multiplanar && mode != NonplanarMode::Auto) || !object.config().nonplanar_atom_toolpath.value.empty())
        return {};
    const std::unique_ptr<ToolKinematics> kinematics = make_tool_kinematics(object.print()->config());
    if (mode == NonplanarMode::Auto && (! kinematics || kinematics->rotary_count() == 0))
        return {};
    if (mode == NonplanarMode::Auto && object.print()->config().spiral_mode.value)
        return {};
    const indexed_triangle_set mesh = slicing_frame_mesh(object);
    const bool full = full_orientation(object.print()->config());
    if (mode == NonplanarMode::Auto && ! full && ! auto_rotary_candidate(object, mesh))
        return {};
    MultiplanarParams params;
    params.layer_height       = object.config().layer_height.value;
    params.first_layer_height = object.print()->config().initial_layer_print_height.value;
    params.overhang_threshold = Geometry::deg2rad(object.config().multiplanar_overhang_threshold.value);
    params.max_planes         = object.config().multiplanar_max_planes.value;
    params.head               = resolve_head_envelope(object.print()->config());
    params.kinematics         = kinematics.get();
    if (const std::vector<Vec2d> &area = object.print()->config().printable_area.values; area.size() >= 3)
        for (const PrintInstance &instance : object.instances()) {
            Polygon plate;
            for (const Vec2d &p : area)
                plate.points.push_back(Point::new_scale(p.x(), p.y()) - instance.shift);
            params.plates.push_back(std::move(plate));
        }
    static thread_local std::pair<std::string, MultiplanarPlan> last;
    std::string key(reinterpret_cast<const char *>(mesh.vertices.data()), mesh.vertices.size() * sizeof(stl_vertex));
    key.append(reinterpret_cast<const char *>(mesh.indices.data()), mesh.indices.size() * sizeof(stl_triangle_vertex_indices));
    for (const double v : { params.layer_height, params.first_layer_height, params.overhang_threshold, double(params.max_planes) })
        key.append(reinterpret_cast<const char *>(&v), sizeof(v));
    for (const Polygon &plate : params.plates)
        key.append(reinterpret_cast<const char *>(plate.points.data()), plate.points.size() * sizeof(Point));
    for (const std::string &opt : object.print()->config().keys())
        key += opt + '=' + object.print()->config().opt_serialize(opt) + '\n';
    if (last.first != key)
        last = { std::move(key), plan_multiplanar(mesh, params, [&object]() { throw_if_canceled(object); }) };
    auto plan = std::make_shared<MultiplanarPlan>(last.second);
    for (const std::string &note : plan->notes)
        BOOST_LOG_TRIVIAL(info) << "Multiplanar " << object.model_object()->name << ": " << note;
    BOOST_LOG_TRIVIAL(info) << "Multiplanar " << object.model_object()->name << ": " << plan->chunks.size()
                            << " chunks, " << plan->residual_overhang_area << " mm2 of overhang left, stair-step area "
                            << plan->step_area << " mm2 (planar " << plan->planar_step_area << ")";
    for (size_t i = 0; i < plan->chunks.size(); ++i) {
        const MultiplanarChunk &c = plan->chunks[i];
        BOOST_LOG_TRIVIAL(info) << "Multiplanar chunk " << i << ": parent " << c.parent << " dir " << c.build_dir.transpose()
                                << " base " << c.base << " overhang " << c.overhang_area << " steps " << c.step_area;
    }
    if (mode == NonplanarMode::Auto && full && plan->planar_overhang_area - plan->residual_overhang_area >= params.min_overhang_area) {
        std::ostringstream why;
        why << "Auto mode prints " << object.model_object()->name << " in " << plan->chunks.size() << " chunks: the table turns "
            << std::lround(plan->planar_overhang_area - plan->residual_overhang_area) << " of its " << std::lround(plan->planar_overhang_area)
            << " mm2 of overhang upright (MULTIPLANAR.md \"Auto\").";
        plan->auto_reason = why.str();
        BOOST_LOG_TRIVIAL(warning) << plan->auto_reason;
    } else if (mode == NonplanarMode::Auto && (plan->auto_reason = auto_rotary_chunks(object, mesh, *plan)).empty())
        return {};

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
        indexed_triangle_set    piece = chunk_piece(mesh, chunk, h);
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
                if (path->role() == erIroning) {
                    size_t covered = 0;
                    for (const Point3 &pt : pts) {
                        const Vec3d p = to_object * Vec3d(unscale<double>(pt.x()) + center.x(), unscale<double>(pt.y()) + center.y(),
                                                          layer->print_z + chunk.base);
                        for (size_t k = 0; k < chunk.clips.size(); ++k)
                            if (int(k) != chunk.base_clip && std::abs(chunk.clips[k].normal.dot(p) - chunk.clips[k].offset) <= 0.75 * h) {
                                ++covered;
                                break;
                            }
                    }
                    if (2 * covered > pts.size())
                        continue;
                }
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
