#include "LayerSources.hpp"

#include "DeformSlice.hpp"
#include "HeadEnvelope.hpp"
#include "HeightField.hpp"
#include "LayerField.hpp"
#include "LayerFieldOpt.hpp"
#include "Multiplanar.hpp"
#include "PatchTools.hpp"
#include "SurfaceClip.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <Eigen/Core>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>
#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr double Inf = std::numeric_limits<double>::infinity();

constexpr double ConeAngle = 15.;

constexpr double TargetWeight = 1000.;

constexpr double CappedOff = 0.05;


double sample_clamped(const HeightField &f, const Vec2d &xy)
{
    const Vec2d  o  = (xy - f.origin()) / f.pitch();
    const double fx = std::clamp(o.x(), 0., double(f.nx() - 1)), fy = std::clamp(o.y(), 0., double(f.ny() - 1));
    const int    ix = std::min(int(fx), std::max(0, f.nx() - 2)), iy = std::min(int(fy), std::max(0, f.ny() - 2));
    const int    jx = std::min(ix + 1, f.nx() - 1), jy = std::min(iy + 1, f.ny() - 1);
    const double tx = fx - ix, ty = fy - iy;
    return (1. - ty) * ((1. - tx) * f.at(ix, iy) + tx * f.at(jx, iy)) + ty * ((1. - tx) * f.at(ix, jy) + tx * f.at(jx, jy));
}



struct SourceParams
{
    double h, first, nozzle;
    double z0;
    double cap;
    double min_ratio;
    double max_ratio;

    double ka() const { return 1. / min_ratio - 1.; }
    double kb() const { return 1. - 1. / max_ratio; }
};

SourceParams source_params(const PrintObject &object)
{
    const PrintConfig &pc = object.print()->config();
    SourceParams       P;
    P.h         = object.config().layer_height.value;
    P.first     = pc.initial_layer_print_height.value;
    P.nozzle    = pc.nozzle_diameter.values.empty() ? 0.4 : pc.nozzle_diameter.values.front();
    P.z0        = P.first + 2. * P.h;
    P.cap       = P.h / (0.75 * P.nozzle);
    P.min_ratio = 0.5;
    P.max_ratio = std::min(1.5, 0.8 * P.nozzle / P.h);
    return P;
}



HeightField top_of(const indexed_triangle_set &mesh, double margin)
{
    BoundingBoxf3 bb;
    for (const stl_vertex &v : mesh.vertices)
        bb.merge(v.cast<double>());
    const double extent = std::max(bb.size().x(), bb.size().y());
    const double pitch  = std::clamp(extent / 250., 0.5, 1.);
    HeightField  T(BoundingBoxf(bb.min.head<2>() - Vec2d(margin, margin), bb.max.head<2>() + Vec2d(margin, margin)), pitch);
    sla::IndexedMesh imesh(mesh);
    tbb::parallel_for(tbb::blocked_range<int>(0, T.ny()), [&](const tbb::blocked_range<int> &range) {
        for (int iy = range.begin(); iy < range.end(); ++iy)
            for (int ix = 0; ix < T.nx(); ++ix)
                T.at(ix, iy) = float(vertical_ray_hit(imesh, T.node(ix, iy), bb.max.z() + 1., -1.));
    });
    return T;
}


std::pair<double, double> range_over(const HeightField &S, const std::vector<char> &foot)
{
    double lo = Inf, hi = -Inf;
    for (size_t n = 0; n < foot.size(); ++n)
        if (foot[n])
            lo = std::min(lo, double(S.data()[n])), hi = std::max(hi, double(S.data()[n]));
    return lo <= hi ? std::make_pair(lo, hi) : std::make_pair(0., 0.);
}




