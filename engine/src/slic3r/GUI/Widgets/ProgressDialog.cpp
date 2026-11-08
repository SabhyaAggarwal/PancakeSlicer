#include "wx/wxprec.h"
#include <cstddef>
#include <wx/time.h>
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/progdlg.h>
#include <wx/panel.h>
#include "slic3r/GUI/Widgets/Button.hpp"
#include <wx/translation.h>
#include <wx/debug.h>

#ifndef WX_PRECOMP
#include "wx/utils.h"
#include "wx/frame.h"
#include "wx/button.h"
#include "wx/stattext.h"
#include "wx/sizer.h"
#include "wx/event.h"
#include "wx/gauge.h"
#include "wx/intl.h"
#include "wx/dcclient.h"
#include "wx/timer.h"
#include "wx/settings.h"
#include "wx/app.h"
#endif

#include "../GUI.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "ProgressDialog.hpp"
#include "wx/evtloop.h"
#include "Label.hpp"
#include <wx/event.h>
#include <wx/gauge.h>
#include <wx/sizer.h>

#ifdef __WXGTK__
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#endif

#define LAYOUT_MARGIN 12

static const int wxID_SKIP = 32000;

namespace Slic3r { namespace GUI {
void ProgressDialog::Init()
{
    SetExtraStyle(GetExtraStyle() | wxWS_EX_TRANSIENT);

    m_pdStyle   = 0;
    m_parentTop = NULL;

    m_gauge   = NULL;
    m_msg     = NULL;
    m_elapsed = m_estimated = m_remaining = NULL;

    m_state   = Uncancelable;
    m_maximum = 0;

    m_timeStart = wxGetCurrentTime();
    m_timeStop  = (unsigned long) -1;
    m_break     = 0;

    m_skip = false;

    m_btnAbort = NULL;
    m_btnSkip  = NULL;

    m_display_estimated = m_last_timeupdate = m_ctdelay = 0;

    m_delay = 3;

    m_winDisabler   = NULL;
    m_tempEventLoop = NULL;

    SetWindowStyle(wxDEFAULT_DIALOG_STYLE);
}

ProgressDialog::ProgressDialog() : wxDialog() { Init(); }

ProgressDialog::ProgressDialog(const wxString &title, const wxString &message, int maximum, wxWindow *parent, int style, bool adaptive) : wxDialog()
{
    m_adaptive = adaptive;
    Init();
    Create(title, message, maximum, parent, style);
    Bind(wxEVT_PAINT, &ProgressDialog::OnPaint, this);
    Bind(wxEVT_CLOSE_WINDOW, &ProgressDialog::OnClose, this);
}

void ProgressDialog::OnPaint(wxPaintEvent &evt) {}

void ProgressDialog::SetTopParent(wxWindow *parent)
{
    m_parent    = parent;
    m_parentTop = parent;
}

wxString ProgressDialog::FormatString(wxString title)
{
    if (!m_adaptive) {
        auto current_width = 0;
        m_mode             = 0;
        for (int i = 0; i < title.length(); i++) {
            current_width = m_msg->GetTextExtent(title.SubString(0, i)).x;
            if (current_width > PROGRESSDIALOG_GAUGE_SIZE.x) {
                m_mode = 1;
                break;
            }
        }
        if (m_mode == 0) {
            m_simplebook->SetSelection(0);
            m_msg->SetLabel(title);
        } else {
            wxSize content_size = m_msg->GetTextExtent(title);
            int resized_height = (int(content_size.x / PROGRESSDIALOG_GAUGE_SIZE.x) + 1) * content_size.y;
            set_panel_height(resized_height);
            m_simplebook->SetSelection(1);
            m_msg_2line->SetLabel(title);
        }
    } else {
        m_msg->SetLabel(title);
        m_msg->SetMaxSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x + 5, -1));
        m_msg->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x  + 5, -1));
        m_msg->Wrap(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x  + 5);
        m_msg->Layout();
        m_msg->Fit();

        m_msg_scrolledWindow->SetSize(wxSize(FromDIP(m_msg->GetSize().x + 5), FromDIP(150)));
        m_msg_scrolledWindow->SetMinSize(wxSize(FromDIP(m_msg->GetSize().x + 5), FromDIP(150)));
        m_msg_scrolledWindow->SetMaxSize(wxSize(FromDIP(m_msg->GetSize().x + 5), FromDIP(150)));
        m_msg_scrolledWindow->Fit();
    }

    
    return title;
}

