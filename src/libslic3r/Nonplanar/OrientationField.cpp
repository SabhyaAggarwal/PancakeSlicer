#include "OrientationField.hpp"

#include "ConformalShells.hpp"
#include "Multiplanar.hpp"
#include "PatchTools.hpp"

#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

#include <algorithm>
#include <cstdlib>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

constexpr double UprightSnap = 2. * M_PI / 180.;

Vec3d clamp_tilt(const Vec3d &d, double max_tilt)
{
    if (max_tilt <= 0.)
        return Vec3d::UnitZ();
    const double tilt = std::acos(std::clamp(d.z(), -1., 1.));
    if (tilt <= max_tilt)
        return d;
    const Vec2d h = d.head<2>();
    if (h.squaredNorm() < 1e-24)
        return Vec3d::UnitZ();
    const Vec2d u = h.normalized();
    return Vec3d(std::sin(max_tilt) * u.x(), std::sin(max_tilt) * u.y(), std::cos(max_tilt));
}

Vec3d lift_overhang(const Vec3d &g, const Vec3d &n, double sin_threshold, const Vec3d &outward)
{
    if (n.dot(g) >= -sin_threshold)
        return g;
    Vec3d tangential = g - n.dot(g) * n;
    if (tangential.squaredNorm() < 1e-12)
        tangential = outward - n.dot(outward) * n;
    if (tangential.squaredNorm() < 1e-12)
        tangential = Vec3d::UnitX() - n.x() * n;
    return (-sin_threshold * n + std::sqrt(1. - sin_threshold * sin_threshold) * tangential.normalized()).normalized();
}

Matrix3d head_frame_for(const ToolKinematics *kinematics, const Vec3d &tip, const Vec3d &axis)
{
    if (kinematics != nullptr && kinematics->rotary_count() > 0) {
        JointPose joints;
        if (kinematics->inverse(ToolPose{ tip, axis }, nullptr, joints))
            return kinematics->head_frame(joints);
    }
    const Matrix3d r = Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), axis).toRotationMatrix();
    return r.transpose();
}

struct FacetSample
{
    Vec3d  p;
    Vec3d  n;
    double area;
    size_t facet;
};

}

void OrientationField::locate(const Vec3d &p, int &i, int &j, int &k, Vec3d &t) const
{
    const Vec3d g = (p - m_origin) / m_pitch;
    auto        axis = [](double x, int n, int &c, double &f) {
        c = std::clamp(int(std::floor(x)), 0, n - 2);
        f = std::clamp(x - double(c), 0., 1.);
    };
    axis(g.x(), m_nx, i, t.x());
    axis(g.y(), m_ny, j, t.y());
    axis(g.z(), m_nz, k, t.z());
}

template<class T> T OrientationField::trilinear(const std::vector<T> &f, const Vec3d &p) const
{
    int   i, j, k;
    Vec3d t;
    this->locate(p, i, j, k, t);
    const size_t n000 = node(i, j, k), dx = 1, dy = size_t(m_nx), dz = size_t(m_nx) * size_t(m_ny);
    const double x = t.x(), y = t.y(), z = t.z();
    auto lerp = [](const T &a, const T &b, double s) -> T { return a + (b - a) * s; };
    const T c00 = lerp(f[n000], f[n000 + dx], x), c10 = lerp(f[n000 + dy], f[n000 + dy + dx], x);
    const T c01 = lerp(f[n000 + dz], f[n000 + dz + dx], x), c11 = lerp(f[n000 + dz + dy], f[n000 + dz + dy + dx], x);
    return lerp(lerp(c00, c10, y), lerp(c01, c11, y), z);
}

double OrientationField::phi(const Vec3d &p) const { return this->trilinear(m_phi, p); }

OrientationTerm OrientationField::orientation_term() const
{
    OrientationTerm term;
    term.direction = [this](const Vec3d &p) {
        const Vec3d d = this->trilinear(m_dir, p);
        return d.squaredNorm() > 1e-24 ? Vec3d(d.normalized()) : Vec3d(Vec3d::UnitZ());
    };
    term.weight = [this](const Vec3d &p) { return this->inside(p) ? 1. : 0.; };
    return term;
}

Vec3d OrientationField::gradient(const Vec3d &p) const { return this->trilinear(m_grad_phi, p).cast<double>(); }

Vec3d OrientationField::orientation(const Vec3d &p) const
{
    const Vec3d g = this->gradient(p);
    if (g.squaredNorm() < 1e-24)
        return Vec3d::UnitZ();
    const Vec3d a = clamp_tilt(g.normalized(), m_tilt_budget);
    return std::acos(std::clamp(a.z(), -1., 1.)) < UprightSnap ? Vec3d(Vec3d::UnitZ()) : a;
}

Vec3d OrientationField::forward(const Vec3d &p) const
{
    return Vec3d(this->trilinear(m_u, p), this->trilinear(m_v, p), this->trilinear(m_phi, p));
}

Matrix3d OrientationField::jacobian(const Vec3d &p) const
{
    Matrix3d j;
    j.row(0) = this->trilinear(m_grad_u, p).cast<double>().transpose();
    j.row(1) = this->trilinear(m_grad_v, p).cast<double>().transpose();
    j.row(2) = this->trilinear(m_grad_phi, p).cast<double>().transpose();
    return j;
}

bool OrientationField::inside(const Vec3d &p) const
{
    int   i, j, k;
    Vec3d t;
    this->locate(p, i, j, k, t);
    double v = 0.;
    for (int c = 0; c < 8; ++c) {
        const int    a = c & 1, b = (c >> 1) & 1, d = (c >> 2) & 1;
        const double w = (a ? t.x() : 1. - t.x()) * (b ? t.y() : 1. - t.y()) * (d ? t.z() : 1. - t.z());
        v += w * double(m_inside[node(i + a, j + b, k + d)] != 0);
    }
    return v >= 0.5;
}

void OrientationField::compute_gradients()
{
    const size_t n = m_phi.size();
    m_grad_phi.assign(n, Vec3f::Zero());
    m_grad_u.assign(n, Vec3f::Zero());
    m_grad_v.assign(n, Vec3f::Zero());
    tbb::parallel_for(tbb::blocked_range<int>(0, m_nz), [&](const tbb::blocked_range<int> &range) {
        for (int k = range.begin(); k < range.end(); ++k)
            for (int j = 0; j < m_ny; ++j)
                for (int i = 0; i < m_nx; ++i) {
                    const size_t id = node(i, j, k);
                    auto diff = [&](const std::vector<double> &f) {
                        Vec3f g;
                        const int lo[3] = { std::max(i - 1, 0), std::max(j - 1, 0), std::max(k - 1, 0) };
                        const int hi[3] = { std::min(i + 1, m_nx - 1), std::min(j + 1, m_ny - 1), std::min(k + 1, m_nz - 1) };
                        g.x() = float((f[node(hi[0], j, k)] - f[node(lo[0], j, k)]) / (double(hi[0] - lo[0]) * m_pitch));
                        g.y() = float((f[node(i, hi[1], k)] - f[node(i, lo[1], k)]) / (double(hi[1] - lo[1]) * m_pitch));
                        g.z() = float((f[node(i, j, hi[2])] - f[node(i, j, lo[2])]) / (double(hi[2] - lo[2]) * m_pitch));
                        return g;
                    };
                    m_grad_phi[id] = diff(m_phi);
                    m_grad_u[id]   = diff(m_u);
                    m_grad_v[id]   = diff(m_v);
                }
    });
}

