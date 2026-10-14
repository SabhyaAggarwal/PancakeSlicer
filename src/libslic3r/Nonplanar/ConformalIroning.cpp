#include "ConformalIroning.hpp"

#include "AutoMode.hpp"
#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "MaterialRules.hpp"
#include "PatchTools.hpp"
#include "Repair.hpp"
#include "Seams.hpp"
#include "ShellPaths.hpp"
#include "SpiralTop.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

double ironing_cross(const HeightField &surface, const ExPolygons &area)
{
    const Mask inside = rasterize(area, surface);
    double     sum = 0.;
    size_t     n   = 0;
    for (int iy = 0; iy < surface.ny(); ++iy)
        for (int ix = 0; ix < surface.nx(); ++ix)
            if (inside[size_t(iy) * surface.nx() + ix])
                if (const Vec2d g = surface.gradient(surface.node(ix, iy)); !std::isnan(g.x()) && !std::isnan(g.y())) {
                    sum += g.norm();
                    ++n;
                }
    const double slope = n > 0 ? sum / double(n) : 0.;
    for (const double deg : { 90., 60. })
        if (slope * (std::sin(Geometry::deg2rad(deg)) - std::sin(0.25 * PI)) <= std::tan(Geometry::deg2rad(5.)))
            return Geometry::deg2rad(deg);
    return 0.25 * PI;
}

}

bool conformal_ironing_enabled(const PrintRegionConfig &config)
{
    return config.nonplanar_ironing.value || config.ironing_type.value != IroningType::NoIroning;
}

