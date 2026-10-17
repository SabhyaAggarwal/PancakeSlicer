#include "DeformSlice.hpp"

#include "BeadModel.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "Multiplanar.hpp"
#include "PatchTools.hpp"
#include "StrengthRoads.hpp"
#include "SurfaceClip.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ConformalEmit.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/ShortestPath.hpp"

#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {



constexpr double Inf = std::numeric_limits<double>::infinity();
constexpr double NearReach = 3.;
constexpr double MaxClimb = 0.2;
constexpr double MaxSegment = 0.6;
constexpr double MinStretch = 0.1;
constexpr double AlignedError = 0.02;
constexpr double MaxPredicted = 0.030;
constexpr double FlatPredicted = 0.025;
constexpr double MaxShells = 1.2;
constexpr double MaxSaddleShare = 0.25;
constexpr double MaxTimeRatioPredicted = 1.25;
constexpr double ZaaMinZ = 0.05;

double sample_clamped(const HeightField &f, const Vec2d &xy)
{
    const Vec2d  o  = (xy - f.origin()) / f.pitch();
    const double fx = std::clamp(o.x(), 0., double(f.nx() - 1)), fy = std::clamp(o.y(), 0., double(f.ny() - 1));
    const int    ix = std::min(int(fx), f.nx() - 2 < 0 ? 0 : f.nx() - 2), iy = std::min(int(fy), f.ny() - 2 < 0 ? 0 : f.ny() - 2);
    const int    jx = std::min(ix + 1, f.nx() - 1), jy = std::min(iy + 1, f.ny() - 1);
    const double tx = fx - ix, ty = fy - iy;
    return (1. - ty) * ((1. - tx) * f.at(ix, iy) + tx * f.at(jx, iy)) + ty * ((1. - tx) * f.at(ix, jy) + tx * f.at(jx, jy));
}

void blur(HeightField &f, int r)
{
    if (r <= 0)
        return;
    const int          nx = f.nx(), ny = f.ny();
    std::vector<float> tmp(f.data().size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int axis = 0; axis < 2; ++axis) {
            const int n = axis == 0 ? nx : ny, m = axis == 0 ? ny : nx;
            for (int j = 0; j < m; ++j) {
                auto at = [&](int i) -> float & { return axis == 0 ? f.data()[size_t(j) * nx + std::clamp(i, 0, n - 1)]
                                                                   : f.data()[size_t(std::clamp(i, 0, n - 1)) * nx + j]; };
                double sum = 0.;
                for (int i = -r; i <= r; ++i)
                    sum += at(i);
                for (int i = 0; i < n; ++i) {
                    tmp[axis == 0 ? size_t(j) * nx + i : size_t(i) * nx + j] = float(sum / (2 * r + 1));
                    sum += at(i + r + 1) - at(i - r);
                }
            }
            f.data().swap(tmp);
        }
    }
}

void fill_nodata(HeightField &f, float fallback)
{
    const int nx = f.nx(), ny = f.ny();
    bool      any = std::any_of(f.data().begin(), f.data().end(), [](float v) { return !std::isnan(v); });
    if (!any) {
        std::fill(f.data().begin(), f.data().end(), fallback);
        return;
    }
    for (bool changed = true; changed;) {
        changed = false;
        std::vector<float> next = f.data();
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix) {
                if (!std::isnan(f.at(ix, iy)))
                    continue;
                double sum = 0.;
                int    n   = 0;
                for (const auto &d : { std::pair(1, 0), std::pair(-1, 0), std::pair(0, 1), std::pair(0, -1) }) {
                    const int jx = ix + d.first, jy = iy + d.second;
                    if (jx >= 0 && jy >= 0 && jx < nx && jy < ny && !std::isnan(f.at(jx, jy)))
                        sum += f.at(jx, jy), ++n;
                }
                if (n > 0) {
                    next[size_t(iy) * nx + ix] = float(sum / n);
                    changed                    = true;
                }
            }
        f.data().swap(next);
    }
}

DynamicPrintConfig deform_print_config(const PrintObject &object, double road_angle)
{
    DynamicPrintConfig config = object.print()->full_print_config();
    config.apply(object.config(), true);
    config.apply(object.printing_region(0).config(), true);
    const auto set = [&config](const char *key, const std::string &value) {
        if (config.option(key) != nullptr)
            config.set_deserialize_strict(key, value);
    };
    set("nonplanar_mode", "off");
    set("nonplanar_deform", "off");
    set("nonplanar_atom_toolpath", "");
    set("nonplanar_kinematics", "none");
    set("nonplanar_collision_check", "off");
    set("zaa_enabled", "0");
    set("enable_support", "0");
    set("raft_layers", "0");
    set("brim_type", "no_brim");
    set("skirt_loops", "0");
    set("enable_prime_tower", "0");
    set("enable_arc_fitting", "0");
    set("ironing_type", "no ironing");
    set("wall_generator", "classic");
    const double nozzle = config.opt<ConfigOptionFloats>("nozzle_diameter")->values.front();
    const double top_w  = object.printing_region(0).config().top_surface_line_width.get_abs_value(nozzle);
    if (top_w <= 0. || top_w > 0.75 * nozzle)
        set("top_surface_line_width", float_to_string_decimal_point(0.75 * nozzle));
    if (!std::isnan(road_angle)) {
        const std::string deg = float_to_string_decimal_point(Geometry::rad2deg(road_angle));
        set("solid_infill_direction", deg);
        set("infill_direction", deg);
        set("align_infill_direction_to_model", "0");
    }
    return config;
}

void map_path(const ExtrusionPath &path, const Vec2d &center, double level, double thickness, const LayerField &field, double raft,
              double step, std::vector<ExtrusionPathConformal *> &result)
{
    const Points3 &src = path.polyline.points;
    if (src.size() < 2)
        return;
    std::vector<Vec2d> xy;
    xy.reserve(src.size() * 2);
    for (size_t i = 0; i < src.size(); ++i) {
        const Vec2d p(unscale<double>(src[i].x()) + center.x(), unscale<double>(src[i].y()) + center.y());
        if (i > 0) {
            const Vec2d  a = xy.back();
            const double l = (p - a).norm();
            const int    n = int(std::ceil(l / step));
            for (int k = 1; k < n; ++k)
                xy.emplace_back(a + (p - a) * (double(k) / n));
        }
        xy.emplace_back(p);
    }
    auto finish = [&](ExtrusionPathConformal *out) {
        out->simplify(0.01);
        Points3           &pts = out->polyline.points;
        std::vector<float> &bh = out->bead_height;
        for (size_t i = 1; i < pts.size(); ++i) {
            const double dz = unscale<double>(pts[i].z() - pts[i - 1].z());
            const Vec2d  a = unscale(pts[i - 1].to_point()), b = unscale(pts[i].to_point());
            int          n = int(std::ceil(std::abs(dz) / MaxClimb));
            if ((b - a).norm() > MaxSegment) {
                const Vec2d m = 0.5 * (a + b);
                const Vec3d g = field.gradient(Vec3d(m.x(), m.y(), field.z_at(m, level)));
                if (g.head<2>().norm() > 0.05 * g.z())
                    n = std::max(n, int(std::ceil((b - a).norm() / MaxSegment)));
            }
            if (n < 2)
                continue;
            Points3            ins;
            std::vector<float> ins_h;
            for (int k = 1; k < n; ++k) {
                const Vec2d p = a + (b - a) * (double(k) / n);
                ins.push_back(Point3::new_scale(p.x(), p.y(), field.z_at(p, level) + raft));
                ins_h.push_back(bh[i - 1] + (bh[i] - bh[i - 1]) * float(k) / float(n));
            }
            pts.insert(pts.begin() + i, ins.begin(), ins.end());
            bh.insert(bh.begin() + i, ins_h.begin(), ins_h.end());
            i += ins.size();
        }
        result.push_back(out);
    };
    const double area = path.mm3_per_mm;
    Polyline3          poly;
    std::vector<float> beads;
    auto flush = [&]() {
        if (poly.points.size() >= 2)
            finish(new ExtrusionPathConformal(std::move(poly), path, std::move(beads)));
        poly.points.clear();
        beads.clear();
    };
    for (const Vec2d &p : xy) {
        const double top = field.z_at(p, level);
        const double bot = field.z_at(p, level - thickness);
        const double j   = std::max(0., top - bot) / thickness;
        if (j < MinStretch) {
            flush();
            continue;
        }
        poly.points.push_back(Point3::new_scale(p.x(), p.y(), top + raft));
        beads.push_back(float(bead_height_for_area(path.width, j * area)));
    }
    flush();
}

struct Flattener
{
    SeamPosition      seam;
    const Layer      *layer;
    double            seam_gap;
    Point             last;
    std::vector<ExtrusionPath> out;

