/************************************************************************
 *
 *  WinSap C++ Version - Panel Class Header
 *
 *  FILE:      Panel.h
 *
 *  PURPOSE: Encapsulates all panel (upper/lower) related data and operations
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#pragma once

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <optional>
#include <string>
#include <array>
#include <cstdint>
#include "SpectrogramPalette.h"

// Forward declarations
class WinsapApp;
struct DrawContext;

#define PANELMAXFILE        6
#define MAXFILENAME         255
#define PRELEN              500000

// FFT constants
#define FFT_LENGTH          2048
#define FFT_LENGTH_HALF     1024
// Number of bins displayed. The historic SAP value was 900, which stopped the
// axis at 0.44 * sampleRate - the passband edge of the anti-aliasing filters in
// the A/D hardware of the era. Modern converters are flat to Nyquist and the
// transform measures clean across the whole band, so the full half-spectrum is
// displayed, matching the original DOS wsap.
#define FFT_CUT_OFF         FFT_LENGTH_HALF
#define FFT_STAGE           11

// ===================== Enumerations =======================
enum LoadFileType {
    ftBinaryShort = 0,
    ftParameter,
    ftFFTAmplitude
};

enum MarkType {
    mtNone = 0,
    mtLeft,
    mtAll
};

// ===================== Structures =======================
typedef struct MY_Point {
    int x, y;
} WPoint;

typedef struct My_Rect {
    WPoint a, b;
} WRect;

// ===================== Panel Class =======================
class Panel {
public:
    // Constructor and Destructor
    Panel();
    ~Panel();

    // Initialization
    void Initialize(bool isUpper);
    void Reset();

    // File Management
    bool OpenFile(int index, const WCHAR* fileName, int fileType,
                  long dataOffset = 0, long dataLength = 0,
                  BOOL isWav = FALSE, DWORD samplingRate = 0);
    void CloseFile(int index);
    void CloseAllFiles();
    bool IsFileOpen(int index) const { return m_flag[index] != 0; }

    // Getters
    const WCHAR* GetFileName(int index) const { return m_fileName[index]; }
    FILE* GetFilePointer(int index) const { return m_fp[index]; }
    int GetFileType(int index) const { return m_fileType[index]; }
    long GetFileLength(int index) const { return m_fileLength[index]; }
    long GetAudioDataLength(int index) const { return m_audioDataLength[index]; }
    void SetAudioDataLength(int index, long length) { m_audioDataLength[index] = length; }

    // Frame parameters
    int GetFrameBegin() const { return m_frameBegin; }
    int GetFrameNum() const { return m_frameNum; }
    int GetFrameLength() const { return m_frameLength; }
    int GetFrameSkip() const { return m_frameSkip; }

    bool SetFrameBegin(int value);
    bool SetFrameNum(int value);
    void SetFrameLength(int value);
    void SetFrameSkip(int value) { m_frameSkip = value; }
    long GetTotalFrameCount() const;
    int GetLastValidFrameBegin() const;
    void ClampFrameWindow();
    void RecalculateFrameTotals();
    void ViewAll();

    // Display limits
    float GetUpLimit() const { return m_upLimit; }
    float GetLowLimit() const { return m_lowLimit; }
    void SetUpLimit(float value) { m_upLimit = value; }
    void SetLowLimit(float value) { m_lowLimit = value; }

    // Panel geometry
    WRect GetPanelRect() const { return m_panelRect; }
    WRect GetWaveBox() const { return m_waveBox; }
    void SetPanelRect(const WRect& rect) { m_panelRect = rect; }
    void SetWaveBox(const WRect& rect) { m_waveBox = rect; }

    // A panel participates whenever it contains at least one open file.
    bool IsActive() const {
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (m_flag[i] != 0) return true;
        }
        return false;
    }

    // Mark & Listen
    MarkType GetMarkType() const { return m_markType; }
    void SetMarkType(MarkType type) { m_markType = type; }
    long GetListenBegin() const { return m_listenBegin; }
    long GetListenEnd() const { return m_listenEnd; }
    void SetListenRange(long begin, long end) {
        m_listenBegin = begin;
        m_listenEnd = end;
    }

    // Data cursor
    int GetDataCursor() const { return m_dataCursor; }
    void SetDataCursor(int cursor) { m_dataCursor = cursor; }
    int GetCursorFile() const { return m_cursorFile; }
    void SetCursorFile(int file) { m_cursorFile = file; }

    // Playback cursor
    void SetPlaybackCursorSample(int64_t sample) {
        m_playbackCursorSample = sample;
        m_playbackCursorActive = true;
    }
    void ClearPlaybackCursor() { m_playbackCursorActive = false; }

    // Drawing interface (DrawContext-based)
    void Draw(const DrawContext& ctx);
    void DrawFrame(const DrawContext& ctx);

    // File access for specific index
    int* GetFlags() { return m_flag; }
    int* GetSelectFlags() { return m_selectFlag; }
    int* GetFileTypes() { return m_fileType; }
    int* GetFileLimitAuto() { return m_fileLimitAuto; }
    int* GetFileIndents() { return m_fileIndent; }
    float* GetFileUpLimits() { return m_fileUpLimit; }
    float* GetFileLowLimits() { return m_fileLowLimit; }
    long* GetFileFrameTotals() { return m_fileFrameTotal; }
    BOOL* GetIsWavFlags() { return m_isWav; }
    DWORD* GetSamplingRates() { return m_samplingRate; }
    long* GetFileHeaderLengths() { return m_fileHeaderLength; }
    char** GetBuffers() { return m_buf; }
    int* GetBufActiveFlags() { return m_bufActive; }
    long* GetFileBufHeads() { return m_fileBufHead; }
    int* GetFileBufLens() { return m_fileBufLen; }

    // History tracking for optimization
    long GetLastDatHp() const { return m_lastDatHp; }
    long GetLastDatTp() const { return m_lastDatTp; }
    long GetLastFrameLength() const { return m_lastFrameLength; }
    long GetLastFrameNum() const { return m_lastFrameNum; }
    void SetLastDat(long hp, long tp, long frameLength, long frameNum) {
        m_lastDatHp = hp;
        m_lastDatTp = tp;
        m_lastFrameLength = frameLength;
        m_lastFrameNum = frameNum;
    }

    // Parameter file support
    bool IsParActive() const { return m_parActive; }
    void SetParActive(bool active) { m_parActive = active; }
    long* GetParameterIndex() { return m_parameterIndex; }

    // UI interaction
    int GetFileIndexAtPosition(int xPos, int yPos, const DrawContext& ctx) const;

    // How many dB below full scale the spectrogram grey ramp spans.
    float GetSpectrogramRangeDb() const { return m_spectrogramRangeDb; }
    void SetSpectrogramRangeDb(float rangeDb);

    // The spectrogram colour map, stepped from the Options menu.
    winsap::SpectrogramPalette GetSpectrogramPalette() const { return m_spectrogramPalette; }
    void SetSpectrogramPalette(winsap::SpectrogramPalette palette) { m_spectrogramPalette = palette; }

    // Which quantities the panel is currently plotting. The spectrum window
    // shows two quite different pictures, and the axis labels and the hover
    // readout both have to follow whichever one is on screen.
    enum class HoverAxis { Amplitude, SpectrumLine, Spectrogram };

    // Reports the current display and, for the two spectrum displays, the
    // frequency belonging at the top of the plot and the file's sample rate.
    HoverAxis GetHoverAxis(double& topHz, DWORD& sampleRate) const;

    // True while a spectrogram is on screen, in which case the vertical
    // axis is frequency rather than dB.
    bool SpectrogramAxisActive(double& topHz) const;

private:
    // Waveform rendering metrics
    struct WaveformMetrics {
        float horizontalScale;
        float verticalStep;
        WRect drawArea;
    };

    // Parameter file helper methods
    long ParFileTotal(FILE* fp);
    void BuildParameterIndex(int index);
    void DispParameter(const DrawContext& ctx, int fileIndex);
    void DispID(const DrawContext& ctx, const WRect& box, const char* buf,
                long totalFrames, int frameBegin, const long* paramIndex);

    // FFT helper methods
    void FFT(float* z, float* y, int n);
    void LoadTwiddles(int stage);
    void LoadHamming(int dataLen);
    void LoadRealTables();
    // Fills m_ar[0 .. FFT_CUT_OFF-1] with the squared magnitude of the
    // FFT_LENGTH point transform of `dataLen` windowed, zero padded samples.
    void RealSpectrum(const short* src, int dataLen);
    static void FFTTabled(float* z, float* y, int n, const float* cosTab, const float* sinTab);
    void DispSpectrum(const DrawContext& ctx, int fileIndex);

    // The single frame line spectrum, and the many frame grey scale
    // spectrogram. The spectrogram streams the view from disk one window at a
    // time; this build has no whole file cache, by design.
    void DispSpectrumLine(const DrawContext& ctx, int fileIndex);
    void DispSpectrogram(const DrawContext& ctx, int fileIndex);

    // Panel identification
    bool m_isUpper;
    bool m_parActive;

    // File data arrays
    int m_flag[PANELMAXFILE];
    int m_selectFlag[PANELMAXFILE];
    WCHAR m_fileName[PANELMAXFILE][MAXFILENAME];
    FILE* m_fp[PANELMAXFILE];

    char* m_buf[PANELMAXFILE];    int m_bufActive[PANELMAXFILE];

    long m_fileLength[PANELMAXFILE];
    long m_audioDataLength[PANELMAXFILE];
    BOOL m_isWav[PANELMAXFILE];
    long m_fileHeaderLength[PANELMAXFILE];
    DWORD m_samplingRate[PANELMAXFILE];
    long m_fileBufHead[PANELMAXFILE];
    int m_fileBufLen[PANELMAXFILE];
    long m_fileFrameTotal[PANELMAXFILE];

    int m_fileType[PANELMAXFILE];
    int m_fileLimitAuto[PANELMAXFILE];
    int m_fileIndent[PANELMAXFILE];

    float m_fileUpLimit[PANELMAXFILE];
    float m_fileLowLimit[PANELMAXFILE];

    // Frame parameters
    int m_frameBegin;
    int m_frameNum;
    int m_frameLength;
    int m_frameSkip;

    // Display limits
    float m_upLimit;
    float m_lowLimit;

    // Mark & Listen
    MarkType m_markType;
    long m_listenBegin;
    long m_listenEnd;

    // Data cursor
    int m_dataCursor;
    int m_cursorFile;

    // Playback cursor
    bool m_playbackCursorActive;
    int64_t m_playbackCursorSample;

    // Panel geometry
    WRect m_panelRect;
    WRect m_waveBox;

    // History for drawing optimization
    long m_lastDatHp;
    long m_lastDatTp;
    long m_lastFrameLength;
    long m_lastFrameNum;

    // Parameter file support
    long m_parameterIndex[40000];

    // FFT buffers
    float m_ar[FFT_LENGTH];         // Real part of FFT result
    float m_ai[FFT_LENGTH];         // Imaginary part of FFT result

    // How many dB below full scale the spectrogram grey ramp spans.
    float m_spectrogramRangeDb;
    winsap::SpectrogramPalette m_spectrogramPalette;

    // Cached twiddle factors and window for FFT(). The original routine
    // regenerated these with cos()/sin() on every transform, which dominated
    // its runtime; they only depend on the FFT size, so they are built once.
    float m_fftTwiddleCos[FFT_LENGTH_HALF];
    float m_fftTwiddleSin[FFT_LENGTH_HALF];
    int m_fftTwiddleStage;
    float m_hammingWindow[FFT_LENGTH];
    int m_hammingLength;

    // Tables for the real-input transform used by DispSpectrum: twiddles for
    // the half length complex FFT plus the untangle factors.
    float m_realHalfCos[FFT_LENGTH / 4];
    float m_realHalfSin[FFT_LENGTH / 4];
    float m_realUntangleCos[FFT_LENGTH_HALF + 1];
    float m_realUntangleSin[FFT_LENGTH_HALF + 1];
    float m_realZr[FFT_LENGTH_HALF];
    float m_realZi[FFT_LENGTH_HALF];
    bool m_realTablesReady;

    // Drawing methods
    void DrawWaveformBackground(const DrawContext& ctx);
    void DrawWaveform(const DrawContext& ctx, int fileIndex);
    void DrawOverlayWaveform(const DrawContext& ctx, Panel* overlayPanel, int fileIndex);
    std::optional<WaveformMetrics> CalculateWaveformMetrics(int fileIndex) const;
    void DrawWaveformBuffered(const DrawContext& ctx, int fileIndex,
                             const WaveformMetrics& metrics);
    void DrawWaveformStreaming(const DrawContext& ctx, int fileIndex,
                              const WaveformMetrics& metrics);
    void DrawWaveformDecimated(const DrawContext& ctx, int fileIndex,
                              const WaveformMetrics& metrics);
    void DrawConnectedLine(HDC hdc, HPEN pen, short x1, short y1, short x2, short y2) const {
        if (!hdc || !pen) return;

        SelectObject(hdc, pen);
        MoveToEx(hdc, x1, y1, NULL);
        LineTo(hdc, x2, y2);
    }
    std::optional<std::vector<short>> ReadWaveformData(
        int fileIndex, long startSample, long endSample);
    short ClampAndScale(short value, float upLimit, float lowLimit,
                       float step) const;
    bool ValidateDrawRequest(int fileIndex, std::string& errorMsg) const;
    void DrawPanelFrame(const DrawContext& ctx);
    void DrawFrameInfo(const DrawContext& ctx);
    void DrawAmplitudeLabels(const DrawContext& ctx);
    void DrawSeparatorLines(const DrawContext& ctx);
    void DrawZeroLine(const DrawContext& ctx);
    void DrawPlaybackCursor(const DrawContext& ctx);
    void ExtractFileName(const WCHAR* path, WCHAR* name) const;

    // Helper utilities
    static void GrowRect(WRect* rect, int dx, int dy);
    static bool ContainsPoint(const WRect& rect, const WPoint& point);

    // Helper methods
    void SetFileDefaults(int index);
};
