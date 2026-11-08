#include "libslic3r/libslic3r.h"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/LifecycleEvents.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "GUI_App.hpp"
#include "Shortcuts.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "BitmapComboBox.hpp"
#include "MainFrame.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "slic3r/plugin/PluginManager.hpp"

#include "OptionsGroup.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/Model.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"
#include "PartPlate.hpp"
#include "format.hpp"
#include "NotificationManager.hpp"
#include "MsgDialog.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "SingleChoiceDialog.hpp"
#include "StepMeshDialog.hpp"


#include "slic3r/GUI/Event.hpp"
#include "libslic3r/Config.hpp"
#include <string>
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include <cstddef>
#include "slic3r/GUI/ExtraRenderers.hpp"
#include <cassert>
#include <algorithm>
#include "libslic3r/TriangleMesh.hpp"
#include "slic3r/GUI/GUI.hpp"
#include <memory>
#include <iterator>
#include "slic3r/GUI/GLToolbar.hpp"
#include <optional>
#include <cstdlib>
#include <utility>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Point.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include "libslic3r/LocalesUtils.hpp"
#include <exception>
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/GUI_ObjectLayers.hpp"
#include <map>
#include <set>
#include "libslic3r/BrimEarsPoint.hpp"
#include "libslic3r/ObjectID.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include <cmath>
#include "slic3r/GUI/GUI_Utils.hpp"
#include <vector>
#include <unordered_map>
#include <functional>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <wx/arrstr.h>
#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>
#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/dvrenderers.h>
#include <wx/chartype.h>
#include <wx/dataobj.h>
#include <wx/dnd.h>
#include <wx/busycursor.h>
#include <wx/msgdlg.h>
#include <wx/accel.h>
#include <wx/app.h>
#include <wx/progdlg.h>
#include <wx/listbook.h>
#include <wx/numformatter.h>
#include <wx/string.h>
#include <wx/setup.h>
#include <wx/settings.h>
#include <wx/utils.h>
#include <wx/headerctrl.h>
#include <wx/window.h>

#include "slic3r/Utils/FixModelByCgal.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/PrintConfig.hpp"

#ifdef __WXMSW__
#include "wx/uiaction.h"
#include <wx/renderer.h>
#endif
#include "Gizmos/GLGizmoScale.hpp"

#include "libslic3r/TriangleMeshDeal.hpp"

class wxMenu;
namespace Slic3r { class Step; }
namespace Slic3r
{
namespace GUI
{

wxDEFINE_EVENT(EVT_OBJ_LIST_OBJECT_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_PARTPLATE_LIST_PLATE_SELECT, IntEvent);

static PrinterTechnology printer_technology()
{
    return wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology();
}

static const Selection& scene_selection()
{
    if (wxGetApp().plater()->get_current_canvas3D()->get_canvas_type() == GLCanvas3D::ECanvasType::CanvasAssembleView)
        return wxGetApp().plater()->get_assmeble_canvas3D()->get_selection();
    
    return wxGetApp().plater()->get_view3D_canvas3D()->get_selection();
}

static DynamicPrintConfig& printer_config()
{
    return wxGetApp().preset_bundle->printers.get_edited_preset().config;
}

static int filaments_count()
{
    return wxGetApp().filaments_cnt();
}

static void take_snapshot(const std::string& snapshot_name)
{
    Plater* plater = wxGetApp().plater();
    if (plater)
        plater->take_snapshot(snapshot_name);
}

class wxRenderer : public wxDelegateRendererNative
{
public:
    wxRenderer() : wxDelegateRendererNative(wxRendererNative::Get()) {}
    virtual void DrawItemSelectionRect(wxWindow *win, wxDC& dc, const wxRect& rect, int flags = 0) override
    {
        dc.SetBrush(StateColor::darkModeColorFor(wxColour("#BFE1DE")));
        dc.DrawRectangle(rect);
    }
    virtual void DrawFocusRect(        wxWindow *win, wxDC& dc, const wxRect& rect, int flags = 0) override
    {
        dc.SetPen(  StateColor::darkModeColorFor(wxColour("#009688")));
        dc.DrawRectangle(rect);
    }
    virtual void DrawTreeItemButton(   wxWindow *win, wxDC& dc, const wxRect& rect, int flags = 0) override
    {
        dc.SetPen(  StateColor::darkModeColorFor(wxColour("#7C8282")));
        dc.SetBrush(StateColor::darkModeColorFor(wxColour("#7C8282")));
        bool expanded = (flags == wxCONTROL_EXPANDED || flags == (wxCONTROL_CURRENT | wxCONTROL_EXPANDED));
        wxRect r = rect;
        r.Deflate((expanded ? wxSize(4, 6) : wxSize(6, 4)) * (wxGetApp().em_unit() * .1));
        wxPoint triangle[3];
        triangle[0] = wxPoint(r.x, r.y);
        triangle[1] = triangle[0] + wxPoint(r.width, expanded ? 0 :r.height/2);
        triangle[2] = triangle[0] + wxPoint(expanded ? r.width/2 : 0, r.height);
        dc.DrawPolygon(3, &triangle[0]);
    }
    virtual void DrawItemText(
        wxWindow* win,
        wxDC& dc,
        const wxString& text,
        const wxRect& rect,
        int align = wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL,
        int flags = 0,
        wxEllipsizeMode ellipsizeMode = wxELLIPSIZE_END
    ) override
    {
        dc.SetTextForeground(StateColor::darkModeColorFor(wxColour("#262E30")));
        dc.DrawText(text,wxPoint(rect.x, rect.y));
    }
};

ObjectList::ObjectList(wxWindow* parent) :
    wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_MULTIPLE | wxNO_BORDER | wxDV_NO_HEADER)
{
    wxGetApp().UpdateDVCDarkUI(this, true);

#ifdef __linux__
    this->SetForegroundColour(*wxBLACK);
#endif

    SetFont(Label::sysFont(13));
#ifdef __WXMSW__
    static auto render = new wxRenderer;
    wxRendererNative::Set(render);
#endif

    create_objects_ctrl();

    Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        if (!GetScreenRect().Contains(wxGetMousePosition())) {
            return;
        }
#ifndef __WXOSX__
        const wxPoint mouse_pos = this->get_mouse_position_in_control();
#endif

#ifndef __APPLE__
        this->SetFocus();
#else
        if (m_prevent_list_events)
            return;
#endif
#ifdef __WXMSW__
		int new_selected_column = -1;
#endif
        if (wxGetKeyState(WXK_SHIFT))
        {
            wxDataViewItemArray sels;
            GetSelections(sels);
            if (! sels.empty() && sels.front() == m_last_selected_item)
                m_last_selected_item = sels.back();
            else
                m_last_selected_item = event.GetItem();
        }
        else {
  	      	wxDataViewItem    new_selected_item  = event.GetItem();
#if 0
#ifdef __WXMSW__
		    wxDataViewItem    item;
		    wxDataViewColumn *col;
		    this->HitTest(this->get_mouse_position_in_control(), item, col);
		    new_selected_column = (col == nullptr) ? -1 : (int)col->GetModelColumn();
	        if (new_selected_item == m_last_selected_item && m_last_selected_column != -1 && m_last_selected_column != new_selected_column) {
	        	wxUIActionSimulator sim;
				sim.Char(WXK_RETURN);
	        }
#endif
#endif
	        m_last_selected_item = new_selected_item;
        }
#ifdef __WXMSW__
        m_last_selected_column = new_selected_column;
#endif

        ObjectDataViewModelNode* sel_node = (ObjectDataViewModelNode*)event.GetItem().GetID();
        if (sel_node && (sel_node->GetType() & ItemType::itPlate)) {
            if (wxGetApp().plater()->is_preview_shown()) {
                wxGetApp().plater()->select_sliced_plate(sel_node->GetPlateIdx());
            } else {
                wxGetApp().plater()->select_plate(sel_node->GetPlateIdx());
            }
            wxGetApp().plater()->deselect_all();
        }
        else {
            selection_changed();
        }
#ifndef __WXMSW__
        set_tooltip_for_item(this->get_mouse_position_in_control());
#endif

#ifndef __WXOSX__
        list_manipulation(mouse_pos);
#endif
    });

#ifdef __WXOSX__
    m_shortcut_id_base = wxWindow::NewControlId(int(Shortcut::Count));
    for (size_t i = 0; i < size_t(Shortcut::Count); ++i)
        this->Bind(wxEVT_MENU, [this, shortcut = Shortcut(i)](wxCommandEvent&) { dispatch_shortcut(shortcut); }, m_shortcut_id_base + int(i));
    for (int i = 1; i < 10; i++)
        this->Bind(wxEVT_MENU, [this, i](wxCommandEvent &evt) {
            if (filaments_count() > 1 && i <= filaments_count())
                this->set_extruder_for_selected_items(i);
        }, wxID_LAST+i);
    update_shortcut_accelerators();
#else
    Bind(wxEVT_CHAR, [this](wxKeyEvent& event) { key_event(event); });
#endif

#ifdef __WXMSW__
    GetMainWindow()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        set_tooltip_for_item(this->get_mouse_position_in_control());
        event.Skip();
    });
#endif

    Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,  &ObjectList::OnContextMenu,     this);

    Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG,    &ObjectList::OnBeginDrag,       this);
    Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &ObjectList::OnDropPossible,    this);
    Bind(wxEVT_DATAVIEW_ITEM_DROP,          &ObjectList::OnDrop,            this);

    Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ObjectList::OnStartEditing, this);
    Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &ObjectList::OnEditingStarted,  this);
    Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE,    &ObjectList::OnEditingDone,     this);

    Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &ObjectList::ItemValueChanged,  this);

    Bind(wxEVT_SIZE, ([this](wxSizeEvent &e) {
             if (m_last_size == this->GetSize()) {
                 e.Skip();
                 return;
             } else {
                 m_last_size = this->GetSize();
             }

        update_name_column_width();

        this->CallAfter([this]() { ensure_current_item_visible(); });

        e.Skip();
    }));
    m_last_size = this->GetSize();
}

ObjectList::~ObjectList()
{
    if (m_objects_model)
            m_objects_model->DecRef();
}

void ObjectList::set_min_height()
{
#if 0
    if (m_items_count == size_t(-1))
        m_items_count = 7;
    int list_min_height = lround(2.25 * (m_items_count + 1) * wxGetApp().em_unit());
    this->SetMinSize(wxSize(1, list_min_height));
#endif
}

void ObjectList::update_min_height()
{
    wxDataViewItemArray all_items;
    m_objects_model->GetAllChildren(wxDataViewItem(nullptr), all_items);
    size_t items_cnt = all_items.Count();
#if 0
    if (items_cnt < 7)
        items_cnt = 7;
    else if (items_cnt >= 15)
        items_cnt = 15;
#else
    items_cnt = 8;
#endif

    if (m_items_count == items_cnt)
        return;

    m_items_count = items_cnt;
    set_min_height();
}


void ObjectList::create_objects_ctrl()
{
#if 0
    SetMinSize(wxSize(-1, 3000));
#endif

    m_objects_model = new ObjectDataViewModel;
    AssociateModel(m_objects_model);
    m_objects_model->SetAssociatedControl(this);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
    EnableDragSource(wxDF_UNICODETEXT);
    EnableDropTarget(wxDF_UNICODETEXT);
#endif

    const int em = wxGetApp().em_unit();

    m_columns_width.resize(colCount);
    m_columns_width[colName] = 22;
    m_columns_width[colHeight] = 3;
    m_columns_width[colPrint] = 3;
    m_columns_width[colFilament] = 5;
    m_columns_width[colSupportPaint] = 3;
    m_columns_width[colSinking] = 3;
    m_columns_width[colColorPaint] = 3;
    m_columns_width[colEditing] = 3;

    BitmapTextRenderer* bmp_text_renderer = new BitmapTextRenderer();
    bmp_text_renderer->set_can_create_editor_ctrl_function([this]() {
        auto type = m_objects_model->GetItemType(GetSelection());
        return type & (itVolume | itObject | itPlate);
    });

    wxDataViewColumn* name_col = new wxDataViewColumn(_L("Name"), bmp_text_renderer,
        colName, m_columns_width[colName] * em, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    AppendColumn(name_col);

    AppendBitmapColumn(" ", colHeight, wxOSX ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT, 3 * em,
        wxALIGN_CENTER_HORIZONTAL, 0);

    AppendBitmapColumn(" ", colPrint, wxOSX ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT, m_columns_width[colPrint]*em,
        wxALIGN_CENTER_HORIZONTAL, 0);

    BitmapChoiceRenderer* bmp_choice_renderer = new BitmapChoiceRenderer();
    const auto get_filament_context_item = [this]() {
#ifdef __WXOSX__
        return m_filament_editor_item.IsOk() ? m_filament_editor_item : GetSelection();
#else
        return GetSelection();
#endif
    };
    bmp_choice_renderer->set_can_create_editor_ctrl_function([this, get_filament_context_item]() {
        const wxDataViewItem item = get_filament_context_item();
        return m_objects_model->GetItemType(item) & (itVolume | itLayer | itObject);
    });
    bmp_choice_renderer->set_default_extruder_idx([this, get_filament_context_item]() {
        const wxDataViewItem item = get_filament_context_item();
        return m_objects_model->GetDefaultExtruderIdx(item);
    });
    bmp_choice_renderer->set_has_default_extruder([this, get_filament_context_item]() {
        const wxDataViewItem item = get_filament_context_item();
        return m_objects_model->GetVolumeType(item) == ModelVolumeType::PARAMETER_MODIFIER ||
               m_objects_model->GetItemType(item) == itLayer;
    });
    AppendColumn(new wxDataViewColumn(_L("Fila."), bmp_choice_renderer,
        colFilament, m_columns_width[colFilament] * em, wxALIGN_CENTER_HORIZONTAL, 0));

    AppendBitmapColumn(" ", colSupportPaint, wxOSX ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT, m_columns_width[colSupportPaint] * em,
        wxALIGN_CENTER_HORIZONTAL, 0);
    AppendBitmapColumn(" ", colColorPaint, wxOSX ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT, m_columns_width[colColorPaint] * em,
        wxALIGN_CENTER_HORIZONTAL, 0);
    AppendBitmapColumn(" ", colSinking, wxOSX ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT, m_columns_width[colSinking] * em,
        wxALIGN_CENTER_HORIZONTAL, 0);

    AppendBitmapColumn(" ", colEditing, wxOSX ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT, m_columns_width[colEditing] * em,
        wxALIGN_CENTER_HORIZONTAL, 0);


    this->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& event) {
        if (event.GetColumn() == colFilament) {
#ifdef __WXOSX__
            start_filament_editor(event.GetItem());
#else
            this->EditItem(event.GetItem(), GetColumn(colFilament));
#endif
            return;
        }

        const wxDataViewItem item = event.GetItem();
        if (!item.IsOk())
            return;
        if (wxGetApp().plater()->is_preview_shown())
            return;
        const ItemType type = m_objects_model->GetItemType(item);
        if (type & (itObject | itVolume | itInstance)) {
            if (GLCanvas3D* canvas = wxGetApp().plater()->get_current_canvas3D())
                canvas->zoom_to_selection();
        }
    });

#ifdef __WXOSX__
        dataview_remove_insets(this);
        for (int cn = colName; cn < colCount; cn++)
            GetColumn(cn)->SetWidth(m_columns_width[cn] * em);
#endif

    SetRowHeight(2 * em + FromDIP(2));
}

void ObjectList::get_selected_item_indexes(int& obj_idx, int& vol_idx, const wxDataViewItem& input_item )
{
    const wxDataViewItem item = input_item == wxDataViewItem(nullptr) ? GetSelection() : input_item;

    if (!item)
    {
        obj_idx = vol_idx = -1;
        return;
    }

    const ItemType type = m_objects_model->GetItemType(item);

    obj_idx =   type & itObject ? m_objects_model->GetIdByItem(item) :
                type & itVolume ? m_objects_model->GetIdByItem(m_objects_model->GetObject(item)) : -1;

    vol_idx =   type & itVolume ? m_objects_model->GetVolumeIdByItem(item) : -1;
}

void ObjectList::get_selection_indexes(std::vector<int>& obj_idxs, std::vector<int>& vol_idxs)
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return;

    if ( m_objects_model->GetItemType(sels[0]) & itVolume ||
        (sels.Count()==1 && m_objects_model->GetItemType(m_objects_model->GetParent(sels[0])) & itVolume) ) {
        for (wxDataViewItem item : sels) {
            obj_idxs.emplace_back(m_objects_model->GetIdByItem(m_objects_model->GetObject(item)));

            if (sels.Count() == 1 && m_objects_model->GetItemType(m_objects_model->GetParent(item)) & itVolume)
                item = m_objects_model->GetParent(item);

            assert(m_objects_model->GetItemType(item) & itVolume);
            vol_idxs.emplace_back(m_objects_model->GetVolumeIdByItem(item));
        }
    }
    else {
        for (wxDataViewItem item : sels) {
            const ItemType type = m_objects_model->GetItemType(item);
            obj_idxs.emplace_back(type & itObject ? m_objects_model->GetIdByItem(item) :
                                  m_objects_model->GetIdByItem(m_objects_model->GetObject(item)));
        }
    }

    std::sort(obj_idxs.begin(), obj_idxs.end(), std::less<int>());
    obj_idxs.erase(std::unique(obj_idxs.begin(), obj_idxs.end()), obj_idxs.end());
}

int ObjectList::get_repaired_errors_count(const int obj_idx, const int vol_idx ) const
{
    return obj_idx >= 0 ? (*m_objects)[obj_idx]->get_repaired_errors_count(vol_idx) : 0;
}

static std::string get_warning_icon_name(const TriangleMeshStats& stats)
{
    return stats.manifold() ? (stats.repaired() ? "obj_warning" : "") : "obj_warning";
}

MeshErrorsInfo ObjectList::get_mesh_errors_info(const int obj_idx, const int vol_idx , wxString* sidebar_info , int* non_manifold_edges) const
{
    if (obj_idx < 0)
        return { {}, {} };

    const TriangleMeshStats& stats = vol_idx == -1 ?
        (*m_objects)[obj_idx]->get_object_stl_stats() :
        (*m_objects)[obj_idx]->volumes[vol_idx]->mesh().stats();

    if (!stats.repaired() && stats.manifold()) {
        return { {}, {} };
    }

    wxString tooltip, auto_repaired_info, remaining_info;

    if (stats.repaired()) {
        const int errors = get_repaired_errors_count(obj_idx, vol_idx);
        auto_repaired_info = format_wxstr(_L_PLURAL("%1$d error repaired", "%1$d errors repaired", errors), errors);
        tooltip += auto_repaired_info + "\n";
    }
    if (!stats.manifold()) {
        remaining_info = format_wxstr(_L_PLURAL("Error: %1$d non-manifold edge.", "Error: %1$d non-manifold edges.", stats.open_edges), stats.open_edges);

        tooltip += _L("Remaining errors") + ":\n";
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d non-manifold edge", "%1$d non-manifold edges", stats.open_edges), stats.open_edges) + "\n";
    }

    if (sidebar_info) {
        *sidebar_info = stats.manifold() ? auto_repaired_info : (remaining_info + (stats.repaired() ? ("\n" + auto_repaired_info) : ""));
    }

    if (non_manifold_edges)
        *non_manifold_edges = stats.open_edges;

    if (!sidebar_info)
        tooltip += "\n" + _L("Click the icon to repair model object");

    return { tooltip, get_warning_icon_name(stats) };
}

MeshErrorsInfo ObjectList::get_mesh_errors_info(wxString* sidebar_info , int* non_manifold_edges)
{
    wxDataViewItem item = GetSelection();
    if (!item)
        return { "", "" };

    int obj_idx, vol_idx;
    get_selected_item_indexes(obj_idx, vol_idx);

    if (obj_idx < 0) {
        if (sidebar_info)
            obj_idx = m_objects_model->GetObjectIdByItem(item);
        else
            return { "", "" };
    }
    return get_mesh_errors_info(obj_idx, vol_idx, sidebar_info, non_manifold_edges);
}

void ObjectList::set_tooltip_for_item(const wxPoint& pt)
{
    wxDataViewItem item;
    wxDataViewColumn* col;
    HitTest(pt, item, col);

    if (!item || GetSelectedItemsCount() > 1)
    {
        GetMainWindow()->SetToolTip("");
        return;
    }

    wxString tooltip = "";
    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();

    if (col->GetModelColumn() == (unsigned int)colEditing) {
        if (node->IsActionEnabled())
#ifdef __WXOSX__
            tooltip = _(L("Right click the icon to drop the object settings"));
#else
            tooltip = _(L("Click the icon to reset all settings of the object"));
#endif
    }
    else if (col->GetModelColumn() == (unsigned int)colPrint)
#ifdef __WXOSX__
        tooltip = _(L("Right click the icon to drop the object printable property"));
#else
        tooltip = _(L("Click the icon to toggle printable properties of the object"));
#endif
    else if (col->GetModelColumn() == (unsigned int)colSupportPaint) {
        if (node->HasSupportPainting())
            tooltip = _(L("Click the icon to edit support painting of the object"));

    }
    else if (col->GetModelColumn() == (unsigned int)colColorPaint) {
        if (node->HasColorPainting())
            tooltip = _(L("Click the icon to edit color painting for the object"));
    }
    else if (col->GetModelColumn() == (unsigned int)colSinking) {
        if (node->HasSinking())
            tooltip = _(L("Click the icon to shift this object to the bed"));
    }
    else if (col->GetModelColumn() == (unsigned int)colName && (pt.x >= 2 * wxGetApp().em_unit() && pt.x <= 4 * wxGetApp().em_unit()))
    {
        if (const ItemType type = m_objects_model->GetItemType(item);
            type & (itObject | itVolume)) {
            int obj_idx = m_objects_model->GetObjectIdByItem(item);
            int vol_idx = type & itVolume ? m_objects_model->GetVolumeIdByItem(item) : -1;
            tooltip = get_mesh_errors_info(obj_idx, vol_idx).tooltip;
        }
    }

    GetMainWindow()->SetToolTip(tooltip);
}

int ObjectList::get_selected_obj_idx() const
{
    if (GetSelectedItemsCount() == 1)
        return m_objects_model->GetIdByItem(m_objects_model->GetObject(GetSelection()));

    return -1;
}

ModelConfig& ObjectList::get_item_config(const wxDataViewItem& item) const
{
    static ModelConfig s_empty_config;

    assert(item);
    const ItemType type = m_objects_model->GetItemType(item);

    if (type & itPlate)
        return s_empty_config;

    const int obj_idx = m_objects_model->GetObjectIdByItem(item);
    const int vol_idx = type & itVolume ? m_objects_model->GetVolumeIdByItem(item) : -1;

    assert(obj_idx >= 0 || ((type & itVolume) && vol_idx >=0));
    return type & itVolume ?(*m_objects)[obj_idx]->volumes[vol_idx]->config :
           type & itLayer  ?(*m_objects)[obj_idx]->layer_config_ranges[m_objects_model->GetLayerRangeByItem(item)] :
                            (*m_objects)[obj_idx]->config;
}

void ObjectList::update_filament_values_for_items(const size_t filaments_count)
{
    for (size_t i = 0; i < m_objects->size(); ++i)
    {
        wxDataViewItem item = m_objects_model->GetItemById(i);
        if (!item) continue;

        auto object = (*m_objects)[i];
        wxString extruder;
        if (!object->config.has("extruder") || size_t(object->config.extruder()) > filaments_count) {
            extruder = "1";
            object->config.set_key_value("extruder", new ConfigOptionInt(1));
        }
        else {
            extruder = wxString::Format("%d", object->config.extruder());
        }
        m_objects_model->SetExtruder(extruder, item);

        static const char *keys[] = {"support_filament", "support_interface_filament"};
        for (auto key : keys)
            if (object->config.has(key) && object->config.opt_int(key) > filaments_count)
                object->config.erase(key);

        if (object->volumes.size() > 1) {
            for (size_t id = 0; id < object->volumes.size(); id++) {
                item = m_objects_model->GetItemByVolumeId(i, id);
                if (!item) continue;
                if (!object->volumes[id]->config.has("extruder") ||
                    size_t(object->volumes[id]->config.extruder()) > filaments_count) {
                    extruder = wxString::Format("%d", object->config.extruder());
                    object->volumes[id]->config.erase("extruder");
                }
                else {
                    extruder = wxString::Format("%d", object->volumes[id]->config.extruder());
                }

                m_objects_model->SetExtruder(extruder, item);

                for (auto key : keys)
                    if (object->volumes[id]->config.has(key) && object->volumes[id]->config.opt_int(key) > filaments_count)
                        object->volumes[id]->config.erase(key);
            }
        }
    }

    wxGetApp().plater()->update();
}

void ObjectList::update_filament_values_for_items_when_delete_filament(const size_t filament_id, const int replace_id)
{
    int replace_filament_id = replace_id == -1 ? 1 : (replace_id + 1);
    for (size_t i = 0; i < m_objects->size(); ++i) {
        wxDataViewItem item = m_objects_model->GetItemById(i);
        if (!item)
            continue;

        auto     object = (*m_objects)[i];
        wxString extruder;
        if (!object->config.has("extruder")) {
            extruder = std::to_string(1);
            object->config.set_key_value("extruder", new ConfigOptionInt(1));
        }
        else if (size_t(object->config.extruder()) == filament_id + 1) {
            extruder = std::to_string(replace_filament_id);
            object->config.set_key_value("extruder", new ConfigOptionInt(replace_filament_id));
        } else {
            int new_extruder = object->config.extruder() > filament_id ? object->config.extruder() - 1 : object->config.extruder();
            extruder = wxString::Format("%d", new_extruder);
            object->config.set_key_value("extruder", new ConfigOptionInt(new_extruder));
        }
        m_objects_model->SetExtruder(extruder, item);

        static const char *keys[] = {"support_filament", "support_interface_filament"};
        for (auto key : keys) {
            if (object->config.has(key)) {
                if(object->config.opt_int(key) == filament_id + 1)
                    object->config.erase(key);
                else {
                    int new_value = object->config.opt_int(key) > filament_id ? object->config.opt_int(key) - 1 : object->config.opt_int(key);
                    object->config.set_key_value(key, new ConfigOptionInt(new_value));
                }
            }
        }

            for (size_t id = 0; id < object->volumes.size(); id++) {
                item = m_objects_model->GetItemByVolumeId(i, id);
                if (!item)
                    continue;

                for (auto key : keys) {
                    if (object->volumes[id]->config.has(key)) {
                        if (object->volumes[id]->config.opt_int(key) == filament_id + 1)
                            object->volumes[id]->config.erase(key);
                        else {
                            int new_value = object->volumes[id]->config.opt_int(key) > filament_id ? object->volumes[id]->config.opt_int(key) - 1 :
                                                                                                     object->volumes[id]->config.opt_int(key);
                            object->config.set_key_value(key, new ConfigOptionInt(new_value));
                        }
                    }
                }

                if (!object->volumes[id]->config.has("extruder")) {
                    continue;
                }
                else if (size_t(object->volumes[id]->config.extruder()) == filament_id + 1) {
                    object->volumes[id]->config.set_key_value("extruder", new ConfigOptionInt(replace_filament_id));
                } else {
                    int new_extruder = object->volumes[id]->config.extruder() > filament_id ? object->volumes[id]->config.extruder() - 1 : object->volumes[id]->config.extruder();
                    extruder = wxString::Format("%d", new_extruder);
                    object->volumes[id]->config.set_key_value("extruder", new ConfigOptionInt(new_extruder));
                }

                m_objects_model->SetExtruder(extruder, item);
            }

        item = m_objects_model->GetItemById(i);
        ObjectDataViewModelNode *object_node = static_cast<ObjectDataViewModelNode *>(item.GetID());
        if (object_node->GetChildCount() == 0)
            continue;

        for (size_t i = 0; i < object_node->GetChildCount(); i++) {
            ObjectDataViewModelNode * layer_root_node = object_node->GetNthChild(i);
            if (layer_root_node->GetType() != ItemType::itLayerRoot)
                continue;
            for (size_t j = 0; j < layer_root_node->GetChildCount(); j++) {
                ObjectDataViewModelNode * layer_node = layer_root_node->GetNthChild(j);
                auto layer_item = wxDataViewItem((void *) layer_root_node->GetNthChild(j));
                if (!layer_item)
                    continue;
                auto l_iter = object->layer_config_ranges.find(layer_node->GetLayerRange());
                if (l_iter != object->layer_config_ranges.end()) {
                    auto& layer_range_item = *(l_iter);
                    if (layer_range_item.second.has("extruder") && layer_range_item.second.option("extruder")->getInt() == filament_id + 1) {
                        int new_extruder = replace_id == -1 ? 0 : (replace_id + 1);
                        extruder         = wxString::Format("%d", new_extruder);
                        layer_range_item.second.set("extruder", new_extruder);
                    } else {
                        int layer_filament_id = layer_range_item.second.option("extruder")->getInt();
                        int new_extruder      = layer_filament_id > filament_id ? layer_filament_id - 1 : layer_filament_id;
                        extruder              = wxString::Format("%d", new_extruder);
                        layer_range_item.second.set("extruder", new_extruder);
                    }
                    m_objects_model->SetExtruder(extruder, layer_item);
                }
            }
        }
    }
}