void OrientationField::build_inverse()
{
    std::vector<uint8_t> near(m_inside.size(), 0);
    for (int k = 0; k < m_nz; ++k)
        for (int j = 0; j < m_ny; ++j)
            for (int i = 0; i < m_nx; ++i)
                if (m_inside[node(i, j, k)])
                    for (int c = -2; c <= 2; ++c)
                        for (int b = -2; b <= 2; ++b)
                            for (int a = -2; a <= 2; ++a) {
                                const int x = i + a, y = j + b, z = k + c;
                                if (x >= 0 && y >= 0 && z >= 0 && x < m_nx && y < m_ny && z < m_nz)
                                    near[node(x, y, z)] = 1;
                            }
    Vec3d lo = Vec3d::Constant(std::numeric_limits<double>::max()), hi = -lo;
    std::vector<uint32_t> cells;
    for (int k = 0; k + 1 < m_nz; ++k)
        for (int j = 0; j + 1 < m_ny; ++j)
            for (int i = 0; i + 1 < m_nx; ++i) {
                bool any = false;
                for (int c = 0; c < 8 && !any; ++c)
                    any = near[node(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))] != 0;
                if (!any)
                    continue;
                cells.push_back(uint32_t(node(i, j, k)));
                for (int c = 0; c < 8; ++c) {
                    const size_t id = node(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1));
                    const Vec3d  q(m_u[id], m_v[id], m_phi[id]);
                    lo = lo.cwiseMin(q);
                    hi = hi.cwiseMax(q);
                }
            }
    m_inv_bins.clear();
    if (cells.empty())
        return;
    m_inv_pitch  = m_pitch;
    m_inv_origin = lo - Vec3d::Constant(m_inv_pitch);
    m_inv_nx     = int(std::ceil((hi.x() - m_inv_origin.x()) / m_inv_pitch)) + 2;
    m_inv_ny     = int(std::ceil((hi.y() - m_inv_origin.y()) / m_inv_pitch)) + 2;
    m_inv_nz     = int(std::ceil((hi.z() - m_inv_origin.z()) / m_inv_pitch)) + 2;
    m_inv_bins.assign(size_t(m_inv_nx) * size_t(m_inv_ny) * size_t(m_inv_nz), {});
    const size_t sx = size_t(m_nx), sxy = size_t(m_nx) * size_t(m_ny);
    for (uint32_t cell : cells) {
        Vec3d clo = Vec3d::Constant(std::numeric_limits<double>::max()), chi = -clo;
        for (int c = 0; c < 8; ++c) {
            const size_t id = cell + size_t(c & 1) + size_t((c >> 1) & 1) * sx + size_t((c >> 2) & 1) * sxy;
            const Vec3d  q(m_u[id], m_v[id], m_phi[id]);
            clo = clo.cwiseMin(q);
            chi = chi.cwiseMax(q);
        }
        const Vec3i32 a = ((clo - m_inv_origin) / m_inv_pitch).array().floor().cast<int32_t>();
        const Vec3i32 b = ((chi - m_inv_origin) / m_inv_pitch).array().floor().cast<int32_t>();
        for (int z = std::max(a.z(), 0); z <= std::min(b.z(), m_inv_nz - 1); ++z)
            for (int y = std::max(a.y(), 0); y <= std::min(b.y(), m_inv_ny - 1); ++y)
                for (int x = std::max(a.x(), 0); x <= std::min(b.x(), m_inv_nx - 1); ++x)
                    m_inv_bins[(size_t(z) * size_t(m_inv_ny) + size_t(y)) * size_t(m_inv_nx) + size_t(x)].push_back(cell);
    }
}

bool OrientationField::inverse(const Vec3d &q, Vec3d &p) const
{
    if (m_inv_bins.empty())
        return false;
    const Vec3i32 b = ((q - m_inv_origin) / m_inv_pitch).array().floor().cast<int32_t>();
    if (b.x() < 0 || b.y() < 0 || b.z() < 0 || b.x() >= m_inv_nx || b.y() >= m_inv_ny || b.z() >= m_inv_nz)
        return false;
    const std::vector<uint32_t> &bin = m_inv_bins[(size_t(b.z()) * size_t(m_inv_ny) + size_t(b.y())) * size_t(m_inv_nx) + size_t(b.x())];
    const size_t sx = size_t(m_nx), sxy = size_t(m_nx) * size_t(m_ny);
    bool         found = false;
    for (uint32_t cell : bin) {
        std::array<Vec3d, 8> c;
        for (int n = 0; n < 8; ++n) {
            const size_t id = cell + size_t(n & 1) + size_t((n >> 1) & 1) * sx + size_t((n >> 2) & 1) * sxy;
            c[n] = Vec3d(m_u[id], m_v[id], m_phi[id]);
        }
        Vec3d t(0.5, 0.5, 0.5);
        Vec3d f;
        for (int it = 0; it < 16; ++it) {
            const double x = t.x(), y = t.y(), z = t.z();
            f = Vec3d::Zero();
            Matrix3d j = Matrix3d::Zero();
            for (int n = 0; n < 8; ++n) {
                const double a = (n & 1) ? x : 1. - x, sa = (n & 1) ? 1. : -1.;
                const double bb = ((n >> 1) & 1) ? y : 1. - y, sb = ((n >> 1) & 1) ? 1. : -1.;
                const double cc = ((n >> 2) & 1) ? z : 1. - z, sc = ((n >> 2) & 1) ? 1. : -1.;
                f += a * bb * cc * c[n];
                j.col(0) += sa * bb * cc * c[n];
                j.col(1) += a * sb * cc * c[n];
                j.col(2) += a * bb * sc * c[n];
            }
            const double det = j.determinant();
            if (std::abs(det) < 1e-18)
                break;
            const Vec3d dt = j.inverse() * (q - f);
            t = (t + dt).cwiseMax(Vec3d::Constant(-0.5)).cwiseMin(Vec3d::Constant(1.5));
            if (dt.squaredNorm() < 1e-20)
                break;
        }
        constexpr double tol = 1e-4;
        if ((t.array() < -tol).any() || (t.array() > 1. + tol).any())
            continue;
        const size_t k  = cell / sxy, j = (cell % sxy) / sx, i = cell % sx;
        const Vec3d  pc = node_pos(int(i), int(j), int(k)) + m_pitch * t.cwiseMax(Vec3d::Zero()).cwiseMin(Vec3d::Ones());
        if ((this->forward(pc) - q).norm() > 1e-3)
            continue;
        p = pc;
        if (this->inside(pc))
            return true;
        found = true;
    }
    return found;
}

indexed_triangle_set OrientationField::deform(const indexed_triangle_set &mesh, double max_edge) const
{
    indexed_triangle_set its = mesh;
    const float          limit2 = float(max_edge * max_edge);
    for (int pass = 0; pass < 12; ++pass) {
        std::unordered_map<uint64_t, int> mid;
        auto key = [](int a, int b) { return (uint64_t(std::min(a, b)) << 32) | uint64_t(std::max(a, b)); };
        auto midpoint = [&](int a, int b) {
            const auto it = mid.find(key(a, b));
            if (it != mid.end())
                return it->second;
            const int id = int(its.vertices.size());
            its.vertices.push_back(0.5f * (its.vertices[a] + its.vertices[b]));
            mid.emplace(key(a, b), id);
            return id;
        };
        std::vector<stl_triangle_vertex_indices> out;
        out.reserve(its.indices.size() * 2);
        bool split = false;
        for (const stl_triangle_vertex_indices &f : its.indices) {
            std::array<int, 3> m { -1, -1, -1 };
            int                count = 0;
            for (int e = 0; e < 3; ++e)
                if ((its.vertices[f[e]] - its.vertices[f[(e + 1) % 3]]).squaredNorm() > limit2) {
                    m[e] = midpoint(f[e], f[(e + 1) % 3]);
                    ++count;
                }
            if (count == 0) {
                out.push_back(f);
                continue;
            }
            split = true;
            if (count == 3) {
                out.emplace_back(f[0], m[0], m[2]);
                out.emplace_back(m[0], f[1], m[1]);
                out.emplace_back(m[2], m[1], f[2]);
                out.emplace_back(m[0], m[1], m[2]);
            } else if (count == 1) {
                const int e = m[0] >= 0 ? 0 : m[1] >= 0 ? 1 : 2;
                out.emplace_back(f[e], m[e], f[(e + 2) % 3]);
                out.emplace_back(m[e], f[(e + 1) % 3], f[(e + 2) % 3]);
            } else {
                const int k  = m[0] < 0 ? 0 : m[1] < 0 ? 1 : 2;
                const int a  = f[k], b = f[(k + 1) % 3], c = f[(k + 2) % 3];
                const int mb = m[(k + 1) % 3];
                const int mc = m[(k + 2) % 3];
                out.emplace_back(mb, c, mc);
                out.emplace_back(a, b, mb);
                out.emplace_back(a, mb, mc);
            }
        }
        its.indices = std::move(out);
        if (!split)
            break;
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, its.vertices.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i)
            its.vertices[i] = this->forward(its.vertices[i].cast<double>()).cast<float>();
    });
    return its;
}

struct OrientationFieldBuilder
{
    OrientationField                &F;
    const indexed_triangle_set      &mesh;
    const OrientationFieldParams    &params;
    const std::function<void()>     &cancel;

    std::vector<FacetSample> samples;
    std::vector<Vec3d>       facet_normal;
    std::vector<double>      facet_area;
    std::vector<uint8_t>     facet_bed;
    std::vector<Vec3d>       facet_lift;
    std::vector<uint8_t>     fixed;
    std::vector<double>      tilt_cap;
    std::vector<double>      scale;
    std::vector<double>      table_drop;
    std::vector<Vec3d>       d;
    std::vector<Vec3d>       corr_target;
    std::vector<double>      corr_weight;
    Vec3d                    center { Vec3d::Zero() };
    double                   sin_threshold = 0.;
    AABBTreeIndirect::Tree3f tree;

