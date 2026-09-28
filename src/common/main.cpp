/************************************************************************
 *
 *  WinSap C++ Version - Main Entry Point
 *
 *  FILE:      main.cpp
 *
 *  PURPOSE: Application entry point
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#include "WinsapApp.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    // Create application instance
    WinsapApp *pWinsap;

    pWinsap = new WinsapApp();
    if (!pWinsap) {
        MessageBox(nullptr, L"Failed to create application instance",
            L"Error", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    // Initialize application
    if (!pWinsap->Initialize(hInstance, nCmdShow, lpCmdLine)) {
        MessageBox(nullptr, L"Failed to initialize application",
            L"Error", MB_OK | MB_ICONERROR);
        delete pWinsap;
        return FALSE;
    }

    // Run message loop
    int result = pWinsap->Run();
    delete pWinsap;
    return result;
}
