#include "PatchTools.hpp"

#include "AutoMode.hpp"
#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "Seams.hpp"
#include "ShellPaths.hpp"
#include "SurfaceClip.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <tuple>
#include <numeric>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <cstdint>
#include <functional>
#include <string>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

void throw_if_canceled(const PrintObject &object)
{
    if (object.print()->canceled())
        throw CanceledException();
}

double vertical_ray_hit(const sla::IndexedMesh &mesh, const Vec2d &p, double z, double dir_z, bool from_inside)
{
    double best = std::numeric_limits<double>::quiet_NaN();
    int    n    = 0;
    for (const Vec2d &nudge : { Vec2d(0., 0.), Vec2d(1e-4, 0.618e-4), Vec2d(-0.707e-4, 1e-4),
                              Vec2d(-1e-4, -0.618e-4), Vec2d(0.707e-4, -1e-4) }) {
        const Vec2d q   = p + nudge;
        const auto  hit = mesh.query_ray_hit(Vec3d(q.x(), q.y(), z), Vec3d(0., 0., dir_z));
        if (hit.is_hit() && hit.normal().normalized().z() * dir_z * (from_inside ? -1. : 1.) < -0.05 &&
            !((hit.position().z() - best) * dir_z >= 0.))
            best = hit.position().z();
        if (++n >= 2 && !std::isnan(best))
            break;
    }
    return best;
}

bool requests_conformal_shells(const Print &print)
{
    for (const PrintObject *object : print.objects()) {
        if (object->shared_regions() == nullptr) {
            const auto &config = print.default_region_config();
            if (top_shells_enabled(config, object->config()) || bottom_shells_enabled(config, object->config()))
                return true;
            continue;
        }
        for (size_t r = 0; r < object->num_printing_regions(); ++r) {
            const auto &config = object->printing_region(r).config();
            if (top_shells_enabled(config, object->config()) || bottom_shells_enabled(config, object->config()))
                return true;
        }
    }
    return false;
}

bool sequential_shell_tower(const Print &print)
{
    return print.config().print_sequence == PrintSequence::ByObject &&
           requests_conformal_shells(print) && print.has_wipe_tower();
}

bool vase_with_conformal_roof(const PrintObject &object)
{
    if (!object.print()->config().spiral_mode)
        return false;
    for (size_t r = 0; r < object.num_printing_regions(); ++r)
        if (top_shells_enabled(object.printing_region(r).config(), object.config()))
            return true;
    return false;
}

void prepare_vase_closure(PrintObject &object)
{
    if (!vase_with_conformal_roof(object))
        return;
    for (size_t r = 0; r < object.num_printing_regions(); ++r) {
        const auto &config = object.printing_region(r).config();
        if (!top_shells_enabled(config, object.config()))
            continue;
        size_t roof = object.layers().size();
        for (size_t i = 0; i < object.layers().size(); ++i)
            for (const Surface &surface : object.layers()[i]->get_region(int(r))->slices.surfaces)
                if (surface.is_top())
                    roof = std::min(roof, i);
        for (size_t i = roof; i < object.layers().size(); ++i)
            for (Surface &surface : object.layers()[i]->get_region(int(r))->fill_surfaces.surfaces) {
                surface.surface_type = i == roof ? stInternalBridge : stInternalSolid;
                if (i == roof)
                    surface.bridge_angle = Geometry::deg2rad(config.infill_direction.value);
            }
    }
}

double automatic_shell_height(const PrintObject &object)
{
    const ModelObject &model = *object.model_object();
    if (!model.layer_height_profile.empty() || !model.layer_config_ranges.empty())
        return std::min(object.slicing_parameters().first_object_layer_height, object.config().layer_height.value);
    double height = std::numeric_limits<double>::infinity();
    for (const Layer *layer : object.layers())
        if (layer->height > 0.)
            height = std::min(height, layer->height);
    return std::isfinite(height) ? height : object.config().layer_height.value;
}

double smallest_nozzle(const PrintConfig &config)
{
    double out = 0.;
    for (double d : config.nozzle_diameter.values)
        if (d > 0. && (out == 0. || d < out))
            out = d;
    return out > 0. ? out : TunedNozzle;
}

double bead_scale(const PrintConfig &config) { return smallest_nozzle(config) / TunedNozzle; }

ObjectMesh::ObjectMesh(const PrintObject &object) : mesh(object.object_frame_mesh(ground_offset)), imesh(mesh)
{
    imesh.ground_level_offset(ground_offset);
    imesh.enable_column_cache();
}

const ObjectMesh &object_mesh(const PrintObject &object, ConformalResult &result)
{
    if (!result.mesh)
        result.mesh = std::make_shared<ObjectMesh>(object);
    return *result.mesh;
}

std::function<bool(const Vec2d &, double)> material_below(const PrintObject &object)
{
    return [&object](const Vec2d &p, double z) {
        const auto layers = object.layers();
        auto it = std::upper_bound(layers.begin(), layers.end(), z - 0.01,
                                   [](double z, const Layer *l) { return z < l->print_z - 0.5 * l->height; });
        if (it == layers.begin())
            return false;
        const Layer &layer = **std::prev(it);
        const Point  pt    = Point::new_scale(p.x(), p.y());
        for (size_t i = 0; i < layer.lslices.size(); ++i)
            if ((i >= layer.lslices_bboxes.size() || layer.lslices_bboxes[i].contains(pt)) && layer.lslices[i].contains(pt))
                return true;
        return false;
    };
}

HeightField object_grid(const PrintObject &object, double pitch, Polygons *silhouette)
{
    BoundingBox obj_bbox;
    for (const Layer *layer : object.layers())
        for (const ExPolygon &expoly : layer->lslices)
            obj_bbox.merge(get_extents(expoly.contour));
    if (silhouette != nullptr) {
        constexpr size_t Chunk = 16;
        const size_t     n     = object.layers().size();
        std::vector<Polygons> parts((n + Chunk - 1) / Chunk);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, parts.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t c = range.begin(); c < range.end(); ++c) {
                Polygons contours;
                for (size_t l = c * Chunk; l < std::min(n, (c + 1) * Chunk); ++l)
                    for (const ExPolygon &expoly : object.layers()[l]->lslices)
                        contours.emplace_back(expoly.contour);
                parts[c] = to_polygons(union_ex(contours));
            }
        });
        while (parts.size() > 1) {
            std::vector<Polygons> merged((parts.size() + 1) / 2);
            tbb::parallel_for(tbb::blocked_range<size_t>(0, merged.size()), [&](const tbb::blocked_range<size_t> &range) {
                for (size_t i = range.begin(); i < range.end(); ++i) {
                    merged[i] = std::move(parts[2 * i]);
                    if (2 * i + 1 < parts.size()) {
                        append(merged[i], std::move(parts[2 * i + 1]));
                        merged[i] = to_polygons(union_ex(merged[i]));
                    }
                }
            });
            parts = std::move(merged);
        }
        if (!parts.empty())
            append(*silhouette, std::move(parts.front()));
    }
    const BoundingBoxf bbf(unscale(obj_bbox.min), unscale(obj_bbox.max));
    const BoundingBoxf grid_bbox(Vec2d(std::floor(bbf.min.x() / pitch) - 1., std::floor(bbf.min.y() / pitch) - 1.) * pitch,
                                 Vec2d(std::ceil(bbf.max.x() / pitch) + 1., std::ceil(bbf.max.y() / pitch) + 1.) * pitch);
    return HeightField(grid_bbox, pitch);
}

Mask rasterize(const ExPolygons &area, const HeightField &grid)
{
    Mask                             mask(size_t(grid.nx()) * grid.ny(), 0);
    std::vector<std::vector<double>> crossings(grid.ny());
    const double                     p  = grid.pitch();
    const Vec2d                      o  = grid.origin();
    auto add_polygon = [&](const Polygon &poly) {
        const Points &pts = poly.points;
        for (size_t i = 0; i < pts.size(); ++i) {
            Vec2d a = unscale(pts[i]), b = unscale(pts[(i + 1) % pts.size()]);
            if (a.y() == b.y())
                continue;
            if (a.y() > b.y())
                std::swap(a, b);
            const int iy0 = std::max(0, int(std::ceil((a.y() - o.y()) / p)) - 1);
            const int iy1 = std::min(grid.ny() - 1, int(std::ceil((b.y() - o.y()) / p)));
            for (int iy = iy0; iy <= iy1; ++iy) {
                const double y = o.y() + iy * p;
                if (y < a.y() || y >= b.y())
                    continue;
                crossings[iy].emplace_back(a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
            }
        }
    };
    for (const ExPolygon &expoly : area) {
        add_polygon(expoly.contour);
        for (const Polygon &hole : expoly.holes)
            add_polygon(hole);
    }
    for (int iy = 0; iy < grid.ny(); ++iy) {
        std::vector<double> &xs = crossings[iy];
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            int ix0 = std::max(0, int(std::ceil((xs[i] - o.x()) / p)) - 1);
            int ix1 = std::min(grid.nx() - 1, int(std::floor((xs[i + 1] - o.x()) / p)) + 1);
            for (; ix0 <= ix1 && o.x() + ix0 * p < xs[i]; ++ix0) ;
            for (; ix1 >= ix0 && o.x() + ix1 * p > xs[i + 1]; --ix1) ;
            for (int ix = ix0; ix <= ix1; ++ix)
                mask[size_t(iy) * grid.nx() + ix] = 1;
        }
    }
    return mask;
}

ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid)
{
    return mask_to_expolygons(mask, grid, 0, 0, grid.nx() - 1, grid.ny() - 1);
}

namespace {
Polygon run_cell(const HeightField &grid, int y, int x0, int x1)
{
    const double  h   = 0.5 * grid.pitch();
    const coord_t eps = 10;
    const Vec2d   lo  = grid.node(x0, y) - Vec2d(h, h), hi = grid.node(x1, y) + Vec2d(h, h);
    const Point   plo = Point::new_scale(lo.x(), lo.y()) - Point(eps, eps);
    const Point   phi = Point::new_scale(hi.x(), hi.y()) + Point(eps, eps);
    return Polygon({ plo, Point(phi.x(), plo.y()), phi, Point(plo.x(), phi.y()) });
}
}

ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid, int ix0, int iy0, int ix1, int iy1)
{
    Polygons cells;
    ix0 = std::max(ix0, 0), iy0 = std::max(iy0, 0), ix1 = std::min(ix1, grid.nx() - 1), iy1 = std::min(iy1, grid.ny() - 1);
    for (int iy = iy0; iy <= iy1; ++iy)
        for (int ix = ix0; ix <= ix1;) {
            if (!mask[size_t(iy) * grid.nx() + ix]) {
                ++ix;
                continue;
            }
            int end = ix;
            while (end + 1 <= ix1 && mask[size_t(iy) * grid.nx() + end + 1])
                ++end;
            cells.emplace_back(run_cell(grid, iy, ix, end));
            ix = end + 1;
        }
    return union_ex(cells);
}

ExPolygons runs_to_expolygons(const std::vector<RunGroups::Run> &runs, const HeightField &grid)
{
    Polygons cells;
    cells.reserve(runs.size());
    for (const RunGroups::Run &r : runs)
        cells.emplace_back(run_cell(grid, r.y, r.x0, r.x1));
    return union_ex(cells);
}

ExPolygons positive_region(const HeightField &f)
{
    Polygons  polys;
    const int nx = f.nx(), ny = f.ny();
    auto val = [&f](int ix, int iy) { const float v = f.at(ix, iy); return std::isnan(v) ? -1. : double(v); };
    auto pt  = [&f](double ix, double iy) {
        const Vec2d p = f.origin() + f.pitch() * Vec2d(ix, iy);
        return Point::new_scale(p.x(), p.y());
    };
    for (int iy = 0; iy + 1 < ny; ++iy) {
        int run = -1;
        for (int ix = 0; ix + 1 < nx; ++ix) {
            const int    cx[4] = { ix, ix + 1, ix + 1, ix };
            const int    cy[4] = { iy, iy, iy + 1, iy + 1 };
            double       v[4];
            int          inside = 0;
            for (int c = 0; c < 4; ++c)
                inside += (v[c] = val(cx[c], cy[c])) >= 0.;
            if (inside == 4) {
                if (run < 0)
                    run = ix;
                continue;
            }
            if (run >= 0) {
                polys.emplace_back(Points{ pt(run, iy), pt(ix, iy), pt(ix, iy + 1), pt(run, iy + 1) });
                run = -1;
            }
            if (inside == 0)
                continue;
            Points cell;
            for (int a = 0; a < 4; ++a) {
                const int b = (a + 1) % 4;
                if (v[a] >= 0.)
                    cell.emplace_back(pt(cx[a], cy[a]));
                if ((v[a] >= 0.) != (v[b] >= 0.)) {
                    const bool a_lo = cy[a] < cy[b] || (cy[a] == cy[b] && cx[a] < cx[b]);
                    const int  lo = a_lo ? a : b, hi = a_lo ? b : a;
                    const double t = v[lo] / (v[lo] - v[hi]);
                    cell.emplace_back(pt(cx[lo] + t * (cx[hi] - cx[lo]), cy[lo] + t * (cy[hi] - cy[lo])));
                }
            }
            if (cell.size() >= 3)
                polys.emplace_back(std::move(cell));
        }
        if (run >= 0)
            polys.emplace_back(Points{ pt(run, iy), pt(nx - 1, iy), pt(nx - 1, iy + 1), pt(run, iy + 1) });
    }
    return union_ex(polys);
}

HeightField crop(const HeightField &src, int ix0, int iy0, int ix1, int iy1)
{
    HeightField out(BoundingBoxf(src.node(ix0, iy0), src.node(ix1, iy1)), src.pitch());
    for (int iy = 0; iy < out.ny(); ++iy)
        for (int ix = 0; ix < out.nx(); ++ix)
            out.at(ix, iy) = src.at(ix0 + ix, iy0 + iy);
    return out;
}

