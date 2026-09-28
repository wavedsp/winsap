/************************************************************************
 *
 *  WinSap C++ Version - Custom File Open Dialog Implementation
 *
 *  FILE:      FileOpenDialog.cpp
 *
 *  PURPOSE: Implementation of the custom file open dialog that provides
 *           file mode selection in addition to standard file selection.
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#include "FileOpenDialog.h"
#include "Resource.h"
#include "FileDialogs.h"
#include <string>

namespace {

const WCHAR* kWarnParameterExtension = L"WarnParameterExtensionMismatch";
const WCHAR* kWarnBinaryExtension = L"WarnBinaryExtensionMismatch";

bool WarningEnabled(const WCHAR* valueName)
{
    HKEY key = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, WINSAP_SETTINGS_REGISTRY_PATH, 0,
                     KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return true;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS result =
        RegQueryValueEx(key, valueName, nullptr, &type,
                        reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return result != ERROR_SUCCESS || type != REG_DWORD || value != 0;
}

bool DisableWarning(const WCHAR* valueName)
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, WINSAP_SETTINGS_REGISTRY_PATH, 0, nullptr,
                       REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                       &key, &disposition) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD disabled = 0;
    const LSTATUS result =
        RegSetValueEx(key, valueName, 0, REG_DWORD,
                      reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool IsBinaryExtension(const std::wstring& extension)
{
    return extension == L".wav" || extension == L".pcm" ||
           extension == L".dat" || extension == L".raw" ||
           extension == L".exc" || extension == L".res" ||
           extension == L".cod";
}

} // namespace

/************************************************************************
 * Constructor
 ************************************************************************/
FileOpenDialog::FileOpenDialog()
{
    // Nothing to initialize
}

/************************************************************************
 * Destructor
 ************************************************************************/
FileOpenDialog::~FileOpenDialog()
{
    // Nothing to clean up
}

/************************************************************************
 * Show
 *
 * Displays the custom file open dialog with a file mode control.
 *
 * Parameters:
 *   hInstance - Application instance handle
 *   hwndOwner - Parent window handle
 *   title     - Dialog title text
 *   data      - FileOpenData structure to receive selections
 *
 * Returns:
 *   true if user selected a file and clicked OK
 *   false if user cancelled the dialog
 ************************************************************************/
bool FileOpenDialog::Show(HINSTANCE hInstance, HWND hwndOwner, const WCHAR* title, FileOpenData& data)
{
    OPENFILENAME ofn = { 0 };
    WCHAR szFile[MAX_PATH] = { 0 };

    // Initialize OPENFILENAME structure
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwndOwner;
    ofn.hInstance = hInstance;
    ofn.lpstrFilter = fileOpenFilters;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_EXPLORER |
                OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpfnHook = DialogHookProc;
    ofn.lpTemplateName = MAKEINTRESOURCE(IDD_FILEOPEN);
    ofn.lCustData = (LPARAM)&data;

    // Show the dialog
    BOOL result = GetOpenFileName(&ofn);

    if (result) {
        // Copy the selected file path to the data structure
        wcsncpy_s(data.szPath, MAX_PATH, szFile, _TRUNCATE);
        return true;
    }

    return false;
}

/************************************************************************
 * DialogHookProc (static)
 *
 * Hook procedure for the file open common dialog.
 * Handles initialization, cleanup, and notifications.
 *
 * Parameters:
 *   hdlg   - Handle to the child dialog (our custom template)
 *   uiMsg  - Message identifier
 *   wParam - Message parameter
 *   lParam - Message parameter (OPENFILENAME* on WM_INITDIALOG)
 *
 * Returns:
 *   TRUE if message was processed, FALSE otherwise
 ************************************************************************/
