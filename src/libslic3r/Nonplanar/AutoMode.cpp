#include "AutoMode.hpp"

#include "ConformalIroning.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"
#include "MaterialRules.hpp"
#include "Multiplanar.hpp"
#include "PatchTools.hpp"
#include "Repair.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

bool flexible_top(const PrintObject &object, const PrintRegionConfig &config)
{
    const size_t idx = size_t(std::max(0, config.top_surface_filament_id.value - 1));
    return road_material(object.print()->config().filament_type.get_at(idx)) == RoadMaterial::Flexible;
}

constexpr int    MaxShells      = 6;
constexpr double LargeArea      = 400.;
constexpr double MinCoherence   = 0.1;
constexpr double MaxRoundAspect = 1.5;
constexpr double MultiplanarThinShare  = 0.6;
constexpr double MultiplanarSteepShare = 0.55;
constexpr double MultiplanarMinGain    = 0.02;
constexpr double MultiplanarMinRmsGain = 0.10;
constexpr double MultiplanarMaxTimeRatio = 1.5;

bool is_auto(const PrintObjectConfig &object) { return object.nonplanar_mode.value == NonplanarMode::Auto; }
bool is_manual(const PrintObjectConfig &object)
{
    return object.nonplanar_mode.value == NonplanarMode::Manual || object.nonplanar_mode.value == NonplanarMode::Multiplanar;
}

double smallest_nozzle(const PrintConfig &config)
{
    double out = 0.;
    for (double d : config.nozzle_diameter.values)
        if (d > 0. && (out == 0. || d < out))
            out = d;
    return out > 0. ? out : 0.4;
}

double percentile(std::vector<double> &v, double q)
{
    if (v.empty())
        return 0.;
    const size_t k = std::min(v.size() - 1, size_t(q * double(v.size())));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

std::optional<NonplanarTopPattern> user_top_pattern(const PrintObject &object, const PrintRegionConfig &config)
{
    const PrintRegionConfig &base = object.print()->default_region_config();
    if (config.nonplanar_top_pattern.value != base.nonplanar_top_pattern.value)
        return config.nonplanar_top_pattern.value;
    if (config.top_surface_pattern.value == base.top_surface_pattern.value)
        return std::nullopt;
    switch (config.top_surface_pattern.value) {
    case ipConcentric:
    case ipSpiralInset:
    case ipArchimedeanChords:
    case ipOctagramSpiral: return NonplanarTopPattern::Concentric;
    default: return NonplanarTopPattern::Rectilinear;
    }
}

}

bool top_shells_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) || (is_manual(object) && config.nonplanar_top_shells.value != 0);
}

double auto_curved_underside_slope() { return std::tan(Geometry::deg2rad(10.)); }

bool bottom_shells_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) ? object.enable_support.value : is_manual(object) && config.nonplanar_bottom_shells.value != 0;
}

double full_conformal_thickness(const PrintRegionConfig &config, const PrintObject &object)
{
    const double set = config.nonplanar_full_conformal_max_thickness.value;
    if (!is_auto(object.config()))
        return is_manual(object.config()) ? set : 0.;
    size_t open_edges = 0, facets = 0;
    for (const ModelVolume *volume : object.model_object()->volumes)
        if (volume->is_model_part()) {
            open_edges += size_t(std::max(0, volume->mesh().stats().open_edges));
            facets += volume->mesh().facets_count();
        }
    const bool open = open_edges > facets / 100;
    if (!open)
        return 0.;
    if (set > 0.)
        return set;
    const double h = config.nonplanar_shell_layer_height.value > 0. ? config.nonplanar_shell_layer_height.value : object.config().layer_height.value;
    return 15. * h;
}

bool overhangs_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) || (is_manual(object) && config.nonplanar_overhangs.value);
}

bool support_free(const PrintObjectConfig &object) { return is_auto(object); }

bool bridges_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) || (is_manual(object) && config.nonplanar_bridges.value);
}

double infill_depth(const PrintRegionConfig &config, const PrintObject &object)
{
    if (!is_auto(object.config()))
        return is_manual(object.config()) ? config.nonplanar_infill_depth.value : 0.;
    return object.print()->config().nonplanar_kinematics.value != RotaryLayout::None ? 0. : 2.;
}

NonplanarFallback effective_fallback(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) ? NonplanarFallback::None : config.nonplanar_fallback.value;
}

