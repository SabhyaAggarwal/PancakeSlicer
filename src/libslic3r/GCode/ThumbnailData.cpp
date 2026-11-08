#include "ThumbnailData.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <admesh/stl.h>
#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r {

void ThumbnailData::set(unsigned int w, unsigned int h)
{
    if ((w == 0) || (h == 0))
        return;

    if ((width != w) || (height != h))
    {
        width = w;
        height = h;
        pixels.clear();
        pixels = std::vector<unsigned char>(width * height * 4, 255);
    }
}

void ThumbnailData::reset()
{
    width = 0;
    height = 0;
    pixels.clear();
}

bool ThumbnailData::is_valid() const
{
    return (width != 0) && (height != 0) && ((unsigned int)pixels.size() == 4 * width * height);
}

ThumbnailData render_thumbnail(const indexed_triangle_set &mesh, unsigned width, unsigned height, bool top, const std::string &colour)
{
    unsigned char rgb[3] = {0x00, 0xAE, 0x42};
    if (colour.size() >= 7 && colour.front() == '#')
        for (size_t c = 0; c < 3; ++c)
            rgb[c] = static_cast<unsigned char>(std::strtoul(colour.substr(1 + 2 * c, 2).c_str(), nullptr, 16));
    ThumbnailData thumb;
    thumb.set(width, height);
    std::fill(thumb.pixels.begin(), thumb.pixels.end(), 0);
    const double c = top ? 0. : std::sqrt(0.5);
    const Vec3d  right(1., 0., 0.), up = top ? Vec3d(0., 1., 0.) : Vec3d(0., c, c), toward = right.cross(up);
    const Vec3d  light = Vec3d(-0.3, -0.5, 1.).normalized();
    std::vector<Vec3d> p(mesh.vertices.size());
    BoundingBoxf       box;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec3d v = mesh.vertices[i].cast<double>();
        p[i]          = Vec3d(v.dot(right), v.dot(up), v.dot(toward));
        box.merge(Vec2d(p[i].x(), p[i].y()));
    }
    if (! box.defined)
        return thumb;
    const double scale = 0.9 * std::min(width / std::max(box.size().x(), 1e-3), height / std::max(box.size().y(), 1e-3));
    const Vec2d  mid   = box.center();
    for (Vec3d &q : p)
        q = Vec3d((q.x() - mid.x()) * scale + 0.5 * width, (q.y() - mid.y()) * scale + 0.5 * height, q.z());
    std::vector<double> depth(size_t(width) * height, -std::numeric_limits<double>::max());
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d &a = p[f[0]], &b = p[f[1]], &d = p[f[2]];
        const double area = (b.x() - a.x()) * (d.y() - a.y()) - (d.x() - a.x()) * (b.y() - a.y());
        if (std::abs(area) < 1e-12)
            continue;
        const Vec3d  n     = (mesh.vertices[f[1]] - mesh.vertices[f[0]]).cast<double>().cross((mesh.vertices[f[2]] - mesh.vertices[f[0]]).cast<double>()).normalized();
        const double shade = 0.35 + 0.65 * std::abs(n.dot(light));
        const int    x0 = std::max(0, int(std::floor(std::min({ a.x(), b.x(), d.x() })))), x1 = std::min(int(width) - 1, int(std::ceil(std::max({ a.x(), b.x(), d.x() }))));
        const int    y0 = std::max(0, int(std::floor(std::min({ a.y(), b.y(), d.y() })))), y1 = std::min(int(height) - 1, int(std::ceil(std::max({ a.y(), b.y(), d.y() }))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double w0 = ((b.x() - px) * (d.y() - py) - (d.x() - px) * (b.y() - py)) / area;
                const double w1 = ((d.x() - px) * (a.y() - py) - (a.x() - px) * (d.y() - py)) / area;
                const double w2 = 1. - w0 - w1;
                if (w0 < 0. || w1 < 0. || w2 < 0.)
                    continue;
                const double z = w0 * a.z() + w1 * b.z() + w2 * d.z();
                double      &zb = depth[size_t(y) * width + x];
                if (z <= zb)
                    continue;
                zb                = z;
                unsigned char *px4 = &thumb.pixels[4 * (size_t(y) * width + x)];
                for (size_t c = 0; c < 3; ++c)
                    px4[c] = static_cast<unsigned char>(rgb[c] * shade);
                px4[3] = 255;
            }
    }
    return thumb;
}

}
