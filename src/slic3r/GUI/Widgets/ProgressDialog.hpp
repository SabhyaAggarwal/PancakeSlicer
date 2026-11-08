#ifndef slic3r_GUI_PROGRESSDIALOG_hpp_
#define slic3r_GUI_PROGRESSDIALOG_hpp_

#include "wx/dialog.h"
#include "wx/progdlg.h"
#include "wx/weakref.h"
#include "wx/simplebook.h"
#include "../I18N.hpp"
#include <wx/dlimpexp.h>
#include <wx/string.h>
#include <cstddef>
#include <wx/event.h>
#include <wx/panel.h>
#include <wx/translation.h>

class Button;
class wxCloseEvent;
class wxCommandEvent;
class wxPaintEvent;
class wxPanel;
class wxScrolledWindow;
class wxSimplebook;
class wxSizer;
class wxWindow;
namespace Slic3r::GUI { class ProgressDialog; }

class WXDLLIMPEXP_FWD_CORE wxButton;
class WXDLLIMPEXP_FWD_CORE wxEventLoop;
class WXDLLIMPEXP_FWD_CORE wxGauge;
class WXDLLIMPEXP_FWD_CORE wxStaticText;
class WXDLLIMPEXP_FWD_CORE wxWindowDisabler;

#define PROGRESSDIALOG_SIMPLEBOOK_SIZE wxSize(FromDIP(320),FromDIP(38))
#define PROGRESSDIALOG_GAUGE_SIZE wxSize(FromDIP(320), FromDIP(8))
#define PROGRESSDIALOG_CANCEL_BUTTON_SIZE wxSize(FromDIP(60), FromDIP(24))
#define PROGRESSDIALOG_DEF_BK wxColour(255,255,255)
#define PROGRESSDIALOG_GREY_700 wxColour(54,54,54)

#define wxPD_NO_PROGRESS 0x0100

namespace Slic3r { namespace GUI {

class WXDLLIMPEXP_CORE ProgressDialog : public wxDialog
{
public:
    ProgressDialog();
    ProgressDialog(const wxString &title, const wxString &message, int maximum = 100, wxWindow *parent = NULL, int style = wxPD_APP_MODAL | wxPD_AUTO_HIDE, bool adaptive = false);

	void OnPaint(wxPaintEvent &evt);
    virtual ~ProgressDialog();

    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
    bool Create(const wxString &title, const wxString &message, int maximum = 100, wxWindow *parent = NULL, int style = wxPD_APP_MODAL | wxPD_AUTO_HIDE);

    virtual bool Update(int value, const wxString &newmsg = wxEmptyString, bool *skip = NULL);
    virtual bool Pulse(const wxString &newmsg = wxEmptyString, bool *skip = NULL);
    bool         WasCanceled() const;

    virtual void Resume();

    virtual int      GetValue() const;
    virtual int      GetRange() const;
    virtual wxString GetMessage() const;

    virtual void SetRange(int maximum);
    virtual bool WasCancelled() const;
    virtual bool WasSkipped() const;

    virtual void OnCancel() {};

    virtual void Update() wxOVERRIDE { wxDialog::Update(); }

    virtual bool Show(bool show = true) wxOVERRIDE;

    enum State {
        Uncancelable = -1,
        Canceled,
        Continue,
        Finished,
        Dismissed
    };

    int m_mode = 0;

    bool                m_adaptive = {false};
    wxSizer *           m_sizer_main = {nullptr};
    wxPanel *           m_top_line= {nullptr};
    wxSimplebook *      m_simplebook= {nullptr};
    wxPanel *           m_panel_2line= {nullptr};
    wxPanel *           m_panel_1line= {nullptr};
    Button*             m_button_cancel = {nullptr};
    wxScrolledWindow*   m_msg_scrolledWindow = {nullptr};

protected:
    void SetMaximum(int maximum);
    static wxString GetElapsedLabel() { return _L("Elapsed time:"); }
    static wxString GetEstimatedLabel() { return _L("Estimated time:"); }
    static wxString GetRemainingLabel() { return _L("Remaining time:"); }

    bool HasPDFlag(int flag) const { return (m_pdStyle & flag) != 0; }

    int  GetPDStyle() const { return m_pdStyle; }
    void SetPDStyle(int pdStyle) { m_pdStyle = pdStyle; }
    void set_panel_height(int height);

    void UpdateTimeEstimates(int value, unsigned long &elapsedTime, unsigned long &estimatedTime, unsigned long &remainingTime);

    static wxString GetFormattedTime(unsigned long timeInSec);

    void EnsureActiveEventLoopExists();

    void OnCancel(wxCommandEvent &);

    void OnSkip(wxCommandEvent &);

    void OnClose(wxCloseEvent &);

    void DisableOtherWindows();

    void ReenableOtherWindows();

    void SetTopParent(wxWindow *parent);

	wxString FormatString(wxString title);
    wxWindow *GetTopParent() const { return m_parentTop; }

    State m_state;

    int m_maximum;

#if defined(__WXMSW__)
    size_t m_factor;
#endif
    unsigned long m_timeStart;
    unsigned long m_timeStop;
    unsigned long m_break;

private:
    static void SetTimeLabel(unsigned long val, wxStaticText *label);

    void Init();

    wxStaticText *CreateLabel(const wxString &text, wxSizer *sizer);

    void UpdateMessage(const wxString &newmsg);

    bool DoBeforeUpdate(bool *skip);

    void DoAfterUpdate();

    void EnableClose();
    void EnableSkip(bool enable = true);
    void EnableAbort(bool enable = true);
    void DisableSkip() { EnableSkip(false); }
    void DisableAbort() { EnableAbort(false); }

    wxGauge *m_gauge;
    wxStaticText *m_msg;
    wxStaticText *m_msg_2line;
    wxStaticText *m_elapsed, *m_estimated, *m_remaining;

    wxWindowRef m_parentTop;

    int m_pdStyle;

    bool m_skip;

    wxButton *m_btnAbort;
    wxButton *m_btnSkip;

    unsigned long m_last_timeupdate;

    int m_delay;

    int           m_ctdelay;
    unsigned long m_display_estimated;

    wxWindowDisabler *m_winDisabler;

    wxEventLoop *m_tempEventLoop;

    wxDECLARE_NO_COPY_CLASS(ProgressDialog);
};

}}

#endif