namespace {

using Quadric = Eigen::Matrix<double, 6, 1>;

Quadric quadric_basis(double dx, double dy) { return Quadric(1., dx, dy, dx * dx, dx * dy, dy * dy); }

template<class Weight> bool local_quadratic(const HeightField &f, int ix, int iy, int r, const Weight &weight, Quadric &c, int stride = 1)
{
    Eigen::Matrix<double, 6, 6> A = Eigen::Matrix<double, 6, 6>::Zero();
    Quadric                     b = Quadric::Zero();
    int                         n = 0;
    for (int dy = -r; dy <= r; dy += stride)
        for (int dx = -r; dx <= r; dx += stride) {
            const int x = ix + dx, y = iy + dy;
            if (x < 0 || y < 0 || x >= f.nx() || y >= f.ny() || std::isnan(f.at(x, y)))
                continue;
            const double w = weight(dx, dy, double(f.at(x, y)));
            if (!(w > 0.))
                continue;
            const Quadric u = quadric_basis(dx, dy);
            A.noalias() += w * u * u.transpose();
            b.noalias() += w * f.at(x, y) * u;
            ++n;
        }
    if (n < 12)
        return false;
    const auto ldlt = A.ldlt();
    if (ldlt.info() != Eigen::Success || !ldlt.isPositive())
        return false;
    c = ldlt.solve(b);
    return c.allFinite();
}

}

double smooth_scan_noise(HeightField &surface, const std::function<void()> &throw_if_canceled)
{
    constexpr double ScanNoiseFloor = 0.006;
    constexpr double NoiseScaleRatio = 3.5;
    constexpr double PatternCorrelation = 0.6;
    const double     pitch          = surface.pitch();
    const int        nx = surface.nx(), ny = surface.ny();
    const double     wall = std::tan(Geometry::deg2rad(60.)) * 4. * pitch;
    auto residual_field = [&](int stride) {
        std::vector<float> residual(size_t(nx) * ny, HeightField::NoData);
        const int          r = 2 * stride;
        tbb::parallel_for(tbb::blocked_range<int>(0, ny), [&](const tbb::blocked_range<int> &range) {
            for (int iy = range.begin(); iy < range.end(); ++iy)
                for (int ix = 0; ix < nx; ++ix) {
                    const float z = surface.at(ix, iy);
                    if (std::isnan(z) || ix < r || iy < r || ix + r >= nx || iy + r >= ny)
                        continue;
                    float lo = z, hi = z;
                    bool  full = true;
                    for (int dy = -r; dy <= r && full; dy += stride)
                        for (int dx = -r; dx <= r; dx += stride) {
                            const float v = surface.at(ix + dx, iy + dy);
                            full = full && !std::isnan(v);
                            lo = std::min(lo, v), hi = std::max(hi, v);
                        }
                    if (!full || hi - lo > stride * wall)
                        continue;
                    if (Quadric c; local_quadratic(surface, ix, iy, r, [](int, int, double) { return 1.; }, c, stride))
                        residual[size_t(iy) * nx + ix] = float(z - c(0));
                }
        });
        return residual;
    };
    auto robust_sigma = [](std::vector<float> &abs_res) {
        std::nth_element(abs_res.begin(), abs_res.begin() + abs_res.size() / 2, abs_res.end());
        return 1.4826 * abs_res[abs_res.size() / 2] / 0.92;
    };
    const int tile = std::max(5, int(std::lround(6. / pitch)));
    auto      noise = [&](const std::vector<float> &residual) {
        std::vector<float> tiles, all, cell;
        for (int ty = 0; ty < ny; ty += tile)
            for (int tx = 0; tx < nx; tx += tile) {
                cell.clear();
                for (int iy = ty; iy < std::min(ny, ty + tile); ++iy)
                    for (int ix = tx; ix < std::min(nx, tx + tile); ++ix)
                        if (const float r = residual[size_t(iy) * nx + ix]; !std::isnan(r))
                            cell.emplace_back(std::abs(r));
                all.insert(all.end(), cell.begin(), cell.end());
                if (2 * cell.size() >= size_t(tile) * tile)
                    tiles.emplace_back(float(robust_sigma(cell)));
            }
        return !tiles.empty() ? robust_sigma(tiles) : all.size() >= 2000 ? robust_sigma(all) : 0.;
    };
    auto periodicity = [&](const std::vector<float> &residual) {
        const int           lags = std::max(2, int(std::lround(2. / pitch))), side = 2 * lags + 1;
        std::vector<double> acf(size_t(side) * side);
        std::vector<int>    lobe;
        double              sum = 0., weight = 0.;
        for (int ty = 0; ty + tile <= ny; ty += tile)
            for (int tx = 0; tx + tile <= nx; tx += tile) {
                auto   at = [&](int x, int y) { return residual[size_t(ty + y) * nx + tx + x]; };
                double mean = 0.;
                int    n    = 0;
                for (int y = 0; y < tile; ++y)
                    for (int x = 0; x < tile; ++x)
                        if (const float r = at(x, y); !std::isnan(r))
                            mean += r, ++n;
                if (2 * n < tile * tile)
                    continue;
                mean /= n;
                for (int dy = -lags; dy <= lags; ++dy)
                    for (int dx = -lags; dx <= lags; ++dx) {
                        double c = 0.;
                        int    m = 0;
                        for (int y = std::max(0, -dy); y < std::min(tile, tile - dy); ++y)
                            for (int x = std::max(0, -dx); x < std::min(tile, tile - dx); ++x)
                                if (const float a = at(x, y), b = at(x + dx, y + dy); !std::isnan(a) && !std::isnan(b))
                                    c += (a - mean) * (b - mean), ++m;
                        acf[size_t(dy + lags) * side + dx + lags] = 10 * m >= 3 * n ? c / m : std::nan("");
                    }
                const double energy = acf[size_t(lags) * side + lags];
                if (!(energy > 0.))
                    continue;
                lobe.assign(size_t(side) * side, 0);
                std::vector<int> stack{ lags * side + lags };
                lobe[size_t(stack.back())] = 1;
                while (!stack.empty()) {
                    const int i = stack.back();
                    stack.pop_back();
                    for (const int j : { i - 1, i + 1, i - side, i + side })
                        if (j >= 0 && j < side * side && std::abs(j % side - i % side) <= 1 && !lobe[size_t(j)] && acf[size_t(j)] > 0.)
                            lobe[size_t(j)] = 1, stack.emplace_back(j);
                }
                double peak = -1.;
                for (size_t i = 0; i < acf.size(); ++i)
                    if (!lobe[i] && !std::isnan(acf[i]))
                        peak = std::max(peak, acf[i] / energy);
                sum += peak * energy * n;
                weight += energy * n;
            }
        return weight > 0. ? sum / weight : 0.;
    };
    const std::vector<float> residual = residual_field(1);
    const double             sigma    = noise(residual);
    if (sigma <= ScanNoiseFloor || noise(residual_field(2)) > NoiseScaleRatio * sigma || periodicity(residual) > PatternCorrelation)
        return 0.;
    if (throw_if_canceled)
        throw_if_canceled();

    constexpr double Radii[] = { 0.6, 0.9, 1.3, 1.8, 2.5 };
    constexpr double Kappa   = 2.;
    struct Disc
    {
        int                 r, stride;
        std::vector<double> kernel;
        double              weight(int dx, int dy) const { return kernel[size_t(dy + r) * (2 * r + 1) + dx + r]; }
    };
    std::vector<Disc> discs;
    for (double radius : Radii) {
        Disc d;
        d.r      = std::max(2, int(std::ceil(radius / pitch)));
        d.stride = std::max(1, int(std::ceil(d.r / 8.)));
        d.kernel.assign(size_t(2 * d.r + 1) * (2 * d.r + 1), 0.);
        const double s2 = 2. * std::pow(0.5 * radius / pitch, 2);
        for (int dy = -d.r; dy <= d.r; ++dy)
            for (int dx = -d.r; dx <= d.r; ++dx)
                if (dx * dx + dy * dy <= d.r * d.r)
                    d.kernel[size_t(dy + d.r) * (2 * d.r + 1) + dx + d.r] = std::exp(-(dx * dx + dy * dy) / s2);
        discs.emplace_back(std::move(d));
    }
    HeightField out = surface;
    tbb::parallel_for(tbb::blocked_range<int>(0, ny), [&](const tbb::blocked_range<int> &range) {
        if (throw_if_canceled)
            throw_if_canceled();
        std::vector<double> errors;
        for (int iy = range.begin(); iy < range.end(); ++iy)
            for (int ix = 0; ix < nx; ++ix) {
                const float z = surface.at(ix, iy);
                if (std::isnan(z))
                    continue;
                double lo = -std::numeric_limits<double>::infinity(), hi = -lo, best = z;
                for (const Disc &d : discs) {
                    Quadric first;
                    if (!local_quadratic(surface, ix, iy, d.r, [&d](int dx, int dy, double) { return d.weight(dx, dy); }, first, d.stride))
                        break;
                    errors.clear();
                    for (int dy = -d.r; dy <= d.r; dy += d.stride)
                        for (int dx = -d.r; dx <= d.r; dx += d.stride)
                            if (const int x = ix + dx, y = iy + dy; x >= 0 && y >= 0 && x < nx && y < ny && d.weight(dx, dy) > 0.)
                                if (const float v = surface.at(x, y); !std::isnan(v))
                                    errors.emplace_back(std::abs(v - first.dot(quadric_basis(dx, dy))));
                    std::nth_element(errors.begin(), errors.begin() + errors.size() / 2, errors.end());
                    const double cut = std::max(4.685 * 1.4826 * errors[errors.size() / 2], 2. * sigma);
                    Eigen::Matrix<double, 6, 6> A = Eigen::Matrix<double, 6, 6>::Zero(), C = A;
                    Quadric                     b = Quadric::Zero();
                    int                         n = 0;
                    for (int dy = -d.r; dy <= d.r; dy += d.stride)
                        for (int dx = -d.r; dx <= d.r; dx += d.stride) {
                            const int x = ix + dx, y = iy + dy;
                            if (x < 0 || y < 0 || x >= nx || y >= ny || std::isnan(surface.at(x, y)))
                                continue;
                            const Quadric u = quadric_basis(dx, dy);
                            const double  e = (surface.at(x, y) - first.dot(u)) / cut;
                            const double  w = std::abs(e) < 1. ? d.weight(dx, dy) * (1. - e * e) * (1. - e * e) : 0.;
                            if (!(w > 0.))
                                continue;
                            A.noalias() += w * u * u.transpose();
                            C.noalias() += w * w * u * u.transpose();
                            b.noalias() += w * surface.at(x, y) * u;
                            ++n;
                        }
                    if (n < 12)
                        break;
                    const auto ldlt = A.ldlt();
                    if (ldlt.info() != Eigen::Success || !ldlt.isPositive())
                        break;
                    const Quadric g      = ldlt.solve(Quadric::Unit(0));
                    const double  height = g.dot(b), sd = sigma * std::sqrt(std::max(0., g.dot(C * g)));
                    if (!std::isfinite(height) || std::abs(height - z) > cut)
                        break;
                    lo = std::max(lo, height - Kappa * sd);
                    hi = std::min(hi, height + Kappa * sd);
                    if (lo > hi)
                        break;
                    best = height;
                }
                out.at(ix, iy) = float(best);
            }
    });
    surface = std::move(out);
    return sigma;
}

void pad_lowest(HeightField &field, int iterations)
{
    constexpr float Inf = std::numeric_limits<float>::infinity();
    for (int it = 0; it < iterations; ++it) {
        HeightField next = field;
        for (int iy = 0; iy < field.ny(); ++iy)
            for (int ix = 0; ix < field.nx(); ++ix) {
                if (!std::isnan(field.at(ix, iy)))
                    continue;
                float lowest = Inf;
                for (auto [dx, dy] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
                    const int x = ix + dx, y = iy + dy;
                    if (x >= 0 && y >= 0 && x < field.nx() && y < field.ny() && !std::isnan(field.at(x, y)))
                        lowest = std::min(lowest, field.at(x, y));
                }
                if (lowest < Inf)
                    next.at(ix, iy) = lowest;
            }
        field = std::move(next);
    }
}

std::vector<int> label_components(const Mask &mask, int nx, int ny, int &count)
{
    std::vector<int>    label(mask.size(), -1);
    std::vector<size_t> stack;
    count = 0;
    for (size_t seed = 0; seed < mask.size(); ++seed) {
        if (!mask[seed] || label[seed] >= 0)
            continue;
        label[seed] = count;
        stack.assign(1, seed);
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            const int ix = int(i % size_t(nx)), iy = int(i / size_t(nx));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = ix + dx, y = iy + dy;
                    if (x < 0 || y < 0 || x >= nx || y >= ny)
                        continue;
                    const size_t j = size_t(y) * nx + x;
                    if (mask[j] && label[j] < 0) {
                        label[j] = count;
                        stack.emplace_back(j);
                    }
                }
        }
        ++count;
    }
    return label;
}

void drop_steep_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, const HeightField &grid, Mask &mask)
{
    const int                 n_az   = 72;
    const std::vector<double> limits = head.slope_map(n_az, gap);
    Mask                      steep(mask.size(), 0);
    for (int iy = 0; iy < grid.ny(); ++iy)
        for (int ix = 0; ix < grid.nx(); ++ix) {
            const size_t i = size_t(iy) * grid.nx() + ix;
            if (!mask[i])
                continue;
            const Vec2d g = surface.gradient(grid.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y())) {
                steep[i] = 1;
                continue;
            }
            if (g.norm() < 1e-9)
                continue;
            const double phi = std::atan2(g.y(), g.x());
            const int    k   = (int(std::lround(phi / (2. * PI) * n_az)) % n_az + n_az) % n_az;
            steep[i]         = std::atan(g.norm()) > limits[size_t(k)];
        }
    for (size_t i = 0; i < mask.size(); ++i)
        if (steep[i])
            mask[i] = 0;
}

