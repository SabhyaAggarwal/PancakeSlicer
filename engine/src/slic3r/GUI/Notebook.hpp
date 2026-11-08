#ifndef slic3r_Notebook_hpp_
#define slic3r_Notebook_hpp_

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>
#include <wx/bookctrl.h>
#include <wx/bitmap.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/debug.h>
#include <wx/chartype.h>
#include <wx/object.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/withimages.h>
#include <wx/window.h>
#include <wx/defs.h>
#include <wx/notebook.h>

class wxObject;

class Button;

wxDECLARE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

class ButtonsListCtrl : public wxControl
{
public:
    ButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools = NULL);
    ~ButtonsListCtrl() {}

    void OnPaint(wxPaintEvent&);
    void SetSelection(int sel);
    void UpdateMode();
    void Rescale();
    bool InsertPage(size_t n, const wxString &text, bool bSelect = false, const std::string &bmp_name = "", const wxBitmap &bmp = wxNullBitmap);
    void RemovePage(size_t n);
    bool SetPageImage(size_t n, const std::string& bmp_name) const;
    void SetPageText(size_t n, const wxString& strText);
    void SetCompact(size_t n, bool compact);
    wxString GetPageText(size_t n) const;
    wxString GetPageLabel(size_t n) const;
    const std::string& GetPageIcon(size_t n) const
    {
        static const std::string empty;
        return n < m_pageIcons.size() ? m_pageIcons[n] : empty;
    }
    wxFlexGridSizer* GetBtnsSizer(){return m_buttons_sizer;};
    void SetOverflowButton(wxWindow* button);

private:
    wxFlexGridSizer*                m_buttons_sizer;
    wxBoxSizer*                     m_sizer;
    std::vector<Button*>            m_pageButtons;
    int                             m_selection {-1};
    int                             m_btn_margin;
    int                             m_line_margin;
    std::vector<wxString>           m_pageLabels;
    std::vector<std::string>        m_pageIcons;
    wxWindow*                       m_overflow_button{nullptr};
};

class Notebook : public wxBookCtrlBase
{
public:
    Notebook(wxWindow * parent,
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
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style | wxBK_TOP))
            return false;

        m_bookctrl = new ButtonsListCtrl(this, side_tools);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(1).Expand());
        wxSizerFlags flags;
        if (IsVertical())
            flags.Expand();
        else
            flags.CentreVertical();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, [this](wxCommandEvent& evt)
        {
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(wxEVT_NAVIGATION_KEY, &Notebook::OnNavigationKey, this);

        return true;
    }


    bool ShowNewPage(wxWindow * page)
    {
        return AddPage(page, wxString(), false, NO_IMAGE);
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


    bool AddPage(const wxString& id,
                 wxWindow* page,
                 const wxString& text,
                 const std::string& bmp_name = "",
                 bool bSelect = false)
    {
        DoInvalidateBestSize();
        return InsertPage(GetPageCount(), id, page, text, bmp_name, bSelect);
    }

    bool AddPage(wxWindow* page, const wxString& text, bool bSelect = false, int imageId = NO_IMAGE) override
    {
        DoInvalidateBestSize();
        return InsertPage(GetPageCount(), page, text, bSelect, imageId);
    }

    bool InsertPage(size_t n,
                    const wxString& id,
                    wxWindow * page,
                    const wxString & text,
                    const std::string& bmp_name = "",
                    bool bSelect = false,
                    const wxBitmap& bmp = wxNullBitmap)
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect))
            return false;

        m_pageNames.insert(m_pageNames.begin() + n, id);
        GetBtnsListCtrl()->InsertPage(n, text, bSelect, bmp_name, bmp);

        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return true;
    }

    virtual bool InsertPage(size_t n,
                            wxWindow * page,
                            const wxString & text,
                            bool bSelect = false,
                            int WXUNUSED(imageId) = NO_IMAGE) override
    {
        return InsertPage(n, wxString(), page, text, "", bSelect);
    }

    virtual int SetSelection(size_t n) override
    {
        int ret = DoSetSelection(n, SetSelection_SendEvent);
        int new_sel = GetSelection();
        if (new_sel != n) {
            return ret;
        }
        GetBtnsListCtrl()->SetSelection(n);

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

    wxString GetPageLabel(size_t n) const
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetBtnsListCtrl()->GetPageLabel(n);
    }

    std::string GetPageIcon(size_t n) const
    {
        wxCHECK_MSG(n < GetPageCount(), std::string(), wxS("Invalid page"));
        return GetBtnsListCtrl()->GetPageIcon(n);
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

    virtual bool DeleteAllPages() override
    {
        m_pageNames.clear();
        return wxBookCtrlBase::DeleteAllPages();
    }

    ButtonsListCtrl* GetBtnsListCtrl() const { return static_cast<ButtonsListCtrl*>(m_bookctrl); }
    void SetOverflowButton(wxWindow* button) { GetBtnsListCtrl()->SetOverflowButton(button); }

    size_t PositionAfter(std::initializer_list<const char*> ids) const
    {
        for (const char* id : ids)
            if (const int idx = FindPageByName(id); idx != wxNOT_FOUND)
                return static_cast<size_t>(idx) + 1;
        return GetPageCount();
    }

    int FindPageByName(const wxString& id) const
    {
        if (id.empty())
            return wxNOT_FOUND;
        for (size_t i = 0; i < m_pageNames.size(); ++i)
            if (m_pageNames[i] == id)
                return static_cast<int>(i);
        return wxNOT_FOUND;
    }

    wxWindow* GetPageByName(const wxString& id) const
    {
        const int idx = FindPageByName(id);
        return idx == wxNOT_FOUND ? nullptr : GetPage(static_cast<size_t>(idx));
    }

    bool SelectPageByName(const wxString& id)
    {
        const int idx = FindPageByName(id);
        if (idx == wxNOT_FOUND)
            return false;
        SetSelection(static_cast<size_t>(idx));
        return true;
    }

    wxString GetPageName(size_t n) const
    {
        return n < m_pageNames.size() ? m_pageNames[n] : wxString();
    }

    wxString GetSelectedPageName() const
    {
        const int sel = GetSelection();
        return sel < 0 ? wxString() : GetPageName(static_cast<size_t>(sel));
    }

    void UpdateMode()
    {
        GetBtnsListCtrl()->UpdateMode();
    }

    void Rescale()
    {
        GetBtnsListCtrl()->Rescale();
    }

    void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            AdvanceSelection(event.GetDirection());
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
            m_pageNames.erase(m_pageNames.begin() + page);
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
    void Init();

    std::vector<wxString> m_pageNames;

    wxShowEffect m_showEffect,
                 m_hideEffect;

    unsigned m_showTimeout,
             m_hideTimeout;
};
#endif
