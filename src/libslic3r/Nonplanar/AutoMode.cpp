#include "AutoMode.hpp"

#include "ConformalIroning.hpp"
#include "ConformalShells.hpp"
#include "HeadEnvelope.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

constexpr int    MaxShells      = 6;
constexpr double LargeArea      = 400.;
constexpr double MaxCurvature   = 0.2;
constexpr double MinCoherence   = 0.1;
constexpr double MaxRoundAspect = 1.5;

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

}

bool top_shells_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) || (is_manual(object) && config.nonplanar_top_shells.value != 0);
}

bool bottom_shells_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) ? object.enable_support.value : is_manual(object) && config.nonplanar_bottom_shells.value != 0;
}

double full_conformal_thickness(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    const double set = config.nonplanar_full_conformal_max_thickness.value;
    if (!is_auto(object))
        return is_manual(object) ? set : 0.;
    if (set > 0.)
        return set;
    const double h = config.nonplanar_shell_layer_height.value > 0. ? config.nonplanar_shell_layer_height.value : object.layer_height.value;
    return 15. * h;
}

bool overhangs_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) || (is_manual(object) && config.nonplanar_overhangs.value);
}

bool bridges_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) || (is_manual(object) && config.nonplanar_bridges.value);
}

double infill_depth(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) ? 2. : is_manual(object) ? config.nonplanar_infill_depth.value : 0.;
}

NonplanarFallback effective_fallback(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return is_auto(object) ? NonplanarFallback::None : config.nonplanar_fallback.value;
}

bool zaa_fallback(const PrintRegionConfig &config, const PrintObjectConfig &object)
{
    return top_shells_enabled(config, object) && effective_fallback(config, object) == NonplanarFallback::ZAA;
}

NonplanarCollisionCheck effective_collision_check(const Print &print)
{
    const NonplanarCollisionCheck set = print.config().nonplanar_collision_check.value;
    for (const PrintObject *object : print.objects())
        if (is_auto(object->config()))
            return std::max(set, NonplanarCollisionCheck::Warn);
    return set;
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
    s.clip_shells      = AutoMinShells;
    s.bottom_shells = std::clamp(int(std::ceil(d / shell_height - 0.15)), AutoMinShells, MaxShells);
    if (is_auto(object.config()) && !object.config().enable_support.value)
        s.bottom_shells = 0;
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
    a.ironing = large && head.tip_flat_radius * std::tan(a.slope90) <= h_c && a.curvature95 <= MaxCurvature;

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

}
