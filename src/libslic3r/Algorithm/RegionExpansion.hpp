#ifndef SRC_LIBSLIC3R_ALGORITHM_REGION_EXPANSION_HPP_
#define SRC_LIBSLIC3R_ALGORITHM_REGION_EXPANSION_HPP_

#include <cstddef>
#include <cstdint>
#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <vector>

namespace Slic3r {
namespace Algorithm {

struct RegionExpansionParameters
{

    float                  tiny_expansion;

    float                  initial_step;

    float                  other_step;

    size_t                 num_other_steps;


    float                  max_inflation;


    double                 arc_tolerance;

    static RegionExpansionParameters build(

        float                full_expansion,

        float                expansion_step,

        size_t               max_nr_expansion_steps);
};

struct WaveSeed {
    uint32_t src;
    uint32_t boundary;
    Points   path;
};
using WaveSeeds = std::vector<WaveSeed>;

inline bool lower_by_boundary_and_src(const WaveSeed &l, const WaveSeed &r)
{
    return l.boundary < r.boundary || (l.boundary == r.boundary && l.src < r.src);
}

inline bool lower_by_src_and_boundary(const WaveSeed &l, const WaveSeed &r)
{
    return l.src < r.src || (l.src == r.src && l.boundary < r.boundary);
}



WaveSeeds wave_seeds(

    const ExPolygons      &src,

    const ExPolygons      &boundary,

    float                  tiny_expansion,
    bool                   sorted);

struct RegionExpansion
{
    Polygon     polygon;
    uint32_t    src_id;
    uint32_t    boundary_id;
};

std::vector<RegionExpansion> propagate_waves(const WaveSeeds &seeds, const ExPolygons &boundary, const RegionExpansionParameters &params);
std::vector<RegionExpansion> propagate_waves(const ExPolygons &src, const ExPolygons &boundary, const RegionExpansionParameters &params);

std::vector<RegionExpansion> propagate_waves(const ExPolygons &src, const ExPolygons &boundary,

    float expansion,

    float expansion_step,

    size_t max_nr_steps);

struct RegionExpansionEx
{
    ExPolygon   expolygon;
    uint32_t    src_id;
    uint32_t    boundary_id;
};

std::vector<RegionExpansionEx> propagate_waves_ex(const WaveSeeds &seeds, const ExPolygons &boundary, const RegionExpansionParameters &params);

std::vector<RegionExpansionEx> propagate_waves_ex(const ExPolygons &src, const ExPolygons &boundary,

    float expansion,

    float expansion_step,

    size_t max_nr_steps);

std::vector<Polygons> expand_expolygons(const ExPolygons &src, const ExPolygons &boundary,

    float expansion,

    float expansion_step,

    size_t max_nr_steps);


std::vector<ExPolygon> merge_expansions_into_expolygons(ExPolygons &&src, std::vector<RegionExpansion> &&expanded);

std::vector<ExPolygon> expand_merge_expolygons(ExPolygons &&src, const ExPolygons &boundary, const RegionExpansionParameters &params);

}
}

#endif
