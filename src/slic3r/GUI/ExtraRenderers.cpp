#include "ExtraRenderers.hpp"
#include "wxExtensions.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "BitmapComboBox.hpp"
#include "Plater.hpp"
#include "Widgets/ComboBox.hpp"

#include "libslic3r/Technologies.hpp"
#include <wx/dataview.h>
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include <vector>
#include <cstddef>
#include <cstdlib>
#include <wx/dc.h>
#include <wx/setup.h>
#include <wx/rtti.h>
#include <wx/object.h>
#include <wx/variant.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/event.h>
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/DropDown.hpp"
#include <wx/dvrenderers.h>
#ifdef wxHAS_GENERIC_DATAVIEWCTRL
#include "wx/generic/private/markuptext.h"
#include "wx/generic/private/rowheightcache.h"
#include "wx/generic/private/widthcalc.h"
#endif
#if wxUSE_ACCESSIBILITY
#include "wx/private/markupparser.h"
#endif

wxIMPLEMENT_DYNAMIC_CLASS(DataViewBitmapText, wxObject)

IMPLEMENT_VARIANT_OBJECT(DataViewBitmapText)

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
BitmapTextRenderer::BitmapTextRenderer(wxDataViewCellMode mode , 
                                                 int align ): 
wxDataViewRenderer(wxT("PrusaDataViewBitmapText"), mode, align)
{
    SetMode(mode);
    SetAlignment(align);
}
#endif

BitmapTextRenderer::~BitmapTextRenderer()
{
#ifdef SUPPORTS_MARKUP
    #ifdef wxHAS_GENERIC_DATAVIEWCTRL
    delete m_markupText;
    #endif
#endif
}

void BitmapTextRenderer::EnableMarkup(bool enable)
{
#ifdef SUPPORTS_MARKUP
#ifdef wxHAS_GENERIC_DATAVIEWCTRL
    if (enable) {
        if (!m_markupText)
            m_markupText = new wxItemMarkupText(wxString());
    }
    else {
        if (m_markupText) {
            delete m_markupText;
            m_markupText = nullptr;
        }
    }
#else
    is_markupText = enable;
#endif
#endif
}

bool BitmapTextRenderer::SetValue(const wxVariant &value)
{
    m_value << value;

#ifdef SUPPORTS_MARKUP
#ifdef wxHAS_GENERIC_DATAVIEWCTRL
    if (m_markupText)
        m_markupText->SetMarkup(m_value.GetText());
#endif
#endif

    return true;
}

bool BitmapTextRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
    return false;
}

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
wxString BitmapTextRenderer::GetAccessibleDescription() const
{
#ifdef SUPPORTS_MARKUP
    if (m_markupText)
        return wxMarkupParser::Strip(m_text);
#endif

    return m_value.GetText();
}
#endif

bool BitmapTextRenderer::Render(wxRect rect, wxDC *dc, int state)
{
    int xoffset = 0;

    const wxBitmap& icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
#ifdef __APPLE__
        wxSize icon_sz = icon.GetScaledSize();
#else
        wxSize icon_sz = icon.GetSize();
#endif
        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon_sz.y) / 2);
        xoffset = icon_sz.x + 4;
    }

#if defined(SUPPORTS_MARKUP) && defined(wxHAS_GENERIC_DATAVIEWCTRL)
    if (m_markupText)
    {
        rect.x += xoffset;
        m_markupText->Render(GetView(), *dc, rect, 0, GetEllipsizeMode());
    }
    else
#endif
#ifdef _WIN32 
        RenderText(m_value.GetText(), xoffset, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 :state);
#else
        RenderText(m_value.GetText(), xoffset, rect, dc, state);
#endif

    return true;
}

wxSize BitmapTextRenderer::GetSize() const
{
    if (!m_value.GetText().empty())
    {
        wxSize size;
#if defined(SUPPORTS_MARKUP) && defined(wxHAS_GENERIC_DATAVIEWCTRL)
        if (m_markupText)
        {
            wxDataViewCtrl* const view = GetView();
            wxClientDC dc(view);
            if (GetAttr().HasFont())
                dc.SetFont(GetAttr().GetEffectiveFont(view->GetFont()));

            size = m_markupText->Measure(dc);

            int lines = m_value.GetText().Freq('\n') + 1;
            size.SetHeight(size.GetHeight() * lines);
        }
        else
#endif
        {
            size = GetTextExtent(m_value.GetText());
            size.x = size.x * 9 / 8;
        }

        if (m_value.GetBitmap().IsOk())
            size.x += m_value.GetBitmap().GetWidth() + 4;
        return size;
    }
    return wxSize(80, 20);
}


wxWindow* BitmapTextRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    if (can_create_editor_ctrl && !can_create_editor_ctrl())
        return nullptr;

    DataViewBitmapText data;
    data << value;

    m_was_unusable_symbol = false;

    wxPoint position = labelRect.GetPosition();
    if (data.GetBitmap().IsOk()) {
        const int bmp_width = data.GetBitmap().GetWidth();
        position.x += bmp_width;
        labelRect.SetWidth(labelRect.GetWidth() - bmp_width);
    }

