/************************************************************************
 *
 *  WinSap C++ Version - Main Application Implementation
 *
 *  FILE:      WinsapApp.cpp
 *
 *  PURPOSE: Implementation of main application class
 *
 *  Update:   2025.4 Update the code base
 *            2026.1 Porting to C++
 *            2026.11 Bounded command line parsing, 
 *                    .par always opens as Parameter, 
 *                    growing backbuffer, raw rate reaches FFT tracks
 *
 ************************************************************************/

#include "WinsapApp.h"
#include "FileDialogs.h"
#include "Resource.h"
#include "Version.h"
#include <windowsx.h>
#include <commdlg.h>
#include "DrawContext.h"
#include "InputDialog.h"
#include "WinsapHelp.h"
#include "SpectrogramSettings.h"
#include "WindowPlacement.h"
#include <algorithm>

// ===================== Reload helpers =======================
// Reload All is best effort: a file that cannot be reopened keeps the copy
// already in memory rather than leaving an empty slot, so an external editor
// holding a file open no longer costs the user the data on screen.
static bool CanReadFile(const WCHAR* fileName)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, fileName, L"rb") != 0 || fp == nullptr) {
        return false;
    }
    fclose(fp);
    return true;
}

// Collects the names of the files that could not be reloaded so they can be
// reported once at the end instead of one message box per file.
static void AppendFailedFile(WCHAR* list, size_t listSize, int& count, const WCHAR* fileName)
{
    ++count;
    if (count > 12) {
        return;   // keep the message box a sensible size
    }
    if (list[0] != L'\0') {
        wcscat_s(list, listSize, L"\n");
    }
    wcscat_s(list, listSize, fileName);
}

static const WCHAR* CommandStatusText(int command)
{
    switch (command) {
    case IDM_ABOUT:                    return L"About WinSap.";
    case ID_FILE_OPENUP:               return L"Open Upper File.";
    case ID_FILE_OPENDOWN:             return L"Open Down File.";
    case ID_FILE_SAVEAS:               return L"Save marked block.";
    case ID_FILE_CLOSE:                return L"Close Files.";
    case ID_FILE_CLOSEALL:             return L"Closed all files.";
    case ID_FILE_RELOADALL:            return L"Reload All.";
    case ID_TOOLS_VIEWALL:             return L"Showing all frames.";
    case ID_TOOLS_MOVEFRAMEBACKWARD:   return L"Moved backward.";
    case ID_TOOLS_MOVEFRAMEFORWARD:    return L"Moved forward.";
    case ID_TOOLS_FASTBACKWARD:        return L"Moved backward by frame skip.";
    case ID_TOOLS_FASTFORWARD:         return L"Moved forward by frame skip.";
    case ID_TOOLS_ZOOMIN:              return L"Zoomed in.";
    case ID_TOOLS_ZOOMOUT:             return L"Zoomed out.";
    case ID_TOOLS_COMPRESS:            return L"Compressed amplitude.";
    case ID_TOOLS_EXPAND:              return L"Expanded amplitude.";
    case ID_TOOLS_LOCKSTARTFRAME:      return L"Toggled starting-frame lock.";
    case ID_UP_FRAMELENGTH:            return L"Set upper frame length.";
    case ID_DOWN_FRAMELENGTH:          return L"Set down frame length.";
    case ID_UP_GOTOFRAME:              return L"Set upper start frame.";
    case ID_DOWN_GOTOFRAME:            return L"Set down start frame.";
    case ID_UP_NUMBEROFFRAMES:         return L"Set upper visible frame count.";
    case ID_DOWN_NUMBEROFFRAMES:       return L"Set down visible frame count.";
    case ID_UP_PAGENUMBERS:            return L"Set upper frame skip.";
    case ID_DOWN_PAGENUMBERS:          return L"Set down frame skip.";
    case ID_UP_LIMIT:                  return L"Set upper amplitude limits.";
    case ID_DOWN_LIMIT:                return L"Set down amplitude limits.";
    case ID_TOOLS_ADDDOWNTOUP:         return L"Toggled lower waveform overlay.";
    case ID_TOOLS_LOOKMARKEDBLOCK:     return L"Showing marked block.";
    case ID_WINDOW_SYNCPANELS:         return L"Down panel synced to upper panel.";
    case ID_PLAYRECORD_PLAYALL:        return L"Play All.";
    case ID_PLAYRECORD_PLAYUP:         return L"Play Up.";
    case ID_PLAYRECORD_PLAYDOWN:       return L"Play Down.";
    case ID_STOP:                      return L"Playback stopped.";
    case ID_HELP:
    case ID_HELP_FINDER:
    case ID_HELP_INDEX:                return L"Opened Help Topics.";
    case ID_OPTIONS_CONFIGURE:         return L"Set raw PCM sampling rate.";
    case ID_OPTIONS_CONTRASTUP:        return L"Spectrogram Contrast Up.";
    case ID_OPTIONS_CONTRASTDOWN:      return L"Spectrogram Contrast Down.";
    case ID_OPTIONS_PALETTE:           return L"Changed spectrogram colour map.";
    case ID_OPTIONS_SAVESTATUS:        return L"Raw PCM sampling rate saved.";
    default:                           return nullptr;
    }
}

// Static member initialization
WinsapApp* WinsapApp::s_pApp = nullptr;

#define MAX_LOADSTRING 100

// About dialog procedure
INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
    {
        // Set version text from Version.h
        WCHAR versionText[64];
        swprintf_s(versionText, 64, L"Version %s", WINSAP_VERSION_STRING);
        SetDlgItemText(hwnd, IDC_VERSION_TEXT, versionText);

        // Get system message font for consistent appearance
        NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
        if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            HFONT hFont = CreateFontIndirect(&ncm.lfMessageFont);
            if (hFont) {
                // Apply font to all child controls
                HWND hwndChild = GetWindow(hwnd, GW_CHILD);
                while (hwndChild) {
                    SendMessage(hwndChild, WM_SETFONT, (WPARAM)hFont, TRUE);
                    hwndChild = GetWindow(hwndChild, GW_HWNDNEXT);
                }
                // Store font handle to delete later
                SetProp(hwnd, L"DialogFont", (HANDLE)hFont);
            }
        }

        // Set windows position to center on parent
        HWND owner = GetWindow(hwnd, GW_OWNER);
        if (owner) {
            RECT ownerRect{}, dialogRect{};
            GetWindowRect(owner, &ownerRect);
            GetWindowRect(hwnd, &dialogRect);

            const int width = dialogRect.right - dialogRect.left;
            const int height = dialogRect.bottom - dialogRect.top;
            const int ownerWidth = ownerRect.right - ownerRect.left;
            const int ownerHeight = ownerRect.bottom - ownerRect.top;

            int x = ownerRect.left + (ownerWidth - width) / 2;
            int y = ownerRect.top + (ownerHeight - height) / 2;

            // clamp to nearest monitor work area
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);

            if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
                const RECT& work = monitorInfo.rcWork;

                int minX = static_cast<int>(work.left);
                int minY = static_cast<int>(work.top);
                int maxX = static_cast<int>(work.right);
                int maxY = static_cast<int>(work.bottom);

                // 1. Calculate the maximum possible X/Y while keeping the window inside the work area
                int maxAllowedX = maxX - width;
                int maxAllowedY = maxY - height;

                // 2. If the window is too large on an axis, align that edge to the work area.
                if (maxAllowedX < minX) {
                    x = minX;
                } else if (x < minX) {
                    x = minX;
                } else if (x > maxAllowedX) {
                    x = maxAllowedX;
                }

                if (maxAllowedY < minY) {
                    y = minY;
                } else if (y < minY) {
                    y = minY;
                } else if (y > maxAllowedY) {
                    y = maxAllowedY;
                }
            }

            SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
                
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hwnd, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_DESTROY:
    {
        HFONT hFont = (HFONT)RemoveProp(hwnd, L"DialogFont");
        if (hFont) {
            DeleteObject(hFont);
        }
    }
    break;
    }
    return FALSE;
}

// DrawContext factory method
DrawContext DrawContext::CreateFrom(WinsapApp* app)
{
    if (!app) {
        // Return empty context if null app
        DrawContext ctx{};
        ctx.memDC = nullptr;
        ctx.framePen = nullptr;
        ctx.charHeight = 0;
        ctx.charWidth = 0;
        return ctx;
    }

    DrawContext ctx;
    ctx.memDC = app->GetMemDC();
    ctx.framePen = app->GetFramePen();
    ctx.redPen = app->GetRedPen();
    ctx.charHeight = app->GetCharHigh();
    ctx.charWidth = app->GetCharWidth();

    for (int i = 0; i < PANELMAXFILE; i++) {
        ctx.wavePens[i] = app->GetWavePen(i);
        ctx.waveColors[i] = app->GetWaveColor(i);
    }

    ctx.toolbarManager = app->GetToolbarManager();

    // Overlay defaults (will be set per-panel in Draw())
    ctx.addFile = false;
    ctx.overlayPanel = nullptr;

    return ctx;
}

// ===================== Constructor =======================
WinsapApp::WinsapApp()
    : m_hInstance(nullptr)
    , m_hwndMain(nullptr)
    , m_cmdLine(nullptr)
    , m_updateType(upNone)
    , m_addFile(false)
    // Start frames independent by default
    , m_lockStartFrame(false)
    , m_samplingRate(16000)
    , m_rawRateActive(false)
    , m_wavRateMixed(false)
    , m_memdc(nullptr)
    , m_hbit(nullptr)
    , m_hOldBitmap(nullptr)
    , m_hbrush(nullptr)
    , m_hOldpen(nullptr)
    , m_hCurpen(nullptr)
    , m_framePen(nullptr)
    , m_textPen(nullptr)
    , m_hRedPen(nullptr)
    , m_hBlackBrush(nullptr)
    , m_hGreyBrush(nullptr)
    , m_holdf(nullptr)
    , m_hnewf(nullptr)
    , m_charHigh(0)
    , m_charWidth(0)
    , m_dpi(96)
    , m_dpiScale(1.0f)
    , m_dwUpFilterIndex(1)
    , m_dwDownFilterIndex(1)
{
    s_pApp = this;

    m_rawRateText[0] = L'\0';
    m_wavRateText[0] = L'\0';

    // Initialize wave colors
    m_waveColor[0] = RGB(255, 255, 0);   // Yellow
    m_waveColor[1] = RGB(255, 0, 255);   // Magenta
    m_waveColor[2] = RGB(0, 255, 255);   // Cyan
    m_waveColor[3] = RGB(255, 255, 255); // White
    m_waveColor[4] = RGB(0, 255, 0);     // Green
    m_waveColor[5] = RGB(0, 0, 255);     // Blue

    // Initialize pens to nullptr
    for (int i = 0; i < PANELMAXFILE; i++) {
        m_wavePen[i] = nullptr;
    }

    // Initialize panels
    m_upperPanel.Initialize(true);
    m_lowerPanel.Initialize(false);

    // Initialize desktop bounds
    ZeroMemory(&m_desktopRect, sizeof(RECT));
    m_deskBound.a.x = m_deskBound.a.y = 0;
    m_deskBound.b.x = m_deskBound.b.y = 0;
}

// ===================== Destructor =======================
WinsapApp::~WinsapApp()
{
    Cleanup();
}

// ===================== Initialize =======================
bool WinsapApp::Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR lpCmdLine)
{
    m_hInstance = hInstance;
    m_cmdLine = lpCmdLine;

    // Initialize common controls
    INITCOMMONCONTROLSEX iccex;
    iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    iccex.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&iccex);
    InitCommonControls();

    // Load the raw PCM sampling rate. Nothing is written here: the value only
    // reaches the registry when the user picks Save Raw Samplingrate, so a
    // missing key or value just leaves the 16000 Hz default in place.
    HKEY hRegKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
            WINSAP_SETTINGS_REGISTRY_PATH,
            0, KEY_QUERY_VALUE, &hRegKey) == ERROR_SUCCESS) {
        DWORD rate = 0;
        DWORD datasize = sizeof(DWORD);
        DWORD datatype = 0;
        if (RegQueryValueEx(hRegKey, L"Rate", nullptr,
                &datatype, (LPBYTE)&rate, &datasize) == ERROR_SUCCESS
            && datatype == REG_DWORD && rate > 0) {
            m_samplingRate = rate;
        }
        RegCloseKey(hRegKey);
    }

    const winsap::SpectrogramDisplaySettings spectrogramSettings =
        winsap::LoadSpectrogramDisplaySettings();
    m_upperPanel.SetSpectrogramRangeDb(spectrogramSettings.rangeDb);
    m_lowerPanel.SetSpectrogramRangeDb(spectrogramSettings.rangeDb);
    m_upperPanel.SetSpectrogramPalette(spectrogramSettings.palette);
    m_lowerPanel.SetSpectrogramPalette(spectrogramSettings.palette);

    // Register window class
    if (!RegisterWindowClass()) {
        return false;
    }

    // Create main window
    if (!CreateMainWindow(nCmdShow)) {
        return false;
    }

    return true;
}

// ===================== Cleanup =======================
void WinsapApp::Cleanup()
{
    CleanupDrawing();
    s_pApp = nullptr;
}

// ===================== Register Window Class =======================
bool WinsapApp::RegisterWindowClass()
{
    WCHAR szWindowClass[MAX_LOADSTRING];
    WCHAR szTitle[MAX_LOADSTRING];

    LoadStringW(m_hInstance, IDC_WINSAP, szWindowClass, MAX_LOADSTRING);
    LoadStringW(m_hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING); // IDS_APP_TITLE

    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = 0;  // No CS_HREDRAW/CS_VREDRAW - we do manual invalidation
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_WINSAP)); // IDI_WINSAP
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WINSAP);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_SMALL)); // IDI_SMALL

    return RegisterClassExW(&wcex) != 0;
}