void drop_unsafe_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, double max_error,
                       Mask &mask, std::vector<int> *limiting_components)
{
    Mask plane_safe = mask;
    drop_steep_nodes(surface, head, gap, surface, plane_safe);
    const HeightField current_safe = min_safe_tip_height(surface, head, surface, limiting_components, true);
    HeightField previous = surface;
    for (float &z : previous.data())
        z -= float(gap);
    std::vector<int> substrate_components;
    const HeightField substrate_safe = min_safe_tip_height(previous, head, surface,
                                                           limiting_components ? &substrate_components : nullptr);
    for (size_t i = 0; i < mask.size(); ++i) {
        if (limiting_components && substrate_safe.data()[i] > current_safe.data()[i])
            (*limiting_components)[i] = substrate_components[i];
        if (mask[i] && !plane_safe[i] &&
            surface.data()[i] < std::max(current_safe.data()[i], substrate_safe.data()[i]) - max_error)
            mask[i] = 0;
    }
}

std::string most_limiting_component(const HeadEnvelope &head, const std::vector<int> &limiting_components,
                                    const Mask &area)
{
    std::vector<size_t> counts(head.boxes.size(), 0);
    for (size_t i = 0; i < area.size(); ++i)
        if (area[i] && limiting_components[i] >= 0)
            ++counts[size_t(limiting_components[i])];
    const auto most = std::max_element(counts.begin(), counts.end());
    return most != counts.end() && *most > 0 ? head.boxes[size_t(most - counts.begin())].name : std::string();
}

namespace {

const coord_t     AreaCell  = coord_t(scale_(1.));
constexpr coord_t AreaClear = 10;

template<typename Fn> bool visit_cells(const Point &a, const Point &b, coord_t pad, Fn &&fn)
{
    auto cell = [](coord_t v) { return int64_t(std::floor(double(v) / AreaCell)); };
    const int64_t x0 = cell(std::min(a.x(), b.x()) - pad), x1 = cell(std::max(a.x(), b.x()) + pad);
    const int64_t y0 = cell(std::min(a.y(), b.y()) - pad), y1 = cell(std::max(a.y(), b.y()) + pad);
    for (int64_t ix = x0; ix <= x1; ++ix)
        for (int64_t iy = y0; iy <= y1; ++iy)
            if (fn((uint64_t(uint32_t(ix)) << 32) | uint32_t(iy)))
                return true;
    return false;
}

double point_segment_distance2(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    const Vec2d  d = b - a;
    const double t = d.squaredNorm() > 0. ? std::clamp((p - a).dot(d) / d.squaredNorm(), 0., 1.) : 0.;
    return (a + t * d - p).squaredNorm();
}

double segment_distance2(const Point &a, const Point &b, const Point &c, const Point &d)
{
    auto orient = [](const Point &p, const Point &q, const Point &r) {
        const double v = double(q.x() - p.x()) * double(r.y() - p.y()) - double(q.y() - p.y()) * double(r.x() - p.x());
        return (v > 0.) - (v < 0.);
    };
    if (orient(a, b, c) * orient(a, b, d) <= 0 && orient(c, d, a) * orient(c, d, b) <= 0)
        return 0.;
    const Vec2d pa = a.cast<double>(), pb = b.cast<double>(), pc = c.cast<double>(), pd = d.cast<double>();
    return std::min({ point_segment_distance2(pa, pc, pd), point_segment_distance2(pb, pc, pd), point_segment_distance2(pc, pa, pb),
                      point_segment_distance2(pd, pa, pb) });
}

}

AreaEdges::AreaEdges(const ExPolygons &area) : m_area(area)
{
    auto add = [this](const Polygon &poly) {
        for (size_t i = 0; i < poly.points.size(); ++i) {
            const Point &a = poly.points[i], &b = poly.points[(i + 1) % poly.points.size()];
            visit_cells(a, b, 0, [&](uint64_t key) { m_cells[key].emplace_back(a, b); return false; });
        }
    };
    for (const ExPolygon &expoly : area) {
        add(expoly.contour);
        for (const Polygon &hole : expoly.holes)
            add(hole);
    }
}

bool AreaEdges::near_edge(const Point &a, const Point &b) const
{
    return visit_cells(a, b, AreaClear, [&](uint64_t key) {
        const auto it = m_cells.find(key);
        if (it != m_cells.end())
            for (const auto &[c, d] : it->second)
                if (segment_distance2(a, b, c, d) <= double(AreaClear) * double(AreaClear))
                    return true;
        return false;
    });
}

int AreaEdges::side(const Point &p) const
{
    for (const ExPolygon &expoly : m_area)
        if (expoly.contains(p))
            return 1;
    return 0;
}

int AreaEdges::classify(const Point &a, const Point &b) const { return near_edge(a, b) ? -1 : side(a); }

int AreaEdges::classify(const Points &points) const
{
    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (near_edge(points[i], points[i + 1]))
            return -1;
    return points.size() == 1 && near_edge(points.front(), points.front()) ? -1 : side(points.front());
}

namespace {

void clip_extrusions(ExtrusionEntityCollection &collection, const AreaEdges &edges, const BoundingBox &area_bbox)
{
    const ExPolygons &area = edges.area();
    ExtrusionEntitiesPtr out;
    out.reserve(collection.entities.size());
    bool any_cut = false;
    std::vector<std::pair<size_t, size_t>> cut_from;
    for (ExtrusionEntity *ee : collection.entities) {
        if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            clip_extrusions(*sub, edges, area_bbox);
            if (sub->entities.empty())
                delete ee;
            else
                out.emplace_back(ee);
            continue;
        }
        const ExtrusionPath *single = nullptr;
        const ExtrusionPaths *paths = nullptr;
        if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee))
            paths = &loop->paths;
        else if (auto *multi = dynamic_cast<ExtrusionMultiPath *>(ee))
            paths = &multi->paths;
        else if (auto *path = dynamic_cast<ExtrusionPath *>(ee); path != nullptr && typeid(*path) == typeid(ExtrusionPath))
            single = path;
        auto bounds = [](const ExtrusionPath &path) {
            BoundingBox bb;
            for (const Point3 &p : path.polyline.points)
                bb.merge(p.to_point());
            return bb;
        };
        bool overlaps = false;
        if (single != nullptr)
            overlaps = bounds(*single).overlap(area_bbox);
        else if (paths != nullptr)
            for (const ExtrusionPath &path : *paths)
                overlaps |= bounds(path).overlap(area_bbox);
        if (!overlaps) {
            out.emplace_back(ee);
            continue;
        }
        bool                      cut = false;
        ExtrusionEntityCollection pieces;
        auto clip_path = [&](const ExtrusionPath &path) {
            const Polylines src { path.polyline.to_polyline() };
            const int       side = bounds(path).overlap(area_bbox) ? edges.classify(src.front().points) : 0;
            if (side == 1 && src.front().length() > SCALED_EPSILON) {
                cut = true;
                return;
            }
            if (side == 0 || intersection_pl(src, area).empty()) {
                pieces.entities.emplace_back(new ExtrusionPath(path));
                return;
            }
            cut = true;
            for (Polyline &pl : diff_pl(src, area))
                if (pl.is_valid() && pl.length() >= scale_(2. * path.width))
                    pieces.entities.emplace_back(new ExtrusionPath(Polyline3(pl), path));
        };
        if (single != nullptr)
            clip_path(*single);
        else
            for (const ExtrusionPath &path : *paths)
                clip_path(path);
        if (!cut) {
            out.emplace_back(ee);
            continue;
        }
        any_cut = true;
        delete ee;
        cut_from.emplace_back(out.size(), out.size() + pieces.entities.size());
        out.insert(out.end(), pieces.entities.begin(), pieces.entities.end());
        pieces.entities.clear();
    }
    for (size_t k = 0; k < cut_from.size();) {
        size_t begin = cut_from[k].first, end = cut_from[k].second;
        const ExtrusionRole role = begin < end ? out[begin]->role() : erNone;
        for (++k; k < cut_from.size() && cut_from[k].first == end && (cut_from[k].first == cut_from[k].second || out[cut_from[k].first]->role() == role); ++k)
            end = cut_from[k].second;
        if (end - begin < 2)
            continue;
        std::vector<ExtrusionEntity *> run(out.begin() + begin, out.begin() + end);
        const Point                    from = begin > 0 ? out[begin - 1]->last_point() : run.front()->first_point();
        reorder_extrusion_entities(run, chain_extrusion_entities(run, &from));
        std::copy(run.begin(), run.end(), out.begin() + begin);
    }
    collection.entities = std::move(out);
    if (any_cut)
        collection.no_sort = false;
}

}

void clip_extrusions(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &area_bbox)
{
    clip_extrusions(collection, AreaEdges(area), area_bbox);
}

void erase_conformal(ExtrusionEntityCollection &collection, double host_z, double step, const std::function<bool(const Vec3d &)> &inside)
{
    ExtrusionEntitiesPtr out;
    out.reserve(collection.entities.size());
    for (ExtrusionEntity *ee : collection.entities) {
        auto *path = dynamic_cast<ExtrusionPathConformal *>(ee);
        if (path == nullptr) {
            out.emplace_back(ee);
            continue;
        }
        Points3            pts;
        std::vector<float> beads;
        std::vector<float> widths;
        std::vector<Vec3f> axes;
        const Points3     &src = path->polyline.points;
        const bool         variable_width = path->bead_width.size() == src.size();
        for (size_t i = 0; i < src.size(); ++i) {
            const int n = i + 1 < src.size() ? std::max(1, int(std::ceil(unscale<double>((src[i + 1] - src[i]).cast<double>().norm()) / step))) : 1;
            for (int j = 0; j < n; ++j) {
                const double f = double(j) / n;
                const size_t k = std::min(i + 1, src.size() - 1);
                pts.emplace_back((src[i].cast<double>() + f * (src[k] - src[i]).cast<double>()).cast<coord_t>());
                beads.emplace_back(float((1. - f) * path->bead_height[i] + f * path->bead_height[k]));
                if (variable_width)
                    widths.emplace_back(float((1. - f) * path->bead_width[i] + f * path->bead_width[k]));
                if (!path->tool_axis.empty())
                    axes.emplace_back(((1.f - float(f)) * path->tool_axis[i] + float(f) * path->tool_axis[k]).normalized());
            }
        }
        std::vector<char> drop(pts.size(), 0);
        bool              any = false;
        for (size_t i = 0; i < pts.size(); ++i)
            any |= drop[i] = inside(Vec3d(unscale<double>(pts[i].x()), unscale<double>(pts[i].y()), host_z + unscale<double>(pts[i].z())));
        if (!any) {
            out.emplace_back(ee);
            continue;
        }
        for (size_t i = 1; i < pts.size();) {
            if (!drop[i] || drop[i - 1]) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < pts.size() && drop[j])
                ++j;
            if (j < pts.size() && unscale<double>((pts[j] - pts[i - 1]).cast<double>().norm()) <= 2. * path->width)
                std::fill(drop.begin() + i, drop.begin() + j, 0);
            i = j;
        }
        for (size_t i = 0; i < pts.size();) {
            if (drop[i]) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < pts.size() && !drop[j])
                ++j;
            const size_t first = i > 0 ? i - 1 : i, last = std::min(j + 1, pts.size());
            if (last - first >= 2) {
                std::vector<float> bead(beads.begin() + first, beads.begin() + last);
                std::vector<Vec3f> axis;
                if (!axes.empty())
                    axis.assign(axes.begin() + first, axes.begin() + last);
                auto *piece     = new ExtrusionPathConformal(Polyline3(Points3(pts.begin() + first, pts.begin() + last)), *path, std::move(bead),
                                                             std::move(axis));
                if (variable_width)
                    piece->bead_width.assign(widths.begin() + first, widths.begin() + last);
                piece->patch_id = path->patch_id;
                piece->shell    = path->shell;
                piece->simplify(0.01);
                out.emplace_back(piece);
            }
            i = j;
        }
        delete ee;
    }
    collection.entities = std::move(out);
}

int planar_transition_layer(const std::vector<double> &boundaries, double shell_face, double shell_height,
                            double h_min, double h_max, PatchKind kind)
{
    if (boundaries.empty())
        return -1;
    const bool top = kind == PatchKind::Top;
    const double target = shell_face + (top ? -1. : 1.) * (shell_height + h_min);
    int layer = top ? int(std::upper_bound(boundaries.begin(), boundaries.end(), target + EPSILON) - boundaries.begin()) - 1 :
                      int(std::lower_bound(boundaries.begin(), boundaries.end(), target - EPSILON) - boundaries.begin());
    layer = std::clamp(layer, 0, int(boundaries.size()) - 1);
    auto height = [&]() { return top ? shell_face - boundaries[size_t(layer)] : boundaries[size_t(layer)] - shell_face; };
    while (height() > h_max) {
        layer += top ? 1 : -1;
        if (layer < 0 || layer >= int(boundaries.size()))
            return -1;
    }
    return height() >= h_min ? layer : -1;
}

bool contour_line_angle(const HeightField &surface, double &angle, double min_coherence)
{
    double jxx = 0., jxy = 0., jyy = 0.;
    for (int iy = 0; iy < surface.ny(); ++iy)
        for (int ix = 0; ix < surface.nx(); ++ix) {
            if (std::isnan(surface.at(ix, iy)))
                continue;
            const Vec2d g = surface.gradient(surface.node(ix, iy));
            if (std::isnan(g.x()) || std::isnan(g.y()))
                continue;
            jxx += g.x() * g.x();
            jxy += g.x() * g.y();
            jyy += g.y() * g.y();
        }
    const double trace = jxx + jyy;
    const double diff  = std::hypot(jxx - jyy, 2. * jxy);
    if (trace < 1e-9 || diff < min_coherence * trace)
        return false;
    angle = 0.5 * std::atan2(2. * jxy, jxx - jyy) + 0.5 * PI;
    return true;
}

