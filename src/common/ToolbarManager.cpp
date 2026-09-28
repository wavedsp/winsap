/************************************************************************
 *
 *  WinSap C++ Version - Toolbar Manager Implementation
 *
 *  FILE:      ToolbarManager.cpp
 *
 *  PURPOSE: Implementation of Toolbar Manager
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#include "ToolbarManager.h"
#include "Resource.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
constexpr int kToolbarIconSize = 24;

struct ToolbarButtonSpec {
    int commandId;
    int iconResourceId;
};

constexpr std::array<ToolbarButtonSpec, 18> kToolbarButtons = {{
    {ID_FILE_OPENUP, IDI_TOOLBAR_OPEN_UP},
    {ID_FILE_OPENDOWN, IDI_TOOLBAR_OPEN_DOWN},
    {ID_FILE_SAVEAS, IDI_TOOLBAR_SAVE_AS},
    {0, 0},
    {ID_TOOLS_FASTBACKWARD, IDI_TOOLBAR_FAST_BACKWARD},
    {ID_TOOLS_MOVEFRAMEBACKWARD, IDI_TOOLBAR_MOVE_BACKWARD},
    {ID_TOOLS_MOVEFRAMEFORWARD, IDI_TOOLBAR_MOVE_FORWARD},
    {ID_TOOLS_FASTFORWARD, IDI_TOOLBAR_FAST_FORWARD},
    {ID_TOOLS_VIEWALL, IDI_TOOLBAR_VIEW_ALL},
    {0, 0},
    {ID_TOOLS_ZOOMIN, IDI_TOOLBAR_ZOOM_IN},
    {ID_TOOLS_ZOOMOUT, IDI_TOOLBAR_ZOOM_OUT},
    {0, 0},
    {ID_PLAYRECORD_PLAYALL, IDI_TOOLBAR_PLAY_ALL},
    {ID_PLAYRECORD_PLAYUP, IDI_TOOLBAR_PLAY_UP},
    {ID_PLAYRECORD_PLAYDOWN, IDI_TOOLBAR_PLAY_DOWN},
    {0, 0},
    {IDM_ABOUT, IDI_TOOLBAR_ABOUT}
}};
}

// ===================== Constructor =======================
ToolbarManager::ToolbarManager()
    : m_hInst(nullptr)
    , m_hwndParent(nullptr)
    , m_hwndStatus(nullptr)
    , m_imageList(nullptr)
    , m_dpiScale(1.0f)
{
    for (int i = 0; i < MAX_TOOLBARS; i++) {
        m_toolbars[i].hwndToolbar = nullptr;
        m_toolbars[i].uImageWidth = kToolbarIconSize;
        m_toolbars[i].uImageHeight = kToolbarIconSize;
        m_toolbars[i].uButtonWidth = kToolbarIconSize + 7;
        m_toolbars[i].uButtonHeight = kToolbarIconSize + 7;
        m_toolbars[i].uButtonNum = 0;
    }
}

// ===================== Destructor =======================
ToolbarManager::~ToolbarManager()
{
    Cleanup();
}

// ===================== Initialize =======================
bool ToolbarManager::Initialize(HWND hwndParent, HINSTANCE hInst)
{
    m_hInst = hInst;

    if (!CreateToolbar(hwndParent)) {
        return false;
    }

    return true;
}

// ===================== Cleanup =======================
void ToolbarManager::Cleanup()
{
    if (m_imageList) {
        ImageList_Destroy(m_imageList);
        m_imageList = nullptr;
    }
    for (HBITMAP bitmap : m_menuBitmaps) {
        DeleteObject(bitmap);
    }
    m_menuBitmaps.clear();

    m_hwndParent = nullptr;
    m_hwndStatus = nullptr;
}

// ===================== Create Toolbar =======================
bool ToolbarManager::CreateToolbar(HWND hwndParent)
{
    m_hwndParent = hwndParent;

    HWND hwndToolbar = CreateWindowEx(
        0,
        TOOLBARCLASSNAME,
        L"",
        WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT,
        0, 0, 0, 0,
        hwndParent,
        nullptr,
        m_hInst,
        nullptr);

    if (!hwndToolbar) {
        return false;
    }

    m_toolbars[0].hwndToolbar = hwndToolbar;
    SendMessage(hwndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    const int imageSize = (std::max)(16, static_cast<int>(std::lround(kToolbarIconSize * m_dpiScale)));
    const int buttonPadding = (std::max)(7, static_cast<int>(std::lround(7.0f * m_dpiScale)));
    const int buttonSize = imageSize + buttonPadding;

    m_toolbars[0].uImageWidth = imageSize;
    m_toolbars[0].uImageHeight = imageSize;
    m_toolbars[0].uButtonWidth = buttonSize;
    m_toolbars[0].uButtonHeight = buttonSize;

    if (m_imageList) {
        ImageList_Destroy(m_imageList);
    }
    m_imageList = ImageList_Create(imageSize, imageSize, ILC_COLOR32 | ILC_MASK, 14, 1);
    if (!m_imageList) {
        DestroyWindow(hwndToolbar);
        m_toolbars[0].hwndToolbar = nullptr;
        return false;
    }
    ImageList_SetBkColor(m_imageList, CLR_NONE);

    int imageIndex = 0;
    int buttonIndex = 0;
    for (const ToolbarButtonSpec& spec : kToolbarButtons) {
        TBBUTTON button{};
        button.fsState = TBSTATE_ENABLED;
        button.iString = -1;

        if (spec.commandId == 0) {
            button.fsStyle = TBSTYLE_SEP;
            button.iBitmap = buttonPadding;
        } else {
            HICON icon = LoadIconForDpi(spec.iconResourceId, imageSize);
            if (!icon || ImageList_AddIcon(m_imageList, icon) == -1) {
                if (icon) {
                    DestroyIcon(icon);
                }
                DestroyWindow(hwndToolbar);
                m_toolbars[0].hwndToolbar = nullptr;
                ImageList_Destroy(m_imageList);
                m_imageList = nullptr;
                return false;
            }
            DestroyIcon(icon);

            button.fsStyle = TBSTYLE_BUTTON;
            button.idCommand = spec.commandId;
            button.iBitmap = imageIndex++;
        }

        SendMessage(hwndToolbar, TB_INSERTBUTTON, buttonIndex++, reinterpret_cast<LPARAM>(&button));
    }

    SendMessage(hwndToolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(m_imageList));
    SendMessage(hwndToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(imageSize, imageSize));
    SendMessage(hwndToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(buttonSize, buttonSize));

    m_toolbars[0].uButtonNum = buttonIndex;
    SendMessage(hwndToolbar, TB_AUTOSIZE, 0, 0);
    MoveToolbar(hwndParent);
    ShowWindow(hwndToolbar, SW_SHOW);
    UpdateWindow(hwndToolbar);
    return true;
}

HICON ToolbarManager::LoadIconForDpi(int resourceId, int size) const
{
    return static_cast<HICON>(LoadImageW(
        m_hInst,
        MAKEINTRESOURCEW(resourceId),
        IMAGE_ICON,
        size,
        size,
        LR_DEFAULTCOLOR));
}

HBITMAP ToolbarManager::CreateMenuBitmap(int resourceId, int size) const
{
    HICON icon = LoadIconForDpi(resourceId, size);
    if (!icon) {
        return nullptr;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap) {
        HDC bitmapDc = CreateCompatibleDC(screenDc);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(bitmapDc, bitmap));
        PatBlt(bitmapDc, 0, 0, size, size, BLACKNESS);
        DrawIconEx(bitmapDc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(bitmapDc, oldBitmap);
        DeleteDC(bitmapDc);
    }
    ReleaseDC(nullptr, screenDc);
    DestroyIcon(icon);
    return bitmap;
}

void ToolbarManager::ApplyMenuIcons(HMENU menu)
{
    if (!menu || !m_hInst) {
        return;
    }

    const int iconSize = (std::max)(16, static_cast<int>(std::lround(16.0f * m_dpiScale)));
    std::vector<HBITMAP> newBitmaps;

    for (const ToolbarButtonSpec& spec : kToolbarButtons) {
        if (spec.commandId == 0 || GetMenuState(menu, spec.commandId, MF_BYCOMMAND) == static_cast<UINT>(-1)) {
            continue;
        }

        HBITMAP bitmap = CreateMenuBitmap(spec.iconResourceId, iconSize);
        if (!bitmap) {
            continue;
        }

        MENUITEMINFO itemInfo{};
        itemInfo.cbSize = sizeof(itemInfo);
        itemInfo.fMask = MIIM_BITMAP;
        itemInfo.hbmpItem = bitmap;
        if (SetMenuItemInfo(menu, spec.commandId, FALSE, &itemInfo)) {
            newBitmaps.push_back(bitmap);
        } else {
            DeleteObject(bitmap);
        }
    }

    for (HBITMAP bitmap : m_menuBitmaps) {
        DeleteObject(bitmap);
    }
    m_menuBitmaps = std::move(newBitmaps);
    DrawMenuBar(m_hwndParent);
}

// ===================== Move Toolbar =======================
void ToolbarManager::MoveToolbar(HWND hwndParent)
{
    RECT rc;
    GetClientRect(hwndParent, &rc);

    // Only one combined toolbar now
    if (m_toolbars[0].hwndToolbar && IsWindow(m_toolbars[0].hwndToolbar)) {
        // Position toolbar at the top, full width
        SendMessage(m_toolbars[0].hwndToolbar, TB_AUTOSIZE, 0, 0);

        RECT rcToolbar;
        GetWindowRect(m_toolbars[0].hwndToolbar, &rcToolbar);
        int height = rcToolbar.bottom - rcToolbar.top;

        MoveWindow(m_toolbars[0].hwndToolbar, 0, 0, rc.right - rc.left, height, TRUE);

        InvalidateRect(m_toolbars[0].hwndToolbar, nullptr, TRUE);
        UpdateWindow(m_toolbars[0].hwndToolbar);
    }
}

// ===================== Create Statusline =======================
HWND ToolbarManager::CreateStatusline(HWND hwndParent)
{
    m_hwndStatus = CreateStatusWindow(
        WS_CHILD | WS_VISIBLE,
        L"",
        hwndParent,
        IDC_STATUSBAR
    );

    if (m_hwndStatus) {
        // Scale status bar part widths for DPI
        // Base widths at 96 DPI: coord(190), mark up(100), mark down(100),
        // raw rate(100), wav rate(100), rest(-1)
        // The coordinate pane has to hold a spectrogram readout, which is the
        // longest of the three: " (00:00:01.234, 21094 Hz)".
        int baseWidths[STATUSBAR_POSITION_COUNT] = {190, 290, 390, 490, 590, -1};
        int iPanesPosition[STATUSBAR_POSITION_COUNT];
        for (int i = 0; i < STATUSBAR_POSITION_COUNT; i++) {
            if (baseWidths[i] == -1) {
                iPanesPosition[i] = -1;  // Last part extends to edge
            } else {
                iPanesPosition[i] = static_cast<int>(baseWidths[i] * m_dpiScale);
            }
        }
        SendMessage(m_hwndStatus, SB_SETPARTS, STATUSBAR_POSITION_COUNT, (LPARAM)iPanesPosition);
    }

    return m_hwndStatus;
}

// ===================== Update Statusline =======================
void ToolbarManager::UpdateStatusline(const WCHAR* text, int pane)
{
    if (m_hwndStatus) {
        SendMessage(m_hwndStatus, SB_SETTEXT, pane, (LPARAM)text);
    }
}

// ===================== Set Statusline Owner Draw =======================
// Hands a pane over to the parent's WM_DRAWITEM so it can be drawn greyed
// or in red. The pane keeps no text of its own once this is done.
void ToolbarManager::SetStatuslineOwnerDraw(int pane)
{
    if (m_hwndStatus) {
        SendMessage(m_hwndStatus, SB_SETTEXT, pane | SBT_OWNERDRAW, 0);
    }
}

// ===================== Move Statusline =======================
void ToolbarManager::MoveStatusline(HWND hwndParent)
{
    if (m_hwndStatus && IsWindow(m_hwndStatus)) {
        SendMessage(m_hwndStatus, WM_SIZE, 0, 0);
    }
}

// ===================== Handle Notify =======================
LRESULT ToolbarManager::HandleNotify(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    LPNMTTDISPINFO lpNM = (LPNMTTDISPINFO)lParam;

    switch (lpNM->hdr.code) {
    case TTN_GETDISPINFO:
        lpNM->hinst = m_hInst;
        lpNM->lpszText = MAKEINTRESOURCE(lpNM->hdr.idFrom);
        break;
    }

    return FALSE;
}

// ===================== Handle System Color Change =======================
void ToolbarManager::HandleSysColorChange()
{
    if (m_toolbars[0].hwndToolbar) {
        InvalidateRect(m_toolbars[0].hwndToolbar, nullptr, TRUE);
    }
}

// ===================== Show Toolbar =======================
void ToolbarManager::ShowToolbar(bool show)
{
    // Only one combined toolbar now
    if (m_toolbars[0].hwndToolbar && IsWindow(m_toolbars[0].hwndToolbar)) {
        ShowWindow(m_toolbars[0].hwndToolbar, show ? SW_SHOW : SW_HIDE);
    }
}

// ===================== Enable Button =======================
void ToolbarManager::EnableButton(int buttonId, bool enable)
{
    // Only one combined toolbar now
    if (m_toolbars[0].hwndToolbar) {
        SendMessage(m_toolbars[0].hwndToolbar, TB_ENABLEBUTTON,
            buttonId, MAKELONG(enable ? TRUE : FALSE, 0));
    }
}

// ===================== Check Button =======================
void ToolbarManager::CheckButton(int buttonId, bool check)
{
    // Only one combined toolbar now
    if (m_toolbars[0].hwndToolbar) {
        SendMessage(m_toolbars[0].hwndToolbar, TB_CHECKBUTTON,
            buttonId, MAKELONG(check ? TRUE : FALSE, 0));
    }
}

// ===================== Get Toolbar Handle =======================
HWND ToolbarManager::GetToolbarHandle(int index) const
{
    if (index >= 0 && index < MAX_TOOLBARS) {
        return m_toolbars[index].hwndToolbar;
    }
    return nullptr;
}

// ===================== Update Toolbar =======================
void ToolbarManager::UpdateToolbar()
{
    // Only one combined toolbar now
    if (m_toolbars[0].hwndToolbar && IsWindow(m_toolbars[0].hwndToolbar)) {
        InvalidateRect(m_toolbars[0].hwndToolbar, nullptr, TRUE);
        UpdateWindow(m_toolbars[0].hwndToolbar);
    }
}

// ===================== Recreate Toolbar =======================
void ToolbarManager::RecreateToolbar(HWND hwndParent)
{
    if (m_toolbars[0].hwndToolbar && IsWindow(m_toolbars[0].hwndToolbar)) {
        DestroyWindow(m_toolbars[0].hwndToolbar);
        m_toolbars[0].hwndToolbar = nullptr;
    }
    if (m_imageList) {
        ImageList_Destroy(m_imageList);
        m_imageList = nullptr;
    }
    CreateToolbar(hwndParent);
}
