#ifndef slic3r_TextureProjectorFrame_hpp_
#define slic3r_TextureProjectorFrame_hpp_

#include <vector>

#include <wx/bitmap.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/gdicmn.h>

class wxPaintEvent;
class wxWindow;

namespace Slic3r { namespace GUI {

class TextureProjectorFrame : public wxFrame
{
public:
    explicit TextureProjectorFrame(wxWindow *parent);

    void set_texture(const std::vector<unsigned char> &grayscale_pixels, int width, int height);

    void set_opacity(int alpha);
    int  opacity() const { return m_alpha; }

    wxRect client_rect_on_screen() const;

private:
    wxBitmap m_bitmap;
    int      m_alpha = 140;

    void on_paint(wxPaintEvent &);
};

}}

#endif