// ===================== Create Main Window =======================
bool WinsapApp::CreateMainWindow(int nCmdShow)
{
    WCHAR szWindowClass[MAX_LOADSTRING];
    WCHAR szTitle[MAX_LOADSTRING];

    LoadStringW(m_hInstance, IDC_WINSAP, szWindowClass, MAX_LOADSTRING);
    LoadStringW(m_hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

    const winsap::SavedWindowBounds bounds = winsap::LoadWindowBounds();

    m_hwndMain = CreateWindowW(szWindowClass, szTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        bounds.x, bounds.y, bounds.width, bounds.height,
        nullptr, nullptr, m_hInstance, nullptr);

    if (!m_hwndMain) {
        return false;
    }

    ShowWindow(m_hwndMain, nCmdShow);
    UpdateWindow(m_hwndMain);

    return true;
}

// ===================== Run =======================
int WinsapApp::Run()
{
    HACCEL hAccelTable = LoadAccelerators(m_hInstance, MAKEINTRESOURCE(IDC_WINSAP));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0)) {
        // The help window scrolls with the arrow and page keys, which are accelerators here.
        if (wavedsp::IsHelpMessage(msg) || !TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

// ===================== Window Procedure =======================
LRESULT CALLBACK WinsapApp::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (s_pApp == nullptr) {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE:
        s_pApp->OnCreate(hwnd);
        break;

    case WM_COMMAND:
        return s_pApp->OnCommand(hwnd, wParam, lParam);

    case WM_SIZE:
        s_pApp->OnSize(hwnd, wParam, lParam);
        break;

    case WM_ERASEBKGND:
        // Erase only the toolbar and status bar areas, not the panel area (double-buffered)
        if (s_pApp) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH hBrush = (HBRUSH)(COLOR_BTNFACE + 1);

            // Erase toolbar area at the top
            if (s_pApp->m_deskBound.a.y > 0) {
                RECT toolbarArea = rc;
                toolbarArea.bottom = s_pApp->m_deskBound.a.y;
                FillRect(hdc, &toolbarArea, hBrush);
            }

            // Erase status bar area at the bottom
            if (s_pApp->m_deskBound.b.y < rc.bottom && s_pApp->m_deskBound.b.y > 0) {
                RECT statusArea = rc;
                statusArea.top = s_pApp->m_deskBound.b.y;
                statusArea.bottom = rc.bottom;
                FillRect(hdc, &statusArea, hBrush);
            }

            // Don't erase the panel area - it's double-buffered
            return 1;
        }
        return DefWindowProc(hwnd, message, wParam, lParam);

    case WM_PAINT:
        s_pApp->OnPaint(hwnd);
        break;

    case WM_KEYDOWN:
        s_pApp->OnKeyDown(hwnd, wParam, lParam);
        break;

    case WM_LBUTTONDOWN:
        s_pApp->OnLButtonDown(hwnd, wParam, lParam);
        break;

    case WM_MOUSEMOVE:
        s_pApp->OnMouseMove(hwnd, wParam, lParam);
        break;

    case WM_RBUTTONDOWN:
        s_pApp->OnRButtonDown(hwnd, wParam, lParam);
        break;

    case WM_NOTIFY:
        return s_pApp->m_toolbarManager.HandleNotify(hwnd, wParam, lParam);

    case WM_SYSCOLORCHANGE:
        s_pApp->m_toolbarManager.HandleSysColorChange();
        break;

    case WM_DPICHANGED:
        s_pApp->OnDpiChanged(hwnd, wParam, lParam);
        break;

    case WM_DRAWITEM:
        s_pApp->OnDrawItem(hwnd, wParam, lParam);
        return TRUE;

    case WM_DESTROY:
        s_pApp->OnDestroy();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

// ===================== On Create =======================
void WinsapApp::OnCreate(HWND hwnd)
{
    // Initialize DPI first
    InitializeDpi(hwnd);

    // Store menu handle for CheckMenuItem operations
    m_hMenu = GetMenu(hwnd);

    // Windows hides the menu mnemonic underlines until the user presses Alt,
    // and which state a window starts in depends on whether the program was
    // last driven by mouse or keyboard. WinSap is a keyboard-driven program, so
    // ask for the underlines to be shown from the start and stay shown.
    SendMessage(hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL), 0);

    // Build toolbar and statusline with DPI-aware sizing
    m_toolbarManager.SetDpiScale(m_dpiScale);
    m_toolbarManager.Initialize(hwnd, m_hInstance);
    m_toolbarManager.ApplyMenuIcons(m_hMenu);
    HWND hStatus = m_toolbarManager.CreateStatusline(hwnd);

    // Initialize GDI objects FIRST (creates memdc, bitmap, font, pens, brushes)
    // This must be done before ResizeDesktop because we need m_charHigh and m_charWidth
    InitializeGDI(hwnd);

    // IMPORTANT: Size the toolbar BEFORE we calculate desktop bounds
    // This ensures GetWindowRect returns the correct toolbar height
    m_toolbarManager.MoveToolbar(hwnd);

    // Setup desktop bounds (needs m_charHigh and m_charWidth for waveBox calculation)
    GetClientRect(hwnd, &m_desktopRect);
    ResizeDesktop(hwnd);

    // Store in m_desktopRect for reference
    m_desktopRect.left = m_deskBound.a.x;
    m_desktopRect.top = m_deskBound.a.y;
    m_desktopRect.right = m_deskBound.b.x;
    m_desktopRect.bottom = m_deskBound.b.y;

    // Hand both rate panes to WM_DRAWITEM so they can be greyed or reddened
    if (hStatus) {
        m_toolbarManager.SetStatuslineOwnerDraw(STATUSBAR_RAW_RATE);
        m_toolbarManager.SetStatuslineOwnerDraw(STATUSBAR_WAV_RATE);
        UpdateRateStatus();
    }

    // Set lock for start frame
    m_lockStartFrame = true;

    // Process command line to open files if specified
    ProcessCommandLine(hwnd);
}

// ===================== On Destroy =======================
void WinsapApp::OnDestroy()
{
    winsap::SaveWindowBounds(m_hwndMain);

    Cleanup();
}

// ===================== On Size =======================
void WinsapApp::OnSize(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    EnsureBackBuffer(hwnd);

    // Resize toolbar
    m_toolbarManager.MoveToolbar(hwnd);

    // Resize statusline
    m_toolbarManager.MoveStatusline(hwnd);

    // Update desktop bounds
    ResizeDesktop(hwnd);

    // Explicitly update toolbar and status bar to ensure they repaint immediately
    HWND hwndToolbar = m_toolbarManager.GetToolbarHandle(0);
    if (hwndToolbar) {
        InvalidateRect(hwndToolbar, nullptr, TRUE);
        UpdateWindow(hwndToolbar);
    }
    HWND hwndStatus = m_toolbarManager.GetStatuslineHandle();
    if (hwndStatus) {
        InvalidateRect(hwndStatus, nullptr, TRUE);
        UpdateWindow(hwndStatus);
    }

    // Trigger redraw
    m_updateType = upAll;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ===================== On Paint =======================
void WinsapApp::OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Draw to memory DC
    Draw(hwnd);

    // Copy to screen - start BELOW the toolbar area
    // Don't draw over the toolbar and status bar
    int drawY = m_deskBound.a.y;
    int drawHeight = m_deskBound.b.y - m_deskBound.a.y;

    BitBlt(hdc, 0, drawY, m_desktopRect.right, drawHeight,
        m_memdc, 0, drawY, SRCCOPY);

    EndPaint(hwnd, &ps);
}

// ===================== On KeyDown =======================
void WinsapApp::OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    Panel* activePanel = nullptr;

    // Determine which panel is active
    if (m_upperPanel.IsActive()) {
        activePanel = &m_upperPanel;
    }
    else if (m_lowerPanel.IsActive()) {
        activePanel = &m_lowerPanel;
    }

    if (!activePanel) return;

    bool needsRedraw = false;
    const int frameBegin = activePanel->GetFrameBegin();
    const int frameNum = activePanel->GetFrameNum();

    switch (wParam) {
    case VK_LEFT:
        activePanel->SetFrameBegin(frameBegin - 1);
        needsRedraw = true;
        break;
    case VK_RIGHT:
        activePanel->SetFrameBegin(frameBegin + 1);
        needsRedraw = true;
        break;
    case VK_UP:
        activePanel->SetFrameBegin(frameBegin - frameNum);
        needsRedraw = true;
        break;
    case VK_DOWN:
        activePanel->SetFrameBegin(frameBegin + frameNum);
        needsRedraw = true;
        break;
    case VK_HOME:
        activePanel->SetFrameBegin(0);
        needsRedraw = true;
        break;
    case VK_END:
        activePanel->SetFrameBegin(activePanel->GetLastValidFrameBegin());
        needsRedraw = true;
        break;
    case VK_PRIOR:
        activePanel->SetFrameBegin(frameBegin - activePanel->GetFrameSkip());
        needsRedraw = true;
        break;
    case VK_NEXT:
        activePanel->SetFrameBegin(frameBegin + activePanel->GetFrameSkip());
        needsRedraw = true;
        break;
    }

    if (needsRedraw) {
        if (activePanel == &m_upperPanel) {
            SyncLockedLowerStart();
        }
        m_updateType = upAll;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// ===================== On LButtonDown =======================
void WinsapApp::OnLButtonDown(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    int xPos = GET_X_LPARAM(lParam);
    int yPos = GET_Y_LPARAM(lParam);
    DrawContext ctx = DrawContext::CreateFrom(this);

    if (m_upperPanel.IsActive()) {
        int fileIndex = m_upperPanel.GetFileIndexAtPosition(xPos, yPos, ctx);
        if (fileIndex >= 0) {
            PlayWholeFile(hwnd, m_upperPanel, fileIndex);
            return;
        }
    }

    if (m_lowerPanel.IsActive()) {
        int fileIndex = m_lowerPanel.GetFileIndexAtPosition(xPos, yPos, ctx);
        if (fileIndex >= 0) {
            PlayWholeFile(hwnd, m_lowerPanel, fileIndex);
        }
    }
}

// ===================== Update Rate Status =======================
// Recomputes both sampling rate panes. The raw pane always carries the rate
// raw PCM streams are read with and is greyed out while none is open. The wav
// pane stays blank until a wav file is loaded, shows the shared rate while the
// open files agree and turns red on the first file's rate when they do not.
void WinsapApp::UpdateRateStatus()
{
    if (m_toolbarManager.GetStatuslineHandle() == nullptr) {
        return;
    }

    bool rawActive = false;
    bool wavFound = false;
    bool wavMixed = false;
    DWORD firstWavRate = 0;

    // The upper panel is scanned first so that its rate is the one reported
    // when the open files disagree
    Panel* panels[2] = { &m_upperPanel, &m_lowerPanel };
    for (Panel* panel : panels) {
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (!panel->IsFileOpen(i)) continue;
            if (panel->GetFileTypes()[i] != ftBinaryShort) continue;

            if (panel->GetIsWavFlags()[i]) {
                DWORD rate = panel->GetSamplingRates()[i];
                if (!wavFound) {
                    wavFound = true;
                    firstWavRate = rate;
                }
                else if (rate != firstWavRate) {
                    wavMixed = true;
                }
            }
            else {
                rawActive = true;
            }
        }
    }

    WCHAR rawText[32];
    swprintf_s(rawText, 32, L"Raw %lu Hz", (unsigned long)m_samplingRate);

    WCHAR wavText[32];
    if (wavFound) {
        swprintf_s(wavText, 32, L"Wav %lu Hz", (unsigned long)firstWavRate);
    }
    else {
        wavText[0] = L'\0';
    }

    // Only disturb the status bar when something actually changed
    if (rawActive == m_rawRateActive && wavMixed == m_wavRateMixed &&
        wcscmp(rawText, m_rawRateText) == 0 &&
        wcscmp(wavText, m_wavRateText) == 0) {
        return;
    }

    m_rawRateActive = rawActive;
    m_wavRateMixed = wavMixed;
    wcscpy_s(m_rawRateText, rawText);
    wcscpy_s(m_wavRateText, wavText);

    m_toolbarManager.SetStatuslineOwnerDraw(STATUSBAR_RAW_RATE);
    m_toolbarManager.SetStatuslineOwnerDraw(STATUSBAR_WAV_RATE);
}

// ===================== Apply Raw Sampling Rate =======================
// A raw PCM stream carries no rate of its own, so changing the setting has to
// reach the streams that are already open as well as the ones opened later.
void WinsapApp::ApplyRawSamplingRate(DWORD rate)
{
    m_samplingRate = rate;

    Panel* panels[2] = { &m_upperPanel, &m_lowerPanel };
    for (Panel* panel : panels) {
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (!panel->IsFileOpen(i)) continue;
            // Raw audio shown as an FFT amplitude plays at the raw rate too, so its
            // frequency axis has to follow the same setting.
            if (panel->GetFileTypes()[i] == ftParameter) continue;
            if (panel->GetIsWavFlags()[i]) continue;   // A wav keeps its own rate
            panel->GetSamplingRates()[i] = rate;
        }
    }

    UpdateRateStatus();
}

// ===================== Spectrogram Contrast and Colour Map =======================
// There is no way to pick one panel over the other, and both normally hold
// files, so the contrast and the colour map are applied to every panel that has
// files open. Driving them from one reference value keeps the two pictures
// directly comparable.
void WinsapApp::AdjustSpectrogramRange(HWND hwnd, float deltaDb)
{
    Panel* panels[2] = { &m_upperPanel, &m_lowerPanel };

    const Panel* reference = nullptr;
    for (const Panel* panel : panels) {
        if (panel->IsActive()) {
            reference = panel;
            break;
        }
    }
    if (!reference) return;

    const float rangeDb = reference->GetSpectrogramRangeDb() + deltaDb;
    for (Panel* panel : panels) {
        if (panel->IsActive()) panel->SetSpectrogramRangeDb(rangeDb);
    }
    winsap::SaveSpectrogramRangeDb(reference->GetSpectrogramRangeDb());

    WCHAR status[128];
    swprintf_s(status, L"Spectrogram Contrast %s: %.0f dB below peak.",
               deltaDb < 0.0f ? L"Up" : L"Down",
               reference->GetSpectrogramRangeDb());
    m_toolbarManager.UpdateStatusline(status, STATUSBAR_FILE_INFO);
    InvalidateRect(hwnd, NULL, FALSE);
}

// Steps every active panel on to the same next colour map. Display only: the
// cached dB values are left alone, so nothing is analysed again.
void WinsapApp::CycleSpectrogramPalette(HWND hwnd)
{
    Panel* panels[2] = { &m_upperPanel, &m_lowerPanel };

    const Panel* reference = nullptr;
    for (const Panel* panel : panels) {
        if (panel->IsActive()) {
            reference = panel;
            break;
        }
    }
    if (!reference) return;

    const winsap::SpectrogramPalette palette =
        winsap::NextSpectrogramPalette(reference->GetSpectrogramPalette());
    for (Panel* panel : panels) {
        if (panel->IsActive()) panel->SetSpectrogramPalette(palette);
    }
    winsap::SaveSpectrogramPalette(palette);

    WCHAR status[128];
    swprintf_s(status, L"Spectrogram colour map: %S.",
               winsap::SpectrogramPaletteName(palette));
    m_toolbarManager.UpdateStatusline(status, STATUSBAR_FILE_INFO);
    InvalidateRect(hwnd, NULL, FALSE);
}

