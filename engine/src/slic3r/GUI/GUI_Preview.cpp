#include "libslic3r/libslic3r.h"
#include "libslic3r/Layer.hpp"
#include "IMSlider.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include <wx/slider.h>
#include <wx/gdicmn.h>
#include <string>
#include <vector>
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include <functional>
#include "libslic3r/Config.hpp"
#include <boost/log/trivial.hpp>
#include "libslic3r/CustomGCode.hpp"
#include <wx/event.h>
#include <algorithm>
#include <cstdlib>
#include <cassert>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ExPolygon.hpp"
#include <cmath>
#include <cstdint>
#if ENABLE_OPENGL_AUTO_AA_SAMPLES
#include "GUI_Init.hpp"
#endif
#include "I18N.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "OpenGLManager.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"

#include <wx/listbook.h>
#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>

#include "libslic3r/Print.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"
#include "slic3r/GUI/GCodeViewer.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Selection.hpp"

class wxDropTarget;
namespace libvgcode { enum class EViewType : uint8_t; }

#ifdef _WIN32
#include "BitmapComboBox.hpp"
#endif

namespace Slic3r {
namespace GUI {

View3D::View3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, model, config, process);
}

View3D::~View3D()
{
    delete m_canvas;
    delete m_canvas_widget;
}

bool View3D::init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 ))
        return false;

#if ENABLE_OPENGL_AUTO_AA_SAMPLES
    const GUI_InitParams* const init_params = wxGetApp().init_params;
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this, (init_params != nullptr) ? init_params->opengl_aa : false);
#else
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->enable_picking(true);
    m_canvas->get_selection().set_mode(Selection::Instance);
    m_canvas->enable_moving(true);
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasView3D);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(true);
    m_canvas->enable_select_plate_toolbar(false);
    m_canvas->enable_assemble_view_toolbar(true);
    m_canvas->enable_separator_toolbar(true);
    m_canvas->enable_labels(true);
    m_canvas->enable_slope(true);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void View3D::set_as_dirty()
{
    if (m_canvas != nullptr) {
        m_canvas->set_as_dirty();
    }
}

void View3D::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void View3D::plates_count_changed()
{
    if (m_canvas != nullptr)
        m_canvas->plates_count_changed();
}

void View3D::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}

void View3D::select_curr_plate_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_curr_plate_all();
}

void View3D::select_object_from_idx(std::vector<int>& object_idxs) {
    if (m_canvas != nullptr)
        m_canvas->select_object_from_idx(object_idxs);
}

void View3D::remove_curr_plate_all()
{
    if (m_canvas != nullptr)
        m_canvas->remove_curr_plate_all();
}

void View3D::select_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_all();
}

void View3D::deselect_all()
{
    if (m_canvas != nullptr)
        m_canvas->deselect_all();
}

void View3D::exit_gizmo()
{
    if (m_canvas != nullptr)
        m_canvas->exit_gizmo();
}

void View3D::delete_selected()
{
    if (m_canvas != nullptr)
        m_canvas->delete_selected();
}

void View3D::center_selected()
{
    if (m_canvas != nullptr)
        m_canvas->do_center();
}

void View3D::drop_selected()
{
    if (m_canvas != nullptr)
        m_canvas->do_drop();
}

void View3D::center_selected_plate(const int plate_idx) {
    if (m_canvas != nullptr)
        m_canvas->do_center_plate(plate_idx);
}

void View3D::mirror_selection(Axis axis)
{
    if (m_canvas != nullptr)
        m_canvas->mirror_selection(axis);
}

bool View3D::is_layers_editing_enabled() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_enabled() : false;
}

bool View3D::is_layers_editing_allowed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_allowed() : false;
}

void View3D::enable_layers_editing(bool enable)
{
    if (m_canvas != nullptr)
        m_canvas->enable_layers_editing(enable);
}

bool View3D::is_dragging() const
{
    return (m_canvas != nullptr) ? m_canvas->is_dragging() : false;
}

bool View3D::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void View3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr)
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
}

void View3D::render()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

Preview::Preview(
    wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config,
    BackgroundSlicingProcess* process, GCodeProcessorResult* gcode_result, std::function<void()> schedule_background_process_func)
    : m_config(config)
    , m_process(process)
    , m_gcode_result(gcode_result)
    , m_schedule_background_process(schedule_background_process_func)
{
    if (init(parent, bed, model))
        load_print();
}

