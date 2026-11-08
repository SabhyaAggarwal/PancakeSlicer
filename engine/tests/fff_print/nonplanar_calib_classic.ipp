#pragma once
#include "libslic3r/Arrange.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ParameterUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/calib.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::optional<CalibMode> classic_calib_mode(const std::string &kind)
{
    static const std::map<std::string, CalibMode> kinds = {
        { "cornering", CalibMode::Calib_Cornering },
        { "flow-rate", CalibMode::Calib_Flow_Rate },
        { "input-shaping-damp", CalibMode::Calib_Input_shaping_damp },
        { "input-shaping-freq", CalibMode::Calib_Input_shaping_freq },
        { "max-volumetric-speed", CalibMode::Calib_Vol_speed_Tower },
        { "pa-line", CalibMode::Calib_PA_Line },
        { "pa-pattern", CalibMode::Calib_PA_Pattern },
        { "pa-tower", CalibMode::Calib_PA_Tower },
        { "retraction", CalibMode::Calib_Retraction_tower },
        { "temp-tower", CalibMode::Calib_Temp_Tower },
        { "vfa", CalibMode::Calib_VFA_Tower },
    };
    const auto it = kinds.find(kind);
    return it == kinds.end() ? std::nullopt : std::optional<CalibMode>(it->second);
}

class ClassicCalibParams
{
public:
    explicit ClassicCalibParams(const std::string &text)
    {
        std::vector<std::string> items;
        if (!text.empty())
            boost::split(items, text, boost::is_any_of(","));
        for (const std::string &item : items) {
            const size_t eq = item.find('=');
            if (eq == std::string::npos || eq == 0)
                throw std::runtime_error("--calib-params expects key=value, got " + item);
            m_values[item.substr(0, eq)] = item.substr(eq + 1);
        }
    }
    double number(const std::string &key, double fallback)
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return fallback;
        m_used.push_back(key);
        char *end = nullptr;
        const double v = std::strtod(it->second.c_str(), &end);
        if (it->second.empty() || *end != '\0')
            throw std::runtime_error("--calib-params " + key + " expects a number, got " + it->second);
        return v;
    }
    std::vector<double> numbers(const std::string &key)
    {
        std::vector<double> out;
        const std::string list = text(key, "");
        std::vector<std::string> items;
        if (!list.empty())
            boost::split(items, list, boost::is_any_of(";"));
        for (const std::string &item : items) {
            char *end = nullptr;
            out.push_back(std::strtod(item.c_str(), &end));
            if (item.empty() || *end != '\0')
                throw std::runtime_error("--calib-params " + key + " expects numbers separated by ';', got " + list);
        }
        return out;
    }
    std::string text(const std::string &key, const std::string &fallback)
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return fallback;
        m_used.push_back(key);
        return it->second;
    }
    void check_all_used(const std::string &kind) const
    {
        for (const auto &[key, value] : m_values)
            if (std::find(m_used.begin(), m_used.end(), key) == m_used.end())
                throw std::runtime_error("--calib-params: " + kind + " has no parameter " + key);
    }

private:
    std::map<std::string, std::string> m_values;
    std::vector<std::string>           m_used;
};

template<class T, class Option> void set_all_values(DynamicPrintConfig &config, const std::string &key, T value)
{
    if (auto *option = config.option<Option>(key))
        for (auto &v : option->values)
            v = value;
}

void cap_speed_at_max_volumetric(DynamicPrintConfig &config, const std::string &key)
{
    const auto *nozzles = config.option<ConfigOptionFloats>("nozzle_diameter");
    const auto &extruder_types = config.option<ConfigOptionEnumsGeneric>("extruder_type")->values;
    const auto &volume_types   = config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    const float nozzle         = float(nozzles->values[0]);
    float       line_width     = float(config.get_abs_value("line_width", nozzle));
    if (line_width <= 0.f)
        line_width = Flow::auto_extrusion_width(frPerimeter, nozzle);
    const Flow flow(line_width, float(config.opt_float("layer_height")), nozzle);
    for (size_t i = 0; i < nozzles->size(); ++i) {
        const auto extruder_type = ExtruderType(extruder_types[std::min(i, extruder_types.size() - 1)]);
        const auto volume_type   = NozzleVolumeType(volume_types[std::min(i, volume_types.size() - 1)]);
        int index = get_index_for_extruder_parameter(config, "filament_max_volumetric_speed", int(i), extruder_type, volume_type);
        const double max_speed = config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(index) / flow.mm3_per_mm();
        index = get_index_for_extruder_parameter(config, key, int(i), extruder_type, volume_type);
        config.option<ConfigOptionFloatsNullable>(key)->values[index] = max_speed;
    }
}

