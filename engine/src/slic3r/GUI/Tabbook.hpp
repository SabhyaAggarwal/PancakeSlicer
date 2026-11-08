#ifndef slic3r_Tabbook_hpp_
#define slic3r_Tabbook_hpp_

#include <cstddef>
#include <string>
#include <vector>
#include <wx/bookctrl.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/debug.h>
#include <wx/chartype.h>
#include <wx/object.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/window.h>
#include <wx/withimages.h>
#include "wxExtensions.hpp"
#include <wx/defs.h>
#include <wx/notebook.h>

class wxObject;
class wxStaticText;


class TabButton;

wxDECLARE_EVENT(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, wxCommandEvent);

class TabButtonsListCtrl : public wxControl
{
public:
    TabButtonsListCtrl(wxWindow *parent, wxBoxSizer *side_tools = NULL);
    ~TabButtonsListCtrl() {}

    void OnPaint(wxPaintEvent&);
    void SetSelection(int sel);
    void showNewTag(int sel, bool show = false);
    void Rescale();
    bool InsertPage(size_t n, const wxString& text, bool bSelect = false, const std::string& bmp_name = "");
    void RemovePage(size_t n);
    bool SetPageImage(size_t n, const std::string& bmp_name);
    void SetPageText(size_t n, const wxString& strText);
    wxString GetPageText(size_t n) const;
    const wxSize& GetPaddingSize(size_t n);
    void SetPaddingSize(const wxSize& size);
    void SetFooterText(const wxString& text);
    TabButton*                      pageButton;

private:
    wxFlexGridSizer*                m_buttons_sizer;
    wxBoxSizer*                     m_sizer;
    ScalableBitmap                  m_arrow_img;
    std::vector<TabButton*>         m_pageButtons;
    int                             m_selection {-1};
    int                             m_btn_margin;
    int                             m_line_margin;
    wxStaticText*                   m_footer_text {nullptr};
};

class Tabbook: public wxBookCtrlBase
{
public:
    Tabbook(wxWindow *     parent,
                 wxWindowID winid = wxID_ANY,
                 const wxPoint & pos = wxDefaultPosition,
                 const wxSize & size = wxDefaultSize,
                 wxBoxSizer* side_tools = NULL,
                 long style = 0)
    {
        Init();
        Create(parent, winid, pos, size, side_tools, style);
    }

