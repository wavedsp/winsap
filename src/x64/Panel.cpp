/************************************************************************
 *
 *  WinSap C++ Version - Panel Class Implementation
 *
 *  FILE:      Panel.cpp
 *
 *  PURPOSE: Implementation of Panel class methods
 *
 *  Update:   2025.4 Update the code base
 *            2026.1 Porting to C++
 *            2026.11 Per slot, bounds checked parameter frame index; 
 *                    spectrogram cache failures degrade to a blank plot
 *            2026.11 Sparse parameter frame index, no 40000 frame limit
 *
 ************************************************************************/

#include "Panel.h"
#include "DrawContext.h"
#include "FftKernel.h"
#include "WinsapApp.h"
#include <string.h>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <climits>
#include <memory>
#include <new>

// ===================== Constructor =======================
Panel::Panel()
    : m_isUpper(true)
    , m_parActive(false)
    , m_frameBegin(0)
    , m_frameNum(1)
    , m_frameLength(320)
    , m_frameSkip(10)
    , m_upLimit(32768.0f)
    , m_lowLimit(-32768.0f)
    , m_markType(mtNone)
    , m_listenBegin(0)
    , m_listenEnd(0)
    , m_dataCursor(0)
    , m_cursorFile(0)
    , m_playbackCursorActive(false)
    , m_playbackCursorSample(0)
    , m_lastDatHp(-1)
    , m_lastDatTp(-1)
    , m_lastFrameLength(0)
    , m_lastFrameNum(0)
    , m_spectrogramRangeDb(winsap::kDefaultDynamicRangeDb)
    , m_spectrogramPalette(winsap::kDefaultSpectrogramPalette)
{
    // Initialize arrays
    for (int i = 0; i < PANELMAXFILE; i++) {
        m_flag[i] = 0;
        m_selectFlag[i] = 0;
        m_fileName[i][0] = L'\0';
        m_buf[i] = nullptr;
        m_bufActive[i] = 0;
        m_fileLength[i] = 0;
        m_isWav[i] = FALSE;
        m_sampleFormat[i] = wavPcm16;
        m_channelCount[i] = 1;
        m_channel[i] = 0;
        m_fileHeaderLength[i] = 0;
        m_samplingRate[i] = 0;
        m_fileFrameTotal[i] = 0;
        m_fileType[i] = ftBinaryShort;
        m_fileLimitAuto[i] = 0;
        m_fileIndent[i] = 0;
        m_fileUpLimit[i] = 32768.0f;
        m_fileLowLimit[i] = -32768.0f;
    }

    // Initialize FFT buffers
    for (int i = 0; i < FFT_LENGTH; i++) {
        m_ar[i] = 0.0f;
        m_ai[i] = 0.0f;
    }
    for (int i = 0; i < FFT_LENGTH_HALF; i++) {
        m_fftTwiddleCos[i] = 0.0f;
        m_fftTwiddleSin[i] = 0.0f;
    }
    m_fftTwiddleStage = 0;
    for (int i = 0; i < FFT_LENGTH; i++) {
        m_hammingWindow[i] = 0.0f;
    }
    m_hammingLength = 0;
    for (int i = 0; i < FFT_LENGTH / 4; i++) {
        m_realHalfCos[i] = 0.0f;
        m_realHalfSin[i] = 0.0f;
    }
    for (int i = 0; i <= FFT_LENGTH_HALF; i++) {
        m_realUntangleCos[i] = 0.0f;
        m_realUntangleSin[i] = 0.0f;
    }
    for (int i = 0; i < FFT_LENGTH_HALF; i++) {
        m_realZr[i] = 0.0f;
        m_realZi[i] = 0.0f;
    }
    m_realTablesReady = false;

    // Initialize geometry
    m_panelRect.a.x = m_panelRect.a.y = 0;
    m_panelRect.b.x = m_panelRect.b.y = 0;
    m_waveBox = m_panelRect;
}

// ===================== Destructor =======================
Panel::~Panel()
{
    CloseAllFiles();
}

// ===================== Initialize =======================
void Panel::Initialize(bool isUpper)
{
    m_isUpper = isUpper;
    Reset();
}

// ===================== Reset =======================
void Panel::Reset()
{
    CloseAllFiles();

    m_parActive = false;
    m_frameBegin = 0;
    m_frameNum = 1;
    m_frameLength = 160;
    m_frameSkip = 10;
    m_upLimit = 32768.0f;
    m_lowLimit = -32768.0f;
    m_markType = mtNone;
    m_listenBegin = 0;
    m_listenEnd = 0;
    m_dataCursor = 0;
    m_cursorFile = 0;
    m_playbackCursorActive = false;
    m_playbackCursorSample = 0;
    m_lastDatHp = -1;
    m_lastDatTp = -1;
}

long Panel::GetTotalFrameCount() const
{
    // While a stereo file is open only stereo channels set the length, so both
    // panels share one range; longer mono data beyond it is not shown.
    const bool stereoOnly = HasStereoFile();
    long total = 0;
    for (int i = 0; i < PANELMAXFILE; ++i) {
        if (m_flag[i] != 0 && (!stereoOnly || IsStereoChannel(i))) {
            total = (std::max)(total, m_fileFrameTotal[i]);
        }
    }
    return total;
}

int Panel::GetLastValidFrameBegin() const
{
    const long total = GetTotalFrameCount();
    return total > m_frameNum ? static_cast<int>(total - m_frameNum) : 0;
}

void Panel::ClampFrameWindow()
{
    const long total = GetTotalFrameCount();
    if (total <= 0) {
        m_frameBegin = 0;
        m_frameNum = (std::max)(1, m_frameNum);
        return;
    }

    m_frameNum = (std::max)(1, (std::min)(m_frameNum, static_cast<int>(total)));
    m_frameBegin = (std::max)(0, (std::min)(m_frameBegin, GetLastValidFrameBegin()));
}

bool Panel::SetFrameBegin(int value)
{
    const int requested = value;
    m_frameBegin = value;
    ClampFrameWindow();
    return m_frameBegin != requested;
}

bool Panel::SetFrameNum(int value)
{
    const int requested = value;
    m_frameNum = value;
    ClampFrameWindow();
    return m_frameNum != requested;
}

void Panel::RecalculateFrameTotals()
{
    if (m_frameLength <= 0) return;
    for (int i = 0; i < PANELMAXFILE; ++i) {
        if (m_flag[i] != 0 && m_fileType[i] != ftParameter) {
            m_fileFrameTotal[i] =
                static_cast<long>(Audio(i).size() / static_cast<size_t>(m_frameLength));
        }
    }
}

void Panel::SetFrameLength(int value)
{
    if (value <= 0) return;
    m_frameLength = value;
    RecalculateFrameTotals();
    ClampFrameWindow();
}

void Panel::ViewAll()
{
    m_frameBegin = 0;
    const long total = GetTotalFrameCount();
    if (total > 0) {
        m_frameNum = static_cast<int>(total);
    }
    ClampFrameWindow();
}

// ===================== File Management =======================
bool Panel::OpenFile(int index, const WCHAR* fileName, int fileType,
                     long dataOffset, long dataLength, BOOL isWav, DWORD samplingRate,
                     int sampleFormat, int channels, int channel)
{
    if (index < 0 || index >= PANELMAXFILE) {
        return false;
    }
    // Raw files are always 16-bit mono; wav data is converted to 16-bit as it loads
    if (!isWav) {
        sampleFormat = wavPcm16;
        channels = 1;
        channel = 0;
    }
    if (channels < 1 || channel < 0 || channel >= channels) {
        return false;
    }
    const int bytesPerSample = WavBytesPerSample(sampleFormat);
    if (bytesPerSample == 0) {
        return false;
    }

    // Close existing file if open
    CloseFile(index);
    const long previousTotalFrames = GetTotalFrameCount();

    FILE* rawFile = nullptr;
    if (_wfopen_s(&rawFile, fileName, L"rb") != 0 || rawFile == nullptr) {
        return false;
    }
    std::unique_ptr<FILE, decltype(&fclose)> file(rawFile, &fclose);

    // Set file properties
    wcsncpy_s(m_fileName[index], MAXFILENAME, fileName, _TRUNCATE);
    m_flag[index] = 1;
    m_fileType[index] = fileType;
    m_isWav[index] = isWav;
    m_sampleFormat[index] = sampleFormat;
    m_channelCount[index] = channels;
    m_channel[index] = channel;
    m_fileHeaderLength[index] = dataOffset;
    m_samplingRate[index] = samplingRate;

    // Get file length
    if (_fseeki64(file.get(), 0, SEEK_END) != 0) {
        CloseFile(index);
        return false;
    }
    const __int64 fileLength = _ftelli64(file.get());
    if (fileLength < 0 || fileLength > LONG_MAX) {
        CloseFile(index);
        return false;
    }
    m_fileLength[index] = static_cast<long>(fileLength);
    if (_fseeki64(file.get(), 0, SEEK_SET) != 0) {
        CloseFile(index);
        return false;
    }

    if (fileType == ftBinaryShort || fileType == ftFFTAmplitude) {
        if (dataOffset < 0 || dataOffset > m_fileLength[index]) {
            CloseFile(index);
            return false;
        }

        const long availableDataLength = m_fileLength[index] - dataOffset;
        const long audioDataLength = dataLength > 0 ? dataLength : availableDataLength;
        if (audioDataLength < 0 || audioDataLength > availableDataLength) {
            CloseFile(index);
            return false;
        }
        const size_t audioBytes = static_cast<size_t>(audioDataLength);
        const size_t sampleCount = audioBytes / (static_cast<size_t>(bytesPerSample) * channels);
        try {
            // Re-seat before filling: a background spectrogram build may still
            // be holding the previous buffer.
            m_audioSamples[index] = std::make_shared<std::vector<int16_t>>();
            m_spectrogram[index].reset();
            Audio(index).resize(sampleCount);
        } catch (const std::bad_alloc&) {
            CloseFile(index);
            return false;
        }

        if (_fseeki64(file.get(), dataOffset, SEEK_SET) != 0 ||
            !ReadWavChannel(file.get(), sampleCount, channels, channel, sampleFormat,
                            Audio(index).data())) {
            CloseFile(index);
            return false;
        }
    }

    // Handle FFT Amplitude files - set dB limits (0 dB = full scale, -120 dB floor)
    if (fileType == ftFFTAmplitude) {
        m_upLimit = 0.0f;
        m_lowLimit = -120.0f;
        m_fileUpLimit[index] = 0.0f;
        m_fileLowLimit[index] = -120.0f;

    }

    // Handle parameter files - requires full file buffering
    if (fileType == ftParameter) {
        // Allocate buffer for entire file
        m_buf[index] = new (std::nothrow) char[m_fileLength[index] + 1];
        if (!m_buf[index]) {
            CloseFile(index);
            return false;
        }

        // Read entire file into buffer
        size_t bytesRead = fread(m_buf[index], sizeof(char), m_fileLength[index], file.get());
        if (bytesRead != static_cast<size_t>(m_fileLength[index])) {
            CloseFile(index);
            return false;
        }

        // Null terminate buffer
        m_buf[index][m_fileLength[index]] = '\0';
        m_bufActive[index] = 1;

        // Build frame index for fast lookup
        BuildParameterIndex(index);

        // Calculate total frames from file content
        _fseeki64(file.get(), 0, SEEK_SET);
        m_fileFrameTotal[index] = ParFileTotal(file.get());

        // Enable parameter display mode
        m_parActive = true;
    }

    SetFileDefaults(index);
    if (GetTotalFrameCount() > previousTotalFrames) {
        ViewAll();
    } else {
        m_frameBegin = 0;
        ClampFrameWindow();
    }

    return true;
}

