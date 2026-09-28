/************************************************************************
 *
 *  WinSap C++ Version - Audio Player Implementation
 *
 *  FILE:      AudioPlayer.cpp
 *
 *  PURPOSE:   Audio playback using Windows multimedia API
 *
 *  Update:    2026.1 Created for winsap
 *
 ************************************************************************/

#include "AudioPlayer.h"
#include <commctrl.h>
#include <string.h>

AudioPlayer::AudioPlayer()
    : m_hwo(nullptr)
    , m_status(StatusOkay)
    , m_initialized(false)
    , m_playable(true)
    , m_playing(false)
    , m_stopRequested(false)
{
    for (int i = 0; i < MAX_BUFFERS; i++) {
        m_wh[i] = nullptr;
    }
}

AudioPlayer::~AudioPlayer()
{
    CleanupBuffers();
    CloseOutputDevice();
}

void AudioPlayer::AllocateBuffers()
{
    if (m_initialized) return;

    // Allocate WAVEHDR buffers
    for (int i = 0; i < MAX_BUFFERS; i++) {
        m_wh[i] = (WAVEHDR*)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY,
            sizeof(WAVEHDR));
        if (m_wh[i] != nullptr) {
            m_wh[i]->lpData = (char*)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY,
                LISTEN_BUFFER_LEN * sizeof(short));
            if (m_wh[i]->lpData == nullptr) {
                m_playable = false;
                return;
            }
            m_wh[i]->dwBufferLength = LISTEN_BUFFER_LEN * sizeof(short);
        }
        else {
            m_playable = false;
            return;
        }
    }

    m_initialized = true;
}

void AudioPlayer::CleanupBuffers()
{
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (m_wh[i] != nullptr) {
            if (m_wh[i]->lpData != nullptr) {
                HeapFree(GetProcessHeap(), 0, m_wh[i]->lpData);
                m_wh[i]->lpData = nullptr;
            }
            HeapFree(GetProcessHeap(), 0, m_wh[i]);
            m_wh[i] = nullptr;
        }
    }

    m_initialized = false;
}

bool AudioPlayer::OpenOutputDevice(HWND hwnd, DWORD rate)
{
    WAVEOUTCAPS woc;
    WAVEFORMATEX wfx;
    UINT nDevId;
    MMRESULT rc = MMSYSERR_NODRIVER;
    UINT nMaxDevices = waveOutGetNumDevs();

    m_hwo = nullptr;
    m_status = StatusOkay;

    for (nDevId = 0; nDevId < nMaxDevices; nDevId++) {
        rc = waveOutGetDevCaps(nDevId, &woc, sizeof(woc));

        if (rc == MMSYSERR_NOERROR) {
            wfx.nChannels = 1;              // mono
            wfx.nSamplesPerSec = rate;      // sampling rate
            wfx.wFormatTag = WAVE_FORMAT_PCM;
            wfx.wBitsPerSample = 16;
            wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
            wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
            wfx.cbSize = 0;

            rc = waveOutOpen(&m_hwo, nDevId, &wfx, 0, 0, CALLBACK_NULL);

            if (rc == MMSYSERR_NOERROR)
                break;
            else
                m_hwo = nullptr;
        }
    }

    if (rc != MMSYSERR_NOERROR) {
        wchar_t errorMsg[256];
        waveOutGetErrorText(rc, errorMsg, 256);
        MessageBox(hwnd, errorMsg, L"ERROR", MB_OK | MB_ICONERROR);
        return false;
    }

    if (m_hwo == nullptr)
        return false;

    return true;
}

void AudioPlayer::CloseOutputDevice()
{
    if (m_hwo != nullptr) {
        waveOutReset(m_hwo);
        waveOutClose(m_hwo);
        m_hwo = nullptr;
    }
}

