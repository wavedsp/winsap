#pragma once
#include <windows.h>
#include <string>

class InputDialog {
public:
    // Show input dialog and return entered value
    static bool Show(HWND hwndParent, const wchar_t* title,
                    const wchar_t* prompt, int& value);

    static bool Show(HWND hwndParent, const wchar_t* title,
                    const wchar_t* prompt, float& value);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam);

    struct DialogData {
        const wchar_t* prompt;
        std::wstring value;
        bool isFloat;
    };
};
