#include <wx/gdicmn.h>
#include <wx/scrolwin.h>
#include <cstddef>
#include <wx/panel.h>
#include <wx/event.h>
#include <wx/colour.h>
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/splitter.h>
#include "ScrolledWindow.hpp"
#include "Scrollbar.hpp"

ScrolledWindow::ScrolledWindow(wxWindow *parent, wxWindowID id, wxPoint position, wxSize size, long style, int marginWidth, int scrollbarWidth, int tipLength)
    : wxScrolled<wxWindow>(parent, id, position, size, style)
{
    bool bVertical   = (style & wxVSCROLL) != 0;
    bool bHorizontal = (style & wxHSCROLL) != 0;
    m_bothDirections = bVertical & bHorizontal;
    EnableScrolling(bHorizontal, bVertical);
    ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);

    m_rightScrollbar     = NULL;
    m_bottomScrollbar    = NULL;
    m_verticalSplitter   = NULL;
    m_horizontalSplitter = NULL;
    m_userPanel          = NULL;
    m_scroll_win         = NULL;

    m_marginWidth        = marginWidth;

    wxSize hsSize = size;
    hsSize.SetWidth(hsSize.GetWidth() - marginWidth);
    hsSize.SetHeight(hsSize.GetHeight() - marginWidth);

    wxSize vsSize = size;
    vsSize.SetWidth(vsSize.GetWidth() - marginWidth);


    if (bVertical) {
        m_verticalSplitter = new wxWindow(this, -1, position, vsSize);
        m_userPanel        = new wxPanel(m_verticalSplitter, -1, wxPoint(0, 0), wxSize(size.GetWidth() - marginWidth, size.GetHeight()));
        m_scroll_win      = new wxWindow(m_verticalSplitter, -1, wxPoint(size.GetWidth() - marginWidth, 0), wxSize(marginWidth, size.GetHeight()));
        m_rightScrollbar   = new MyScrollbar(m_scroll_win, -1, wxPoint(0, 0), wxSize(scrollbarWidth, size.GetHeight()), this, wxVSCROLL, scrollbarWidth, tipLength);
    } else if (bHorizontal) {
        m_horizontalSplitter = new wxSplitterWindow(this, -1, position, hsSize);
        m_userPanel          = new wxPanel(m_horizontalSplitter, -1, wxPoint(0, 0), wxSize(size.GetWidth() - marginWidth, size.GetHeight() - marginWidth));
        m_userPanel->SetBackgroundColour(parent->GetBackgroundColour());

        m_bottomScrollbar = new MyScrollbar(m_horizontalSplitter, -1, wxPoint(0, 0), wxSize(size.GetWidth() - marginWidth, marginWidth), this, wxHSCROLL, scrollbarWidth,
                                            tipLength);
        m_horizontalSplitter->SplitHorizontally(m_userPanel, m_bottomScrollbar, -marginWidth);
        m_horizontalSplitter->SetSashInvisible();
    }

    SetTargetWindow(m_userPanel);

    Bind(wxEVT_SIZE, &ScrolledWindow::OnSize, this);
    Bind(wxEVT_SCROLLWIN_TOP, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_BOTTOM, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_LINEUP, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_LINEDOWN, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_PAGEUP, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_PAGEDOWN, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_THUMBTRACK, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &ScrolledWindow::OnScroll, this);
    Bind(wxEVT_MOUSEWHEEL, &ScrolledWindow::OnMouseWheel, this);
}

void ScrolledWindow::OnMouseWheel(wxMouseEvent &event)
{
    m_rightScrollbar->GetEventHandler()->ProcessEvent(event);

}

void ScrolledWindow::SetTipColor(wxColour color)
{
    if (m_rightScrollbar) m_rightScrollbar->SetTipColor(color);
    if (m_bottomScrollbar) m_bottomScrollbar->SetTipColor(color);
}

bool ScrolledWindow::SetBackgroundColour(const wxColour &color)
{
    const bool result = wxWindow::SetBackgroundColour(color);
    m_verticalSplitter->SetBackgroundColour(color); 
    m_userPanel->SetBackgroundColour(color);
    m_scroll_win->SetBackgroundColour(color);
    return result;
}

