#pragma once
#include "libslic3r/PrintConfig.hpp"

#include <string>

namespace Slic3r {
class Print;
class PrintObject;
class PrintRegion;
}

namespace Slic3r::Nonplanar {

struct ConformalPatch;
struct HeadEnvelope;

constexpr int AutoMinShells = 2;

bool top_shells_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object);
bool bottom_shells_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object);
double full_conformal_thickness(const PrintRegionConfig &config, const PrintObject &object);
bool overhangs_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object);
bool bridges_enabled(const PrintRegionConfig &config, const PrintObjectConfig &object);
double infill_depth(const PrintRegionConfig &config, const PrintObjectConfig &object);
NonplanarFallback effective_fallback(const PrintRegionConfig &config, const PrintObjectConfig &object);
bool zaa_fallback(const PrintRegionConfig &config, const PrintObjectConfig &object);
NonplanarCollisionCheck effective_collision_check(const Print &print);

struct AutoObjectSettings
{
    double max_error        = 0.;
    double min_area         = 0.;
    double max_patch_height = 0.;
    int    clip_shells      = AutoMinShells;
    int    bottom_shells    = 0;
};
AutoObjectSettings auto_object_settings(const PrintObject &object, const PrintRegion &region, double shell_height);

struct AutoPatchSettings
{
    int                 shells  = AutoMinShells;
    NonplanarTopPattern pattern = NonplanarTopPattern::Contour;
    bool                ironing = false;
    double              max_error = 0.;
    double shell_area = 0.;
    double slope90    = 0.;
    double curvature95 = 0.;
    double coherence  = 0.;
    double aspect     = 1.;
};

void choose_patch_settings(const PrintObject &object, const HeadEnvelope &head, double max_error, double first_layer_top,
                           ConformalPatch &patch);

NonplanarTopPattern top_pattern(const ConformalPatch &patch, const PrintRegionConfig &config);
bool                ironing_enabled(const ConformalPatch &patch, const PrintRegionConfig &config);

std::string auto_comment(const ConformalPatch &patch);

}