bool ProgressDialog::Create(const wxString &title, const wxString &message, int maximum, wxWindow *parent, int style)
{
    SetFont(wxGetApp().normal_font());
    SetTopParent(parent);

    m_pdStyle = style;

    if (!wxDialog::Create(m_parentTop, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, GetWindowStyle())) return false;
    SetBackgroundColour(PROGRESSDIALOG_DEF_BK);

    SetMaximum(maximum);
    EnsureActiveEventLoopExists();

#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
    if (!HasPDFlag(wxPD_CAN_ABORT)) { EnableCloseButton(false); }
#endif

    m_state = HasPDFlag(wxPD_CAN_ABORT) ? Continue : Uncancelable;

    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_top_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_top_line->SetBackgroundColour(wxColour(166, 169, 170));

    m_sizer_main->Add(m_top_line, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(24));

    if (!m_adaptive) {
        m_simplebook = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);

        m_panel_1line = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_panel_1line->SetMaxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE);
        m_panel_1line->SetBackgroundColour(PROGRESSDIALOG_DEF_BK);
        m_simplebook->AddPage(m_panel_1line, wxEmptyString, false);

        m_panel_2line = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_panel_2line->SetMaxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE);
        m_panel_2line->SetBackgroundColour(PROGRESSDIALOG_DEF_BK);
        m_simplebook->AddPage(m_panel_2line, wxEmptyString, false);

        wxBoxSizer *sizer_1line = new wxBoxSizer(wxHORIZONTAL);
        m_msg                   = new wxStaticText(m_panel_1line, wxID_ANY, wxEmptyString, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_msg->Wrap(-1);
        m_msg->SetFont(::Label::Body_14);
        m_msg->SetForegroundColour(PROGRESSDIALOG_GREY_700);
        sizer_1line->Add(m_msg, 0, wxALIGN_CENTER, 0);
        m_panel_1line->SetSizer(sizer_1line);
        m_panel_1line->Layout();
        sizer_1line->Fit(m_panel_1line);

        wxBoxSizer *sizer_2line = new wxBoxSizer(wxVERTICAL);
        m_msg_2line             = new wxStaticText(m_panel_2line, wxID_ANY, wxEmptyString, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_msg_2line->Wrap(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x);
        m_msg_2line->SetFont(::Label::Body_14);
        m_msg_2line->SetForegroundColour(PROGRESSDIALOG_GREY_700);
        m_msg_2line->SetMaxSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, -1));
        sizer_2line->Add(m_msg_2line, 1, wxALL, 0);
        m_panel_2line->SetSizer(sizer_2line);
        m_panel_2line->Layout();
        sizer_2line->Fit(m_panel_2line);

        m_sizer_main->Add(m_simplebook, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(28));
    } else {
        m_msg_scrolledWindow = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL );
        m_msg_scrolledWindow->SetScrollRate(0, FromDIP(20));
        wxBoxSizer* m_msg_sizer= new wxBoxSizer(wxVERTICAL);

        m_msg = new wxStaticText(m_msg_scrolledWindow, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, -1), 0);
        m_msg->Wrap(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x);
        m_msg->SetFont(::Label::Body_14);
        m_msg->SetForegroundColour(PROGRESSDIALOG_GREY_700);

        m_msg_sizer->Add(m_msg, 0, wxEXPAND | wxALL, 0);
        m_msg_scrolledWindow->SetSizer(m_msg_sizer);
        m_msg_scrolledWindow->Layout();
        m_msg_sizer->Fit(m_msg_scrolledWindow);
        m_sizer_main->Add(m_msg_scrolledWindow, 0, wxEXPAND | wxALL, FromDIP(28));
    }


    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 0);

    int gauge_style = wxGA_HORIZONTAL;
    if (style & wxPD_SMOOTH) gauge_style |= wxGA_SMOOTH;
    gauge_style |= wxGA_PROGRESS;

#ifdef __WXMSW__
    maximum /= m_factor;
