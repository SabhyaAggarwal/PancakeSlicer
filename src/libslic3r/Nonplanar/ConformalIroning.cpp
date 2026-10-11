#include "ConformalIroning.hpp"

#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "PatchTools.hpp"
#include "ShellPaths.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>

namespace Slic3r::Nonplanar {

void build_conformal_ironing(const PrintObject &object, ConformalResult &result)
{
    const PrintConfig &print_config = object.print()->config();
    for (ConformalPatch &patch : result.patches) {
        if (patch.kind != PatchKind::Top || patch.shell_paths.empty() || patch.shell_paths.back().entities.empty())
            continue;
        const PrintRegionConfig &cfg = object.printing_region(size_t(patch.region_id)).config();
        if (!cfg.nonplanar_ironing.value)
            continue;
        const int    filament  = patch_filament_id(object, patch);
        const size_t idx       = size_t(std::max(0, filament - 1));
        const double nozzle    = nozzle_diameter_for_filament(print_config, filament, object.print()->is_BBL_printer());
        const double spacing   = std::max(IRONING_SPACING_MIN, !cfg.filament_ironing_spacing.is_nil(idx) ?
                                                                   cfg.filament_ironing_spacing.get_at(idx) : cfg.ironing_spacing.value);
        const double inset     = !cfg.filament_ironing_inset.is_nil(idx) ? cfg.filament_ironing_inset.get_at(idx) : cfg.ironing_inset.value;
        const double flow_pct  = !cfg.filament_ironing_flow.is_nil(idx) ? cfg.filament_ironing_flow.get_at(idx) : cfg.ironing_flow.value;
        const double width_top   = object.printing_region(size_t(patch.region_id)).flow(object, frTopSolidInfill, patch.shell_height).width();
        ShellFill    fill        = plan_shell_fills(patch, cfg, width_top).back();
        fill.outward             = !fill.outward;
        fill.angle               = (cfg.ironing_angle_fixed ? 0. : fill.angle) + Geometry::deg2rad(cfg.ironing_angle.value);
        const double height    = patch.shell_height * 0.01 * flow_pct * spacing / nozzle;
        const float  width     = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle), float(height));
        const ExPolygons area  = offset_ex(diff_ex(patch.footprint, patch.wall_band), -float(scale_(inset == 0. ? 0.5 * nozzle : inset)));
        const double host_z    = object.layers()[patch.host_layer]->print_z;
        const auto     &top    = static_cast<const ExtrusionPathConformal &>(*patch.shell_paths.back().entities.back());
        const Point3   &from   = top.polyline.points.back();
        const Point     seam   = shell_seam(area, patch.surface, object.config().seam_position.value, unsigned(patch.id));
        const std::vector<Polylines> lines = shell_polylines(area, fill, cfg.ironing_pattern.value, spacing, true,
                                                             Flow(width, float(height), float(nozzle)), host_z,
                                                             fill.spiral ? seam : from.to_point(), MaxGlide);

        const ExtrusionPath proto(erIroning, nozzle * height, width, float(height));
        auto at = [&patch, height](const Vec2d &p, double &z, float &bead) {
            const float s = patch.surface.sample(p);
            if (std::isnan(s))
                return false;
            z    = s;
            bead = float(height);
            return true;
        };
        patch.ironing.clear();
        lift_linked(lines, proto, at, host_z, patch.surface.pitch(), patch.id, patch.shells, patch.ironing.entities);
        if (!patch.ironing.entities.empty())
            prepend_glide(*static_cast<ExtrusionPathConformal *>(patch.ironing.entities.front()), from,
                          diff_ex(patch.footprint, patch.wall_band), at, host_z, patch.surface.pitch(), MaxGlide);
        patch.ironing.no_sort = true;
    }
}

}
