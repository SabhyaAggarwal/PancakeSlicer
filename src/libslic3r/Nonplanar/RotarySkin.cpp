#include "RotarySkin.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ClearanceVerifier.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/PatchTools.hpp"
#include "libslic3r/Nonplanar/Repair.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <tbb/parallel_for.h>

#include <math.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <memory>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double Deg = M_PI / 180.;


constexpr double MinFaceTilt = 39. * Deg;


constexpr double MaxFaceTilt = 80. * Deg;

constexpr double MinCoverage = 0.9;

constexpr double MinNormalZ = -0.1;


constexpr double SharpEdge    = 20. * Deg;
constexpr double MinEdgeBound = 0.8;


constexpr double MaxResidual = 32. * Deg;

constexpr double TiltReserve = 1. * Deg;
constexpr double Sample = 0.5;
constexpr double MinRun = 3.;
constexpr double Spacing = 0.4;
constexpr double MinRoadLayers = 3;
constexpr double TableMargin = 0.6;
constexpr int    SkinPasses = 2;
constexpr double EndBand = 1.6;
constexpr double GlideRaise = 0.3;
constexpr double Pitch = 0.25;

double polyline_length(const std::vector<Vec2d> &p, bool closed)
{
    double l = 0.;
    for (size_t i = 1; i < p.size(); ++i)
        l += (p[i] - p[i - 1]).norm();
    if (closed && p.size() > 1)
        l += (p.front() - p.back()).norm();
    return l;
}


Vec2d point_at(const std::vector<Vec2d> &p, const std::vector<double> &cum, bool closed, double s)
{
    const double total = cum.back();
    if (closed)
        s = std::fmod(std::fmod(s, total) + total, total);
    else
        s = std::clamp(s, 0., total);
    const size_t i = std::min(size_t(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin()), cum.size() - 1);
    if (i == 0)
        return p.front();
    const Vec2d &a = p[i - 1], &b = closed && i == p.size() ? p.front() : p[i];
    const double seg = cum[i] - cum[i - 1];
    return seg > 1e-12 ? a + (b - a) * ((s - cum[i - 1]) / seg) : a;
}


Vec2d tangent_at(const std::vector<Vec2d> &p, const std::vector<double> &cum, bool closed, double s)
{
    const double total = cum.back();
    s = closed ? std::fmod(std::fmod(s, total) + total, total) : std::clamp(s, 0., total);
    size_t i = std::min(size_t(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin()), cum.size() - 1);
    i = std::max<size_t>(i, 1);
    const Vec2d d = (closed && i == p.size() ? p.front() : p[std::min(i, p.size() - 1)]) - p[i - 1];
    return d.norm() > 1e-12 ? Vec2d(d.normalized()) : Vec2d(Vec2d::UnitX());
}


struct Stretch
{
    double              width = 0., height = 0.;
    size_t              loop = 0;
    double              s0 = 0.;
    std::vector<Vec2d>  pts;
    std::vector<double> cum;
    bool                closed = false;
    bool                right_out = true;
    std::vector<double> sharp;
    double              length() const { return cum.back(); }
};

void finish(Stretch &s)
{
    s.cum.assign(1, 0.);
    for (size_t i = 1; i < s.pts.size(); ++i)
        s.cum.emplace_back(s.cum.back() + (s.pts[i] - s.pts[i - 1]).norm());
    if (s.closed)
        s.cum.emplace_back(s.cum.back() + (s.pts.front() - s.pts.back()).norm());
}


std::pair<double, double> project(const Stretch &s, const Vec2d &p)
{
    double best = std::numeric_limits<double>::infinity(), at = 0.;
    const size_t n = s.closed ? s.pts.size() : s.pts.size() - 1;
    for (size_t i = 0; i < n; ++i) {
        const Vec2d &a = s.pts[i], &b = s.pts[(i + 1) % s.pts.size()];
        const Vec2d  d = b - a;
        const double l2 = d.squaredNorm();
        const double t  = l2 > 1e-12 ? std::clamp((p - a).dot(d) / l2, 0., 1.) : 0.;
        const double dist = (a + t * d - p).norm();
        if (dist < best)
            best = dist, at = s.cum[i] + t * std::sqrt(l2);
    }
    return { best, at };
}






std::vector<int> steep_surfaces(const indexed_triangle_set &its, const Matrix3d &frame, size_t &count)
{
    const size_t       n = its.indices.size();
    std::vector<Vec3d> normal(n);
    std::vector<int>   parent(n);
    auto vertex = [&](size_t f, int k) { return Vec3d(frame * its.vertices[size_t(its.indices[f][k])].cast<double>()); };
    for (size_t f = 0; f < n; ++f) {
        const Vec3d  c = (vertex(f, 1) - vertex(f, 0)).cross(vertex(f, 2) - vertex(f, 0));
        normal[f]      = c.norm() > 1e-12 ? Vec3d(c.normalized()) : Vec3d(Vec3d::Zero());
        const double beta = std::acos(std::clamp(normal[f].z(), -1., 1.));
        parent[f] = c.norm() > 1e-12 && beta >= MinFaceTilt - 2. * Deg && beta <= MaxFaceTilt + 2. * Deg && normal[f].z() >= MinNormalZ - 0.03 ? int(f) : -1;
    }
    std::function<int(int)> find = [&](int f) { return parent[size_t(f)] == f ? f : parent[size_t(f)] = find(parent[size_t(f)]); };
    const std::vector<Vec3i32> neighbours = its_face_neighbors(its);
    for (size_t f = 0; f < n; ++f)
        for (int k = 0; k < 3; ++k)
            if (const int g = neighbours[f][k]; g >= 0 && parent[f] >= 0 && parent[size_t(g)] >= 0)
                parent[size_t(find(int(f)))] = find(g);

    std::map<int, std::pair<double, double>> outline;
    for (size_t f = 0; f < n; ++f) {
        if (parent[f] < 0)
            continue;
        const int root = find(int(f));
        for (int k = 0; k < 3; ++k) {
            const int g = neighbours[f][k];
            if (g >= 0 && parent[size_t(g)] >= 0 && find(g) == root)
                continue;
            const double length = (vertex(f, (k + 1) % 3) - vertex(f, k)).norm();
            auto &[total, sharp] = outline[root];
            total += length;
            if (g < 0 || normal[f].dot(normal[size_t(g)]) <= std::cos(SharpEdge))
                sharp += length;
        }
    }
    std::vector<int>   surface(n, -1);
    std::map<int, int> ids;
    for (size_t f = 0; f < n; ++f)
        if (parent[f] >= 0) {
            const auto &[total, sharp] = outline[find(int(f))];
            if (total <= 0. || sharp >= MinEdgeBound * total)
                surface[f] = ids.emplace(find(int(f)), int(ids.size())).first->second;
        }
    count = ids.size();
    return surface;
}


