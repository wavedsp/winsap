/************************************************************************
 *
 *  WinSap C++ Version - File Dialog Functions Implementation
 *
 *  FILE:      FileDialogs.cpp
 *
 *  PURPOSE: File dialog helper implementations
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#include "FileDialogs.h"
#include <string.h>

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

/*****************************************************************************
*  ReadWavFile
*
*  Read the wav file header and extract sampling rate
*
*****************************************************************************/
BOOL ReadWavFile(FILE* fp, long* pcHeaderLength, DWORD* pdwDataLength,
                 DWORD* pdwSamplingRate, unsigned short* pChannels)
{
    unsigned long la;
    unsigned short fmttag = 0;
    unsigned long ckid;
    unsigned long cksize;
    unsigned long filesize;
    unsigned long nextseek;
    unsigned short channel;
    unsigned long sampleRate;

    fseek(fp, 0L, SEEK_SET);
    fread(&la, 1, 4, fp);
    if (la != 0x46464952) {  // 'RIFF'
        return FALSE;
    }

    fread(&filesize, 1, 4, fp);
    filesize += ftell(fp);
    fread(&la, 1, 4, fp);
    if (la != 0x45564157) {  // 'WAVE'
        return FALSE;
    }

    nextseek = ftell(fp);

    while (nextseek < filesize) {
        fseek(fp, nextseek, 0);
        fread(&ckid, 1, 4, fp);
        fread(&cksize, 1, 4, fp);
        nextseek = cksize + ftell(fp) + (cksize & 1);

        switch (ckid) {
        case 0x20746D66:  // 'fmt '
            fread(&fmttag, 1, 2, fp);
            if (fmttag != 0001) {  // WAVE_FORMAT_PCM
                return FALSE;
            }
            fread(&channel, 1, 2, fp);
            fread(&sampleRate, 1, 4, fp);
            *pdwSamplingRate = sampleRate;
            *pChannels = channel;
            break;

        case 0x61746164:  // 'data'
            *pcHeaderLength = ftell(fp);
            *pdwDataLength = cksize;
            fseek(fp, 0L, SEEK_SET);
            return TRUE;
        }
    }

    fseek(fp, 0L, SEEK_SET);
    return FALSE;
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

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, fileName, L"wb") != 0 || fp == nullptr) {
        return FALSE;
    }

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
    ok &= (fwrite(pSamples, 2, sampleCount, fp) == sampleCount);

    if (fclose(fp) != 0) {
        ok = FALSE;
    }

    // Never leave a half written file behind for the user to trip over
    if (!ok) {
        _wremove(fileName);
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