ExtrusionEntityCollection make_conformal_ironing(const PrintObject &object, const ConformalPatch &patch, const ExPolygons &shell_area,
                                                 const Point &seam, const Polyline &seam_line, double host_z, bool banded,
                                                 const std::function<bool(const HeightField &, const Vec2d &)> &wall)
{
    ExtrusionEntityCollection out;
    const PrintConfig       &print_config = object.print()->config();
    const PrintRegion       &region       = object.printing_region(size_t(patch.region_id));
    const PrintRegionConfig &cfg          = region.config();
    const int    filament = patch_filament_id(object, patch);
    const size_t idx      = size_t(std::max(0, filament - 1));
    if (road_material(print_config.filament_type.get_at(idx)) == RoadMaterial::Flexible)
        return out;
    const double nozzle   = nozzle_diameter_for_filament(print_config, filament, object.print()->is_BBL_printer());
    const double spacing  = std::max(IRONING_SPACING_MIN, !cfg.filament_ironing_spacing.is_nil(idx) ? cfg.filament_ironing_spacing.get_at(idx) :
                                                                                                    cfg.ironing_spacing.value);
    const double inset    = !cfg.filament_ironing_inset.is_nil(idx) ? cfg.filament_ironing_inset.get_at(idx) : cfg.ironing_inset.value;
    const double flow_pct = !cfg.filament_ironing_flow.is_nil(idx) ? cfg.filament_ironing_flow.get_at(idx) : cfg.ironing_flow.value;
    const double height   = patch.shell_height * 0.01 * flow_pct * spacing / nozzle;
    const float  width    = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle), float(height));
    const Flow   flow(width, float(height), float(nozzle));
    const double     edge = std::max(0., (inset == 0. ? 0.5 * nozzle : inset) - 0.5 * spacing);
    const ExPolygons area = intersection_ex(offset_ex(shell_area, float(scale_(0.5 * spacing))), offset_ex(patch.footprint, -float(scale_(edge))));
    if (area.empty())
        return out;

    std::vector<Polylines> linked;
    if (cfg.ironing_pattern.value == ipConcentric) {
        ShellFill spiral;
        spiral.spiral = true;
        spiral.helix  = spiral_top_enabled(cfg, object.config()) && helix_worthwhile(patch.surface, area);
        linked        = shell_polylines(area, spiral, ipRectilinear, spacing, false, flow, host_z, seam, MaxGlide, nullptr, &seam_line);
    } else {
        double angle     = 0.;
        bool   piecewise = false;
        if (cfg.ironing_angle_fixed.value)
            angle = Geometry::deg2rad(cfg.ironing_angle.value);
        else {
            const double cross = cfg.ironing_angle.value != 0. ? Geometry::deg2rad(cfg.ironing_angle.value) : ironing_cross(patch.surface, area);
            if (banded && top_pattern(patch, cfg) == NonplanarTopPattern::Contour) {
                double contour = 0.;
                piecewise      = !contour_line_angle(patch.surface, contour, 0.9);
                angle          = contour + cross;
            } else {
                const ShellFill top = plan_shell_fills(patch, cfg, region.flow(object, frTopSolidInfill, patch.shell_height).width()).back();
                piecewise           = top.spiral;
                angle               = top.angle + cross;
            }
            if (piecewise)
                linked = link_paths(chain_nearest(contour_fill(patch.surface, area, 0., spacing, flow, false, host_z, cross,
                                                               std::numeric_limits<double>::infinity()),
                                                  seam),
                                    area, spacing, MaxGlide);
        }
        if (!piecewise) {
            ShellFill lines;
            lines.angle = angle;
            linked      = shell_polylines(area, lines, ipRectilinear, spacing, false, flow, host_z, seam, MaxGlide);
        }
    }

    const double      max_rise = patch.shell_height / std::max(0.05, resolve_head_envelope(print_config).tip_flat_radius);
    const HeightField &S       = patch.surface;
    Mask               allowed(S.data().size(), 0);
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix) {
            const Vec2d p = S.node(ix, iy);
            const Vec2d g = S.gradient(p);
            allowed[size_t(iy) * S.nx() + ix] = !std::isnan(S.at(ix, iy)) && !(g.norm() > max_rise);
        }
    const double reach = 0.5 * region.flow(object, frTopSolidInfill, patch.shell_height).width();
    const int    grow  = std::max(1, int(std::ceil(reach / S.pitch())));
    Mask         ironed(allowed.size(), 0);
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix)
            for (int dy = -grow; dy <= grow && !ironed[size_t(iy) * S.nx() + ix]; ++dy)
                for (int dx = -grow; dx <= grow; ++dx) {
                    const int jx = ix + dx, jy = iy + dy;
                    if (jx >= 0 && jy >= 0 && jx < S.nx() && jy < S.ny() && allowed[size_t(jy) * S.nx() + jx]) {
                        ironed[size_t(iy) * S.nx() + ix] = 1;
                        break;
                    }
                }
    const RepairHints *hints = object.nonplanar_repair();
    const ExtrusionPath proto(erIroning, nozzle * height, width, float(height));
    auto at = [&S, &ironed, &wall, hints, height](const Vec2d &p, double &z, float &bead) {
        const float s = S.sample(p);
        const Vec2d o = (p - S.origin()) / S.pitch();
        const int   ix = int(std::lround(o.x())), iy = int(std::lround(o.y()));
        if (std::isnan(s) || (wall && wall(S, p)) || ix < 0 || iy < 0 || ix >= S.nx() || iy >= S.ny() || !ironed[size_t(iy) * S.nx() + ix])
            return false;
        if (hints != nullptr)
            for (const RepairHints::Disc &d : hints->ironing_drops)
                if ((p - d.p).squaredNorm() < d.radius * d.radius)
                    return false;
        z    = s;
        bead = float(height);
        return true;
    };
    lift_linked(linked, proto, at, host_z, patch.surface.pitch(), patch.id, patch.shells, out.entities);
    out.no_sort = true;
    return out;
}

void build_conformal_ironing(const PrintObject &object, ConformalResult &result)
{
    for (ConformalPatch &patch : result.patches) {
        if (patch.kind != PatchKind::Top || patch.band >= 0 || patch.shell_paths.empty() || patch.shell_paths.back().entities.empty())
            continue;
        const PrintRegionConfig &cfg = object.printing_region(size_t(patch.region_id)).config();
        if (!ironing_enabled(patch, cfg))
            continue;
        const ExPolygons area   = diff_ex(patch.footprint, patch.wall_band);
        const double     host_z = object.layers()[patch.host_layer]->print_z;
        const Point      seam   = shell_seam(area, patch.surface, object.config().seam_position.value, unsigned(patch.id));
        patch.ironing           = make_conformal_ironing(object, patch, area, seam, seam_line(patch.surface, seam, area), host_z, false);
        if (patch.ironing.entities.empty())
            continue;
        const Point3 &from = static_cast<const ExtrusionPathConformal &>(*patch.shell_paths.back().entities.back()).polyline.points.back();
        auto at = [&patch](const Vec2d &p, double &z, float &bead) {
            const float s = patch.surface.sample(p);
            z             = s;
            bead          = 0.f;
            return !std::isnan(s);
        };
        prepend_glide(*static_cast<ExtrusionPathConformal *>(patch.ironing.entities.front()), from, area, at, host_z,
                      patch.surface.pitch(), MaxGlide);
    }
}

}
