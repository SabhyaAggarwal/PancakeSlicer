#include "Validate.hpp"

#include "libslic3r/I18N.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cstddef>

namespace Slic3r::Nonplanar {

StringObjectException validate(const Print &print, std::vector<StringObjectException> * )
{
    const PrintConfig &config = print.config();

    double min_nozzle = 0.;
    for (double d : config.nozzle_diameter.values)
        if (d > 0. && (min_nozzle == 0. || d < min_nozzle))
            min_nozzle = d;

    size_t instances = 0;
    for (const PrintObject *object : print.objects()) {
        instances += object->instances().size();
        for (size_t region_id = 0; region_id < object->num_printing_regions(); ++region_id) {
            const PrintRegionConfig &region = object->printing_region(region_id).config();
            if (region.nonplanar_top_shells.value <= 0 && region.nonplanar_bottom_shells.value <= 0)
                continue;
            if (config.spiral_mode)
                return { L("Non-planar shells cannot be combined with spiral vase mode."), object, "nonplanar_top_shells" };
            if (min_nozzle > 0. && region.nonplanar_shell_layer_height.value > 0.75 * min_nozzle)
                return { L("Non-planar shell thickness must not exceed 75% of the nozzle diameter."), object,
                         "nonplanar_shell_layer_height" };
        }
    }

    if (config.nonplanar_kinematics.value != RotaryLayout::None) {
        const bool remapped = config.gcode_remap_x.value != RemapAxis::PosX || config.gcode_remap_y.value != RemapAxis::PosY ||
                              config.gcode_remap_z.value != RemapAxis::PosZ;
        if (remapped || config.belt_printer)
            return { L("Rotary axes cannot be combined with a belt printer or G-code axis remapping."), nullptr, "nonplanar_kinematics" };
        if (instances > 1 && config.print_sequence != PrintSequence::ByObject)
            return { L("With rotary axes, several objects can only be printed in \"By object\" sequence."), nullptr, "print_sequence" };
        if (config.rotary_tool_length.value <= 0. && !config.rotary_firmware_rtcp)
            return { L("Rotary axes need the tool length, or tool centre point control in the firmware."), nullptr, "rotary_tool_length" };
    }

    return {};
}

}
