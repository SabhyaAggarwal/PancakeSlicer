#include "StudentNet.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

struct Vol
{
    int                c = 0;
    std::array<int, 3> d {};
    std::vector<float> v;

    Vol() = default;
    Vol(int c, std::array<int, 3> d) : c(c), d(d), v(size_t(c) * d[0] * d[1] * d[2], 0.f) {}
    size_t plane() const { return size_t(d[0]) * d[1] * d[2]; }
    float *ch(int i) { return v.data() + size_t(i) * plane(); }
    const float *ch(int i) const { return v.data() + size_t(i) * plane(); }
};

void parallel(int n, int threads, const std::function<void(int)> &f)
{
    threads = std::max(1, std::min(n, threads));
    if (threads == 1) {
        for (int i = 0; i < n; ++i)
            f(i);
        return;
    }
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (int i = t; i < n; i += threads)
                f(i);
        });
    for (std::thread &th : pool)
        th.join();
}

Vol conv3(const Vol &in, const std::vector<float> &w, const std::vector<float> &b, int cout, int threads)
{
    Vol out(cout, in.d);
    const int X = in.d[0], Y = in.d[1], Z = in.d[2];
    parallel(cout, threads, [&](int co) {
        float *o = out.ch(co);
        std::fill(o, o + out.plane(), b[co]);
        for (int ci = 0; ci < in.c; ++ci) {
            const float *src = in.ch(ci);
            const float *wk  = w.data() + (size_t(co) * in.c + ci) * 27;
            for (int dx = 0; dx < 3; ++dx)
                for (int dy = 0; dy < 3; ++dy)
                    for (int dz = 0; dz < 3; ++dz) {
                        const float wt = wk[(dx * 3 + dy) * 3 + dz];
                        const int   x0 = std::max(0, 1 - dx), x1 = std::min(X, X + 1 - dx);
                        const int   y0 = std::max(0, 1 - dy), y1 = std::min(Y, Y + 1 - dy);
                        const int   z0 = std::max(0, 1 - dz), z1 = std::min(Z, Z + 1 - dz);
                        for (int x = x0; x < x1; ++x)
                            for (int y = y0; y < y1; ++y) {
                                float       *orow = o + (size_t(x) * Y + y) * Z;
                                const float *irow = src + (size_t(x + dx - 1) * Y + (y + dy - 1)) * Z + (dz - 1);
                                for (int z = z0; z < z1; ++z)
                                    orow[z] += wt * irow[z];
                            }
                    }
        }
    });
    return out;
}

void group_norm_silu(Vol &x, int groups, const std::vector<float> &gamma, const std::vector<float> &beta, int threads)
{
    const int per = x.c / groups;
    parallel(groups, threads, [&](int g) {
        double s = 0., s2 = 0.;
        for (int c = g * per; c < (g + 1) * per; ++c)
            for (const float *p = x.ch(c), *e = p + x.plane(); p != e; ++p) {
                s += *p;
                s2 += double(*p) * *p;
            }
        const double n = double(per) * x.plane(), mean = s / n, var = std::max(0., s2 / n - mean * mean);
        const double inv = 1. / std::sqrt(var + 1e-5);
        for (int c = g * per; c < (g + 1) * per; ++c) {
            const float a = float(gamma[c] * inv), b = float(beta[c] - gamma[c] * mean * inv);
            for (float *p = x.ch(c), *e = p + x.plane(); p != e; ++p) {
                const float y = a * *p + b;
                *p = y / (1.f + std::exp(-y));
            }
        }
    });
}

Vol avg_pool2(const Vol &in)
{
    Vol out(in.c, {in.d[0] / 2, in.d[1] / 2, in.d[2] / 2});
    const int X = out.d[0], Y = out.d[1], Z = out.d[2];
    for (int c = 0; c < in.c; ++c) {
        const float *s = in.ch(c);
        float       *o = out.ch(c);
        for (int x = 0; x < X; ++x)
            for (int y = 0; y < Y; ++y)
                for (int z = 0; z < Z; ++z) {
                    float acc = 0.f;
                    for (int i = 0; i < 8; ++i)
                        acc += s[(size_t(2 * x + (i >> 2)) * in.d[1] + 2 * y + ((i >> 1) & 1)) * in.d[2] + 2 * z + (i & 1)];
                    o[(size_t(x) * Y + y) * Z + z] = acc * .125f;
                }
    }
    return out;
}

Vol up2_concat(const Vol &x, const Vol &skip)
{
    Vol out(x.c + skip.c, skip.d);
    const int X = skip.d[0], Y = skip.d[1], Z = skip.d[2];
    for (int c = 0; c < x.c; ++c) {
        const float *s = x.ch(c);
        float       *o = out.ch(c);
        for (int i = 0; i < X; ++i)
            for (int j = 0; j < Y; ++j)
                for (int k = 0; k < Z; ++k)
                    o[(size_t(i) * Y + j) * Z + k] = s[(size_t(i / 2) * x.d[1] + j / 2) * x.d[2] + k / 2];
    }
    std::copy(skip.v.begin(), skip.v.end(), out.ch(x.c));
    return out;
}