void Panel::CloseFile(int index)
{
    if (index < 0 || index >= PANELMAXFILE) {
        return;
    }

    // Check if closing a parameter file or FFT file
    bool wasParameterFile = (m_fileType[index] == ftParameter);
    bool wasFFTFile = (m_fileType[index] == ftFFTAmplitude);

    if (m_buf[index] != nullptr) {
        delete[] m_buf[index];
        m_buf[index] = nullptr;
    }
    m_parameterIndex[index].clear();

    m_flag[index] = 0;
    m_audioSamples[index] = std::make_shared<std::vector<int16_t>>();
    m_spectrogram[index].reset();
    m_bufActive[index] = 0;
    m_fileLength[index] = 0;
    m_fileName[index][0] = L'\0';
    m_fileType[index] = ftBinaryShort;  // Reset file type
    m_channelCount[index] = 1;
    m_channel[index] = 0;

    // If we closed a parameter file, check if any others remain
    if (wasParameterFile) {
        bool hasParameterFile = false;
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (m_flag[i] != 0 && m_fileType[i] == ftParameter) {
                hasParameterFile = true;
                break;
            }
        }
        m_parActive = hasParameterFile;
    }

    // If we closed an FFT file, check if any FFT files remain
    // If not, reset limits to default binary short range
    if (wasFFTFile) {
        bool hasFFTFile = false;
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (m_flag[i] != 0 && m_fileType[i] == ftFFTAmplitude) {
                hasFFTFile = true;
                break;
            }
        }
        if (!hasFFTFile) {
            // Reset to default binary short limits
            m_upLimit = 32768.0f;
            m_lowLimit = -32768.0f;
        }
    }
    ClampFrameWindow();
}

void Panel::CloseAllFiles()
{
    for (int i = 0; i < PANELMAXFILE; i++) {
        CloseFile(i);
    }
}

// ===================== Helper Methods =======================
void Panel::SetFileDefaults(int index)
{
    m_fileLimitAuto[index] = 0;
    m_fileIndent[index] = 0;
    m_fileUpLimit[index] = m_upLimit;
    m_fileLowLimit[index] = m_lowLimit;
    if (m_fileType[index] != ftParameter) {
        m_fileFrameTotal[index] =
            static_cast<long>(Audio(index).size() / static_cast<size_t>(m_frameLength));
    }
    // Start the whole file analysis now, so that it overlaps with the user
    // looking at the waveform rather than stalling the first spectrum draw.
    EnsureSpectrogramCache(index);
}

/*****************************************************************************
 * Audio
 *
 * The sample buffer for a slot, creating it on first use so that the shared
 * pointer is never null.
 *
 *****************************************************************************/
std::vector<int16_t>& Panel::Audio(int index)
{
    if (!m_audioSamples[index]) {
        m_audioSamples[index] = std::make_shared<std::vector<int16_t>>();
    }
    return *m_audioSamples[index];
}

const std::vector<int16_t>& Panel::Audio(int index) const
{
    static const std::vector<int16_t> empty;
    return m_audioSamples[index] ? *m_audioSamples[index] : empty;
}

/*****************************************************************************
 * EnsureSpectrogramCache
 *
 * Starts the background whole file analysis for a slot. Sharing the sample
 * buffer means this costs nothing up front even on a long file.
 *
 *****************************************************************************/
void Panel::EnsureSpectrogramCache(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= PANELMAXFILE) return;
    if (m_fileType[fileIndex] == ftParameter) return;
    if (!m_audioSamples[fileIndex] || m_audioSamples[fileIndex]->empty()) return;
    if (m_spectrogram[fileIndex]) return;

    int rate = static_cast<int>(m_samplingRate[fileIndex]);
    if (rate <= 0) rate = 16000;

    // Without memory for the cache object the spectrogram stays blank; this is
    // called from painting, so an escaping bad_alloc would end the process.
    try {
        m_spectrogram[fileIndex] = std::make_unique<winsap::SpectrogramCache>();
    } catch (const std::bad_alloc&) {
        return;
    }
    m_spectrogram[fileIndex]->BuildAsync(m_audioSamples[fileIndex], rate);
}

/*****************************************************************************
 * SpectrogramBuilding
 *
 * True while any slot is still being analysed, so the frame knows to schedule
 * a repaint and let the partially drawn picture fill in.
 *
 *****************************************************************************/
bool Panel::SpectrogramBuilding() const
{
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_spectrogram[i] && m_spectrogram[i]->building()) return true;
    }
    return false;
}

/*****************************************************************************
 * InvalidateSpectrogram
 *
 * The cache geometry (window, FFT size and hop) is derived from the sampling
 * rate, so a cache built at the old rate cannot be reused. Resetting joins any
 * build still running before the cache is released.
 *
 *****************************************************************************/
void Panel::InvalidateSpectrogram(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= PANELMAXFILE) return;
    m_spectrogram[fileIndex].reset();
}

/*****************************************************************************
 * SetSpectrogramRangeDb
 *
 * Widens or narrows the grey ramp. The cache stores dB, not grey levels, so
 * changing the contrast only re-renders; it never re-analyses.
 *
 *****************************************************************************/
void Panel::SetSpectrogramRangeDb(float rangeDb)
{
    if (rangeDb < 20.0f) rangeDb = 20.0f;
    if (rangeDb > 120.0f) rangeDb = 120.0f;
    m_spectrogramRangeDb = rangeDb;
}

// ===================== Helper Utilities =======================
void Panel::GrowRect(WRect* rect, int dx, int dy)
{
    if (!rect) return;
    rect->a.x -= dx;
    rect->a.y -= dy;
    rect->b.x += dx;
    rect->b.y += dy;
}

bool Panel::ContainsPoint(const WRect& rect, const WPoint& point)
{
    return (point.x >= rect.a.x && point.x <= rect.b.x &&
            point.y >= rect.a.y && point.y <= rect.b.y);
}

// ===================== Validation =======================
bool Panel::ValidateDrawRequest(int fileIndex, std::string& errorMsg) const
{
    if (!IsActive()) {
        errorMsg = "Panel has no open files";
        return false;
    }

    if (fileIndex < 0 || fileIndex >= PANELMAXFILE) {
        errorMsg = "Invalid file index: " + std::to_string(fileIndex);
        return false;
    }

    if (m_flag[fileIndex] == 0) {
        errorMsg = "File slot is empty";
        return false;
    }

    if (m_fileType[fileIndex] == ftParameter) {
        if (!m_buf[fileIndex]) {
            errorMsg = "Parameter buffer is null";
            return false;
        }
    } else if (Audio(fileIndex).empty()) {
        errorMsg = "Audio sample buffer is empty";
        return false;
    }

    return true;
}

// ===================== Scaling Utilities =======================
short Panel::ClampAndScale(short value, float upLimit, float lowLimit, float step) const
{
    // Clamp value to limits
    if (value > upLimit) value = static_cast<short>(upLimit);
    if (value < lowLimit) value = static_cast<short>(lowLimit);

    // Scale to screen coordinates
    return static_cast<short>((upLimit - value) / step);
}

// ===================== Metrics Calculation =======================
std::optional<Panel::WaveformMetrics> Panel::CalculateWaveformMetrics(int fileIndex) const
{
    // Validate inputs
    if (fileIndex < 0 || fileIndex >= PANELMAXFILE) {
        return std::nullopt;
    }

    if (Audio(fileIndex).empty()) {
        return std::nullopt;
    }

    // Get panel data
    int frameBegin = m_frameBegin;
    int frameLength = m_frameLength;
    int frameNum = m_frameNum;
    float upLimit = m_upLimit;
    float lowLimit = m_lowLimit;
    // Calculate data range
    const size_t datHp =
        static_cast<size_t>(frameBegin) * static_cast<size_t>(frameLength);
    const size_t datTp =
        static_cast<size_t>(frameBegin + frameNum) * static_cast<size_t>(frameLength);

    // Check bounds
    if (datHp >= Audio(fileIndex).size() || datTp <= datHp) {
        return std::nullopt;
    }

    // Get draw area and shrink by 2 pixels
    WRect waveBox = m_waveBox;
    GrowRect(&waveBox, -2, -2);

    // Calculate metrics
    WaveformMetrics metrics;
    metrics.drawArea = waveBox;

    // Horizontal scale (pixels per sample)
    int widthPixels = waveBox.b.x - waveBox.a.x;
    const size_t sampleCount = datTp - datHp;
    metrics.horizontalScale = static_cast<float>(widthPixels) / static_cast<float>(sampleCount);

    // Vertical step (amplitude units per pixel)
    int heightPixels = waveBox.b.y - waveBox.a.y - 2;
    metrics.verticalStep = (upLimit - lowLimit) / static_cast<float>(heightPixels);

    return metrics;
}

// ===================== Waveform Rendering =======================
void Panel::DrawWaveformSamples(const DrawContext& ctx, int fileIndex, const WaveformMetrics& metrics)
{
    const std::vector<int16_t>& data = Audio(fileIndex);
    const size_t startSample =
        static_cast<size_t>(m_frameBegin) * static_cast<size_t>(m_frameLength);
    const size_t endSample = min(
        data.size(),
        static_cast<size_t>(m_frameBegin + m_frameNum) * static_cast<size_t>(m_frameLength));
    if (startSample >= endSample) {
        return;
    }

    float upLimit = m_upLimit;
    float lowLimit = m_lowLimit;
    HPEN wavePen = ctx.wavePens[fileIndex];

    // Initial position
    short x1 = metrics.drawArea.a.x;
    float t = static_cast<float>(x1);
    t += metrics.horizontalScale;
    short x2 = static_cast<short>(t);

    // Get and scale first point
    short y1 = ClampAndScale(data[startSample], upLimit, lowLimit, metrics.verticalStep);

    // Draw all points
    for (size_t i = startSample + 1; i < endSample; i++) {
        short y2 = data[i];

        y2 = ClampAndScale(y2, upLimit, lowLimit, metrics.verticalStep);
        DrawConnectedLine(ctx.memDC, wavePen,
                          x1, metrics.drawArea.a.y + y1,
                          x2, metrics.drawArea.a.y + y2);

        // Move to next position
        x1 = x2;
        y1 = y2;
        t += metrics.horizontalScale;
        x2 = static_cast<short>(t);
    }
}