double top_shell_angle(const ConformalPatch &patch, const PrintRegionConfig &config)
{
    double angle = Geometry::deg2rad(config.solid_infill_direction.value);
    if (top_pattern(patch, config) == NonplanarTopPattern::Contour && !contour_line_angle(patch.surface, angle))
        angle = Geometry::deg2rad(config.solid_infill_direction.value);
    return angle;
}

void split_by_owner(const Polylines &polylines, const std::function<int(const Vec2d &)> &owner_at, double step,
                    const std::function<void(int, Polyline &&)> &sink)
{
    for (const Polyline &pl : polylines) {
        if (pl.points.size() < 2)
            continue;
        Polyline piece;
        int      current = owner_at(unscale(pl.points.front()));
        piece.points.emplace_back(pl.points.front());
        auto flush = [&]() {
            if (current != -1 && piece.points.size() >= 2)
                sink(current, std::move(piece));
            piece = Polyline();
        };
        for (size_t i = 0; i + 1 < pl.points.size(); ++i) {
            const Vec2d a = unscale(pl.points[i]), b = unscale(pl.points[i + 1]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / step)));
            Vec2d       prev = a;
            for (int j = 1; j <= n; ++j) {
                const Vec2d p = a + (b - a) * (double(j) / n);
                const int   o = owner_at(p);
                if (o != current) {
                    double t0 = 0., t1 = 1.;
                    for (int it = 0; it < 24; ++it) {
                        const double t = 0.5 * (t0 + t1);
                        (owner_at(prev + (p - prev) * t) == current ? t0 : t1) = t;
                    }
                    const Point cut = Point::new_scale(prev + (p - prev) * (0.5 * (t0 + t1)));
                    piece.points.emplace_back(cut);
                    flush();
                    current = o;
                    piece.points.emplace_back(cut);
                }
                prev = p;
            }
            piece.points.emplace_back(pl.points[i + 1]);
        }
        flush();
    }
}

Polylines fill_solid(const ExPolygons &area, InfillPattern pattern, double spacing, double angle, bool monotonic,
                     const Flow &flow, double z)
{
    std::unique_ptr<Fill> fill(Fill::new_from_type(pattern));
    fill->layer_id        = 0;
    fill->z               = z;
    fill->spacing         = spacing;
    fill->overlap         = 0.;
    fill->angle           = float(angle);
    fill->fixed_angle     = true;
    fill->link_max_length = coord_t(scale_(3. * spacing));
    fill->bounding_box    = get_extents(area);
    FillParams params;
    params.density     = 1.f;
    params.dont_adjust = false;
    params.monotonic   = monotonic;
    params.flow        = flow;

    Polylines out;
    Surface   surface(stTop, ExPolygon());
    for (const ExPolygon &expoly : area) {
        surface.expolygon = expoly;
        try {
            append(out, fill->fill_surface(&surface, params));
        } catch (InfillFailedException &) {
            if (pattern == ipRectilinear)
                continue;
            BOOST_LOG_TRIVIAL(warning) << "Non-planar: shell fill failed on a " << expoly.area() * SCALING_FACTOR * SCALING_FACTOR
                                       << " mm2 piece, filled with lines";
            std::unique_ptr<Fill> lines(Fill::new_from_type(ipRectilinear));
            lines->layer_id    = 0;
            lines->z           = z;
            lines->spacing     = spacing;
            lines->overlap     = 0.;
            lines->angle       = float(angle);
            lines->fixed_angle = true;
            lines->bounding_box = fill->bounding_box;
            try {
                append(out, lines->fill_surface(&surface, params));
            } catch (InfillFailedException &) {
            }
        }
    }
    return out;
}

void lift_polylines(const Polylines &polylines, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                    int patch_id, int shell, ExtrusionEntitiesPtr &out, const std::vector<std::vector<float>> *widths)
{
    for (size_t ipl = 0; ipl < polylines.size(); ++ipl) {
        const Polyline           &pl    = polylines[ipl];
        const std::vector<float> *width = widths != nullptr ? &(*widths)[ipl] : nullptr;
        Points3            pts;
        std::vector<float> heights, bead_widths;
        auto flush = [&]() {
            if (pts.size() >= 2) {
                auto *path = new ExtrusionPathConformal(Polyline3(pts), proto, std::move(heights));
                path->bead_width = std::move(bead_widths);
                path->patch_id   = patch_id;
                path->shell      = shell;
                path->simplify(0.01);
                out.emplace_back(path);
            }
            pts.clear();
            heights.clear();
            bead_widths.clear();
        };
        for (size_t i = 0; i + 1 < pl.points.size(); ++i) {
            const Vec2d a = unscale(pl.points[i]), b = unscale(pl.points[i + 1]);
            const int   n = std::max(1, int(std::ceil((b - a).norm() / resample)));
            for (int j = i == 0 ? 0 : 1; j <= n; ++j) {
                const Vec2d p = a + (b - a) * (double(j) / n);
                double      z    = 0.;
                float       bead = 0.f;
                if (!at(p, z, bead)) {
                    flush();
                    continue;
                }
                if (width != nullptr)
                    bead_widths.emplace_back((*width)[i] + float(double(j) / n) * ((*width)[i + 1] - (*width)[i]));
                pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(z - host_z)));
                heights.emplace_back(bead);
            }
        }
        flush();
    }
}

double head_band_height(const HeadEnvelope &head, double tilt)
{
    const double c = std::cos(tilt), s = std::sin(tilt);
    const double allowance_r = head.tip_allowance.radius, allowance_h = head.tip_allowance.height;
    double       lowest = std::numeric_limits<double>::infinity();
    for (const HeadBox &box : head.boxes) {
        const double z0 = box.min.z(), z1 = box.max.z();
        const double half_x = std::max(std::abs(box.min.x()), std::abs(box.max.x()));
        const double half_y = std::max(std::abs(box.min.y()), std::abs(box.max.y()));
        double       floor_z;
        if (tilt <= 0.) {
            floor_z = z0;
            if (box.nozzle) {
                if (std::hypot(half_x, half_y) <= allowance_r)
                    floor_z = std::max(floor_z, allowance_h);
                if (floor_z >= z1)
                    continue;
            } else
                floor_z -= head.margin;
        } else if (box.nozzle) {
            const double r = std::max(half_x, half_y);
            if (r * c + z1 * s <= allowance_r)
                floor_z = std::max(z0 * c - r * s, allowance_h);
            else {
                const double z_out = s > 0. ? std::max(z0, (allowance_r - r * c) / s) : z0;
                floor_z            = z_out * c - r * s;
                if (r * c + z0 * s <= allowance_r)
                    floor_z = std::min(floor_z, std::max(z0 * c - r * s, allowance_h));
            }
            if (floor_z >= z1 * c + r * s)
                continue;
        } else
            floor_z = z0 * c - std::hypot(half_x + head.lateral_margin, half_y + head.lateral_margin) * s - head.margin;
        lowest = std::min(lowest, floor_z);
    }
    return lowest;
}

double head_band_tilt_limit(const HeadEnvelope &head)
{
    const double upright = head_band_height(head);
    if (! std::isfinite(upright) || upright <= 0.)
        return 0.;
    constexpr double step = 0.25 * M_PI / 180.;
    double           tilt = 0.;
    while (tilt + step < 0.5 * M_PI && head_band_height(head, tilt + step) >= upright - EPSILON)
        tilt += step;
    return tilt;
}

void cut_into_bands(const ExtrusionEntitiesPtr &paths, double pitch, double tolerance,
                    const std::function<void(int band, ExtrusionPathConformal *piece)> &sink,
                    const std::function<int(const Point3 &)> &cut_rank)
{
    auto band_of = [pitch](double z) { return int(std::floor(z / pitch + 1e-6)); };
    auto fits    = [&](double lo, double hi) { return hi <= (band_of(lo) + 1) * pitch + tolerance; };
    for (const ExtrusionEntity *ee : paths) {
        const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
        if (path == nullptr || path->polyline.points.size() < 2)
            continue;
        const Points3 &src = path->polyline.points;
        auto point = [](const Point3 &p) { return Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), unscale<double>(p.z())); };

        const bool         widths = !path->bead_width.empty();
        Points3            pts;
        std::vector<float> heights, bead_widths;
        double             lo = 0., hi = 0.;
        auto start = [&](const Vec3d &p, float h, float w) {
            pts.assign(1, Point3(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(p.z()))));
            heights.assign(1, h);
            bead_widths.assign(widths ? 1 : 0, w);
            lo = hi = p.z();
        };
        auto add = [&](const Vec3d &p, float h, float w) {
            pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())), coord_t(scale_(p.z())));
            heights.emplace_back(h);
            if (widths)
                bead_widths.emplace_back(w);
            lo = std::min(lo, p.z());
            hi = std::max(hi, p.z());
        };
        auto flush = [&]() {
            size_t begin = 0, end = pts.size();
            while (end > 1 && heights[end - 1] == 0.f && heights[end - 2] == 0.f)
                --end;
            while (begin + 1 < end && heights[begin] == 0.f && heights[begin + 1] == 0.f)
                ++begin;
            if (end - begin >= 2 && (begin > 0 || end < pts.size())) {
                pts     = Points3(pts.begin() + begin, pts.begin() + end);
                heights = std::vector<float>(heights.begin() + begin, heights.begin() + end);
                if (widths)
                    bead_widths = std::vector<float>(bead_widths.begin() + begin, bead_widths.begin() + end);
            }
            if (pts.size() >= 2 && std::any_of(heights.begin(), heights.end(), [](float h) { return h > 0.f; })) {
                auto *piece       = new ExtrusionPathConformal(Polyline3(pts), *path, std::vector<float>(heights));
                piece->bead_width = bead_widths;
                piece->patch_id   = path->patch_id;
                piece->shell      = path->shell;
                sink(band_of(lo), piece);
            }
        };

        auto width_at = [&](size_t i) { return widths ? path->bead_width[i] : 0.f; };
        start(point(src.front()), path->bead_height.front(), width_at(0));
        for (size_t i = 1; i < src.size(); ++i) {
            Vec3d       a  = point(pts.back());
            float       ha = heights.back();
            float       wa = widths ? bead_widths.back() : 0.f;
            const Vec3d b  = point(src[i]);
            const float hb = path->bead_height[i];
            const float wb = width_at(i);
            bool        snapped = false;
            for (int guard = 0; guard < 10000; ++guard) {
                if (fits(std::min(lo, b.z()), std::max(hi, b.z()))) {
                    add(b, hb, wb);
                    break;
                }
                if (cut_rank && !snapped) {
                    snapped          = true;
                    const int band   = band_of(lo);
                    double    tlo    = std::numeric_limits<double>::infinity(), thi = -tlo;
                    size_t    cut    = 0;
                    int       rank   = 0;
                    for (size_t j = pts.size() - 1; j > 0; --j) {
                        const double z = unscale<double>(pts[j].z());
                        tlo = std::min(tlo, z);
                        thi = std::max(thi, z);
                        if (band_of(tlo) == band)
                            break;
                        if (const int r = cut_rank(pts[j]); r > rank && fits(tlo, thi)) {
                            rank = r;
                            cut  = j;
                        }
                    }
                    if (cut > 0) {
                        Points3            tail(pts.begin() + cut, pts.end());
                        std::vector<float> tail_h(heights.begin() + cut, heights.end());
                        std::vector<float> tail_w(widths ? bead_widths.begin() + cut : bead_widths.end(), bead_widths.end());
                        pts.resize(cut + 1);
                        heights.resize(cut + 1);
                        if (widths)
                            bead_widths.resize(cut + 1);
                        flush();
                        pts         = std::move(tail);
                        heights     = std::move(tail_h);
                        bead_widths = std::move(tail_w);
                        lo = std::numeric_limits<double>::infinity();
                        hi = -lo;
                        for (const Point3 &q : pts) {
                            lo = std::min(lo, unscale<double>(q.z()));
                            hi = std::max(hi, unscale<double>(q.z()));
                        }
                        a  = point(pts.back());
                        ha = heights.back();
                        wa = widths ? bead_widths.back() : 0.f;
                        continue;
                    }
                }
                double t0 = 0., t1 = 1.;
                for (int it = 0; it < 40; ++it) {
                    const double t = 0.5 * (t0 + t1);
                    const double z = a.z() + t * (b.z() - a.z());
                    (fits(std::min(lo, z), std::max(hi, z)) ? t0 : t1) = t;
                }
                const Vec3d cut = a + t0 * (b - a);
                const float hc  = ha + float(t0) * (hb - ha);
                const float wc  = wa + float(t0) * (wb - wa);
                if (t0 > 0.)
                    add(cut, hc, wc);
                flush();
                start(cut, hc, wc);
                a  = cut;
                ha = hc;
                wa = wc;
            }
        }
        flush();
    }
}