    OrientationFieldBuilder(OrientationField &f, const indexed_triangle_set &m, const OrientationFieldParams &p,
                            const std::function<void()> &c)
        : F(f), mesh(m), params(p), cancel(c)
    {}

    void check_cancel() const
    {
        if (cancel)
            cancel();
    }

    size_t nodes() const { return F.m_phi.size(); }

    void setup_grid()
    {
        BoundingBoxf3 box;
        for (const stl_vertex &v : mesh.vertices)
            box.merge(v.cast<double>());
        const Vec3d  size   = box.size();
        const double volume = std::max(1., (size.x() + 6.) * (size.y() + 6.) * (size.z() + 3.));
        double       pitch  = params.pitch > 0. ? params.pitch : std::clamp(std::cbrt(volume / 160000.), 0.6, 2.5);
        F.m_pitch           = pitch;
        const double margin = 3. * pitch;
        F.m_origin          = Vec3d(box.min.x() - margin, box.min.y() - margin, 0.);
        F.m_nx              = int(std::ceil((size.x() + 2. * margin) / pitch)) + 1;
        F.m_ny              = int(std::ceil((size.y() + 2. * margin) / pitch)) + 1;
        F.m_nz              = int(std::ceil((box.max.z() + margin) / pitch)) + 1;
        const size_t n      = size_t(F.m_nx) * size_t(F.m_ny) * size_t(F.m_nz);
        F.m_phi.assign(n, 0.);
        F.m_u.assign(n, 0.);
        F.m_v.assign(n, 0.);
        F.m_inside.assign(n, 0);
        fixed.assign(n, 0);
        scale.assign(n, 1.);
        tilt_cap.assign(n, params.max_tilt);
        d.assign(n, Vec3d::UnitZ());
        center = box.center();
        F.m_bed_band = params.first_layer_height;
        F.m_tilt_budget = params.max_tilt;
        const double zone = params.first_layer_height + pitch + 1e-6;
        for (int k = 0; k < F.m_nz; ++k)
            for (int j = 0; j < F.m_ny; ++j)
                for (int i = 0; i < F.m_nx; ++i) {
                    const size_t id = F.node(i, j, k);
                    const Vec3d  p  = F.node_pos(i, j, k);
                    F.m_phi[id] = p.z();
                    F.m_u[id]   = p.x();
                    F.m_v[id]   = p.y();
                    fixed[id]   = p.z() <= zone;
                }
    }

    void voxelize()
    {
        std::vector<float> zs(size_t(F.m_nz));
        BoundingBoxf3      box;
        for (const stl_vertex &v : mesh.vertices)
            box.merge(v.cast<double>());
        for (int k = 0; k < F.m_nz; ++k)
            zs[size_t(k)] = float(std::clamp(F.m_origin.z() + k * F.m_pitch, box.min.z() + 1e-3, box.max.z() - 1e-3));
        const std::vector<ExPolygons> layers = slice_mesh_ex(mesh, zs);
        tbb::parallel_for(tbb::blocked_range<int>(0, F.m_nz), [&](const tbb::blocked_range<int> &range) {
            for (int k = range.begin(); k < range.end(); ++k) {
                const double z = F.m_origin.z() + k * F.m_pitch;
                if (z > box.max.z() + 1e-6)
                    continue;
                std::vector<std::pair<Vec2d, Vec2d>> edges;
                for (const ExPolygon &ex : layers[size_t(k)]) {
                    auto add = [&edges](const Polygon &poly) {
                        for (size_t a = 0; a < poly.points.size(); ++a)
                            edges.emplace_back(unscale(poly.points[a]), unscale(poly.points[(a + 1) % poly.points.size()]));
                    };
                    add(ex.contour);
                    for (const Polygon &hole : ex.holes)
                        add(hole);
                }
                std::vector<double> xs;
                for (int j = 0; j < F.m_ny; ++j) {
                    const double y = F.m_origin.y() + j * F.m_pitch;
                    xs.clear();
                    for (const auto &[a, b] : edges)
                        if ((a.y() <= y) != (b.y() <= y))
                            xs.push_back(a.x() + (y - a.y()) / (b.y() - a.y()) * (b.x() - a.x()));
                    std::sort(xs.begin(), xs.end());
                    for (size_t s = 0; s + 1 < xs.size(); s += 2) {
                        const int i0 = int(std::ceil((xs[s] - F.m_origin.x()) / F.m_pitch));
                        const int i1 = int(std::floor((xs[s + 1] - F.m_origin.x()) / F.m_pitch));
                        for (int i = std::max(i0, 0); i <= std::min(i1, F.m_nx - 1); ++i)
                            F.m_inside[F.node(i, j, k)] = 1;
                    }
                }
            }
        });
    }

    void make_samples()
    {
        const size_t nf = mesh.indices.size();
        facet_normal.assign(nf, Vec3d::UnitZ());
        facet_area.assign(nf, 0.);
        facet_bed.assign(nf, 0);
        const double step = 0.75 * F.m_pitch;
        for (size_t f = 0; f < nf; ++f) {
            const Vec3d a = mesh.vertices[mesh.indices[f][0]].cast<double>();
            const Vec3d b = mesh.vertices[mesh.indices[f][1]].cast<double>();
            const Vec3d c = mesh.vertices[mesh.indices[f][2]].cast<double>();
            const Vec3d cr = (b - a).cross(c - a);
            const double area2 = cr.norm();
            if (area2 < 1e-12)
                continue;
            const Vec3d n = cr / area2;
            facet_normal[f] = n;
            facet_area[f]   = 0.5 * area2;
            facet_bed[f]    = n.z() < -0.95 && std::max({ a.z(), b.z(), c.z() }) < 0.5 * params.first_layer_height + 0.05;
            const double longest = std::max({ (b - a).norm(), (c - b).norm(), (a - c).norm() });
            const int    m       = std::max(1, int(std::ceil(longest / step)));
            const double w       = facet_area[f] / double(m * m);
            for (int i = 0; i < m; ++i)
                for (int j = 0; j < m - i; ++j) {
                    const double u0 = double(i), v0 = double(j);
                    {
                        const double u = (u0 + 1. / 3.) / m, v = (v0 + 1. / 3.) / m;
                        samples.push_back({ a + u * (b - a) + v * (c - a), n, w, f });
                    }
                    if (i + j + 1 < m) {
                        const double u = (u0 + 2. / 3.) / m, v = (v0 + 2. / 3.) / m;
                        samples.push_back({ a + u * (b - a) + v * (c - a), n, w, f });
                    }
                }
        }
        cluster_lifts();
        corr_target.assign(samples.size(), Vec3d::UnitZ());
        corr_weight.assign(samples.size(), 0.);
        tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(mesh.vertices, mesh.indices);
    }

    void cluster_lifts()
    {
        const size_t nf = mesh.indices.size();
        facet_lift.assign(nf, Vec3d::Zero());
        std::vector<uint8_t> over(nf, 0);
        for (size_t f = 0; f < nf; ++f)
            over[f] = facet_area[f] > 0. && !facet_bed[f] && facet_normal[f].z() < -sin_threshold;
        const std::vector<Vec3i32> nb = its_face_neighbors(mesh);
        std::vector<int>           cluster(nf, -1);
        const double               sin_target = std::sin(0.5 * M_PI - params.overhang_threshold - Geometry::deg2rad(5.));
        int                        next = 0;
        for (size_t seed = 0; seed < nf; ++seed) {
            if (!over[seed] || cluster[seed] >= 0)
                continue;
            std::vector<size_t> members { seed }, stack { seed };
            cluster[seed] = next;
            while (!stack.empty()) {
                const size_t f = stack.back();
                stack.pop_back();
                for (int e = 0; e < 3; ++e) {
                    const int g = nb[f][e];
                    if (g >= 0 && over[size_t(g)] && cluster[size_t(g)] < 0) {
                        cluster[size_t(g)] = next;
                        members.push_back(size_t(g));
                        stack.push_back(size_t(g));
                    }
                }
            }
            ++next;
            Vec2d  mean = Vec2d::Zero(), centroid = Vec2d::Zero();
            double area = 0., spread = 0.;
            for (size_t f : members) {
                const Vec3d c = (mesh.vertices[mesh.indices[f][0]] + mesh.vertices[mesh.indices[f][1]] + mesh.vertices[mesh.indices[f][2]]).cast<double>() / 3.;
                mean += facet_area[f] * facet_normal[f].head<2>();
                spread += facet_area[f] * facet_normal[f].head<2>().norm();
                centroid += facet_area[f] * c.head<2>();
                area += facet_area[f];
            }
            centroid /= area;
            Vec2d u = mean.norm() > 0.3 * spread && mean.norm() > 1e-9 ? Vec2d(mean.normalized()) : Vec2d(centroid - center.head<2>());
            u = u.norm() > 1e-6 ? Vec2d(u.normalized()) : Vec2d(Vec2d::UnitX());
            for (size_t f : members) {
                const Vec3d &n   = facet_normal[f];
                const double s    = std::clamp((n.head<2>().norm() - 0.2) / 0.3, 0., 1.);
                const double w    = s * s * (3. - 2. * s);
                Vec2d        dir  = (1. - w) * u + (n.head<2>().norm() > 1e-9 ? Vec2d(w * n.head<2>().normalized()) : Vec2d::Zero());
                dir               = dir.norm() > 1e-6 ? Vec2d(dir.normalized()) : u;
                double       tau = params.max_tilt;
                for (int deg = 0; deg <= 90; ++deg) {
                    const double t = Geometry::deg2rad(double(deg));
                    if (std::sin(t) * n.head<2>().dot(dir) + std::cos(t) * n.z() >= -sin_target) {
                        tau = t;
                        break;
                    }
                }
                tau          = std::min(tau, params.max_tilt);
                facet_lift[f] = Vec3d(std::sin(tau) * dir.x(), std::sin(tau) * dir.y(), std::cos(tau));
            }
        }
    }

