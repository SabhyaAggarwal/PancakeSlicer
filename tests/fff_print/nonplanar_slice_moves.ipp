#pragma once
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <initializer_list>
#include <ios>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template<class T> void move_le(std::ostream &out, T value)
{
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    char bytes[sizeof(T)];
    for (size_t i = 0; i < sizeof(T); ++i) bytes[i] = char(bits >> (8 * i));
    out.write(bytes, sizeof(bytes));
}
void move_float(std::ostream &out, float value)
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    move_le(out, bits);
}

void write_moves(const std::string &path, const GCodeProcessorResult &result, const std::vector<std::string> &filaments)
{
    static_assert(uint8_t(EMoveType::Count) < 128, "Move type must leave bit 7 for internal_only");
    struct Layer { uint32_t id; uint64_t first; float z; };
    std::vector<Layer> layers;
    for (size_t i = 1; i < result.moves.size(); ++i) {
        const auto &move = result.moves[i];
        const auto &state = result.state(move);
        float z = state.print_z;
        if (z <= 0.f && !result.belt_tilt_angle && move.type == EMoveType::Extrude && state.nonplanar_patch_shell == 0xFF)
            z = move.position.z();
        if (layers.empty() || layers.back().id != move.layer_id)
            layers.push_back({move.layer_id, i, z});
        else layers.back().z = std::max(layers.back().z, z);
    }
    std::vector<std::string> colors = result.extruder_colors;
    for (const auto &item : result.custom_gcode_per_print_z)
        if (item.type == CustomGCode::ColorChange) colors.push_back(item.color);
    if (!result.custom_gcode_per_print_z.empty()) colors.emplace_back("#808080");
    std::vector<std::string> filament_names = result.settings_ids.filament;
    if (filament_names.size() < filaments.size()) filament_names.resize(filaments.size());
    for (size_t i = 0; i < filaments.size(); ++i)
        if (filament_names[i].empty()) filament_names[i] = filaments[i];
    nlohmann::json metadata = {{"extruder_colors", result.extruder_colors}, {"color_print_colors", colors},
        {"filament_names", filament_names}, {"estimated_times", nlohmann::json::array()},
        {"belt_tilt_angle", result.belt_tilt_angle}, {"belt_z_origin", result.belt_z_origin},
        {"machine_frame_transform_active", result.machine_frame_transform_active}};
    for (const auto &mode : result.print_statistics.modes)
        metadata["estimated_times"].push_back({{"time", mode.time}, {"prepare_time", mode.prepare_time}});
    const std::string json = metadata.dump();
    if (result.move_states.size() > UINT32_MAX || layers.size() > UINT32_MAX)
        throw std::runtime_error("move stream table exceeds uint32 range");
    const fs::path destination(path);
    const fs::path temporary = destination.parent_path() / fs::unique_path(destination.filename().string() + ".%%%%%%.tmp");
    try {
        boost::nowide::ofstream out(temporary.string(), std::ios::binary | std::ios::trunc);
        out.write("ORCAMV01", 8);
        for (uint32_t value : {64u, 52u, 32u, 16u, 1u, 0u}) move_le(out, value);
        move_le(out, uint64_t(result.moves.size()));
        move_le(out, uint32_t(result.move_states.size()));
        move_le(out, uint32_t(layers.size()));
        move_le(out, uint64_t(json.size()));
        move_le(out, uint64_t(0));
        for (const auto &state : result.move_states) {
            for (float value : {state.fan_speed, state.temperature, state.pressure_advance, state.acceleration, state.jerk, state.print_z})
                move_float(out, value);
            move_le(out, int32_t(state.object_label_id));
            move_le(out, uint8_t(state.nonplanar_patch_shell));
            move_le(out, int8_t(state.nonplanar_band));
            move_le(out, int16_t(state.nonplanar_patch));
        }
        for (const auto &move : result.moves) {
            move_le(out, uint32_t(move.gcode_id));
            move_le(out, uint8_t(uint8_t(move.type) | (move.internal_only ? 0x80 : 0)));
            move_le(out, uint8_t(move.extrusion_role));
            move_le(out, uint8_t(move.extruder_id));
            move_le(out, uint8_t(move.cp_color_id));
            for (float value : {move.position.x(), move.position.y(), move.position.z(), move.delta_extruder,
                    move.feedrate, move.width, move.height, move.mm3_per_mm,
                    move.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)]}) move_float(out, value);
            move_le(out, uint32_t(move.layer_id));
            move_le(out, uint32_t(move.state));
        }
        for (const Layer &layer : layers) {
            move_le(out, layer.id); move_le(out, layer.first); move_float(out, layer.z);
        }
        out.write(json.data(), std::streamsize(json.size()));
        out.close();
        if (!out) throw std::runtime_error("cannot write move stream " + path);
        fs::rename(temporary, destination);
    } catch (...) {
        boost::system::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
}