UINT_PTR CALLBACK FileOpenDialog::DialogHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uiMsg) {
        case WM_INITDIALOG:
        {
            // Save pointer to OPENFILENAME structure for later use
            SetWindowLongPtr(hdlg, DWLP_USER, lParam);

            // Initialize the custom controls
            InitializeControls(hdlg);

            return TRUE;
        }

        case WM_DESTROY:
        {
            // Retrieve the OPENFILENAME structure
            LPOPENFILENAME lpOFN = (LPOPENFILENAME)GetWindowLongPtr(hdlg, DWLP_USER);
            if (lpOFN != nullptr) {
                FileOpenData* pData = (FileOpenData*)lpOFN->lCustData;
                if (pData != nullptr) {
                    // Capture the user's selections before dialog closes
                    CaptureSelections(hdlg, pData);
                }
            }
            return TRUE;
        }

        case WM_NOTIFY:
        {
            // Process common dialog notifications
            LPOFNOTIFY pofn = (LPOFNOTIFY)lParam;
            return HandleNotification(hdlg, pofn);
        }

        default:
            return FALSE;
    }
}

/************************************************************************
 * InitializeControls (static)
 *
 * Initializes the combo boxes with available options.
 * Called during WM_INITDIALOG processing.
 *
 * Parameters:
 *   hdlg - Handle to the child dialog
 ************************************************************************/
void FileOpenDialog::InitializeControls(HWND hdlg)
{
    // Get the parent dialog (the common file dialog)
    HWND hwndParent = GetParent(hdlg);
    
    // Get the font used by the common dialog
    HFONT hDialogFont = (HFONT)SendMessage(hwndParent, WM_GETFONT, 0, 0);
    
    // If we got a font, apply it to all child controls in our custom area
    if (hDialogFont) {
        // Enumerate all child windows and apply the font
        HWND hwndChild = GetWindow(hdlg, GW_CHILD);
        while (hwndChild) {
            SendMessage(hwndChild, WM_SETFONT, (WPARAM)hDialogFont, TRUE);
            hwndChild = GetWindow(hwndChild, GW_HWNDNEXT);
        }
    }

    // Populate the File Mode combo box
    SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_ADDSTRING, 0, (LPARAM)L"Binary");
    SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_ADDSTRING, 0, (LPARAM)L"Parameter");
    SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_ADDSTRING, 0, (LPARAM)L"FFT Amplitude");

    // Set default selection to Binary (index 0)
    SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_SETCURSEL, 0, 0);

}

/************************************************************************
 * CaptureSelections (static)
 *
 * Captures the user's selections from the combo boxes and stores them
 * in the FileOpenData structure. Called during WM_DESTROY processing.
 *
 * Parameters:
 *   hdlg  - Handle to the child dialog
 *   pData - Pointer to FileOpenData structure to receive selections
 ************************************************************************/
void FileOpenDialog::CaptureSelections(HWND hdlg, FileOpenData* pData)
{
    if (pData == nullptr) {
        return;
    }

    // Get the path from the edit control
    GetDlgItemText(hdlg, IDE_PATH, pData->szPath, MAX_PATH);

    // Get the file mode selection
    int modeIndex = (int)SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_GETCURSEL, 0, 0);
    if (modeIndex == CB_ERR) {
        modeIndex = 0; // Default to Binary
    }
    pData->fileMode = static_cast<LoadFileType>(modeIndex);

}

/************************************************************************
 * HandleNotification (static)
 *
 * Processes WM_NOTIFY messages from the common dialog.
 * Handles selection changes and filter type changes.
 *
 * Parameters:
 *   hdlg - Handle to the child dialog
 *   pofn - Pointer to OFNOTIFY structure
 *
 * Returns:
 *   TRUE if notification was processed
 ************************************************************************/
BOOL FileOpenDialog::HandleNotification(HWND hdlg, LPOFNOTIFY pofn)
{
    if (pofn == nullptr) {
        return FALSE;
    }

    switch (pofn->hdr.code) {
        case CDN_SELCHANGE:
        {
            // User selected a different file - update path display
            HWND hwndParent = GetParent(hdlg);
            UpdatePathDisplay(hdlg, hwndParent);
            return TRUE;
        }

        case CDN_TYPECHANGE:
        {
            // File type filter changed - auto-select appropriate mode
            if (pofn->lpOFN != nullptr) {
                AutoSelectMode(hdlg, pofn->lpOFN->nFilterIndex);
            }
            return TRUE;
        }

        default:
            return FALSE;
    }
}

