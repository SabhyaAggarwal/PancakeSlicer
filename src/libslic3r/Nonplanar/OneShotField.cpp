#include "OneShotField.hpp"

#include "Config.hpp"
#include "HeadEnvelope.hpp"
#include "Nonplanar/FieldObjective.hpp"
#include "Nonplanar/NpField.hpp"
#include "Npy.hpp"
#include "Point.hpp"
#include "StudentNet.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <math.h>
#include <ios>
#include <functional>
#include <miniz.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <utility>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr int    Samples = 8192;
const double     Cos70   = std::cos(70. * M_PI / 180.);
constexpr double Inf     = std::numeric_limits<double>::infinity();
constexpr int    StudentPolish = 3;
constexpr size_t MaxNodes = 300000;

std::vector<int> columns(const std::vector<std::array<Vec3d, 3>> &tris, const ObjectiveGeometry &g, int axis)
{
    const int u = axis == 0 ? 1 : 0, w = axis == 2 ? 1 : 2;
    const double cyc = axis == 1 ? -1. : 1.;
    const size_t nd[3] = { g.dims[0], g.dims[1], g.dims[2] };
    std::vector<int> acc((nd[axis] + 1) * nd[u] * nd[w], 0);
    auto at = [&](size_t ia, size_t iu, size_t iw) -> int & { return acc[(iu * nd[w] + iw) * (nd[axis] + 1) + ia]; };
    const Vec2d o(g.origin[u] + 1e-5 * g.spacing[u], g.origin[w] + 1e-5 * g.spacing[w] * M_PI / 3.);
    const Vec2d s(g.spacing[u], g.spacing[w]);
    for (const auto &t : tris) {
        const Vec2d  P0(t[0][u], t[0][w]), P1(t[1][u], t[1][w]), P2(t[2][u], t[2][w]);
        const Vec2d  A = P1 - P0, B = P2 - P0;
        const double det = A.x() * B.y() - A.y() * B.x();
        if (std::abs(det) <= 1e-12)
            continue;
        const double sign = (det > 0. ? 1. : -1.) * cyc;
        const Vec2d  lo = P0.cwiseMin(P1).cwiseMin(P2), hi = P0.cwiseMax(P1).cwiseMax(P2);
        const long   u0 = std::max(0L, long(std::ceil((lo.x() - o.x()) / s.x()))), u1 = std::min(long(nd[u]) - 1, long(std::floor((hi.x() - o.x()) / s.x())));
        const long   w0 = std::max(0L, long(std::ceil((lo.y() - o.y()) / s.y()))), w1 = std::min(long(nd[w]) - 1, long(std::floor((hi.y() - o.y()) / s.y())));
        for (long iu = u0; iu <= u1; ++iu)
            for (long iw = w0; iw <= w1; ++iw) {
                const Vec2d  q  = o + Vec2d(double(iu) * s.x(), double(iw) * s.y()) - P0;
                const double bs = (q.x() * B.y() - q.y() * B.x()) / det, bt = (A.x() * q.y() - A.y() * q.x()) / det;
                if (bs < 0. || bt < 0. || bs + bt > 1.)
                    continue;
                const double z  = t[0][axis] + bs * (t[1][axis] - t[0][axis]) + bt * (t[2][axis] - t[0][axis]);
                const long   ia = std::clamp(long(std::ceil((z - g.origin[axis]) / g.spacing[axis])), 0L, long(nd[axis]));
                at(size_t(ia), size_t(iu), size_t(iw)) += int(sign);
            }
    }
    std::vector<int> wind(g.nodes(), 0);
    for (size_t iu = 0; iu < nd[u]; ++iu)
        for (size_t iw = 0; iw < nd[w]; ++iw) {
            int run = 0;
            for (size_t ia = 0; ia < nd[axis]; ++ia) {
                run += at(ia, iu, iw);
                size_t ijk[3];
                ijk[axis] = ia, ijk[u] = iu, ijk[w] = iw;
                wind[g.index(ijk[0], ijk[1], ijk[2])] = run;
            }
        }
    return wind;
}

