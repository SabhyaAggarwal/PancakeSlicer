#include "TextureDisplacement.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <math.h>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <ratio>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <Eigen/Eigenvalues>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>

#include <boost/log/trivial.hpp>
#include <utility>
#include <vector>

#include "AABBTreeIndirect.hpp"
#include "MeshBoolean.hpp"
#include "Model.hpp"
#include "PNGReadWrite.hpp"
#include "Point.hpp"
#include "TriangleMesh.hpp"
#include "TextureBake/TextureBakeDisplace.hpp"
#include "TriangleSelector.hpp"
#include "TextureBake/TextureBakeDebug.hpp"
#include "TextureBake/TextureBakeMesh.hpp"
#include "TextureBake/TextureBakePipeline.hpp"
#include "TextureBake/TextureBakeIndex.hpp"

namespace Slic3r {

bool DecodedHeightTexture::texel_tap(const Vec2f &uv, bool tile_enabled, TextureTileMethod tile_method,
                                     TexelTap &tap) const
{
    if (empty())
        return false;

    auto repeat01 = [](float x) {
        x = std::fmod(x, 1.f);
        return x < 0.f ? x + 1.f : x;
    };


    auto mirrored_repeat01 = [](float x) {
        x = std::fmod(std::abs(x), 2.f);
        return x > 1.f ? 2.f - x : x;
    };

    if (!tile_enabled && (uv.x() < 0.f || uv.x() >= 1.f || uv.y() < 0.f || uv.y() >= 1.f))



        return false;

    float u, v;
    if (!tile_enabled) {
        u = uv.x();
        v = uv.y();
    } else if (tile_method == TextureTileMethod::MirroredRepeat) {
        u = mirrored_repeat01(uv.x());
        v = mirrored_repeat01(uv.y());
    } else {
        u = repeat01(uv.x());
        v = repeat01(uv.y());
    }

    const float fx = u * float(width);
    const float fy = v * float(height);
    const int   x0 = std::clamp(int(std::floor(fx)), 0, width - 1);
    const int   y0 = std::clamp(int(std::floor(fy)), 0, height - 1);


    tap.x0 = x0;
    tap.y0 = y0;
    tap.x1 = tile_enabled ? (x0 + 1) % width  : std::min(x0 + 1, width - 1);
    tap.y1 = tile_enabled ? (y0 + 1) % height : std::min(y0 + 1, height - 1);
    tap.tx = fx - std::floor(fx);
    tap.ty = fy - std::floor(fy);
    return true;
}

float DecodedHeightTexture::sample(const Vec2f &uv, bool tile_enabled, TextureTileMethod tile_method) const
{
    TexelTap tap;
    if (!texel_tap(uv, tile_enabled, tile_method, tap))
        return 0.f;
    const int   x0 = tap.x0, y0 = tap.y0, x1 = tap.x1, y1 = tap.y1;
    const float tx = tap.tx, ty = tap.ty;

    auto at = [this](int x, int y) { return float(pixels[size_t(y) * size_t(width) + size_t(x)]) / 255.f; };
    const float top    = at(x0, y0) * (1.f - tx) + at(x1, y0) * tx;
    const float bottom = at(x0, y1) * (1.f - tx) + at(x1, y1) * tx;
    return top * (1.f - ty) + bottom * ty;
}

Vec3f DecodedHeightTexture::sample_color(const Vec2f &uv, bool tile_enabled, TextureTileMethod tile_method) const
{
    TexelTap tap;
    if (!has_color() || !texel_tap(uv, tile_enabled, tile_method, tap))
        return Vec3f::Zero();

    auto at = [this](int x, int y) {
        const size_t i = (size_t(y) * size_t(width) + size_t(x)) * 3;
        return Vec3f(float(rgb[i]) / 255.f, float(rgb[i + 1]) / 255.f, float(rgb[i + 2]) / 255.f);
    };
    const Vec3f top    = at(tap.x0, tap.y0) * (1.f - tap.tx) + at(tap.x1, tap.y0) * tap.tx;
    const Vec3f bottom = at(tap.x0, tap.y1) * (1.f - tap.tx) + at(tap.x1, tap.y1) * tap.tx;
    return top * (1.f - tap.ty) + bottom * tap.ty;
}

namespace {








struct DecodedTextureCache
{
    std::mutex mutex;
    std::unordered_map<const void *, std::pair<std::weak_ptr<const std::vector<unsigned char>>, DecodedHeightTexture>> entries;
};
DecodedTextureCache g_decoded_texture_cache;






struct SmoothedTextureCache
{
    struct Entry
    {
        std::weak_ptr<const std::vector<unsigned char>> source;
        float                                           smoothing = 0.f;
        DecodedHeightTexture                            texture;
    };
    std::mutex                                   mutex;
    std::unordered_map<const void *, Entry>      entries;
};
SmoothedTextureCache g_smoothed_texture_cache;
}

namespace {









void smooth_height_pixels_box(std::vector<uint8_t> &pixels, int width, int height, int radius)
{
    if (radius <= 0 || width <= 0 || height <= 0 || pixels.size() != size_t(width) * size_t(height))
        return;

    const int   window = 2 * radius + 1;
    const float inv    = 1.f / float(window);
    const auto  wrap   = [](int i, int n) { return (i % n + n) % n; };
    std::vector<uint8_t> tmp(pixels.size());

    for (int pass = 0; pass < 2; ++pass) {

        tbb::parallel_for(tbb::blocked_range<int>(0, height), [&](const tbb::blocked_range<int> &r) {
            for (int y = r.begin(); y < r.end(); ++y) {
                const uint8_t *src = pixels.data() + size_t(y) * size_t(width);
                uint8_t       *dst = tmp.data() + size_t(y) * size_t(width);
                uint32_t       sum = 0;
                for (int k = -radius; k <= radius; ++k)
                    sum += src[wrap(k, width)];
                for (int x = 0; x < width; ++x) {
                    dst[x] = uint8_t(std::lround(float(sum) * inv));
                    sum += src[wrap(x + radius + 1, width)];
                    sum -= src[wrap(x - radius, width)];
                }
            }
        });

        tbb::parallel_for(tbb::blocked_range<int>(0, width, 256), [&](const tbb::blocked_range<int> &r) {
            std::vector<uint32_t> sum(size_t(r.size()), 0);
            for (int k = -radius; k <= radius; ++k) {
                const uint8_t *row = tmp.data() + size_t(wrap(k, height)) * size_t(width);
                for (int x = r.begin(); x < r.end(); ++x)
                    sum[size_t(x - r.begin())] += row[x];
            }
            for (int y = 0; y < height; ++y) {
                uint8_t       *dst = pixels.data() + size_t(y) * size_t(width);
                const uint8_t *add = tmp.data() + size_t(wrap(y + radius + 1, height)) * size_t(width);
                const uint8_t *sub = tmp.data() + size_t(wrap(y - radius, height)) * size_t(width);
                for (int x = r.begin(); x < r.end(); ++x) {
                    uint32_t &sx = sum[size_t(x - r.begin())];
                    dst[x] = uint8_t(std::lround(float(sx) * inv));
                    sx += add[x];
                    sx -= sub[x];
                }
            }
        });
    }
}









void smooth_height_pixels(std::vector<uint8_t> &pixels, int width, int height, float radius)
{
    if (radius <= 0.f || width <= 0 || height <= 0 || pixels.size() != size_t(width) * size_t(height))
        return;

    const int   whole = std::max(1, int(std::ceil(radius)));
    const float mix   = std::clamp(radius / float(whole), 0.f, 1.f);
    const std::vector<uint8_t> raw = (mix < 0.999f) ? pixels : std::vector<uint8_t>{};
    smooth_height_pixels_box(pixels, width, height, whole);
    if (!raw.empty())
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = uint8_t(std::lround(float(raw[i]) + (float(pixels[i]) - float(raw[i])) * mix));
}
}

bool height_texture_has_color(const TextureDisplacementLayer &layer)
{
    if (layer.empty())
        return false;
    {
        std::lock_guard<std::mutex> lock(g_decoded_texture_cache.mutex);
        const auto it = g_decoded_texture_cache.entries.find(layer.image_data.get());
        if (it != g_decoded_texture_cache.entries.end() && it->second.first.lock() == layer.image_data)
            return it->second.second.has_color();
    }

    return decode_height_texture(layer).has_color();
}

DecodedHeightTexture decode_height_texture(const TextureDisplacementLayer &layer)
{
    DecodedHeightTexture result;
    if (layer.empty())
        return result;




    const void *key = layer.image_data.get();
    bool        have_raw = false;
    {
        std::lock_guard<std::mutex> lock(g_decoded_texture_cache.mutex);
        auto it = g_decoded_texture_cache.entries.find(key);
        if (it != g_decoded_texture_cache.entries.end() && it->second.first.lock() == layer.image_data) {
            result   = it->second.second;
            have_raw = true;
        }
    }

    if (!have_raw) {
        const png::ReadBuf rbuf{ layer.image_data->data(), layer.image_data->size() };
        if (!png::is_png(rbuf))


            return result;

        png::ImageGreyscale img;
        if (png::decode_png(rbuf, img) && img.cols > 0 && img.rows > 0) {

            result.width  = int(img.cols);
            result.height = int(img.rows);
            result.pixels = std::move(img.buf);
        } else {






            png::ImageColorscale col;
            if (!png::decode_colored_png(rbuf, col) || col.cols == 0 || col.rows == 0 ||
                col.bytes_per_pixel < 3 || col.buf.size() != col.cols * col.rows * size_t(col.bytes_per_pixel))
                return result;

            const int    w   = int(col.cols);
            const int    h   = int(col.rows);
            const size_t bpp = size_t(col.bytes_per_pixel);
            result.width  = w;
            result.height = h;
            result.pixels.resize(size_t(w) * size_t(h));
            result.rgb.resize(size_t(w) * size_t(h) * 3);




            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint8_t *src = col.buf.data() + (size_t(h - 1 - y) * size_t(w) + size_t(x)) * bpp;
                    const size_t   dst = size_t(y) * size_t(w) + size_t(x);
                    const uint8_t  r = src[0], g = src[1], b = src[2];
                    result.rgb[dst * 3]     = r;
                    result.rgb[dst * 3 + 1] = g;
                    result.rgb[dst * 3 + 2] = b;
                    result.pixels[dst] = uint8_t(std::lround(0.299 * r + 0.587 * g + 0.114 * b));
                }
        }

        std::lock_guard<std::mutex> lock(g_decoded_texture_cache.mutex);


        auto &entries = g_decoded_texture_cache.entries;
        for (auto it = entries.begin(); it != entries.end();)
            it = it->second.first.expired() ? entries.erase(it) : std::next(it);
        entries[key] = { std::weak_ptr<const std::vector<unsigned char>>(layer.image_data), result };
    }






    if (layer.smoothing > 0.f) {
        {
            std::lock_guard<std::mutex> lock(g_smoothed_texture_cache.mutex);
            auto it = g_smoothed_texture_cache.entries.find(key);
            if (it != g_smoothed_texture_cache.entries.end() && it->second.smoothing == layer.smoothing &&
                it->second.source.lock() == layer.image_data)
                return it->second.texture;
        }
        const float span   = 0.05f * float(std::min(result.width, result.height));
        const float radius = std::clamp(layer.smoothing, 0.f, 1.f) * std::min(span, 48.f);
        smooth_height_pixels(result.pixels, result.width, result.height, radius);



        if (result.has_color()) {
            const size_t         n = size_t(result.width) * size_t(result.height);
            std::vector<uint8_t> channel(n);
            for (int c = 0; c < 3; ++c) {
                for (size_t i = 0; i < n; ++i)
                    channel[i] = result.rgb[i * 3 + size_t(c)];
                smooth_height_pixels(channel, result.width, result.height, radius);
                for (size_t i = 0; i < n; ++i)
                    result.rgb[i * 3 + size_t(c)] = channel[i];
            }
        }
        std::lock_guard<std::mutex> lock(g_smoothed_texture_cache.mutex);
        auto &entries = g_smoothed_texture_cache.entries;
        for (auto it = entries.begin(); it != entries.end();)
            it = it->second.source.expired() ? entries.erase(it) : std::next(it);
        entries[key] = { std::weak_ptr<const std::vector<unsigned char>>(layer.image_data), layer.smoothing, result };
    }
    return result;
}

namespace {
struct TextureDetailCache
{
    std::mutex mutex;
    std::unordered_map<const void *, std::pair<std::weak_ptr<const std::vector<unsigned char>>, TextureDetail>> entries;
};
TextureDetailCache g_texture_detail_cache;
}

TextureDetail analyze_texture_detail(const TextureDisplacementLayer &layer)
{
    TextureDetail out;
    if (layer.empty())
        return out;
    const void *key = layer.image_data.get();
    {
        std::lock_guard<std::mutex> lock(g_texture_detail_cache.mutex);
        auto it = g_texture_detail_cache.entries.find(key);
        if (it != g_texture_detail_cache.entries.end() && it->second.first.lock() == layer.image_data)
            return it->second.second;
    }

    TextureDisplacementLayer raw = layer;
    raw.smoothing               = 0.f;
    const DecodedHeightTexture tex = decode_height_texture(raw);
    const int w = tex.width, h = tex.height;
    if (w >= 3 && h >= 3) {
        double sum = 0.0;
        size_t sharp = 0, n = 0;
        for (int y = 1; y < h - 1; ++y) {
            const uint8_t *row = tex.pixels.data() + size_t(y) * size_t(w);
            for (int x = 1; x < w - 1; ++x) {
                const float dx  = 0.5f * (float(row[x + 1]) - float(row[x - 1]));
                const float dy  = 0.5f * (float(row[x + w]) - float(row[x - w]));
                const float mag = std::sqrt(dx * dx + dy * dy);
                sum += mag;
                sharp += mag > 30.f;
                ++n;
            }
        }
        out.mean_gradient  = float(sum / double(n));
        out.sharp_fraction = float(sharp) / float(n);
        if (out.sharp_fraction > 0.15f || out.mean_gradient > 50.f)      out.pixels_per_edge = 1.f;
        else if (out.sharp_fraction > 0.05f || out.mean_gradient > 20.f) out.pixels_per_edge = 1.5f;
        else if (out.mean_gradient > 8.f)                                out.pixels_per_edge = 2.5f;
        else                                                             out.pixels_per_edge = 4.f;




        std::vector<uint32_t> bins(size_t(8 * 8 * 8), 0);
        const size_t          npx = size_t(w) * size_t(h);
        if (tex.has_color())
            for (size_t i = 0; i < npx; ++i)
                ++bins[size_t(tex.rgb[i * 3] >> 5) * 64 + size_t(tex.rgb[i * 3 + 1] >> 5) * 8 + size_t(tex.rgb[i * 3 + 2] >> 5)];
        else
            for (size_t i = 0; i < npx; ++i)
                ++bins[size_t(tex.pixels[i] >> 2) * 8];
        std::partial_sort(bins.begin(), bins.begin() + 8, bins.end(), std::greater<uint32_t>());
        uint64_t top = 0;
        for (int i = 0; i < 8; ++i)
            top += bins[size_t(i)];
        out.flat_share  = float(double(top) / double(npx));
        out.flat_colors = out.flat_share >= 0.85f;
    }
    std::lock_guard<std::mutex> lock(g_texture_detail_cache.mutex);
    auto &entries = g_texture_detail_cache.entries;
    for (auto it = entries.begin(); it != entries.end();)
        it = it->second.first.expired() ? entries.erase(it) : std::next(it);
    entries[key] = { std::weak_ptr<const std::vector<unsigned char>>(layer.image_data), out };
    return out;
}

V2Resolution recommend_v2_resolution(const indexed_triangle_set                  &mesh,
                                     const std::vector<TextureDisplacementLayer> &layers,
                                     const Transform3d                           &volume_to_world)
{




    constexpr double EDGE_MIN = 0.05, EDGE_MAX = 5.0, DIAG_DIVISOR = 250.0;
    constexpr int    BUDGET_K = 750;

    V2Resolution out;
    if (mesh.vertices.empty())
        return out;
    for (const TextureDisplacementLayer &layer : layers) {
        if (layer.empty() || layer.tiling_scale <= 0.f)
            continue;
        const DecodedHeightTexture &tex = decode_height_texture(layer);
        if (tex.width <= 0)
            continue;
        const float texel = layer.tiling_scale / float(tex.width);
        if (out.texel_mm <= 0.f || texel < out.texel_mm) {
            out.texel_mm        = texel;
            out.pixels_per_edge = analyze_texture_detail(layer).pixels_per_edge;
        }
    }
    Vec3d bmin = Vec3d::Constant(std::numeric_limits<double>::max()), bmax = -bmin;
    for (const Vec3f &v : mesh.vertices) {
        const Vec3d w = volume_to_world * v.cast<double>();
        bmin = bmin.cwiseMin(w);
        bmax = bmax.cwiseMax(w);
    }
    const double diag = (bmax - bmin).norm();
    double       edge = std::clamp(diag / DIAG_DIVISOR, EDGE_MIN, EDGE_MAX);
    edge              = std::max(EDGE_MIN, std::ceil(edge * 100.0) / 100.0);
    out.edge_mm       = float(edge);
    out.budget_k      = BUDGET_K;
    out.budget_bound  = false;
    return out;
}

Vec2f project_planar(const Vec3f &position, const Vec3f &normal)
{






    const Vec3f n = normal.cwiseAbs();
    if (n.x() >= n.y() && n.x() >= n.z())
        return Vec2f(position.y(), position.z());
    if (n.y() >= n.x() && n.y() >= n.z())
        return Vec2f(position.x(), position.z());
    return Vec2f(position.x(), position.y());
}

namespace {



Vec2f project_cylindrical(const Vec3f &position, const Vec3f &patch_center, const Vec3f &patch_axis)
{
    Vec3f up = patch_axis;
    up       = (up.norm() > 1e-8f) ? Vec3f(up.normalized()) : Vec3f::UnitZ();
    const Vec3f arbitrary = (std::abs(up.dot(Vec3f::UnitZ())) < 0.9f) ? Vec3f::UnitZ() : Vec3f::UnitX();
    const Vec3f right     = Vec3f(up.cross(arbitrary).normalized());
    const Vec3f fwd       = Vec3f(right.cross(up).normalized());

    const Vec3f rel        = position - patch_center;
    const float along_axis = rel.dot(up);
    const float x          = rel.dot(right);
    const float y          = rel.dot(fwd);
    const float radius     = std::sqrt(x * x + y * y);
    const float angle      = std::atan2(y, x);

    return Vec2f(angle * radius, along_axis);
}




Vec2f project_spherical(const Vec3f &position, const Vec3f &patch_center)
{
    const Vec3f rel    = position - patch_center;
    const float radius = rel.norm();
    if (radius < 1e-8f)
        return Vec2f::Zero();

    const Vec3f dir       = rel / radius;
    const float longitude = std::atan2(dir.y(), dir.x());
    const float latitude  = std::asin(std::clamp(dir.z(), -1.f, 1.f));
    return Vec2f(longitude, latitude) * radius;
}






indexed_triangle_set compact_patch_with_map(const indexed_triangle_set &patch, std::vector<int> &original_to_compact)
{
    original_to_compact.assign(patch.vertices.size(), -1);
    indexed_triangle_set compact;
    compact.vertices.reserve(patch.vertices.size());
    compact.indices.reserve(patch.indices.size());
    for (const stl_triangle_vertex_indices &tri : patch.indices) {
        stl_triangle_vertex_indices new_tri;
        for (int i = 0; i < 3; ++i) {
            const int vi = tri[i];
            if (original_to_compact[vi] < 0) {
                original_to_compact[vi] = int(compact.vertices.size());
                compact.vertices.push_back(patch.vertices[vi]);
            }
            new_tri[i] = original_to_compact[vi];
        }
        compact.indices.push_back(new_tri);
    }
    return compact;
}

}

namespace {


struct UnionFind
{
    std::vector<int> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int x)
    {
        while (parent[size_t(x)] != x) {
            parent[size_t(x)] = parent[size_t(parent[size_t(x)])];
            x                 = parent[size_t(x)];
        }
        return x;
    }
    void unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[size_t(b)] = a;
    }
};

uint64_t undirected_edge_key(int a, int b)
{
    if (a > b)
        std::swap(a, b);
    return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
}

Vec3f face_normal(const indexed_triangle_set &mesh, const stl_triangle_vertex_indices &tri)
{
    const Vec3f n   = (mesh.vertices[tri[1]] - mesh.vertices[tri[0]]).cross(mesh.vertices[tri[2]] - mesh.vertices[tri[0]]);
    const float len = n.norm();
    return (len > 1e-12f) ? Vec3f(n / len) : Vec3f::UnitZ();
}




bool chart_is_flattenable(const indexed_triangle_set &mesh, const std::vector<Vec3f> &normals, const std::vector<int> &faces)
{
    constexpr float MIN_NORMAL_COS = -0.17f;

    std::unordered_map<uint64_t, int> edge_use;
    std::unordered_map<int, int>      local_vertex;
    edge_use.reserve(faces.size() * 2);
    local_vertex.reserve(faces.size());
    Vec3f normal_sum = Vec3f::Zero();
    for (const int f : faces) {
        const stl_triangle_vertex_indices &tri = mesh.indices[size_t(f)];
        for (int i = 0; i < 3; ++i) {
            ++edge_use[undirected_edge_key(tri[i], tri[(i + 1) % 3])];
            local_vertex.emplace(tri[i], int(local_vertex.size()));
        }
        normal_sum += normals[size_t(f)];
    }
    if (int(local_vertex.size()) - int(edge_use.size()) + int(faces.size()) != 1)
        return false;

    UnionFind loops(local_vertex.size());
    int       boundary_seed = -1;
    for (const auto &[key, uses] : edge_use) {
        if (uses > 2)
            return false;
        if (uses == 1) {
            const int a = local_vertex[int(key >> 32)], b = local_vertex[int(uint32_t(key))];
            loops.unite(a, b);
            boundary_seed = a;
        }
    }
    if (boundary_seed < 0)
        return false;
    const int loop = loops.find(boundary_seed);
    for (const auto &[key, uses] : edge_use)
        if (uses == 1 && loops.find(local_vertex[int(key >> 32)]) != loop)
            return false;

    const float len = normal_sum.norm();
    if (len < 1e-6f)
        return false;
    const Vec3f mean = normal_sum / len;
    for (const int f : faces) {
        const stl_triangle_vertex_indices &tri = mesh.indices[size_t(f)];
        const bool degenerate = (mesh.vertices[tri[1]] - mesh.vertices[tri[0]]).cross(mesh.vertices[tri[2]] - mesh.vertices[tri[0]])
                                    .squaredNorm() < 1e-20f;
        if (!degenerate && normals[size_t(f)].dot(mean) < MIN_NORMAL_COS)
            return false;
    }
    return true;
}





std::vector<char> split_chart_sides(const indexed_triangle_set &mesh, const std::vector<Vec3f> &normals, const std::vector<int> &faces)
{
    const auto principal_axis = [](const std::vector<Vec3f> &samples, Vec3f &mean, Vec3f &axis) {
        mean = Vec3f::Zero();
        for (const Vec3f &s : samples)
            mean += s;
        mean /= float(samples.size());
        Eigen::Matrix3f covariance = Eigen::Matrix3f::Zero();
        for (const Vec3f &s : samples) {
            const Vec3f d = s - mean;
            covariance += d * d.transpose();
        }
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> solver(covariance);
        axis = solver.eigenvectors().col(2);
        return solver.eigenvalues()(2) / float(samples.size());
    };

    std::vector<Vec3f> samples(faces.size());
    for (size_t i = 0; i < faces.size(); ++i)
        samples[i] = normals[size_t(faces[i])];
    Vec3f mean, axis;
    if (principal_axis(samples, mean, axis) < 1e-3f) {
        for (size_t i = 0; i < faces.size(); ++i) {
            const stl_triangle_vertex_indices &tri = mesh.indices[size_t(faces[i])];
            samples[i] = (mesh.vertices[tri[0]] + mesh.vertices[tri[1]] + mesh.vertices[tri[2]]) / 3.f;
        }
        principal_axis(samples, mean, axis);
    }
    std::vector<char> side(faces.size());
    for (size_t i = 0; i < faces.size(); ++i)
        side[i] = (samples[i] - mean).dot(axis) > 0.f;
    return side;
}