    void table_limits()
    {
        table_drop.assign(91, std::numeric_limits<double>::infinity());
        for (int deg = 0; deg <= 90; ++deg) {
            const double tau  = Geometry::deg2rad(double(deg));
            double       drop = -std::numeric_limits<double>::infinity();
            bool         ok   = true;
            for (int s = 0; s < 24 && ok; ++s) {
                const double psi = 2. * M_PI * s / 24.;
                const Vec3d  axis(std::sin(tau) * std::cos(psi), std::sin(tau) * std::sin(psi), std::cos(tau));
                if (params.kinematics != nullptr && params.kinematics->rotary_count() > 0) {
                    JointPose joints;
                    if (!params.kinematics->inverse(ToolPose{ Vec3d(0., 0., 50.), axis }, nullptr, joints)) {
                        ok = false;
                        break;
                    }
                }
                const Matrix3d h = head_frame_for(params.kinematics, Vec3d(0., 0., 50.), axis);
                for (const HeadBox &box : params.head.boxes) {
                    if (box.infinite_x || box.infinite_y)
                        continue;
                    const double grow = box.nozzle ? 0. : params.head.lateral_margin;
                    const double low  = box.nozzle ? 0. : params.head.margin;
                    for (int c = 0; c < 8; ++c) {
                        const Vec3d corner((c & 1) ? box.max.x() + grow : box.min.x() - grow,
                                           ((c >> 1) & 1) ? box.max.y() + grow : box.min.y() - grow,
                                           ((c >> 2) & 1) ? box.max.z() : box.min.z() - low);
                        drop = std::max(drop, -(h.transpose() * corner).z());
                    }
                }
            }
            table_drop[size_t(deg)] = ok ? std::max(drop, 0.) : std::numeric_limits<double>::infinity();
        }
    }

    double table_tilt(double z) const
    {
        double best = 0.;
        for (int deg = 0; deg <= 90; ++deg) {
            if (table_drop[size_t(deg)] > z)
                break;
            best = Geometry::deg2rad(double(deg));
        }
        return best;
    }

    void update_caps()
    {
        for (int k = 0; k < F.m_nz; ++k) {
            const double cap = std::min(params.max_tilt, table_tilt(F.m_origin.z() + k * F.m_pitch));
            for (int j = 0; j < F.m_ny; ++j)
                for (int i = 0; i < F.m_nx; ++i) {
                    const size_t id = F.node(i, j, k);
                    tilt_cap[id]    = fixed[id] ? 0. : cap * scale[id];
                }
        }
    }