void edt_line(std::vector<double> &f, double s)
{
    const size_t        n = f.size();
    std::vector<double> d(n), z(n + 1);
    std::vector<size_t> v(n);
    size_t              k = 0;
    size_t              first = n;
    for (size_t q = 0; q < n; ++q)
        if (std::isfinite(f[q])) {
            first = q;
            break;
        }
    if (first == n)
        return;
    v[0] = first, z[0] = -Inf, z[1] = Inf;
    for (size_t q = first + 1; q < n; ++q) {
        if (!std::isfinite(f[q]))
            continue;
        double x;
        while ((x = ((f[q] + s * s * double(q) * double(q)) - (f[v[k]] + s * s * double(v[k]) * double(v[k]))) /
                    (2. * s * s * (double(q) - double(v[k])))) <= z[k])
            --k;
        ++k;
        v[k] = q, z[k] = x, z[k + 1] = Inf;
    }
    k = 0;
    for (size_t q = 0; q < n; ++q) {
        while (z[k + 1] < double(q))
            ++k;
        const double dq = s * (double(q) - double(v[k]));
        d[q]            = dq * dq + f[v[k]];
    }
    f = std::move(d);
}

std::vector<float> shell_fraction(const ObjectiveGeometry &g, double want, double shell_mm)
{
    const size_t        n[3] = { g.dims[0] + 2, g.dims[1] + 2, g.dims[2] + 2 };
    auto                pid  = [&](size_t i, size_t j, size_t k) { return (i * n[1] + j) * n[2] + k; };
    std::vector<double> d(n[0] * n[1] * n[2], 0.);
    for (size_t i = 0; i < g.dims[0]; ++i)
        for (size_t j = 0; j < g.dims[1]; ++j)
            for (size_t k = 0; k < g.dims[2]; ++k)
                if (g.occ[g.index(i, j, k)])
                    d[pid(i + 1, j + 1, k + 1)] = Inf;
    for (int a = 0; a < 3; ++a) {
        const int b = (a + 1) % 3, c = (a + 2) % 3;
        std::vector<double> line(n[a]);
        size_t              ijk[3];
        for (size_t x = 0; x < n[b]; ++x)
            for (size_t y = 0; y < n[c]; ++y) {
                ijk[b] = x, ijk[c] = y;
                for (size_t q = 0; q < n[a]; ++q)
                    ijk[a] = q, line[q] = d[pid(ijk[0], ijk[1], ijk[2])];
                edt_line(line, g.spacing[a]);
                for (size_t q = 0; q < n[a]; ++q)
                    ijk[a] = q, d[pid(ijk[0], ijk[1], ijk[2])] = line[q];
            }
    }
    const double V     = g.spacing.prod();
    size_t       total = 0;
    for (uint8_t o : g.occ)
        total += o;
    want         = std::min(want, V * double(total));
    double reach = std::max(shell_mm, g.spacing.maxCoeff());
    std::vector<uint8_t> layer(g.nodes(), 0);
    for (;;) {
        size_t count = 0;
        for (size_t i = 0; i < g.dims[0]; ++i)
            for (size_t j = 0; j < g.dims[1]; ++j)
                for (size_t k = 0; k < g.dims[2]; ++k) {
                    const size_t id = g.index(i, j, k);
                    layer[id]       = g.occ[id] && std::sqrt(d[pid(i + 1, j + 1, k + 1)]) <= reach + 1e-9;
                    count += layer[id];
                }
        if (V * double(count) >= want || count == total) {
            const float f = float(std::min(1., want / std::max(V * double(count), 1e-12)));
            std::vector<float> out(g.nodes(), 0.f);
            for (size_t id = 0; id < g.nodes(); ++id)
                if (layer[id])
                    out[id] = f;
            return out;
        }
        reach += g.spacing.maxCoeff();
    }
}