bool zaa_fallback(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return top_shells_enabled(config, object) && effective_fallback(config, object) == NonplanarFallback::ZAA;
}

bool spiral_top_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return !is_auto(object) && config.nonplanar_spiral_top.value;
}

NonplanarCollisionCheck effective_collision_check(const Print &print)
{
    const NonplanarCollisionCheck set = print.config().nonplanar_collision_check.value;
    for (const PrintObject *object : print.objects())
        if (is_auto(object->config()))
            return std::max(set, NonplanarCollisionCheck::Warn);
    return set;
}

bool auto_multiplanar_candidate(double steep_top_share, double thin_top_share)
{
    return steep_top_share >= MultiplanarSteepShare || thin_top_share >= MultiplanarThinShare;
}

bool auto_multiplanar_confirmed(double chunks_rms, double planar_rms, double chunks_time, double planar_time)
{
    return std::isfinite(chunks_rms) && std::isfinite(planar_rms) && std::isfinite(planar_time) && planar_time > 0. &&
           chunks_rms <= (1. - MultiplanarMinRmsGain) * planar_rms && chunks_time < MultiplanarMaxTimeRatio * planar_time;
}

bool auto_multiplanar_chunks(const MultiplanarPlan &plan)
{
    return plan.chunks.size() > 1 && plan.step_area < (1. - MultiplanarMinGain) * plan.planar_step_area;
}

AutoObjectSettings auto_object_settings(const PrintObject &object, const PrintRegion &region, double shell_height)
{
    const PrintConfig &print_config = object.print()->config();
    const double       d            = smallest_nozzle(print_config);
    const double       w            = region.flow(object, frTopSolidInfill, shell_height).width();
    const HeadEnvelope head         = resolve_head_envelope(print_config);

    AutoObjectSettings s;
    s.max_error = std::clamp(0.25 * shell_height, 0.02, 0.1);
    s.min_area  = (10. * w) * (10. * w);
    const double gantry = head.gantry_clearance();
    s.max_patch_height  = std::isfinite(gantry) ? std::max(0., gantry - head.margin) : 0.;
    if (print_config.nonplanar_kinematics.value != RotaryLayout::None) {
        const double reach = std::max(shell_height, head_band_height(head) - head.margin);
        s.max_patch_height = s.max_patch_height > 0. ? std::min(s.max_patch_height, reach) : reach;
    }
    s.clip_shells      = AutoMinShells;
    s.bottom_shells = std::clamp(int(std::ceil(d / shell_height - 0.15)), AutoMinShells, MaxShells);
    return s;
}