void lin_taps(int n_in, int o, int &i0, int &i1, float &f)
{
    const double s = std::max(0., (o + .5) / 2. - .5);
    i0             = std::min(int(s), n_in - 1);
    i1             = std::min(i0 + 1, n_in - 1);
    f              = float(s - i0);
}

size_t read_u32(const std::string &s, size_t at)
{
    return size_t(uint8_t(s[at])) | size_t(uint8_t(s[at + 1])) << 8 | size_t(uint8_t(s[at + 2])) << 16 |
           size_t(uint8_t(s[at + 3])) << 24;
}

double json_number(const std::string &h, const std::string &key)
{
    const size_t at = h.find("\"" + key + "\":");
    if (at == std::string::npos)
        throw std::runtime_error("student.nsw: no " + key);
    return std::stod(h.substr(at + key.size() + 3));
}

std::vector<int> json_ints(const std::string &h, size_t from, size_t *end)
{
    const size_t     open = h.find('[', from), close = h.find(']', open);
    std::vector<int> out;
    for (size_t i = open + 1; i < close;) {
        size_t used = 0;
        out.push_back(std::stoi(h.substr(i), &used));
        i += used;
        while (i < close && (h[i] == ',' || h[i] == ' '))
            ++i;
    }
    if (end)
        *end = close;
    return out;
}

}

StudentNet::StudentNet(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("student.nsw: cannot open " + path);
    const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.size() < 8 || s.compare(0, 4, "NSW1") != 0)
        throw std::runtime_error("student.nsw: not an NSW1 file: " + path);
    const size_t      hl = read_u32(s, 4);
    const std::string h  = s.substr(8, hl);
    m_width              = json_ints(h, h.find("\"width\":"), nullptr);
    m_groups             = int(json_number(h, "groups"));
    m_r_scale            = json_number(h, "r_scale");
    m_fade               = json_number(h, "fade");
    if (int(json_number(h, "n_feat")) != NFeat || int(json_number(h, "n_mach")) != NMach)
        throw std::runtime_error("student.nsw: input sizes differ from this build's");
    size_t at = 8 + hl;
    for (size_t k = h.find("\"shape\":"); k != std::string::npos; k = h.find("\"shape\":", k)) {
        Tensor t;
        t.shape  = json_ints(h, k, &k);
        size_t n = 1;
        for (int d : t.shape)
            n *= size_t(d);
        if (at + 4 * n > s.size())
            throw std::runtime_error("student.nsw: truncated " + path);
        t.v.resize(n);
        std::memcpy(t.v.data(), s.data() + at, 4 * n);
        at += 4 * n;
        m_tensors.push_back(std::move(t));
    }
    const size_t want = m_width.size() * 8 + 2 + (m_width.size() - 1) * 8 + 2;
    if (m_tensors.size() != want || at != s.size())
        throw std::runtime_error("student.nsw: tensor count or size mismatch in " + path);
}

std::array<float, StudentNet::NMach> StudentNet::machine_vector(double tilt_max, double printable_overhang, double h,
                                                                bool rotary, double thickness_min, double thickness_max)
{
    return {float(std::tan(tilt_max)), float(std::sin(printable_overhang)), float(h / .2), rotary ? 1.f : 0.f,
            float(thickness_min / h), float(thickness_max / h)};
}

std::vector<float> StudentNet::features(const Input &in)
{
    const size_t X = in.dims[0], Y = in.dims[1], Z = in.dims[2], nodes = X * Y * Z;
    std::vector<double> acc(7 * nodes, 0.);
    for (size_t s = 0; s < in.a.size(); ++s) {
        size_t idx[3];
        for (int c = 0; c < 3; ++c) {
            const double q = (in.p[3 * s + c] - in.origin[c]) / in.spacing[c];
            const double r = q >= 0 ? std::floor(q + .5) : std::ceil(q - .5);
            idx[c]         = size_t(std::clamp(r, 0., double(in.dims[c] - 1)));
        }
        const size_t id = (idx[0] * Y + idx[1]) * Z + idx[2];
        const double a  = in.a[s];
        for (int c = 0; c < 3; ++c)
            acc[c * nodes + id] += a * in.n[3 * s + c];
        acc[3 * nodes + id] += a;
        acc[(4 + in.cls[s]) * nodes + id] += a;
    }
    const double       pitch = std::min({in.spacing[0], in.spacing[1], in.spacing[2]});
    std::vector<float> f(NFeat * nodes);
    for (size_t id = 0; id < nodes; ++id) {
        const double nn = std::max(std::sqrt(acc[id] * acc[id] + acc[nodes + id] * acc[nodes + id] +
                                             acc[2 * nodes + id] * acc[2 * nodes + id]),
                                   1e-12);
        const double area = acc[3 * nodes + id];
        f[id]             = in.occ[id] ? 1.f : 0.f;
        f[nodes + id]     = in.shell[id];
        for (int c = 0; c < 3; ++c)
            f[(2 + c) * nodes + id] = float(acc[c * nodes + id] / nn);
        f[5 * nodes + id] = float(area / (pitch * pitch));
        for (int c = 0; c < 3; ++c)
            f[(6 + c) * nodes + id] = float(acc[(4 + c) * nodes + id] / std::max(area, 1e-12));
        f[9 * nodes + id] = float(double(id % Z) / double(std::max<size_t>(Z - 1, 1)));
    }
    return f;
}

