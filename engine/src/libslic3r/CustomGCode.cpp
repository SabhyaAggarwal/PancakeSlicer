#include "CustomGCode.hpp"
#include <vector>
#include <utility>
#include <cstddef>
#include <cassert>

namespace Slic3r {

namespace CustomGCode {

#if 0
extern void update_custom_gcode_per_print_z_from_config(Info& info, DynamicPrintConfig* config)
{
	auto *colorprint_heights = config->option<ConfigOptionFloats>("colorprint_heights");
    if (colorprint_heights == nullptr)
        return;
    if (info.gcodes.empty() && ! colorprint_heights->values.empty()) {
        const std::vector<std::string>& colors = ColorPrintColors::get();
        const auto& colorprint_values = colorprint_heights->values;
        info.gcodes.clear();
        info.gcodes.reserve(colorprint_values.size());
        int i = 0;
        for (auto val : colorprint_values)
            info.gcodes.emplace_back(Item{ val, ColorChange, 1, colors[(++i)%7] });

        info.mode = SingleExtruder;
	}

    config->erase("colorprint_heights");
}
#endif

extern void check_mode_for_custom_gcode_per_print_z(Info& info)
{
    if (info.mode != Undef)
        return;

    bool is_single_extruder = true;
    for (auto item : info.gcodes) 
    {
        if (item.type == ToolChange) {
            info.mode = MultiAsSingle;
            return;
        }
        if (item.type == ColorChange && item.extruder > 1)
            is_single_extruder = false;
    }

    info.mode = is_single_extruder ? SingleExtruder : MultiExtruder;
}

std::vector<std::pair<double, unsigned int>> custom_tool_changes(const Info& custom_gcode_per_print_z, size_t num_extruders)
{
    std::vector<std::pair<double, unsigned int>> custom_tool_changes;
    for (const Item& custom_gcode : custom_gcode_per_print_z.gcodes)
        if (custom_gcode.type == ToolChange) {
            assert(custom_gcode.extruder >= 0);
            custom_tool_changes.emplace_back(custom_gcode.print_z, static_cast<unsigned int>(size_t(custom_gcode.extruder) > num_extruders ? 1 : custom_gcode.extruder));
        }
    return custom_tool_changes;
}

}

}
