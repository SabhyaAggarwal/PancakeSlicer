#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/event.h>
#include <wx/dcclient.h>
#include <wx/colour.h>
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "Scrollbar.hpp"
#include "ScrolledWindow.hpp"

MyScrollbar::MyScrollbar(wxWindow *parent, wxWindowID id, wxPoint position, wxSize size, ScrolledWindow* scrolledWindow, long direction, int scrollbarWidth, int tipLength) : wxPanel(parent, id, position, size)
{
	m_parent = parent;
	m_direction = direction;
	m_tipLength = tipLength;
	if (m_direction == wxVSCROLL)
		m_virtualDim = size.GetHeight();
	else
		m_virtualDim = size.GetWidth();

	m_actualDim = GetSize().GetHeight();

	m_viewStartInScrollUnits = 0;
	m_viewStartInPixels = 0;
	m_minSliderSize = 30;

	m_scrollbarWidth = scrollbarWidth;
    m_marginWidth    = scrollbarWidth;

	m_tipColor = m_parent->GetBackgroundColour();
	m_marginColor = m_parent->GetBackgroundColour();
	m_scrollbarColor = wxColour(255,255,255);
	m_scrolledWindow = scrolledWindow;

	Bind(wxEVT_PAINT, &MyScrollbar::OnPaint, this);
	Bind(wxEVT_SIZE, &MyScrollbar::OnSize, this);
	Bind(wxEVT_ERASE_BACKGROUND, &MyScrollbar::OnEraseBackground, this);
	Bind(wxEVT_LEFT_DOWN, &MyScrollbar::OnMouseLeftDown, this);
	Bind(wxEVT_LEFT_UP, &MyScrollbar::OnMouseLeftUp, this);
	Bind(wxEVT_MOTION, &MyScrollbar::OnMouseMove, this);
	Bind(wxEVT_MOUSEWHEEL, &MyScrollbar::OnMouseWheel, this);
}

void MyScrollbar::SetViewStart(int start)
{
	m_viewStartInScrollUnits = start;
    m_viewStartInPixels      = start;
}

void MyScrollbar::SetTipColor(wxColour color)
{
	m_tipColor = color;
}

void MyScrollbar::SetMarginColor(wxColour color)
{
	m_marginColor = color;
}

void MyScrollbar::SetScrollbarColor(wxColour color)
{
	m_scrollbarColor = color;
}

void MyScrollbar::SetScrollbarTip(int len)
{
	m_tipLength = len ;
}

void MyScrollbar::SetVirtualDim(int pixelsPerUnit, int noUnits)
{
	m_virtualDim = pixelsPerUnit* noUnits;
	m_pixelsPerUnit = pixelsPerUnit;
	Refresh();
}

void MyScrollbar::OnSize(wxSizeEvent& WXUNUSED(event))
{
	if (m_direction == wxVSCROLL)
	{
		m_actualDim = GetSize().GetHeight();
		if (m_actualDim < m_virtualDim)
			m_marginWidth = GetSize().GetWidth(); 
		else
			m_marginWidth = 0;
	}
	else
	{
		m_actualDim = GetSize().GetWidth();
		if (m_actualDim < m_virtualDim)
			m_marginWidth = GetSize().GetHeight();
		else
			m_marginWidth = 0;
	}
}