void ObjectList::update_plate_values_for_items()
{
#ifdef __WXOSX__
    AssociateModel(nullptr);
#endif
    PartPlateList& list = wxGetApp().plater()->get_partplate_list();
    for (size_t i = 0; i < m_objects->size(); ++i)
    {
        wxDataViewItem item = m_objects_model->GetItemById(i);
        if (!item) continue;

        int plate_idx = list.find_instance_belongs(i, 0);
        wxDataViewItem old_parent = m_objects_model->GetParent(item);
        ObjectDataViewModelNode* old_parent_node = (ObjectDataViewModelNode*)old_parent.GetID();
        int old_plate_idx = old_parent_node->GetPlateIdx();
        if (plate_idx == old_plate_idx)
            continue;

        Unselect(item);

        bool is_old_parent_expanded = IsExpanded(old_parent);
        bool is_expanded = IsExpanded(item);
        m_objects_model->OnPlateChange(plate_idx, item);
        if (is_old_parent_expanded)
            Expand(old_parent);
        ExpandAncestors(item);
        Expand(item);
        Select(item);
    }
#ifdef __WXOSX__
    AssociateModel(m_objects_model);
#endif
}

void ObjectList::update_name_for_items()
{
    m_objects_model->UpdateItemNames();

    wxGetApp().plater()->update();
}

void ObjectList::object_config_options_changed(const ObjectVolumeID& ov_id)
{
    if (ov_id.object == nullptr)
        return;

    ModelObjectPtrs& objects = wxGetApp().model().objects;
    ModelObject* mo = ov_id.object;
    ModelVolume* mv = ov_id.volume;

    wxDataViewItem obj_item = m_objects_model->GetObjectItem(mo);
    if (mv != nullptr) {
        size_t vol_idx;
        for (vol_idx = 0; vol_idx < mo->volumes.size(); vol_idx++) {
            if (mo->volumes[vol_idx] == mv)
                break;
        }
        assert(vol_idx < mo->volumes.size());

        SettingsFactory::Bundle cat_options = SettingsFactory::get_bundle(&mv->config.get(), false);
        wxDataViewItem vol_item = m_objects_model->GetVolumeItem(obj_item, vol_idx);
        if (cat_options.size() > 0) {
            add_settings_item(vol_item, &mv->config.get());
        }
        else {
            m_objects_model->DeleteSettings(vol_item);
        }
    }
    else {
        SettingsFactory::Bundle cat_options = SettingsFactory::get_bundle(&mo->config.get(), true);
        if (cat_options.size() > 0) {
            add_settings_item(obj_item, &mo->config.get());
        }
        else {
            m_objects_model->DeleteSettings(obj_item);
        }
    }
}

void ObjectList::printable_state_changed(const std::vector<ModelObject*> model_objects)
{
    std::vector<size_t> obj_idxs;
    for (const ModelObject* mo : model_objects) {
        if (mo == nullptr)
            continue;

        ModelInstance* mi       = mo->instances[0];
        wxDataViewItem obj_item = m_objects_model->GetObjectItem(mo);
        m_objects_model->SetObjectPrintableState(mi->printable ? piPrintable : piUnprintable, obj_item);

        int obj_idx = m_objects_model->GetObjectIdByItem(obj_item);
        obj_idxs.emplace_back(static_cast<size_t>(obj_idx));
    }

    sort(obj_idxs.begin(), obj_idxs.end());
    obj_idxs.erase(unique(obj_idxs.begin(), obj_idxs.end()), obj_idxs.end());

    wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_objects(obj_idxs);

    wxGetApp().plater()->update();
}

void ObjectList::printable_state_changed(const std::vector<ObjectVolumeID>& ov_ids)
{
    std::vector<ModelObject*> model_objects;
    model_objects.reserve(ov_ids.size());

    for (const ObjectVolumeID& ov_id : ov_ids) {
        if (ov_id.object != nullptr)
            model_objects.emplace_back(ov_id.object);
    }

    printable_state_changed(model_objects);
}

void ObjectList::assembly_plate_object_name()
{
    m_objects_model->assembly_name();
}

void ObjectList::selected_object(ObjectDataViewModelNode* item)
{
    if (!item) {
        return;
    }
    this->SetFocus();
    select_item(wxDataViewItem(item));
    ensure_current_item_visible();
    selection_changed();
}

void ObjectList::update_objects_list_filament_column(size_t filaments_count)
{
    assert(filaments_count >= 1);

    if (printer_technology() == ptSLA)
        filaments_count = 1;

    m_prevent_update_filament_in_config = true;

    if (m_objects)
        update_filament_values_for_items(filaments_count);

    update_filament_colors();

    set_filament_column_hidden(filaments_count == 1);
    auto em = em_unit(this);
    GetColumn(colEditing)->SetWidth(m_columns_width[colEditing]*em);

    m_prevent_update_filament_in_config = false;
}

void ObjectList::update_objects_list_filament_column_when_delete_filament(size_t filament_id, size_t filaments_count, int replace_filament_id)
{
    m_prevent_update_filament_in_config = true;

    if (m_objects)
        update_filament_values_for_items_when_delete_filament(filament_id, replace_filament_id);

    update_filament_colors();

    set_filament_column_hidden(filaments_count == 1);
    GetColumn(colEditing)->SetWidth(25);

    m_prevent_update_filament_in_config = false;
}

void ObjectList::update_filament_colors()
{
    m_objects_model->UpdateColumValues(colFilament);
    Refresh();
}

void ObjectList::update_name_column_width() const
{
    wxSize client_size = this->GetClientSize();
    bool p_vbar = this->GetParent()->HasScrollbar(wxVERTICAL);
    bool p_hbar = this->GetParent()->HasScrollbar(wxHORIZONTAL);

    auto em = em_unit(const_cast<ObjectList*>(this));
    int others_width = 0;
    for (int cn = colName; cn < colCount; cn++) {
        if (cn != colName) {
            if (!GetColumn(cn)->IsHidden())
                others_width += m_columns_width[cn];
        }
    }

    GetColumn(colName)->SetWidth(std::max(0, client_size.x - (others_width)*em));
}

void ObjectList::set_filament_column_hidden(const bool hide) const
{
    GetColumn(colFilament)->SetHidden(hide);
    update_name_column_width();
}

void ObjectList::set_variable_height_column_hidden(const bool hide) const
{
    GetColumn(colHeight)->SetHidden(hide);
    update_name_column_width();
}

void ObjectList::set_color_paint_hidden(const bool hide) const
{
    GetColumn(colColorPaint)->SetHidden(hide);
    update_name_column_width();
}

void ObjectList::set_support_paint_hidden(const bool hide) const
{
    GetColumn(colSupportPaint)->SetHidden(hide);
    update_name_column_width();
}

void GUI::ObjectList::set_sinking_hidden(const bool hide) const
{
    GetColumn(colSinking)->SetHidden(hide);
    update_name_column_width();
}

void ObjectList::update_filament_in_config(const wxDataViewItem& item)
{
    if (m_prevent_update_filament_in_config)
        return;

    ModelConfig* config = nullptr;
    const ItemType item_type = m_objects_model->GetItemType(item);
    if (item_type & itObject) {
        const int obj_idx = m_objects_model->GetIdByItem(item);
        config = &(*m_objects)[obj_idx]->config;
    }
    else {
        const int obj_idx = m_objects_model->GetIdByItem(m_objects_model->GetObject(item));
        if (item_type & itVolume){
             const int ui_volume_idx = m_objects_model->GetVolumeIdByItem(item);
             if (obj_idx < 0 || ui_volume_idx < 0)
                return;
             int volume_in3d_idx = m_objects_model->get_real_volume_index_in_3d(obj_idx,ui_volume_idx);
             config              = &(*m_objects)[obj_idx]->volumes[volume_in3d_idx]->config;
        }
        else if (item_type & itLayer)
            config = &get_item_config(item);
    }

    if (!config)
        return;

    m_config = config;

    take_snapshot(_u8L("Change Filament"));

    const int extruder = m_objects_model->GetExtruderNumber(item);
    m_config->set_key_value("extruder", new ConfigOptionInt(extruder));

    if (item_type & itObject) {
        const int obj_idx = m_objects_model->GetIdByItem(item);
        for (ModelVolume* mv : (*m_objects)[obj_idx]->volumes) {
            if (mv->config.has("extruder"))
                mv->config.erase("extruder");
        }
    }

    wxGetApp().plater()->update();
}

void ObjectList::update_name_in_model(const wxDataViewItem& item) const
{
    if (m_objects_model->GetItemType(item) & itPlate) {
        std::string name = m_objects_model->GetName(item).ToUTF8().data();
        int plate_idx = -1;
        const ItemType type0 = m_objects_model->GetItemType(item, plate_idx);
        if (plate_idx >= 0) {
            auto plate = wxGetApp().plater()->get_partplate_list().get_plate(plate_idx);
            if (plate->get_plate_name() != name) {
                plate->set_plate_name(name);
            }
            m_objects_model->SetCurSelectedPlateFullName(plate_idx, name);
        }
        return;
    }

    const int obj_idx = m_objects_model->GetObjectIdByItem(item);
    if (obj_idx < 0) return;
    const int volume_id = m_objects_model->GetVolumeIdByItem(item);

    take_snapshot(volume_id < 0 ? _u8L("Rename Object") : _u8L("Rename Part"));

    ModelObject* obj = object(obj_idx);
    if (m_objects_model->GetItemType(item) & itObject) {
        std::string name = m_objects_model->GetName(item).ToUTF8().data();
        if (obj->name != name) {
            const std::string previous_name = obj->name;
            obj->name = name;
            if (obj->volumes.size() == 1)
                obj->volumes[0]->name = obj->name;
            Slic3r::save_object_mesh(*obj);

            LifecycleEventContext ctx;
            ctx.name = name;
            ctx.previous_name = previous_name;
            ctx.id = std::to_string(obj->id().id);
            ctx.index = obj_idx;
            ctx.source = "object";
            fire_lifecycle_event(LifecycleEvent::ObjectRenamed, ctx);
        }
        return;
    }

    if (volume_id < 0) return;
    std::string name = m_objects_model->GetName(item).ToUTF8().data();
    if (obj->volumes[volume_id]->name == name)
        return;

    const std::string previous_name = obj->volumes[volume_id]->name;
    obj->volumes[volume_id]->name = name;

    LifecycleEventContext ctx;
    ctx.name = name;
    ctx.previous_name = previous_name;
    ctx.id = std::to_string(obj->volumes[volume_id]->id().id);
    ctx.index = volume_id;
    ctx.source = "volume";
    fire_lifecycle_event(LifecycleEvent::ObjectRenamed, ctx);
}

void ObjectList::update_name_in_list(int obj_idx, int vol_idx) const
{
    if (obj_idx < 0) return;
    wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item) & (itVolume | itObject)))
        return;

    wxString new_name = from_u8(object(obj_idx)->volumes[vol_idx]->name);
    if (new_name.IsEmpty() || m_objects_model->GetName(item) == new_name)
        return;

    m_objects_model->SetName(new_name, item);
}

void ObjectList::selection_changed()
{
    if (wxGetApp().is_closing()) return;
    if (m_prevent_list_events) return;

    fix_multiselection_conflicts();

    fix_cut_selection();

    if (!m_prevent_canvas_selection_update)
        update_selections_on_canvas();

    if (!GetSelection() || m_objects_model->GetItemType(GetSelection()) == itObject) {
        auto event = SimpleEvent(EVT_OBJ_LIST_OBJECT_SELECT);
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }

    if (const wxDataViewItem item = GetSelection())
    {
        const ItemType type = m_objects_model->GetItemType(item);
        if (type & (itLayer|itLayerRoot)) {
            wxGetApp().obj_layers()->reset_selection();

            if (type & itLayerRoot)
                wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event("", false);
            else {
                wxGetApp().obj_layers()->set_selectable_range(m_objects_model->GetLayerRangeByItem(item));
                wxGetApp().obj_layers()->update_scene_from_editor_selection();
            }
        }
    }

    part_selection_changed();
}

void ObjectList::copy_layers_to_clipboard()
{
    wxDataViewItemArray sel_layers;
    GetSelections(sel_layers);

    const int obj_idx = m_objects_model->GetObjectIdByItem(sel_layers.front());
    if (obj_idx < 0 || (int)m_objects->size() <= obj_idx)
        return;

    const t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;
    t_layer_config_ranges& cache_ranges = m_clipboard.get_ranges_cache();

    if (sel_layers.Count() == 1 && m_objects_model->GetItemType(sel_layers.front()) & itLayerRoot)
    {
        cache_ranges.clear();
        cache_ranges = ranges;
        return;
    }

    for (const auto& layer_item : sel_layers)
        if (m_objects_model->GetItemType(layer_item) & itLayer) {
            auto range = m_objects_model->GetLayerRangeByItem(layer_item);
            auto it = ranges.find(range);
            if (it != ranges.end())
                cache_ranges[it->first] = it->second;
        }
}

void ObjectList::paste_layers_into_list()
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(GetSelection());
    t_layer_config_ranges& cache_ranges = m_clipboard.get_ranges_cache();

    if (obj_idx < 0 || (int)m_objects->size() <= obj_idx ||
        cache_ranges.empty() || printer_technology() == ptSLA)
        return;

    const wxDataViewItem object_item = m_objects_model->GetItemById(obj_idx);
    wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(object_item);
    if (layers_item)
        m_objects_model->Delete(layers_item);

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    for (const auto& range : cache_ranges)
        ranges.emplace(range);

    layers_item = add_layer_root_item(object_item);

    changed_object(obj_idx);

    select_item(layers_item);
#ifndef __WXOSX__
    selection_changed();
#endif
}

void ObjectList::copy_settings_to_clipboard()
{
    wxDataViewItem item = GetSelection();
    assert(item.IsOk());
    if (m_objects_model->GetItemType(item) & itSettings)
        item = m_objects_model->GetParent(item);

    wxDataViewItemArray items;
    items.push_back(item);
    while ((m_objects_model->GetItemType(items.back()) & itObject) == 0) {
        if (m_objects_model->GetItemType(items.back()) & itLayerRoot)
            items.back() = m_objects_model->GetParent(items.back());
        else
            items.push_back(m_objects_model->GetParent(items.back()));
    }
    auto &config = m_clipboard.get_config_cache();
    config.clear();
    while (!items.empty()) {
        config.apply(get_item_config(items.back()).get());
        items.pop_back();
    }

    m_clipboard.set_type(ItemType(m_objects_model->GetItemType(item) | itSettings));
}

bool GUI::ObjectList::can_paste_settings_into_list()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.empty())
        return false;
    return m_clipboard.get_type() == (m_objects_model->GetItemType(sels.front()) | itSettings);
}

void ObjectList::paste_settings_into_list()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    take_snapshot(_u8L("Paste settings"));

    std::unique_ptr<t_config_option_keys> global_keys;

    for (auto item : sels) {

        if (m_objects_model->GetItemType(item) & itSettings)
            item = m_objects_model->GetParent(item);

        ItemType item_type = m_objects_model->GetItemType(item);
        if(!(item_type & (itObject | itVolume |itLayer)))
            return;

        DynamicPrintConfig& config_cache = m_clipboard.get_config_cache();
        assert(!config_cache.empty());

        auto keys = config_cache.keys();
        auto part_options = SettingsFactory::get_options(true);
        auto config       = &get_item_config(item);
        auto extruder     = config->option("extruder") ? config->option("extruder")->clone() : nullptr;
        config->reset();

        if (item_type & (itVolume | itLayer)) {
            if (global_keys == nullptr) {
                auto object = m_objects_model->GetParent(item);
                DynamicPrintConfig config;
                config.apply_only(*wxGetApp().get_tab(Preset::TYPE_PRINT)->get_config(), keys);
                config.apply_only(get_item_config(object).get(), keys);
                auto equals = config.equal(config_cache);
                global_keys.reset(new t_config_option_keys);
                auto keys2 = get_item_config(object).keys();
                std::copy_if(keys2.begin(), keys2.end(), std::back_inserter(*global_keys),
                             [&equals](auto &e) { return std::find(equals.begin(), equals.end(), e) == equals.end(); });
                keys.erase(std::remove_if(keys.begin(), keys.end(),
                             [&equals](auto &e) { return std::find(equals.begin(), equals.end(), e) != equals.end(); }), keys.end());
            }
            config->apply_only(*wxGetApp().get_tab(Preset::TYPE_PRINT)->get_config(), *global_keys);
        }

        for (const std::string& opt_key: keys) {
            if (item_type & (itVolume | itLayer) &&
                std::find(part_options.begin(), part_options.end(), opt_key) == part_options.end())
                continue;

            const ConfigOption* option = config_cache.option(opt_key);
            if (option)
                config->set_key_value(opt_key, option->clone());
        }
        if (extruder)
            config->set_key_value("extruder", extruder);
        else
            config->erase("extruder");

        add_settings_item(item, &config->get());
    }

    part_selection_changed();
}

void ObjectList::paste_volumes_into_list(int obj_idx, const ModelVolumePtrs& volumes)
{
    if ((obj_idx < 0) || ((int)m_objects->size() <= obj_idx))
        return;

    if (volumes.empty())
        return;

    wxDataViewItemArray items = reorder_volumes_and_get_selection(obj_idx, [volumes](const ModelVolume* volume) {
        return std::find(volumes.begin(), volumes.end(), volume) != volumes.end(); });
    if (items.size() > 1) {
        m_selection_mode = smVolume;
        m_last_selected_item = wxDataViewItem(nullptr);
    }

    select_items(items);
    selection_changed();

    notify_instance_updated(obj_idx);
}

void ObjectList::paste_objects_into_list(const std::vector<size_t>& object_idxs)
{
    if (object_idxs.empty())
        return;

    wxDataViewItemArray items;
    for (const size_t object : object_idxs)
    {
        add_object_to_list(object);
        items.Add(m_objects_model->GetItemById(object));
    }

    wxGetApp().plater()->changed_objects(object_idxs);

    select_items(items);
    selection_changed();
}

#ifdef __WXOSX__
#endif

void ObjectList::OnContextMenu(wxDataViewEvent& evt)
{
    wxPoint mouse_pos = this->get_mouse_position_in_control();

    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    bool evt_context_menu = (canvas != nullptr) ? !canvas->is_mouse_dragging() : true;
    if (!evt_context_menu)
        canvas->mouse_up_cleanup();

    list_manipulation(mouse_pos, evt_context_menu);
}

void ObjectList::list_manipulation(const wxPoint& mouse_pos, bool evt_context_menu )
{
    if (m_prevent_list_manipulation) return;

    if (mouse_pos.x < 0)
        return;

    wxDataViewItem item;
    wxDataViewColumn* col = nullptr;
    HitTest(mouse_pos, item, col);

    if (m_extruder_editor)
        m_extruder_editor->Hide();

    {
        if (col == nullptr) {
            if (wxOSX && !multiple_selection())
                UnselectAll();
            else if (!evt_context_menu)
                return;
        }

        if (evt_context_menu) {
            show_context_menu(evt_context_menu);
            return;
        }
    }

    if (wxOSX && item && col) {
        wxDataViewItemArray sels;
        GetSelections(sels);
        UnselectAll();
        if (sels.Count() > 1)
            SetSelections(sels);
        else
            Select(item);
    }

    if (col != nullptr)
    {
	    const wxString title = col->GetTitle();
        ColumnNumber col_num = (ColumnNumber)col->GetModelColumn();
        if (col_num == colHeight)
            enable_layers_editing();
        else if (col_num == colPrint)
	        toggle_printable_state();
        else if (col_num == colSupportPaint) {
            ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
            if (node && node->HasSupportPainting()) {
                GLGizmosManager& gizmos_mgr = wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager();
                if (gizmos_mgr.get_current_type() != GLGizmosManager::EType::FdmSupports)
                    gizmos_mgr.open_gizmo(GLGizmosManager::EType::FdmSupports);
                else
                    gizmos_mgr.reset_all_states();
            }
        }
        else if (col_num == colColorPaint) {
            if (wxGetApp().plater()->get_current_canvas3D()->get_canvas_type() != GLCanvas3D::CanvasAssembleView) {
                ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
                if (node && node->HasColorPainting()) {
                    GLGizmosManager& gizmos_mgr = wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager();
                    if (gizmos_mgr.get_current_type() != GLGizmosManager::EType::MmSegmentation)
                        gizmos_mgr.open_gizmo(GLGizmosManager::EType::MmSegmentation);
                    else
                        gizmos_mgr.reset_all_states();
                }
            }
        }
        else if (col_num == colSinking) {
            Plater *    plater = wxGetApp().plater();
            GLCanvas3D *cnv    = plater->canvas3D();
            if (cnv->get_canvas_type() == GLCanvas3D::ECanvasType::CanvasPreview) {
                return;
            }
            int obj_idx, vol_idx;
            get_selected_item_indexes(obj_idx, vol_idx, item);
            if (obj_idx != -1) {
                Plater::TakeSnapshot(plater, _u8L("Shift objects to bed"));
                (*m_objects)[obj_idx]->ensure_on_bed();
                cnv->reload_scene(true, true);
                update_info_items(obj_idx);
                notify_instance_updated(obj_idx);
            }
        }
        else if (col_num == colEditing) {
            int obj_idx, vol_idx;

            get_selected_item_indexes(obj_idx, vol_idx, item);
            if (m_objects_model->GetItemType(item) & itPlate)
                dynamic_cast<TabPrintPlate*>(wxGetApp().get_plate_tab())->reset_model_config();
            else if (m_objects_model->GetItemType(item) & itLayer)
                dynamic_cast<TabPrintLayer*>(wxGetApp().get_layer_tab())->reset_model_config();
            else if (item.IsOk())
                dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab(vol_idx >= 0))->reset_model_config();
            wxGetApp().params_panel()->notify_object_config_changed();
        }
        else if (col_num == colName)
        {
            if (m_objects_model->HasWarningIcon(item) &&
                mouse_pos.x > 2 * wxGetApp().em_unit() && mouse_pos.x < 4 * wxGetApp().em_unit())
                fix_through_cgal();
            else if (evt_context_menu)
                show_context_menu(evt_context_menu);
        }
	    else if (wxOSX && evt_context_menu && col_num == colFilament)
	        extruder_editing();
	}

#ifndef __WXMSW__
    GetMainWindow()->SetToolTip("");
#endif
}

void ObjectList::show_context_menu(const bool evt_context_menu)
{
    if (wxGetApp().plater()->get_current_canvas3D()->get_canvas_type() == GLCanvas3D::ECanvasType::CanvasPreview)
        return;

    wxMenu* menu {nullptr};
    Plater* plater = wxGetApp().plater();

    if (multiple_selection())
    {
        if (selected_instances_of_same_object())
            menu = plater->instance_menu();
        else
            menu = plater->multi_selection_menu();
    }
    else {
        const auto item = GetSelection();
        if (item)
        {
            const ItemType type = m_objects_model->GetItemType(item);
            if (!(type & (itPlate | itObject | itVolume | itInstance)))
                return;
            if (type & itVolume) {
                int obj_idx, vol_idx;
                get_selected_item_indexes(obj_idx, vol_idx, item);
                if (obj_idx < 0 || vol_idx < 0)
                    return;
                const ModelVolume *volume = object(obj_idx)->volumes[vol_idx];

                menu = volume->is_text() ? plater->text_part_menu() :
			volume->is_svg() ? plater->svg_part_menu() :
                    plater->part_menu();
            }
            else
                menu =  type & itPlate                                              ? plater->plate_menu() :
                        type & itInstance                                           ? plater->instance_menu() :
                        printer_technology() == ptFFF                               ? plater->object_menu() : plater->sla_object_menu();
            plater->SetPlateIndexByRightMenuInLeftUI(-1);
            if (type & itPlate) {
                int            plate_idx = -1;
                const ItemType type0      = m_objects_model->GetItemType(item, plate_idx);
                if (plate_idx >= 0) { 
                    plater->SetPlateIndexByRightMenuInLeftUI(plate_idx);
                }
            }
        }
        else if (evt_context_menu)
            menu = plater->default_menu();
    }

    if (menu)
        plater->PopupMenu(menu);
}

void ObjectList::extruder_editing()
{
    wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item) & (itVolume | itObject)))
        return;

    const int column_width = GetColumn(colFilament)->GetWidth() + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) + 5;

    wxPoint pos = this->get_mouse_position_in_control();
    wxSize size = wxSize(column_width, -1);
    pos.x = GetColumn(colName)->GetWidth() + GetColumn(colPrint)->GetWidth() + GetColumn(colHeight)->GetWidth() + 5;
    pos.y -= GetTextExtent("m").y;

    apply_extruder_selector(&m_extruder_editor, this, "1", pos, size);

    m_extruder_editor->SetSelection(m_objects_model->GetExtruderNumber(item));
    m_extruder_editor->Show();

    auto set_extruder = [this]()
    {
        wxDataViewItem item = GetSelection();
        if (!item) return;

        const int selection = m_extruder_editor->GetSelection();
        if (selection >= 0)
            m_objects_model->SetExtruder(m_extruder_editor->GetString(selection), item);

        m_extruder_editor->Hide();
        update_filament_in_config(item);
    };

    m_extruder_editor->Bind(wxEVT_COMBOBOX, [set_extruder](wxCommandEvent& evt)
    {
        set_extruder();
        evt.StopPropagation();
    });
}

