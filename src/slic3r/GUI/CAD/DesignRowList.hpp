#ifndef slic3r_DesignRowList_hpp_
#define slic3r_DesignRowList_hpp_

#include <wx/vlbox.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/types.h>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "slic3r/GUI/wxExtensions.hpp"

class wxTextCtrl;

namespace Slic3r { namespace GUI {

class DesignRowList : public wxVListBox
{
public:
    struct Action {
        int         id;
        std::string icon;
        wxString    tip;
    };
    struct Row {
        std::string         icon;
        wxString            label;
        wxString            edit_text;
        wxColour            colour;
        std::vector<Action> actions;
    };

    static constexpr const char* hover_chip = "#D4D4D4";

    DesignRowList(wxWindow* parent, int max_visible, bool selectable = true, long style = wxBORDER_SIMPLE);

    void set_rows(std::vector<Row> rows);
    int  selection() const { return GetSelection(); }
    void select(int row);
    void unselect() { select(wxNOT_FOUND); }
    void begin_rename(int row);
    void Rescale();

    std::function<void()>                              on_select;
    std::function<void()>                              on_activate;
    std::function<void(int row, int id)>              on_action;
    std::function<void(int row, const wxPoint& screen)> on_menu;
    std::function<void(int row, const wxString& name)> on_rename;

    bool AcceptsFocus() const override { return m_selectable && wxVListBox::AcceptsFocus(); }

protected:
    void    OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override;
    void    OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const override;
    wxCoord OnMeasureItem(size_t n) const override;

private:
    struct Hit {
        int row{wxNOT_FOUND};
        int cell{-1};
        bool operator==(const Hit& o) const { return row == o.row && cell == o.cell; }
        bool operator!=(const Hit& o) const { return !(*this == o); }
    };

    Hit    hit_test(const wxPoint& pt) const;
    wxRect cell_rect(const wxRect& row, size_t count, size_t i) const;
    wxRect label_rect(const wxRect& row, const Row& r) const;
    const ScalableBitmap* icon(const std::string& name) const;
    void   load_icons();
    void   set_hover(const Hit& h);
    void   open_editor(int row);
    void   end_rename(bool commit);
    void   measure_row();
    void   fit_rows();

    std::vector<Row>                      m_rows;
    std::map<std::string, ScalableBitmap> m_icons;
    int                                   m_max_visible;
    bool                                  m_selectable;
    int                                   m_row_h{0};
    Hit                                   m_hover;
    Hit                                   m_pressed;
    wxTextCtrl*                           m_editor{nullptr};
    int                                   m_edit_row{wxNOT_FOUND};
};

}}

#endif