ModelObject *add_calib_model(Model &model, const DynamicPrintConfig &config, const std::string &file)
{
    const std::string path = resources_dir() + "/calib/" + file;
    Model loaded = Model::read_from_file(path, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
    if (loaded.objects.empty())
        throw std::runtime_error("no objects in " + path);
    ModelObject *object = model.add_object(*loaded.objects.front());
    object->center_around_origin();
    object->ensure_on_bed();
    const BoundingBoxf bed(config.opt<ConfigOptionPoints>("printable_area")->values);
    object->instances.front()->set_offset(Vec3d(bed.center().x(), bed.center().y(), object->instances.front()->get_offset().z()));
    return object;
}

void cut_calib_model(Model &model, size_t index, double z, ModelObjectCutAttribute keep = ModelObjectCutAttribute::KeepLower)
{
    ModelObject *object = model.objects[index];
    Cut cut(object, 0, Geometry::translation_transform(z * Vec3d::UnitZ() - object->instances.front()->get_offset()), keep);
    const ModelObjectPtrs &pieces = cut.perform_with_plane();
    if (pieces.empty())
        throw std::runtime_error("calibration model cut produced nothing");
    ModelObject *lower = model.add_object(*pieces.front());
    lower->ensure_on_bed();
    model.delete_object(index);
    std::rotate(model.objects.begin() + index, model.objects.end() - 1, model.objects.end());
}

void pa_common_settings(DynamicPrintConfig &config)
{
    config.set_key_value("overhang_reverse", new ConfigOptionBool(false));
    config.set_key_value("precise_z_height", new ConfigOptionBool(false));
    config.set_key_value("adaptive_layer_height_error", new ConfigOptionFloat(0));
    config.set_key_value("overhang_layer_contact", new ConfigOptionPercent(0));
    config.set_key_value("wipe_inward", new ConfigOptionBool(false));
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
}

bool is_bowden(const DynamicPrintConfig &config)
{
    const auto *types = config.option<ConfigOptionEnumsGeneric>("extruder_type");
    return types != nullptr && !types->values.empty() && ExtruderType(types->values.front()) == etBowden;
}

void pa_range(Calib_Params &params, ClassicCalibParams &in, const DynamicPrintConfig &config, double end, double step,
              double bowden_step, const char *kind)
{
    params.start = in.number("start", 0.);
    params.end   = in.number("end", is_bowden(config) ? 1.0 : end);
    params.step  = in.number("step", is_bowden(config) ? bowden_step : step);
    if (params.start < 0 || params.step < 10 * EPSILON || params.end < params.start + params.step)
        throw std::runtime_error(std::string(kind) + ": start >= 0, end > start + step and step >= 0.001");
}

void calib_pa_line(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    pa_range(params, in, config, 0.1, 0.002, 0.02, "pa-line");
    params.print_numbers = in.number("print_numbers", 1.) != 0.;
    pa_common_settings(config);
    add_calib_model(model, config, "pressure_advance/pressure_advance_test.drc");
}

bool has_junction_deviation(const DynamicPrintConfig &config)
{
    const auto *flavor   = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
    const auto *junction = config.option<ConfigOptionFloats>("machine_max_junction_deviation");
    return flavor && flavor->value == GCodeFlavor::gcfMarlinFirmware && junction && !junction->values.empty() &&
           junction->values.front() > 0.0;
}

void calib_pa_pattern(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    pa_range(params, in, config, 0.08, 0.005, 0.05, "pa-pattern");
    params.print_numbers = true;
    params.speeds        = in.numbers("speeds");
    params.accelerations = in.numbers("accelerations");
    if (!params.accelerations.empty() && !params.speeds.empty() &&
        *std::min_element(params.accelerations.begin(), params.accelerations.end()) <=
            *std::max_element(params.speeds.begin(), params.speeds.end()))
        throw std::runtime_error("pa-pattern: acceleration values must be greater than speed values");
    pa_common_settings(config);
    std::vector<double> speeds = params.speeds, accels = params.accelerations;
    const double nozzle = config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    set_all_values<bool, ConfigOptionBoolsNullable>(config, "filament_retract_when_changing_layer", false);
    set_all_values<bool, ConfigOptionBoolsNullable>(config, "filament_wipe", false);
    set_all_values<bool, ConfigOptionBools>(config, "wipe", false);
    set_all_values<bool, ConfigOptionBools>(config, "retract_when_changing_layer", false);
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    double accel = config.get_abs_value_at("outer_wall_acceleration", params.extruder_id);
    if (accel == 0)
        accel = config.get_abs_value_at("inner_wall_acceleration", params.extruder_id);
    if (accel == 0)
        accel = config.get_abs_value_at("default_acceleration", params.extruder_id);
    if (accels.empty())
        accels.assign({ accel });
    else
        accel = *std::max_element(accels.begin(), accels.end());
    set_all_values<double, ConfigOptionFloatsNullable>(config, "outer_wall_acceleration", accel);
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));
    if (!has_junction_deviation(config) && config.get_abs_value_at("default_jerk", params.extruder_id) > 0) {
        double jerk = config.get_abs_value_at("outer_wall_jerk", params.extruder_id);
        if (jerk == 0)
            jerk = config.get_abs_value_at("inner_wall_jerk", params.extruder_id);
        if (jerk == 0)
            jerk = config.get_abs_value_at("default_jerk", params.extruder_id);
        for (const char *key : { "default_jerk", "outer_wall_jerk", "inner_wall_jerk", "top_surface_jerk", "infill_jerk", "travel_jerk" })
            set_all_values<double, ConfigOptionFloatsNullable>(config, key, jerk);
    }
    if (has_junction_deviation(config))
        set_all_values<double, ConfigOptionFloatsNullable>(config, "default_junction_deviation", 0.);
    const SuggestedConfigCalibPAPattern suggested;
    for (const auto &[key, values] : suggested.floats_pairs)
        set_all_values<double, ConfigOptionFloatsNullable>(config, key, values[0]);
    for (const auto &[key, ratio] : suggested.nozzle_ratio_pairs)
        config.set_key_value(key, new ConfigOptionFloatOrPercent(nozzle * ratio / 100, false));
    for (const auto &[key, value] : suggested.int_pairs)
        config.set_key_value(key, new ConfigOptionInt(value));
    config.set_key_value(suggested.brim_pair.first, new ConfigOptionEnum<BrimType>(suggested.brim_pair.second));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    if (speeds.empty()) {
        const double speed = CalibPressureAdvance::find_optimal_PA_speed(config, config.get_abs_value("line_width", nozzle),
                                                                         config.get_abs_value("layer_height"), 0, 0);
        set_all_values<double, ConfigOptionFloatsNullable>(config, "outer_wall_speed", speed);
        speeds.assign({ speed });
    } else if (speeds.size() == 1)
        set_all_values<double, ConfigOptionFloatsNullable>(config, "outer_wall_speed", speeds.front());

    ModelObject *cube = model.add_object();
    cube->add_volume(TriangleMesh(its_make_cube(10., 10., 10.)));
    cube->name = "Cube";
    cube->add_instance();
    const bool is_bbl = config.opt_string("printer_model").rfind("Bambu Lab", 0) == 0;
    CalibPressureAdvancePattern pattern(params, config, is_bbl, *cube, Vec3d::Zero());
    BoundingBoxf3 box = cube->raw_bounding_box();
    cube->scale((pattern.print_size_x() + 4) / box.size().x(), (pattern.print_size_y() + 4) / box.size().y(),
                pattern.max_layer_z() / box.size().z());
    arrangement::ArrangePolygons items;
    arrangement::ArrangeParams arrange_params;
    const Points bed = arrangement::get_shrink_bedpts(&config, arrange_params);
    for (size_t i = 0; i < speeds.size() * accels.size(); ++i) {
        arrangement::ArrangePolygon item;
        cube->instances[0]->get_arrange_polygon(&item);
        item.bed_idx = 0;
        items.emplace_back(item);
    }
    arrangement::arrange(items, bed, arrange_params);
    if (std::any_of(items.begin(), items.end(), [](const auto &item) { return item.bed_idx != 0; }))
        throw std::runtime_error("pa-pattern: " + std::to_string(items.size()) + " tests do not fit on one plate");
    box = cube->raw_bounding_box();
    cube->scale(pattern.handle_xy_size() / box.size().x(), pattern.handle_xy_size() / box.size().y(), pattern.max_layer_z() / box.size().z());
    for (size_t i = 0; i < items.size(); ++i) {
        const double speed = speeds[i % speeds.size()], acceleration = accels[i / speeds.size()];
        ModelObject *object = i == 0 ? cube : model.add_object(*cube);
        object->name = "pa_pattern_" + std::to_string(int(speed)) + "_" + std::to_string(int(acceleration));
        if (speeds.size() > 1)
            object->config.set_key_value("outer_wall_speed", new ConfigOptionFloatsNullable(1, speed));
        if (accels.size() > 1)
            object->config.set_key_value("outer_wall_acceleration", new ConfigOptionFloatsNullable(1, acceleration));
        object->instances[0]->set_offset(Vec3d(unscale<double>(items[i].translation.x()), unscale<double>(items[i].translation.y()), 0.) +
                                         pattern.handle_pos_offset());
        object->ensure_on_bed();
    }
    model.calib_pa_pattern = std::make_unique<CalibPressureAdvancePattern>(pattern);
}