void ObjectList::copy()
{
    wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_COPY));
}

void ObjectList::paste()
{
    wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_PASTE));
}

void ObjectList::cut()
{
    wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_CUT));
}

void ObjectList::clone()
{
    wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_CLONE));
}

bool ObjectList::cut_to_clipboard()
{
    return copy_to_clipboard();
}

bool ObjectList::copy_to_clipboard()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return false;
    ItemType type = m_objects_model->GetItemType(sels.front());
    if (!(type & (itSettings | itLayer | itLayerRoot))) {
        m_clipboard.reset();
        return false;
    }

    if (type & itSettings)
        copy_settings_to_clipboard();
    if (type & (itLayer | itLayerRoot))
        copy_layers_to_clipboard();

    m_clipboard.set_type(type);
    return true;
}

bool ObjectList::paste_from_clipboard()
{
    if (!(m_clipboard.get_type() & (itSettings | itLayer | itLayerRoot))) {
        m_clipboard.reset();
        return false;
    }

    if (m_clipboard.get_type() & itSettings)
        paste_settings_into_list();
    else if (m_clipboard.get_type() & (itLayer | itLayerRoot))
        paste_layers_into_list();

    return true;
}

void ObjectList::undo()
{
	wxGetApp().plater()->undo();
}

void ObjectList::redo()
{
	wxGetApp().plater()->redo();
}

void ObjectList::increase_instances()
{
    wxGetApp().plater()->increase_instances(1);
}

void ObjectList::decrease_instances()
{
    wxGetApp().plater()->decrease_instances(1);
}

#ifndef __WXOSX__
void ObjectList::key_event(wxKeyEvent& event)
{
    const std::optional<Shortcut> shortcut = wxGetApp().shortcuts().lookup(ShortcutContext::ObjectList, KeyChord::from_event(event));
    if (shortcut.has_value() && dispatch_shortcut(*shortcut))
        return;
    if (filaments_count() > 1) {
        std::vector<wxChar> numbers = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        wxChar key_char = event.GetUnicodeKey();
        if (std::find(numbers.begin(), numbers.end(), key_char) != numbers.end()) {
            long extruder_number;
            if (wxNumberFormatter::FromString(wxString(key_char), &extruder_number) &&
                filaments_count() >= extruder_number)
                set_extruder_for_selected_items(int(extruder_number));
        }
        else
            event.Skip();
    }
    else
        event.Skip();
}
#endif

#ifdef __WXOSX__
void ObjectList::update_shortcut_accelerators()
{
    std::vector<wxAcceleratorEntry> entries;
    const ShortcutRegistry& shortcuts = wxGetApp().shortcuts();
    for (Shortcut shortcut : shortcuts_in(ShortcutContext::ObjectList))
        if (const KeyChord chord = shortcuts.binding(shortcut); chord.valid())
            entries.push_back(chord.to_accelerator_entry(m_shortcut_id_base + int(shortcut)));
    for (int i = 1; i < 10; ++i) {
        entries.emplace_back(wxACCEL_NORMAL, '0' + i, wxID_LAST + i);
        entries.emplace_back(wxACCEL_NORMAL, WXK_NUMPAD0 + i, wxID_LAST + i);
    }
    m_accel = wxAcceleratorTable(int(entries.size()), entries.data());
    SetAcceleratorTable(m_accel);
}
#endif

bool ObjectList::dispatch_shortcut(Shortcut shortcut)
{
    switch (shortcut) {
    case Shortcut::DeleteSelected:  remove(); break;
    case Shortcut::SelectAll:       select_item_all_children(); break;
    case Shortcut::Copy:            copy(); break;
    case Shortcut::Paste:           paste(); break;
    case Shortcut::Cut:             cut(); break;
    case Shortcut::Undo:            undo(); break;
    case Shortcut::Redo:            redo(); break;
    case Shortcut::CloneSelected:   clone(); break;
    case Shortcut::AddInstance:     increase_instances(); break;
    case Shortcut::RemoveInstance:  decrease_instances(); break;
    case Shortcut::TogglePrintable: toggle_printable_state(); break;
    case Shortcut::ToggleAutoDrop:  toggle_auto_drop(); break;
    default: return false;
    }
    return true;
}

void ObjectList::OnBeginDrag(wxDataViewEvent &event)
{
    const bool mult_sel = multiple_selection();
    if (mult_sel) {
        event.Veto();
        return;
    }

    const wxDataViewItem item(event.GetItem());
    const ItemType& type = m_objects_model->GetItemType(item);
    if (!(type & (itVolume | itObject))) {
        event.Veto();
        return;
    }

    if (type & itObject) {
        m_dragged_data.init(m_objects_model->GetIdByItem(item), type);
    }
    else if (type & itVolume){
        m_dragged_data.init(m_objects_model->GetObjectIdByItem(item), m_objects_model->GetVolumeIdByItem(item), type);
    }
#if 0
    if ((mult_sel && !selected_instances_of_same_object()) ||
        (!mult_sel && (GetSelection() != item)) ) {
        event.Veto();
        return;
    }

    if (!(type & (itVolume | itObject | itInstance))) {
        event.Veto();
        return;
    }

    if (mult_sel)
    {
        m_dragged_data.init(m_objects_model->GetObjectIdByItem(item),type);
        std::set<int>& sub_obj_idxs = m_dragged_data.inst_idxs();
        wxDataViewItemArray sels;
        GetSelections(sels);
        for (auto sel : sels )
            sub_obj_idxs.insert(m_objects_model->GetInstanceIdByItem(sel));
    }
    else if (type & itObject)
        m_dragged_data.init(m_objects_model->GetIdByItem(item), type);
    else
        m_dragged_data.init(m_objects_model->GetObjectIdByItem(item),
                            type&itVolume ? m_objects_model->GetVolumeIdByItem(item) :
                                        m_objects_model->GetInstanceIdByItem(item),
                            type);
#endif

    m_prevent_list_events = true;
    wxTextDataObject* obj = new wxTextDataObject;
    obj->SetText("Some text");

    event.SetDataObject(obj);
    event.SetDragFlags(wxDrag_DefaultMove);
}

bool ObjectList::can_drop(const wxDataViewItem& item, int& src_obj_id, int& src_plate, int& dest_obj_id, int& dest_plate) const
{
    if (!item.IsOk()
        || (m_objects_model->GetItemType(item) != m_dragged_data.type())
        || !(m_dragged_data.type() & (itVolume|itObject)))
        return false;

    if (m_dragged_data.type() & itObject){
        int from_obj_id = m_dragged_data.obj_idx();
        int to_obj_id   = m_objects_model->GetIdByItem(item);
        PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();

        int from_plate = partplate_list.find_instance(from_obj_id, 0);
        if (from_plate == -1)
            return false;
        int to_plate = partplate_list.find_instance(to_obj_id, 0);
        if ((to_plate == -1) || (from_plate != to_plate))
            return false;

        src_obj_id = from_obj_id;
        dest_obj_id = to_obj_id;
        src_plate = from_plate;
        dest_plate = to_plate;

    }
    else if (m_dragged_data.type() & itVolume) {
        if (m_dragged_data.obj_idx() != m_objects_model->GetObjectIdByItem(item))
            return false;
        wxDataViewItem dragged_item = m_objects_model->GetItemByVolumeId(m_dragged_data.obj_idx(), m_dragged_data.sub_obj_idx());
        if (!dragged_item)
            return false;
        ModelVolumeType item_v_type = m_objects_model->GetVolumeType(item);
        ModelVolumeType dragged_item_v_type = m_objects_model->GetVolumeType(dragged_item);

        if (dragged_item_v_type == item_v_type && dragged_item_v_type != ModelVolumeType::MODEL_PART)
            return true;

        if (is_precise_seam(dragged_item_v_type) && is_precise_seam(item_v_type))
            return is_precise_seam_strong(dragged_item_v_type) == is_precise_seam_strong(item_v_type);

        if ((dragged_item_v_type != item_v_type) ||
            item_v_type >= ModelVolumeType::SUPPORT_BLOCKER)
            return false;

        bool only_one_solid_part = true;
        auto& volumes = (*m_objects)[m_dragged_data.obj_idx()]->volumes;
        for (size_t cnt, id = cnt = 0; id < volumes.size() && cnt < 2; id ++)
            if (volumes[id]->type() == ModelVolumeType::MODEL_PART) {
                if (++cnt > 1)
                    only_one_solid_part = false;
            }

        if (dragged_item_v_type == ModelVolumeType::MODEL_PART) {
            if (only_one_solid_part)
                return false;
            return (m_objects_model->GetVolumeIdByItem(item) == 0 ||
                    (m_dragged_data.sub_obj_idx()==0 && volumes[1]->type() == ModelVolumeType::MODEL_PART) ||
                    (m_dragged_data.sub_obj_idx()!=0 && volumes[0]->type() == ModelVolumeType::MODEL_PART));
        }
        if ((dragged_item_v_type == ModelVolumeType::NEGATIVE_VOLUME || dragged_item_v_type == ModelVolumeType::PARAMETER_MODIFIER)) {
            if (only_one_solid_part)
                return false;
            return m_objects_model->GetVolumeIdByItem(item) != 0;
        }
        return false;
    }
    return true;
}

void ObjectList::OnDropPossible(wxDataViewEvent &event)
{
    const wxDataViewItem& item = event.GetItem();

    int src_obj_id, src_plate, dest_obj_id, dest_plate;
    if (!can_drop(item, src_obj_id, src_plate, dest_obj_id, dest_plate)) {
        event.Veto();
        m_prevent_list_events = false;
    }
}

void ObjectList::OnDrop(wxDataViewEvent &event)
{
    const wxDataViewItem& item = event.GetItem();

    int src_obj_id, src_plate, dest_obj_id, dest_plate;
    if (!can_drop(item, src_obj_id, src_plate, dest_obj_id, dest_plate))
    {
        event.Veto();
        m_dragged_data.clear();
        return;
    }

    take_snapshot(_u8L("Object order changed"));

    if(m_dragged_data.type() & itObject){
        int delta = dest_obj_id < src_obj_id ? -1 : 1;
        PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();

        int cnt = 0;
        for (int id = src_obj_id; cnt < abs(src_obj_id - dest_obj_id); id += delta, cnt++)
            std::swap((*m_objects)[id], (*m_objects)[id + delta]);

        select_item(m_objects_model->ReorganizeObjects(src_obj_id, dest_obj_id));

        partplate_list.reload_all_objects(false, src_plate);
        changed_object(src_obj_id);
    }
    else if (m_dragged_data.type() & itVolume) {
        int from_volume_id = m_dragged_data.sub_obj_idx();
        int to_volume_id   = m_objects_model->GetVolumeIdByItem(item);
        int delta          = to_volume_id < from_volume_id ? -1 : 1;

        const int obj_idx = m_dragged_data.obj_idx();
        if (obj_idx < 0 || size_t(obj_idx) >= m_objects->size()) {
            event.Veto();
            m_dragged_data.clear();
            return;
        }
        const ModelObject *object = (*m_objects)[obj_idx];
        auto &volumes = (*m_objects)[obj_idx]->volumes;
        std::vector<size_t> visible_volume_indices;
        visible_volume_indices.reserve(volumes.size());
        for (size_t idx = 0; idx < volumes.size(); ++idx)
            if (!(object->is_cut() && volumes[idx]->is_cut_connector()))
                visible_volume_indices.push_back(idx);

        if (from_volume_id < 0 || to_volume_id < 0 ||
            size_t(from_volume_id) >= visible_volume_indices.size() || size_t(to_volume_id) >= visible_volume_indices.size()) {
            event.Veto();
            m_dragged_data.clear();
            return;
        }

        for (int id = from_volume_id; id != to_volume_id; id += delta) {
            const size_t current_idx = visible_volume_indices[id];
            const size_t next_idx = visible_volume_indices[id + delta];
            std::swap(volumes[current_idx], volumes[next_idx]);
        }

        auto &ui_to_model = m_objects_model->get_ui_and_3d_volume_map()[obj_idx];
        ui_to_model.clear();
        for (size_t idx = 0; idx < visible_volume_indices.size(); ++idx)
            ui_to_model[int(idx)] = int(visible_volume_indices[idx]);

        select_item(m_objects_model->ReorganizeChildren(from_volume_id, to_volume_id, m_objects_model->GetParent(item)));

        changed_object(m_dragged_data.obj_idx());
    }

    m_dragged_data.clear();

    wxGetApp().plater()->set_current_canvas_as_dirty();
}

void ObjectList::add_category_to_settings_from_selection(const std::vector< std::pair<std::string, bool> >& category_options, wxDataViewItem item)
{
    if (category_options.empty())
        return;

    const ItemType item_type = m_objects_model->GetItemType(item);

    if (!m_config)
        m_config = &get_item_config(item);

    assert(m_config);
    auto opt_keys = m_config->keys();

    const std::string snapshot_text =  item_type & itLayer   ? _u8L("Layer setting added") :
                                    item_type & itVolume  ? _u8L("Part setting added") :
                                                            _u8L("Object setting added");
    take_snapshot(snapshot_text);

    const DynamicPrintConfig& from_config = printer_technology() == ptFFF ?
                                            wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                            wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;

    for (auto& opt : category_options) {
        auto& opt_key = opt.first;
        if (find(opt_keys.begin(), opt_keys.end(), opt_key) != opt_keys.end() && !opt.second)
            m_config->erase(opt_key);

        if (find(opt_keys.begin(), opt_keys.end(), opt_key) == opt_keys.end() && opt.second) {
            const ConfigOption* option = from_config.option(opt_key);
            if (!option) {
                option = DynamicPrintConfig::new_from_defaults_keys({ opt_key })->option(opt_key);
            }
            m_config->set_key_value(opt_key, option->clone());
        }
    }

    if (!(item_type & (itPlate | itObject | itVolume | itLayer)))
        item = m_objects_model->GetObject(item);
    show_settings(add_settings_item(item, &m_config->get()));
}

void ObjectList::add_category_to_settings_from_frequent(const std::vector<std::string>& options, wxDataViewItem item)
{
    const ItemType item_type = m_objects_model->GetItemType(item);

    if (!m_config)
        m_config = &get_item_config(item);

    assert(m_config);
    auto opt_keys = m_config->keys();

    const std::string snapshot_text = item_type & itLayer  ? _u8L("Height range settings added") :
                                   item_type & itVolume ? _u8L("Part settings added") :
                                                          _u8L("Object settings added");
    take_snapshot(snapshot_text);

    const DynamicPrintConfig& from_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    for (auto& opt_key : options)
    {
        if (find(opt_keys.begin(), opt_keys.end(), opt_key) == opt_keys.end()) {
            const ConfigOption* option = from_config.option(opt_key);
            if (!option) {
                option = DynamicPrintConfig::new_from_defaults_keys({ opt_key })->option(opt_key);
            }
            m_config->set_key_value(opt_key, option->clone());
        }
    }

    if (!(item_type & (itPlate | itObject | itVolume | itLayer)))
        item = m_objects_model->GetObject(item);

    show_settings(add_settings_item(item, &m_config->get()));
}

void ObjectList::show_settings(const wxDataViewItem settings_item)
{
    if (!settings_item)
        return;

    select_item(settings_item);

    if (!m_prevent_canvas_selection_update)
        update_selections_on_canvas();
}

bool ObjectList::is_instance_or_object_selected()
{
    const Selection& selection = scene_selection();
    return selection.is_single_full_instance() || selection.is_single_full_object();
}

void ObjectList::load_subobject(ModelVolumeType type, bool from_galery )
{
    wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item)&(itObject|itInstance)))
        return;
    const int obj_idx = m_objects_model->GetObjectIdByItem(item);

    if (obj_idx < 0) return;

    if (m_objects_model->GetItemType(item)&itInstance)
        item = m_objects_model->GetItemById(obj_idx);

    wxArrayString input_files;
        wxGetApp().import_model(wxGetApp().tab_panel()->GetPage(0), input_files);

    if (input_files.IsEmpty())
        return;

    take_snapshot((type == ModelVolumeType::MODEL_PART) ? _u8L("Load Part") : _u8L("Load Modifier"));

    std::vector<ModelVolume*> volumes;
        load_modifier(input_files, *(*m_objects)[obj_idx], volumes, type, from_galery);

    if (volumes.empty())
        return;

    wxDataViewItemArray items = reorder_volumes_and_get_selection(obj_idx, [volumes](const ModelVolume* volume) {
        return std::find(volumes.begin(), volumes.end(), volume) != volumes.end(); });

    if (type == ModelVolumeType::MODEL_PART)
        wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

    if (items.size() > 1) {
        m_selection_mode = smVolume;
        m_last_selected_item = wxDataViewItem(nullptr);
    }
    select_items(items);

    selection_changed();

    notify_instance_updated(obj_idx);
}
void ObjectList::load_modifier(const wxArrayString& input_files, ModelObject& model_object, std::vector<ModelVolume*>& added_volumes, ModelVolumeType type, bool from_galery)
{

    wxWindow* parent = wxGetApp().tab_panel()->GetPage(0);

    ProgressDialog dlg(_L("Loading") + dots, "", 100, wxGetApp().mainframe, wxPD_AUTO_HIDE);

    wxBusyCursor busy;

    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0)
        return;

    const Selection& selection = scene_selection();
    assert(obj_idx == selection.get_object_idx());

    int instance_idx = *selection.get_instance_idxs().begin();
    assert(instance_idx != -1);
    if (instance_idx == -1)
        return;

    const BoundingBoxf3 instance_bb = model_object.instance_bounding_box(instance_idx);

    const GLVolume* v = selection.get_first_volume();
    const Geometry::Transformation inst_transform = v->get_instance_transformation();
    const Transform3d inv_inst_transform = inst_transform.get_matrix_no_offset().inverse();
    const Vec3d instance_offset = v->get_instance_offset();

    for (size_t i = 0; i < input_files.size(); ++i) {
        const std::string input_file = input_files.Item(i).ToUTF8().data();

        dlg.Update(static_cast<int>(100.0f * static_cast<float>(i) / static_cast<float>(input_files.size())),
            _L("Loading file") + ": " + from_path(boost::filesystem::path(input_file).filename()));
        dlg.Fit();
        
        bool is_user_cancel = false;
        Model model;
        try {
            if (boost::iends_with(input_file, ".stp") ||
                boost::iends_with(input_file, ".step")) {
                double linear  = string_to_double_decimal_point(wxGetApp().app_config->get("linear_deflection"));
                if (linear <= 0) linear = 0.003;
                double angle = string_to_double_decimal_point(wxGetApp().app_config->get("angle_deflection"));
                if (angle <= 0) angle = 0.5;
                bool split_compound = wxGetApp().app_config->get_bool("is_split_compound");
                model = Model::read_from_step(
                    input_file, LoadStrategy::LoadModel, nullptr, nullptr,
                    [&is_user_cancel, &linear, &angle, &split_compound](Slic3r::Step& file, double& linear_value,
                                                                                     double& angle_value, bool& is_split) -> int {
                        if (wxGetApp().app_config->get_bool("enable_step_mesh_setting")) {
                            StepMeshDialog mesh_dlg(nullptr, file, linear, angle);
                            if (mesh_dlg.ShowModal() == wxID_OK) {
                                linear_value = mesh_dlg.get_linear_deflection();
                                angle_value  = mesh_dlg.get_angle_deflection();
                                is_split     = mesh_dlg.get_split_compound_value();
                                return 1;
                            }
                        } else {
                            linear_value = linear;
                            angle_value  = angle;
                            is_split     = split_compound;
                            return 1;
                        }
                        is_user_cancel = true;
                        return -1;
                    },
                    linear, angle, split_compound);
            } else {
                model = Model::read_from_file(input_file, nullptr, nullptr, LoadStrategy::LoadModel);
            }
        }
        catch (std::exception&) {
            if (!is_user_cancel) {
                auto msg = _L("Error!") + " " + _L("Failed to get the model data in the current file.");
                show_error(parent, msg);
            }
            return;
        }

        if (from_galery)
            model.center_instances_around_point(Vec2d::Zero());
        else {
            for (auto object : model.objects) {
                if (model_object.origin_translation != Vec3d::Zero()) {
                    object->center_around_origin();
                    const Vec3d delta = model_object.origin_translation - object->origin_translation;
                    for (auto volume : object->volumes) {
                        volume->translate(delta);
                    }
                }
            }
        }

        model.add_default_instances();
        TriangleMesh mesh = model.mesh();
        ModelVolume* new_volume = model_object.add_volume(std::move(mesh), type);
        new_volume->name = boost::filesystem::path(input_file).filename().string();

        int extruder_id = 0;
        if (new_volume->type() == ModelVolumeType::MODEL_PART && model_object.config.has("extruder"))
            extruder_id = model_object.config.opt_int("extruder");
        new_volume->config.set_key_value("extruder", new ConfigOptionInt(extruder_id));
        new_volume->source.input_file = input_file;
        new_volume->source.object_idx = obj_idx;
        new_volume->source.volume_idx = int(model_object.volumes.size()) - 1;
        if (model.objects.size() == 1 && model.objects.front()->volumes.size() == 1)
            new_volume->source.mesh_offset = model.objects.front()->volumes.front()->source.mesh_offset;

        if (from_galery) {
            const BoundingBoxf3 mesh_bb = new_volume->mesh().bounding_box();
            new_volume->set_transformation(Geometry::Transformation::volume_to_bed_transformation(inst_transform, mesh_bb));
            const Vec3d offset = Vec3d(instance_bb.max.x(), instance_bb.min.y(), instance_bb.min.z()) + 0.5 * mesh_bb.size() - instance_offset;
            new_volume->set_offset(inv_inst_transform * offset);
        }
        else
            new_volume->set_offset(new_volume->source.mesh_offset - model_object.volumes.front()->source.mesh_offset);

        added_volumes.push_back(new_volume);
    }
}

static TriangleMesh create_mesh(const std::string& type_name, const BoundingBoxf3& bb)
{
    const double side = wxGetApp().plater()->canvas3D()->get_size_proportional_to_max_bed_size(0.1);

    TriangleMesh mesh;
    if (type_name == "Cube")
        mesh = TriangleMesh(its_make_cube(side, side, side));
    else if (type_name == "Cylinder")
        mesh = TriangleMesh(its_make_cylinder(0.5 * side, side));
    else if (type_name == "Sphere")
        mesh = TriangleMesh(its_make_sphere(0.5 * side, PI / 90));
    else if (type_name == "Slab")
        mesh = TriangleMesh(its_make_cube(bb.size().x() * 1.5, bb.size().y() * 1.5, bb.size().z() * 0.5));
    else if (type_name == "Cone")
        mesh = TriangleMesh(its_make_cone(0.5 * side, side));
    else if (type_name == "Disc")
        mesh = TriangleMesh(its_make_cylinder(0.5 * side, 0.2f));
    else if (type_name == "Torus")
        mesh = TriangleMesh(its_make_torus(0.5 * side, 0.125 * side,(PI / 60)));
    return mesh;
}

void ObjectList::load_generic_subobject(const std::string& type_name, const ModelVolumeType type)
{
    Plater::SingleSnapshot single(wxGetApp().plater());

    if (type == ModelVolumeType::INVALID) {
        load_shape_object(type_name);
        return;
    }

    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0)
        return;

    const Selection& selection = scene_selection();
    assert(obj_idx == selection.get_object_idx());

    int instance_idx = *selection.get_instance_idxs().begin();
    assert(instance_idx != -1);
    if (instance_idx == -1)
        return;

    take_snapshot(_u8L("Add primitive"));

    ModelObject  &model_object = *(*m_objects)[obj_idx];
    BoundingBoxf3 instance_bb = model_object.instance_bounding_box(instance_idx);

    TriangleMesh mesh = create_mesh(type_name, instance_bb);

    ModelVolume *new_volume = model_object.add_volume(std::move(mesh), type);

    const GLVolume* v = selection.get_first_volume();
    new_volume->set_transformation(v->get_instance_transformation().get_matrix_no_offset().inverse());
    const BoundingBoxf3 mesh_bb = new_volume->mesh().bounding_box();
    auto offset = (type_name == "Slab") ?
		Vec3d(0., 0., 0.5 * mesh_bb.size().z() + instance_bb.min.z() - v->get_instance_offset().z()) :
        Vec3d(instance_bb.max.x(), instance_bb.min.y(), instance_bb.min.z()) + 0.5 * mesh_bb.size() - v->get_instance_offset();
    new_volume->set_offset(v->get_instance_transformation().get_matrix_no_offset().inverse() * offset);

    Slic3r::save_object_mesh(model_object);

    const wxString name = _L("Generic") + "-" + _(type_name);
    new_volume->name = into_u8(name);

    int extruder_id = 0;
    if (new_volume->type() == ModelVolumeType::MODEL_PART && model_object.config.has("extruder"))
        extruder_id = model_object.config.opt_int("extruder");
    new_volume->config.set_key_value("extruder", new ConfigOptionInt(extruder_id));
    new_volume->source.is_from_builtin_objects = true;

    select_item([this, obj_idx, new_volume]() {
        wxDataViewItem sel_item;

        wxDataViewItemArray items = reorder_volumes_and_get_selection(obj_idx, [new_volume](const ModelVolume* volume) { return volume == new_volume; });
        if (!items.IsEmpty())
            sel_item = items.front();

        return sel_item;
    });
    if (type == ModelVolumeType::MODEL_PART)
        wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

    apply_object_instance_transfrom_to_all_volumes(&model_object);

    selection_changed();

    notify_instance_updated(obj_idx);

    wxGetApp().params_panel()->switch_to_object(true);

    if (wxGetApp().app_config->get("do_not_show_modifer_tips").empty()) {
        TipsDialog dlg(wxGetApp().mainframe, _L("Add Modifier"), _L("Switch to per-object setting mode to edit modifier settings."), "do_not_show_modifer_tips");
        dlg.ShowModal();
    }
}

void ObjectList::switch_to_object_process()
{
    wxGetApp().params_panel()->switch_to_object(true);

    if (wxGetApp().app_config->get("do_not_show_object_process_tips").empty()) {
        TipsDialog dlg(wxGetApp().mainframe, _L("Edit Process Settings"), _L("Switch to per-object setting mode to edit process settings of selected objects."), "do_not_show_object_process_tips");
        dlg.ShowModal();
    }
}

void ObjectList::load_shape_object(const std::string &type_name)
{
    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    if (selection.get_object_idx() != -1)
        return;

    const int obj_idx = m_objects->size();
    if (obj_idx < 0)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Add Primitive"));

    BoundingBoxf3 bb;
    TriangleMesh mesh = create_mesh(type_name, bb);
    const Slic3r::DynamicPrintConfig& full_config = wxGetApp().preset_bundle->full_config();
    if (type_name != "Cube" && full_config.has("fan_direction") && full_config.has("auxiliary_fan")) {
        FanDirection config_dir = full_config.option<ConfigOptionEnum<FanDirection>>("fan_direction")->value;
        orientation::orient_for_cooling(mesh, config_dir);
    }
    load_mesh_object(mesh, _(type_name));
    wxGetApp().mainframe->update_title();
}

void ObjectList::load_mesh_object(const TriangleMesh &mesh, const wxString &name, bool center)
{
    load_mesh_object(std::vector<std::pair<const TriangleMesh*, wxString>>{{&mesh, name}}, name, center);
}

