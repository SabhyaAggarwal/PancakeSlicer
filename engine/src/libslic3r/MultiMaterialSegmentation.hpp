#ifndef slic3r_MultiMaterialSegmentation_hpp_
#define slic3r_MultiMaterialSegmentation_hpp_

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <boost/polygon/polygon.hpp>

#include "Line.hpp"
#include "Point.hpp"
#include "libslic3r.h"

namespace Slic3r {

class ExPolygon;
class ModelVolume;
class PrintObject;
class PrintConfig;
class PrintObjectConfig;
class PrintRegionConfig;
class FacetsAnnotation;

using ExPolygons = std::vector<ExPolygon>;

struct ColoredLine
{
    Line line;
    int  color;
    int  poly_idx       = -1;
    int  local_line_idx = -1;
};

using ColoredLines = std::vector<ColoredLine>;

enum class IncludeTopAndBottomLayers {
    Yes,
    No
};

struct ModelVolumeFacetsInfo {
    const FacetsAnnotation &facets_annotation;
    const bool              is_painted;
    const bool              replace_default_extruder;
};

std::vector<std::vector<ExPolygons>> segmentation_by_painting(const PrintObject                                               &print_object,
                                                              const std::function<ModelVolumeFacetsInfo(const ModelVolume &)> &extract_facets_info,
                                                              size_t                                                           num_facets_states,
                                                              float                                                            segmentation_max_width,
                                                              float                                                            segmentation_interlocking_depth,
                                                              bool                                                             segmentation_interlocking_beam,
                                                              IncludeTopAndBottomLayers                                        include_top_and_bottom_layers,
                                                              const std::function<void()>                                     &throw_on_cancel_callback);

std::vector<std::vector<ExPolygons>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);

std::vector<std::vector<ExPolygons>> fuzzy_skin_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);

double resolve_outer_wall_line_width(const PrintRegionConfig &region_config, const PrintObjectConfig &object_config, const PrintConfig &print_config);

}

namespace boost::polygon {
template<> struct geometry_concept<Slic3r::ColoredLine>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::ColoredLine>
{
    typedef coord_t       coordinate_type;
    typedef Slic3r::Point point_type;

    static inline point_type get(const Slic3r::ColoredLine &line, const direction_1d &dir)
    {
        return dir.to_int() ? line.line.b : line.line.a;
    }
};
}

#endif