std::vector<double> flowrate_speeds(const DynamicPrintConfig &config, const std::string &key, bool linear, int pass)
{
    const auto *nozzles = config.option<ConfigOptionFloats>("nozzle_diameter");
    const auto &extruder_types = config.option<ConfigOptionEnumsGeneric>("extruder_type")->values;
    const auto &volume_types   = config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    const float nozzle = float(nozzles->values[0]);
    const Flow  flow(float(config.get_abs_value("line_width", nozzle)), float(config.opt_float("layer_height")), nozzle);
    std::vector<double> out;
    for (size_t i = 0; i < nozzles->size(); ++i) {
        const auto extruder_type = ExtruderType(extruder_types[std::min(i, extruder_types.size() - 1)]);
        const auto volume_type   = NozzleVolumeType(volume_types[std::min(i, volume_types.size() - 1)]);
        int index = get_index_for_extruder_parameter(config, "filament_max_volumetric_speed", int(i), extruder_type, volume_type);
        const double max_volumetric = config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(index);
        const double flowrate       = config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(index);
        const double max_speed = linear ? max_volumetric / (flow.mm3_per_mm() * (flowrate + (pass == 2 ? 0.035 : 0.05)) / flowrate) :
                                          max_volumetric / (flow.mm3_per_mm() * (pass == 1 ? 1.2 : 1));
        index = get_index_for_extruder_parameter(config, key, int(i), extruder_type, volume_type);
        const double speed = std::floor(std::min(config.option<ConfigOptionFloatsNullable>(key)->values[index], max_speed));
        out.insert(out.end(), volume_types.size(), speed);
    }
    return out;
}