void Panel::DrawWaveformDecimated(const DrawContext& ctx, int fileIndex, const WaveformMetrics& metrics)
{
    // Min/max decimation: When multiple samples map to a single pixel,
    // compute min and max values for each pixel column and draw a vertical line.
    // This preserves waveform peaks while reducing draw calls dramatically.
    
    const std::vector<int16_t>& data = Audio(fileIndex);
    const size_t startSample =
        static_cast<size_t>(m_frameBegin) * static_cast<size_t>(m_frameLength);
    const size_t endSample = min(
        data.size(),
        static_cast<size_t>(m_frameBegin + m_frameNum) * static_cast<size_t>(m_frameLength));
    if (startSample >= endSample) return;
    const size_t availableSamples = endSample - startSample;
    const size_t visibleSamples =
        static_cast<size_t>(m_frameNum) * static_cast<size_t>(m_frameLength);

    // Get display settings
    float upLimit = m_upLimit;
    float lowLimit = m_lowLimit;
    HPEN wavePen = ctx.wavePens[fileIndex];

    // Calculate pixel width and samples per pixel
    int widthPixels = metrics.drawArea.b.x - metrics.drawArea.a.x;
    if (widthPixels <= 0) return;
    
    // Samples per pixel (this should be > 1 for decimation to make sense)
    double samplesPerPixel =
        static_cast<double>(visibleSamples) / static_cast<double>(widthPixels);

    SelectObject(ctx.memDC, wavePen);

    short prevMinY = 0, prevMaxY = 0;
    bool firstPixel = true;

    // Process each pixel column
    for (int pixel = 0; pixel < widthPixels; pixel++) {
        // Calculate sample range for this pixel
        size_t sampleStart = static_cast<size_t>(pixel * samplesPerPixel);
        size_t sampleEnd = static_cast<size_t>((pixel + 1) * samplesPerPixel);
        if (sampleStart >= availableSamples) break;
        if (sampleEnd > availableSamples) sampleEnd = availableSamples;
        if (sampleEnd <= sampleStart) sampleEnd = min(sampleStart + 1, availableSamples);
        
        // Find min and max for this pixel
        short minVal = SHRT_MAX;
        short maxVal = SHRT_MIN;
        
        for (size_t sample = sampleStart; sample < sampleEnd; sample++) {
            const short val = data[startSample + sample];
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }
        
        if (minVal == SHRT_MAX) continue;  // No samples for this pixel

        // Scale min and max to screen coordinates
        short minY = ClampAndScale(minVal, upLimit, lowLimit, metrics.verticalStep);
        short maxY = ClampAndScale(maxVal, upLimit, lowLimit, metrics.verticalStep);
        
        // Ensure minY <= maxY in screen coords (may be inverted due to scaling)
        if (minY > maxY) std::swap(minY, maxY);

        int x = metrics.drawArea.a.x + pixel;
        int baseY = metrics.drawArea.a.y;

        // Draw vertical line from min to max
        MoveToEx(ctx.memDC, x, baseY + minY, NULL);
        LineTo(ctx.memDC, x, baseY + maxY + 1);  // +1 to include endpoint

        // Connect to previous pixel for continuous appearance
        if (!firstPixel) {
            // Draw connector from previous max to current min if there's a gap
            if (prevMaxY < minY) {
                MoveToEx(ctx.memDC, x - 1, baseY + prevMaxY, NULL);
                LineTo(ctx.memDC, x, baseY + minY);
            } else if (prevMinY > maxY) {
                MoveToEx(ctx.memDC, x - 1, baseY + prevMinY, NULL);
                LineTo(ctx.memDC, x, baseY + maxY);
            }
        }
        
        prevMinY = minY;
        prevMaxY = maxY;
        firstPixel = false;
    }
}

void Panel::DrawWaveformBackground(const DrawContext& ctx)
{
    if (!ctx.memDC) return;

    WRect waveBox = m_waveBox;

    // Shrink by 2 pixels on all sides to not cover the separator lines
    waveBox.a.x += 2;
    waveBox.a.y += 2;
    waveBox.b.x -= 2;
    waveBox.b.y -= 2;

    // Calculate data range being displayed
    int frameBegin = m_frameBegin;
    int frameNum = m_frameNum;
    int frameLength = m_frameLength;

    float updateHp = static_cast<float>(frameBegin) * static_cast<float>(frameLength);
    float updateTp = static_cast<float>(frameBegin + frameNum) * static_cast<float>(frameLength);
    float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (updateTp - updateHp);

    // Check if we have a marked region
    if (m_markType == mtAll && m_listenBegin < static_cast<long>(updateTp)) {
        // Calculate pixel positions of marked region
        int x1 = (m_listenBegin > static_cast<long>(updateHp)) ?
                 static_cast<int>(waveBox.a.x + (m_listenBegin - updateHp) * horiz) :
                 waveBox.a.x;

        if (m_listenEnd > static_cast<long>(updateHp)) {
            int y1 = (m_listenEnd < static_cast<long>(updateTp)) ?
                     static_cast<int>(waveBox.a.x + (m_listenEnd - updateHp) * horiz) :
                     waveBox.b.x;

            // Fill area before marked region with black
            if (waveBox.a.x != x1) {
                HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
                SelectObject(ctx.memDC, hBlackBrush);
                PatBlt(ctx.memDC, waveBox.a.x, waveBox.a.y,
                       x1 - waveBox.a.x, waveBox.b.y - waveBox.a.y, PATCOPY);
            }

            // Fill marked region with grey
            HBRUSH hGreyBrush = (HBRUSH)GetStockObject(GRAY_BRUSH);
            SelectObject(ctx.memDC, hGreyBrush);
            PatBlt(ctx.memDC, x1, waveBox.a.y,
                   y1 - x1, waveBox.b.y - waveBox.a.y, PATCOPY);

            // Fill area after marked region with black
            if (y1 != waveBox.b.x) {
                HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
                SelectObject(ctx.memDC, hBlackBrush);
                PatBlt(ctx.memDC, y1, waveBox.a.y,
                       waveBox.b.x - y1, waveBox.b.y - waveBox.a.y, PATCOPY);
            }
        } else {
            // Marked region ends before visible area - fill entire area with black
            HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
            SelectObject(ctx.memDC, hBlackBrush);
            PatBlt(ctx.memDC, waveBox.a.x, waveBox.a.y,
                   waveBox.b.x - waveBox.a.x, waveBox.b.y - waveBox.a.y, PATCOPY);
        }
    } else {
        // No marked region - fill entire area with black
        HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        SelectObject(ctx.memDC, hBlackBrush);
        PatBlt(ctx.memDC, waveBox.a.x, waveBox.a.y,
               waveBox.b.x - waveBox.a.x, waveBox.b.y - waveBox.a.y, PATCOPY);
    }
}

void Panel::DrawWaveform(const DrawContext& ctx, int fileIndex)
{
    // Validate request
    std::string error;
    if (!ValidateDrawRequest(fileIndex, error)) {
        return;  // Silently skip invalid files
    }

    // Handle FFT Amplitude display
    if (m_fileType[fileIndex] == ftFFTAmplitude) {
        DispSpectrum(ctx, fileIndex);
        return;
    }

    // Only handle binary short files for waveform display
    if (m_fileType[fileIndex] != ftBinaryShort) {
        return;
    }

    // Calculate rendering metrics
    auto metricsOpt = CalculateWaveformMetrics(fileIndex);
    if (!metricsOpt) {
        return;  // Cannot calculate metrics
    }

    const WaveformMetrics& metrics = *metricsOpt;

    const size_t totalSamples =
        static_cast<size_t>(m_frameNum) * static_cast<size_t>(m_frameLength);

    // Calculate pixels width to determine if decimation is needed
    int widthPixels = metrics.drawArea.b.x - metrics.drawArea.a.x;
    
    // Use decimation when we have significantly more samples than pixels
    // (horizontalScale < 1.0 means multiple samples per pixel)
    if (metrics.horizontalScale < 0.5f && totalSamples > widthPixels * 2) {
        // Use min/max decimation for large datasets with many samples per pixel
        DrawWaveformDecimated(ctx, fileIndex, metrics);
    } else {
        DrawWaveformSamples(ctx, fileIndex, metrics);
    }
}

/*****************************************************************************
 * DrawOverlayWaveform
 *
 * Draw overlay waveform from another panel (Add Down to Up feature)
 * Uses this panel's waveBox coordinates but overlay panel's data
 *
 *****************************************************************************/
