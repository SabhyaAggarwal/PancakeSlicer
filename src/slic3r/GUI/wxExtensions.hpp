#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <string>
#include <wx/anybutton.h>
#include <cstddef>
#include <wx/checklst.h>
#include <wx/colour.h>
#include <wx/colourdata.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/button.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/rtti.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/bmpcbox.h>
#include <wx/statbmp.h>
#include <wx/popupwin.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/colordlg.h>

#include <vector>
#include <functional>
#include <wx/string.h>
#include "BitmapCache.hpp"
#include "Widgets/PopupWindow.hpp"

#ifdef __WXMSW__
void                msw_rescale_menu(wxMenu* menu);
#else
inline void         msw_rescale_menu(wxMenu* ) {}
#endif

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const wxBitmap& icon, wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true;}, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);
wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon = "", wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description,
    const std::string& icon = "",
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler);

wxMenuItem* append_menu_check_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent & event)> cb, wxEvtHandler* event_handler,
    std::function<bool()> const enable_condition = []() { return true; },
    std::function<bool()> const check_condition = []() { return true; }, wxWindow* parent = nullptr);

void enable_menu_item(wxUpdateUIEvent& evt, std::function<bool()> const cb_condition, wxMenuItem* item, wxWindow* win);

class wxDialog;

void    edit_tooltip(wxString& tooltip);
void    msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids);
int     em_unit(wxWindow* win);

wxBitmap create_menu_bitmap(const std::string& bmp_name);

#if 1
wxBitmap create_scaled_bitmap(const std::string& bmp_name, wxWindow *win = nullptr,
    const int px_cnt = 16, const bool grayscale = false,
    const std::string& new_color = std::string(),
    const bool menu_bitmap = false, const bool resize = false,
    const bool bitmap2 = false,
    const std::vector<std::string>& array_new_color = std::vector<std::string>());
wxBitmap create_scaled_bitmap2(const std::string& bmp_name_in, Slic3r::GUI::BitmapCache& cache, wxWindow* win = nullptr,
    const int px_cnt = 16, const bool grayscale = false, const bool resize = false,
    const std::vector<std::string>& array_new_color = std::vector<std::string>());
#else
wxBitmap create_scaled_bitmap(const std::string& bmp_name, wxWindow *win = nullptr,
    const int px_cnt = 16, const bool grayscale = false, const bool resize = false);
#endif

wxBitmap* get_default_extruder_color_icon(bool thin_icon = false);
std::vector<wxBitmap *> get_extruder_color_icons(bool thin_icon = false);
wxBitmap * get_extruder_color_icon(std::string color, std::string label, int icon_width, int icon_height);
wxBitmap * get_extruder_color_icon(std::vector<std::string> colors, bool is_gradient, std::string label, int icon_width, int icon_height,
                                   const std::vector<wxColour> *ramp = nullptr);
std::vector<std::vector<std::string>> read_color_pack(std::vector<std::string> color_pack);
wxColourData show_sys_picker_dialog(wxWindow *parent, const wxColourData &clr_data);

namespace Slic3r {
namespace GUI {
class BitmapComboBox;
}
}
void apply_extruder_selector(Slic3r::GUI::BitmapComboBox** ctrl,
                             wxWindow* parent,
                             const std::string& first_item = "",
                             wxPoint pos = wxDefaultPosition,
                             wxSize size = wxDefaultSize,
                             bool use_thin_icon = false);

class wxCheckListBoxComboPopup : public wxCheckListBox, public wxComboPopup
{
    static const unsigned int DefaultWidth;
    static const unsigned int DefaultHeight;

    wxString m_text;

    enum class OnCheckListBoxFunction{
        FreeToProceed,
        RefuseToProceed,
        WasRefusedLastTime
    } m_check_box_events_status = OnCheckListBoxFunction::FreeToProceed;


public:
    virtual bool Create(wxWindow* parent);
    virtual wxWindow* GetControl();
    virtual void SetStringValue(const wxString& value);
    virtual wxString GetStringValue() const;
    virtual wxSize GetAdjustedSize(int minWidth, int prefHeight, int maxHeight);

    virtual void OnKeyEvent(wxKeyEvent& evt);

    void OnCheckListBox(wxCommandEvent& evt);
    void OnListBoxSelection(wxCommandEvent& evt);
};


class wxDataViewTreeCtrlComboPopup: public wxDataViewTreeCtrl, public wxComboPopup
{
    static const unsigned int DefaultWidth;
    static const unsigned int DefaultHeight;
    static const unsigned int DefaultItemHeight;

    wxString	m_text;
    int			m_cnt_open_items{0};

public:
    virtual bool		Create(wxWindow* parent);
    virtual wxWindow*	GetControl() { return this; }
    virtual void		SetStringValue(const wxString& value) { m_text = value; }
    virtual wxString	GetStringValue() const { return m_text; }

    virtual void		OnKeyEvent(wxKeyEvent& evt);
    void				OnDataViewTreeCtrlSelection(wxCommandEvent& evt);
    void				SetItemsCnt(int cnt) { m_cnt_open_items = cnt; }
};


