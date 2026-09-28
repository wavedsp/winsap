/************************************************************************
 *
 *  WinSAP - spectrogram display settings
 *
 *  FILE:      common/SpectrogramSettings.h
 *
 *  PURPOSE:   Loads and saves the global spectrogram contrast and colour
 *             map beside WinSap's existing window and sampling-rate values.
 *
 *  Update:    2026.1
 *
 ************************************************************************/

#pragma once

#include "Resource.h"
#include "SpectrogramCache.h"
#include "SpectrogramPalette.h"
#include <windows.h>

namespace winsap {

struct SpectrogramDisplaySettings {
    float rangeDb = kDefaultDynamicRangeDb;
    SpectrogramPalette palette = kDefaultSpectrogramPalette;
};

inline SpectrogramDisplaySettings LoadSpectrogramDisplaySettings()
{
    SpectrogramDisplaySettings settings;

    HKEY key = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, WINSAP_SETTINGS_REGISTRY_PATH, 0,
                     KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return settings;
    }

    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueEx(key, L"SpectrogramRangeDb", nullptr, &type,
                        reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && value >= 20 && value <= 120) {
        settings.rangeDb = static_cast<float>(value);
    }

    value = 0;
    size = sizeof(value);
    type = 0;
    if (RegQueryValueEx(key, L"SpectrogramPalette", nullptr, &type,
                        reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD &&
        value < static_cast<DWORD>(SpectrogramPalette::Count)) {
        settings.palette = static_cast<SpectrogramPalette>(value);
    }

    RegCloseKey(key);
    return settings;
}

inline void SaveSpectrogramSetting(const WCHAR* name, DWORD value)
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, WINSAP_SETTINGS_REGISTRY_PATH, 0, nullptr,
                       REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                       &key, &disposition) != ERROR_SUCCESS) {
        return;
    }

    RegSetValueEx(key, name, 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

inline void SaveSpectrogramRangeDb(float rangeDb)
{
    SaveSpectrogramSetting(L"SpectrogramRangeDb", static_cast<DWORD>(rangeDb));
}

inline void SaveSpectrogramPalette(SpectrogramPalette palette)
{
    SaveSpectrogramSetting(L"SpectrogramPalette", static_cast<DWORD>(palette));
}

} // namespace winsap