void Preview::update_gcode_result(GCodeProcessorResult* gcode_result)
{
    m_gcode_result = gcode_result;

    return;
}

bool Preview::init(wxWindow* parent, Bed3D& bed, Model* model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 ))
        return false;

#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
#endif
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));
    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasPreview);
    m_canvas->get_gcode_viewer().enable_legend(true);
    if (wxGetApp().is_editor()) {
        m_canvas->enable_select_plate_toolbar(true);
    }
    m_canvas->enable_assemble_view_toolbar(false);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();

    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    if (m_canvas != nullptr)
        delete m_canvas;

    if (m_canvas_widget != nullptr)
        delete m_canvas_widget;
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void Preview::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void Preview::select_view(const std::string& direction)
{
    m_canvas->select_view(direction);
}

void Preview::set_drop_target(wxDropTarget* target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

void Preview::load_print(bool keep_z_range, bool only_gcode)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range, only_gcode);

    Layout();
}

void Preview::reload_print(bool only_gcode)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: enter")%__LINE__;

    m_loaded_print = nullptr;

    load_print(false, only_gcode);
    m_only_gcode = only_gcode;
}

void Preview::refresh_belt_view()
{
    IMSlider *layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
    const int lower  = layers_slider->GetLowerValue();
    const int higher = layers_slider->GetHigherValue();
    m_loaded_print = nullptr;
    load_print(true , m_only_gcode);
    if (higher <= layers_slider->GetMaxValue())
        layers_slider->SetSelectionSpan(lower, higher);
}

void Preview::load_shells(const Print& print, bool force_previewing)
{
    m_canvas->load_shells(print, force_previewing);
}

void Preview::reset_shells()
{
    m_canvas->reset_shells();
}

void Preview::msw_rescale()
{
    get_canvas3d()->msw_rescale();

    reload_print(m_only_gcode);
}

void Preview::sys_color_changed()
{
}

void Preview::on_tick_changed(CustomGCode::Type type)
{
    m_keep_current_preview_type = false;
    reload_print(false);
}

void Preview::bind_event_handlers()
{
    this->Bind(wxEVT_SIZE, &Preview::on_size, this);
}

void Preview::unbind_event_handlers()
{
    this->Unbind(wxEVT_SIZE, &Preview::on_size, this);
}

void Preview::show_sliders(bool show)
{
    show_moves_sliders(show);
    show_layers_sliders(show);
}

void Preview::show_moves_sliders(bool show)
{
    ;
}

void Preview::show_layers_sliders(bool show)
{
    ;
}


void Preview::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    Refresh();
}

void Preview::check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model, const std::vector<double>& layers_z)
{
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                     [layers_z](CustomGCode::Item val)
        {
            auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val.print_z - epsilon());
            return it == layers_z.end();
        }),
        ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

static int find_close_layer_idx(const std::vector<double> &zs, double &z, double eps)
{
    if (zs.empty()) return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        --it_l;
        if (z - *it_l < eps) return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps) return 0;
    } else {
        auto it_l = it_h;
        --it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps) { return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin()); }
    }
    return -1;
}

void Preview::update_layers_slider_mode()
{
    bool one_extruder_printed_model = true;
    bool can_change_color = true;
    int only_extruder = -1;

    if (wxGetApp().filaments_cnt() > 1) {
        auto plate_extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_extruders_without_support();
        for (auto extruder : plate_extruders) {
            if (extruder != plate_extruders[0])
                can_change_color = false;
        }
        if (!plate_extruders.empty()) {
            only_extruder = plate_extruders[0];
        }
    }

    IMSlider *m_layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
    m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder, can_change_color);
}