    template<class EdgeTarget>
    void solve(std::vector<double> &x, const std::vector<double> &c, const std::vector<double> &ct, EdgeTarget edge,
               double tol = 1e-6, int max_iter = 3000) const
    {
        const int    nx = F.m_nx, ny = F.m_ny, nz = F.m_nz;
        const size_t n = x.size(), sy = size_t(nx), sz = size_t(nx) * size_t(ny);
        std::vector<double> b(n, 0.), r(n, 0.), z(n, 0.), p(n, 0.), ap(n, 0.), diag(n, 1.);
        auto for_nodes = [&](auto fn) {
            tbb::parallel_for(tbb::blocked_range<int>(0, nz), [&](const tbb::blocked_range<int> &range) {
                for (int k = range.begin(); k < range.end(); ++k)
                    for (int j = 0; j < ny; ++j)
                        for (int i = 0; i < nx; ++i)
                            fn(i, j, k, F.node(i, j, k));
            });
        };
        for_nodes([&](int i, int j, int k, size_t id) {
            if (fixed[id]) {
                b[id]    = 0.;
                diag[id] = 1.;
                return;
            }
            double rhs = c[id] * ct[id], deg = 0.;
            auto nb = [&](bool ok, size_t jd, int axis, int sign) {
                if (!ok)
                    return;
                deg += 1.;
                rhs -= edge(id, jd, axis, sign);
                if (fixed[jd])
                    rhs += x[jd];
            };
            nb(i > 0, id - 1, 0, -1);
            nb(i + 1 < nx, id + 1, 0, 1);
            nb(j > 0, id - sy, 1, -1);
            nb(j + 1 < ny, id + sy, 1, 1);
            nb(k > 0, id - sz, 2, -1);
            nb(k + 1 < nz, id + sz, 2, 1);
            b[id]    = rhs;
            diag[id] = deg + c[id];
        });
        auto apply = [&](const std::vector<double> &v, std::vector<double> &out) {
            for_nodes([&](int i, int j, int k, size_t id) {
                if (fixed[id]) {
                    out[id] = 0.;
                    return;
                }
                double s = diag[id] * v[id];
                auto nb = [&](bool ok, size_t jd) {
                    if (ok && !fixed[jd])
                        s -= v[jd];
                };
                nb(i > 0, id - 1);
                nb(i + 1 < nx, id + 1);
                nb(j > 0, id - sy);
                nb(j + 1 < ny, id + sy);
                nb(k > 0, id - sz);
                nb(k + 1 < nz, id + sz);
                out[id] = s;
            });
        };
        auto dot = [&](const std::vector<double> &a, const std::vector<double> &bb) {
            return tbb::parallel_deterministic_reduce(
                tbb::blocked_range<size_t>(0, n, 8192), 0.,
                [&](const tbb::blocked_range<size_t> &range, double s) {
                    for (size_t i = range.begin(); i < range.end(); ++i)
                        s += a[i] * bb[i];
                    return s;
                },
                std::plus<double>());
        };
        auto each = [&](auto fn) {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, n, 8192), [&](const tbb::blocked_range<size_t> &range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                    fn(i);
            });
        };
        std::vector<double> xf = x;
        for (size_t i = 0; i < n; ++i)
            if (fixed[i])
                xf[i] = 0.;
        apply(xf, ap);
        for (size_t i = 0; i < n; ++i) {
            r[i] = fixed[i] ? 0. : b[i] - ap[i];
            z[i] = r[i] / diag[i];
            p[i] = z[i];
        }
        const double bnorm = std::sqrt(std::max(dot(b, b), 1e-30));
        double       rz    = dot(r, z);
        for (int it = 0; it < max_iter; ++it) {
            if (std::sqrt(dot(r, r)) < tol * bnorm)
                break;
            if ((it & 63) == 0)
                check_cancel();
            apply(p, ap);
            const double pap = dot(p, ap);
            if (std::abs(pap) < 1e-300)
                break;
            const double alpha = rz / pap;
            each([&](size_t i) {
                xf[i] += alpha * p[i];
                r[i] -= alpha * ap[i];
                z[i] = r[i] / diag[i];
            });
            const double rz_new = dot(r, z);
            const double beta   = rz_new / rz;
            rz                  = rz_new;
            each([&](size_t i) { p[i] = z[i] + beta * p[i]; });
        }
        for (size_t i = 0; i < n; ++i)
            if (!fixed[i])
                x[i] = xf[i];
    }

    static constexpr double TargetWeight = 24.;
    void splat(const Vec3d &p, const Vec3d &target, double weight, std::vector<Vec3d> &acc, std::vector<double> &wsum) const
    {
        weight *= TargetWeight / (F.m_pitch * F.m_pitch);
        int   i, j, k;
        Vec3d t;
        F.locate(p, i, j, k, t);
        for (int c = 0; c < 8; ++c) {
            const int    a = c & 1, b = (c >> 1) & 1, e = (c >> 2) & 1;
            const double w = (a ? t.x() : 1. - t.x()) * (b ? t.y() : 1. - t.y()) * (e ? t.z() : 1. - t.z());
            const size_t id = F.node(i + a, j + b, k + e);
            acc[id] += w * weight * target;
            wsum[id] += w * weight;
        }
    }

    Vec3d outward(const Vec3d &p) const
    {
        const Vec3d o(p.x() - center.x(), p.y() - center.y(), 0.);
        return o.squaredNorm() > 1e-12 ? Vec3d(o.normalized()) : Vec3d(Vec3d::UnitX());
    }

    Vec3d layer_normal(const Vec3d &p) const
    {
        const double e = 0.5 * F.m_pitch;
        Vec3d        g(F.phi(p + Vec3d(e, 0., 0.)) - F.phi(p - Vec3d(e, 0., 0.)),
                       F.phi(p + Vec3d(0., e, 0.)) - F.phi(p - Vec3d(0., e, 0.)),
                       F.phi(p + Vec3d(0., 0., e)) - F.phi(p - Vec3d(0., 0., e)));
        g /= 2. * e;
        return g;
    }

    void base_targets(std::vector<Vec3d> &acc, std::vector<double> &wsum) const
    {
        const double cos_flat = std::cos(Geometry::deg2rad(3.));
        for (size_t s = 0; s < samples.size(); ++s) {
            const FacetSample &fs = samples[s];
            if (facet_bed[fs.facet] || fs.p.z() <= F.m_bed_band + 1e-3)
                continue;
            const Vec3d &n = fs.n;
            if (n.z() < -sin_threshold) {
                splat(fs.p, facet_lift[fs.facet], 4. * fs.area, acc, wsum);
            } else if (params.conform_tops && n.z() > 0.) {
                if (n.z() >= cos_flat)
                    splat(fs.p, Vec3d::UnitZ(), 0.5 * fs.area, acc, wsum);
                else if (n.z() < std::cos(Geometry::deg2rad(80.))) {
                } else {
                    const double slope = std::acos(std::clamp(n.z(), -1., 1.));
                    const Vec2d  away  = -n.head<2>().normalized();
                    const double tau   = std::min(0.5 * M_PI - slope, params.max_tilt);
                    splat(fs.p, Vec3d(std::sin(tau) * away.x(), std::sin(tau) * away.y(), std::cos(tau)), fs.area, acc, wsum);
                }
            }
        }
    }

    void diffuse_orientation()
    {
        const size_t        n = nodes();
        std::vector<Vec3d>  acc(n, Vec3d::Zero());
        std::vector<double> wsum(n, 0.);
        base_targets(acc, wsum);
        for (size_t s = 0; s < samples.size(); ++s)
            if (corr_weight[s] > 0.)
                splat(samples[s].p, corr_target[s], corr_weight[s], acc, wsum);
        const double reg = 0.02;
        std::vector<double> c(n), ct(n), x(n);
        for (int axis = 0; axis < 3; ++axis) {
            for (size_t i = 0; i < n; ++i) {
                c[i]  = wsum[i] + reg;
                ct[i] = (acc[i][axis] + reg * (axis == 2 ? 1. : 0.)) / c[i];
                x[i]  = fixed[i] ? (axis == 2 ? 1. : 0.) : d[i][axis];
            }
            solve(x, c, ct, [](size_t, size_t, int, int) { return 0.; }, 1e-4, 1500);
            for (size_t i = 0; i < n; ++i)
                d[i][axis] = x[i];
        }
        tbb::parallel_for(tbb::blocked_range<size_t>(0, n), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                Vec3d v = d[i];
                if (v.squaredNorm() < 1e-12 || v.z() <= 0.)
                    v = Vec3d::UnitZ();
                d[i] = clamp_tilt(v.normalized(), tilt_cap[i]);
            }
        });
    }

    void integrate_phi(double tol = 1e-6)
    {
        const size_t        n = nodes();
        std::vector<double> c(n, 0.), ct(n, 0.);
        const double        h = F.m_pitch;
        solve(F.m_phi, c, ct, [&](size_t i, size_t j, int axis, int sign) { return sign * h * 0.5 * (d[i][axis] + d[j][axis]); }, tol, 4000);
    }

    void integrate_uv()
    {
        const size_t        n = nodes();
        std::vector<Vec3d>  ex(n), ey(n);
        for (size_t i = 0; i < n; ++i) {
            const Vec3d &a = d[i];
            const double k = 1. / (1. + a.z());
            ex[i]          = Vec3d(1. - a.x() * a.x() * k, -a.x() * a.y() * k, -a.x());
            ey[i]          = Vec3d(-a.x() * a.y() * k, 1. - a.y() * a.y() * k, -a.y());
        }
        std::vector<double> c(n, 0.), ct(n, 0.);
        const double        h = F.m_pitch;
        solve(F.m_u, c, ct, [&](size_t i, size_t j, int axis, int sign) { return sign * h * 0.5 * (ex[i][axis] + ex[j][axis]); }, 1e-7, 3000);
        solve(F.m_v, c, ct, [&](size_t i, size_t j, int axis, int sign) { return sign * h * 0.5 * (ey[i][axis] + ey[j][axis]); }, 1e-7, 3000);
    }

    struct Eval
    {
        double overhang_area = 0., support_volume = 0., step_area = 0.;
        double shell_step_area = 0.;
    };

    template<class Normal, class Phi> Eval evaluate(Normal g_of, Phi phi_of, bool support) const
    {
        Eval         e;
        const double h = params.layer_height;
        for (const FacetSample &fs : samples) {
            if (facet_bed[fs.facet] || fs.p.z() <= F.m_bed_band + 1e-3)
                continue;
            const Vec3d  g  = g_of(fs.p);
            const double ng = fs.n.dot(g);
            if (ng >= -sin_threshold)
                continue;
            e.overhang_area += fs.area;
            if (!support)
                continue;
            const Vec3d  dir    = -g;
            const Vec3d  origin = fs.p + 0.02 * dir;
            igl::Hit<float> hit;
            double       dist   = dir.z() < -1e-6 ? origin.z() / -dir.z() : 1e3;
            if (AABBTreeIndirect::intersect_ray_first_hit(mesh.vertices, mesh.indices, tree, origin, dir, hit))
                dist = std::min(dist, double(hit.t));
            e.support_volume += fs.area * (-ng) * std::min(dist, 1e3);
        }
        for (size_t f = 0; f < mesh.indices.size(); ++f) {
            const Vec3d &n = facet_normal[f];
            if (facet_area[f] <= 0. || n.z() <= 0.05)
                continue;
            const Vec3d a = mesh.vertices[mesh.indices[f][0]].cast<double>();
            const Vec3d b = mesh.vertices[mesh.indices[f][1]].cast<double>();
            const Vec3d c = mesh.vertices[mesh.indices[f][2]].cast<double>();
            const Vec3d m = (a + b + c) / 3.;
            const Vec3d g = g_of(m);
            const double ng = n.dot(g);
            if (ng <= 0.05)
                continue;
            const std::array<Vec3d, 7> pts { a, b, c, 0.5 * (a + b), 0.5 * (b + c), 0.5 * (c + a), m };
            double lo = std::numeric_limits<double>::max(), hi = -lo, e2 = 0.;
            for (const Vec3d &p : pts) {
                const double v = phi_of(p) - params.first_layer_height;
                lo             = std::min(lo, v);
                hi             = std::max(hi, v);
                const double r = v - h * std::round(v / h);
                e2 += r * r;
            }
            const double phase = hi - lo >= h ? 1. : std::min(1., 12. * e2 / (7. * h * h));
            e.step_area += facet_area[f] * ng * ng * phase;
            e.shell_step_area += facet_area[f] * ng * ng * phase * (n.z() >= std::cos(0.25 * M_PI) ? 0.15 : 1.);
        }
        return e;
    }

    Eval evaluate_field(bool support) const
    {
        return evaluate([this](const Vec3d &p) {
                            const Vec3d g = layer_normal(p);
                            return g.squaredNorm() > 1e-24 ? Vec3d(g.normalized()) : Vec3d(Vec3d::UnitZ());
                        },
                        [this](const Vec3d &p) { return F.phi(p); }, support);
    }

    double correct_overhangs()
    {
        const double sin_target = std::sin(0.5 * M_PI - params.overhang_threshold - Geometry::deg2rad(5.));
        double       area       = 0.;
        for (size_t s = 0; s < samples.size(); ++s) {
            const FacetSample &fs = samples[s];
            if (facet_bed[fs.facet] || fs.p.z() <= F.m_bed_band + 1e-3)
                continue;
            Vec3d g = layer_normal(fs.p);
            if (g.squaredNorm() < 1e-24)
                continue;
            g.normalize();
            if (fs.n.dot(g) >= -sin_threshold)
                continue;
            area += fs.area;
            corr_target[s] = lift_overhang(g, fs.n, sin_target, outward(fs.p));
            corr_weight[s] = corr_weight[s] <= 0. ? 4. * fs.area : std::min(corr_weight[s] * 2., 256. * fs.area);
        }
        return area;
    }

    std::vector<size_t> head_conflicts() const
    {
        std::vector<Vec3d> pts;
        const double       step = std::max(1.5, F.m_pitch);
        for (const HeadBox &box : params.head.boxes) {
            if (box.infinite_x || box.infinite_y)
                continue;
            if (box.nozzle && box.max.z() <= params.head.tip_allowance.height)
                continue;
            const double grow = box.nozzle ? 0. : params.head.lateral_margin;
            const Vec3d  lo(box.min.x() - grow, box.min.y() - grow,
                            box.nozzle ? std::max(box.min.z(), params.head.tip_allowance.height) : box.min.z() - params.head.margin);
            const Vec3d  hi(box.max.x() + grow, box.max.y() + grow, box.max.z());
            const Vec3i32  cnt = ((hi - lo) / step).array().ceil().cast<int32_t>().cwiseMax(1);
            for (int c = 0; c <= cnt.z(); ++c)
                for (int b = 0; b <= cnt.y(); ++b)
                    for (int a = 0; a <= cnt.x(); ++a) {
                        const bool face = a == 0 || b == 0 || c == 0 || a == cnt.x() || b == cnt.y() || c == cnt.z();
                        if (!face)
                            continue;
                        pts.emplace_back(lo + Vec3d(double(a) / cnt.x() * (hi.x() - lo.x()), double(b) / cnt.y() * (hi.y() - lo.y()),
                                                    double(c) / cnt.z() * (hi.z() - lo.z())));
                    }
        }
        std::sort(pts.begin(), pts.end(), [](const Vec3d &a, const Vec3d &b) { return a.z() < b.z(); });
        const double hop = std::max(0.4, params.head.margin);
        std::vector<uint8_t> near(F.m_inside.size(), 0);
        for (int k = 0; k < F.m_nz; ++k)
            for (int j = 0; j < F.m_ny; ++j)
                for (int i = 0; i < F.m_nx; ++i)
                    if (F.m_inside[F.node(i, j, k)])
                        for (int c = -1; c <= 1; ++c)
                            for (int b = -1; b <= 1; ++b)
                                for (int a = -1; a <= 1; ++a) {
                                    const int x = i + a, y = j + b, z = k + c;
                                    if (x >= 0 && y >= 0 && z >= 0 && x < F.m_nx && y < F.m_ny && z < F.m_nz)
                                        near[F.node(x, y, z)] = 1;
                                }
        std::vector<uint8_t> hit(near.size(), 0);
        tbb::parallel_for(tbb::blocked_range<int>(0, F.m_nz), [&](const tbb::blocked_range<int> &range) {
            for (int k = range.begin(); k < range.end(); ++k)
                for (int j = 0; j < F.m_ny; ++j)
                    for (int i = 0; i < F.m_nx; ++i) {
                        const size_t id = F.node(i, j, k);
                        if (!near[id] || fixed[id] || ((i + j + k) & 1))
                            continue;
                        const Vec3d p = F.node_pos(i, j, k);
                        Vec3d       g = layer_normal(p);
                        if (g.squaredNorm() < 1e-24)
                            continue;
                        g.normalize();
                        const Vec3d    axis  = std::acos(std::clamp(g.z(), -1., 1.)) < UprightSnap ? Vec3d(Vec3d::UnitZ()) : g;
                        const Matrix3d h     = head_frame_for(params.kinematics, p, axis).transpose();
                        const double   level = F.phi(p) + hop;
                        for (const Vec3d &s : pts) {
                            const Vec3d q = p + h * s;
                            if (q.z() < 0.) {
                                hit[id] = 1;
                                break;
                            }
                            if (F.inside(q) && F.phi(q) < level) {
                                hit[id] = 2;
                                break;
                            }
                        }
                    }
        });
        std::vector<size_t> out;
        size_t table = 0;
        double zlo = std::numeric_limits<double>::max(), zhi = -zlo;
        for (size_t i = 0; i < hit.size(); ++i)
            if (hit[i]) {
                out.push_back(i);
                table += hit[i] == 1;
                const double z = F.m_origin.z() + double(i / (size_t(F.m_nx) * size_t(F.m_ny))) * F.m_pitch;
                zlo = std::min(zlo, z);
                zhi = std::max(zhi, z);
            }
        if (!out.empty())
            F.notes.emplace_back("head check: " + std::to_string(table) + " table, " + std::to_string(out.size() - table) +
                                 " earlier material, z " + std::to_string(zlo) + ".." + std::to_string(zhi));
        return out;
    }

    void relax(const std::vector<size_t> &conflicts, int round)
    {
        if (round >= 3) {
            for (double &s : scale)
                s *= 0.5;
            F.m_stats.tilt_scale *= 0.5;
            if (round >= 6)
                std::fill(scale.begin(), scale.end(), 0.);
            return;
        }
        const int            r = std::max(1, int(std::ceil(6. / F.m_pitch)));
        std::vector<uint8_t> mark(scale.size(), 0);
        const size_t         sx = size_t(F.m_nx), sxy = size_t(F.m_nx) * size_t(F.m_ny);
        for (size_t id : conflicts) {
            const int k = int(id / sxy), j = int((id % sxy) / sx), i = int(id % sx);
            for (int c = -r; c <= r; ++c)
                for (int b = -r; b <= r; ++b)
                    for (int a = -r; a <= r; ++a) {
                        if (a * a + b * b + c * c > r * r)
                            continue;
                        const int x = i + a, y = j + b, z = k + c;
                        if (x >= 0 && y >= 0 && z >= 0 && x < F.m_nx && y < F.m_ny && z < F.m_nz)
                            mark[F.node(x, y, z)] = 1;
                    }
        }
        for (size_t i = 0; i < scale.size(); ++i)
            if (mark[i])
                scale[i] *= 0.5;
    }

    bool solve_with_layer_solver()
    {
        LayerFieldParams lp;
        lp.layer_height      = params.layer_height;
        lp.min_layer_height *= params.layer_height / 0.2;
        lp.max_layer_height *= params.layer_height / 0.2;
        lp.flat_base_height  = params.first_layer_height + F.m_pitch;
        lp.overhang_angle    = 90. - Geometry::rad2deg(params.overhang_threshold);
        lp.head              = &params.head;
        lp.whole_layer_order = false;
        lp.throw_if_canceled = cancel;
        lp.tool_axis         = [this](const Vec3d &p) {
            const Vec3d v = F.trilinear(d, p);
            return v.squaredNorm() > 1e-24 ? Vec3d(v.normalized()) : Vec3d(Vec3d::UnitZ());
        };
        LayerFieldReport      report;
        const StackLayerField field = solve_layer_field(mesh, lp, &report);
        if (field.empty())
            return false;
        tbb::parallel_for(tbb::blocked_range<int>(0, F.m_nz), [&](const tbb::blocked_range<int> &range) {
            for (int k = range.begin(); k < range.end(); ++k)
                for (int j = 0; j < F.m_ny; ++j)
                    for (int i = 0; i < F.m_nx; ++i) {
                        const size_t id = F.node(i, j, k);
                        if (!fixed[id])
                            F.m_phi[id] = field.phi(F.node_pos(i, j, k));
                    }
        });
        BOOST_LOG_TRIVIAL(info) << "Orientation field: layer solver " << report.solve_seconds << " s, conformal top "
                                << report.field.conformal_top << ", axis deviation mean " << report.field.axis_dev_mean << " deg";
        return true;
    }

    void choose_layers()
    {
        F.m_stats.solver_field = false;
        const Eval                own   = evaluate_field(false);
        const std::vector<double> saved = F.m_phi;
        bool ok = false;
        try {
            ok = solve_with_layer_solver();
        } catch (const CanceledException &) {
            throw;
        } catch (const std::exception &ex) {
            F.notes.emplace_back(std::string("layer solver failed: ") + ex.what());
        }
        if (!ok) {
            F.m_phi = saved;
            return;
        }
        const Eval   lfo   = evaluate_field(false);
        const double s_own = own.overhang_area + 0.5 * own.step_area, s_lfo = lfo.overhang_area + 0.5 * lfo.step_area;
        F.notes.emplace_back("layers: own integral " + std::to_string(s_own) + ", layer solver " + std::to_string(s_lfo));
        if (s_lfo <= s_own)
            F.m_stats.solver_field = true;
        else
            F.m_phi = saved;
    }

    void solve_field()
    {
        update_caps();
        double best = std::numeric_limits<double>::max();
        int    stale = 0;
        for (int it = 0; it < 10; ++it) {
            check_cancel();
            diffuse_orientation();
            integrate_phi();
            const double area = correct_overhangs();
            BOOST_LOG_TRIVIAL(debug) << "Orientation field: iteration " << it << " overhang " << area << " mm2";
            if (area < 0.5)
                break;
            if (area < best * 0.98) {
                best  = area;
                stale = 0;
            } else if (++stale >= 2)
                break;
        }
    }

    void run()
    {
        sin_threshold = std::sin(0.5 * M_PI - params.overhang_threshold);
        setup_grid();
        voxelize();
        check_cancel();
        make_samples();
        table_limits();
        const Eval planar = evaluate([](const Vec3d &) { return Vec3d(Vec3d::UnitZ()); }, [](const Vec3d &p) { return p.z(); }, true);
        OrientationFieldStats &st = F.m_stats;
        st.planar_overhang_area  = planar.overhang_area;
        st.planar_support_volume = planar.support_volume;
        st.planar_step_area      = planar.step_area;
        st.shell_step_area       = planar.shell_step_area;
        std::vector<size_t> conflicts;
        for (int round = 0; round < 8; ++round) {
            solve_field();
            choose_layers();
            conflicts = head_conflicts();
            st.relax_rounds = round;
            if (conflicts.empty())
                break;
            F.notes.emplace_back("round " + std::to_string(round) + ": " + std::to_string(conflicts.size()) +
                                 " poses reach earlier material or the table; tilt relaxed there");
            relax(conflicts, round);
        }
        st.head_conflicts = conflicts.size();
        if (!st.solver_field)
            integrate_phi(1e-7);
        F.m_dir = d;
        integrate_uv();
        F.compute_gradients();
        F.build_inverse();
        const Eval field = evaluate_field(true);
        st.overhang_area  = field.overhang_area;
        st.support_volume = field.support_volume;
        st.step_area      = field.step_area;
        st.nodes          = nodes();
        for (size_t i = 0; i < nodes(); ++i) {
            if (!F.m_inside[i])
                continue;
            const Vec3d  g = F.m_grad_phi[i].cast<double>();
            const double l = g.norm();
            if (l < 1e-9)
                continue;
            st.max_tilt      = std::max(st.max_tilt, std::acos(std::clamp(g.z() / l, -1., 1.)));
            st.min_thickness = std::min(st.min_thickness, 1. / l);
            st.max_thickness = std::max(st.max_thickness, 1. / l);
        }
    }
};