void cut_by_colour(const ExtrusionEntitiesPtr &paths, const std::function<int(const Vec3d &)> &colour, double pullback,
                   double min_length, const std::function<void(int colour, ExtrusionPathConformal *piece)> &sink)
{
    struct Sample { Vec3d p; float h; float w; };
    auto point = [](const Point3 &p) { return Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), unscale<double>(p.z())); };
    auto trim = [](std::vector<Sample> &run, double length, bool front) {
        if (front)
            std::reverse(run.begin(), run.end());
        while (length > 0. && run.size() >= 2) {
            Sample      &last = run.back();
            const Sample &prev = run[run.size() - 2];
            const double  l    = (last.p - prev.p).head<2>().norm();
            if (l > length) {
                const double t = length / l;
                last.p = last.p + t * (prev.p - last.p);
                last.h = last.h + float(t) * (prev.h - last.h);
                last.w = last.w + float(t) * (prev.w - last.w);
                break;
            }
            length -= l;
            run.pop_back();
        }
        if (front)
            std::reverse(run.begin(), run.end());
    };
    for (const ExtrusionEntity *ee : paths) {
        const auto *path = dynamic_cast<const ExtrusionPathConformal *>(ee);
        if (path == nullptr || path->polyline.points.size() < 2)
            continue;
        const Points3       &src = path->polyline.points;
        const bool           widths = path->bead_width.size() == src.size();
        auto width_at = [&](size_t i) { return widths ? path->bead_width[i] : 0.f; };
        std::vector<Sample>  run{ { point(src.front()), path->bead_height.front(), width_at(0) } };
        int                  run_colour = colour(run.front().p);
        bool                 cut_front  = false;
        auto flush = [&](bool cut_back) {
            if (cut_back)
                trim(run, pullback, false);
            if (cut_front)
                trim(run, pullback, true);
            double length = 0.;
            for (size_t i = 1; i < run.size(); ++i)
                length += (run[i].p - run[i - 1].p).head<2>().norm();
            if (run.size() >= 2 && length >= min_length) {
                Points3            pts;
                std::vector<float> heights, bead_widths;
                for (const Sample &s : run) {
                    pts.emplace_back(coord_t(scale_(s.p.x())), coord_t(scale_(s.p.y())), coord_t(scale_(s.p.z())));
                    heights.emplace_back(s.h);
                    if (widths)
                        bead_widths.emplace_back(s.w);
                }
                auto *piece       = new ExtrusionPathConformal(Polyline3(std::move(pts)), *path, std::move(heights));
                piece->bead_width = std::move(bead_widths);
                piece->patch_id = path->patch_id;
                piece->shell    = path->shell;
                sink(run_colour, piece);
            }
        };
        for (size_t i = 1; i < src.size(); ++i) {
            const Sample b{ point(src[i]), path->bead_height[i], width_at(i) };
            const int    cb = colour(b.p);
            if (cb != run_colour) {
                const Sample a = run.back();
                double       t0 = 0., t1 = 1.;
                for (int it = 0; it < 24; ++it) {
                    const double t = 0.5 * (t0 + t1);
                    (colour(a.p + t * (b.p - a.p)) == run_colour ? t0 : t1) = t;
                }
                const Sample cut{ a.p + t0 * (b.p - a.p), a.h + float(t0) * (b.h - a.h), a.w + float(t0) * (b.w - a.w) };
                run.push_back(cut);
                flush(true);
                run.assign(1, cut);
                run_colour = cb;
                cut_front  = true;
            }
            run.push_back(b);
        }
        flush(false);
    }
}

namespace {
constexpr double MinBandReach = 0.05;
constexpr float  Inf          = std::numeric_limits<float>::infinity();

std::vector<ExtrusionEntityCollection *> passes(ConformalPatch &patch)
{
    std::vector<ExtrusionEntityCollection *> out;
    for (ExtrusionEntityCollection &shell : patch.shell_paths)
        out.emplace_back(&shell);
    out.emplace_back(&patch.ironing);
    return out;
}
}

namespace {
constexpr double WallSlope = 60.;

}

bool spans_slope(const HeightField &surface, const Vec2d &p, double slope_deg)
{
    if (surface.empty())
        throw RuntimeError("Cannot classify a slope on an empty non-planar surface grid.");
    const Vec2d o = (p - surface.origin()) / surface.pitch();
    if (!o.allFinite() || !std::isfinite(slope_deg))
        throw RuntimeError("Non-planar slope classification requires finite coordinates and slope limit.");
    const int ix = int(std::clamp(std::floor(o.x()), 0., double(std::max(0, surface.nx() - 2))));
    const int iy = int(std::clamp(std::floor(o.y()), 0., double(std::max(0, surface.ny() - 2))));
    const int ix1 = std::min(ix + 1, surface.nx() - 1), iy1 = std::min(iy + 1, surface.ny() - 1);
    const float z[4] = { surface.at(ix, iy), surface.at(ix1, iy), surface.at(ix, iy1), surface.at(ix1, iy1) };
    const double span = std::sqrt(double((ix1 - ix) + (iy1 - iy))) * surface.pitch();
    thread_local double last_deg = std::numeric_limits<double>::quiet_NaN(), last_tan = 0.;
    if (slope_deg != last_deg) {
        last_deg = slope_deg;
        last_tan = std::tan(Geometry::deg2rad(slope_deg));
    }
    return *std::max_element(z, z + 4) - *std::min_element(z, z + 4) > last_tan * span;
}

bool spans_wall(const HeightField &surface, const Vec2d &p) { return spans_slope(surface, p, WallSlope); }

double max_shell_slope(double h, double w) { return h / w; }

Mask steep_nodes(const HeightField &surface, double max_tan, double min_width)
{
    const int nx = surface.nx(), ny = surface.ny();
    Mask      out(surface.data().size(), 0);
    auto      slope = [&](int ix, int iy, int dx, int dy) {
        const float a = surface.at(std::max(0, ix - dx), std::max(0, iy - dy));
        const float b = surface.at(std::min(nx - 1, ix + dx), std::min(ny - 1, iy + dy));
        const float z = surface.at(ix, iy);
        const int   n = (ix - dx >= 0 && iy - dy >= 0) + (ix + dx < nx && iy + dy < ny);
        if (!std::isnan(a) && !std::isnan(b) && n == 2)
            return double(b - a) / (2. * surface.pitch());
        if (!std::isnan(b) && ix + dx < nx && iy + dy < ny)
            return double(b - z) / surface.pitch();
        if (!std::isnan(a) && ix - dx >= 0 && iy - dy >= 0)
            return double(z - a) / surface.pitch();
        return 0.;
    };
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix)
            if (!std::isnan(surface.at(ix, iy)))
                out[size_t(iy) * nx + ix] = std::hypot(slope(ix, iy, 1, 0), slope(ix, iy, 0, 1)) > max_tan;
    return rasterize(opening_ex(mask_to_expolygons(out, surface), float(scale_(0.5 * min_width))), surface);
}

double effective_patch_height_cap(const PrintObject &object)
{
    const double cap = object.config().nonplanar_max_patch_height.value;
    if (cap <= 0.)
        return cap;
    double minimum = std::max(object.config().layer_height.value, automatic_shell_height(object));
    for (size_t i = 0; i < object.num_printing_regions(); ++i) {
        const auto &region = object.printing_region(i).config();
        if (top_shells_enabled(region, object.config()) || bottom_shells_enabled(region, object.config()))
            minimum = std::max(minimum, region.nonplanar_shell_layer_height.value);
    }
    return std::max(cap, minimum);
}

void band_schedule(const HeadEnvelope &head, double shell_height, double max_patch_height, double &pitch, double &tolerance)
{
    const double reach = head_band_height(head);
    const double cap        = max_patch_height > 0. ? std::min(reach, max_patch_height) : reach;
    if (!std::isfinite(cap) || cap < MinBandReach)
        throw RuntimeError("Head clearance cannot support non-planar bands. Check the head model and margin, or set a finite maximum patch height.");
    const double band_reach = cap;
    tolerance = std::min(0.5 * band_reach, 0.45 * shell_height);
    pitch     = band_reach - tolerance;
}

double rotary_band_reach(const PrintObject &object, const HeadEnvelope &head, double layer_height)
{
    const PrintConfig &config = object.print()->config();
    if (config.nonplanar_kinematics.value == RotaryLayout::None)
        return 0.;
    double width = 0.;
    for (size_t i = 0; i < object.num_printing_regions(); ++i)
        for (const FlowRole role : { frExternalPerimeter, frPerimeter, frInfill, frSolidInfill, frTopSolidInfill })
            width = std::max(width, double(object.printing_region(i).flow(object, role, layer_height).width()));
    const double tilt = std::clamp(Geometry::deg2rad(config.nonplanar_max_tilt.value), 0., 0.5 * M_PI);
    const double reach = head_band_height(head) - 0.5 * width * std::sin(tilt);
    if (!(reach >= MinBandReach))
        throw RuntimeError("Head clearance cannot support non-planar bands with tilted roads. Lower the maximum tilt, "
                           "or check the head model and margin.");
    return reach;
}

namespace {
constexpr double ColourPitch = 0.05;
}

TopColours top_colours(const PrintObject &object, const ExPolygons &candidate)
{
    TopColours            out;
    std::map<int, size_t> index;
    for (size_t i = object.layer_count(); i-- > 0;)
        for (const LayerRegion *layerm : object.get_layer(int(i))->regions())
            if (std::any_of(layerm->slices.surfaces.begin(), layerm->slices.surfaces.end(),
                            [](const Surface &surface) { return surface.surface_type == stTop; }) &&
                index.try_emplace(layerm->region().config().top_surface_filament_id.value, out.region_ids.size()).second) {
                out.region_ids.emplace_back(layerm->region().print_object_region_id());
                out.filaments.emplace_back(layerm->region().config().top_surface_filament_id.value);
            }
    if (out.filaments.size() < 2)
        return out;
    out.region_ids.clear();
    out.filaments.clear();
    index.clear();
    std::vector<Polygons> tops;
    Polygons              covered;
    for (size_t i = object.layer_count(); i-- > 0;) {
        Polygons layer_tops;
        for (const LayerRegion *layerm : object.get_layer(int(i))->regions()) {
            const PrintRegionConfig &cfg = layerm->region().config();
            Polygons top;
            for (const Surface &surface : layerm->slices.surfaces)
                if (surface.surface_type == stTop)
                    polygons_append(top, to_polygons(surface.expolygon));
            if (top.empty())
                continue;
            const auto [entry, inserted] = index.try_emplace(cfg.top_surface_filament_id.value, out.region_ids.size());
            if (inserted) {
                out.region_ids.emplace_back(layerm->region().print_object_region_id());
                out.filaments.emplace_back(cfg.top_surface_filament_id.value);
                tops.emplace_back();
            }
            polygons_append(tops[entry->second], covered.empty() ? top : diff(top, covered));
            polygons_append(layer_tops, std::move(top));
        }
        if (!layer_tops.empty())
            covered = union_(covered, layer_tops);
    }
    if (tops.size() < 2)
        return out;
    const float seam = float(scale_(0.01));
    ExPolygons  assigned;
    for (const Polygons &top : tops) {
        out.areas.emplace_back(diff_ex(intersection_ex(candidate, offset(top, seam)), assigned));
        append(assigned, out.areas.back());
    }
    const float half_road = float(scale_(0.5 * smallest_nozzle(object.print()->config())));
    ExPolygons  slivers;
    for (ExPolygons &area : out.areas) {
        ExPolygons kept = opening_ex(area, half_road);
        append(slivers, diff_ex(area, kept));
        area = std::move(kept);
    }
    for (const ExPolygon &sliver : union_ex(slivers)) {
        const ExPolygons around = offset_ex(sliver, 2.f * half_road);
        size_t           best = 0;
        double           most = -1.;
        for (size_t c = 0; c < out.areas.size(); ++c)
            if (const double a = area(intersection_ex(around, out.areas[c])); a > most) {
                most = a;
                best = c;
            }
        out.areas[best] = union_ex(out.areas[best], ExPolygons{ sliver });
    }
    BoundingBoxf bbox;
    for (const ExPolygon &expoly : candidate)
        for (const Point &pt : expoly.contour.points)
            bbox.merge(unscale(pt));
    bbox.min -= Vec2d(1., 1.);
    bbox.max += Vec2d(1., 1.);
    out.lookup = HeightField(bbox, ColourPitch);
    for (size_t c = 0; c < out.areas.size(); ++c) {
        const Mask mask = rasterize(out.areas[c], out.lookup);
        for (size_t n = 0; n < mask.size(); ++n)
            if (mask[n])
                out.lookup.data()[n] = float(c);
    }
    const int nx = out.lookup.nx(), ny = out.lookup.ny();
    for (int pass = 0; pass < int(1. / ColourPitch) + 2; ++pass) {
        std::vector<float> next = out.lookup.data();
        bool               grew = false;
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix)
                if (std::isnan(out.lookup.at(ix, iy)))
                    for (const auto &[dx, dy] : { std::pair(1, 0), std::pair(-1, 0), std::pair(0, 1), std::pair(0, -1) })
                        if (ix + dx >= 0 && ix + dx < nx && iy + dy >= 0 && iy + dy < ny && !std::isnan(out.lookup.at(ix + dx, iy + dy))) {
                            next[size_t(iy) * nx + ix] = out.lookup.at(ix + dx, iy + dy);
                            grew                       = true;
                            break;
                        }
        out.lookup.data() = std::move(next);
        if (!grew)
            break;
    }
    return out;
}

std::function<int(const Vec3d &)> planar_colour(const PrintObject &object, const TopColours &colours, const ExPolygons &area, double pitch)
{
    struct Layers
    {
        std::vector<double>                       z;
        std::vector<std::unique_ptr<HeightField>> fields;
        std::mutex                                mutex;
    };
    auto layers = std::make_shared<Layers>();
    for (const Layer *layer : object.layers())
        layers->z.emplace_back(layer->print_z);
    layers->fields.resize(layers->z.size());
    BoundingBoxf bbox;
    for (const ExPolygon &expoly : area)
        for (const Point &pt : expoly.contour.points)
            bbox.merge(unscale(pt));
    bbox.min -= Vec2d(1., 1.);
    bbox.max += Vec2d(1., 1.);
    return [layers, &object, &colours, bbox, pitch](const Vec3d &p) {
        if (layers->z.empty() || !bbox.defined)
            return colours.at(p.head<2>());
        const size_t i = std::min(layers->z.size() - 1,
                                  size_t(std::lower_bound(layers->z.begin(), layers->z.end(), p.z() - EPSILON) - layers->z.begin()));
        const HeightField *field;
        {
            std::lock_guard<std::mutex> lock(layers->mutex);
            if (!layers->fields[i]) {
                auto f = std::make_unique<HeightField>(bbox, pitch);
                for (const LayerRegion *layerm : object.get_layer(int(i))->regions()) {
                    const auto c = std::find(colours.filaments.begin(), colours.filaments.end(),
                                             layerm->region().config().outer_wall_filament_id.value);
                    if (c == colours.filaments.end())
                        continue;
                    const Mask mask = rasterize(to_expolygons(layerm->slices.surfaces), *f);
                    for (size_t n = 0; n < mask.size(); ++n)
                        if (mask[n])
                            f->data()[n] = float(c - colours.filaments.begin());
                }
                layers->fields[i] = std::move(f);
            }
            field = layers->fields[i].get();
        }
        const Vec2d o  = (p.head<2>() - field->origin()) / field->pitch();
        const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
        const float c  = ix >= 0 && iy >= 0 && ix < field->nx() && iy < field->ny() ? field->at(ix, iy) : HeightField::NoData;
        return std::isnan(c) ? colours.at(p.head<2>()) : int(c);
    };
}