/************************************************************************
 * UpdatePathDisplay (static)
 *
 * Updates the IDE_PATH edit control with the currently selected file path.
 * Called when the user selects a different file in the dialog.
 *
 * Parameters:
 *   hdlg        - Handle to the child dialog
 *   hwndParent  - Handle to the parent dialog (common dialog)
 ************************************************************************/
void FileOpenDialog::UpdatePathDisplay(HWND hdlg, HWND hwndParent)
{
    WCHAR szFile[MAX_PATH];

    // Get the full path of the selected file from the parent dialog
    if (CommDlg_OpenSave_GetFilePath(hwndParent, szFile, MAX_PATH) <= MAX_PATH) {
        // Ensure null-termination
        szFile[MAX_PATH - 1] = L'\0';

        // Update the path display control
        SetDlgItemText(hdlg, IDE_PATH, szFile);
    }
}

/************************************************************************
 * AutoSelectMode (static)
 *
 * Automatically selects the appropriate file mode based on the selected
 * file type filter. For example, when the user selects "Parameter Files",
 * the mode is automatically set to "Parameter".
 *
 * Parameters:
 *   hdlg        - Handle to the child dialog
 *   filterIndex - Selected filter index (1-based)
 ************************************************************************/
void FileOpenDialog::AutoSelectMode(HWND hdlg, int filterIndex)
{
    // Filter index 4 corresponds to Parameter files (.par)
    // Auto-select "Parameter" mode for these files
    if (filterIndex == 4) {
        SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_SETCURSEL, 1, 0); // Parameter = index 1
    }
    else {
        // For all other filters, default to Binary mode
        SendDlgItemMessage(hdlg, IDC_FILEMODE, CB_SETCURSEL, 0, 0); // Binary = index 0
    }
}

/************************************************************************
 * CheckFileTypeConflict (static)
 *
 * Checks if the selected file type is compatible with the selected mode.
 * Displays a warning message if there's a potential conflict.
 *
 * Parameters:
 *   hwndOwner - Parent window for message box
 *   fileName  - Selected file name
 *   mode      - Selected file mode
 *
 * Returns:
 *   true if no conflict, the warning was disabled, or the user continues
 *   false if user wants to cancel
 ************************************************************************/
bool FileOpenDialog::CheckFileTypeConflict(HWND hwndOwner, const WCHAR* fileName,
                                          LoadFileType mode)
{
    if (fileName == nullptr || wcslen(fileName) == 0) {
        return true;
    }

    const WCHAR* ext = wcsrchr(fileName, L'.');
    std::wstring extension = ext != nullptr ? ext : L"";
    for (auto& c : extension) {
        c = towlower(c);
    }

    const bool parameterMismatch = mode == ftParameter && extension != L".par";
    const bool binaryMismatch = mode != ftParameter && !IsBinaryExtension(extension);
    if (!parameterMismatch && !binaryMismatch) {
        return true;
    }

    const WCHAR* valueName =
        parameterMismatch ? kWarnParameterExtension : kWarnBinaryExtension;
    if (!WarningEnabled(valueName)) {
        return true;
    }

    const WCHAR* warning =
        parameterMismatch
            ? L"You selected Parameter mode, but the file does not have a .par extension.\n\n"
              L"Select OK to continue and stop showing this parameter-extension warning. "
              L"Select Cancel to stop opening the file."
            : L"You selected Binary or FFT Amplitude mode, but the file extension is not "
              L".wav, .pcm, .dat, .raw, .exc, .res, or .cod.\n\n"
              L"Select OK to continue and stop showing this binary/spectrum-extension warning. "
              L"Select Cancel to stop opening the file.";
    const int result = MessageBox(hwndOwner, warning, L"File Type Mismatch",
                                  MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    if (result != IDOK) {
        return false;
    }

    if (!DisableWarning(valueName)) {
        MessageBox(hwndOwner,
                   L"The file will still be opened, but WinSap could not save "
                   L"your warning preference. The warning may appear again.",
                   L"Warning Preference Not Saved",
                   MB_OK | MB_ICONWARNING);
    }
    return true;
}