void Preview::update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range)
{
    IMSlider *m_layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
    double z_low     = m_layers_slider->GetLowerValueD();
    double z_high    = m_layers_slider->GetHigherValueD();
    bool   was_empty = m_layers_slider->GetMaxValue() == 0;

    bool force_sliders_full_range = was_empty;
    if (!keep_z_range) {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_layers_slider->GetMaxValueD()) > epsilon() ;
        force_sliders_full_range |= span_changed;
    }
    bool snap_to_min = force_sliders_full_range || m_layers_slider->is_lower_at_min();
    bool snap_to_max = force_sliders_full_range || m_layers_slider->is_higher_at_max();

    update_layers_slider_mode();

    Plater* plater = wxGetApp().plater();
    CustomGCode::Info ticks_info_from_curr_plate;
    if (wxGetApp().is_editor())
        ticks_info_from_curr_plate = plater->model().get_curr_plate_custom_gcodes();
    else {
        ticks_info_from_curr_plate.mode   = CustomGCode::Mode::SingleExtruder;
        ticks_info_from_curr_plate.gcodes = m_gcode_result->custom_gcode_per_print_z;
    }
    check_layers_slider_values(ticks_info_from_curr_plate.gcodes, layers_z);

    m_layers_slider->SetExtruderColors(plater->get_extruder_colors_from_plater_config(wxGetApp().is_editor() ? nullptr : m_gcode_result));
    m_layers_slider->SetSliderValues(layers_z);
    assert(m_layers_slider->GetMinValue() == 0);
    m_layers_slider->SetMaxValue(layers_z.empty() ? 0 : layers_z.size() - 1);

    int idx_low  = 0;
    int idx_high = m_layers_slider->GetMaxValue();
    if (!layers_z.empty()) {
        if (!snap_to_min) {
            int idx_new = find_close_layer_idx(layers_z, z_low, epsilon() );
            if (idx_new != -1) idx_low = idx_new;
        }
        if (!snap_to_max) {
            int idx_new = find_close_layer_idx(layers_z, z_high, epsilon() );
            if (idx_new != -1) idx_high = idx_new;
        }
    }
    m_layers_slider->SetSelectionSpan(idx_low, idx_high);

    auto curr_plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    auto curr_print_seq = curr_plate->get_real_print_seq();
    bool sequential_print = (curr_print_seq == PrintSequence::ByObject);
    m_layers_slider->SetDrawMode(sequential_print);
    
    m_layers_slider->SetTicksValues(ticks_info_from_curr_plate);

    auto print_mode_stat = m_gcode_result->print_statistics.modes.front();
    m_layers_slider->SetLayersTimes(m_canvas->get_gcode_layers_times_cache(), print_mode_stat.time);

    if (m_layers_slider->IsNewPrint()) {
        const Print &print = wxGetApp().plater()->fff_print();

        for (auto object : print.objects()) {
            double object_x = double(object->size().x());
            double object_y = double(object->size().y());

            double  height      = object->height();
            coord_t longer_side = std::max(object_x, object_y);
            auto    num_layers  = int(object->layers().size());
            if (height / longer_side > 0.3 || num_layers < 2) continue;

            const ExPolygons &bottom      = object->get_layer(0)->lslices;
            double            bottom_area = area(bottom);

            int i, min_solid_height = int(0.25 * num_layers);
            for (i = 1; i <= min_solid_height; ++i) {
                double cur_area = area(object->get_layer(i)->lslices);
                if (!equivalent_areas(bottom_area, cur_area)) {
                    if (i == 1 && fabs(cur_area - bottom_area) / bottom_area < 0.1) {
                        bottom_area = cur_area;
                        continue;
                    }
                    break;
                }
            }
            if (i < min_solid_height) continue;
        }
    }
}