    void add_path(const ExtrusionPath &p)
    {
        if (p.polyline.points.size() < 2)
            return;
        out.push_back(p);
        last = out.back().polyline.points.back().to_point();
    }
    void add(ExtrusionEntity *ee)
    {
        if (auto *coll = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            if (!coll->no_sort)
                chain_and_reorder_extrusion_entities(coll->entities, last);
            for (ExtrusionEntity *e : coll->entities)
                add(e);
        } else if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee)) {
            Point at = last;
            if (seam != spNearest) {
                std::vector<Point> pts;
                for (const ExtrusionPath &p : loop->paths)
                    for (const Point3 &q : p.polyline.points)
                        pts.push_back(q.to_point());
                if (!pts.empty())
                    at = seam == spRandom ? pts[size_t(layer->id() * 2654435761u) % pts.size()] :
                                            *std::max_element(pts.begin(), pts.end(), [](const Point &a, const Point &b) {
                                                return a.y() < b.y() || (a.y() == b.y() && a.x() > b.x());
                                            });
            }
            loop->split_at(at, false);
            ExtrusionPaths paths;
            loop->clip_end(seam_gap, &paths);
            for (const ExtrusionPath &p : paths)
                add_path(p);
        } else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(ee)) {
            if (multi->can_reverse() && !multi->paths.empty() &&
                (multi->last_point() - last).cast<double>().squaredNorm() < (multi->first_point() - last).cast<double>().squaredNorm())
                multi->reverse();
            for (const ExtrusionPath &p : multi->paths)
                add_path(p);
        } else if (auto *path = dynamic_cast<ExtrusionPath *>(ee)) {
            if (path->can_reverse() &&
                (path->last_point() - last).cast<double>().squaredNorm() < (path->first_point() - last).cast<double>().squaredNorm())
                path->reverse();
            add_path(*path);
        }
    }
};

void link_pieces(ExtrusionEntityCollection &shell, Point &last)
{
    auto is_wall = [](const ExtrusionEntity *e) {
        const ExtrusionRole r = e->role();
        return r == erExternalPerimeter || r == erPerimeter || r == erOverhangPerimeter;
    };
    ExtrusionEntitiesPtr walls, rest;
    for (ExtrusionEntity *e : shell.entities)
        (is_wall(e) ? walls : rest).push_back(e);
    if (!walls.empty())
        last = walls.back()->last_point();
    if (!rest.empty())
        chain_and_reorder_extrusion_entities(rest, last);
    ExtrusionEntitiesPtr out;
    out.reserve(shell.entities.size());
    for (ExtrusionEntitiesPtr *group : { &walls, &rest })
        for (ExtrusionEntity *e : *group) {
            auto *cur  = static_cast<ExtrusionPathConformal *>(e);
            auto *prev = out.empty() ? nullptr : static_cast<ExtrusionPathConformal *>(out.back());
            if (prev != nullptr && group == &rest && !is_wall(prev) && prev->role() == cur->role() && prev->width == cur->width &&
                prev->mm3_per_mm == cur->mm3_per_mm && prev->bead_width.empty() && cur->bead_width.empty() &&
                (prev->last_point() - cur->first_point()).cast<double>().norm() <= scale_(1.5 * cur->width)) {
                prev->polyline.points.insert(prev->polyline.points.end(), cur->polyline.points.begin(), cur->polyline.points.end());
                prev->bead_height.insert(prev->bead_height.end(), cur->bead_height.begin(), cur->bead_height.end());
                delete cur;
                continue;
            }
            out.push_back(e);
        }
    shell.entities = std::move(out);
    if (!shell.entities.empty())
        last = shell.entities.back()->last_point();
}

}

double deform_band_height(const HeadEnvelope &head, double slope, double inflation)
{
    const double allowance_r = head.tip_allowance.radius, allowance_h = head.tip_allowance.height;
    struct Floor { double z, far; };
    std::vector<Floor> floors;
    for (const HeadBox &box : head.boxes) {
        const double lm     = box.nozzle ? 0. : head.lateral_margin;
        const double half_x = std::max(std::abs(box.min.x()), std::abs(box.max.x())) + lm;
        const double half_y = std::max(std::abs(box.min.y()), std::abs(box.max.y())) + lm;
        double       z      = box.min.z();
        if (box.nozzle) {
            if (std::hypot(half_x, half_y) <= allowance_r)
                z = std::max(z, allowance_h);
            if (z >= box.max.z())
                continue;
        } else
            z -= head.margin;
        floors.push_back({ z, box.infinite_x || box.infinite_y ? Inf : std::hypot(half_x, half_y) + inflation });
    }
    double reach = Inf;
    for (const Floor &f : floors)
        if (slope * f.far > f.z)
            reach = std::min(reach, f.z);
    return std::max(reach, head_band_height(head));
}

double ColumnScaleField::rho(const Vec2d &xy) const { return m_rho.empty() ? 1. : sample_clamped(m_rho, xy); }

Vec3d ColumnScaleField::gradient(const Vec3d &p) const
{
    if (p.z() <= m_z0)
        return Vec3d::UnitZ();
    const double r = this->rho(p.head<2>());
    const double e = 0.5 * m_rho.pitch();
    const Vec2d  g((this->rho(p.head<2>() + Vec2d(e, 0.)) - this->rho(p.head<2>() - Vec2d(e, 0.))) / (2. * e),
                   (this->rho(p.head<2>() + Vec2d(0., e)) - this->rho(p.head<2>() - Vec2d(0., e))) / (2. * e));
    const double k = -(p.z() - m_z0) / (r * r);
    return Vec3d(k * g.x(), k * g.y(), 1. / r);
}