void Panel::DrawOverlayWaveform(const DrawContext& ctx, Panel* overlayPanel, int fileIndex)
{
    if (!ctx.memDC || !ctx.redPen || !overlayPanel) return;

    const std::vector<int16_t>& overlayData = overlayPanel->Audio(fileIndex);
    if (overlayData.empty()) return;

    // Use THIS panel's waveBox position (upper panel's position for drawing)
    // IMPORTANT: Shrink by 2 pixels to match normal drawing
    WRect waveBox = m_waveBox;
    GrowRect(&waveBox, -2, -2);
    
    // Use OVERLAY panel's limits for scaling (lower panel's amplitude range)
    float overlayUpLimit = overlayPanel->m_upLimit;
    float overlayLowLimit = overlayPanel->m_lowLimit;

    // Use OVERLAY panel's waveBox for calculating the step (height-based scaling)
    // IMPORTANT: Shrink by 2 pixels to match normal drawing
    WRect overlayWaveBox = overlayPanel->m_waveBox;
    GrowRect(&overlayWaveBox, -2, -2);

    // Use OVERLAY panel's frame parameters and data (lower panel)
    int overlayFrameBegin = overlayPanel->m_frameBegin;
    int overlayFrameNum = overlayPanel->m_frameNum;
    int overlayFrameLength = overlayPanel->m_frameLength;

    // Calculate overlay data range
    float overlayDatHp = static_cast<float>(overlayFrameBegin) * static_cast<float>(overlayFrameLength);
    float overlayDatTp = static_cast<float>(overlayFrameBegin + overlayFrameNum) * static_cast<float>(overlayFrameLength);

    // Calculate horizontal scale using THIS panel's waveBox width
    float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (overlayDatTp - overlayDatHp);
    
    // Calculate vertical step using OVERLAY panel's box height and limits
    // This matches exactly how the normal drawing calculates step
    float step = (overlayUpLimit - overlayLowLimit) / static_cast<float>(overlayWaveBox.b.y - overlayWaveBox.a.y - 2);
    
    // Read overlay waveform data
    const size_t startSample = static_cast<size_t>(overlayDatHp);
    const size_t endSample = min(overlayData.size(), static_cast<size_t>(overlayDatTp));
    if (startSample >= endSample) return;
    const size_t totalSamples = endSample - startSample;

    const int widthPixels = waveBox.b.x - waveBox.a.x;
    if (widthPixels <= 0) return;

    if (horiz < 0.5f && totalSamples > static_cast<size_t>(widthPixels) * 2) {
        SelectObject(ctx.memDC, ctx.redPen);
        const double samplesPerPixel =
            static_cast<double>(totalSamples) / static_cast<double>(widthPixels);

        for (int pixel = 0; pixel < widthPixels; pixel++) {
            size_t pixelStart = static_cast<size_t>(pixel * samplesPerPixel);
            size_t pixelEnd = static_cast<size_t>((pixel + 1) * samplesPerPixel);
            if (pixelEnd > totalSamples) pixelEnd = totalSamples;
            if (pixelEnd <= pixelStart) pixelEnd = min(pixelStart + 1, totalSamples);

            short minValue = SHRT_MAX;
            short maxValue = SHRT_MIN;
            for (size_t sample = pixelStart; sample < pixelEnd; sample++) {
                const short value = overlayData[startSample + sample];
                if (value < minValue) minValue = value;
                if (value > maxValue) maxValue = value;
            }

            const short minY =
                ClampAndScale(maxValue, overlayUpLimit, overlayLowLimit, step);
            const short maxY =
                ClampAndScale(minValue, overlayUpLimit, overlayLowLimit, step);
            const int x = waveBox.a.x + pixel;
            MoveToEx(ctx.memDC, x, waveBox.a.y + minY, NULL);
            LineTo(ctx.memDC, x, waveBox.a.y + maxY + 1);
        }
        return;
    }

    // Draw the overlay waveform in RED
    SelectObject(ctx.memDC, ctx.redPen);

    short x1, x2, y1, y2;
    float t;
    
    // Initialize first point
    x1 = static_cast<short>(waveBox.a.x);
    x2 = x1;
    t = static_cast<float>(waveBox.a.x);
    
    y1 = overlayData[startSample];
    if (y1 > overlayUpLimit) y1 = static_cast<short>(overlayUpLimit);
    if (y1 < overlayLowLimit) y1 = static_cast<short>(overlayLowLimit);
    y1 = static_cast<short>((overlayUpLimit - y1) / step);

    for (size_t j = startSample; j < endSample; j++) {
        y2 = overlayData[j];
        if (y2 > overlayUpLimit) y2 = static_cast<short>(overlayUpLimit);
        if (y2 < overlayLowLimit) y2 = static_cast<short>(overlayLowLimit);
        
        y2 = static_cast<short>((overlayUpLimit - y2) / step);

        MoveToEx(ctx.memDC, x1, waveBox.a.y + y1, NULL);
        LineTo(ctx.memDC, x2, waveBox.a.y + y2);
        
        x1 = x2;
        y1 = y2;
        t = t + horiz;
        x2 = static_cast<short>(t);
    }
}

// ===================== Frame Drawing =======================
void Panel::ExtractFileName(const WCHAR* path, WCHAR* name) const
{
    int len = static_cast<int>(wcslen(path));
    int i;
    for (i = len - 1; i >= 0; i--) {
        if (path[i] == L'\\' || path[i] == L':' || path[i] == L'/') {
            break;
        }
    }

    int j = 0;
    if (i != 0) i++;
    for (; i <= len; i++) {
        name[j++] = path[i];
    }
    name[j] = L'\0';
}

void Panel::DisplayFileName(int index, WCHAR* name, size_t nameSize) const
{
    WCHAR baseName[MAXFILENAME];
    ExtractFileName(m_fileName[index], baseName);
    if (m_channelCount[index] > 1) {
        swprintf_s(name, nameSize, L"%s [%s]", baseName, m_channel[index] == 0 ? L"L" : L"R");
    }
    else {
        wcscpy_s(name, nameSize, baseName);
    }
}

void Panel::DrawFrameInfo(const DrawContext& ctx)
{
    if (!ctx.memDC) return;

    WRect panelBox = m_panelRect;
    WCHAR out[256];

    // Position constants (character positions from left edge)
    // Frame numbers take 10 columns (PARAMETER_MAX_FRAMES has 10 digits) and
    // times 13 (hhh:mm:ss.mmm); GetFileIndexAtPosition repeats the filename column.
    const int FRAME_NUMBER_START_POS = 7;
    const int FRAME_SEPARATOR_POS = 18;
    const int FRAME_NUMBER_END_POS = 22;
    const int FRAME_SIZE_LABEL_POS = 33;
    const int FRAME_SIZE_POS = 43;
    const int START_TIME_POS = 50;
    const int TIME_SEPARATOR_POS = 64;
    const int END_TIME_POS = 68;
    const int FILENAME_START_POS = 82;

    // Set cyan color for frame info
    COLORREF oldColor = SetTextColor(ctx.memDC, RGB(0, 255, 255));

    int yPos = m_isUpper ? (panelBox.a.y + PANEL_Y_LINE_SPACE ) : (panelBox.b.y - ctx.charHeight - 1 - (PANEL_Y_LINE_SPACE >> 1));

    // Draw "Frame=" label
    TextOut(ctx.memDC, panelBox.a.x + ctx.charWidth, yPos, L"Frame=", 6);

    // Draw "..." separator
    TextOut(ctx.memDC, panelBox.a.x + FRAME_SEPARATOR_POS * ctx.charWidth, yPos, L"...", 3);

    // Draw "FrameSize=" label
    TextOut(ctx.memDC, panelBox.a.x + FRAME_SIZE_LABEL_POS * ctx.charWidth, yPos, L"FrameSize=", 10);

    // Draw time range separator
    TextOut(ctx.memDC, panelBox.a.x + TIME_SEPARATOR_POS * ctx.charWidth, yPos, L"...", 3);

    // Set white color for frame numbers and size
    SetTextColor(ctx.memDC, RGB(255, 255, 255));

    // Draw start frame number
    swprintf_s(out, 256, L"%10d", m_frameBegin);
    TextOut(ctx.memDC, panelBox.a.x + FRAME_NUMBER_START_POS * ctx.charWidth, yPos, out, (int)wcslen(out));

    // Draw end frame number
    swprintf_s(out, 256, L"%10d", m_frameBegin + m_frameNum);
    TextOut(ctx.memDC, panelBox.a.x + FRAME_NUMBER_END_POS * ctx.charWidth, yPos, out, (int)wcslen(out));

    // Draw frame size
    swprintf_s(out, 256, L"%5d", m_frameLength);
    TextOut(ctx.memDC, panelBox.a.x + FRAME_SIZE_POS * ctx.charWidth, yPos, out, (int)wcslen(out));

    // Calculate and display the visible time interval
    int64_t startSamples = static_cast<int64_t>(m_frameBegin) * m_frameLength;
    int64_t endSamples = static_cast<int64_t>(m_frameBegin + m_frameNum) * m_frameLength;
    DWORD samplingRate = 8000;

    // Get sampling rate from first active file
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_flag[i] != 0 && m_samplingRate[i] != 0) {
            samplingRate = m_samplingRate[i];
            break;
        }
    }

    auto formatTime = [samplingRate](int64_t samples, WCHAR* buffer, size_t bufferLength) {
        const int64_t totalMilliseconds = samples * 1000 / samplingRate;
        const int64_t hours = totalMilliseconds / 3600000;
        const int64_t minutes = (totalMilliseconds / 60000) % 60;
        const int64_t seconds = (totalMilliseconds / 1000) % 60;
        const int64_t milliseconds = totalMilliseconds % 1000;
        swprintf_s(buffer, bufferLength, L"%02lld:%02lld:%02lld.%03lld",
                   hours, minutes, seconds, milliseconds);
    };

    formatTime(startSamples, out, _countof(out));
    TextOut(ctx.memDC, panelBox.a.x + START_TIME_POS * ctx.charWidth, yPos, out, (int)wcslen(out));
    formatTime(endSamples, out, _countof(out));
    TextOut(ctx.memDC, panelBox.a.x + END_TIME_POS * ctx.charWidth, yPos, out, (int)wcslen(out));

    // Draw filenames after time with matching colors
    int xPos = panelBox.a.x + FILENAME_START_POS * ctx.charWidth;
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_flag[i] != 0) {
            WCHAR name[MAXFILENAME + 8];
            DisplayFileName(i, name, _countof(name));

            // Set color to match waveform
            SetTextColor(ctx.memDC, ctx.waveColors[i]);

            int nameLen = static_cast<int>(wcslen(name));
            TextOut(ctx.memDC, xPos, yPos, name, nameLen);

            // Calculate text width for next filename position
            SIZE textSize;
            GetTextExtentPoint32(ctx.memDC, name, nameLen, &textSize);
            xPos += textSize.cx + ctx.charWidth;  // One character space between filenames

            // Stop if we run out of space
            if (xPos >= panelBox.b.x - 5 * ctx.charWidth) {
                break;
            }
        }
    }

    // Restore original color
    SetTextColor(ctx.memDC, oldColor);
}

int Panel::GetFileIndexAtPosition(int xPos, int yPos, const DrawContext& ctx) const
{
    if (!ctx.memDC) return -1;

    WRect panelBox = m_panelRect;

    // Check if Y position is in the Frame= info line
    int frameLineY = m_isUpper ? (panelBox.a.y + 1) : (panelBox.b.y - ctx.charHeight);
    if (yPos < frameLineY || yPos > frameLineY + ctx.charHeight) {
        return -1;  // Not in Frame= line
    }

    // Filename starts after the frame and time interval fields (see DrawFrameInfo).
    const int FILENAME_START_POS = 82;
    int startX = panelBox.a.x + FILENAME_START_POS * ctx.charWidth;

    // Check if X position is in filename area
    if (xPos < startX) {
        return -1;  // Before filename area
    }

    // Calculate which filename was clicked
    int currentX = startX;
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_flag[i] != 0) {
            WCHAR name[MAXFILENAME + 8];
            DisplayFileName(i, name, _countof(name));

            int nameLen = static_cast<int>(wcslen(name));
            SIZE textSize;
            GetTextExtentPoint32(ctx.memDC, name, nameLen, &textSize);

            // Check if click is within this filename
            if (xPos >= currentX && xPos <= currentX + textSize.cx) {
                return i;
            }

            // Move to next filename position (width + one character space)
            currentX += textSize.cx + ctx.charWidth;

            // Stop if we've passed the click position
            if (currentX > xPos) {
                break;
            }

            // Stop if we run out of space
            if (currentX >= panelBox.b.x - 5 * ctx.charWidth) {
                break;
            }
        }
    }

    return -1;  // No filename at this position
}