std::vector<int> segment_into_charts(const indexed_triangle_set &mesh, const std::vector<Vec3f> &normals,
                                      float seam_angle_deg, const std::unordered_set<uint64_t> &seam_keys,
                                      int &chart_count)
{
    constexpr int MAX_SPLIT_DEPTH = 8;
    const int     n_faces         = int(mesh.indices.size());
    const float   cos_threshold   = std::cos(std::clamp(seam_angle_deg, 0.f, 180.f) * float(M_PI) / 180.f);


    std::unordered_map<uint64_t, std::pair<int, int>> edge_faces;
    edge_faces.reserve(size_t(n_faces) * 3);
    for (int f = 0; f < n_faces; ++f) {
        const stl_triangle_vertex_indices &tri = mesh.indices[size_t(f)];
        for (int i = 0; i < 3; ++i) {
            const uint64_t key        = undirected_edge_key(tri[i], tri[(i + 1) % 3]);
            const auto [it, inserted] = edge_faces.emplace(key, std::make_pair(f, -1));
            if (!inserted)
                it->second.second = f;
        }
    }


    std::vector<std::array<int, 3>> adjacent(static_cast<size_t>(n_faces), { -1, -1, -1 });
    const auto link = [&adjacent](int f, int nb) {
        for (int &slot : adjacent[size_t(f)])
            if (slot < 0) {
                slot = nb;
                return;
            }
    };
    for (const auto &[key, fp] : edge_faces) {
        if (fp.second < 0 || fp.first == fp.second)
            continue;


        if (!seam_keys.empty() && seam_keys.count(key))
            continue;
        if (normals[size_t(fp.first)].dot(normals[size_t(fp.second)]) >= cos_threshold) {
            link(fp.first, fp.second);
            link(fp.second, fp.first);
        }
    }


    std::vector<int> group(static_cast<size_t>(n_faces), 0), visited(static_cast<size_t>(n_faces), -1);
    int              visit_pass = 0;
    const auto connected_pieces = [&](const std::vector<int> &faces, std::vector<std::vector<int>> &out) {
        const int        pass = visit_pass++;
        std::vector<int> stack;
        for (const int seed : faces) {
            if (visited[size_t(seed)] == pass)
                continue;
            visited[size_t(seed)] = pass;
            std::vector<int> piece{ seed };
            stack.assign(1, seed);
            while (!stack.empty()) {
                const int f = stack.back();
                stack.pop_back();
                for (const int nb : adjacent[size_t(f)])
                    if (nb >= 0 && visited[size_t(nb)] != pass && group[size_t(nb)] == group[size_t(f)]) {
                        visited[size_t(nb)] = pass;
                        piece.push_back(nb);
                        stack.push_back(nb);
                    }
            }
            out.push_back(std::move(piece));
        }
    };

    std::vector<int> all_faces(static_cast<size_t>(n_faces));
    std::iota(all_faces.begin(), all_faces.end(), 0);
    std::vector<std::vector<int>> pieces;
    connected_pieces(all_faces, pieces);
    std::vector<std::pair<std::vector<int>, int>> pending;
    for (std::vector<int> &piece : pieces)
        pending.emplace_back(std::move(piece), 0);

    std::vector<std::vector<int>> charts;
    int                           next_group = 1;
    while (!pending.empty()) {
        std::vector<int> faces = std::move(pending.back().first);
        const int        depth = pending.back().second;
        pending.pop_back();
        if (faces.size() < 2 || depth >= MAX_SPLIT_DEPTH || chart_is_flattenable(mesh, normals, faces)) {
            charts.push_back(std::move(faces));
            continue;
        }
        const std::vector<char> side = split_chart_sides(mesh, normals, faces);
        std::vector<int>        halves[2];
        for (size_t i = 0; i < faces.size(); ++i)
            halves[side[i] ? 1 : 0].push_back(faces[i]);
        if (halves[0].empty() || halves[1].empty()) {
            charts.push_back(std::move(faces));
            continue;
        }
        for (std::vector<int> &half : halves) {
            for (const int f : half)
                group[size_t(f)] = next_group;
            ++next_group;
            pieces.clear();
            connected_pieces(half, pieces);
            for (std::vector<int> &piece : pieces)
                pending.emplace_back(std::move(piece), depth + 1);
        }
    }


    std::vector<std::pair<int, size_t>> first_face(charts.size());
    for (size_t c = 0; c < charts.size(); ++c)
        first_face[c] = { *std::min_element(charts[c].begin(), charts[c].end()), c };
    std::sort(first_face.begin(), first_face.end());

    std::vector<int> chart_of(static_cast<size_t>(n_faces), -1);
    for (size_t id = 0; id < first_face.size(); ++id)
        for (const int f : charts[first_face[id].second])
            chart_of[size_t(f)] = int(id);
    chart_count = int(charts.size());
    return chart_of;
}




std::vector<Vec2f> project_to_tangent_plane(const indexed_triangle_set &chart, const Vec3f &normal)
{
    const Vec3f seed = (std::abs(normal.z()) < 0.9f) ? Vec3f::UnitZ() : Vec3f::UnitX();
    const Vec3f u    = Vec3f(seed.cross(normal).normalized());
    const Vec3f v    = Vec3f(normal.cross(u).normalized());

    std::vector<Vec2f> uvs(chart.vertices.size());
    for (size_t i = 0; i < chart.vertices.size(); ++i)
        uvs[i] = Vec2f(chart.vertices[i].dot(u), chart.vertices[i].dot(v));
    return uvs;
}

float area_3d(const indexed_triangle_set &mesh)
{
    float area = 0.f;
    for (const stl_triangle_vertex_indices &t : mesh.indices)
        area += 0.5f * (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]).norm();
    return area;
}

float area_2d(const std::vector<Vec2f> &uvs, const std::vector<stl_triangle_vertex_indices> &indices)
{
    float area = 0.f;
    for (const stl_triangle_vertex_indices &t : indices) {
        const Vec2f e0 = uvs[size_t(t[1])] - uvs[size_t(t[0])];
        const Vec2f e1 = uvs[size_t(t[2])] - uvs[size_t(t[0])];
        area += 0.5f * std::abs(e0.x() * e1.y() - e0.y() * e1.x());
    }
    return area;
}




uint64_t unwrap_cache_key(const indexed_triangle_set &patch, float seam_angle_deg, float padding_mm,
                          const std::vector<std::pair<int, int>> &seam_edges)
{
    uint64_t   h   = 1469598103934665603ull;
    const auto mix = [&h](const void *data, size_t bytes) {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < bytes; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    mix(patch.vertices.data(), patch.vertices.size() * sizeof(Vec3f));
    mix(patch.indices.data(), patch.indices.size() * sizeof(stl_triangle_vertex_indices));
    mix(&seam_angle_deg, sizeof(seam_angle_deg));
    mix(&padding_mm, sizeof(padding_mm));
    mix(seam_edges.data(), seam_edges.size() * sizeof(std::pair<int, int>));
    return h;
}

struct UnwrapCache
{
    std::mutex                                mutex;
    std::unordered_map<uint64_t, PatchUnwrap> entries;
};
UnwrapCache  g_unwrap_cache;
const size_t UNWRAP_CACHE_MAX_ENTRIES = 8;

}

PatchUnwrap compute_patch_unwrap(const indexed_triangle_set &patch, float seam_angle_deg, float padding_mm,
                                 const std::vector<std::pair<int, int>> &seam_edges)
{
    PatchUnwrap result;
    if (patch.indices.empty())
        return result;

    const uint64_t key = unwrap_cache_key(patch, seam_angle_deg, padding_mm, seam_edges);
    {
        std::lock_guard<std::mutex> lock(g_unwrap_cache.mutex);
        if (auto it = g_unwrap_cache.entries.find(key); it != g_unwrap_cache.entries.end())
            return it->second;
    }



    std::vector<int>           patch_to_compact;
    const indexed_triangle_set compact = compact_patch_with_map(patch, patch_to_compact);
    std::vector<int>           compact_to_patch(compact.vertices.size(), -1);
    for (size_t vi = 0; vi < patch_to_compact.size(); ++vi)
        if (patch_to_compact[vi] >= 0)
            compact_to_patch[size_t(patch_to_compact[vi])] = int(vi);

    std::vector<Vec3f> normals(compact.indices.size());
    for (size_t f = 0; f < compact.indices.size(); ++f)
        normals[f] = face_normal(compact, compact.indices[f]);



    std::unordered_set<uint64_t> seam_keys;
    seam_keys.reserve(seam_edges.size() * 2);
    for (const auto &[a, b] : seam_edges) {
        if (a < 0 || b < 0 || size_t(a) >= patch_to_compact.size() || size_t(b) >= patch_to_compact.size())
            continue;
        const int ca = patch_to_compact[size_t(a)];
        const int cb = patch_to_compact[size_t(b)];
        if (ca >= 0 && cb >= 0)
            seam_keys.insert(undirected_edge_key(ca, cb));
    }






    const float effective_seam_angle = seam_keys.empty() ? seam_angle_deg : 180.f;
    int                    chart_count = 0;
    const std::vector<int> chart_of    = segment_into_charts(compact, normals, effective_seam_angle, seam_keys, chart_count);

    struct FlatChart
    {
        std::vector<Vec2f>                       uvs;
        std::vector<int>                         to_patch;
        std::vector<stl_triangle_vertex_indices> indices;


        std::vector<int>                         faces;
        Vec2f                                    min  = Vec2f::Zero();
        Vec2f                                    size = Vec2f::Zero();
    };

    std::vector<FlatChart> charts(static_cast<size_t>(chart_count));

    for (int c = 0; c < chart_count; ++c) {
        FlatChart &chart = charts[size_t(c)];




        indexed_triangle_set chart_mesh;
        std::vector<int>     compact_to_local(compact.vertices.size(), -1);
        Vec3f                normal_sum = Vec3f::Zero();
        for (size_t f = 0; f < compact.indices.size(); ++f) {
            if (chart_of[f] != c)
                continue;
            const stl_triangle_vertex_indices &tri = compact.indices[f];

            normal_sum += (compact.vertices[tri[1]] - compact.vertices[tri[0]]).cross(compact.vertices[tri[2]] - compact.vertices[tri[0]]);

            stl_triangle_vertex_indices local_tri;
            for (int i = 0; i < 3; ++i) {
                const int cv = tri[i];
                if (compact_to_local[size_t(cv)] < 0) {
                    compact_to_local[size_t(cv)] = int(chart_mesh.vertices.size());
                    chart_mesh.vertices.push_back(compact.vertices[size_t(cv)]);
                    chart.to_patch.push_back(compact_to_patch[size_t(cv)]);
                }
                local_tri[i] = compact_to_local[size_t(cv)];
            }
            chart_mesh.indices.push_back(local_tri);
            chart.faces.push_back(int(f));
        }
        if (chart_mesh.indices.empty())
            continue;

        const Vec3f chart_normal = (normal_sum.norm() > 1e-12f) ? Vec3f(normal_sum.normalized()) : Vec3f::UnitZ();






        bool planar = true;
        for (size_t f = 0; f < compact.indices.size() && planar; ++f)
            if (chart_of[f] == c)
                planar = normals[f].dot(chart_normal) >= 0.9998f;



        const float mesh_area_3d = area_3d(chart_mesh);

        std::optional<std::vector<Vec2f>> uvs;
        if (!planar)
            uvs = MeshBoolean::cgal::parameterize_lscm(chart_mesh);

        chart.uvs = uvs ? std::move(*uvs) : project_to_tangent_plane(chart_mesh, chart_normal);
        chart.indices = std::move(chart_mesh.indices);




        const float uv_area = area_2d(chart.uvs, chart.indices);
        if (uv_area > 1e-12f && mesh_area_3d > 1e-12f) {
            const float scale = std::sqrt(mesh_area_3d / uv_area);
            for (Vec2f &uv : chart.uvs)
                uv *= scale;
        }

        Vec2f lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Vec2f hi(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
        for (const Vec2f &uv : chart.uvs) {
            lo = lo.cwiseMin(uv);
            hi = hi.cwiseMax(uv);
        }
        chart.min  = lo;
        chart.size = hi - lo;
    }



    float total_area = 0.f;
    float widest     = 0.f;
    for (const FlatChart &chart : charts) {
        total_area = total_area + chart.size.x() * chart.size.y();
        widest     = std::max(widest, chart.size.x());
    }
    const float shelf_width = std::max(widest, std::sqrt(std::max(total_area, 0.f)) * 1.4f);


    const float margin      = (padding_mm >= 0.f) ? padding_mm : std::max(shelf_width * 0.02f, 1e-4f);

    std::vector<int> order(charts.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&charts](int a, int b) { return charts[size_t(a)].size.y() > charts[size_t(b)].size.y(); });

    float cursor_x = 0.f, cursor_y = 0.f, row_height = 0.f;
    for (const int c : order) {
        FlatChart &chart = charts[size_t(c)];
        if (chart.uvs.empty())
            continue;
        if (cursor_x > 0.f && cursor_x + chart.size.x() > shelf_width) {
            cursor_x   = 0.f;
            cursor_y  += row_height + margin;
            row_height = 0.f;
        }
        const Vec2f translation = Vec2f(cursor_x, cursor_y) - chart.min;
        for (Vec2f &uv : chart.uvs)
            uv += translation;

        cursor_x  += chart.size.x() + margin;
        row_height = std::max(row_height, chart.size.y());
    }


    result.chart_centroid.assign(size_t(chart_count), Vec2f::Zero());
    for (int c = 0; c < chart_count; ++c) {
        FlatChart &chart = charts[size_t(c)];
        const int  base  = int(result.uvs.size());
        result.uvs.insert(result.uvs.end(), chart.uvs.begin(), chart.uvs.end());
        result.source_vertex.insert(result.source_vertex.end(), chart.to_patch.begin(), chart.to_patch.end());
        result.vertex_chart.insert(result.vertex_chart.end(), chart.uvs.size(), c);
        for (const stl_triangle_vertex_indices &tri : chart.indices)
            result.indices.emplace_back(tri[0] + base, tri[1] + base, tri[2] + base);
        result.source_face.insert(result.source_face.end(), chart.faces.begin(), chart.faces.end());

        if (!chart.uvs.empty()) {
            Vec2f sum = Vec2f::Zero();
            for (const Vec2f &uv : chart.uvs)
                sum += uv;
            result.chart_centroid[size_t(c)] = sum / float(chart.uvs.size());
        }
    }
    result.chart_count = chart_count;




    {
        std::unordered_map<uint64_t, int> edge_use;
        edge_use.reserve(result.indices.size() * 3);
        for (const stl_triangle_vertex_indices &tri : result.indices)
            for (int i = 0; i < 3; ++i)
                ++edge_use[undirected_edge_key(tri[i], tri[(i + 1) % 3])];
        for (const stl_triangle_vertex_indices &tri : result.indices)
            for (int i = 0; i < 3; ++i) {
                const int a = tri[i], b = tri[(i + 1) % 3];
                if (edge_use[undirected_edge_key(a, b)] == 1)
                    result.boundary_edges.emplace_back(a, b);
            }
    }

    {
        std::lock_guard<std::mutex> lock(g_unwrap_cache.mutex);


        if (g_unwrap_cache.entries.size() >= UNWRAP_CACHE_MAX_ENTRIES)
            g_unwrap_cache.entries.clear();
        g_unwrap_cache.entries.emplace(key, result);
    }
    return result;
}

Eigen::Matrix<float, 2, 3> island_transform_matrix(int chart, const PatchUnwrap &unwrap, const std::vector<TextureIsland> &islands)
{
    Eigen::Matrix<float, 2, 3> m;
    m << 1.f, 0.f, 0.f,
         0.f, 1.f, 0.f;
    if (chart < 0 || size_t(chart) >= islands.size() || size_t(chart) >= unwrap.chart_centroid.size())
        return m;

    const TextureIsland &island = islands[size_t(chart)];
    const Vec2f          centre = unwrap.chart_centroid[size_t(chart)];

    const float rad = island.rotation_deg * float(M_PI) / 180.f;
    const float s   = island.scale;
    const float cs  = std::cos(rad) * s;
    const float sn  = std::sin(rad) * s;



    Eigen::Matrix2f linear;
    linear << cs, -sn,
              sn,  cs;
    const Vec2f translation = centre + island.offset - linear * centre;

    m.block<2, 2>(0, 0) = linear;
    m.col(2)            = translation;
    return m;
}

Vec2f apply_island_transform(const Vec2f &uv, int chart, const PatchUnwrap &unwrap, const std::vector<TextureIsland> &islands)
{
    const Eigen::Matrix<float, 2, 3> m = island_transform_matrix(chart, unwrap, islands);
    return m.block<2, 2>(0, 0) * uv + m.col(2);
}

namespace {


struct ChartEdge { int chart = -1; int uv_lo = -1; int uv_hi = -1; };




std::map<std::pair<int, int>, std::vector<ChartEdge>> build_shared_edges(const PatchUnwrap &u)
{
    std::map<std::pair<int, int>, std::vector<ChartEdge>> edges;
    for (const auto &[ua, ub] : u.boundary_edges) {
        if (ua < 0 || ub < 0 || size_t(ua) >= u.source_vertex.size() || size_t(ub) >= u.source_vertex.size())
            continue;
        const int ba = u.source_vertex[size_t(ua)], bb = u.source_vertex[size_t(ub)];
        if (ba < 0 || bb < 0 || ba == bb)
            continue;
        const int chart = (size_t(ua) < u.vertex_chart.size()) ? u.vertex_chart[size_t(ua)] : -1;
        if (chart < 0)
            continue;
        const std::pair<int, int> key{ std::min(ba, bb), std::max(ba, bb) };

        ChartEdge e{ chart, ua, ub };
        if (u.source_vertex[size_t(ua)] != key.first)
            std::swap(e.uv_lo, e.uv_hi);
        edges[key].push_back(e);
    }
    return edges;
}




TextureIsland solve_edge_alignment(const Vec2f &rc0, const Vec2f &rc1, const Vec2f &pp0, const Vec2f &pp1, const Vec2f &cen)
{
    const Vec2f dP = pp1 - pp0, dC = rc1 - rc0;
    const float rho = std::atan2(dP.y(), dP.x()) - std::atan2(dC.y(), dC.x());
    const float cs = std::cos(rho), sn = std::sin(rho);
    const auto  rot = [&](const Vec2f &v) { return Vec2f(v.x() * cs - v.y() * sn, v.x() * sn + v.y() * cs); };

    TextureIsland island;
    island.scale        = 1.f;
    island.rotation_deg = rho * 180.f / float(M_PI);
    island.offset       = pp0 - (rot(rc0 - cen) + cen);
    return island;
}
}

bool join_chart_placement(const PatchUnwrap &unwrap, const std::vector<TextureIsland> &islands, int child, int parent,
                          TextureIsland &out_child)
{
    if (child < 0 || parent < 0 || child == parent || size_t(child) >= unwrap.chart_centroid.size())
        return false;
    const auto edges = build_shared_edges(unwrap);
    for (const auto &[base_edge, list] : edges) {
        const ChartEdge *pe = nullptr;
        const ChartEdge *ce = nullptr;
        for (const ChartEdge &e : list) {
            if (e.chart == parent) pe = &e;
            if (e.chart == child)  ce = &e;
        }
        if (pe == nullptr || ce == nullptr)
            continue;

        const Vec2f pp0 = apply_island_transform(unwrap.uvs[size_t(pe->uv_lo)], parent, unwrap, islands);
        const Vec2f pp1 = apply_island_transform(unwrap.uvs[size_t(pe->uv_hi)], parent, unwrap, islands);
        out_child = solve_edge_alignment(unwrap.uvs[size_t(ce->uv_lo)], unwrap.uvs[size_t(ce->uv_hi)], pp0, pp1,
                                          unwrap.chart_centroid[size_t(child)]);
        return true;
    }
    return false;
}

namespace {
using Tri2 = std::array<Vec2f, 3>;



bool triangles_overlap(const Tri2 &a, const Tri2 &b, float eps)
{
    for (const Tri2 *t : { &a, &b })
        for (int i = 0; i < 3; ++i) {
            const Vec2f edge = (*t)[(i + 1) % 3] - (*t)[i];
            const float len  = edge.norm();
            if (len < 1e-12f)
                continue;
            const Vec2f axis(-edge.y() / len, edge.x() / len);
            float       a_min = std::numeric_limits<float>::max(), a_max = std::numeric_limits<float>::lowest();
            float       b_min = a_min, b_max = a_max;
            for (int k = 0; k < 3; ++k) {
                const float pa = axis.dot(a[k]), pb = axis.dot(b[k]);
                a_min = std::min(a_min, pa);
                a_max = std::max(a_max, pa);
                b_min = std::min(b_min, pb);
                b_max = std::max(b_max, pb);
            }
            if (a_max <= b_min + eps || b_max <= a_min + eps)
                return false;
        }
    return true;
}



struct NetGrid
{
    static constexpr int BIG_SPAN = 16;









    struct Entry
    {
        Tri2  tri;
        Vec2f lo, hi;
    };
    float                                           cell;
    float                                           eps;
    std::unordered_map<uint64_t, std::vector<Entry>> cells;
    std::vector<Entry>                              big;

    static uint64_t key(int x, int y) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(y); }
    static Entry    entry(const Tri2 &t)
    {
        return Entry{ t, t[0].cwiseMin(t[1]).cwiseMin(t[2]), t[0].cwiseMax(t[1]).cwiseMax(t[2]) };
    }
    bool range(const Vec2f &lo, const Vec2f &hi, int &x0, int &y0, int &x1, int &y1) const
    {
        x0 = int(std::floor(lo.x() / cell));
        y0 = int(std::floor(lo.y() / cell));
        x1 = int(std::floor(hi.x() / cell));
        y1 = int(std::floor(hi.y() / cell));
        return x1 - x0 <= BIG_SPAN && y1 - y0 <= BIG_SPAN;
    }


    bool boxes_apart(const Entry &a, const Entry &b) const
    {
        return a.hi.x() + eps < b.lo.x() || b.hi.x() + eps < a.lo.x() || a.hi.y() + eps < b.lo.y() ||
               b.hi.y() + eps < a.lo.y();
    }
    bool hits(const Entry &q, const std::vector<Entry> &bucket) const
    {
        for (const Entry &b : bucket)
            if (!boxes_apart(q, b) && triangles_overlap(q.tri, b.tri, eps))
                return true;
        return false;
    }
    bool overlaps(const Tri2 &t) const
    {
        const Entry q = entry(t);
        if (hits(q, big))
            return true;
        int x0, y0, x1, y1;
        if (!range(q.lo, q.hi, x0, y0, x1, y1)) {
            for (const auto &[k, bucket] : cells)
                if (hits(q, bucket))
                    return true;
            return false;
        }
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                if (const auto it = cells.find(key(x, y)); it != cells.end() && hits(q, it->second))
                    return true;
        return false;
    }
    void insert(const Tri2 &t)
    {
        const Entry e = entry(t);
        int         x0, y0, x1, y1;
        if (!range(e.lo, e.hi, x0, y0, x1, y1)) {
            big.push_back(e);
            return;
        }
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                cells[key(x, y)].push_back(e);
    }
};
}

