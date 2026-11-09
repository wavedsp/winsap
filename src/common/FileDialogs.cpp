/************************************************************************
 *
 *  WinSap C++ Version - File Dialog Functions Implementation
 *
 *  FILE:      FileDialogs.cpp
 *
 *  PURPOSE: File dialog helper implementations
 *
 *  Update:   2025.4
 *            2026.11 Checked 64-bit RIFF chunk walk; Save As replaces the
 *                    destination only after a complete staging file is written
 *
 ************************************************************************/

#include "FileDialogs.h"
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

// File filters for open dialog
const WCHAR* fileOpenFilters =
    L"Windows PCM files (*.wav)\0" L"*.wav\0"
    L"Raw PCM data (*.pcm)\0"      L"*.pcm\0"
    L"Data Files (*.dat)\0"        L"*.dat\0"
    L"Output Files (*.out)\0"      L"*.out\0"
    L"Parameter Files (*.par)\0"   L"*.par\0"
    L"Residual Files (*.res)\0"    L"*.res\0"
    L"Excitation Files (*.exc)\0"  L"*.exc\0"
    L"Encoding Files (*.cod)\0"    L"*.cod\0"
    L"All Files (*.*)\0"           L"*.*\0"
    L"\0\0";

bool IsParameterFileName(const WCHAR* fileName)
{
    if (fileName == nullptr) {
        return false;
    }
    const WCHAR* ext = wcsrchr(fileName, L'.');
    return ext != nullptr && _wcsicmp(ext, L".par") == 0;
}

int ParameterFilterIndex()
{
    // The filter list alternates description and pattern strings, so walk the
    // pairs rather than hard-coding a position that drifts when filters change.
    int index = 1;
    for (const WCHAR* p = fileOpenFilters; *p != L'\0'; index++) {
        const WCHAR* pattern = p + wcslen(p) + 1;
        if (_wcsicmp(pattern, L"*.par") == 0) {
            return index;
        }
        p = pattern + wcslen(pattern) + 1;
    }
    return 0;
}

/*****************************************************************************
*  ReadWavFile
*
*  Read the wav file header and extract sampling rate
*
*****************************************************************************/
BOOL ReadWavFile(FILE* fp, long* pcHeaderLength, DWORD* pdwDataLength,
                 DWORD* pdwSamplingRate, unsigned short* pChannels,
                 int* pSampleFormat)
{
    unsigned long la;
    BOOL haveFormat = FALSE;
    int sampleFormat = wavUnsupported;
    unsigned long ckid;
    unsigned long cksize;
    unsigned long riffSize;
    unsigned short channel = 0;
    unsigned long sampleRate = 0;

    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        return FALSE;
    }
    const long long physicalSize = _ftelli64(fp);
    fseek(fp, 0L, SEEK_SET);
    if (physicalSize < 12 || fread(&la, 1, 4, fp) != 4 || la != 0x46464952) {  // 'RIFF'
        fseek(fp, 0L, SEEK_SET);
        return FALSE;
    }

    if (fread(&riffSize, 1, 4, fp) != 4 || fread(&la, 1, 4, fp) != 4 ||
        la != 0x45564157) {  // 'WAVE'
        fseek(fp, 0L, SEEK_SET);
        return FALSE;
    }

    // Chunk positions are 64-bit so that a chunk size near 4 GB cannot wrap the
    // next offset back to an earlier chunk, and the walk stops at whichever of
    // the declared RIFF end or the physical end of file comes first.
    long long riffEnd = 8 + (long long)riffSize;
    if (riffEnd > physicalSize) {
        riffEnd = physicalSize;
    }
    long long chunkPos = 12;

    while (chunkPos + 8 <= riffEnd) {
        if (_fseeki64(fp, chunkPos, SEEK_SET) != 0 ||
            fread(&ckid, 1, 4, fp) != 4 || fread(&cksize, 1, 4, fp) != 4) {
            break;
        }
        const long long chunkData = chunkPos + 8;
        // RIFF chunks are word aligned, so an odd sized chunk carries a pad byte
        const long long nextChunk = chunkData + (long long)cksize + (cksize & 1);

        switch (ckid) {
        case 0x20746D66: {  // 'fmt '
            // WAVEFORMATEX, optionally followed by the WAVE_FORMAT_EXTENSIBLE fields
            uint8_t fmt[40] = {};
            const size_t fmtLength = cksize < sizeof(fmt) ? cksize : sizeof(fmt);
            if (fmtLength < 16 || fread(fmt, 1, fmtLength, fp) != fmtLength) {
                return FALSE;
            }
            unsigned short fmttag = (unsigned short)(fmt[0] | (fmt[1] << 8));
            channel = (unsigned short)(fmt[2] | (fmt[3] << 8));
            memcpy(&sampleRate, fmt + 4, 4);
            const unsigned short blockAlign = (unsigned short)(fmt[12] | (fmt[13] << 8));
            const unsigned short bits = (unsigned short)(fmt[14] | (fmt[15] << 8));
            if (fmttag == 0xFFFE) {  // WAVE_FORMAT_EXTENSIBLE: the real tag leads the SubFormat GUID
                static const uint8_t guidTail[12] = {
                    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38 };
                fmttag = 0;
                if (fmtLength >= 40 && memcmp(fmt + 26, guidTail, sizeof(guidTail)) == 0 &&
                    fmt[38] == 0x9B && fmt[39] == 0x71) {
                    fmttag = (unsigned short)(fmt[24] | (fmt[25] << 8));
                }
            }
            sampleFormat = wavUnsupported;
            if (fmttag == 1) {  // WAVE_FORMAT_PCM
                if (bits == 8) sampleFormat = wavPcm8;
                else if (bits == 16) sampleFormat = wavPcm16;
                else if (bits == 24) sampleFormat = wavPcm24;
                else if (bits == 32) sampleFormat = wavPcm32;
            }
            else if (fmttag == 3) {  // WAVE_FORMAT_IEEE_FLOAT
                if (bits == 32) sampleFormat = wavFloat32;
                else if (bits == 64) sampleFormat = wavFloat64;
            }
            if (channel == 0 || sampleRate == 0 ||
                blockAlign != channel * WavBytesPerSample(sampleFormat)) {
                sampleFormat = wavUnsupported;
            }
            *pdwSamplingRate = sampleRate;
            *pChannels = channel;
            haveFormat = TRUE;
            break;
        }

        case 0x61746164:  // 'data'
            if (!haveFormat) {
                fseek(fp, 0L, SEEK_SET);
                return FALSE;
            }
            if (pSampleFormat != nullptr) {
                *pSampleFormat = sampleFormat;
            }
            else if (sampleFormat != wavPcm16) {
                fseek(fp, 0L, SEEK_SET);
                return FALSE;
            }
            if (chunkData > LONG_MAX) {
                fseek(fp, 0L, SEEK_SET);
                return FALSE;
            }
            *pcHeaderLength = (long)chunkData;
            *pdwDataLength = cksize;
            fseek(fp, 0L, SEEK_SET);
            return TRUE;
        }

        chunkPos = nextChunk;
    }

    fseek(fp, 0L, SEEK_SET);
    return FALSE;
}