std::string file_sha256(const std::string &path)
{
    std::ifstream     in(path, std::ios::binary);
    std::string       msg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
    uint32_t       h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const uint64_t bits = uint64_t(msg.size()) * 8;
    msg.push_back(char(0x80));
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(char((bits >> (8 * i)) & 0xff));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(uint8_t(msg[off + 4 * i])) << 24 | uint32_t(uint8_t(msg[off + 4 * i + 1])) << 16 |
                   uint32_t(uint8_t(msg[off + 4 * i + 2])) << 8 | uint32_t(uint8_t(msg[off + 4 * i + 3]));
        for (int i = 16; i < 64; ++i)
            w[i] = w[i - 16] + (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
                   (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10));
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    char hex[65];
    for (int i = 0; i < 8; ++i)
        std::snprintf(hex + 8 * i, 9, "%08x", h[i]);
    return std::string(hex, 64);
}

std::string student_model()
{
    if (const char *env = std::getenv("NP_ONESHOT_MODEL"); env != nullptr && *env != 0)
        return std::string(env) == "none" ? std::string() : std::string(env);
    const boost::filesystem::path bundled = boost::filesystem::path(resources_dir()) / "nonplanar" / "student" / "oneshot.nsw";
    return boost::filesystem::exists(bundled) ? bundled.string() : std::string();
}

void save_objective_geometry(const std::string &path, const ObjectiveGeometry &g)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_writer(&zip, path))
        throw RuntimeError(path + ": cannot write it");
    bool ok  = true;
    auto add = [&](const std::string &name, const std::string &bytes) {
        ok = ok && mz_zip_writer_add_mem(&zip, (name + ".npy").c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION);
    };
    const size_t        n = g.p.size();
    std::vector<double> p(3 * n), nn(3 * n), cls(n);
    for (size_t s = 0; s < n; ++s)
        for (int a = 0; a < 3; ++a)
            p[3 * s + a] = g.p[s][a], nn[3 * s + a] = g.n[s][a], cls[s] = g.cls[s];
    add("p", npy_f8({ n, 3 }, p.data()));
    add("n", npy_f8({ n, 3 }, nn.data()));
    add("a", npy_f8({ n }, g.a.data()));
    add("cls", npy_f8({ n }, cls.data()));
    add("origin", npy_f8({ 3 }, g.origin.data()));
    add("spacing", npy_f8({ 3 }, g.spacing.data()));
    const double dims[3] = { double(g.dims[0]), double(g.dims[1]), double(g.dims[2]) };
    add("dims", npy_f8({ 3 }, dims));
    const std::vector<float> occ(g.occ.begin(), g.occ.end());
    add("occ", npy_f4({ g.dims[0], g.dims[1], g.dims[2] }, occ.data()));
    add("shell", npy_f4({ g.dims[0], g.dims[1], g.dims[2] }, g.shell.data()));
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    close_zip_writer(&zip);
    if (!ok)
        throw RuntimeError(path + ": writing it failed");
}

}

double max_time_ratio_override()
{
    const char *env = std::getenv("NP_ONESHOT_MAX_TIME_RATIO");
    if (env == nullptr || *env == 0)
        return -1.;
    char        *end   = nullptr;
    const double ratio = std::strtod(env, &end);
    return end != env && *end == 0 && std::isfinite(ratio) && ratio > 0. ? ratio : -1.;
}

