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
#include <stdio.h>
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
    using PlaybackProgressCallback = std::function<void(long)>;
    using PlaybackCompletionCallback = std::function<void(bool)>;

    AudioPlayer();
    ~AudioPlayer();

    // Play entire file
    bool PlayFile(HWND hwnd, FILE* fp, long offset, int numBlocks, int lastBlockLen,
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
    void* m_playbackBuffer;
    PlayStatus m_status;
    bool m_initialized;
    bool m_playable;
    bool m_playing;
    bool m_stopRequested;
};
