#ifndef slic3r_GUI_KBShortcutsDialog_hpp_
#define slic3r_GUI_KBShortcutsDialog_hpp_

#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include "slic3r/GUI/KeyChord.hpp"
#include <wx/event.h>
#include <wx/colour.h>
#include <wx/wx.h>
#include <map>
#include <variant>
#include <vector>
#include <cstdint>

#include "GUI_Utils.hpp"
#include <wx/simplebook.h>

class ScalableButton;
class wxKeyEvent;
class wxPanel;
class wxSimplebook;
class wxStaticText;
class wxWindow;
namespace Slic3r::GUI { enum class Shortcut : uint8_t; }
namespace Slic3r::GUI { enum class ShortcutContext : uint8_t; }
namespace Slic3r::GUI { enum class ShortcutSection : uint8_t; }

class Button;
class Label;
class TabCtrl;

namespace Slic3r {
namespace GUI {

class KBShortcutsDialog : public DPIDialog
{
    struct FixedKey
    {
        std::vector<wxString> keys;
        const char*           description;
    };
    struct MouseAction
    {
        wxString    button;
        const char* preference;
    };
    struct Row
    {
        std::variant<Shortcut, FixedKey, MouseAction> content;
        ShortcutSection                               section;
    };
    struct Page
    {
        wxString         title;
        wxString         caption;
        ShortcutContext  context;
        std::vector<Row> rows;
    };
    struct EditableRow
    {
        Shortcut        shortcut;
        wxStaticText*   description;
        wxStaticText*   modifiers;
        wxStaticText*   key;
        ScalableButton* reset;
    };
    struct PreferenceRow
    {
        const char*   preference;
        wxStaticText* description;
    };

    std::vector<Page>          m_pages;
    std::vector<EditableRow>   m_editable_rows;
    std::vector<PreferenceRow> m_preference_rows;
    wxSize                     m_edit_size;
    int                        m_buttons_width  = 0;
    int                        m_row_text_width = 0;
    int                        m_key_slot       = 0;

    TabCtrl*      m_tabs;
    wxSimplebook* m_simplebook;

public:
    KBShortcutsDialog(wxWindow* parent, ShortcutContext page);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void fill_pages();
    wxPanel* create_page(wxWindow* parent, const Page& page);
    void edit_shortcut(Shortcut shortcut);
    void reset_shortcut(Shortcut shortcut);
    bool take_chord_from(Shortcut shortcut, const std::vector<Shortcut>& conflicts, const wxString& question);
    void apply_bindings();
    int  set_chord_labels(wxStaticText* modifiers, wxStaticText* key, std::vector<wxString> parts);
    void open_mouse_preferences(const char* preference);
};

class ShortcutCaptureDialog : public DPIDialog
{
public:
    ShortcutCaptureDialog(wxWindow* parent, Shortcut shortcut);

    const KeyChord&              chord() const { return m_chord; }
    const std::vector<Shortcut>& conflicts() const { return m_conflicts; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void on_key(wxKeyEvent& evt);
    void on_char(wxKeyEvent& evt);
    void record(const KeyChord& chord);

    Shortcut              m_shortcut;
    KeyChord              m_chord;
    std::vector<Shortcut> m_conflicts;
    wxStaticText*         m_chord_label;
    wxString              m_hint;
    wxString              m_rejection;
    Label*                m_status;
    wxColour              m_status_colour;
    Button*               m_ok;
};

}
}

#endif