std::vector<TextureIsland> compute_connected_net(const PatchUnwrap &unwrap)
{
    const int                  n = std::max(unwrap.chart_count, 0);
    std::vector<TextureIsland> islands(static_cast<size_t>(n));
    if (n <= 1)
        return islands;











    const auto edges = build_shared_edges(unwrap);
    struct PairEdge { ChartEdge a, b; };
    struct SharedEdge { PairEdge fold; int base_lo = -1, base_hi = -1; float length = 0.f; };
    std::map<std::pair<int, int>, std::vector<SharedEdge>> pair_shared;
    for (const auto &[base_edge, list] : edges) {
        for (size_t i = 0; i < list.size(); ++i)
            for (size_t j = i + 1; j < list.size(); ++j) {
                const int c1 = list[i].chart, c2 = list[j].chart;
                if (c1 == c2 || c1 < 0 || c2 < 0 || c1 >= n || c2 >= n)
                    continue;
                const std::pair<int, int> pk{ std::min(c1, c2), std::max(c1, c2) };
                SharedEdge se;
                se.fold    = (c1 < c2) ? PairEdge{ list[i], list[j] } : PairEdge{ list[j], list[i] };
                se.base_lo = base_edge.first;
                se.base_hi = base_edge.second;

                se.length = (unwrap.uvs[size_t(se.fold.a.uv_lo)] - unwrap.uvs[size_t(se.fold.a.uv_hi)]).norm();
                pair_shared[pk].push_back(se);
            }
    }

    std::map<std::pair<int, int>, PairEdge> pair_edge;
    std::map<std::pair<int, int>, float>    pair_weight;
    std::vector<std::vector<int>>           adj(static_cast<size_t>(n));
    for (const auto &[pk, shared] : pair_shared) {


        std::unordered_map<int, int> local;
        for (const SharedEdge &se : shared)
            for (const int v : { se.base_lo, se.base_hi })
                local.emplace(v, int(local.size()));
        UnionFind runs(local.size());
        for (const SharedEdge &se : shared)
            runs.unite(local[se.base_lo], local[se.base_hi]);

        std::unordered_map<int, float>  run_length;
        std::unordered_map<int, size_t> run_first;
        for (size_t i = 0; i < shared.size(); ++i) {
            const int root = runs.find(local[shared[i].base_lo]);
            run_length[root] += shared[i].length;
            run_first.emplace(root, i);
        }
        int   best_root = -1;
        float best_len  = -1.f;
        for (const auto &[root, len] : run_length)
            if (len > best_len) { best_len = len; best_root = root; }
        if (best_root < 0)
            continue;

        pair_edge[pk] = shared[run_first[best_root]].fold;
        pair_weight[pk] = best_len;
        adj[size_t(pk.first)].push_back(pk.second);
        adj[size_t(pk.second)].push_back(pk.first);
    }


    std::vector<std::vector<int>> chart_verts(static_cast<size_t>(n)), chart_tris(static_cast<size_t>(n));
    std::vector<float>            chart_area(static_cast<size_t>(n), 0.f);
    for (size_t i = 0; i < unwrap.uvs.size(); ++i)
        if (const int c = unwrap.vertex_chart[i]; c >= 0 && c < n)
            chart_verts[size_t(c)].push_back(int(i));
    float extent_sum = 0.f, extent = 0.f;
    for (size_t t = 0; t < unwrap.indices.size(); ++t) {
        const stl_triangle_vertex_indices &tri = unwrap.indices[t];
        const int                          c   = unwrap.vertex_chart[size_t(tri[0])];
        if (c < 0 || c >= n)
            continue;
        chart_tris[size_t(c)].push_back(int(t));
        const Vec2f &p0 = unwrap.uvs[size_t(tri[0])], &p1 = unwrap.uvs[size_t(tri[1])], &p2 = unwrap.uvs[size_t(tri[2])];
        const Vec2f  e0 = p1 - p0, e1 = p2 - p0;
        chart_area[size_t(c)] += 0.5f * std::abs(e0.x() * e1.y() - e0.y() * e1.x());
        const Vec2f size = p0.cwiseMax(p1).cwiseMax(p2) - p0.cwiseMin(p1).cwiseMin(p2);
        extent_sum += std::max(size.x(), size.y());
        extent = std::max({ extent, p0.cwiseAbs().maxCoeff(), p1.cwiseAbs().maxCoeff(), p2.cwiseAbs().maxCoeff() });
    }
    const float cell = std::max(extent_sum / float(std::max<size_t>(unwrap.indices.size(), 1)), 1e-6f);


    const float eps  = std::max(0.02f * cell, 4e-6f * extent);

    const auto placed = [&](const Eigen::Matrix<float, 2, 3> &m, int t) {
        const stl_triangle_vertex_indices &tri = unwrap.indices[size_t(t)];
        Tri2                               out;
        for (int k = 0; k < 3; ++k)
            out[size_t(k)] = m.block<2, 2>(0, 0) * unwrap.uvs[size_t(tri[k])] + m.col(2);
        return out;
    };




    std::vector<int> by_area(static_cast<size_t>(n));
    std::iota(by_area.begin(), by_area.end(), 0);
    std::stable_sort(by_area.begin(), by_area.end(), [&chart_area](int a, int b) { return chart_area[size_t(a)] > chart_area[size_t(b)]; });

    std::vector<int> net_of(static_cast<size_t>(n), -1);
    int              net_count = 0;
    for (const int root : by_area) {
        if (net_of[size_t(root)] >= 0 || chart_tris[size_t(root)].empty())
            continue;
        const int net = net_count++;
        NetGrid   grid{ cell, eps, {}, {} };
        net_of[size_t(root)] = net;
        {
            const Eigen::Matrix<float, 2, 3> m = island_transform_matrix(root, unwrap, islands);
            for (const int t : chart_tris[size_t(root)])
                grid.insert(placed(m, t));
        }








        using Candidate = std::pair<float, std::pair<int, int>>;
        std::priority_queue<Candidate>                              q;
        const auto push_neighbours = [&](int p) {
            for (const int c : adj[size_t(p)])
                if (net_of[size_t(c)] < 0 && !chart_tris[size_t(c)].empty()) {
                    const auto w = pair_weight.find({ std::min(p, c), std::max(p, c) });
                    q.push({ w == pair_weight.end() ? 0.f : w->second, { p, c } });
                }
        };
        push_neighbours(root);
        while (!q.empty()) {
            const auto [weight, link] = q.top();
            q.pop();
            const int p = link.first, c = link.second;
            {
                if (net_of[size_t(c)] >= 0 || chart_tris[size_t(c)].empty())
                    continue;
                const auto it = pair_edge.find({ std::min(p, c), std::max(p, c) });
                if (it == pair_edge.end())
                    continue;
                const ChartEdge &pe  = (p < c) ? it->second.a : it->second.b;
                const ChartEdge &ce  = (p < c) ? it->second.b : it->second.a;
                const Vec2f      pp0 = apply_island_transform(unwrap.uvs[size_t(pe.uv_lo)], p, unwrap, islands);
                const Vec2f      pp1 = apply_island_transform(unwrap.uvs[size_t(pe.uv_hi)], p, unwrap, islands);
                islands[size_t(c)]   = solve_edge_alignment(unwrap.uvs[size_t(ce.uv_lo)], unwrap.uvs[size_t(ce.uv_hi)], pp0,
                                                            pp1, unwrap.chart_centroid[size_t(c)]);

                const Eigen::Matrix<float, 2, 3> m = island_transform_matrix(c, unwrap, islands);
                std::vector<Tri2>                tris;
                tris.reserve(chart_tris[size_t(c)].size());
                bool fits = true;
                for (const int t : chart_tris[size_t(c)]) {
                    tris.push_back(placed(m, t));
                    if (grid.overlaps(tris.back())) {
                        fits = false;
                        break;
                    }
                }
                if (!fits) {
                    islands[size_t(c)] = TextureIsland{};
                    continue;
                }
                for (const Tri2 &t : tris)
                    grid.insert(t);
                net_of[size_t(c)] = net;
                push_neighbours(c);
            }
        }
    }


    std::vector<Vec2f> lo(static_cast<size_t>(net_count), Vec2f::Constant(std::numeric_limits<float>::max()));
    std::vector<Vec2f> hi(static_cast<size_t>(net_count), Vec2f::Constant(std::numeric_limits<float>::lowest()));
    for (int c = 0; c < n; ++c) {
        const int net = net_of[size_t(c)];
        if (net < 0)
            continue;
        const Eigen::Matrix<float, 2, 3> m = island_transform_matrix(c, unwrap, islands);
        for (const int v : chart_verts[size_t(c)]) {
            const Vec2f p   = m.block<2, 2>(0, 0) * unwrap.uvs[size_t(v)] + m.col(2);
            lo[size_t(net)] = lo[size_t(net)].cwiseMin(p);
            hi[size_t(net)] = hi[size_t(net)].cwiseMax(p);
        }
    }
    float total_area = 0.f, widest = 0.f;
    for (int k = 0; k < net_count; ++k) {
        const Vec2f size = hi[size_t(k)] - lo[size_t(k)];
        total_area += size.x() * size.y();
        widest = std::max(widest, size.x());
    }
    const float      shelf_width = std::max(widest, std::sqrt(std::max(total_area, 0.f)) * 1.4f);
    const float      margin      = std::max(shelf_width * 0.02f, 1e-4f);
    std::vector<int> order(static_cast<size_t>(net_count));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return hi[size_t(a)].y() - lo[size_t(a)].y() > hi[size_t(b)].y() - lo[size_t(b)].y();
    });
    std::vector<Vec2f> shift(static_cast<size_t>(net_count), Vec2f::Zero());
    float              cursor_x = 0.f, cursor_y = 0.f, row_height = 0.f;
    for (const int k : order) {
        const Vec2f size = hi[size_t(k)] - lo[size_t(k)];
        if (cursor_x > 0.f && cursor_x + size.x() > shelf_width) {
            cursor_x = 0.f;
            cursor_y += row_height + margin;
            row_height = 0.f;
        }
        shift[size_t(k)] = Vec2f(cursor_x, cursor_y) - lo[size_t(k)];
        cursor_x += size.x() + margin;
        row_height = std::max(row_height, size.y());
    }
    for (int c = 0; c < n; ++c)
        if (net_of[size_t(c)] >= 0)
            islands[size_t(c)].offset += shift[size_t(net_of[size_t(c)])];
    return islands;
}

void average_island_scales(std::vector<TextureIsland> &islands)
{
    if (islands.empty())
        return;
    float sum = 0.f;
    for (const TextureIsland &island : islands)
        sum += island.scale;
    const float mean = sum / float(islands.size());
    for (TextureIsland &island : islands)
        island.scale = mean;
}

std::vector<bool> apply_lscm_uv_overrides(PatchUnwrap &unwrap, const std::vector<std::pair<int, Vec2f>> &overrides)
{
    std::vector<bool> edited(unwrap.uvs.size(), false);
    if (overrides.empty())
        return edited;

    std::map<int, Vec2f> by_mesh_vertex;
    for (const auto &[key, uv] : overrides)
        if (key >= 0)
            by_mesh_vertex[key] = uv;
    if (!by_mesh_vertex.empty())
        for (size_t i = 0; i < unwrap.uvs.size(); ++i)
            if (const auto it = by_mesh_vertex.find(unwrap.source_vertex[i]); it != by_mesh_vertex.end()) {
                unwrap.uvs[i] = it->second;
                edited[i]     = true;
            }
    for (const auto &[key, uv] : overrides)
        if (const int i = -key - 1; key < 0 && size_t(i) < unwrap.uvs.size()) {
            unwrap.uvs[size_t(i)] = uv;
            edited[size_t(i)]     = true;
        }
    return edited;
}

std::vector<Vec2f> compute_lscm_uvs(const indexed_triangle_set &patch, const TextureDisplacementLayer &layer)
{



    const PatchUnwrap unwrap = compute_patch_unwrap(patch, layer.lscm_seam_angle_deg, 0.f, layer.lscm_seam_edges);
    if (unwrap.empty())
        return {};



    PatchUnwrap             edited_unwrap = unwrap;
    const std::vector<bool> edited        = apply_lscm_uv_overrides(edited_unwrap, layer.lscm_uv_overrides);





    std::vector<Vec2f> per_vertex(patch.vertices.size(), Vec2f::Zero());
    std::vector<bool>  assigned(patch.vertices.size(), false);
    for (const bool edited_pass : { true, false })
        for (size_t i = 0; i < edited_unwrap.uvs.size(); ++i) {
            const int pv = edited_unwrap.source_vertex[i];
            if (pv < 0 || assigned[size_t(pv)] || edited[i] != edited_pass)
                continue;
            per_vertex[size_t(pv)] = apply_island_transform(edited_unwrap.uvs[i], edited_unwrap.vertex_chart[i], unwrap, layer.islands);
            assigned[size_t(pv)]   = true;
        }
    return per_vertex;
}

std::vector<Vec2f> compute_lscm_corner_uvs(const indexed_triangle_set &patch, const TextureDisplacementLayer &layer)
{


    const PatchUnwrap unwrap = compute_patch_unwrap(patch, layer.lscm_seam_angle_deg, 0.f, layer.lscm_seam_edges);
    if (unwrap.empty() || unwrap.source_face.size() != unwrap.indices.size())
        return {};

    PatchUnwrap edited_unwrap = unwrap;
    apply_lscm_uv_overrides(edited_unwrap, layer.lscm_uv_overrides);





    std::vector<Vec2f> corner(patch.indices.size() * 3, Vec2f::Zero());
    for (size_t t = 0; t < edited_unwrap.indices.size(); ++t) {
        const int f = edited_unwrap.source_face[t];
        if (f < 0 || size_t(f) >= patch.indices.size())
            continue;
        const stl_triangle_vertex_indices &tri = edited_unwrap.indices[t];
        for (int k = 0; k < 3; ++k) {
            const int uvi = tri[k];
            if (uvi < 0 || size_t(uvi) >= edited_unwrap.uvs.size())
                continue;


            corner[size_t(f) * 3 + size_t(k)] = apply_island_transform(edited_unwrap.uvs[size_t(uvi)],
                                                                       edited_unwrap.vertex_chart[size_t(uvi)],
                                                                       unwrap, layer.islands);
        }
    }
    return corner;
}

namespace {



struct UVTransform
{
    float scale, cs, sn, aspect;
    Vec2f offset;

    UVTransform(const TextureDisplacementLayer &layer, float aspect_)
    {
        scale = (layer.tiling_scale > 1e-6f) ? (1.f / layer.tiling_scale) : 1.f;
        const float rad = layer.rotation_deg * float(M_PI) / 180.f;
        cs     = std::cos(rad);
        sn     = std::sin(rad);
        aspect = aspect_;
        offset = layer.offset;
    }
    Vec2f operator()(const Vec2f &planar) const
    {
        const Vec2f scaled = planar * scale;
        Vec2f       rotated(scaled.x() * cs - scaled.y() * sn, scaled.x() * sn + scaled.y() * cs);

        if (aspect > 0.f && aspect != 1.f)
            rotated.y() *= aspect;
        return rotated + offset;
    }
};



static_assert(TRIPLANAR_BLEND_SHARPNESS == 4.f, "triplanar_weights() hard-codes the fourth power");
inline Vec3f triplanar_weights(const Vec3f &normal)
{
    Vec3f w = normal.cwiseAbs();
    w       = w.cwiseProduct(w);
    return w.cwiseProduct(w);
}
}

Vec2f apply_uv_transform(const Vec2f &planar, const TextureDisplacementLayer &layer, float aspect)
{
    const float scale = (layer.tiling_scale > 1e-6f) ? (1.f / layer.tiling_scale) : 1.f;
    const Vec2f scaled = planar * scale;

    const float rad = layer.rotation_deg * float(M_PI) / 180.f;
    const float cs  = std::cos(rad);
    const float sn  = std::sin(rad);
    Vec2f       rotated(scaled.x() * cs - scaled.y() * sn, scaled.x() * sn + scaled.y() * cs);











    if (aspect > 0.f && aspect != 1.f)
        rotated.y() *= aspect;

    return rotated + layer.offset;
}

float blend_displacement(float accumulated, float value, TextureBlendMode mode)
{
    switch (mode) {
    case TextureBlendMode::Subtract: return accumulated - value;


    case TextureBlendMode::Multiply: return accumulated * value;
    case TextureBlendMode::Divide: {






        constexpr float min_divisor = 0.05f;
        const float     divisor     = (std::abs(value) < min_divisor) ? std::copysign(min_divisor, value < 0.f ? -1.f : 1.f) :
                                                                        value;
        return accumulated / divisor;
    }
    case TextureBlendMode::Add:
    default: return accumulated + value;
    }
}

bool project_uv_projective(const std::array<float, 12> &m, const Vec3f &position, Vec2f &uv)
{
    const float x = position.x(), y = position.y(), z = position.z();
    const float w = m[8] * x + m[9] * y + m[10] * z + m[11];



    if (!(w > 1e-6f))
        return false;
    uv = Vec2f((m[0] * x + m[1] * y + m[2] * z + m[3]) / w, (m[4] * x + m[5] * y + m[6] * z + m[7]) / w);
    return true;
}

float sample_layer_height(const DecodedHeightTexture &texture, const TextureDisplacementLayer &layer,
                          const Vec3f &position, const Vec3f &normal,
                          const Vec3f &patch_center, const Vec3f &patch_axis, const Vec2f *lscm_uv)
{
    if (texture.empty())
        return 0.f;




    const float       aspect = (texture.height > 0) ? float(texture.width) / float(texture.height) : 1.f;
    const UVTransform xf(layer, aspect);
    auto sample_at = [&](const Vec2f &planar) {
        return texture.sample(xf(planar), layer.tile_enabled, layer.tile_method);
    };




    if (lscm_uv != nullptr)
        return sample_at(*lscm_uv);

    switch (layer.projection_method) {
    case TextureProjectionMethod::Cylindrical:
        return sample_at(project_cylindrical(position, patch_center, patch_axis));
    case TextureProjectionMethod::Spherical:
        return sample_at(project_spherical(position, patch_center));
    case TextureProjectionMethod::ViewProjected:
        if (layer.view_project_projective) {




            Vec2f uv;
            if (!project_uv_projective(layer.view_project_matrix, position, uv))
                return 0.f;
            return texture.sample(uv, layer.tile_enabled, layer.tile_method);
        }


        return sample_at(Vec2f(position.dot(layer.view_project_right), position.dot(layer.view_project_up)));
    case TextureProjectionMethod::LSCM:
    case TextureProjectionMethod::Triplanar:
    default: break;
    }






    Vec3f       w     = triplanar_weights(normal);
    const float w_sum = w.x() + w.y() + w.z();
    if (w_sum < 1e-8f)

        return sample_at(Vec2f(position.x(), position.y()));
    w /= w_sum;



    return w.x() * sample_at(Vec2f(position.y(), position.z())) +
           w.y() * sample_at(Vec2f(position.x(), position.z())) +
           w.z() * sample_at(Vec2f(position.x(), position.y()));
}

bool sample_layer_color(const DecodedHeightTexture &texture, const TextureDisplacementLayer &layer,
                        const Vec3f &position, const Vec3f &normal, Vec3f &out, const Vec3f &patch_center,
                        const Vec3f &patch_axis, const Vec2f *lscm_uv)
{
    if (!texture.has_color())
        return false;





    const float       aspect = (texture.height > 0) ? float(texture.width) / float(texture.height) : 1.f;
    const UVTransform xf(layer, aspect);
    auto sample_at = [&](const Vec2f &planar) {
        return texture.sample_color(xf(planar), layer.tile_enabled, layer.tile_method);
    };


    auto covered = [&](const Vec2f &planar) {
        if (layer.tile_enabled)
            return true;
        const Vec2f uv = xf(planar);
        return uv.x() >= 0.f && uv.x() < 1.f && uv.y() >= 0.f && uv.y() < 1.f;
    };

    if (lscm_uv != nullptr) {
        if (!covered(*lscm_uv))
            return false;
        out = sample_at(*lscm_uv);
        return true;
    }

    switch (layer.projection_method) {
    case TextureProjectionMethod::Cylindrical: {
        const Vec2f p = project_cylindrical(position, patch_center, patch_axis);
        if (!covered(p))
            return false;
        out = sample_at(p);
        return true;
    }
    case TextureProjectionMethod::Spherical: {
        const Vec2f p = project_spherical(position, patch_center);
        if (!covered(p))
            return false;
        out = sample_at(p);
        return true;
    }
    case TextureProjectionMethod::ViewProjected:
        if (layer.view_project_projective) {


            Vec2f uv;
            if (!project_uv_projective(layer.view_project_matrix, position, uv))
                return false;
            if (!layer.tile_enabled && (uv.x() < 0.f || uv.x() >= 1.f || uv.y() < 0.f || uv.y() >= 1.f))
                return false;
            out = texture.sample_color(uv, layer.tile_enabled, layer.tile_method);
            return true;
        } else {
            const Vec2f p(position.dot(layer.view_project_right), position.dot(layer.view_project_up));
            if (!covered(p))
                return false;
            out = sample_at(p);
            return true;
        }
    case TextureProjectionMethod::LSCM:
    case TextureProjectionMethod::Triplanar:
    default: break;
    }



    Vec3f w = triplanar_weights(normal);
    const float w_sum = w.x() + w.y() + w.z();
    if (w_sum < 1e-8f) {
        const Vec2f p(position.x(), position.y());
        if (!covered(p))
            return false;
        out = sample_at(p);
        return true;
    }
    w /= w_sum;




    const std::array<Vec2f, 3> planes = { Vec2f(position.y(), position.z()), Vec2f(position.x(), position.z()),
                                          Vec2f(position.x(), position.y()) };
    Vec3f acc     = Vec3f::Zero();
    float acc_w   = 0.f;
    for (int i = 0; i < 3; ++i)
        if (w[i] > 0.f && covered(planes[size_t(i)])) {
            acc   += w[i] * sample_at(planes[size_t(i)]);
            acc_w += w[i];
        }
    if (acc_w <= 0.f)
        return false;
    out = acc / acc_w;
    return true;
}

indexed_triangle_set extract_painted_patch(const indexed_triangle_set                    &base_mesh,
                                            const TriangleSelector::TriangleSplittingData &facet_data)
{
    if (facet_data.triangles_to_split.empty())
        return {};

    const TriangleMesh selector_mesh(base_mesh);
    TriangleSelector    selector(selector_mesh);
    selector.deserialize(facet_data, false);
    return selector.get_facets_strict(EnforcerBlockerType::ENFORCER);
}

bool compute_layer_paint_anchor(const indexed_triangle_set                    &base_mesh,
                                 const TriangleSelector::TriangleSplittingData &facet_data,
                                 Vec3f                                         &anchor_pos,
                                 Vec3f                                         &anchor_normal)
{
    const indexed_triangle_set patch = extract_painted_patch(base_mesh, facet_data);
    if (patch.indices.empty())
        return false;

    Vec3f  centroid_sum = Vec3f::Zero();
    Vec3f  normal_sum   = Vec3f::Zero();
    for (const stl_triangle_vertex_indices &tri : patch.indices) {
        const Vec3f &a = patch.vertices[tri[0]];
        const Vec3f &b = patch.vertices[tri[1]];
        const Vec3f &c = patch.vertices[tri[2]];


        normal_sum   += (b - a).cross(c - a);
        centroid_sum += (a + b + c) / 3.f;
    }

    anchor_pos    = centroid_sum / float(patch.indices.size());
    anchor_normal = (normal_sum.norm() > 1e-8f) ? Vec3f(normal_sum.normalized()) : Vec3f::UnitZ();
    return true;
}