void calib_flow_rate(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    const std::string type = in.text("type", "yolo"), pattern_name = in.text("pattern", "archimedean");
    const int pass = int(in.number("pass", 1));
    if ((type != "classic" && type != "yolo") || (pass != 1 && pass != 2) || (pattern_name != "archimedean" && pattern_name != "monotonic"))
        throw std::runtime_error("flow-rate: type classic|yolo, pass 1|2, pattern archimedean|monotonic");
    const bool linear = type == "yolo";
    const InfillPattern pattern = pattern_name == "monotonic" ? ipMonotonic : ipArchimedeanChords;
    const std::string file = linear ? (pass == 1 ? "Orca-LinearFlow.3mf" : "Orca-LinearFlow_fine.3mf") :
                                      (pass == 1 ? "flowrate-test-pass1.3mf" : "flowrate-test-pass2.3mf");
    const std::string path = resources_dir() + "/calib/filament_flow/" + file;
    Model loaded = Model::read_from_file(path, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);
    if (loaded.objects.empty())
        throw std::runtime_error("no objects in " + path);
    const float nozzle = float(config.option<ConfigOptionFloats>("nozzle_diameter")->values[0]);
    const float xy_scale = nozzle / 0.6f;
    const double layer_height = nozzle / 2.0;
    const double first_layer = std::max(config.opt_float("initial_layer_print_height"), layer_height);
    const Vec3d scale(xy_scale > 1.2f ? xy_scale : 1., xy_scale > 1.2f ? xy_scale : 1., (first_layer + 9 * layer_height) / 2);
    BoundingBoxf3 group;
    for (const ModelObject *object : loaded.objects)
        group.merge(object->instance_bounding_box(0));
    const BoundingBoxf bed(config.opt<ConfigOptionPoints>("printable_area")->values);
    const Vec3d centre(group.center().x(), group.center().y(), 0.), target(bed.center().x(), bed.center().y(), 0.);
    const double flowrate = config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
    const std::vector<double> solid_speeds = flowrate_speeds(config, "internal_solid_infill_speed", linear, pass);
    const std::vector<double> top_speeds   = flowrate_speeds(config, "top_surface_speed", linear, pass);
    for (const ModelObject *source : loaded.objects) {
        ModelObject *object = model.add_object(*source);
        ModelInstance *instance = object->instances.front();
        instance->set_scaling_factor(instance->get_scaling_factor().cwiseProduct(scale));
        const Vec3d offset = instance->get_offset();
        instance->set_offset(target + (offset - centre).cwiseProduct(Vec3d(scale.x(), scale.y(), 1.)));
        object->invalidate_bounding_box();
        object->ensure_on_bed();
        ModelConfig &c = object->config;
        c.set_key_value("wall_loops", new ConfigOptionInt(1));
        c.set_key_value("only_one_wall_top", new ConfigOptionBool(true));
        c.set_key_value("thick_internal_bridges", new ConfigOptionBool(false));
        c.set_key_value("enable_extra_bridge_layer", new ConfigOptionEnum<EnableExtraBridgeLayer>(eblDisabled));
        c.set_key_value("internal_bridge_density", new ConfigOptionPercent(100));
        c.set_key_value("sparse_infill_density", new ConfigOptionPercent(35));
        c.set_key_value("min_width_top_surface", new ConfigOptionFloatOrPercent(100, true));
        c.set_key_value("bottom_shell_layers", new ConfigOptionInt(2));
        c.set_key_value("top_shell_layers", new ConfigOptionInt(5));
        c.set_key_value("top_shell_thickness", new ConfigOptionFloat(0));
        c.set_key_value("bottom_shell_thickness", new ConfigOptionFloat(0));
        c.set_key_value("detect_thin_wall", new ConfigOptionBool(true));
        c.set_key_value("filter_out_gap_fill", new ConfigOptionFloat(0));
        c.set_key_value("sparse_infill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        c.set_key_value("top_surface_line_width", new ConfigOptionFloatOrPercent(nozzle * 1.2f, false));
        c.set_key_value("internal_solid_infill_line_width", new ConfigOptionFloatOrPercent(nozzle * 1.2f, false));
        c.set_key_value("top_surface_pattern", new ConfigOptionEnum<InfillPattern>(pattern));
        const auto *top_flow = dynamic_cast<const ConfigOptionFloatsNullable *>(c.option("top_solid_infill_flow_ratio"));
        c.set_key_value("top_solid_infill_flow_ratio", new ConfigOptionFloatsNullable(top_flow ? top_flow->size() : 1, 1.0));
        c.set_key_value("infill_direction", new ConfigOptionFloat(45));
        c.set_key_value("solid_infill_direction", new ConfigOptionFloat(135));
        c.set_key_value("center_of_surface_pattern", new ConfigOptionEnum<CenterOfSurfacePattern>(CenterOfSurfacePattern::Each_Surface));
        c.set_key_value("separated_infills", new ConfigOptionBool(false));
        c.set_key_value("align_infill_direction_to_model", new ConfigOptionBool(true));
        c.set_key_value("ironing_type", new ConfigOptionEnum<IroningType>(IroningType::NoIroning));
        c.set_key_value("internal_solid_infill_speed", new ConfigOptionFloatsNullable(solid_speeds));
        c.set_key_value("top_surface_speed", new ConfigOptionFloatsNullable(top_speeds));
        c.set_key_value("seam_slope_type", new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));
        c.set_key_value("gap_fill_target", new ConfigOptionEnum<GapFillTarget>(GapFillTarget::gftNowhere));
        c.set_key_value("calib_flowrate_topinfill_special_order", new ConfigOptionBool(true));
        c.set_key_value("top_surface_fill_order", new ConfigOptionEnum<SurfaceFillOrder>(SurfaceFillOrder::Default));
        std::string modifier_text = object->name.size() > 9 ? object->name.substr(9) : std::string();
        if (!modifier_text.empty() && modifier_text[0] == 'm')
            modifier_text[0] = '-';
        double modifier = 1.0;
        try { modifier = std::stod(modifier_text); } catch (...) {}
        c.set_key_value("print_flow_ratio", new ConfigOptionFloat(linear ? (flowrate + modifier) / flowrate : 1.0 + modifier / 100.));
    }
    config.set_key_value("max_volumetric_extrusion_rate_slope", new ConfigOptionFloat(0));
    config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    config.set_key_value("alternate_extra_wall", new ConfigOptionBool(false));
    config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(first_layer));
    config.set_key_value("reduce_crossing_wall", new ConfigOptionBool(true));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    params.mode = CalibMode::Calib_None;
}