int WavBytesPerSample(int sampleFormat)
{
    switch (sampleFormat) {
    case wavPcm8:    return 1;
    case wavPcm16:   return 2;
    case wavPcm24:   return 3;
    case wavPcm32:   return 4;
    case wavFloat32: return 4;
    case wavFloat64: return 8;
    default:         return 0;
    }
}

const WCHAR* WavSampleFormatName(int sampleFormat)
{
    switch (sampleFormat) {
    case wavPcm8:    return L"8-bit PCM";
    case wavPcm16:   return L"16-bit PCM";
    case wavPcm24:   return L"24-bit PCM";
    case wavPcm32:   return L"32-bit PCM";
    case wavFloat32: return L"32-bit float";
    case wavFloat64: return L"64-bit float";
    default:         return L"unsupported";
    }
}

static int16_t SaturateSample(long long value)
{
    return (int16_t)(value > 32767 ? 32767 : (value < -32768 ? -32768 : value));
}

static int16_t FloatToSample(double value)
{
    if (!(value == value)) {
        return 0;  // NaN
    }
    const double scaled = value * 32768.0;
    if (scaled >= 32767.0) return 32767;
    if (scaled <= -32768.0) return -32768;
    return (int16_t)floor(scaled + 0.5);
}

/*****************************************************************************
*  ConvertWavSamples
*
*  Convert little-endian wav samples to 16-bit PCM, rounding to the nearest
*  value and saturating at full scale.
*
*****************************************************************************/
void ConvertWavSamples(const uint8_t* pSource, size_t sampleCount, int sampleFormat,
                       int16_t* pDestination)
{
    for (size_t i = 0; i < sampleCount; ++i) {
        switch (sampleFormat) {
        case wavPcm8:
            pDestination[i] = (int16_t)((pSource[i] - 128) * 256);
            break;
        case wavPcm16:
            pDestination[i] = (int16_t)(pSource[2 * i] | (pSource[2 * i + 1] << 8));
            break;
        case wavPcm24: {
            const uint8_t* s = pSource + 3 * i;
            long long value = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 |
                                        (uint32_t)s[2] << 24) >> 8;
            pDestination[i] = SaturateSample((value + 128) >> 8);
            break;
        }
        case wavPcm32: {
            int32_t value;
            memcpy(&value, pSource + 4 * i, 4);
            pDestination[i] = SaturateSample(((long long)value + 32768) >> 16);
            break;
        }
        case wavFloat32: {
            float value;
            memcpy(&value, pSource + 4 * i, 4);
            pDestination[i] = FloatToSample(value);
            break;
        }
        case wavFloat64: {
            double value;
            memcpy(&value, pSource + 8 * i, 8);
            pDestination[i] = FloatToSample(value);
            break;
        }
        default:
            pDestination[i] = 0;
            break;
        }
    }
}

