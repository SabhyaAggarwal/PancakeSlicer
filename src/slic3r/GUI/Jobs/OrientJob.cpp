#include "OrientJob.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <cstddef>
#include <vector>
#include <utility>
#include "slic3r/GUI/PartPlate.hpp"
#include <cassert>
#include <boost/log/trivial.hpp>
#include "slic3r/GUI/Jobs/Job.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Nonplanar/AutoOrient.hpp"
#include <chrono>
#include <string>
#include <sstream>
#include <ios>
#include <iomanip>
#include <exception>
#include "libslic3r/LifecycleEvents.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Selection.hpp"


namespace Slic3r { namespace GUI {


void OrientJob::clear_input()
{
    const Model &model = m_plater->model();

    size_t count = 0, cunprint = 0;
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;

    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_selected.reserve(count);
    m_unselected.reserve(count);
    m_unprintable.reserve(cunprint);
}

void OrientJob::prepare_selection(std::vector<bool> obj_sel, bool only_one_plate)
{
    Model& model = m_plater->model();
    PartPlateList& plate_list = m_plater->get_partplate_list();
    bool selected_is_locked = false;

    for (size_t oidx = 0; oidx < obj_sel.size(); ++oidx) {
        bool selected = obj_sel[oidx];
        ModelObject* mo = model.objects[oidx];

        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            ModelInstance* mi = mo->instances[inst_idx];
            OrientMesh&& om = get_orient_mesh(mi);

            bool locked = false;
            if (!only_one_plate) {
                int plate_index = plate_list.find_instance(oidx, inst_idx);
                if ((plate_index >= 0)&&(plate_index < plate_list.get_plate_count())) {
                    if (plate_list.is_locked(plate_index)) {
                        if (selected) {
                            selected_is_locked = true;
                        }
                        continue;
                    }
                }
            }
            auto& cont = mo->printable ? (selected ? m_selected : m_unselected) : m_unprintable;

            cont.emplace_back(std::move(om));
        }
    }

    if (m_selected.empty()) {
        if (!selected_is_locked) {
            m_selected.swap(m_unselected);
        }
        else {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("All the selected objects are on a locked plate.\nCannot auto-orient these objects.")));
        }
    }
}

void OrientJob::prepare_selected() {
    clear_input();

    Model &model = m_plater->model();

    std::vector<bool> obj_sel(model.objects.size(), false);

    for (auto &s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = !s.second.empty();

    prepare_selection(obj_sel, false);
}

void OrientJob::prepare_partplate() {
    clear_input();

    PartPlateList& plate_list = m_plater->get_partplate_list();
    PartPlate* plate = plate_list.get_curr_plate();
    assert(plate != nullptr);

    if (plate->empty())
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": no instances in current plate!");

        return;
    }

    if (plate->is_locked()) {
        m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
            NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("This plate is locked.\nCannot auto-orient on this plate.")));
        return;
    }

    Model& model = m_plater->model();

    std::vector<bool> obj_sel(model.objects.size(), false);

    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx)
    {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            obj_sel[oidx] = plate->contain_instance(oidx, inst_idx);
        }
    }

    prepare_selection(obj_sel, true);
}

void OrientJob::prepare()
{
    int state = m_plater->get_prepare_state();
    m_plater->get_notification_manager()->bbl_close_plateinfo_notification();
    if (state == Job::JobPrepareState::PREPARE_STATE_DEFAULT) {
        prepare_selected();
    }
    else if (state == Job::JobPrepareState::PREPARE_STATE_MENU) {
        prepare_partplate();
    }
}

static void setMinimalSupportAreaPrams(Slic3r::orientation::OrientParams &out)
{
    out.TAR_A               = 0.015f;
    out.TAR_B               = 0.177f;
    out.RELATIVE_F          = 20;
    out.CONTOUR_F           = 0.5f;
    out.BOTTOM_F            = 2.5f;
    out.BOTTOM_HULL_F       = 0.1f;
    out.TAR_C               = 0.1f;
    out.TAR_D               = 1;
    out.TAR_E               = 0.0115f;
    out.FIRST_LAY_H         = 0.2f;
    out.VECTOR_TOL          = -0.00083f;
    out.NEGL_FACE_SIZE      = 0.01f;
    out.ASCENT              = -0.5f;
    out.PLAFOND_ADV         = 0.0599f;
    out.CONTOUR_AMOUNT      = 0.0182427f;
    out.OV_H                = 2.574f;
    out.height_offset       = 2.3728f;
    out.height_log          = 0.041375f;
    out.height_log_k        = 1.9325457f;
    out.LAF_MAX             = 0.999f;
    out.LAF_MIN             = 0.97f;
    out.TAR_LAF             = 0.001f;
    out.TAR_PROJ_AREA       = 0.1f;
    out.BOTTOM_MIN          = 0.1f;
    out.BOTTOM_MAX          = 2000;
    out.height_to_bottom_hull_ratio_MIN = 1,
    out.BOTTOM_HULL_MAX     = 2000;
    out.APPERANCE_FACE_SUPP = 3;
    out.overhang_angle      = 60.f;
    out.use_low_angle_face  = true;
    out.min_volume          = false;
    out.fun_dir             = {};
    out.parallel            = true;
    out.progressind         = {};
    out.stopcondition       = {};
}