void ObjectList::load_mesh_object(const std::vector<std::pair<const TriangleMesh*, wxString>> &parts, const wxString &name, bool center)
{
    Model& model = wxGetApp().plater()->model();

#ifdef _DEBUG
    check_model_ids_validity(model);
#endif

    std::vector<size_t> object_idxs;
    BoundingBoxf3 bb;
    ModelObject* new_object = model.add_object();
    new_object->name = into_u8(name);
    new_object->add_instance();
    for (const auto& [mesh, part_name] : parts) {
        bb.merge(mesh->bounding_box());
        ModelVolume* new_volume = new_object->add_volume(*mesh);
        new_volume->name = into_u8(part_name);
    }
    new_object->sort_volumes(true);
    new_object->config.set_key_value("extruder", new ConfigOptionInt(1));
    new_object->invalidate_bounding_box();
    new_object->translate(-bb.center());

    Slic3r::save_object_mesh(*new_object);

    auto start_point = wxGetApp().plater()->build_volume().bed_center();
    auto empty_cell  = wxGetApp().plater()->canvas3D()->get_nearest_empty_cell({start_point(0), start_point(1)});

    new_object->instances[0]->set_offset(center ? to_3d(Vec2d(empty_cell(0), empty_cell(1)), -new_object->origin_translation.z()) : bb.center());

    new_object->ensure_on_bed();

    Geometry::Transformation t = new_object->instances[0]->get_transformation();
    new_object->instances[0]->set_assemble_transformation(t);

    object_idxs.push_back(model.objects.size() - 1);
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif

    paste_objects_into_list(object_idxs);

#ifdef _DEBUG
    check_model_ids_validity(model);
#endif
}

bool ObjectList::del_object(const int obj_idx, bool refresh_immediately)
{
    return wxGetApp().plater()->delete_object_from_model(obj_idx, refresh_immediately);
}

void ObjectList::del_subobject_item(wxDataViewItem& item)
{
    if (!item) return;

    int obj_idx, idx;
    ItemType type;

    m_objects_model->GetItemInfo(item, type, obj_idx, idx);
    if (type == itUndef)
        return;

    wxDataViewItem parent = m_objects_model->GetParent(item);

    InfoItemType item_info_type = m_objects_model->GetInfoItemType(item);
    if (type & itSettings)
        del_settings_from_config(parent);
    else if (type & itInstanceRoot && obj_idx != -1)
        del_instances_from_object(obj_idx);
    else if (type & itLayerRoot && obj_idx != -1)
        del_layers_from_object(obj_idx);
    else if (type & itLayer && obj_idx != -1)
        del_layer_from_object(obj_idx, m_objects_model->GetLayerRangeByItem(item));
    else if (type & itInfo && obj_idx != -1)
        del_info_item(obj_idx, item_info_type);
    else if (idx == -1)
        return;
    else if (!del_subobject_from_object(obj_idx, idx, type))
        return;

    if (type & itVolume) {
        const std::string& icon_name = get_warning_icon_name(object(obj_idx)->get_object_stl_stats());
        m_objects_model->UpdateWarningIcon(parent, icon_name);
    }

    if (!(type & itInfo) || item_info_type != InfoItemType::CutConnectors) {
        m_objects_model->Delete(item);
        update_info_items(obj_idx);
    }
}

void ObjectList::del_info_item(const int obj_idx, InfoItemType type)
{
    Plater* plater = wxGetApp().plater();
    GLCanvas3D* cnv = plater->canvas3D();

    switch (type) {
    case InfoItemType::CustomSupports:
        cnv->get_gizmos_manager().reset_all_states();
        Plater::TakeSnapshot(plater, "Remove support painting");
        for (ModelVolume* mv : (*m_objects)[obj_idx]->volumes)
            mv->supported_facets.reset();
        break;

    case InfoItemType::CutConnectors:
        if (!del_from_cut_object(true)) {
            return;
        }
        break;

    case InfoItemType::MmSegmentation:
        cnv->get_gizmos_manager().reset_all_states();
        Plater::TakeSnapshot(plater, "Remove color painting");
        for (ModelVolume* mv : (*m_objects)[obj_idx]->volumes)
            mv->mmu_segmentation_facets.reset();
        break;

    case InfoItemType::Nonplanar:
        cnv->get_gizmos_manager().reset_all_states();
        Plater::TakeSnapshot(plater, _u8L("Erase non-planar painting"));
        for (ModelVolume *mv : (*m_objects)[obj_idx]->volumes)
            mv->nonplanar_facets.reset();
        break;

    case InfoItemType::FuzzySkin:
        cnv->get_gizmos_manager().reset_all_states();
        Plater::TakeSnapshot(plater, _u8L("Remove paint-on fuzzy skin"));
        for (ModelVolume* mv : (*m_objects)[obj_idx]->volumes)
            mv->fuzzy_skin_facets.reset();
        break;

    case InfoItemType::Undef : assert(false); break;
    }
    cnv->post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void ObjectList::del_settings_from_config(const wxDataViewItem& parent_item)
{
    const bool is_layer_settings = m_objects_model->GetItemType(parent_item) == itLayer;

    const size_t opt_cnt = m_config->keys().size();
    if ((opt_cnt == 1 && m_config->has("extruder")) ||
        (is_layer_settings && opt_cnt == 2 && m_config->has("extruder") && m_config->has("layer_height")))
        return;

    take_snapshot(_u8L("Delete Settings"));

    int extruder = m_config->has("extruder") ? m_config->extruder() : -1;

    coordf_t layer_height = 0.0;
    if (is_layer_settings)
        layer_height = m_config->opt_float("layer_height");

    m_config->reset();

    if (extruder >= 0)
        m_config->set_key_value("extruder", new ConfigOptionInt(extruder));
    if (is_layer_settings)
        m_config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));

    changed_object();
}

void ObjectList::del_instances_from_object(const int obj_idx)
{
    auto& instances = (*m_objects)[obj_idx]->instances;
    if (instances.size() <= 1)
        return;

    take_snapshot("");

    while ( instances.size()> 1)
        instances.pop_back();

    (*m_objects)[obj_idx]->invalidate_bounding_box();

    changed_object(obj_idx);
}

void ObjectList::del_layer_from_object(const int obj_idx, const t_layer_height_range& layer_range)
{
    const auto del_range = object(obj_idx)->layer_config_ranges.find(layer_range);
    if (del_range == object(obj_idx)->layer_config_ranges.end())
        return;

    take_snapshot(_u8L("Remove height range"));

    object(obj_idx)->layer_config_ranges.erase(del_range);

    changed_object(obj_idx);
}

void ObjectList::del_layers_from_object(const int obj_idx)
{
    object(obj_idx)->layer_config_ranges.clear();

    changed_object(obj_idx);
}

bool ObjectList::del_from_cut_object(bool is_cut_connector, bool is_model_part , bool is_negative_volume )
{
    const long buttons_style = is_cut_connector   ? (wxYES | wxNO | wxCANCEL) : (wxYES | wxCANCEL);

    const wxString title     = is_cut_connector   ? _L("Delete connector from object which is a part of cut") :
                               is_model_part      ? _L("Delete solid part from object which is a part of cut") :
                               is_negative_volume ? _L("Delete negative volume from object which is a part of cut") : "";
                             
    const wxString msg_end   = is_cut_connector   ? ("\n" + _L("To save cut correspondence you can delete all connectors from all related objects.")) : "";

    InfoDialog dialog(wxGetApp().plater(), title,
                      (_L("This action will break a cut correspondence.\nAfter that, model consistency can\'t be guaranteed.\n\nTo manipulate with solid parts or negative volumes you have to invalidate cut information first.") + msg_end ),
                      false, buttons_style | wxCANCEL_DEFAULT | wxICON_WARNING);

    dialog.SetButtonLabel(wxID_YES, _L("Invalidate cut info"));
    if (is_cut_connector)
        dialog.SetButtonLabel(wxID_NO, _L("Delete all connectors"));

    const int answer = dialog.ShowModal();
    if (answer == wxID_CANCEL)
        return false;

    if (answer == wxID_YES)
        invalidate_cut_info_for_selection();
    else if (answer == wxID_NO)
        delete_all_connectors_for_selection();
    return true;
}

bool ObjectList::del_subobject_from_object(const int obj_idx, const int idx, const int type)
{
    assert(idx >= 0);

    int n_plates = wxGetApp().plater()->get_partplate_list().get_plate_count();
	if ((obj_idx >= 1000 && obj_idx < 1000 + n_plates) || idx<0)
		return false;

    ModelObject* object = (*m_objects)[obj_idx];

    if (type == itVolume) {
        const auto volume = object->volumes[idx];

        int solid_cnt = 0;
        for (auto vol : object->volumes)
            if (vol->is_model_part())
                ++solid_cnt;
        if (volume->is_model_part() && solid_cnt == 1) {
            Slic3r::GUI::show_error(nullptr, _L("Deleting the last solid part is not allowed."));
            return false;
        }
        if (object->is_cut() && (volume->is_model_part() || volume->is_negative_volume())) {
            del_from_cut_object(volume->is_cut_connector(), volume->is_model_part(), volume->is_negative_volume());
            return false;
        }

        take_snapshot(_u8L("Delete part"));

        object->delete_volume(idx);

        if (object->volumes.size() == 1) {
            const auto last_volume = object->volumes[0];
            if (!last_volume->config.empty()) {
                object->config.apply(last_volume->config);
                last_volume->config.reset();

                wxDataViewItem obj_item = m_objects_model->GetItemById(obj_idx);
                if (obj_item) {
                    if (last_volume->config.has("extruder")) {
                        int extruder_id = last_volume->config.opt_int("extruder");
                        object->config.set("extruder", extruder_id);
                    }
                    wxString extruder = object->config.has("extruder") ? wxString::Format("%d", object->config.extruder()) : _devL("1");
                    m_objects_model->SetExtruder(extruder, obj_item);
                }
                add_settings_item(obj_item, &object->config.get());
            }
        }
        notify_instance_updated(obj_idx);
    }
    else if (type == itInstance) {
        if (object->instances.size() == 1) {
            Slic3r::GUI::show_error(nullptr, "");
            return false;
        }

        take_snapshot("");
        object->delete_instance(idx);
    }
    else
        return false;

    changed_object(obj_idx);

    return true;
}

void ObjectList::split()
{
    const auto item = GetSelection();
    const int obj_idx = get_selected_obj_idx();
    if (!item || obj_idx < 0)
        return;

    ModelVolume* volume;
    if (!get_volume_by_item(item, volume)) return;
    DynamicPrintConfig&	config = printer_config();
    const ConfigOptionStrings* filament_colors = config.option<ConfigOptionStrings>("filament_colour", false);
    const auto filament_cnt = (filament_colors == nullptr) ? size_t(1) : filament_colors->size();
    if (!volume->is_splittable()) {
        wxMessageBox(_(L("The target object contains only one part and can not be split.")));
        return;
    }

    take_snapshot(_u8L("Split to parts"));

    volume->split(filament_cnt, wxGetApp().app_config->get_bool("keep_painting"));

    wxBusyCursor wait;

    auto model_object = (*m_objects)[obj_idx];

    auto parent = m_objects_model->GetObject(item);
    if (parent)
        m_objects_model->DeleteVolumeChildren(parent);
    else
        parent = item;

    for (const ModelVolume* volume : model_object->volumes) {
        const wxDataViewItem& vol_item = m_objects_model->AddVolumeChild(parent, from_u8(volume->name),
            volume->type(),
            volume->is_text(),
            volume->is_svg(),
			get_warning_icon_name(volume->mesh().stats()),
            volume->config.has("extruder") ? volume->config.extruder() : 0,
            false);
        add_settings_item(vol_item, &volume->config.get());
    }

    model_object->input_file.clear();

    if (parent == item)
        Expand(parent);

    changed_object(obj_idx);

    notify_instance_updated(obj_idx);

    update_info_items(obj_idx);
}

void ObjectList::merge(bool to_multipart_object)
{
    wxBusyCursor wait;
    if (to_multipart_object) {
        auto get_object_idxs = [this](std::vector<int>& obj_idxs, wxDataViewItemArray& sels)
        {
            bool instance_selection = false;
            for (wxDataViewItem item : sels)
                if (m_objects_model->GetItemType(item) & itInstance) {
                    instance_selection = true;
                    break;
                }

            if (!instance_selection) {
                for (wxDataViewItem item : sels) {
                    assert(m_objects_model->GetItemType(item) & itObject);
                    obj_idxs.emplace_back(m_objects_model->GetIdByItem(item));
                }
                return;
            }

            std::map<int, std::set<int>> sel_map;
            std::set<int> empty_set;
            for (wxDataViewItem item : sels) {
                if (m_objects_model->GetItemType(item) & itObject) {
                    int obj_idx = m_objects_model->GetIdByItem(item);
                    int inst_cnt = (*m_objects)[obj_idx]->instances.size();
                    if (inst_cnt == 1)
                        sel_map.emplace(obj_idx, empty_set);
                    else
                        for (int i = 0; i < inst_cnt; i++)
                            sel_map[obj_idx].emplace(i);
                    continue;
                }
                int obj_idx = m_objects_model->GetIdByItem(m_objects_model->GetObject(item));
                sel_map[obj_idx].emplace(m_objects_model->GetInstanceIdByItem(item));
            }

            int new_objects_cnt = 0;

            for (auto map_item : sel_map) {
                int obj_idx = map_item.first;
                if (map_item.second.empty()) {
                    obj_idxs.emplace_back(obj_idx);
                    continue;
                }

                if ((*m_objects)[map_item.first]->instances.size() == map_item.second.size()) {
                    instances_to_separated_objects(obj_idx);
                    obj_idxs.emplace_back(obj_idx);
                    new_objects_cnt += map_item.second.size() - 1;
                    continue;
                }

                instances_to_separated_object(obj_idx, map_item.second);

                if (map_item.second.size() == 1)
                    new_objects_cnt += 1;
                else {
                    instances_to_separated_objects(m_objects->size() - 1);
                    new_objects_cnt += map_item.second.size();
                }
            }

            m_prevent_list_events = true;
            sels.Clear();
            for (int obj_idx : obj_idxs)
                sels.Add(m_objects_model->GetItemById(obj_idx));
            int obj_cnt = m_objects->size();
            for (int obj_idx = obj_cnt - new_objects_cnt; obj_idx < obj_cnt; obj_idx++) {
                sels.Add(m_objects_model->GetItemById(obj_idx));
                obj_idxs.emplace_back(obj_idx);
            }
            UnselectAll();
            SetSelections(sels);
            assert(!sels.IsEmpty());
            m_prevent_list_events = false;
        };

        std::vector<int> obj_idxs;
        wxDataViewItemArray sels;
        GetSelections(sels);
        assert(!sels.IsEmpty());

        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Assemble"));

        get_object_idxs(obj_idxs, sels);

        Model* model = (*m_objects)[0]->get_model();
        ModelObject* new_object = model->add_object();
        new_object->name = _u8L("Assembly");
        ModelConfig &config = new_object->config;

        Slic3r::SaveObjectGaurd gaurd(*new_object);

        for (int obj_idx : obj_idxs) {
            ModelObject* object = (*m_objects)[obj_idx];

            const Geometry::Transformation& transformation = object->instances[0]->get_transformation();

            if (object->id() == (*m_objects)[obj_idxs.front()]->id())
                new_object->add_instance();
            const Transform3d& transformation_matrix = transformation.get_matrix();

            for (const ModelVolume* volume : object->volumes) {
                ModelVolume* new_volume = new_object->add_volume(*volume);

                const Transform3d& volume_matrix = new_volume->get_matrix();
                Transform3d new_matrix = transformation_matrix * volume_matrix;
                new_volume->set_transformation(new_matrix);
                if (object->volumes.size() > 1){
                    new_volume->config.assign_config(volume->config);
                }
                auto option = new_volume->config.option("extruder");
                if (!option) {
                    auto opt = object->config.option("extruder");
                    if (opt) { new_volume->config.set_key_value("extruder", new ConfigOptionInt(opt->getInt())); }
                }
            }
            new_object->sort_volumes(true);

            auto new_opt_keys = config.keys();
            const ModelConfig& from_config = object->config;
            auto opt_keys = from_config.keys();

            for (auto& opt_key : opt_keys) {
                if (find(new_opt_keys.begin(), new_opt_keys.end(), opt_key) == new_opt_keys.end()) {
                    const ConfigOption* option = from_config.option(opt_key);
                    if (!option) {
                        option = DynamicPrintConfig::new_from_defaults_keys({ opt_key })->option(opt_key);
                    }
                    config.set_key_value(opt_key, option->clone());
                }
            }
            if (object->volumes.size() == 1 && find(opt_keys.begin(), opt_keys.end(), "extruder") != opt_keys.end()) {
                ModelVolume* volume = new_object->volumes.back();
                const ConfigOption* option = from_config.option("extruder");
                if (option)
                    volume->config.set_key_value("extruder", option->clone());
            }

            if (object->instances[0]->printable == false) {
                new_object->printable = false;
                new_object->instances[0]->printable = false;
            }
            if (object->instances[0]->auto_drop == false) {
                new_object->instances[0]->auto_drop = false;
            }

            for (const auto& range : object->layer_config_ranges)
                new_object->layer_config_ranges.emplace(range);

            BrimPoints temp_brim_points = object->brim_points;
            for(auto& p : temp_brim_points) {
                p.set_transform(transformation_matrix);
                new_object->brim_points.push_back(p);
            }
        }

        new_object->ensure_on_bed();
        new_object->center_around_origin();
        new_object->translate_instances(-new_object->origin_translation);
        new_object->origin_translation = Vec3d::Zero();
        Geometry::Transformation new_object_trsf = new_object->instances[0]->get_transformation();
        new_object->instances[0]->set_assemble_transformation(new_object_trsf);

        const Transform3d& new_object_inverse_matrix = new_object_trsf.get_matrix().inverse();
        for (auto& p : new_object->brim_points) {
            p.set_transform(new_object_inverse_matrix);
        }
        notify_instance_updated(m_objects->size() - 1);

        remove();

        add_object_to_list(m_objects->size() - 1);
        if (new_object->printable == false) {
            wxGetApp().obj_list()->printable_state_changed({new_object});
        }
        select_item(m_objects_model->GetItemById(m_objects->size() - 1));
        update_selections_on_canvas();
    }
    else {
        wxDataViewItem item = GetSelection();
        if (!item)
            return;
        const int obj_idx = m_objects_model->GetIdByItem(item);
        if (obj_idx == -1)
            return;

        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Merge parts to an object"));

        ModelObject* model_object = (*m_objects)[obj_idx];
        model_object->merge();

        m_objects_model->DeleteVolumeChildren(item);

        changed_object(obj_idx);
    }
}

void ObjectList::layers_editing()
{
    const auto& print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (print_config.opt_bool("enable_mixed_color_sublayer")) {
        if (wxGetApp().app_config->get("no_warn_mixed_sublayer_variable_layer") != "1") {
            MessageDialog dlg(wxGetApp().plater(),
                _L("Using variable layer height together with mixed color sublayer may result in poor color mixing quality."),
                _L("Warning"), wxICON_WARNING | wxOK);
            dlg.show_dsa_button();
            dlg.ShowModal();
            if (dlg.get_checkbox_state())
                wxGetApp().app_config->set("no_warn_mixed_sublayer_variable_layer", "1");
        }
    }

    const Selection& selection = scene_selection();
    const int obj_idx = selection.get_object_idx();
    wxDataViewItem item = obj_idx >= 0 && GetSelectedItemsCount() > 1 && selection.is_single_full_object() ?
                          m_objects_model->GetItemById(obj_idx) :
                          GetSelection();

    if (!item)
        return;

    const wxDataViewItem obj_item = m_objects_model->GetObject(item);
    wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(obj_item);

    if (!layers_item.IsOk())
    {
        t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

        if (ranges.empty()) {
            take_snapshot(_u8L("Add layers"));
            ranges[{ 0.0f, 2.0f }].assign_config(get_default_layer_config(obj_idx));
        }

        layers_item = add_layer_root_item(obj_item);
    }
    if (!layers_item.IsOk())
        return;

    wxGetApp().obj_layers()->reset_selection();
    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event("", false);

    select_item(layers_item);
    Expand(layers_item);
}

void ObjectList::boolean()
{
    std::vector<int> obj_idxs, vol_idxs;
    get_selection_indexes(obj_idxs, vol_idxs);
    if (obj_idxs.empty() && vol_idxs.empty())
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "boolean");

    ModelObject* object = (*m_objects)[obj_idxs.front()];

    const bool keep_painting = wxGetApp().app_config->get_bool("keep_painting");
    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings;
    if (keep_painting) {
        saved_paintings.reserve(object->volumes.size());
        for (const ModelVolume* vol : object->volumes) {
            if (vol && vol->mesh_ptr() && vol->is_model_part() && vol->is_any_painted()) {
                saved_paintings.emplace_back(vol->save_painting());
                if (saved_paintings.back()) {
                    saved_paintings.back()->mesh.transform(vol->get_matrix(), true);
                }
            }
        }
    }

    TriangleMesh mesh = Plater::combine_mesh_fff(*object, -1, [](const std::string& msg) {return wxGetApp().notification_manager()->push_plater_error_notification(msg); });

    Model* model = object->get_model();
    ModelObject* new_object = model->add_object();
    new_object->name = object->name;
    new_object->config.assign_config(object->config);
    if (new_object->instances.empty())
        new_object->add_instance();
    ModelVolume* new_volume = new_object->add_volume(mesh);

    if (keep_painting) {
        for (auto& saved_painting : saved_paintings) {
            if (saved_painting) {
                TriangleMesh vols_mesh(std::move(saved_painting->mesh));
                
                for (const ModelInstance* i : object->instances) {
                    saved_painting->mesh = vols_mesh;
                    saved_painting->mesh.transform(i->get_matrix());

                    new_volume->restore_painting(saved_painting, true);
                }

            }
        }
    }

    new_object->ensure_on_bed();
    new_object->center_around_origin();
    new_object->translate_instances(-new_object->origin_translation);
    new_object->origin_translation = Vec3d::Zero();

    notify_instance_updated(m_objects->size() - 1);

    remove();

    add_object_to_list(m_objects->size() - 1);
    select_item(m_objects_model->GetItemById(m_objects->size() - 1));
    update_selections_on_canvas();
}

wxDataViewItem ObjectList::add_layer_root_item(const wxDataViewItem obj_item)
{
    const int obj_idx = m_objects_model->GetIdByItem(obj_item);
    if (obj_idx < 0 ||
        object(obj_idx)->layer_config_ranges.empty() ||
        printer_technology() == ptSLA)
        return wxDataViewItem(nullptr);

    wxDataViewItem layers_item = m_objects_model->AddLayersRoot(obj_item);

    for (const auto& range : object(obj_idx)->layer_config_ranges)
        add_layer_item(range.first, layers_item);

    Expand(layers_item);
    return layers_item;
}

DynamicPrintConfig ObjectList::get_default_layer_config(const int obj_idx)
{
    DynamicPrintConfig config;
    coordf_t layer_height = object(obj_idx)->config.has("layer_height") ?
                            object(obj_idx)->config.opt_float("layer_height") :
                            wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("layer_height");
    config.set_key_value("layer_height",new ConfigOptionFloat(layer_height));
    int extruder = object(obj_idx)->config.has("extruder") ?
        object(obj_idx)->config.opt_int("extruder") :
        wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("extruder");
    config.set_key_value("extruder",    new ConfigOptionInt(0));

    return config;
}

bool ObjectList::get_volume_by_item(const wxDataViewItem& item, ModelVolume*& volume)
{
    auto obj_idx = get_selected_obj_idx();
    if (!item || obj_idx < 0)
        return false;
    const auto volume_id = m_objects_model->GetVolumeIdByItem(item);
    const bool split_part = m_objects_model->GetItemType(item) == itVolume;

    if (volume_id < 0) {
        if ( split_part || (*m_objects)[obj_idx]->volumes.size() > 1 )
            return false;
        volume = (*m_objects)[obj_idx]->volumes[0];
    }
    else
        volume = (*m_objects)[obj_idx]->volumes[volume_id];

    return true;
}

bool ObjectList::is_splittable(bool to_objects)
{
    const wxDataViewItem item = GetSelection();
    if (!item) return false;

    if (to_objects)
    {
        ItemType type = m_objects_model->GetItemType(item);
        if (type == itVolume)
            return false;
        if (type == itObject || m_objects_model->GetItemType(m_objects_model->GetParent(item)) == itObject) {
            auto obj_idx = get_selected_obj_idx();
            if (obj_idx < 0)
                return false;
            if ((*m_objects)[obj_idx]->volumes.size() > 1)
                return true;
            return (*m_objects)[obj_idx]->volumes[0]->is_splittable();
        }
        return false;
    }

    ModelVolume* volume;
    if (!get_volume_by_item(item, volume) || !volume)
        return false;

    return volume->is_splittable();
}

bool ObjectList::selected_instances_of_same_object()
{
    wxDataViewItemArray sels;
    GetSelections(sels);

    const int obj_idx = m_objects_model->GetObjectIdByItem(sels.front());

    for (auto item : sels) {
        if (! (m_objects_model->GetItemType(item) & itInstance) ||
            obj_idx != m_objects_model->GetObjectIdByItem(item))
            return false;
    }
    return true;
}

bool ObjectList::can_split_instances()
{
    const Selection& selection = scene_selection();
    return selection.is_multiple_full_instance() || selection.is_single_full_instance();
}

bool ObjectList::has_selected_cut_object() const
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return false;

    for (wxDataViewItem item : sels) {
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);
        if (obj_idx >= 0 && obj_idx < int(m_objects->size()) && object(obj_idx)->is_cut())
            return true;
    }

    return false;
}

void ObjectList::invalidate_cut_info_for_selection()
{
    const wxDataViewItem item = GetSelection();
    if (item) {
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);
        if (obj_idx >= 0)
            invalidate_cut_info_for_object(size_t(obj_idx));
    }
}

void ObjectList::invalidate_cut_info_for_object(int obj_idx)
{
    ModelObject* init_obj = object(obj_idx);
    if (!init_obj->is_cut())
        return;

    take_snapshot(_u8L("Invalidate cut info"));

    const CutObjectBase cut_id = init_obj->cut_id;
    for (size_t idx = 0; idx < m_objects->size(); idx++)
        if (ModelObject* obj = object(int(idx)); obj->cut_id.is_equal(cut_id)) {
            obj->invalidate_cut();
            update_info_items(idx);
            add_volumes_to_object_in_list(idx);
        }

    update_lock_icons_for_model();
}

void ObjectList::delete_all_connectors_for_selection()
{
    const wxDataViewItem item = GetSelection();
    if (item) {
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);
        if (obj_idx >= 0)
            delete_all_connectors_for_object(size_t(obj_idx));
    }
}