std::unique_ptr<ColumnScaleField> build_column_scale_field(const indexed_triangle_set &mesh, const DeformFieldParams &P,
                                                           DeformFieldReport *report, HeightField *top, const MeshRays *rays)
{
    BoundingBoxf3 bb = rays != nullptr ? rays->bbox : BoundingBoxf3();
    if (rays == nullptr)
        for (const stl_vertex &v : mesh.vertices)
            bb.merge(v.cast<double>());
    const double pitch  = P.pitch;
    const double margin = P.blur + 2. * pitch;
    HeightField  S(BoundingBoxf(bb.min.head<2>() - Vec2d(margin, margin), bb.max.head<2>() + Vec2d(margin, margin)), pitch);
    {
        std::unique_ptr<sla::IndexedMesh> own;
        if (rays == nullptr)
            own = std::make_unique<sla::IndexedMesh>(mesh);
        const sla::IndexedMesh &imesh = rays != nullptr ? *rays->imesh : *own;
        const double            shift = rays != nullptr ? rays->shift : 0.;
        const double            ray_z = bb.max.z() + 1. - shift;
        tbb::parallel_for(tbb::blocked_range<int>(0, S.ny()), [&](const tbb::blocked_range<int> &range) {
            for (int iy = range.begin(); iy < range.end(); ++iy)
                for (int ix = 0; ix < S.nx(); ++ix)
                    S.at(ix, iy) = float(vertical_ray_hit(imesh, S.node(ix, iy), ray_z, -1.) + shift);
        });
    }
    if (report != nullptr && (P.flat_share_limit < 1. || P.saddle_share_limit < 1.)) {
        size_t flat = 0, all = 0, curved_n = 0, saddle = 0;
        const double k_min = 1. / 50., p2 = 2. * pitch;
        for (int iy = 2; iy + 2 < S.ny(); ++iy)
            for (int ix = 2; ix + 2 < S.nx(); ++ix) {
                const Vec2d g = S.gradient(S.node(ix, iy));
                if (std::isnan(g.x()) || std::isnan(g.y()) || g.norm() > std::tan(Geometry::deg2rad(70.)))
                    continue;
                ++all;
                if (g.norm() < std::tan(Geometry::deg2rad(1.))) {
                    ++flat;
                    continue;
                }
                const float c = S.at(ix, iy), xm = S.at(ix - 2, iy), xp = S.at(ix + 2, iy), ym = S.at(ix, iy - 2), yp = S.at(ix, iy + 2);
                const float a = S.at(ix - 2, iy - 2), b = S.at(ix + 2, iy - 2), d = S.at(ix - 2, iy + 2), e = S.at(ix + 2, iy + 2);
                if (std::isnan(c + xm + xp + ym + yp + a + b + d + e))
                    continue;
                ++curved_n;
                const double fxx = (xp - 2. * c + xm) / (p2 * p2), fyy = (yp - 2. * c + ym) / (p2 * p2);
                const double fxy = (e - b - d + a) / (4. * p2 * p2);
                const double w   = 1. + g.squaredNorm();
                saddle += (fxx * fyy - fxy * fxy) / (w * w) < -k_min * k_min;
            }
        report->flat_share   = all > 0 ? double(flat) / double(all) : 0.;
        report->saddle_share = curved_n > 0 ? double(saddle) / double(curved_n) : 0.;
        if (report->flat_share > P.flat_share_limit || report->saddle_share > P.saddle_share_limit) {
            const double f = report->flat_share, sd = report->saddle_share;
            *report              = DeformFieldReport{};
            report->flat_share   = f;
            report->saddle_share = sd;
            return nullptr;
        }
    }
    const HeightField T       = slope_cap_minorant(S, std::atan(P.max_slope));
    const double      cell    = pitch * pitch;
    const double      z0_min  = P.first_layer_height;
    const double      tan_min = std::tan(Geometry::deg2rad(1.));

    struct Node { double s; size_t n; };
    std::vector<Node> curved;
    double            s_max = 0.;
    for (int iy = 1; iy + 1 < S.ny(); ++iy)
        for (int ix = 1; ix + 1 < S.nx(); ++ix) {
            const float s = S.at(ix, iy), t = T.at(ix, iy);
            if (std::isnan(s) || std::isnan(t))
                continue;
            s_max = std::max(s_max, double(s));
            const Vec2d g = S.gradient(S.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y()))
                continue;
            const double slope = g.norm();
            if (slope >= tan_min && slope <= P.max_slope && s - t < 0.05 && s > z0_min + P.layer_height)
                curved.push_back({ double(t), size_t(iy) * S.nx() + ix });
        }

    double best_area = 0., best_z0 = z0_min, best_d = 1.;
    std::vector<std::pair<double, double>> by_z0;
    auto sweep = [&](double z0, double &d_out) {
        std::vector<std::pair<double, double>> ev;
        std::vector<double>                    mid;
        for (const Node &nd : curved) {
            if (nd.s <= z0 + P.min_ratio * P.layer_height)
                continue;
            ev.emplace_back((nd.s - z0) / P.max_ratio, cell);
            ev.emplace_back((nd.s - z0) / P.min_ratio, -cell);
            mid.push_back(nd.s - z0);
        }
        if (ev.empty())
            return 0.;
        std::sort(ev.begin(), ev.end(), [](const auto &a, const auto &b) { return a.first < b.first || (a.first == b.first && a.second > b.second); });
        std::nth_element(mid.begin(), mid.begin() + mid.size() / 2, mid.end());
        const double d1  = mid[mid.size() / 2];
        double       cur = 0., best = 0.;
        for (const auto &e : ev)
            best = std::max(best, cur += e.second);
        cur            = 0.;
        double best_dd = Inf;
        for (size_t i = 0; i < ev.size(); ++i) {
            cur += ev[i].second;
            if (cur < 0.995 * best || i + 1 == ev.size())
                continue;
            const double lo = ev[i].first, hi = ev[i + 1].first;
            const double d  = std::clamp(d1, lo, hi);
            const double dd = std::abs(std::log(d / d1));
            if (dd < best_dd)
                best_dd = dd, d_out = d;
        }
        return best;
    };
    const double z0_step = P.layer_height * std::max(1., std::ceil((s_max - z0_min) / (40. * P.layer_height)));
    for (double z0 = z0_min; z0 < s_max - P.layer_height; z0 += z0_step) {
        double       d    = 1.;
        const double area = sweep(z0, d);
        by_z0.emplace_back(z0, area);
        if (area > best_area)
            best_area = area, best_z0 = z0, best_d = d;
    }
    for (auto it = by_z0.rbegin(); it != by_z0.rend(); ++it)
        if (it->second >= P.z0_keep * best_area && it->second > 0.) {
            best_z0 = it->first;
            sweep(best_z0, best_d);
            break;
        }
    const double z0 = best_z0;
    const double D  = std::max(P.layer_height, P.layer_height * std::round(best_d / P.layer_height));

    HeightField rho = S, target = S;
    std::fill(target.data().begin(), target.data().end(), HeightField::NoData);
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix) {
            const float s = S.at(ix, iy), t = T.at(ix, iy);
            float      &r = rho.at(ix, iy);
            r             = HeightField::NoData;
            if (std::isnan(s) || std::isnan(t) || s <= z0)
                continue;
            const Vec2d g     = S.gradient(S.node(ix, iy));
            const double slope = std::isnan(g.x()) || std::isnan(g.y()) ? Inf : g.norm();
            const double fit   = (t - z0) / D;
            if (slope < tan_min)
                continue;
            const bool follow = slope <= P.max_slope && s - t < 0.05 && fit >= P.min_ratio && fit <= P.max_ratio;
            r                 = float(follow ? fit : P.min_ratio);
            if (follow)
                target.at(ix, iy) = r;
        }
    fill_nodata(rho, 1.f);
    const int radius = std::max(1, int(std::lround(P.blur / pitch / 1.7)));
    auto lipschitz = [&](HeightField &f) {
        for (int pass = 0; pass < 2; ++pass) {
            double level_max = z0 + D;
            for (size_t n = 0; n < S.data().size(); ++n)
                if (!std::isnan(S.data()[n]) && S.data()[n] > z0)
                    level_max = std::max(level_max, z0 + (S.data()[n] - z0) / std::max(double(f.data()[n]), P.min_ratio));
            f = slope_cap_minorant(f, std::atan(0.9 * P.max_slope / std::max(level_max - z0, P.layer_height)));
        }
    };
    lipschitz(rho);
    HeightField smooth = rho;
    blur(smooth, radius);
    for (int pass = 0; pass < 4; ++pass) {
        for (size_t n = 0; n < rho.data().size(); ++n)
            if (!std::isnan(target.data()[n]))
                rho.data()[n] += target.data()[n] - smooth.data()[n];
        smooth = rho;
        blur(smooth, radius);
    }
    rho = std::move(smooth);
    lipschitz(rho);
    blur(rho, radius);
    for (float &r : rho.data())
        r = float(std::clamp(double(r), P.min_ratio, P.max_ratio));

    auto field = std::make_unique<ColumnScaleField>(z0, rho);
    if (top != nullptr)
        *top = S;
    if (report != nullptr) {
        DeformFieldReport &r = *report;
        const double flat_share = r.flat_share, saddle_share = r.saddle_share;
        r                    = DeformFieldReport{};
        r.flat_share         = flat_share;
        r.saddle_share       = saddle_share;
        r.z0                 = z0;
        r.top_level          = z0 + D;
        r.curved_area        = double(curved.size()) * cell;
        r.rho_min            = Inf;
        r.rho_max            = 0.;
        for (const Node &nd : curved) {
            const Vec2d  p = S.node(int(nd.n % size_t(S.nx())), int(nd.n / size_t(S.nx())));
            const double s = S.data()[nd.n];
            if (std::abs(field->z_at(p, z0 + D) - s) <= 0.05)
                r.aligned_area += cell;
        }
        HeightField level = S;
        for (int iy = 0; iy < S.ny(); ++iy)
            for (int ix = 0; ix < S.nx(); ++ix) {
                const float s = S.at(ix, iy);
                if (std::isnan(s) || s <= z0)
                    level.at(ix, iy) = HeightField::NoData;
                else {
                    level.at(ix, iy) = float(field->phi(Vec3d(S.node(ix, iy).x(), S.node(ix, iy).y(), s)));
                    r.rho_min        = std::min(r.rho_min, field->rho(S.node(ix, iy)));
                    r.rho_max        = std::max(r.rho_max, field->rho(S.node(ix, iy)));
                }
            }
        const int reach = int(std::ceil(NearReach / pitch));
        for (int axis = 0; axis < 2; ++axis) {
            HeightField next = level;
            for (int iy = 0; iy < S.ny(); ++iy)
                for (int ix = 0; ix < S.nx(); ++ix) {
                    float m = HeightField::NoData;
                    for (int d = -reach; d <= reach; ++d) {
                        const int jx = axis == 0 ? ix + d : ix, jy = axis == 0 ? iy : iy + d;
                        if (jx >= 0 && jy >= 0 && jx < S.nx() && jy < S.ny() && !std::isnan(level.at(jx, jy)) &&
                            (std::isnan(m) || level.at(jx, jy) > m))
                            m = level.at(jx, jy);
                    }
                    next.at(ix, iy) = m;
                }
            level = std::move(next);
        }
        for (int iy = 0; iy < S.ny(); ++iy)
            for (int ix = 0; ix < S.nx(); ++ix) {
                const float l = level.at(ix, iy);
                if (std::isnan(l))
                    continue;
                const Vec2d p = S.node(ix, iy);
                const Vec3d g = field->gradient(Vec3d(p.x(), p.y(), z0 + 1.));
                const double rr   = field->rho(p);
                r.max_layer_slope = std::max(r.max_layer_slope, (l - z0) * rr * rr * g.head<2>().norm());
            }
        if (r.rho_max < r.rho_min)
            r.rho_min = r.rho_max = 1.;
        double sum = 0.;
        size_t n   = 0;
        for (int iy = 1; iy + 1 < S.ny(); ++iy)
            for (int ix = 1; ix + 1 < S.nx(); ++ix) {
                const float s = S.at(ix, iy);
                const Vec2d g = S.gradient(S.node(ix, iy));
                if (std::isnan(s) || std::isnan(g.x()) || std::isnan(g.y()) || g.norm() > std::tan(Geometry::deg2rad(70.)))
                    continue;
                const Vec2d  p = S.node(ix, iy);
                const double e = s <= z0 ? P.layer_height / std::sqrt(12.) :
                                 std::abs(field->z_at(p, z0 + D) - s) <= 0.05 ? AlignedError :
                                                                                 field->rho(p) * P.layer_height / std::sqrt(12.);
                sum += e * e;
                ++n;
            }
        r.predicted_rms = n == 0 ? 0. : std::sqrt(sum / n);
    }
    return field;
}