void calib_pa_tower(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    pa_range(params, in, config, 0.1, 0.002, 0.02, "pa-tower");
    pa_common_settings(config);
    ModelObject *tower = add_calib_model(model, config, "pressure_advance/tower_with_seam.drc");
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats(config.option<ConfigOptionFloats>("slow_down_layer_time")->size(), 1.0));
    ModelConfig &object = tower->config;
    object.set_key_value("alternate_extra_wall", new ConfigOptionBool(false));
    cap_speed_at_max_volumetric(config, "outer_wall_speed");
    cap_speed_at_max_volumetric(config, "inner_wall_speed");
    object.set_key_value("seam_position", new ConfigOptionEnum<SeamPosition>(spRear));
    object.set_key_value("wall_loops", new ConfigOptionInt(2));
    object.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    object.set_key_value("bottom_shell_layers", new ConfigOptionInt(0));
    object.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    object.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btEar));
    object.set_key_value("brim_object_gap", new ConfigOptionFloat(0.));
    object.set_key_value("brim_ears_max_angle", new ConfigOptionFloat(135.));
    object.set_key_value("brim_width", new ConfigOptionFloat(6.));
    object.set_key_value("seam_slope_type", new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));
    config.set_key_value("max_volumetric_extrusion_rate_slope", new ConfigOptionFloat(0));
    const double height = std::ceil((params.end - params.start) / params.step) + 1;
    if (height < tower->bounding_box_exact().size().z())
        cut_calib_model(model, 0, height);
}

std::pair<double, double> temp_range(const DynamicPrintConfig &config)
{
    const auto  *types = config.option<ConfigOptionStrings>("filament_type");
    const std::string type = types && !types->values.empty() ? types->values.front() : "PLA";
    if (type == "ABS" || type == "ASA") return { 270, 230 };
    if (type == "PETG") return { 250, 230 };
    if (type == "PCTG") return { 280, 240 };
    if (type == "TPU") return { 240, 210 };
    if (type == "PA-CF" || type == "PET-CF") return { 320, 280 };
    return { 230, 190 };
}

void calib_temp_tower(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    const auto [start, end] = temp_range(config);
    params.start = in.number("start", start);
    params.end   = in.number("end", end);
    params.nozzle_based_resize = in.number("resize", 1.) != 0.;
    if (params.start > 500 || params.end < 155 || params.end > params.start - 5)
        throw std::runtime_error("temp-tower: start <= 500, end >= 155 and start >= end + 5");
    constexpr double base_nozzle = 0.4, block_height = 10.0;
    constexpr int    temp_step   = 5;
    add_calib_model(model, config, "temperature_tower/temperature_tower.drc");
    double nozzle = config.option<ConfigOptionFloats>("nozzle_diameter")->values.front();
    if (nozzle <= 0.)
        nozzle = base_nozzle;
    const double nozzle_scale = nozzle / base_nozzle;
    long blocks = std::lround((500 - params.end) / temp_step + 1);
    if (blocks > 0 && blocks * block_height - EPSILON < model.objects[0]->bounding_box_exact().size().z())
        cut_calib_model(model, 0, blocks * block_height - EPSILON, ModelObjectCutAttribute::KeepLower);
    blocks = std::lround((500 - params.start) / temp_step);
    if (blocks > 0 && blocks * block_height + EPSILON < model.objects[0]->bounding_box_exact().size().z())
        cut_calib_model(model, 0, blocks * block_height + EPSILON, ModelObjectCutAttribute::KeepUpper);
    ModelObject *tower = model.objects[0];
    if (params.nozzle_based_resize && std::abs(nozzle_scale - 1.0) > EPSILON)
        tower->scale(nozzle_scale, nozzle_scale, nozzle_scale);
    tower->ensure_on_bed();
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    set_all_values<int, ConfigOptionInts>(config, "nozzle_temperature_initial_layer", int(std::lround(params.start)));
    set_all_values<int, ConfigOptionInts>(config, "nozzle_temperature", int(std::lround(params.start)));
    ModelConfig &c = tower->config;
    if (params.nozzle_based_resize)
        c.set_key_value("layer_height", new ConfigOptionFloat(nozzle / 2));
    c.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    c.set_key_value("brim_width", new ConfigOptionFloat(5.0));
    c.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));
    c.set_key_value("alternate_extra_wall", new ConfigOptionBool(false));
    c.set_key_value("seam_slope_type", new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));
    c.set_key_value("overhang_reverse", new ConfigOptionBool(false));
    c.set_key_value("precise_z_height", new ConfigOptionBool(false));
    c.set_key_value("adaptive_layer_height_error", new ConfigOptionFloat(0));
    c.set_key_value("overhang_layer_contact", new ConfigOptionPercent(0));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    if (params.nozzle_based_resize)
        config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(nozzle / 2));
}

void allow_layer_height(DynamicPrintConfig &config, double height)
{
    for (double &max : config.option<ConfigOptionFloats>("max_layer_height")->values)
        max = std::max(max, height);
}

