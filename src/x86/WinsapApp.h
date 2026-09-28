/************************************************************************
 *
 *  WinSap C++ Version - Main Application Header
 *
 *  FILE:      WinsapApp.h
 *
 *  PURPOSE: Main application class that manages panels and UI
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <commctrl.h>
#include <deque>
#include "Panel.h"
#include "ToolbarManager.h"
#include "FileOpenDialog.h"
#include "AudioPlayer.h"

#define MAX_STR_LEN 2000

// Update types
enum UpdateType {
    upNone = 0,
    upUp,
    upDown,
    upWindow,
    upAll
};

// Color definitions
enum ColorName {
    coNone = -1,
    coBlack = 0,
    coBlue = 1,
    coGreen = 2,
    coCyan = 3,
    coRed = 4,
    coViolet = 5,
    coBrown = 6,
    coWhite = 7,
    coGrey = 8,
    coBrightBlue = 9,
    coBrightGreen = 10,
    coBrightCyan = 11,
    coBrightRed = 12,
    coBrightViolet = 13,
    coYellow = 14,
    coBrightWhite = 15
};

// panel box constants
uint32_t const PANEL_LABEL_X_CHAR = 10;     // X offset 10 chars for labels
uint32_t const PANEL_FRAME_INFO_Y_CHAR = 1; // Y offset 1 line for showing Frame number length duration and file names
uint32_t const PANEL_PAR_INFO_Y_CHAR = 2;   // Y offset 2 lines for showing Parameter file info
uint32_t const PANEL_Y_LINE_SPACE = 2;      // Y offset pixels between lines
uint32_t const PANEL_OFFSET_Y_MARGIN = 4;   // Y offset leave 4 pixels for wave box margin needed for drawing frame lines

// ===================== WinsapApp Class =======================
class WinsapApp {
public:
    // Constructor and Destructor
    WinsapApp();
    ~WinsapApp();

    // Initialization
    bool Initialize(HINSTANCE hInstance, int nCmdShow, LPWSTR lpCmdLine);
    void Cleanup();

    // Message loop
    int Run();

    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Panel access
    Panel* GetUpperPanel() { return &m_upperPanel; }
    Panel* GetLowerPanel() { return &m_lowerPanel; }

    // Toolbar access
    ToolbarManager* GetToolbarManager() { return &m_toolbarManager; }

    // Drawing
    void Draw(HWND hwnd);
    void CleanupDrawing();
    void ResizeDesktop(HWND hwnd);

    // File operations
    bool OpenFileUp(HWND hwnd);
    bool OpenFileDown(HWND hwnd);
    bool SaveAsFile(HWND hwnd);
    bool SelectFiles(HWND hwnd);  // Show dialog to select files for close/play operations

    // Audio playback commands
    void PlayAll(HWND hwnd);
    void PlayUp(HWND hwnd);
    void PlayDown(HWND hwnd);
    void SetupFileUp(HWND hwnd, const WCHAR* fileName, int index, int fileMode);
    void SetupFileDown(HWND hwnd, const WCHAR* fileName, int index, int fileMode);

    // Update management
    void SetUpdateType(UpdateType type) { m_updateType = type; }
    UpdateType GetUpdateType() const { return m_updateType; }

    // Instance handle
    HINSTANCE GetInstance() const { return m_hInstance; }
    HWND GetMainWindow() const { return m_hwndMain; }

    // Sampling rate
    DWORD GetSamplingRate() const { return m_samplingRate; }
    void SetSamplingRate(DWORD rate) { m_samplingRate = rate; }

    // Command line
    LPWSTR GetCommandLine() const { return m_cmdLine; }

    // Public access to GDI objects (for drawing functions)
    HDC GetMemDC() const { return m_memdc; }
    int GetCharHigh() const { return m_charHigh; }
    int GetCharWidth() const { return m_charWidth; }
    HPEN GetWavePen(int index) const { return m_wavePen[index]; }
    COLORREF GetWaveColor(int index) const { return m_waveColor[index]; }
    HPEN GetFramePen() const { return m_framePen; }
    HBRUSH GetBlackBrush() const { return m_hBlackBrush; }
    HBRUSH GetGreyBrush() const { return m_hGreyBrush; }
    HPEN GetRedPen() const { return m_hRedPen; }

private:
    // Window registration and creation
    bool RegisterWindowClass();
    bool CreateMainWindow(int nCmdShow);

    // Message handlers
    void OnCreate(HWND hwnd);
    void OnDestroy();
    void OnSize(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hwnd);
    void OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnLButtonDown(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnRButtonDown(HWND hwnd, WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnDrawItem(HWND hwnd, WPARAM wParam, LPARAM lParam);

    // Status bar sampling rate panes
    void UpdateRateStatus();
    void ApplyRawSamplingRate(DWORD rate);

    // Spectrogram contrast and colour map, applied to every panel holding files
    void AdjustSpectrogramRange(HWND hwnd, float deltaDb);
    void CycleSpectrogramPalette(HWND hwnd);
    bool PlayWholeFile(HWND hwnd, Panel& panel, int fileIndex);
    bool PlayFileRange(HWND hwnd, Panel& panel, int fileIndex, long offset,
                       long startSample, int numBlocks, int lastBlockLen,
                       DWORD samplingRate);
    void StartNextPlayback(HWND hwnd);
    void FinishPlayback(HWND hwnd, Panel& panel, bool completed);
    void StopPlayback(HWND hwnd, const wchar_t* status);
    void ShowHelpTopics(HWND hwnd);
    void SyncLockedLowerStart();

    // DPI handling
    void InitializeDpi(HWND hwnd);
    int GetDpiScaledValue(int value) const;
    void CreateScaledFont(HWND hwnd);

    // Drawing initialization
    void InitializeGDI(HWND hwnd);
    void InitializePens();
    void InitializeBrushes();

    // Command line processing
    void ProcessCommandLine(HWND hwnd);

    // Member variables
    HINSTANCE m_hInstance;
    HWND m_hwndMain;
    LPWSTR m_cmdLine;

    // Panels
    Panel m_upperPanel;
    Panel m_lowerPanel;

    // Toolbar
    ToolbarManager m_toolbarManager;

    // Audio playback
    AudioPlayer m_audioPlayer;
    struct PlaybackItem {
        Panel* panel;
        int fileIndex;
    };
    std::deque<PlaybackItem> m_playbackQueue;

    // Update state
    UpdateType m_updateType;

    // Frame synchronization
    bool m_lockStartFrame;  // Lock upper/lower panel start frames together
    bool m_addFile;  // Overlay lower panel waveform onto upper panel in red
    HMENU m_hMenu;  // Main menu handle for CheckMenuItem

    // Sampling rate
    DWORD m_samplingRate;

    // Status bar rate panes. The text is cached here because both panes are
    // owner drawn so that they can appear greyed out or in red.
    WCHAR m_rawRateText[32];
    WCHAR m_wavRateText[32];
    bool m_rawRateActive;   // A raw PCM stream is open, so the rate applies
    bool m_wavRateMixed;    // Open files disagree about the sampling rate

    // GDI objects
    HDC m_memdc;
    HBITMAP m_hbit;
    HBITMAP m_hOldBitmap;
    HBRUSH m_hbrush;
    HPEN m_hOldpen;
    HPEN m_hCurpen;

    // Pens for drawing
    HPEN m_wavePen[PANELMAXFILE];
    HPEN m_framePen;
    HPEN m_textPen;
    HPEN m_hRedPen;

    // Brushes
    HBRUSH m_hBlackBrush;
    HBRUSH m_hGreyBrush;

    // Wave colors
    COLORREF m_waveColor[PANELMAXFILE];

    // Font metrics
    TEXTMETRIC m_tm{};
    HFONT m_holdf;
    HFONT m_hnewf;
    int m_charHigh;
    int m_charWidth;

    // DPI scaling
    UINT m_dpi;
    float m_dpiScale;

    // Desktop bounds
    RECT m_desktopRect;
    WRect m_deskBound;

    // File dialog state
    DWORD m_dwUpFilterIndex;
    DWORD m_dwDownFilterIndex;

    // Static window procedure helper
    static WinsapApp* s_pApp;
};