void ObjectList::delete_all_connectors_for_object(int obj_idx)
{
    ModelObject* init_obj = object(obj_idx);
    if (!init_obj->is_cut())
        return;

    take_snapshot(_u8L("Delete all connectors"));

    const CutObjectBase cut_id = init_obj->cut_id;
    Model& model = wxGetApp().plater()->model();
    for (int idx = int(m_objects->size())-1; idx >= 0; idx--)
        if (ModelObject* obj = object(idx); obj->cut_id.is_equal(cut_id)) {
            obj->delete_connectors();

            if (obj->volumes.empty() || !obj->has_solid_mesh()) {
                const std::string deleted_obj_name = obj->name;
                model.delete_object(idx);
                {
                    Slic3r::LifecycleEventContext ctx;
                    ctx.name = deleted_obj_name;
                    ctx.code = Slic3r::LifecycleEvtCode::Ok;
                    Slic3r::fire_lifecycle_event(Slic3r::LifecycleEvent::ObjectDeleted, ctx);
                }
                m_objects_model->Delete(m_objects_model->GetItemById(idx));
                continue;
            }

            update_info_items(idx);
            add_volumes_to_object_in_list(idx);
            changed_object(int(idx));
        }

    update_lock_icons_for_model();
}

bool ObjectList::can_merge_to_multipart_object() const
{
    if (has_selected_cut_object())
        return false;

    if (printer_technology() == ptSLA)
        return false;

    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return false;

    for (wxDataViewItem item : sels)
        if (!(m_objects_model->GetItemType(item) & (itObject | itInstance)))
            return false;

    return true;
}

bool ObjectList::can_merge_to_single_object() const
{
    int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0)
        return false;

    return (*m_objects)[obj_idx]->volumes.size() > 1;
}

bool ObjectList::can_mesh_boolean() const
{
    int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0)
        return false;

    return (*m_objects)[obj_idx]->volumes.size() > 1 || ((*m_objects)[obj_idx]->volumes.size() == 1 && (*m_objects)[obj_idx]->volumes[0]->is_splittable());
}

void ObjectList::changed_object(const int obj_idx ) const
{
    wxGetApp().plater()->changed_object(obj_idx < 0 ? get_selected_obj_idx() : obj_idx);
}

void ObjectList::part_selection_changed()
{
    if (wxGetApp().is_closing())
        return;

    if (m_extruder_editor) m_extruder_editor->Hide();
    int obj_idx = -1;
    int volume_id = -1;
    m_config = nullptr;
    wxString og_name = wxEmptyString;

    bool update_and_show_manipulations = false;
    bool update_and_show_settings = false;
    bool update_and_show_layers = false;

    bool enable_manipulation{true};
    bool disable_ss_manipulation{false};
    bool disable_ununiform_scale{false};

    const auto item = GetSelection();
    if (item && m_objects_model->GetItemType(item) == itInfo && m_objects_model->GetInfoItemType(item) == InfoItemType::CutConnectors) {
        og_name = _L("Cut Connectors information");

        update_and_show_manipulations = true;
        enable_manipulation           = false;
        disable_ununiform_scale       = true;
    }
    else if (item && (m_objects_model->GetItemType(item) & itPlate)) {
    }
    else if ( multiple_selection() || (item && m_objects_model->GetItemType(item) == itInstanceRoot )) {
        const Selection& selection = scene_selection();

        if (selection.is_single_full_object()) {
            og_name                       = _L("Object manipulation");
            update_and_show_manipulations = true;

            obj_idx                 = selection.get_object_idx();
            ModelObject *object     = (*m_objects)[obj_idx];
            m_config                = &object->config;
            disable_ss_manipulation = object->is_cut();
        }
        else {
            og_name = _L("Group manipulation");

            update_and_show_manipulations = !selection.is_single_full_instance();

            if (int obj_idx = selection.get_object_idx(); obj_idx >= 0) {
                if (selection.is_any_volume() || selection.is_any_modifier())
                    enable_manipulation = !(*m_objects)[obj_idx]->is_cut();
                else
                    disable_ss_manipulation = (*m_objects)[obj_idx]->is_cut();
            }
            else {
                wxDataViewItemArray sels;
                GetSelections(sels);
                if (selection.is_single_full_object() || selection.is_multiple_full_instance()) {
                    int obj_idx             = m_objects_model->GetObjectIdByItem(sels.front());
                    disable_ss_manipulation = (*m_objects)[obj_idx]->is_cut();
                } else if (selection.is_mixed() || selection.is_multiple_full_object()) {
                    std::map<CutObjectBase, std::set<int>> cut_objects;

                    for (auto item : sels) {
                        int                obj_idx = m_objects_model->GetObjectIdByItem(item);
                        const ModelObject *obj     = object(obj_idx);
                        if (obj && obj->is_cut()) {
                            if (cut_objects.find(obj->cut_id) == cut_objects.end())
                                cut_objects[obj->cut_id] = std::set<int>{obj_idx};
                            else
                                cut_objects.at(obj->cut_id).insert(obj_idx);
                        }
                    }

                    for (auto cut_object : cut_objects)
                        if (cut_object.first.check_sum() != cut_object.second.size()) {
                            disable_ss_manipulation = true;
                            break;
                        }
                    disable_ununiform_scale = !cut_objects.empty();
                }
            }
        }

        update_and_show_settings = true;
    }
    else {
        if (item) {
            const ItemType type = m_objects_model->GetItemType(item);
            const wxDataViewItem parent = m_objects_model->GetParent(item);
            const ItemType parent_type = m_objects_model->GetItemType(parent);
            obj_idx = m_objects_model->GetObjectIdByItem(item);

            if ((type & itObject) || type == itInfo) {
                m_config = &(*m_objects)[obj_idx]->config;
                update_and_show_manipulations = true;

                if (type == itInfo) {
                    InfoItemType info_type = m_objects_model->GetInfoItemType(item);
                    switch (info_type)
                    {
                    case InfoItemType::CustomSupports:
                    case InfoItemType::MmSegmentation:
                    case InfoItemType::Nonplanar:
                    case InfoItemType::FuzzySkin:
                    {
                        GLGizmosManager::EType gizmo_type = info_type == InfoItemType::CustomSupports ? GLGizmosManager::EType::FdmSupports :
                                                            info_type == InfoItemType::Nonplanar        ? GLGizmosManager::EType::Nonplanar :
                                                            info_type == InfoItemType::FuzzySkin        ? GLGizmosManager::EType::FuzzySkin :
                                                            GLGizmosManager::EType::MmSegmentation;
                        GLGizmosManager& gizmos_mgr = wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager();
                        if (gizmos_mgr.get_current_type() != gizmo_type)
                            gizmos_mgr.open_gizmo(gizmo_type);
                        break;
                    }
                    default: { break; }
                    }
                } else {
                    m_config = &(*m_objects)[obj_idx]->config;
                    update_and_show_settings = true;
                    disable_ss_manipulation  = (*m_objects)[obj_idx]->is_cut();
                }
            }
            else {
                if (type & itSettings) {
                    if (parent_type & itObject) {
                        og_name  = _L("Object Settings to Modify");
                        m_config = &(*m_objects)[obj_idx]->config;
                    }
                    else if (parent_type & itVolume) {
                        og_name   = _L("Part Settings to Modify");
                        volume_id = m_objects_model->GetVolumeIdByItem(parent);
                        m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    }
                    else if (parent_type & itLayer) {
                        og_name  = _L("Layer Range Settings to Modify");
                        m_config = &get_item_config(parent);
                    }
                    update_and_show_settings = true;
                }
                else if (type & itVolume) {
                    og_name = _L("Part manipulation");
                    volume_id = m_objects_model->GetVolumeIdByItem(item);
                    m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    update_and_show_manipulations = true;
                    m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    update_and_show_settings = true;

                    const ModelVolume *volume = (*m_objects)[obj_idx]->volumes[volume_id];
                    enable_manipulation       = !((*m_objects)[obj_idx]->is_cut() && (volume->is_cut_connector() || volume->is_model_part()));
                }
                else if (type & itInstance) {
                    og_name = _L("Instance manipulation");
                    update_and_show_manipulations = true;

                    m_config = &(*m_objects)[obj_idx]->config;
                    disable_ss_manipulation = (*m_objects)[obj_idx]->is_cut();
                }
                else if (type & (itLayerRoot | itLayer)) {
                    og_name = type & itLayerRoot ? _L("Height ranges") : _L("Settings for height range");
                    update_and_show_layers = true;
                    update_and_show_settings = true;

                    if (type & itLayer)
                        m_config = &get_item_config(item);
                }
            }
        }
    }

    m_selected_object_id = obj_idx;

    if (update_and_show_manipulations) {

        if (item) {
        }

        GLGizmosManager &gizmos_mgr = wxGetApp().plater()->canvas3D()->get_gizmos_manager();

        if (GLGizmoScale3D *scale = dynamic_cast<GLGizmoScale3D *>(gizmos_mgr.get_gizmo(GLGizmosManager::Scale)))
            scale->enable_ununiversal_scale(!disable_ununiform_scale);
    }

#if !NEW_OBJECT_SETTING
    if (update_and_show_settings)
        wxGetApp().obj_settings()->get_og()->set_name(" " + og_name + " ");
#endif

    if (!this->IsShown())
        update_and_show_layers = false;
    if (printer_technology() == ptSLA)
        update_and_show_layers = false;
    else if (update_and_show_layers) {
        ;
    }

    update_min_height();

    Sidebar& panel = wxGetApp().sidebar();
    panel.Freeze();


    const ItemType type = m_objects_model->GetItemType(item);
    if (!(type & itLayer)) {
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event("", false);
    }
    wxGetApp().obj_settings()->UpdateAndShow(update_and_show_settings);
    wxGetApp().obj_layers()  ->UpdateAndShow(update_and_show_layers);
    wxGetApp().plater()->show_object_info();

    panel.Layout();
    panel.Thaw();
}

wxDataViewItem ObjectList::add_settings_item(wxDataViewItem parent_item, const DynamicPrintConfig* config)
{
    wxDataViewItem ret = wxDataViewItem(nullptr);

    if (!parent_item)
        return ret;

    const bool is_object_settings = m_objects_model->GetItemType(parent_item) == itObject;
    const bool is_volume_settings = m_objects_model->GetItemType(parent_item) == itVolume;
    const bool is_layer_settings = m_objects_model->GetItemType(parent_item) == itLayer;
    if (!is_object_settings) {
        ModelVolumeType volume_type = m_objects_model->GetVolumeType(parent_item);
        if (volume_type == ModelVolumeType::NEGATIVE_VOLUME || volume_type == ModelVolumeType::SUPPORT_BLOCKER || volume_type == ModelVolumeType::SUPPORT_ENFORCER || is_precise_seam(volume_type))
            return ret;
    }

    SettingsFactory::Bundle cat_options = SettingsFactory::get_bundle(config, is_object_settings, is_layer_settings);
    if (is_layer_settings) {
        auto tab_object = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        auto object_cfg = tab_object->get_config();
        if (config->opt_float("layer_height") == object_cfg->opt_float("layer_height")) {
            SettingsFactory::Bundle new_cat_options;
            for (auto cat_opt : cat_options) {
                std::vector<std::string> temp;
                for (auto value : cat_opt.second) {
                    if (value != "layer_height")
                        temp.push_back(value);
                }
                if (!temp.empty())
                    new_cat_options[cat_opt.first] = temp;
            }
            cat_options = new_cat_options;
        }
    }

    if (cat_options.empty()) {
#if NEW_OBJECT_SETTING
        ObjectDataViewModelNode *node = static_cast<ObjectDataViewModelNode*>(parent_item.GetID());
        if (node) node->set_action_icon(false);
        m_objects_model->ItemChanged(parent_item);
        return parent_item;
#else
        return ret;
#endif
    }

    std::vector<std::string> categories;
    categories.reserve(cat_options.size());
    for (auto& cat : cat_options)
        categories.push_back(cat.first);

    if (m_objects_model->GetItemType(parent_item) & itInstance)
        parent_item = m_objects_model->GetObject(parent_item);

#if NEW_OBJECT_SETTING
    ObjectDataViewModelNode *node = static_cast<ObjectDataViewModelNode*>(parent_item.GetID());
    if (node) node->set_action_icon(true);
    m_objects_model->ItemChanged(parent_item);
    return parent_item;
#else
    ret = m_objects_model->IsSettingsItem(parent_item) ? parent_item : m_objects_model->GetSettingsItem(parent_item);

    if (!ret) ret = m_objects_model->AddSettingsChild(parent_item);

    m_objects_model->UpdateSettingsDigest(ret, categories);
    Expand(parent_item);

    return ret;
#endif
}

void ObjectList::update_variable_layer_obj_num(ObjectDataViewModelNode* obj_node, size_t layer_data_count) {
    if (obj_node){
        if (obj_node->IsVaribaleHeight() == hiVariable && layer_data_count <= 4){
            m_variable_layer_obj_num--;
        }
        else if (obj_node->IsVaribaleHeight() == hiUnVariable && layer_data_count > 4){
            m_variable_layer_obj_num++;
        }
        GetColumn(colHeight)->SetHidden(m_variable_layer_obj_num == 0);
        update_name_column_width();
    }
}

void ObjectList::update_info_items(size_t obj_idx, wxDataViewItemArray *selections , bool added_object , bool color_mode_changed )
{
    if (obj_idx >= m_objects->size())
        return;

    const ModelObject* model_object = (*m_objects)[obj_idx];
    wxDataViewItem item_obj = m_objects_model->GetItemById(obj_idx);
    assert(item_obj.IsOk());

    {
        const wxDataViewItem item = m_objects_model->GetInfoItemByType(item_obj, InfoItemType::Nonplanar);
        const bool painted = printer_technology() == ptFFF && std::any_of(model_object->volumes.begin(), model_object->volumes.end(),
            [](const ModelVolume *volume) { return volume->is_nonplanar_painted(); });
        if (painted && !item.IsOk()) {
            m_objects_model->AddInfoChild(item_obj, InfoItemType::Nonplanar);
            Expand(item_obj);
        } else if (!painted && item.IsOk()) {
            if (selections && selections->Index(item) != wxNOT_FOUND) {
                selections->Remove(item);
                if (selections->Index(item_obj) == wxNOT_FOUND) selections->Add(item_obj);
            } else if (!selections) Unselect(item);
            m_objects_model->Delete(item);
        }
    }

    {
        wxDataViewItem item = m_objects_model->GetInfoItemByType(item_obj, InfoItemType::CutConnectors);
        bool shows = item.IsOk();
        bool should_show = model_object->is_cut() && model_object->has_connectors() && model_object->volumes.size() > 1;

        if (!shows && should_show) {
            m_objects_model->AddInfoChild(item_obj, InfoItemType::CutConnectors);
            Expand(item_obj);
            if (added_object)
                wxGetApp().notification_manager()->push_updated_item_info_notification(InfoItemType::CutConnectors);
        } else if (shows && !should_show) {
            if (!selections) Unselect(item);
            m_objects_model->Delete(item);
            if (selections) {
                if (selections->Index(item) != wxNOT_FOUND) {
                    selections->Remove(item);
                    if (selections->Index(item_obj) == wxNOT_FOUND) selections->Add(item_obj);
                }
            } else
                Select(item_obj);
        }
    }

    {
        ObjectDataViewModelNode* obj_node = static_cast<ObjectDataViewModelNode*>(item_obj.GetID());
        auto layer_data_count = model_object->layer_height_profile.get().size();
        update_variable_layer_obj_num(obj_node, layer_data_count);
        m_objects_model->SetObjectVariableHeightState(layer_data_count > 4 ? hiVariable : hiUnVariable, item_obj);
    }

    {
        bool shows = m_objects_model->IsSupportPainted(item_obj);
        bool should_show = printer_technology() == ptFFF
            && std::any_of(model_object->volumes.begin(), model_object->volumes.end(),
                [](const ModelVolume* mv) {
                    return !mv->supported_facets.empty();
                });
        if (shows && !should_show) {
            m_objects_model->SetSupportPaintState(false, item_obj);
        }
        else if (!shows && should_show) {
            m_objects_model->SetSupportPaintState(true, item_obj);
        }
        if (color_mode_changed && shows) {
            m_objects_model->SetSupportPaintState(true, item_obj,true);
        }
    }

    {
        bool shows = m_objects_model->IsColorPainted(item_obj);
        bool should_show = printer_technology() == ptFFF
            && std::any_of(model_object->volumes.begin(), model_object->volumes.end(),
                [](const ModelVolume* mv) {
                    return !mv->mmu_segmentation_facets.empty();
                });
        if (shows && !should_show) {
            m_objects_model->SetColorPaintState(false, item_obj);
        }
        else if (!shows && should_show) {
            m_objects_model->SetColorPaintState(true, item_obj);
        }
        if (color_mode_changed && shows) {
            m_objects_model->SetColorPaintState(true, item_obj, true);
        }
    }

    {
        bool shows = m_objects_model->IsSinked(item_obj);
        bool should_show = printer_technology() == ptFFF
            && wxGetApp().plater()->canvas3D()->is_object_sinking(obj_idx);
        if (shows && !should_show) {
            m_objects_model->SetSinkState(false, item_obj);
        }
        else if (!shows && should_show) {
            m_objects_model->SetSinkState(true, item_obj);
        }
        if (color_mode_changed && shows) {
            m_objects_model->SetSinkState(true, item_obj, true);
        }
    }

    {
        bool shows = this->GetColumn(colSupportPaint)->IsShown();
        bool should_show = false;
        for (ModelObject* mo : *m_objects) {
            for (ModelVolume* mv : mo->volumes) {
                if (!mv->supported_facets.empty()) {
                    should_show = true;
                    break;
                }
            }
            if (should_show)
                break;
        }

        if (shows && !should_show) {
            this->set_support_paint_hidden(true);
        }
        else if (!shows && should_show) {
            this->set_support_paint_hidden(false);
        }
    }

    {
        bool shows = this->GetColumn(colColorPaint)->IsShown();
        bool should_show = false;
        for (ModelObject* mo : *m_objects) {
            for (ModelVolume* mv : mo->volumes) {
                if (!mv->mmu_segmentation_facets.empty()) {
                    should_show = true;
                    break;
                }
            }
            if (should_show)
                break;
        }

        if (shows && !should_show) {
            this->set_color_paint_hidden(true);
        }
        else if (!shows && should_show) {
            this->set_color_paint_hidden(false);
        }
    }

    {
        bool shows = this->GetColumn(colSinking)->IsShown();
        bool should_show = false;
        for (int i = 0; i < m_objects->size(); ++i) {
            if (wxGetApp().plater()->canvas3D()->is_object_sinking(i)) {
                should_show = true;
                break;
            }
            if (should_show)
                break;
        }

        if (shows && !should_show) {
            this->set_sinking_hidden(true);
        }
        else if (!shows && should_show) {
            this->set_sinking_hidden(false);
        }
    }
}

void ObjectList::add_objects_to_list(std::vector<size_t> obj_idxs, bool call_selection_changed, bool notify_partplate, bool do_info_update)
{
#ifdef __WXOSX__
    AssociateModel(nullptr);
#endif
    for (const size_t idx : obj_idxs) {
        add_object_to_list(idx, call_selection_changed, notify_partplate, do_info_update);
    }
#ifdef __WXOSX__
    AssociateModel(m_objects_model);
#endif
}


void ObjectList::add_object_to_list(size_t obj_idx, bool call_selection_changed, bool notify_partplate, bool do_info_update)
{
    auto model_object = (*m_objects)[obj_idx];
    PartPlateList& list = wxGetApp().plater()->get_partplate_list();
    if (notify_partplate) {
        list.notify_instance_update(obj_idx, 0, true);
    }
    const wxString& item_name = from_u8(model_object->name);
    std::string warning_bitmap = get_warning_icon_name(model_object->mesh().stats());
    const auto item = m_objects_model->AddObject(model_object, warning_bitmap, model_object->is_cut());
    Expand(m_objects_model->GetParent(item));

    {
        Slic3r::LifecycleEventContext ctx;
        ctx.name = model_object->name;
        ctx.code = Slic3r::LifecycleEvtCode::Ok;
        Slic3r::fire_lifecycle_event(Slic3r::LifecycleEvent::ObjectAdded, ctx);
    }

    if (!do_info_update)
        return;

    update_info_items(obj_idx, nullptr, call_selection_changed);

    add_volumes_to_object_in_list(obj_idx);

    if (model_object->instances.size()>1)
    {
        std::vector<bool> print_idicator(model_object->instances.size());
        std::vector<int> plate_idicator(model_object->instances.size());
        for (size_t i = 0; i < model_object->instances.size(); ++i) {
            print_idicator[i] = model_object->instances[i]->printable;
            plate_idicator[i] = list.find_instance_belongs(obj_idx, i);
        }

        const wxDataViewItem object_item = m_objects_model->GetItemById(obj_idx);
        m_objects_model->AddInstanceChild(object_item, print_idicator, plate_idicator);
        Expand(m_objects_model->GetInstanceRootItem(object_item));
    }
    else
        m_objects_model->SetPrintableState(model_object->instances[0]->printable ? piPrintable : piUnprintable, obj_idx);

    m_objects_model->SetObjectVariableHeightState(model_object->layer_height_profile.get().size() > 4 ? hiVariable : hiUnVariable, m_objects_model->GetItemById(obj_idx));

    add_settings_item(item, &model_object->config.get());

    add_layer_root_item(item);

#ifndef __WXOSX__
    if (call_selection_changed) {
        UnselectAll();
        Select(item);
        selection_changed();
    }
#endif
}

static bool can_add_volumes_to_object(const ModelObject *object)
{
    bool can = object->volumes.size() > 1;

    if (can && object->is_cut()) {
        int no_connectors_cnt = 0;
        for (const ModelVolume *v : object->volumes)
            if (!v->is_cut_connector()) {
                if (!v->is_model_part())
                    return true;
                no_connectors_cnt++;
            }
        can = no_connectors_cnt > 1;
    }

    return can;
}

wxDataViewItemArray ObjectList::add_volumes_to_object_in_list(size_t obj_idx, std::function<bool(const ModelVolume *)> add_to_selection )
{
    const bool is_prevent_list_events = m_prevent_list_events;
    m_prevent_list_events             = true;

    wxDataViewItem object_item = m_objects_model->GetItemById(int(obj_idx));
    m_objects_model->DeleteVolumeChildren(object_item);

    wxDataViewItemArray items;

    const ModelObject *object = (*m_objects)[obj_idx];
    if (can_add_volumes_to_object(object)) {
        if (object->volumes.size() > 1) {
            wxString obj_item_name = from_u8(object->name);
            if (m_objects_model->GetName(object_item) != obj_item_name)
                m_objects_model->SetName(obj_item_name, object_item);
        }

        int volume_idx{-1};
        auto& ui_and_3d_volume_map = m_objects_model->get_ui_and_3d_volume_map();
        for (auto item : ui_and_3d_volume_map) {
            if (item.first == obj_idx) {
                item.second.clear();
            }
        }
        int ui_volume_idx = 0;
        for (const ModelVolume *volume : object->volumes) {
            ++volume_idx;
            if (object->is_cut() && volume->is_cut_connector())
                continue;

            const wxDataViewItem &vol_item = m_objects_model->AddVolumeChild(
                object_item,
                from_u8(volume->name),
                volume->type(),
                volume->is_text(),
                volume->is_svg(),
                get_warning_icon_name(volume->mesh().stats()),
                volume->config.has("extruder") ? volume->config.extruder() : 0,
                false);
            ui_and_3d_volume_map[obj_idx][ui_volume_idx] = volume_idx;
            ui_volume_idx++;
            add_settings_item(vol_item, &volume->config.get());

            if (add_to_selection && add_to_selection(volume))
                items.Add(vol_item);
        }
        Expand(object_item);
    }

    m_prevent_list_events = is_prevent_list_events;
    return items;
}

void ObjectList::delete_object_from_list()
{
    auto item = GetSelection();
    if (!item)
        return;
    if (m_objects_model->GetParent(item) == wxDataViewItem(nullptr))
        select_item([this, item]() { return m_objects_model->Delete(item); });
    else
        select_item([this, item]() { return m_objects_model->Delete(m_objects_model->GetParent(item)); });
}

void ObjectList::delete_object_from_list(const size_t obj_idx)
{
    select_item([this, obj_idx]() { return m_objects_model->Delete(m_objects_model->GetItemById(obj_idx)); });
}

void ObjectList::delete_volume_from_list(const size_t obj_idx, const size_t vol_idx)
{
    select_item([this, obj_idx, vol_idx]() { return m_objects_model->Delete(m_objects_model->GetItemByVolumeId(obj_idx, vol_idx)); });
}

void ObjectList::delete_instance_from_list(const size_t obj_idx, const size_t inst_idx)
{
    select_item([this, obj_idx, inst_idx]() { return m_objects_model->Delete(m_objects_model->GetItemByInstanceId(obj_idx, inst_idx)); });
}

void ObjectList::delete_from_model_and_list(const ItemType type, const int obj_idx, const int sub_obj_idx)
{
    if (!(type&(itObject|itVolume|itInstance)))
        return;

    take_snapshot(_u8L("Delete selected"));

    if (type&itObject) {
        bool was_cut = object(obj_idx)->is_cut();
        bool vari_layer_height = (object(obj_idx)->layer_height_profile.get().size() > 4);
        if (del_object(obj_idx)) {
            delete_object_from_list(obj_idx);
            if (was_cut)
                update_lock_icons_for_model();
            if (vari_layer_height) {
                m_variable_layer_obj_num--;
                set_variable_height_column_hidden(m_variable_layer_obj_num == 0);
            }
        }
    }
    else {
        del_subobject_from_object(obj_idx, sub_obj_idx, type);

        type == itVolume ? delete_volume_from_list(obj_idx, sub_obj_idx) :
            delete_instance_from_list(obj_idx, sub_obj_idx);
    }
}

void ObjectList::delete_from_model_and_list(const std::vector<ItemForDelete>& items_for_delete)
{
    if (items_for_delete.empty())
        return;

    m_prevent_list_events = true;
    bool need_update = false;

    std::set<size_t> modified_objects_ids;
    for (std::vector<ItemForDelete>::const_reverse_iterator item = items_for_delete.rbegin(); item != items_for_delete.rend(); ++item) {
        if (!(item->type&(itObject | itVolume | itInstance)))
            continue;
        if (item->type&itObject) {
            need_update = true;
            bool refresh_immediately = false;
            bool was_cut = object(item->obj_idx)->is_cut();
            bool vari_layer_height = (object(item->obj_idx)->layer_height_profile.get().size() > 4);
            if (!del_object(item->obj_idx, refresh_immediately))
                return;
            m_objects_model->Delete(m_objects_model->GetItemById(item->obj_idx));
            if (was_cut)
                update_lock_icons_for_model();
            if (vari_layer_height) {
                m_variable_layer_obj_num--;
                set_variable_height_column_hidden(m_variable_layer_obj_num == 0);
            }
        }
        else {
            if (!del_subobject_from_object(item->obj_idx, item->sub_obj_idx, item->type))
                return;
            if (item->type&itVolume) {
                m_objects_model->Delete(m_objects_model->GetItemByVolumeId(item->obj_idx, item->sub_obj_idx));
#if 0
                ModelObject* obj = object(item->obj_idx);
                if (obj->volumes.size() == 1) {
                    wxDataViewItem parent = m_objects_model->GetItemById(item->obj_idx);
                    if (obj->config.has("extruder")) {
                        const wxString extruder = wxString::Format("%d", obj->config.extruder());
                        m_objects_model->SetExtruder(extruder, parent);
                    }
                    m_objects_model->UpdateWarningIcon(parent, get_warning_icon_name(obj->get_object_stl_stats()));
                }
#endif
                wxGetApp().plater()->canvas3D()->ensure_on_bed(item->obj_idx, printer_technology() != ptSLA);
            }
            else
                m_objects_model->Delete(m_objects_model->GetItemByInstanceId(item->obj_idx, item->sub_obj_idx));
        }

        modified_objects_ids.insert(static_cast<size_t>(item->obj_idx));
    }

    if (need_update) {
        wxGetApp().plater()->update();
        wxGetApp().plater()->object_list_changed();
    }

    for (size_t id : modified_objects_ids) {
        update_info_items(id);
    }

    m_prevent_list_events = true;
    part_selection_changed();
}