std::vector<Vec3f> texture_displacement_vertex_normals(const indexed_triangle_set &its)
{
    std::vector<Vec3f> normals(its.vertices.size(), Vec3f::Zero());
    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f &a = its.vertices[tri[0]];
        const Vec3f &b = its.vertices[tri[1]];
        const Vec3f &c = its.vertices[tri[2]];


        const Vec3f area_weighted_normal = (b - a).cross(c - a);
        normals[tri[0]] += area_weighted_normal;
        normals[tri[1]] += area_weighted_normal;
        normals[tri[2]] += area_weighted_normal;
    }
    for (Vec3f &n : normals) {
        const float len = n.norm();
        n = (len > 1e-8f) ? Vec3f(n / len) : Vec3f::UnitZ();
    }
    return normals;
}

namespace {



std::vector<float> patch_boundary_distance(const indexed_triangle_set &patch, const std::vector<bool> &is_boundary)
{
    const size_t n = patch.vertices.size();
    std::vector<std::vector<std::pair<int, float>>> adj(n);
    for (const stl_triangle_vertex_indices &tri : patch.indices)
        for (int i = 0; i < 3; ++i) {
            const int a = tri[i], b = tri[(i + 1) % 3];
            if (a < 0 || b < 0 || size_t(a) >= n || size_t(b) >= n)
                continue;
            const float w = (patch.vertices[size_t(a)] - patch.vertices[size_t(b)]).norm();
            adj[size_t(a)].push_back({ b, w });
            adj[size_t(b)].push_back({ a, w });
        }

    std::vector<float> dist(n, std::numeric_limits<float>::infinity());
    using QN = std::pair<float, int>;
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;
    for (size_t v = 0; v < n; ++v)
        if (v < is_boundary.size() && is_boundary[v]) {
            dist[v] = 0.f;
            pq.push({ 0.f, int(v) });
        }
    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[size_t(u)])
            continue;
        for (const auto &[w, ew] : adj[size_t(u)]) {
            const float nd = d + ew;
            if (nd < dist[size_t(w)]) {
                dist[size_t(w)] = nd;
                pq.push({ nd, w });
            }
        }
    }
    return dist;
}
}

namespace {







void despeckle_triangle_colors(const indexed_triangle_set &mesh, std::vector<int> &color, int passes)
{
    if (passes <= 0 || color.size() != mesh.indices.size())
        return;
    const std::vector<Vec3i32> neighbors = its_face_neighbors(mesh);
    if (neighbors.size() != mesh.indices.size())
        return;

    std::vector<int> prev;
    for (int pass = 0; pass < passes; ++pass) {
        prev = color;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, color.size()),
                          [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                if (prev[i] < 0)
                    continue;


                int  cand[4]  = { prev[i], -1, -1, -1 };
                int  count[4] = { 1, 0, 0, 0 };
                int  n        = 1;
                for (int e = 0; e < 3; ++e) {
                    const int nb = neighbors[i][e];
                    if (nb < 0 || size_t(nb) >= prev.size() || prev[size_t(nb)] < 0)
                        continue;
                    const int c = prev[size_t(nb)];
                    int       k = 0;
                    for (; k < n; ++k)
                        if (cand[k] == c) {
                            ++count[k];
                            break;
                        }
                    if (k == n && n < 4) {
                        cand[n]  = c;
                        count[n] = 1;
                        ++n;
                    }
                }

                int best = 0;
                for (int k = 1; k < n; ++k)
                    if (count[k] > count[best])
                        best = k;
                if (count[best] > count[0])
                    color[i] = cand[best];
            }
        });
    }
}
}

void merge_small_color_regions(const indexed_triangle_set &mesh, std::vector<int> &color, float min_area_mm2)
{
    const size_t n = mesh.indices.size();
    if (min_area_mm2 <= 0.f || color.size() != n)
        return;
    const std::vector<Vec3i32> neighbors = its_face_neighbors(mesh);
    if (neighbors.size() != n)
        return;

    const auto edge_length = [&mesh](size_t f, int e) {
        const stl_triangle_vertex_indices &t = mesh.indices[f];
        return (mesh.vertices[size_t(t[(e + 1) % 3])] - mesh.vertices[size_t(t[e])]).norm();
    };




    std::vector<int>    component(n, -1);
    std::vector<int>    faces;
    std::vector<size_t> start;
    std::vector<float>  area;
    std::vector<int>    stack;
    faces.reserve(n);
    for (size_t seed = 0; seed < n; ++seed) {
        if (color[seed] < 0 || component[seed] >= 0)
            continue;
        const int c  = color[seed];
        const int id = int(area.size());
        start.push_back(faces.size());
        area.push_back(0.f);
        component[seed] = id;
        stack.push_back(int(seed));
        while (!stack.empty()) {
            const size_t f = size_t(stack.back());
            stack.pop_back();
            faces.push_back(int(f));
            const stl_triangle_vertex_indices &t = mesh.indices[f];
            const Vec3f &a = mesh.vertices[size_t(t[0])], &b = mesh.vertices[size_t(t[1])], &cv = mesh.vertices[size_t(t[2])];
            area[size_t(id)] += 0.5f * (b - a).cross(cv - a).norm();
            for (int e = 0; e < 3; ++e) {
                const int nb = neighbors[f][e];
                if (nb < 0 || size_t(nb) >= n || component[size_t(nb)] >= 0 || color[size_t(nb)] != c)
                    continue;
                component[size_t(nb)] = id;
                stack.push_back(nb);
            }
        }
    }
    start.push_back(faces.size());




    std::vector<int> order;
    for (int id = 0; id < int(area.size()); ++id)
        if (area[size_t(id)] < min_area_mm2)
            order.push_back(id);
    std::sort(order.begin(), order.end(), [&area](int l, int r) { return area[size_t(l)] < area[size_t(r)]; });

    std::vector<std::pair<int, float>> weights;
    for (const int id : order) {
        const size_t begin = start[size_t(id)], end = start[size_t(id) + 1];
        const int    own   = color[size_t(faces[begin])];
        weights.clear();
        for (size_t k = begin; k < end; ++k) {
            const size_t f = size_t(faces[k]);
            for (int e = 0; e < 3; ++e) {
                const int nb = neighbors[f][e];
                if (nb < 0 || size_t(nb) >= n)
                    continue;
                const int c = color[size_t(nb)];
                if (c < 0 || c == own)
                    continue;
                const float len = edge_length(f, e);
                auto it = std::find_if(weights.begin(), weights.end(), [c](const std::pair<int, float> &w) { return w.first == c; });
                if (it == weights.end())
                    weights.emplace_back(c, len);
                else
                    it->second += len;
            }
        }
        if (weights.empty())
            continue;
        const int target = std::max_element(weights.begin(), weights.end(),
                                            [](const std::pair<int, float> &l, const std::pair<int, float> &r) {
                                                return l.second < r.second;
                                            })->first;
        for (size_t k = begin; k < end; ++k)
            color[size_t(faces[k])] = target;
    }
}

namespace {



indexed_triangle_set build_texture_displacement_v2(const indexed_triangle_set                  &mesh,
                                                   const std::vector<TextureDisplacementLayer> &layers,
                                                   const TextureDisplacementFacetsData         &facets_data,
                                                   const TextureDisplacementOptions            &options,
                                                   const DisplacementProgressFn                &progress,
                                                   const TextureColorRequest                   *color,
                                                   bool                                         flip_normals,
                                                   BakeStageRecorder                           *debug,
                                                   TextureBakeStats                            *stats)
{
    HeightFieldSampler combined = make_combined_displacement_sampler(mesh, layers, facets_data);
    if (!combined)
        return mesh;






    std::vector<uint8_t> excluded(mesh.indices.size(), 1);
    indexed_triangle_set painted_pieces;
    {
        const TriangleMesh selector_mesh(mesh);
        TriangleSelector   selector(selector_mesh);
        bool               dirty = false;
        for (const TriangleSelector::TriangleSplittingData &data : facets_data) {
            if (data.triangles_to_split.empty())
                continue;
            selector.deserialize(data, dirty);
            dirty = true;
            std::vector<int> piece_src;
            const indexed_triangle_set patch =
                selector.get_facets_strict(EnforcerBlockerType::ENFORCER, &piece_src);
            for (const int src : piece_src)
                if (src >= 0 && size_t(src) < excluded.size())
                    excluded[size_t(src)] = 0;


            std::vector<int>           unused;
            const indexed_triangle_set compact = compact_patch_with_map(patch, unused);
            const int                  offset  = int(painted_pieces.vertices.size());
            painted_pieces.vertices.insert(painted_pieces.vertices.end(), compact.vertices.begin(), compact.vertices.end());
            for (const stl_triangle_vertex_indices &t : compact.indices)
                painted_pieces.indices.emplace_back(t[0] + offset, t[1] + offset, t[2] + offset);
        }
    }
    if (std::all_of(excluded.begin(), excluded.end(), [](uint8_t e) { return e != 0; }) || painted_pieces.indices.empty())
        return mesh;



    const AABBTreeIndirect::Tree3f painted_tree =
        AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(painted_pieces.vertices, painted_pieces.indices);



    const auto painted_closest = [&painted_pieces, &painted_tree](const Vec3f &p, Vec3f *foot, Vec3f *normal) {
        size_t      hit = 0;
        Vec3f       hit_point;
        const float d2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
            painted_pieces.vertices, painted_pieces.indices, painted_tree, p, hit, hit_point);
        if (foot != nullptr)
            *foot = hit_point;
        if (normal != nullptr && hit < painted_pieces.indices.size()) {
            const stl_triangle_vertex_indices &t = painted_pieces.indices[hit];
            const Vec3f &a = painted_pieces.vertices[size_t(t[0])], &b = painted_pieces.vertices[size_t(t[1])],
                        &c = painted_pieces.vertices[size_t(t[2])];
            Vec3f       n = (b - a).cross(c - a);
            const float l = n.norm();
            *normal = (l > 0.f) ? Vec3f(n / l) : Vec3f::UnitZ();
        }
        return d2;
    };
    const auto painted_dist2 = [&painted_closest](const Vec3f &p) { return painted_closest(p, nullptr, nullptr); };


    constexpr float paint_tol = 0.05f;
    const auto      painted_at = [&painted_dist2](const Vec3f &p) { return painted_dist2(p) < paint_tol * paint_tol; };



    const bool         auto_edge = options.v2_refine_mm <= 0.f, auto_budget = options.v2_max_triangles_k < 0;
    const V2Resolution rec = (auto_edge || auto_budget) ? recommend_v2_resolution(mesh, layers) : V2Resolution{};
    TextureBake::PipelineSettings settings;
    settings.refine_length = auto_edge ? std::max(0.05, double(rec.edge_mm)) : std::max(0.01f, options.v2_refine_mm);
    settings.regularize    = options.v2_regularize;
    settings.max_triangles = size_t(auto_budget ? std::max(0, rec.budget_k) : options.v2_max_triangles_k) * 1000;
    BOOST_LOG_TRIVIAL(info) << "TextureBake resolution: " << settings.refine_length << " mm"
                            << (auto_edge ? " (auto)" : "") << ", budget " << settings.max_triangles / 1000 << " k"
                            << (auto_budget ? " (auto)" : "");





    settings.harvest_tol = std::clamp(settings.refine_length / 32.0, 0.005, 0.05);
    settings.preserve_untextured = true;



    settings.clamp_below_plate   = true;
    settings.relocate            = options.v2_relocate;
    settings.flip_edges          = options.v2_flip_edges;

    settings.displace.amplitude = 1.f;
    settings.displace.symmetric = false;

    settings.displace.bottom_angle_limit = 0.f;
    settings.displace.top_angle_limit    = 0.f;





    settings.displace.blend_normal_smoothing = 32;





    {
        const auto area_of = [](const indexed_triangle_set &its) {
            double a = 0.0;
            for (const stl_triangle_vertex_indices &t : its.indices)
                a += 0.5 * double((its.vertices[size_t(t[1])] - its.vertices[size_t(t[0])])
                                      .cross(its.vertices[size_t(t[2])] - its.vertices[size_t(t[0])]).norm());
            return a;
        };
        double included_area = 0.0;
        for (size_t t = 0; t < mesh.indices.size(); ++t)
            if (excluded[t] == 0) {
                const stl_triangle_vertex_indices &f = mesh.indices[t];
                included_area += 0.5 * double((mesh.vertices[size_t(f[1])] - mesh.vertices[size_t(f[0])])
                                                  .cross(mesh.vertices[size_t(f[2])] - mesh.vertices[size_t(f[0])]).norm());
            }
        const double pieces_area = area_of(painted_pieces);
        if (pieces_area < included_area * (1.0 - 1e-4))
            settings.painted = painted_at;
    }


    const bool all_painted = std::none_of(excluded.begin(), excluded.end(), [](uint8_t e) { return e != 0; });
    if (settings.painted || !all_painted)
        settings.paint_within = [&painted_pieces, &painted_tree](const Vec3d &p, double radius) {
            float r2 = float(radius * radius);
            return AABBTreeIndirect::is_any_triangle_in_radius(painted_pieces.vertices, painted_pieces.indices,
                                                               painted_tree, Vec3f(p.cast<float>()), r2);
        };

    TextureBake::DisplaceBounds bounds;
    bounds.min = bounds.max = mesh.vertices.empty() ? Vec3f::Zero() : mesh.vertices.front();
    for (const Vec3f &v : mesh.vertices) {
        bounds.min = bounds.min.cwiseMin(v);
        bounds.max = bounds.max.cwiseMax(v);
    }

    const auto sample = [&combined](const Vec3f &pos, const Vec3f &, const Vec3f &blend_normal) {


        return combined(pos, blend_normal);
    };






    indexed_triangle_set oriented = mesh;
    if (flip_normals)
        for (stl_triangle_vertex_indices &t : oriented.indices)
            std::swap(t[1], t[2]);


    const TextureBake::PipelineMode mode = settings.max_triangles > 0 ? TextureBake::PipelineMode::Export
                                                                      : TextureBake::PipelineMode::Bake;



    const bool              want_color = color != nullptr && color->out_triangle != nullptr && bool(color->quantize);
    const ColorFieldSampler color_sampler =
        want_color ? make_combined_color_sampler(mesh, layers, facets_data, color->quantize, color->quantize_pure) : ColorFieldSampler{};








    const TextureBake::ColorSampleFn color_sample =
        color_sampler ? TextureBake::ColorSampleFn([&color_sampler](const Vec3f &p, const Vec3f &n) {
                            return color_sampler(p, n);
                        })
                      : TextureBake::ColorSampleFn{};



    const size_t                debug_mark = (debug != nullptr) ? debug->mark() : 0;
    TextureBake::PipelineResult result     = TextureBake::run_pipeline(
        TextureBake::to_soup(oriented, excluded), sample, settings, bounds, mode, excluded,
        [&progress](const char *, double f) {
            return !progress || progress(std::clamp(int(f * 100.0), 0, 99));
        },
        debug, color_sample);
    if (debug != nullptr && flip_normals)
        debug->rebase(debug_mark, nullptr,   true);
    if (result.canceled || result.geometry.empty())
        return {};

    if (stats != nullptr) {
        stats->triangles_refined = result.triangles_refined;
        stats->triangles_budget  = result.triangles_budget;
        stats->budget_limited    = result.budget_limited;
    }
    indexed_triangle_set out = TextureBake::to_indexed_triangle_set(result.geometry, &result.face_color);
    if (out.indices.empty())
        return mesh;
    if (flip_normals)
        for (stl_triangle_vertex_indices &t : out.indices)
            std::swap(t[1], t[2]);






    if (want_color) {
        std::vector<uint8_t>     out_color(out.indices.size(), 0);
        const ColorFieldSampler &sampler = color_sampler;
        if (sampler) {
            float max_depth = 0.f;
            for (const TextureDisplacementLayer &layer : layers)
                max_depth = std::max(max_depth, std::abs(layer.depth_mm));
            const float relief_tol = max_depth + paint_tol;
            std::vector<int> palette(out.indices.size(), -1);
            const std::vector<int> &face_mask      = result.face_color;
            const bool              have_face_mask = face_mask.size() == out.indices.size();
            tbb::parallel_for(tbb::blocked_range<size_t>(0, out.indices.size()), [&](const tbb::blocked_range<size_t> &r) {
                for (size_t i = r.begin(); i < r.end(); ++i) {
                    const stl_triangle_vertex_indices &t = out.indices[i];
                    const Vec3f &a = out.vertices[size_t(t[0])], &b = out.vertices[size_t(t[1])], &c = out.vertices[size_t(t[2])];
                    const Vec3f  centroid = (a + b + c) / 3.f;








                    Vec3f       foot = centroid, base_n = Vec3f::UnitZ();
                    const float d2   = painted_closest(centroid, &foot, &base_n);







                    if (have_face_mask) {
                        if (face_mask[i] == TextureBake::FACE_UNPAINTED)
                            continue;
                    } else if (!all_painted && d2 >= relief_tol * relief_tol) {
                        continue;
                    }
                    palette[i] = sampler(foot, base_n);
                }
            });






            const bool simplified = result.simplified;
            despeckle_triangle_colors(out, palette, simplified ? 0 : color->despeckle_passes);
            merge_small_color_regions(out, palette, color->min_color_region_mm2);
            for (size_t i = 0; i < out.indices.size(); ++i) {
                if (palette[i] < 0)
                    continue;
                const stl_triangle_vertex_indices &t = out.indices[i];
                const Vec3f &a = out.vertices[size_t(t[0])], &b = out.vertices[size_t(t[1])], &c = out.vertices[size_t(t[2])];
                const Vec3f  centroid = (a + b + c) / 3.f;
                Vec3f        normal   = (b - a).cross(c - a);
                const float  nl       = normal.norm();
                normal                = (nl > 0.f) ? Vec3f(normal / nl) : Vec3f::UnitZ();
                const int filament = palette[i];
                if (filament >= 0)
                    out_color[i] = uint8_t(std::min(filament + 1, 255));
            }
        }
        *color->out_triangle = std::move(out_color);
    }
    return out;
}

}






