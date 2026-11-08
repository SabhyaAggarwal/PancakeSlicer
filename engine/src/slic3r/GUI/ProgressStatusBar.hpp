#ifndef PROGRESSSTATUSBAR_HPP
#define PROGRESSSTATUSBAR_HPP

#include <memory>
#include <string>
#include <functional>

#include "Jobs/ProgressIndicator.hpp"

class wxTimer;
class wxGauge;
class wxButton;
class wxStatusBar;
class wxWindow;
class wxFrame;
class wxString;
class wxFont;

namespace Slic3r {

class ProgressStatusBar : public ProgressIndicator
{
    wxStatusBar *self;
    wxGauge *m_prog;
    wxButton *m_cancelbutton;
    std::unique_ptr<wxTimer> m_timer;
public:


    ProgressStatusBar(wxWindow *parent = nullptr, int id = -1);
    ~ProgressStatusBar() override;

    int         get_progress() const;
    void        set_progress(int) override;
    int         get_range() const override;
    void        set_range(int = 100) override;
    void        show_progress(bool);
    void        start_busy(int = 100);
    void        stop_busy();
    inline bool is_busy() const { return m_busy; }
    void        set_cancel_callback(CancelFn = CancelFn()) override;
    inline void reset_cancel_callback() { set_cancel_callback(); }
    void        run(int rate);
    void        embed(wxFrame *frame = nullptr);
    void        set_status_text(const wxString& txt);
    void        set_status_text(const std::string& txt);
    void        set_status_text(const char *txt) override;
    wxString    get_status_text() const;
    void        set_font(const wxFont &font);

    void        show_cancel_button();
    void        hide_cancel_button();

    void        update_dark_ui();

private:
    bool m_busy = false;
    CancelFn m_cancel_cb;
};

}

#endif