struct Pose
{
    bool  ok = false;
    Vec3d surface { Vec3d::Zero() };
    Vec3d normal { Vec3d::UnitZ() };
    Vec3d axis { Vec3d::UnitZ() };
    double residual = 0.;
    double thickness = 0.;
    int    component = -1;
};

class Planner
{
public:

    Planner(const PrintObject &object, const ObjectMesh &mesh, const std::vector<int> &band_of)
        : m_object(object), m_mesh(mesh), m_kin(make_tool_kinematics(object.print()->config())),
          m_head(resolve_head_envelope(object.print()->config())), m_nozzle(object.print()->config().nozzle_diameter.get_at(0))
    {



        m_check = m_head;
        m_check.boxes.clear();
        for (HeadBox box : m_head.boxes) {
            if (box.infinite_x || box.infinite_y)
                continue;
            if (!box.nozzle) {
                box.min.head<2>().array() -= m_head.lateral_margin;
                box.max.head<2>().array() += m_head.lateral_margin;
                box.min.z() -= m_head.margin;
                box.max.z() += m_head.margin;
                box.nozzle = true;
                m_check.boxes.emplace_back(box);
            }
            m_table.emplace_back(box);
        }
        m_check.margin = m_check.lateral_margin = 0.;

        const indexed_triangle_set &its = mesh.mesh.its;
        m_component = steep_surfaces(its, Matrix3d::Identity(), m_components);

        BoundingBoxf bbox;
        for (const Layer *layer : object.layers())
            for (const ExPolygon &e : layer->lslices)
                for (const Point &p : e.contour.points)
                    bbox.merge(unscale(p));
        const double reach = m_head.reach() + 5.;
        bbox.min.array() -= reach;
        bbox.max.array() += reach;
        MaterialColumns columns(bbox, Pitch);
        const auto &layers = object.layers();
        for (size_t l = 0; l < layers.size(); ++l) {
            const Layer      *layer = layers[l];
            const BoundingBox lb    = get_extents(layer->lslices);
            if (lb.defined) {
                Polylines hatch;
                for (coord_t y = lb.min.y() + scale_(0.5 * Pitch); y < lb.max.y(); y += scale_(Pitch))
                    hatch.emplace_back(Point(lb.min.x() - coord_t(scale_(1.)), y), Point(lb.max.x() + coord_t(scale_(1.)), y));


                for (const Polyline &pl : intersection_pl(hatch, offset_ex(layer->lslices, -scale_(0.6 * Pitch))))
                    for (size_t i = 1; i < pl.points.size(); ++i) {
                        const Vec2d a = unscale(pl.points[i - 1]), b = unscale(pl.points[i]);
                        columns.deposit(Vec3d(a.x(), a.y(), layer->print_z), Vec3d(b.x(), b.y(), layer->print_z), Pitch);
                    }
            }
            if (l + 1 == layers.size() || band_of[l + 1] != band_of[l])
                m_columns.emplace_back(std::make_unique<MaterialColumns>(columns));
        }
    }




    Pose pose(const Vec2d &q, const Vec2d &outward, double print_z, double height, double width, int band) const
    {
        Pose out;


        const double z_mid = print_z - 0.5 * height;
        const Vec2d  edge  = q + 0.5 * width * outward;
        const Vec3d  query(edge.x(), edge.y(), z_mid - m_mesh.ground_offset);
        int          face = -1;
        Vec3d        c;
        const double d2 = m_mesh.imesh.squared_distance(query, face, c);
        if (face < 0)
            return out;

        if (std::sqrt(d2) > width + height)
            return out;
        Vec3d n = m_mesh.imesh.normal_by_face_id(face).normalized();
        if (n.head<2>().dot(outward) < 0.)
            return out;
        const double nz = std::clamp(n.z(), -1., 1.);
        const double beta = std::acos(nz);
        if (beta < MinFaceTilt || beta > MaxFaceTilt || nz < MinNormalZ || m_component[size_t(face)] < 0)
            return out;
        out.component = m_component[size_t(face)];

        if (const RepairHints *repair = m_object.nonplanar_repair(); repair != nullptr)
            for (const RepairHints::Disc &disc : repair->upright)
                if ((q - disc.p).squaredNorm() <= disc.radius * disc.radius && (std::isnan(disc.z) || std::abs(z_mid - disc.z) <= disc.radius))
                    return out;
        const Vec2d nh = n.head<2>().normalized();
        const double phi = std::atan2(nh.y(), nh.x());
        out.surface = c + Vec3d(0., 0., m_mesh.ground_offset);
        out.normal  = n;

        const double cap = std::min(beta, m_kin->max_tilt(phi) - TiltReserve);
        for (double tau = cap; tau >= beta - MaxResidual - 1e-9 && tau >= 0.; tau -= 2. * Deg) {
            const Vec3d axis(std::sin(tau) * nh.x(), std::sin(tau) * nh.y(), std::cos(tau));
            JointPose   joints;
            if (!m_kin->inverse(ToolPose{ Vec3d::Zero(), axis }, nullptr, joints) || !this->clears(out.surface, m_kin->head_frame(joints), band))
                continue;
            out.ok       = true;
            out.axis     = axis;
            out.residual = beta - tau;


            out.thickness = std::min((width - height * (1. - 0.25 * M_PI)) * std::sin(beta), 0.9 * m_nozzle);
            return out;
        }
        return out;
    }

private:
    bool clears(const Vec3d &tip, const Matrix3d &frame, int band) const
    {

        for (const HeadBox &box : m_table)
            for (int k = 0; k < 8; ++k) {
                const Vec3d v = tip + frame.transpose() * Vec3d(k & 1 ? box.max.x() : box.min.x(), k & 2 ? box.max.y() : box.min.y(),
                                                                k & 4 ? box.max.z() : box.min.z());
                if (v.z() < TableMargin)
                    return false;
            }
        return m_columns[size_t(band)]->tilted_clearance(tip, frame, m_check) >= 0.;
    }