void choose_patch_settings(const PrintObject &object, const HeadEnvelope &head, double max_error, double first_layer_top,
                           ConformalPatch &patch)
{
    const PrintRegionConfig &config = object.printing_region(size_t(patch.region_id)).config();
    if (!is_auto(object.config()) && config.nonplanar_top_shells.value >= 0)
        return;

    const HeightField &S     = patch.surface;
    const double       pitch = S.pitch();
    const double       h_c   = patch.shell_height;
    const double       d     = smallest_nozzle(object.print()->config());

    AutoPatchSettings a;
    a.max_error  = max_error;
    a.shell_area = area(diff_ex(patch.footprint, patch.wall_band)) * SCALING_FACTOR * SCALING_FACTOR;

    std::vector<double> slopes, curvatures;
    double jxx = 0., jxy = 0., jyy = 0.;
    double n = 0., mx = 0., my = 0., mxx = 0., mxy = 0., myy = 0.;
    float  z_lo = std::numeric_limits<float>::infinity();
    const int step = 2;
    auto      z_at = [&S](int ix, int iy) {
        return ix >= 0 && iy >= 0 && ix < S.nx() && iy < S.ny() ? S.at(ix, iy) : HeightField::NoData;
    };
    for (int iy = 0; iy < S.ny(); ++iy)
        for (int ix = 0; ix < S.nx(); ++ix) {
            const float z = S.at(ix, iy);
            if (std::isnan(z))
                continue;
            z_lo = std::min(z_lo, z);
            const Vec2d p = S.node(ix, iy);
            n += 1., mx += p.x(), my += p.y(), mxx += p.x() * p.x(), mxy += p.x() * p.y(), myy += p.y() * p.y();
            const Vec2d g = S.gradient(p);
            if (std::isnan(g.x()) || std::isnan(g.y()))
                continue;
            slopes.emplace_back(std::atan(g.norm()));
            jxx += g.x() * g.x(), jxy += g.x() * g.y(), jyy += g.y() * g.y();
            const float xm = z_at(ix - step, iy), xp = z_at(ix + step, iy), ym = z_at(ix, iy - step), yp = z_at(ix, iy + step);
            const float a0 = z_at(ix - step, iy - step), a1 = z_at(ix + step, iy - step), a2 = z_at(ix - step, iy + step),
                        a3 = z_at(ix + step, iy + step);
            if (std::isnan(xm + xp + ym + yp + a0 + a1 + a2 + a3))
                continue;
            const double hh  = (step * pitch) * (step * pitch);
            const double fxx = (xp - 2. * z + xm) / hh, fyy = (yp - 2. * z + ym) / hh, fxy = (a3 - a1 - a2 + a0) / (4. * hh);
            const double k = (std::abs(0.5 * (fxx + fyy)) + std::hypot(0.5 * (fxx - fyy), fxy)) / std::pow(1. + g.squaredNorm(), 1.5);
            curvatures.emplace_back(k);
        }
    a.slope90     = percentile(slopes, 0.9);
    a.curvature95 = percentile(curvatures, 0.95);
    const double curvature80 = percentile(curvatures, 0.8);
    a.curvature80 = curvature80;
    const double trace = jxx + jyy;
    a.coherence = trace > 1e-9 ? std::hypot(jxx - jyy, 2. * jxy) / trace : 0.;
    if (n > 0.) {
        const double cxx = mxx / n - (mx / n) * (mx / n), cyy = myy / n - (my / n) * (my / n), cxy = mxy / n - (mx / n) * (my / n);
        const double mid = 0.5 * (cxx + cyy), rad = std::hypot(0.5 * (cxx - cyy), cxy);
        a.aspect = mid - rad > 1e-12 ? std::sqrt((mid + rad) / (mid - rad)) : std::numeric_limits<double>::infinity();
    }

    const bool   large   = a.shell_area >= LargeArea;
    const double T       = (large ? 1.5 : 1.0) * d;
    const int    n_depth = std::isfinite(z_lo) ? int(std::floor((z_lo - 0.5 * h_c - first_layer_top) / h_c + 1e-6)) : AutoMinShells;
    a.shells = std::clamp(int(std::ceil(T / (h_c * std::cos(a.slope90)) - 0.15)), AutoMinShells, MaxShells);
    a.shells = std::max(AutoMinShells, std::min(a.shells, n_depth));
    patch.shells = a.shells;
    if (!is_auto(object.config()))
        return;

    const bool banded = effective_fallback(config, object.config()) == NonplanarFallback::None;
    a.pattern = banded || a.coherence >= MinCoherence ? NonplanarTopPattern::Contour :
                a.aspect <= MaxRoundAspect            ? NonplanarTopPattern::Concentric :
                                                        NonplanarTopPattern::Rectilinear;
    if (const std::optional<NonplanarTopPattern> user = user_top_pattern(object, config))
        a.pattern = *user;
    const double r = head.tip_flat_radius;
    const bool ironing_off = config.ironing_type.value == IroningType::NoIroning &&
                             object.print()->default_region_config().ironing_type.value != IroningType::NoIroning;
    a.ironing = !ironing_off && conformal_ironing_enabled(config) && !flexible_top(object, config) &&
                0.5 * r * r * curvature80 <= 0.25 * h_c &&
                object.print()->config().nonplanar_kinematics.value == RotaryLayout::None;

    patch.automatic = std::make_shared<const AutoPatchSettings>(a);
    BOOST_LOG_TRIVIAL(info) << "Non-planar auto: patch " << patch.id << " area " << a.shell_area << " mm2, slope90 "
                            << Geometry::rad2deg(a.slope90) << " deg, curvature95 " << a.curvature95 << ", coherence "
                            << a.coherence << ", aspect " << a.aspect << ": " << auto_comment(patch);
}

NonplanarTopPattern top_pattern(const ConformalPatch &patch, const PrintRegionConfig &config)
{
    return patch.automatic ? patch.automatic->pattern : config.nonplanar_top_pattern.value;
}

bool ironing_enabled(const ConformalPatch &patch, const PrintRegionConfig &config)
{
    return patch.automatic ? patch.automatic->ironing : conformal_ironing_enabled(config);
}

