#include "UpdateDialogs.hpp"

#include <cstring>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <string>
#include "libslic3r/Semver.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include <wx/hyperlink.h>
#include <vector>
#include "slic3r/GUI/GUI_Utils.hpp"
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include "slic3r/GUI/Widgets/Label.hpp"
#include <wx/scrolwin.h>
#include "slic3r/GUI/Widgets/Button.hpp"
#include <unordered_map>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/event.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>
#include <wx/string.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "ConfigWizard.hpp"
#include "wxExtensions.hpp"
#include "MainFrame.hpp"

namespace Slic3r {
namespace GUI {


static const char* URL_CHANGELOG = "%1%";
static const char* URL_DOWNLOAD = "%1%";
static const char* URL_DEV = "%1%";

static const std::string CONFIG_UPDATE_WIKI_URL("");


MsgUpdateSlic3r::MsgUpdateSlic3r(const Semver &ver_current, const Semver &ver_online)
    : MsgDialog(nullptr, wxString::Format(_(L("%s Update")), SLIC3R_APP_FULL_NAME), _(L("A new version is available")))
{

	finalize();
}

MsgUpdateSlic3r::~MsgUpdateSlic3r() {}

void MsgUpdateSlic3r::on_hyperlink(wxHyperlinkEvent& evt)
{
	wxGetApp().open_browser_with_warning_dialog(evt.GetURL());
}

bool MsgUpdateSlic3r::disable_version_check() const
{
	return true;
}

MsgUpdateConfig::MsgUpdateConfig(const std::vector<Update> &updates, bool force_before_wizard )
    : DPIDialog(wxGetApp().mainframe, wxID_ANY, _L("Configuration update"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
{
	auto  title = force_before_wizard ? _L("Configuration update") : _L("Configuration update");
	SetTitle(title);

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(38));

    auto sm    = create_scaled_bitmap("OrcaSlicer", nullptr, 70);
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));

    m_sizer_body->Add(brand, 0, wxALL, 0);

    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(25));

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);


    auto m_text_up_info = new wxStaticText(this, wxID_ANY, _L("A new configuration package is available. Do you want to install it?"), wxDefaultPosition, wxDefaultSize, 0);
    m_text_up_info->SetFont(::Label::Head_14);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_text_up_info->Wrap(-1);
    m_sizer_right->Add(m_text_up_info, 0, 0, 0);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    auto m_scrollwindw_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)),wxVSCROLL);
    m_scrollwindw_release_note->SetScrollRate(0, FromDIP(20));
    m_scrollwindw_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_scrollwindw_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));
    m_scrollwindw_release_note->SetWindowStyle(wxVSCROLL);

	auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    sizer_button->Add(0, 0, 1, wxEXPAND, 5);
  
	auto m_butto_ok = new Button(this, _L("OK"));
    m_butto_ok->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);

    auto m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetStyle(ButtonStyle::Regular, ButtonType::Choice);

    sizer_button->Add(m_butto_ok, 0, wxALL, 5);
    sizer_button->Add(m_button_cancel, 0, wxALL, 5);

	m_sizer_right->Add(m_scrollwindw_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_right->Add(sizer_button, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(30));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

	wxBoxSizer *content_sizer             = new wxBoxSizer(wxVERTICAL);

   



	const auto lang_code = wxGetApp().current_language_code_safe().ToStdString();

    auto *versions = new wxBoxSizer(wxVERTICAL);
    wxStaticText *changelog_textctrl = new wxStaticText(m_scrollwindw_release_note, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(560), -1));


    for (const auto &update : updates) {
        auto *flex = new wxFlexGridSizer(2, 0, FromDIP(15));

        auto *text_vendor = new wxStaticText(m_scrollwindw_release_note, wxID_ANY, update.vendor);
        text_vendor->SetFont(::Label::Body_13);
        flex->Add(text_vendor);
        flex->Add(new wxStaticText(m_scrollwindw_release_note, wxID_ANY, update.version.to_string()));

        if (!update.comment.empty()) {
            flex->Add(new wxStaticText(m_scrollwindw_release_note, wxID_ANY, _(L("Description:"))), 0, wxALIGN_RIGHT);
            auto *update_comment = new Label(m_scrollwindw_release_note,std::string(""));
            update_comment->SetLabel(from_u8(update.comment));
            update_comment->SetMaxSize(wxSize(FromDIP(545), -1));
            update_comment->SetMinSize(wxSize(FromDIP(545), -1));
            update_comment->Wrap(FromDIP(450));
            flex->Add(update_comment);
        }

        versions->Add(flex);

		

			changelog_textctrl->SetLabel(changelog_textctrl->GetLabel() + wxString::Format("%s\n", from_u8(update.change_log)));
    }

	content_sizer->Add(versions);

	

    if (changelog_textctrl) 
		content_sizer->Add(changelog_textctrl, 1, wxEXPAND | wxTOP, FromDIP(30));


	m_butto_ok->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) { EndModal(wxID_OK); });
	m_button_cancel->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) { EndModal(wxID_CLOSE); });


    m_scrollwindw_release_note->SetSizer(content_sizer);
    m_scrollwindw_release_note->Layout();


    SetSizerAndFit(m_sizer_main);
    Layout();

    Centre(wxBOTH);
	wxGetApp().UpdateDlgDarkUI(this);
}