ObjectiveGeometry objective_geometry(const indexed_triangle_set &mesh, int wall_loops, int top_shell_layers, double line_width, double layer_height)
{
    ObjectiveGeometry                 g;
    std::vector<std::array<Vec3d, 3>> tris;
    std::vector<double>               cdf;
    BoundingBoxf3                     box;
    double                            area = 0.;
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const std::array<Vec3d, 3> t { mesh.vertices[f[0]].cast<double>(), mesh.vertices[f[1]].cast<double>(), mesh.vertices[f[2]].cast<double>() };
        tris.push_back(t);
        for (const Vec3d &v : t)
            box.merge(v);
        area += 0.5 * (t[1] - t[0]).cross(t[2] - t[0]).norm();
        cdf.push_back(area);
    }
    if (tris.empty())
        return g;
    std::mt19937_64                        rng(0x6e733273ULL ^ uint64_t(tris.size()));
    std::uniform_real_distribution<double> uni(0., 1.);
    const double                           a = area / Samples;
    double                                 area_wall = 0.;
    for (int k = 0; k < Samples; ++k) {
        const size_t f  = std::min<size_t>(size_t(std::upper_bound(cdf.begin(), cdf.end(), uni(rng) * area) - cdf.begin()), tris.size() - 1);
        const double r1 = uni(rng), r2 = uni(rng), s1 = std::sqrt(r1);
        const auto  &t  = tris[f];
        const Vec3d  p  = (1. - s1) * t[0] + s1 * (1. - r2) * t[1] + s1 * r2 * t[2];
        const Vec3d  c  = (t[1] - t[0]).cross(t[2] - t[0]);
        const Vec3d  n  = c / std::max(c.norm(), 1e-30);
        if (n.z() < -std::cos(M_PI / 180.) && p.z() < 0.05)
            continue;
        g.p.push_back(p), g.n.push_back(n), g.a.push_back(a);
        g.cls.push_back(n.z() >= Cos70 ? 0 : n.z() <= -Cos70 ? 2 : 1);
        if (g.cls.back() == 1)
            area_wall += a;
    }
    double pitch = std::clamp(box.size().maxCoeff() / 96., 0.5, 2.);
    auto   nodes = [&box](double p) {
        size_t n = 1;
        for (int ax = 0; ax < 3; ++ax)
            n *= size_t(std::ceil((box.max[ax] + 2. * p - (ax < 2 ? box.min[ax] - 2. * p : 0.)) / p)) + 1;
        return n;
    };
    while (nodes(pitch) > MaxNodes)
        pitch *= 1.02;
    g.origin           = Vec3d(box.min.x() - 2. * pitch, box.min.y() - 2. * pitch, 0.);
    g.spacing          = Vec3d::Constant(pitch);
    for (int ax = 0; ax < 3; ++ax)
        g.dims[size_t(ax)] = size_t(std::ceil((box.max[ax] + 2. * pitch - g.origin[ax]) / pitch)) + 1;
    std::vector<uint8_t> votes(g.nodes(), 0);
    for (int ax = 0; ax < 3; ++ax) {
        const std::vector<int> wind = columns(tris, g, ax);
        for (size_t id = 0; id < g.nodes(); ++id)
            votes[id] += wind[id] != 0;
    }
    g.occ.resize(g.nodes());
    for (size_t id = 0; id < g.nodes(); ++id)
        g.occ[id] = votes[id] >= 2;
    const double shell_mm = std::max(wall_loops * line_width, top_shell_layers * layer_height);
    g.shell = shell_fraction(g, area_wall * wall_loops * line_width + (area - area_wall) * top_shell_layers * layer_height, shell_mm);
    return g;
}

ObjectiveMachine objective_machine(const PrintObject &object, const ObjectiveGeometry &geom, double tilt_max)
{
    const PrintConfig       &pc = object.print()->config();
    const PrintObjectConfig &oc = object.config();
    const PrintRegionConfig &rc = object.printing_region(0).config();
    const auto               rad = [](double deg) { return deg * M_PI / 180.; };
    ObjectiveMachine         m;
    m.h                  = oc.layer_height.value;
    m.first_layer        = pc.initial_layer_print_height.value;
    m.thickness_min      = 0.5 * m.h;
    m.thickness_max      = 1.6 * m.h;
    m.printable_overhang = rad(oc.multiplanar_overhang_threshold.value);
    m.rotary             = true;
    m.tilt_max           = tilt_max;
    const double nozzle = pc.nozzle_diameter.get_at(0);
    m.line_width     = oc.line_width.get_abs_value(nozzle);
    m.infill_density = rc.sparse_infill_density.value / 100.;
    m.shell_speed    = 0.5 * (rc.outer_wall_speed.get_at(0) + rc.inner_wall_speed.get_at(0));
    m.sparse_speed   = rc.sparse_infill_speed.get_at(0);
    m.max_volumetric = pc.filament_max_volumetric_speed.get_at(0);
    m.travel_speed   = pc.travel_speed.get_at(0);
    m.min_layer_time = pc.slow_down_layer_time.get_at(0);
    m.road_factor = 1.30;
    m.b_rate       = rad(pc.rotary_max_speed.get_at(0));
    m.c_rate       = rad(pc.rotary_max_speed.get_at(1));
    m.c_continuous = pc.rotary_axis_continuous.get_at(1);
    double rmin = Inf, rmax = 0.;
    for (const Vec2d &p : pc.printable_area.values)
        rmin = std::min(rmin, p.norm()), rmax = std::max(rmax, p.norm());
    if (rmax > 0. && rmax - rmin < 0.05 * rmax)
        m.plate_radius = rmax;
    const Vec3d span = Vec3d(double(geom.dims[0]), double(geom.dims[1]), double(geom.dims[2])).cwiseProduct(geom.spacing);
    m.head           = head_points(resolve_head_envelope(pc), geom.spacing.minCoeff(), span.norm());
    return m;
}

