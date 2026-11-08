#ifndef slic3r_BitmapComboBox_hpp_
#define slic3r_BitmapComboBox_hpp_

#include <cstddef>
#include <wx/bmpcbox.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

#include "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

class BitmapComboBox : public wxBitmapComboBox
{
public:
BitmapComboBox(wxWindow* parent,
    wxWindowID id = wxID_ANY,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    int n = 0,
    const wxString choices[] = NULL,
    long style = 0);
~BitmapComboBox();

#ifdef _WIN32
    int Append(const wxString& item);
#endif
    int Append(const wxString& item, const wxBitmap& bitmap)
    {
        return wxBitmapComboBox::Append(item, bitmap);
    }

protected:

#ifdef __APPLE__
bool OnAddBitmap(const wxBitmapBundle& bitmap) override;
void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const override;
#endif

#ifdef _WIN32
bool MSWOnDraw(WXDRAWITEMSTRUCT* item) override;
void DrawBackground_(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const;
public:
void Rescale();
#endif

};

    }}
#endif