indexed_triangle_set deform_mesh(const indexed_triangle_set &mesh, const LayerField &field, double max_edge)
{
    indexed_triangle_set its   = mesh;
    const float          z0    = float(field.identity_below());
    const double         max_2 = max_edge * max_edge;
    for (int pass = 0; pass < 16; ++pass) {
        std::unordered_map<uint64_t, int> mids;
        auto split = [&](int a, int b) {
            const stl_vertex &va = its.vertices[size_t(a)], &vb = its.vertices[size_t(b)];
            return (va.z() > z0 || vb.z() > z0) && (va - vb).cast<double>().squaredNorm() > max_2;
        };
        auto mid = [&](int a, int b) {
            const uint64_t key = (uint64_t(std::min(a, b)) << 32) | uint64_t(uint32_t(std::max(a, b)));
            const auto [it, inserted] = mids.try_emplace(key, int(its.vertices.size()));
            if (inserted) {
                const stl_vertex m = 0.5f * (its.vertices[size_t(a)] + its.vertices[size_t(b)]);
                its.vertices.push_back(m);
            }
            return it->second;
        };
        std::vector<stl_triangle_vertex_indices> out;
        out.reserve(its.indices.size() * 2);
        bool any = false;
        for (const stl_triangle_vertex_indices &f : its.indices) {
            const int  v[3] = { f[0], f[1], f[2] };
            const bool s[3] = { split(v[0], v[1]), split(v[1], v[2]), split(v[2], v[0]) };
            const int  n    = int(s[0]) + int(s[1]) + int(s[2]);
            if (n == 0) {
                out.push_back(f);
                continue;
            }
            any = true;
            if (n == 3) {
                const int m01 = mid(v[0], v[1]), m12 = mid(v[1], v[2]), m20 = mid(v[2], v[0]);
                out.emplace_back(v[0], m01, m20);
                out.emplace_back(m01, v[1], m12);
                out.emplace_back(m20, m12, v[2]);
                out.emplace_back(m01, m12, m20);
            } else if (n == 1) {
                const int r = s[0] ? 0 : s[1] ? 1 : 2;
                const int a = v[r], b = v[(r + 1) % 3], c = v[(r + 2) % 3];
                const int m = mid(a, b);
                out.emplace_back(a, m, c);
                out.emplace_back(m, b, c);
            } else {
                const int i = !s[0] ? 0 : !s[1] ? 1 : 2;
                const int r = (i + 1) % 3;
                const int a = v[r], b = v[(r + 1) % 3], c = v[(r + 2) % 3];
                const int mab = mid(a, b), mbc = mid(b, c);
                out.emplace_back(mab, b, mbc);
                out.emplace_back(a, mab, mbc);
                out.emplace_back(a, mbc, c);
            }
        }
        its.indices.swap(out);
        if (!any)
            break;
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, its.vertices.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i)
            its.vertices[i].z() = float(field.phi(its.vertices[i].cast<double>()));
    });
    return its;
}

bool deform_slice_requested(const PrintObject &object)
{
    const PrintObjectConfig &cfg = object.config();
    if (!cfg.nonplanar_atom_toolpath.value.empty())
        return false;
    switch (cfg.nonplanar_deform.value) {
    case NonplanarDeform::On: return cfg.nonplanar_mode.value == NonplanarMode::Auto || cfg.nonplanar_mode.value == NonplanarMode::Manual;
    default: return false;
    }
}

bool deform_slice_considered(const PrintObject &object)
{
    const PrintObjectConfig &cfg = object.config();
    return cfg.nonplanar_atom_toolpath.value.empty() && cfg.nonplanar_deform.value == NonplanarDeform::Auto &&
           cfg.nonplanar_mode.value == NonplanarMode::Auto;
}

static double strength_road_angle(const PrintObject &object, const indexed_triangle_set &mesh, const LayerField &field,
                                  const HeightField *top)
{
    if (!strength_roads_enabled(object.printing_region(0).config(), object.config()))
        return std::numeric_limits<double>::quiet_NaN();
    BoundingBoxf3 bb;
    for (const stl_vertex &v : mesh.vertices)
        bb.merge(v.cast<double>());
    HeightField S = top != nullptr ? *top : HeightField(BoundingBoxf(bb.min.head<2>(), bb.max.head<2>()), 0.5);
    if (top == nullptr) {
        sla::IndexedMesh imesh(mesh);
        tbb::parallel_for(tbb::blocked_range<int>(0, S.ny()), [&](const tbb::blocked_range<int> &range) {
            for (int iy = range.begin(); iy < range.end(); ++iy)
                for (int ix = 0; ix < S.nx(); ++ix)
                    S.at(ix, iy) = float(vertical_ray_hit(imesh, S.node(ix, iy), bb.max.z() + 1., -1.));
        });
    }
    std::vector<double> levels;
    Mask                mask(S.data().size(), 0);
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix)
            if (const float s = S.at(ix, iy); !std::isnan(s) && s > field.identity_below()) {
                levels.push_back(field.phi(Vec3d(S.node(ix, iy).x(), S.node(ix, iy).y(), s)));
                mask[size_t(iy) * S.nx() + ix] = 1;
            }
    if (levels.empty())
        return std::numeric_limits<double>::quiet_NaN();
    std::nth_element(levels.begin(), levels.begin() + levels.size() / 2, levels.end());
    const double level = levels[levels.size() / 2];
    HeightField  layer = S;
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix)
            layer.at(ix, iy) = float(field.z_at(S.node(ix, iy), level));
    double angle = 0.;
    return StrengthField(object).direction(layer, mask_to_expolygons(mask, S), angle) ? angle : std::numeric_limits<double>::quiet_NaN();
}

DeformFieldReport measure_layer_field(const indexed_triangle_set &mesh, const LayerField &field, double layer_height, const HeightField *top)
{
    DeformFieldReport r;
    r.z0      = field.identity_below();
    r.rho_min = Inf;
    r.rho_max = 0.;
    BoundingBoxf3 bb;
    for (const stl_vertex &v : mesh.vertices)
        bb.merge(v.cast<double>());
    HeightField S = top != nullptr ? *top : HeightField(BoundingBoxf(bb.min.head<2>(), bb.max.head<2>()), 0.5);
    if (top == nullptr) {
        sla::IndexedMesh imesh(mesh);
        tbb::parallel_for(tbb::blocked_range<int>(0, S.ny()), [&](const tbb::blocked_range<int> &range) {
            for (int iy = range.begin(); iy < range.end(); ++iy)
                for (int ix = 0; ix < S.nx(); ++ix)
                    S.at(ix, iy) = float(vertical_ray_hit(imesh, S.node(ix, iy), bb.max.z() + 1., -1.));
        });
    }
    std::vector<double> rmin(S.data().size(), Inf), rmax(S.data().size(), 0.), smax(S.data().size(), 0.), err(S.data().size(), -1.),
        mult(S.data().size(), 0.);
    tbb::parallel_for(tbb::blocked_range<int>(0, S.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy)
            for (int ix = 0; ix < S.nx(); ++ix) {
                const size_t n = size_t(iy) * S.nx() + ix;
                const float  s = S.data()[n];
                if (std::isnan(s))
                    continue;
                const Vec2d p = S.node(ix, iy);
                const Vec2d g = S.gradient(p);
                double      j_top = 1.;
                if (s > 0.)
                    mult[n] = std::min(double(s), r.z0) + std::max(0., field.phi(Vec3d(p.x(), p.y(), s)) - r.z0);
                if (s > r.z0)
                    for (int k = 0; k <= 4; ++k) {
                        const double z  = r.z0 + (s - r.z0) * (0.02 + 0.98 * k / 4.);
                        const Vec3d  gr = field.gradient(Vec3d(p.x(), p.y(), z));
                        if (!(gr.z() > 1e-6))
                            continue;
                        const double j = 1. / gr.z();
                        if (k == 4)
                            j_top = j;
                        if (j >= 0.25)
                            rmin[n] = std::min(rmin[n], j);
                        rmax[n] = std::max(rmax[n], j);
                        smax[n] = std::max(smax[n], gr.head<2>().norm() / gr.z());
                    }
                if (std::isnan(g.x()) || std::isnan(g.y()) || g.norm() > std::tan(Geometry::deg2rad(70.)))
                    continue;
                const double level = field.phi(Vec3d(p.x(), p.y(), s));
                const double off   = std::abs(field.z_at(p, layer_height * std::round(level / layer_height)) - s);
                const Vec3d  gl    = field.gradient(Vec3d(p.x(), p.y(), s));
                const bool   along = gl.z() > 1e-9 && (Vec2d(-gl.x() / gl.z(), -gl.y() / gl.z()) - g).norm() <= std::tan(Geometry::deg2rad(3.));
                err[n] = off <= 0.05 && along && g.norm() > 1e-3 ? AlignedError : std::max(0.25, std::min(j_top, 1.5)) * layer_height / std::sqrt(12.);
            }
    });
    double sum = 0.;
    size_t cnt = 0;
    for (size_t n = 0; n < err.size(); ++n) {
        if (rmax[n] > 0.) {
            r.rho_min         = std::min(r.rho_min, rmin[n]);
            r.rho_max         = std::max(r.rho_max, rmax[n]);
            r.max_layer_slope = std::max(r.max_layer_slope, smax[n]);
        }
        if (err[n] >= 0.)
            sum += err[n] * err[n], ++cnt;
    }
    if (!(r.rho_min <= r.rho_max))
        r.rho_min = r.rho_max = 1.;
    r.predicted_rms = cnt == 0 ? 0. : std::sqrt(sum / cnt);
    double levels = 0., heights = 0.;
    for (size_t n = 0; n < S.data().size(); ++n)
        if (!std::isnan(S.data()[n]) && S.data()[n] > 0.)
            levels += mult[n], heights += S.data()[n];
    r.layer_factor = heights > 0. ? levels / heights : 1.;
    return r;
}

