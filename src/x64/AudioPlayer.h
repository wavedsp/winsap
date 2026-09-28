/************************************************************************
 *
 *  WinSap C++ Version - Audio Player
 *
 *  FILE:      AudioPlayer.h
 *
 *  PURPOSE:   Audio playback using Windows multimedia API
 *
 *  Update:    2026.1 Created for winsap
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <cstddef>
#include <cstdint>
#include <functional>

#define LISTEN_BUFFER_LEN   8000
#define MAX_BUFFERS         2

enum PlayStatus {
    StatusOkay,
    StatusError,
    StatusDone
};

class AudioPlayer
{
public:
    using PlaybackProgressCallback = std::function<void(size_t)>;
    using PlaybackCompletionCallback = std::function<void(bool)>;

    AudioPlayer();
    ~AudioPlayer();

    bool PlaySamples(HWND hwnd, const int16_t* samples, size_t sampleCount,
                     const wchar_t* fileName, DWORD samplingRate,
                     const PlaybackProgressCallback& progressCallback = {},
                     const PlaybackCompletionCallback& completionCallback = {});

    // Stop playback
    void Stop();
    bool IsPlaying() const { return m_playing; }

private:
    bool OpenOutputDevice(HWND hwnd, DWORD rate);
    void CloseOutputDevice();
    void AllocateBuffers();
    void CleanupBuffers();

    HWAVEOUT m_hwo;
    WAVEHDR* m_wh[MAX_BUFFERS];
    PlayStatus m_status;
    bool m_initialized;
    bool m_playable;
    bool m_playing;
    bool m_stopRequested;
};