static indexed_triangle_set build_texture_displacement_in_place(
    const indexed_triangle_set                  &base_mesh,
    const std::vector<TextureDisplacementLayer> &layers,
    const TextureDisplacementFacetsData         &facets_data,
    const TextureDisplacementOptions            &options,
    const DisplacementProgressFn                &progress,
    const TextureColorRequest                   *color,
    bool                                         flip_normals,
    BakeStageRecorder                           *debug,
    TextureBakeStats                            *stats)
{

    (void) stats;


    const auto report = [&progress](int percent) { return !progress || progress(percent); };



    auto       stage_clock = std::chrono::steady_clock::now();
    const auto capture     = [&](const char *name, const indexed_triangle_set &m,
                             const std::string &detail = {}) {
        if (debug == nullptr)
            return;
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stage_clock).count();
        debug->capture(name, m.vertices, m.indices, ms, detail);
        stage_clock = std::chrono::steady_clock::now();
    };

    indexed_triangle_set mesh = base_mesh;







    its_compactify_vertices(mesh);
    if (mesh.vertices.empty() || mesh.indices.empty())
        return mesh;

    capture("input", mesh, "as handed to the bake");

    if (options.pipeline_v2)
        return build_texture_displacement_v2(mesh, layers, facets_data, options, progress, color, flip_normals,
                                             debug, stats);



    std::vector<const TextureDisplacementLayer *> ordered_layers;
    for (const TextureDisplacementLayer &layer : layers)
        if (!layer.empty() && layer.slot >= 0 && size_t(layer.slot) < TEXTURE_DISPLACEMENT_MAX_LAYERS)
            ordered_layers.push_back(&layer);
    std::sort(ordered_layers.begin(), ordered_layers.end(),
               [](const TextureDisplacementLayer *a, const TextureDisplacementLayer *b) { return a->slot < b->slot; });





    std::vector<Vec3f> vertex_normals = texture_displacement_vertex_normals(mesh);










    {
        std::vector<uint8_t> painted_face(mesh.indices.size(), 0);
        bool                 any_paint = false;
        for (const TextureDisplacementLayer *layer : ordered_layers)
            for (const TriangleSelector::TriangleBitStreamMapping &m : facets_data[size_t(layer->slot)].triangles_to_split)
                if (size_t(m.triangle_idx) < mesh.indices.size()) {
                    painted_face[size_t(m.triangle_idx)] = 1;
                    any_paint                            = true;
                }
        if (any_paint) {
            std::vector<Vec3f> painted_normals(mesh.vertices.size(), Vec3f::Zero());
            for (size_t i = 0; i < mesh.indices.size(); ++i) {
                if (!painted_face[i])
                    continue;
                const stl_triangle_vertex_indices &t = mesh.indices[i];
                const Vec3f fn = (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]);
                for (int k = 0; k < 3; ++k)
                    painted_normals[size_t(t[k])] += fn;
            }
            for (size_t v = 0; v < vertex_normals.size(); ++v)
                if (const float l = painted_normals[v].norm(); l > 1e-8f)
                    vertex_normals[v] = painted_normals[v] / l;


        }
    }





    if (flip_normals)
        for (Vec3f &n : vertex_normals)
            n = -n;

    if (!report(5))
        return {};

    std::vector<float> displacement(mesh.vertices.size(), 0.f);



    std::vector<uint8_t> displaced(mesh.vertices.size(), 0);




    std::vector<bool>  on_patch_border(mesh.vertices.size(), false);
    bool               any_displacement = false;






    const bool       want_color = color != nullptr && color->out_triangle != nullptr && bool(color->quantize);



    std::vector<int> triangle_palette;
    if (want_color)
        triangle_palette.assign(mesh.indices.size(), -1);

    const TriangleMesh selector_mesh(mesh);





    TriangleSelector selector(selector_mesh);
    bool             selector_dirty = false;

    const int layer_count = std::max(int(ordered_layers.size()), 1);
    int       layer_index = 0;
    for (const TextureDisplacementLayer *layer : ordered_layers) {

        if (!report(5 + (60 * layer_index++) / layer_count))
            return {};

        const TriangleSelector::TriangleSplittingData &data = facets_data[size_t(layer->slot)];
        if (data.triangles_to_split.empty())
            continue;

        const DecodedHeightTexture height = decode_height_texture(*layer);
        if (height.empty())
            continue;


        selector.deserialize(data, selector_dirty);
        selector_dirty = true;

        const bool       color_this_layer = want_color && layer->color_enabled;

        const ColorQuantizeFn &layer_quantize =
            (color_this_layer && color->quantize_pure && analyze_texture_detail(*layer).flat_colors) ? color->quantize_pure
                                                                                                     : color->quantize;
        std::vector<int> patch_source;
        const indexed_triangle_set patch =
            selector.get_facets_strict(EnforcerBlockerType::ENFORCER, color_this_layer ? &patch_source : nullptr);
        if (patch.indices.empty())
            continue;



        const indexed_triangle_set rest = selector.get_facets_strict(EnforcerBlockerType::NONE);






        std::vector<bool> is_boundary(patch.vertices.size(), false);
        for (const stl_triangle_vertex_indices &tri : rest.indices)
            for (int i = 0; i < 3; ++i) {
                is_boundary[tri[i]] = true;
                if (tri[i] < int(mesh.vertices.size()))
                    on_patch_border[size_t(tri[i])] = true;
            }
        const bool pin_boundary = !options.displace_border;




        Vec3f average_normal, patch_centroid, patch_axis;
        texture_displacement_patch_frame(patch, vertex_normals, patch_centroid, patch_axis, average_normal);




        const std::vector<Vec2f> lscm_uvs = (layer->projection_method == TextureProjectionMethod::LSCM) ?
                                                 compute_lscm_uvs(patch, *layer) :
                                                 std::vector<Vec2f>{};




        const std::vector<Vec2f> lscm_corner_uvs = (layer->projection_method == TextureProjectionMethod::LSCM) ?
                                                        compute_lscm_corner_uvs(patch, *layer) :
                                                        std::vector<Vec2f>{};
        const bool               corner_uv_ok    = !lscm_corner_uvs.empty() &&
                                                   lscm_corner_uvs.size() == patch.indices.size() * 3;






        if (color_this_layer) {
            const DecodedHeightTexture &tex = height;
            if (tex.has_color()) {
                std::vector<Vec3f> sum(mesh.indices.size(), Vec3f::Zero());
                std::vector<float> sum_area(mesh.indices.size(), 0.f);
                for (size_t j = 0; j < patch.indices.size() && j < patch_source.size(); ++j) {
                    const size_t S = size_t(patch_source[j]);
                    if (S >= mesh.indices.size())
                        continue;
                    const stl_triangle_vertex_indices &t = patch.indices[j];
                    const Vec3f &pa = patch.vertices[size_t(t[0])];
                    const Vec3f &pb = patch.vertices[size_t(t[1])];
                    const Vec3f &pc = patch.vertices[size_t(t[2])];
                    const float  area2 = (pb - pa).cross(pc - pa).norm();
                    if (area2 <= 0.f)
                        continue;
                    const Vec3f centroid = (pa + pb + pc) / 3.f;




                    Vec3f n = Vec3f::Zero();
                    Vec2f uv = Vec2f::Zero();
                    bool  have_uv = !lscm_uvs.empty();
                    for (int k = 0; k < 3; ++k) {
                        const int vi = t[k];
                        if (vi < int(vertex_normals.size()))
                            n += vertex_normals[size_t(vi)];
                        if (!have_uv)
                            continue;
                        if (corner_uv_ok)
                            uv += lscm_corner_uvs[j * 3 + size_t(k)];
                        else if (size_t(vi) < lscm_uvs.size())
                            uv += lscm_uvs[size_t(vi)];
                        else
                            have_uv = false;
                    }
                    n = (n.norm() > 1e-8f) ? Vec3f(n.normalized()) : average_normal;
                    uv /= 3.f;

                    Vec3f rgb;
                    if (sample_layer_color(tex, *layer, centroid, n, rgb, patch_centroid, patch_axis,
                                           have_uv ? &uv : nullptr)) {
                        sum[S]      += area2 * rgb;
                        sum_area[S] += area2;
                    }
                }
                for (size_t i = 0; i < mesh.indices.size(); ++i)
                    if (sum_area[i] > 0.f) {
                        const int idx = layer_quantize(sum[i] / sum_area[i]);


                        if (idx >= 0)
                            triangle_palette[i] = idx;
                    }
            }
        }




        std::vector<float> edge_weight;
        if (layer->edge_smoothing && layer->edge_smoothing_amount > 0.f) {
            const std::vector<float> bdist = patch_boundary_distance(patch, is_boundary);
            float max_d = 0.f;
            for (const float d : bdist)
                if (std::isfinite(d))
                    max_d = std::max(max_d, d);
            edge_weight.assign(patch.vertices.size(), 1.f);
            if (max_d > 1e-6f) {
                const float a = std::clamp(layer->edge_smoothing_amount, 0.f, 1.f);
                const float k = (a < 1.f) ? (1.f - a) / std::max(a, 1e-3f) : 0.f;
                for (size_t v = 0; v < bdist.size() && v < edge_weight.size(); ++v) {
                    const float d = std::isfinite(bdist[v]) ? bdist[v] : max_d;
                    edge_weight[v] = std::clamp((d / max_d) * k, 0.f, 1.f);
                }
            }
        }

        const float sign = layer->invert ? -1.f : 1.f;






        std::vector<int>  layer_vertices;
        std::vector<char> visited(patch.vertices.size(), 0);
        layer_vertices.reserve(patch.vertices.size());
        for (const stl_triangle_vertex_indices &tri : patch.indices)
            for (int i = 0; i < 3; ++i) {
                const int vi = tri[i];


                if (vi >= int(mesh.vertices.size()) || (pin_boundary && is_boundary[vi]) || visited[vi])
                    continue;
                visited[vi] = 1;
                layer_vertices.push_back(vi);
            }
        if (layer_vertices.empty())
            continue;

        std::vector<float> sampled(layer_vertices.size(), 0.f);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layer_vertices.size()),
                          [&](const tbb::blocked_range<size_t> &range) {
            for (size_t k = range.begin(); k < range.end(); ++k) {
                const size_t vi      = size_t(layer_vertices[k]);
                const Vec2f *lscm_uv = lscm_uvs.empty() ? nullptr : &lscm_uvs[vi];
                const float  h       = sample_layer_height(height, *layer, mesh.vertices[vi], vertex_normals[vi],
                                                            patch_centroid, patch_axis, lscm_uv);



                sampled[k] = (h - layer->midlevel) * layer->depth_mm * sign;
            }
        });

        for (size_t k = 0; k < layer_vertices.size(); ++k) {
            const size_t vi = size_t(layer_vertices[k]);




            const float accumulated = displacement[vi];
            const float blended     = blend_displacement(accumulated, sampled[k],
                                                          displaced[vi] ? layer->blend_mode : TextureBlendMode::Add);







            const float edge_w = edge_weight.empty() ? 1.f : edge_weight[vi];
            displacement[vi]   = accumulated + (blended - accumulated) * edge_w;
            displaced[vi]      = 1;
        }
        any_displacement = true;
    }

    if (!report(65))
        return {};

    if (!any_displacement)
        return mesh;


    float resting_z = std::numeric_limits<float>::max();
    for (const Vec3f &v : mesh.vertices)
        resting_z = std::min(resting_z, v.z());

    size_t moved_count = 0;
    for (size_t vi = 0; vi < mesh.vertices.size(); ++vi)
        if (displaced[vi]) {
            mesh.vertices[vi] += vertex_normals[vi] * displacement[vi];
            ++moved_count;
        }
    capture("displace", mesh, std::to_string(moved_count) + " of " +
                                  std::to_string(mesh.vertices.size()) + " vertices moved");







    if (options.smooth_enabled && options.smooth_strength > 0.f && options.smooth_iterations > 0) {
        if (!report(70))
            return {};
        std::vector<uint8_t> movable(mesh.vertices.size(), 0);
        for (size_t vi = 0; vi < mesh.vertices.size(); ++vi)
            movable[vi] = (displaced[vi] && !(options.smooth_skip_border && on_patch_border[vi])) ? 1 : 0;



        smooth_mesh_vertices(mesh, movable, options.smooth_strength, options.smooth_iterations,
                             progress ? DisplacementProgressFn([&report, it = options.smooth_iterations](int pass) {
                                 return report(70 + (29 * (pass + 1)) / std::max(it, 1));
                             }) : DisplacementProgressFn{});
        capture("smooth", mesh,
                std::to_string(options.smooth_iterations) + " iterations at strength " +
                    std::to_string(options.smooth_strength));
    }








    size_t clamped_count = 0;
    if (resting_z < std::numeric_limits<float>::max())
        for (size_t vi = 0; vi < mesh.vertices.size(); ++vi)
            if (displaced[vi] && mesh.vertices[vi].z() < resting_z) {
                mesh.vertices[vi].z() = resting_z;
                ++clamped_count;
            }
    capture("clamp to plate", mesh, std::to_string(clamped_count) + " vertices raised to z = " +
                                        std::to_string(resting_z));

    if (!report(99))
        return {};
    if (want_color) {




        despeckle_triangle_colors(mesh, triangle_palette, color->despeckle_passes);
        merge_small_color_regions(mesh, triangle_palette, color->min_color_region_mm2);

        std::vector<uint8_t> out_color(mesh.indices.size(), 0);
        for (size_t i = 0; i < mesh.indices.size(); ++i) {
            if (triangle_palette[i] < 0)
                continue;
            const stl_triangle_vertex_indices &t = mesh.indices[i];
            const Vec3f &a = mesh.vertices[size_t(t[0])], &b = mesh.vertices[size_t(t[1])], &c = mesh.vertices[size_t(t[2])];
            const Vec3f  centroid = (a + b + c) / 3.f;
            Vec3f        normal   = (b - a).cross(c - a);
            const float  nl       = normal.norm();
            normal                = (nl > 0.f) ? Vec3f(normal / nl) : Vec3f::UnitZ();
            const int filament = triangle_palette[i];
            if (filament >= 0)
                out_color[i] = uint8_t(std::min(filament + 1, 255));
        }


        *color->out_triangle = std::move(out_color);
    }
    return mesh;
}

indexed_triangle_set build_texture_displacement(const indexed_triangle_set                  &base_mesh,
                                                 const std::vector<TextureDisplacementLayer> &layers,
                                                 const TextureDisplacementFacetsData         &facets_data,
                                                 const TextureDisplacementOptions            &options,
                                                 const DisplacementProgressFn                &progress,
                                                 const TextureColorRequest                   *color,
                                                 const Transform3d                           &volume_to_world,
                                                 BakeStageRecorder                           *debug,
                                                 TextureBakeStats                            *stats)
{





    const Transform3d frame = texture_displacement_bake_frame(volume_to_world);
    if (frame.matrix().isApprox(Transform3d::Identity().matrix()))
        return build_texture_displacement_in_place(base_mesh, layers, facets_data, options, progress, color,
                                                   false, debug, stats);

    const Transform3d to_local = frame.inverse();




    const bool mirrored = frame.linear().determinant() < 0.0;

    indexed_triangle_set world = base_mesh;
    for (Vec3f &v : world.vertices)
        v = (frame * v.cast<double>()).cast<float>();

    const size_t         debug_mark = (debug != nullptr) ? debug->mark() : 0;
    indexed_triangle_set out = build_texture_displacement_in_place(world, layers, facets_data, options,
                                                                   progress, color, mirrored, debug, stats);


    if (debug != nullptr)
        debug->rebase(debug_mark, &to_local,   false);


    if (out.vertices.empty())
        return out;

    for (Vec3f &v : out.vertices)
        v = (to_local * v.cast<double>()).cast<float>();
    return out;
}

void texture_displacement_patch_frame(const indexed_triangle_set &patch, const std::vector<Vec3f> &vertex_normals,
                                      Vec3f &center, Vec3f &axis, Vec3f &average_normal)
{
    Vec3f normal_sum   = Vec3f::Zero();
    Vec3f centroid_sum = Vec3f::Zero();
    int   count        = 0;
    for (const stl_triangle_vertex_indices &tri : patch.indices)
        for (int i = 0; i < 3; ++i) {
            const int vi = tri[i];
            centroid_sum += patch.vertices[size_t(vi)];
            ++count;


            if (vi < int(vertex_normals.size()))
                normal_sum += vertex_normals[size_t(vi)];
        }
    average_normal = (normal_sum.norm() > 1e-8f) ? Vec3f(normal_sum.normalized()) : Vec3f::UnitZ();
    center         = (count > 0) ? Vec3f(centroid_sum / float(count)) : Vec3f::Zero();
    if (count == 0) {


        axis = Vec3f::UnitZ();
        return;
    }



    const Vec3f an = average_normal.cwiseAbs();
    axis = (an.x() <= an.y() && an.x() <= an.z()) ? Vec3f::UnitX() :
           (an.y() <= an.x() && an.y() <= an.z()) ? Vec3f::UnitY() : Vec3f::UnitZ();
}

Transform3d texture_displacement_bake_frame(const Transform3d &volume_to_world)
{




    return Eigen::Translation3d(-volume_to_world.translation()) * volume_to_world;
}

Transform3d texture_displacement_volume_to_world(const ModelVolume &volume)
{
    const ModelObject *object = volume.get_object();
    if (object == nullptr || object->instances.empty() || object->instances.front() == nullptr)
        return volume.get_matrix();
    return object->instances.front()->get_matrix() * volume.get_matrix();
}

indexed_triangle_set build_texture_displacement(const ModelVolume &volume)
{
    TextureDisplacementFacetsData facets_data;
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        facets_data[size_t(i)] = volume.texture_displacement_facet(i).get_data();

    return build_texture_displacement(volume.mesh().its, volume.texture_displacement_layers, facets_data,
                                      volume.texture_displacement_options, {}, nullptr,
                                      texture_displacement_volume_to_world(volume));
}

void smooth_mesh_vertices(indexed_triangle_set &mesh, const std::vector<uint8_t> &movable, float strength,
                          int iterations, const DisplacementProgressFn &on_pass)
{
    if (iterations <= 0 || mesh.vertices.empty() || movable.size() != mesh.vertices.size())
        return;
    strength = std::clamp(strength, 0.f, 1.f);
    if (strength <= 0.f)
        return;
    if (std::none_of(movable.begin(), movable.end(), [](uint8_t m) { return m != 0; }))
        return;





    const size_t       nv = mesh.vertices.size();
    std::vector<int>   start(nv + 1, 0);
    for (const stl_triangle_vertex_indices &t : mesh.indices)
        for (int e = 0; e < 3; ++e) {
            ++start[size_t(t[e]) + 1];
            ++start[size_t(t[(e + 1) % 3]) + 1];
        }
    for (size_t v = 0; v < nv; ++v)
        start[v + 1] += start[v];
    const size_t     total_refs = size_t(start[nv]);
    std::vector<int> nbr(total_refs, 0);
    std::vector<int> fill(start.begin(), start.begin() + nv);
    for (const stl_triangle_vertex_indices &t : mesh.indices)
        for (int e = 0; e < 3; ++e) {
            const int a = t[e], b = t[(e + 1) % 3];
            nbr[size_t(fill[size_t(a)]++)] = b;
            nbr[size_t(fill[size_t(b)]++)] = a;
        }




    std::vector<Vec3f> prev;
    for (int it = 0; it < iterations; ++it) {
        prev = mesh.vertices;


        tbb::parallel_for(tbb::blocked_range<size_t>(0, nv), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t v = range.begin(); v < range.end(); ++v) {
                if (!movable[v] || start[v] == start[v + 1])
                    continue;
                Vec3f sum = Vec3f::Zero();
                for (int k = start[v]; k < start[v + 1]; ++k)
                    sum += prev[size_t(nbr[size_t(k)])];
                const Vec3f avg = sum / float(start[v + 1] - start[v]);
                mesh.vertices[v] = prev[v] + (avg - prev[v]) * strength;
            }
        });
        if (on_pass && !on_pass(it))
            return;
    }
}

namespace {








struct LscmLookup {
    indexed_triangle_set    patch;
    AABBTreeIndirect::Tree3f tree;
    std::vector<Vec2f>      corner;



    bool uv_at(const Vec3f &pos, float tol, Vec2f &uv) const
    {
        size_t      hit = 0;
        Vec3f       foot;
        const float d2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(patch.vertices, patch.indices, tree,
                                                                                    pos, hit, foot);
        if (d2 < 0.f || d2 > tol * tol || hit >= patch.indices.size())
            return false;
        const stl_triangle_vertex_indices &t = patch.indices[hit];
        const Vec3f &a = patch.vertices[size_t(t[0])], &b = patch.vertices[size_t(t[1])], &c = patch.vertices[size_t(t[2])];
        const Vec3f  e0 = b - a, e1 = c - a, ep = foot - a;
        const float  d00 = e0.dot(e0), d01 = e0.dot(e1), d11 = e1.dot(e1), dp0 = ep.dot(e0), dp1 = ep.dot(e1);
        const float  den = d00 * d11 - d01 * d01;
        float        w1 = 1.f / 3.f, w2 = 1.f / 3.f;
        if (std::abs(den) > 1e-20f) {
            w1 = (d11 * dp0 - d01 * dp1) / den;
            w2 = (d00 * dp1 - d01 * dp0) / den;
        }
        const Vec2f *c3 = &corner[hit * 3];
        uv = (1.f - w1 - w2) * c3[0] + w1 * c3[1] + w2 * c3[2];
        return true;
    }
};




struct PatchRegion {
    indexed_triangle_set     patch;
    AABBTreeIndirect::Tree3f tree;

    bool contains(const Vec3f &pos, float tol) const
    {
        size_t      hit = 0;
        Vec3f       foot;
        const float d2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(patch.vertices, patch.indices, tree,
                                                                                    pos, hit, foot);
        return d2 >= 0.f && d2 <= tol * tol;
    }
};

struct PreparedLayer {
    DecodedHeightTexture     tex;
    TextureDisplacementLayer layer;
    Vec3f                    center;
    Vec3f                    axis;


    std::shared_ptr<const LscmLookup> lscm;


    std::shared_ptr<const PatchRegion> region;




    bool lscm_uv(const Vec3f &pos, Vec2f &uv, const Vec2f *&out) const
    {
        out = nullptr;

        constexpr float ON_PATCH_TOL = 0.05f;
        if (region && !region->contains(pos, ON_PATCH_TOL))
            return false;
        if (!lscm)
            return true;
        if (!lscm->uv_at(pos, ON_PATCH_TOL, uv))
            return false;
        out = &uv;
        return true;
    }
};



std::shared_ptr<std::vector<PreparedLayer>> prepare_sampleable_layers(
    const indexed_triangle_set &base_mesh, const std::vector<TextureDisplacementLayer> &layers,
    const TextureDisplacementFacetsData &facets_data, bool need_color)
{
    auto prepared = std::make_shared<std::vector<PreparedLayer>>();

    if (base_mesh.indices.empty())
        return prepared;

    std::vector<const TextureDisplacementLayer *> ordered;
    for (const TextureDisplacementLayer &l : layers)
        if (l.slot >= 0 && l.slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS))
            ordered.push_back(&l);
    std::sort(ordered.begin(), ordered.end(),
              [](const TextureDisplacementLayer *a, const TextureDisplacementLayer *b) { return a->slot < b->slot; });

    const std::vector<Vec3f> vertex_normals = texture_displacement_vertex_normals(base_mesh);
    const TriangleMesh       selector_mesh(base_mesh);
    const float              mesh_area = area_3d(base_mesh);

    for (const TextureDisplacementLayer *layer : ordered) {



        if (need_color && !layer->color_enabled)
            continue;
        const TriangleSelector::TriangleSplittingData &data = facets_data[size_t(layer->slot)];
        if (data.triangles_to_split.empty())
            continue;
        const DecodedHeightTexture tex = decode_height_texture(*layer);
        if (tex.empty() || (need_color && !tex.has_color()))
            continue;

        TriangleSelector selector(selector_mesh);
        selector.deserialize(data, false);
        const indexed_triangle_set patch = selector.get_facets_strict(EnforcerBlockerType::ENFORCER);
        if (patch.indices.empty())
            continue;



        Vec3f centroid, axis, average_normal;
        texture_displacement_patch_frame(patch, vertex_normals, centroid, axis, average_normal);

        std::shared_ptr<const LscmLookup> lscm;
        if (layer->projection_method == TextureProjectionMethod::LSCM) {



            auto l    = std::make_shared<LscmLookup>();
            l->corner = compute_lscm_corner_uvs(patch, *layer);
            if (l->corner.size() == patch.indices.size() * 3) {
                l->patch = patch;
                l->tree  = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(l->patch.vertices, l->patch.indices);
                lscm     = std::move(l);
            }
        }

        std::shared_ptr<const PatchRegion> region;
        if (!lscm && area_3d(patch) < 0.9999f * mesh_area) {
            auto r   = std::make_shared<PatchRegion>();
            r->patch = patch;
            r->tree  = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(r->patch.vertices, r->patch.indices);
            region   = std::move(r);
        }

        prepared->push_back({ tex, *layer, centroid, axis, std::move(lscm), std::move(region) });
    }
    return prepared;
}
}

ColorFieldSampler make_combined_color_sampler(const indexed_triangle_set                  &base_mesh,
                                              const std::vector<TextureDisplacementLayer> &layers,
                                              const TextureDisplacementFacetsData         &facets_data,
                                              ColorQuantizeFn                              quantize,
                                              ColorQuantizeFn                              quantize_pure)
{
    if (!quantize)
        return nullptr;
    auto prepared = prepare_sampleable_layers(base_mesh, layers, facets_data,   true);
    if (prepared->empty())
        return nullptr;


    auto pure = std::make_shared<std::vector<uint8_t>>(prepared->size(), 0);
    if (quantize_pure)
        for (size_t i = 0; i < prepared->size(); ++i)
            (*pure)[i] = analyze_texture_detail((*prepared)[i].layer).flat_colors ? 1 : 0;

    return [prepared, pure, quantize = std::move(quantize), quantize_pure = std::move(quantize_pure)](const Vec3f &pos, const Vec3f &normal) -> int {


        int result = -1;
        for (size_t i = 0; i < prepared->size(); ++i) {
            const PreparedLayer &p = (*prepared)[i];
            Vec2f        uv;
            const Vec2f *lscm_uv = nullptr;
            if (!p.lscm_uv(pos, uv, lscm_uv))
                continue;
            Vec3f rgb;
            if (sample_layer_color(p.tex, p.layer, pos, normal, rgb, p.center, p.axis, lscm_uv))
                if (const int idx = ((*pure)[i] ? quantize_pure : quantize)(rgb); idx >= 0)
                    result = idx;
        }
        return result;
    };
}

HeightFieldSampler make_combined_displacement_sampler(const indexed_triangle_set                  &base_mesh,
                                                      const std::vector<TextureDisplacementLayer> &layers,
                                                      const TextureDisplacementFacetsData         &facets_data)
{
    auto prepared = prepare_sampleable_layers(base_mesh, layers, facets_data,   false);
    if (prepared->empty())
        return nullptr;

    return [prepared](const Vec3f &pos, const Vec3f &normal) -> float {
        float total = 0.f;
        bool  any   = false;
        for (const PreparedLayer &p : *prepared) {
            Vec2f        uv;
            const Vec2f *lscm_uv = nullptr;
            if (!p.lscm_uv(pos, uv, lscm_uv))
                continue;
            const float h        = sample_layer_height(p.tex, p.layer, pos, normal, p.center, p.axis, lscm_uv);
            const float sign     = p.layer.invert ? -1.f : 1.f;
            const float signed_h = (h - p.layer.midlevel) * p.layer.depth_mm * sign;


            total = blend_displacement(total, signed_h, any ? p.layer.blend_mode : TextureBlendMode::Add);
            any   = true;
        }
        return total;
    };
}

indexed_triangle_set subdivide_mesh_uniform(const indexed_triangle_set &mesh, float max_edge_length_mm, int max_iterations)
{
    indexed_triangle_set current   = mesh;
    const float          max_edge_sq = max_edge_length_mm * max_edge_length_mm;

    for (int iter = 0; iter < max_iterations; ++iter) {




        std::unordered_map<uint64_t, int> midpoint_cache;
        auto edge_key = [](int a, int b) -> uint64_t {
            if (a > b)
                std::swap(a, b);
            return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
        };
        auto get_midpoint = [&](int a, int b) -> int {
            const uint64_t key = edge_key(a, b);
            auto             it  = midpoint_cache.find(key);
            if (it != midpoint_cache.end())
                return it->second;
            const int idx = int(current.vertices.size());
            current.vertices.push_back((current.vertices[a] + current.vertices[b]) * 0.5f);
            midpoint_cache.emplace(key, idx);
            return idx;
        };

        std::vector<stl_triangle_vertex_indices> new_indices;
        new_indices.reserve(current.indices.size());
        bool any_split = false;
        for (const stl_triangle_vertex_indices &tri : current.indices) {
            const Vec3f &a = current.vertices[tri[0]];
            const Vec3f &b = current.vertices[tri[1]];
            const Vec3f &c = current.vertices[tri[2]];
            if ((b - a).squaredNorm() <= max_edge_sq && (c - b).squaredNorm() <= max_edge_sq && (a - c).squaredNorm() <= max_edge_sq) {
                new_indices.push_back(tri);
                continue;
            }

            any_split = true;
            const int m01 = get_midpoint(tri[0], tri[1]);
            const int m12 = get_midpoint(tri[1], tri[2]);
            const int m20 = get_midpoint(tri[2], tri[0]);
            new_indices.push_back(stl_triangle_vertex_indices(tri[0], m01, m20));
            new_indices.push_back(stl_triangle_vertex_indices(m01, tri[1], m12));
            new_indices.push_back(stl_triangle_vertex_indices(m20, m12, tri[2]));
            new_indices.push_back(stl_triangle_vertex_indices(m01, m12, m20));
        }

        current.indices = std::move(new_indices);
        if (!any_split)
            break;
    }

    return current;
}

