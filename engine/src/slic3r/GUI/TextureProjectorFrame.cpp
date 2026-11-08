#include "TextureProjectorFrame.hpp"

#include <algorithm>

#include <vector>
#include <cstddef>
#include <wx/dcclient.h>
#include <wx/gdicmn.h>
#include <wx/frame.h>
#include <wx/event.h>
#include <wx/image.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/types.h>

#include "slic3r/GUI/I18N.hpp"

class wxWindow;

namespace Slic3r { namespace GUI {

TextureProjectorFrame::TextureProjectorFrame(wxWindow *parent)
    : wxFrame(parent, wxID_ANY, _L("Projection frame - drag over the model, then Apply"), wxDefaultPosition,
              wxSize(360, 360),
              wxCAPTION | wxRESIZE_BORDER | wxCLOSE_BOX | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &TextureProjectorFrame::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) { Refresh(); evt.Skip(); });
    SetTransparent(wxByte(m_alpha));

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt) {
        if (evt.CanVeto()) {
            evt.Veto();
            Hide();
        } else
            evt.Skip();
    });
}

void TextureProjectorFrame::set_texture(const std::vector<unsigned char> &gray, int width, int height)
{
    if (width <= 0 || height <= 0 || gray.size() < size_t(width) * size_t(height)) {
        m_bitmap = wxBitmap();
        Refresh();
        return;
    }

    wxImage img(width, height);
    unsigned char *dst = img.GetData();
    for (size_t i = 0, n = size_t(width) * size_t(height); i < n; ++i) {
        const unsigned char v = gray[i];
        dst[i * 3 + 0] = v;
        dst[i * 3 + 1] = v;
        dst[i * 3 + 2] = v;
    }
    m_bitmap = wxBitmap(img);
    Refresh();
}

void TextureProjectorFrame::set_opacity(int alpha)
{
    m_alpha = std::clamp(alpha, 20, 255);
    SetTransparent(wxByte(m_alpha));
    Refresh();
}

wxRect TextureProjectorFrame::client_rect_on_screen() const
{
    const wxSize sz = GetClientSize();
    return wxRect(ClientToScreen(wxPoint(0, 0)), sz);
}

void TextureProjectorFrame::on_paint(wxPaintEvent &)
{
    wxPaintDC    dc(this);
    const wxSize sz = GetClientSize();
    if (sz.x <= 0 || sz.y <= 0)
        return;

    dc.SetBackground(wxBrush(wxColour(20, 20, 20)));
    dc.Clear();

    if (m_bitmap.IsOk()) {
        wxImage scaled = m_bitmap.ConvertToImage().Scale(sz.x, sz.y, wxIMAGE_QUALITY_NORMAL);
        dc.DrawBitmap(wxBitmap(scaled), 0, 0, false);
    }

    dc.SetPen(wxPen(wxColour(0, 200, 180), 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, 0, sz.x, sz.y);
}

}}