void MsgUpdateConfig::on_dpi_changed(const wxRect &suggested_rect) {}


MsgUpdateConfig::~MsgUpdateConfig() {}

MsgUpdateForced::MsgUpdateForced(const std::vector<Update>& updates) :
    MsgDialog(nullptr, _(L("Configuration incompatible")), _(L("the configuration package is incompatible with the current application.")) + " ", wxOK | wxICON_ERROR)
{
	auto* text = new wxStaticText(this, wxID_ANY, wxString::Format(_(L(
		"The configuration package is incompatible with the current application.\n"
		"%s will update the configuration package to allow the application to start."
	)), SLIC3R_APP_FULL_NAME));
	

	text->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());
	content_sizer->Add(text);
	content_sizer->AddSpacer(VERT_SPACING);

	const auto lang_code = wxGetApp().current_language_code_safe().ToStdString();

	auto* versions = new wxFlexGridSizer(2, 0, VERT_SPACING);
	wxTextCtrl* changelog_textctrl = nullptr;
	for (const auto& update : updates) {
		auto* text_vendor = new wxStaticText(this, wxID_ANY, update.vendor);
		text_vendor->SetFont(boldfont);
		versions->Add(text_vendor);
		versions->Add(new wxStaticText(this, wxID_ANY, update.version.to_string()));

		if (!update.comment.empty()) {
			versions->Add(new wxStaticText(this, wxID_ANY, _(L("Description:"))) );
			auto* update_comment = new wxStaticText(this, wxID_ANY, from_u8(update.comment));
			update_comment->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());
			versions->Add(update_comment);
		}
		if (! update.change_log.empty()) {
			if (!changelog_textctrl)
				changelog_textctrl = new wxTextCtrl(this, wxID_ANY, from_u8(update.change_log), wxDefaultPosition,  wxDefaultSize, wxTE_MULTILINE|wxTE_READONLY|wxHSCROLL);
			else
				changelog_textctrl->AppendText(from_u8(update.change_log));
		}

	}

	content_sizer->Add(versions);
	if (changelog_textctrl)
		content_sizer->Add(changelog_textctrl);

	add_button(wxID_EXIT, false, wxString::Format(_L("Exit %s"), SLIC3R_APP_FULL_NAME));
	for (auto ID : { wxID_EXIT, wxID_OK })
		get_button(ID)->Bind(wxEVT_BUTTON, [this](const wxCommandEvent& evt) { EndModal(evt.GetId()); });

	finalize();
}

MsgUpdateForced::~MsgUpdateForced() {}

MsgDataIncompatible::MsgDataIncompatible(const std::unordered_map<std::string, wxString> &incompats) :
    MsgDialog(nullptr,  _(L("Configuration incompatible")), _(L("the configuration package is incompatible with the current application.")), wxICON_ERROR)
{

	finalize();
}

MsgDataIncompatible::~MsgDataIncompatible() {}


MsgNoUpdates::MsgNoUpdates() :
    MsgDialog(nullptr, _(L("Configuration updates")), _(L("No updates available.")), wxICON_ERROR | wxOK)
{

	auto* text = new wxStaticText(this, wxID_ANY, _(L("The configuration is up to date.")));
	text->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());
	content_sizer->Add(text);
	content_sizer->AddSpacer(VERT_SPACING);

	finalize();
}

MsgNoUpdates::~MsgNoUpdates() {}

}
}