void ObjectList::update_lock_icons_for_model()
{
    for (size_t obj_idx = 0; obj_idx < (*m_objects).size(); ++obj_idx)
        if (!(*m_objects)[obj_idx]->is_cut())
            m_objects_model->UpdateCutObjectIcon(m_objects_model->GetItemById(int(obj_idx)), false);
}

void ObjectList::delete_all_objects_from_list()
{
    m_prevent_list_events = true;
    reload_all_plates();
    m_prevent_list_events = false;
    part_selection_changed();
}

void ObjectList::increase_object_instances(const size_t obj_idx, const size_t num)
{
    select_item([this, obj_idx, num]() { return m_objects_model->AddInstanceChild(m_objects_model->GetItemById(obj_idx), num); });
    selection_changed();
}

void ObjectList::decrease_object_instances(const size_t obj_idx, const size_t num)
{
    select_item([this, obj_idx, num]() { return m_objects_model->DeleteLastInstance(m_objects_model->GetItemById(obj_idx), num); });
}

void ObjectList::unselect_objects()
{
    if (!GetSelection())
        return;

    m_prevent_list_events = true;
    UnselectAll();
    part_selection_changed();
    m_prevent_list_events = false;
}

void ObjectList::select_object_item(bool is_msr_gizmo)
{
    if (wxDataViewItem item = GetSelection()) {
        ItemType type = m_objects_model->GetItemType(item);
        bool is_volume_item = type == itVolume || (type == itSettings && m_objects_model->GetItemType(m_objects_model->GetParent(item)) == itVolume);
        if ((is_msr_gizmo && is_volume_item) || type == itObject)
            return;

        if (wxDataViewItem obj_item = m_objects_model->GetTopParent(item)) {
            m_prevent_list_events = true;
            UnselectAll();
            Select(obj_item);
            part_selection_changed();
            m_prevent_list_events = false;
        }
    }
}

static void update_selection(wxDataViewItemArray& sels, ObjectList::SELECTION_MODE mode, ObjectDataViewModel* model)
{
    if (mode == ObjectList::smInstance)
    {
        for (auto& item : sels)
        {
            ItemType type = model->GetItemType(item);
            if (type == itObject)
                continue;
            if (type == itInstanceRoot) {
                wxDataViewItem obj_item = model->GetParent(item);
                sels.Remove(item);
                sels.Add(obj_item);
                update_selection(sels, mode, model);
                return;
            }
            if (type == itInstance)
            {
                wxDataViewItemArray instances;
                model->GetChildren(model->GetParent(item), instances);
                assert(instances.Count() > 0);
                size_t selected_instances_cnt = 0;
                for (auto& inst : instances) {
                    if (sels.Index(inst) == wxNOT_FOUND)
                        break;
                    selected_instances_cnt++;
                }

                if (selected_instances_cnt == instances.Count())
                {
                    wxDataViewItem obj_item = model->GetObject(item);
                    for (auto& inst : instances)
                        sels.Remove(inst);
                    sels.Add(obj_item);
                    update_selection(sels, mode, model);
                    return;
                }
            }
            else
                return;
        }
    }
}

void ObjectList::remove()
{
    if (GetSelectedItemsCount() == 0)
        return;

    auto delete_item = [this](wxDataViewItem item)
    {
        wxDataViewItem parent = m_objects_model->GetParent(item);
        ItemType type = m_objects_model->GetItemType(item);
        if (type & itObject)
            delete_from_model_and_list(itObject, m_objects_model->GetIdByItem(item), -1);
        else {
            if (type & (itLayer | itInstance)) {
                wxDataViewItemArray children;
                if (m_objects_model->GetChildren(parent, children) == (type & itLayer ? 1 : 2))
                    parent = m_objects_model->GetObject(item);
            }

            del_subobject_item(item);
        }

        return parent;
    };

    wxDataViewItemArray sels;
    GetSelections(sels);

    wxDataViewItem parent = wxDataViewItem(nullptr);

    if (sels.Count() == 1)
        parent = delete_item(GetSelection());
    else
    {
        SELECTION_MODE sels_mode = m_selection_mode;
        UnselectAll();
        update_selection(sels, sels_mode, m_objects_model);

        Plater::TakeSnapshot snapshot = Plater::TakeSnapshot(wxGetApp().plater(), _u8L("Delete selected"));

        for (auto& item : sels)
        {
            if (m_objects_model->InvalidItem(item))
                continue;
            parent = delete_item(item);
        }
    }

    if (parent && !m_objects_model->InvalidItem(parent)) {
        select_item(parent);
        update_selections_on_canvas();
    }
}

void ObjectList::del_layer_range(const t_layer_height_range& range)
{
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) return;

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    wxDataViewItem selectable_item = GetSelection();

    if (ranges.size() == 1)
        selectable_item = m_objects_model->GetParent(selectable_item);

    wxDataViewItem layer_item = m_objects_model->GetItemByLayerRange(obj_idx, range);
    del_subobject_item(layer_item);

    select_item(selectable_item);
}

static double get_min_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    return config.opt_float("min_layer_height", std::max(0, extruder_idx - 1));
}

static double get_max_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    int extruder_idx_zero_based = std::max(0, extruder_idx - 1);
    double max_layer_height = config.opt_float("max_layer_height", extruder_idx_zero_based);

    if (max_layer_height < EPSILON)
        max_layer_height = 0.75 * config.opt_float("nozzle_diameter", extruder_idx_zero_based);

    return max_layer_height;
}

void ObjectList::add_layer_range_after_current(const t_layer_height_range current_range)
{
    const int obj_idx = get_selected_obj_idx();
    assert(obj_idx >= 0);
    if (obj_idx < 0)
        return;

    const wxDataViewItem layers_item = GetSelection();

    auto& ranges = object(obj_idx)->layer_config_ranges;
    auto it_range = ranges.find(current_range);
    assert(it_range != ranges.end());
    if (it_range == ranges.end())
        return;

    auto it_next_range = it_range;
    bool changed = false;
    if (++ it_next_range == ranges.end())
    {
        take_snapshot("");
        changed = true;

        const t_layer_height_range new_range = { current_range.second, current_range.second + 2. };
        ranges[new_range].assign_config(get_default_layer_config(obj_idx));
        add_layer_item(new_range, layers_item);
    }
    else if (const std::pair<coordf_t, coordf_t> &next_range = it_next_range->first; current_range.second <= next_range.first)
    {
        const int layer_idx = m_objects_model->GetItemIdByLayerRange(obj_idx, next_range);
        assert(layer_idx >= 0);
        if (layer_idx >= 0)
        {
            if (current_range.second == next_range.first)
            {
                const auto old_config = ranges.at(next_range);
                const coordf_t delta = next_range.second - next_range.first;
                const coordf_t old_min_layer_height = get_min_layer_height(old_config.opt_int("extruder"));
                const coordf_t new_min_layer_height = get_min_layer_height(0);
                if (delta >= old_min_layer_height + new_min_layer_height - EPSILON) {
                    const coordf_t middle_layer_z = (new_min_layer_height > 0.5 * delta) ?
	                    next_range.second - new_min_layer_height :
                    	next_range.first + std::max(old_min_layer_height, 0.5 * delta);
                    t_layer_height_range new_range = { middle_layer_z, next_range.second };

                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "");
                    changed = true;

                    wxDataViewItem layer_item = m_objects_model->GetItemByLayerRange(obj_idx, next_range);
                    del_subobject_item(layer_item);

                    ranges[new_range] = old_config;
                    add_layer_item(new_range, layers_item, layer_idx);

                    new_range = { current_range.second, middle_layer_z };
                    ranges[new_range].assign_config(get_default_layer_config(obj_idx));
                    add_layer_item(new_range, layers_item, layer_idx);
                }
            }
            else if (next_range.first - current_range.second >= get_min_layer_height(0) - EPSILON)
            {
                take_snapshot("");
                changed = true;

                const t_layer_height_range new_range = { current_range.second, next_range.first };
                ranges[new_range].assign_config(get_default_layer_config(obj_idx));
                add_layer_item(new_range, layers_item, layer_idx);
            }
        }
    }

    if (changed)
        changed_object(obj_idx);

    select_item(layers_item);
}

wxString ObjectList::can_add_new_range_after_current(const t_layer_height_range current_range)
{
    const int obj_idx = get_selected_obj_idx();
    assert(obj_idx >= 0);
    if (obj_idx < 0)
        return "ObjectList assert";

    auto& ranges = object(obj_idx)->layer_config_ranges;
    auto it_range = ranges.find(current_range);
    assert(it_range != ranges.end());
    if (it_range == ranges.end())
        return "ObjectList assert";

    auto it_next_range = it_range;
    if (++ it_next_range == ranges.end())
        return "";

	return "";
}

void ObjectList::add_layer_item(const t_layer_height_range& range,
                                const wxDataViewItem layers_item,
                                const int layer_idx )
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(layers_item);
    if (obj_idx < 0) return;

    const DynamicPrintConfig& config = object(obj_idx)->layer_config_ranges[range].get();
    if (!config.has("extruder"))
        return;

    const auto layer_item = m_objects_model->AddLayersChild(layers_item,
                                                            range,
                                                            config.opt_int("extruder"),
                                                            layer_idx);
    add_settings_item(layer_item, &config);
}

bool ObjectList::edit_layer_range(const t_layer_height_range& range, coordf_t layer_height)
{
    const int obj_idx = m_selected_object_id ;
    if (obj_idx < 0)
        return false;

    ModelConfig* config = &object(obj_idx)->layer_config_ranges[range];
    if (fabs(layer_height - config->opt_float("layer_height")) < EPSILON)
        return false;

    const int extruder_idx = config->opt_int("extruder");

    if (layer_height >= get_min_layer_height(extruder_idx) &&
        layer_height <= get_max_layer_height(extruder_idx))
    {
        config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        changed_object(obj_idx);
        return true;
    }

    return false;
}

bool ObjectList::edit_layer_range(const t_layer_height_range& range, const t_layer_height_range& new_range, bool dont_update_ui)
{
    const int obj_idx = m_selected_object_id;
    if (obj_idx < 0) return false;

    take_snapshot("");

    const ItemType sel_type = m_objects_model->GetItemType(GetSelection());

    auto& ranges = object(obj_idx)->layer_config_ranges;

    {
        ModelConfig config = std::move(ranges[range]);
        ranges.erase(range);
        ranges[new_range] = std::move(config);
    }

    changed_object(obj_idx);

    wxDataViewItem root_item = m_objects_model->GetLayerRootItem(m_objects_model->GetItemById(obj_idx));
    m_prevent_list_events = true;
    m_objects_model->DeleteChildren(root_item);

    if (root_item.IsOk()) {
        for (const auto& r : ranges)
            add_layer_item(r.first, root_item);
    }

    if (!dont_update_ui && (sel_type & (itLayer | itLayerRoot)))
        select_item(sel_type&itLayer ? m_objects_model->GetItemByLayerRange(obj_idx, new_range) : root_item);

    Expand(root_item);

    m_prevent_list_events = false;
    return true;
}

void ObjectList::init()
{
    m_objects_model->Init();
    m_objects = &wxGetApp().model().objects;
}

bool ObjectList::multiple_selection() const
{
    return GetSelectedItemsCount() > 1;
}

bool ObjectList::is_selected(const ItemType type) const
{
    const wxDataViewItem& item = GetSelection();
    if (item)
        return m_objects_model->GetItemType(item) == type;

    return false;
}

bool ObjectList::is_connectors_item_selected() const
{
    const wxDataViewItem &item = GetSelection();
    if (item)
        return m_objects_model->GetItemType(item) == itInfo && m_objects_model->GetInfoItemType(item) == InfoItemType::CutConnectors;

    return false;
}

bool ObjectList::is_connectors_item_selected(const wxDataViewItemArray &sels) const
{
    for (auto item : sels)
        if (m_objects_model->GetItemType(item) == itInfo && m_objects_model->GetInfoItemType(item) == InfoItemType::CutConnectors)
            return true;

    return false;
}


int ObjectList::get_selected_layers_range_idx() const
{
    const wxDataViewItem& item = GetSelection();
    if (!item)
        return -1;

    const ItemType type = m_objects_model->GetItemType(item);
    if (type & itSettings && m_objects_model->GetItemType(m_objects_model->GetParent(item)) != itLayer)
        return -1;

    return m_objects_model->GetLayerIdByItem(type & itLayer ? item : m_objects_model->GetParent(item));
}

void ObjectList::update_selections()
{
    if (wxGetApp().is_closing())
        return;

    const Selection& selection = scene_selection();
    wxDataViewItemArray sels;

#if 0
    if (m_selection_mode == smUndef) {
        PartPlate* pp = wxGetApp().plater()->get_partplate_list().get_selected_plate();
        assert(pp != nullptr);
        wxDataViewItem sel_plate = m_objects_model->GetItemByPlateId(pp->get_index());
        sels.Add(sel_plate);
        select_items(sels);

        ensure_current_item_visible();
        return;
    }
#endif

    if ( ( m_selection_mode & (smSettings|smLayer|smLayerRoot|smVolume) ) == 0)
        m_selection_mode = smInstance;

    if (GetSelectedItemsCount() == 1 && m_objects_model->GetItemType(GetSelection()) & (itSettings | itLayerRoot | itLayer))
    {
        const auto item = GetSelection();
        if (selection.is_single_full_object()) {
            if (m_objects_model->GetItemType(m_objects_model->GetParent(item)) & itObject &&
                m_objects_model->GetObjectIdByItem(item) == selection.get_object_idx() )
                return;
            sels.Add(m_objects_model->GetItemById(selection.get_object_idx()));
        }
        else if (selection.is_single_volume() || selection.is_any_modifier()) {
            const auto gl_vol = selection.get_first_volume();
            if (m_objects_model->GetVolumeIdByItem(m_objects_model->GetParent(item)) == gl_vol->volume_idx())
                return;
        }
        else if (selection.is_single_full_instance())
            sels.Add(m_objects_model->GetItemByInstanceId(selection.get_object_idx(), selection.get_instance_idx()));
        else {
            m_last_selected_item = wxDataViewItem(nullptr);
            m_prevent_list_events = true;
            UnselectAll();
            m_prevent_list_events = false;
            update_selections();
            return;
        }
    }
    else if (selection.is_single_full_object() || selection.is_multiple_full_object())
    {
        const Selection::ObjectIdxsToInstanceIdxsMap& objects_content = selection.get_content();
        if (!selection.is_multiple_full_object() && (m_selection_mode & (smSettings | smLayer | smLayerRoot)))
        {
            auto obj_idx = objects_content.begin()->first;
            wxDataViewItem obj_item = m_objects_model->GetItemById(obj_idx);
            if (m_selection_mode & smSettings)
            {
                if (m_selected_layers_range_idx < 0)
                    sels.Add(m_objects_model->GetSettingsItem(obj_item));
                else
                    sels.Add(m_objects_model->GetSettingsItem(m_objects_model->GetItemByLayerId(obj_idx, m_selected_layers_range_idx)));
            }
            else if (m_selection_mode & smLayerRoot)
                sels.Add(m_objects_model->GetLayerRootItem(obj_item));
            else if (m_selection_mode & smLayer) {
                if (m_selected_layers_range_idx >= 0)
                    sels.Add(m_objects_model->GetItemByLayerId(obj_idx, m_selected_layers_range_idx));
                else
                    sels.Add(obj_item);
            }
        }
        else {
        for (const auto& object : objects_content) {
            if (object.second.size() == 1)
                sels.Add(m_objects_model->GetItemById(object.first));
            else if (object.second.size() > 1)
            {
                wxDataViewItemArray current_sels;
                GetSelections(current_sels);
                const wxDataViewItem frst_inst_item = m_objects_model->GetItemByInstanceId(object.first, 0);

                bool root_is_selected = false;
                for (const auto& item:current_sels)
                    if (item == m_objects_model->GetParent(frst_inst_item) ||
                        item == m_objects_model->GetObject(frst_inst_item)) {
                        root_is_selected = true;
                        sels.Add(item);
                        break;
                    }
                if (root_is_selected)
                    continue;

                const Selection::InstanceIdxsList& instances = object.second;
                for (const auto& inst : instances)
                    sels.Add(m_objects_model->GetItemByInstanceId(object.first, inst));
            }
        } }
    }
    else if (selection.is_any_volume() || selection.is_any_modifier())
    {
        if (m_selection_mode & smSettings)
        {
            const auto gl_vol = selection.get_first_volume();
            if (gl_vol->volume_idx() >= 0) {
                wxDataViewItem vol_item = m_objects_model->GetItemByVolumeId(gl_vol->object_idx(), gl_vol->volume_idx());
                sels.Add(m_objects_model->GetSettingsItem(vol_item));
            }
        }
        else {
        for (auto idx : selection.get_volume_idxs()) {
            const auto gl_vol = selection.get_volume(idx);
            if (gl_vol->volume_idx() >= 0) {
                int obj_idx = gl_vol->object_idx();
                int vol_idx = gl_vol->volume_idx();
                assert(obj_idx >= 0 && vol_idx >= 0);
                if (object(obj_idx)->volumes[vol_idx]->is_cut_connector())
                    sels.Add(m_objects_model->GetInfoItemByType(m_objects_model->GetItemById(obj_idx), InfoItemType::CutConnectors));
                else {
                    vol_idx = m_objects_model->get_real_volume_index_in_ui(obj_idx,vol_idx);
                    sels.Add(m_objects_model->GetItemByVolumeId(obj_idx, vol_idx));
                }
            }
        }
        m_selection_mode = smVolume; }
    }
    else if (selection.is_single_full_instance() || selection.is_multiple_full_instance())
    {
        for (auto idx : selection.get_instance_idxs()) {
            sels.Add(m_objects_model->GetItemByInstanceId(selection.get_object_idx(), idx));
        }
    }
    else if (selection.is_mixed())
    {
        const Selection::ObjectIdxsToInstanceIdxsMap& objects_content_list = selection.get_content();

        for (auto idx : selection.get_volume_idxs()) {
            const auto gl_vol = selection.get_volume(idx);
            const auto& glv_obj_idx = gl_vol->object_idx();
            const auto& glv_ins_idx = gl_vol->instance_idx();

            bool is_selected = false;

            for (auto obj_ins : objects_content_list) {
                if (obj_ins.first == glv_obj_idx) {
                    if (obj_ins.second.find(glv_ins_idx) != obj_ins.second.end() &&
                        !selection.is_from_single_instance() )
                    {
                        if (glv_ins_idx == 0 && (*m_objects)[glv_obj_idx]->instances.size() == 1)
                            sels.Add(m_objects_model->GetItemById(glv_obj_idx));
                        else
                            sels.Add(m_objects_model->GetItemByInstanceId(glv_obj_idx, glv_ins_idx));

                        is_selected = true;
                        break;
                    }
                }
            }

            if (is_selected)
                continue;

            const auto& glv_vol_idx = gl_vol->volume_idx();
            if (glv_vol_idx == 0 && (*m_objects)[glv_obj_idx]->volumes.size() == 1)
                sels.Add(m_objects_model->GetItemById(glv_obj_idx));
            else
                sels.Add(m_objects_model->GetItemByVolumeId(glv_obj_idx, glv_vol_idx));
        }
    }

    if (sels.size() == 0 || m_selection_mode & smSettings)
        m_selection_mode = smUndef;

    if (fix_cut_selection(sels) || is_connectors_item_selected(sels)) {
        m_prevent_list_events = true;

        UnselectAll();
        SetSelections(sels);

        m_prevent_list_events = false;

        if (!m_prevent_canvas_selection_update)
            update_selections_on_canvas();

        if (!GetSelection() || m_objects_model->GetItemType(GetSelection()) == itObject || is_connectors_item_selected()) {
            auto event = SimpleEvent(EVT_OBJ_LIST_OBJECT_SELECT);
            event.SetEventObject(this);
            wxPostEvent(this, event);
        }
        part_selection_changed();
    }
    else {
        select_items(sels);

        ensure_current_item_visible();
    }
}

void ObjectList::update_selections_on_canvas()
{
    auto canvas_type = wxGetApp().plater()->get_current_canvas3D()->get_canvas_type();
    GLCanvas3D* canvas = canvas_type == GLCanvas3D::ECanvasType::CanvasAssembleView ? wxGetApp().plater()->get_current_canvas3D() : wxGetApp().plater()->get_view3D_canvas3D();
    Selection& selection = canvas->get_selection();

    const int sel_cnt = GetSelectedItemsCount();
    if (sel_cnt == 0) {
        selection.remove_all();
        if (canvas_type != GLCanvas3D::ECanvasType::CanvasPreview)
            wxGetApp().plater()->get_current_canvas3D()->update_gizmos_on_off_state();
        return;
    }

    std::vector<unsigned int> volume_idxs;
    Selection::EMode mode = Selection::Volume;
    bool single_selection = sel_cnt == 1;
    auto add_to_selection = [this, &volume_idxs, &single_selection](const wxDataViewItem& item, const Selection& selection, int instance_idx, Selection::EMode& mode)
    {
        const ItemType& type = m_objects_model->GetItemType(item);
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);

        if (type == itVolume) {
            int vol_idx = m_objects_model->GetVolumeIdByItem(item);
            vol_idx                        = m_objects_model->get_real_volume_index_in_3d(obj_idx,vol_idx);
            std::vector<unsigned int> idxs = selection.get_volume_idxs_from_volume(obj_idx, std::max(instance_idx, 0), vol_idx);
            volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
        }
        else if (type == itInstance) {
            const int inst_idx = m_objects_model->GetInstanceIdByItem(item);
            mode = Selection::Instance;
            std::vector<unsigned int> idxs = selection.get_volume_idxs_from_instance(obj_idx, inst_idx);
            volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
        }
        else if (type == itInfo) {
            if (m_objects_model->GetInfoItemType(item) == InfoItemType::CutConnectors) {
                mode = Selection::Volume;

                const ModelObject *obj = object(obj_idx);
                for (unsigned int vol_idx = 0; vol_idx < obj->volumes.size(); vol_idx++)
                    if (obj->volumes[vol_idx]->is_cut_connector()) {
                        std::vector<unsigned int> idxs = selection.get_volume_idxs_from_volume(obj_idx, std::max(instance_idx, 0), vol_idx);
                        volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
                    }
            } else {
                int inst_idx      = selection.get_instance_idx();
                int scene_obj_idx = selection.get_object_idx();
                mode              = Selection::Instance;
                if (scene_obj_idx == -1 || inst_idx == -1 || scene_obj_idx != obj_idx) inst_idx = 0;
                std::vector<unsigned int> idxs = selection.get_volume_idxs_from_instance(obj_idx, inst_idx);
                volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
            }
        }
        else
        {
            mode = Selection::Instance;
            single_selection &= (obj_idx != selection.get_object_idx());
            std::vector<unsigned int> idxs = selection.get_volume_idxs_from_object(obj_idx);
            volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
        }
    };

    int instance_idx = selection.get_instance_idx();

    if (sel_cnt == 1) {
        wxDataViewItem item = GetSelection();
        if (m_objects_model->GetInfoItemType(item) == InfoItemType::CutConnectors)
            selection.remove_all();

        if (m_objects_model->GetItemType(item) & (itSettings | itInstanceRoot | itLayerRoot | itLayer))
            add_to_selection(m_objects_model->GetParent(item), selection, instance_idx, mode);
        else
            add_to_selection(item, selection, instance_idx, mode);
    }
    else
    {
        wxDataViewItemArray sels;
        GetSelections(sels);

        selection.clear();

        for (auto item : sels)
        {
            add_to_selection(item, selection, instance_idx, mode);
        }
    }

    if (selection.contains_all_volumes(volume_idxs))
    {
        volume_idxs = selection.get_missing_volume_idxs_from(volume_idxs);
        if (volume_idxs.size() > 0)
        {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Remove selected from list", UndoRedo::SnapshotType::Selection);
            selection.remove_volumes(mode, volume_idxs);
        }
    }
    else
    {
        if (selection.get_mode() == mode || !single_selection)
            volume_idxs = selection.get_unselected_volume_idxs_from(volume_idxs);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Add selected to list", UndoRedo::SnapshotType::Selection);
        selection.add_volumes(mode, volume_idxs, single_selection);
    }

    if (canvas_type != GLCanvas3D::ECanvasType::CanvasPreview)
        wxGetApp().plater()->get_current_canvas3D()->update_gizmos_on_off_state();
    wxGetApp().plater()->canvas3D()->render();
}

void ObjectList::select_item(const wxDataViewItem& item)
{
    if (! item.IsOk()) { return; }

    m_prevent_list_events = true;

    UnselectAll();
    Select(item);
    part_selection_changed();

    m_prevent_list_events = false;
}

void ObjectList::select_item(std::function<wxDataViewItem()> get_item)
{
    if (!get_item)
        return;

    m_prevent_list_events = true;
    m_prevent_list_manipulation = true;

    wxDataViewItem item = get_item();
    if (item.IsOk()) {
        UnselectAll();
        Select(item);
        part_selection_changed();
    }

    m_prevent_list_manipulation = false;
    m_prevent_list_events = false;
}

void ObjectList::select_item(const ObjectVolumeID& ov_id)
{
    std::vector<ObjectVolumeID> ov_ids;
    ov_ids.push_back(ov_id);
    select_items(ov_ids);
}

void ObjectList::select_items(const std::vector<ObjectVolumeID>& ov_ids)
{
    ModelObjectPtrs& objects = wxGetApp().model().objects;

    wxDataViewItemArray sel_items;
    for (auto ov_id : ov_ids) {
        if (ov_id.object == nullptr)
            continue;

        ModelObject* mo = ov_id.object;
        ModelVolume* mv = ov_id.volume;

        wxDataViewItem obj_item = m_objects_model->GetObjectItem(mo);
        if (mv != nullptr) {
            size_t vol_idx;
            for (vol_idx = 0; vol_idx < mo->volumes.size(); vol_idx++) {
                if (mo->volumes[vol_idx] == mv)
                    break;
            }
            assert(vol_idx < mo->volumes.size());

            wxDataViewItem vol_item = m_objects_model->GetVolumeItem(obj_item, vol_idx);
            if (vol_item.GetID() != nullptr) {
                sel_items.push_back(vol_item);
            }
            else {
                sel_items.push_back(obj_item);
            }
        }
        else {
            sel_items.push_back(obj_item);
        }
    }

    select_items(sel_items);
    selection_changed();
}