namespace {
void translate_entity(ExtrusionEntity &ee, const Point3 &d)
{
    if (auto *coll = dynamic_cast<ExtrusionEntityCollection *>(&ee)) {
        for (ExtrusionEntity *e : coll->entities)
            translate_entity(*e, d);
    } else if (auto *path = dynamic_cast<ExtrusionPath *>(&ee)) {
        path->polyline.translate(d);
    } else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(&ee)) {
        for (ExtrusionPath &p : multi->paths)
            p.polyline.translate(d);
    } else if (auto *loop = dynamic_cast<ExtrusionLoop *>(&ee)) {
        for (ExtrusionPath &p : loop->paths)
            p.polyline.translate(d);
        if (auto *sloped = dynamic_cast<ExtrusionLoopSloped *>(loop))
            for (std::vector<ExtrusionPathSloped> *v : { &sloped->starts, &sloped->ends })
                for (ExtrusionPathSloped &p : *v)
                    p.polyline.translate(d);
    }
}

}
bool compute_deform_slice(const PrintObject &object, DeformSlice &out, std::string *why, double give_up_above,
                          const LayerField *given, double planar_time, double time_budget,
                          const std::function<bool(const DeformFieldReport &)> &admit, const MeshRays *rays)
{
    const auto t_start = std::chrono::steady_clock::now();
    const std::string &name = object.model_object()->name;
    auto fail = [&](const std::string &msg) {
        BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": not used, " << msg;
        if (why != nullptr)
            *why = msg;
        return false;
    };
    const Print            &host_print = *object.print();
    const PrintObjectConfig &ocfg      = object.config();
    const auto               layers    = object.layers();
    if (object.num_printing_regions() != 1)
        return fail("several regions");
    if (host_print.config().spiral_mode.value)
        return fail("vase mode");
    if (layers.size() < 3)
        return fail("fewer than three layers");
    if (!object.model_object()->layer_height_profile.empty() || !object.model_object()->layer_config_ranges.empty())
        return fail("custom layer heights");
    const double h      = ocfg.layer_height.value;
    const double first  = host_print.config().initial_layer_print_height.value;
    const double nozzle = host_print.config().nozzle_diameter.values.empty() ? 0.4 : host_print.config().nozzle_diameter.values.front();
    std::vector<double> layer_z;
    for (const Layer *layer : layers)
        layer_z.push_back(layer->print_z);
    const double raft = layers.front()->print_z - (layers.front()->slice_z + 0.5 * layers.front()->height);
    for (size_t i = 1; i < layers.size(); ++i)
        if (std::abs(layers[i]->height - h) > 1e-4)
            return fail("variable layer heights");

    DeformFieldParams params;
    params.first_layer_height = first;
    params.layer_height       = h;
    params.min_ratio          = 0.5;
    params.max_ratio          = std::min(1.5, 0.8 * nozzle / h);
    params.max_slope          = h / (0.75 * nozzle);
    std::unique_ptr<indexed_triangle_set> mesh_copy;
    auto mesh_of = [&]() -> const indexed_triangle_set & {
        if (!mesh_copy)
            mesh_copy = std::make_unique<indexed_triangle_set>(slicing_frame_mesh(object));
        return *mesh_copy;
    };
    std::unique_ptr<ColumnScaleField> own;
    HeightField                       top;
    if (given == nullptr) {
        DeformFieldReport built;
        if (admit) {
            params.flat_share_limit   = std::pow(FlatPredicted / (params.min_ratio * h / std::sqrt(12.)), 2.);
            params.saddle_share_limit = MaxSaddleShare;
        }
        own = rays != nullptr ? build_column_scale_field(indexed_triangle_set(), params, &built, &top, rays) :
                                build_column_scale_field(mesh_of(), params, &built, &top);
        if (!own)
            return fail("top " + std::to_string(int(100. * built.flat_share)) + " % flat, " + std::to_string(int(100. * built.saddle_share)) +
                        " % saddle");
        BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": top " << built.flat_share << " flat, " << built.saddle_share << " saddle";
        BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": column field z0 " << built.z0 << ", top level " << built.top_level
                                << ", curved top " << built.curved_area << " mm2, followed " << built.aligned_area << " mm2";
    }
    const LayerField       &field_ref = given != nullptr ? *given : *own;
    const LayerField *const field     = &field_ref;
    const DeformFieldReport report    = measure_layer_field(top.empty() ? mesh_of() : indexed_triangle_set(), field_ref, h, top.empty() ? nullptr : &top);
    BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": rho " << report.rho_min << ".." << report.rho_max << ", steepest layer "
                            << Geometry::rad2deg(std::atan(report.max_layer_slope)) << " deg, expected top RMS " << report.predicted_rms << ", layer factor " << report.layer_factor;
    if (report.predicted_rms > give_up_above)
        return fail("expected top RMS " + std::to_string(report.predicted_rms) + " mm, no better");
    if (planar_time * report.layer_factor > time_budget)
        return fail("expected time " + std::to_string(planar_time * report.layer_factor / 60.) + " min, over budget");
    if (admit && !admit(report))
        return fail("not admitted");

    Model        model;
    ModelObject *model_object = model.add_object();
    model_object->name        = name + " deformed";
    model_object->add_volume(TriangleMesh(deform_mesh(mesh_of(), *field, 1.)));
    model_object->add_instance();
    Print print;
    try {
        print.apply(model, deform_print_config(object, strength_road_angle(object, mesh_of(), *field, top.empty() ? nullptr : &top)));
        print.set_status_silent();
        print.process();
    } catch (const std::exception &ex) {
        return fail(std::string("slicing the deformed mesh failed: ") + ex.what());
    }
    if (print.objects().empty() || print.objects().front()->layers().empty())
        return fail("the deformed mesh sliced empty");
    throw_if_canceled(object);
    const PrintObject &sliced = *print.objects().front();
    if (sliced.num_printing_regions() != 1)
        return fail("the deformed slice has several regions");
    const Vec2d center = unscale(sliced.center_offset());

    const double seam_gap = scale_(object.printing_region(0).config().seam_gap.get_abs_value(nozzle));

    const double                                       z0 = field->identity_below();
    for (size_t i = 1; i < sliced.layers().size() && sliced.layers()[i]->print_z <= z0 + EPSILON; ++i)
        if (i >= layers.size() || std::abs(layers[i]->print_z - raft - sliced.layers()[i]->print_z) >= 1e-5)
            return fail("layer " + std::to_string(i) + " below the curved layers is off the host's layers");
    std::vector<std::vector<ExtrusionPathConformal *>> curved(sliced.layers().size());
    std::vector<int>                                   planar_host(sliced.layers().size(), -1);
    const double                                       step = 0.5;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, sliced.layers().size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            const Layer *layer = sliced.layers()[i];
            const double level = layer->print_z;
            if (level <= z0 + EPSILON) {
                if (i < layers.size() && std::abs(layers[i]->print_z - raft - level) < 1e-5)
                    planar_host[i] = int(i);
                continue;
            }
            Flattener flat{ object.config().seam_position.value, layer, seam_gap, Point(0, 0), {} };
            for (const LayerRegion *layerm : layer->regions()) {
                ExtrusionEntityCollection perimeters = layerm->perimeters, fills = layerm->fills;
                flat.add(&perimeters);
                flat.add(&fills);
            }
            for (const ExtrusionPath &p : flat.out) {
                if (p.role() == erIroning)
                    continue;
                map_path(p, center, level, layer->height, *field, raft, step, curved[i]);
            }
        }
    });

    out.planar.clear();
    out.planar.resize(layers.size());
    for (size_t i = 1; i < layers.size(); ++i)
        if (i < planar_host.size() && planar_host[i] >= 0) {
            const LayerRegion *src = sliced.layers()[i]->regions().front();
            out.planar[i]          = std::make_unique<DeformSlice::Planar>(DeformSlice::Planar{ src->perimeters, src->fills, src->thin_fills });
            if (sliced.center_offset() != Point(0, 0)) {
                const Point3 d(sliced.center_offset().x(), sliced.center_offset().y(), coord_t(0));
                for (ExtrusionEntityCollection *c : { &out.planar[i]->perimeters, &out.planar[i]->fills, &out.planar[i]->thin_fills })
                    translate_entity(*c, d);
            }
        }

    const HeadEnvelope head = resolve_head_envelope(host_print.config());
    double             pitch = 0., tolerance = 0.;
    const double inflation = 0.6 * nozzle + 0.25 + 0.5 * MaxSegment;
    const double reach     = object.has_support() ? head_band_height(head) : deform_band_height(head, 1.1 * report.max_layer_slope, inflation);
    double       cap   = multi_material_patch_height(object, head, ocfg.nonplanar_max_patch_height.value);
    cap                = cap > 0. ? std::min(cap, reach) : reach;
    tolerance = std::min(0.5 * cap, 0.45 * report.rho_min * h);
    pitch     = cap - tolerance;
    const std::vector<double>                     cuts = event_cuts(object);
    std::map<std::pair<int, int>, ConformalPatch> bands;
    std::map<std::pair<int, int>, size_t>         band_layer;
    for (size_t k = 0; k < curved.size(); ++k) {
        if (curved[k].empty())
            continue;
        ExtrusionEntitiesPtr paths(curved[k].begin(), curved[k].end());
        cut_into_bands(paths, pitch, tolerance, [&](int b, ExtrusionPathConformal *whole) {
            cut_at_events(whole, cuts, [&](int slab, ExtrusionPathConformal *piece) {
                ConformalPatch &patch = bands[{ b, slab }];
                const auto [it, inserted] = band_layer.try_emplace({ b, slab }, k);
                if (inserted || it->second != k || patch.shell_paths.empty()) {
                    patch.shell_paths.emplace_back();
                    patch.shell_paths.back().no_sort = true;
                    it->second                       = k;
                }
                if (inserted) {
                    patch.z_min = Inf;
                    patch.z_max = -Inf;
                }
                for (const Point3 &pt : piece->polyline.points) {
                    patch.z_min = std::min(patch.z_min, unscale<double>(pt.z()));
                    patch.z_max = std::max(patch.z_max, unscale<double>(pt.z()));
                }
                piece->shell = int(patch.shell_paths.size()) - 1;
                patch.shell_paths.back().entities.push_back(piece);
            });
        });
        for (ExtrusionPathConformal *p : curved[k])
            delete p;
    }
    out.patches.clear();
    for (auto &[key, patch] : bands) {
        const auto [b, slab] = key;
        Point last(0, 0);
        for (ExtrusionEntityCollection &shell : patch.shell_paths)
            link_pieces(shell, last);
        patch.kind         = PatchKind::Volume;
        patch.band         = b;
        patch.region_id    = 0;
        patch.shells       = int(patch.shell_paths.size());
        patch.shell_height = h;
        const double top   = (b + 1) * pitch + tolerance;
        const auto   it    = std::upper_bound(layer_z.begin(), layer_z.end(), top + EPSILON);
        patch.host_layer   = event_host(cuts, slab, layer_z, it == layer_z.begin() ? 0 : size_t(it - layer_z.begin()) - 1);
        const coord_t dz   = coord_t(scale_(layer_z[patch.host_layer]));
        for (ExtrusionEntityCollection &shell : patch.shell_paths)
            for (ExtrusionEntity *ee : shell.entities)
                for (Point3 &pt : static_cast<ExtrusionPathConformal *>(ee)->polyline.points)
                    pt.z() -= dz;
        out.patches.push_back(std::move(patch));
    }
    out.report = report;

    BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": " << sliced.layers().size() << " layers, " << bands.size() << " bands (reach " << reach << ", pitch "
                            << pitch << ", tolerance " << tolerance << "), "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() << " s";
    return true;
}