HeightField solve_surface(const HeightField &T, const std::vector<int> &comp, int ncomp, const std::vector<double> *fixed,
                          std::vector<double> &zc)
{
    const int    nx = T.nx(), ny = T.ny();
    const size_t N  = size_t(nx) * ny;
    const int    nz = fixed != nullptr ? 0 : ncomp;
    using Triplet   = Eigen::Triplet<double>;
    std::vector<Triplet> tr;
    tr.reserve(N * 7 + size_t(ncomp));
    Eigen::VectorXd b = Eigen::VectorXd::Zero(Eigen::Index(N) + nz);
    auto edge = [&](size_t p, size_t q) {
        tr.emplace_back(Eigen::Index(p), Eigen::Index(p), 1.);
        tr.emplace_back(Eigen::Index(q), Eigen::Index(q), 1.);
        tr.emplace_back(Eigen::Index(p), Eigen::Index(q), -1.);
        tr.emplace_back(Eigen::Index(q), Eigen::Index(p), -1.);
    };
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            const size_t p = size_t(iy) * nx + ix;
            if (ix + 1 < nx)
                edge(p, p + 1);
            if (iy + 1 < ny)
                edge(p, p + nx);
            tr.emplace_back(Eigen::Index(p), Eigen::Index(p), 1e-6);
            const int c = comp[p];
            if (c < 0)
                continue;
            const double t = T.data()[p];
            tr.emplace_back(Eigen::Index(p), Eigen::Index(p), TargetWeight);
            if (fixed != nullptr) {
                b[Eigen::Index(p)] += TargetWeight * (t + (*fixed)[size_t(c)]);
            } else {
                const Eigen::Index z = Eigen::Index(N) + c;
                tr.emplace_back(z, z, TargetWeight);
                tr.emplace_back(Eigen::Index(p), z, -TargetWeight);
                tr.emplace_back(z, Eigen::Index(p), -TargetWeight);
                b[Eigen::Index(p)] += TargetWeight * t;
                b[z] -= TargetWeight * t;
            }
        }
    for (int c = 0; c < nz; ++c)
        tr.emplace_back(Eigen::Index(N) + c, Eigen::Index(N) + c, 1e-6);
    Eigen::SparseMatrix<double> A(Eigen::Index(N) + nz, Eigen::Index(N) + nz);
    A.setFromTriplets(tr.begin(), tr.end());

    double mean = 0.;
    size_t cnt  = 0;
    for (float v : T.data())
        if (!std::isnan(v))
            mean += v, ++cnt;
    mean = cnt > 0 ? mean / double(cnt) : 0.;
    Eigen::VectorXd x0(A.rows());
    for (size_t p = 0; p < N; ++p)
        x0[Eigen::Index(p)] = std::isnan(T.data()[p]) ? mean : T.data()[p];
    for (int c = 0; c < nz; ++c)
        x0[Eigen::Index(N) + c] = 0.;
    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper> cg;
    cg.setMaxIterations(5000);
    cg.setTolerance(1e-9);
    cg.compute(A);
    const Eigen::VectorXd x = cg.solveWithGuess(b, x0);
    HeightField S = T;
    for (size_t p = 0; p < N; ++p)
        S.data()[p] = float(x[Eigen::Index(p)]);
    if (fixed != nullptr)
        zc = *fixed;
    else {
        zc.assign(size_t(ncomp), 0.);
        for (int c = 0; c < ncomp; ++c)
            zc[size_t(c)] = x[Eigen::Index(N) + c];
    }
    return S;
}


int label_components(const HeightField &T, const std::vector<char> &target, double cap, std::vector<int> &comp)
{
    const int nx = T.nx(), ny = T.ny();
    comp.assign(target.size(), -1);
    int                 ncomp = 0;
    std::vector<size_t> stack;
    const double        step = 2. * cap * T.pitch();
    for (size_t s = 0; s < target.size(); ++s) {
        if (!target[s] || comp[s] >= 0)
            continue;
        comp[s] = ncomp;
        stack.assign(1, s);
        while (!stack.empty()) {
            const size_t p = stack.back();
            stack.pop_back();
            const int ix = int(p % size_t(nx)), iy = int(p / size_t(nx));
            for (const auto &[dx, dy] : { std::pair(1, 0), std::pair(-1, 0), std::pair(0, 1), std::pair(0, -1) }) {
                const int jx = ix + dx, jy = iy + dy;
                if (jx < 0 || jy < 0 || jx >= nx || jy >= ny)
                    continue;
                const size_t q = size_t(jy) * nx + jx;
                if (target[q] && comp[q] < 0 && std::abs(T.data()[q] - T.data()[p]) <= step) {
                    comp[q] = ncomp;
                    stack.push_back(q);
                }
            }
        }
        ++ncomp;
    }
    return ncomp;
}