void OrientJob::process(Ctl &ctl)
{
    static const auto arrangestr = _u8L("Orienting...");

    ctl.update_status(0, arrangestr);
    ctl.call_on_main_thread([this]{ prepare(); }).wait();;

    auto start = std::chrono::steady_clock::now();

    const GLCanvas3D::OrientSettings& settings = m_plater->canvas3D()->get_orient_settings();

    orientation::OrientParams params;
    if (settings.min_area) {
        setMinimalSupportAreaPrams(params);
        params.min_volume = false;
    }
    else {
        params.min_volume = true;
    }

    auto count = unsigned(m_selected.size() + m_unprintable.size());
    params.stopcondition = [&ctl]() { return ctl.was_canceled(); };

    params.progressind = [this, count, &ctl](unsigned st, std::string orientstr) {
        st += m_unprintable.size();
        if (st > 0) ctl.update_status(int(st / float(count) * 100), _u8L("Orienting") + " " + orientstr);
    };

    orientation::orient(m_selected, m_unselected, params);

    auto time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);

    std::stringstream ss;
    if (!m_selected.empty())
        ss << std::fixed << std::setprecision(3) << "Orient " << m_selected.back().name << " in " << time_elapsed.count() << " seconds. "
        << "Orientation: " << m_selected.back().orientation.transpose() << "; v,phi: " << m_selected.back().axis.transpose() << ", " << m_selected.back().angle << "; euler: " << m_selected.back().euler_angles.transpose();

    ctl.update_status(100,
        ctl.was_canceled() ? _u8L("Orienting canceled.")
        : _u8L(ss.str().c_str()));
    wxGetApp().plater()->show_status_message(ctl.was_canceled() ? "Orienting canceled." : ss.str());
}

OrientJob::OrientJob() : m_plater{wxGetApp().plater()} {}

void OrientJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
        eptr = nullptr;
    } catch (...) {
        eptr = std::current_exception();
    }

    if (canceled || eptr)
        return;

    for (OrientMesh& mesh : m_selected)
    {
        mesh.apply();
    }

    if (!m_selected.empty()) {
        Slic3r::LifecycleEventContext ctx;
        ctx.code = Slic3r::LifecycleEvtCode::Ok;
        ctx.msg = "auto_oriented";
        Slic3r::fire_lifecycle_event(Slic3r::LifecycleEvent::ObjectTransformed, ctx);
    }

    m_plater->update();

}

orientation::OrientMesh OrientJob::get_orient_mesh(ModelInstance* instance)
{
    using OrientMesh = orientation::OrientMesh;
    OrientMesh om;
    auto obj = instance->get_object();
    om.name = obj->name;
    om.mesh = obj->mesh();
    const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
    if (obj->config.has("support_threshold_angle"))
        om.overhang_angle = obj->config.opt_int("support_threshold_angle");
    else {
        om.overhang_angle = config.opt_int("support_threshold_angle");
    }

    if (config.has("fan_direction") && config.has("auxiliary_fan")) {
        FanDirection config_dir = config.option<ConfigOptionEnum<FanDirection>>("fan_direction")->value;
        if (config_dir == FanDirection::fdUndefine || !config.opt_bool("auxiliary_fan")) {
            om.cooling_direction = {0, 0, 0};
        } else if (config_dir == FanDirection::fdRight) {
            om.cooling_direction = {1, 0, 0};
            om.has_cooling_fan = true;
        } else {
            om.cooling_direction = {-1, 0, 0};
            om.has_cooling_fan = true;
        }
    }

    const auto *mode = obj->config.has("nonplanar_mode") ? obj->config.get().option<ConfigOptionEnum<NonplanarMode>>("nonplanar_mode") :
                                                           config.option<ConfigOptionEnum<NonplanarMode>>("nonplanar_mode");
    if (mode != nullptr && mode->value == NonplanarMode::Auto && wxGetApp().plater()->canvas3D()->get_orient_settings().nonplanar) {
        om.nonplanar        = true;
        Slic3r::DynamicPrintConfig object_config = config;
        object_config.apply(obj->config.get(), true);
        om.nonplanar_params = Nonplanar::orient_params(object_config);
    }

    om.setter = [instance](const OrientMesh& p) {
        wxGetApp().plater()->set_nonplanar_orientation(instance->get_object()->id().id, p.nonplanar_decision);
        instance->rotate(p.rotation_matrix);
        instance->rotate(p.rotation_matrix_vertical);
        instance->get_object()->invalidate_bounding_box();
        instance->get_object()->ensure_on_bed();
    };
    return om;
}

}}