indexed_triangle_set subdivide_mesh_adaptive(const indexed_triangle_set &mesh,
                                             const std::vector<uint8_t> &refine_region,
                                             float target_edge_length_mm, int max_triangles,
                                             std::vector<int> *out_source, const HeightFieldSampler &sampler,
                                             float chord_tolerance_mm, float min_edge_length_mm,
                                             float border_edge_length_mm,
                                             const DisplacementProgressFn &progress,
                                             const ColorFieldSampler &color, float color_edge_length_mm,
                                             bool split_multi_crossings)
{

    constexpr int NB_BOUNDARY    = -1;
    constexpr int NB_NONMANIFOLD = -2;




    struct Tri { int v[3]; int nb[3]; int src; };

    std::vector<Vec3f> verts = mesh.vertices;
    std::vector<Tri>   tris(mesh.indices.size());
    for (size_t i = 0; i < mesh.indices.size(); ++i)
        tris[i] = { { mesh.indices[i][0], mesh.indices[i][1], mesh.indices[i][2] },
                    { NB_BOUNDARY, NB_BOUNDARY, NB_BOUNDARY }, int(i) };

    auto emit = [&]() -> indexed_triangle_set {
        indexed_triangle_set out;
        out.vertices = verts;
        out.indices.reserve(tris.size());
        if (out_source) {
            out_source->clear();
            out_source->reserve(tris.size());
        }
        for (const Tri &t : tris) {
            out.indices.emplace_back(t.v[0], t.v[1], t.v[2]);
            if (out_source)
                out_source->push_back(t.src);
        }
        return out;
    };



    const bool  feature_mode = bool(sampler) && chord_tolerance_mm > 0.f;
    const bool  color_mode   = bool(color) && color_edge_length_mm > 0.f;
    const bool  multi_mode   = feature_mode && split_multi_crossings;
    float       step_iso_mm  = 0.f;
    float       step_dev_mm  = std::numeric_limits<float>::max();
    const float color_sq     = color_edge_length_mm > 0.f ? color_edge_length_mm * color_edge_length_mm : 0.f;
    const float min_floor_sq = min_edge_length_mm > 0.f ? min_edge_length_mm * min_edge_length_mm : 0.f;
    const float target_sq    = target_edge_length_mm > 0.f ? target_edge_length_mm * target_edge_length_mm : 0.f;
    const float border_sq    = border_edge_length_mm > 0.f ? border_edge_length_mm * border_edge_length_mm : 0.f;



    if (refine_region.size() != mesh.indices.size() || int(tris.size()) + 2 > max_triangles)
        return emit();
    if (!feature_mode && !color_mode && target_sq <= 0.f && border_sq <= 0.f)
        return emit();
    if (std::none_of(refine_region.begin(), refine_region.end(), [](uint8_t v) { return v != 0; }))
        return emit();

    auto edge_key = [](int a, int b) -> uint64_t {
        if (a > b)
            std::swap(a, b);
        return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
    };





    {




        std::vector<std::pair<uint64_t, int>> he(tris.size() * 3);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, tris.size()),
                          [&](const tbb::blocked_range<size_t> &range) {
                              for (size_t ti = range.begin(); ti < range.end(); ++ti)
                                  for (int e = 0; e < 3; ++e)
                                      he[ti * 3 + size_t(e)] = {
                                          edge_key(tris[ti].v[e], tris[ti].v[(e + 1) % 3]), int(ti) * 3 + e
                                      };
                          });
        tbb::parallel_sort(he.begin(), he.end());


        for (size_t i = 0; i < he.size();) {
            size_t j = i + 1;
            while (j < he.size() && he[j].first == he[i].first)
                ++j;
            const size_t count = j - i;
            if (count == 2) {
                const int a = he[i].second, b = he[i + 1].second;
                tris[size_t(a / 3)].nb[a % 3] = b / 3;
                tris[size_t(b / 3)].nb[b % 3] = a / 3;
            } else if (count > 2) {
                for (size_t k = i; k < j; ++k)
                    tris[size_t(he[k].second / 3)].nb[he[k].second % 3] = NB_NONMANIFOLD;
            }

            i = j;
        }
    }





    std::vector<Vec3f>   vnormal;
    std::vector<float>   vheight;
    std::vector<uint8_t> vheight_valid;
    if (feature_mode) {
        vnormal.assign(verts.size(), Vec3f::Zero());
        for (const Tri &t : tris) {
            const Vec3f fn = (verts[t.v[1]] - verts[t.v[0]]).cross(verts[t.v[2]] - verts[t.v[0]]);
            for (int i = 0; i < 3; ++i)
                vnormal[t.v[i]] += fn;
        }
        for (Vec3f &n : vnormal) {
            const float l = n.norm();
            n = (l > 1e-12f) ? Vec3f(n / l) : Vec3f(Vec3f::UnitZ());
        }
        vheight.assign(verts.size(), 0.f);
        vheight_valid.assign(verts.size(), 0);
    }
    auto height_of = [&](int v) -> float {
        if (!vheight_valid[v]) {
            vheight[v]       = sampler(verts[v], vnormal[v]);
            vheight_valid[v] = 1;
        }
        return vheight[v];
    };



    std::vector<int>     vcolor;
    std::vector<uint8_t> vcolor_valid;
    if (color_mode) {
        if (vnormal.empty()) {
            vnormal.assign(verts.size(), Vec3f::Zero());
            for (const Tri &t : tris) {
                const Vec3f fn = (verts[t.v[1]] - verts[t.v[0]]).cross(verts[t.v[2]] - verts[t.v[0]]);
                for (int i = 0; i < 3; ++i)
                    vnormal[t.v[i]] += fn;
            }
            for (Vec3f &n : vnormal) {
                const float l = n.norm();
                n = (l > 1e-12f) ? Vec3f(n / l) : Vec3f(Vec3f::UnitZ());
            }
        }
        vcolor.assign(verts.size(), -2);
        vcolor_valid.assign(verts.size(), 0);
    }
    auto color_of = [&](int v) -> int {
        if (!vcolor_valid[v]) {
            vcolor[v]       = color(verts[v], vnormal[v]);
            vcolor_valid[v] = 1;
        }
        return vcolor[v];
    };










    if (feature_mode || color_mode) {
        std::vector<uint8_t> wanted(verts.size(), 0);
        for (const Tri &t : tris)
            if (refine_region[t.src] != 0)
                for (int i = 0; i < 3; ++i)
                    wanted[size_t(t.v[i])] = 1;


        tbb::parallel_for(tbb::blocked_range<size_t>(0, verts.size()),
                          [&](const tbb::blocked_range<size_t> &range) {
                              for (size_t v = range.begin(); v < range.end(); ++v) {
                                  if (!wanted[v])
                                      continue;
                                  if (feature_mode) {
                                      vheight[v]       = sampler(verts[v], vnormal[v]);
                                      vheight_valid[v] = 1;
                                  }
                                  if (color_mode) {
                                      vcolor[v]       = color(verts[v], vnormal[v]);
                                      vcolor_valid[v] = 1;
                                  }
                              }
                          });
        if (multi_mode) {

            float lo = std::numeric_limits<float>::max(), hi = -lo;
            for (size_t v = 0; v < verts.size(); ++v)
                if (wanted[v]) { lo = std::min(lo, vheight[v]); hi = std::max(hi, vheight[v]); }
            step_iso_mm = (lo < hi) ? 0.5f * (lo + hi) : 0.f;
            step_dev_mm = (lo < hi) ? 0.4f * (hi - lo) : std::numeric_limits<float>::max();
        }
    }

    auto elen_sq = [&](int a, int b) -> float { return (verts[a] - verts[b]).squaredNorm(); };





    auto longest_local = [&](int ti) -> int {
        const Tri &t    = tris[ti];
        int        best = 0;
        float      bl   = elen_sq(t.v[0], t.v[1]);
        uint64_t   bk   = edge_key(t.v[0], t.v[1]);
        for (int e = 1; e < 3; ++e) {
            const float    l = elen_sq(t.v[e], t.v[(e + 1) % 3]);
            const uint64_t k = edge_key(t.v[e], t.v[(e + 1) % 3]);
            if (l > bl || (l == bl && k < bk)) {
                bl   = l;
                bk   = k;
                best = e;
            }
        }
        return best;
    };






    std::vector<float> tri_err;
    if (feature_mode)
        tri_err.assign(tris.size(), -1.f);




    std::vector<uint8_t> tri_color_split;
    if (color_mode)
        tri_color_split.assign(tris.size(), 0);
    auto straddles_color = [&](int ti) -> bool {
        if (tri_color_split[ti] != 0)
            return tri_color_split[ti] == 1;
        const Tri  &t  = tris[ti];
        const Vec3f pa = verts[t.v[0]], pb = verts[t.v[1]], pc = verts[t.v[2]];
        const Vec3f na = vnormal[t.v[0]], nb = vnormal[t.v[1]], nc = vnormal[t.v[2]];
        const int   ca = color_of(t.v[0]);
        bool        split = color_of(t.v[1]) != ca || color_of(t.v[2]) != ca;
        if (!split) {
            static const float BARY[4][3] = { { 0.5f, 0.5f, 0.f }, { 0.f, 0.5f, 0.5f },
                                              { 0.5f, 0.f, 0.5f }, { 1.f / 3, 1.f / 3, 1.f / 3 } };
            for (const auto &w : BARY) {
                Vec3f       n  = w[0] * na + w[1] * nb + w[2] * nc;
                const float nl = n.norm();
                n = (nl > 1e-12f) ? Vec3f(n / nl) : na;
                if (color(w[0] * pa + w[1] * pb + w[2] * pc, n) != ca) {
                    split = true;
                    break;
                }
            }
        }
        tri_color_split[ti] = split ? 1 : 2;
        return split;
    };





    const float sample_spacing = std::max(min_edge_length_mm, 0.05f);

    auto detail_error = [&](int ti) -> float {
        if (tri_err[ti] >= 0.f)
            return tri_err[ti];
        const Tri  &t  = tris[ti];
        const Vec3f pa = verts[t.v[0]], pb = verts[t.v[1]], pc = verts[t.v[2]];
        const Vec3f na = vnormal[t.v[0]], nb = vnormal[t.v[1]], nc = vnormal[t.v[2]];
        const float ha = height_of(t.v[0]), hb = height_of(t.v[1]), hc = height_of(t.v[2]);








        const float longest = std::sqrt(std::max({ (pb - pa).squaredNorm(), (pc - pb).squaredNorm(), (pa - pc).squaredNorm() }));
        const int   n       = std::clamp(int(std::ceil(longest / sample_spacing)), 2, 8);
        float       maxerr  = 0.f;



        bool sharp_cross = false;
        if (multi_mode) {
            const Vec3f *P[3] = { &pa, &pb, &pc };
            const Vec3f *Nn[3] = { &na, &nb, &nc };
            const float  H[3]  = { ha, hb, hc };
            for (int e = 0; e < 3; ++e) {
                const Vec3f &p0 = *P[e], &p1 = *P[(e + 1) % 3], &n0 = *Nn[e], &n1 = *Nn[(e + 1) % 3];
                const int    m  = std::clamp(int(std::ceil((p1 - p0).norm() / sample_spacing)), 2, 32);
                float        last = H[e];
                for (int i = 1; i <= m; ++i) {
                    float h;
                    if (i == m) h = H[(e + 1) % 3];
                    else {
                        const float t  = float(i) / float(m);
                        Vec3f       nn = n0 + (n1 - n0) * t;
                        const float nl = nn.norm();
                        nn = (nl > 1e-12f) ? Vec3f(nn / nl) : n0;
                        h  = sampler(p0 + (p1 - p0) * t, nn);
                    }
                    if ((h > step_iso_mm) != (last > step_iso_mm) && std::abs(h - last) > step_dev_mm) {
                        sharp_cross = true;
                        break;
                    }
                    last = h;
                }
                if (sharp_cross)
                    break;
            }
        }
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n - i; ++j) {
                const int   k  = n - i - j;
                const float wa = float(i) / float(n), wb = float(j) / float(n), wc = float(k) / float(n);
                float       actual;
                if (i == n)      actual = ha;
                else if (j == n) actual = hb;
                else if (k == n) actual = hc;
                else {
                    Vec3f       nn = wa * na + wb * nb + wc * nc;
                    const float nl = nn.norm();
                    nn = (nl > 1e-12f) ? Vec3f(nn / nl) : na;
                    actual = sampler(wa * pa + wb * pb + wc * pc, nn);

                    maxerr = std::max(maxerr, std::abs(actual - (wa * ha + wb * hb + wc * hc)));
                }
            }





        if (multi_mode && sharp_cross)
            maxerr = 0.f;
        tri_err[ti] = maxerr;
        return maxerr;
    };






    auto priority = [&](int ti) -> float {
        const Tri    &t     = tris[ti];
        const uint8_t flags = refine_region[t.src];
        if (flags == 0)
            return 0.f;
        const int   le = longest_local(ti);
        const float ll = elen_sq(t.v[le], t.v[(le + 1) % 3]);
        if (ll <= min_floor_sq)
            return 0.f;
        float p = 0.f;
        if (flags & REFINE_PAINTED) {
            p = (target_sq > 0.f) ? ll / target_sq : 0.f;
            if (feature_mode) {
                p = std::max(p, detail_error(ti) / chord_tolerance_mm);
            }


            if (color_mode && straddles_color(ti))
                p = std::max(p, ll / color_sq);
        }





        if ((flags & REFINE_BORDER) && border_sq > 0.f)
            p = std::max(p, ll / border_sq);
        return p;
    };







    auto tri_area = [&](int ti) -> float {
        const Tri &t = tris[ti];
        return 0.5f * (verts[t.v[1]] - verts[t.v[0]]).cross(verts[t.v[2]] - verts[t.v[0]]).norm();
    };
    auto heap_key = [&](int ti, float p) -> float { return p * tri_area(ti); };

    auto set_nb = [&](int ti, int u, int v, int val) {
        if (ti < 0)
            return;
        Tri &t = tris[ti];
        for (int e = 0; e < 3; ++e)
            if ((t.v[e] == u && t.v[(e + 1) % 3] == v) || (t.v[e] == v && t.v[(e + 1) % 3] == u)) {
                t.nb[e] = val;
                return;
            }
    };






    std::vector<int> touched;
    auto bisect = [&](int ti, int e) -> bool {
        const int a = tris[ti].v[e], b = tris[ti].v[(e + 1) % 3], c = tris[ti].v[(e + 2) % 3];
        const int n = tris[ti].nb[e];


        int f = -1;
        if (n >= 0) {
            for (int k = 0; k < 3; ++k) {
                const int u = tris[n].v[k], w = tris[n].v[(k + 1) % 3];
                if ((u == a && w == b) || (u == b && w == a)) {
                    f = k;
                    break;
                }
            }
            if (f < 0) {
                tris[ti].nb[e] = NB_NONMANIFOLD;
                return false;
            }
        }

        const int m = int(verts.size());
        verts.push_back(0.5f * (verts[a] + verts[b]));
        if (feature_mode || color_mode) {
            const Vec3f mn = vnormal[a] + vnormal[b];
            const float ml = mn.norm();
            vnormal.push_back(ml > 1e-12f ? Vec3f(mn / ml) : vnormal[a]);
        }
        if (feature_mode) {
            vheight.push_back(0.f);
            vheight_valid.push_back(0);
        }
        if (color_mode) {
            vcolor.push_back(-2);
            vcolor_valid.push_back(0);
        }



        const int nb_bc = tris[ti].nb[(e + 1) % 3];
        const int nb_ca = tris[ti].nb[(e + 2) % 3];
        const int src   = tris[ti].src;
        const int t2    = int(tris.size());
        tris.push_back(Tri{ { m, b, c }, { NB_BOUNDARY, nb_bc, ti }, src });
        {
            Tri &t1 = tris[ti];
            t1.v[0] = a; t1.v[1] = m; t1.v[2] = c;
            t1.nb[0] = NB_BOUNDARY; t1.nb[1] = t2; t1.nb[2] = nb_ca;
        }
        set_nb(nb_bc, b, c, t2);
        if (feature_mode) {
            tri_err.push_back(-1.f);
            tri_err[ti] = -1.f;
        }
        if (color_mode) {
            tri_color_split.push_back(0);
            tri_color_split[ti] = 0;
        }
        touched.assign({ ti, t2 });

        if (n < 0) {
            return true;
        }


        const int p = tris[n].v[f], q = tris[n].v[(f + 1) % 3], d = tris[n].v[(f + 2) % 3];
        const int nb_qd = tris[n].nb[(f + 1) % 3];
        const int nb_dp = tris[n].nb[(f + 2) % 3];
        const int nsrc  = tris[n].src;
        const int n2    = int(tris.size());
        tris.push_back(Tri{ { m, q, d }, { NB_BOUNDARY, nb_qd, n }, nsrc });
        {
            Tri &n1 = tris[n];
            n1.v[0] = p; n1.v[1] = m; n1.v[2] = d;
            n1.nb[0] = NB_BOUNDARY; n1.nb[1] = n2; n1.nb[2] = nb_dp;
        }
        set_nb(nb_qd, q, d, n2);
        if (feature_mode) {
            tri_err.push_back(-1.f);
            tri_err[n] = -1.f;
        }
        if (color_mode) {
            tri_color_split.push_back(0);
            tri_color_split[n] = 0;
        }



        const int side_a = (p == a) ? n : n2;
        const int side_b = (p == a) ? n2 : n;
        tris[ti].nb[0] = side_a;
        tris[t2].nb[0] = side_b;
        set_nb(side_a, a, m, ti);
        set_nb(side_b, b, m, t2);
        touched.assign({ ti, t2, n, n2 });
        return true;
    };




    std::priority_queue<std::pair<float, int>> queue;
    {





        std::vector<float> initial(tris.size(), 0.f);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, tris.size()),
                          [&](const tbb::blocked_range<size_t> &range) {
                              for (size_t ti = range.begin(); ti < range.end(); ++ti)
                                  initial[ti] = priority(int(ti));
                          });
        for (int ti = 0; ti < int(tris.size()); ++ti)
            if (initial[size_t(ti)] > 1.f)
                queue.emplace(heap_key(ti, initial[size_t(ti)]), ti);
    }






    const int start_tris  = int(tris.size());
    const int budget_tris = std::max(1, max_triangles - start_tris);
    int       next_poll   = start_tris;
    while (!queue.empty() && int(tris.size()) + 2 <= max_triangles) {
        if (progress && int(tris.size()) >= next_poll) {
            next_poll = int(tris.size()) + std::max(1024, budget_tris / 100);
            if (!progress(std::clamp((int(tris.size()) - start_tris) * 100 / budget_tris, 0, 100)))
                break;
        }
        const int ti = queue.top().second;
        queue.pop();
        if (priority(ti) <= 1.f)
            continue;




        int cur = ti, split_edge = -1;
        for (size_t guard = 0; guard <= tris.size(); ++guard) {
            const int le  = longest_local(cur);
            const int nbr = tris[cur].nb[le];
            if (nbr == NB_NONMANIFOLD)
                break;
            if (nbr == NB_BOUNDARY) {
                split_edge = le;
                break;
            }
            const int nle = longest_local(nbr);
            if (edge_key(tris[nbr].v[nle], tris[nbr].v[(nle + 1) % 3]) ==
                edge_key(tris[cur].v[le], tris[cur].v[(le + 1) % 3])) {
                split_edge = le;
                break;
            }
            cur = nbr;
        }
        if (split_edge < 0 || !bisect(cur, split_edge))
            continue;

        for (const int t : touched)
            if (const float p = priority(t); p > 1.f)
                queue.emplace(heap_key(t, p), t);


        if (cur != ti)
            if (const float p = priority(ti); p > 1.f)
                queue.emplace(heap_key(ti, p), ti);
    }

    return emit();
}

namespace {






void ear_clip(const std::vector<Vec2f> &pts, std::vector<int> poly, const std::function<void(int, int, int)> &emit)
{
    auto cross2 = [](const Vec2f &o, const Vec2f &a, const Vec2f &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };



    float flat = 0.f;
    {
        Vec2f lo = pts[size_t(poly[0])], hi = lo;
        for (int id : poly) { lo = lo.cwiseMin(pts[size_t(id)]); hi = hi.cwiseMax(pts[size_t(id)]); }
        flat = 1e-5f * (hi - lo).squaredNorm();
    }
    while (poly.size() > 3) {
        const size_t n     = poly.size();
        bool         found = false;
        for (size_t i = 0; i < n && !found; ++i) {
            const int    ia = poly[(i + n - 1) % n], ib = poly[i], ic = poly[(i + 1) % n];
            const Vec2f &a = pts[size_t(ia)], &b = pts[size_t(ib)], &c = pts[size_t(ic)];
            const float  area2 = cross2(a, b, c);
            if (area2 <= flat)
                continue;
            const float eps   = 1e-6f * area2;
            bool        clean = true;
            size_t      on_base = 0;
            for (size_t j = 0; j < n && clean; ++j) {
                const int ij = poly[j];
                if (ij == ia || ij == ib || ij == ic)
                    continue;
                const Vec2f &p = pts[size_t(ij)];
                clean = !(cross2(a, b, p) > eps && cross2(b, c, p) > eps && cross2(c, a, p) > eps);


                if (std::abs(cross2(c, a, p)) <= flat && (p - c).dot(a - c) > 0.f && (p - a).dot(c - a) > 0.f)
                    ++on_base;
            }
            if (!clean)
                continue;
            if (on_base > 0) {



                if (on_base + 3 != n)
                    continue;
                for (size_t j = 0; j + 1 < n - 1; ++j) {
                    const int u = poly[(i + 1 + j) % n], v = poly[(i + 2 + j) % n];
                    emit(ib, u, v);
                }
                poly.clear();
                return;
            }
            emit(ia, ib, ic);
            poly.erase(poly.begin() + long(i));
            found = true;
        }
        if (!found) {


            size_t pick = 0;
            float  best = std::numeric_limits<float>::max();
            bool   convex_found = false;
            for (size_t i = 0; i < n; ++i) {
                const Vec2f &a = pts[size_t(poly[(i + n - 1) % n])], &b = pts[size_t(poly[i])], &c = pts[size_t(poly[(i + 1) % n])];
                const float  m = cross2(a, b, c);
                if (m > flat && (!convex_found || m < best)) { best = m; pick = i; convex_found = true; }
                else if (!convex_found && std::abs(m) < best) { best = std::abs(m); pick = i; }
            }
            emit(poly[(pick + n - 1) % n], poly[pick], poly[(pick + 1) % n]);
            poly.erase(poly.begin() + long(pick));
        }
    }
    if (poly.size() == 3)
        emit(poly[0], poly[1], poly[2]);
}


void simplify_polyline(const std::vector<Vec2f> &in, float tol, std::vector<Vec2f> &out)
{
    out.clear();
    if (in.size() < 3) { out = in; return; }
    std::vector<uint8_t> keep(in.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> stack = { { 0, in.size() - 1 } };
    while (!stack.empty()) {
        const auto [i0, i1] = stack.back();
        stack.pop_back();
        if (i1 <= i0 + 1)
            continue;
        const Vec2f d   = in[i1] - in[i0];
        const float len = d.norm();
        float       best = -1.f;
        size_t      bi   = i0;
        for (size_t i = i0 + 1; i < i1; ++i) {
            const Vec2f r    = in[i] - in[i0];
            const float dist = (len > 1e-12f) ? std::abs(d.x() * r.y() - d.y() * r.x()) / len : r.norm();
            if (dist > best) { best = dist; bi = i; }
        }
        if (best > tol) {
            keep[bi] = 1;
            stack.push_back({ i0, bi });
            stack.push_back({ bi, i1 });
        }
    }
    for (size_t i = 0; i < in.size(); ++i)
        if (keep[i]) out.push_back(in[i]);
}

}

namespace {


struct Crossing
{
    Vec3f p, n;
    float t;
    int   a = -1, b = -1;
    float sin_min = 1.f;
    float dt_max  = std::numeric_limits<float>::max();

