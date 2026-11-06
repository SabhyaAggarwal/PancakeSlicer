#ifndef slic3r_SketchImport_hpp_
#define slic3r_SketchImport_hpp_

#include "libslic3r/Point.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Slic3r {

namespace Emboss { struct FontFile; }




using ImportRegion  = std::vector<std::vector<Vec2d>>;
using ImportRegions = std::vector<ImportRegion>;




ImportRegions text_to_regions(const std::string& utf8, double size_mm,
                              const std::string& font_path = std::string());



ImportRegions text_to_regions(const std::string& utf8, double size_mm,
                              const std::shared_ptr<const Emboss::FontFile>& font);




ImportRegions svg_to_regions(const std::string& svg_path, double scale = 1.0);





ImportRegions transform_regions(const ImportRegions& src, const Vec2d& offset,
                                double scale_x, double scale_y);

}

#endif
