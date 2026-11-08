#include "BeltPurgeTower.hpp"

#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "PartPlate.hpp"
#include "I18N.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/FilamentMixer.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

#include <boost/log/trivial.hpp>
#include <cstddef>
#include <math.h>
#include <string>
#include <utility>

namespace Slic3r {
namespace GUI {

bool ensure_belt_purge_tower(Model &model, PartPlateList &partplate_list, ObjectList *obj_list, std::vector<BeltPurgeSignature> &sigs)
{
    auto is_prism = [](const ModelObject *mo) {
        const ConfigOption *opt = mo->config.option("belt_purge_tower_object");
        return opt != nullptr && opt->getBool();
    };

    const int plate_count = partplate_list.get_plate_count();
    sigs.resize(size_t(plate_count));
    std::vector<std::vector<int>> prisms_by_plate(static_cast<size_t>(plate_count));
    std::vector<int>              stale_prisms;
    for (int i = 0; i < (int) model.objects.size(); ++i)
        if (is_prism(model.objects[i])) {
            const int plate_idx = model.objects[i]->instances.empty() ? -1 : partplate_list.find_instance(i, 0);
            if (plate_idx >= 0 && plate_idx < plate_count)
                prisms_by_plate[size_t(plate_idx)].push_back(i);
            else
                stale_prisms.push_back(i);
        }

    auto remove_prisms = [&](std::vector<int> idxs) {
        std::sort(idxs.begin(), idxs.end());
        for (auto it = idxs.rbegin(); it != idxs.rend(); ++it) {
            model.delete_object(size_t(*it));
            partplate_list.notify_instance_removed(*it, -1);
            obj_list->delete_object_from_list(size_t(*it));
        }
    };
    auto all_prisms = [&]() {
        std::vector<int> all = stale_prisms;
        for (const auto &v : prisms_by_plate)
            all.insert(all.end(), v.begin(), v.end());
        return all;
    };

    {
        const auto *belt_pre = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionBool>("belt_printer");
        if (belt_pre == nullptr || !belt_pre->value) {
            std::fill(sigs.begin(), sigs.end(), BeltPurgeSignature{});
            const std::vector<int> all = all_prisms();
            if (all.empty())
                return false;
            remove_prisms(all);
            return true;
        }
    }

    const DynamicPrintConfig  full_cfg       = wxGetApp().preset_bundle->full_config();
    const DynamicPrintConfig &printer_config = full_cfg;
    const DynamicPrintConfig &print_config   = full_cfg;
    const DynamicPrintConfig &project_config = full_cfg;

    const auto *belt_opt = printer_config.option<ConfigOptionBool>("belt_printer");
    const bool  belt     = belt_opt != nullptr && belt_opt->value;
    const bool  prime_tower_enabled = printer_config.has("enable_belt_purge_tower") && printer_config.opt_bool("enable_belt_purge_tower");
    const auto *seq_opt   = print_config.option<ConfigOptionEnum<PrintSequence>>("print_sequence");
    const bool  by_object = seq_opt != nullptr && seq_opt->value == PrintSequence::ByObject;

    std::vector<unsigned char> is_mixed;
    std::vector<std::string>   comp_strs;
    if (const auto *o = full_cfg.option<ConfigOptionBools>("filament_is_mixed"))
        is_mixed = o->values;
    if (const auto *o = full_cfg.option<ConfigOptionStrings>("filament_mixed_components"))
        comp_strs = o->values;

    const double gap     = 1.0;
    const double layer_h = print_config.has("layer_height") ? print_config.opt_float("layer_height") : 0.2;

    const auto *axis_opt  = printer_config.option<ConfigOptionEnum<BeltRotationAxis>>("belt_slice_rotation");
    const auto *angle_opt = printer_config.option<ConfigOptionFloat>("belt_slice_rotation_angle");
    const BeltRotationAxis rot = axis_opt != nullptr ? axis_opt->value : BeltRotationAxis::X;
    const bool   belt_is_y = (rot != BeltRotationAxis::Y);
    double       theta     = M_PI / 2.;
    if ((rot == BeltRotationAxis::X || rot == BeltRotationAxis::Y) && angle_opt != nullptr && std::abs(angle_opt->value) > EPSILON)
        theta = std::clamp(Geometry::deg2rad(std::abs(angle_opt->value)), Geometry::deg2rad(5.), M_PI / 2.);
    const double sin_t = std::sin(theta);
    const double cot_t = std::cos(theta) / sin_t;

    const bool use_matrix = (printer_config.has("purge_in_prime_tower") && printer_config.opt_bool("purge_in_prime_tower"))
        && (printer_config.has("single_extruder_multi_material") && printer_config.opt_bool("single_extruder_multi_material"));
    const double prime_volume     = print_config.has("prime_volume") ? print_config.opt_float("prime_volume") : 45.;
    const double printable_height = printer_config.has("printable_height") ? printer_config.opt_float("printable_height") : 250.;

    const double inset = 1.;
    BoundingBoxf bed_ext;
    if (const auto *bed_opt = printer_config.option<ConfigOptionPoints>("printable_area");
        bed_opt != nullptr && !bed_opt->values.empty())
        bed_ext = get_extents(bed_opt->values);

    struct Plan
    {
        bool               wanted = false;
        BeltPurgeSignature sig;
        int                n_islands = 1;
        double             w_sub = 0., length = 0., height = 0.;
        Vec3d              center = Vec3d::Zero();
    };
    std::vector<Plan> plans(static_cast<size_t>(plate_count));

    for (int plate_idx = 0; plate_idx < plate_count; ++plate_idx) {
        Plan      &plan  = plans[size_t(plate_idx)];
        PartPlate *plate = partplate_list.get_plate(plate_idx);

        std::set<int> filaments;
        double        x_min = std::numeric_limits<double>::max();
        double        x_max = -std::numeric_limits<double>::max();
        double        y_min = std::numeric_limits<double>::max();
        double        y_max = -std::numeric_limits<double>::max();
        double        z_max = 0.;
        bool          have_objects = false;
        if (belt && plate != nullptr) {
            for (int obj_idx = 0; obj_idx < (int) model.objects.size(); ++obj_idx) {
                const ModelObject *mo = model.objects[obj_idx];
                if (is_prism(mo))
                    continue;
                int obj_extruder = 1;
                if (const ConfigOption *opt = mo->config.option("extruder"); opt != nullptr && opt->getInt() > 0)
                    obj_extruder = opt->getInt();
                bool any_instance_on_plate = false;
                for (int inst_idx = 0; inst_idx < (int) mo->instances.size(); ++inst_idx) {
                    if (!plate->contain_instance_totally(obj_idx, inst_idx))
                        continue;
                    any_instance_on_plate = true;
                    const BoundingBoxf3 bb = mo->instance_bounding_box(inst_idx);
                    x_min = std::min(x_min, bb.min.x());
                    x_max = std::max(x_max, bb.max.x());
                    y_min = std::min(y_min, bb.min.y());
                    y_max = std::max(y_max, bb.max.y());
                    z_max = std::max(z_max, bb.max.z());
                }
                if (!any_instance_on_plate)
                    continue;
                have_objects = true;
                for (const ModelVolume *mv : mo->volumes)
                    for (int e : mv->get_extruders())
                        filaments.insert(e > 0 ? e : obj_extruder);
            }
        }

        if (has_any_mixed_filament(is_mixed)) {
            std::vector<unsigned int> zero_based;
            zero_based.reserve(filaments.size());
            for (int f : filaments)
                if (f > 0)
                    zero_based.push_back((unsigned int) (f - 1));
            zero_based = expand_mixed_filaments(zero_based, is_mixed, comp_strs);
            filaments.clear();
            for (unsigned int f : zero_based)
                filaments.insert((int) f + 1);
        }

        plan.wanted = belt && prime_tower_enabled && !by_object && have_objects && filaments.size() > 1;
        if (!plan.wanted)
            continue;

        const int    n_islands = std::max(1, (int) filaments.size() - 1);
        const double min_width = n_islands + (n_islands - 1) * gap;
        const double width     = std::max(min_width,
            print_config.has("belt_purge_tower_width") ? print_config.opt_float("belt_purge_tower_width") : 35.);
        const double printable_width = width - (n_islands - 1) * gap;

        const double belt_min = belt_is_y ? y_min : x_min;
        const double belt_max = belt_is_y ? y_max : x_max;
        const double lat_min  = belt_is_y ? x_min : y_min;
        const double lat_max  = belt_is_y ? x_max : y_max;

        double max_flush = prime_volume;
        if (use_matrix) {
            const size_t extruder_nums = wxGetApp().preset_bundle->get_printer_extruder_count();
            const std::vector<double> matrix = get_flush_volumes_matrix(
                project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values, 0, extruder_nums);
            const auto * multi_opt = project_config.option<ConfigOptionFloats>("flush_multiplier");
            const double multiplier = multi_opt != nullptr && !multi_opt->values.empty() ? multi_opt->get_at(0) : 1.;
            const int    n_total    = (int) (std::sqrt(double(matrix.size())) + 0.5);
            double       m          = 0.;
            for (int i : filaments)
                for (int j : filaments)
                    if (i != j && i <= n_total && j <= n_total)
                        m = std::max(m, matrix[size_t(i - 1) * n_total + size_t(j - 1)]);
            if (m > 0.)
                max_flush = m * multiplier;
        }
        const double v_layer = double(filaments.size() - 1) * max_flush;

        const double eta    = 0.85;
        const double safety = 1.6;
        double       height = safety * v_layer * sin_t / (printable_width * layer_h * eta);
        height = std::clamp(height, 8.0, std::max(8.0, printable_height));

        auto q = [](double v) { return std::lround(v * 10.0); };
        BeltPurgeSignature &new_sig = plan.sig;
        new_sig.valid          = true;
        new_sig.filament_count = (int) filaments.size();
        new_sig.key[0] = q(width);
        new_sig.key[1] = q(layer_h);
        new_sig.key[2] = q(height);
        new_sig.key[3] = q(belt_min);
        new_sig.key[4] = q(belt_max);
        new_sig.key[5] = q(lat_min);
        new_sig.key[6] = q(z_max);
        new_sig.key[7] = static_cast<long>(rot);
        new_sig.key[8] = std::lround(theta * 10000.0);
        new_sig.key[9] = q(lat_max);
        const Vec3d plate_origin = plate->get_origin();
        new_sig.key[10] = q(plate_origin.x());
        new_sig.key[11] = q(plate_origin.y());
        if (bed_ext.defined) {
            new_sig.key[12] = q(bed_ext.max.x());
            new_sig.key[13] = q(bed_ext.max.y());
        }

        const double margin            = 5.;
        const double ramp_compensation = height / sin_t;
        const double belt_origin  = plate_origin[belt_is_y ? 1 : 0];
        double       belt_end     = belt_max + margin + ramp_compensation + z_max * cot_t;
        if (bed_ext.defined)
            belt_end = std::min(belt_end, belt_origin + (belt_is_y ? bed_ext.max.y() : bed_ext.max.x()) - inset);
        const double belt_start   = std::max(belt_origin, std::min(belt_min - ramp_compensation, belt_end - 10.));
        const double length       = std::max(belt_end - belt_start, 10.);
        belt_end                  = belt_start + length;
        const double belt_center  = 0.5 * (belt_start + belt_end);

        const double lat_origin   = plate_origin[belt_is_y ? 0 : 1];
        double       lat_center   = lat_max + 5. + 0.5 * width;
        if (bed_ext.defined) {
            const double bed_lat_max = belt_is_y ? bed_ext.max.x() : bed_ext.max.y();
            lat_center = lat_origin + bed_lat_max - inset - 0.5 * width;
        }

        plan.n_islands = n_islands;
        plan.w_sub     = (width - (n_islands - 1) * gap) / n_islands;
        plan.length    = length;
        plan.height    = height;
        plan.center    = Vec3d(belt_is_y ? lat_center : belt_center,
                               belt_is_y ? belt_center : lat_center,
                               0.5 * height);
    }

    std::vector<int> to_delete = stale_prisms;
    std::vector<int> to_create;
    for (int plate_idx = 0; plate_idx < plate_count; ++plate_idx) {
        const Plan             &plan     = plans[size_t(plate_idx)];
        const std::vector<int> &existing = prisms_by_plate[size_t(plate_idx)];
        BeltPurgeSignature     &sig      = sigs[size_t(plate_idx)];
        if (!plan.wanted) {
            sig = BeltPurgeSignature{};
            to_delete.insert(to_delete.end(), existing.begin(), existing.end());
        } else if (existing.size() == 1 && model.objects[size_t(existing.front())]->instances.size() == 1 && plan.sig == sig) {
            continue;
        } else {
            to_delete.insert(to_delete.end(), existing.begin(), existing.end());
            to_create.push_back(plate_idx);
        }
    }
    if (to_delete.empty() && to_create.empty())
        return false;

    remove_prisms(to_delete);

    for (int plate_idx : to_create) {
        const Plan &plan = plans[size_t(plate_idx)];

        TriangleMesh prism_mesh;
        for (int i = 0; i < plan.n_islands; ++i) {
            const double lat_off = i * (plan.w_sub + gap);
            TriangleMesh box = belt_is_y ? make_cube(plan.w_sub, plan.length, plan.height)
                                         : make_cube(plan.length, plan.w_sub, plan.height);
            box.translate(belt_is_y ? Vec3f((float) lat_off, 0.f, 0.f) : Vec3f(0.f, (float) lat_off, 0.f));
            prism_mesh.merge(box);
        }

        ModelObject *new_object = model.add_object();
        new_object->name        = _u8L("Belt Purge Tower");
        new_object->add_instance();
        ModelVolume *new_volume = new_object->add_volume(std::move(prism_mesh));
        new_volume->name        = new_object->name;

        auto &cfg = new_object->config;
        cfg.set_key_value("belt_purge_tower_object", new ConfigOptionBool(true));
        cfg.set_key_value("flush_into_objects", new ConfigOptionBool(true));
        cfg.set_key_value("extruder", new ConfigOptionInt(1));
        cfg.set_key_value("wall_loops", new ConfigOptionInt(1));
        cfg.set_key_value("top_shell_layers", new ConfigOptionInt(0));
        cfg.set_key_value("bottom_shell_layers", new ConfigOptionInt(0));
        cfg.set_key_value("sparse_infill_density", new ConfigOptionPercent(100));
        cfg.set_key_value("sparse_infill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        cfg.set_key_value("enable_support", new ConfigOptionBool(false));
        cfg.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
        cfg.set_key_value("seam_slope_type", new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));
        cfg.set_key_value("precise_z_height", new ConfigOptionBool(false));

        new_object->invalidate_bounding_box();
        new_object->ensure_on_bed();
        const BoundingBoxf3 cur = new_object->bounding_box_exact();
        new_object->translate_instances(Vec3d(plan.center.x() - cur.center().x(),
                                              plan.center.y() - cur.center().y(),
                                              0.0));
        new_object->instances.front()->set_assemble_transformation(new_object->instances.front()->get_transformation());

        const size_t obj_idx = model.objects.size() - 1;
        obj_list->add_object_to_list(obj_idx, false);

        sigs[size_t(plate_idx)] = plan.sig;
    }

    return true;
}

}
}
