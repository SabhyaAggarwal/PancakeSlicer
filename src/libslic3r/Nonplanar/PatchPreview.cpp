#include "PatchPreview.hpp"
#include "libslic3r/Nonplanar/HeightField.hpp"

#include "AutoMode.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "PatchTools.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <tbb/global_control.h>
#include <tbb/task_arena.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

bool PatchPreviewParams::operator==(const PatchPreviewParams &rhs) const
{
    return ((!config && !rhs.config) || (config && rhs.config && *config == *rhs.config)) &&
           banded == rhs.banded && top_shells == rhs.top_shells && shells == rhs.shells && shell_height == rhs.shell_height && max_slope == rhs.max_slope &&
           max_error == rhs.max_error && min_area == rhs.min_area && pitch == rhs.pitch &&
           first_layer_height == rhs.first_layer_height && line_width == rhs.line_width && wall_width == rhs.wall_width &&
           max_tilt == rhs.max_tilt;
}

PatchPreviewParams patch_preview_params(const DynamicPrintConfig &config)
{
    PatchPreviewParams p;
    p.config = std::make_shared<const DynamicPrintConfig>(config);
    auto num = [&config](const char *key, double fallback) {
        const ConfigOption *opt = config.option(key);
        if (const auto *integer = dynamic_cast<const ConfigOptionInt *>(opt))
            return double(integer->value);
        return opt != nullptr ? opt->getFloat() : fallback;
    };
    auto width = [&config](const char *key, FlowRole role) {
        const auto  *nozzles = config.option<ConfigOptionFloats>("nozzle_diameter");
        const double nozzle  = nozzles != nullptr && !nozzles->values.empty() ? nozzles->values.front() : 0.4;
        double       w       = config.has(key) ? Flow::extrusion_width(key, config) : 0.;
        return w > 0. ? w : double(Flow::auto_extrusion_width(role, float(nozzle)));
    };
    const auto  *mode_opt = config.option<ConfigOptionEnum<NonplanarMode>>("nonplanar_mode");
    const auto   mode     = mode_opt != nullptr ? mode_opt->value : NonplanarMode::Manual;
    if (const auto *fallback = config.option<ConfigOptionEnum<NonplanarFallback>>("nonplanar_fallback"))
        p.banded = mode == NonplanarMode::Auto || fallback->value == NonplanarFallback::None;
    const double layer_height = num("layer_height", 0.2);
    if (const auto *opt = config.option<ConfigOptionInt>("nonplanar_top_shells"))
        p.shells = mode == NonplanarMode::Auto || opt->value < 0 ? AutoMinShells : opt->value;
    if (p.banded && p.shells > 0)
        p.shells = std::max(p.shells, AutoMinShells);
    if (mode == NonplanarMode::Off)
        p.shells = 0;
    const double shell_height = num("nonplanar_shell_layer_height", 0.);
    p.shell_height       = shell_height > 0. ? shell_height : layer_height;
    p.top_shells         = p.shells;
    const double infill_depth = num("nonplanar_infill_depth", 0.);
    if (p.banded && p.top_shells > 0 && infill_depth > 0.) {
        const int top_solid = std::max(int(num("top_shell_layers", 0.)),
                                      int(std::ceil(num("top_shell_thickness", 0.) / p.shell_height - EPSILON)));
        p.shells = std::max(p.shells, top_solid);
        if (num("sparse_infill_density", 0.) > 0.)
            p.shells += int(std::lround(infill_depth / p.shell_height));
    }
    p.max_slope          = Geometry::deg2rad(num("nonplanar_max_slope", 0.));
    p.max_error          = num("nonplanar_max_surface_error", p.max_error);
    p.min_area           = num("nonplanar_min_patch_area", p.min_area);
    p.pitch              = std::max(0.05, num("nonplanar_surface_resolution", p.pitch));
    p.first_layer_height = num("initial_layer_print_height", layer_height);
    p.line_width         = width("top_surface_line_width", frTopSolidInfill);
    if (const auto *loops = config.option<ConfigOptionInt>("wall_loops"))
        p.wall_width = std::max(0, loops->value) * width("inner_wall_line_width", frPerimeter);
    const auto *kinematics = config.option<ConfigOptionEnum<RotaryLayout>>("nonplanar_kinematics");
    if (kinematics != nullptr && kinematics->value != RotaryLayout::None)
        p.max_tilt = Geometry::deg2rad(num("nonplanar_max_tilt", 0.));
    return p;
}