std::vector<std::pair<size_t, size_t>> split_by_colour(ConformalResult &result, const TopColours &colours,
                                                       const std::function<int(const Vec3d &)> &colour)
{
    std::function<int(const Vec3d &)> colour_at = colour;
    if (!colour_at)
        colour_at = [&colours](const Vec3d &p) { return colours.at(p.head<2>()); };
    std::vector<std::pair<size_t, size_t>> sources;
    std::vector<ConformalPatch>   patches;
    std::vector<PatchDiagnostics> diagnostics;
    for (size_t idx = 0; idx < result.patches.size(); ++idx) {
        ConformalPatch                      &comp = result.patches[idx];
        std::vector<std::optional<size_t>>   piece(colours.areas.size());
        auto patch_of = [&](int c) -> ConformalPatch & {
            if (!piece[size_t(c)]) {
                piece[size_t(c)] = patches.size();
                ConformalPatch &p = patches.emplace_back();
                p.kind         = comp.kind;
                p.footprint    = intersection_ex(comp.footprint, colours.areas[size_t(c)]);
                p.wall_band    = intersection_ex(comp.wall_band, colours.areas[size_t(c)]);
                p.surface      = comp.surface;
                p.base         = comp.base;
                p.shells       = comp.shells;
                p.sparse       = comp.sparse;
                p.bridged_base = comp.bridged_base;
                p.shell_height = comp.shell_height;
                p.z_min        = comp.z_min;
                p.z_max        = comp.z_max;
                p.host_layer   = comp.host_layer;
                p.region_id    = colours.region_ids[size_t(c)];
                p.band         = comp.band;
                p.automatic    = comp.automatic;
                p.shell_paths.assign(comp.shell_paths.size(), ExtrusionEntityCollection());
                for (ExtrusionEntityCollection &shell : p.shell_paths)
                    shell.no_sort = true;
                p.ironing.no_sort = true;
                diagnostics.emplace_back(result.diagnostics[idx]);
                sources.emplace_back(idx, size_t(c));
            }
            return patches[*piece[size_t(c)]];
        };
        for (size_t k = 0; k < comp.shell_paths.size(); ++k) {
            const ExtrusionEntitiesPtr &entities = comp.shell_paths[k].entities;
            if (entities.empty())
                continue;
            const double width = static_cast<const ExtrusionPath *>(entities.front())->width;
            cut_by_colour(entities, colour_at, 0.25 * width, width,
                          [&](int c, ExtrusionPathConformal *path) { patch_of(c).shell_paths[k].entities.emplace_back(path); });
        }
        if (!comp.ironing.entities.empty())
            cut_by_colour(comp.ironing.entities, colour_at, 0., static_cast<const ExtrusionPath *>(comp.ironing.entities.front())->width,
                          [&](int c, ExtrusionPathConformal *path) { patch_of(c).ironing.entities.emplace_back(path); });
        if (colour)
            for (size_t c = 0; c < piece.size(); ++c)
                if (piece[c]) {
                    ConformalPatch &p = patches[*piece[c]];
                    Polylines       roads;
                    float           width = 0.f;
                    for (const ExtrusionEntityCollection *pass : passes(p))
                        for (const ExtrusionEntity *ee : pass->entities) {
                            roads.emplace_back(ee->as_polyline());
                            width = std::max(width, static_cast<const ExtrusionPath *>(ee)->width);
                        }
                    Polygons own = offset(roads, float(scale_(0.5 * width)));
                    polygons_append(own, to_polygons(colours.areas[c]));
                    p.footprint = intersection_ex(comp.footprint, union_ex(own));
                }
    }
    for (size_t i = 0; i < patches.size(); ++i) {
        patches[i].id          = int(i);
        diagnostics[i].patch_id = int(i);
        for (ExtrusionEntityCollection *pass : passes(patches[i]))
            for (ExtrusionEntity *ee : pass->entities)
                static_cast<ExtrusionPathConformal *>(ee)->patch_id = int(i);
    }
    for (PatchDiagnostics &diag : result.diagnostics)
        if (diag.patch_id < 0)
            diagnostics.emplace_back(std::move(diag));
    result.patches     = std::move(patches);
    result.diagnostics = std::move(diagnostics);
    return sources;
}

void group_hosts_by_filament(PrintObject &object, ConformalResult &result)
{
    if (result.patches.empty() || !prints_several_filaments(object))
        return;
    LayerPtrs   &layers = object.layers();
    const size_t n      = layers.size();
    double       reach  = head_band_height(resolve_head_envelope(object.print()->config()));
    if (const double cap = effective_patch_height_cap(object); cap > 0.)
        reach = std::min(reach, cap);
    if (!(reach > 0.) || !std::isfinite(reach) || n == 0)
        return;
    std::vector<double> layer_z(n);
    for (size_t i = 0; i < n; ++i)
        layer_z[i] = layers[i]->print_z;
    const std::vector<double> cuts = event_cuts(object);

    std::vector<std::set<int>> planar(n);
    for (size_t i = 0; i < n; ++i)
        for (const LayerRegion *layerm : layers[i]->regions()) {
            const PrintRegionConfig &cfg = layerm->region().config();
            if (!layerm->perimeters.empty())
                planar[i].insert({ cfg.outer_wall_filament_id.value, cfg.inner_wall_filament_id.value });
            if (!layerm->fills.empty())
                planar[i].insert({ cfg.sparse_infill_filament_id.value, cfg.internal_solid_filament_id.value,
                                   cfg.top_surface_filament_id.value, cfg.bottom_surface_filament_id.value });
        }
    struct Item
    {
        int         filament;
        BoundingBox bbox;
        bool        movable;
    };
    std::vector<Item> items(result.patches.size());
    for (size_t i = 0; i < result.patches.size(); ++i) {
        const ConformalPatch &p = result.patches[i];
        items[i].filament       = patch_has_extrusions(p) ? patch_filament_id(object, p) : 0;
        items[i].bbox           = get_extents(p.footprint);
        items[i].bbox.offset(scale_(1.));
        items[i].movable        = items[i].filament > 0 && p.band >= 0 && (p.kind == PatchKind::Top || p.kind == PatchKind::Volume);
    }
    auto has_filament = [&](size_t layer, int filament, size_t except) {
        if (planar[layer].count(filament))
            return true;
        for (size_t q = 0; q < result.patches.size(); ++q)
            if (q != except && items[q].filament == filament && result.patches[q].host_layer == layer)
                return true;
        return false;
    };
    auto allowed = [&](size_t p, size_t h) {
        const ConformalPatch &P = result.patches[p];
        for (size_t q = 0; q < result.patches.size(); ++q) {
            if (q == p || items[q].filament == 0)
                continue;
            const ConformalPatch &Q = result.patches[q];
            const bool before = Q.host_layer <= h, after = Q.host_layer >= h;
            if (before && Q.z_max > P.z_min + reach + EPSILON)
                return false;
            if (after && P.z_max > Q.z_min + reach + EPSILON)
                return false;
            if (P.band >= 0 && Q.band >= 0 && P.band != Q.band && items[p].bbox.overlap(items[q].bbox)) {
                const bool same_slot = Q.host_layer == h && items[q].filament == items[p].filament;
                if (Q.band < P.band && !(Q.host_layer < h || same_slot))
                    return false;
                if (Q.band > P.band && !(Q.host_layer > h || same_slot))
                    return false;
            }
        }
        return true;
    };

    std::vector<size_t> order(result.patches.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const ConformalPatch &pa = result.patches[a], &pb = result.patches[b];
        return pa.host_layer != pb.host_layer ? pa.host_layer < pb.host_layer : pa.band < pb.band;
    });
    std::vector<size_t> old_host(result.patches.size());
    for (size_t i = 0; i < result.patches.size(); ++i)
        old_host[i] = result.patches[i].host_layer;
    size_t moved = 0;
    for (size_t p : order) {
        ConformalPatch &P = result.patches[p];
        if (!items[p].movable || has_filament(P.host_layer, items[p].filament, p))
            continue;
        const size_t lo = size_t(std::lower_bound(layer_z.begin(), layer_z.end(), P.z_max - EPSILON) - layer_z.begin());
        double       top = P.z_min + reach;
        if (const auto cut = std::lower_bound(cuts.begin(), cuts.end(), P.z_max - SCALING_FACTOR); cut != cuts.end())
            top = std::min(top, *cut);
        const size_t hi = size_t(std::upper_bound(layer_z.begin(), layer_z.end(), top + EPSILON) - layer_z.begin());
        std::vector<size_t> candidates;
        for (size_t h = lo; h < std::min(hi, n); ++h)
            if (h != P.host_layer && has_filament(h, items[p].filament, p))
                candidates.emplace_back(h);
        std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
            return std::abs(double(a) - double(P.host_layer)) < std::abs(double(b) - double(P.host_layer));
        });
        for (size_t h : candidates)
            if (allowed(p, h)) {
                P.host_layer = h;
                ++moved;
                break;
            }
    }
    if (moved == 0)
        return;
    for (size_t p = 0; p < result.patches.size(); ++p) {
        ConformalPatch &P = result.patches[p];
        if (P.host_layer == old_host[p])
            continue;
        const coord_t dz = coord_t(scale_(layer_z[old_host[p]] - layer_z[P.host_layer]));
        std::function<void(ExtrusionEntityCollection &)> rebase = [&](ExtrusionEntityCollection &collection) {
            for (ExtrusionEntity *ee : collection.entities)
                if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee))
                    rebase(*sub);
                else if (auto *path = dynamic_cast<ExtrusionPathConformal *>(ee))
                    for (Point3 &pt : path->polyline.points)
                        pt.z() += dz;
        };
        for (ExtrusionEntityCollection &shell : P.shell_paths)
            rebase(shell);
        rebase(P.ironing);
        std::vector<int> &from = layers[old_host[p]]->conformal_patches_after;
        from.erase(std::remove(from.begin(), from.end(), int(p)), from.end());
        layers[P.host_layer]->conformal_patches_after.emplace_back(int(p));
    }
    for (Layer *layer : layers)
        std::stable_sort(layer->conformal_patches_after.begin(), layer->conformal_patches_after.end(), [&result](int a, int b) {
            const ConformalPatch &pa = result.patches[size_t(a)], &pb = result.patches[size_t(b)];
            return pa.band != pb.band ? pa.band < pb.band : a < b;
        });
    BOOST_LOG_TRIVIAL(info) << "Non-planar: " << moved << " band patch(es) of " << object.model_object()->name
                            << " moved onto layers printing their filament";
}

namespace {

void object_filaments(const PrintObject &object, std::set<int> &filaments)
{
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
        const PrintRegionConfig &cfg = object.printing_region(region_id).config();
        for (int id : { cfg.outer_wall_filament_id.value, cfg.inner_wall_filament_id.value, cfg.sparse_infill_filament_id.value,
                        cfg.internal_solid_filament_id.value, cfg.top_surface_filament_id.value, cfg.bottom_surface_filament_id.value })
            filaments.insert(id);
    }
    if (object.has_support_material())
        for (int id : { object.config().support_filament.value, object.config().support_interface_filament.value })
            if (id > 0)
                filaments.insert(id);
}

}

bool prints_several_filaments(const PrintObject &object)
{
    std::set<int> filaments;
    object_filaments(object, filaments);
    return filaments.size() > 1;
}

bool layer_changes_filament(const PrintObject &object)
{
    if (prints_several_filaments(object))
        return true;
    const Print *print = object.print();
    if (print == nullptr || print->config().print_sequence == PrintSequence::ByObject)
        return false;
    std::set<int> filaments;
    for (const PrintObject *other : print->objects())
        object_filaments(*other, filaments);
    return filaments.size() > 1;
}

int event_slab(const std::vector<double> &cuts, double z)
{
    return int(std::lower_bound(cuts.begin(), cuts.end(), z) - cuts.begin());
}

void cut_at_events(ExtrusionPathConformal *piece, const std::vector<double> &cuts,
                   const std::function<void(int slab, ExtrusionPathConformal *part)> &sink)
{
    if (cuts.empty())
        return sink(0, piece);
    cut_by_colour(ExtrusionEntitiesPtr{ piece }, [&cuts](const Vec3d &p) { return event_slab(cuts, p.z()); }, 0., 0.,
                  [&sink](int slab, ExtrusionPathConformal *part) {
                      if (std::any_of(part->bead_height.begin(), part->bead_height.end(), [](float h) { return h > 0.f; }))
                          sink(slab, part);
                      else
                          delete part;
                  });
    delete piece;
}

size_t event_host(const std::vector<double> &cuts, int slab, const std::vector<double> &layer_z, size_t host)
{
    if (size_t(slab) >= cuts.size())
        return host;
    return std::min(host, size_t(std::lower_bound(layer_z.begin(), layer_z.end(), cuts[size_t(slab)] - EPSILON) - layer_z.begin()));
}

