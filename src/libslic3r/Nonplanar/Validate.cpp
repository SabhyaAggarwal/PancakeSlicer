#include "Validate.hpp"

#include "HeadEnvelope.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Nonplanar/ConformalShells.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {

StringObjectException validate(const Print &print, std::vector<StringObjectException> *warnings)
{
    const PrintConfig &config = print.config();

    double min_nozzle = 0.;
    for (double d : config.nozzle_diameter.values)
        if (d > 0. && (min_nozzle == 0. || d < min_nozzle))
            min_nozzle = d;

    bool needs_head = config.nonplanar_collision_check.value == NonplanarCollisionCheck::Block;
    bool enabled = needs_head || config.nonplanar_kinematics.value != RotaryLayout::None;
    size_t instances = 0;
    for (const PrintObject *object : print.objects()) {
        instances += object->instances().size();
        for (size_t region_id = 0; region_id < object->num_printing_regions(); ++region_id) {
            const PrintRegionConfig &region = object->printing_region(region_id).config();
            if (config.spiral_mode && region.nonplanar_full_conformal_max_thickness.value > 0.)
                return { L("Full conformal layers cannot be combined with spiral vase mode."), object,
                         "nonplanar_full_conformal_max_thickness" };
            const bool shells = region.nonplanar_top_shells.value > 0 || region.nonplanar_bottom_shells.value > 0;
            needs_head |= shells || region.nonplanar_ironing.value;
            enabled |= shells || region.nonplanar_ironing.value;
            if (!shells)
                continue;
            if (config.spiral_mode)
                return { L("Non-planar shells cannot be combined with spiral vase mode."), object,
                         region.nonplanar_top_shells.value > 0 ? "nonplanar_top_shells" : "nonplanar_bottom_shells" };
            const double height = region.nonplanar_shell_layer_height.value > 0. ? region.nonplanar_shell_layer_height.value :
                                                                                 object->config().layer_height.value;
            if (min_nozzle > 0. && height > 0.75 * min_nozzle)
                return { L("Non-planar shell layer height must not exceed 75% of the nozzle diameter."), object,
                         "nonplanar_shell_layer_height" };
            if (object->config().nonplanar_max_patch_height.value != 0. &&
                object->config().nonplanar_max_patch_height.value < std::max(height, object->config().layer_height.value))
                return { L("Non-planar maximum patch height must be at least the layer height and the shell layer height."), object,
                         "nonplanar_max_patch_height" };
            if (object->model_object()->has_custom_layering())
                return { L("Non-planar shells do not support variable layer heights."), object, "layer_height" };
        }
    }

    if (config.nonplanar_kinematics.value != RotaryLayout::None) {
        const auto valid_floats = [](const ConfigOptionFloats &option, size_t size) {
            return option.values.size() == size && std::all_of(option.values.begin(), option.values.end(),
                                                               [](double value) { return std::isfinite(value); });
        };
        if (!valid_floats(config.rotary_axis_limits, 4) ||
            config.rotary_axis_limits.values[0] >= config.rotary_axis_limits.values[1] ||
            config.rotary_axis_limits.values[2] >= config.rotary_axis_limits.values[3])
            return { L("Rotary axes need two ordered pairs of finite axis limits."), nullptr, "rotary_axis_limits" };
        if (!valid_floats(config.rotary_pivot, 3))
            return { L("Rotary axes need three finite pivot coordinates."), nullptr, "rotary_pivot" };
        if (!valid_floats(config.rotary_max_speed, 2) ||
            std::any_of(config.rotary_max_speed.values.begin(), config.rotary_max_speed.values.end(), [](double speed) { return speed < 0.; }))
            return { L("Rotary axes need two non-negative maximum speeds."), nullptr, "rotary_max_speed" };
        if (config.rotary_axis_continuous.values.size() != 2)
            return { L("Rotary axes need a continuous-rotation setting for each axis."), nullptr, "rotary_axis_continuous" };
        if (!valid_floats(config.rotary_tool_mount, 3))
            return { L("Rotary axes need three finite tool mount coordinates."), nullptr, "rotary_tool_mount" };
        if (!valid_floats(config.rotary_axis_home, 2))
            return { L("Rotary axes need two finite home angles."), nullptr, "rotary_axis_home" };
        if (config.rotary_axis_reversed.values.size() != 2)
            return { L("Rotary axes need a direction setting for each axis."), nullptr, "rotary_axis_reversed" };
        if (config.rotary_linear_moves_table.values.size() != 3)
            return { L("Rotary axes need a table-motion setting for each linear axis."), nullptr, "rotary_linear_moves_table" };
        const bool remapped = config.gcode_remap_x.value != RemapAxis::PosX || config.gcode_remap_y.value != RemapAxis::PosY ||
                              config.gcode_remap_z.value != RemapAxis::PosZ;
        if (remapped || config.belt_printer)
            return { L("Rotary axes cannot be combined with a belt printer or G-code axis remapping."), nullptr, "nonplanar_kinematics" };
        if (instances > 1 && config.print_sequence != PrintSequence::ByObject)
            return { L("With rotary axes, several objects can only be printed in \"By object\" sequence."), nullptr, "print_sequence" };
        if (config.rotary_tool_length.value <= 0. && !config.rotary_firmware_rtcp)
            return { L("Rotary axes need the tool length, or tool centre point control in the firmware."), nullptr, "rotary_tool_length" };
    }

    if (needs_head && config.nonplanar_head.value.empty())
        return { L("Select a non-planar head before enabling curved shells, curved ironing, or blocking collision checks."), nullptr,
                 "nonplanar_head" };
    if (!config.nonplanar_head.value.empty() && !HeadLibrary::instance().find(config.nonplanar_head.value)) {
        const std::string message = (boost::format(_u8L("Non-planar head \"%1%\" is missing from the head library or could not be loaded. Select a valid head.")) %
                                     config.nonplanar_head.value).str();
        if (enabled || config.nonplanar_collision_check.value != NonplanarCollisionCheck::Off)
            return { message, nullptr, "nonplanar_head" };
        if (warnings != nullptr) {
            StringObjectException warning { message, nullptr, "nonplanar_head" };
            warning.is_warning = true;
            warnings->push_back(std::move(warning));
        }
    }
    return validate_layer_gcodes(print, warnings);
}

