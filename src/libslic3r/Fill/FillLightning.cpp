#include "../ClipperUtils.hpp"
#include "../Print.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Fill/Lightning/Layer.hpp"
#include "FillBase.hpp"
#include "FillCornerSmoothing.hpp"
#include "FillLightning.hpp"
#include "Lightning/Generator.hpp"
#include <utility>
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include <algorithm>
#include <functional>

namespace Slic3r::FillLightning {

void Filler::_fill_surface_single(
    const FillParams              &params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point> &direction,
    ExPolygon                      expolygon,
    Polylines                     &polylines_out)
{
    const Layer &layer      = generator->getTreesForLayer(this->layer_id);
    Polylines    fill_lines = layer.convertToLines(to_polygons(expolygon), scaled<coord_t>(0.5 * this->spacing - this->overlap));









    const double branch_width   = scaled<double>(this->spacing) * params.multiline;
    const double branch_spacing = branch_width / std::max(double(params.density), EPSILON);
    const double max_reach      = 0.5 * (params.multiline > 1 ? branch_width : branch_spacing);
    smooth_polylines_corners(fill_lines, params.smooth_factor, scaled<double>(params.resolution), max_reach);


    multiline_fill(fill_lines, params, spacing);

    fill_lines = Slic3r::intersection_pl(std::move(fill_lines), expolygon);

    chain_or_connect_infill(std::move(fill_lines), expolygon, polylines_out, this->spacing, params);
}

void GeneratorDeleter::operator()(Generator *p) {
    delete p;
}

GeneratorPtr build_generator(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback)
{
    return GeneratorPtr(new Generator(print_object, throw_on_cancel_callback));
}

}