#endif

    if (!HasPDFlag(wxPD_NO_PROGRESS)) {
        m_gauge = new wxGauge(this, wxID_ANY, maximum, wxDefaultPosition, PROGRESSDIALOG_GAUGE_SIZE, gauge_style);
#ifdef __WXGTK__
        SetGaugeColor(m_gauge, 
            StateColor::darkModeColorFor(wxColour("#009688")).GetAsString(), 
            StateColor::darkModeColorFor(wxColour("#D9D9D9")).GetAsString()
        );
        m_gauge->SetValue(0);
#else
        m_gauge->Pulse();
        m_gauge->SetValue(0);
        m_gauge->SetForegroundColour(wxColour("#009688"));
        m_gauge->SetBackgroundColour(wxColour("#D9D9D9"));
        wxGetApp().UpdateDarkUI(m_gauge);
#endif
        m_sizer_main->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(28));
    }

    if (HasPDFlag(wxPD_ELAPSED_TIME | wxPD_ESTIMATED_TIME | wxPD_REMAINING_TIME)) {
        wxFlexGridSizer *sizer_times = new wxFlexGridSizer(2, FromDIP(2), FromDIP(8));
        if (HasPDFlag(wxPD_ELAPSED_TIME))   m_elapsed   = CreateLabel(GetElapsedLabel(), sizer_times);
        if (HasPDFlag(wxPD_ESTIMATED_TIME)) m_estimated = CreateLabel(GetEstimatedLabel(), sizer_times);
        if (HasPDFlag(wxPD_REMAINING_TIME)) m_remaining = CreateLabel(GetRemainingLabel(), sizer_times);
        m_sizer_main->Add(sizer_times, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(12));
    }

#ifdef __WXMSW__
#endif
    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_bottom->Add(0, 0, 1, wxEXPAND, 0);

    if (HasPDFlag(wxPD_CAN_ABORT)) {
        m_button_cancel = new Button(this, _L("Cancel"));
        m_button_cancel->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
        m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            if (m_state == Finished) {
                event.Skip();
            } else {
                m_state = Canceled;
                DisableAbort();
                m_button_cancel->Enable(false);
                DisableSkip();
                OnCancel();
                m_timeStop = wxGetCurrentTime();
            }
        });
        m_sizer_bottom->Add(m_button_cancel, 0, wxALL, 0);
    }

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(16));
    m_sizer_main->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(28));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(10));

    wxGetApp().UpdateDlgDarkUI(this);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxCENTER_FRAME | wxBOTH);
    DisableOtherWindows();
    FormatString(message);

    Show();
    Enable();
    if (m_elapsed) { SetTimeLabel(0, m_elapsed); }

    Update();
    return true;

}

void ProgressDialog::UpdateTimeEstimates(int value, unsigned long &elapsedTime, unsigned long &estimatedTime, unsigned long &remainingTime)
{
    unsigned long elapsed = wxGetCurrentTime() - m_timeStart;
    if (value != 0 && (m_last_timeupdate < elapsed || value == m_maximum)) {
        m_last_timeupdate       = elapsed;
        unsigned long estimated = m_break + (unsigned long) (((double) (elapsed - m_break) * m_maximum) / ((double) value));
        if (estimated > m_display_estimated && m_ctdelay >= 0) {
            ++m_ctdelay;
        } else if (estimated < m_display_estimated && m_ctdelay <= 0) {
            --m_ctdelay;
        } else {
            m_ctdelay = 0;
        }
        if (m_ctdelay >= m_delay
            || m_ctdelay <= (m_delay * -1)
            || value == m_maximum
            || elapsed > m_display_estimated
            || (elapsed > 0 && elapsed < 4)
        ) {
            m_display_estimated = estimated;
            m_ctdelay           = 0;
        }
    }

    if (value != 0) {
        long display_remaining = m_display_estimated - elapsed;
        if (display_remaining < 0) { display_remaining = 0; }

        estimatedTime = m_display_estimated;
        remainingTime = display_remaining;
    }

    elapsedTime = elapsed;
}

wxString ProgressDialog::GetFormattedTime(unsigned long timeInSec)
{
    wxString timeAsHMS;

    if (timeInSec == (unsigned long) -1) {
        timeAsHMS = wxGetTranslation("Unknown");
    } else {
        unsigned hours   = timeInSec / 3600;
        unsigned minutes = (timeInSec % 3600) / 60;
        unsigned seconds = timeInSec % 60;
        timeAsHMS.Printf("%u:%02u:%02u", hours, minutes, seconds);
    }

    return timeAsHMS;
}

void ProgressDialog::EnsureActiveEventLoopExists()
{
    if (!wxEventLoopBase::GetActive()) {
        m_tempEventLoop = new wxEventLoop;
        wxEventLoop::SetActive(m_tempEventLoop);
    }
}