std::pair<double, double> ramp_of(double lo, double hi, bool balanced, const SourceParams &P)
{
    if (!balanced)
        return { -hi, std::max((hi - lo) / P.ka(), P.h) };
    const double ramp = std::max((hi - lo) / (P.ka() + P.kb()), P.h);
    return { -lo - P.ka() * ramp, ramp };
}



std::unique_ptr<OffsetLayerField> offset_field(HeightField S, const std::vector<char> &foot, double shift, double top_z,
                                               const SourceParams &P, std::string *why)
{
    const auto [lo0, hi0] = range_over(S, foot);
    const double lo = lo0 + shift, hi = hi0 + shift;
    for (float &v : S.data())
        v = float(std::clamp(double(v) + shift, lo, hi));
    const double ramp = std::max({ -lo / P.ka(), hi / P.kb(), P.h });
    if (P.z0 + ramp + P.h > top_z) {
        if (why != nullptr)
            *why = "the top's relief (" + std::to_string(hi - lo).substr(0, 4) + " mm) needs a " + std::to_string(ramp).substr(0, 4) +
                   " mm base ramp, more than the part's height allows";
        return nullptr;
    }
    return std::make_unique<OffsetLayerField>(std::move(S), P.z0, ramp);
}




std::vector<std::pair<double, double>> band_steps(const HeadEnvelope &head, const SourceParams &P)
{
    const double inflation = 0.6 * P.nozzle + 0.25 + 0.3;
    std::vector<std::pair<double, double>> steps;
    double prev_band = -1.;
    for (double deg = 0.25; deg <= Geometry::rad2deg(std::atan(P.cap)) + 1e-9; deg += 0.25) {
        const double slope = std::tan(Geometry::deg2rad(deg));
        const double band  = deform_band_height(head, slope, inflation);
        if (band != prev_band)
            steps.emplace_back(slope, band), prev_band = band;
        else
            steps.back().first = slope;
    }
    for (size_t i = 0; i + 1 < steps.size(); ++i)
        steps[i].first *= 0.85;
    if (steps.empty() || steps.back().first < P.cap)
        steps.emplace_back(P.cap, steps.empty() ? head_band_height(head) : steps.back().second);
    return steps;
}



struct QuickCurveSurface
{
    HeightField       S;
    double            shift = 0.;
    std::vector<char> target;
    double            followed = 0.;
    int               components = 0;
};

