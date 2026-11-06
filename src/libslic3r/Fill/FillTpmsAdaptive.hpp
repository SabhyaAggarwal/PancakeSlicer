#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "../libslic3r.h"
#include "FillBase.hpp"
#include "../BoundingBox.hpp"
#include "../ExPolygon.hpp"
#include "../Point.hpp"
#include "../Polyline.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {





class TpmsRadialField
{
public:
    struct Slice
    {
        coordf_t          bottom_z;
        coordf_t          top_z;
        const ExPolygons *expolygons;
    };

    struct Radial
    {
        Vec3d  center;
        double t;
        float  weight;
    };


    TpmsRadialField(const std::vector<Slice> &slices, const BoundingBox &bbox, TpmsAdaptiveMode mode,
                    const std::function<void()> &throw_if_canceled);


    static constexpr size_t MaxMorph = 4;
    using Radials                    = std::array<Radial, 2 * MaxMorph>;


    bool empty() const { return m_bodies.empty(); }




    size_t radial(const Vec3d &pt, Radials &out) const;


    int              axis() const { return m_axis; }
    TpmsAdaptiveMode mode() const { return m_mode; }



    double depth(const Vec3d &pt) const;

private:
    struct Lobe
    {
        Vec3d  center;
        double depth;

        std::vector<float> reach;

        std::vector<float> mean_depth;
    };

    struct Body
    {
        size_t first_lobe;
        size_t lobes;

        double depth;
    };

    double radial(const Lobe &lobe, const Vec3d &pt) const;
    size_t body_radial(size_t node, const Vec3d &pt, float weight, Radial *out) const;

    Vec3d  offset(const Vec3d &pt, const Vec3d &center) const;
    int    directions() const { return m_axis < 0 ? Polar * Azimuth : Azimuth; }


    static constexpr int Polar   = 24;
    static constexpr int Azimuth = 48;

    TpmsAdaptiveMode  m_mode;
    int               m_axis;
    Vec3d             m_origin;
    double            m_cell;
    Vec3i32           m_size;

    std::vector<int>  m_body;
    std::vector<Body> m_bodies;
    std::vector<Lobe> m_lobes;

    std::vector<int>  m_section;

    std::vector<float> m_depth;
};

using TpmsRadialFieldPtr = std::unique_ptr<TpmsRadialField>;

using TpmsRadialFields = std::array<TpmsRadialFieldPtr, size_t(TpmsAdaptiveMode::Count)>;

struct AdaptiveTpms
{

    float (*equation)(float x, float y, float z);

    double               surface_frequency;
    double               interior_frequency;
    TpmsAdaptiveGradient gradient;
};


Polylines make_adaptive_tpms(const AdaptiveTpms &tpms, const TpmsRadialField &field, BoundingBox bbox,
                             coordf_t z, coordf_t layer_height, coordf_t spacing, float angle);

struct TpmsShell
{
    float      density;
    ExPolygons expolygons;
};



std::vector<TpmsShell> make_tpms_shells(const TpmsRadialField &field, const ExPolygon &expolygon, coordf_t z,
                                        float surface_density, float interior_density, TpmsAdaptiveGradient gradient);



void fill_tpms_shells(const TpmsRadialField &field, const ExPolygon &expolygon, coordf_t z, const FillParams &params, coordf_t spacing,
                      const std::function<void(const FillParams &, const ExPolygon &)> &fill_shell);

}