BOOL ReadWavSamples(FILE* fp, size_t sampleCount, int sampleFormat, int16_t* pDestination)
{
    const int bytesPerSample = WavBytesPerSample(sampleFormat);
    if (bytesPerSample == 0) {
        return FALSE;
    }
    if (sampleFormat == wavPcm16) {
        return fread(pDestination, sizeof(int16_t), sampleCount, fp) == sampleCount;
    }
    const size_t chunkSamples = 65536;
    std::vector<uint8_t> buffer(chunkSamples * bytesPerSample);
    for (size_t done = 0; done < sampleCount; ) {
        const size_t count = sampleCount - done < chunkSamples ? sampleCount - done : chunkSamples;
        if (fread(buffer.data(), bytesPerSample, count, fp) != count) {
            return FALSE;
        }
        ConvertWavSamples(buffer.data(), count, sampleFormat, pDestination + done);
        done += count;
    }
    return TRUE;
}

BOOL ReadWavChannel(FILE* fp, size_t frameCount, int channels, int channel,
                    int sampleFormat, int16_t* pDestination)
{
    if (channels == 1 && channel == 0) {
        return ReadWavSamples(fp, frameCount, sampleFormat, pDestination);
    }
    const int bytesPerSample = WavBytesPerSample(sampleFormat);
    if (bytesPerSample == 0 || channels < 1 || channel < 0 || channel >= channels) {
        return FALSE;
    }
    const size_t frameBytes = (size_t)bytesPerSample * channels;
    const size_t chunkFrames = 65536;
    std::vector<uint8_t> buffer(chunkFrames * frameBytes);
    for (size_t done = 0; done < frameCount; ) {
        const size_t count = frameCount - done < chunkFrames ? frameCount - done : chunkFrames;
        if (fread(buffer.data(), frameBytes, count, fp) != count) {
            return FALSE;
        }
        const uint8_t* pSource = buffer.data() + (size_t)channel * bytesPerSample;
        for (size_t i = 0; i < count; i++, pSource += frameBytes) {
            ConvertWavSamples(pSource, 1, sampleFormat, pDestination + done + i);
        }
        done += count;
    }
    return TRUE;
}

static BOOL WriteWavHeader(FILE* fp, size_t sampleCount, DWORD dwSamplingRate)
{
    const unsigned short channels = 1;
    const unsigned short bitsPerSample = 16;
    const unsigned short blockAlign = channels * bitsPerSample / 8;
    const DWORD byteRate = dwSamplingRate * blockAlign;
    const DWORD dataLength = (DWORD)(sampleCount * 2);
    const DWORD riffLength = 36 + dataLength;
    const DWORD fmtLength = 16;
    const unsigned short fmtTag = 1;  // WAVE_FORMAT_PCM

    BOOL ok = TRUE;
    ok &= (fwrite("RIFF", 1, 4, fp) == 4);
    ok &= (fwrite(&riffLength, 4, 1, fp) == 1);
    ok &= (fwrite("WAVE", 1, 4, fp) == 4);
    ok &= (fwrite("fmt ", 1, 4, fp) == 4);
    ok &= (fwrite(&fmtLength, 4, 1, fp) == 1);
    ok &= (fwrite(&fmtTag, 2, 1, fp) == 1);
    ok &= (fwrite(&channels, 2, 1, fp) == 1);
    ok &= (fwrite(&dwSamplingRate, 4, 1, fp) == 1);
    ok &= (fwrite(&byteRate, 4, 1, fp) == 1);
    ok &= (fwrite(&blockAlign, 2, 1, fp) == 1);
    ok &= (fwrite(&bitsPerSample, 2, 1, fp) == 1);
    ok &= (fwrite("data", 1, 4, fp) == 4);
    ok &= (fwrite(&dataLength, 4, 1, fp) == 1);
    return ok;
}