QuickCurveSurface quickcurve_surface(const HeightField &T, const std::vector<char> &foot, const SourceParams &P, double cap, bool balanced)
{
    const HeightField Tcap = slope_cap_minorant(T, std::atan(cap));
    const size_t      N    = T.data().size();


    auto targets = [&](double z_f) {
        std::vector<char> t(N, 0);
        for (int iy = 1; iy + 1 < T.ny(); ++iy)
            for (int ix = 1; ix + 1 < T.nx(); ++ix) {
                const size_t n = size_t(iy) * T.nx() + ix;
                const float  s = T.data()[n], c = Tcap.data()[n];
                if (!foot[n] || std::isnan(c) || s < z_f || s - c > CappedOff)
                    continue;
                const Vec2d g = T.gradient(T.node(ix, iy));
                t[n] = g.allFinite() && g.norm() <= cap;
            }
        return t;
    };
    QuickCurveSurface   out;
    std::vector<int>    comp;
    std::vector<double> zc;
    double              z_f = P.z0 + P.h;
    for (int round = 0; round < 6; ++round) {
        std::vector<char> next = targets(z_f);
        if (round > 0 && next == out.target)
            break;
        out.target     = std::move(next);
        out.components = label_components(T, out.target, cap, comp);
        if (out.components == 0)
            return out;
        out.S               = slope_cap_minorant(solve_surface(T, comp, out.components, nullptr, zc), std::atan(cap));
        const auto [lo, hi] = range_over(out.S, foot);
        z_f                 = P.z0 + ramp_of(lo, hi, balanced, P).second + P.h;
    }

    const auto [lo, hi] = range_over(out.S, foot);
    out.shift           = ramp_of(lo, hi, balanced, P).first;
    for (double &z : zc)
        z = -(P.first + P.h * std::round((-z - out.shift - P.first) / P.h)) - out.shift;
    out.S = slope_cap_minorant(solve_surface(T, comp, out.components, &zc, zc), std::atan(cap));


    const double tan_min = std::tan(Geometry::deg2rad(1.));
    for (size_t n = 0; n < N; ++n)
        if (out.target[n] && T.gradient(T.node(int(n % size_t(T.nx())), int(n / size_t(T.nx())))).norm() >= tan_min) {
            const double level = T.data()[n] - (out.S.data()[n] + out.shift);
            if (std::abs(level - (P.first + P.h * std::round((level - P.first) / P.h))) <= CappedOff)
                out.followed += T.pitch() * T.pitch();
        }
    return out;
}

std::unique_ptr<LayerField> build_quickcurve(const PrintObject &object, const indexed_triangle_set &mesh, const SourceParams &P,
                                             std::string *why)
{
    const HeightField T = top_of(mesh, 3.);
    const size_t      N = T.data().size();
    std::vector<char> foot(N, 0);
    double            top_z = 0., top_area = 0.;
    for (size_t n = 0; n < N; ++n)
        if (const float t = T.data()[n]; !std::isnan(t) && t > P.z0) {
            foot[n] = 1;
            top_z   = std::max(top_z, double(t));
            top_area += T.pitch() * T.pitch();
        }




    const HeadEnvelope                           head  = resolve_head_envelope(object.print()->config());
    const std::vector<std::pair<double, double>> steps = band_steps(head, P);
    struct Candidate { double cap, band; bool balanced; };
    std::vector<Candidate>         candidates;
    std::vector<QuickCurveSurface> surfaces;
    double                         best = 0.;
    for (size_t i = 0; i < steps.size(); ++i)
        for (const bool balanced : { false, true }) {

            const double cap = i + 1 == steps.size() || !balanced ? steps[i].first : steps[i].first / P.max_ratio;
            candidates.push_back({ cap, steps[i].second, balanced });
            surfaces.push_back(quickcurve_surface(T, foot, P, cap, balanced));
            best = std::max(best, surfaces.back().followed);
        }
    size_t pick = surfaces.size();
    for (size_t i = 0; i < surfaces.size(); ++i)
        if (surfaces[i].components > 0 && best > 0. && surfaces[i].followed >= 0.85 * best &&
            (pick == surfaces.size() || candidates[i].band > candidates[pick].band ||
             (candidates[i].band == candidates[pick].band && surfaces[i].followed > surfaces[pick].followed)))
            pick = i;
    if (pick == surfaces.size()) {
        if (why != nullptr)
            *why = "no top within the head's slope cap stands above the base ramp";
        return nullptr;
    }
    auto field = offset_field(std::move(surfaces[pick].S), foot, surfaces[pick].shift, top_z, P, why);
    if (field)
        BOOST_LOG_TRIVIAL(info) << "QuickCurve " << object.model_object()->name << ": cap " << Geometry::rad2deg(std::atan(candidates[pick].cap))
                                << " deg for " << candidates[pick].band << " mm bands, " << (candidates[pick].balanced ? "balanced" : "thin")
                                << " ramp, " << surfaces[pick].components << " top components, ramp "
                                << field->ramp() << " mm from z " << P.z0 << ", top followed " << surfaces[pick].followed << " of " << top_area
                                << " mm2 (best " << best << ")";
    return field;
}