void apply_deform_slice(PrintObject &object, DeformSlice &&slice, ConformalResult &result)
{
    LayerPtrs &layers = object.layers();
    for (size_t i = 0; i < layers.size(); ++i) {
        Layer *layer = layers[i];
        layer->conformal_patches_before.clear();
        layer->conformal_patches_after.clear();
        layer->conformal_covered = false;
        if (i == 0)
            continue;
        LayerRegion *host = layer->regions().front();
        if (i < slice.planar.size() && slice.planar[i]) {
            host->perimeters = std::move(slice.planar[i]->perimeters);
            host->fills      = std::move(slice.planar[i]->fills);
            host->thin_fills = std::move(slice.planar[i]->thin_fills);
        } else {
            host->perimeters.clear();
            host->fills.clear();
            host->thin_fills.clear();
            layer->conformal_covered = true;
        }
    }
    result.patches.clear();
    result.diagnostics.clear();
    result.planar_exclusion.assign(layers.size(), ExPolygons());
    for (ConformalPatch &patch : slice.patches) {
        patch.id = int(result.patches.size());
        for (ExtrusionEntityCollection &shell : patch.shell_paths)
            for (ExtrusionEntity *ee : shell.entities)
                static_cast<ExtrusionPathConformal *>(ee)->patch_id = patch.id;
        layers[patch.host_layer]->conformal_patches_after.push_back(patch.id);
        result.patches.push_back(std::move(patch));
    }
    for (Layer *layer : layers)
        std::stable_sort(layer->conformal_patches_after.begin(), layer->conformal_patches_after.end(),
                         [&result](int a, int b) { return result.patches[size_t(a)].band < result.patches[size_t(b)].band; });
    slice.patches.clear();
}

namespace {

class TopMetric
{
public:
    static constexpr double Pitch = 0.5;
    static constexpr double Buried = 1.5;

    TopMetric(const MeshRays &rays, double raft)
    {
        const BoundingBoxf3 &bb = rays.bbox;
        m_mesh = HeightField(BoundingBoxf(bb.min.head<2>(), bb.max.head<2>()), Pitch);
        m_dep  = m_mesh;
        const int nx = m_mesh.nx(), ny = m_mesh.ny();
        std::vector<float> nz(m_mesh.data().size(), 0.f);
        {
            const sla::IndexedMesh &imesh = *rays.imesh;
            tbb::parallel_for(tbb::blocked_range<int>(0, ny), [&](const tbb::blocked_range<int> &range) {
                for (int iy = range.begin(); iy < range.end(); ++iy)
                    for (int ix = 0; ix < nx; ++ix) {
                        const Vec2d p   = m_mesh.node(ix, iy) + Vec2d(0.5 * Pitch, 0.5 * Pitch);
                        const auto  hit = imesh.query_ray_hit(Vec3d(p.x(), p.y(), bb.max.z() + 1. - rays.shift), -Vec3d::UnitZ());
                        if (hit.is_hit()) {
                            m_mesh.at(ix, iy)          = float(hit.position().z() + rays.shift + raft);
                            nz[size_t(iy) * nx + ix] = float(std::abs(hit.normal().normalized().z()));
                        }
                    }
            });
        }
        const int    half  = std::max(1, int(std::lround(0.5 / Pitch)));
        const double tan70 = std::tan(Geometry::deg2rad(70.));
        std::vector<char> wall(nz.size(), 0), in(nz.size(), 0);
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix) {
                const size_t n = size_t(iy) * nx + ix;
                const float  z = m_mesh.data()[n];
                if (std::isnan(z))
                    continue;
                in[n]    = 1;
                float lo = z, hi = z;
                for (int dy = -half; dy <= half; ++dy)
                    for (int dx = -half; dx <= half; ++dx) {
                        const int jx = ix + dx, jy = iy + dy;
                        if (jx < 0 || jy < 0 || jx >= nx || jy >= ny || std::isnan(m_mesh.at(jx, jy)))
                            continue;
                        lo = std::min(lo, m_mesh.at(jx, jy)), hi = std::max(hi, m_mesh.at(jx, jy));
                    }
                wall[n] = nz[n] < std::cos(Geometry::deg2rad(70.)) || hi - lo > 2 * half * Pitch * tan70;
            }
        m_eval.assign(nz.size(), 0);
        const int inset = int(std::lround(2. / Pitch)), wall_r = half;
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix) {
                bool ok = in[size_t(iy) * nx + ix] != 0;
                for (int dy = -inset; ok && dy <= inset; ++dy)
                    for (int dx = -inset; ok && dx <= inset; ++dx) {
                        if (dx * dx + dy * dy > inset * inset)
                            continue;
                        const int jx = ix + dx, jy = iy + dy;
                        if (jx < 0 || jy < 0 || jx >= nx || jy >= ny || !in[size_t(jy) * nx + jx])
                            ok = false;
                        else if (std::abs(dx) <= wall_r && std::abs(dy) <= wall_r && wall[size_t(jy) * nx + jx])
                            ok = false;
                    }
                m_eval[size_t(iy) * nx + ix] = ok;
            }
        std::fill(m_dep.data().begin(), m_dep.data().end(), HeightField::NoData);
        for (size_t n = 0; n < m_eval.size(); ++n)
            if (m_eval[n])
                m_floor = std::min(m_floor, double(m_mesh.data()[n]));
    }

    double floor() const { return m_floor - 2.; }

    void layer(double height) { m_layer_h = height; }
    void end_layer() {}

    void add(const Vec3d &a, const Vec3d &b, double width, int contour = 0)
    {
        if ((b - a).head<2>().norm() < 8. * Pitch) {
            auto buried = [&](const Vec2d &q) {
                const Vec2d o  = (q - m_mesh.origin()) / Pitch;
                const int   ix = int(o.x()), iy = int(o.y());
                if (ix < 0 || iy < 0 || ix >= m_mesh.nx() || iy >= m_mesh.ny())
                    return false;
                const float top = m_mesh.at(ix, iy);
                return !std::isnan(top) && top - std::max(a.z(), b.z()) > Buried;
            };
            if (buried(a.head<2>()) && buried(b.head<2>()) && buried(0.5 * (a + b).head<2>()))
                return;
        }
        const double r   = 0.5 * width;
        const Vec2d  d   = (b - a).head<2>();
        const double l2  = std::max(d.squaredNorm(), 1e-18);
        const Vec2d  lo  = a.head<2>().cwiseMin(b.head<2>()) - Vec2d(r, r), hi = a.head<2>().cwiseMax(b.head<2>()) + Vec2d(r, r);
        const Vec2d  o   = m_dep.origin() + Vec2d(0.5 * Pitch, 0.5 * Pitch);
        const int    ix0 = std::max(0, int(std::ceil((lo.x() - o.x()) / Pitch))), ix1 = std::min(m_dep.nx() - 1, int(std::floor((hi.x() - o.x()) / Pitch)));
        const int    iy0 = std::max(0, int(std::ceil((lo.y() - o.y()) / Pitch))), iy1 = std::min(m_dep.ny() - 1, int(std::floor((hi.y() - o.y()) / Pitch)));
        for (int iy = iy0; iy <= iy1; ++iy)
            for (int ix = ix0; ix <= ix1; ++ix) {
                const Vec2d  c = o + Pitch * Vec2d(ix, iy);
                const double t = std::clamp((c - a.head<2>()).dot(d) / l2, 0., 1.);
                if ((a.head<2>() + t * d - c).squaredNorm() > r * r)
                    continue;
                float &v = m_dep.at(ix, iy);
                float      z = float(a.z() + t * (b.z() - a.z()));
                if (contour != 0 && !std::isnan(m_mesh.at(ix, iy))) {
                    const double d = m_mesh.at(ix, iy) - z;
                    if (d >= -m_layer_h && d <= ZaaMinZ + 0.03)
                        z += float(std::clamp(d, -(m_layer_h - ZaaMinZ), contour > 1 ? 0. : ZaaMinZ));
                }
                if (std::isnan(v) || z > v)
                    v = z;
            }
    }

    void add(const ExtrusionEntity &ee, double z)
    {
        std::vector<const ExtrusionPath *> paths;
        collect_paths(&ee, paths);
        for (const ExtrusionPath *path : paths) {
            if (path->role() == erIroning || is_support(path->role()))
                continue;
            const Points3 &pts       = path->polyline.points;
            const auto    *conformal = dynamic_cast<const ExtrusionPathConformal *>(path);
            for (size_t i = 1; i < pts.size(); ++i) {
                if (conformal != nullptr && i < conformal->bead_height.size() &&
                    (conformal->bead_height[i - 1] <= 0.f || conformal->bead_height[i] <= 0.f))
                    continue;
                const double w = conformal != nullptr && !conformal->bead_width.empty() ?
                                     0.5 * (conformal->bead_width[i - 1] + conformal->bead_width[i]) : path->width;
                const double za = conformal != nullptr ? z + unscale<double>(pts[i - 1].z()) : z;
                const double zb = conformal != nullptr ? z + unscale<double>(pts[i].z()) : z;
                const ExtrusionRole r = path->role();
                const int contour = conformal != nullptr ? 0 : r == erTopSolidInfill ? 1 : r == erPerimeter || r == erExternalPerimeter ? 2 : 0;
                this->add(Vec3d(unscale<double>(pts[i - 1].x()), unscale<double>(pts[i - 1].y()), za),
                          Vec3d(unscale<double>(pts[i].x()), unscale<double>(pts[i].y()), zb), w, contour);
            }
        }
    }

    void add(const ConformalPatch &patch, double host_z)
    {
        for (const ExtrusionEntityCollection &shell : patch.shell_paths)
            this->add(shell, host_z);
    }

    void clear() { std::fill(m_dep.data().begin(), m_dep.data().end(), HeightField::NoData); }

    double rms() const
    {
        double sum = 0.;
        size_t n   = 0;
        for (size_t i = 0; i < m_eval.size(); ++i)
            if (m_eval[i] && !std::isnan(m_dep.data()[i]) && !std::isnan(m_mesh.data()[i])) {
                const double e = m_dep.data()[i] - m_mesh.data()[i];
                sum += e * e;
                ++n;
            }
        return n == 0 ? 0. : std::sqrt(sum / n);
    }