void calib_max_vol_speed(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    params.start = in.number("start", 5.);
    params.end   = in.number("end", 20.);
    params.step  = in.number("step", 0.5);
    if (params.start <= 0 || params.step <= 0 || params.end < params.start + params.step)
        throw std::runtime_error("max-volumetric-speed: start > 0, step > 0 and end >= start + step");
    ModelObject *object = add_calib_model(model, config, "volumetric_speed/SpeedTestStructure.drc");
    const BoundingBoxf bed(config.opt<ConfigOptionPoints>("printable_area")->values);
    if (const double scale = (bed.size().x() - 10) / object->bounding_box_exact().size().x(); scale < 1.0)
        object->scale(scale, 1, 1);
    const double nozzle = config.option<ConfigOptionFloats>("nozzle_diameter")->values[0];
    const double line_width = nozzle * 1.75, layer_height = nozzle * 0.8;
    allow_layer_height(config, layer_height);
    set_all_values<double, ConfigOptionFloats>(config, "filament_max_volumetric_speed",
        std::max(config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(0), 200.0));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats(config.option<ConfigOptionFloats>("slow_down_layer_time")->size(), 0.0));
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    ModelConfig &c = object->config;
    c.set_key_value("enable_overhang_speed", new ConfigOptionBoolsNullable(1, false));
    c.set_key_value("wall_loops", new ConfigOptionInt(1));
    c.set_key_value("alternate_extra_wall", new ConfigOptionBool(false));
    c.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    c.set_key_value("bottom_shell_layers", new ConfigOptionInt(0));
    c.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    c.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(line_width, false));
    c.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    c.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterAndInner));
    c.set_key_value("brim_width", new ConfigOptionFloat(5.0));
    c.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));
    c.set_key_value("precise_z_height", new ConfigOptionBool(false));
    c.set_key_value("adaptive_layer_height_error", new ConfigOptionFloat(0));
    c.set_key_value("overhang_layer_contact", new ConfigOptionPercent(0));
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    config.set_key_value("max_volumetric_extrusion_rate_slope", new ConfigOptionFloat(0));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    if (const double height = (params.end - params.start + 1) / params.step; height < object->bounding_box_exact().size().z())
        cut_calib_model(model, 0, height);
    const double mm3_per_mm = Flow(float(line_width), float(layer_height), float(nozzle)).mm3_per_mm() *
                              config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
    params.start /= mm3_per_mm;
    params.end /= mm3_per_mm;
    params.step /= mm3_per_mm;
}

void calib_retraction(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    params.start = in.number("start", 0.);
    params.end   = in.number("end", 2.);
    params.step  = in.number("step", 0.1);
    if (params.start < 0 || params.step <= 0 || params.end < params.start + params.step)
        throw std::runtime_error("retraction: start >= 0, step > 0 and end >= start + step");
    ModelObject *object = add_calib_model(model, config, "retraction/retraction_tower.drc");
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    config.set_key_value("wipe_inward", new ConfigOptionBool(false));
    const float nozzle = float(config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0));
    const float layer_height = nozzle <= 0.1f ? 0.05f : nozzle <= 0.2f ? 0.1f : 0.2f;
    allow_layer_height(config, layer_height);
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    config.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));
    ModelConfig &c = object->config;
    c.set_key_value("wall_loops", new ConfigOptionInt(2));
    c.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    c.set_key_value("bottom_shell_layers", new ConfigOptionInt(3));
    c.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
    c.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    c.set_key_value("alternate_extra_wall", new ConfigOptionBool(false));
    c.set_key_value("seam_position", new ConfigOptionEnum<SeamPosition>(spAligned));
    c.set_key_value("wall_sequence", new ConfigOptionEnum<WallSequence>(WallSequence::InnerOuter));
    c.set_key_value("overhang_reverse", new ConfigOptionBool(false));
    c.set_key_value("precise_z_height", new ConfigOptionBool(false));
    c.set_key_value("adaptive_layer_height_error", new ConfigOptionFloat(0));
    c.set_key_value("overhang_layer_contact", new ConfigOptionPercent(0));
    c.set_key_value("seam_slope_type", new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));
    if (const double height = 1.0 + 0.4 + (params.end - params.start) / params.step - EPSILON; height < object->bounding_box_exact().size().z())
        cut_calib_model(model, 0, height);
}

void calib_vfa(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    params.start = in.number("start", 40.);
    params.end   = in.number("end", 200.);
    params.step  = in.number("step", 10.);
    params.nozzle_based_resize = in.number("resize", 1.) != 0.;
    params.vfa_layer_height    = in.number("layer_height", 0.);
    if (params.start <= 10 || params.step <= 0 || params.end < params.start + params.step)
        throw std::runtime_error("vfa: start > 10, step > 0 and end >= start + step");
    add_calib_model(model, config, "vfa/vfa.drc");
    double nozzle = config.option<ConfigOptionFloats>("nozzle_diameter")->values.front();
    if (nozzle <= 0.)
        nozzle = vfa_base_nozzle_diameter;
    const double layer_height = params.vfa_layer_height > 0. ? params.vfa_layer_height : nozzle / 2.0;
    if (const double height = vfa_base_block_height * ((params.end - params.start) / params.step + 1) - EPSILON;
        height < model.objects[0]->bounding_box_exact().size().z())
        cut_calib_model(model, 0, height);
    ModelObject *tower = model.objects[0];
    if (params.nozzle_based_resize) {
        const double xy = nozzle / vfa_base_nozzle_diameter, z = vfa_layers_per_block * layer_height / vfa_base_block_height;
        if (std::abs(xy - 1.0) > EPSILON || std::abs(z - 1.0) > EPSILON)
            tower->scale(xy, xy, z);
    }
    tower->ensure_on_bed();
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats{ 0.0 });
    set_all_values<bool, ConfigOptionBoolsNullable>(config, "enable_overhang_speed", false);
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    config.set_key_value("wall_loops", new ConfigOptionInt(1));
    config.set_key_value("alternate_extra_wall", new ConfigOptionBool(false));
    config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    config.set_key_value("detect_thin_wall", new ConfigOptionBool(false));
    config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    config.set_key_value("precise_z_height", new ConfigOptionBool(false));
    config.set_key_value("adaptive_layer_height_error", new ConfigOptionFloat(0));
    config.set_key_value("overhang_layer_contact", new ConfigOptionPercent(0));
    if (params.nozzle_based_resize) {
        config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
        tower->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    }
    tower->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    tower->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    tower->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));
    params.vfa_layer_height = params.nozzle_based_resize ? layer_height : 0.0;
}