std::unique_ptr<LayerField> build_conical(const indexed_triangle_set &mesh, const SourceParams &P, const std::string &name,
                                          std::string *why)
{
    HeightField       T = top_of(mesh, 3.);
    const size_t      N = T.data().size();
    std::vector<char> foot(N, 0);
    Vec2d             centre = Vec2d::Zero();
    double            area = 0., top_z = 0.;
    for (int iy = 0; iy < T.ny(); ++iy)
        for (int ix = 0; ix < T.nx(); ++ix) {
            const size_t n = size_t(iy) * T.nx() + ix;
            if (const float t = T.data()[n]; !std::isnan(t)) {
                centre += T.node(ix, iy);
                area += 1.;
                if (t > P.z0)
                    foot[n] = 1, top_z = std::max(top_z, double(t));
            }
        }
    if (area == 0.) {
        if (why != nullptr)
            *why = "the part has no footprint";
        return nullptr;
    }
    centre /= area;
    const double slope = std::min(std::tan(Geometry::deg2rad(ConeAngle)), P.cap);
    HeightField  S     = T;
    for (int iy = 0; iy < T.ny(); ++iy)
        for (int ix = 0; ix < T.nx(); ++ix)
            S.at(ix, iy) = float(-slope * (T.node(ix, iy) - centre).norm());
    const double shift = -range_over(S, foot).second;
    auto         field = offset_field(std::move(S), foot, shift, top_z, P, why);
    if (field)
        BOOST_LOG_TRIVIAL(info) << "Conical " << name << ": cone " << Geometry::rad2deg(std::atan(slope)) << " deg about (" << centre.x()
                                << ", " << centre.y() << "), ramp " << field->ramp() << " mm";
    return field;
}




class SolvedLayerField final : public LayerField
{
public:
    SolvedLayerField(StackLayerField f, double identity) : m_f(std::move(f)), m_identity(identity) {}
    double phi(const Vec3d &p) const override { return m_f.phi(p); }
    double z_at(const Vec2d &xy, double level) const override { return m_f.z_at(xy, level); }
    Vec3d  gradient(const Vec3d &p) const override { return m_f.gradient(p); }
    double identity_below() const override { return m_identity; }

private:
    StackLayerField m_f;
    double          m_identity;
};

std::unique_ptr<LayerField> build_optimized(const PrintObject &object, const indexed_triangle_set &mesh, const SourceParams &P,
                                            std::string *why)
{
    const HeadEnvelope head = resolve_head_envelope(object.print()->config());
    LayerFieldParams   lp;
    lp.layer_height      = P.h;
    lp.min_layer_height  = P.min_ratio * P.h;
    lp.max_layer_height  = P.max_ratio * P.h;
    lp.flat_base_height  = P.z0;
    lp.max_slope         = std::atan(P.cap);
    lp.head              = &head;
    lp.throw_if_canceled = [&object]() { throw_if_canceled(object); };
    LayerFieldReport report;
    StackLayerField  f = solve_layer_field(mesh, lp, &report);
    BOOST_LOG_TRIVIAL(info) << "Optimized layer field " << object.model_object()->name << ": " << report.solve_seconds << " s, conformal top "
                            << report.planar.conformal_top << " -> " << report.field.conformal_top << ", overhang " << report.planar.overhang_area
                            << " -> " << report.field.overhang_area << " mm2";

    double dev = 0.;
    for (int k = 0; k < f.levels(); ++k)
        for (size_t n = 0; n < size_t(f.nx()) * f.ny(); ++n)
            dev = std::max(dev, std::abs(double(f.data()[size_t(k) * f.nx() * f.ny() + n]) - k * f.level_spacing()));
    if (f.empty() || dev < 0.01) {
        if (why != nullptr)
            *why = "the layer field solver keeps planar layers on this part: no top it can follow or overhang it can lean";
        return nullptr;
    }
    const double identity = P.first + P.h * std::floor((f.identity_below() - P.first) / P.h + 1e-6);
    return std::make_unique<SolvedLayerField>(std::move(f), std::max(identity, P.z0));
}

}