    bool  used = false;
    bool  no_seam = false;
    bool  sharp = false;
    float thick = 0.f;
    int   single = -1, lo = -1, hi = -1;
    int   near_a = -1, near_b = -1;
};
struct Edge
{
    std::vector<Crossing> xs;
};

inline uint64_t step_edge_key(int a, int b)
{
    if (a > b) std::swap(a, b);
    return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
}





struct StepScan
{
    bool                               ok = false;
    std::vector<Vec3f>                 pos, vnormal;
    std::vector<float>                 vh;
    float                              iso = 0.f, range = 0.f;
    std::unordered_map<uint64_t, Edge> edges;

    std::pair<Vec3f, Vec3f> sample_between(int a, int b, float t) const
    {
        const Vec3f p = pos[size_t(a)] + (pos[size_t(b)] - pos[size_t(a)]) * t;
        Vec3f       n = vnormal[size_t(a)] + (vnormal[size_t(b)] - vnormal[size_t(a)]) * t;
        const float l = n.norm();
        n = (l > 1e-12f) ? Vec3f(n / l) : vnormal[size_t(a)];
        return std::make_pair(p, n);
    }
    bool side_of(float h) const { return h > iso; }
};

StepScan scan_steps(const indexed_triangle_set &mesh, const std::vector<uint8_t> &region,
                    const HeightFieldSampler &sampler, float step_width_mm, float seam_gap_mm,
                    float min_feature_mm, bool nudge)
{
    StepScan     scan;
    const size_t nv = mesh.vertices.size(), nt = mesh.indices.size();
    if (!sampler || region.size() != nt || step_width_mm <= 0.f)
        return scan;




    std::vector<Vec3f>       &pos    = scan.pos;
    std::vector<Vec3f>       &vnormal = scan.vnormal;
    pos     = mesh.vertices;
    vnormal = texture_displacement_vertex_normals(mesh);
    std::vector<uint8_t>     wanted(nv, 0);
    for (size_t t = 0; t < nt; ++t)
        if (region[t] != 0)
            for (int k = 0; k < 3; ++k)
                wanted[size_t(mesh.indices[t][k])] = 1;
    std::vector<float> &vh = scan.vh;
    vh.assign(nv, 0.f);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, nv), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t v = r.begin(); v < r.end(); ++v)
            if (wanted[v])
                vh[v] = sampler(pos[v], vnormal[v]);
    });
    float hmin = std::numeric_limits<float>::max(), hmax = -hmin;
    for (size_t v = 0; v < nv; ++v)
        if (wanted[v]) { hmin = std::min(hmin, vh[v]); hmax = std::max(hmax, vh[v]); }
    const float range = scan.range = hmax - hmin;
    if (!(range > 1e-6f))
        return scan;
    const float iso = scan.iso = 0.5f * (hmin + hmax);





    {
        size_t near_level = 0, total = 0;
        for (size_t v = 0; v < nv; ++v)
            if (wanted[v]) { ++total; near_level += std::abs(vh[v] - iso) > 0.35f * range; }
        if (total == 0 || double(near_level) < 0.6 * double(total))
            return scan;
    }






    if (nudge) {
        std::vector<uint8_t> creased(nv, 0);
        std::vector<Vec3f>   first_normal(nv, Vec3f::Zero());
        std::vector<float>   shortest(nv, std::numeric_limits<float>::max());
        for (size_t t = 0; t < nt; ++t) {
            const auto  &tri = mesh.indices[t];
            const Vec3f &a = pos[size_t(tri[0])];
            Vec3f        fn = (pos[size_t(tri[1])] - a).cross(pos[size_t(tri[2])] - a);
            const float  fl = fn.norm();
            if (fl < 1e-12f) continue;
            fn /= fl;
            for (int k = 0; k < 3; ++k) {
                Vec3f &f0 = first_normal[size_t(tri[k])];
                if (f0.isZero()) f0 = fn;
                else if (f0.dot(fn) < 0.985f) creased[size_t(tri[k])] = 1;
                const float el = (pos[size_t(tri[(k + 1) % 3])] - pos[size_t(tri[k])]).norm();
                shortest[size_t(tri[k])]           = std::min(shortest[size_t(tri[k])], el);
                shortest[size_t(tri[(k + 1) % 3])] = std::min(shortest[size_t(tri[(k + 1) % 3])], el);
            }
        }
        tbb::parallel_for(tbb::blocked_range<size_t>(0, nv), [&](const tbb::blocked_range<size_t> &r) {
            for (size_t v = r.begin(); v < r.end(); ++v) {
                if (!wanted[v] || creased[v] || std::abs(vh[v] - iso) > 0.47f * range)
                    continue;
                const Vec3f &n  = vnormal[v];
                const Vec3f  ax = (std::abs(n.x()) < 0.9f) ? Vec3f::UnitX() : Vec3f::UnitY();
                const Vec3f  t1 = n.cross(ax).normalized(), t2 = n.cross(t1).normalized();
                const float  d  = step_width_mm;
                const float  gx = sampler(pos[v] + t1 * d, n) - sampler(pos[v] - t1 * d, n);
                const float  gy = sampler(pos[v] + t2 * d, n) - sampler(pos[v] - t2 * d, n);
                Vec3f        g  = t1 * gx + t2 * gy;
                const float  gl = g.norm();
                if (gl < 0.25f * range)
                    continue;
                g /= gl;



                const Vec3f dir   = (vh[v] > iso) ? g : Vec3f(-g);
                Vec3f       best  = pos[v];
                float       bh    = vh[v];
                const float limit = std::min(2.f * step_width_mm, 0.3f * shortest[v]);
                for (int i = 1; i <= 8; ++i) {
                    const float dist = 0.25f * step_width_mm * float(i);
                    if (dist > limit)
                        break;
                    const Vec3f q = pos[v] + dir * dist;
                    const float h = sampler(q, n);
                    if ((h > iso) != (vh[v] > iso))
                        break;
                    best = q; bh = h;
                    if (std::abs(h - iso) > 0.48f * range)
                        break;
                }
                if (std::abs(bh - iso) > std::abs(vh[v] - iso)) { pos[v] = best; vh[v] = bh; }
            }
        });
    }

    const auto sample_between = [&scan](int a, int b, float t) { return scan.sample_between(a, b, t); };
    const auto side_of        = [&scan](float h) { return scan.side_of(h); };




    auto &edges = scan.edges;
    edges.reserve(nt);

    const auto find_crossings = [&](int a, int b, Edge &edge) {
        if (a > b) std::swap(a, b);
        const float len   = (pos[size_t(b)] - pos[size_t(a)]).norm();
        const int   steps = std::clamp(int(std::ceil(len / (0.5f * step_width_mm))), 4, 128);
        float       t0 = 0.f, h0 = vh[size_t(a)] - iso;
        for (int i = 1; i <= steps; ++i) {
            const float ti = float(i) / float(steps);
            const auto [pp, nn] = sample_between(a, b, ti);
            const float hi_ = sampler(pp, nn) - iso;
            if (h0 * hi_ < 0.f || (hi_ == 0.f && h0 != 0.f)) {
                float lo_t = t0, hi_t = ti, lo_h = h0;
                for (int k = 0; k < 10; ++k) {
                    const float tm = 0.5f * (lo_t + hi_t);
                    const auto [pm, nm] = sample_between(a, b, tm);
                    if (lo_h * (sampler(pm, nm) - iso) <= 0.f) hi_t = tm; else { lo_t = tm; }
                }
                Crossing c;
                c.a = a; c.b = b;

                c.t = std::clamp(0.5f * (lo_t + hi_t), 1e-3f, 1.f - 1e-3f);
                std::tie(c.p, c.n) = sample_between(a, b, c.t);


                const Vec3f ax  = (std::abs(c.n.x()) < 0.9f) ? Vec3f::UnitX() : Vec3f::UnitY();
                const Vec3f t1v = c.n.cross(ax).normalized(), t2v = c.n.cross(t1v).normalized();
                const float d   = 0.5f * step_width_mm;
                const float gx  = sampler(c.p + t1v * d, c.n) - sampler(c.p - t1v * d, c.n);
                const float gy  = sampler(c.p + t2v * d, c.n) - sampler(c.p - t2v * d, c.n);
                const float gl  = std::sqrt(gx * gx + gy * gy);
                c.sharp = gl >= 0.5f * range;




                {
                    const float thick_max = 4.f * step_width_mm;
                    c.thick = thick_max;
                    if (gl > 1e-12f) {
                        const Vec3f g = (t1v * gx + t2v * gy) / gl;
                        for (int sgn = -1; sgn <= 1; sgn += 2) {
                            const bool high = sgn > 0;
                            for (int i = 1; i <= 8; ++i) {
                                const float dist = 0.5f * step_width_mm * float(i);
                                if (dist >= c.thick)
                                    break;
                                if (side_of(sampler(c.p + g * (float(sgn) * dist), c.n)) != high) {
                                    c.thick = std::min(c.thick, dist);
                                    break;
                                }
                            }
                        }
                    }
                }
                edge.xs.push_back(c);
            }
            t0 = ti; h0 = hi_;
        }



        const float thinnest = std::max(min_feature_mm, seam_gap_mm);
        for (size_t i = 0; i + 1 < edge.xs.size();) {
            if ((edge.xs[i + 1].t - edge.xs[i].t) * len < thinnest)
                edge.xs.erase(edge.xs.begin() + long(i), edge.xs.begin() + long(i) + 2);
            else
                ++i;
        }



        const bool odd_expected = side_of(vh[size_t(a)]) != side_of(vh[size_t(b)]);
        if ((edge.xs.size() % 2 == 1) != odd_expected)
            edge.xs.clear();
    };


    std::vector<uint64_t> edge_keys;
    for (size_t t = 0; t < nt; ++t) {
        if (region[t] == 0)
            continue;
        const auto &tri = mesh.indices[t];
        for (int e = 0; e < 3; ++e)
            edge_keys.push_back(step_edge_key(tri[e], tri[(e + 1) % 3]));
    }
    std::sort(edge_keys.begin(), edge_keys.end());
    edge_keys.erase(std::unique(edge_keys.begin(), edge_keys.end()), edge_keys.end());
    std::vector<Edge> found(edge_keys.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, edge_keys.size()), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t i = r.begin(); i < r.end(); ++i)
            find_crossings(int(edge_keys[i] >> 32), int(uint32_t(edge_keys[i])), found[i]);
    });
    size_t crossings = 0, sharp = 0;
    double thick_sum = 0.;
    for (size_t i = 0; i < edge_keys.size(); ++i) {
        for (const Crossing &c : found[i].xs) { ++crossings; sharp += c.sharp; thick_sum += c.thick; }
        edges.emplace(edge_keys[i], std::move(found[i]));
    }


    if (crossings == 0 || double(sharp) < 0.8 * double(crossings))
        return scan;





    if (thick_sum < 2.25 * double(step_width_mm) * double(crossings))
        return scan;

    scan.ok = true;
    return scan;

}

}

bool texture_has_steps_to_cut(const indexed_triangle_set &mesh, const std::vector<uint8_t> &region,
                              const HeightFieldSampler &sampler, float step_width_mm, float seam_gap_mm,
                              float min_feature_mm)
{
    return scan_steps(mesh, region, sampler, step_width_mm, seam_gap_mm, min_feature_mm,   false).ok;
}

