/************************************************************************
 *
 *  WinSap C++ Version - File Dialog Functions
 *
 *  FILE:      FileDialogs.h
 *
 *  PURPOSE: File open/save dialog helpers
 *
 *  Update:   2025.4
 *            2026.11 Parameter file name helpers
 *
 ************************************************************************/

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <windows.h>

// File filters for open dialog
extern const WCHAR* fileOpenFilters;

// Parameter mode is hard-linked to the .par extension in every open path.
bool IsParameterFileName(const WCHAR* fileName);
// 1-based OPENFILENAME filter index of the *.par entry in fileOpenFilters.
int ParameterFilterIndex();

// Sample encodings a wav data chunk can hold. Everything is shown and played as
// 16-bit PCM, so the other encodings are converted when the file is loaded.
enum WavSampleFormat {
    wavUnsupported = 0,
    wavPcm8,
    wavPcm16,
    wavPcm24,
    wavPcm32,
    wavFloat32,
    wavFloat64
};

// Helper functions
// Returns TRUE for a RIFF/WAVE file with fmt and data chunks. *pSampleFormat is
// wavUnsupported for encodings that cannot be converted; without pSampleFormat
// only 16-bit PCM is accepted.
BOOL ReadWavFile(FILE* fp, long* pcHeaderLength, DWORD* pdwDataLength,
                 DWORD* pdwSamplingRate, unsigned short* pChannels,
                 int* pSampleFormat = nullptr);
int WavBytesPerSample(int sampleFormat);
const WCHAR* WavSampleFormatName(int sampleFormat);
void ConvertWavSamples(const uint8_t* pSource, size_t sampleCount, int sampleFormat,
                       int16_t* pDestination);
// Reads sampleCount samples at the current file position as 16-bit PCM.
BOOL ReadWavSamples(FILE* fp, size_t sampleCount, int sampleFormat, int16_t* pDestination);
// Reads one channel of frameCount interleaved frames as 16-bit PCM.
BOOL ReadWavChannel(FILE* fp, size_t frameCount, int channels, int channel,
                    int sampleFormat, int16_t* pDestination);
// Writes the data chunk of sourceName as a mono 16-bit PCM wav in the temp folder.
BOOL ConvertWavToPcm16File(const WCHAR* sourceName, long dataOffset, DWORD dataLength,
                           int sampleFormat, DWORD dwSamplingRate,
                           WCHAR* tempName, size_t tempNameSize,
                           long* pcHeaderLength, DWORD* pdwDataLength);
BOOL WriteWavFile(const WCHAR* fileName, const int16_t* pSamples,
                  size_t sampleCount, DWORD dwSamplingRate);
long FileLength(FILE* fp);
void PathName(WCHAR* path, WCHAR* name);