BOOL ConvertWavToPcm16File(const WCHAR* sourceName, long dataOffset, DWORD dataLength,
                           int sampleFormat, DWORD dwSamplingRate,
                           WCHAR* tempName, size_t tempNameSize,
                           long* pcHeaderLength, DWORD* pdwDataLength)
{
    const int bytesPerSample = WavBytesPerSample(sampleFormat);
    if (sourceName == nullptr || tempName == nullptr || tempNameSize < MAX_PATH ||
        bytesPerSample == 0 || dataOffset < 0 || dwSamplingRate == 0) {
        return FALSE;
    }
    const size_t sampleCount = dataLength / bytesPerSample;
    if (sampleCount > (0xFFFFFFFFUL - 36UL) / 2UL) {
        return FALSE;
    }

    WCHAR tempFolder[MAX_PATH];
    const DWORD folderLength = GetTempPathW(MAX_PATH, tempFolder);
    if (folderLength == 0 || folderLength >= MAX_PATH ||
        GetTempFileNameW(tempFolder, L"wsp", 0, tempName) == 0) {
        return FALSE;
    }

    FILE* source = nullptr;
    FILE* destination = nullptr;
    BOOL ok = _wfopen_s(&source, sourceName, L"rb") == 0 && source != nullptr &&
              _wfopen_s(&destination, tempName, L"wb") == 0 && destination != nullptr &&
              fseek(source, dataOffset, SEEK_SET) == 0 &&
              WriteWavHeader(destination, sampleCount, dwSamplingRate);
    if (ok) {
        const size_t chunkSamples = 65536;
        std::vector<int16_t> samples(chunkSamples);
        for (size_t done = 0; ok && done < sampleCount; ) {
            const size_t count = sampleCount - done < chunkSamples ? sampleCount - done : chunkSamples;
            ok = ReadWavSamples(source, count, sampleFormat, samples.data()) &&
                 fwrite(samples.data(), sizeof(int16_t), count, destination) == count;
            done += count;
        }
    }
    if (source != nullptr) {
        fclose(source);
    }
    if (destination != nullptr && fclose(destination) != 0) {
        ok = FALSE;
    }
    if (!ok) {
        _wremove(tempName);
        tempName[0] = L'\0';
        return FALSE;
    }
    *pcHeaderLength = 44;
    *pdwDataLength = (DWORD)(sampleCount * 2);
    return TRUE;
}

/*****************************************************************************
*  WriteWavFile
*
*  Write a block of samples out as a mono 16 bit PCM wav file. The canonical
*  44 byte RIFF header is built by hand so that the writer stays symmetrical
*  with ReadWavFile and needs no multimedia library.
*
*****************************************************************************/
BOOL WriteWavFile(const WCHAR* fileName, const int16_t* pSamples,
                  size_t sampleCount, DWORD dwSamplingRate)
{
    if (fileName == nullptr || pSamples == nullptr ||
        sampleCount == 0 || dwSamplingRate == 0) {
        return FALSE;
    }

    // A RIFF size field is 32 bits, so refuse anything that would overflow it
    if (sampleCount > (0xFFFFFFFFUL - 36UL) / 2UL) {
        return FALSE;
    }

    // Write a sibling staging file and move it over the destination only once
    // it is complete, so a failed overwrite leaves the previous file intact.
    // The same directory keeps the final rename on one volume.
    std::wstring tempName;
    FILE* fp = nullptr;
    for (int attempt = 0; attempt < 100 && fp == nullptr; attempt++) {
        tempName = std::wstring(fileName) + L"." + std::to_wstring(attempt) + L".wsptmp";
        const errno_t openError = _wfopen_s(&fp, tempName.c_str(), L"wbx");
        if (openError != 0 && openError != EEXIST) {
            fp = nullptr;
            break;
        }
    }
    if (fp == nullptr) {
        return FALSE;
    }

    BOOL ok = WriteWavHeader(fp, sampleCount, dwSamplingRate);
    ok &= (fwrite(pSamples, 2, sampleCount, fp) == sampleCount);
    ok &= (fflush(fp) == 0);
    ok &= (_commit(_fileno(fp)) == 0);

    if (fclose(fp) != 0) {
        ok = FALSE;
    }

    if (ok) {
        ok = MoveFileExW(tempName.c_str(), fileName,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    // Never leave a half written file behind for the user to trip over
    if (!ok) {
        _wremove(tempName.c_str());
    }

    return ok;
}

/*****************************************************************************
*  FileLength
*
*  Get the file length
*
*****************************************************************************/
long FileLength(FILE* fp)
{
    long off;

    fseek(fp, 0L, SEEK_END);
    off = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    return off;
}

/*****************************************************************************
*  PathName
*
*  Extract the file name from a full path
*
*****************************************************************************/
void PathName(WCHAR* path, WCHAR* name)
{
    int i, j;
    int len = (int)wcslen(path);

    for (i = len - 1; i >= 0; i--) {
        if ((path[i] == '\\') || (path[i] == ':') || (path[i] == '/'))
            break;
    }

    j = 0;
    if (i != 0) i++;
    for (; i <= len; i++)
        name[j++] = path[i];
    name[j] = '\0';
}
