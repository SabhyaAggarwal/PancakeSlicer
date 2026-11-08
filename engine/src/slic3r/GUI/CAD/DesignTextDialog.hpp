#ifndef slic3r_GUI_DesignTextDialog_hpp_
#define slic3r_GUI_DesignTextDialog_hpp_

#include "slic3r/GUI/GUI_Utils.hpp"
#include "libslic3r/CAD/SketchImport.hpp"

#include <wx/font.h>
#include <wx/string.h>

#include <functional>
#include <memory>
#include <string>

class wxStaticText;
class TextInput;
class ComboBox;
class CheckBox;

namespace Slic3r {
namespace Emboss { struct FontFile; }
namespace GUI {

class DesignTextDialog : public DPIDialog
{
public:
    struct Spec {
        wxString    text;
        std::string font;
        double      height{10.0};
    };
    DesignTextDialog(wxWindow* parent, const Spec* initial = nullptr);

    const ImportRegions& regions() const { return m_regions; }
    wxString             text() const;
    Spec                 spec() const;

    std::function<void()> on_change;
    std::function<void()> on_accept;
    std::function<void()> on_cancel;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

private:
    void     load_font();
    void     update_preview();
    wxFont   current_font() const;
    void     draw_preview(wxWindow* canvas);
    void     accept();
    void     cancel();
    bool     m_done{false};

    double   height_mm() const;

    ::TextInput*      m_text{nullptr};
    ::ComboBox*       m_face{nullptr};
    ::CheckBox*       m_bold{nullptr};
    ::CheckBox*       m_italic{nullptr};
    ::TextInput*      m_height{nullptr};
    double            m_last_height{10.0};
    wxWindow*         m_preview{nullptr};
    wxStaticText*     m_size{nullptr};
    wxWindow*         m_ok{nullptr};

    std::shared_ptr<const Emboss::FontFile> m_font_file;
    ImportRegions                           m_regions;
};

}
}

#endif