std::vector<double> event_cuts(const PrintObject &object)
{
    std::vector<double> cuts;
    const Print        *print = object.print();
    if (print == nullptr || print->config().print_sequence == PrintSequence::ByObject || object.layers().size() < 2)
        return cuts;
    const ConstLayerPtrsAdaptor layers = object.layers();
    for (const CustomGCode::Item &item : print->model().get_curr_plate_custom_gcodes().gcodes) {
        if (item.print_z > layers.back()->print_z + 0.5 * layers.back()->height)
            continue;
        size_t l = 0;
        if (item.type == CustomGCode::ToolChange)
            while (l < layers.size() && layers[l]->print_z < item.print_z - EPSILON)
                ++l;
        else
            while (l + 1 < layers.size() && 0.5 * (layers[l]->print_z + layers[l + 1]->print_z) < item.print_z)
                ++l;
        if (l > 0 && l < layers.size())
            cuts.emplace_back(layers[l - 1]->print_z);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    return cuts;
}

void host_before_events(PrintObject &object, ConformalResult &result)
{
    LayerPtrs &layers = object.layers();
    for (const double cut : event_cuts(object)) {
        const size_t below = size_t(std::lower_bound(layers.begin(), layers.end(), cut - EPSILON,
                                                     [](const Layer *layer, double z) { return layer->print_z < z; }) - layers.begin());
        if (below + 1 >= layers.size() || layers[below + 1]->conformal_patches_before.empty())
            continue;
        const coord_t dz = coord_t(scale_(layers[below + 1]->print_z - layers[below]->print_z));
        std::function<void(ExtrusionEntityCollection &)> rebase = [&](ExtrusionEntityCollection &collection) {
            for (ExtrusionEntity *ee : collection.entities)
                if (auto *sub = dynamic_cast<ExtrusionEntityCollection *>(ee))
                    rebase(*sub);
                else if (auto *path = dynamic_cast<ExtrusionPathConformal *>(ee))
                    for (Point3 &pt : path->polyline.points)
                        pt.z() += dz;
        };
        for (const int id : layers[below + 1]->conformal_patches_before) {
            ConformalPatch &patch = result.patches[size_t(id)];
            for (ExtrusionEntityCollection &shell : patch.shell_paths)
                rebase(shell);
            rebase(patch.ironing);
            patch.host_layer = below;
            layers[below]->conformal_patches_after.emplace_back(id);
        }
        layers[below + 1]->conformal_patches_before.clear();
    }
}

double multi_material_patch_height(const PrintObject &object, const HeadEnvelope &head, double max_patch_height)
{
    if (!layer_changes_filament(object))
        return max_patch_height;
    double layer_height = 0.;
    for (const Layer *layer : object.layers())
        layer_height = std::max(layer_height, layer->height);
    const double reach = head_band_height(head) - layer_height;
    if (!(reach >= MinBandReach))
        throw RuntimeError("Head clearance cannot support non-planar bands with several filaments: the head must clear "
                           "a layer printed after each band. Check the head model and margin.");
    return max_patch_height > 0. ? std::min(max_patch_height, reach) : reach;
}

namespace {

constexpr double SparseStub     = 2.;
constexpr double MinSparsePiece = 2.;

double extruded_length(const ExtrusionPathConformal &path)
{
    double length = 0.;
    for (size_t i = 0; i + 1 < path.polyline.points.size(); ++i)
        if (path.bead_height[i] > 0.f && path.bead_height[i + 1] > 0.f)
            length += unscale<double>((path.polyline.points[i + 1].to_point() - path.polyline.points[i].to_point()).cast<double>().norm());
    return length;
}

constexpr double PartCell = 5.;

std::vector<ConformalPatch> split_by_reach(ConformalPatch &&p, const ExPolygons &area, double floor,
                                           const std::function<bool(const HeightField &, const Vec2d &)> &wall)
{
    const HeightField &G      = p.surface;
    const Mask         inside = rasterize(area, G);
    Mask               reach(inside.size(), 0);
    for (int iy = 0; iy < G.ny(); ++iy)
        for (int ix = 0; ix < G.nx(); ++ix) {
            const size_t n = size_t(iy) * G.nx() + ix;
            const float  z = G.at(ix, iy);
            reach[n] = inside[n] && !std::isnan(z) && z >= floor - 0.01 && !(wall && wall(G, G.node(ix, iy)));
        }
    int                    count = 0;
    const std::vector<int> label = label_components(reach, G.nx(), G.ny(), count);
    auto component_of = [&](const ExtrusionPathConformal &path) {
        for (const Point3 &pt : path.polyline.points) {
            const Vec2d o  = (unscale(pt.to_point()) - G.origin()) / G.pitch();
            const int   cx = int(std::lround(o.x())), cy = int(std::lround(o.y()));
            for (int r = 0; r <= 2; ++r)
                for (int iy = std::max(0, cy - r); iy <= std::min(G.ny() - 1, cy + r); ++iy)
                    for (int ix = std::max(0, cx - r); ix <= std::min(G.nx() - 1, cx + r); ++ix)
                        if (const int l = label[size_t(iy) * G.nx() + ix]; l >= 0)
                            return l;
        }
        return 0;
    };
    std::vector<std::pair<size_t, ExtrusionPathConformal *>> pieces;
    for (size_t k = 0; k < p.shell_paths.size(); ++k)
        for (ExtrusionEntity *ee : p.shell_paths[k].entities)
            pieces.emplace_back(k, static_cast<ExtrusionPathConformal *>(ee));
    for (ExtrusionEntity *ee : p.ironing.entities)
        pieces.emplace_back(p.shell_paths.size(), static_cast<ExtrusionPathConformal *>(ee));
    std::vector<size_t> parent(pieces.size());
    std::iota(parent.begin(), parent.end(), size_t(0));
    auto find = [&parent](size_t i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    std::map<std::tuple<int, int64_t, int64_t>, size_t> owner;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const int      l   = count > 1 ? component_of(*pieces[i].second) : 0;
        const Points3 &pts = pieces[i].second->polyline.points;
        auto visit = [&](const Vec2d &q) {
            const int64_t cx = int64_t(std::floor(q.x() / PartCell)), cy = int64_t(std::floor(q.y() / PartCell));
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dx = -1; dx <= 1; ++dx)
                    if (const auto it = owner.find({ l, cx + dx, cy + dy }); it != owner.end())
                        parent[find(it->second)] = find(i);
            owner.try_emplace({ l, cx, cy }, i);
        };
        for (size_t j = 0; j < pts.size(); ++j) {
            const Vec2d q = unscale(pts[j].to_point());
            if (j == 0)
                visit(q);
            else {
                const Vec2d a = unscale(pts[j - 1].to_point());
                const int   n = std::max(1, int(std::ceil((q - a).norm() / PartCell)));
                for (int t = 1; t <= n; ++t)
                    visit(a + (q - a) * (double(t) / n));
            }
        }
    }
    std::map<size_t, size_t> part_index;
    for (size_t i = 0; i < pieces.size(); ++i)
        part_index.try_emplace(find(i), part_index.size());
    std::vector<ConformalPatch> parts(part_index.size());

    const size_t n_shells = p.shell_paths.size();
    for (ConformalPatch &part : parts) {
        part.shell_paths.resize(n_shells);
        for (ExtrusionEntityCollection &shell : part.shell_paths)
            shell.no_sort = true;
        part.ironing.no_sort = true;
    }
    for (size_t i = 0; i < pieces.size(); ++i) {
        ConformalPatch &part = parts[part_index.at(find(i))];
        (pieces[i].first < n_shells ? part.shell_paths[pieces[i].first] : part.ironing).entities.emplace_back(pieces[i].second);
    }
    for (size_t pa = 0; pa < parts.size() && parts.size() > 1; ++pa)
        for (size_t k = 0; k <= n_shells; ++k) {
            ExtrusionEntitiesPtr &mine = k < n_shells ? parts[pa].shell_paths[k].entities : parts[pa].ironing.entities;
            for (size_t i = 0; i < mine.size();) {
                const auto *s     = static_cast<const ExtrusionPathConformal *>(mine[i]);
                if (!is_short_road(*s)) {
                    ++i;
                    continue;
                }
                size_t      to    = parts.size();
                double      best  = std::numeric_limits<double>::max();
                for (size_t pb = 0; pb < parts.size(); ++pb) {
                    if (pb == pa)
                        continue;
                    for (const ExtrusionEntity *ee : k < n_shells ? parts[pb].shell_paths[k].entities : parts[pb].ironing.entities) {
                        double d = 0.;
                        Point  ja, jb;
                        if (!short_road_joint(*s, *static_cast<const ExtrusionPathConformal *>(ee), d, ja, jb) || d >= best)
                            continue;
                        bool across_wall = false;
                        const Vec2d qa = unscale(ja), qb = unscale(jb);
                        const int   n  = std::max(1, int(std::ceil((qb - qa).norm() / (0.5 * G.pitch()))));
                        for (int t = 0; t <= n && wall && !across_wall; ++t)
                            across_wall = wall(G, qa + (qb - qa) * (double(t) / n));
                        if (!across_wall) {
                            best = d;
                            to   = pb;
                        }
                    }
                }
                if (to == parts.size()) {
                    ++i;
                    continue;
                }
                (k < n_shells ? parts[to].shell_paths[k].entities : parts[to].ironing.entities).emplace_back(mine[i]);
                mine.erase(mine.begin() + i);
            }
        }
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [](const ConformalPatch &part) {
                                   return part.ironing.entities.empty() &&
                                          std::all_of(part.shell_paths.begin(), part.shell_paths.end(),
                                                      [](const ExtrusionEntityCollection &shell) { return shell.entities.empty(); });
                               }),
                parts.end());
    if (parts.size() <= 1) {
        for (ConformalPatch &part : parts) {
            for (ExtrusionEntityCollection &shell : part.shell_paths)
                shell.entities.clear();
            part.ironing.entities.clear();
        }
        parts.clear();
        parts.emplace_back(std::move(p));
        return parts;
    }
    for (ExtrusionEntityCollection &shell : p.shell_paths)
        shell.entities.clear();
    p.ironing.entities.clear();
    std::vector<int>    owner_of(size_t(G.nx()) * G.ny(), -1);
    std::vector<size_t> queue;
    for (size_t i = 0; i < parts.size(); ++i)
        for (const ExtrusionEntityCollection &shell : parts[i].shell_paths)
            for (const ExtrusionEntity *ee : shell.entities)
                for (const Point3 &pt : static_cast<const ExtrusionPathConformal *>(ee)->polyline.points) {
                    const Vec2d o  = (unscale(pt.to_point()) - G.origin()) / G.pitch();
                    const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
                    if (ix >= 0 && iy >= 0 && ix < G.nx() && iy < G.ny() && owner_of[size_t(iy) * G.nx() + ix] < 0) {
                        owner_of[size_t(iy) * G.nx() + ix] = int(i);
                        queue.emplace_back(size_t(iy) * G.nx() + ix);
                    }
                }
    for (size_t head = 0; head < queue.size(); ++head) {
        const size_t n  = queue[head];
        const int    ix = int(n % G.nx()), iy = int(n / G.nx());
        for (const auto &[dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } }) {
            const int jx = ix + dx, jy = iy + dy;
            if (jx < 0 || jy < 0 || jx >= G.nx() || jy >= G.ny() || owner_of[size_t(jy) * G.nx() + jx] >= 0)
                continue;
            owner_of[size_t(jy) * G.nx() + jx] = owner_of[n];
            queue.emplace_back(size_t(jy) * G.nx() + jx);
        }
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        ConformalPatch                        &part  = parts[i];
        std::vector<ExtrusionEntityCollection> paths   = std::move(part.shell_paths);
        ExtrusionEntityCollection              ironing = std::move(part.ironing);
        part             = p;
        part.shell_paths = std::move(paths);
        part.ironing     = std::move(ironing);
        Mask mine(owner_of.size(), 0);
        int  ix0 = G.nx(), iy0 = G.ny(), ix1 = -1, iy1 = -1;
        for (size_t n = 0; n < owner_of.size(); ++n)
            if (owner_of[n] == int(i)) {
                mine[n]      = 1;
                const int ix = int(n % G.nx()), iy = int(n / G.nx());
                ix0 = std::min(ix0, ix), iy0 = std::min(iy0, iy), ix1 = std::max(ix1, ix), iy1 = std::max(iy1, iy);
            }
        part.footprint = intersection_ex(p.footprint, mask_to_expolygons(mine, G, ix0, iy0, ix1, iy1));
        part.wall_band = intersection_ex(part.footprint, p.wall_band);
    }
    return parts;
}

void order_band_part(ConformalPatch &p, const BandSeams &seams, size_t comp, int band, double pitch, double tolerance, Point &cursor)
{
    const HeightField &G      = p.surface;
    const Mask         inside = rasterize(seams.areas[comp], G);
    RoadGrid           roads;
    const double       floor  = band * pitch;
    const double       top    = (band + 1) * pitch + tolerance;
    for (size_t k = 0; k <= p.shell_paths.size(); ++k) {
        const bool                 ironing = k == p.shell_paths.size();
        ExtrusionEntityCollection &shell   = ironing ? p.ironing : p.shell_paths[k];
        const double               drop    = ironing ? 0. : double(p.shells - 1 - int(k)) * p.shell_height;
        const bool                 hidden  = !ironing && int(k) < seams.hidden;
        const HeightField *count = hidden && seams.layers != nullptr ? &(*seams.layers)[comp] : nullptr;
        auto glide_z = [&](const Vec2d &q, double &z) {
            const Vec2d o  = (q - G.origin()) / G.pitch();
            const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
            if (ix < 0 || iy < 0 || ix >= G.nx() || iy >= G.ny() || !inside[size_t(iy) * G.nx() + ix] || (seams.wall && seams.wall(G, q)))
                return GlideAt::No;
            double d = drop;
            if (!ironing && seams.drop) {
                if (!seams.drop(comp, int(k), q, d))
                    return GlideAt::No;
            } else if (count != nullptr) {
                const Vec2d c  = (q - count->origin()) / count->pitch();
                const int   cx = int(std::lround(c.x())), cy = int(std::lround(c.y()));
                if (cx < 0 || cy < 0 || cx >= count->nx() || cy >= count->ny() || !(count->at(cx, cy) > float(p.shells - 1 - int(k))))
                    return GlideAt::No;
            }
            const float s = G.sample(q);
            if (std::isnan(s))
                return GlideAt::No;
            z = s - d;
            if (hidden)
                z = z < floor ? floor + tolerance : std::min(z, top);
            return z >= floor - 0.01 ? GlideAt::Ok : GlideAt::Range;
        };
        const ExtrusionEntityCollection *below = nullptr;
        for (size_t j = k; j-- > 0 && below == nullptr;) {
            if (!p.shell_paths[j].entities.empty())
                below = &p.shell_paths[j];
            if (!ironing)
                break;
        }
        const Point3 *from = below != nullptr ? &static_cast<const ExtrusionPathConformal *>(below->entities.back())->polyline.points.back() :
                                                nullptr;
        order_band_pieces(shell.entities, cursor, roads, glide_z, seams.max_glide, G.pitch(), hidden || ironing, floor, top, &G, from);
        shell.no_sort = true;
    }
}