private:
    HeightField       m_mesh, m_dep;
    std::vector<char> m_eval;
    double            m_layer_h = 0.2;
    double            m_floor   = Inf;
};

class TimeProxy
{
public:
    explicit TimeProxy(const PrintObject &object)
    {
        const PrintConfig       &pc = object.print()->config();
        const PrintObjectConfig &oc = object.config();
        const PrintRegionConfig &rc = object.printing_region(0).config();
        auto first = [](const auto &opt, double fallback) { return opt.values.empty() || opt.values.front() <= 0. ? fallback : double(opt.values.front()); };
        const double machine_a = first(pc.machine_max_acceleration_extruding, 1e9);
        const double def_a     = std::min(first(oc.default_acceleration, 2000.), machine_a);
        auto accel = [&](const ConfigOptionFloatsNullable &opt) { return std::min(first(opt, def_a), machine_a); };
        auto accel_pct = [&](const ConfigOptionFloatsOrPercentsNullable &opt) {
            const double v = opt.values.empty() ? 0. : opt.get_at(0).get_abs_value(def_a);
            return std::min(v > 0. ? v : def_a, machine_a);
        };
        m_vz     = first(pc.machine_max_speed_z, 10.);
        m_rate   = planner_segment_rate(pc.nonplanar_segment_rate.value, pc.gcode_flavor.value, object.print()->is_BBL_printer());
        m_travel = first(pc.travel_speed, 150.);
        m_travel_a = std::min(first(oc.travel_acceleration, def_a), first(pc.machine_max_acceleration_travel, 1e9));
        m_decel  = pc.accel_to_decel_enable.value ? std::clamp(0.01 * pc.accel_to_decel_factor.value, 0.05, 1.) : 1.;
        m_jerk   = std::max(1., first(pc.machine_max_jerk_x, 8.));
        m_retract = 2. * first(pc.retraction_length, 0.8) / 30. + 2. * (first(pc.z_hop, 0.4) + 0.2) / m_vz;
        m_layer_floor = first(pc.slow_down_layer_time, 0.);
        m_speed[0] = first(rc.outer_wall_speed, 60.);
        m_speed[1] = first(rc.inner_wall_speed, 100.);
        m_speed[2] = first(rc.sparse_infill_speed, 100.);
        m_speed[3] = first(rc.internal_solid_infill_speed, 100.);
        m_speed[4] = first(rc.top_surface_speed, 60.);
        m_speed[5] = first(rc.gap_infill_speed, 50.);
        m_speed[6] = first(rc.bridge_speed, 30.);
        m_accel[0] = accel(oc.outer_wall_acceleration);
        m_accel[1] = accel(oc.inner_wall_acceleration);
        m_accel[2] = accel_pct(oc.sparse_infill_acceleration);
        m_accel[3] = accel_pct(oc.internal_solid_infill_acceleration);
        m_accel[4] = accel(oc.top_surface_acceleration);
        m_accel[5] = def_a;
        m_accel[6] = accel_pct(oc.bridge_acceleration);
        auto lowest = [](const ConfigOptionFloats &opt) {
            double v = 0.;
            for (double x : opt.values)
                v = x > 0. ? (v > 0. ? std::min(v, x) : x) : v;
            return v;
        };
        auto pair_min = [](double a, double b) { return a > 0. && b > 0. ? std::min(a, b) : std::max(a, b); };
        m_limits.max_v_xy    = pair_min(lowest(pc.machine_max_speed_x), lowest(pc.machine_max_speed_y));
        m_limits.max_a_xy    = pair_min(lowest(pc.machine_max_acceleration_x), lowest(pc.machine_max_acceleration_y));
        m_limits.max_v_z     = 0.8 * lowest(pc.machine_max_speed_z);
        m_limits.max_a_z     = lowest(pc.machine_max_acceleration_z);
        m_limits.max_jerk_xy = pair_min(lowest(pc.machine_max_jerk_x), lowest(pc.machine_max_jerk_y));
        m_limits.max_jerk_z  = lowest(pc.machine_max_jerk_z);
        if (pc.gcode_flavor.value == gcfKlipper && m_limits.max_a_xy > 0.)
            m_limits.junction_deviation = m_limits.max_jerk_xy * m_limits.max_jerk_xy * (std::sqrt(2.) - 1.) / m_limits.max_a_xy;
        else if (pc.gcode_flavor.value == gcfMarlinFirmware)
            m_limits.junction_deviation = lowest(pc.machine_max_junction_deviation);
        m_limits.segment_rate = m_rate;
    }

    void add(const ExtrusionEntity &ee, double z)
    {
        std::vector<const ExtrusionPath *> paths;
        collect_paths(&ee, paths);
        for (const ExtrusionPath *path : paths) {
            const Points3 &pts = path->polyline.points;
            if (pts.size() < 2 || is_support(path->role()))
                continue;
            const auto  *c   = dynamic_cast<const ExtrusionPathConformal *>(path);
            const int    r   = this->role_index(path->role());
            const double v   = m_speed[r], a = m_accel[r];
            std::vector<Vec3d> p3(pts.size());
            for (size_t i = 0; i < pts.size(); ++i)
                p3[i] = Vec3d(unscale<double>(pts[i].x()), unscale<double>(pts[i].y()), c != nullptr ? z + unscale<double>(pts[i].z()) : z);
            if (m_have) {
                const double d = (p3.front() - m_last).head<2>().norm();
                if (d > 2. * path->width)
                    m_layer += this->trapezoid(d, m_travel, m_travel_a, m_jerk, m_jerk) + m_retract;
                else
                    m_layer += this->trapezoid(d, v, a, m_jerk, m_jerk);
            }
            std::vector<double> feed(pts.size() - 1, v);
            if (c != nullptr) {
                std::vector<double> nominal(pts.size() - 1);
                std::vector<bool>   glides(pts.size() - 1);
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    glides[i]  = i + 1 < c->bead_height.size() && (c->bead_height[i] <= 0.f || c->bead_height[i + 1] <= 0.f);
                    nominal[i] = glides[i] ? m_travel : v;
                }
                feed = limit_feedrates(p3, nominal, m_limits, 0.05, nullptr, &glides);
            } else
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    const double l = (p3[i + 1] - p3[i]).norm(), dz = std::abs(p3[i + 1].z() - p3[i].z());
                    if (dz > 1e-9)
                        feed[i] = std::min(feed[i], m_vz * l / dz);
                }
            auto junction = [&](size_t i) {
                const Vec3d  u = (p3[i] - p3[i - 1]).normalized(), w = (p3[i + 1] - p3[i]).normalized();
                const double vm = std::min(feed[i - 1], feed[i]);
                return u.dot(w) > std::cos(Geometry::deg2rad(20.)) ? vm : std::min(vm, m_jerk);
            };
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const double l  = (p3[i + 1] - p3[i]).norm();
                const double v0 = i == 0 ? m_jerk : junction(i), v1 = i + 2 == pts.size() ? m_jerk : junction(i + 1);
                m_layer += feed[i] > 0. ? this->trapezoid(l, feed[i], a, v0, v1) : 0.;
            }
            m_last = p3.back();
            m_have = true;
        }
    }

    void add(const ConformalPatch &patch, double host_z)
    {
        if (!m_in_patches)
            m_planar_layer = m_layer, m_in_patches = true;
        for (const ExtrusionEntityCollection &shell : patch.shell_paths)
            this->add(shell, host_z);
    }

    void layer(double) {}
    void end_layer()
    {
        const double planar = m_in_patches ? m_planar_layer : m_layer;
        if (m_layer > 0.)
            m_time += std::max(m_layer, m_layer_floor);
        if (planar > 0.)
            m_planar += std::max(planar, m_layer_floor);
        m_layer = 0., m_planar_layer = 0., m_in_patches = false;
    }
    double floor() const { return -Inf; }

    double seconds() const { return m_time + std::max(m_layer, m_layer > 0. ? m_layer_floor : 0.); }
    double planar_seconds() const { return m_planar; }