/*****************************************************************************
 * GetHoverAxis
 *
 * Says which picture the panel is drawing, so the vertical axis labels and the
 * status bar readout can report the right quantities.
 *
 * The line spectrum plots bins 0 .. FFT_CUT_OFF-1 of an FFT_LENGTH point
 * transform, which deliberately stops short of Nyquist, so its top frequency
 * is not the same as the spectrogram's.
 *
 *****************************************************************************/
Panel::HoverAxis Panel::GetHoverAxis(double& topHz, DWORD& sampleRate) const
{
    topHz = 0.0;
    sampleRate = 0;

    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_fileType[i] != ftFFTAmplitude) continue;
        if (!m_audioSamples[i] || m_audioSamples[i]->empty()) continue;

        DWORD rate = m_samplingRate[i];
        if (rate == 0) rate = 16000;
        sampleRate = rate;

        const int frames = m_frameNum > 0 ? m_frameNum : 1;
        const size_t span = static_cast<size_t>(frames) * static_cast<size_t>(m_frameLength);
        if (span > winsap::kSpectrogramThreshold) {
            topHz = static_cast<double>(rate) / 2.0;
            return HoverAxis::Spectrogram;
        }

        topHz = static_cast<double>(rate) * FFT_CUT_OFF / FFT_LENGTH;
        return HoverAxis::SpectrumLine;
    }

    return HoverAxis::Amplitude;
}

bool Panel::SpectrogramAxisActive(double& topHz) const
{
    DWORD rate = 0;
    double top = 0.0;
    if (GetHoverAxis(top, rate) != HoverAxis::Spectrogram) return false;
    topHz = top;
    return true;
}

void Panel::DrawAmplitudeLabels(const DrawContext& ctx)
{
    if (!ctx.memDC) return;

    WRect waveBox = m_waveBox;
    WRect panelBox = m_panelRect;
    WCHAR out[32];

    // Clip drawing to panel bounds to prevent drawing into other panel's area
    HRGN clipRegion = CreateRectRgn(panelBox.a.x, panelBox.a.y, panelBox.b.x, panelBox.b.y);
    SelectClipRgn(ctx.memDC, clipRegion);

    // ---- Calculate number of tick marks -----
    if ((waveBox.b.y - waveBox.a.y) < 3 * ctx.charHeight) {
        // The height of the box is less than 3 character heights; cannot draw labels
        SelectClipRgn(ctx.memDC, NULL);  // Remove clipping
        DeleteObject(clipRegion);
        return;
    }
    // number of ticks that can fit at one side, pixels excluding top, bottom, and middle labels
    // (waveBox.b.y - waveBox.a.y - 3 * ctx.charHeight), one side so divide by 2,
    // consider one char height space between labels, this pixel count can fit how many lables
    // The spectrum window carries two different displays, so the vertical
    // axis has to say which one it is labelling. A single frame is amplitude
    // against frequency, so the axis is dB; a spectrogram is frequency against
    // time, so the axis is Hz, zero at the bottom and Nyquist at the top.
    float axisTop = m_upLimit;
    float axisBottom = m_lowLimit;
    double nyquistHz = 0.0;
    if (SpectrogramAxisActive(nyquistHz)) {
        axisTop = static_cast<float>(nyquistHz);
        axisBottom = 0.0f;
    }

    int oneSidePixels = (waveBox.b.y - waveBox.a.y - 3 * ctx.charHeight) >> 1;
    int numTicks = (oneSidePixels - ctx.charHeight) / (ctx.charHeight << 1);
    // now make the space between ticks even
    float lineSpacing = static_cast<float>(oneSidePixels - numTicks * ctx.charHeight) / static_cast<float>(numTicks + 1);
    float step = (axisTop - axisBottom) / static_cast<float>(2 * (numTicks + 1));

    // output lineSpacing number to status bar position 1 for debug
    //WCHAR debugMsg[64];
    //swprintf_s(debugMsg, 64, L"%d %d %6.2f %d", oneSidePixels, ctx.charHeight, lineSpacing, numTicks);
    //ctx.toolbarManager->UpdateStatusline(debugMsg, 1);

    // Set text color to cyan
    COLORREF oldColor = SetTextColor(ctx.memDC, RGB(0, 255, 255));

    // Draw upper limit - use 8 characters max (e.g., "32768" or "-32768")
    swprintf_s(out, 32, L"%8.0f", axisTop);
    TextOut(ctx.memDC, panelBox.a.x + ctx.charWidth, waveBox.a.y + 1, out, (int)wcslen(out));

    // Draw lower limit - ensure it stays within panel bounds
    int lowerLabelY = waveBox.b.y - ctx.charHeight - 1;
    if (lowerLabelY > panelBox.b.y - ctx.charHeight) {
        lowerLabelY = panelBox.b.y - ctx.charHeight;
    }
    swprintf_s(out, 32, L"%8.0f", axisBottom);
    TextOut(ctx.memDC, panelBox.a.x + ctx.charWidth, lowerLabelY, out, (int)wcslen(out));

    // Draw zero line labels
    float zeroY = (axisTop + axisBottom) / 2.0f;
    swprintf_s(out, 32, L"%8.0f", zeroY);
    TextOut(ctx.memDC, panelBox.a.x + ctx.charWidth, waveBox.a.y + ctx.charHeight + oneSidePixels, out, (int)wcslen(out));

    // Draw intermediate tick marks
    float ySpace = lineSpacing;
    float upStartValue = axisTop - step;
    int upStartY = waveBox.a.y + ctx.charHeight;
    for (int i = 0; i < numTicks; i++) {
        swprintf_s(out, 32, L"%8.0f", upStartValue);
        int yPos = upStartY + static_cast<int>(ySpace);
        TextOut(ctx.memDC, panelBox.a.x + ctx.charWidth, yPos, out, (int)wcslen(out));
        ySpace += ctx.charHeight + lineSpacing;
        upStartValue -= step;
    }
    ySpace = lineSpacing;
    float lowStartValue = zeroY - step;
    int lowStartY = waveBox.a.y + ctx.charHeight + oneSidePixels + ctx.charHeight;
    for (int i = 0; i < numTicks; i++) {
        swprintf_s(out, 32, L"%8.0f", lowStartValue);
        int yPos = lowStartY + static_cast<int>(ySpace);
        TextOut(ctx.memDC, panelBox.a.x + ctx.charWidth, yPos, out, (int)wcslen(out));
        ySpace += ctx.charHeight + lineSpacing;
        lowStartValue -= step;
    }

    // Restore original color and remove clipping
    SetTextColor(ctx.memDC, oldColor);
    SelectClipRgn(ctx.memDC, NULL);
    DeleteObject(clipRegion);
}

void Panel::DrawSeparatorLines(const DrawContext& ctx)
{
    if (!ctx.memDC) return;

    WRect panelBox = m_panelRect;
    WRect waveBox = m_waveBox;

    SelectObject(ctx.memDC, ctx.framePen);

    // Use 10 character width for label area (changed from 15)
    int labelWidth = panelBox.a.x + PANEL_LABEL_X_CHAR * ctx.charWidth;

    if (m_isUpper) {
        // Upper panel: Frame= line is always at top
        // Line below Frame= info
        int lineY1 = panelBox.a.y + ctx.charHeight + PANEL_Y_LINE_SPACE + 1;
        MoveToEx(ctx.memDC, panelBox.a.x, lineY1, NULL);
        LineTo(ctx.memDC, panelBox.b.x, lineY1);

        if (m_parActive) {
            // When parameter file is active:
            // Line below parameter display (2 lines of param + line spacing)
            int lineY2 = panelBox.a.y + ctx.charHeight + 1 + PANEL_Y_LINE_SPACE + 2 * (ctx.charHeight + PANEL_Y_LINE_SPACE) + 1;
            MoveToEx(ctx.memDC, panelBox.a.x, lineY2, NULL);
            LineTo(ctx.memDC, panelBox.b.x, lineY2);

            // Vertical separator from below param display to panel bottom
            MoveToEx(ctx.memDC, labelWidth, lineY2, NULL);
            LineTo(ctx.memDC, labelWidth, panelBox.b.y);
        } else {
            // No parameter file: vertical separator from below Frame= line to panel bottom
            MoveToEx(ctx.memDC, labelWidth, lineY1 + PANEL_Y_LINE_SPACE, NULL);
            LineTo(ctx.memDC, labelWidth, panelBox.b.y);
        }
    } else {
        // Lower panel: Frame= line is always at bottom
        // Line above Frame= info
        int lineY1 = panelBox.b.y - ctx.charHeight - PANEL_Y_LINE_SPACE - 1;
        MoveToEx(ctx.memDC, panelBox.a.x, lineY1, NULL);
        LineTo(ctx.memDC, panelBox.b.x, lineY1);

        if (m_parActive) {
            // When parameter file is active:
            // Line above parameter display (2 lines of param + line spacing above Frame=)
            int lineY2 = panelBox.b.y - ctx.charHeight - 2 - PANEL_Y_LINE_SPACE - 2 * (ctx.charHeight + PANEL_Y_LINE_SPACE) - 1;
            MoveToEx(ctx.memDC, panelBox.a.x, lineY2, NULL);
            LineTo(ctx.memDC, panelBox.b.x, lineY2);

            // Vertical separator from panel top to above param display
            MoveToEx(ctx.memDC, labelWidth, panelBox.a.y, NULL);
            LineTo(ctx.memDC, labelWidth, lineY2);
        } else {
            // No parameter file: vertical separator from panel top to above Frame= line
            MoveToEx(ctx.memDC, labelWidth, panelBox.a.y, NULL);
            LineTo(ctx.memDC, labelWidth, lineY1 - PANEL_Y_LINE_SPACE);
        }
    }
}

