/************************************************************************
 *
 *  WinSap C++ Version - Toolbar Manager Header
 *
 *  FILE:      ToolbarManager.h
 *
 *  PURPOSE: Manages toolbar creation and refresh
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include "Resource.h"

// Status bar position definitions
#define STATUSBAR_POSITION_COUNT    6
#define STATUSBAR_WAVEFORM_COORD    0
#define STATUSBAR_MARK_UP           1
#define STATUSBAR_MARK_DOWN         2
#define STATUSBAR_RAW_RATE          3   // Raw PCM rate, greyed when no raw stream
#define STATUSBAR_WAV_RATE          4   // Wav rate, red when the open files disagree
#define STATUSBAR_FILE_INFO         5   // Last stretchy part (extends to edge)

class ToolbarManager {
public:
    // Constructor and Destructor
    ToolbarManager();
    ~ToolbarManager();

    // Initialization
    bool Initialize(HWND hwndParent, HINSTANCE hInst);
    void Cleanup();

    // Toolbar operations
    bool CreateToolbar(HWND hwndParent);
    void UpdateToolbar();
    void ShowToolbar(bool show);
    void EnableButton(int buttonId, bool enable);
    void CheckButton(int buttonId, bool check);

    // Size and position
    void MoveToolbar(HWND hwndParent);
    HWND GetToolbarHandle(int index) const;

    // Statusline operations
    HWND CreateStatusline(HWND hwndParent);
    void UpdateStatusline(const WCHAR* text, int pane = 0);
    void SetStatuslineOwnerDraw(int pane);
    void MoveStatusline(HWND hwndParent);
    HWND GetStatuslineHandle() const { return m_hwndStatus; }

    // Notification handling
    LRESULT HandleNotify(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void HandleSysColorChange();
    void ApplyMenuIcons(HMENU menu);

    // DPI scaling
    void SetDpiScale(float scale) { m_dpiScale = scale; }
    float GetDpiScale() const { return m_dpiScale; }
    void RecreateToolbar(HWND hwndParent);

private:
    // Toolbar data structure
    struct ToolbarInfo {
        HWND    hwndToolbar;
        UINT    uImageWidth;
        UINT    uImageHeight;
        UINT    uButtonWidth;
        UINT    uButtonHeight;
        UINT    uButtonNum;
    };

    static const int MAX_TOOLBARS = 1;

    // Member variables
    HINSTANCE m_hInst;
    HWND m_hwndParent;
    HWND m_hwndStatus;
    HIMAGELIST m_imageList;
    std::vector<HBITMAP> m_menuBitmaps;
    ToolbarInfo m_toolbars[MAX_TOOLBARS];
    float m_dpiScale;

    // Helper methods
    HICON LoadIconForDpi(int resourceId, int size) const;
    HBITMAP CreateMenuBitmap(int resourceId, int size) const;
    void DisplayStatusMenuHelp(HWND hStatusWnd, UINT idMsg);
};
