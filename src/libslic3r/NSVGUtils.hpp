#ifndef slic3r_NSVGUtils_hpp_
#define slic3r_NSVGUtils_hpp_

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <sstream>
#include "Point.hpp"
#include "Polygon.hpp"
#include "EmbossShape.hpp"
#include "libslic3r.h"
#include "nanosvg/nanosvg.h"


namespace Slic3r {




struct NSVGLineParams
{


    double tesselation_tolerance = 10.f;


    int max_level = 10;



    double scale = 1. / SCALING_FACTOR;


    bool is_y_negative = true;


    double arc_tolerance = 1.;


    unsigned max_heal_iteration = 10;

    explicit NSVGLineParams(double tesselation_tolerance):
        tesselation_tolerance(tesselation_tolerance),
        arc_tolerance(std::pow(tesselation_tolerance, 1/3.))
    {}
};











ExPolygonsWithIds create_shape_with_ids(const NSVGimage &image, const NSVGLineParams &param);



Polygons to_polygons(const NSVGimage &image, const NSVGLineParams &param);

void bounds(const NSVGimage &image, Vec2f &min, Vec2f &max);


std::unique_ptr<std::string> read_from_disk(const std::string &path);

using NSVGimage_ptr = std::unique_ptr<NSVGimage, void (*)(NSVGimage*)>;
NSVGimage_ptr nsvgParseFromFile(const std::string &svg_file_path, const char *units = "mm", float dpi = 96.0f);
NSVGimage_ptr nsvgParse(const std::string& file_data, const char *units = "mm", float dpi = 96.0f);
NSVGimage *init_image(EmbossShape::SvgFile &svg_file);






size_t get_shapes_count(const NSVGimage &image);



}
#endif
