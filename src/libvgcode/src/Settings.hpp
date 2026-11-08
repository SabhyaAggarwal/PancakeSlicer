///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_SETTINGS_HPP
#define VGCODE_SETTINGS_HPP

#include "../include/Types.hpp"

#include <array>
#include <cstddef>
#include <map>

namespace libvgcode {

struct Settings
{
		EViewType view_type{ EViewType::FeatureType };
		ENonplanarColor nonplanar_color{ ENonplanarColor::Shell };
		ETimeMode time_mode{ ETimeMode::Normal };
		bool top_layer_only_view_range{ false };
		bool dim_previous_layers{ false };
		float dim_previous_layers_brightness{ 0.4f };
		bool spiral_vase_mode{ false };
		bool update_view_full_range{ true };
		bool update_enabled_entities{ true };
		bool update_colors{ true };

		std::array<bool, std::size_t(EOptionType::COUNT)> options_visibility{
			    false,
				false,
				false,
				false,
				true,
				false,
				false,
				false,
				false,
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
				false,
				true
#endif
		};

		std::array<bool, std::size_t(EGCodeExtrusionRole::COUNT)> extrusion_roles_visibility{
				true,
				true,
				true,
				true,
				true,
                true,
				true,
				true,
				true,
				true,
				true,
				true,
				true,
				true,
				true,
		        true,
		        true,
		        true,
		        true,
		        true,
		};
};

}

#endif