std::shared_ptr<OrientationField> OrientationField::build(const indexed_triangle_set &mesh, const OrientationFieldParams &params,
                                                          const std::function<void()> &throw_if_canceled)
{
    auto field = std::make_shared<OrientationField>();
    if (mesh.indices.empty())
        return field;
    OrientationFieldBuilder builder(*field, mesh, params, throw_if_canceled);
    builder.run();
    return field;
}

Vec3d settle_axis(const Vec3d &axis, double &plane, double none, double full)
{
    const double tilt = std::acos(std::clamp(axis.z(), -1., 1.));
    if (tilt >= full) {
        plane = std::atan2(axis.y(), axis.x());
        return axis;
    }
    const Vec2d u(std::cos(plane), std::sin(plane));
    const Vec3d in_plane = Vec3d(u.x() * u.dot(axis.head<2>()), u.y() * u.dot(axis.head<2>()), axis.z()).normalized();
    const double s = std::clamp((tilt - none) / (full - none), 0., 1.);
    const double w = s * s * (3. - 2. * s);
    const Vec3d  out = ((1. - w) * in_plane + w * axis).normalized();
    if (w > 0. && out.head<2>().squaredNorm() > 1e-6)
        plane = std::atan2(out.y(), out.x());
    return out;
}

static double field_tilt_budget(const ToolKinematics &kinematics)
{
    double tilt = 0.5 * M_PI;
    for (int s = 0; s < 72; ++s)
        tilt = std::min(tilt, kinematics.max_tilt(2. * M_PI * s / 72.));
    return tilt;
}