void order_nearest_from_lowest(ExtrusionEntityCollection &shell)
{
    ExtrusionEntity *lowest_piece = nullptr;
    coord_t          lowest_z     = std::numeric_limits<coord_t>::max();
    for (ExtrusionEntity *ee : shell.entities) {
        for (const Point3 &pt : static_cast<const ExtrusionPathConformal *>(ee)->polyline.points) {
            if (pt.z() < lowest_z) {
                lowest_z     = pt.z();
                lowest_piece = ee;
            }
        }
    }
    ExtrusionEntitiesPtr left = std::move(shell.entities);
    shell.entities.clear();
    RoadGrid             roads;
    if (lowest_piece != nullptr) {
        std::swap(*std::find(left.begin(), left.end(), lowest_piece), left.front());
        shell.entities.push_back(left.front());
        roads.add(*static_cast<const ExtrusionPathConformal *>(left.front()));
        left.erase(left.begin());
    }
    while (!left.empty()) {
        const Point last    = shell.entities.back()->last_point();
        size_t      best    = 0;
        bool        flip    = false;
        double      best_d2 = std::numeric_limits<double>::max();
        for (size_t j = 0; j < left.size(); ++j) {
            const double d_first = (left[j]->first_point() - last).cast<double>().squaredNorm();
            const double d_last  = (left[j]->last_point() - last).cast<double>().squaredNorm();
            if (std::min(d_first, d_last) < best_d2) {
                best_d2 = std::min(d_first, d_last);
                best    = j;
                flip    = d_last < d_first;
            }
        }
        if (flip)
            left[best]->reverse();
        auto *prev = static_cast<ExtrusionPathConformal *>(shell.entities.back());
        auto *next = static_cast<ExtrusionPathConformal *>(left[best]);
        std::swap(left[best], left.back());
        left.pop_back();
        const Point3 a = prev->polyline.points.back(), b = next->polyline.points.front();
        if ((unscale(b.to_point()) - unscale(a.to_point())).norm() <= 2. * std::min(prev->width, next->width) && prev->role() == next->role() &&
            prev->width == next->width && prev->mm3_per_mm == next->mm3_per_mm && prev->tool_axis.empty() && next->tool_axis.empty() &&
            prev->bead_height.back() > 0.f && next->bead_height.front() > 0.f && !roads.crosses(a.to_point(), b.to_point())) {
            if (!prev->bead_width.empty() || !next->bead_width.empty()) {
                for (ExtrusionPathConformal *path : { prev, next })
                    if (path->bead_width.empty())
                        path->bead_width.assign(path->polyline.points.size(), float(path->width));
                append(prev->bead_width, next->bead_width);
            }
            append(prev->polyline.points, next->polyline.points);
            append(prev->bead_height, next->bead_height);
            roads.add(*next);
            delete next;
            continue;
        }
        roads.add(*next);
        shell.entities.push_back(next);
    }
    absorb_short_roads(shell.entities);
    shell.no_sort = true;
}

}
std::vector<ConformalPatch> split_into_bands(ConformalResult &result, const HeightField &S, const std::vector<double> &layer_z,
                                             double pitch, double tolerance, const BandSeams *seams, bool host_above, int patch_offset, std::vector<size_t> *band_component,
                                             const std::vector<double> &cuts)
{
    std::vector<ConformalPatch>   components = std::move(result.patches);
    std::vector<PatchDiagnostics> comp_diag  = std::move(result.diagnostics);
    result.patches.clear();
    result.diagnostics.clear();

    struct Band
    {
        int              band;
        int              slab;
        size_t           comp;
        ConformalPatch   patch;
        PatchDiagnostics diag;
        double           err_sq = 0.;
        size_t           err_n  = 0;
    };
    std::vector<Band> bands;
    auto band_of = [pitch](double z) { return int(std::floor(z / pitch)); };
    for (size_t c = 0; c < components.size(); ++c) {
        ConformalPatch                       &comp = components[c];
        std::map<std::pair<int, int>, size_t> index;
        auto band = [&](int b, int slab) -> Band & {
            const auto [it, inserted] = index.try_emplace({ b, slab }, bands.size());
            if (inserted) {
                Band nb{ b, slab, c, {}, {} };
                ConformalPatch &p = nb.patch;
                p.kind         = comp.kind;
                p.surface      = comp.surface;
                p.base         = comp.base;
                p.shells       = comp.shells;
                p.sparse       = comp.sparse;
                p.bridged_base = comp.bridged_base;
                p.shell_height = comp.shell_height;
                p.region_id    = comp.region_id;
                p.automatic    = comp.automatic;
                p.z_min        = Inf;
                p.z_max        = -Inf;
                p.shell_paths.assign(size_t(comp.shells), ExtrusionEntityCollection());
                bands.emplace_back(std::move(nb));
            }
            return bands[it->second];
        };
        std::function<int(const Point3 &)> cut_rank;
        Mask                               on_seam, near_edge;
        const HeightField                 &G = comp.surface;
        if (seams != nullptr) {
            const ExPolygons &area = seams->areas[c];
            on_seam   = rasterize(union_ex(offset(seams->seam_lines[c], float(scale_(0.75 * seams->spacing)))), G);
            near_edge = rasterize(diff_ex(area, offset_ex(area, -float(scale_(1.5 * seams->spacing)))), G);
            cut_rank  = [&](const Point3 &q) {
                const Vec2d o  = (unscale(q.to_point()) - G.origin()) / G.pitch();
                const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
                if (ix < 0 || iy < 0 || ix >= G.nx() || iy >= G.ny())
                    return 0;
                const size_t n = size_t(iy) * G.nx() + ix;
                return on_seam[n] ? 2 : near_edge[n] ? 1 : 0;
            };
        }
        for (size_t k = 0; k < comp.shell_paths.size(); ++k)
            cut_into_bands(comp.shell_paths[k].entities, pitch, tolerance, [&](int b, ExtrusionPathConformal *piece) {
                if (seams != nullptr && piece->role() == erInternalInfill &&
                    extruded_length(*piece) < std::max(SparseStub * piece->width, MinSparsePiece)) {
                    delete piece;
                    return;
                }
                cut_at_events(piece, cuts, [&](int slab, ExtrusionPathConformal *part) {
                    ConformalPatch &p = band(b, slab).patch;
                    for (const Point3 &pt : part->polyline.points) {
                        p.z_min = std::min(p.z_min, unscale<double>(pt.z()));
                        p.z_max = std::max(p.z_max, unscale<double>(pt.z()));
                    }
                    p.shell_paths[k].entities.emplace_back(part);
                });
            }, cut_rank);
        comp.shell_paths.clear();
        cut_into_bands(comp.ironing.entities, pitch, 0., [&](int b, ExtrusionPathConformal *piece) {
            cut_at_events(piece, cuts, [&](int slab, ExtrusionPathConformal *part) { band(b, slab).patch.ironing.entities.emplace_back(part); });
        }, cut_rank);
        comp.ironing.clear();

        const HeightField &F  = comp.surface;
        const Mask         in = rasterize(comp.footprint, F);
        const int  slabs = int(cuts.size()) + 1;
        RunGroups  cells;
        for (int iy = 0; iy < F.ny(); ++iy)
            for (int ix = 0; ix < F.nx(); ++ix) {
                const size_t n = size_t(iy) * F.nx() + ix;
                const float  z = F.data()[n];
                if (!in[n] || std::isnan(z))
                    continue;
                const int b    = band_of(z);
                const int slab = event_slab(cuts, z);
                cells.add(b * slabs + slab, ix, iy);
                Band       &bd = band(b, slab);
                const Vec2d  p = F.node(ix, iy);
                const Vec2d  g = F.gradient(p);
                const Vec2d  o = (p - S.origin()) / S.pitch();
                const double e = S.at(int(std::lround(o.x())), int(std::lround(o.y()))) - z;
                if (!std::isnan(e)) {
                    bd.diag.max_surface_error = std::max(bd.diag.max_surface_error, e);
                    bd.err_sq += e * e;
                    ++bd.err_n;
                }
                if (!std::isnan(g.x()) && !std::isnan(g.y()))
                    bd.diag.max_slope_deg = std::max(bd.diag.max_slope_deg, Geometry::rad2deg(std::atan(g.norm())));
                bd.patch.z_max = std::max(bd.patch.z_max, double(z));
            }
        for (const auto &[key, runs] : cells.groups()) {
            const int       b = key >= 0 ? key / slabs : -((-key + slabs - 1) / slabs);
            ConformalPatch &p = band(b, key - b * slabs).patch;
            p.footprint       = intersection_ex(comp.footprint, runs_to_expolygons(runs, F));
            p.wall_band       = intersection_ex(p.footprint, comp.wall_band);
        }
        if (!index.empty()) {
            PatchDiagnostics &diag = bands[index.begin()->second].diag;
            diag.fallback_area = comp_diag[c].fallback_area;
            diag.fallback_reason = comp_diag[c].fallback_reason;
            diag.fallback_shape = comp_diag[c].fallback_shape;
            diag.fallback_hatch = comp_diag[c].fallback_hatch;
            diag.repaired_nodes = comp_diag[c].repaired_nodes;
            diag.repair_lowering = comp_diag[c].repair_lowering;
        }
    }

    std::stable_sort(bands.begin(), bands.end(),
                     [](const Band &a, const Band &b) { return a.band != b.band ? a.band < b.band : a.slab < b.slab; });
    for (Band &bd : bands) {
        ConformalPatch &p = bd.patch;
        p.band            = bd.band;
        const double top  = (bd.band + 1) * pitch + tolerance;
        if (host_above) {
            const auto it = std::lower_bound(layer_z.begin(), layer_z.end(), top - EPSILON);
            p.host_layer  = std::min(size_t(it - layer_z.begin()), layer_z.size() - 1);
        } else {
            const auto it = std::upper_bound(layer_z.begin(), layer_z.end(), top + EPSILON);
            p.host_layer  = it == layer_z.begin() ? 0 : size_t(it - layer_z.begin()) - 1;
        }
        p.host_layer = event_host(cuts, bd.slab, layer_z, p.host_layer);
        const coord_t dz  = coord_t(scale_(layer_z[p.host_layer]));
        std::vector<ConformalPatch> parts;
        if (seams != nullptr)
            parts = split_by_reach(std::move(p), seams->areas[bd.comp], bd.band * pitch, seams->wall);
        else
            parts.emplace_back(std::move(p));
        Point cursor = seams != nullptr ? seams->seams[bd.comp] : Point(0, 0);
        for (size_t left = parts.size(); left > 0; --left) {
            size_t next = parts.size() - left;
            if (seams != nullptr) {
                double best = std::numeric_limits<double>::max();
                for (size_t i = parts.size() - left; i < parts.size(); ++i)
                    for (const ExtrusionEntityCollection &shell : parts[i].shell_paths)
                        for (const ExtrusionEntity *ee : shell.entities)
                            for (const Point &end : { ee->first_point(), ee->last_point() })
                                if (const double d = (end - cursor).cast<double>().squaredNorm(); d < best) {
                                    best = d;
                                    next = i;
                                }
                std::swap(parts[next], parts[parts.size() - left]);
            }
            ConformalPatch &part = parts[parts.size() - left];
            part.id              = patch_offset + int(result.patches.size());
            if (seams != nullptr)
                order_band_part(part, *seams, bd.comp, bd.band, pitch, tolerance, cursor);
            for (ExtrusionEntityCollection *pass : passes(part))
                for (ExtrusionEntity *ee : pass->entities) {
                    auto *path     = static_cast<ExtrusionPathConformal *>(ee);
                    path->patch_id = part.id;
                    for (Point3 &pt : path->polyline.points)
                        pt.z() -= dz;
                }
            if (seams == nullptr)
                for (ExtrusionEntityCollection *pass : passes(part))
                    order_nearest_from_lowest(*pass);
            if (part.z_min > part.z_max)
                part.z_min = part.z_max;
            ConformalPatch &comp = components[bd.comp];
            comp.host_layer      = std::max(comp.host_layer, part.host_layer);
            if (band_component != nullptr)
                band_component->emplace_back(bd.comp);
            PatchDiagnostics diag = bd.diag;
            diag.patch_id          = part.id;
            diag.rms_surface_error = bd.err_n > 0 ? std::sqrt(bd.err_sq / bd.err_n) : 0.;
            if (left < parts.size()) {
                diag.fallback_area   = 0.;
                diag.fallback_shape.clear();
                diag.fallback_hatch.clear();
                diag.repaired_nodes  = 0;
                diag.repair_lowering = 0.;
            }
            result.diagnostics.emplace_back(std::move(diag));
            result.patches.emplace_back(std::move(part));
        }
    }
    for (PatchDiagnostics &diag : comp_diag)
        if (diag.patch_id < 0)
            result.diagnostics.emplace_back(std::move(diag));
    return components;
}


}
