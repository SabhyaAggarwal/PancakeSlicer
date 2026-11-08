#pragma once

#include <functional>
#include <string>
#include <utility>

#include <wx/bookctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include "Lazy.hpp"
#include <wx/notebook.h>

namespace Slic3r { namespace GUI {

void apply_dark_ui_to_lazy_panel(wxWindow* panel);

template <class Panel>
class LazyPage : public wxPanel, public Lazy<Panel>
{
public:
    using Factory = std::function<Panel*(wxWindow* parent)>;

    LazyPage(wxWindow* parent, std::string name, int order, Factory make = [](wxWindow* parent) { return new Panel(parent); })
        : wxPanel(parent), Lazy<Panel>(std::move(name), order, [this, make = std::move(make)] {
            Panel* panel = make(this);
            GetSizer()->Add(panel, 1, wxEXPAND);
            if (!IsShown())
                panel->Hide();
            return panel;
        })
    {
        SetSizer(new wxBoxSizer(wxVERTICAL));
        Hide();
        this->when_built([this](Panel& panel) {
            apply_dark_ui_to_lazy_panel(&panel);
            Layout();
        });
    }

    bool in_book() const
    {
        auto* book = dynamic_cast<wxBookCtrlBase*>(GetParent());
        return book != nullptr && book->FindPage(this) != wxNOT_FOUND;
    }

    bool pending() const override { return in_book() && Lazy<Panel>::pending(); }

    bool Show(bool show = true) override
    {
        if (show && wxGetTopLevelParent(this)->IsShown())
            this->ensure();
        const bool changed = wxPanel::Show(show);
        if (show) {
            if (this->built() || wxGetTopLevelParent(this)->IsShown()) {
                if (Panel* panel = this->ensure())
                    panel->Show(true);
                Layout();
            }
        } else if (Panel* panel = this->get()) {
            panel->Show(false);
        }
        return changed;
    }
};

}}