bool field_motion_supported(const PrintConfig &config, std::string *why)
{
    const std::unique_ptr<ToolKinematics> kinematics = make_tool_kinematics(config);
    auto fail = [why](const std::string &reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    if (kinematics == nullptr || kinematics->rotary_count() < 2)
        return fail(_u8L("The orientation field needs a printer with two rotary axes."));
    if (!kinematics->inverse_time_feed() && !kinematics->firmware_rtcp())
        return fail(_u8L("The orientation field needs rotary moves streamed along each road: enable inverse-time feed (G93) or firmware RTCP."));
    const double budget = field_tilt_budget(*kinematics);
    if (budget < Geometry::deg2rad(10.)) {
        double best = 0.;
        for (int s = 0; s < 72; ++s)
            best = std::max(best, kinematics->max_tilt(2. * M_PI * s / 72.));
        return fail(format(_u8L("The orientation field needs at least 10 degrees of tilt in every direction; the rotary axis limits allow %1% degrees in the tightest direction (%2% in the widest)."),
                           int(std::lround(Geometry::rad2deg(budget))), int(std::lround(Geometry::rad2deg(best)))));
    }
    return true;
}

namespace {

bool field_requested(const PrintObject &object)
{
    return object.config().nonplanar_mode.value == NonplanarMode::Field && object.config().nonplanar_atom_toolpath.value.empty();
}

OrientationFieldParams field_params(const PrintObject &object, const ToolKinematics &kinematics)
{
    OrientationFieldParams params;
    params.layer_height       = object.config().layer_height.value;
    params.first_layer_height = object.print()->config().initial_layer_print_height.value;
    params.overhang_threshold = Geometry::deg2rad(object.config().multiplanar_overhang_threshold.value);
    params.max_tilt   = std::max(0., field_tilt_budget(kinematics) - Geometry::deg2rad(5.));
    params.head       = resolve_head_envelope(object.print()->config());
    params.kinematics = &kinematics;
    return params;
}

}

std::shared_ptr<const OrientationField> clip_field_slices(PrintObject &object)
{
    if (!field_requested(object))
        return {};
    if (!field_motion_supported(object.print()->config()))
        return {};
    const std::unique_ptr<ToolKinematics> kinematics = make_tool_kinematics(object.print()->config());
    const OrientationFieldParams params = field_params(object, *kinematics);
    const indexed_triangle_set   mesh   = slicing_frame_mesh(object);
    std::shared_ptr<OrientationField> field = OrientationField::build(mesh, params, [&object]() { throw_if_canceled(object); });
    const OrientationFieldStats &st = field->stats();
    for (const std::string &note : field->notes)
        BOOST_LOG_TRIVIAL(info) << "Orientation field " << object.model_object()->name << ": " << note;
    BOOST_LOG_TRIVIAL(info) << "Orientation field " << object.model_object()->name << ": " << st.nodes << " nodes, overhang "
                            << st.overhang_area << " mm2 (planar " << st.planar_overhang_area << "), support volume "
                            << st.support_volume << " mm3 (planar " << st.planar_support_volume << "), stair-step area "
                            << st.step_area << " mm2 (planar " << st.planar_step_area << "), max tilt "
                            << Geometry::rad2deg(st.max_tilt) << " deg, layer thickness " << st.min_thickness << ".."
                            << st.max_thickness << " h, head conflicts " << st.head_conflicts << ", relax rounds "
                            << st.relax_rounds << ", tilt scale " << st.tilt_scale << ", layers from " << (st.solver_field ? "the layer solver" : "the own integral");
    if (st.head_conflicts > 0)
        throw Slic3r::SlicingError(Slic3r::format(_u8L("Orientation field: %1% poses still reach earlier material or the "
                                                       "table after relaxing the tilt. Use multiplanar chunks or rotary curved shells."),
                                                  st.head_conflicts));
    LayerPtrs &layers = object.layers();
    for (size_t layer_id = 1; layer_id < layers.size(); ++layer_id)
        for (LayerRegion *region : layers[layer_id]->regions())
            region->slices.set(ExPolygons(), stInternal);
    return field;
}

void build_field_patch(PrintObject &object, const std::shared_ptr<const OrientationField> &field, ConformalResult &result)
{
    if (!field || object.layers().empty())
        return;
    const size_t host_id = object.layers().size() - 1;
    Layer       *host    = object.layers()[host_id];
    const double raft    = host->print_z - (host->slice_z + 0.5 * host->height);
    const double first   = object.print()->config().initial_layer_print_height.value;
    DynamicPrintConfig config = chunk_print_config(object);
    config.set_deserialize_strict("initial_layer_print_height", object.print()->config().opt_serialize("initial_layer_print_height"));
    config.set_deserialize_strict("ironing_type", "no ironing");

    const indexed_triangle_set deformed = field->deform(slicing_frame_mesh(object), 0.75 * field->pitch());
    Model        model;
    ModelObject *model_object = model.add_object();
    model_object->name        = object.model_object()->name + " field";
    model_object->add_volume(TriangleMesh(indexed_triangle_set(deformed)));
    model_object->add_instance();
    Print print;
    try {
        print.apply(model, config);
        print.set_status_silent();
        print.process();
    } catch (const std::exception &ex) {
        throw Slic3r::SlicingError(std::string(_u8L("Orientation field: slicing the curved layers failed: ")) + ex.what());
    }
    if (print.objects().empty())
        return;
    const PrintObject &sliced = *print.objects().front();
    const Vec2d        center = unscale(sliced.center_offset());

    ConformalPatch patch;
    patch.id           = int(result.patches.size());
    patch.kind         = PatchKind::Field;
    patch.shell_height = object.config().layer_height.value;
    patch.host_layer   = host_id;
    patch.region_id    = 0;
    patch.field        = field;
    patch.z_min        = std::numeric_limits<double>::infinity();
    patch.z_max        = -std::numeric_limits<double>::infinity();
    const double step = 0.5;
    size_t unmapped = 0;
    for (const Layer *layer : sliced.layers()) {
        if (layer->print_z <= first + EPSILON)
            continue;
        throw_if_canceled(object);
        std::vector<const ExtrusionPath *> walls, fills;
        for (const LayerRegion *region : layer->regions()) {
            collect_paths(&region->perimeters, walls);
            collect_paths(&region->fills, fills);
        }
        std::vector<std::pair<const ExtrusionPath *, bool>> order;
        for (const ExtrusionPath *p : walls)
            order.emplace_back(p, false);
        {
            std::vector<char> used(fills.size(), 0);
            Point             at = walls.empty() ? Point(0, 0) : walls.back()->last_point();
            for (size_t n = 0; n < fills.size(); ++n) {
                size_t best = 0;
                bool   rev  = false;
                double bd   = std::numeric_limits<double>::max();
                for (size_t i = 0; i < fills.size(); ++i) {
                    if (used[i] || fills[i]->polyline.points.size() < 2)
                        continue;
                    const double d0 = (fills[i]->first_point() - at).cast<double>().squaredNorm();
                    const double d1 = (fills[i]->last_point() - at).cast<double>().squaredNorm();
                    if (d0 < bd) { bd = d0; best = i; rev = false; }
                    if (fills[i]->can_reverse() && d1 < bd) { bd = d1; best = i; rev = true; }
                }
                if (bd == std::numeric_limits<double>::max())
                    break;
                used[best] = 1;
                order.emplace_back(fills[best], rev);
                at = rev ? fills[best]->first_point() : fills[best]->last_point();
            }
        }
        ExtrusionEntityCollection shell;
        for (const auto &[path, reversed] : order) {
            const Points3 &src = path->polyline.points;
            if (src.size() < 2)
                continue;
            std::vector<Vec3d> dpts;
            dpts.reserve(src.size());
            for (size_t i = 0; i < src.size(); ++i) {
                const Point3 &pt = reversed ? src[src.size() - 1 - i] : src[i];
                dpts.emplace_back(unscale<double>(pt.x()) + center.x(), unscale<double>(pt.y()) + center.y(), layer->print_z);
            }
            std::vector<Vec3d> q;
            q.push_back(dpts.front());
            for (size_t i = 1; i < dpts.size(); ++i) {
                const double l = (dpts[i] - dpts[i - 1]).norm();
                const int    n = std::max(1, int(std::ceil(l / step)));
                for (int s = 1; s <= n; ++s)
                    q.push_back(dpts[i - 1] + (dpts[i] - dpts[i - 1]) * (double(s) / n));
            }
            auto flush = [&](std::vector<Vec3d> &tips, std::vector<Vec3d> &dq) {
                if (tips.size() >= 2) {
                    Polyline3          polyline;
                    std::vector<float> heights(tips.size()), widths(tips.size());
                    std::vector<Vec3f> axes(tips.size());
                    for (size_t i = 0; i < tips.size(); ++i) {
                        const Matrix3d j     = field->jacobian(tips[i]);
                        const Vec3d    grad  = j.row(2).transpose();
                        const double   gnorm = std::max(grad.norm(), 1e-6);
                        heights[i]           = float(path->height * std::clamp(1. / gnorm, 0.5, 1.6));
                        axes[i]              = field->orientation(tips[i]).cast<float>();
                        const Vec3d t   = (dq[std::min(i + 1, dq.size() - 1)] - dq[i > 0 ? i - 1 : 0]);
                        double      fac = 1.;
                        if (t.head<2>().squaredNorm() > 1e-12 && std::abs(j.determinant()) > 1e-9) {
                            const Matrix3d ji  = j.inverse();
                            const Vec3d    tn  = Vec3d(t.x(), t.y(), 0.).normalized();
                            const Vec3d    lat = ji * Vec3d(-tn.y(), tn.x(), 0.);
                            const Vec3d    T   = (ji * tn).normalized();
                            const Vec3d    N   = grad / gnorm;
                            fac                = (lat - lat.dot(T) * T - lat.dot(N) * N).norm();
                        }
                        widths[i] = float(path->width * std::clamp(fac, 0.6, 1.6));
                        const Vec3d tip(tips[i].x(), tips[i].y(), tips[i].z() + raft);
                        patch.z_min = std::min(patch.z_min, tip.z());
                        patch.z_max = std::max(patch.z_max, tip.z());
                        polyline.points.push_back(Point3::new_scale(tip.x(), tip.y(), tip.z() - host->print_z));
                    }
                    const bool   inner = is_infill(path->role()) && path->role() != erTopSolidInfill;
                    const double rate  = Geometry::deg2rad(inner ? 3. : 5.);
                    const std::vector<Vec3f> exact = axes;
                    for (int pass = 0; pass < 2; ++pass)
                        for (size_t k = 1; k < axes.size(); ++k) {
                            const size_t i = pass == 0 ? k : axes.size() - 1 - k, j = pass == 0 ? i - 1 : i + 1;
                            const Vec3d  a = axes[j].cast<double>(), b = axes[i].cast<double>();
                            const double allowed = rate * std::max((tips[i] - tips[j]).norm(), 1e-3);
                            const double angle   = std::acos(std::clamp(a.dot(b), -1., 1.));
                            if (angle > allowed)
                                axes[i] = (Eigen::Quaterniond::Identity().slerp(allowed / angle, Eigen::Quaterniond::FromTwoVectors(a, b)) * a).cast<float>();
                        }
                    const double max_lag = Geometry::deg2rad(10.);
                    for (size_t i = 0; i < axes.size(); ++i) {
                        const Vec3d  e = exact[i].cast<double>(), a = axes[i].cast<double>();
                        const double off = std::acos(std::clamp(e.dot(a), -1., 1.));
                        if (off > max_lag)
                            axes[i] = (Eigen::Quaterniond::Identity().slerp(1. - max_lag / off, Eigen::Quaterniond::FromTwoVectors(a, e)) * a).cast<float>();
                    }
                    auto *conformal       = new ExtrusionPathConformal(std::move(polyline), *path, std::move(heights), std::move(axes));
                    conformal->bead_width = std::move(widths);
                    shell.entities.push_back(conformal);
                }
                tips.clear();
                dq.clear();
            };
            std::vector<Vec3d> tips, dq;
            for (const Vec3d &d : q) {
                Vec3d p;
                if (field->inverse(d, p)) {
                    tips.push_back(p);
                    dq.push_back(d);
                } else {
                    ++unmapped;
                    flush(tips, dq);
                }
            }
            flush(tips, dq);
        }
        if (!shell.entities.empty())
            patch.shell_paths.push_back(std::move(shell));
    }
    if (unmapped > 0)
        BOOST_LOG_TRIVIAL(warning) << "Orientation field " << object.model_object()->name << ": " << unmapped
                                   << " road points mapped nowhere and were dropped";
    if (patch.shell_paths.empty())
        return;
    patch.shells = int(patch.shell_paths.size());
    host->conformal_patches_after.push_back(patch.id);
    result.patches.push_back(std::move(patch));
}

}