// ===================== On DrawItem =======================
// Draws the two owner drawn rate panes. Greyed means the raw rate is not in
// use, red means the open files do not share one sampling rate.
void WinsapApp::OnDrawItem(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
    if (dis == nullptr || dis->CtlID != IDC_STATUSBAR) {
        return;
    }

    const WCHAR* text = nullptr;
    COLORREF colour = GetSysColor(COLOR_BTNTEXT);

    if (dis->itemID == STATUSBAR_RAW_RATE) {
        text = m_rawRateText;
        if (!m_rawRateActive) {
            colour = GetSysColor(COLOR_GRAYTEXT);
        }
    }
    else if (dis->itemID == STATUSBAR_WAV_RATE) {
        text = m_wavRateText;
        if (m_wavRateMixed) {
            colour = RGB(255, 0, 0);
        }
    }
    else {
        return;
    }

    if (text == nullptr || text[0] == L'\0') {
        return;
    }

    // The device context arrives with the stock system font, so borrow the
    // control's own font to match the panes the statusline draws itself.
    HFONT font = (HFONT)SendMessage(dis->hwndItem, WM_GETFONT, 0, 0);
    HFONT oldFont = (font != nullptr) ? (HFONT)SelectObject(dis->hDC, font) : nullptr;

    const int oldMode = SetBkMode(dis->hDC, TRANSPARENT);
    const COLORREF oldColour = SetTextColor(dis->hDC, colour);

    RECT rc = dis->rcItem;
    rc.left += 2;
    DrawText(dis->hDC, text, -1, &rc,
             DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    SetTextColor(dis->hDC, oldColour);
    SetBkMode(dis->hDC, oldMode);
    if (oldFont != nullptr) {
        SelectObject(dis->hDC, oldFont);
    }
}

// ===================== On MouseMove =======================
void WinsapApp::OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    int xPos = GET_X_LPARAM(lParam);
    int yPos = GET_Y_LPARAM(lParam);

    // Create DrawContext for file detection
    DrawContext ctx = DrawContext::CreateFrom(this);
    HWND hwndStatus = m_toolbarManager.GetStatuslineHandle();

    // Check if mouse is over a filename and show file info
    int fileIndex = -1;
    Panel* filePanel = nullptr;
    
    if (m_upperPanel.IsActive()) {
        fileIndex = m_upperPanel.GetFileIndexAtPosition(xPos, yPos, ctx);
        if (fileIndex >= 0) {
            filePanel = &m_upperPanel;
        }
    }
    if (fileIndex < 0 && m_lowerPanel.IsActive()) {
        fileIndex = m_lowerPanel.GetFileIndexAtPosition(xPos, yPos, ctx);
        if (fileIndex >= 0) {
            filePanel = &m_lowerPanel;
        }
    }

    // Update file info in status bar position 4
    if (filePanel && fileIndex >= 0) {
        WCHAR fileInfo[512] = L"";
        int fileType = filePanel->GetFileType(fileIndex);
        const WCHAR* fullPath = filePanel->GetFileName(fileIndex);

        if (fileType == ftParameter) {
            // Parameter file
            long frameTotal = filePanel->GetFileFrameTotals()[fileIndex];
            swprintf_s(fileInfo, 512, L"%s  |  Parameter file, Total frames = %ld",
                       fullPath, frameTotal);
        }
        else {
            // Binary data file (WAV or raw PCM)
            BOOL isWav = filePanel->GetIsWavFlags()[fileIndex];
            DWORD samplingRate = filePanel->GetSamplingRates()[fileIndex];

            // Calculate samples (16-bit = 2 bytes per sample)
            long dataLength = filePanel->GetAudioDataLength(fileIndex);
            long samples = dataLength / 2;
            double seconds = (samplingRate > 0) ? (double)samples / samplingRate : 0.0;

            if (isWav) {
                // WAV file - the rate comes from the file header
                swprintf_s(fileInfo, 512,
                           L"%s  |  WAV: %lu Hz, Mono, %s, %ld samples (%.3f sec)",
                           fullPath, samplingRate,
                           WavSampleFormatName(filePanel->GetSampleFormat(fileIndex)),
                           samples, seconds);
            }
            else {
                // Raw PCM - the rate is the one set from the Options menu
                swprintf_s(fileInfo, 512,
                           L"%s  |  Raw PCM: %lu Hz, 16-bit, %ld samples (%.3f sec)",
                           fullPath, samplingRate, samples, seconds);
            }
        }
        
        if (hwndStatus) {
            SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_FILE_INFO, (LPARAM)fileInfo);
        }
    }
    else {
        // Clear file info when not hovering over a file
        if (hwndStatus) {
            SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_FILE_INFO, (LPARAM)L"");
        }
    }

    // Determine which panel the mouse is in for waveform coordinates
    Panel* panel = nullptr;
    if (m_upperPanel.IsActive()) {
        WRect upperRect = m_upperPanel.GetPanelRect();
        WRect upperWaveBox = m_upperPanel.GetWaveBox();
        if (xPos >= upperWaveBox.a.x && xPos <= upperWaveBox.b.x &&
            yPos >= upperWaveBox.a.y && yPos <= upperWaveBox.b.y) {
            panel = &m_upperPanel;
        }
    }

    if (!panel && m_lowerPanel.IsActive()) {
        WRect lowerRect = m_lowerPanel.GetPanelRect();
        WRect lowerWaveBox = m_lowerPanel.GetWaveBox();
        if (xPos >= lowerWaveBox.a.x && xPos <= lowerWaveBox.b.x &&
            yPos >= lowerWaveBox.a.y && yPos <= lowerWaveBox.b.y) {
            panel = &m_lowerPanel;
        }
    }

    if (panel) {
        WRect waveBox = panel->GetWaveBox();

        // Calculate actual data coordinates (same as winsap)
        int frameBegin = panel->GetFrameBegin();
        int frameNum = panel->GetFrameNum();
        int frameLength = panel->GetFrameLength();

        // Calculate horizontal scale: pixels per sample
        float datHp = static_cast<float>(frameBegin) * static_cast<float>(frameLength);
        float datTp = static_cast<float>(frameBegin + frameNum) * static_cast<float>(frameLength);
        float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (datTp - datHp);

        // Convert pixel position to actual sample number in file; 64 bits because
        // a long parameter file spans more samples than an int holds
        const long long xCoord = static_cast<long long>(static_cast<float>(xPos - waveBox.a.x) / horiz);

        // Calculate y coordinate (amplitude value)
        float upLimit = panel->GetUpLimit();
        float lowLimit = panel->GetLowLimit();
        float step = (upLimit - lowLimit) / static_cast<float>(waveBox.b.y - waveBox.a.y - 2);
        int yCoord = static_cast<int>(upLimit - static_cast<float>(yPos - waveBox.a.y) * step);

        // The spectrum window plots two quite different pictures, so the
        // readout has to follow whichever is on screen. Sample index against
        // amplitude means nothing on either of them.
        WCHAR statusText[256];
        double topHz = 0.0;
        DWORD hoverRate = 0;
        const Panel::HoverAxis axis = panel->GetHoverAxis(topHz, hoverRate);

        const int plotWidth = waveBox.b.x - waveBox.a.x;
        const int plotHeight = waveBox.b.y - waveBox.a.y;

        if (axis == Panel::HoverAxis::SpectrumLine) {
            // Amplitude against frequency: the horizontal axis is the spectrum.
            const double across = plotWidth > 0
                ? static_cast<double>(xPos - waveBox.a.x) / static_cast<double>(plotWidth)
                : 0.0;
            const int hz = static_cast<int>(across * topHz + 0.5);
            swprintf_s(statusText, 256, L" (%d Hz, %d dB)", hz, yCoord);
        }
        else if (axis == Panel::HoverAxis::Spectrogram) {
            // Frequency against time. Time is reported from the start of the
            // file rather than the start of the view, because that is what the
            // user is trying to locate.
            const double up = plotHeight > 0
                ? static_cast<double>(waveBox.b.y - yPos) / static_cast<double>(plotHeight)
                : 0.0;
            const int hz = static_cast<int>(up * topHz + 0.5);

            const double seconds = hoverRate > 0
                ? (datHp + static_cast<double>(xCoord)) / static_cast<double>(hoverRate)
                : 0.0;
            const long totalMs = static_cast<long>(seconds * 1000.0 + 0.5);
            const int hours = static_cast<int>(totalMs / 3600000);
            const int minutes = static_cast<int>((totalMs / 60000) % 60);
            const int secs = static_cast<int>((totalMs / 1000) % 60);
            const int millis = static_cast<int>(totalMs % 1000);
            swprintf_s(statusText, 256, L" (%02d:%02d:%02d.%03d, %d Hz)",
                       hours, minutes, secs, millis, hz);
        }
        else {
            // Waveform: sample index against amplitude, as it always was.
            swprintf_s(statusText, 256, L" (%lld, %d)", xCoord, yCoord);
        }

        if (hwndStatus) {
            SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_WAVEFORM_COORD, (LPARAM)statusText);
        }
    } else {
        // Mouse is outside waveform area, clear status
        if (hwndStatus) {
            SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_WAVEFORM_COORD, (LPARAM)L"");
        }
    }
}

// ===================== On RButtonDown =======================
void WinsapApp::OnRButtonDown(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    int xPos = GET_X_LPARAM(lParam);
    int yPos = GET_Y_LPARAM(lParam);

    // Create DrawContext for text measurement
    DrawContext ctx = DrawContext::CreateFrom(this);

    // Check if click is in waveform area (for marking)
    Panel* panel = nullptr;
    HWND hwndStatus = m_toolbarManager.GetStatuslineHandle();

    if (m_upperPanel.IsActive()) {
        WRect waveBox = m_upperPanel.GetWaveBox();
        // Shrink by 1 pixel for inside detection
        if (xPos >= waveBox.a.x + 1 && xPos <= waveBox.b.x - 1 &&
            yPos >= waveBox.a.y + 1 && yPos <= waveBox.b.y - 1) {
            panel = &m_upperPanel;

            // Handle marking
            MarkType currentMark = panel->GetMarkType();
            if (currentMark == mtNone) {
                // First click - set begin point
                int frameBegin = panel->GetFrameBegin();
                int frameNum = panel->GetFrameNum();
                int frameLength = panel->GetFrameLength();
                float datHp = static_cast<float>(frameBegin) * static_cast<float>(frameLength);
                float datTp = static_cast<float>(frameBegin + frameNum) * static_cast<float>(frameLength);
                float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (datTp - datHp);
                long listenBegin = static_cast<long>(datHp + static_cast<float>(xPos - waveBox.a.x) / horiz);

                panel->SetListenRange(listenBegin, listenBegin);
                panel->SetMarkType(mtLeft);
                if (hwndStatus) {
                    SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_MARK_UP, (LPARAM)L"Up Mark Left");
                }
            }
            else if (currentMark == mtLeft) {
                // Second click - set end point
                int frameBegin = panel->GetFrameBegin();
                int frameNum = panel->GetFrameNum();
                int frameLength = panel->GetFrameLength();
                float datHp = static_cast<float>(frameBegin) * static_cast<float>(frameLength);
                float datTp = static_cast<float>(frameBegin + frameNum) * static_cast<float>(frameLength);
                float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (datTp - datHp);
                long listenEnd = static_cast<long>(datHp + static_cast<float>(xPos - waveBox.a.x) / horiz);

                long listenBegin = panel->GetListenBegin();
                if (listenBegin > listenEnd) {
                    // Swap if backwards
                    long temp = listenBegin;
                    listenBegin = listenEnd;
                    listenEnd = temp;
                }
                panel->SetListenRange(listenBegin, listenEnd);
                panel->SetMarkType(mtAll);
                if (hwndStatus) {
                    SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_MARK_UP, (LPARAM)L"Up Mark All");
                }
            }
            else if (currentMark == mtAll) {
                // Third click - clear mark
                panel->SetMarkType(mtNone);
                if (hwndStatus) {
                    SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_MARK_UP, (LPARAM)L"");
                }
            }

            m_updateType = upUp;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    if (!panel && m_lowerPanel.IsActive()) {
        WRect waveBox = m_lowerPanel.GetWaveBox();
        // Shrink by 1 pixel for inside detection
        if (xPos >= waveBox.a.x + 1 && xPos <= waveBox.b.x - 1 &&
            yPos >= waveBox.a.y + 1 && yPos <= waveBox.b.y - 1) {
            panel = &m_lowerPanel;

            // Handle marking
            MarkType currentMark = panel->GetMarkType();
            if (currentMark == mtNone) {
                // First click - set begin point
                int frameBegin = panel->GetFrameBegin();
                int frameNum = panel->GetFrameNum();
                int frameLength = panel->GetFrameLength();
                float datHp = static_cast<float>(frameBegin) * static_cast<float>(frameLength);
                float datTp = static_cast<float>(frameBegin + frameNum) * static_cast<float>(frameLength);
                float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (datTp - datHp);
                long listenBegin = static_cast<long>(datHp + static_cast<float>(xPos - waveBox.a.x) / horiz);

                panel->SetListenRange(listenBegin, listenBegin);
                panel->SetMarkType(mtLeft);
                if (hwndStatus) {
                    SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_MARK_DOWN, (LPARAM)L"Down Mark Left");
                }
            }
            else if (currentMark == mtLeft) {
                // Second click - set end point
                int frameBegin = panel->GetFrameBegin();
                int frameNum = panel->GetFrameNum();
                int frameLength = panel->GetFrameLength();
                float datHp = static_cast<float>(frameBegin) * static_cast<float>(frameLength);
                float datTp = static_cast<float>(frameBegin + frameNum) * static_cast<float>(frameLength);
                float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (datTp - datHp);
                long listenEnd = static_cast<long>(datHp + static_cast<float>(xPos - waveBox.a.x) / horiz);

                long listenBegin = panel->GetListenBegin();
                if (listenBegin > listenEnd) {
                    // Swap if backwards
                    long temp = listenBegin;
                    listenBegin = listenEnd;
                    listenEnd = temp;
                }
                panel->SetListenRange(listenBegin, listenEnd);
                panel->SetMarkType(mtAll);
                if (hwndStatus) {
                    SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_MARK_DOWN, (LPARAM)L"Down Mark All");
                }
            }
            else if (currentMark == mtAll) {
                // Third click - clear mark
                panel->SetMarkType(mtNone);
                if (hwndStatus) {
                    SendMessage(hwndStatus, SB_SETTEXT, STATUSBAR_MARK_DOWN, (LPARAM)L"");
                }
            }

            m_updateType = upDown;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    // Not in waveform area, check if clicking on filename to close
    if (m_upperPanel.IsActive()) {
        int fileIndex = m_upperPanel.GetFileIndexAtPosition(xPos, yPos, ctx);
        if (fileIndex >= 0) {
            // Close the file
            m_upperPanel.CloseFile(fileIndex);

            // Check if all files are closed
            bool hasFiles = false;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_upperPanel.GetFlags()[i] != 0) {
                    hasFiles = true;
                    break;
                }
            }

            // If no files are left, recompute the one-panel layout.
            if (!hasFiles) {
                ResizeDesktop(hwnd);
            }

            // Redraw
            m_updateType = upAll;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check lower panel
    if (m_lowerPanel.IsActive()) {
        int fileIndex = m_lowerPanel.GetFileIndexAtPosition(xPos, yPos, ctx);
        if (fileIndex >= 0) {
            // Close the file
            m_lowerPanel.CloseFile(fileIndex);

            // Check if all files are closed
            bool hasFiles = false;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_lowerPanel.GetFlags()[i] != 0) {
                    hasFiles = true;
                    break;
                }
            }

            // If no files are left, recompute the one-panel layout.
            if (!hasFiles) {
                ResizeDesktop(hwnd);
            }

            // Redraw
            m_updateType = upAll;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }
}