private:
    int role_index(ExtrusionRole role) const
    {
        switch (role) {
        case erExternalPerimeter: return 0;
        case erPerimeter: return 1;
        case erInternalInfill: return 2;
        case erSolidInfill: return 3;
        case erTopSolidInfill: return 4;
        case erGapFill: return 5;
        case erBridgeInfill:
        case erOverhangPerimeter: return 6;
        default: return 3;
        }
    }
    double trapezoid(double l, double v, double a, double v0, double v1) const
    {
        if (l <= 0. || v <= 0.)
            return 0.;
        v0 = std::min(v0, v), v1 = std::min(v1, v);
        const double ad  = a * m_decel;
        const double acc = (v * v - v0 * v0) / (2. * a), dec = (v * v - v1 * v1) / (2. * ad);
        if (acc + dec <= l)
            return (v - v0) / a + (v - v1) / ad + (l - acc - dec) / v;
        const double vp = std::sqrt(std::max(0., (2. * l * a * ad + ad * v0 * v0 + a * v1 * v1) / (a + ad)));
        return std::max(0., vp - v0) / a + std::max(0., vp - v1) / ad;
    }

    double       m_vz = 10., m_travel = 150., m_travel_a = 2000., m_decel = 1., m_jerk = 8., m_retract = 0., m_rate = 0.;
    double       m_layer_floor = 0.;
    double       m_speed[7] {}, m_accel[7] {};
    MotionLimits m_limits;
    double       m_time = 0., m_layer = 0., m_planar = 0., m_planar_layer = 0.;
    bool         m_in_patches = false;
    Vec3d        m_last { Vec3d::Zero() };
    bool         m_have = false;
};

class OverhangLength
{
public:
    void add(const ExtrusionEntity &ee, double)
    {
        std::vector<const ExtrusionPath *> paths;
        collect_paths(&ee, paths);
        for (const ExtrusionPath *path : paths) {
            const ExtrusionRole r = path->role();
            if (r != erOverhangPerimeter && r != erBridgeInfill)
                continue;
            const auto    *conformal = dynamic_cast<const ExtrusionPathConformal *>(path);
            const Points3 &pts       = path->polyline.points;
            for (size_t i = 1; i < pts.size(); ++i) {
                const double h = conformal != nullptr && i < conformal->bead_height.size() ?
                                     0.5 * (conformal->bead_height[i - 1] + conformal->bead_height[i]) : path->height;
                m_length += unscale<double>((pts[i].to_point() - pts[i - 1].to_point()).cast<double>().norm()) * h;
            }
        }
    }
    void add(const ConformalPatch &patch, double)
    {
        for (const ExtrusionEntityCollection &shell : patch.shell_paths)
            this->add(shell, 0.);
    }
    void   layer(double) {}
    void   end_layer() {}
    double floor() const { return -Inf; }
    double length() const { return m_length; }

private:
    double m_length = 0.;
};

template<class Proxy> void walk(Proxy &proxy, const PrintObject &object, const ConformalResult *result, const DeformSlice *slice)
{
    const auto layers = object.layers();
    std::vector<std::vector<const ConformalPatch *>> hosted(layers.size());
    if (slice != nullptr)
        for (const ConformalPatch &patch : slice->patches)
            hosted[patch.host_layer].push_back(&patch);
    else if (result != nullptr)
        for (size_t i = 0; i < layers.size(); ++i)
            for (const std::vector<int> *ids : { &layers[i]->conformal_patches_before, &layers[i]->conformal_patches_after })
                for (int id : *ids)
                    if (id >= 0 && size_t(id) < result->patches.size())
                        hosted[i].push_back(&result->patches[size_t(id)]);
    for (size_t i = 0; i < layers.size(); ++i) {
        const Layer *layer = layers[i];
        proxy.layer(layer->height);
        if (layer->print_z < proxy.floor() && hosted[i].empty())
            continue;
        if (slice != nullptr && i > 0) {
            if (i < slice->planar.size() && slice->planar[i]) {
                proxy.add(slice->planar[i]->perimeters, layer->print_z);
                proxy.add(slice->planar[i]->fills, layer->print_z);
                proxy.add(slice->planar[i]->thin_fills, layer->print_z);
            }
        } else
            for (const LayerRegion *layerm : layer->regions()) {
                proxy.add(layerm->perimeters, layer->print_z);
                proxy.add(layerm->fills, layer->print_z);
                proxy.add(layerm->thin_fills, layer->print_z);
            }
        std::stable_sort(hosted[i].begin(), hosted[i].end(), [](const ConformalPatch *a, const ConformalPatch *b) { return a->band < b->band; });
        for (const ConformalPatch *patch : hosted[i])
            proxy.add(*patch, layer->print_z);
        proxy.end_layer();
    }
}

}

double print_time_proxy(const PrintObject &object, const ConformalResult *result, const DeformSlice *slice)
{
    TimeProxy proxy(object);
    walk(proxy, object, result, slice);
    return proxy.seconds();
}

bool choose_deform_slice(PrintObject &object, ConformalResult &result)
{
    struct CpuLog
    {
        const std::string &name;
        const std::clock_t start = std::clock();
        ~CpuLog() { BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": choice took " << double(std::clock() - start) / CLOCKS_PER_SEC << " s CPU"; }
    } cpu_log { object.model_object()->name };
    const std::string &name   = object.model_object()->name;
    const auto         layers = object.layers();
    const ObjectMesh &om = object_mesh(object, result);
    MeshRays          rays;
    {
        const BoundingBoxf3 box = om.mesh.bounding_box();
        rays.imesh              = &om.imesh;
        rays.shift              = -(box.min.z() + om.ground_offset);
        rays.bbox               = BoundingBoxf3(Vec3d(box.min.x(), box.min.y(), box.min.z() + rays.shift),
                                                Vec3d(box.max.x(), box.max.y(), box.max.z() + rays.shift));
    }
    std::unique_ptr<TopMetric> metric;
    double                     shells = 0., t_shells = -1., t_planar = 0., best_rms = 0.;
    auto score_rms = [&]() {
        if (metric)
            return;
        metric = std::make_unique<TopMetric>(rays, layers.front()->print_z - (layers.front()->slice_z + 0.5 * layers.front()->height));
        walk(*metric, object, &result, nullptr);
        shells   = metric->rms();
        best_rms = 0.95 * shells;
    };
    auto score_time = [&]() {
        if (t_shells >= 0.)
            return;
        TimeProxy proxy(object);
        walk(proxy, object, &result, nullptr);
        t_shells = proxy.seconds();
        t_planar = proxy.planar_seconds();
        BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": shells top RMS " << shells << " mm, " << t_shells / 60.
                                << " min, their planar layers " << t_planar / 60. << " min";
    };
    auto admit = [&](const DeformFieldReport &r) {
        if (r.predicted_rms > MaxPredicted)
            return false;
        score_rms();
        const double crossing = object.config().layer_height.value / std::sqrt(12.);
        if (shells < 0.5 * crossing / 0.95 || shells > MaxShells * crossing || r.predicted_rms > 0.8 * shells)
            return false;
        score_time();
        return t_planar * r.layer_factor <= MaxTimeRatioPredicted * t_shells;
    };
    std::unique_ptr<DeformSlice> best;
    if (auto slice = std::make_unique<DeformSlice>();
        compute_deform_slice(object, *slice, nullptr, std::numeric_limits<double>::infinity(), nullptr, 0.,
                             std::numeric_limits<double>::infinity(), admit, &rays)) {
        metric->clear();
        walk(*metric, object, nullptr, slice.get());
        const double rms = metric->rms();
        const double t   = print_time_proxy(object, nullptr, slice.get());
        OverhangLength over, mine;
        walk(over, object, nullptr, slice.get());
        walk(mine, object, &result, nullptr);
        const bool ok = rms < best_rms && t <= 1.2 * t_shells && over.length() <= 1.1 * mine.length() + 0.2;
        BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": top RMS " << rms << " mm against the shells' " << shells
                                << " mm, time " << t / 60. << " min against " << t_shells / 60. << ", overhang " << over.length() << " mm2 against "
                                << mine.length() << (ok ? ", kept" : ", rejected");
        if (ok)
            best = std::move(slice);
    }
    BOOST_LOG_TRIVIAL(info) << "Deform slice " << name << ": " << (best ? "curved layers" : "curved shells");
    if (!best)
        return false;
    apply_deform_slice(object, std::move(*best), result);
    return true;
}

bool build_deform_slice(PrintObject &object, ConformalResult &result, std::string *why)
{
    DeformSlice slice;
    if (!compute_deform_slice(object, slice, why))
        return false;
    apply_deform_slice(object, std::move(slice), result);
    return true;
}

}