bool oneshot_requested(const PrintObject &object)
{
    const auto *mode = object.model_object()->config.get().option<ConfigOptionEnum<NonplanarMode>>("nonplanar_mode");
    return (mode ? mode->value : object.print()->default_object_config().nonplanar_mode.value) == NonplanarMode::OneShot &&
           object.config().nonplanar_atom_toolpath.value.empty();
}

double in_plane_spread(const NpField &f)
{
    double spread = 0.;
    for (size_t k = 0; k < f.dims[2]; ++k) {
        double lo = Inf, hi = -Inf;
        for (size_t i = 0; i < f.dims[0]; ++i)
            for (size_t j = 0; j < f.dims[1]; ++j)
                lo = std::min(lo, double(f.phi[f.index(i, j, k)])), hi = std::max(hi, double(f.phi[f.index(i, j, k)]));
        spread = std::max(spread, 0.5 * (hi - lo));
    }
    return spread;
}

std::vector<double> layer_profile(const NpField &f, double blend, double first_layer, double top, double min_h, double max_h)
{
    std::vector<double> z(f.dims[2]), phi(f.dims[2]);
    for (size_t k = 0; k < f.dims[2]; ++k) {
        double sum = 0.;
        for (size_t i = 0; i < f.dims[0]; ++i)
            for (size_t j = 0; j < f.dims[1]; ++j)
                sum += f.phi[f.index(i, j, k)];
        z[k]   = f.node(0, 0, k).z();
        phi[k] = z[k] + blend * (sum / double(f.dims[0] * f.dims[1]) - z[k]);
    }
    auto inverse = [&](double level) {
        for (size_t k = 1; k < z.size(); ++k)
            if (phi[k] >= level)
                return z[k - 1] + (z[k] - z[k - 1]) * (level - phi[k - 1]) / std::max(phi[k] - phi[k - 1], 1e-12);
        return z.back() + (level - phi.back());
    };
    std::vector<double> out { 0., first_layer };
    double              below = first_layer;
    for (int j = 1; below < top - 1e-6; ++j) {
        double h = std::clamp(inverse(first_layer + j * f.h) - below, min_h, max_h);
        if (const double rest = top - below; rest - h < min_h)
            h = rest <= max_h ? rest : 0.5 * rest;
        out.insert(out.end(), { below, h, std::min(below + h, top) - 1e-4, h });
        below += h;
    }
    out.insert(out.end(), { top, out.back() });
    return out;
}

double OneShotState::support_time() const
{
    if (solved)
        for (const auto &[name, value] : solved->terms)
            if (name == "stat_planar_support_s")
                return value;
    return 0.;
}

NpField blend_toward_planar(const NpField &field, double blend)
{
    NpField out = field;
    for (size_t i = 0; i < field.dims[0]; ++i)
        for (size_t j = 0; j < field.dims[1]; ++j)
            for (size_t k = 0; k < field.dims[2]; ++k) {
                const size_t id = field.index(i, j, k);
                const double z  = field.node(i, j, k).z();
                out.phi[id]     = float(z + blend * (double(field.phi[id]) - z));
            }
    return out;
}