void MyScrollbar::OnPaint(wxPaintEvent& event)
{
	int scrollbarStart, scrollbarLength, scrollbarSide;
	wxClientDC dc(this);
	PrepareDC(dc);
	int actualDim = m_actualDim;

	if (m_direction == wxVSCROLL && actualDim < m_virtualDim)
	{
		scrollbarSide = (m_marginWidth - m_scrollbarWidth) / 2;
		scrollbarStart = (m_pixelsPerUnit*actualDim*m_viewStartInScrollUnits) / (float)m_virtualDim + 0.5;
		scrollbarLength = (actualDim*actualDim) / (float)m_virtualDim + 0.5;

		if (scrollbarLength < m_minSliderSize)
		{
			scrollbarStart = (actualDim - m_minSliderSize)*(m_viewStartInScrollUnits*m_pixelsPerUnit / (float)(m_virtualDim - actualDim)) + 0.5;
			scrollbarLength = m_minSliderSize;
		}

		if (scrollbarStart < 0)
			scrollbarStart = 0;
		else if (scrollbarStart + scrollbarLength >= actualDim)
			scrollbarStart = actualDim - scrollbarLength;

		dc.SetPen(m_tipColor);
		dc.SetBrush(m_tipColor);
		dc.DrawRectangle(scrollbarSide, 0, m_scrollbarWidth, m_tipLength);
		dc.DrawRectangle(scrollbarSide, actualDim - m_tipLength, m_scrollbarWidth, m_tipLength);
		dc.SetPen(m_marginColor);
		dc.SetBrush(m_marginColor);
		dc.DrawRectangle(scrollbarSide, m_tipLength, m_scrollbarWidth, scrollbarStart + m_tipLength);
		dc.DrawRectangle(scrollbarSide, scrollbarStart + scrollbarLength - m_tipLength, m_scrollbarWidth, actualDim - scrollbarLength - scrollbarStart);
		dc.DrawRectangle(0, 0, scrollbarSide, actualDim);
		dc.DrawRectangle(m_marginWidth - scrollbarSide, 0, scrollbarSide, actualDim);
		dc.SetPen(m_scrollbarColor);
		dc.SetBrush(m_scrollbarColor);
        dc.DrawRectangle(scrollbarSide, scrollbarStart + m_tipLength, m_scrollbarWidth, scrollbarLength - 2 * m_tipLength);	 
	}
	else if (m_direction == wxHSCROLL && actualDim < m_virtualDim)
	{
		if (m_scrolledWindow->IsBothDirections())
			actualDim -= m_marginWidth;

		scrollbarSide = (m_marginWidth - m_scrollbarWidth) / 2;
		scrollbarStart = (m_pixelsPerUnit*actualDim*m_viewStartInScrollUnits) / (float)m_virtualDim + 0.5;
		scrollbarLength = (actualDim*actualDim) / (float)m_virtualDim + 0.5;

		if (scrollbarLength < m_minSliderSize)
		{
			scrollbarStart = (actualDim - m_minSliderSize)*(m_viewStartInScrollUnits*m_pixelsPerUnit / (float)(m_virtualDim - actualDim)) + 0.5;
			scrollbarLength = m_minSliderSize;
		}

		if (scrollbarStart < 0)
			scrollbarStart = 0;
		else if (scrollbarStart + scrollbarLength >= actualDim)
			scrollbarStart = actualDim - scrollbarLength;

		dc.SetPen(m_tipColor);
		dc.SetBrush(m_tipColor);
		dc.DrawRectangle(0, scrollbarSide, m_tipLength, m_scrollbarWidth);
		dc.DrawRectangle(actualDim - m_tipLength, scrollbarSide, m_tipLength, m_scrollbarWidth);
		dc.SetPen(m_marginColor);
		dc.SetBrush(m_marginColor);
		dc.DrawRectangle(m_tipLength, scrollbarSide, scrollbarStart + m_tipLength, m_scrollbarWidth);
		dc.DrawRectangle(scrollbarStart + scrollbarLength - m_tipLength, scrollbarSide, actualDim - scrollbarLength - scrollbarStart, m_scrollbarWidth);
		dc.DrawRectangle(0, 0, actualDim, scrollbarSide);
		dc.DrawRectangle(0, m_marginWidth - scrollbarSide, actualDim, scrollbarSide);
		dc.DrawRectangle(actualDim, 0, m_marginWidth, m_marginWidth);
		dc.SetPen(m_scrollbarColor);
		dc.SetBrush(m_scrollbarColor);
		dc.DrawRectangle(scrollbarStart + m_tipLength, scrollbarSide, scrollbarLength - 2 * m_tipLength, m_scrollbarWidth);
	}
}

void MyScrollbar::OnEraseBackground(wxEraseEvent & event)
{
    SetBackgroundColour(wxColor(255,255,255));
}

void MyScrollbar::OnMouseLeftDown(wxMouseEvent &event)
{
	int scrollbarStart, scrollbarLength, eventPos ;

	if (m_direction == wxVSCROLL)
		eventPos = event.m_y;
	else
		eventPos = event.m_x;

	int actualDim = m_actualDim;
	scrollbarStart = (m_pixelsPerUnit*actualDim*m_viewStartInScrollUnits) / (float)m_virtualDim + 0.5;
	scrollbarLength = MAX((actualDim*actualDim) / (float)m_virtualDim + 0.5, m_minSliderSize);

	m_previousMouse = eventPos;
	if (eventPos < scrollbarStart + scrollbarLength)
		m_mouseLocation = ON_SCROLLBAR;
	else
		m_mouseLocation = BEFORE_SCROLLBAR;

	CaptureMouse();
}