void Panel::DrawZeroLine(const DrawContext& ctx)
{
    if (!ctx.memDC) return;

    // Only draw zero line if range crosses zero
    if (m_upLimit > 0 && m_lowLimit < 0) {
        WRect waveBox = m_waveBox;
        int y = static_cast<int>(waveBox.a.y + m_upLimit * (waveBox.b.y - waveBox.a.y) / (m_upLimit - m_lowLimit));

        SelectObject(ctx.memDC, ctx.framePen);
        MoveToEx(ctx.memDC, waveBox.a.x, y, NULL);
        LineTo(ctx.memDC, waveBox.b.x, y);
    }
}

void Panel::DrawPanelFrame(const DrawContext& ctx)
{
    if (!ctx.memDC) return;

    WRect panelBox = m_panelRect;
    WRect waveBox = m_waveBox;

    // Draw panel border
    SelectObject(ctx.memDC, ctx.framePen);
    SelectObject(ctx.memDC, GetStockObject(NULL_BRUSH));
    Rectangle(ctx.memDC, panelBox.a.x, panelBox.a.y, panelBox.b.x, panelBox.b.y);

    // Draw frame information line with filenames
    DrawFrameInfo(ctx);

    // Draw amplitude labels
    DrawAmplitudeLabels(ctx);

    // Draw separator lines
    DrawSeparatorLines(ctx);

    // Note: Zero line is drawn later in Draw() after background is painted
}

// ===================== Public Drawing Interface =======================
void Panel::DrawFrame(const DrawContext& ctx)
{
    if (!IsActive() || !ctx.memDC) return;

    DrawPanelFrame(ctx);
}

void Panel::Draw(const DrawContext& ctx)
{
    if (!IsActive() || !ctx.memDC) return;

    // Draw frame first
    DrawFrame(ctx);

    // Draw waveform background (with grey for marked regions)
    DrawWaveformBackground(ctx);

    // Draw zero crossing line (after background, before waveforms)
    DrawZeroLine(ctx);

    // Display parameter file data if active
    if (m_parActive) {
        for (int i = 0; i < PANELMAXFILE; i++) {
            if (m_flag[i] != 0 && m_fileType[i] == ftParameter) {
                DispParameter(ctx, i);
                break;  // Only one parameter file per panel
            }
        }
    }

    // Draw each active file's waveform
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_flag[i] != 0) {
            try {
                DrawWaveform(ctx, i);
            } catch (const std::exception&) {
                // Silently continue if one file fails
                continue;
            }
        }
    }

    // Draw overlay waveform if addFile is enabled (Add Down to Up feature)
    if (ctx.addFile && ctx.overlayPanel && ctx.redPen) {
        Panel* overlayPanel = ctx.overlayPanel;

        // Only draw if overlay panel has first file open and it's a binary file
        if (overlayPanel->m_flag[0] != 0 &&
            overlayPanel->m_fileType[0] == ftBinaryShort) {

            // Draw the overlay panel's first waveform in RED
            DrawOverlayWaveform(ctx, overlayPanel, 0);
        }
    }

    DrawSpectrogramMark(ctx);
    DrawPlaybackCursor(ctx);
}

// A spectrogram covers the grey mark background, so the marked block is shown
// by its edges instead: white lines at the start and, once complete, the end.
bool Panel::HasSpectrumFile() const
{
    for (int i = 0; i < PANELMAXFILE; i++) {
        if (m_flag[i] != 0 && m_fileType[i] == ftFFTAmplitude) {
            return true;
        }
    }
    return false;
}

bool Panel::IsSpectrogramView() const
{
    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    const size_t span = static_cast<size_t>(frames) * static_cast<size_t>(m_frameLength);
    return HasSpectrumFile() && span > winsap::kSpectrogramThreshold;
}

void Panel::DrawSpectrogramMark(const DrawContext& ctx)
{
    if (m_markType == mtNone || !ctx.memDC) {
        return;
    }
    if (!IsSpectrogramView()) {
        return;
    }

    WRect waveBox = m_waveBox;
    Panel::GrowRect(&waveBox, -2, -2);
    const int64_t visibleStart = static_cast<int64_t>(m_frameBegin) * m_frameLength;
    const size_t span = static_cast<size_t>(m_frameNum > 0 ? m_frameNum : 1) *
                        static_cast<size_t>(m_frameLength);
    const double pixelsPerSample =
        static_cast<double>(waveBox.b.x - waveBox.a.x) / static_cast<double>(span);

    HPEN oldPen = static_cast<HPEN>(SelectObject(ctx.memDC, GetStockObject(DC_PEN)));
    COLORREF oldColor = SetDCPenColor(ctx.memDC, RGB(255, 255, 255));
    const long edges[2] = { m_listenBegin, m_listenEnd };
    for (int e = 0; e < (m_markType == mtAll ? 2 : 1); e++) {
        const double offset = static_cast<double>(edges[e] - visibleStart) * pixelsPerSample;
        if (offset < 0.0 || offset > static_cast<double>(waveBox.b.x - waveBox.a.x)) {
            continue;
        }
        const int x = waveBox.a.x + static_cast<int>(offset);
        MoveToEx(ctx.memDC, x, waveBox.a.y, nullptr);
        LineTo(ctx.memDC, x, waveBox.b.y);
    }
    SetDCPenColor(ctx.memDC, oldColor);
    SelectObject(ctx.memDC, oldPen);
}

void Panel::DrawPlaybackCursor(const DrawContext& ctx)
{
    if (!m_playbackCursorActive || !ctx.memDC) {
        return;
    }
    // A single-frame spectrum line has a frequency x-axis, so there is no time cursor
    if (HasSpectrumFile() && !IsSpectrogramView()) {
        return;
    }

    const int64_t visibleStart = static_cast<int64_t>(m_frameBegin) * m_frameLength;
    const int64_t visibleEnd = static_cast<int64_t>(m_frameBegin + m_frameNum) * m_frameLength;
    if (visibleEnd <= visibleStart ||
        m_playbackCursorSample < visibleStart ||
        m_playbackCursorSample > visibleEnd) {
        return;
    }

    WRect waveBox = m_waveBox;
    waveBox.a.x += 2;
    waveBox.a.y += 2;
    waveBox.b.x -= 2;
    waveBox.b.y -= 2;

    const double position =
        static_cast<double>(m_playbackCursorSample - visibleStart) /
        static_cast<double>(visibleEnd - visibleStart);
    const int x = waveBox.a.x +
        static_cast<int>(position * static_cast<double>(waveBox.b.x - waveBox.a.x));

    HPEN oldPen = static_cast<HPEN>(SelectObject(ctx.memDC, GetStockObject(DC_PEN)));
    COLORREF oldColor = SetDCPenColor(ctx.memDC, RGB(255, 128, 0));
    MoveToEx(ctx.memDC, x, waveBox.a.y, nullptr);
    LineTo(ctx.memDC, x, waveBox.b.y);
    if (x < waveBox.b.x) {
        MoveToEx(ctx.memDC, x + 1, waveBox.a.y, nullptr);
        LineTo(ctx.memDC, x + 1, waveBox.b.y);
    }
    SetDCPenColor(ctx.memDC, oldColor);
    SelectObject(ctx.memDC, oldPen);
}

// ===================== Parameter File Support =======================

// Frame number after "FRAME=" with atol() rules, or -1 when it is negative,
// overflows or reaches PARAMETER_MAX_FRAMES.
static long ParseFrameNumber(const char* text)
{
    const long frame = strtol(text, nullptr, 10);
    return (frame >= 0 && frame < PARAMETER_MAX_FRAMES) ? frame : -1;
}

/*****************************************************************************
 * ParFileTotal
 *
 * Calculate total number of frames in a parameter file
 * Reads last 1000 bytes and searches for highest FRAME= number
 *
 *****************************************************************************/
