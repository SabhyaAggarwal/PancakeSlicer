#ifndef SLIC3R_FILAMENT_MIXER_HPP
#define SLIC3R_FILAMENT_MIXER_HPP

#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

class ConfigBase;

struct GradientAnchor {
    double x     = 0.0;
    double y     = 0.0;
    double m_in  = std::numeric_limits<double>::quiet_NaN();
    double m_out = std::numeric_limits<double>::quiet_NaN();
};

struct GradientCurve {
    std::vector<GradientAnchor> points;
    bool empty() const { return points.empty(); }
};

constexpr double kGradientMinRatio = 0.1;
constexpr double kGradientMaxRatio = 0.9;

GradientCurve parse_gradient_curve(const std::string& s);

std::string serialize_gradient_curve(const GradientCurve& c);

double sample_gradient_curve(const GradientCurve& c, double t);

std::vector<double> compute_pchip_default_tangents(const std::vector<GradientAnchor>& pts);

void filament_mixer_lerp(unsigned char r1, unsigned char g1, unsigned char b1,
                         unsigned char r2, unsigned char g2, unsigned char b2,
                         float t,
                         unsigned char* out_r, unsigned char* out_g, unsigned char* out_b);

void filament_mixer_lerp_float(float r1, float g1, float b1,
                               float r2, float g2, float b2,
                               float t,
                               float* out_r, float* out_g, float* out_b);

void filament_mixer_lerp_linear_float(float r1, float g1, float b1,
                                      float r2, float g2, float b2,
                                      float t,
                                      float* out_r, float* out_g, float* out_b);

std::string blend_color(const std::string& hex_a, const std::string& hex_b, float ratio_b);

std::string blend_color_multi(const std::vector<std::string> &hex_colors,
                              const std::vector<int> &weights);

std::vector<unsigned int> parse_mixed_components(const std::string &str);

std::vector<double> parse_mixed_ratios(const std::string &str, size_t n_components);

std::string format_mixed_components(const std::vector<unsigned int> &components);

std::string format_mixed_ratios(const std::vector<int> &weights);

int find_fixed_mixed_filament(const ConfigBase                &project_config,
                              const std::vector<unsigned int> &components,
                              const std::vector<int>          &weights);

bool has_any_mixed_filament(const std::vector<unsigned char> &is_mixed);

std::vector<size_t> check_mixed_filament_integrity(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    size_t num_physical);

std::vector<unsigned int> expand_mixed_filaments(
    const std::vector<unsigned int> &extruders_0based,
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>  &comp_strs);

void remap_mixed_components_on_delete(
    const std::vector<unsigned char> &is_mixed,
    std::vector<std::string>         &comp_strs,
    unsigned int                      del_1based);

std::vector<size_t> check_mixed_filament_type_consistency(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    const std::vector<std::string>   &filament_types);

void expand_mixed_slots_in_unprintables(
    std::vector<std::set<int>>        &unprintables,
    const std::vector<unsigned char>  &is_mixed,
    const std::vector<std::string>    &comp_strs);

void sanitize_mixed_gradient_curve_array(std::vector<std::string>& vals);

std::map<std::string, std::string> validate_mixed_filament_params(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    const std::vector<std::string>   &ratio_strs,
    const std::vector<unsigned char> &gradient_flags,
    const std::vector<std::string>   &gradient_range_strs,
    const std::vector<std::string>   &gradient_curve_strs);

}

#endif