void MyScrollbar::OnMouseLeftUp(wxMouseEvent &event)
{
	if (HasCapture())
	{
		ReleaseMouse();
	}
	m_mouseLocation = NOWHERE;
}

void MyScrollbar::OnMouseMove(wxMouseEvent &event)
{ 
	int dMotion, actualDim; 
	if (!event.Dragging())
		return;

	actualDim = m_actualDim;

	if (m_direction == wxVSCROLL && m_mouseLocation == ON_SCROLLBAR)
	{
		dMotion = event.m_y - m_previousMouse;
		m_viewStartInPixels += (dMotion*m_virtualDim) / (float)actualDim + 0.5;

		if (m_viewStartInPixels < 0)
			m_viewStartInPixels = 0;
		else if (m_viewStartInPixels + actualDim >= m_virtualDim)
			m_viewStartInPixels = m_virtualDim - actualDim;

		m_viewStartInScrollUnits = m_viewStartInPixels / (float)m_pixelsPerUnit + 0.5;
		m_scrolledWindow->Scroll(-1, m_viewStartInScrollUnits);
		SetViewStart(m_viewStartInScrollUnits);
		m_previousMouse = event.m_y;
	}
	else if (m_direction == wxHSCROLL && m_mouseLocation == ON_SCROLLBAR)
	{
		if (m_scrolledWindow->IsBothDirections())	 
			actualDim -= m_marginWidth;

		dMotion = event.m_x - m_previousMouse;
		m_viewStartInPixels += (dMotion*m_virtualDim) / (float)actualDim + 0.5;

		if (m_viewStartInPixels < 0)
			m_viewStartInPixels = 0;
		else if (m_viewStartInPixels + actualDim >= m_virtualDim)
			m_viewStartInPixels = m_virtualDim - actualDim;

		m_viewStartInScrollUnits = m_viewStartInPixels / (float)m_pixelsPerUnit + 0.5;
		m_scrolledWindow->Scroll(m_viewStartInScrollUnits, -1);
		SetViewStart(m_viewStartInScrollUnits);
		m_previousMouse = event.m_x;
	}
	Refresh();
	Update();
}

void MyScrollbar::OnMouseWheel(wxMouseEvent &event)
{
	int dMotion, actualDim;
	if (event.GetWheelRotation() > 0)
        dMotion = -SCROLL_D_MOTION;
	else 
        dMotion = SCROLL_D_MOTION;
		
	actualDim = m_actualDim;

	if (m_direction == wxVSCROLL)
	{
		m_viewStartInPixels += (dMotion*m_virtualDim) / (float)actualDim + 0.5;

		if (m_viewStartInPixels < 0)
			m_viewStartInPixels = 0;
		else if (m_viewStartInPixels + actualDim >= m_virtualDim)
			m_viewStartInPixels = m_virtualDim - actualDim;

		m_viewStartInScrollUnits = m_viewStartInPixels / (float)m_pixelsPerUnit + 0.5;
		m_scrolledWindow->Scroll(-1, m_viewStartInScrollUnits);
		SetViewStart(m_viewStartInScrollUnits);
	}
	else
	{
		dMotion = -dMotion;
		if (m_scrolledWindow->IsBothDirections())
			actualDim -= m_marginWidth;

		m_viewStartInPixels += (dMotion*m_virtualDim) / (float)actualDim + 0.5;

		if (m_viewStartInPixels < 0)
			m_viewStartInPixels = 0;
		else if (m_viewStartInPixels + actualDim >= m_virtualDim)
			m_viewStartInPixels = m_virtualDim - actualDim;

		m_viewStartInScrollUnits = m_viewStartInPixels / (float)m_pixelsPerUnit + 0.5;
		m_scrolledWindow->Scroll(m_viewStartInScrollUnits, -1);
		SetViewStart(m_viewStartInScrollUnits);
	}
	Refresh();
	Update();
}
