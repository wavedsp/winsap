/************************************************************************
 *
 *  WinSap C++ Version - File Dialog Functions
 *
 *  FILE:      FileDialogs.h
 *
 *  PURPOSE: File open/save dialog helpers
 *
 *  Update:   2025.4
 *
 ************************************************************************/

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <windows.h>

// File filters for open dialog
extern const WCHAR* fileOpenFilters;

// Helper functions
BOOL ReadWavFile(FILE* fp, long* pcHeaderLength, DWORD* pdwDataLength,
                 DWORD* pdwSamplingRate, unsigned short* pChannels);
BOOL WriteWavFile(const WCHAR* fileName, const int16_t* pSamples,
                  size_t sampleCount, DWORD dwSamplingRate);
long FileLength(FILE* fp);
void PathName(WCHAR* path, WCHAR* name);
