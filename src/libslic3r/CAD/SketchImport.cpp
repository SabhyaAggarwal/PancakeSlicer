#include "libslic3r/CAD/SketchImport.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/EmbossShape.hpp"
#include "libslic3r/Emboss.hpp"
#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/TextConfiguration.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"

#include <algorithm>
#include <limits>
#include <vector>
#include <string>
#include <memory>
#include <utility>
#include <nanosvg/nanosvg.h>

namespace Slic3r {

template<class ToMm>
static ImportRegion expoly_to_region(const ExPolygon& ex, ToMm to_mm)
{
    auto contour_pts = [&](const Polygon& poly) {
        std::vector<Vec2d> c;
        c.reserve(poly.points.size());
        for (const Point& p : poly.points)
            c.push_back(to_mm(p));
        return c;
    };
    ImportRegion region;
    region.push_back(contour_pts(ex.contour));
    for (const Polygon& h : ex.holes)
        region.push_back(contour_pts(h));
    return region;
}

static void center_regions(ImportRegions& regs)
{
    double lo_x =  std::numeric_limits<double>::max();
    double lo_y =  std::numeric_limits<double>::max();
    double hi_x = -std::numeric_limits<double>::max();
    double hi_y = -std::numeric_limits<double>::max();
    bool any = false;
    for (const auto& region : regs)
        for (const auto& contour : region)
            for (const Vec2d& p : contour) {
                lo_x = std::min(lo_x, p.x()); hi_x = std::max(hi_x, p.x());
                lo_y = std::min(lo_y, p.y()); hi_y = std::max(hi_y, p.y());
                any = true;
            }
    if (!any) return;
    const Vec2d c(0.5 * (lo_x + hi_x), 0.5 * (lo_y + hi_y));
    for (auto& region : regs)
        for (auto& contour : region)
            for (Vec2d& p : contour)
                p -= c;
}

static std::string default_font_path()
{
    return resources_dir() + "/fonts/HarmonyOS_Sans_SC_Regular.ttf";
}

ImportRegions text_to_regions(const std::string& utf8, double size_mm,
                              const std::string& font_path)
{
    if (utf8.empty() || size_mm <= 0.0)
        return {};

    const std::string path = font_path.empty() ? default_font_path() : font_path;
    return text_to_regions(utf8, size_mm,
                           std::shared_ptr<const Emboss::FontFile>(Emboss::create_font_file(path.c_str())));
}

ImportRegions text_to_regions(const std::string& utf8, double size_mm,
                              const std::shared_ptr<const Emboss::FontFile>& ff)
{
    if (utf8.empty() || size_mm <= 0.0 || !ff)
        return {};
    Emboss::FontFileWithCache fwc;
    fwc.font_file = ff;
    fwc.cache     = std::make_shared<Emboss::Glyphs>();

    FontProp prop(static_cast<float>(size_mm));
    HealedExPolygons healed = Emboss::text2shapes(fwc, utf8.c_str(), prop);
    if (healed.expolygons.empty())
        return {};

    const double s = Emboss::get_text_shape_scale(prop, *fwc.font_file);
    auto to_mm = [s](const Point& p) { return Vec2d(p.x() * s, p.y() * s); };

    ImportRegions regs;
    regs.reserve(healed.expolygons.size());
    for (const ExPolygon& ex : healed.expolygons)
        regs.push_back(expoly_to_region(ex, to_mm));

    center_regions(regs);
    return regs;
}

ImportRegions svg_to_regions(const std::string& svg_path, double scale)
{
    if (svg_path.empty() || scale <= 0.0)
        return {};

    NSVGimage_ptr image = nsvgParseFromFile(svg_path, "mm", 96.0f);
    if (!image)
        return {};

    for (NSVGshape* s = image->shapes; s != nullptr; s = s->next)
        if (s->fill.type != NSVG_PAINT_NONE)
            s->stroke.type = NSVG_PAINT_NONE;

    NSVGLineParams param(0.3);
    ExPolygonsWithIds ids = create_shape_with_ids(*image, param);

    const double s = SCALING_FACTOR * scale;
    auto to_mm = [s](const Point& p) { return Vec2d(p.x() * s, p.y() * s); };

    ImportRegions regs;
    for (const ExPolygonsWithId& w : ids)
        for (const ExPolygon& ex : w.expoly)
            regs.push_back(expoly_to_region(ex, to_mm));

    center_regions(regs);
    return regs;
}

ImportRegions transform_regions(const ImportRegions& src, const Vec2d& offset,
                                double scale_x, double scale_y)
{
    ImportRegions out = src;
    for (auto& region : out)
        for (auto& contour : region)
            for (Vec2d& p : contour)
                p = Vec2d(p.x() * scale_x + offset.x(), p.y() * scale_y + offset.y());
    return out;
}

}