OffsetLayerField::OffsetLayerField(HeightField S, double z0, double ramp) : m_S(std::move(S)), m_z0(z0), m_ramp(ramp) {}

double OffsetLayerField::s(const Vec2d &xy) const { return sample_clamped(m_S, xy); }

double OffsetLayerField::phi(const Vec3d &p) const
{
    if (p.z() <= m_z0)
        return p.z();
    return p.z() - this->s(p.head<2>()) * std::min(1., (p.z() - m_z0) / m_ramp);
}

double OffsetLayerField::z_at(const Vec2d &xy, double level) const
{
    if (level <= m_z0)
        return level;
    const double s = this->s(xy);
    if (level >= m_z0 + m_ramp - s)
        return level + s;

    return (level - s * m_z0 / m_ramp) / (1. - s / m_ramp);
}

Vec3d OffsetLayerField::gradient(const Vec3d &p) const
{
    if (p.z() <= m_z0)
        return Vec3d::UnitZ();
    const Vec2d  xy = p.head<2>();
    const double e  = 0.5 * m_S.pitch();
    const Vec2d  g((this->s(xy + Vec2d(e, 0.)) - this->s(xy - Vec2d(e, 0.))) / (2. * e),
                   (this->s(xy + Vec2d(0., e)) - this->s(xy - Vec2d(0., e))) / (2. * e));
    if (p.z() >= m_z0 + m_ramp)
        return Vec3d(-g.x(), -g.y(), 1.);
    const double t = (p.z() - m_z0) / m_ramp;
    return Vec3d(-g.x() * t, -g.y() * t, 1. - this->s(xy) / m_ramp);
}

double OffsetLayerField::max_slope_near(const Vec2d &xy, double level) const
{
    if (level <= m_z0)
        return 0.;
    const int  cx = int(std::floor((xy.x() - m_S.origin().x()) / m_S.pitch()));
    const int  cy = int(std::floor((xy.y() - m_S.origin().y()) / m_S.pitch()));
    const auto at = [this](int ix, int iy) { return double(m_S.at(std::clamp(ix, 0, m_S.nx() - 1), std::clamp(iy, 0, m_S.ny() - 1))); };



    double g = 0., s_max = -Inf;
    for (int iy = cy - 1; iy <= cy + 1; ++iy)
        for (int ix = cx - 1; ix <= cx + 1; ++ix) {
            const double x0 = at(ix + 1, iy) - at(ix, iy), x1 = at(ix + 1, iy + 1) - at(ix, iy + 1);
            const double y0 = at(ix, iy + 1) - at(ix, iy), y1 = at(ix + 1, iy + 1) - at(ix + 1, iy);
            for (const double gx : { x0, x1 })
                for (const double gy : { y0, y1 })
                    g = std::max(g, std::hypot(gx, gy));
            s_max = std::max({ s_max, at(ix, iy), at(ix + 1, iy), at(ix, iy + 1), at(ix + 1, iy + 1) });
        }
    return g / m_S.pitch() / std::max(1e-3, 1. - std::max(0., s_max) / m_ramp);
}

std::unique_ptr<LayerField> build_layer_source(const PrintObject &object, std::string *why)
{
    const SourceParams          P    = source_params(object);
    const std::string          &name = object.model_object()->name;
    const indexed_triangle_set  mesh = slicing_frame_mesh(object);
    switch (object.config().nonplanar_deform.value) {
    case NonplanarDeform::QuickCurve: return build_quickcurve(object, mesh, P, why);
    case NonplanarDeform::Conical: return build_conical(mesh, P, name, why);
    case NonplanarDeform::Optimized: return build_optimized(object, mesh, P, why);
    default: return nullptr;
    }
}

}
