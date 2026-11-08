#include "BitmapComboBox.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/bmpcbox.h>
#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>
#include <wx/odcombo.h>
#include <wx/listbook.h>
#include <wx/window.h>

#ifdef __WINDOWS__
#include <wx/msw/dcclient.h>
#include <wx/msw/private.h>
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif
#endif

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <glib-2.0/glib-object.h>
    #include <pango-1.0/pango/pango-layout.h>
    #include <gtk/gtk.h>
#endif

#define BORDER_W 10

namespace Slic3r {
namespace GUI {

BitmapComboBox::BitmapComboBox(wxWindow* parent,
                                wxWindowID id ,
                                const wxString& value ,
                                const wxPoint& pos ,
                                const wxSize& size ,
                                int n ,
                                const wxString choices[] ,
                                    long style ) :
    wxBitmapComboBox(parent, id, value, pos, size, n, choices, style)
{
    SetFont(Slic3r::GUI::wxGetApp().normal_font());
#ifdef _WIN32
    EnableTextChangedEvents(false);
    wxGetApp().UpdateDarkUI(this);
    if (!HasFlag(wxCB_READONLY))
        wxTextEntry::SetMargins(0,0);
#endif
}

BitmapComboBox::~BitmapComboBox()
{
}

#ifdef __APPLE__
bool BitmapComboBox::OnAddBitmap(const wxBitmapBundle& bundle)
{
    wxBitmap bitmap = bundle.GetBitmap(bundle.GetDefaultSize());
    if (bitmap.IsOk())
    {
        int width = (int)bitmap.GetScaledWidth();
        int height = (int)bitmap.GetScaledHeight();

        if (m_usedImgSize.x < 0)
        {
            m_usedImgSize.x = width;
            m_usedImgSize.y = height;

            wxWindow* ctrl = GetControl();
            ctrl->InvalidateBestSize();
            wxSize newSz = ctrl->GetBestSize();
            wxSize sz = ctrl->GetSize();
            if (newSz.y > sz.y)
                ctrl->SetSize(sz.x, newSz.y);
            else
                DetermineIndent();
        }

        wxCHECK_MSG(width == m_usedImgSize.x && height == m_usedImgSize.y,
            false,
            "you can only add images of same size");

        return true;
    }

    return false;
}

void BitmapComboBox::OnDrawItem(wxDC& dc,
    const wxRect& rect,
    int item,
    int flags) const
{
    const wxBitmapBundle& bundle = m_bitmapbundles[item];
    wxBitmap bmp = bundle.GetBitmap(bundle.GetDefaultSize());
    if (bmp.IsOk())
    {
        wxCoord w = bmp.GetScaledWidth();
        wxCoord h = bmp.GetScaledHeight();

        const int imgSpacingLeft = 4;

        dc.DrawBitmap(bmp,
            rect.x + (m_usedImgSize.x - w) / 2 + imgSpacingLeft,
            rect.y + (rect.height - h) / 2,
            true);
    }

    wxString text = GetString(item);
    if (!text.empty())
        dc.DrawText(text,
            rect.x + m_imgAreaWidth + 1,
            rect.y + (rect.height - dc.GetCharHeight()) / 2);
}
#endif


#ifdef _WIN32

int BitmapComboBox::Append(const wxString& item)
{

    wxBitmap bitmap(1, int(1.6 * wxGetApp().em_unit() + 1));
    {
        bitmap.UnShare();
        bitmap.GetGDIImageData()->m_width = 0;
    }

    OnAddBitmap(bitmap);
    const int n = wxComboBox::Append(item);
    if (n != wxNOT_FOUND)
        DoSetItemBitmap(n, bitmap);
    return n;
}

enum OwnerDrawnComboBoxPaintingFlags
{
    ODCB_PAINTING_DISABLED = 0x0004,
};

bool BitmapComboBox::MSWOnDraw(WXDRAWITEMSTRUCT* item)
{
    LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)item;
    int pos = lpDrawItem->itemID;

    if (pos == -1)
        return false;

    int flags = 0;
    if (lpDrawItem->itemState & ODS_COMBOBOXEDIT)
        flags |= wxODCB_PAINTING_CONTROL;
    if (lpDrawItem->itemState & ODS_SELECTED)
        flags |= wxODCB_PAINTING_SELECTED;
    if (lpDrawItem->itemState & ODS_DISABLED)
        flags |= ODCB_PAINTING_DISABLED;

    wxPaintDCEx dc(this, lpDrawItem->hDC);
    wxRect rect = wxRectFromRECT(lpDrawItem->rcItem);

    DrawBackground_(dc, rect, pos, flags);

    wxString text;

    if (flags & wxODCB_PAINTING_CONTROL)
    {

        pos = GetSelection();
        if (pos < 0)
            return true;

        text = GetValue();
    }
    else
    {
        text = GetString(pos);
    }

    wxBitmapComboBoxBase::DrawItem(dc, rect, pos, text, flags);

    return true;
}

void BitmapComboBox::DrawBackground_(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const
{
    if (flags & wxODCB_PAINTING_SELECTED)
    {
        const int vSizeDec = 0;

        dc.SetTextForeground(wxGetApp().get_label_highlight_clr());

        wxColour selCol = wxGetApp().get_highlight_default_clr();
        dc.SetPen(selCol);
        dc.SetBrush(selCol);
        dc.DrawRectangle(rect.x,
            rect.y + vSizeDec,
            rect.width,
            rect.height - (vSizeDec * 2));
    }
    else
    {
        dc.SetTextForeground(flags & ODCB_PAINTING_DISABLED ? wxColour(108,108,108) : wxGetApp().get_label_clr_default());

        wxColour selCol = flags & ODCB_PAINTING_DISABLED ? 
            wxGetApp().get_highlight_default_clr() :
            wxGetApp().get_window_default_clr();
        dc.SetPen(selCol);
        dc.SetBrush(selCol);
        dc.DrawRectangle(rect);
    }
}

void BitmapComboBox::Rescale()
{
    const wxString selection = this->GetValue();
    std::vector<wxString> items;
    for (size_t i = 0; i < GetCount(); i++)
        items.push_back(GetString(i));

    this->Clear();
    for (const wxString& item : items)
        Append(item);
    this->SetValue(selection);
}
#endif

}}