    const PrintObject             &m_object;
    const ObjectMesh              &m_mesh;
    std::unique_ptr<ToolKinematics> m_kin;
    HeadEnvelope                   m_head, m_check;
    double                         m_nozzle = 0.4;
    std::vector<int>               m_component;
public:
    size_t                         m_components = 0;
private:
    std::vector<HeadBox>           m_table;
    std::vector<std::unique_ptr<MaterialColumns>> m_columns;
};


struct WallLoop
{
    ExtrusionEntityCollection *owner = nullptr;
    size_t                     index = 0;
    std::vector<Vec2d>         pts;
    std::vector<double>        cum;
    double                     width = 0., height = 0.;
    bool                       right_out = true;
    std::vector<std::pair<double, double>> skinned;
    std::vector<uint8_t>                   cover;
};

void collect_loops(ExtrusionEntityCollection &collection, std::vector<WallLoop> &out)
{
    for (size_t i = 0; i < collection.entities.size(); ++i) {
        ExtrusionEntity *ee = collection.entities[i];
        if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            collect_loops(*sub, out);
            continue;
        }
        auto *loop = dynamic_cast<ExtrusionLoop *>(ee);
        if (loop == nullptr || loop->paths.empty() ||
            !std::all_of(loop->paths.begin(), loop->paths.end(), [](const ExtrusionPath &p) {
                return is_external_perimeter(p.role()) || p.role() == erOverhangPerimeter;
            }))
            continue;
        WallLoop wl;
        wl.owner = &collection;
        wl.index = i;
        for (const ExtrusionPath &path : loop->paths)
            for (const Point3 &p : path.polyline.points) {
                const Vec2d v = unscale(p.to_point());
                if (wl.pts.empty() || (wl.pts.back() - v).squaredNorm() > 1e-10)
                    wl.pts.emplace_back(v);
            }
        if (wl.pts.size() > 2 && (wl.pts.front() - wl.pts.back()).squaredNorm() < 1e-10)
            wl.pts.pop_back();
        if (wl.pts.size() < 3)
            continue;
        wl.width  = loop->paths.front().width;
        wl.height = loop->paths.front().height;
        wl.cum.assign(1, 0.);
        for (size_t k = 1; k <= wl.pts.size(); ++k)
            wl.cum.emplace_back(wl.cum.back() + (wl.pts[k % wl.pts.size()] - wl.pts[k - 1]).norm());
        out.emplace_back(std::move(wl));
    }
}


void remove_skinned(WallLoop &wl)
{
    auto *loop = dynamic_cast<ExtrusionLoop *>(wl.owner->entities[wl.index]);
    const double total = wl.cum.back();

    std::vector<std::pair<double, double>> skinned = wl.skinned;
    std::sort(skinned.begin(), skinned.end());
    std::vector<std::pair<double, double>> keep;
    if (skinned.size() == 1 && skinned.front().second - skinned.front().first >= total - 1e-6) {

    } else
        for (size_t i = 0; i < skinned.size(); ++i) {
            const double from = skinned[i].second;
            const double to   = i + 1 < skinned.size() ? skinned[i + 1].first : skinned.front().first + total;
            if (to - from > 1e-3)
                keep.emplace_back(from, to);
        }


    ExtrusionEntitiesPtr replacement;
    for (const auto &[from, to] : keep) {
        ExtrusionPaths pieces;
        double         offset = 0.;
        for (int lap = 0; lap < 2; ++lap)
            for (const ExtrusionPath &path : loop->paths) {
                Polyline3 piece;
                for (size_t k = 0; k + 1 < path.polyline.points.size(); ++k) {
                    const Point3 &a = path.polyline.points[k], &b = path.polyline.points[k + 1];
                    const double  l = unscale<double>((b.to_point() - a.to_point()).cast<double>().norm());
                    const double  s0 = std::max(from, offset), s1 = std::min(to, offset + l);
                    if (s1 > s0 + 1e-6 && l > 1e-9) {
                        auto at = [&](double s) {
                            const double t = (s - offset) / l;
                            return Point3((a.cast<double>() + t * (b - a).cast<double>()).cast<coord_t>());
                        };
                        if (piece.points.empty())
                            piece.points.emplace_back(at(s0));
                        piece.points.emplace_back(at(s1));
                    }
                    offset += l;
                }
                if (piece.points.size() >= 2)
                    pieces.emplace_back(std::move(piece), path);
            }
        if (!pieces.empty())
            replacement.emplace_back(new ExtrusionMultiPath(std::move(pieces)));
    }
    delete wl.owner->entities[wl.index];
    ExtrusionEntitiesPtr &entities = wl.owner->entities;
    entities.erase(entities.begin() + wl.index);
    entities.insert(entities.begin() + wl.index, replacement.begin(), replacement.end());
}

struct RoadPoint
{
    size_t layer;
    size_t stretch = 0;
    double s = 0.;
    Vec2d  q;
    Vec2d  outward { Vec2d::UnitX() };
    double width = 0., height = 0.;
    double spacing = Spacing;
    double corner = std::numeric_limits<double>::infinity();
    Pose   pose;
};

struct Road
{
    std::vector<RoadPoint> points;
    std::map<int, int>     next;
    bool                   alive = true;
    double                 azimuth = 0.;
};

}

bool rotary_skin_claims(const PrintObject &object)
{
    if (!object.config().nonplanar_steep_skin.value)
        return false;
    const indexed_triangle_set its     = object.model_object()->raw_indexed_triangle_set();
    const Matrix3d             frame   = object.trafo().linear();
    size_t                     count   = 0;
    const std::vector<int>     surface = steep_surfaces(its, frame, count);
    std::vector<Vec3d>         v(its.vertices.size());
    double                     z_min = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < v.size(); ++i)
        z_min = std::min(z_min, (v[i] = frame * its.vertices[i].cast<double>()).z());
    double steep = 0., visible = 0.;
    for (size_t f = 0; f < its.indices.size(); ++f) {
        const stl_triangle_vertex_indices &face = its.indices[f];
        const Vec3d &a = v[size_t(face[0])], &b = v[size_t(face[1])], &c = v[size_t(face[2])];
        const Vec3d  n   = (b - a).cross(c - a);
        const double len = n.norm();
        if (len <= 1e-12)
            continue;
        const double nz = n.z() / len, beta = std::acos(std::clamp(nz, -1., 1.));

        if (nz < -0.999 && std::max({ a.z(), b.z(), c.z() }) < z_min + 0.05)
            continue;
        visible += 0.5 * len;
        if (surface[f] >= 0 && beta >= MinFaceTilt && beta <= MaxFaceTilt && nz >= MinNormalZ)
            steep += 0.5 * len;
    }
    return steep >= SkinClaimArea && steep >= SkinClaimShare * visible;
}

