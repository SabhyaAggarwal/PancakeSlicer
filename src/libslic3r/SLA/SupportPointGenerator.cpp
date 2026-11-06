


#include <functional>
#include <cstddef>
#include <cassert>
#include <cmath>
#include <math.h>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <limits>
#include <tbb/parallel_for.h>

#include "SupportPointGenerator.hpp"
#include "Geometry/ConvexHull.hpp"
#include "Concurrency.hpp"
#include "ExPolygon.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/SLA/SupportPoint.hpp"
#include "libslic3r/Polygon.hpp"
#include "Point.hpp"
#include "ClipperUtils.hpp"
#include "Tesselate.hpp"
#include "MinAreaBoundingBox.hpp"
#include "libslic3r.h"

#include <iostream>
#include <random>
#include <vector>
#include <utility>
#include <unordered_map>
#include "libslic3r/ExtrusionEntity.hpp"

namespace Slic3r {
namespace sla {
































SupportPointGenerator::SupportPointGenerator(
        const sla::IndexedMesh &emesh,
        const std::vector<ExPolygons> &slices,
        const std::vector<float> &     heights,
        const Config &                 config,
        std::function<void(void)> throw_on_cancel,
        std::function<void(int)>  statusfn)
    : SupportPointGenerator(emesh, config, throw_on_cancel, statusfn)
{
    std::random_device rd;
    m_rng.seed(rd());
    execute(slices, heights);
}

SupportPointGenerator::SupportPointGenerator(
        const IndexedMesh &emesh,
        const SupportPointGenerator::Config &config,
        std::function<void ()> throw_on_cancel,
        std::function<void (int)> statusfn)
    : m_config(config)
    , m_emesh(emesh)
    , m_throw_on_cancel(throw_on_cancel)
    , m_statusfn(statusfn)
{
}

void SupportPointGenerator::execute(const std::vector<ExPolygons> &slices,
                                    const std::vector<float> &     heights)
{
    process(slices, heights);
    project_onto_mesh(m_output);
}

void SupportPointGenerator::project_onto_mesh(std::vector<sla::SupportPoint>& points) const
{



    static constexpr size_t gransize = 64;

    ccr_par::for_each(size_t(0), points.size(), [this, &points](size_t idx)
    {
        if ((idx % 16) == 0)

            m_throw_on_cancel();

        Vec3f& p = points[idx].pos;

        sla::IndexedMesh::hit_result hit_up   = m_emesh.query_ray_hit(p.cast<double>(), Vec3d(0., 0., 1.));
        sla::IndexedMesh::hit_result hit_down = m_emesh.query_ray_hit(p.cast<double>(), Vec3d(0., 0., -1.));

        bool up   = hit_up.is_hit();
        bool down = hit_down.is_hit();

        if (!up && !down)
            return;

        sla::IndexedMesh::hit_result& hit = (!down || (hit_up.distance() < hit_down.distance())) ? hit_up : hit_down;
        p = p + (hit.distance() * hit.direction()).cast<float>();
    }, gransize);
}

static std::vector<SupportPointGenerator::MyLayer> make_layers(
    const std::vector<ExPolygons>& slices, const std::vector<float>& heights,
    std::function<void(void)> throw_on_cancel)
{
    assert(slices.size() == heights.size());


    std::vector<SupportPointGenerator::MyLayer> layers;
    layers.reserve(slices.size());
    for (size_t i = 0; i < slices.size(); ++ i)
        layers.emplace_back(i, heights[i]);



    const float pixel_area = pow(0.047f, 2.f);

    ccr_par::for_each(size_t(0), layers.size(),
        [&layers, &slices, &heights, pixel_area, throw_on_cancel](size_t layer_id)
    {
        if ((layer_id % 8) == 0)


            throw_on_cancel();

        SupportPointGenerator::MyLayer &layer   = layers[layer_id];
        const ExPolygons &              islands = slices[layer_id];

        const float height = (layer_id > 2 ?
                                  heights[layer_id - 3] :
                                  heights[0] - (heights[1] - heights[0]));
        layer.islands.reserve(islands.size());
        for (const ExPolygon &island : islands) {
            float area = float(island.area() * SCALING_FACTOR * SCALING_FACTOR);
            if (area >= pixel_area)

                layer.islands.emplace_back(layer, island, get_extents(island.contour),
                                           unscaled<float>(island.contour.centroid()), area, height);
        }
    }, 32  );


    ccr_par::for_each(size_t(1), layers.size(),
                      [&layers, &heights, throw_on_cancel] (size_t layer_id)
    {
      if ((layer_id % 2) == 0)

          throw_on_cancel();
      SupportPointGenerator::MyLayer &layer_above = layers[layer_id];
      SupportPointGenerator::MyLayer &layer_below = layers[layer_id - 1];

      const float layer_height = (layer_id!=0 ? heights[layer_id]-heights[layer_id-1] : heights[0]);
      const float safe_angle = 35.f * (float(M_PI)/180.f);
      const float between_layers_offset = scaled<float>(layer_height * std::tan(safe_angle));
      const float slope_angle = 75.f * (float(M_PI)/180.f);
      const float slope_offset = scaled<float>(layer_height * std::tan(slope_angle));

      for (SupportPointGenerator::Structure &top : layer_above.islands) {
          for (SupportPointGenerator::Structure &bottom : layer_below.islands) {
              float overlap_area = top.overlap_area(bottom);
              if (overlap_area > 0) {
                  top.islands_below.emplace_back(&bottom, overlap_area);
                  bottom.islands_above.emplace_back(&top, overlap_area);
              }
          }
          if (! top.islands_below.empty()) {
              Polygons bottom_polygons = top.polygons_below();
              top.overhangs = diff_ex(*top.polygon, bottom_polygons);
              if (! top.overhangs.empty()) {




                  auto dangl_mask = expand(bottom_polygons, between_layers_offset, jtSquare);
                  auto overh_mask = expand(bottom_polygons, slope_offset, jtSquare);


                  top.overhangs = diff_ex(*top.polygon, overh_mask);




                  overh_mask = diff(overh_mask, dangl_mask);
                  dangl_mask = diff(dangl_mask, bottom_polygons);

                  top.dangling_areas = intersection_ex(*top.polygon, dangl_mask);
                  top.overhangs_slopes = intersection_ex(*top.polygon, overh_mask);

                  top.overhangs_area = 0.f;
                  std::vector<std::pair<ExPolygon*, float>> expolys_with_areas;
                  for (ExPolygon &ex : top.overhangs) {
                      float area = float(ex.area());
                      expolys_with_areas.emplace_back(&ex, area);
                      top.overhangs_area += area;
                  }
                  std::sort(expolys_with_areas.begin(), expolys_with_areas.end(),
                            [](const std::pair<ExPolygon*, float> &p1, const std::pair<ExPolygon*, float> &p2)
                            { return p1.second > p2.second; });
                  ExPolygons overhangs_sorted;
                  for (auto &p : expolys_with_areas)
                      overhangs_sorted.emplace_back(std::move(*p.first));
                  top.overhangs = std::move(overhangs_sorted);
                  top.overhangs_area *= float(SCALING_FACTOR * SCALING_FACTOR);
              }
          }
      }
    }, 8  );

    return layers;
}

void SupportPointGenerator::process(const std::vector<ExPolygons>& slices, const std::vector<float>& heights)
{
#ifdef SLA_SUPPORTPOINTGEN_DEBUG
    std::vector<std::pair<ExPolygon, coord_t>> islands;
#endif

    std::vector<SupportPointGenerator::MyLayer> layers = make_layers(slices, heights, m_throw_on_cancel);

    PointGrid3D point_grid;
    point_grid.cell_size = Vec3f(10.f, 10.f, 10.f);

    double increment = 100.0 / layers.size();
    double status    = 0;

    for (unsigned int layer_id = 0; layer_id < layers.size(); ++ layer_id) {
        SupportPointGenerator::MyLayer *layer_top     = &layers[layer_id];
        SupportPointGenerator::MyLayer *layer_bottom  = (layer_id > 0) ? &layers[layer_id - 1] : nullptr;
        std::vector<float>        support_force_bottom;
        if (layer_bottom != nullptr) {
            support_force_bottom.assign(layer_bottom->islands.size(), 0.f);
            for (size_t i = 0; i < layer_bottom->islands.size(); ++ i)
                support_force_bottom[i] = layer_bottom->islands[i].supports_force_total();
        }
        for (Structure &top : layer_top->islands)
            for (Structure::Link &bottom_link : top.islands_below) {
                Structure &bottom = *bottom_link.island;



                float &support_force = support_force_bottom[&bottom - layer_bottom->islands.data()];




                support_force *= std::min(1.f, 20.f * bottom.area / top.area);
            }

        if (layer_id > 0) {
            for (Structure &below : layer_bottom->islands) {
                float below_support_force = support_force_bottom[&below - layer_bottom->islands.data()];
                float above_overlap_area = 0.f;
                for (Structure::Link &above_link : below.islands_above)
                    above_overlap_area += above_link.overlap_area;
                for (Structure::Link &above_link : below.islands_above)
                    above_link.island->supports_force_inherited += below_support_force * above_link.overlap_area / above_overlap_area;
            }
        }

        for (Structure &s : layer_top->islands) {

            s.supports_force_inherited /= std::max(1.f, 0.17f * (s.overhangs_area) / s.area);

            add_support_points(s, point_grid);
        }

        m_throw_on_cancel();

        status += increment;
        m_statusfn(int(std::round(status)));

#ifdef SLA_SUPPORTPOINTGEN_DEBUG





#endif
    }
}

void SupportPointGenerator::add_support_points(SupportPointGenerator::Structure &s, SupportPointGenerator::PointGrid3D &grid3d)
{



    float tp      = m_config.tear_pressure();
    float current = s.supports_force_total();

    if (s.islands_below.empty()) {


        uniformly_cover({ *s.polygon }, s, s.area * tp, grid3d, IslandCoverageFlags(icfIsNew | icfWithBoundary) );
        return;
    }

    if (! s.overhangs.empty()) {
        uniformly_cover(s.overhangs, s, s.overhangs_area * tp, grid3d);
    }

    auto areafn = [](double sum, auto &p) { return sum + p.area() * SCALING_FACTOR * SCALING_FACTOR; };

    current = s.supports_force_total();
    if (! s.dangling_areas.empty()) {



        double a = std::accumulate(s.dangling_areas.begin(), s.dangling_areas.end(), 0., areafn);
        uniformly_cover(s.dangling_areas, s, a * tp - a * current * s.area, grid3d, icfWithBoundary);
    }

    current = s.supports_force_total();
    if (! s.overhangs_slopes.empty()) {
        double a = std::accumulate(s.overhangs_slopes.begin(), s.overhangs_slopes.end(), 0., areafn);
        uniformly_cover(s.overhangs_slopes, s, a * tp - a * current / s.area, grid3d, icfWithBoundary);
    }
}

std::vector<Vec2f> sample_expolygon(const ExPolygon &expoly, float samples_per_mm2, std::mt19937 &rng)
{

    std::vector<Vec2f> triangles = Slic3r::triangulate_expolygon_2f(expoly);

    std::vector<Vec2f> out;
    if (! triangles.empty())
    {

        auto   areas = reserve_vector<float>(triangles.size() / 3);
        double aback = 0.;
        for (size_t i = 0; i < triangles.size(); ) {
            const Vec2f &a  = triangles[i ++];
            const Vec2f  v1 = triangles[i ++] - a;
            const Vec2f  v2 = triangles[i ++] - a;


            areas.emplace_back(aback + 0.5f * std::abs(cross2(v1, v2)));
            aback = areas.back();
        }

        size_t num_samples = size_t(ceil(areas.back() * samples_per_mm2));
        std::uniform_real_distribution<> random_triangle(0., double(areas.back()));
        std::uniform_real_distribution<> random_float(0., 1.);
        for (size_t i = 0; i < num_samples; ++ i) {
            double r = random_triangle(rng);
            size_t idx_triangle = std::min<size_t>(std::upper_bound(areas.begin(), areas.end(), (float)r) - areas.begin(), areas.size() - 1) * 3;

            const Vec2f &a = triangles[idx_triangle ++];
            const Vec2f &b = triangles[idx_triangle++];
            const Vec2f &c = triangles[idx_triangle];
#if 1


            double u = float(std::sqrt(random_float(rng)));
            double v = float(random_float(rng));
            out.emplace_back(a * (1.f - u) + b * (u * (1.f - v)) + c * (v * u));
#else


            double u = float(random_float(rng));
            double v = float(random_float(rng));
            if (u + v >= 1.f) {
              u = 1.f - u;
              v = 1.f - v;
            }
            out.emplace_back(a + u * (b - a) + v * (c - a));
#endif
        }
    }
    return out;
}


std::vector<Vec2f> sample_expolygon(const ExPolygons &expolys, float samples_per_mm2, std::mt19937 &rng)
{
    std::vector<Vec2f> out;
    for (const ExPolygon &expoly : expolys)
        append(out, sample_expolygon(expoly, samples_per_mm2, rng));

    return out;
}

void sample_expolygon_boundary(const ExPolygon &   expoly,
                               float               samples_per_mm,
                               std::vector<Vec2f> &out,
                               std::mt19937 &       )
{
    double  point_stepping_scaled = scale_(1.f) / samples_per_mm;
    for (size_t i_contour = 0; i_contour <= expoly.holes.size(); ++ i_contour) {
        const Polygon &contour = (i_contour == 0) ? expoly.contour :
                                                    expoly.holes[i_contour - 1];

        const Points pts = contour.equally_spaced_points(point_stepping_scaled);
        for (size_t i = 0; i < pts.size(); ++ i)
            out.emplace_back(unscale<float>(pts[i].x()),
                             unscale<float>(pts[i].y()));
    }
}

std::vector<Vec2f> sample_expolygon_with_boundary(const ExPolygon &expoly, float samples_per_mm2, float samples_per_mm_boundary, std::mt19937 &rng)
{
    std::vector<Vec2f> out = sample_expolygon(expoly, samples_per_mm2, rng);
    sample_expolygon_boundary(expoly, samples_per_mm_boundary, out, rng);
    return out;
}

std::vector<Vec2f> sample_expolygon_with_boundary(const ExPolygons &expolys, float samples_per_mm2, float samples_per_mm_boundary, std::mt19937 &rng)
{
    std::vector<Vec2f> out;
    for (const ExPolygon &expoly : expolys)
        append(out, sample_expolygon_with_boundary(expoly, samples_per_mm2, samples_per_mm_boundary, rng));
    return out;
}

template<typename REFUSE_FUNCTION>
static inline std::vector<Vec2f> poisson_disk_from_samples(const std::vector<Vec2f> &raw_samples, float radius, REFUSE_FUNCTION refuse_function)
{
    Vec2f corner_min(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    for (const Vec2f &pt : raw_samples) {
        corner_min.x() = std::min(corner_min.x(), pt.x());
        corner_min.y() = std::min(corner_min.y(), pt.y());
    }


    struct RawSample
    {
        Vec2f coord;
        Vec2i32 cell_id;
        RawSample(const Vec2f &crd = {}, const Vec2i32 &id = {}): coord{crd}, cell_id{id} {}
    };

    auto raw_samples_sorted = reserve_vector<RawSample>(raw_samples.size());
    for (const Vec2f &pt : raw_samples)
        raw_samples_sorted.emplace_back(pt, ((pt - corner_min) / radius).cast<int>());

    std::sort(raw_samples_sorted.begin(), raw_samples_sorted.end(), [](const RawSample &lhs, const RawSample &rhs)
        { return lhs.cell_id.x() < rhs.cell_id.x() || (lhs.cell_id.x() == rhs.cell_id.x() && lhs.cell_id.y() < rhs.cell_id.y()); });

    struct PoissonDiskGridEntry {

        enum {
            max_positions = 4
        };
        Vec2f   poisson_samples[max_positions];
        int     num_poisson_samples = 0;


        int     first_sample_idx;
        int     sample_cnt;
    };

    struct CellIDHash {
        std::size_t operator()(const Vec2i32 &cell_id) const {
            return std::hash<int>()(cell_id.x()) ^ std::hash<int>()(cell_id.y() * 593);
        }
    };




    typedef std::unordered_map<Vec2i32, PoissonDiskGridEntry, CellIDHash> Cells;
    Cells cells;
    {
        typename Cells::iterator last_cell_id_it;
        Vec2i32           last_cell_id(-1, -1);
        for (size_t i = 0; i < raw_samples_sorted.size(); ++ i) {
            const RawSample &sample = raw_samples_sorted[i];
            if (sample.cell_id == last_cell_id) {


                ++ last_cell_id_it->second.sample_cnt;
            } else {

                PoissonDiskGridEntry data;
                data.first_sample_idx = int(i);
                data.sample_cnt       = 1;
                auto result     = cells.insert({sample.cell_id, data});
                last_cell_id    = sample.cell_id;
                last_cell_id_it = result.first;
            }
        }
    }

    const int   max_trials = 5;
    const float radius_squared = radius * radius;
    for (int trial = 0; trial < max_trials; ++ trial) {

        for (auto &it : cells) {
            const Vec2i32          &cell_id   = it.first;
            PoissonDiskGridEntry &cell_data = it.second;

            int next_sample_idx = cell_data.first_sample_idx + trial;
            if (trial >= cell_data.sample_cnt)

                continue;
            const RawSample &candidate = raw_samples_sorted[next_sample_idx];


            bool conflict = refuse_function(candidate.coord);
            for (int i = -1; i < 2 && ! conflict; ++ i) {
                for (int j = -1; j < 2; ++ j) {
                    const auto &it_neighbor = cells.find(cell_id + Vec2i32(i, j));
                    if (it_neighbor != cells.end()) {
                        const PoissonDiskGridEntry &neighbor = it_neighbor->second;
                        for (int i_sample = 0; i_sample < neighbor.num_poisson_samples; ++ i_sample)
                            if ((neighbor.poisson_samples[i_sample] - candidate.coord).squaredNorm() < radius_squared) {
                                conflict = true;
                                break;
                            }
                    }
                }
            }
            if (! conflict) {

                assert(cell_data.num_poisson_samples < cell_data.max_positions);
                if (cell_data.num_poisson_samples < cell_data.max_positions)
                    cell_data.poisson_samples[cell_data.num_poisson_samples ++] = candidate.coord;
            }
        }
    }


    std::vector<Vec2f> out;
    for (const auto& it : cells)
        for (int i = 0; i < it.second.num_poisson_samples; ++ i)
            out.emplace_back(it.second.poisson_samples[i]);
    return out;
}


void SupportPointGenerator::uniformly_cover(const ExPolygons& islands, Structure& structure, float deficit, PointGrid3D &grid3d, IslandCoverageFlags flags)
{


    float support_force_deficit = deficit;


    if (flags & icfIsNew) {
        auto chull = Geometry::convex_hull(islands);
        auto rotbox = MinAreaBoundigBox{chull, MinAreaBoundigBox::pcConvex};
        Vec2d bbdim = {unscaled(rotbox.width()), unscaled(rotbox.height())};

        if (bbdim.x() > bbdim.y()) std::swap(bbdim.x(), bbdim.y());
        double aspectr = bbdim.y() / bbdim.x();

        support_force_deficit *= (1 + aspectr / 2.);
    }

    if (support_force_deficit < 0)
        return;


    const size_t poisson_samples_target = size_t(ceil(support_force_deficit / m_config.support_force()));

    const float density_horizontal = m_config.tear_pressure() / m_config.support_force();

    float poisson_radius		= std::max(m_config.minimal_distance, 1.f / (5.f * density_horizontal));

    const float samples_per_mm2 = 30.f / (float(M_PI) * poisson_radius * poisson_radius);


    float min_spacing			= poisson_radius;



    std::vector<Vec2f> raw_samples =
        flags & icfWithBoundary ?
            sample_expolygon_with_boundary(islands, samples_per_mm2,
                                           5.f / poisson_radius, m_rng) :
            sample_expolygon(islands, samples_per_mm2, m_rng);

    std::vector<Vec2f>  poisson_samples;
    for (size_t iter = 0; iter < 4; ++ iter) {
        poisson_samples = poisson_disk_from_samples(raw_samples, poisson_radius,
            [&structure, &grid3d, min_spacing](const Vec2f &pos) {
                return grid3d.collides_with(pos, structure.layer->print_z, min_spacing);
            });
        if (poisson_samples.size() >= poisson_samples_target || m_config.minimal_distance > poisson_radius-EPSILON)
            break;
        float coeff = 0.5f;
        if (poisson_samples.size() * 2 > poisson_samples_target)
            coeff = float(poisson_samples.size()) / float(poisson_samples_target);
        poisson_radius = std::max(m_config.minimal_distance, poisson_radius * coeff);
        min_spacing    = std::max(m_config.minimal_distance, min_spacing * coeff);
    }

#ifdef SLA_SUPPORTPOINTGEN_DEBUG
    {
        static int irun = 0;
        Slic3r::SVG svg(debug_out_path("SLA_supports-uniformly_cover-%d.svg", irun ++), get_extents(islands));
        for (const ExPolygon &island : islands)
            svg.draw(island);
        for (const Vec2f &pt : raw_samples)
            svg.draw(Point(scale_(pt.x()), scale_(pt.y())), "red");
        for (const Vec2f &pt : poisson_samples)
            svg.draw(Point(scale_(pt.x()), scale_(pt.y())), "blue");
    }
#endif


    if (poisson_samples_target < poisson_samples.size()) {
        std::shuffle(poisson_samples.begin(), poisson_samples.end(), m_rng);
        poisson_samples.erase(poisson_samples.begin() + poisson_samples_target, poisson_samples.end());
    }
    for (const Vec2f &pt : poisson_samples) {
        m_output.emplace_back(float(pt(0)), float(pt(1)), structure.zlevel, m_config.head_diameter/2.f, flags & icfIsNew);
        structure.supports_force_this_layer += m_config.support_force();
        grid3d.insert(pt, &structure);
    }
}


void remove_bottom_points(std::vector<SupportPoint> &pts, float lvl)
{

    auto endit = std::remove_if(pts.begin(), pts.end(), [lvl]
                                (const sla::SupportPoint &sp) {
        return sp.pos.z() <= lvl;
    });


    pts.erase(endit, pts.end());
}

#ifdef SLA_SUPPORTPOINTGEN_DEBUG
void SupportPointGenerator::output_structures(const std::vector<Structure>& structures)
{
    for (unsigned int i=0 ; i<structures.size(); ++i) {
        std::stringstream ss;
        ss << structures[i].unique_id.count() << "_" << std::setw(10) << std::setfill('0') << 1000 + (int)structures[i].height/1000 << ".png";
        output_expolygons(std::vector<ExPolygon>{*structures[i].polygon}, ss.str());
    }
}

void SupportPointGenerator::output_expolygons(const ExPolygons& expolys, const std::string &filename)
{
    BoundingBox bb(Point(-30000000, -30000000), Point(30000000, 30000000));
    Slic3r::SVG svg_cummulative(filename, bb);
    for (size_t i = 0; i < expolys.size(); ++ i) {






        svg_cummulative.draw(expolys[i]);
        svg_cummulative.draw_outline(expolys[i].contour, "black", scale_(0.05));
        svg_cummulative.draw_outline(expolys[i].holes, "blue", scale_(0.05));
    }
}
#endif

}
}
