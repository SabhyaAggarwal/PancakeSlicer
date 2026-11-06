








#ifndef slic3r_WaveOverhangs_hpp_
#define slic3r_WaveOverhangs_hpp_

#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { class PrintObject; }

namespace Slic3r::WaveOverhangs {




bool enabled(const PrintRegionConfig &region, const PrintObject &object);


enum class SpacingMode { Uniform, Progressive };


enum class SeamMode { Alternating, Aligned, Random };

struct CommonParams {
    int         perimeter_count        = 1;
    double      line_spacing           = 0.35;
    double      line_width             = 0.4;
    Flow        overhang_flow;
    double      scaled_resolution      = 1.0;
    SpacingMode spacing_mode           = SpacingMode::Uniform;
    SeamMode    seam_mode              = SeamMode::Alternating;
    double      min_length_mm          = 0.0;
    int         max_iterations         = 0;

    double      perimeter_overlap      = 0.1;
    double      minimum_wave_width     = 0.7;
    WaveOverhangPattern pattern        = WaveOverhangPattern::Smart;
    double      min_new_area           = 0.01;
    bool        use_instead_of_bridges = false;







    bool        corner_taper_enable    = false;
    double      line_spacing_corner    = 0.0;
    double      corner_taper_distance  = 0.0;
    double      corner_angle_threshold = 90.0;
};

struct GenerateResult {
    std::vector<ExtrusionPaths> paths;
    Polygons                    residual;
};

GenerateResult generate(const ExPolygons   &overhang_area,
                        const Polygons     &lower_slices_polygons,
                        const CommonParams &params);


namespace detail {



Polylines reconnect_polylines(const Polylines &polylines, double limit_distance);


Points detect_overhang_corners(const Polygons &polys, double angle_threshold_rad);




Polygons painted_overhang_zone(const Polygons &island, const Polygons &lower, const Polygons &filled_area, coord_t sliver_radius);

}

}

#endif