wxStaticText *ProgressDialog::CreateLabel(const wxString &text, wxSizer *sizer)
{
    wxStaticText *label = new wxStaticText(this, wxID_ANY, text);
    wxStaticText *value = new wxStaticText(this, wxID_ANY, _L("unknown"));

    for (wxStaticText *st : {label, value}) {
        st->SetFont(::Label::Body_13);
        st->SetForegroundColour(PROGRESSDIALOG_GREY_700);
    }

#if defined(__WXMSW__) || defined(__WXMAC__) || defined(__WXGTK20__)
    sizer->Add(label, 1, wxALIGN_RIGHT | wxTOP | wxRIGHT, LAYOUT_MARGIN);
    sizer->Add(value, 1, wxALIGN_LEFT | wxTOP, LAYOUT_MARGIN);
#else
    sizer->Add(label);
    sizer->Add(value, 0, wxLEFT, LAYOUT_MARGIN);
#endif

    return value;
}

bool ProgressDialog::Update(int value, const wxString &newmsg, bool *skip)
{
    if (!DoBeforeUpdate(skip)) return false;

    wxCHECK_MSG(m_msg || m_gauge, false, "dialog should be fully created");

#ifdef __WXMSW__
    value /= m_factor;
#endif

    wxASSERT_MSG(value <= m_maximum, wxT("invalid progress value"));
    if (m_gauge)
        m_gauge->SetValue(value);

    UpdateMessage(newmsg);

    if ((m_elapsed || m_remaining || m_estimated) && (value != 0)) {
        unsigned long elapsed;
        unsigned long display_remaining;

        UpdateTimeEstimates(value, elapsed, m_display_estimated, display_remaining);

        SetTimeLabel(elapsed, m_elapsed);
        SetTimeLabel(m_display_estimated, m_estimated);
        SetTimeLabel(display_remaining, m_remaining);
    }

    if (value == m_maximum) {
        if (m_state == Finished) {
            return true;
        }

        m_state = Finished;
        if (!HasPDFlag(wxPD_AUTO_HIDE)) {
            EnableClose();
            DisableSkip();
#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
            EnableCloseButton();
#endif

            if (newmsg.empty()) {
                m_msg->SetLabel(_L("Done."));
            }

            wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);

            (void) ShowModal();
        } else
        {
            ReenableOtherWindows();

            Hide();
        }
    } else
    {
        DoAfterUpdate();
    }

    Update();

    return m_state != Canceled;
}

bool ProgressDialog::Pulse(const wxString &newmsg, bool *skip)
{
    if (!DoBeforeUpdate(skip)) return false;

    wxCHECK_MSG(m_msg || m_gauge, false, "dialog should be fully created");

    if (m_gauge) m_gauge->Pulse();

    UpdateMessage(newmsg);

    if (m_elapsed || m_remaining || m_estimated) {
        unsigned long elapsed = wxGetCurrentTime() - m_timeStart;

        SetTimeLabel(elapsed, m_elapsed);
        SetTimeLabel((unsigned long) -1, m_estimated);
        SetTimeLabel((unsigned long) -1, m_remaining);
    }

    DoAfterUpdate();

    return m_state != Canceled;
}

bool ProgressDialog::WasCanceled() const { return m_state == Canceled; }

bool ProgressDialog::DoBeforeUpdate(bool *skip)
{
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);

    Update();

    if (m_skip && skip && !*skip) {
        *skip  = true;
        m_skip = false;
        EnableSkip();
    }

    return m_state != Canceled;
}

void ProgressDialog::DoAfterUpdate()
{
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
}

void ProgressDialog::Resume()
{
    m_state   = Continue;
    m_ctdelay = m_delay;
    m_break += wxGetCurrentTime() - m_timeStop;

    EnableAbort();
    m_button_cancel->Enable(true);
    EnableSkip();
    m_skip = false;
}

bool ProgressDialog::Show(bool show)
{
    if (!show) ReenableOtherWindows();
    return wxDialog::Show(show);
}

int ProgressDialog::GetValue() const
{
    wxCHECK_MSG(m_gauge, -1, "dialog should be fully created");

    return m_gauge->GetValue();
}

int ProgressDialog::GetRange() const { return m_maximum; }

wxString ProgressDialog::GetMessage() const { return m_msg->GetLabel(); }

void ProgressDialog::SetRange(int maximum)
{
    wxCHECK_RET(m_gauge, "dialog should be fully created");

    wxCHECK_RET(maximum > 0, "Invalid range");

    m_gauge->SetRange(maximum);

    SetMaximum(maximum);
}