class ScalableBitmap
{
public:
    ScalableBitmap() {};
    ScalableBitmap( wxWindow *parent,
                    const std::string& icon_name = "",
                    const int px_cnt = 16,
                    const bool grayscale = false,
                    const bool resize = false,
                    const bool bitmap2 = false,
                    const std::vector<std::string>& new_color = std::vector<std::string>());

    ~ScalableBitmap() {}

    wxSize  GetBmpSize() const;
    static wxSize GetBmpSize(const wxBitmap &bmp);

    int     GetBmpWidth() const;
    int     GetBmpHeight() const;

    void                msw_rescale();

    const wxBitmap&     bmp() const { return m_bmp; }
    wxBitmap&           bmp()       { return m_bmp; }
    const std::string&  name() const{ return m_icon_name; }

    int                 px_cnt()const           {return m_px_cnt;}

private:
    wxWindow*       m_parent{ nullptr };
    wxBitmap        m_bmp = wxBitmap();
    std::string     m_icon_name = "";
    int             m_px_cnt {16};
    bool            m_grayscale{ false };
    bool            m_resize{ false };
};


class LockButton : public wxButton
{
public:
    LockButton(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~LockButton() {}

    void    OnButton(wxCommandEvent& event);

    bool    IsLocked() const                { return m_is_pushed; }
    void    SetLock(bool lock);

    void    enable()                        { m_disabled = false; }
    void    disable()                       { m_disabled = true;  }

    void    msw_rescale();

protected:
    void    update_button_bitmaps();

private:
    bool        m_is_pushed = false;
    bool        m_disabled = false;

    ScalableBitmap    m_bmp_lock_closed;
    ScalableBitmap    m_bmp_lock_closed_f;
    ScalableBitmap    m_bmp_lock_open;
    ScalableBitmap    m_bmp_lock_open_f;
};


class ScalableButton : public wxButton
{
public:
    ScalableButton(){}
    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const std::string&  icon_name = "",
        const wxString&     label = wxEmptyString,
        const wxSize&       size = wxDefaultSize,
        const wxPoint&      pos = wxDefaultPosition,
        long                style = wxBU_EXACTFIT | wxNO_BORDER,
        bool                use_default_disabled_bitmap = false,
        int                 bmp_px_cnt = 16);

    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const ScalableBitmap&  bitmap,
        const wxString&     label = wxEmptyString,
        long                style = wxBU_EXACTFIT | wxNO_BORDER);

    ~ScalableButton() {}

    void SetBitmap_(const ScalableBitmap& bmp);
    bool SetBitmap_(const std::string& bmp_name);
    void SetBitmapDisabled_(const ScalableBitmap &bmp);
    int  GetBitmapHeight();
    void UseDefaultBitmapDisabled();

    void    msw_rescale();
    void    UpdateDarkUI() { msw_rescale(); };

private:
    wxWindow*       m_parent { nullptr };
    std::string     m_current_icon_name;
    std::string     m_disabled_icon_name;
    int             m_width {-1};
    int             m_height{-1};

    bool            m_use_default_disabled_bitmap {false};

    int             m_px_cnt{ 16 };
    bool            m_has_border {false};
};


class MenuWithSeparators : public wxMenu
{
public:
    MenuWithSeparators(const wxString& title, long style = 0)
        : wxMenu(title, style) {}

    MenuWithSeparators(long style = 0)
        : wxMenu(style) {}

    ~MenuWithSeparators() {}

    void DestroySeparators();
    void SetFirstSeparator();
    void SetSecondSeparator();

private:
    wxMenuItem* m_separator_frst { nullptr };
    wxMenuItem* m_separator_scnd { nullptr };
};


class BlinkingBitmap : public wxStaticBitmap
{
public:
    BlinkingBitmap() {};
    BlinkingBitmap(wxWindow* parent, const std::string& icon_name = "blank_16");

    ~BlinkingBitmap() {}

    void    msw_rescale();
    void    invalidate();
    void    activate();
    void    blink();

    const wxBitmap& get_bmp() const { return bmp.bmp(); }

private:
    ScalableBitmap  bmp;
    bool            show {false};
};


class ImageTransientPopup : public PopupWindow
{
    public:
    ImageTransientPopup( wxWindow *parent, bool scrolled, wxBitmap bmp);
    virtual ~ImageTransientPopup();

    void SetImage(wxBitmap bmp);

    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    virtual bool Show( bool show = true ) wxOVERRIDE;

private:

    wxScrolledWindow *m_panel;
    wxStaticBitmap* m_image;

private:
    void OnMouse( wxMouseEvent &event );
    void OnSize( wxSizeEvent &event );
    void OnSetFocus( wxFocusEvent &event );
    void OnKillFocus( wxFocusEvent &event );

private:
    wxDECLARE_ABSTRACT_CLASS(ImageTransientPopup);
    wxDECLARE_EVENT_TABLE();
};


#endif