std::vector<float> StudentNet::phi(const Input &in, int threads) const
{
    if (threads <= 0)
        threads = int(std::max(1u, std::thread::hardware_concurrency()));
    const int X = int(in.dims[0]), Y = int(in.dims[1]), Z = int(in.dims[2]);
    const int mult = 1 << int(m_width.size());
    const std::vector<float> feat = features(in);
    Vol                      x(NFeat, {X + (mult - X % mult) % mult, Y + (mult - Y % mult) % mult, Z + (mult - Z % mult) % mult});
    for (int c = 0; c < NFeat; ++c)
        for (int i = 0; i < X; ++i)
            for (int j = 0; j < Y; ++j)
                std::copy_n(feat.data() + size_t(c) * X * Y * Z + (size_t(i) * Y + j) * Z, Z,
                            x.ch(c) + (size_t(i) * x.d[1] + j) * x.d[2]);
    x = avg_pool2(x);

    size_t t     = 0;
    auto   block = [&](const Vol &v, int cout) {
        Vol y = conv3(v, m_tensors[t].v, m_tensors[t + 1].v, cout, threads);
        group_norm_silu(y, m_groups, m_tensors[t + 2].v, m_tensors[t + 3].v, threads);
        Vol z = conv3(y, m_tensors[t + 4].v, m_tensors[t + 5].v, cout, threads);
        group_norm_silu(z, m_groups, m_tensors[t + 6].v, m_tensors[t + 7].v, threads);
        t += 8;
        return z;
    };
    std::vector<Vol> skips;
    for (size_t l = 0; l < m_width.size(); ++l) {
        x = block(x, m_width[l]);
        if (l + 1 < m_width.size()) {
            skips.push_back(x);
            x = avg_pool2(x);
        }
    }
    const std::vector<float> &W = m_tensors[t].v, &B = m_tensors[t + 1].v;
    t += 2;
    for (int c = 0; c < x.c; ++c) {
        float gamma = B[c], beta = B[x.c + c];
        for (int k = 0; k < NMach; ++k) {
            gamma += W[size_t(c) * NMach + k] * in.machine[k];
            beta += W[size_t(x.c + c) * NMach + k] * in.machine[k];
        }
        for (float *p = x.ch(c), *e = p + x.plane(); p != e; ++p)
            *p = *p * (1.f + gamma) + beta;
    }
    for (int l = int(m_width.size()) - 2; l >= 0; --l) {
        x = block(up2_concat(x, skips.back()), m_width[l]);
        skips.pop_back();
    }
    const std::vector<float> &hw = m_tensors[t].v;
    const float               hb = m_tensors[t + 1].v[0];
    std::vector<float>        head(x.plane(), hb);
    for (int c = 0; c < x.c; ++c) {
        const float *s = x.ch(c);
        for (size_t i = 0; i < head.size(); ++i)
            head[i] += hw[c] * s[i];
    }

    const double sz   = in.spacing[2];
    const int    kb   = int(std::floor(in.first_layer / sz + 1e-9)) + 1;
    const double lo   = sz * in.h / in.thickness_max, hi = sz * in.h / in.thickness_min;
    std::vector<float> out(size_t(X) * Y * Z);
    const int    cx = x.d[0], cy = x.d[1], cz = x.d[2];
    for (int i = 0; i < X; ++i) {
        int   i0, i1, j0, j1, k0, k1;
        float fi, fj, fk;
        lin_taps(cx, i, i0, i1, fi);
        for (int j = 0; j < Y; ++j) {
            lin_taps(cy, j, j0, j1, fj);
            float *col  = out.data() + (size_t(i) * Y + j) * Z;
            double prev = 0.;
            for (int k = 0; k < Z; ++k) {
                lin_taps(cz, k, k0, k1, fk);
                auto at = [&](int a, int b, int c) { return head[(size_t(a) * cy + b) * cz + c]; };
                const float r = (1 - fi) * ((1 - fj) * ((1 - fk) * at(i0, j0, k0) + fk * at(i0, j0, k1)) +
                                            fj * ((1 - fk) * at(i0, j1, k0) + fk * at(i0, j1, k1))) +
                                fi * ((1 - fj) * ((1 - fk) * at(i1, j0, k0) + fk * at(i1, j0, k1)) +
                                      fj * ((1 - fk) * at(i1, j1, k0) + fk * at(i1, j1, k1)));
                const double fade = std::clamp((k - kb) / m_fade, 0., 1.);
                double       v    = in.origin[2] + k * sz + double(r) * m_r_scale * sz * fade;
                if (k > kb)
                    v = prev + std::clamp(v - prev, lo, hi);
                col[k] = float(v);
                prev   = v;
            }
        }
    }
    return out;
}

}
