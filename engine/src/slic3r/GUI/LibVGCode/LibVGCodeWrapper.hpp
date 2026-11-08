///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#ifndef slic3r_LibVGCodeWrapper_hpp_
#define slic3r_LibVGCodeWrapper_hpp_

#include <../../src/libvgcode/include/Viewer.hpp>
#include <../../src/libvgcode/include/PathVertex.hpp>
#include <../../src/libvgcode/include/GCodeInputData.hpp>
#include <../../src/libvgcode/include/ColorRange.hpp>
#include <stddef.h>
#include <string>
#include <vector>
#include <cstddef>

#include "../../src/libvgcode/include/Types.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"


namespace Slic3r {
class Print;

namespace CustomGCode {
struct Item;
}
}

namespace libvgcode {
class Viewer;

extern Vec3 convert(const Slic3r::Vec3f& v);

extern Slic3r::Vec3f convert(const Vec3& v);

extern Mat4x4 convert(const Slic3r::Matrix4f& m);

extern Slic3r::ColorRGBA convert(const Color& c);

extern Color convert(const Slic3r::ColorRGBA& c);

extern Color convert(const std::string& color_str);

extern Slic3r::ExtrusionRole convert(EGCodeExtrusionRole role);

extern EGCodeExtrusionRole convert(Slic3r::ExtrusionRole role);

extern EMoveType convert(Slic3r::EMoveType type);

extern ETimeMode convert(const Slic3r::PrintEstimatedStatistics::ETimeMode& mode);

extern Slic3r::PrintEstimatedStatistics::ETimeMode convert(const ETimeMode& mode);

extern GCodeInputData convert(const Slic3r::GCodeProcessorResult& result, const std::vector<std::string>& str_tool_colors,
    const std::vector<std::string>& str_color_print_colors, const Viewer& viewer,
    const Slic3r::Transform3d* belt_xform = nullptr);

extern GCodeInputData convert(const Slic3r::Print& print, const std::vector<std::string>& str_tool_colors,
    const std::vector<std::string>& str_color_print_colors, const std::vector<Slic3r::CustomGCode::Item>& color_print_values,
    size_t extruders_count);

}

#endif