void WinsapApp::SyncLockedLowerStart()
{
    if (m_lockStartFrame && m_upperPanel.IsActive() && m_lowerPanel.IsActive()) {
        m_lowerPanel.SetFrameBegin(m_upperPanel.GetFrameBegin());
    }
}

// Moves every active panel by one frame or by its frame skip. With the
// starting-frame lock on, the lower panel follows the upper one instead.
void WinsapApp::MovePanels(HWND hwnd, int direction, bool bySkip)
{
    const bool upperActive = m_upperPanel.IsActive();
    const bool lowerActive = m_lowerPanel.IsActive();
    if (upperActive) {
        const int step = bySkip ? m_upperPanel.GetFrameSkip() : 1;
        m_upperPanel.SetFrameBegin(m_upperPanel.GetFrameBegin() + direction * step);
    }
    if (lowerActive) {
        if (m_lockStartFrame && upperActive) {
            SyncLockedLowerStart();
        }
        else {
            const int step = bySkip ? m_lowerPanel.GetFrameSkip() : 1;
            m_lowerPanel.SetFrameBegin(m_lowerPanel.GetFrameBegin() + direction * step);
        }
    }
    if (upperActive || lowerActive) {
        m_updateType = upAll;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void WinsapApp::ShowHelpTopics(HWND hwnd)
{
    wavedsp::ShowHelp(hwnd, L"WinSap Help Topics", WinsapHelpMarkup(false), WinsapHelpTheme(),
        MAKEINTRESOURCE(IDI_WSAP));
}

// ===================== On Command =======================
LRESULT WinsapApp::OnCommand(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    int wmId = LOWORD(wParam);

    if (m_audioPlayer.IsPlaying() && wmId != ID_STOP && wmId != ID_HELP && wmId != ID_HELP_FINDER &&
        wmId != ID_HELP_INDEX && wmId != IDM_ABOUT) {
        return 0;
    }

    const WCHAR* commandStatus = CommandStatusText(wmId);
    if (commandStatus != nullptr) {
        m_toolbarManager.UpdateStatusline(commandStatus, STATUSBAR_FILE_INFO);
    }

    switch (wmId) {
    case IDM_EXIT:
        DestroyWindow(hwnd);
        break;

    case IDM_ABOUT:
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutDlgProc);
        break;

    case ID_FILE_OPENUP:
        if (OpenFileUp(hwnd)) {
            m_updateType = upAll;
            Draw(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case ID_FILE_OPENDOWN:
        if (OpenFileDown(hwnd)) {
            m_updateType = upAll;
            Draw(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case ID_FILE_SAVEAS:
        SaveAsFile(hwnd);
        break;

    case ID_FILE_CLOSEALL:
        m_upperPanel.CloseAllFiles();
        m_upperPanel.SetFrameBegin(0);
        m_upperPanel.SetFrameNum(1);
        m_upperPanel.SetUpLimit(32768.0f);
        m_upperPanel.SetLowLimit(-32768.0f);

        m_lowerPanel.CloseAllFiles();
        m_lowerPanel.SetFrameBegin(0);
        m_lowerPanel.SetFrameNum(1);
        m_lowerPanel.SetUpLimit(32768.0f);
        m_lowerPanel.SetLowLimit(-32768.0f);

        m_updateType = upAll;
        Draw(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;

    case ID_TOOLS_VIEWALL:
        if (m_upperPanel.IsActive()) m_upperPanel.ViewAll();
        if (m_lowerPanel.IsActive()) m_lowerPanel.ViewAll();
        m_updateType = upAll;
        Draw(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;

    case ID_TOOLS_MOVEFRAMEBACKWARD:  // Move backward 1 frame
        MovePanels(hwnd, -1, false);
        break;

    case ID_TOOLS_MOVEFRAMEFORWARD:  // Move forward 1 frame
        MovePanels(hwnd, 1, false);
        break;

    case ID_TOOLS_FASTBACKWARD:  // Fast backward (10 frames)
        MovePanels(hwnd, -1, true);
        break;

    case ID_TOOLS_FASTFORWARD:  // Fast forward (frameSkip frames)
        MovePanels(hwnd, 1, true);
        break;

    case ID_TOOLS_ZOOMIN:  // Zoom in (halve visible frames)
        if (m_upperPanel.IsActive()) {
            int frameNum = m_upperPanel.GetFrameNum();
            int frameBegin = m_upperPanel.GetFrameBegin();

            if (frameNum > 1) {
                int newFrameNum = frameNum / 2;
                int newFrameBegin = frameBegin + newFrameNum / 2;

                m_upperPanel.SetFrameNum(newFrameNum);
                m_upperPanel.SetFrameBegin(newFrameBegin);

                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        if (m_lowerPanel.IsActive()) {
            int frameNum = m_lowerPanel.GetFrameNum();
            int frameBegin = m_lowerPanel.GetFrameBegin();

            if (frameNum > 1) {
                int newFrameNum = frameNum / 2;
                int newFrameBegin = frameBegin + newFrameNum / 2;

                m_lowerPanel.SetFrameNum(newFrameNum);
                m_lowerPanel.SetFrameBegin(newFrameBegin);

                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_TOOLS_ZOOMOUT:  // Zoom out (double visible frames)
        if (m_upperPanel.IsActive()) {
            int frameNum = m_upperPanel.GetFrameNum();
            int frameBegin = m_upperPanel.GetFrameBegin();

            int newFrameNum = static_cast<int>(
                (std::min<long>)(m_upperPanel.GetTotalFrameCount(),
                                 (std::max<long>)(1, static_cast<long>(frameNum) * 2)));
            int newFrameBegin = frameBegin - newFrameNum / 4;
            if (newFrameBegin < 0) newFrameBegin = 0;

            m_upperPanel.SetFrameNum(newFrameNum);
            m_upperPanel.SetFrameBegin(newFrameBegin);

            m_updateType = upAll;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (m_lowerPanel.IsActive()) {
            int frameNum = m_lowerPanel.GetFrameNum();
            int frameBegin = m_lowerPanel.GetFrameBegin();

            int newFrameNum = static_cast<int>(
                (std::min<long>)(m_lowerPanel.GetTotalFrameCount(),
                                 (std::max<long>)(1, static_cast<long>(frameNum) * 2)));
            int newFrameBegin = frameBegin - newFrameNum / 4;
            if (newFrameBegin < 0) newFrameBegin = 0;

            m_lowerPanel.SetFrameNum(newFrameNum);
            m_lowerPanel.SetFrameBegin(newFrameBegin);

            m_updateType = upAll;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case ID_TOOLS_COMPRESS:  // Compress amplitude (zoom out vertically)
        if (m_upperPanel.IsActive()) {
            // Skip FFT files
            bool hasNonFFT = false;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_upperPanel.GetFlags()[i] != 0) {
                    int fileType = m_upperPanel.GetFileTypes()[i];
                    if (fileType != ftFFTAmplitude) {
                        hasNonFFT = true;
                        break;
                    }
                }
            }

            if (hasNonFFT) {
                float upLimit = m_upperPanel.GetUpLimit();
                float lowLimit = m_upperPanel.GetLowLimit();
                m_upperPanel.SetUpLimit(upLimit * 2.0f);
                m_upperPanel.SetLowLimit(lowLimit * 2.0f);

                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        if (m_lowerPanel.IsActive()) {
            // Skip FFT files
            bool hasNonFFT = false;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_lowerPanel.GetFlags()[i] != 0) {
                    int fileType = m_lowerPanel.GetFileTypes()[i];
                    if (fileType != ftFFTAmplitude) {
                        hasNonFFT = true;
                        break;
                    }
                }
            }

            if (hasNonFFT) {
                float upLimit = m_lowerPanel.GetUpLimit();
                float lowLimit = m_lowerPanel.GetLowLimit();
                m_lowerPanel.SetUpLimit(upLimit * 2.0f);
                m_lowerPanel.SetLowLimit(lowLimit * 2.0f);

                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_TOOLS_EXPAND:  // Expand amplitude (zoom in vertically)
        if (m_upperPanel.IsActive()) {
            // Skip FFT files
            bool hasNonFFT = false;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_upperPanel.GetFlags()[i] != 0) {
                    int fileType = m_upperPanel.GetFileTypes()[i];
                    if (fileType != ftFFTAmplitude) {
                        hasNonFFT = true;
                        break;
                    }
                }
            }

            if (hasNonFFT) {
                float upLimit = m_upperPanel.GetUpLimit();
                float lowLimit = m_upperPanel.GetLowLimit();
                m_upperPanel.SetUpLimit(upLimit / 2.0f);
                m_upperPanel.SetLowLimit(lowLimit / 2.0f);

                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        if (m_lowerPanel.IsActive()) {
            // Skip FFT files
            bool hasNonFFT = false;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_lowerPanel.GetFlags()[i] != 0) {
                    int fileType = m_lowerPanel.GetFileTypes()[i];
                    if (fileType != ftFFTAmplitude) {
                        hasNonFFT = true;
                        break;
                    }
                }
            }

            if (hasNonFFT) {
                float upLimit = m_lowerPanel.GetUpLimit();
                float lowLimit = m_lowerPanel.GetLowLimit();
                m_lowerPanel.SetUpLimit(upLimit / 2.0f);
                m_lowerPanel.SetLowLimit(lowLimit / 2.0f);

                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_TOOLS_LOCKSTARTFRAME:  // Toggle frame lock
        m_lockStartFrame = !m_lockStartFrame;

        // Update menu check mark
        if (m_hMenu) {
            CheckMenuItem(m_hMenu, ID_TOOLS_LOCKSTARTFRAME,
                m_lockStartFrame ? MF_CHECKED : MF_UNCHECKED);
        }

        // If locking, sync lower panel to upper panel
        if (m_lockStartFrame && m_upperPanel.IsActive() && m_lowerPanel.IsActive()) {
            SyncLockedLowerStart();
            m_updateType = upAll;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case ID_UP_FRAMELENGTH:  // Set upper panel frame length
        if (m_upperPanel.IsActive()) {
            int frameLength = m_upperPanel.GetFrameLength();
            if (InputDialog::Show(hwnd, L"Frame Length",
                                 L"Enter frame length in samples (valid range: 1 or greater):",
                                 frameLength)) {
                if (frameLength > 0) {
                    m_upperPanel.SetFrameLength(frameLength);

                    m_updateType = upAll;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    MessageBox(hwnd, L"Frame length must be greater than zero.",
                               L"Invalid Frame Length", MB_OK | MB_ICONERROR);
                }
            }
        }
        break;

    case ID_DOWN_FRAMELENGTH:  // Set lower panel frame length
        if (m_lowerPanel.IsActive()) {
            int frameLength = m_lowerPanel.GetFrameLength();
            if (InputDialog::Show(hwnd, L"Frame Length",
                                 L"Enter frame length in samples (valid range: 1 or greater):",
                                 frameLength)) {
                if (frameLength > 0) {
                    m_lowerPanel.SetFrameLength(frameLength);

                    m_updateType = upAll;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    MessageBox(hwnd, L"Frame length must be greater than zero.",
                               L"Invalid Frame Length", MB_OK | MB_ICONERROR);
                }
            }
        }
        break;

    case ID_UP_GOTOFRAME:  // Jump to specific frame in upper panel
        if (m_upperPanel.IsActive()) {
            int frameBegin = m_upperPanel.GetFrameBegin();
            if (InputDialog::Show(hwnd, L"Go To Frame",
                                 L"Enter frame number:", frameBegin)) {
                const int requested = frameBegin;
                if (m_upperPanel.SetFrameBegin(requested)) {
                    WCHAR message[192];
                    swprintf_s(message, L"Requested frame %d was adjusted to %d "
                                         L"to fit the available data.",
                               requested, m_upperPanel.GetFrameBegin());
                    MessageBox(hwnd, message, L"Go To Frame", MB_OK | MB_ICONINFORMATION);
                }
                SyncLockedLowerStart();
                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_DOWN_GOTOFRAME:  // Jump to specific frame in lower panel
        if (m_lowerPanel.IsActive()) {
            int frameBegin = m_lowerPanel.GetFrameBegin();
            if (InputDialog::Show(hwnd, L"Go To Frame",
                                 L"Enter frame number:", frameBegin)) {
                const int requested = frameBegin;
                if (m_lowerPanel.SetFrameBegin(requested)) {
                    WCHAR message[192];
                    swprintf_s(message, L"Requested frame %d was adjusted to %d "
                                         L"to fit the available data.",
                               requested, m_lowerPanel.GetFrameBegin());
                    MessageBox(hwnd, message, L"Go To Frame", MB_OK | MB_ICONINFORMATION);
                }
                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_UP_NUMBEROFFRAMES:  // Set number of visible frames in upper panel
        if (m_upperPanel.IsActive()) {
            int frameNum = m_upperPanel.GetFrameNum();
            if (InputDialog::Show(hwnd, L"Number of Frames",
                                 L"Enter number of frames to display (valid range: 1 or greater):",
                                 frameNum)) {
                const int requested = frameNum;
                if (m_upperPanel.SetFrameNum(requested)) {
                    WCHAR message[192];
                    swprintf_s(message, L"Requested frame count %d was adjusted to %d "
                                          L"to fit the available data.",
                               requested, m_upperPanel.GetFrameNum());
                    MessageBox(hwnd, message, L"Number of Frames",
                               MB_OK | MB_ICONINFORMATION);
                }
                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_DOWN_NUMBEROFFRAMES:  // Set number of visible frames in lower panel
        if (m_lowerPanel.IsActive()) {
            int frameNum = m_lowerPanel.GetFrameNum();
            if (InputDialog::Show(hwnd, L"Number of Frames",
                                 L"Enter number of frames to display (valid range: 1 or greater):",
                                 frameNum)) {
                const int requested = frameNum;
                if (m_lowerPanel.SetFrameNum(requested)) {
                    WCHAR message[192];
                    swprintf_s(message, L"Requested frame count %d was adjusted to %d "
                                          L"to fit the available data.",
                               requested, m_lowerPanel.GetFrameNum());
                    MessageBox(hwnd, message, L"Number of Frames",
                               MB_OK | MB_ICONINFORMATION);
                }
                m_updateType = upAll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_UP_PAGENUMBERS:
        if (m_upperPanel.IsActive()) {
            int frameSkip = m_upperPanel.GetFrameSkip();
            if (InputDialog::Show(
                    hwnd, L"Upper Page Numbers",
                    L"Input number of frames to jump when using PageUp/PageDown (>0):",
                    frameSkip)) {
                if (frameSkip > 0) {
                    m_upperPanel.SetFrameSkip(frameSkip);
                } else {
                    MessageBox(hwnd, L"Page size must be greater than zero.",
                               L"Invalid Page Size", MB_OK | MB_ICONERROR);
                }
            }
        }
        break;

    case ID_DOWN_PAGENUMBERS:
        if (m_lowerPanel.IsActive()) {
            int frameSkip = m_lowerPanel.GetFrameSkip();
            if (InputDialog::Show(
                    hwnd, L"Down Page Numbers",
                    L"Input number of frames to jump when using PageUp/PageDown (>0):",
                    frameSkip)) {
                if (frameSkip > 0) {
                    m_lowerPanel.SetFrameSkip(frameSkip);
                } else {
                    MessageBox(hwnd, L"Page size must be greater than zero.",
                               L"Invalid Page Size", MB_OK | MB_ICONERROR);
                }
            }
        }
        break;

    case ID_UP_LIMIT:  // Set upper panel amplitude limits
        if (m_upperPanel.IsActive()) {
            float upLimit = m_upperPanel.GetUpLimit();
            float lowLimit = m_upperPanel.GetLowLimit();

            wchar_t prompt[256];
            swprintf_s(prompt, 256, L"Enter upper limit (current: %.1f):", upLimit);

            if (InputDialog::Show(hwnd, L"Upper Limit", prompt, upLimit)) {
                swprintf_s(prompt, 256, L"Enter lower limit (current: %.1f):", lowLimit);
                if (InputDialog::Show(hwnd, L"Lower Limit", prompt, lowLimit)) {
                    // Swap if reversed
                    if (upLimit < lowLimit) {
                        float temp = upLimit;
                        upLimit = lowLimit;
                        lowLimit = temp;
                    }

                    // Set both limits atomically after swap validation
                    m_upperPanel.SetUpLimit(upLimit);
                    m_upperPanel.SetLowLimit(lowLimit);

                    m_updateType = upAll;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        break;

    case ID_DOWN_LIMIT:  // Set lower panel amplitude limits
        if (m_lowerPanel.IsActive()) {
            float upLimit = m_lowerPanel.GetUpLimit();
            float lowLimit = m_lowerPanel.GetLowLimit();

            wchar_t prompt[256];
            swprintf_s(prompt, 256, L"Enter upper limit (current: %.1f):", upLimit);

            if (InputDialog::Show(hwnd, L"Upper Limit", prompt, upLimit)) {
                swprintf_s(prompt, 256, L"Enter lower limit (current: %.1f):", lowLimit);
                if (InputDialog::Show(hwnd, L"Lower Limit", prompt, lowLimit)) {
                    // Swap if reversed
                    if (upLimit < lowLimit) {
                        float temp = upLimit;
                        upLimit = lowLimit;
                        lowLimit = temp;
                    }

                    // Set both limits atomically after swap validation
                    m_lowerPanel.SetUpLimit(upLimit);
                    m_lowerPanel.SetLowLimit(lowLimit);

                    m_updateType = upAll;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        break;

    case ID_FILE_CLOSE:  // Close selected files
        if (SelectFiles(hwnd)) {
            bool upHasFiles = false;
            bool downHasFiles = false;
            bool upHasParFile = false;
            bool downHasParFile = false;

            // Close selected upper panel files
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_upperPanel.IsFileOpen(i)) {
                    if (m_upperPanel.GetSelectFlags()[i] != 0) {
                        m_upperPanel.CloseFile(i);
                    }
                    else {
                        upHasFiles = true;
                        if (m_upperPanel.GetFileTypes()[i] == ftParameter) {
                            upHasParFile = true;
                        }
                    }
                }
            }

            // Close selected lower panel files
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (m_lowerPanel.IsFileOpen(i)) {
                    if (m_lowerPanel.GetSelectFlags()[i] != 0) {
                        m_lowerPanel.CloseFile(i);
                    }
                    else {
                        downHasFiles = true;
                        if (m_lowerPanel.GetFileTypes()[i] == ftParameter) {
                            downHasParFile = true;
                        }
                    }
                }
            }

            m_upperPanel.SetParActive(upHasParFile);
            m_lowerPanel.SetParActive(downHasParFile);

            // Reset parameters if no files remain
            if (!upHasFiles) {
                m_upperPanel.SetFrameBegin(0);
                m_upperPanel.SetFrameNum(1);
                m_upperPanel.SetUpLimit(32768.0f);
                m_upperPanel.SetLowLimit(-32768.0f);
            }
            if (!downHasFiles) {
                m_lowerPanel.SetFrameBegin(0);
                m_lowerPanel.SetFrameNum(1);
                m_lowerPanel.SetUpLimit(32768.0f);
                m_lowerPanel.SetLowLimit(-32768.0f);
            }

            // Update display
            if (!upHasFiles && !downHasFiles)
                m_updateType = upAll;
            else
                m_updateType = upWindow;

            ResizeDesktop(hwnd);
            Draw(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case ID_FILE_RELOADALL: {  // Reload all files
        // Each slot is reloaded on its own. A file that cannot be opened is
        // left exactly as it is instead of being closed into an empty slot,
        // and every file that failed is named once when the sweep is over.
        WCHAR failedList[MAX_STR_LEN];
        failedList[0] = L'\0';
        int failedCount = 0;
        bool slotsEmptied = false;

        for (int panel = 0; panel < 2; panel++) {
            Panel& target = (panel == 0) ? m_upperPanel : m_lowerPanel;
            for (int i = 0; i < PANELMAXFILE; i++) {
                if (!target.IsFileOpen(i)) {
                    continue;
                }

                WCHAR fileName[MAXFILENAME];
                wcscpy_s(fileName, target.GetFileName(i));
                int fileType = target.GetFileTypes()[i];

                // Probe before closing, so a deleted or locked file costs
                // nothing and the panel keeps showing what it already has.
                if (!CanReadFile(fileName)) {
                    AppendFailedFile(failedList, MAX_STR_LEN, failedCount, fileName);
                    continue;
                }

                target.CloseFile(i);
                if (panel == 0) {
                    SetupFileUp(hwnd, fileName, i, fileType);
                }
                else {
                    SetupFileDown(hwnd, fileName, i, fileType);
                }

                // The file opened but the contents were rejected, so the slot
                // really is empty now. Close it again so nothing stale is left
                // behind, and remember that the layout has to be tidied up.
                if (!target.IsFileOpen(i)) {
                    target.CloseFile(i);
                    AppendFailedFile(failedList, MAX_STR_LEN, failedCount, fileName);
                    slotsEmptied = true;
                }
            }
        }

        // Losing a slot is the same situation as closing a file by hand, so
        // lay out the remaining panels again.
        if (slotsEmptied) {
            ResizeDesktop(hwnd);
        }

        if (failedCount > 0) {
            WCHAR message[MAX_STR_LEN];
            swprintf_s(message, MAX_STR_LEN,
                       L"%d of the open files could not be reloaded:\n\n%s%s",
                       failedCount, failedList,
                       failedCount > 12 ? L"\n..." : L"");
            MessageBox(hwnd, message, L"Reload All", MB_OK | MB_ICONWARNING);
        }

        m_updateType = upAll;
        Draw(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }

    case ID_TOOLS_ADDDOWNTOUP:  // Toggle add/replace mode
        m_addFile = !m_addFile;
        if (m_addFile) {
            m_updateType = upWindow;
        }
        else {
            m_updateType = upUp;
        }
        Draw(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;

    case ID_TOOLS_LOOKMARKEDBLOCK:  // Look at marked block
        // Zoom to show the marked region in the active panel
        if (m_upperPanel.IsActive() && m_upperPanel.GetMarkType() == mtAll) {
            long listenBegin = m_upperPanel.GetListenBegin();
            long listenEnd = m_upperPanel.GetListenEnd();
            int frameLength = m_upperPanel.GetFrameLength();

            if (frameLength > 0 && listenEnd > listenBegin) {
                // Calculate frame range to show the marked region
                int newFrameBegin = static_cast<int>(listenBegin / frameLength);
                int newFrameNum = static_cast<int>((listenEnd - listenBegin) / frameLength) + 1;

                // Ensure at least 1 frame is shown
                if (newFrameNum < 1) newFrameNum = 1;

                m_upperPanel.SetFrameNum(newFrameNum);
                m_upperPanel.SetFrameBegin(newFrameBegin);

                // Lock Starting Frame keeps the lower panel on the same frame,
                // so jumping to a marked block has to carry it along too.
                if (m_lockStartFrame && m_lowerPanel.IsActive()) {
                    SyncLockedLowerStart();
                }

                m_updateType = upAll;
                Draw(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        else if (m_lowerPanel.IsActive() && m_lowerPanel.GetMarkType() == mtAll) {
            long listenBegin = m_lowerPanel.GetListenBegin();
            long listenEnd = m_lowerPanel.GetListenEnd();
            int frameLength = m_lowerPanel.GetFrameLength();

            if (frameLength > 0 && listenEnd > listenBegin) {
                // Calculate frame range to show the marked region
                int newFrameBegin = static_cast<int>(listenBegin / frameLength);
                int newFrameNum = static_cast<int>((listenEnd - listenBegin) / frameLength) + 1;

                // Ensure at least 1 frame is shown
                if (newFrameNum < 1) newFrameNum = 1;

                m_lowerPanel.SetFrameNum(newFrameNum);
                m_lowerPanel.SetFrameBegin(newFrameBegin);

                m_updateType = upAll;
                Draw(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        break;

    case ID_WINDOW_SYNCPANELS:  // Match the lower panel's view to the upper one
        if (m_upperPanel.IsActive() && m_lowerPanel.IsActive()) {
            int frameLength = m_upperPanel.GetFrameLength();

            // Frame length first: it decides how many frames a file holds, so
            // the totals have to be rebuilt before the frame window is applied.
            if (frameLength > 0 && m_lowerPanel.GetFrameLength() != frameLength) {
                m_lowerPanel.SetFrameLength(frameLength);
            }

            m_lowerPanel.SetFrameNum(m_upperPanel.GetFrameNum());
            m_lowerPanel.SetFrameBegin(m_upperPanel.GetFrameBegin());

            m_updateType = upAll;
            Draw(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case ID_PLAYRECORD_PLAYALL:  // Play all files
        PlayAll(hwnd);
        break;

    case ID_PLAYRECORD_PLAYUP:  // Play upper panel
        PlayUp(hwnd);
        break;

    case ID_PLAYRECORD_PLAYDOWN:  // Play lower panel
        PlayDown(hwnd);
        break;

    case ID_STOP:
        StopPlayback(hwnd, L"Playback stopped.");
        break;

    case ID_HELP:
    case ID_HELP_FINDER:
    case ID_HELP_INDEX:
        ShowHelpTopics(hwnd);
        break;

    case ID_OPTIONS_CONFIGURE:  // Set the sampling rate used for raw PCM streams
        {
            int rate = static_cast<int>(m_samplingRate);
            if (InputDialog::Show(hwnd, L"Set Samplingrate",
                                  L"Please input the sampling rate for raw PCM:", rate)) {
                if (rate == 0) {
                    break;  // User entered 0, cancel
                }
                if (rate < 8000 || rate > 48000) {
                    MessageBox(hwnd, L"The sampling rate is out of range (8000-48000)!",
                              L"ERROR", MB_OK | MB_ICONERROR);
                }
                else {
                    // Reaches the raw streams already open as well as later ones
                    ApplyRawSamplingRate(static_cast<DWORD>(rate));
                    m_updateType = upAll;
                    Draw(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        break;

    case ID_OPTIONS_CONTRASTUP:  // Narrow the spectrogram dynamic range
        AdjustSpectrogramRange(hwnd, -5.0f);
        break;

    case ID_OPTIONS_CONTRASTDOWN:  // Widen the spectrogram dynamic range
        AdjustSpectrogramRange(hwnd, 5.0f);
        break;

    case ID_OPTIONS_PALETTE:  // Step through the spectrogram colour maps
        CycleSpectrogramPalette(hwnd);
        break;

    case ID_OPTIONS_SAVESTATUS:  // Save the raw PCM sampling rate
        {
            // The window geometry is stored automatically when the program
            // quits, so this only has to remember the raw sampling rate.
            HKEY hRegKey;
            DWORD result;

            RegCreateKeyEx(HKEY_CURRENT_USER,
                WINSAP_SETTINGS_REGISTRY_PATH,
                0, (LPWSTR)L"Sampling Rate", REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                nullptr, &hRegKey, &result);
            RegSetValueEx(hRegKey, L"Rate", 0, REG_DWORD,
                (LPBYTE)&m_samplingRate, sizeof(DWORD));
            RegCloseKey(hRegKey);
        }
        break;

    default:
        return DefWindowProc(hwnd, WM_COMMAND, wParam, lParam);
    }

    return 0;
}

// ===================== Initialize GDI =======================
void WinsapApp::InitializeGDI(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);

    // Get screen coordinates
    int maxX = GetSystemMetrics(SM_CXSCREEN);
    int maxY = GetSystemMetrics(SM_CYSCREEN);

    // Create memory DC and compatible bitmap
    m_memdc = CreateCompatibleDC(hdc);
    m_hbit = CreateCompatibleBitmap(hdc, maxX, maxY);
    m_hOldBitmap = (HBITMAP)SelectObject(m_memdc, m_hbit);
    if (m_hbit) {
        m_hbitWidth = maxX;
        m_hbitHeight = maxY;
    }

    // Get black brush and select it
    m_hbrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    SelectObject(m_memdc, m_hbrush);
    PatBlt(m_memdc, 0, 0, maxX, maxY, PATCOPY);

    // Set background color and mode
    SetBkColor(m_memdc, RGB(0, 0, 0));
    SetBkMode(m_memdc, OPAQUE);

    // Create DPI-scaled font
    CreateScaledFont(hwnd);
    m_holdf = (HFONT)SelectObject(m_memdc, m_hnewf);

    // Get text metrics
    GetTextMetrics(m_memdc, &m_tm);
    m_charHigh = m_tm.tmHeight + m_tm.tmExternalLeading;
    m_charWidth = m_tm.tmAveCharWidth;

    // Set text color
    SetTextColor(m_memdc, RGB(0, 255, 255));

    // Initialize pens and brushes
    InitializePens();
    InitializeBrushes();

    ReleaseDC(hwnd, hdc);
}

// ===================== DPI Handling =======================
void WinsapApp::InitializeDpi(HWND hwnd)
{
    // Try to get DPI using modern API (Windows 10 1607+)
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    if (hUser32) {
        typedef UINT(WINAPI* GetDpiForWindowFunc)(HWND);
        GetDpiForWindowFunc pGetDpiForWindow = 
            (GetDpiForWindowFunc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow && hwnd) {
            m_dpi = pGetDpiForWindow(hwnd);
        }
        else {
            // Fallback: get system DPI
            HDC hdc = GetDC(hwnd);
            m_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hwnd, hdc);
        }
    }
    else {
        m_dpi = 96;  // Default DPI
    }

    m_dpiScale = static_cast<float>(m_dpi) / 96.0f;
}

int WinsapApp::GetDpiScaledValue(int value) const
{
    return static_cast<int>(value * m_dpiScale);
}

void WinsapApp::CreateScaledFont(HWND hwnd)
{
    // Delete old font if it exists and was created by us
    if (m_hnewf) {
        DeleteObject(m_hnewf);
        m_hnewf = nullptr;
    }

    // Base font size at 96 DPI
    int baseFontSize = 14;
    int scaledSize = GetDpiScaledValue(baseFontSize);

    // Create a fixed-width font scaled for DPI
    m_hnewf = CreateFont(
        scaledSize,                 // Height (negative for character height)
        0,                          // Width (0 = auto)
        0,                          // Escapement
        0,                          // Orientation
        FW_NORMAL,                  // Weight
        FALSE,                      // Italic
        FALSE,                      // Underline
        FALSE,                      // StrikeOut
        DEFAULT_CHARSET,            // CharSet
        OUT_DEFAULT_PRECIS,         // Output precision
        CLIP_DEFAULT_PRECIS,        // Clip precision
        DEFAULT_QUALITY,            // Quality
        FIXED_PITCH | FF_MODERN,    // Pitch and family
        L"Consolas"                 // Face name (modern fixed-width font)
    );

    // Fallback to Courier New if Consolas not available
    if (!m_hnewf) {
        m_hnewf = CreateFont(
            scaledSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New"
        );
    }

    // Final fallback to stock font
    if (!m_hnewf) {
        m_hnewf = (HFONT)GetStockObject(OEM_FIXED_FONT);
    }
}

void WinsapApp::OnDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    // Get new DPI
    m_dpi = HIWORD(wParam);
    m_dpiScale = static_cast<float>(m_dpi) / 96.0f;

    // Get suggested new window rect
    RECT* prcNewWindow = (RECT*)lParam;

    // Resize window to suggested size
    SetWindowPos(hwnd, NULL,
        prcNewWindow->left, prcNewWindow->top,
        prcNewWindow->right - prcNewWindow->left,
        prcNewWindow->bottom - prcNewWindow->top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Recreate font with new DPI
    if (m_memdc) {
        SelectObject(m_memdc, m_holdf);  // Deselect current font
        CreateScaledFont(hwnd);
        SelectObject(m_memdc, m_hnewf);

        // Update text metrics
        GetTextMetrics(m_memdc, &m_tm);
        m_charHigh = m_tm.tmHeight + m_tm.tmExternalLeading;
        m_charWidth = m_tm.tmAveCharWidth;
    }

    // Update toolbar with new DPI
    m_toolbarManager.SetDpiScale(m_dpiScale);
    m_toolbarManager.RecreateToolbar(hwnd);
    m_toolbarManager.ApplyMenuIcons(m_hMenu);

    // The new monitor may be larger than any the backbuffer has covered
    EnsureBackBuffer(hwnd);

    // Resize desktop
    ResizeDesktop(hwnd);

    // Redraw
    m_updateType = upAll;
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ===================== Ensure Back Buffer =======================
// The backbuffer starts at primary screen size, but a client on a larger
// secondary monitor, or after a resolution change, can outgrow it. It only
// ever grows, so ordinary resizing does not reallocate. If the larger bitmap
// cannot be created the old one stays selected and drawing is clipped rather
// than lost.
void WinsapApp::EnsureBackBuffer(HWND hwnd)
{
    if (!m_memdc) return;

    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width < m_hbitWidth) width = m_hbitWidth;
    if (height < m_hbitHeight) height = m_hbitHeight;
    if (width <= 0 || height <= 0) return;
    if (m_hbit && width == m_hbitWidth && height == m_hbitHeight) return;

    HDC hdc = GetDC(hwnd);
    HBITMAP hbit = CreateCompatibleBitmap(hdc, width, height);
    ReleaseDC(hwnd, hdc);
    if (!hbit) return;

    SelectObject(m_memdc, hbit);
    if (m_hbit) {
        DeleteObject(m_hbit);
    }
    m_hbit = hbit;
    m_hbitWidth = width;
    m_hbitHeight = height;

    RECT fill = { 0, 0, width, height };
    FillRect(m_memdc, &fill, (HBRUSH)GetStockObject(BLACK_BRUSH));
}

// ===================== Initialize Pens =======================
void WinsapApp::InitializePens()
{
    // Create wave pens
    for (int i = 0; i < PANELMAXFILE; i++) {
        m_wavePen[i] = CreatePen(PS_SOLID, 1, m_waveColor[i]);
    }

    // Create red pen
    m_hRedPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));

    // Assign frame and text pens from existing wave pens
    m_framePen = m_wavePen[2];  // Cyan pen (waveColor[2])
    m_textPen = m_wavePen[3];   // White pen (waveColor[3])
}

// ===================== Initialize Brushes =======================
void WinsapApp::InitializeBrushes()
{
    m_hBlackBrush = CreateSolidBrush(RGB(0, 0, 0));
    m_hGreyBrush = CreateSolidBrush(RGB(128, 128, 128));
}

// ===================== Cleanup Drawing =======================
void WinsapApp::CleanupDrawing()
{
    // Delete pens (framePen and textPen are just references to wave pens, don't delete separately)
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_wavePen[i]) {
            DeleteObject(m_wavePen[i]);
            m_wavePen[i] = nullptr;
        }
    }

    // Delete red pen
    if (m_hRedPen) {
        DeleteObject(m_hRedPen);
        m_hRedPen = nullptr;
    }

    // Delete brushes
    if (m_hBlackBrush) {
        DeleteObject(m_hBlackBrush);
        m_hBlackBrush = nullptr;
    }
    if (m_hGreyBrush) {
        DeleteObject(m_hGreyBrush);
        m_hGreyBrush = nullptr;
    }

    // Restore old font
    if (m_memdc && m_holdf) {
        SelectObject(m_memdc, m_holdf);
    }

    // Delete bitmap and DC
    if (m_memdc) {
        if (m_hOldBitmap) {
            SelectObject(m_memdc, m_hOldBitmap);
        }
        DeleteDC(m_memdc);
        m_memdc = nullptr;
    }

    if (m_hbit) {
        DeleteObject(m_hbit);
        m_hbit = nullptr;
    }
}

// ===================== Resize Desktop =======================
void WinsapApp::ResizeDesktop(HWND hwnd)
{
    GetClientRect(hwnd, &m_desktopRect);

    // Adjust for toolbar and statusline using fixed heights
    int toolbarHeight = 0;
    int statusHeight = 0;

    // Get toolbar height (now just one combined toolbar)
    HWND hwndToolbar = m_toolbarManager.GetToolbarHandle(0);
    if (hwndToolbar && IsWindow(hwndToolbar)) {
        RECT rcToolbar;
        GetWindowRect(hwndToolbar, &rcToolbar);
        toolbarHeight = rcToolbar.bottom - rcToolbar.top;
    }

    HWND hwndStatus = m_toolbarManager.GetStatuslineHandle();
    // Get status bar height
    if (hwndStatus && IsWindow(hwndStatus)) {
        RECT rcStatus;
        GetWindowRect(hwndStatus, &rcStatus);
        statusHeight = rcStatus.bottom - rcStatus.top;
    }

    // Set desktop bounds excluding toolbar and statusline, this is where panels will be drawn
    m_deskBound.a.x = 0;
    m_deskBound.a.y = toolbarHeight;
    m_deskBound.b.x = m_desktopRect.right;
    m_deskBound.b.y = m_desktopRect.bottom - statusHeight;

    // Update panel rectangles based on active panels
    int midY = (m_deskBound.b.y - m_deskBound.a.y) / 2 + m_deskBound.a.y;
    bool upActive = m_upperPanel.IsActive();
    bool downActive = m_lowerPanel.IsActive();

    WRect upperRect, lowerRect;

    if (upActive && downActive) {
        // Both panels active - split screen
        upperRect.a.x = m_deskBound.a.x;
        upperRect.a.y = m_deskBound.a.y;
        upperRect.b.x = m_deskBound.b.x;
        upperRect.b.y = midY;

        lowerRect.a.x = m_deskBound.a.x;
        lowerRect.a.y = midY + 1;
        lowerRect.b.x = m_deskBound.b.x;
        lowerRect.b.y = m_deskBound.b.y;
    }
    else if (upActive) {
        // Only upper panel active - full screen
        upperRect.a.x = m_deskBound.a.x;
        upperRect.a.y = m_deskBound.a.y;
        upperRect.b.x = m_deskBound.b.x;
        upperRect.b.y = m_deskBound.b.y;
    }
    else if (downActive) {
        // Only lower panel active - full screen
        lowerRect.a.x = m_deskBound.a.x;
        lowerRect.a.y = m_deskBound.a.y;
        lowerRect.b.x = m_deskBound.b.x;
        lowerRect.b.y = m_deskBound.b.y;
    }
    else {
        // No panels active - set up for potential activation
        upperRect.a.x = m_deskBound.a.x;
        upperRect.a.y = m_deskBound.a.y;
        upperRect.b.x = m_deskBound.b.x;
        upperRect.b.y = midY;

        lowerRect.a.x = m_deskBound.a.x;
        lowerRect.a.y = midY + 1;
        lowerRect.b.x = m_deskBound.b.x;
        lowerRect.b.y = m_deskBound.b.y;
    }

    m_upperPanel.SetPanelRect(upperRect);
    m_lowerPanel.SetPanelRect(lowerRect);

    // Calculate wave box for panels
    // Label area is 10 characters wide on the left
    
    // Match separator line calculations from Panel::DrawSeparatorLines()
    // lineY1 = panelBox.a.y + charHeight + PANEL_Y_LINE_SPACE + 1 (line below Frame=)
    // lineY2 = panelBox.a.y + charHeight + 1 + PANEL_Y_LINE_SPACE + 2*(charHeight + PANEL_Y_LINE_SPACE) + 1 (line below param)
    // Wave box starts just below the separator line (lineY1 or lineY2)

    // Calculate wave box for lower panel FIRST (reference for height matching)
    WRect downWaveBox;
    downWaveBox.a.x = lowerRect.a.x + PANEL_LABEL_X_CHAR * m_charWidth;
    downWaveBox.a.y = lowerRect.a.y;
    downWaveBox.b.x = lowerRect.b.x;

    // End Y matches lineY1 from DrawSeparatorLines: panelBox.b.y - charHeight - PANEL_Y_LINE_SPACE - 1
    int lowerEndY = lowerRect.b.y - m_charHigh - PANEL_Y_LINE_SPACE - 1;
    if (m_lowerPanel.IsParActive()) {
        // When param active, matches lineY2: subtract additional 2*(charHeight + PANEL_Y_LINE_SPACE) + 1
        lowerEndY -= 2 * (m_charHigh + PANEL_Y_LINE_SPACE) + 1;
    }
    downWaveBox.b.y = lowerEndY;
    m_lowerPanel.SetWaveBox(downWaveBox);

    // Calculate wave box for upper panel
    WRect upWaveBox;
    upWaveBox.a.x = upperRect.a.x + PANEL_LABEL_X_CHAR * m_charWidth;
    upWaveBox.b.x = upperRect.b.x;

    // Start Y matches lineY1 from DrawSeparatorLines: panelBox.a.y + charHeight + PANEL_Y_LINE_SPACE + 1
    int upperStartY = upperRect.a.y + m_charHigh + PANEL_Y_LINE_SPACE + 1;
    if (m_upperPanel.IsParActive()) {
        // When param active, matches lineY2: add additional 2*(charHeight + PANEL_Y_LINE_SPACE) + 1
        upperStartY += 2 * (m_charHigh + PANEL_Y_LINE_SPACE) + 1;
    }
    upWaveBox.a.y = upperStartY;

    // When both panels are active, force upper waveBox height to match lower
    // This ensures Add Down to Up overlay aligns perfectly
    if (upActive && downActive) {
        int downWaveBoxHeight = downWaveBox.b.y - downWaveBox.a.y;
        upWaveBox.b.y = upperStartY + downWaveBoxHeight;
    } else {
        upWaveBox.b.y = upperRect.b.y;
    }
    m_upperPanel.SetWaveBox(upWaveBox);
}

// ===================== Draw =======================
void WinsapApp::Draw(HWND hwnd)
{
    // Keep the rate panes in step with whatever is currently open. The call is
    // cheap because it only touches the status bar when the state changed.
    UpdateRateStatus();

    // Clear background
    SelectObject(m_memdc, m_hBlackBrush);
    PatBlt(m_memdc, m_deskBound.a.x, m_deskBound.a.y,
        m_deskBound.b.x - m_deskBound.a.x,
        m_deskBound.b.y - m_deskBound.a.y, PATCOPY);

    // Create DrawContext for panel drawing
    DrawContext ctx = DrawContext::CreateFrom(this);

    // Draw panels
    if (m_upperPanel.IsActive()) {
        m_upperPanel.DrawFrame(ctx);

        // Set overlay for upper panel if addFile is enabled
        if (m_addFile && m_lowerPanel.IsActive()) {
            ctx.addFile = true;
            ctx.overlayPanel = &m_lowerPanel;
        }
        else {
            ctx.addFile = false;
            ctx.overlayPanel = nullptr;
        }

        m_upperPanel.Draw(ctx);
    }

    if (m_lowerPanel.IsActive()) {
        // Lower panel never has overlay
        ctx.addFile = false;
        ctx.overlayPanel = nullptr;

        m_lowerPanel.DrawFrame(ctx);
        m_lowerPanel.Draw(ctx);
    }

    m_updateType = upNone;
}

// ===================== File Operations =======================
bool WinsapApp::OpenFileUp(HWND hwnd)
{
    FileOpenDialog dialog;
    FileOpenData fileData;

    // Show custom dialog with file mode selection
    if (dialog.Show(m_hInstance, hwnd, L"Open Upper Panel File", fileData)) {
        // Check for file type conflicts (optional validation)
        if (!FileOpenDialog::CheckFileTypeConflict(hwnd, fileData.szPath, fileData.fileMode)) {
            return false;  // Conflict - user chose to abort
        }

        // Find available slot
        int index = -1;
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (m_upperPanel.GetFlags()[i] == 0) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            // Use last slot, close existing file
            index = PANELMAXFILE - 1;
            m_upperPanel.CloseFile(index);
        }

        SetupFileUp(hwnd, fileData.szPath, index, (int)fileData.fileMode);
        return true;
    }

    return false;
}

bool WinsapApp::OpenFileDown(HWND hwnd)
{
    FileOpenDialog dialog;
    FileOpenData fileData;

    // Show custom dialog with file mode selection
    if (dialog.Show(m_hInstance, hwnd, L"Open Lower Panel File", fileData)) {
        // Check for file type conflicts (optional validation)
        if (!FileOpenDialog::CheckFileTypeConflict(hwnd, fileData.szPath, fileData.fileMode)) {
            return false;  // Conflict - user chose to abort
        }

        // Find available slot
        int index = -1;
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (m_lowerPanel.GetFlags()[i] == 0) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            // Use last slot, close existing file
            index = PANELMAXFILE - 1;
            m_lowerPanel.CloseFile(index);
        }

        SetupFileDown(hwnd, fileData.szPath, index, (int)fileData.fileMode);
        return true;
    }

    return false;
}

/*****************************************************************************
*  SaveAsFile
*
*  Write the marked block of the active panel out as a mono 16 bit wav file.
*  The samples are streamed back off the disk because this build never holds
*  the whole file in memory.
*
*****************************************************************************/
bool WinsapApp::SaveAsFile(HWND hwnd)
{
    // Locate a panel holding a complete mark over a binary sample file
    Panel* pPanel = nullptr;
    for (Panel* candidate : { &m_upperPanel, &m_lowerPanel }) {
        if (!candidate->IsActive()) continue;
        if (candidate->GetMarkType() != mtAll) continue;
        if (candidate->GetFileTypes()[0] != ftBinaryShort) continue;
        if (candidate->GetFilePointer(0) == nullptr) continue;
        if (candidate->GetListenEnd() - candidate->GetListenBegin() < 1) continue;
        pPanel = candidate;
        break;
    }

    if (pPanel == nullptr) {
        MessageBox(hwnd, L"Mark a complete block over a sample file in the "
                         L"upper or lower panel first.",
                   L"Save As", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    long listenBegin = pPanel->GetListenBegin();
    long listenEnd = pPanel->GetListenEnd();
    long totalSamples = pPanel->GetAudioDataLength(0) / 2;
    if (listenBegin < 0 || listenEnd > totalSamples) {
        MessageBox(hwnd, L"The marked block extends beyond the file.",
                   L"Save As", MB_OK | MB_ICONERROR);
        return false;
    }

    // A wav file carries its own rate, otherwise fall back on the raw default
    DWORD samplingRate = pPanel->GetIsWavFlags()[0]
                             ? pPanel->GetSamplingRates()[0]
                             : m_samplingRate;

    WCHAR szFile[MAX_PATH];
    wcscpy_s(szFile, L"marked.wav");

    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwnd;
    ofn.hInstance = m_hInstance;
    ofn.lpstrFilter = L"Windows PCM files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
    ofn.lpstrTitle = L"Save Marked Block As";
    ofn.lpstrDefExt = L"wav";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    if (!GetSaveFileName(&ofn)) {
        return false;
    }

    // Read the marked samples through a private handle so that the panel's
    // own file position is left undisturbed
    size_t sampleCount = (size_t)(listenEnd - listenBegin);
    std::vector<int16_t> samples(sampleCount);

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, pPanel->GetDataFileName(0), L"rb") != 0 || fp == nullptr) {
        MessageBox(hwnd, L"Cannot reopen the source file!", L"ERROR",
                   MB_OK | MB_ICONERROR);
        return false;
    }

    long offset = pPanel->GetFileHeaderLengths()[0] + listenBegin * 2;
    bool readOk = (fseek(fp, offset, SEEK_SET) == 0) &&
                  (fread(samples.data(), 2, sampleCount, fp) == sampleCount);
    fclose(fp);

    if (!readOk) {
        MessageBox(hwnd, L"Cannot read the marked block!", L"ERROR",
                   MB_OK | MB_ICONERROR);
        return false;
    }

    if (!WriteWavFile(szFile, samples.data(), sampleCount, samplingRate)) {
        MessageBox(hwnd, L"Cannot write the wav file!", L"ERROR", MB_OK | MB_ICONERROR);
        return false;
    }

    WCHAR out[MAX_STR_LEN];
    swprintf_s(out, MAX_STR_LEN, L"Saved %zu samples at %d Hz.",
               sampleCount, samplingRate);
    m_toolbarManager.UpdateStatusline(out, STATUSBAR_FILE_INFO);
    return true;
}

void WinsapApp::SetupFileUp(HWND hwnd, const WCHAR* fileName, int index, int fileMode)
{
    WCHAR wOut[MAX_STR_LEN];
    char futf8[MAX_STR_LEN];
    unsigned long la;
    unsigned long filesize;

    // Convert wide string to multi-byte for fopen
    size_t converted;
    wcstombs_s(&converted, futf8, MAX_STR_LEN, fileName, _TRUNCATE);

    FILE* fp;
    if (fopen_s(&fp, futf8, "rb") != 0 || fp == nullptr) {
        wsprintf(wOut, L"Cannot open file %s!", fileName);
        MessageBox(hwnd, wOut, L"ERROR", MB_OK | MB_ICONERROR);
        return;
    }

    // Parameter mode and the .par extension are tied together, so a .par file
    // opens as parameters whichever mode the caller chose or remembered.
    if (IsParameterFileName(fileName)) {
        fileMode = ftParameter;
    }

    // Test if the file is a WAV file
    BOOL isWav = FALSE;
    long headerLength = 0;
    DWORD wavDataLength = 0;
    DWORD samplingRate = m_samplingRate;
    unsigned short channels = 1;
    int sampleFormat = wavPcm16;

    fread(&la, 1, 4, fp);
    if (la == 0x46464952) {  // 'RIFF'
        fread(&filesize, 1, 4, fp);
        filesize += ftell(fp);
        fread(&la, 1, 4, fp);
        if (la == 0x45564157) {  // 'WAVE'
            if (ReadWavFile(fp, &headerLength, &wavDataLength, &samplingRate, &channels,
                            &sampleFormat)) {
                // Check for stereo - reject if not mono
                if (channels > 1) {
                    fclose(fp);
                    MessageBox(hwnd, L"Stereo WAV files are supported only in the 64-bit build.\nPlease convert to mono first.",
                              L"Unsupported Format", MB_OK | MB_ICONWARNING);
                    return;
                }
                if (sampleFormat == wavUnsupported) {
                    fclose(fp);
                    MessageBox(hwnd, L"This WAV sample format is not supported.\n"
                                     L"Supported formats are 8, 16, 24 and 32-bit integer PCM "
                                     L"and 32 or 64-bit float.",
                               L"Unsupported Format", MB_OK | MB_ICONWARNING);
                    return;
                }
                // WAV files are always binary - override parameter mode if selected
                if (fileMode == ftParameter)
                    fileMode = ftBinaryShort;
                isWav = TRUE;
            }
        }
    }
    fseek(fp, 0L, SEEK_SET);

    // The panel reads 16-bit samples straight from disk, so other wav encodings
    // are converted into a temporary 16-bit wav that the panel deletes on close
    WCHAR tempDataFile[MAX_PATH] = L"";
    if (isWav && sampleFormat != wavPcm16 &&
        !ConvertWavToPcm16File(fileName, headerLength, wavDataLength, sampleFormat, samplingRate,
                               tempDataFile, _countof(tempDataFile),
                               &headerLength, &wavDataLength)) {
        fclose(fp);
        wsprintf(wOut, L"Cannot convert %s to 16-bit samples!", fileName);
        MessageBox(hwnd, wOut, L"ERROR", MB_OK | MB_ICONERROR);
        return;
    }

    // Setup the file in the panel
    if (!m_upperPanel.OpenFile(index, fileName, fileMode,
                               headerLength, static_cast<long>(wavDataLength),
                               isWav, samplingRate, sampleFormat,
                               tempDataFile[0] != L'\0' ? tempDataFile : nullptr)) {
        fclose(fp);
        wsprintf(wOut, L"Cannot load file %s!", fileName);
        MessageBox(hwnd, wOut, L"ERROR", MB_OK | MB_ICONERROR);
        return;
    }

    WCHAR status[MAX_STR_LEN];
    swprintf_s(status, L"Opened %s in upper slot %d.", fileName, index + 1);
    m_toolbarManager.UpdateStatusline(status, STATUSBAR_FILE_INFO);

    fclose(fp);

    // Resize desktop to adjust panel layout based on active panels
    ResizeDesktop(hwnd);

    // Set focus to main window for keyboard input
    SetFocus(hwnd);

    // Trigger redraw
    m_updateType = upAll;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void WinsapApp::SetupFileDown(HWND hwnd, const WCHAR* fileName, int index, int fileMode)
{
    WCHAR wOut[MAX_STR_LEN];
    char futf8[MAX_STR_LEN];
    unsigned long la;
    unsigned long filesize;

    // Convert wide string to multi-byte for fopen
    size_t converted;
    wcstombs_s(&converted, futf8, MAX_STR_LEN, fileName, _TRUNCATE);

    FILE* fp;
    if (fopen_s(&fp, futf8, "rb") != 0 || fp == nullptr) {
        wsprintf(wOut, L"Cannot open file %s!", fileName);
        MessageBox(hwnd, wOut, L"ERROR", MB_OK | MB_ICONERROR);
        return;
    }

    // Parameter mode and the .par extension are tied together, so a .par file
    // opens as parameters whichever mode the caller chose or remembered.
    if (IsParameterFileName(fileName)) {
        fileMode = ftParameter;
    }

    // Test if the file is a WAV file
    BOOL isWav = FALSE;
    long headerLength = 0;
    DWORD wavDataLength = 0;
    DWORD samplingRate = m_samplingRate;
    unsigned short channels = 1;
    int sampleFormat = wavPcm16;

    fread(&la, 1, 4, fp);
    if (la == 0x46464952) {  // 'RIFF'
        fread(&filesize, 1, 4, fp);
        filesize += ftell(fp);
        fread(&la, 1, 4, fp);
        if (la == 0x45564157) {  // 'WAVE'
            if (ReadWavFile(fp, &headerLength, &wavDataLength, &samplingRate, &channels,
                            &sampleFormat)) {
                // Check for stereo - reject if not mono
                if (channels > 1) {
                    fclose(fp);
                    MessageBox(hwnd, L"Stereo WAV files are supported only in the 64-bit build.\nPlease convert to mono first.",
                              L"Unsupported Format", MB_OK | MB_ICONWARNING);
                    return;
                }
                if (sampleFormat == wavUnsupported) {
                    fclose(fp);
                    MessageBox(hwnd, L"This WAV sample format is not supported.\n"
                                     L"Supported formats are 8, 16, 24 and 32-bit integer PCM "
                                     L"and 32 or 64-bit float.",
                               L"Unsupported Format", MB_OK | MB_ICONWARNING);
                    return;
                }
                // WAV files are always binary - override parameter mode if selected
                if (fileMode == ftParameter)
                    fileMode = ftBinaryShort;
                isWav = TRUE;
            }
        }
    }
    fseek(fp, 0L, SEEK_SET);

    // The panel reads 16-bit samples straight from disk, so other wav encodings
    // are converted into a temporary 16-bit wav that the panel deletes on close
    WCHAR tempDataFile[MAX_PATH] = L"";
    if (isWav && sampleFormat != wavPcm16 &&
        !ConvertWavToPcm16File(fileName, headerLength, wavDataLength, sampleFormat, samplingRate,
                               tempDataFile, _countof(tempDataFile),
                               &headerLength, &wavDataLength)) {
        fclose(fp);
        wsprintf(wOut, L"Cannot convert %s to 16-bit samples!", fileName);
        MessageBox(hwnd, wOut, L"ERROR", MB_OK | MB_ICONERROR);
        return;
    }

    // Setup the file in the panel
    if (!m_lowerPanel.OpenFile(index, fileName, fileMode,
                               headerLength, static_cast<long>(wavDataLength),
                               isWav, samplingRate, sampleFormat,
                               tempDataFile[0] != L'\0' ? tempDataFile : nullptr)) {
        fclose(fp);
        wsprintf(wOut, L"Cannot load file %s!", fileName);
        MessageBox(hwnd, wOut, L"ERROR", MB_OK | MB_ICONERROR);
        return;
    }

    WCHAR status[MAX_STR_LEN];
    swprintf_s(status, L"Opened %s in down slot %d.", fileName, index + 1);
    m_toolbarManager.UpdateStatusline(status, STATUSBAR_FILE_INFO);

    fclose(fp);

    // Resize desktop to adjust panel layout based on active panels
    ResizeDesktop(hwnd);

    // Set focus to main window for keyboard input
    SetFocus(hwnd);

    // Trigger redraw
    m_updateType = upAll;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ===================== Process Command Line =======================
void WinsapApp::ProcessCommandLine(HWND hwnd)
{
    if (!m_cmdLine || m_cmdLine[0] == L'\0') {
        return;  // No command line arguments
    }

    // Make a working copy of the command line since we'll modify the pointer
    WCHAR* cmdLine = m_cmdLine;
    int num = 0;  // Argument index (0, 1, 2, ...)
    int index;
    WCHAR str[MAX_PATH];

    // Parse command line arguments
    while (*cmdLine != L'\0') {
        // Skip leading spaces
        while (*cmdLine == L' ') {
            cmdLine++;
        }

        // Extract next argument into str. Explorer quotes paths containing
        // spaces, so a quoted argument runs to its closing quote. Characters
        // past the buffer are skipped, not copied, and reported as too long.
        const bool quoted = *cmdLine == L'"';
        if (quoted) {
            cmdLine++;
        }
        size_t length = 0;
        bool tooLong = false;
        while ((*cmdLine != L'\0') && (*cmdLine != (quoted ? L'"' : L' '))) {
            if (length < _countof(str) - 1) {
                str[length++] = *cmdLine;
            }
            else {
                tooLong = true;
            }
            cmdLine++;
        }
        if (quoted && *cmdLine == L'"') {
            cmdLine++;
        }
        str[length] = L'\0';

        // Process the argument if non-empty
        if (str[0] != L'\0') {
            // Check filename length
            if (tooLong || length > MAXFILENAME) {
                MessageBox(hwnd, L"Command line file name is too long!", L"ERROR",
                          MB_OK | MB_ICONERROR);
            }
            else {
                // Alternate between upper and lower panels
                index = num / 2;  // File index within panel (0, 1, 2, ...)

                if (index < PANELMAXFILE) {
                    if (num % 2 == 0) {
                        // Even arguments go to upper panel (0, 2, 4, ...)
                        SetupFileUp(hwnd, str, index, ftBinaryShort);
                    }
                    else {
                        // Odd arguments go to lower panel (1, 3, 5, ...)
                        SetupFileDown(hwnd, str, index, ftBinaryShort);
                    }
                }
                num++;
            }
        }
    }
}

// ===================== Play Commands =======================
bool WinsapApp::PlayWholeFile(HWND hwnd, Panel& panel, int fileIndex)
{
    if (!panel.IsFileOpen(fileIndex) ||
        panel.GetFileType(fileIndex) != ftBinaryShort) {
        return false;
    }

    long headerLength = panel.GetFileHeaderLengths()[fileIndex];
    long audioBytes = panel.GetAudioDataLength(fileIndex);
    if (audioBytes <= 0) {
        return false;
    }

    long totalSamples = audioBytes / sizeof(short);
    int numBlocks = static_cast<int>(totalSamples / LISTEN_BUFFER_LEN);
    int lastBlockLen = static_cast<int>(
        totalSamples - static_cast<long>(numBlocks) * LISTEN_BUFFER_LEN);

    // A wav plays at the rate stored in its own header. The raw PCM default is
    // a separate setting and must survive playing a wav.
    DWORD playRate = m_samplingRate;
    if (panel.GetIsWavFlags()[fileIndex]) {
        playRate = panel.GetSamplingRates()[fileIndex];
    }

    return PlayFileRange(hwnd, panel, fileIndex, headerLength, 0,
                         numBlocks, lastBlockLen, playRate);
}

bool WinsapApp::PlayFileRange(HWND hwnd, Panel& panel, int fileIndex, long offset,
                              long startSample, int numBlocks, int lastBlockLen,
                              DWORD samplingRate)
{
    if (m_audioPlayer.IsPlaying() ||
        panel.GetFilePointer(fileIndex) == nullptr ||
        samplingRate == 0) {
        return false;
    }

    const WCHAR* fileName = panel.GetFileName(fileIndex);
    FILE* playbackFile = nullptr;
    if (_wfopen_s(&playbackFile, panel.GetDataFileName(fileIndex), L"rb") != 0 ||
        playbackFile == nullptr) {
        WCHAR errorMessage[MAXFILENAME + 64];
        swprintf_s(errorMessage, _countof(errorMessage),
                   L"Cannot open file %s for playback.", fileName);
        MessageBox(hwnd, errorMessage, L"Playback Error", MB_OK | MB_ICONERROR);
        return false;
    }

    HWND hwndStatus = m_toolbarManager.GetStatuslineHandle();

    panel.SetPlaybackCursorSample(startSample);
    InvalidateRect(hwnd, nullptr, FALSE);

    bool completionCalled = false;
    m_audioPlayer.PlayFile(
        hwnd, playbackFile, offset, numBlocks, lastBlockLen, fileName, samplingRate,
        [this, hwnd, hwndStatus, &panel, fileName, samplingRate, startSample](long playedSamples) {
            const int64_t currentSample = static_cast<int64_t>(startSample) + playedSamples;
            panel.SetPlaybackCursorSample(currentSample);

            const int64_t totalMilliseconds = currentSample * 1000 / samplingRate;
            const int64_t hours = totalMilliseconds / 3600000;
            const int64_t minutes = (totalMilliseconds / 60000) % 60;
            const int64_t seconds = (totalMilliseconds / 1000) % 60;
            const int64_t milliseconds = totalMilliseconds % 1000;

            if (hwndStatus) {
                WCHAR statusText[768];
                swprintf_s(statusText, _countof(statusText),
                           L"%02lld:%02lld:%02lld.%03lld  Playing file: %s  "
                           L"(right-click to stop)",
                           hours, minutes, seconds, milliseconds, fileName);
                SendMessage(hwndStatus, SB_SETTEXT, 255, reinterpret_cast<LPARAM>(statusText));
            }

            InvalidateRect(hwnd, nullptr, FALSE);
        },
        [this, hwnd, &panel, &completionCalled](bool completed) {
            completionCalled = true;
            FinishPlayback(hwnd, panel, completed);
        });

    fclose(playbackFile);
    if (!completionCalled) {
        FinishPlayback(hwnd, panel, false);
    }
    return completionCalled;
}

void WinsapApp::FinishPlayback(HWND hwnd, Panel& panel, bool completed)
{
    panel.ClearPlaybackCursor();
    InvalidateRect(hwnd, nullptr, FALSE);

    if (!completed) {
        m_playbackQueue.clear();
        m_upperPanel.ClearPlaybackCursor();
        m_lowerPanel.ClearPlaybackCursor();
        m_toolbarManager.UpdateStatusline(L"Playback stopped.", STATUSBAR_FILE_INFO);
        return;
    }

    if (!m_playbackQueue.empty()) {
        StartNextPlayback(hwnd);
        return;
    }

    m_toolbarManager.UpdateStatusline(L"Playback complete.", STATUSBAR_FILE_INFO);
}

void WinsapApp::StartNextPlayback(HWND hwnd)
{
    while (!m_playbackQueue.empty() && !m_audioPlayer.IsPlaying()) {
        const PlaybackItem item = m_playbackQueue.front();
        m_playbackQueue.pop_front();
        if (item.panel != nullptr && PlayWholeFile(hwnd, *item.panel, item.fileIndex)) {
            return;
        }
    }
}

void WinsapApp::StopPlayback(HWND hwnd, const wchar_t* status)
{
    m_playbackQueue.clear();
    m_audioPlayer.Stop();
    m_upperPanel.ClearPlaybackCursor();
    m_lowerPanel.ClearPlaybackCursor();
    m_toolbarManager.UpdateStatusline(status, STATUSBAR_FILE_INFO);
    m_updateType = upAll;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void WinsapApp::PlayAll(HWND hwnd)
{
    m_playbackQueue.clear();
    for (Panel* panel : { &m_upperPanel, &m_lowerPanel }) {
        for (int i = 0; i < PANELMAXFILE; ++i) {
            if (panel->IsFileOpen(i) && panel->GetFileType(i) == ftBinaryShort) {
                m_playbackQueue.push_back({ panel, i });
            }
        }
    }
    StartNextPlayback(hwnd);
}

void WinsapApp::PlayUp(HWND hwnd)
{
    m_playbackQueue.clear();
    // Play marked region in upper panel
    if (!m_upperPanel.IsActive()) return;

    MarkType markType = m_upperPanel.GetMarkType();
    if (markType != mtAll) return;

    long listenBegin = m_upperPanel.GetListenBegin();
    long listenEnd = m_upperPanel.GetListenEnd();
    if (listenEnd - listenBegin < 1) return;

    FILE* fp = m_upperPanel.GetFilePointer(0);
    if (fp == nullptr) return;

    int* fileTypes = m_upperPanel.GetFileTypes();
    if (fileTypes[0] != ftBinaryShort) return;

    // Calculate playback parameters
    long length = listenEnd - listenBegin;
    int numBlocks = length / LISTEN_BUFFER_LEN;
    int lastBlockLen = length - (long)numBlocks * LISTEN_BUFFER_LEN;

    // A wav plays at its own rate, but the raw PCM default is a separate
    // setting and must not be overwritten here.
    BOOL* isWavFlags = m_upperPanel.GetIsWavFlags();
    DWORD* samplingRates = m_upperPanel.GetSamplingRates();
    DWORD playRate = isWavFlags[0] ? samplingRates[0] : m_samplingRate;

    // Calculate file offset
    long* fileHeaderLengths = m_upperPanel.GetFileHeaderLengths();
    long offset = listenBegin * 2 + fileHeaderLengths[0];

    // Play the marked region
    PlayFileRange(hwnd, m_upperPanel, 0, offset, listenBegin,
                  numBlocks, lastBlockLen, playRate);
}

void WinsapApp::PlayDown(HWND hwnd)
{
    m_playbackQueue.clear();
    // Play marked region in lower panel
    if (!m_lowerPanel.IsActive()) return;

    MarkType markType = m_lowerPanel.GetMarkType();
    if (markType != mtAll) return;

    long listenBegin = m_lowerPanel.GetListenBegin();
    long listenEnd = m_lowerPanel.GetListenEnd();
    if (listenEnd - listenBegin < 1) return;

    FILE* fp = m_lowerPanel.GetFilePointer(0);
    if (fp == nullptr) return;

    int* fileTypes = m_lowerPanel.GetFileTypes();
    if (fileTypes[0] != ftBinaryShort) return;

    // Calculate playback parameters
    long length = listenEnd - listenBegin;
    int numBlocks = length / LISTEN_BUFFER_LEN;
    int lastBlockLen = length - (long)numBlocks * LISTEN_BUFFER_LEN;

    // A wav plays at its own rate, but the raw PCM default is a separate
    // setting and must not be overwritten here.
    BOOL* isWavFlags = m_lowerPanel.GetIsWavFlags();
    DWORD* samplingRates = m_lowerPanel.GetSamplingRates();
    DWORD playRate = isWavFlags[0] ? samplingRates[0] : m_samplingRate;

    // Calculate file offset
    long* fileHeaderLengths = m_lowerPanel.GetFileHeaderLengths();
    long offset = listenBegin * 2 + fileHeaderLengths[0];

    // Play the marked region
    PlayFileRange(hwnd, m_lowerPanel, 0, offset, listenBegin,
                  numBlocks, lastBlockLen, playRate);
}

// ===================== Select Files Dialog =======================
// Static pointers for dialog callback to access panels
static Panel* s_pUpperPanel = nullptr;
static Panel* s_pLowerPanel = nullptr;

static BOOL CALLBACK SelectDialogFunc(HWND hDlg, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
    int i, index;

    switch (uMessage) {
    case WM_INITDIALOG:
        // Populate upper panel file list
        for (i = 0; i < PANELMAXFILE; i++) {
            if (s_pUpperPanel && s_pUpperPanel->IsFileOpen(i)) {
                SendDlgItemMessage(hDlg, IDC_UP_LIST,
                    LB_ADDSTRING, 0, (LPARAM)s_pUpperPanel->GetFileName(i));
            }
        }
        // Populate lower panel file list
        for (i = 0; i < PANELMAXFILE; i++) {
            if (s_pLowerPanel && s_pLowerPanel->IsFileOpen(i)) {
                SendDlgItemMessage(hDlg, IDC_DOWN_LIST,
                    LB_ADDSTRING, 0, (LPARAM)s_pLowerPanel->GetFileName(i));
            }
        }
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            // Get selected files from upper panel list
            index = 0;
            for (i = 0; i < PANELMAXFILE; i++) {
                if (s_pUpperPanel && s_pUpperPanel->IsFileOpen(i)) {
                    if (SendDlgItemMessage(hDlg, IDC_UP_LIST,
                        LB_GETSEL, (WPARAM)index, 0) > 0)
                        s_pUpperPanel->GetSelectFlags()[i] = 1;
                    else
                        s_pUpperPanel->GetSelectFlags()[i] = 0;
                    index++;
                }
                else if (s_pUpperPanel) {
                    s_pUpperPanel->GetSelectFlags()[i] = 0;
                }
            }

            // Get selected files from lower panel list
            index = 0;
            for (i = 0; i < PANELMAXFILE; i++) {
                if (s_pLowerPanel && s_pLowerPanel->IsFileOpen(i)) {
                    if (SendDlgItemMessage(hDlg, IDC_DOWN_LIST,
                        LB_GETSEL, (WPARAM)index, 0) > 0)
                        s_pLowerPanel->GetSelectFlags()[i] = 1;
                    else
                        s_pLowerPanel->GetSelectFlags()[i] = 0;
                    index++;
                }
                else if (s_pLowerPanel) {
                    s_pLowerPanel->GetSelectFlags()[i] = 0;
                }
            }

            EndDialog(hDlg, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

bool WinsapApp::SelectFiles(HWND hwnd)
{
    // Initialize select flags
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_upperPanel.IsFileOpen(i)) {
            m_upperPanel.GetSelectFlags()[i] = 0;
        }
        if (m_lowerPanel.IsFileOpen(i)) {
            m_lowerPanel.GetSelectFlags()[i] = 0;
        }
    }

    // Set static pointers for dialog callback
    s_pUpperPanel = &m_upperPanel;
    s_pLowerPanel = &m_lowerPanel;

    INT_PTR result = DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_LISTDIALOG),
        hwnd, (DLGPROC)SelectDialogFunc);

    // Clear static pointers
    s_pUpperPanel = nullptr;
    s_pLowerPanel = nullptr;

    return (result != -1 && result != IDCANCEL);
}