void build_rotary_skin(PrintObject &object, ConformalResult &result)
{
    const auto &layers = object.layers();
    if (layers.size() < 4)
        return;


    const double band_height = 0.;
    std::vector<int> band_of(layers.size(), 0);
    for (size_t l = 0; l < layers.size(); ++l)
        band_of[l] = band_height > 0. ? int(std::floor(layers[l]->print_z / band_height + 1e-6)) : 0;

    {
        int next = 0, last = band_of[0];
        band_of[0] = 0;
        for (size_t l = 1; l < layers.size(); ++l) {
            const int raw = band_of[l];
            if (raw != last)
                ++next, last = raw;
            band_of[l] = next;
        }
    }
    const int         bands = band_of.back() + 1;
    const ObjectMesh &mesh  = object_mesh(object, result);
    const Planner     planner(object, mesh, band_of);


    std::vector<std::vector<WallLoop>> loops(layers.size());
    std::vector<std::vector<Stretch>>  stretches(layers.size());
    int region_id = -1;
    for (size_t l = 1; l < layers.size(); ++l)
        for (size_t r = 0; r < layers[l]->regions().size(); ++r) {
            const size_t before = loops[l].size();
            collect_loops(layers[l]->regions()[r]->perimeters, loops[l]);
            if (region_id < 0 && loops[l].size() > before)
                region_id = int(r);
        }
    if (region_id < 0)
        return;

    for (size_t l = 1; l < layers.size(); ++l)
        for (WallLoop &wl : loops[l]) {
            size_t best = 0;
            for (size_t k = 1; k < wl.pts.size(); ++k)
                if (wl.cum[k + 1] - wl.cum[k] > wl.cum[best + 1] - wl.cum[best])
                    best = k;
            const Vec2d a = wl.pts[best], b = wl.pts[(best + 1) % wl.pts.size()], d = (b - a).normalized();
            const Vec2d probe = 0.5 * (a + b) + wl.width * Vec2d(d.y(), -d.x());
            const Point pp(scale_(probe.x()), scale_(probe.y()));
            wl.right_out = std::none_of(layers[l]->lslices.begin(), layers[l]->lslices.end(), [&pp](const ExPolygon &e) { return e.contains(pp); });
        }
    auto outward = [](const std::vector<Vec2d> &pts, const std::vector<double> &cum, bool closed, double at, bool right_out) {
        const Vec2d d = tangent_at(pts, cum, closed, at);
        return right_out ? Vec2d(d.y(), -d.x()) : Vec2d(-d.y(), d.x());
    };

    std::vector<std::vector<std::vector<Pose>>> samples(layers.size());
    tbb::parallel_for(size_t(1), layers.size(), [&](size_t l) {
        const Layer &layer = *layers[l];
        samples[l].resize(loops[l].size());
        for (size_t w = 0; w < loops[l].size(); ++w) {
            const WallLoop &wl    = loops[l][w];
            const double    total = wl.cum.back();
            const size_t    n     = std::max<size_t>(8, size_t(std::ceil(total / Sample)));
            for (size_t k = 0; k < n; ++k)
                samples[l][w].emplace_back(planner.pose(point_at(wl.pts, wl.cum, true, total * k / n),
                                                        outward(wl.pts, wl.cum, true, total * k / n, wl.right_out), layer.print_z,
                                                        wl.height, wl.width, band_of[l]));
        }
    });

    std::vector<size_t> steep_total(planner.m_components, 0), steep_ok(planner.m_components, 0);
    for (const auto &layer_samples : samples)
        for (const auto &loop_samples : layer_samples)
            for (const Pose &p : loop_samples)
                if (p.component >= 0)
                    ++steep_total[size_t(p.component)], steep_ok[size_t(p.component)] += p.ok;
    std::vector<uint8_t> whole(planner.m_components, 0);
    for (size_t c = 0; c < whole.size(); ++c)
        whole[c] = steep_total[c] > 0 && double(steep_ok[c]) >= MinCoverage * double(steep_total[c]);
    tbb::parallel_for(size_t(1), layers.size(), [&](size_t l) {
        for (size_t w = 0; w < loops[l].size(); ++w) {
            WallLoop    &wl    = loops[l][w];
            const double total = wl.cum.back();
            const size_t n     = samples[l][w].size();
            std::vector<uint8_t> ok(n);
            for (size_t k = 0; k < n; ++k)
                ok[k] = samples[l][w][k].ok && whole[size_t(samples[l][w][k].component)];
            const double step = total / n;
            if (std::all_of(ok.begin(), ok.end(), [](uint8_t v) { return v != 0; })) {
                wl.skinned.emplace_back(0., total);
                continue;
            }

            const size_t start = size_t(std::find(ok.begin(), ok.end(), uint8_t(0)) - ok.begin());
            for (size_t k = 1; k <= n; ++k) {
                const size_t i = (start + k) % n;
                if (!ok[i])
                    continue;
                size_t j = k;
                while (j + 1 <= n && ok[(start + j + 1) % n])
                    ++j;

                const double s0 = (double(start + k) - 0.5) * step, s1 = (double(start + j) + 0.5) * step;
                if (s1 - s0 >= MinRun)
                    wl.skinned.emplace_back(s0, s1);
                k = j;
            }
        }
    });
    for (size_t l = 1; l < layers.size(); ++l)
        for (size_t i = 0; i < loops[l].size(); ++i)
            for (const auto &iv : loops[l][i].skinned) {
                const WallLoop &wl = loops[l][i];
                Stretch s;
                s.width  = wl.width;
                s.height = wl.height;
                s.loop   = i;
                s.s0     = iv.first;
                s.right_out = wl.right_out;
                s.closed = iv.second - iv.first >= wl.cum.back() - 1e-6;
                const double length = iv.second - iv.first;
                const size_t n = std::max<size_t>(2, size_t(std::ceil(length / 0.1)));
                for (size_t k = 0; k < (s.closed ? n : n + 1); ++k)
                    s.pts.emplace_back(point_at(wl.pts, wl.cum, true, iv.first + length * k / n));
                finish(s);



                const double total = wl.cum.back();
                const size_t m     = size_t(std::ceil(total / 0.05));
                std::vector<uint8_t> turning(m);
                for (size_t k = 0; k < m; ++k) {
                    const double at = total * double(k) / double(m);
                    turning[k] = tangent_at(wl.pts, wl.cum, true, at - 0.25).dot(tangent_at(wl.pts, wl.cum, true, at + 0.25)) < std::cos(30. * Deg);
                }
                if (std::find(turning.begin(), turning.end(), uint8_t(0)) != turning.end())
                    for (size_t k = 0; k < m; ++k) {
                        if (!turning[k] || turning[(k + m - 1) % m])
                            continue;
                        size_t j = k;
                        while (turning[(j + 1) % m] && j - k < m)
                            ++j;
                        const double mid = std::fmod(total * (double(k) + double(j)) / (2. * double(m)), total);
                        const double at  = std::fmod(mid - iv.first + 2. * total, total);
                        if (s.closed || at <= length)
                            s.sharp.emplace_back(at);
                    }
                stretches[l].emplace_back(std::move(s));
            }



    std::vector<Road> roads;
    struct Live { int road; size_t stretch; double s; };
    std::vector<Live> live;
    for (size_t l = 1; l < layers.size(); ++l) {
        const Layer &layer = *layers[l];
        const double dmax  = std::max(0.8, 2. * layer.height / std::tan(MinFaceTilt));
        std::vector<Live> next;
        for (const Live &lv : live) {
            const Vec2d p = roads[lv.road].points.back().q;
            double best = dmax;
            Live   to{ lv.road, size_t(-1), 0. };
            for (size_t k = 0; k < stretches[l].size(); ++k)
                if (auto [d, s] = project(stretches[l][k], p); d < best)
                    best = d, to.stretch = k, to.s = s;
            if (to.stretch != size_t(-1))
                next.emplace_back(to);
        }

        std::vector<Live> placed;
        for (size_t k = 0; k < stretches[l].size(); ++k) {
            const Stretch &st = stretches[l][k];
            const double   total = st.length();
            std::vector<Live> on;
            for (const Live &lv : next)
                if (lv.stretch == k)
                    on.emplace_back(lv);
            std::sort(on.begin(), on.end(), [](const Live &a, const Live &b) { return a.s < b.s; });
            std::vector<Live> kept;
            for (const Live &lv : on)
                if (kept.empty() || lv.s - kept.back().s >= 0.55 * Spacing)
                    kept.emplace_back(lv);
            if (st.closed && kept.size() > 1 && kept.front().s + total - kept.back().s < 0.55 * Spacing)
                kept.pop_back();

            std::vector<double> at;
            for (const Live &lv : kept)
                at.emplace_back(lv.s);
            std::vector<double> born;
            auto fill = [&](double a, double b) {
                const int m = int(std::lround((b - a) / Spacing)) - 1;
                if (b - a > 1.45 * Spacing && m > 0)
                    for (int i = 1; i <= m; ++i)
                        born.emplace_back(a + (b - a) * i / (m + 1));
            };
            if (at.empty()) {
                const int m = std::max(1, int(std::lround(total / Spacing)));
                for (int i = 0; i < m; ++i)
                    born.emplace_back(st.closed ? total * i / m : total * (i + 0.5) / m);
            } else {
                for (size_t i = 0; i + 1 < at.size(); ++i)
                    fill(at[i], at[i + 1]);
                if (st.closed)
                    fill(at.back(), at.front() + total);
                else {
                    fill(-0.5 * Spacing, at.front());
                    fill(at.back(), total + 0.5 * Spacing);
                }
            }
            for (double s : born) {
                if (!st.closed && (s < 0. || s > total))
                    continue;
                roads.emplace_back();
                kept.push_back({ int(roads.size()) - 1, k, st.closed ? std::fmod(s + total, total) : s });
            }
            std::sort(kept.begin(), kept.end(), [](const Live &a, const Live &b) { return a.s < b.s; });

            for (size_t i = 0; i < kept.size(); ++i) {
                const bool   wrap_l = st.closed && kept.size() > 1, has_l = i > 0 || wrap_l, has_r = i + 1 < kept.size() || wrap_l;
                const double gl = has_l ? (i > 0 ? kept[i].s - kept[i - 1].s : kept[i].s + total - kept.back().s) : 2. * kept[i].s;
                const double gr = has_r ? (i + 1 < kept.size() ? kept[i + 1].s - kept[i].s : kept.front().s + total - kept[i].s) :
                                          2. * (total - kept[i].s);
                RoadPoint rp;
                rp.layer   = l;
                rp.stretch = k;
                rp.s       = kept[i].s;
                rp.outward = outward(st.pts, st.cum, st.closed, kept[i].s, st.right_out);
                rp.q       = point_at(st.pts, st.cum, st.closed, kept[i].s);
                rp.width   = st.width;
                rp.height  = st.height;
                rp.spacing = std::clamp(0.5 * (gl + gr), 0.55 * Spacing, 1.6 * Spacing);
                for (const double v : st.sharp) {
                    double d = v - kept[i].s;
                    if (st.closed)
                        d = std::remainder(d, total);
                    if (std::abs(d) < std::abs(rp.corner))
                        rp.corner = d;
                }
                roads[kept[i].road].points.emplace_back(rp);
                if (has_r)
                    ++roads[kept[i].road].next[kept[(i + 1) % kept.size()].road];
                placed.emplace_back(kept[i]);
            }
        }
        live = std::move(placed);
    }


    tbb::parallel_for(size_t(0), roads.size(), [&](size_t r) {

        Road &road = roads[r];
        size_t reached = 0;
        Vec2d  sum     = Vec2d::Zero();
        for (RoadPoint &rp : road.points) {
            rp.pose = planner.pose(rp.q, rp.outward, layers[rp.layer]->print_z, rp.height, rp.width, band_of[rp.layer]);
            if (rp.pose.ok && !whole[size_t(rp.pose.component)])
                rp.pose.ok = false;
            if (rp.pose.ok)
                ++reached, sum += rp.pose.normal.head<2>();
        }



        std::vector<size_t> okay;
        for (size_t i = 0; i < road.points.size(); ++i)
            if (road.points[i].pose.ok)
                okay.emplace_back(i);
        for (size_t k = 1; k + 1 < okay.size(); ++k) {
            const Vec3d &prev = road.points[okay[k - 1]].pose.normal, &next = road.points[okay[k + 1]].pose.normal;
            RoadPoint   &rp   = road.points[okay[k]];
            const double c    = std::cos(6. * Deg);
            if (prev.dot(next) >= c && rp.pose.normal.dot(prev) < c && rp.pose.normal.dot(next) < c)
                rp.pose.ok = false, --reached;
        }
        road.alive   = reached >= MinRoadLayers;
        road.azimuth = std::atan2(sum.y(), sum.x());
    });




    std::vector<uint8_t> corner(roads.size(), 0);
    for (size_t r = 0; r < roads.size(); ++r)
        for (const auto &[to, count] : roads[r].next)
            if (roads[r].alive && roads[size_t(to)].alive &&
                std::abs(std::remainder(roads[r].azimuth - roads[size_t(to)].azimuth, 2. * M_PI)) > 30. * Deg)
                corner[r] = corner[size_t(to)] = 1;




    std::vector<uint8_t> rim(roads.size(), 0);
    {
        std::vector<std::vector<int>> adjacent(roads.size());
        for (size_t r = 0; r < roads.size(); ++r)
            for (const auto &[to, count] : roads[r].next)
                adjacent[r].emplace_back(to), adjacent[size_t(to)].emplace_back(int(r));
        std::vector<int> hops(roads.size(), std::numeric_limits<int>::max());
        std::vector<int> queue;
        for (size_t r = 0; r < roads.size(); ++r)
            if (!roads[r].alive || adjacent[r].size() < 2 || corner[r])
                hops[r] = roads[r].alive && !corner[r] ? 1 : 0, queue.emplace_back(int(r));
        for (size_t q = 0; q < queue.size(); ++q)
            for (int to : adjacent[size_t(queue[q])])
                if (hops[size_t(to)] > hops[size_t(queue[q])] + 1)
                    hops[size_t(to)] = hops[size_t(queue[q])] + 1, queue.emplace_back(to);
        for (size_t r = 0; r < roads.size(); ++r)
            rim[r] = hops[r] <= 2;
    }




    {
        constexpr double Cell = 0.05;
        for (size_t l = 1; l < layers.size(); ++l)
            for (WallLoop &wl : loops[l]) {
                wl.skinned.clear();
                wl.cover.assign(size_t(std::ceil(wl.cum.back() / Cell)), 0);
            }
        auto cells = [&](const RoadPoint &rp, auto &&fn) {
            const Stretch  &st = stretches[rp.layer][rp.stretch];
            WallLoop       &wl = loops[rp.layer][st.loop];
            const double    total = wl.cum.back();
            const long      n = long(wl.cover.size());
            const double    at = st.s0 + rp.s;

            double lo = at - 0.5 * rp.spacing, hi = at + 0.5 * rp.spacing;
            if (std::abs(rp.corner) < rp.spacing)
                (rp.corner > 0. ? hi : lo) = at + rp.corner;
            for (long c = long(std::floor(lo / Cell)); c <= long(std::floor(hi / Cell)); ++c)
                fn(wl, size_t(((c % n) + n) % n));
            (void) total;
        };


        for (Road &road : roads)
            for (size_t i = road.points.size(); i-- > 0;)
                if (road.points[i].pose.ok) {
                    road.points[i].pose.ok = false;
                    break;
                }
        for (const Road &road : roads)
            if (road.alive)
                for (const RoadPoint &rp : road.points)
                    if (rp.pose.ok)
                        cells(rp, [](WallLoop &wl, size_t c) { wl.cover[c] = 1; });
        for (size_t l = 1; l < layers.size(); ++l)
            for (WallLoop &wl : loops[l]) {
                const size_t n = wl.cover.size();
                if (n == 0 || std::none_of(wl.cover.begin(), wl.cover.end(), [](uint8_t v) { return v != 0; }))
                    continue;
                const double total = wl.cum.back(), cell = total / double(n);
                if (std::all_of(wl.cover.begin(), wl.cover.end(), [](uint8_t v) { return v != 0; })) {
                    wl.skinned.emplace_back(0., total);
                    continue;
                }
                const size_t start = size_t(std::find(wl.cover.begin(), wl.cover.end(), uint8_t(0)) - wl.cover.begin());
                std::vector<uint8_t> kept(n, 0);
                for (size_t k = 1; k <= n; ++k) {
                    if (!wl.cover[(start + k) % n])
                        continue;
                    size_t j = k;
                    while (j + 1 <= n && wl.cover[(start + j + 1) % n])
                        ++j;
                    if (double(j - k + 1) * cell >= MinRun) {
                        wl.skinned.emplace_back(double(start + k) * cell, double(start + j + 1) * cell);
                        for (size_t m = k; m <= j; ++m)
                            kept[(start + m) % n] = 1;
                    }
                    k = j;
                }
                wl.cover.swap(kept);
            }

        for (Road &road : roads)
            for (RoadPoint &rp : road.points)
                if (rp.pose.ok) {
                    const Stretch &st = stretches[rp.layer][rp.stretch];
                    const WallLoop &wl = loops[rp.layer][st.loop];
                    const long n = long(wl.cover.size());
                    const long c = long(std::floor((st.s0 + rp.s) / Cell));
                    if (n == 0 || !wl.cover[size_t(((c % n) + n) % n)])
                        rp.pose.ok = false;
                }
    }
    size_t removed = 0;

    for (size_t l = 1; l < layers.size(); ++l)
        for (size_t i = loops[l].size(); i-- > 0;)
            if (!loops[l][i].skinned.empty()) {
                remove_skinned(loops[l][i]);
                ++removed;
            }
    if (removed == 0)
        return;



    const auto  kin = make_tool_kinematics(object.print()->config());
    double      wrap_azimuth = 0., wrap_c = -1.;
    for (int a = 0; a < 72; ++a) {
        const double phi = a * 5. * Deg;
        JointPose    j;
        if (kin->inverse(ToolPose{ Vec3d::Zero(), Vec3d(std::sin(50. * Deg) * std::cos(phi), std::sin(50. * Deg) * std::sin(phi), std::cos(50. * Deg)) }, nullptr, j) &&
            std::abs(j.rot[1]) > wrap_c)
            wrap_c = std::abs(j.rot[1]), wrap_azimuth = phi;
    }
    std::vector<int> order;
    std::vector<uint8_t> visited(roads.size(), 0);
    for (size_t r = 0; r < roads.size(); ++r)
        visited[r] = !roads[r].alive;
    auto angle_to_wrap = [&](const Road &road) { return std::abs(std::remainder(road.azimuth - wrap_azimuth, 2. * M_PI)); };
    while (true) {
        int start = -1;
        for (size_t r = 0; r < roads.size(); ++r)
            if (!visited[r] && (start < 0 || angle_to_wrap(roads[r]) < angle_to_wrap(roads[size_t(start)]) - 1e-9))
                start = int(r);
        if (start < 0)
            break;
        for (int cur = start; cur >= 0;) {
            visited[size_t(cur)] = 1;
            order.emplace_back(cur);
            int best = -1, count = 0;
            for (const auto &[to, c] : roads[size_t(cur)].next)
                if (!visited[size_t(to)] && c > count)
                    best = to, count = c;
            cur = best;
        }
    }



    struct Tip { Vec3d tip, axis; float h, w, layer; };
    for (int band = 0; band < bands; ++band) {
        size_t host_layer = 0;
        for (size_t l = 0; l < layers.size(); ++l)
            if (band_of[l] == band)
                host_layer = l;
        const double   host_z = layers[host_layer]->print_z;
        const double   nozzle = object.print()->config().nozzle_diameter.get_at(0);
        ConformalPatch patch;
        patch.id         = int(result.patches.size());
        patch.kind       = PatchKind::Skin;
        patch.host_layer = host_layer;
        patch.region_id  = region_id;
        patch.shells     = SkinPasses;
        patch.z_min      = std::numeric_limits<double>::infinity();
        patch.z_max      = -std::numeric_limits<double>::infinity();
        std::vector<int> inner_seq;



        for (int pass = 0; pass < SkinPasses; ++pass) {
            ExtrusionEntityCollection shell;
            shell.no_sort = true;
            const double depth_share = double(SkinPasses - 1 - pass) / SkinPasses;
            const double share       = 1. / SkinPasses;
            Polyline3          poly;
            std::vector<float> bead, width;
            std::vector<Vec3f> axis;
            double             nominal_w = 0., nominal_h = 0.;
            auto flush = [&]() {
                if (poly.points.size() >= 2) {
                    ExtrusionPath base(erExternalPerimeter, nominal_w * nominal_h, float(nominal_w), float(nominal_h));
                    auto *path = new ExtrusionPathConformal(std::move(poly), base, std::move(bead), std::move(axis));
                    path->bead_width = std::move(width);
                    path->shell      = pass;
                    path->patch_id   = patch.id;
                    shell.entities.emplace_back(path);
                }
                poly = Polyline3();
                bead.clear(), width.clear(), axis.clear();
            };
            auto add = [&](const Vec3d &tip, const Vec3d &ax, float h, float w) {
                poly.points.emplace_back(Point3(coord_t(scale_(tip.x())), coord_t(scale_(tip.y())), coord_t(scale_(tip.z() - host_z))));
                bead.emplace_back(h);
                width.emplace_back(w);
                axis.emplace_back(ax.cast<float>());
                patch.z_min = std::min(patch.z_min, tip.z());
                patch.z_max = std::max(patch.z_max, tip.z());
            };
            std::optional<Tip> last;
            JointPose          last_joints;



            auto run = [&](std::vector<Tip> &tips, bool end_front, bool end_back) {
                if (tips.size() < 2)
                    return;
                for (int end = 0; end < 2; ++end) {
                    if (!(end == 0 ? end_front : end_back))
                        continue;
                    Tip        &a = end == 0 ? tips[0] : tips.back();
                    const Tip  &b = end == 0 ? tips[1] : tips[tips.size() - 2];
                    const Vec3d d = a.tip - b.tip;


                    const double shift = std::min(0.5 * double(a.layer), 0.5 * d.norm());
                    a.tip += shift * d.normalized();
                }



                {
                    std::vector<Tip> smooth = tips;
                    for (size_t i = 0; i < tips.size(); ++i) {
                        double w = 0., h = 0.;
                        int    k = 0;
                        for (size_t j = i < 2 ? 0 : i - 2; j <= std::min(tips.size() - 1, i + 2); ++j, ++k)
                            w += tips[j].w, h += tips[j].h;
                        smooth[i].w = float(w / k), smooth[i].h = float(h / k);
                    }
                    std::vector<Tip> kept { smooth.front() };
                    for (size_t a = 0, b = 2; b <= smooth.size(); ++b) {


                        bool fits = b < smooth.size() && (smooth[b].tip - smooth[a].tip).norm() <= 1.5;
                        for (size_t m = a + 1; fits && m < b; ++m) {
                            const double t  = double(m - a) / double(b - a);
                            const Tip   &A = smooth[a], &B = smooth[b], &M = smooth[m];
                            const Vec3d  chord = A.tip + t * (B.tip - A.tip);
                            fits = (M.tip - chord).norm() < 0.01 && std::acos(std::clamp(M.axis.dot((A.axis + t * (B.axis - A.axis)).normalized()), -1., 1.)) < Deg &&
                                   std::abs(M.w - (A.w + t * (B.w - A.w))) < 0.02 && std::abs(M.h - (A.h + t * (B.h - A.h))) < 0.005;
                        }
                        if (!fits) {
                            kept.emplace_back(smooth[b - 1]);
                            a = b - 1;
                        }
                    }
                    if ((kept.back().tip - smooth.back().tip).norm() > 1e-9)
                        kept.emplace_back(smooth.back());
                    tips.swap(kept);
                }

                if (last && (tips.back().tip - last->tip).norm() < (tips.front().tip - last->tip).norm())
                    std::reverse(tips.begin(), tips.end());




                auto jump = [](const JointPose &a, const JointPose &b) {
                    return std::max(std::abs(a.rot[0] - b.rot[0]), std::abs(a.rot[1] - b.rot[1]));
                };
                bool join = last && (tips.front().tip - last->tip).norm() <= 1.5 && tips.front().axis.dot(last->axis) >= std::cos(10. * Deg);
                JointPose cur;
                if (join) {
                    JointPose j;
                    join = kin->inverse(ToolPose{ tips.front().tip, tips.front().axis }, &last_joints, j) && jump(j, last_joints) < 30.;
                }
                if (!join) {
                    flush();


                    if (tips.back().tip.z() > tips.front().tip.z())
                        std::reverse(tips.begin(), tips.end());
                }
                bool have = join;
                if (join) {
                    cur = last_joints;


                    add(last->tip + GlideRaise * last->axis, last->axis, 0.f, last->w);
                    add(tips.front().tip + GlideRaise * tips.front().axis, tips.front().axis, 0.f, tips.front().w);
                }
                for (const Tip &t : tips) {
                    JointPose j;
                    if (have && (!kin->inverse(ToolPose{ t.tip, t.axis }, &cur, j) || jump(j, cur) >= 30.)) {
                        flush();
                        have = false;
                    }
                    if (!have)
                        kin->inverse(ToolPose{ t.tip, t.axis }, nullptr, j);
                    if (poly.points.empty())
                        nominal_w = t.w, nominal_h = t.h;
                    add(t.tip, t.axis, t.h, t.w);
                    cur  = j;
                    have = true;
                }
                last_joints = cur;
                last = tips.back();
            };


            std::vector<int> seq = order;
            auto jumps = [&](const std::vector<int> &sq, size_t *first = nullptr) {
                int       n = 0;
                size_t    at = 0;
                bool      have = false;
                JointPose cur;
                for (int r : sq) {
                    const Road &road = roads[size_t(r)];
                    const RoadPoint *mid = nullptr;
                    for (const RoadPoint &rp : road.points)
                        if (rp.pose.ok)
                            mid = &rp;
                    if (mid == nullptr)
                        continue;
                    JointPose j;
                    if (!kin->inverse(ToolPose{ mid->pose.surface, mid->pose.axis }, have ? &cur : nullptr, j))
                        continue;
                    if (have && std::max(std::abs(j.rot[0] - cur.rot[0]), std::abs(j.rot[1] - cur.rot[1])) >= 30.) {
                        if (n++ == 0 && first != nullptr)
                            *first = at;
                    }
                    cur = j, have = true;
                    ++at;
                }
                return n;
            };
            if (pass == 0) {
                std::vector<int> back(order.rbegin(), order.rend());
                if (jumps(back) < jumps(order))
                    seq = back;


                size_t k = 0;
                if (jumps(seq, &k) > 0 && k > 0) {
                    std::vector<int> turned(seq.begin() + long(k), seq.end());
                    turned.insert(turned.end(), seq.begin(), seq.begin() + long(k));
                    if (jumps(turned) < jumps(seq))
                        seq.swap(turned);
                }
                inner_seq = seq;
            } else
                seq.assign(inner_seq.rbegin(), inner_seq.rend());
            for (int r : seq) {
                const Road      &road = roads[size_t(r)];



                std::vector<double> below_top(road.points.size(), std::numeric_limits<double>::infinity());
                for (size_t i = 0; i < road.points.size();) {
                    if (!road.points[i].pose.ok) {
                        ++i;
                        continue;
                    }
                    size_t j = i;
                    while (j + 1 < road.points.size() && road.points[j + 1].pose.ok)
                        ++j;
                    const bool up = road.points[j].pose.surface.z() >= road.points[i].pose.surface.z();
                    double     arc = 0.;
                    for (size_t k = 0; k <= j - i; ++k) {
                        const size_t at = up ? j - k : i + k, prev = up ? at + 1 : at - 1;
                        if (k > 0)
                            arc += (road.points[at].pose.surface - road.points[prev].pose.surface).norm();
                        below_top[at] = arc;
                    }
                    i = j + 1;
                }
                std::vector<Tip> tips;
                bool             front = true;
                for (size_t i = 0; i < road.points.size(); ++i) {
                    const RoadPoint &rp = road.points[i];
                    const Pose      &p  = rp.pose;
                    const bool       top_band = SkinPasses > 1 && (below_top[i] < EndBand || rim[size_t(r)]);
                    if (pass + 1 < SkinPasses && p.ok && band_of[rp.layer] == band && top_band) {
                        run(tips, front, true);
                        tips.clear();
                        front = true;
                        continue;
                    }
                    if (band_of[rp.layer] != band || !p.ok) {

                        const bool cut = band_of[rp.layer] != band && p.ok;
                        run(tips, front, !cut);
                        tips.clear();
                        front = !cut;
                        continue;
                    }
                    if (!tips.empty() && tips.back().axis.dot(p.axis) < std::cos(30. * Deg)) {
                        run(tips, front, true);
                        tips.clear();
                        front = true;
                    }
                    const double t = (top_band ? double(SkinPasses - pass) : 1.) * share * p.thickness;

                    const float h = float(t / std::max(std::cos(p.residual), 0.2));

                    const float w = float(std::abs(rp.corner) < rp.spacing ? std::min(2. * std::abs(rp.corner), 2. * nozzle) : rp.spacing + t * (1. - 0.25 * M_PI));
                    if (w < 0.15f) {

                        run(tips, front, true);
                        tips.clear();
                        front = true;
                        continue;
                    }
                    tips.push_back({ p.surface - depth_share * p.thickness * p.normal, p.axis, h, w, float(rp.height) });
                }
                run(tips, front, true);
            }
            flush();
            patch.shell_paths.emplace_back(std::move(shell));
        }
        if (patch.shell_paths.front().entities.empty() && patch.shell_paths.back().entities.empty())
            continue;
        patch.shell_height = layers[host_layer]->height;
        layers[host_layer]->conformal_patches_after.emplace_back(patch.id);
        result.patches.emplace_back(std::move(patch));
    }
}

}