NpField one_shot_field(const PrintObject &object, const indexed_triangle_set &mesh, double tilt_max, double abort_after,
                       const std::function<void()> &cancel, std::string &student, int &polish_k, bool &aborted)
{
    const auto               t0 = std::chrono::steady_clock::now();
    const PrintObjectConfig &oc = object.config();
    const PrintRegionConfig &rc = object.printing_region(0).config();
    const double             nozzle = object.print()->config().nozzle_diameter.get_at(0);
    const ObjectiveGeometry  g = objective_geometry(mesh, rc.wall_loops.value, rc.top_shell_layers.value, oc.line_width.get_abs_value(nozzle),
                                                    oc.layer_height.value);
    const ObjectiveMachine   m = objective_machine(object, g, tilt_max);
    ObjectiveParams          params;
    params.max_starts   = 3;
    params.lottery_keep = 1;
    params.level_rounds = 1;
    params.local_rounds = 0;
    params.budget       = 0.;
    const std::string model = student_model();
    student  = model.empty() ? std::string("none") : file_sha256(model);
    polish_k = model.empty() ? 0 : StudentPolish;
    NpField f;
    f.origin      = g.origin;
    f.spacing     = g.spacing;
    f.dims        = g.dims;
    f.h           = m.h;
    f.first_layer = m.first_layer;
    f.phi.resize(g.nodes());
    for (size_t i = 0; i < g.dims[0]; ++i)
        for (size_t j = 0; j < g.dims[1]; ++j)
            for (size_t k = 0; k < g.dims[2]; ++k)
                f.phi[g.index(i, j, k)] = float(g.origin.z() + double(k) * g.spacing.z());
    if (!model.empty()) {
        f.phi                = student_phi(StudentNet(model), g, m);
        params.multi_start   = false;
        params.warm_adaptive = false;
        params.local_rounds  = 0;
        params.level_rounds  = 0;
        params.iterations    = StudentPolish;
    }
    struct Aborted
    {};
    const std::vector<float> planar = f.phi;
    ObjectiveTerms           t;
    aborted = false;
    try {
        t = solve_objective(g, m, ObjectiveWeights(), params, f.phi,
                            [&cancel, t0, abort_after]() {
                                if (cancel)
                                    cancel();
                                if (abort_after > 0. && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > abort_after)
                                    throw Aborted();
                            },
                            [&object](const std::string &line) {
                                BOOST_LOG_TRIVIAL(info) << "One-shot field " << object.model_object()->name << ": " << line;
                            });
    } catch (const Aborted &) {
        aborted = true;
        f.phi   = planar;
        t       = evaluate_objective(g, m, ObjectiveWeights(), f.phi, true);
        BOOST_LOG_TRIVIAL(warning) << "One-shot field " << object.model_object()->name << ": solve aborted at the " << abort_after
                                   << " s time cap, planar layers";
    }
    BoundingBoxf3 box;
    for (const stl_vertex &v : mesh.vertices)
        box.merge(v.cast<double>());
    f.mesh_min = box.min;
    f.solver   = model.empty() ? std::string("one-shot teacher ") + ObjectiveVersion :
                                 "one-shot student " + boost::filesystem::path(model).filename().string() + " " + ObjectiveVersion;
    f.terms    = t.named();
    f.terms.emplace_back("stat_j_version", ObjectiveVersionNumber);
    f.proxy_version = ObjectiveVersion;
    f.terms.emplace_back("stat_oneshot_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    f.terms.emplace_back("stat_oneshot_aborted", aborted ? 1. : 0.);
    f.terms.emplace_back("stat_planar_support_s", planar_support_time(g, m));
    if (const char *dir = std::getenv("NP_ONESHOT_DUMP"); dir != nullptr && *dir != 0) {
        const std::string stem = (boost::filesystem::path(dir) / object.model_object()->name).string();
        save_objective_geometry(stem + ".geom.npz", g);
        save_npfield(stem + ".field.npz", f);
    }
    BOOST_LOG_TRIVIAL(info) << "One-shot field " << object.model_object()->name << ": J " << t.J << ", " << g.nodes() << " nodes at "
                            << g.spacing.x() << " mm, " << (t.feasible() ? "feasible" : "infeasible") << ", "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s";
    return f;
}

}