    bool Create(wxWindow * parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint & pos = wxDefaultPosition,
                const wxSize & size = wxDefaultSize,
                wxBoxSizer* side_tools = NULL,
                long style = 0)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style))
            return false;

        m_bookctrl = new TabButtonsListCtrl(this, side_tools);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(0).Expand());
        wxSizerFlags flags;
        if (IsVertical())
            flags.Expand();
        else
            flags.Expand();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, [this](wxCommandEvent& evt)
        {
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(wxEVT_NAVIGATION_KEY, &Tabbook::OnNavigationKey, this);

        return true;
    }


    bool ShowNewPage(wxWindow * page)
    {
        return AddPage(page, wxString());
    }

    void SetEffects(wxShowEffect showEffect, wxShowEffect hideEffect)
    {
        m_showEffect = showEffect;
        m_hideEffect = hideEffect;
    }

    void SetEffect(wxShowEffect effect)
    {
        SetEffects(effect, effect);
    }

    void SetEffectsTimeouts(unsigned showTimeout, unsigned hideTimeout)
    {
        m_showTimeout = showTimeout;
        m_hideTimeout = hideTimeout;
    }

    void SetEffectTimeout(unsigned timeout)
    {
        SetEffectsTimeouts(timeout, timeout);
    }


    bool AddPage(wxWindow* page,
                 const wxString& text,
                 bool bSelect = false,
                 int imageId = NO_IMAGE) override
    {
        DoInvalidateBestSize();
        return InsertPage(GetPageCount(), page, text, bSelect, imageId);
    }

    virtual bool InsertPage(size_t n,
                            wxWindow * page,
                            const wxString & text,
                            bool bSelect = false,
                            int imageId = NO_IMAGE) override
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect, imageId))
            return false;

        GetBtnsListCtrl()->InsertPage(n, text, bSelect);

        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return true;
    }

    bool RemovePage(size_t n) override
    {
        if (!wxBookCtrlBase::RemovePage(n))
            return false;

        SetSelection(0);
        return true;
    }

    virtual int SetSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        int ret = DoSetSelection(n, SetSelection_SendEvent);

        for (size_t page = 0; page < m_pages.size(); page++) {
            wxWindow* win_a = GetPage(page);
            wxWindow* win_b = GetPage(n);
            if (page != n && GetPage(page) != GetPage(n)) {
                m_pages[page]->Hide();
            }
        }

        return ret;
    }

    virtual int ChangeSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    virtual bool SetPageText(size_t n, const wxString & strText) override
    {
        wxCHECK_MSG(n < GetPageCount(), false, wxS("Invalid page"));

        GetBtnsListCtrl()->SetPageText(n, strText);

        return true;
    }

    virtual wxString GetPageText(size_t n) const override
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetBtnsListCtrl()->GetPageText(n);
    }

    virtual bool SetPageImage(size_t WXUNUSED(n), int WXUNUSED(imageId)) override
    {
        return false;
    }

    virtual int GetPageImage(size_t WXUNUSED(n)) const override
    {
        return NO_IMAGE;
    }

    bool SetPageImage(size_t n, const std::string& bmp_name)
    {
        return GetBtnsListCtrl()->SetPageImage(n, bmp_name);
    }

    virtual void SetFocus() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetFocus();
    }

    TabButtonsListCtrl *GetBtnsListCtrl() const { return static_cast<TabButtonsListCtrl *>(m_bookctrl); }

    void Rescale()
    {
        GetBtnsListCtrl()->Rescale();
    }

    void SetFooterText(const wxString& text)
    {
        GetBtnsListCtrl()->SetFooterText(text);
    }

    void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            this->GetGrandParent()->HandleWindowEvent(event);
        }
        else {
            wxWindow* const parent = GetParent();

            const bool isFromParent = event.GetEventObject() == (wxObject*)parent;
            const bool isFromSelf = event.GetEventObject() == (wxObject*)this;
            const bool isForward = event.GetDirection();

            if (isFromSelf && !isForward)
            {
                event.SetCurrentFocus(this);
                parent->HandleWindowEvent(event);
            }
            else if (isFromParent || isFromSelf)
            {
                if (m_selection != wxNOT_FOUND &&
                    (!event.GetDirection() || isFromSelf))
                {
                    event.SetEventObject(this);

                    wxWindow* page = m_pages[m_selection];
                    if (!page->HandleWindowEvent(event))
                    {
                        page->SetFocus();
                    }
                }
                else
                {
                    SetFocus();
                }
            }
            else
            {
                if (!isForward)
                {
                    SetFocus();
                }
                else if (parent)
                {
                    event.SetCurrentFocus(this);
                    parent->HandleWindowEvent(event);
                }
            }
        }
    }

protected:
    virtual void UpdateSelectedPage(size_t WXUNUSED(newsel)) override
    {
    }

    virtual wxBookCtrlEvent * CreatePageChangingEvent() const override
    {
        return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING,
                                   GetId());
    }

    virtual void MakeChangedEvent(wxBookCtrlEvent & event) override
    {
        event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED);
    }

    virtual wxWindow * DoRemovePage(size_t page) override
    {
        wxWindow* const win = wxBookCtrlBase::DoRemovePage(page);
        if (win)
        {
            GetBtnsListCtrl()->RemovePage(page);
            DoSetSelectionAfterRemoval(page);
        }

        return win;
    }

    virtual void DoSize() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetSize(GetPageRect());
    }

    virtual void DoShowPage(wxWindow * page, bool show) override
    {
        if (show)
            page->ShowWithEffect(m_showEffect, m_showTimeout);
        else
            page->HideWithEffect(m_hideEffect, m_hideTimeout);
    }

private:
    void Init()
    {
        SetInternalBorder(0);

        m_showEffect =
        m_hideEffect = wxSHOW_EFFECT_NONE;

        m_showTimeout =
        m_hideTimeout = 0;
    }

    wxShowEffect m_showEffect,
                 m_hideEffect;

    unsigned m_showTimeout,
             m_hideTimeout;

};
#endif