void ObjectList::select_items(const wxDataViewItemArray& sels)
{
    m_prevent_list_events = true;
    m_prevent_list_manipulation = true;
    m_last_selected_item = sels.empty() ? wxDataViewItem(nullptr) : sels.back();

    UnselectAll();

    if (!sels.empty()) {
        SetSelections(sels);
    }
    else {
        int curr_plate_idx = wxGetApp().plater()->get_partplate_list().get_curr_plate_index();
        on_plate_selected(curr_plate_idx);
    }

    part_selection_changed();

    m_prevent_list_manipulation = false;
    m_prevent_list_events = false;
}

void ObjectList::select_all()
{
    SelectAll();
    selection_changed();
}

void ObjectList::select_item_all_children()
{
    if (wxGetApp().plater()  && !wxGetApp().plater()->canvas3D()->get_gizmos_manager().is_allow_select_all()) {
        return;
    }
    wxDataViewItemArray sels;

    if (!GetSelection() || m_objects_model->GetItemType(GetSelection()) == itObject) {
        for (size_t i = 0; i < m_objects->size(); i++)
            sels.Add(m_objects_model->GetItemById(i));
        m_selection_mode = smInstance;
    }
    else {
        const auto item = GetSelection();
        const ItemType item_type = m_objects_model->GetItemType(item);
        if (item_type & (itVolume | itInstance | itLayer))
            m_objects_model->GetChildren(m_objects_model->GetParent(item), sels);

        m_selection_mode = item_type&itVolume ? smVolume :
                           item_type&itLayer  ? smLayer  : smInstance;
    }

    SetSelections(sels);
    selection_changed();
}

void ObjectList::update_selection_mode()
{
    m_selected_layers_range_idx=-1;
    if (!GetSelection())
    {
        m_last_selected_item = wxDataViewItem(nullptr);
        m_selection_mode = smUndef;
        return;
    }

    const ItemType type = m_objects_model->GetItemType(GetSelection());
    m_selection_mode =  type & itSettings ? smUndef  :
                        type & itLayer    ? smLayer  :
                        type & itVolume   ? smVolume : smInstance;
}

bool ObjectList::check_last_selection(wxString& msg_str)
{
    if (!m_last_selected_item)
        return true;

    const bool is_shift_pressed = wxGetKeyState(WXK_SHIFT);

    const ItemType type = m_objects_model->GetItemType(m_last_selected_item);

    auto impossible_multi_selection = [type, this](const ItemType item_type, const SELECTION_MODE selection_mode) {
        if (!(type & item_type && m_selection_mode & selection_mode))
            return false;

        wxDataViewItemArray sels;
        GetSelections(sels);
        for (const auto& sel : sels)
            if (sel != m_last_selected_item &&
                m_objects_model->GetObject(sel) != m_objects_model->GetObject(m_last_selected_item))
                return true;

        return false;
    };

    if (impossible_multi_selection(itVolume, smVolume) ||
        impossible_multi_selection(itLayer,  smLayer ) ||
        type & itSettings ||
        (type & itVolume   && !(m_selection_mode & smVolume  )) ||
        (type & itLayer    && !(m_selection_mode & smLayer   )) ||
        (type & itInstance && !(m_selection_mode & smInstance))
        )
    {
        const wxString item_type = m_selection_mode & smInstance ? _(L("Object")) :
                                   m_selection_mode & smVolume   ? _(L("Part")) : _(L("Layer"));

        if (m_selection_mode == smInstance) {
            msg_str = wxString::Format(_(L("Selection conflicts")) + "\n\n" +
                _(L("If the first selected item is an object, the second should also be an object.")) + "\n");
        }
        else {
            msg_str = wxString::Format(_(L("Selection conflicts")) + "\n\n" +
                _(L("If the first selected item is a part, the second should be a part in the same object.")) + "\n");
        }

        if (!is_shift_pressed) {
            Unselect(m_last_selected_item);
            show_info(this, msg_str, _(L("Info")));
        }

        return is_shift_pressed;
    }

    return true;
}

void ObjectList::fix_multiselection_conflicts()
{
    if (GetSelectedItemsCount() <= 1) {
        update_selection_mode();
        return;
    }

    wxString msg_string;
    if (!check_last_selection(msg_string))
        return;

    m_prevent_list_events = true;

    wxDataViewItemArray sels;
    GetSelections(sels);

    if (m_selection_mode & (smVolume|smLayer))
    {
        const wxDataViewItem& parent = m_objects_model->GetParent(m_last_selected_item == sels.front() ? sels.back() : sels.front());

        sels.clear();
        wxDataViewItemArray children;
        m_objects_model->GetChildren(parent, children);

        const ItemType item_type = m_selection_mode & smVolume ? itVolume : itLayer;

        for (const auto& child : children)
            if (IsSelected(child) && m_objects_model->GetItemType(child) & item_type)
                sels.Add(child);

        UnselectAll();
        SetSelections(sels);
    }
    else
    {
        for (const auto& item : sels)
        {
            if (!IsSelected(item))
                continue;

            if (m_objects_model->GetItemType(item) & itSettings) {
                Unselect(item);
                continue;
            }

            const wxDataViewItem& parent = m_objects_model->GetParent(item);
            if (parent != wxDataViewItem(nullptr) && IsSelected(parent))
                Unselect(parent);
            else
            {
                wxDataViewItemArray unsels;
                m_objects_model->GetAllChildren(item, unsels);
                for (const auto& unsel_item : unsels)
                    Unselect(unsel_item);
            }

            if (m_objects_model->GetItemType(item) & itVolume)
                Unselect(item);

            m_selection_mode = smInstance;
        }
    }

    if (!msg_string.IsEmpty())
        show_info(this, msg_string, _(L("Info")));

    if (!IsSelected(m_last_selected_item))
        m_last_selected_item = wxDataViewItem(nullptr);

    m_prevent_list_events = false;
}

void ObjectList::fix_cut_selection()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (fix_cut_selection(sels)) {
        m_prevent_list_events = true;

        UnselectAll();
        SetSelections(sels);

        m_prevent_list_events = false;
    }
}

bool ObjectList::fix_cut_selection(wxDataViewItemArray &sels)
{
    if (wxGetApp().plater()->canvas3D()->get_gizmos_manager().get_current_type() == GLGizmosManager::Scale) {
        for (const auto &item : sels) {
            if (m_objects_model->GetItemType(item) & (itInstance | itObject) ||
                (m_objects_model->GetItemType(item) & itSettings && m_objects_model->GetItemType(m_objects_model->GetParent(item)) & itObject)) {
                bool is_instance_selection = m_objects_model->GetItemType(item) & itInstance;

                int object_idx = m_objects_model->GetObjectIdByItem(item);
                int inst_idx   = is_instance_selection ? m_objects_model->GetInstanceIdByItem(item) : 0;

                if (auto obj = object(object_idx); obj->is_cut()) {
                    sels.Clear();

                    auto cut_id = obj->cut_id;

                    int objects_cnt = int((*m_objects).size());
                    for (int obj_idx = 0; obj_idx < objects_cnt; ++obj_idx) {
                        auto object = (*m_objects)[obj_idx];
                        if (object->is_cut() && object->cut_id.has_same_id(cut_id))
                            sels.Add(is_instance_selection ? m_objects_model->GetItemByInstanceId(obj_idx, inst_idx) : m_objects_model->GetItemById(obj_idx));
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

ModelVolume* ObjectList::get_selected_model_volume()
{
    wxDataViewItem item = GetSelection();
    if (!item)
        return nullptr;
    if (m_objects_model->GetItemType(item) != itVolume) {
        if (m_objects_model->GetItemType(m_objects_model->GetParent(item)) == itVolume)
            item = m_objects_model->GetParent(item);
        else
            return nullptr;
    }

    const auto vol_idx = m_objects_model->GetVolumeIdByItem(item);
    const auto obj_idx = get_selected_obj_idx();
    if (vol_idx < 0 || obj_idx < 0)
        return nullptr;

    return (*m_objects)[obj_idx]->volumes[vol_idx];
}

#if 0
void ObjectList::change_part_type()
{
  wxDataViewItemArray selections;
  GetSelections(selections);

  if (selections.size() <= 1) {
    int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) {
      return;
    }

    ModelVolume* volume = get_selected_model_volume();
    if (!volume) {
      return;
    }

    const ModelVolumeType type = volume->type();
    if (type == ModelVolumeType::MODEL_PART) {
      int model_part_cnt = 0;
      for (auto vol : (*m_objects)[obj_idx]->volumes) {
        if (vol->type() == ModelVolumeType::MODEL_PART)
          ++model_part_cnt;
      }

      if (model_part_cnt == 1) {
        Slic3r::GUI::show_error(nullptr, _(L("The type of the last solid object part cannot be changed.")));
        return;
      }
    }

    wxArrayString names;
    names.Add(_L("Part"));
    names.Add(_L("Negative Part"));
    names.Add(_L("Modifier"));
    if (!volume->is_svg() && !volume->is_text()) {
      names.Add(_L("Support Blocker"));
      names.Add(_L("Support Enforcer"));
    }

    SingleChoiceDialog dlg(_L("Type:"), _L("Choose part type"), names, int(type));
    auto new_type = ModelVolumeType(dlg.GetSingleChoiceIndex());

    if (new_type == type || new_type == ModelVolumeType::INVALID) {
      return;
    }

    take_snapshot(_u8L("Change part type"));

    volume->set_type(new_type);
    wxDataViewItemArray sel = reorder_volumes_and_get_selection(obj_idx, [volume](const ModelVolume* vol) { return vol == volume; });
    if (!sel.IsEmpty()) {
      select_item(sel.front());
    }
    
  return;
  }

  struct Target { int obj_idx; Slic3r::ModelVolume* vol; };
  std::vector<Target> targets;
  targets.reserve(selections.size());
  bool any_text_or_svg = false;

  for (const auto& item : selections) {
    auto typeMask = m_objects_model->GetItemType(item);
    if (!(typeMask & itVolume)) {
      continue;
    }

    int obj_idx = -1, vol_idx = -1;
    get_selected_item_indexes(obj_idx, vol_idx, item);
    if (obj_idx < 0 || vol_idx < 0) {
      continue;
    }

    ModelVolume* vol = (*m_objects)[obj_idx]->volumes[vol_idx];
    if (!vol) {
      continue;
    }

    targets.push_back({ obj_idx, vol });
    if (vol->is_svg() || vol->is_text())
      any_text_or_svg = true;
  }
  
  if (targets.empty()) {
    return;
  }

  wxArrayString names;
  names.Add(_L("Part"));
  names.Add(_L("Negative Part"));
  names.Add(_L("Modifier"));
  if (!any_text_or_svg) {
    names.Add(_L("Support Blocker"));
    names.Add(_L("Support Enforcer"));
  }

  ModelVolumeType initial_type = targets.front().vol->type();
  SingleChoiceDialog dlg(_L("Type:"), _L("Choose part type"), names, int(initial_type));
  auto new_type = ModelVolumeType(dlg.GetSingleChoiceIndex());
  if (new_type == ModelVolumeType::INVALID) {
    return;
  }

  if (new_type != ModelVolumeType::MODEL_PART) {
    std::unordered_map<int, int> parts_initial;
    for (const auto& t : targets) {
      int cnt = 0;
      for (auto v : (*m_objects)[t.obj_idx]->volumes)
          if (v->type() == ModelVolumeType::MODEL_PART) ++cnt;
      parts_initial[t.obj_idx] = cnt;
    }

    std::unordered_map<int, int> parts_to_remove;
    for (const auto& t : targets) {
      if (t.vol->type() == ModelVolumeType::MODEL_PART) {
        ++parts_to_remove[t.obj_idx];
      }
    }

    bool would_remove_all_for_any = false;
    for (const auto& kv : parts_to_remove) {
      const int obj_idx   = kv.first;
      const int removing  = kv.second;
      const int initial   = parts_initial[obj_idx];
      if (initial > 0 && removing == initial) {
        would_remove_all_for_any = true;
        break;
      }
    }

    if (would_remove_all_for_any) {
      Slic3r::GUI::show_error(nullptr, _(L("The type of the last solid object part cannot be changed.")));
      return;
    }
  }

  take_snapshot("Change part type (multi)");

  size_t applied = 0, skipped_same = 0;
  std::unordered_map<int, std::vector<ModelVolume*>> changed_per_object;
  for (const auto& t : targets) {
    const auto current = t.vol->type();
    if (current == new_type) {
        ++skipped_same;
        continue;
    }
    t.vol->set_type(new_type);
    changed_per_object[t.obj_idx].push_back(t.vol);
    ++applied;
  }

  if (applied == 0) {
    select_items(selections);
    return;
  }

  wxDataViewItemArray new_selection;
  for (const auto& kv : changed_per_object) {
    const int obj_idx = kv.first;
    const auto& changed_vols = kv.second;
    
    std::unordered_set<const ModelVolume*> changed_set;
    changed_set.reserve(changed_vols.size());
    for (const auto* v : changed_vols) {
      changed_set.insert(v);
    }

    wxDataViewItemArray sel = reorder_volumes_and_get_selection(
      obj_idx,
      [&changed_set](const ModelVolume* v) -> bool {
        return changed_set.find(v) != changed_set.end();
      }
    );

    for (const auto& it : sel) new_selection.Add(it);
  }

  if (!new_selection.IsEmpty()) {
    select_items(new_selection);
  } else {
    select_items(selections);
  }

  return;
}
#endif

ModelVolumeType ObjectList::get_selected_volume_type()
{
    ModelVolume* volume = get_selected_model_volume();
    if (volume)
        return volume->type();
    return ModelVolumeType::INVALID;
}

static bool precise_seam_group_changed(ModelVolumeType old_type, ModelVolumeType new_type)
{
    const bool old_is_ps = is_precise_seam(old_type);
    const bool new_is_ps = is_precise_seam(new_type);
    if (old_is_ps != new_is_ps)
        return true;
    if (!old_is_ps)
        return false;
    return is_precise_seam_strong(old_type) != is_precise_seam_strong(new_type);
}

static void move_volume_to_end(ModelObject* obj, ModelVolume* volume)
{
    if (obj == nullptr || volume == nullptr)
        return;
    auto it = std::find(obj->volumes.begin(), obj->volumes.end(), volume);
    if (it != obj->volumes.end()) {
        obj->volumes.erase(it);
        obj->volumes.push_back(volume);
    }
}

void ObjectList::set_volume_type(ModelVolumeType new_type, bool preserve_ps_subtype)
{
    struct VolumeSelection {
        int          object_idx;
        ModelVolume* volume;
    };

    std::vector<VolumeSelection> volumes;
    auto add_volume = [&volumes](int obj_idx, ModelVolume* volume) {
        if (volume == nullptr)
            return;
        auto it = std::find_if(volumes.begin(), volumes.end(), [volume](const VolumeSelection& other) { return other.volume == volume; });
        if (it == volumes.end())
            volumes.push_back({ obj_idx, volume });
    };

    wxDataViewItemArray sels;
    GetSelections(sels);
    for (auto item : sels) {
        wxDataViewItem volume_item = item;
        ItemType       type        = m_objects_model->GetItemType(item);
        if (!(type & itVolume)) {
            if ((type & itSettings) && (m_objects_model->GetItemType(m_objects_model->GetParent(item)) & itVolume))
                volume_item = m_objects_model->GetParent(item);
            else
                continue;
        }

        const int obj_idx = m_objects_model->GetObjectIdByItem(volume_item);
        const int vol_idx = m_objects_model->GetVolumeIdByItem(volume_item);
        if (obj_idx < 0 || vol_idx < 0 || obj_idx >= m_objects->size())
            continue;

        const int real_idx = m_objects_model->get_real_volume_index_in_3d(obj_idx, vol_idx);
        if (real_idx < 0 || real_idx >= (*m_objects)[obj_idx]->volumes.size())
            continue;

        add_volume(obj_idx, (*m_objects)[obj_idx]->volumes[real_idx]);
    }

    auto collect_from_canvas = [&add_volume](GLCanvas3D* canvas) {
        if (canvas == nullptr)
            return;
        const Selection& selection = canvas->get_selection();
        for (auto idx : selection.get_volume_idxs()) {
            const GLVolume* gl_volume = selection.get_volume(idx);
            if (gl_volume == nullptr || gl_volume->object_idx() < 0)
                continue;
            ModelVolume* volume = get_model_volume(*gl_volume, selection.get_model()->objects);
            add_volume(gl_volume->object_idx(), volume);
        }
    };

    if (volumes.empty()) {
        collect_from_canvas(wxGetApp().plater()->canvas3D());
        if (volumes.empty()) {
            auto canvas_type = wxGetApp().plater()->get_current_canvas3D()->get_canvas_type();
            if (canvas_type == GLCanvas3D::ECanvasType::CanvasView3D && is_connectors_item_selected())
                collect_from_canvas(wxGetApp().plater()->get_view3D_canvas3D());
        }
        if (volumes.empty())
            return;
    }

    if (new_type == ModelVolumeType::SUPPORT_BLOCKER || new_type == ModelVolumeType::SUPPORT_ENFORCER
        || is_precise_seam(new_type)) {
        const bool has_text_or_svg = std::any_of(volumes.begin(), volumes.end(),
            [](const VolumeSelection& sel) { return sel.volume->is_svg() || sel.volume->is_text(); });
        if (has_text_or_svg) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                << ": blocked attempt to set SUPPORT_BLOCKER/ENFORCER or Precise Seam on SVG/text volume; "
                << "UI guard should have prevented this -- possible regression in the Change Type menu";
            return;
        }
    }

    auto effective_new_type = [new_type, preserve_ps_subtype](const ModelVolume* v) -> ModelVolumeType {
        if (preserve_ps_subtype && new_type == ModelVolumeType::PRECISE_SEAM_CENTER && v->is_precise_seam())
            return v->type();
        return new_type;
    };

    const bool any_diff = std::any_of(volumes.begin(), volumes.end(),
        [&effective_new_type](const VolumeSelection& sel) {
            return sel.volume->type() != effective_new_type(sel.volume);
        });

    if (!any_diff)
        return;

    if (new_type != ModelVolumeType::MODEL_PART) {
        std::map<int, int> total_part_cnt;
        std::map<int, int> selected_part_cnt;

        for (const auto& sel : volumes) {
            if (total_part_cnt.find(sel.object_idx) == total_part_cnt.end()) {
                int count = 0;
                for (auto vol : (*m_objects)[sel.object_idx]->volumes)
                    if (vol->type() == ModelVolumeType::MODEL_PART)
                        ++count;
                total_part_cnt.emplace(sel.object_idx, count);
            }
            if (sel.volume->type() == ModelVolumeType::MODEL_PART)
                ++selected_part_cnt[sel.object_idx];
        }

        for (const auto& sel : selected_part_cnt) {
            auto it = total_part_cnt.find(sel.first);
            if (it != total_part_cnt.end() && it->second > 0 && sel.second == it->second) {
                Slic3r::GUI::show_error(nullptr, _(L("The type of the last solid object part cannot be changed.")));
                return;
            }
        }
    }

    take_snapshot(_u8L("Change part type"));

    std::set<const ModelVolume*> changed_volumes;
    std::set<int>                touched_objects;
    for (const auto& sel : volumes) {
        const ModelVolumeType target   = effective_new_type(sel.volume);
        const ModelVolumeType old_type = sel.volume->type();

        changed_volumes.insert(sel.volume);
        touched_objects.insert(sel.object_idx);

        if (old_type == target)
            continue;

        sel.volume->set_type(target);

        if (precise_seam_group_changed(old_type, target))
            move_volume_to_end((*m_objects)[sel.object_idx], sel.volume);
    }

    wxDataViewItemArray new_selection;
    for (int obj_idx : touched_objects) {
        wxDataViewItemArray sel_items = reorder_volumes_and_get_selection(obj_idx, [&changed_volumes](const ModelVolume* volume) {
            return changed_volumes.find(volume) != changed_volumes.end();
        });
        for (const auto& item : sel_items)
            new_selection.push_back(item);
    }

    if (!new_selection.IsEmpty()) {
        m_prevent_list_events = true;
        UnselectAll();
        SetSelections(new_selection);
        m_prevent_list_events = false;
    }
}

void ObjectList::last_volume_is_deleted(const int obj_idx)
{
#if 0
    if (obj_idx < 0 || size_t(obj_idx) >= m_objects->size() || (*m_objects)[obj_idx]->volumes.size() != 1)
        return;

    auto volume = (*m_objects)[obj_idx]->volumes.front();

    volume->config.reset();

    volume->config.set_key_value("extruder", new ConfigOptionInt(1));
#endif
}

void ObjectList::update_and_show_object_settings_item()
{
    part_selection_changed();
}

void ObjectList::update_settings_item_and_selection(wxDataViewItem item, wxDataViewItemArray& selections)
{
    const wxDataViewItem old_settings_item = m_objects_model->GetSettingsItem(item);
    const wxDataViewItem new_settings_item = add_settings_item(item, &get_item_config(item).get());

    if (!new_settings_item && old_settings_item)
        m_objects_model->Delete(old_settings_item);

    if (selections.Index(old_settings_item) != wxNOT_FOUND)
    {
        if (old_settings_item == new_settings_item)
        {
            Sidebar& panel = wxGetApp().sidebar();
            panel.Freeze();

            wxGetApp().obj_settings()->UpdateAndShow(true);

            panel.Layout();
            panel.Thaw();
        }
        else
        {
            selections.Remove(old_settings_item);

            if (selections.Index(item) == wxNOT_FOUND) {
                selections.Add(item);
                select_item(item);
            }
        }
    }
}

void ObjectList::update_object_list_by_printer_technology()
{
    m_prevent_canvas_selection_update = true;
    wxDataViewItemArray sel;
    GetSelections(sel);

    wxDataViewItemArray object_items;
    m_objects_model->GetChildren(wxDataViewItem(nullptr), object_items);

    for (auto& object_item : object_items) {
        update_info_items(m_objects_model->GetObjectIdByItem(object_item), &sel);

        update_settings_item_and_selection(object_item, sel);

        wxDataViewItemArray all_object_subitems;
        m_objects_model->GetChildren(object_item, all_object_subitems);
        for (auto item : all_object_subitems)
            if (m_objects_model->GetItemType(item) & itVolume)
                update_settings_item_and_selection(item, sel);

        wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(object_item);
        if (!layers_item)
            layers_item = add_layer_root_item(object_item);
        else if (printer_technology() == ptSLA) {
            wxDataViewItemArray del_items;
            bool some_layers_was_selected = false;
            m_objects_model->GetAllChildren(layers_item, del_items);
            for (auto& del_item:del_items)
                if (sel.Index(del_item) != wxNOT_FOUND) {
                    some_layers_was_selected = true;
                    sel.Remove(del_item);
                }
            if (sel.Index(layers_item) != wxNOT_FOUND) {
                some_layers_was_selected = true;
                sel.Remove(layers_item);
            }

            m_objects_model->Delete(layers_item);

            if (some_layers_was_selected)
                sel.Add(object_item);
        }
        else {
            wxDataViewItemArray all_obj_layers;
            m_objects_model->GetChildren(layers_item, all_obj_layers);

            for (auto item : all_obj_layers)
                update_settings_item_and_selection(item, sel);
        }
    }

    SetSelections(sel);
    m_prevent_canvas_selection_update = false;
}

void ObjectList::instances_to_separated_object(const int obj_idx, const std::set<int>& inst_idxs)
{
    if ((*m_objects)[obj_idx]->instances.size() == inst_idxs.size())
    {
        instances_to_separated_objects(obj_idx);
        return;
    }

    ModelObject* model_object = (*m_objects)[obj_idx]->get_model()->add_object(*(*m_objects)[obj_idx]);
    for (int inst_idx = int(model_object->instances.size()) - 1; inst_idx >= 0; inst_idx--)
    {
        if (find(inst_idxs.begin(), inst_idxs.end(), inst_idx) != inst_idxs.end())
            continue;
        model_object->delete_instance(inst_idx);
    }

    const size_t new_obj_indx = static_cast<size_t>(m_objects->size() - 1);
    add_object_to_list(new_obj_indx);

    for (std::set<int>::const_reverse_iterator it = inst_idxs.rbegin(); it != inst_idxs.rend(); ++it)
    {
        del_subobject_from_object(obj_idx, *it, itInstance);
        delete_instance_from_list(obj_idx, *it);
    }

    wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_object(new_obj_indx);
    update_info_items(new_obj_indx);
}

void ObjectList::instances_to_separated_objects(const int obj_idx)
{
    const int inst_cnt = (*m_objects)[obj_idx]->instances.size();

    std::vector<size_t> object_idxs;

    for (int i = inst_cnt-1; i > 0 ; i--)
    {
        ModelObject* object = (*m_objects)[obj_idx]->get_model()->add_object(*(*m_objects)[obj_idx]);
        for (int inst_idx = object->instances.size() - 1; inst_idx >= 0; inst_idx--)
        {
            if (inst_idx == i)
                continue;
            object->delete_instance(inst_idx);
        }

        const size_t new_obj_indx = static_cast<size_t>(m_objects->size() - 1);
        add_object_to_list(new_obj_indx);
        object_idxs.push_back(new_obj_indx);

        del_subobject_from_object(obj_idx, i, itInstance);
        delete_instance_from_list(obj_idx, i);
    }

    wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_objects(object_idxs);
    for (size_t object : object_idxs)
        update_info_items(object);
}

void ObjectList::split_instances()
{
    const Selection& selection = scene_selection();
    const int obj_idx = selection.get_object_idx();
    if (obj_idx == -1)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Instances to Separated Objects"));

    if (selection.is_single_full_object())
    {
        instances_to_separated_objects(obj_idx);
        return;
    }

    const int inst_idx = selection.get_instance_idx();
    const std::set<int> inst_idxs = inst_idx < 0 ?
                                    selection.get_instance_idxs() :
                                    std::set<int>{ inst_idx };

    instances_to_separated_object(obj_idx, inst_idxs);
}

void ObjectList::rename_item()
{
    const wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item) & (itVolume | itObject)))
        return ;

    const wxString new_name = wxGetTextFromUser(_(L("Enter new name"))+":", _(L("Renaming")),
                                                m_objects_model->GetName(item), this);

    if (new_name.IsEmpty())
        return;

    if (Plater::has_illegal_filename_characters(new_name)) {
        Plater::show_illegal_characters_warning(this);
        return;
    }

    if (m_objects_model->SetName(new_name, item))
        update_name_in_model(item);
}

void ObjectList::fix_through_cgal()
{
    if (!wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager().check_gizmos_closed_except(GLGizmosManager::Undefined))
        return;

    std::vector<std::string>                           succes_models;
    std::vector<std::pair<std::string, std::string>>   failed_models;

    std::vector<int> obj_idxs, vol_idxs;
    get_selection_indexes(obj_idxs, vol_idxs);

    std::vector<std::string> model_names;

    if (vol_idxs.empty()) {
#if !FIX_THROUGH_CGAL_ALWAYS
        for (int i = int(obj_idxs.size())-1; i >= 0; --i)
                if (object(obj_idxs[i])->get_repaired_errors_count() == 0)
                    obj_idxs.erase(obj_idxs.begin()+i);
#endif
        for (int obj_idx : obj_idxs)
            if (object(obj_idx))
                model_names.push_back(object(obj_idx)->name);
    }
    else {
        ModelObject* obj = object(obj_idxs.front());
        if (obj) {
#if !FIX_THROUGH_CGAL_ALWAYS
            for (int i = int(vol_idxs.size()) - 1; i >= 0; --i)
                if (obj->get_repaired_errors_count(vol_idxs[i]) == 0)
                    vol_idxs.erase(vol_idxs.begin() + i);
#endif
            for (int vol_idx : vol_idxs)
                model_names.push_back(obj->volumes[vol_idx]->name);
        }
    }

    auto plater = wxGetApp().plater();

    auto fix_and_update_progress = [this, plater, model_names](const int obj_idx, const int vol_idx,
                                          int model_idx,
                                          ProgressDialog& progress_dlg,
                                          std::vector<std::string>& succes_models,
                                          std::vector<std::pair<std::string, std::string>>& failed_models)
    {
        if (!object(obj_idx))
            return false;

        const std::string& model_name = model_names[model_idx];
        wxString msg = _L("Repairing model object");
        if (model_names.size() == 1)
            msg += ": " + from_u8(model_name) + "\n";
        else {
            msg += ":\n";
            for (int i = 0; i < int(model_names.size()); ++i)
                msg += (i == model_idx ? " > " : "   ") + from_u8(model_names[i]) + "\n";
            msg += "\n";
        }

        const bool keep_painting = GUI::wxGetApp().app_config->get_bool("keep_painting");
        if (!keep_painting) {
            plater->clear_before_change_mesh(obj_idx);
        }
        const size_t volumes_before = object(obj_idx)->volumes.size();
        std::string res;
        if (!fix_model_with_cgal_gui(*(object(obj_idx)), vol_idx, progress_dlg, msg, res, keep_painting))
            return false;
        object(obj_idx)->ensure_on_bed();
        plater->changed_mesh(obj_idx);

        const size_t volumes_after = object(obj_idx)->volumes.size();
        if (volumes_after != volumes_before)
            add_volumes_to_object_in_list(obj_idx);

        plater->get_partplate_list().notify_instance_update(obj_idx, 0);
        plater->sidebar().obj_list()->update_plate_values_for_items();

        if (res.empty())
            succes_models.push_back(model_name);
        else
            failed_models.push_back({ model_name, res });

        update_item_error_icon(obj_idx, vol_idx);
        update_info_items(obj_idx);

        return true;
    };

    Plater::TakeSnapshot snapshot(plater, _u8L("Repairing model object"));

    ProgressDialog progress_dlg(_L("Repairing model object"), "", 100, find_toplevel_parent(plater),
                                    wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT, true);
    int model_idx{ 0 };
    if (vol_idxs.empty()) {
        int vol_idx{ -1 };
        for (int obj_idx : obj_idxs) {
#if !FIX_THROUGH_CGAL_ALWAYS
            if (object(obj_idx)->get_repaired_errors_count(vol_idx) == 0)
                continue;
#endif
            if (!fix_and_update_progress(obj_idx, vol_idx, model_idx, progress_dlg, succes_models, failed_models))
                break;
            model_idx++;
        }
    }
    else {
        int obj_idx{ obj_idxs.front() };
        for (int vol_idx : vol_idxs) {
            if (!fix_and_update_progress(obj_idx, vol_idx, model_idx, progress_dlg, succes_models, failed_models))
                break;
            model_idx++;
        }
    }
    progress_dlg.Update(100, "");

    wxString msg;
    wxString bullet_suf = "\n   - ";
    if (!succes_models.empty()) {
        msg = _L_PLURAL("The following model object has been repaired", "The following model objects have been repaired", succes_models.size()) + ":";
        for (auto& model : succes_models)
            msg += bullet_suf + from_u8(model);
        msg += "\n\n";
    }
    if (!failed_models.empty()) {
        msg += _L_PLURAL("Failed to repair the following model object", "Failed to repair the following model objects", failed_models.size()) + ":\n";
        for (auto& model : failed_models)
            msg += bullet_suf + from_u8(model.first) + ": " + _(model.second);
    }
    if (msg.IsEmpty())
        msg = _L("Repairing was canceled");
    plater->get_notification_manager()->push_notification(NotificationType::CgalFinished, NotificationManager::NotificationLevel::PrintInfoShortNotificationLevel, into_u8(msg));
}

void ObjectList::simplify()
{
    auto plater = wxGetApp().plater();
    GLGizmosManager& gizmos_mgr = plater->get_view3D_canvas3D()->get_gizmos_manager();

    if (! gizmos_mgr.check_gizmos_closed_except(GLGizmosManager::EType::Simplify))
        return;

    if (gizmos_mgr.get_current_type() == GLGizmosManager::Simplify) {
        gizmos_mgr.open_gizmo(GLGizmosManager::EType::Simplify);
    }
    gizmos_mgr.open_gizmo(GLGizmosManager::EType::Simplify);
}

void GUI::ObjectList::smooth_mesh()
{
    wxBusyCursor cursor;
    auto plater = wxGetApp().plater();
    if (!plater) { return; }
    plater->take_snapshot("smooth_mesh");
    std::vector<int> obj_idxs, vol_idxs;
    get_selection_indexes(obj_idxs, vol_idxs);
    auto object_idx = obj_idxs.front();
    ModelObject *obj{nullptr};
    auto show_warning_dlg = [](int cur_face_count,std::string name,bool is_part) {
        int limit_face_count = 1000000;
        if (cur_face_count > limit_face_count) {
            auto name_str = wxString::FromUTF8(name);
            auto content = wxString::Format(_L("\"%s\" will exceed 1 million faces after this subdivision, which may increase slicing time. Do you want to continue?"), name_str);
            WarningDialog dlg(static_cast<wxWindow *>(wxGetApp().mainframe), (is_part ? _L("Part") : _L("Object")) + " " + content, wxEmptyString, wxYES_NO);
            if (dlg.ShowModal() == wxID_NO) {
                return true;
            }
            return false;
        }
        return false;
    };
    auto show_smooth_mesh_error_dlg = [](std::string name) {
        auto name_str = wxString::FromUTF8(name);
        auto content  = wxString::Format(_L("\"%s\" part's mesh contains errors. Please repair it first."), name_str);
        WarningDialog dlg(static_cast<wxWindow *>(wxGetApp().mainframe), content, wxEmptyString, wxOK);
        dlg.ShowModal();
    };
    const bool keep_painting = GUI::wxGetApp().app_config->get_bool("keep_painting");
    bool has_show_smooth_mesh_error_dlg = false;
    if (vol_idxs.empty()) {
        obj        = object(object_idx);
        auto             future_face_count = static_cast<int>(obj->facets_count()) * 4;
        if (show_warning_dlg(future_face_count, obj->name,false)) {
            return;
        }
        for (auto mv : obj->volumes) {
            bool ok;
            auto result_mesh = TriangleMeshDeal::smooth_triangle_mesh(mv->mesh(), ok);
            if (ok) {
                const std::optional<TriangleSelector::SavedPainting> saved_painting = keep_painting ?
                                                                                          mv->save_painting() :
                                                                                          std::optional<TriangleSelector::SavedPainting>{};
                mv->set_mesh(result_mesh);
                mv->restore_painting(saved_painting);
                mv->calculate_convex_hull();
                mv->invalidate_convex_hull_2d();
                mv->set_new_unique_id();
            } else {
                if (!has_show_smooth_mesh_error_dlg) {
                    show_smooth_mesh_error_dlg(mv->name);
                    has_show_smooth_mesh_error_dlg = true;
                }
            }
        }
        obj->invalidate_bounding_box();
        obj->ensure_on_bed();
        plater->changed_mesh(object_idx);
    } else {
        obj = object(obj_idxs.front());
        for (int vol_idx : vol_idxs) {
            auto mv = obj->volumes[vol_idx];
            auto future_face_count = static_cast<int>(mv->mesh().facets_count()) * 4;
            if (show_warning_dlg(future_face_count, mv->name,true)) {
                return;
            }
            bool ok;
            auto result_mesh = TriangleMeshDeal::smooth_triangle_mesh(mv->mesh(),ok);
            if (ok) {
                const std::optional<TriangleSelector::SavedPainting> saved_painting = keep_painting ?
                                                                                          mv->save_painting() :
                                                                                          std::optional<TriangleSelector::SavedPainting>{};
                mv->set_mesh(result_mesh);
                mv->restore_painting(saved_painting);
                mv->calculate_convex_hull();
                mv->invalidate_convex_hull_2d();
                mv->set_new_unique_id();
            } else {
                if (!has_show_smooth_mesh_error_dlg) {
                    show_smooth_mesh_error_dlg(mv->name);
                    has_show_smooth_mesh_error_dlg = true;
                }
            }
        }
    }
    if (obj) {
        obj->invalidate_bounding_box();
        obj->ensure_on_bed();
        plater->changed_mesh(object_idx);
    }
}

void ObjectList::update_item_error_icon(const int obj_idx, const int vol_idx) const
{
    auto obj = object(obj_idx);
    if (wxDataViewItem obj_item = m_objects_model->GetItemById(obj_idx)) {
        const std::string& icon_name = get_warning_icon_name(obj->get_object_stl_stats());
        m_objects_model->UpdateWarningIcon(obj_item, icon_name);
    }

    if (vol_idx < 0)
        return;

    if (wxDataViewItem vol_item = m_objects_model->GetItemByVolumeId(obj_idx, vol_idx)) {
        const std::string& icon_name = get_warning_icon_name(obj->volumes[vol_idx]->mesh().stats());
        m_objects_model->UpdateWarningIcon(vol_item, icon_name);
    }
}

void ObjectList::msw_rescale()
{
    set_min_height();

    const int em = wxGetApp().em_unit();

    for (int cn = colName; cn < colCount; cn++)
        GetColumn(cn)->SetWidth(m_columns_width[cn] * em);

    SetRowHeight(2 * em + FromDIP(2));

    m_objects_model->Rescale();

    Layout();
}

void ObjectList::sys_color_changed()
{
    wxGetApp().UpdateDVCDarkUI(this, true);
    
    msw_rescale();

    if (m_objects_model) { m_objects_model->sys_color_changed(); }
}

void ObjectList::ItemValueChanged(wxDataViewEvent &event)
{
    if (event.GetColumn() == colName)
        update_name_in_model(event.GetItem());
    else if (event.GetColumn() == colFilament) {
        wxDataViewItem item = event.GetItem();
        if (m_objects_model->GetItemType(item) == itObject)
            m_objects_model->UpdateVolumesExtruderBitmap(item, true);
        update_filament_in_config(item);
    }
}

#ifdef __WXOSX__
bool ObjectList::is_live_model_item(wxDataViewItem item) const
{
    if (!item.IsOk() || !m_objects_model)
        return false;

    wxDataViewItemArray all_items;
    m_objects_model->GetAllChildren(wxDataViewItem(nullptr), all_items);
    for (const wxDataViewItem& live_item : all_items)
        if (live_item.GetID() == item.GetID())
            return true;

    return false;
}

void ObjectList::start_filament_editor(wxDataViewItem item)
{
    if (m_starting_filament_editor || !is_live_model_item(item))
        return;

    const ItemType type = m_objects_model->GetItemType(item);
    if (!(type & (itVolume | itLayer | itObject)))
        return;

    auto* column = GetColumn(colFilament);
    if (!column)
        return;

    auto* custom_renderer = dynamic_cast<wxDataViewCustomRenderer*>(column->GetRenderer());
    if (!custom_renderer || custom_renderer->GetEditorCtrl())
        return;

    m_starting_filament_editor = true;
    m_filament_editor_item = item;
    const bool started = custom_renderer->StartEditing(item, GetItemRect(item, column));
    m_filament_editor_item = wxDataViewItem(nullptr);
    m_starting_filament_editor = false;

    if (!started)
        return;

    SetCustomRendererPtr(custom_renderer);
    SetCustomRendererItem(item);
}
#endif

void GUI::ObjectList::OnStartEditing(wxDataViewEvent &event)
{
    auto col  = event.GetColumn();
    auto item = event.GetItem();
#ifdef __WXOSX__
    if (col == colFilament) {
        if (m_starting_filament_editor)
            return;

        event.Veto();
        CallAfter([this, item] {
            if (!is_live_model_item(item))
                return;
            start_filament_editor(item);
        });
        return;
    }
#endif

    if (col == colName) {
        ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
        if (node->GetType() & itPlate) {
            int plate_idx = node->GetPlateIdx();
            if (plate_idx >= 0) {
                auto plate = wxGetApp().plater()->get_partplate_list().get_plate(plate_idx);
                m_objects_model->SetName(from_u8(plate->get_plate_name()), GetSelection());
            }
        }
    }
}

void ObjectList::OnEditingStarted(wxDataViewEvent &event)
{
#ifdef __WXMSW__
	m_last_selected_column = -1;
#else
    event.Veto();
    auto col = event.GetColumn();
    auto item = event.GetItem();
    if (col == colPrint) {
        toggle_printable_state();
        return;
    } else if (col == colSupportPaint) {
        ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
        if (node->HasSupportPainting()) {
            GLGizmosManager& gizmos_mgr = wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager();
            if (gizmos_mgr.get_current_type() != GLGizmosManager::EType::FdmSupports)
                gizmos_mgr.open_gizmo(GLGizmosManager::EType::FdmSupports);
            else
                gizmos_mgr.reset_all_states();
        }
        return;
    }
    else if (col == colColorPaint) {
        ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
        if (node->HasColorPainting()) {
            GLGizmosManager& gizmos_mgr = wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager();
            if (gizmos_mgr.get_current_type() != GLGizmosManager::EType::MmSegmentation)
                gizmos_mgr.open_gizmo(GLGizmosManager::EType::MmSegmentation);
            else
                gizmos_mgr.reset_all_states();
        }
        return;
    }
    else if (col == colSinking) {
        Plater *    plater = wxGetApp().plater();
        GLCanvas3D *cnv    = plater->canvas3D();
        Plater::TakeSnapshot(plater, _u8L("Shift objects to bed"));
        int obj_idx, vol_idx;
        get_selected_item_indexes(obj_idx, vol_idx, item);
        (*m_objects)[obj_idx]->ensure_on_bed();
        cnv->reload_scene(true, true);
        update_info_items(obj_idx);
        notify_instance_updated(obj_idx);
    }
    else if (col == colEditing) {
        int obj_idx, vol_idx;

        get_selected_item_indexes(obj_idx, vol_idx, item);
        dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab(vol_idx >= 0))->reset_model_config();
        return;
    }
#ifdef __WXOSX__
    if (col == colFilament) {
        start_filament_editor(item);
        return;
    }
#endif
    if (col != colFilament && col != colName)
        return;
    auto column = GetColumn(col);
    const auto renderer = column->GetRenderer();
    if (!renderer->GetEditorCtrl()) {
        renderer->StartEditing(item, GetItemRect(item, column));
        if (col == colName)
            SetAcceleratorTable(wxNullAcceleratorTable);
    }
#ifdef __WXOSX__
    SetCustomRendererPtr(dynamic_cast<wxDataViewCustomRenderer*>(renderer));
#endif
#endif
}