bool AudioPlayer::PlaySamples(HWND hwnd, const int16_t* samples, size_t sampleCount,
                              const wchar_t* fileName, DWORD samplingRate,
                              const PlaybackProgressCallback& progressCallback,
                              const PlaybackCompletionCallback& completionCallback)
{
    if (m_playing) {
        return false;
    }

    if (samples == nullptr || sampleCount == 0 || samplingRate == 0) {
        return false;
    }

    if (!m_playable) {
        MessageBox(hwnd, L"The audio file cannot be played due to insufficient memory!",
                   L"Information", MB_OK | MB_ICONWARNING);
        return false;
    }

    // Allocate buffers if not already done
    if (!m_initialized) {
        AllocateBuffers();
        if (!m_playable) {
            MessageBox(hwnd, L"Failed to allocate playback buffers!",
                       L"Error", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    // Open output device
    if (!OpenOutputDevice(hwnd, samplingRate)) {
        MessageBox(hwnd, L"Failed to open the wave output device!",
                   L"Error", MB_OK | MB_ICONERROR);
        return false;
    }
    m_playing = true;
    m_stopRequested = false;

    // Set status bar to simple mode
    HWND hwndStatus = FindWindowEx(hwnd, nullptr, STATUSCLASSNAME, nullptr);
    if (hwndStatus) {
        SendMessage(hwndStatus, SB_SIMPLE, TRUE, 0);
        wchar_t statusText[512];
        swprintf_s(statusText, 512, L"Playing file: %s,  Press RIGHT mouse button to stop playing!", fileName);
        SendMessage(hwndStatus, SB_SETTEXT, 255, (LPARAM)statusText);
    }

    const size_t totalSamples = sampleCount;
    size_t nextSample = 0;
    bool queued[MAX_BUFFERS] = {};
    bool prepared[MAX_BUFFERS] = {};
    DWORD lastProgressTick = 0;

    auto showWaveError = [hwnd](MMRESULT rc) {
        wchar_t errorMsg[256];
        waveOutGetErrorText(rc, errorMsg, 256);
        MessageBox(hwnd, errorMsg, L"ERROR", MB_OK | MB_ICONERROR);
    };

    auto queueBuffer = [&](int index) {
        if (nextSample >= totalSamples) {
            return true;
        }

        const size_t samplesToQueue =
            min(static_cast<size_t>(LISTEN_BUFFER_LEN), totalSamples - nextSample);
        memcpy(m_wh[index]->lpData, samples + nextSample,
               samplesToQueue * sizeof(int16_t));
        m_wh[index]->dwBufferLength =
            static_cast<DWORD>(samplesToQueue * sizeof(int16_t));
        m_wh[index]->dwFlags = 0;
        m_wh[index]->dwLoops = 0;
        nextSample += samplesToQueue;

        MMRESULT rc = waveOutPrepareHeader(
            m_hwo, m_wh[index], sizeof(WAVEHDR));
        if (rc != MMSYSERR_NOERROR) {
            showWaveError(rc);
            m_status = StatusError;
            return false;
        }
        prepared[index] = true;

        rc = waveOutWrite(m_hwo, m_wh[index], sizeof(WAVEHDR));
        if (rc != MMSYSERR_NOERROR) {
            showWaveError(rc);
            waveOutUnprepareHeader(m_hwo, m_wh[index], sizeof(WAVEHDR));
            prepared[index] = false;
            m_status = StatusError;
            return false;
        }

        queued[index] = true;
        return true;
    };

    // Start playback
    m_status = StatusOkay;
    for (int i = 0; i < MAX_BUFFERS && nextSample < totalSamples; i++) {
        if (!queueBuffer(i)) {
            break;
        }
    }
    if (progressCallback) {
        progressCallback(0);
    }

    // Playback loop
    while (m_status == StatusOkay) {
        bool hasQueuedBuffer = false;
        bool completedBuffer = false;

        for (int i = 0; i < MAX_BUFFERS; i++) {
            if (!queued[i]) {
                continue;
            }

            hasQueuedBuffer = true;
            if (!(m_wh[i]->dwFlags & WHDR_DONE)) {
                continue;
            }

            queued[i] = false;
            completedBuffer = true;

            MMRESULT rc = waveOutUnprepareHeader(
                m_hwo, m_wh[i], sizeof(WAVEHDR));
            if (rc != MMSYSERR_NOERROR) {
                showWaveError(rc);
                m_status = StatusError;
                break;
            }
            prepared[i] = false;

            if (nextSample < totalSamples && !queueBuffer(i)) {
                break;
            }
        }

        if (m_status != StatusOkay) {
            break;
        }

        hasQueuedBuffer = false;
        for (int i = 0; i < MAX_BUFFERS; i++) {
            if (queued[i]) {
                hasQueuedBuffer = true;
                break;
            }
        }

        if (!hasQueuedBuffer) {
            m_status = StatusDone;
            break;
        }

        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_RBUTTONDOWN) {
                m_stopRequested = true;
                continue;
            }
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                m_stopRequested = true;
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (m_stopRequested) {
            waveOutReset(m_hwo);
            break;
        }

        if (progressCallback) {
            const DWORD currentTick = GetTickCount();
            if (currentTick - lastProgressTick >= 33) {
                MMTIME position{};
                position.wType = TIME_SAMPLES;
                if (waveOutGetPosition(m_hwo, &position, sizeof(position)) == MMSYSERR_NOERROR) {
                    size_t playedSamples = 0;
                    if (position.wType == TIME_SAMPLES) {
                        playedSamples = static_cast<size_t>(position.u.sample);
                    } else if (position.wType == TIME_BYTES) {
                        playedSamples = static_cast<size_t>(position.u.cb / sizeof(int16_t));
                    } else if (position.wType == TIME_MS) {
                        playedSamples = static_cast<size_t>(
                            static_cast<uint64_t>(position.u.ms) * samplingRate / 1000);
                    }
                    if (playedSamples > totalSamples) {
                        playedSamples = totalSamples;
                    }
                    progressCallback(playedSamples);
                }
                lastProgressTick = currentTick;
            }
        }

        if (!completedBuffer) {
            Sleep(1);
        }
    }

    if (m_status == StatusDone && progressCallback) {
        progressCallback(totalSamples);
    }

    // Return every submitted header to the application before closing.
    waveOutReset(m_hwo);
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (prepared[i]) {
            waveOutUnprepareHeader(m_hwo, m_wh[i], sizeof(WAVEHDR));
            prepared[i] = false;
            queued[i] = false;
        }
    }

    const bool completed = m_status == StatusDone && !m_stopRequested;
    CloseOutputDevice();

    // Restore status bar to normal mode
    if (hwndStatus) {
        SendMessage(hwndStatus, SB_SIMPLE, FALSE, 0);
    }

    m_playing = false;
    if (completionCallback) {
        completionCallback(completed);
    }
    return m_status != StatusError && completed;
}

void AudioPlayer::Stop()
{
    m_stopRequested = true;
    if (m_hwo != nullptr) {
        waveOutReset(m_hwo);
    }
}