#ifdef __WXMSW__
    if (auto children = parent->GetChildren(); children.GetCount() > 0)
        for (auto child : children)
            if (dynamic_cast<wxTextCtrl*>(child)) {
                parent->RemoveChild(child);
                child->Destroy();
                break;
            }
#endif

    wxTextCtrl* text_editor = new wxTextCtrl(parent, wxID_ANY, data.GetText(),
                                             position, labelRect.GetSize(), wxTE_PROCESS_ENTER);
    text_editor->SetInsertionPointEnd();
    text_editor->SelectAll();
    text_editor->SetBackgroundColour(parent->GetBackgroundColour());
    text_editor->SetForegroundColour(parent->GetForegroundColour());

    return text_editor;
}

bool BitmapTextRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
    wxTextCtrl* text_editor = wxDynamicCast(ctrl, wxTextCtrl);
    auto item = GetView()->GetModel()->GetParent(m_item);
    if (!text_editor || (item.IsOk() && text_editor->GetValue().IsEmpty()))
        return false;

    m_was_unusable_symbol = Slic3r::GUI::Plater::has_illegal_filename_characters(text_editor->GetValue());
    if (m_was_unusable_symbol)
        return false;

    wxVariant valueOld;
    GetView()->GetModel()->GetValue(valueOld, m_item, Slic3r::GUI::ColumnNumber::colName); 
    
    DataViewBitmapText bmpText;
    bmpText << valueOld;

    bmpText.SetText(text_editor->GetValue());

    value << bmpText;
    return true;
}

bool BitmapChoiceRenderer::SetValue(const wxVariant& value)
{
    m_value << value;
    return true;
}

bool BitmapChoiceRenderer::GetValue(wxVariant& value) const 
{
    value << m_value;
    return true;
}

bool BitmapChoiceRenderer::Render(wxRect rect, wxDC* dc, int state)
{
    const wxBitmap& icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon.GetHeight()) / 2);

        if (rect.height == 0)
          rect.height = icon.GetHeight();
    }

#ifdef _WIN32
#else
#endif

    return true;
}

wxSize BitmapChoiceRenderer::GetSize() const
{
    wxSize sz;

    if (m_value.GetBitmap().IsOk()) {
        sz.x += m_value.GetBitmap().GetWidth() + 4;
        sz.y = m_value.GetBitmap().GetHeight() + 4;
    }

    return sz;
}

wxWindow* BitmapChoiceRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    if (can_create_editor_ctrl && !can_create_editor_ctrl())
        return nullptr;

    std::vector<wxBitmap*> icons = get_extruder_color_icons();
    if (icons.empty())
        return nullptr;

    DataViewBitmapText data;
    data << value;

    ::ComboBox *c_editor = new ::ComboBox(parent, wxID_ANY, wxEmptyString,
        labelRect.GetTopLeft(), wxSize(labelRect.GetWidth(), -1),
        0, nullptr, wxCB_READONLY | CB_NO_DROP_ICON | CB_NO_TEXT);
    c_editor->GetDropDown().SetUseContentWidth(true);
       
    if (has_default_extruder && has_default_extruder())
        c_editor->Append(_L("default"), *get_default_extruder_color_icon());

    for (size_t i = 0; i < icons.size(); i++)
        c_editor->Append(wxString::Format("%d", i+1), *icons[i]);

    if (has_default_extruder && has_default_extruder())
        c_editor->SetSelection(atoi(data.GetText().c_str()));
    else
        c_editor->SetSelection(atoi(data.GetText().c_str()) - 1);

    c_editor->Bind(wxEVT_SET_FOCUS, [c_editor](wxFocusEvent& evt) {
#ifdef __WXGTK__
        c_editor->CallAfter([c_editor]() {
            if (c_editor->IsShownOnScreen())
                c_editor->ForceDropdownOpen();
        });
#else
        c_editor->ForceDropdownOpen();
#endif
        evt.Skip(); 
    });

    c_editor->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        evt.StopPropagation(); 
        this->FinishEditing();
    });

    return c_editor;
}

bool BitmapChoiceRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
    auto* c = dynamic_cast<::ComboBox*>(ctrl);
    if (!c)
        return false;

    int selection = c->GetSelection();
    if (selection < 0)
        return false;
   
    DataViewBitmapText bmpText;

    bmpText.SetText(c->GetString(selection));
    bmpText.SetBitmap(c->GetItemBitmap(selection));

    value << bmpText;
    return true;
}

bool TextRenderer::SetValue(const wxVariant& value)
{
    m_value = value.GetString();
    return true;
}

bool TextRenderer::GetValue(wxVariant& value) const
{
    return false;
}

bool TextRenderer::Render(wxRect rect, wxDC* dc, int state)
{
#ifdef _WIN32
    RenderText(m_value, 0, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 : state);
#else
    RenderText(m_value, 0, rect, dc, state);
#endif

    return true;
}

wxSize TextRenderer::GetSize() const
{
    return GetTextExtent(m_value);
}
