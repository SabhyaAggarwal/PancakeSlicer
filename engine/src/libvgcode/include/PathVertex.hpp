///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_PATHVERTEX_HPP
#define VGCODE_PATHVERTEX_HPP

#include "Types.hpp"

#include <array>
#include <cfloat>
#include <cstdint>
#include <cstddef>
#include <limits>

namespace libvgcode {

struct PathVertex
{
    Vec3 position{ FLT_MAX, FLT_MAX, FLT_MAX };
    float height{ 0.0f };
    float width{ 0.0f };
    float feedrate{ 0.0f };
    float actual_feedrate{ 0.0f };
    float mm3_per_mm{ 0.0f };
    float fan_speed{ 0.0f };
    float temperature{ 0.0f };
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    float weight{ 0.0f };
#endif
    EGCodeExtrusionRole role{ EGCodeExtrusionRole::None };
    EMoveType type{ EMoveType::Noop };
    uint32_t gcode_id{ 0 };
    uint32_t layer_id{ 0 };
    uint8_t extruder_id{ 0 };
    uint8_t color_id{ 0 };
    std::array<float, TIME_MODES_COUNT> times{ 0.0f, 0.0f };
    float layer_duration{ 0.0f };
    float pressure_advance{ 0.0f };
    float acceleration{ 0.0f };
    float jerk{ 0.0f };
    float clearance{ std::numeric_limits<float>::quiet_NaN() };
    uint32_t source_move_id{ std::numeric_limits<uint32_t>::max() };
    uint8_t nonplanar_shell{ 0xFF };
    int8_t nonplanar_band{ -1 };
    int16_t nonplanar_patch{ -1 };
    std::array<float, 2> surface_normal{ 0.0f, 0.0f };

    bool is_extrusion() const;
    bool is_travel() const;
    bool is_option() const;
    bool is_wipe() const;
    bool is_custom_gcode() const;
    bool is_nonplanar() const { return nonplanar_shell != 0xFF; }
    float slope_deg() const;
    float volumetric_rate() const { return feedrate * mm3_per_mm; }
    float actual_volumetric_rate() const { return actual_feedrate * mm3_per_mm; }

    static const PathVertex DUMMY_PATH_VERTEX;
};

}

#endif