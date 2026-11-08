#ifndef slic3r_GUI_FilamentPickerDialog_hpp_
#define slic3r_GUI_FilamentPickerDialog_hpp_

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "FilamentBitmapUtils.hpp"
#include "Widgets/Button.hpp"
#include "EncodedFilament.hpp"
#include <wx/colourdata.h>
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/bitmap.h>
#include <wx/region.h>
#include <wx/timer.h>
#include <vector>
#include <string>

namespace Slic3r { namespace GUI {

class FilamentPickerDialog : public DPIDialog
{
public:
    FilamentPickerDialog(wxWindow *parent, const wxString &fila_id, const FilamentColor &fila_color, const std::string &fila_type);
    virtual ~FilamentPickerDialog();

    bool IsDataLoaded() const { return m_is_data_loaded; }
    wxColour GetSelectedColour() const;
    const FilamentColor& GetSelectedFilamentColor() const { return m_cur_filament_color; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

#ifdef __WXGTK__
    void OnWindowCreate(wxWindowCreateEvent& event);
#endif
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnButtonPaint(wxPaintEvent& event);
    void OnTimerCheck(wxTimerEvent& event);
    void OnFlashTimer(wxTimerEvent& event);

    bool IsClickOnTopMostWindow(const wxPoint& mouse_pos);
    void StartClickDetection();
    void StopClickDetection();
    void CleanupTimers();

private:
    wxBoxSizer* CreatePreviewPanel(const FilamentColor& fila_color, const std::string& fila_type);
    wxScrolledWindow* CreateColorGrid();
    wxBoxSizer* CreateSeparatorLine();
    void CreateMoreInfoButton();
    void BindEvents();

    void CreateColorBitmap(const FilamentColor& fila_color);
    wxBoxSizer* CreateInfoSection();
    void SetupLabelsContent(const FilamentColor& fila_color, const std::string& fila_type);

    void UpdatePreview(const FilamentColorCode& filament);
    void UpdateCustomColorPreview(const wxColour& custom_color);
    void UpdateButtonStates(wxBitmapButton* selected_btn);

    void SetWindowShape();
    void CreateShapedBitmap();

    bool LoadFilamentData(const wxString& fila_id);
    wxColourData GetSingleColorData();

    void StartFlashing();

    wxStaticBitmap* m_color_demo{nullptr};
    wxStaticText* m_label_preview_color{nullptr};
    wxStaticText* m_label_preview_idx{nullptr};
    wxStaticText* m_label_preview_type{nullptr};
    Button* m_more_btn{nullptr};
    Button* m_ok_btn{nullptr};
    Button* m_cancel_btn{nullptr};

    bool m_is_data_loaded{false};
    wxString *m_cur_color_name{nullptr};
    FilamentColorCodeQuery* m_color_query{nullptr};
    FilamentColorCodes* m_cur_color_codes{nullptr};
    wxBitmapButton* m_cur_selected_btn{nullptr};
    FilamentColor m_cur_filament_color;

    wxBitmap m_shape_bmp;
    int m_corner_radius{8};

    wxPoint m_drag_delta;

    wxTimer* m_click_timer{nullptr};
    bool m_last_mouse_down{false};

    wxTimer* m_flash_timer{nullptr};
    int m_flash_step{0};
};

}}

#endif