StringObjectException validate_layer_gcodes(const Print &print, std::vector<StringObjectException> *warnings)
{
    for (const CustomGCode::Item &item : print.model().get_curr_plate_custom_gcodes().gcodes)
        for (const PrintObject *object : print.objects()) {
            if (! object->is_step_done(posNonplanar) || object->layers().empty())
                continue;
            const auto layer = std::lower_bound(object->layers().begin(), object->layers().end(), item.print_z - EPSILON,
                                                [](const Layer *l, double z) { return l->print_z < z; });
            if (layer == object->layers().end())
                continue;
            const size_t layer_idx = size_t(layer - object->layers().begin());
            for (const ConformalPatch &patch : object->conformal().patches) {
                if (patch.host_layer < layer_idx || patch.z_min >= (*layer)->bottom_z() - EPSILON)
                    continue;
                if (item.type == CustomGCode::PausePrint || item.type == CustomGCode::Custom || item.type == CustomGCode::Template)
                    return { Slic3r::format(_u8L("The pause or custom G-code at %1$.2f mm falls inside the curved shells of %2%, which print "
                                                 "after it but reach below it. Move it above %3$.2f mm or below %4$.2f mm."),
                                            item.print_z, object->model_object()->name, object->get_layer(int(patch.host_layer))->print_z,
                                            patch.z_min),
                             object, "nonplanar_top_shells" };
                if (warnings != nullptr) {
                    StringObjectException w { Slic3r::format(_u8L("The filament change at %1$.2f mm falls inside the curved shells of %2%: "
                                                                  "their part below it prints in the new filament."),
                                                             item.print_z, object->model_object()->name),
                                              object, "nonplanar_top_shells" };
                    w.is_warning = true;
                    warnings->push_back(std::move(w));
                }
                break;
            }
        }
    return {};
}

void report_head(const Print &print)
{
    const PrintConfig &config = print.config();
    bool enabled = config.nonplanar_kinematics.value != RotaryLayout::None ||
                   config.nonplanar_collision_check.value == NonplanarCollisionCheck::Block;
    double gap = 0.;
    for (const PrintObject *object : print.objects())
        for (size_t i = 0; i < object->num_printing_regions(); ++i) {
            const PrintRegionConfig &region = object->printing_region(i).config();
            if (region.nonplanar_top_shells.value <= 0 && region.nonplanar_bottom_shells.value <= 0 && !region.nonplanar_ironing.value)
                continue;
            enabled = true;
            const double height = region.nonplanar_shell_layer_height.value > 0. ? region.nonplanar_shell_layer_height.value :
                                                                                 object->config().layer_height.value;
            gap = gap == 0. ? height : std::min(gap, height);
        }
    if (!enabled)
        return;
    if (gap == 0. && !print.objects().empty())
        gap = print.objects().front()->config().layer_height.value;
    const HeadEnvelope head = resolve_head_envelope(config);
    const std::vector<double> slopes = head.slope_map(360, gap);
    const double limit = Geometry::rad2deg(*std::min_element(slopes.begin(), slopes.end()));
    const std::string message = (boost::format(_u8L("Non-planar: head %1% (%2%), limit %3$.1f° at %4% mm")) % head.id %
                                 (head.calibrated ? _u8L("calibrated") : _u8L("uncalibrated")) % limit % gap).str();
    BOOST_LOG_TRIVIAL(info) << message;
    print.set_status(0, message);
}

}
