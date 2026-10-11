#include "ConformalIroning.hpp"

#include "ConformalBottom.hpp"
#include "ConformalShells.hpp"
#include "PatchTools.hpp"

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
        if (patch.kind != PatchKind::Top || patch.shell_paths.empty())
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
        const double angle     = (cfg.ironing_angle_fixed ? 0. : top_shell_angle(patch, cfg)) + Geometry::deg2rad(cfg.ironing_angle.value);
        const double height    = patch.shell_height * 0.01 * flow_pct * spacing / nozzle;
        const float  width     = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle), float(height));
        const ExPolygons area  = offset_ex(diff_ex(patch.footprint, patch.wall_band), -float(scale_(inset == 0. ? 0.5 * nozzle : inset)));
        const double host_z    = object.layers()[patch.host_layer]->print_z;
        const Polylines lines  = fill_solid(area, cfg.ironing_pattern.value, spacing, angle, true,
                                            Flow(width, float(height), float(nozzle)), host_z);

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
        lift_polylines(lines, proto, at, host_z, patch.surface.pitch(), patch.id, patch.shells, patch.ironing.entities);
        patch.ironing.no_sort = true;
    }
}

}