std::string auto_comment(const ConformalPatch &patch)
{
    if (!patch.automatic)
        return {};
    const AutoPatchSettings &a = *patch.automatic;
    const char *pattern = a.pattern == NonplanarTopPattern::Contour ? "contour" :
                          a.pattern == NonplanarTopPattern::Concentric ? "concentric" : "rectilinear";
    char buf[160];
    std::snprintf(buf, sizeof(buf), "; nonplanar auto: patch %d shells=%d pattern=%s err=%.3g ironing=%d\n", patch.id, patch.shells,
                  pattern, a.max_error, int(a.ironing));
    return buf;
}

std::string explain_top_patch(const PrintObject &object, const ConformalPatch &patch)
{
    if (patch.kind != PatchKind::Top)
        return {};
    const auto &region = object.printing_region(size_t(patch.region_id));
    const auto &config = region.config();
    const auto head = resolve_head_envelope(object.print()->config());
    const auto mode = object.config().nonplanar_mode.value;
    const bool automatic = mode == NonplanarMode::Auto;
    const auto pattern = top_pattern(patch, config);
    const char *pattern_name = pattern == NonplanarTopPattern::Contour ? "contour" :
                               pattern == NonplanarTopPattern::Concentric ? "concentric" : "rectilinear";
    std::ostringstream out;
    out << "patch " << patch.id << " mode=" << (automatic ? "auto" : mode == NonplanarMode::Multiplanar ? "multiplanar" : "manual")
        << " (object setting) pattern=" << pattern_name << " (";
    if (patch.automatic) {
        const auto &a = *patch.automatic;
        out << (effective_fallback(config, object.config()) == NonplanarFallback::None ? "auto: Z-band schedule" :
                a.coherence >= MinCoherence ? "auto: dominant gradient" :
                a.aspect <= MaxRoundAspect ? "auto: round footprint" : "auto: elongated footprint");
    } else
        out << "region setting";
    out << ") shells=" << patch.shells << " shell_height=" << patch.shell_height << " mm";
    if (patch.band >= 0) {
        double cap = automatic ? auto_object_settings(object, region, patch.shell_height).max_patch_height :
                                 effective_patch_height_cap(object);
        const double object_cap = cap;
        cap = multi_material_patch_height(object, head, cap);
        const double material_cap = cap;
        if (const auto *repair = object.nonplanar_repair())
            for (const auto &band : repair->order_bands)
                cap = std::min(cap > 0. ? cap : std::numeric_limits<double>::infinity(), band.height);
        const double repair_cap = cap;
        if (const double rotary = rotary_band_reach(object, head, patch.shell_height); rotary > 0.)
            cap = cap > 0. ? std::min(cap, rotary) : rotary;
        double pitch = 0., tolerance = 0.;
        band_schedule(head, patch.shell_height, cap, pitch, tolerance);
        out << " band_height=" << pitch + tolerance << " mm (head reach=" << head_band_height(head);
        if (automatic)
            out << "; auto gantry cap=" << object_cap;
        else if (object_cap > 0.)
            out << "; user cap=" << object_cap;
        if (material_cap != object_cap)
            out << "; multi-material cap=" << material_cap;
        if (repair_cap != material_cap)
            out << "; repair cap=" << repair_cap;
        if (cap != repair_cap)
            out << "; rotary tilt cap=" << cap;
        out << ") band_pitch=" << pitch << " mm band=" << patch.band;
    } else
        out << " band_height=" << patch.z_max - patch.z_min << " mm (unbanded patch relief)";
    const bool requested = ironing_enabled(patch, config);
    out << " ironing=" << (patch.ironing.entities.empty() ? "off" : "on") << " (";
    if (patch.automatic) {
        const auto &a = *patch.automatic;
        const double r = head.tip_flat_radius, deviation = 0.5 * r * r * a.curvature80;
        if (!conformal_ironing_enabled(config))
            out << "auto: region ironing off";
        else
            out << "auto: land deviation " << deviation << (deviation <= 0.25 * patch.shell_height ? " <= " : " > ")
                << 0.25 * patch.shell_height << " mm (h_c/4)";
        out << "; area=" << a.shell_area << " mm2 slope90=" << Geometry::rad2deg(a.slope90)
            << " deg curvature80=" << a.curvature80 << "/mm";
    } else
        out << (requested ? "region enabled" : "region disabled");
    if (requested && patch.ironing.entities.empty())
        out << (flexible_top(object, config) ? "; flexible filament: curved tops are not ironed" :
                                               "; no paths survived surface, wall, band or repair clipping");
    out << ")\n";
    return out.str();
}

}