indexed_triangle_set cut_mesh_at_steps(const indexed_triangle_set &mesh, const std::vector<uint8_t> &region,
                                       const HeightFieldSampler &sampler, float step_width_mm,
                                       float seam_gap_mm, float min_feature_mm, std::vector<int> *out_source,
                                       size_t *out_cut_count)
{
    const size_t nv = mesh.vertices.size(), nt = mesh.indices.size();
    if (out_cut_count)
        *out_cut_count = 0;
    const auto passthrough = [&]() {
        if (out_source) {
            out_source->resize(nt);
            std::iota(out_source->begin(), out_source->end(), 0);
        }
        return mesh;
    };
    if (!sampler || region.size() != nt || step_width_mm <= 0.f)
        return passthrough();

    StepScan scan = scan_steps(mesh, region, sampler, step_width_mm, seam_gap_mm, min_feature_mm,   true);
    if (!scan.ok)
        return passthrough();
    std::vector<Vec3f>       &pos     = scan.pos;
    const std::vector<Vec3f> &vnormal = scan.vnormal;
    std::vector<float>       &vh      = scan.vh;
    const float               iso = scan.iso, range = scan.range;
    auto                     &edges = scan.edges;
    const auto sample_between = [&scan](int a, int b, float t) { return scan.sample_between(a, b, t); };
    const auto side_of        = [&scan](float h) { return scan.side_of(h); };
    const auto edge_key       = [](int a, int b) { return step_edge_key(a, b); };
    (void) vnormal; (void) range; (void) iso;









    struct Chain
    {
        int                i = -1, j = -1;
        std::vector<Vec3f> pts;
        std::vector<int>   lo, hi;
        float              clearance = std::numeric_limits<float>::max();
    };
    struct Loop
    {
        std::vector<int>        ids;
        std::vector<Crossing *> xs;
        std::vector<int>        xpos;
        std::vector<Chain>      chains;
        std::vector<int>        partner;
        std::vector<int>        chain_of;
        std::vector<int>        no_seam;
        Vec3f                   N = Vec3f::Zero();
        bool                    ok = false;
    };
    const auto walk_edge = [&](int a, int b, std::vector<Crossing *> &out) {
        auto it = edges.find(edge_key(a, b));
        if (it == edges.end()) return;
        Edge &edge = it->second;
        if (a < b) for (auto &c : edge.xs) out.push_back(&c);
        else       for (auto ci = edge.xs.rbegin(); ci != edge.xs.rend(); ++ci) out.push_back(&*ci);
    };
    std::vector<Loop> loops(nt);
    for (size_t t = 0; t < nt; ++t) {
        const auto &tri = mesh.indices[t];
        Loop       &L   = loops[t];
        for (int e = 0; e < 3; ++e) {
            L.ids.push_back(tri[e]);
            std::vector<Crossing *> on_edge;
            walk_edge(tri[e], tri[(e + 1) % 3], on_edge);
            for (Crossing *c : on_edge) {
                L.xpos.push_back(int(L.ids.size()));
                L.ids.push_back(-(1 + int(L.xs.size())));
                L.xs.push_back(c);
            }
        }
        const Vec3f &a0 = pos[size_t(tri[0])];
        L.N             = (pos[size_t(tri[1])] - a0).cross(pos[size_t(tri[2])] - a0);
        const float nl  = L.N.norm();
        if (nl > 1e-12f) L.N /= nl;
    }

    const float cell = 0.5f * step_width_mm;
    const auto  trace = [&](size_t t) {
        Loop       &L   = loops[t];
        const int   n   = int(L.xs.size());
        const auto &tri = mesh.indices[t];
        if (n == 0 || region[t] == 0)
            return;
        if (n % 2 != 0 || L.N.squaredNorm() < 0.5f)
            return;

        const Vec3f &A = pos[size_t(tri[0])], &B = pos[size_t(tri[1])], &C = pos[size_t(tri[2])];
        const Vec3f  U = (B - A).normalized(), V = L.N.cross(U);
        const auto   to2 = [&](const Vec3f &p) { return Vec2f((p - A).dot(U), (p - A).dot(V)); };
        const auto   to3 = [&](const Vec2f &q) { return Vec3f(A + U * q.x() + V * q.y()); };
        const Vec2f  a2 = to2(A), b2 = to2(B), c2 = to2(C);
        const float  det = (b2.x() - a2.x()) * (c2.y() - a2.y()) - (c2.x() - a2.x()) * (b2.y() - a2.y());
        if (std::abs(det) < 1e-12f)
            return;







        const float longest = std::sqrt(std::max({ (b2 - a2).squaredNorm(), (c2 - b2).squaredNorm(), (a2 - c2).squaredNorm() }));
        const int   N       = std::clamp(int(std::ceil(longest / cell)), 4, 96);
        const float h       = longest / float(N);
        const int   W       = N + 1;


        const auto at2 = [&](int j, int k) { return Vec2f(a2 + (b2 - a2) * (float(j) / float(N)) + (c2 - a2) * (float(k) / float(N))); };
        std::vector<float>  f(size_t(W) * size_t(W), 0.f);
        std::vector<int8_t> sign(size_t(W) * size_t(W), 0);
        const auto          id = [&](int j, int k) { return size_t(k) * size_t(W) + size_t(j); };
        for (int k = 0; k <= N; ++k)
            for (int j = 0; j + k <= N; ++j) {
                const float wb = float(j) / float(N), wc = float(k) / float(N), wa = 1.f - wb - wc;
                Vec3f       nn = wa * vnormal[size_t(tri[0])] + wb * vnormal[size_t(tri[1])] + wc * vnormal[size_t(tri[2])];
                const float nl = nn.norm();
                nn = (nl > 1e-12f) ? Vec3f(nn / nl) : L.N;
                const float v = sampler(A + (B - A) * wb + (C - A) * wc, nn) - iso;
                f[id(j, k)]    = v;
                sign[id(j, k)] = v > 0.f ? 1 : -1;
            }


        const auto edge_point = [&](int e, int m) -> std::pair<int, int> {
            return e == 0 ? std::make_pair(m, 0) : e == 1 ? std::make_pair(N - m, m) : std::make_pair(0, N - m);
        };

        std::vector<std::vector<std::pair<float, int>>> edge_x(3);
        {
            int e = -1, k = 0;
            for (int idv : L.ids) {
                if (idv >= 0) { ++e; continue; }
                const Crossing &c = *L.xs[size_t(k)];

                const float u = (tri[e] < tri[(e + 1) % 3]) ? c.t : 1.f - c.t;
                edge_x[size_t(e)].push_back({ u, k });
                ++k;
            }
        }
        for (int e = 0; e < 3; ++e) {
            int8_t s = side_of(vh[size_t(tri[e])]) ? 1 : -1;
            size_t next = 0;
            for (int m = 0; m <= N; ++m) {
                const float u = float(m) / float(N);
                while (next < edge_x[size_t(e)].size() && edge_x[size_t(e)][next].first < u) { s = -s; ++next; }
                const auto [j, k] = edge_point(e, m);
                sign[id(j, k)]    = s;



                if ((f[id(j, k)] > 0.f) != (s > 0))
                    f[id(j, k)] = float(s) * std::max(std::abs(f[id(j, k)]), 0.05f * range);
            }
            if ((s > 0) != side_of(vh[size_t(tri[(e + 1) % 3])]))
                return;
        }




        std::vector<Vec2f>              pts;
        std::vector<int>                pt_x;
        std::unordered_map<uint32_t, int> point_of;
        std::vector<std::array<int, 2>> link;
        bool                            bad = false;
        const auto point_on = [&](int j, int k, int dir) -> int {
            const uint32_t key = (uint32_t(j) << 16) | (uint32_t(k) << 2) | uint32_t(dir);
            auto           it  = point_of.find(key);
            if (it != point_of.end())
                return it->second;

            const int j0 = dir == 2 ? j + 1 : j, k0 = k;
            const int jb = dir == 0 ? j + 1 : j, kb = dir == 0 ? k : k + 1;
            int   xk = -1;
            Vec2f p;
            int   e = -1;
            float u0 = 0.f, u1 = 0.f;
            if (k0 == 0 && kb == 0) { e = 0; u0 = float(j0) / float(N); u1 = float(jb) / float(N); }
            else if (j0 + k0 == N && jb + kb == N) { e = 1; u0 = float(k0) / float(N); u1 = float(kb) / float(N); }
            else if (j0 == 0 && jb == 0) { e = 2; u0 = float(N - k0) / float(N); u1 = float(N - kb) / float(N); }
            if (e >= 0) {
                const float lo = std::min(u0, u1), hi = std::max(u0, u1);
                for (const auto &[u, k_] : edge_x[size_t(e)])
                    if (u >= lo && u < hi) { if (xk >= 0) bad = true; xk = k_; }
                if (xk < 0) { bad = true; p = 0.5f * (at2(j0, k0) + at2(jb, kb)); }
                else p = to2(L.xs[size_t(xk)]->p);
            } else {
                const float fa = f[id(j0, k0)], fb = f[id(jb, kb)];
                const float s  = (fa != fb) ? std::clamp(fa / (fa - fb), 0.f, 1.f) : 0.5f;
                p = at2(j0, k0) + (at2(jb, kb) - at2(j0, k0)) * s;
            }
            const int pid = int(pts.size());
            pts.push_back(p);
            pt_x.push_back(xk);
            link.push_back({ -1, -1 });
            point_of.emplace(key, pid);
            return pid;
        };
        const auto connect = [&](int p, int q) {
            for (int *slot : { &link[size_t(p)][0], &link[size_t(p)][1] })
                if (*slot < 0) { *slot = q; break; }
            for (int *slot : { &link[size_t(q)][0], &link[size_t(q)][1] })
                if (*slot < 0) { *slot = p; break; }
        };


        for (int k = 0; k < N; ++k)
            for (int j = 0; j + k < N; ++j) {
                {
                    const int8_t s0_ = sign[id(j, k)], s1_ = sign[id(j + 1, k)], s2_ = sign[id(j, k + 1)];
                    if (!(s0_ == s1_ && s1_ == s2_)) {
                        std::vector<int> ends;
                        if (s0_ != s1_) ends.push_back(point_on(j, k, 0));
                        if (s0_ != s2_) ends.push_back(point_on(j, k, 1));
                        if (s1_ != s2_) ends.push_back(point_on(j, k, 2));
                        if (ends.size() == 2) connect(ends[0], ends[1]);
                    }
                }
                if (j + k + 1 < N) {
                    const int8_t s0_ = sign[id(j + 1, k)], s1_ = sign[id(j + 1, k + 1)], s2_ = sign[id(j, k + 1)];
                    if (!(s0_ == s1_ && s1_ == s2_)) {
                        std::vector<int> ends;
                        if (s0_ != s1_) ends.push_back(point_on(j + 1, k, 1));
                        if (s1_ != s2_) ends.push_back(point_on(j, k + 1, 0));
                        if (s0_ != s2_) ends.push_back(point_on(j, k, 2));
                        if (ends.size() == 2) connect(ends[0], ends[1]);
                    }
                }
            }
        if (bad)
            return;




        std::vector<int>     matched(static_cast<size_t>(n), -1);
        std::vector<Chain>   chains;
        std::vector<std::vector<Vec2f>> raws;
        std::vector<uint8_t> seen(pts.size(), 0);
        for (size_t s = 0; s < pts.size(); ++s) {
            if (seen[s] || pt_x[s] < 0)
                continue;
            std::vector<Vec2f> full;
            int prev = -1, cur = int(s);
            while (cur >= 0 && !seen[size_t(cur)]) {
                seen[size_t(cur)] = 1;
                full.push_back(pts[size_t(cur)]);
                if (pt_x[size_t(cur)] >= 0 && cur != int(s))
                    break;
                const int nx0 = link[size_t(cur)][0], nx1 = link[size_t(cur)][1];
                const int next = (nx0 != prev) ? nx0 : nx1;
                prev = cur; cur = next;
            }
            if (cur < 0 || pt_x[size_t(cur)] < 0 || cur == int(s))
                return;
            Chain ch;
            ch.i = pt_x[s];
            ch.j = pt_x[size_t(cur)];
            if (matched[size_t(ch.i)] >= 0 || matched[size_t(ch.j)] >= 0 || ch.i == ch.j)
                return;
            matched[size_t(ch.i)] = ch.j;
            matched[size_t(ch.j)] = ch.i;
            chains.push_back(std::move(ch));
            raws.push_back(std::move(full));
        }



        {
            const auto seg_dist = [](const Vec2f &a, const Vec2f &b, const Vec2f &c, const Vec2f &d) {
                const auto pt_seg = [](const Vec2f &q, const Vec2f &u, const Vec2f &v) {
                    const Vec2f uv = v - u;
                    const float t  = std::clamp((q - u).dot(uv) / std::max(uv.squaredNorm(), 1e-12f), 0.f, 1.f);
                    return (q - (u + uv * t)).norm();
                };
                return std::min({ pt_seg(a, c, d), pt_seg(b, c, d), pt_seg(c, a, b), pt_seg(d, a, b) });
            };
            for (size_t x = 0; x < raws.size(); ++x)
                for (size_t y = x + 1; y < raws.size(); ++y) {
                    float best = std::min(chains[x].clearance, chains[y].clearance);
                    for (size_t i = 0; i + 1 < raws[x].size(); ++i)
                        for (size_t j = 0; j + 1 < raws[y].size(); ++j)
                            best = std::min(best, seg_dist(raws[x][i], raws[x][i + 1], raws[y][j], raws[y][j + 1]));
                    chains[x].clearance = std::min(chains[x].clearance, best);
                    chains[y].clearance = std::min(chains[y].clearance, best);
                }
        }
        for (size_t c = 0; c < chains.size(); ++c) {
            Chain                    &ch   = chains[c];
            const std::vector<Vec2f> &full = raws[c];
            const float               tol  = std::min(h, 0.25f * ch.clearance);
            const float               gap  = std::min(seam_gap_mm, 0.5f * ch.clearance);
            std::vector<Vec2f>        simple;
            simplify_polyline(full, tol, simple);



            {
                std::vector<Vec2f> spaced;
                spaced.push_back(simple.front());
                for (size_t m = 1; m + 1 < simple.size(); ++m)
                    if ((simple[m] - spaced.back()).norm() >= gap && (simple[m] - simple.back()).norm() >= gap)
                        spaced.push_back(simple[m]);
                spaced.push_back(simple.back());
                simple = std::move(spaced);
            }
            const int    m_loop   = int(L.ids.size());
            const bool   adjacent = (L.xpos[size_t(ch.j)] - L.xpos[size_t(ch.i)] + m_loop) % m_loop == 1 ||
                                    (L.xpos[size_t(ch.i)] - L.xpos[size_t(ch.j)] + m_loop) % m_loop == 1;
            if (simple.size() == 2 && full.size() > 2 && adjacent) {



                const Vec2f d  = full.back() - full.front();
                const float dl = std::max(d.norm(), 1e-9f);
                size_t      bi = full.size() / 2;
                float       bd = 0.1f * tol;
                for (size_t m = 1; m + 1 < full.size(); ++m) {
                    const Vec2f r = full[m] - full.front();
                    const float dist = std::abs(d.x() * r.y() - d.y() * r.x()) / dl;
                    if (dist > bd) { bd = dist; bi = m; }
                }
                simple.insert(simple.begin() + 1, full[bi]);
            }




            for (bool again = true; again;) {
                again = false;
                for (size_t m = 1; m + 1 < simple.size(); ++m) {
                    const Vec2f d1 = simple[m] - simple[m - 1], d2 = simple[m + 1] - simple[m];
                    const float l1 = d1.norm(), l2 = d2.norm();
                    if (l1 < 1e-9f || l2 < 1e-9f) { simple.erase(simple.begin() + long(m)); again = true; break; }
                    if (d1.dot(d2) / (l1 * l2) > -0.5f)
                        continue;

                    const Vec2f &tip = simple[m];
                    const auto   dist_to = [&](const Vec2f &q, const Vec2f &a, const Vec2f &b) {
                        const Vec2f ab = b - a;
                        const float t  = std::clamp((q - a).dot(ab) / std::max(ab.squaredNorm(), 1e-12f), 0.f, 1.f);
                        return (q - (a + ab * t)).norm();
                    };
                    const float between = (l1 < l2) ? dist_to(simple[m - 1], tip, simple[m + 1]) : dist_to(simple[m + 1], simple[m - 1], tip);
                    if (between < seam_gap_mm) {
                        simple.erase(simple.begin() + long(m));
                        again = true;
                        break;
                    }
                }
            }
            for (size_t m = 1; m + 1 < simple.size(); ++m)
                ch.pts.push_back(to3(simple[m]));
            if (ch.pts.empty() && adjacent) {




                L.no_seam.push_back(ch.i);
                L.no_seam.push_back(ch.j);
            }
        }
        for (int k = 0; k < n; ++k)
            if (matched[size_t(k)] < 0)
                return;
        L.chains   = std::move(chains);
        L.partner  = std::move(matched);
        L.chain_of.assign(static_cast<size_t>(n), -1);
        for (size_t c = 0; c < L.chains.size(); ++c) {
            L.chain_of[size_t(L.chains[c].i)] = int(c);
            L.chain_of[size_t(L.chains[c].j)] = int(c);
        }
        L.ok = true;
    };
    tbb::parallel_for(tbb::blocked_range<size_t>(0, nt), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t t = r.begin(); t < r.end(); ++t)
            trace(t);
    });
    size_t cut_count = 0;
    for (size_t t = 0; t < nt; ++t) {
        cut_count += loops[t].ok ? 1 : 0;
        if (loops[t].ok)
            for (int k : loops[t].no_seam)
                loops[t].xs[size_t(k)]->no_seam = true;
    }




    for (size_t t = 0; t < nt; ++t) {
        Loop &L = loops[t];
        if (!L.ok)
            continue;
        for (auto &ch : L.chains) {
            Crossing &p = *L.xs[size_t(ch.i)], &q = *L.xs[size_t(ch.j)];
            p.used = q.used = true;
            const Vec3f p_next = ch.pts.empty() ? q.p : ch.pts.front();
            const Vec3f q_prev = ch.pts.empty() ? p.p : ch.pts.back();
            for (auto [c, toward] : { std::make_pair(&p, p_next), std::make_pair(&q, q_prev) }) {
                Vec3f       d  = toward - c->p;
                const float dl = d.norm();
                if (dl < 1e-12f) continue;
                const Vec3f ed = (pos[size_t(c->b)] - pos[size_t(c->a)]).normalized();
                c->sin_min     = std::min(c->sin_min, ed.cross(d / dl).norm());



                if (ch.pts.empty())
                    c->dt_max = std::min(c->dt_max, 0.45f * dl / std::max(std::abs(ed.dot(d / dl)), 1e-3f));
            }
        }
    }








    indexed_triangle_set out;
    out.vertices = pos;
    const float half_gap = 0.5f * seam_gap_mm;



    for (size_t t = 0; t < nt; ++t) {
        Loop &L = loops[t];
        if (!L.ok)
            continue;
        const auto &tri = mesh.indices[t];
        const Vec3f &A = pos[size_t(tri[0])], &B = pos[size_t(tri[1])], &C = pos[size_t(tri[2])];
        const Vec3f  U = (B - A).normalized(), V = L.N.cross(U);
        const Vec2f  a2(0.f, 0.f), b2((B - A).dot(U), (B - A).dot(V)), c2((C - A).dot(U), (C - A).dot(V));
        const float  det = (b2.x() - a2.x()) * (c2.y() - a2.y()) - (c2.x() - a2.x()) * (b2.y() - a2.y());
        const auto   bary2 = [&](const Vec3f &p) {
            const Vec2f q((p - A).dot(U), (p - A).dot(V));
            const float wb = ((q.x() - a2.x()) * (c2.y() - a2.y()) - (c2.x() - a2.x()) * (q.y() - a2.y())) / det;
            const float wc = ((b2.x() - a2.x()) * (q.y() - a2.y()) - (q.x() - a2.x()) * (b2.y() - a2.y())) / det;
            return Vec3f(1.f - wb - wc, wb, wc);
        };

        const auto inside_fraction = [&](const Vec3f &p, const Vec3f &off) {
            const Vec3f w0 = bary2(p), w1 = bary2(p + off);
            float       lambda = 1.f;
            for (int i = 0; i < 3; ++i)
                if (w1[i] < 0.f && w0[i] > w1[i])
                    lambda = std::min(lambda, w0[i] / (w0[i] - w1[i]));
            return 0.9f * std::max(lambda, 0.f);
        };
        const bool s0 = side_of(vh[size_t(mesh.indices[t][0])]);
        for (auto &ch : L.chains) {
            const Crossing &p = *L.xs[size_t(ch.i)], &q = *L.xs[size_t(ch.j)];
            const bool doubled = p.sharp && p.used && !p.no_seam && q.sharp && q.used && !q.no_seam;



            const bool  arc_high  = (ch.i % 2 == 0) ? !s0 : s0;
            const int   after_pos = L.xpos[size_t(ch.i)] + 1;
            const int   entry     = L.ids[size_t(after_pos) % L.ids.size()];
            const Vec3f ref       = entry >= 0 ? pos[size_t(entry)] : L.xs[size_t(-entry - 1)]->p;
            const size_t m = ch.pts.size();
            ch.lo.resize(m); ch.hi.resize(m);




            Vec3f d_first = q.p - p.p;
            for (size_t k = 0; k < m; ++k)
                if ((ch.pts[k] - p.p).norm() > 0.25f * seam_gap_mm) { d_first = ch.pts[k] - p.p; break; }
            d_first.normalize();
            const bool left_high = (L.N.cross(d_first).dot(ref - p.p) > 0.f) == arc_high;
            for (size_t k = 0; k < m; ++k) {
                if (!doubled) {
                    ch.lo[k] = ch.hi[k] = int(out.vertices.size());
                    out.vertices.push_back(ch.pts[k]);
                    continue;
                }
                const Vec3f prev = (k == 0) ? p.p : ch.pts[k - 1];
                const Vec3f next = (k + 1 == m) ? q.p : ch.pts[k + 1];
                Vec3f       d1 = (ch.pts[k] - prev).normalized(), d2 = (next - ch.pts[k]).normalized();
                Vec3f       n1 = L.N.cross(d1), n2 = L.N.cross(d2);
                Vec3f       bis = n1 + n2;
                float       bl  = bis.norm();
                Vec3f       off;
                if (bl < 1e-6f) off = n1;
                else {
                    bis /= bl;

                    const float cos_half = std::max(bis.dot(n1), 0.5f);
                    off = bis / cos_half;
                }
                if (!left_high) off = -off;


                const float ol    = off.norm();
                const float reach = std::min({ half_gap, 0.45f * ch.clearance,
                                               0.45f * (ch.pts[k] - prev).norm() / std::max(std::abs(off.dot(d1)) / ol, 1e-3f),
                                               0.45f * (next - ch.pts[k]).norm() / std::max(std::abs(off.dot(d2)) / ol, 1e-3f) });
                const Vec3f off_lo = -off * (reach * inside_fraction(ch.pts[k], -off * reach));
                const Vec3f off_hi = off * (reach * inside_fraction(ch.pts[k], off * reach));


                const auto place = [&](const Vec3f &o, bool high) -> int {
                    for (int step = 1; step <= 4; ++step) {
                        const Vec3f q = ch.pts[k] + o * (0.5f * float(step));
                        if (step > 2 && inside_fraction(ch.pts[k], q - ch.pts[k]) < 0.999f) break;
                        const float h = sampler(q, L.N) - iso;
                        if ((h > 0.f) == high && std::abs(h) >= 0.3f * range) {
                            out.vertices.push_back(q);
                            return int(out.vertices.size()) - 1;
                        }
                    }
                    return -1;
                };
                const int il = place(off_lo, false), ih = place(off_hi, true);
                if (il < 0 || ih < 0) {
                    out.vertices.resize(out.vertices.size() - (il >= 0 ? 1 : 0) - (ih >= 0 ? 1 : 0));
                    ch.lo[k] = ch.hi[k] = int(out.vertices.size());
                    out.vertices.push_back(ch.pts[k]);
                    continue;
                }
                ch.lo[k] = il; ch.hi[k] = ih;
            }



            if (m > 0)
                for (auto [c, k, other] : { std::make_tuple(&p, size_t(0), q.p), std::make_tuple(&q, m - 1, p.p) }) {
                    (void) other;
                    Crossing   &cr = const_cast<Crossing &>(*c);
                    const Vec3f d  = ch.pts[k] - cr.p;
                    const float dl = d.norm();
                    if (dl < 1e-9f) continue;
                    const Vec3f dn   = d / dl;
                    const float u_lo = (out.vertices[size_t(ch.lo[k])] - cr.p).dot(dn);
                    const float u_hi = (out.vertices[size_t(ch.hi[k])] - cr.p).dot(dn);
                    const Vec3f ed   = (pos[size_t(cr.b)] - pos[size_t(cr.a)]).normalized();
                    cr.dt_max        = std::min(cr.dt_max, 0.9f * std::max(std::min(u_lo, u_hi), 0.f) / std::max(std::abs(ed.dot(dn)), 1e-3f));
                }
        }
    }

    for (auto &kv : edges) {
        Edge &edge = kv.second;
        for (size_t i = 0; i < edge.xs.size(); ++i) {
            Crossing &c = edge.xs[i];
            if (!(c.sharp && c.used) || c.no_seam) {
                c.single = int(out.vertices.size());
                out.vertices.push_back(c.p);
                continue;
            }
            const float len = (pos[size_t(c.b)] - pos[size_t(c.a)]).norm();



            const float t_prev = (i == 0) ? 0.f : edge.xs[i - 1].t;
            const float t_next = (i + 1 == edge.xs.size()) ? 1.f : edge.xs[i + 1].t;
            float       dt     = half_gap / std::max(c.sin_min, 0.15f) / std::max(len, 1e-6f);
            dt                 = std::min({ dt, (c.t - t_prev) / 3.f, (t_next - c.t) / 3.f, c.dt_max / std::max(len, 1e-6f) });
            if (!(dt > 1e-6f)) {
                c.single = int(out.vertices.size());
                out.vertices.push_back(c.p);
                continue;
            }


            const bool before_high = side_of(vh[size_t(c.a)]) != (i % 2 == 1);








            const float lo_share = (i == 0) ? 0.9f : 0.49f, hi_share = (i + 1 == edge.xs.size()) ? 0.9f : 0.49f;
            const float t_lo_lim = c.t - std::min(lo_share * (c.t - t_prev), c.dt_max / std::max(len, 1e-6f));
            const float t_hi_lim = c.t + std::min(hi_share * (t_next - c.t), c.dt_max / std::max(len, 1e-6f));
            const auto place = [&](float sgn, bool high) -> int {
                for (int step = 1; step <= 6; ++step) {
                    const float tt = c.t + sgn * dt * (0.5f * float(step));
                    if (tt < t_lo_lim || tt > t_hi_lim) break;
                    const auto [q, qn] = sample_between(c.a, c.b, tt);
                    const float h = sampler(q, qn) - iso;
                    if ((h > 0.f) == high && std::abs(h) >= 0.3f * range) {
                        out.vertices.push_back(q);
                        return int(out.vertices.size()) - 1;
                    }
                }
                return -1;
            };
            const int na = place(-1.f, before_high), nb = place(+1.f, !before_high);
            if (na < 0 || nb < 0) {
                out.vertices.resize(out.vertices.size() - (na >= 0 ? 1 : 0) - (nb >= 0 ? 1 : 0));
                c.single = int(out.vertices.size());
                out.vertices.push_back(c.p);
                continue;
            }
            c.near_a = na; c.near_b = nb;
            c.lo = before_high ? c.near_b : c.near_a;
            c.hi = before_high ? c.near_a : c.near_b;
        }
    }


    out.indices.reserve(nt * 2);
    std::vector<int> source;
    source.reserve(nt * 2);
    const auto emit = [&](int a, int b, int c, int src) { out.indices.emplace_back(a, b, c); source.push_back(src); };
    const auto copy_for = [&](const Crossing &c, bool high) { return c.single >= 0 ? c.single : (high ? c.hi : c.lo); };





    const std::function<void(std::array<std::vector<int>, 3>, int)> split_tri =
        [&](std::array<std::vector<int>, 3> side, int src) {
            for (int e = 0; e < 3; ++e)
                if (side[size_t(e)].size() > 1) {
                    const auto &S = side[size_t(e)];
                    const int   a = S[0], x = S[1], c = side[size_t((e + 2) % 3)][0];
                    std::vector<int> rest(S.begin() + 1, S.end());
                    split_tri({ std::vector<int>{ a }, std::vector<int>{ x }, side[size_t((e + 2) % 3)] }, src);
                    split_tri({ std::move(rest), side[size_t((e + 1) % 3)], std::vector<int>{ c } }, src);
                    return;
                }
            emit(side[0][0], side[1][0], side[2][0], src);
        };

    const auto emit_wall = [&](const Crossing &p, const Chain &ch, const Crossing &q, const Vec3f &N, int src) {
        std::vector<int> lo, hi;
        lo.push_back(copy_for(p, false)); hi.push_back(copy_for(p, true));
        for (size_t k = 0; k < ch.pts.size(); ++k) { lo.push_back(ch.lo[k]); hi.push_back(ch.hi[k]); }
        lo.push_back(copy_for(q, false)); hi.push_back(copy_for(q, true));
        auto tri_up = [&](int i, int j, int k) {
            if (i == j || j == k || k == i) return;
            const Vec3f &A = out.vertices[size_t(i)], &B = out.vertices[size_t(j)], &C = out.vertices[size_t(k)];
            if ((B - A).cross(C - A).dot(N) >= 0.f) emit(i, j, k, src); else emit(i, k, j, src);
        };
        for (size_t k = 0; k + 1 < lo.size(); ++k) {
            tri_up(lo[k], lo[k + 1], hi[k + 1]);
            tri_up(lo[k], hi[k + 1], hi[k]);
        }
    };

    for (size_t t = 0; t < nt; ++t) {
        const auto &tri = mesh.indices[t];
        const Loop &L   = loops[t];
        const int   n   = int(L.xs.size());
        if (n == 0) { emit(tri[0], tri[1], tri[2], int(t)); continue; }
        const size_t m  = L.ids.size();
        const bool   s0 = side_of(vh[size_t(tri[0])]);

        if (L.ok) {





            const Vec3f &A = pos[size_t(tri[0])], &B = pos[size_t(tri[1])];
            const Vec3f  U = (B - A).normalized(), V = L.N.cross(U);
            std::vector<Vec2f> pts2;
            std::vector<int>   ids2;
            const auto push = [&](int id) {
                const Vec3f &p = out.vertices[size_t(id)];
                pts2.emplace_back((p - A).dot(U), (p - A).dot(V));
                ids2.push_back(id);
            };

            const auto push_chain = [&](int c, int from, bool S) {
                const Chain &ch = L.chains[size_t(c)];
                const auto  &cp = S ? ch.hi : ch.lo;
                if (from == ch.i) for (size_t k = 0; k < cp.size(); ++k) push(cp[k]);
                else              for (size_t k = cp.size(); k-- > 0;) push(cp[k]);
            };
            const auto side_after = [&](int k) { return (k % 2 == 0) ? !s0 : s0; };
            std::vector<std::array<bool, 2>> consumed(static_cast<size_t>(n), { false, false });
            for (int k = 0; k < n; ++k) {
                const bool S = side_after(k);
                if (consumed[size_t(k)][S])
                    continue;
                pts2.clear(); ids2.clear();
                push(copy_for(*L.xs[size_t(k)], S));
                consumed[size_t(k)][S] = true;
                size_t i = size_t(L.xpos[size_t(k)] + 1) % m;
                for (size_t guard = 0; guard < 2 * m; ++guard) {
                    const int id = L.ids[i];
                    if (id >= 0) { push(id); i = (i + 1) % m; continue; }
                    const int c = -id - 1;
                    consumed[size_t(c)][S] = true;
                    push(copy_for(*L.xs[size_t(c)], S));
                    if (c == L.partner[size_t(k)])
                        break;

                    const int pc = L.partner[size_t(c)];
                    push_chain(L.chain_of[size_t(c)], c, S);
                    consumed[size_t(pc)][S] = true;
                    push(copy_for(*L.xs[size_t(pc)], S));
                    if (pc == L.partner[size_t(k)])
                        break;
                    i = size_t(L.xpos[size_t(pc)] + 1) % m;
                }

                push_chain(L.chain_of[size_t(k)], L.partner[size_t(k)], S);
                std::vector<int> poly(ids2.size());
                std::iota(poly.begin(), poly.end(), 0);
                ear_clip(pts2, poly, [&](int a, int b, int c) { emit(ids2[size_t(a)], ids2[size_t(b)], ids2[size_t(c)], int(t)); });
            }
            for (const auto &ch : L.chains)
                emit_wall(*L.xs[size_t(ch.i)], ch, *L.xs[size_t(ch.j)], L.N, int(t));
            continue;
        }


        std::array<std::vector<int>, 3> side;
        int e = -1;
        for (size_t i = 0; i < m; ++i) {
            if (L.ids[i] >= 0) { ++e; side[size_t(e)].push_back(L.ids[i]); continue; }
            const Crossing &c = *L.xs[size_t(-L.ids[i] - 1)];
            if (c.single >= 0) { side[size_t(e)].push_back(c.single); continue; }
            const bool from_a = tri[e] == c.a;
            side[size_t(e)].push_back(from_a ? c.near_a : c.near_b);
            side[size_t(e)].push_back(from_a ? c.near_b : c.near_a);
        }
        split_tri(std::move(side), int(t));
    }



    {
        size_t inverted = 0;
        for (size_t i = 0; i < out.indices.size(); ++i) {
            const auto  &f  = out.indices[i];
            const auto  &g  = mesh.indices[size_t(source[i])];
            const Vec3f  n  = (out.vertices[size_t(f[1])] - out.vertices[size_t(f[0])]).cross(out.vertices[size_t(f[2])] - out.vertices[size_t(f[0])]);
            const Vec3f  ns = (mesh.vertices[size_t(g[1])] - mesh.vertices[size_t(g[0])]).cross(mesh.vertices[size_t(g[2])] - mesh.vertices[size_t(g[0])]);
            inverted += n.dot(ns) < 0.f && n.norm() > 1e-7f;
        }
        if (double(inverted) > 0.002 * double(out.indices.size()))
            return passthrough();
    }
    if (out_source) *out_source = std::move(source);
    if (out_cut_count) *out_cut_count = cut_count;
    return out;
}


}