void ScrolledWindow::SetMarginColor(wxColour color)
{
    if (m_rightScrollbar) m_rightScrollbar->SetMarginColor(color);
    if (m_bottomScrollbar) m_bottomScrollbar->SetMarginColor(color);
}

void ScrolledWindow::SetScrollbarColor(wxColour color)
{
    if (m_rightScrollbar) m_rightScrollbar->SetScrollbarColor(color);
    if (m_bottomScrollbar) m_bottomScrollbar->SetScrollbarColor(color);
}

void ScrolledWindow::SetScrollbarTip(int len)
{
    if (m_rightScrollbar) m_rightScrollbar->SetScrollbarTip(len);
    if (m_bottomScrollbar) m_bottomScrollbar->SetScrollbarTip(len);
}

void ScrolledWindow::SetVirtualSize(int x, int y) { SetScrollbars(1, 1, x, y); }

void ScrolledWindow::SetVirtualSize(wxSize &size) { SetScrollbars(1, 1, size.GetWidth(), size.GetHeight()); }

void ScrolledWindow::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY, int noUnitsX, int noUnitsY, int xPos, int yPos, bool noRefresh)
{
    wxScrolled<wxWindow>::SetScrollbars(pixelsPerUnitX, pixelsPerUnitY, noUnitsX, noUnitsY, xPos, yPos, noRefresh);
    wxScrolled<wxWindow>::SetVirtualSize(pixelsPerUnitX * noUnitsX, pixelsPerUnitY * noUnitsY);
    if (m_rightScrollbar) m_rightScrollbar->SetVirtualDim(pixelsPerUnitY, noUnitsY);
    if (m_bottomScrollbar) m_bottomScrollbar->SetVirtualDim(pixelsPerUnitX, noUnitsX);
}

void ScrolledWindow::OnSize(wxSizeEvent &event)
{
    int startX, startY, virtX, virtY, clientW, clientH;

    if (!m_verticalSplitter && !m_horizontalSplitter) return;

    GetViewStart(&startX, &startY);
    GetVirtualSize(&virtX, &virtY);
    GetClientSize(&clientW, &clientH);

    if (m_verticalSplitter) m_verticalSplitter->SetSize(clientW, clientH);
    if (m_horizontalSplitter) m_horizontalSplitter->SetSize(clientW, clientH);

    if (m_rightScrollbar) {
        if (clientH >= virtY)
        {
            m_userPanel->SetSize(GetClientSize());
            m_rightScrollbar->SetSize(0, clientH);
            m_userPanel->Refresh();
            m_userPanel->Update();
        } else {
        }
        m_rightScrollbar->SetViewStart(startY);
        m_rightScrollbar->Refresh();
        m_rightScrollbar->Update();
    }
    if (m_bottomScrollbar) {
        if (clientW >= virtX)
        {
            m_horizontalSplitter->SetSashPosition(clientH);
            if (m_rightScrollbar)
                m_userPanel->SetSize(clientW - m_marginWidth, clientH);
            else
                m_userPanel->SetSize(GetClientSize());

            m_bottomScrollbar->SetSize(clientW, 0);
            m_userPanel->Refresh();
            m_userPanel->Update();
        } else {
            m_horizontalSplitter->SetSashPosition(clientH - m_marginWidth);
        }
        m_bottomScrollbar->SetViewStart(startX);
        m_bottomScrollbar->Refresh();
        m_bottomScrollbar->Update();
    }
    Layout();
    AdjustScrollbars();
}

void ScrolledWindow::OnScroll(wxScrollWinEvent &event)
{
    int startX, startY;
    GetViewStart(&startX, &startY);
    if (m_rightScrollbar) {
        m_rightScrollbar->SetViewStart(startY);
        m_rightScrollbar->Refresh();
        m_rightScrollbar->Update();
    }
    if (m_bottomScrollbar) {
        m_bottomScrollbar->SetViewStart(startX);
        m_bottomScrollbar->Refresh();
        m_bottomScrollbar->Update();
    }
}