void zero_pressure_advance_where_disabled(DynamicPrintConfig &config)
{
    auto *enable = config.option<ConfigOptionBools>("enable_pressure_advance");
    auto *pa = config.option<ConfigOptionFloats>("pressure_advance");
    auto *adaptive = config.option<ConfigOptionBools>("adaptive_pressure_advance");
    if (!enable || !pa || !adaptive || pa->empty() || adaptive->empty())
        return;
    if (pa->size() < enable->size()) pa->resize(enable->size());
    if (adaptive->size() < enable->size()) adaptive->resize(enable->size());
    for (size_t i = 0; i < enable->size(); ++i)
        if (!enable->get_at(i)) {
            enable->values[i] = true;
            pa->values[i] = 0.0;
            adaptive->values[i] = false;
        }
}

ModelObject *ringing_test(Model &model, DynamicPrintConfig &config, int test_model)
{
    ModelObject *object = add_calib_model(model, config, test_model == 0 ? "input_shaping/ringing_tower.drc" :
                                                         test_model == 1 ? "input_shaping/fast_tower_test.drc" : "cornering/SCV-V2.drc");
    zero_pressure_advance_where_disabled(config);
    config.set_key_value("resonance_avoidance", new ConfigOptionBool(false));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats{ 0.0 });
    config.set_key_value("slow_down_min_speed", new ConfigOptionFloats{ 0.0 });
    config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools{ false });
    set_all_values<bool, ConfigOptionBools>(config, "enable_overhang_speed", false);
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    config.set_key_value("wall_loops", new ConfigOptionInt(1));
    config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    config.set_key_value("detect_thin_wall", new ConfigOptionBool(false));
    config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    config.set_key_value("spiral_mode_smooth", new ConfigOptionBool(false));
    config.set_key_value("bottom_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    const double max_speed = std::min(config.option<ConfigOptionFloats>("machine_max_speed_x")->get_at(0),
                                      config.option<ConfigOptionFloats>("machine_max_speed_y")->get_at(0));
    const double max_acceleration = config.option<ConfigOptionFloats>("machine_max_acceleration_extruding")->get_at(0);
    set_all_values<double, ConfigOptionFloatsNullable>(config, "outer_wall_speed", max_speed);
    set_all_values<double, ConfigOptionFloatsNullable>(config, "default_acceleration", max_acceleration);
    set_all_values<double, ConfigOptionFloatsNullable>(config, "outer_wall_acceleration", max_acceleration);
    config.set_key_value("precise_z_height", new ConfigOptionBool(false));
    config.set_key_value("adaptive_layer_height_error", new ConfigOptionFloat(0));
    config.set_key_value("overhang_layer_contact", new ConfigOptionPercent(0));
    object->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    object->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    object->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));
    return object;
}

void input_shaping_cornering(DynamicPrintConfig &config)
{
    if (has_junction_deviation(config)) {
        config.set_key_value("machine_max_junction_deviation",
                             new ConfigOptionFloats{ std::max(config.option<ConfigOptionFloats>("machine_max_junction_deviation")->values.front(), 0.25) });
        set_all_values<double, ConfigOptionFloatsNullable>(config, "default_junction_deviation", 0.);
    } else {
        const double jerk = config.opt_enum<GCodeFlavor>("gcode_flavor") == gcfKlipper ? 5.0 : 10.0;
        for (const char *key : { "machine_max_jerk_x", "machine_max_jerk_y" })
            config.set_key_value(key, new ConfigOptionFloats{ std::max(config.option<ConfigOptionFloats>(key)->values.front(), jerk) });
        set_all_values<double, ConfigOptionFloatsNullable>(config, "default_jerk", 0.);
    }
    config.set_key_value("input_shaping_emit", new ConfigOptionBool(false));
}

std::string shaper_type(const DynamicPrintConfig &config, ClassicCalibParams &in)
{
    const ConfigOptionDef *def = print_config_def.get("input_shaping_type");
    const GCodeFlavor flavor = config.opt_enum<GCodeFlavor>("gcode_flavor");
    std::vector<InputShaperType> types =
        flavor == gcfKlipper ? std::vector<InputShaperType>{ InputShaperType::Default, InputShaperType::ZV, InputShaperType::MZV, InputShaperType::ZVD,
                                                             InputShaperType::EI, InputShaperType::TwoHumpEI, InputShaperType::ThreeHumpEI } :
        flavor == gcfRepRapFirmware ? std::vector<InputShaperType>{ InputShaperType::Default, InputShaperType::MZV, InputShaperType::ZVD, InputShaperType::ZVDD,
                                                                    InputShaperType::ZVDDD, InputShaperType::EI2, InputShaperType::EI3, InputShaperType::DAA } :
        flavor == gcfMarlinFirmware ? std::vector<InputShaperType>{ InputShaperType::ZV } : std::vector<InputShaperType>{ InputShaperType::Default };
    std::vector<std::string> values;
    for (InputShaperType type : types)
        values.push_back(def->enum_values[size_t(type)]);
    const std::string chosen = in.text("shaper", values.front());
    if (std::find(values.begin(), values.end(), chosen) == values.end())
        throw std::runtime_error("shaper " + chosen + " is not one this printer's firmware offers");
    return chosen;
}

void calib_input_shaping_freq(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    const bool reprap = config.opt_enum<GCodeFlavor>("gcode_flavor") == gcfRepRapFirmware;
    params.freqStartX = in.number("freq_start_x", 15.);
    params.freqEndX   = in.number("freq_end_x", 110.);
    params.freqStartY = reprap ? params.freqStartX : in.number("freq_start_y", 15.);
    params.freqEndY   = reprap ? params.freqEndX : in.number("freq_end_y", 110.);
    params.start      = in.number("damping", 0.15);
    params.test_model = int(in.number("model", 0.)) == 0 ? 0 : 1;
    if (params.freqStartX < 0 || params.freqEndX > 500 || params.freqStartY < 0 || params.freqEndY > 500 ||
        params.freqStartX >= params.freqEndX || params.freqStartY >= params.freqEndY)
        throw std::runtime_error("input-shaping-freq: 0 < freq start < freq end < 500");
    if (params.start < 0 || params.start >= 1)
        throw std::runtime_error("input-shaping-freq: 0 <= damping < 1");
    params.shaper_type = shaper_type(config, in);
    input_shaping_cornering(config);
    config.set_key_value("layer_height", new ConfigOptionFloat(0.2));
    ringing_test(model, config, params.test_model);
}

