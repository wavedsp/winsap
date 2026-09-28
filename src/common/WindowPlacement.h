/************************************************************************
 *
 *  WinSap C++ Version - Window Placement Persistence
 *
 *  FILE:      WindowPlacement.h
 *
 *  PURPOSE: Restore the main window to its last monitor and position.
 *
 *  Update:   2026.9
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <algorithm>
#include "Resource.h"

namespace winsap {

struct SavedWindowBounds {
    int x;
    int y;
    int width;
    int height;
};

struct MonitorSearch {
    const WCHAR* device;
    HMONITOR result;
};

inline BOOL CALLBACK FindMonitorCallback(
    HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
    MonitorSearch* search = reinterpret_cast<MonitorSearch*>(parameter);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) &&
        wcscmp(info.szDevice, search->device) == 0) {
        search->result = monitor;
        return FALSE;
    }
    return TRUE;
}

inline HMONITOR FindMonitorByDevice(const WCHAR* device)
{
    if (device == nullptr || device[0] == L'\0') {
        return nullptr;
    }
    MonitorSearch search = { device, nullptr };
    EnumDisplayMonitors(nullptr, nullptr, FindMonitorCallback,
                        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

inline bool ReadRegistryDword(HKEY key, const WCHAR* name, DWORD& value)
{
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegQueryValueEx(key, name, nullptr, &type,
                           reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

inline bool ReadRegistryString(
    HKEY key, const WCHAR* name, WCHAR* value, DWORD characterCount)
{
    DWORD type = 0;
    DWORD size = characterCount * sizeof(WCHAR);
    if (RegQueryValueEx(key, name, nullptr, &type,
                        reinterpret_cast<LPBYTE>(value), &size) != ERROR_SUCCESS ||
        type != REG_SZ) {
        value[0] = L'\0';
        return false;
    }
    value[characterCount - 1] = L'\0';
    return true;
}

inline MONITORINFOEXW GetMonitorDetails(HMONITOR monitor)
{
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return info;
}

inline void ClampToWorkArea(SavedWindowBounds& bounds, const RECT& work)
{
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    bounds.width = std::clamp(bounds.width, 1, workWidth);
    bounds.height = std::clamp(bounds.height, 1, workHeight);
    bounds.x = std::clamp(bounds.x, static_cast<int>(work.left),
                          static_cast<int>(work.right) - bounds.width);
    bounds.y = std::clamp(bounds.y, static_cast<int>(work.top),
                          static_cast<int>(work.bottom) - bounds.height);
}

inline SavedWindowBounds DefaultWindowBounds(const RECT& work)
{
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    SavedWindowBounds bounds = {};
    bounds.width = (std::min)(1024, workWidth);
    bounds.height = (std::min)(768, workHeight);
    bounds.x = work.left + (workWidth - bounds.width) / 2;
    bounds.y = work.top + (workHeight - bounds.height) / 2;
    return bounds;
}

inline SavedWindowBounds LoadWindowBounds()
{
    const POINT origin = { 0, 0 };
    const HMONITOR primary =
        MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    const MONITORINFOEXW primaryInfo = GetMonitorDetails(primary);
    SavedWindowBounds bounds = DefaultWindowBounds(primaryInfo.rcWork);

    HKEY key = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, WINSAP_SETTINGS_REGISTRY_PATH, 0,
                     KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return bounds;
    }

    DWORD rawX = 0;
    DWORD rawY = 0;
    DWORD rawWidth = 0;
    DWORD rawHeight = 0;
    const bool hasBounds =
        ReadRegistryDword(key, L"x", rawX) &&
        ReadRegistryDword(key, L"y", rawY) &&
        ReadRegistryDword(key, L"width", rawWidth) &&
        ReadRegistryDword(key, L"height", rawHeight);
    if (!hasBounds) {
        RegCloseKey(key);
        return bounds;
    }

    bounds.x = static_cast<LONG>(rawX);
    bounds.y = static_cast<LONG>(rawY);
    bounds.width = static_cast<int>(rawWidth);
    bounds.height = static_cast<int>(rawHeight);

    WCHAR savedDevice[CCHDEVICENAME] = {};
    const bool hasDevice =
        ReadRegistryString(key, L"Monitor", savedDevice, CCHDEVICENAME);
    DWORD rawOffsetX = 0;
    DWORD rawOffsetY = 0;
    const bool hasOffsets =
        ReadRegistryDword(key, L"MonitorOffsetX", rawOffsetX) &&
        ReadRegistryDword(key, L"MonitorOffsetY", rawOffsetY);
    RegCloseKey(key);

    HMONITOR target = hasDevice ? FindMonitorByDevice(savedDevice) : nullptr;
    if (target == nullptr && !hasDevice) {
        RECT savedRect = {
            bounds.x, bounds.y,
            bounds.x + bounds.width, bounds.y + bounds.height
        };
        target = MonitorFromRect(&savedRect, MONITOR_DEFAULTTONULL);
    }

    if (target == nullptr) {
        target = primary;
    }
    const MONITORINFOEXW targetInfo = GetMonitorDetails(target);

    if (hasOffsets && (hasDevice || target == primary)) {
        bounds.x = targetInfo.rcWork.left + static_cast<LONG>(rawOffsetX);
        bounds.y = targetInfo.rcWork.top + static_cast<LONG>(rawOffsetY);
    }
    ClampToWorkArea(bounds, targetInfo.rcWork);
    return bounds;
}

inline void SaveWindowBounds(HWND window)
{
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    RECT rect = {};
    if (GetWindowPlacement(window, &placement)) {
        rect = placement.rcNormalPosition;
    } else {
        GetWindowRect(window, &rect);
    }

    const HMONITOR monitor =
        MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    const MONITORINFOEXW info = GetMonitorDetails(monitor);

    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, WINSAP_SETTINGS_REGISTRY_PATH, 0,
                       nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                       nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return;
    }

    const LONG x = rect.left;
    const LONG y = rect.top;
    const DWORD width = static_cast<DWORD>(rect.right - rect.left);
    const DWORD height = static_cast<DWORD>(rect.bottom - rect.top);
    const LONG offsetX = rect.left - info.rcWork.left;
    const LONG offsetY = rect.top - info.rcWork.top;

    RegSetValueEx(key, L"x", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&x), sizeof(x));
    RegSetValueEx(key, L"y", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&y), sizeof(y));
    RegSetValueEx(key, L"width", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&width), sizeof(width));
    RegSetValueEx(key, L"height", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&height), sizeof(height));
    RegSetValueEx(key, L"Monitor", 0, REG_SZ,
                  reinterpret_cast<const BYTE*>(info.szDevice),
                  static_cast<DWORD>((wcslen(info.szDevice) + 1) * sizeof(WCHAR)));
    RegSetValueEx(key, L"MonitorOffsetX", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&offsetX), sizeof(offsetX));
    RegSetValueEx(key, L"MonitorOffsetY", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&offsetY), sizeof(offsetY));
    RegCloseKey(key);
}

} // namespace winsap