void Preview::load_print_as_fff(bool keep_z_range, bool only_gcode)
{
    if (wxGetApp().mainframe == nullptr || wxGetApp().is_recreating_gui())
        return;

    const Print *print = m_process->fff_print();
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: previous print %2%, new print %3%")%__LINE__ %m_loaded_print %print;
    if ((m_loaded_print&&(m_loaded_print == print)) || m_process->current_printer_technology() != ptFFF) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: already loaded before, return directly")%__LINE__;
        return;
    }

    bool has_layers = false;
    load_shells(*print, true);
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: print: %2%, gcode_result %3%, check started")%__LINE__ %print %m_gcode_result;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: print is step done, posSlice %2%, posSupportMaterial %3%, psGCodeExport %4%") % __LINE__ % print->is_step_done(posSlice) %print->is_step_done(posSupportMaterial) % print->is_step_done(psGCodeExport);
    if (print->is_step_done(posSlice)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->layers().empty()) {
                has_layers = true;
                break;
            }
    }
    if (print->is_step_done(posSupportMaterial)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->support_layers().empty()) {
                has_layers = true;
                break;
            }
    }

    bool directly_preview = print->is_step_done(psGCodeExport) && !m_gcode_result->moves.empty();
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": directly_preview: %1%, gcode_result moves %2%, has_layers %3%") % directly_preview % m_gcode_result->moves.size() %has_layers;
    if (wxGetApp().is_editor() && !has_layers && !directly_preview) {
        show_sliders(false);
        m_canvas_widget->Refresh();
        return;
    }
    else if (directly_preview && !has_layers)
        keep_z_range = false;

    libvgcode::EViewType gcode_view_type = m_canvas->get_gcode_view_type();
    const bool gcode_preview_data_valid = !m_gcode_result->moves.empty();
    const bool is_pregcode_preview = !gcode_preview_data_valid && wxGetApp().is_editor();

    const std::vector<std::string> tool_colors = wxGetApp().plater()->get_extruder_colors_from_plater_config(m_gcode_result);
    const std::vector<CustomGCode::Item>& color_print_values = wxGetApp().is_editor() ?
        wxGetApp().plater()->model().get_curr_plate_custom_gcodes().gcodes : m_gcode_result->custom_gcode_per_print_z;
    std::vector<std::string> color_print_colors;
    if (!color_print_values.empty()) {
        color_print_colors = wxGetApp().plater()->get_colors_for_color_print(m_gcode_result);
        color_print_colors.push_back("#808080");
    }

    std::vector<double> zs;

    if (IsShown()) {
        m_canvas->set_selected_extruder(0);
        bool is_slice_result_valid = wxGetApp().plater()->get_partplate_list().get_curr_plate()->is_slice_result_valid();
        if (gcode_preview_data_valid && (is_slice_result_valid || only_gcode)) {
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": will load gcode_preview from result, moves count %1%") % m_gcode_result->moves.size();
            m_canvas->load_gcode_preview(*m_gcode_result, tool_colors, color_print_colors, only_gcode);
            gcode_view_type = m_canvas->get_gcode_view_type();
            show_moves_sliders();

            m_canvas->set_shells_on_previewing(true);
            m_canvas->set_shell_transparence();
            Refresh();
            zs = m_canvas->get_gcode_layers_zs();
            m_loaded_print = print;
        }
        else if (is_pregcode_preview) {
            show_moves_sliders(false);
            Refresh();
        }

        if (!zs.empty() && !m_keep_current_preview_type) {
            unsigned int number_extruders = wxGetApp().is_editor() ?
                (unsigned int)print->extruders().size() :
                m_canvas->get_gcode_extruders_count();
            std::vector<CustomGCode::Item> gcodes = wxGetApp().is_editor() ?
                wxGetApp().plater()->model().get_curr_plate_custom_gcodes().gcodes :
                m_gcode_result->custom_gcode_per_print_z;
            const wxString choice = !gcodes.empty() ?
                _L("Multicolor Print") :
                (number_extruders > 1) ? _L("Filaments") : _L("Line Type");
        }

        if (zs.empty()) {
            show_layers_sliders(false);
            m_canvas_widget->Refresh();
        }
        else
            update_layers_slider(zs, keep_z_range);
    }
}

AssembleView::AssembleView(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, model, config, process);
}

AssembleView::~AssembleView()
{
    delete m_canvas;
    delete m_canvas_widget;
}

bool AssembleView::init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 ))
        return false;

#if ENABLE_OPENGL_AUTO_AA_SAMPLES
    const GUI_InitParams* const init_params = wxGetApp().init_params;
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this, (init_params != nullptr) ? init_params->opengl_aa : false);
#else
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
#endif
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->enable_picking(true);
    m_canvas->enable_moving(true);
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasAssembleView);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(false);
    m_canvas->enable_labels(false);
    m_canvas->enable_slope(false);
    m_canvas->enable_assemble_view_toolbar(false);
    m_canvas->enable_return_toolbar(true);
    m_canvas->enable_separator_toolbar(false);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void AssembleView::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void AssembleView::render()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

bool AssembleView::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void AssembleView::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr) {
        if (!m_canvas->is_initialized()) {
            m_canvas->render(true);
        }
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
    }
}

void AssembleView::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}


}
}
