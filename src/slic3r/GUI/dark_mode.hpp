#pragma once

#include <Windows.h>

namespace NppDarkMode
{
	bool IsEnabled();
	bool IsSupported();
	bool IsSystemMenuEnabled();

	COLORREF InvertLightness(COLORREF c);
	COLORREF InvertLightnessSofter(COLORREF c);

	COLORREF GetBackgroundColor();
	COLORREF GetSofterBackgroundColor();
	COLORREF GetTextColor();
	COLORREF GetDarkerTextColor();
	COLORREF GetEdgeColor();

	HBRUSH GetBackgroundBrush();
	HBRUSH GetSofterBackgroundBrush();

	bool OnSettingChange(HWND hwnd, LPARAM lParam);
	bool UAHWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* lr);

    void DrawUAHMenuNCBottomLine(HWND hWnd);

	void InitDarkMode(bool set_dark_mode, bool set_sys_menu);
	void SetDarkMode(bool set_dark_mode);
	void SetSystemMenuForApp(bool set_sys_menu);
	void AllowDarkModeForApp(bool allow);
	bool AllowDarkModeForWindow(HWND hWnd, bool allow);
	void RefreshTitleBarThemeColor(HWND hWnd);

	void EnableDarkScrollBarForWindowAndChildren(HWND hwnd);

    void SetDarkTitleBar(HWND hwnd);
    void SetDarkExplorerTheme(HWND hwnd);
    void SetDarkListView(HWND hwnd);
    void SetDarkListViewHeader(HWND hwnd);
    void AutoSubclassAndThemeChildControls(HWND hwndParent, bool subclass = true, bool theme = true);

}
