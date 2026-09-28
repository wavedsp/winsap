#include "InputDialog.h"
#include "Resource.h"
#include <sstream>

bool InputDialog::Show(HWND hwndParent, const wchar_t* title,
                      const wchar_t* prompt, int& value)
{
    if (!prompt) {
        return false;
    }

    DialogData data;
    data.prompt = prompt;
    data.value = std::to_wstring(value);
    data.isFloat = false;

    INT_PTR result = DialogBoxParam(
        GetModuleHandle(nullptr),
        MAKEINTRESOURCE(IDD_INPUTDIALOG),
        hwndParent,
        DialogProc,
        reinterpret_cast<LPARAM>(&data)
    );

    if (result == IDOK) {
        try {
            value = std::stoi(data.value);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool InputDialog::Show(HWND hwndParent, const wchar_t* title,
                      const wchar_t* prompt, float& value)
{
    if (!prompt) {
        return false;
    }

    DialogData data;
    data.prompt = prompt;

    std::wstringstream ss;
    ss << value;
    data.value = ss.str();
    data.isFloat = true;

    INT_PTR result = DialogBoxParam(
        GetModuleHandle(nullptr),
        MAKEINTRESOURCE(IDD_INPUTDIALOG),
        hwndParent,
        DialogProc,
        reinterpret_cast<LPARAM>(&data)
    );

    if (result == IDOK) {
        try {
            value = std::stof(data.value);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

INT_PTR CALLBACK InputDialog::DialogProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    DialogData* data = nullptr;

    if (msg == WM_INITDIALOG) {
        data = reinterpret_cast<DialogData*>(lParam);
        SetWindowLongPtr(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(data));
        SetDlgItemText(hwnd, IDC_INPUT_TITLE, data->prompt);
        SetDlgItemText(hwnd, IDC_INPUT_PARAMETER, data->value.c_str());

        RECT extraWidth = { 0, 0, 110, 0 };
        MapDialogRect(hwnd, &extraWidth);
        const int widthIncrease = extraWidth.right;

        RECT dialogRect;
        GetWindowRect(hwnd, &dialogRect);
        SetWindowPos(hwnd, nullptr, 0, 0,
                     dialogRect.right - dialogRect.left + widthIncrease,
                     dialogRect.bottom - dialogRect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        const auto adjustControl = [hwnd](int controlId, int moveX, int growWidth) {
            HWND control = GetDlgItem(hwnd, controlId);
            RECT rect;
            GetWindowRect(control, &rect);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&rect), 2);
            SetWindowPos(control, nullptr, rect.left + moveX, rect.top,
                         rect.right - rect.left + growWidth, rect.bottom - rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        };
        adjustControl(IDC_INPUT_TITLE, 0, widthIncrease);
        adjustControl(IDC_INPUT_PARAMETER, 0, widthIncrease);
        adjustControl(IDOK, widthIncrease / 2, 0);
        adjustControl(IDCANCEL, widthIncrease / 2, 0);

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
                // Store font handle to delete later (store in window property)
                SetProp(hwnd, L"DialogFont", (HANDLE)hFont);
            }
        }

        // Set focus to input field and select all text
        HWND hwndInput = GetDlgItem(hwnd, IDC_INPUT_PARAMETER);
        SetFocus(hwndInput);
        SendMessage(hwndInput, EM_SETSEL, 0, -1);  // Select all text

        return FALSE;  // We set focus ourselves
    } else {
        data = reinterpret_cast<DialogData*>(GetWindowLongPtr(hwnd, DWLP_USER));
    }

    if (!data) {
        // Handle WM_DESTROY even without data to clean up font
        if (msg == WM_DESTROY) {
            HFONT hFont = (HFONT)RemoveProp(hwnd, L"DialogFont");
            if (hFont) {
                DeleteObject(hFont);
            }
        }
        return FALSE;
    }

    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            wchar_t buffer[256];
            GetDlgItemText(hwnd, IDC_INPUT_PARAMETER, buffer, 256);

            // Validate before closing dialog
            try {
                if (data->isFloat) {
                    (void)std::stof(buffer);  // Test conversion see if we catch error
                } else {
                    (void)std::stoi(buffer);  // Test conversion see if we catch error
                }
                data->value = buffer;
                EndDialog(hwnd, IDOK);
            } catch (...) {
                MessageBox(hwnd,
                    data->isFloat ? L"Please enter a valid number." : L"Please enter a valid integer.",
                    L"Invalid Input", MB_OK | MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_INPUT_PARAMETER));
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
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
