/************************************************************************
 *
 *  WinSap C++ Version - Custom File Open Dialog
 *
 *  FILE:      FileOpenDialog.h
 *
 *  PURPOSE: Custom file open dialog with file mode selection.
 *           Restores the functionality from the original winsap C program
 *           that uses the IDD_FILEOPEN dialog resource.
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <commdlg.h>
#include "Panel.h"

// Forward declaration
struct FileOpenData;

/************************************************************************
 * FileOpenData Structure
 *
 * Holds the user selections from the custom file open dialog:
 * - szPath: Full path to selected file
 * - fileMode: LoadFileType enum value (Binary, Parameter, FFT Amplitude, FFT Phase)
 ************************************************************************/
struct FileOpenData {
    WCHAR szPath[MAX_PATH];      // Full path to the selected file
    LoadFileType fileMode;        // File type/mode selection
    FileOpenData()
        : fileMode(ftBinaryShort)
    {
        szPath[0] = L'\0';
    }
};

/************************************************************************
 * FileOpenDialog Class
 *
 * Encapsulates the custom file open dialog functionality.
 * Uses the Windows common dialog with a custom template (IDD_FILEOPEN)
 * to provide an additional control for file mode selection.
 ************************************************************************/
class FileOpenDialog {
public:
    FileOpenDialog();
    ~FileOpenDialog();

    // Show the custom file open dialog
    // Returns: true if user selected a file and clicked OK, false if cancelled
    bool Show(HINSTANCE hInstance, HWND hwndOwner, const WCHAR* title, FileOpenData& data);
    // Check for file type conflicts and warn user
    static bool CheckFileTypeConflict(HWND hwndOwner, const WCHAR* fileName, LoadFileType mode);

private:
    // Dialog hook procedure for the file open common dialog
    static UINT_PTR CALLBACK DialogHookProc(HWND hdlg, UINT uiMsg,
                                            WPARAM wParam, LPARAM lParam);

    // WM_INITDIALOG handler - Initialize combo boxes
    static void InitializeControls(HWND hdlg);

    // WM_DESTROY handler - Capture user selections
    static void CaptureSelections(HWND hdlg, FileOpenData* pData);

    // WM_NOTIFY handler - Process common dialog notifications
    static BOOL HandleNotification(HWND hdlg, LPOFNOTIFY pofn);

    // Update the path display control (IDE_PATH) with selected file
    static void UpdatePathDisplay(HWND hdlg, HWND hwndParent);

    // Auto-select file mode based on filter selection
    static void AutoSelectMode(HWND hdlg, int filterIndex);

};