void ObjectList::OnEditingDone(wxDataViewEvent &event)
{
    if (event.GetColumn() != colName)
        return;

    if (event.IsEditCancelled()) {
        if (m_objects_model->GetItemType(event.GetItem()) & itPlate) {
            int plate_idx = -1;
            m_objects_model->GetItemType(event.GetItem(), plate_idx);
            if (plate_idx >= 0) {
                auto plate = wxGetApp().plater()->get_partplate_list().get_plate(plate_idx);
                m_objects_model->SetCurSelectedPlateFullName(plate_idx, plate->get_plate_name());
            }
        }
    }

    const auto renderer = dynamic_cast<BitmapTextRenderer*>(GetColumn(colName)->GetRenderer());
#if __WXOSX__
    SetAcceleratorTable(m_accel);
#endif

    if (renderer->WasCanceled())
		wxTheApp->CallAfter([this]{ Plater::show_illegal_characters_warning(this); });

#ifdef __WXMSW__
	m_last_selected_column = -1;
#endif

    Plater* plater = wxGetApp().plater();
    if (plater)
        plater->set_current_canvas_as_dirty();
}

void ObjectList::set_extruder_for_selected_items(const int extruder)
{
    std::vector<std::string> colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    if (extruder > colors.size())
        return;

    wxDataViewItemArray sels;
    GetSelections(sels);

    if (sels.empty())
        return;

    take_snapshot(_u8L("Change Filaments"));

    for (const wxDataViewItem& sel_item : sels)
    {
        ItemType sel_item_type = m_objects_model->GetItemType(sel_item);
        wxDataViewItem item = (sel_item_type & itInstance) ? m_objects_model->GetObject(sel_item) : sel_item;
        ItemType type = m_objects_model->GetItemType(item);
        if (type & itVolume) {
            const int obj_idx = m_objects_model->GetObjectIdByItem(item);
            int vol_idx = m_objects_model->GetVolumeIdByItem(item);
            vol_idx     = m_objects_model->get_real_volume_index_in_3d(obj_idx, vol_idx);
            if ((obj_idx < m_objects->size()) && (obj_idx < (*m_objects)[obj_idx]->volumes.size())) {
                auto volume_type = (*m_objects)[obj_idx]->volumes[vol_idx]->type();
                if (volume_type != ModelVolumeType::MODEL_PART && volume_type != ModelVolumeType::PARAMETER_MODIFIER)
                    continue;
            }
        }

        if (type & itLayerRoot)
            continue;

        int new_extruder = extruder;
        if (extruder == 0) {
            if (type & itObject) {
                new_extruder = 1;
            }
            else if ((type & itVolume) && (m_objects_model->GetVolumeType(sel_item) == ModelVolumeType::MODEL_PART)) {
                new_extruder = m_objects_model->GetExtruderNumber(m_objects_model->GetParent(sel_item));
            }
        }

        if (type & itInfo && m_objects_model->GetInfoItemType(item) == InfoItemType::CutConnectors) {
            const int obj_idx = m_objects_model->GetObjectIdByItem(item);
            for (size_t i = 0; i < (*m_objects)[obj_idx]->volumes.size(); i++) {
                auto mv = (*m_objects)[obj_idx]->volumes[i];
                if (mv->is_cut_connector()) {
                    ModelConfig &config = mv->config;
                    if (config.has("extruder"))
                        config.set("extruder", new_extruder);
                    else
                        config.set_key_value("extruder", new ConfigOptionInt(new_extruder));
                }
            }
        } else {
            ModelConfig &config = get_item_config(item);
            if (config.has("extruder"))
                config.set("extruder", new_extruder);
            else
                config.set_key_value("extruder", new ConfigOptionInt(new_extruder));
        }

        if (type & itObject) {
            ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
            for (ModelVolume* mv : node->m_model_object->volumes) {
                if (mv->type() == ModelVolumeType::MODEL_PART && mv->config.has("extruder"))
                    mv->config.erase("extruder");
            }
        }

        const wxString extruder_str = wxString::Format("%d", new_extruder);
        m_objects_model->SetExtruder(extruder_str, item);
    }

    wxGetApp().plater()->update();

    Refresh();
}

void ObjectList::on_plate_added(PartPlate* part_plate)
{
    wxDataViewItem plate_item = m_objects_model->AddPlate(part_plate);
}

void ObjectList::on_plate_deleted(int plate_idx)
{
    m_objects_model->DeletePlate(plate_idx);

    wxDataViewItemArray top_list;
    m_objects_model->GetChildren(wxDataViewItem(nullptr), top_list);
    for (wxDataViewItem item : top_list) {
        Expand(item);
    }
}

void ObjectList::reload_all_plates(bool notify_partplate)
{
    m_prevent_canvas_selection_update = true;

    m_prevent_list_events = true;
    this->UnselectAll();
    m_objects_model->ResetAll();
    m_prevent_list_events = false;

    PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
    for (int i = 0; i < ppl.get_plate_count(); i++) {
        PartPlate* pp = ppl.get_plate(i);
        m_objects_model->AddPlate(pp, wxEmptyString);
    }

    size_t obj_idx = 0;
    std::vector<size_t> obj_idxs;
    obj_idxs.reserve(m_objects->size());
    while (obj_idx < m_objects->size()) {
        add_object_to_list(obj_idx, false, notify_partplate);
        obj_idxs.push_back(obj_idx);
        ++obj_idx;
    }

    update_selections();

    m_prevent_canvas_selection_update = false;

    wxGetApp().plater()->update();
    wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_objects(obj_idxs);
}

void ObjectList::on_plate_selected(int plate_index)
{
    wxDataViewItem item = m_objects_model->GetItemByPlateId(plate_index);
    wxDataViewItem sel = GetSelection();

    if (sel == item)
        return;

    UnselectAll();
    Select(item);
}

void ObjectList::notify_instance_updated(int obj_idx)
{
    const int inst_cnt = (*m_objects)[obj_idx]->instances.size();
    PartPlateList& list = wxGetApp().plater()->get_partplate_list();
    for (int index = 0; index < inst_cnt; index ++)
        list.notify_instance_update(obj_idx, index);
}

void ObjectList::update_after_undo_redo()
{
    Plater::SuppressSnapshots suppress(wxGetApp().plater());
    reload_all_plates(false);
}

wxDataViewItemArray ObjectList::reorder_volumes_and_get_selection(int obj_idx, std::function<bool(const ModelVolume*)> add_to_selection )
{
    wxDataViewItemArray items;
    if (obj_idx < 0 || obj_idx >= m_objects->size()) {
        return items;
    }
    ModelObject* object = (*m_objects)[obj_idx];
    if (object->volumes.size() <= 1)
        return items;

    object->sort_volumes(true);
    update_info_items(obj_idx, nullptr, true);
    items = add_volumes_to_object_in_list(obj_idx, std::move(add_to_selection));

    changed_object(obj_idx);
    return items;
}

void ObjectList::apply_volumes_order()
{
    if (!m_objects)
        return;

    for (size_t obj_idx = 0; obj_idx < m_objects->size(); obj_idx++)
        reorder_volumes_and_get_selection(obj_idx);
}

void ObjectList::update_printable_state(int obj_idx, int instance_idx)
{
    ModelObject* object = (*m_objects)[obj_idx];

    const PrintIndicator printable = object->instances[instance_idx]->printable ? piPrintable : piUnprintable;
    if (object->instances.size() == 1)
        instance_idx = -1;

    m_objects_model->SetPrintableState(printable, obj_idx, instance_idx);
}

void ObjectList::toggle_printable_state()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return;

    wxDataViewItem frst_item = sels[0];

    ItemType type = m_objects_model->GetItemType(frst_item);
    if (!(type & (itObject | itInstance)))
        return;


    int obj_idx = m_objects_model->GetObjectIdByItem(frst_item);
    int inst_idx = type == itObject ? 0 : m_objects_model->GetInstanceIdByItem(frst_item);
    bool printable = !object(obj_idx)->instances[inst_idx]->printable;

    take_snapshot("");

    std::vector<size_t> obj_idxs;
    for (auto item : sels)
    {
        type = m_objects_model->GetItemType(item);
        if (!(type & (itObject | itInstance)))
            continue;

        obj_idx = m_objects_model->GetObjectIdByItem(item);
        ModelObject* obj = object(obj_idx);

        obj_idxs.emplace_back(static_cast<size_t>(obj_idx));

        if (type == itInstance) {
            inst_idx = m_objects_model->GetInstanceIdByItem(item);
            obj->instances[m_objects_model->GetInstanceIdByItem(item)]->printable = printable;
        }
        else
            for (auto inst : obj->instances)
                inst->printable = printable;

        m_objects_model->SetObjectPrintableState(printable ? piPrintable : piUnprintable, item);
    }

    sort(obj_idxs.begin(), obj_idxs.end());
    obj_idxs.erase(unique(obj_idxs.begin(), obj_idxs.end()), obj_idxs.end());

    wxGetApp().plater()->get_view3D_canvas3D()->update_instance_printable_state_for_objects(obj_idxs);

    wxGetApp().plater()->update();
    wxGetApp().plater()->reload_paint_after_background_process_apply();
}

void ObjectList::toggle_auto_drop()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return;

    for (auto item : sels) {
        ItemType type = m_objects_model->GetItemType(item);
        if (!(type & (itObject | itInstance)))
            return;
    }

    const bool current_auto_drop = wxGetApp().plater()->get_selection().get_auto_drop();

    take_snapshot("");

    std::vector<size_t> obj_idxs;
    for (auto item : sels) {
        int obj_idx      = m_objects_model->GetObjectIdByItem(item);
        ModelObject* obj = object(obj_idx);

        obj_idxs.emplace_back(static_cast<size_t>(obj_idx));

        ItemType type = m_objects_model->GetItemType(item);
        if (type == itInstance) {
            int inst_idx = m_objects_model->GetInstanceIdByItem(item);
            obj->instances[inst_idx]->auto_drop = !current_auto_drop;
        } else {
            for (auto inst : obj->instances)
                inst->auto_drop = !current_auto_drop;
                
        if (current_auto_drop == false) 
            obj->ensure_on_bed();
        }
    }

    sort(obj_idxs.begin(), obj_idxs.end());
    obj_idxs.erase(unique(obj_idxs.begin(), obj_idxs.end()), obj_idxs.end());

    wxGetApp().plater()->update();
    wxGetApp().plater()->reload_paint_after_background_process_apply();
}

void ObjectList::enable_layers_editing()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return;

    wxDataViewItem frst_item = sels[0];

    ItemType type = m_objects_model->GetItemType(frst_item);
    if (!(type & itObject))
        return;

    auto view3d = wxGetApp().plater()->get_view3D_canvas3D();
    if (view3d != nullptr && m_objects_model->IsVariableHeight(frst_item)){
        view3d->enable_layers_editing(true);
    }
}

ModelObject* ObjectList::object(const int obj_idx) const
{
    if (obj_idx < 0)
        return nullptr;

    return (*m_objects)[obj_idx];
}

void ObjectList::apply_object_instance_transfrom_to_all_volumes(ModelObject *model_object, bool need_update_assemble_matrix)
{
    const Geometry::Transformation &instance_transformation  = model_object->instances[0]->get_transformation();
    Vec3d                           original_instance_center = instance_transformation.get_offset();

    if (need_update_assemble_matrix) {
        Geometry::Transformation instance_transformation_copy = instance_transformation;
        instance_transformation_copy.set_offset(Vec3d(0, 0, 0));
        const Transform3d &instance_inverse_matrix = instance_transformation_copy.get_matrix().inverse();
        const Transform3d &assemble_matrix         = model_object->instances[0]->get_assemble_transformation().get_matrix();
        Transform3d        new_assemble_transform  = assemble_matrix * instance_inverse_matrix;
        model_object->instances[0]->set_assemble_from_transform(new_assemble_transform);
    }

    const Transform3d &transformation_matrix = instance_transformation.get_matrix();
    for (ModelVolume *volume : model_object->volumes) {
        const Transform3d &volume_matrix = volume->get_matrix();
        Transform3d        new_matrix    = transformation_matrix * volume_matrix;
        volume->set_transformation(new_matrix);
    }
    model_object->instances[0]->set_transformation(Geometry::Transformation());

    model_object->ensure_on_bed();
    model_object->translate(-original_instance_center);
    model_object->translate_instances(original_instance_center);

    wxGetApp().plater()->update();
}

}
}