long Panel::ParFileTotal(FILE* fp)
{
    char buf[1001];
    char* p;
    long fileLen, seekPos;
    long maxFrame = -1;

    // Get file length
    fseek(fp, 0L, SEEK_END);
    fileLen = ftell(fp);

    // Seek to last 1000 bytes (or start of file if shorter)
    seekPos = (fileLen > 1000) ? (fileLen - 1000) : 0;
    fseek(fp, seekPos, SEEK_SET);

    // Read the end section
    size_t bytesRead = fread(buf, sizeof(char), 1000, fp);
    buf[bytesRead] = '\0';

    // Search backwards through buffer for FRAME= lines
    p = buf;
    while (*p != '\0') {
        if (strncmp(p, "FRAME=", 6) == 0) {
            maxFrame = (std::max)(maxFrame, ParseFrameNumber(p + 6));
        }
        // Move to next line
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    // Frame numbers are 0-based; a file without a usable tag in its tail still
    // counts frame 0, as it always did.
    return (std::max)(maxFrame, 0L) + 1;
}

/*****************************************************************************
 * BuildParameterIndex
 *
 * Build index mapping frame numbers to file byte positions
 * Parses entire parameter file buffer; memory follows the FRAME= lines
 * present, not the highest frame number.
 *
 *****************************************************************************/
void Panel::BuildParameterIndex(int index)
{
    ParameterIndex& frameIndex = m_parameterIndex[index];
    frameIndex.clear();
    if (!m_buf[index]) return;

    char* p = m_buf[index];
    long pos = 0;

    // Parse through entire buffer
    while (*p != '\0') {
        // Check if line starts with "FRAME="
        if (strncmp(p, "FRAME=", 6) == 0) {
            const long frameNum = ParseFrameNumber(p + 6);
            if (frameNum >= 0) {
                frameIndex.emplace_back(frameNum, pos);
            }
        }

        // Move to next line
        while (*p != '\0' && *p != '\n') {
            p++;
            pos++;
        }
        if (*p == '\n') {
            p++;
            pos++;
        }
    }

    // Files are normally written in frame order, so sort only when needed.
    // The stable sort keeps repeated frames in file order for the pass below.
    const auto byFrame = [](const std::pair<long, long>& a, const std::pair<long, long>& b) {
        return a.first < b.first;
    };
    if (!std::is_sorted(frameIndex.begin(), frameIndex.end(), byFrame)) {
        std::stable_sort(frameIndex.begin(), frameIndex.end(), byFrame);
    }

    // Keep the last occurrence of each repeated frame number, as the old
    // table overwrite did
    size_t kept = 0;
    for (size_t i = 0; i < frameIndex.size(); ++i) {
        if (i + 1 < frameIndex.size() && frameIndex[i + 1].first == frameIndex[i].first) {
            continue;
        }
        frameIndex[kept++] = frameIndex[i];
    }
    frameIndex.resize(kept);
}

/*****************************************************************************
 * DispParameter
 *
 * Display parameter file data in the panel area
 * Shows 2 lines of parameter text below the panel title
 *
 *****************************************************************************/
void Panel::DispParameter(const DrawContext& ctx, int fileIndex)
{
    if (!m_parActive || !m_buf[fileIndex]) return;
    if (m_fileType[fileIndex] != ftParameter) return;

    WRect paramBox = m_panelRect;

    if (m_isUpper) {
        // Upper panel: parameter display below Frame= line and separator
        // Frame line is at top, separator at charHeight + 1, then PANEL_Y_LINE_SPACE spacing
        paramBox.a.y = m_panelRect.a.y + ctx.charHeight + 1 + PANEL_Y_LINE_SPACE + 1;
        paramBox.b.y = paramBox.a.y + 2 * (ctx.charHeight + PANEL_Y_LINE_SPACE);
    } else {
        // Lower panel: parameter display above Frame= line and separator
        // Frame line is at bottom - charHeight, separator at bottom - charHeight - 12
        paramBox.b.y = m_panelRect.b.y - ctx.charHeight - 1 - PANEL_Y_LINE_SPACE - 1;
        paramBox.a.y = paramBox.b.y - 2 * (ctx.charHeight + PANEL_Y_LINE_SPACE);
    }

    // Call DispID to render the parameter text
    DispID(ctx, paramBox, m_buf[fileIndex], m_fileLength[fileIndex], m_fileFrameTotal[fileIndex],
           m_frameBegin, m_parameterIndex[fileIndex]);
}

/*****************************************************************************
 * DispID
 *
 * Display 2 lines of parameter data at current frame position
 * Uses yellow text color and extracts data from frame index
 *
 *****************************************************************************/
void Panel::DispID(const DrawContext& ctx, const WRect& box, const char* buf, long bufLength,
                   long totalFrames, int frameBegin, const ParameterIndex& paramIndex)
{
    if (!buf || !ctx.memDC) return;

    char line1[256] = {0};
    char line2[256] = {0};
    WCHAR wLine1[256] = {0};
    WCHAR wLine2[256] = {0};

    // Check frame bounds
    if (frameBegin < 0 || frameBegin >= totalFrames) return;

    const auto entry = std::lower_bound(
        paramIndex.begin(), paramIndex.end(), static_cast<long>(frameBegin),
        [](const std::pair<long, long>& item, long frame) { return item.first < frame; });
    if (entry == paramIndex.end() || entry->first != frameBegin) return;  // Frame missing from this file

    // Get file position for this frame; the buffer holds bufLength bytes plus a
    // terminating NUL that bounds the line scans below.
    long pos = entry->second;
    if (pos < 0 || pos >= bufLength) return;

    // Find the start of frame line
    const char* p = buf + pos;

    // Skip the FRAME= line
    while (*p != '\0' && *p != '\n') p++;
    if (*p == '\n') p++;

    // Extract first data line
    int i = 0;
    while (*p != '\0' && *p != '\n' && i < 255) {
        line1[i++] = *p++;
    }
    line1[i] = '\0';
    if (*p == '\n') p++;

    // Extract second data line
    i = 0;
    while (*p != '\0' && *p != '\n' && i < 255) {
        line2[i++] = *p++;
    }
    line2[i] = '\0';

    // Convert to wide strings
    MultiByteToWideChar(CP_ACP, 0, line1, -1, wLine1, 256);
    MultiByteToWideChar(CP_ACP, 0, line2, -1, wLine2, 256);

    // Set yellow text color for parameters
    COLORREF oldColor = SetTextColor(ctx.memDC, RGB(255, 255, 0));

    // Calculate maximum available width (with 1 charwidth margins on both sides)
    int maxWidth = box.b.x - box.a.x - 2 * ctx.charWidth;
    int startX = box.a.x + ctx.charWidth;

    // Clip first line to fit within boundaries
    SIZE textSize;
    int len1 = (int)wcslen(wLine1);
    GetTextExtentPoint32(ctx.memDC, wLine1, len1, &textSize);
    if (textSize.cx > maxWidth) {
        // Find how many characters fit
        for (int i = len1 - 1; i > 0; i--) {
            GetTextExtentPoint32(ctx.memDC, wLine1, i, &textSize);
            if (textSize.cx <= maxWidth) {
                len1 = i;
                break;
            }
        }
    }

    // Display first line
    TextOut(ctx.memDC, startX, box.a.y + (PANEL_Y_LINE_SPACE >> 1), wLine1, len1);

    // Clip second line to fit within boundaries
    int len2 = (int)wcslen(wLine2);
    GetTextExtentPoint32(ctx.memDC, wLine2, len2, &textSize);
    if (textSize.cx > maxWidth) {
        // Find how many characters fit
        for (int i = len2 - 1; i > 0; i--) {
            GetTextExtentPoint32(ctx.memDC, wLine2, i, &textSize);
            if (textSize.cx <= maxWidth) {
                len2 = i;
                break;
            }
        }
    }

    // Display second line
    TextOut(ctx.memDC, startX, box.a.y + (ctx.charHeight + PANEL_Y_LINE_SPACE), wLine2, len2);

    // Restore original text color
    SetTextColor(ctx.memDC, oldColor);
}

// ===================== FFT Support =======================

/*****************************************************************************
 * LoadTwiddles
 *
 * Build the twiddle table used by FFT(). FFT() previously called cos() and
 * sin() once per butterfly group - 2*(2^n - 1) libm calls per transform,
 * which cost more than the butterflies themselves. The values only depend on
 * the transform size, so they are computed once in double precision. This is
 * also more accurate than the original, which advanced the angle by repeated
 * float addition and so accumulated phase drift across a stage.
 *
 *****************************************************************************/
void Panel::LoadTwiddles(int stage)
{
    if (m_fftTwiddleStage == stage) return;

    const int size = 1 << stage;
    const int half = size >> 1;
    if (half > FFT_LENGTH_HALF) return;

    const double step = 6.283185307179586 / (double)size;
    for (int j = 0; j < half; j++) {
        const double angle = step * (double)j;
        m_fftTwiddleCos[j] = (float)cos(angle);
        m_fftTwiddleSin[j] = (float)sin(angle);
    }
    m_fftTwiddleStage = stage;
}

/*****************************************************************************
 * LoadHamming
 *
 * Cache the Hamming window used by DispSpectrum, which previously recomputed
 * cos() for every sample of every frame it drew.
 *
 *****************************************************************************/
void Panel::LoadHamming(int dataLen)
{
    if (m_hammingLength == dataLen) return;
    if (dataLen < 2 || dataLen > FFT_LENGTH) return;

    const double doubleConst = 6.2831853 / (double)(dataLen - 1);
    for (int k = 0; k < dataLen; k++) {
        m_hammingWindow[k] = 0.54f - 0.46f * (float)cos((double)k * doubleConst);
    }
    m_hammingLength = dataLen;
}

/*****************************************************************************
 * FFT
 *
 * Main FFT calculation using Cooley-Tukey algorithm
 * Parameters: z (real part), y (imaginary part), n (FFT stage/power-of-2)
 *
 * The decimation, bit reversal and butterfly are unchanged; only the source
 * of the twiddle factors differs (see LoadTwiddles).
 *
 *****************************************************************************/
void Panel::FFT(float* z, float* y, int n)
{
    LoadTwiddles(n);
    FFTTabled(z, y, n, m_fftTwiddleCos, m_fftTwiddleSin);
}

/*****************************************************************************
 * FFTTabled
 *
 * The transform kernel shared by FFT() and RealSpectrum(). cosTab/sinTab must
 * hold 2^(n-1) entries sampling exp(i*2*pi*j / 2^n).
 *
 *****************************************************************************/
void Panel::FFTTabled(float* z, float* y, int n, const float* cosTab, const float* sinTab)
{
    winsap::FftTabled(z, y, n, cosTab, sinTab);
}

/*****************************************************************************
 * LoadRealTables
 *
 * Tables for the real-input transform: twiddles for the half length complex
 * FFT, and the exp(i*2*pi*k/FFT_LENGTH) factors used by the untangle.
 *
 *****************************************************************************/
void Panel::LoadRealTables()
{
    if (m_realTablesReady) return;

    const int halfSize = FFT_LENGTH / 2;   // points in the complex transform
    const double halfStep = 6.283185307179586 / (double)halfSize;
    for (int j = 0; j < halfSize / 2; j++) {
        const double angle = halfStep * (double)j;
        m_realHalfCos[j] = (float)cos(angle);
        m_realHalfSin[j] = (float)sin(angle);
    }

    const double fullStep = 6.283185307179586 / (double)FFT_LENGTH;
    for (int k = 0; k <= halfSize; k++) {
        const double angle = fullStep * (double)k;
        m_realUntangleCos[k] = (float)cos(angle);
        m_realUntangleSin[k] = (float)sin(angle);
    }

    m_realTablesReady = true;
}

/*****************************************************************************
 * RealSpectrum
 *
 * The FFT input is real, so it is wasteful to run a full FFT_LENGTH point
 * complex transform over an array whose imaginary half is all zeros. Pack
 * z[n] = x[2n] + i*x[2n+1] instead and run a half length transform, then
 * recover the true spectrum from the Hermitian structure. Writing E and O for
 * the transforms of the even and odd samples, and using the same exp(+i) sign
 * convention as FFT(),
 *
 *     Z[k]         = E[k] + i*O[k]
 *     conj(Z[M-k]) = E[k] - i*O[k]
 *
 * so E[k] = (Z[k] + conj(Z[M-k]))/2, O[k] = (Z[k] - conj(Z[M-k]))/(2i), and
 * X[k] = E[k] + exp(i*2*pi*k/FFT_LENGTH) * O[k].
 *
 * FFT_CUT_OFF never exceeds M, so k stops one short of M and the wrap-around
 * case never arises. km = (M - k) & mask stays inside the half size buffers for
 * every k in range, including k = 0, which maps to km = 0 and gives the correct
 * DC term.
 *
 * On entry `src` points at `dataLen` samples; on exit m_ar[0..FFT_CUT_OFF-1]
 * holds the squared magnitudes.
 *
 * (This is what LoadSinCos/ComplexFFT/AdjustFFT/RealFFT were reaching for.
 * Those four were removed: nothing ever called them, and ComplexFFT indexed
 * m_fftSinTable up to 523520 on a 512 entry table, so the path could not work
 * at any useful size.)
 *
 *****************************************************************************/
void Panel::RealSpectrum(const short* src, int dataLen)
{
    LoadHamming(dataLen);
    LoadRealTables();

    const int halfSize = FFT_LENGTH / 2;

    // Window and pack: even samples to the real part, odd to the imaginary.
    const int fullPairs = dataLen / 2;
    for (int n = 0; n < fullPairs; n++) {
        m_realZr[n] = (float)src[2 * n] * m_hammingWindow[2 * n];
        m_realZi[n] = (float)src[2 * n + 1] * m_hammingWindow[2 * n + 1];
    }
    int packed = fullPairs;
    if (dataLen & 1) {
        m_realZr[packed] = (float)src[dataLen - 1] * m_hammingWindow[dataLen - 1];
        m_realZi[packed] = 0.0f;
        packed++;
    }
    for (int n = packed; n < halfSize; n++) {
        m_realZr[n] = 0.0f;
        m_realZi[n] = 0.0f;
    }

    FFTTabled(m_realZr, m_realZi, FFT_STAGE - 1, m_realHalfCos, m_realHalfSin);

    const int mask = halfSize - 1;
    for (int k = 0; k < FFT_CUT_OFF; k++) {
        const int km = (halfSize - k) & mask;

        const float zrk = m_realZr[k];
        const float zik = m_realZi[k];
        const float zrm = m_realZr[km];
        const float zim = m_realZi[km];

        const float er = 0.5f * (zrk + zrm);
        const float ei = 0.5f * (zik - zim);

        // O = (Z[k] - conj(Z[M-k])) * (-i/2)
        const float dr = zrk - zrm;
        const float di = zik + zim;
        const float orr = 0.5f * di;
        const float oi = -0.5f * dr;

        const float c = m_realUntangleCos[k];
        const float s = m_realUntangleSin[k];

        const float xr = er + (c * orr - s * oi);
        const float xi = ei + (s * orr + c * oi);

        m_ar[k] = xr * xr + xi * xi;
    }
}

/*****************************************************************************
 * DispSpectrum
 *
 * Display FFT Amplitude spectrum for a file
 * Computes FFT and displays power spectrum in dB scale
 *
 *****************************************************************************/
void Panel::DispSpectrum(const DrawContext& ctx, int fileIndex)
{
    if (m_fileType[fileIndex] != ftFFTAmplitude) return;
    if (Audio(fileIndex).empty()) return;

    // SAP has always had two spectrum displays. One frame in view is drawn as
    // an amplitude against frequency line; a view spanning many frames is drawn
    // as a grey scale spectrogram, frequency against time. The second mode came
    // from the original DOS wsap and was lost in the Windows ports.
    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    const size_t span = static_cast<size_t>(frames) * static_cast<size_t>(m_frameLength);
    if (span > winsap::kSpectrogramThreshold) {
        DispSpectrogram(ctx, fileIndex);
        return;
    }

    DispSpectrumLine(ctx, fileIndex);
}

/*****************************************************************************
 * DispSpectrumLine
 *
 * The single frame spectrum: amplitude in dB against frequency.
 *
 * The bins are reduced to one vertical span per pixel column before drawing.
 * The original emitted a MoveToEx/LineTo pair for each of the 899 bins, which
 * rasterised a near full height segment per bin into a plot only a few hundred
 * pixels wide; the transform itself is a small fraction of that cost.
 *
 *****************************************************************************/
void Panel::DispSpectrumLine(const DrawContext& ctx, int fileIndex)
{
    const std::vector<int16_t>& data = Audio(fileIndex);

    // Honour the frame count: the user asked to see this many frames, so the
    // transform covers all of them. More frames means a longer window, which
    // buys frequency resolution at the cost of time resolution, which is the
    // whole point of the control. Beyond 1024 samples DispSpectrum switches to
    // the spectrogram instead.
    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    const size_t datHp =
        static_cast<size_t>(m_frameBegin) * static_cast<size_t>(m_frameLength);
    const size_t datTp = datHp + static_cast<size_t>(frames) *
                                     static_cast<size_t>(m_frameLength);

    // Validate data availability
    if (datTp > data.size()) return;
    // Clamp to the FFT size so an oversized span cannot overrun m_ar/m_ai.
    size_t span = datTp - datHp;
    if (span > static_cast<size_t>(FFT_LENGTH)) span = static_cast<size_t>(FFT_LENGTH);
    const int dataLen = static_cast<int>(span);
    if (dataLen < 2) return;

    // Window, transform, and leave squared magnitudes in m_ar[].
    RealSpectrum(&data[datHp], dataLen);

    // Calculate power spectrum in normalized dB (0 dB = full scale sine wave)
    const float upLimit = m_upLimit;
    const float lowLimit = m_lowLimit;

    // Reference power: full-scale sine wave magnitude = (N/2) * 32768
    const float refMag = (FFT_LENGTH / 2.0f) * 32768.0f;
    const float refPower = refMag * refMag;

    for (int k = 0; k < FFT_CUT_OFF; k++) {
        m_ar[k] = 10.0f * log10f(m_ar[k] / refPower + 1e-20f);  // Normalized dB
        if (m_ar[k] > upLimit) m_ar[k] = upLimit;
        if (m_ar[k] < lowLimit) m_ar[k] = lowLimit;
    }

    // Get draw area
    WRect waveBox = m_waveBox;
    Panel::GrowRect(&waveBox, -2, -2);

    const int widthPixels = waveBox.b.x - waveBox.a.x;
    const int heightPixels = waveBox.b.y - waveBox.a.y - 2;
    if (widthPixels <= 0 || heightPixels <= 0) return;

    const float verticalStep = (upLimit - lowLimit) / static_cast<float>(heightPixels);
    if (verticalStep <= 0.0f) return;

    HPEN wavePen = ctx.wavePens[fileIndex];
    SelectObject(ctx.memDC, wavePen);

    const int columns = widthPixels < FFT_CUT_OFF ? widthPixels : FFT_CUT_OFF;

    // Two points per column, walked as an envelope so the whole spectrum is a
    // single Polyline call instead of one GDI round trip per bin.
    std::vector<POINT> points;
    points.reserve(static_cast<size_t>(columns) * 2u);

    int bin = 0;
    for (int column = 0; column < columns; column++) {
        const int binEnd =
            static_cast<int>((static_cast<long long>(column) + 1) * FFT_CUT_OFF / columns);
        float lo = m_ar[bin];
        float hi = m_ar[bin];
        for (; bin < binEnd && bin < FFT_CUT_OFF; bin++) {
            if (m_ar[bin] < lo) lo = m_ar[bin];
            if (m_ar[bin] > hi) hi = m_ar[bin];
        }

        const int x = waveBox.a.x +
                      static_cast<int>(static_cast<long long>(column) * widthPixels / columns);
        const LONG yHigh = waveBox.a.y + static_cast<LONG>((upLimit - hi) / verticalStep);
        const LONG yLow = waveBox.a.y + static_cast<LONG>((upLimit - lo) / verticalStep);

        // Alternate the order so consecutive columns join without a stray
        // diagonal across the plot.
        if ((column & 1) == 0) {
            points.push_back(POINT { x, yHigh });
            points.push_back(POINT { x, yLow });
        } else {
            points.push_back(POINT { x, yLow });
            points.push_back(POINT { x, yHigh });
        }
    }

    if (points.size() >= 2) {
        Polyline(ctx.memDC, points.data(), static_cast<int>(points.size()));
    }
}

/*****************************************************************************
 * DispSpectrogram
 *
 * The grey scale spectrogram: frequency against time, silence white and full
 * scale black, as in the original wsap.
 *
 * Columns come from the whole file cache, whose analysis grid is anchored to
 * the file rather than to the view. Scrolling therefore reduces cached columns
 * instead of re-running transforms, and the picture does not shift underneath
 * itself as the frame boundaries move. The cache renders straight into a
 * buffer the size of the plot, so this is one SetDIBitsToDevice rather than
 * the original's pen per grey level and line per pixel row.
 *
 *****************************************************************************/
void Panel::DispSpectrogram(const DrawContext& ctx, int fileIndex)
{
    EnsureSpectrogramCache(fileIndex);

    winsap::SpectrogramCache* cache = m_spectrogram[fileIndex].get();
    if (cache == nullptr || !cache->usable()) return;

    WRect waveBox = m_waveBox;
    Panel::GrowRect(&waveBox, -2, -2);

    const int width = waveBox.b.x - waveBox.a.x;
    const int height = waveBox.b.y - waveBox.a.y;
    if (width <= 0 || height <= 0) return;

    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    const size_t firstSample =
        static_cast<size_t>(m_frameBegin) * static_cast<size_t>(m_frameLength);
    const size_t span = static_cast<size_t>(frames) * static_cast<size_t>(m_frameLength);

    // Full band: Render() reads a non-positive range as the whole spectrum.
    winsap::SpectrogramView view;
    if (!cache->Render(firstSample, span, width, height,
                       m_spectrogramRangeDb, 0.0, 0.0, view)) {
        return;
    }

    // An 8 bit DIB with the selected colour map as its palette, top down so that row 0 is the top
    // of the plot and carries the highest frequency.
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD palette[256];
    } info = {};

    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = view.width;
    info.header.biHeight = -view.height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biCompression = BI_RGB;
    info.header.biClrUsed = 256;
    winsap::SpectrogramColor colors[256];
    winsap::BuildSpectrogramPalette(m_spectrogramPalette, colors);
    for (int level = 0; level < 256; level++) {
        info.palette[level].rgbRed = colors[level].r;
        info.palette[level].rgbGreen = colors[level].g;
        info.palette[level].rgbBlue = colors[level].b;
    }

    // DIB rows are DWORD aligned.
    const int stride = (view.width + 3) & ~3;
    std::vector<BYTE> bits(static_cast<size_t>(stride) * static_cast<size_t>(view.height));
    for (int row = 0; row < view.height; row++) {
        memcpy(bits.data() + static_cast<size_t>(row) * static_cast<size_t>(stride),
               view.grey.data() + static_cast<size_t>(row) * static_cast<size_t>(view.width),
               static_cast<size_t>(view.width));
    }

    SetDIBitsToDevice(ctx.memDC,
                      waveBox.a.x, waveBox.a.y,
                      static_cast<DWORD>(view.width), static_cast<DWORD>(view.height),
                      0, 0,
                      0, static_cast<UINT>(view.height),
                      bits.data(),
                      reinterpret_cast<const BITMAPINFO*>(&info),
                      DIB_RGB_COLORS);

    if (cache->building()) {
        char message[64];
        sprintf_s(message, "analysing %d%%", cache->progress());
        const int oldMode = SetBkMode(ctx.memDC, TRANSPARENT);
        const COLORREF oldColor = SetTextColor(ctx.memDC, RGB(255, 120, 136));
        TextOutA(ctx.memDC, waveBox.a.x + 8, waveBox.a.y + 4,
                 message, static_cast<int>(strlen(message)));
        SetTextColor(ctx.memDC, oldColor);
        SetBkMode(ctx.memDC, oldMode);
    }
}