void ProgressDialog::set_panel_height(int height) {
    m_simplebook->SetSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_simplebook->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_panel_2line->SetSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_panel_2line->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_msg_2line->SetSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_msg_2line->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
}

void ProgressDialog::SetMaximum(int maximum)
{
    m_maximum = maximum;

#if defined(__WXMSW__)
    m_factor = m_maximum / 65536 + 1;
    m_maximum /= m_factor;
#endif
}

bool ProgressDialog::WasCancelled() const { return HasPDFlag(wxPD_CAN_ABORT) && m_state == Canceled; }

bool ProgressDialog::WasSkipped() const { return HasPDFlag(wxPD_CAN_SKIP) && m_skip; }

void ProgressDialog::SetTimeLabel(unsigned long val, wxStaticText *label)
{
    if (label) {
        wxString s;

        if (val != (unsigned long) -1) {
            s = GetFormattedTime(val);
        } else {
            s = wxGetTranslation("Unknown");
        }

        if (s != label->GetLabel()) label->SetLabel(s);
    }
}

void ProgressDialog::OnCancel(wxCommandEvent &event)
{
    if (m_state == Finished) {
        event.Skip();
    } else {
        m_state = Canceled;

        DisableAbort();
        DisableSkip();

        OnCancel();

        m_timeStop = wxGetCurrentTime();
    }
}

void ProgressDialog::OnSkip(wxCommandEvent &WXUNUSED(event))
{
    DisableSkip();
    m_skip = true;
}

void ProgressDialog::OnClose(wxCloseEvent &event)
{
    if (m_state == Uncancelable) {
        event.Veto();
    } else if (m_state == Finished) {
        event.Skip();
    } else {
        m_state = Canceled;
        DisableAbort();
        DisableSkip();
        OnCancel();

        m_timeStop = wxGetCurrentTime();
    }
}

ProgressDialog::~ProgressDialog()
{
    ReenableOtherWindows();

    if (m_tempEventLoop) {
        wxCHECK_RET(wxEventLoopBase::GetActive() == m_tempEventLoop, "current event loop must not be changed during "
                                                                     "ProgressDialog lifetime");

        wxEventLoopBase::SetActive(NULL);
        delete m_tempEventLoop;
    }
}

void ProgressDialog::DoSetSize(int x, int y, int width, int height, int sizeFlags )
{

#ifdef __WXMSW__
#endif
    wxDialog::DoSetSize(x, y, width, height, sizeFlags);
}

void ProgressDialog::DisableOtherWindows()
{
    if (HasPDFlag(wxPD_APP_MODAL)) {
#if defined(__WXOSX__)
        if (m_parentTop) m_parentTop->Disable();
        m_winDisabler = NULL;
#else
        m_winDisabler = new wxWindowDisabler(this);
#endif
    } else {
        if (m_parentTop) m_parentTop->Disable();
        m_winDisabler = NULL;
    }

}

void ProgressDialog::ReenableOtherWindows()
{
    if (HasPDFlag(wxPD_APP_MODAL)) {
#if defined(__WXOSX__)
        if (m_parentTop) m_parentTop->Enable();
#else
        wxDELETE(m_winDisabler);
#endif
    } else {
        if (m_parentTop) m_parentTop->Enable();
    }
}

void ProgressDialog::EnableSkip(bool enable)
{
    if (HasPDFlag(wxPD_CAN_SKIP)) {
        if (m_btnSkip) m_btnSkip->Enable(enable);
    }
}

void ProgressDialog::EnableAbort(bool enable)
{
    if (HasPDFlag(wxPD_CAN_ABORT)) {
        if (m_btnAbort) m_btnAbort->Enable(enable);
    }
}

void ProgressDialog::EnableClose()
{
    if (HasPDFlag(wxPD_CAN_ABORT)) {
        if (m_btnAbort) {
            m_btnAbort->Enable();
            m_btnAbort->SetLabel(wxGetTranslation("Close"));
        }
    }
}

void ProgressDialog::UpdateMessage(const wxString &newmsg)
{
    if (!newmsg.empty() && newmsg != m_msg->GetLabel()) {
        const wxSize sizeOld = m_msg->GetSize();

        FormatString(newmsg);
        Fit();

        if (m_msg->GetSize().x > sizeOld.x) {
            Fit();
        }

        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    }
}
}}
