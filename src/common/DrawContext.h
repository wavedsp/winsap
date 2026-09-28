/************************************************************************
 *
 *  WinSap C++ Version - DrawContext
 *
 *  FILE:      DrawContext.h
 *
 *  PURPOSE: Context structure for passing drawing resources to Panel
 *
 *  Update:   2026.01
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <array>

// Forward declaration
class WinsapApp;
class ToolbarManager;
class Panel;

// Number of files per panel
#define PANELMAXFILE 6

struct DrawContext {
    HDC memDC;
    std::array<HPEN, PANELMAXFILE> wavePens;
    std::array<COLORREF, PANELMAXFILE> waveColors;
    HPEN framePen;
    HPEN redPen;         // Red pen for overlay drawing
    int charHeight;
    int charWidth;

    ToolbarManager* toolbarManager;

    // Overlay support (for "Add Down to Up" feature)
    bool addFile;        // Whether to overlay another panel's waveform
    Panel* overlayPanel; // Panel to overlay (lower panel when drawing upper)

    // Factory method to create from WinsapApp
    // Implementation in WinsapApp.cpp
    static DrawContext CreateFrom(WinsapApp* app);
};