PatchPreview preview_conformal_patches(const indexed_triangle_set &its, const HeadEnvelope &head, const PatchPreviewParams &params,
                                       const std::function<void()> &throw_if_canceled)
{
    PatchPreview out;
    if (its.indices.empty())
        return out;
    const TriangleMesh mesh(its);
    const BoundingBoxf3 bb3   = mesh.bounding_box();
    const double        pitch = params.pitch;
    const HeightField grid(BoundingBoxf(Vec2d(std::floor(bb3.min.x() / pitch) - 1., std::floor(bb3.min.y() / pitch) - 1.) * pitch,
                                        Vec2d(std::ceil(bb3.max.x() / pitch) + 1., std::ceil(bb3.max.y() / pitch) + 1.) * pitch),
                           pitch);
    {
        sla::IndexedMesh imesh(mesh);
        out.surface = sample_top_surface(imesh, bb3.max.z() + 1., 0., grid, throw_if_canceled);
        smooth_scan_noise(out.surface, throw_if_canceled);
    }
    out.conformal = grid;
    if (params.shells <= 0)
        return out;

    DynamicPrintConfig config = params.config ? *params.config : DynamicPrintConfig::full_print_config();
    config.set_key_value("nonplanar_surface_resolution", new ConfigOptionFloat(params.pitch));
    if (!params.config) {
        config.set_key_value("nonplanar_head", new ConfigOptionString(head.id));
        config.set_key_value("layer_height", new ConfigOptionFloat(params.shell_height));
        config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(params.first_layer_height));
        config.set_key_value("nonplanar_top_shells", new ConfigOptionInt(params.top_shells > 0 ? params.top_shells : params.shells));
        config.set_key_value("nonplanar_shell_layer_height", new ConfigOptionFloat(params.shell_height));
        config.set_key_value("nonplanar_max_slope", new ConfigOptionFloat(Geometry::rad2deg(params.max_slope)));
        config.set_key_value("nonplanar_max_surface_error", new ConfigOptionFloat(params.max_error));
        config.set_key_value("nonplanar_min_patch_area", new ConfigOptionFloat(params.min_area));
        config.set_key_value("nonplanar_fallback", new ConfigOptionEnum<NonplanarFallback>(params.banded ? NonplanarFallback::None : NonplanarFallback::Planar));
        config.set_key_value("top_surface_line_width", new ConfigOptionFloatOrPercent(params.line_width, false));
        config.set_key_value("inner_wall_line_width", new ConfigOptionFloatOrPercent(params.line_width, false));
        config.set_key_value("wall_loops", new ConfigOptionInt(int(std::lround(params.wall_width / params.line_width))));
    }
    Model model;
    model.add_object("nonplanar preview", "", mesh)->add_instance();
    Print print;
    print.set_status_callback([&throw_if_canceled](const PrintBase::SlicingStatus &) {
        if (throw_if_canceled)
            throw_if_canceled();
    });
    print.apply(model, std::move(config));
    const int concurrency = int(std::min(size_t(tbb::this_task_arena::max_concurrency()),
                                        tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism)));
    tbb::task_arena arena(std::max(1, concurrency));
    arena.execute([&print] { print.process(); });
    for (const PrintObject *object : print.objects()) {
        const ConformalResult &result = object->conformal();
        for (const ConformalPatch &patch : result.patches) {
            if (patch.kind != PatchKind::Top)
                continue;
            ExPolygons footprint = patch.footprint;
            for (ExPolygon &component : footprint)
                component.translate(object->center_offset());
            const Mask mask = rasterize(footprint, grid);
            const Vec2d center = unscale(object->center_offset());
            for (int iy = 0; iy < grid.ny(); ++iy)
                for (int ix = 0; ix < grid.nx(); ++ix)
                    if (mask[size_t(iy) * grid.nx() + ix])
                        out.conformal.at(ix, iy) = patch.surface.sample(grid.node(ix, iy) - center) + float(bb3.min.z());
            out.patches.emplace_back(std::move(footprint));
        }
    }
    return out;
}

std::vector<double> preview_slope_limits(const HeadEnvelope &head, const PatchPreviewParams &params)
{
    std::vector<double> limits = head.slope_map(SlopeAzimuths, params.shell_height);
    if (params.max_slope > 0.)
        for (double &limit : limits)
            limit = std::min(limit, params.max_slope);
    return limits;
}

double slope_limit_at(const std::vector<double> &limits, double phi)
{
    const int n = int(limits.size());
    return limits[size_t((int(std::lround(phi / (2. * PI) * n)) % n + n) % n)];
}

std::pair<double, double> face_slope(const Vec3d &n)
{
    return { std::acos(std::clamp(n.z(), -1., 1.)), std::atan2(-n.y(), -n.x()) };
}

SlopeBand slope_band(double slope, double limit, double max_tilt)
{
    constexpr double near_margin = 5. * PI / 180.;
    if (slope > limit)
        return slope <= limit + max_tilt ? SlopeBand::Tilt : SlopeBand::Beyond;
    return slope > limit - std::min(near_margin, 0.25 * limit) ? SlopeBand::Near : SlopeBand::Within;
}

double preview_pitch(const Vec2d &size, double pitch)
{
    constexpr double max_nodes = 4000000.;
    return size.x() * size.y() / (pitch * pitch) > max_nodes ? std::sqrt(size.x() * size.y() / max_nodes) : pitch;
}

}