void calib_input_shaping_damp(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    const bool reprap = config.opt_enum<GCodeFlavor>("gcode_flavor") == gcfRepRapFirmware;
    params.freqStartX = in.number("freq_x", 30.);
    params.freqStartY = reprap ? params.freqStartX : in.number("freq_y", 30.);
    params.start      = in.number("damping_start", 0.);
    params.end        = in.number("damping_end", 0.4);
    params.test_model = int(in.number("model", 0.)) == 0 ? 0 : 1;
    if (params.freqStartX < 0 || params.freqStartX > 500 || params.freqStartY < 0 || params.freqStartY > 500)
        throw std::runtime_error("input-shaping-damp: 0 < freq < 500");
    if (params.start < 0 || params.end > 1 || params.start >= params.end)
        throw std::runtime_error("input-shaping-damp: 0 <= damping start < damping end <= 1");
    params.shaper_type = shaper_type(config, in);
    input_shaping_cornering(config);
    ringing_test(model, config, params.test_model);
}

void calib_cornering(Model &model, DynamicPrintConfig &config, Calib_Params &params, ClassicCalibParams &in)
{
    const bool junction = has_junction_deviation(config);
    params.start = in.number("start", junction ? 0.0 : 1.0);
    params.end   = in.number("end", junction ? 0.25 : 15.0);
    params.test_model = int(in.number("model", 0.));
    const double max_end = junction ? 0.3 : 100.0;
    if (params.start < 0 || params.end > max_end || params.start >= params.end || params.test_model < 0 || params.test_model > 2)
        throw std::runtime_error("cornering: 0 <= start < end <= " + std::string(junction ? "0.3 mm" : "100 mm/s") + ", model 0, 1 or 2");
    if (junction) {
        config.set_key_value("machine_max_junction_deviation", new ConfigOptionFloats{ params.end });
        set_all_values<double, ConfigOptionFloatsNullable>(config, "default_junction_deviation", 0.);
    } else {
        config.set_key_value("machine_max_jerk_x", new ConfigOptionFloats{ params.end });
        config.set_key_value("machine_max_jerk_y", new ConfigOptionFloats{ params.end });
        set_all_values<double, ConfigOptionFloatsNullable>(config, "default_jerk", 0.);
    }
    config.set_key_value("input_shaping_emit", new ConfigOptionBool(true));
    config.set_key_value("input_shaping_type", new ConfigOptionEnum<InputShaperType>(InputShaperType::Disable));
    config.set_key_value("filament_max_volumetric_speed",
                         new ConfigOptionFloats{ std::max(config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(0), 200.0) });
    ringing_test(model, config, params.test_model);
}

Calib_Params classic_calib(const Args &args, Model &model, DynamicPrintConfig &config)
{
    if (config.has("belt_printer") && config.opt_bool("belt_printer"))
        throw std::runtime_error("--calib " + args.calib + ": belt printers are not supported");
    Calib_Params params;
    params.mode = *classic_calib_mode(args.calib);
    ClassicCalibParams in(args.calib_params);
    config.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonplanarMode>(NonplanarMode::Off));
    switch (params.mode) {
    case CalibMode::Calib_Flow_Rate: calib_flow_rate(model, config, params, in); break;
    case CalibMode::Calib_PA_Line: calib_pa_line(model, config, params, in); break;
    case CalibMode::Calib_PA_Pattern: calib_pa_pattern(model, config, params, in); break;
    case CalibMode::Calib_PA_Tower: calib_pa_tower(model, config, params, in); break;
    case CalibMode::Calib_Temp_Tower: calib_temp_tower(model, config, params, in); break;
    case CalibMode::Calib_VFA_Tower: calib_vfa(model, config, params, in); break;
    case CalibMode::Calib_Input_shaping_freq: calib_input_shaping_freq(model, config, params, in); break;
    case CalibMode::Calib_Input_shaping_damp: calib_input_shaping_damp(model, config, params, in); break;
    case CalibMode::Calib_Cornering: calib_cornering(model, config, params, in); break;
    case CalibMode::Calib_Vol_speed_Tower: calib_max_vol_speed(model, config, params, in); break;
    case CalibMode::Calib_Retraction_tower: calib_retraction(model, config, params, in); break;
    default: throw std::runtime_error("--calib " + args.calib + " is not implemented");
    }
    in.check_all_used(args.calib);
    return params;
}

void finish_classic_calib(Model &model, const DynamicPrintConfig &config)
{
    if (!model.calib_pa_pattern)
        return;
    const bool is_bbl = config.opt_string("printer_model").rfind("Bambu Lab", 0) == 0;
    CustomGCode::Info &plate = model.plates_custom_gcodes[0];
    for (size_t i = 0; i < model.objects.size(); ++i) {
        CustomGCode::Info gcode = model.calib_pa_pattern->generate_custom_gcodes(config, is_bbl, *model.objects[i], Vec3d::Zero());
        if (i == 0)
            plate = std::move(gcode);
        else
            for (size_t layer = 0; layer < plate.gcodes.size(); ++layer)
                plate.gcodes[layer].extra += gcode.gcodes[layer].extra;
    }
}

}
