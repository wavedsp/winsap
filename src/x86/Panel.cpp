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
 *                    FFT buffer length counted in samples
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
    , m_spectrogramRangeDb(60.0f)
    , m_spectrogramPalette(winsap::kDefaultSpectrogramPalette)
{
    // Initialize arrays
    for (int i = 0; i < PANELMAXFILE; i++) {
        m_flag[i] = 0;
        m_selectFlag[i] = 0;
        m_fileName[i][0] = L'\0';
        m_fp[i] = nullptr;
        m_buf[i] = nullptr;
        m_bufActive[i] = 0;
        m_fileLength[i] = 0;
        m_audioDataLength[i] = 0;
        m_isWav[i] = FALSE;
        m_sampleFormat[i] = wavPcm16;
        m_tempDataFile[i][0] = L'\0';
        m_fileHeaderLength[i] = 0;
        m_samplingRate[i] = 0;
        m_fileBufHead[i] = 0;
        m_fileBufLen[i] = 0;
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
    long total = 0;
    for (int i = 0; i < PANELMAXFILE; ++i) {
        if (m_flag[i] != 0) {
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
            const long sampleCount = m_audioDataLength[i] / static_cast<long>(sizeof(short));
            m_fileFrameTotal[i] = sampleCount / m_frameLength;
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
                     int sampleFormat, const WCHAR* dataFileName)
{
    if (index < 0 || index >= PANELMAXFILE) {
        return false;
    }

    // Close existing file if open
    CloseFile(index);
    const long previousTotalFrames = GetTotalFrameCount();

    // Copy file name
    wcsncpy_s(m_fileName[index], MAXFILENAME, fileName, _TRUNCATE);
    if (dataFileName != nullptr) {
        // The panel owns the converted copy from here on, even if opening fails
        wcsncpy_s(m_tempDataFile[index], MAX_PATH, dataFileName, _TRUNCATE);
    }

    // Open file based on type
    if (_wfopen_s(&m_fp[index], GetDataFileName(index), L"rb") != 0) {
        CloseFile(index);
        return false;
    }

    // Set file properties
    m_flag[index] = 1;
    m_fileType[index] = fileType;
    m_isWav[index] = isWav;
    m_sampleFormat[index] = isWav ? sampleFormat : wavPcm16;
    m_fileHeaderLength[index] = dataOffset;
    m_samplingRate[index] = samplingRate;

    // Get file length
    fseek(m_fp[index], 0, SEEK_END);
    m_fileLength[index] = ftell(m_fp[index]);
    fseek(m_fp[index], 0, SEEK_SET);

    if (dataOffset < 0 || dataOffset > m_fileLength[index]) {
        CloseFile(index);
        return false;
    }
    const long availableDataLength = m_fileLength[index] - dataOffset;
    m_audioDataLength[index] = dataLength > 0 ? dataLength : availableDataLength;
    if (m_audioDataLength[index] < 0 || m_audioDataLength[index] > availableDataLength) {
        CloseFile(index);
        return false;
    }

    // Set defaults
    SetFileDefaults(index);

    // Handle FFT Amplitude files - set dB limits (0 dB = full scale, -120 dB floor)
    if (fileType == ftFFTAmplitude) {
        m_upLimit = 0.0f;
        m_lowLimit = -120.0f;
        m_fileUpLimit[index] = 0.0f;
        m_fileLowLimit[index] = -120.0f;

        // FFT requires full file buffering
        // Allocate buffer for entire file
        m_buf[index] = new char[m_fileLength[index]];
        if (!m_buf[index]) {
            CloseFile(index);
            return false;
        }

        // Read entire file into buffer
        size_t bytesRead = fread(m_buf[index], sizeof(char), m_fileLength[index], m_fp[index]);
        if (bytesRead != static_cast<size_t>(m_fileLength[index])) {
            CloseFile(index);
            return false;
        }

        m_bufActive[index] = 1;
        // The buffer holds the whole physical file, but the line spectrum
        // indexes it in samples from the start of the audio data, so record
        // the extent in those units rather than in bytes.
        m_fileBufHead[index] = 0;
        m_fileBufLen[index] = static_cast<int>(m_audioDataLength[index] / sizeof(short));
    }

    // Handle parameter files - requires full file buffering
    if (fileType == ftParameter) {
        // Allocate buffer for entire file
        m_buf[index] = new char[m_fileLength[index] + 1];
        if (!m_buf[index]) {
            CloseFile(index);
            return false;
        }

        // Read entire file into buffer
        size_t bytesRead = fread(m_buf[index], sizeof(char), m_fileLength[index], m_fp[index]);
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
        fseek(m_fp[index], 0, SEEK_SET);
        m_fileFrameTotal[index] = ParFileTotal(m_fp[index]);

        // Enable parameter display mode
        m_parActive = true;
    }

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

    if (m_fp[index] != nullptr) {
        fclose(m_fp[index]);
        m_fp[index] = nullptr;
    }
    if (m_tempDataFile[index][0] != L'\0') {
        _wremove(m_tempDataFile[index]);
        m_tempDataFile[index][0] = L'\0';
    }

    if (m_buf[index] != nullptr) {
        delete[] m_buf[index];
        m_buf[index] = nullptr;
    }
    m_parameterIndex[index].clear();

    m_flag[index] = 0;
    m_bufActive[index] = 0;
    m_fileLength[index] = 0;
    m_audioDataLength[index] = 0;
    m_fileName[index][0] = L'\0';
    m_fileType[index] = ftBinaryShort;  // Reset file type

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
    m_fileBufHead[index] = 0L;
    m_fileLimitAuto[index] = 0;
    m_fileIndent[index] = 0;
    m_fileUpLimit[index] = m_upLimit;
    m_fileLowLimit[index] = m_lowLimit;
    m_fileFrameTotal[index] = (long)((float)(m_audioDataLength[index]) /
                                      (2.0f * (float)(m_frameLength)));
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

    if (!m_fp[fileIndex]) {
        errorMsg = "File pointer is null";
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

    if (!m_fp[fileIndex]) {
        return std::nullopt;
    }

    // Get panel data
    int frameBegin = m_frameBegin;
    int frameLength = m_frameLength;
    int frameNum = m_frameNum;
    float upLimit = m_upLimit;
    float lowLimit = m_lowLimit;
    int frameTotal = m_fileFrameTotal[fileIndex];

    // Calculate data range
    long datHp = static_cast<long>(frameBegin) * static_cast<long>(frameLength);
    long datTp = static_cast<long>(frameBegin + frameNum) * static_cast<long>(frameLength);

    // A shorter file may end before the panel's shared visible window. Keep
    // the panel-wide horizontal scale so it remains aligned at frame zero.
    if (datHp >= frameTotal * frameLength || datTp <= datHp) {
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
    long sampleCount = datTp - datHp;
    metrics.horizontalScale = static_cast<float>(widthPixels) / static_cast<float>(sampleCount);

    // Vertical step (amplitude units per pixel)
    int heightPixels = waveBox.b.y - waveBox.a.y - 2;
    metrics.verticalStep = (upLimit - lowLimit) / static_cast<float>(heightPixels);

    return metrics;
}

// ===================== Data Reading =======================
std::optional<std::vector<short>> Panel::ReadWaveformData(int fileIndex, long startSample, long endSample)
{
    // Validate file pointer
    FILE* fp = m_fp[fileIndex];
    if (!fp) {
        return std::nullopt;
    }

    // Validate range
    if (startSample < 0 || endSample <= startSample) {
        return std::nullopt;
    }

    long headerLength = m_fileHeaderLength[fileIndex];
    const int dataSize = sizeof(short);

    // Seek to start position
    long seekPos = startSample * dataSize + headerLength;
    if (fseek(fp, seekPos, SEEK_SET) != 0) {
        return std::nullopt;  // Seek failed
    }

    // Calculate samples to read
    size_t samplesToRead = static_cast<size_t>(endSample - startSample);

    // Allocate vector
    std::vector<short> data(samplesToRead);

    // Read data
    size_t samplesRead = fread(data.data(), dataSize, samplesToRead, fp);

    if (samplesRead == 0) {
        return std::nullopt;  // Read failed
    }

    // Resize if partial read
    if (samplesRead < samplesToRead) {
        data.resize(samplesRead);
    }

    return data;
}

// ===================== Waveform Rendering =======================
void Panel::DrawWaveformBuffered(const DrawContext& ctx, int fileIndex, const WaveformMetrics& metrics)
{
    // Calculate sample range
    int frameBegin = m_frameBegin;
    int frameLength = m_frameLength;
    int frameNum = m_frameNum;
    long datHp = static_cast<long>(frameBegin) * static_cast<long>(frameLength);
    long datTp = static_cast<long>(frameBegin + frameNum) * static_cast<long>(frameLength);

    // Read waveform data into buffer
    auto dataOpt = ReadWaveformData(fileIndex, datHp, datTp);
    if (!dataOpt) {
        return;  // Failed to read data
    }

    const std::vector<short>& data = *dataOpt;
    if (data.empty()) {
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
    short y1 = ClampAndScale(data[0], upLimit, lowLimit, metrics.verticalStep);

    // Draw all points
    for (size_t i = 1; i < data.size(); i++) {
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

void Panel::DrawWaveformStreaming(const DrawContext& ctx, int fileIndex, const WaveformMetrics& metrics)
{
    FILE* fp = m_fp[fileIndex];
    if (!fp) return;

    // Calculate sample range
    int frameBegin = m_frameBegin;
    int frameLength = m_frameLength;
    int frameNum = m_frameNum;
    long datHp = static_cast<long>(frameBegin) * static_cast<long>(frameLength);
    long datTp = static_cast<long>(frameBegin + frameNum) * static_cast<long>(frameLength);

    float upLimit = m_upLimit;
    float lowLimit = m_lowLimit;
    long headerLength = m_fileHeaderLength[fileIndex];
    const int dataSize = sizeof(short);
    HPEN wavePen = ctx.wavePens[fileIndex];

    // Seek to start
    if (fseek(fp, datHp * dataSize + headerLength, SEEK_SET) != 0) {
        return;
    }

    // Initial position
    short x1 = metrics.drawArea.a.x;
    float t = static_cast<float>(x1);
    t += metrics.horizontalScale;
    short x2 = static_cast<short>(t);

    // Read and draw first point
    short y1;
    if (fread(&y1, dataSize, 1, fp) != 1) {
        return;
    }
    y1 = ClampAndScale(y1, upLimit, lowLimit, metrics.verticalStep);

    // Stream remaining points
    for (long j = datHp + 1; j < datTp; j++) {
        short y2;
        if (fread(&y2, dataSize, 1, fp) != 1) {
            break;  // End of file or read error
        }

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
    
    FILE* fp = m_fp[fileIndex];
    if (!fp) return;

    // Calculate sample range
    int frameBegin = m_frameBegin;
    int frameLength = m_frameLength;
    int frameNum = m_frameNum;
    long datHp = static_cast<long>(frameBegin) * static_cast<long>(frameLength);
    long datTp = static_cast<long>(frameBegin + frameNum) * static_cast<long>(frameLength);
    long totalSamples = datTp - datHp;

    // Get display settings
    float upLimit = m_upLimit;
    float lowLimit = m_lowLimit;
    long headerLength = m_fileHeaderLength[fileIndex];
    const int dataSize = sizeof(short);
    HPEN wavePen = ctx.wavePens[fileIndex];

    // Calculate pixel width and samples per pixel
    int widthPixels = metrics.drawArea.b.x - metrics.drawArea.a.x;
    if (widthPixels <= 0) return;
    
    // Samples per pixel (this should be > 1 for decimation to make sense)
    double samplesPerPixel = static_cast<double>(totalSamples) / static_cast<double>(widthPixels);

    // Seek to start
    if (fseek(fp, datHp * dataSize + headerLength, SEEK_SET) != 0) {
        return;
    }

    // Allocate read buffer - read in chunks for efficiency
    const int CHUNK_SIZE = 8192;  // Read 8K samples at a time
    std::vector<short> readBuffer(CHUNK_SIZE);
    
    // Track read position
    long samplesRead = 0;
    int bufferPos = 0;
    int bufferFilled = 0;
    
    // Lambda to get next sample from buffered reads
    auto getNextSample = [&]() -> std::pair<bool, short> {
        if (bufferPos >= bufferFilled) {
            // Need to refill buffer
            long remaining = totalSamples - samplesRead;
            if (remaining <= 0) return {false, 0};
            
            int toRead = static_cast<int>(min(static_cast<long>(CHUNK_SIZE), remaining));
            bufferFilled = static_cast<int>(fread(readBuffer.data(), dataSize, toRead, fp));
            if (bufferFilled <= 0) return {false, 0};
            bufferPos = 0;
        }
        short val = readBuffer[bufferPos++];
        samplesRead++;
        return {true, val};
    };

    SelectObject(ctx.memDC, wavePen);

    short prevMinY = 0, prevMaxY = 0;
    bool firstPixel = true;

    // Process each pixel column
    for (int pixel = 0; pixel < widthPixels; pixel++) {
        // Calculate sample range for this pixel
        long sampleStart = static_cast<long>(pixel * samplesPerPixel);
        long sampleEnd = static_cast<long>((pixel + 1) * samplesPerPixel);
        if (sampleEnd > totalSamples) sampleEnd = totalSamples;
        
        // Skip to sampleStart if needed (shouldn't happen in sequential read)
        while (samplesRead < sampleStart) {
            auto [ok, val] = getNextSample();
            if (!ok) return;
        }
        
        // Find min and max for this pixel
        short minVal = SHRT_MAX;
        short maxVal = SHRT_MIN;
        
        while (samplesRead < sampleEnd) {
            auto [ok, val] = getNextSample();
            if (!ok) break;
            
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

    // Determine rendering strategy based on data size
    int frameLength = m_frameLength;
    int frameNum = m_frameNum;
    long totalSamples = static_cast<long>(frameNum) * static_cast<long>(frameLength);
    const int dataSize = sizeof(short);
    const long bufferThreshold = PRELEN / dataSize;

    // Calculate pixels width to determine if decimation is needed
    int widthPixels = metrics.drawArea.b.x - metrics.drawArea.a.x;
    
    // Use decimation when we have significantly more samples than pixels
    // (horizontalScale < 1.0 means multiple samples per pixel)
    if (metrics.horizontalScale < 0.5f && totalSamples > widthPixels * 2) {
        // Use min/max decimation for large datasets with many samples per pixel
        DrawWaveformDecimated(ctx, fileIndex, metrics);
    } else if (totalSamples <= bufferThreshold) {
        // Use buffered drawing for small datasets
        DrawWaveformBuffered(ctx, fileIndex, metrics);
    } else {
        // Use streaming for large datasets with ~1 sample per pixel
        DrawWaveformStreaming(ctx, fileIndex, metrics);
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

    // Get overlay panel's file data
    FILE* fp = overlayPanel->m_fp[fileIndex];
    if (!fp) return;

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
    long overlayHeaderLength = overlayPanel->m_fileHeaderLength[fileIndex];

    // Calculate overlay data range
    float overlayDatHp = static_cast<float>(overlayFrameBegin) * static_cast<float>(overlayFrameLength);
    float overlayDatTp = static_cast<float>(overlayFrameBegin + overlayFrameNum) * static_cast<float>(overlayFrameLength);

    // Calculate horizontal scale using THIS panel's waveBox width
    float horiz = static_cast<float>(waveBox.b.x - waveBox.a.x) / (overlayDatTp - overlayDatHp);
    
    // Calculate vertical step using OVERLAY panel's box height and limits
    // This matches exactly how the normal drawing calculates step
    float step = (overlayUpLimit - overlayLowLimit) / static_cast<float>(overlayWaveBox.b.y - overlayWaveBox.a.y - 2);
    
    // Read overlay waveform data
    long startSample = static_cast<long>(overlayDatHp);
    long endSample = static_cast<long>(overlayDatTp);
    long totalSamples = endSample - startSample;

    if (totalSamples <= 0) return;

    // Allocate buffer for overlay data
    std::vector<short> overlayData(totalSamples);

    // Read data from overlay panel's file
    long seekPos = overlayHeaderLength + startSample * sizeof(short);
    if (fseek(fp, seekPos, SEEK_SET) != 0) return;

    size_t samplesRead = fread(overlayData.data(), sizeof(short), totalSamples, fp);
    if (samplesRead == 0) return;

    // Draw the overlay waveform in RED
    SelectObject(ctx.memDC, ctx.redPen);

    short x1, x2, y1, y2;
    float t;
    
    // Initialize first point
    x1 = static_cast<short>(waveBox.a.x);
    x2 = x1;
    t = static_cast<float>(waveBox.a.x);
    
    y1 = overlayData[0];
    if (y1 > overlayUpLimit) y1 = static_cast<short>(overlayUpLimit);
    if (y1 < overlayLowLimit) y1 = static_cast<short>(overlayLowLimit);
    y1 = static_cast<short>((overlayUpLimit - y1) / step);

    for (size_t j = 0; j < samplesRead; j++) {
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
            WCHAR name[MAXFILENAME];
            ExtractFileName(m_fileName[i], name);

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
            WCHAR name[MAXFILENAME];
            ExtractFileName(m_fileName[i], name);

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
        if (!m_fp[i]) continue;

        DWORD rate = m_samplingRate[i];
        if (rate == 0) rate = 16000;
        sampleRate = rate;

        const int frames = m_frameNum > 0 ? m_frameNum : 1;
        const size_t span = static_cast<size_t>(frames) * static_cast<size_t>(m_frameLength);
        if (span > 1024u) {
            topHz = static_cast<double>(rate) * FFT_CUT_OFF / FFT_LENGTH;
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

    DrawPlaybackCursor(ctx);
}

void Panel::DrawPlaybackCursor(const DrawContext& ctx)
{
    if (!m_playbackCursorActive || !ctx.memDC) {
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
/*****************************************************************************
 * SetSpectrogramRangeDb
 *
 * Widens or narrows the spectrogram grey ramp, which spans this many dB below
 * full scale. Display only: it changes no analysis.
 *
 *****************************************************************************/
void Panel::SetSpectrogramRangeDb(float rangeDb)
{
    if (rangeDb < 20.0f) rangeDb = 20.0f;
    if (rangeDb > 120.0f) rangeDb = 120.0f;
    m_spectrogramRangeDb = rangeDb;
}

void Panel::DispSpectrum(const DrawContext& ctx, int fileIndex)
{
    if (m_fileType[fileIndex] != ftFFTAmplitude) return;
    if (!m_fp[fileIndex]) return;

    // SAP has always had two spectrum displays. One frame in view is drawn as
    // an amplitude against frequency line; a view spanning many frames is drawn
    // as a grey scale spectrogram, frequency against time. The second mode came
    // from the original DOS wsap and was lost in the Windows ports.
    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    const size_t span = static_cast<size_t>(frames) * static_cast<size_t>(m_frameLength);
    if (span > 1024u) {
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
    // Honour the frame count: the user asked to see this many frames, so the
    // transform covers all of them. More frames means a longer window, which
    // buys frequency resolution at the cost of time resolution, which is the
    // whole point of the control. Beyond 1024 samples DispSpectrum switches to
    // the spectrogram instead.
    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    long datHp = static_cast<long>(m_frameBegin) * static_cast<long>(m_frameLength);
    long datTp = datHp + static_cast<long>(frames) * static_cast<long>(m_frameLength);

    // Validate data availability; positions and the buffer extent are samples
    if (!m_buf[fileIndex] || !m_bufActive[fileIndex]) return;
    if (datHp < m_fileBufHead[fileIndex]) return;
    if (datTp > m_fileBufHead[fileIndex] + m_fileBufLen[fileIndex]) return;

    // Skip the wav header so that it is not analysed as samples
    const short* intData =
        reinterpret_cast<const short*>(m_buf[fileIndex] + m_fileHeaderLength[fileIndex]);
    long dataLen = datTp - datHp;
    // Clamp to the FFT size so an oversized span cannot overrun m_ar/m_ai.
    if (dataLen > FFT_LENGTH) dataLen = FFT_LENGTH;
    if (dataLen < 2) return;

    // Window, transform, and leave squared magnitudes in m_ar[].
    RealSpectrum(intData + static_cast<int>(datHp - m_fileBufHead[fileIndex]),
                 static_cast<int>(dataLen));

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
 * This is the 32-bit build, which streams the waveform from disk rather than
 * holding it in memory, so there is no whole file analysis cache here: the
 * x64 build's cache needs every sample resident, which is exactly what this
 * build exists to avoid. The view is analysed afresh on every repaint, one
 * transform per pixel column, reading only that column's window from disk.
 * Peak working set is therefore one window, whatever the file size, at the
 * cost of being slow and of re-cutting the column grid whenever the view
 * moves. That is the same trade the original wsap made.
 *
 * dB are referenced to a full scale sine, as in the line spectrum, rather
 * than to a peak measured over the view. A view relative reference would make
 * the brightness jump around as the cursor moves, and without the whole file
 * in memory there is no file peak to use instead.
 *
 *****************************************************************************/
void Panel::DispSpectrogram(const DrawContext& ctx, int fileIndex)
{
    if (!m_fp[fileIndex]) return;

    WRect waveBox = m_waveBox;
    Panel::GrowRect(&waveBox, -2, -2);

    const int width = waveBox.b.x - waveBox.a.x;
    const int height = waveBox.b.y - waveBox.a.y;
    if (width <= 0 || height <= 0) return;

    // Total samples available in the file.
    const long headerLength = m_fileHeaderLength[fileIndex];
    const long totalSamples =
        static_cast<long>((m_fileLength[fileIndex] - headerLength) / sizeof(short));
    if (totalSamples < 2) return;

    const int frames = m_frameNum > 0 ? m_frameNum : 1;
    const long firstSample = static_cast<long>(m_frameBegin) * static_cast<long>(m_frameLength);
    long span = static_cast<long>(frames) * static_cast<long>(m_frameLength);
    if (firstSample >= totalSamples) return;
    if (firstSample + span > totalSamples) span = totalSamples - firstSample;
    if (span < 2) return;

    // Analysis window: 20 ms of audio, as in the x64 build, clamped to the
    // transform length. RealSpectrum zero pads whatever it is given.
    DWORD rate = m_samplingRate[fileIndex];
    if (rate == 0) rate = 16000;
    long windowLength = static_cast<long>(rate) / 50;   // 20 ms
    if (windowLength > FFT_LENGTH) windowLength = FFT_LENGTH;
    if (windowLength < 32) windowLength = 32;

    // One column per pixel, so the image needs no horizontal reduction.
    const float rangeDb = m_spectrogramRangeDb > 1.0f ? m_spectrogramRangeDb : 1.0f;
    const float refMag = (FFT_LENGTH / 2.0f) * 32768.0f;
    const float refPower = refMag * refMag;

    const int stride = (width + 3) & ~3;   // DIB rows are DWORD aligned
    std::vector<BYTE> bits(static_cast<size_t>(stride) * static_cast<size_t>(height), 255);

    std::vector<float> columnDb(FFT_CUT_OFF);

    for (int x = 0; x < width; x++) {
        // Centre this column's window on the sample it represents.
        const long centre =
            firstSample + static_cast<long>((static_cast<long long>(x) * span) / width) +
            static_cast<long>((static_cast<long long>(span) / width) / 2);
        long begin = centre - windowLength / 2;
        if (begin < 0) begin = 0;
        if (begin + windowLength > totalSamples) begin = totalSamples - windowLength;
        if (begin < 0) continue;

        std::optional<std::vector<short>> window =
            ReadWaveformData(fileIndex, begin, begin + windowLength);
        if (!window || window->size() < 2) continue;

        RealSpectrum(window->data(), static_cast<int>(window->size()));

        for (int k = 0; k < FFT_CUT_OFF; k++) {
            columnDb[k] = 10.0f * log10f(m_ar[k] / refPower + 1e-20f);
        }

        // Row 0 is the top of the plot and carries the highest frequency.
        // Reduce by maximum so a narrow peak survives the bin to row mapping.
        for (int y = 0; y < height; y++) {
            const int flipped = height - 1 - y;
            int binBegin = static_cast<int>(
                (static_cast<long long>(flipped) * FFT_CUT_OFF) / height);
            int binEnd = static_cast<int>(
                (static_cast<long long>(flipped + 1) * FFT_CUT_OFF) / height);
            if (binBegin < 0) binBegin = 0;
            if (binEnd <= binBegin) binEnd = binBegin + 1;
            if (binEnd > FFT_CUT_OFF) binEnd = FFT_CUT_OFF;

            float peak = columnDb[binBegin];
            for (int k = binBegin + 1; k < binEnd; k++) {
                if (columnDb[k] > peak) peak = columnDb[k];
            }

            // 0 dB (full scale) is black, rangeDb below it is white.
            float fraction = (peak + rangeDb) / rangeDb;
            if (fraction < 0.0f) fraction = 0.0f;
            if (fraction > 1.0f) fraction = 1.0f;
            bits[static_cast<size_t>(y) * static_cast<size_t>(stride) +
                 static_cast<size_t>(x)] = static_cast<BYTE>(255.0f * (1.0f - fraction) + 0.5f);
        }
    }

    // An 8 bit DIB with the selected colour map as its palette, top down so that row 0 is the top.
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD palette[256];
    } info = {};

    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = -height;
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

    SetDIBitsToDevice(ctx.memDC,
                      waveBox.a.x, waveBox.a.y,
                      static_cast<DWORD>(width), static_cast<DWORD>(height),
                      0, 0,
                      0, static_cast<UINT>(height),
                      bits.data(),
                      reinterpret_cast<const BITMAPINFO*>(&info),
                      DIB_RGB_COLORS);
}
