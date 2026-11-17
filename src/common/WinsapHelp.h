/************************************************************************
 *
 *  WinSap - Help Topics text
 *
 *  FILE:      WinsapHelp.h
 *
 *  PURPOSE:   The Help Topics page in helpwindow.h markup. The x64 build
 *             adds stereo WAV files and the file-name menu.
 *
 *  Update:    2026.11 Created
 *
 ************************************************************************/

#pragma once

#include "helpwindow.h"

#include <string>

inline std::wstring WinsapHelpMarkup(bool x64)
{
  std::wstring text =
    L"# WinSap Help\n"
    L"WinSap is a speech and audio analysis program. It shows audio as waveforms, spectra and spectrograms "
    L"in two panels, **Up** and **Down**, each holding several files on a shared timeline, so recordings "
    L"can be compared frame by frame.\n"

    L"## Opening files\n"
    L"Choose **Open Up** (F3) or **Open Down** (F4), pick a file and a mode:\n"
    L"- **Binary** - audio: WAV, or raw 16-bit PCM (.pcm, .dat, .raw, .exc, .res, .cod), drawn as a waveform.\n"
    L"- **Parameter** - a .par text file of parameters, drawn as curves.\n"
    L"- **FFT Amplitude** - audio drawn as a spectrum or spectrogram.\n";
  text += x64 ? L"WAV files may be 8, 16, 24 or 32-bit integer PCM or 32/64-bit float, mono or stereo. "
                L"Raw PCM uses the rate set with **Options > Set Raw Samplingrate**.\n"
              : L"WAV files must be mono, 8, 16, 24 or 32-bit integer PCM or 32/64-bit float; stereo needs "
                L"the 64-bit winsap.exe. Raw PCM uses the rate set with **Options > Set Raw Samplingrate**.\n";
  text +=
    L"The first file in a panel opens in View All. Every file is drawn against its panel's timeline, so a "
    L"sound at frame 150 stays at frame 150 whatever else is open.\n";
  if (x64) {
    text +=
      L"### Stereo files\n"
      L"A stereo WAV opens its left channel in the upper panel and its right channel in the lower one, "
      L"labelled [L] and [R]. The two panels then share frames, zoom and marks, and closing either "
      L"channel closes both.\n"
      L"> A stereo file in FFT Amplitude mode must be the only open file. While a stereo file is open in "
      L"Binary mode, no file can use FFT Amplitude mode, and mono files cannot open in the lower panel.\n";
  }
  text +=
    L"## Moving through a file\n"
    L"The keys move the upper panel when it has files, otherwise the lower one. With **Lock Starting "
    L"Frame** (Ctrl+L) on, the lower panel follows the upper panel's start.\n"
    L"Left arrow / Right arrow\tOne frame back or forward\n"
    L"Up arrow / Down arrow\tOne screen back or forward\n"
    L"Page Up / Page Down\tBack or forward by the frame skip\n"
    L"Home / End\tStart or end of the longest file\n"
    L"The **Up** and **Down** menus set each panel's Frame Length, Goto Frame, Number of Frames (the frames "
    L"on screen), amplitude Limit and Page Numbers.\n"

    L"## Marking and saving\n"
    L"Right-click a waveform twice to mark a range; right-click a third time to clear it. **Save As** (F2) "
    L"writes the marked range of the first file in the upper panel as a WAV file, or of the lower panel "
    L"when the upper one has no mark. **Look Marked Block** (Ctrl+F4) fills the screen with the mark.\n"

    L"## Playing\n"
    L"- **Play All** (F5) plays every open audio file, upper panel first.\n"
    L"- **Play Up** (F6) and **Play Down** (F7) play the marked range of the first file in that panel.\n";
  text += x64 ? L"- Click a file name to play the whole file. A stereo file always plays in stereo.\n"
                L"- Right-click a file name for **Close file** and **Play marked area**; a stereo file adds "
                L"**Play this channel** and **Play marked area in this channel**.\n"
              : L"- Click a file name to play the whole file.\n";
  text +=
    L"- **Stop** (F8), or a right-click, stops playing and clears the queue.\n"
    L"A cursor follows playback, over the spectrogram too.\n"

    L"## Spectrum and spectrogram\n"
    L"In FFT Amplitude mode, a view of 1024 samples or fewer is a line spectrum, level against frequency. "
    L"Raising Number of Frames lengthens the analysis window for finer frequency detail. A longer view "
    L"becomes a spectrogram, frequency against time, with a mark shown as white lines at its edges.\n"
    L"The status bar reads (sample, amplitude) on a waveform, (Hz, dB) on a spectrum and (time, Hz) on a "
    L"spectrogram. The contrast and colour map are remembered for the next start.\n"

    L"## Shortcut keys\n"
    L"### Files\n"
    L"F3 or Ctrl+O\tOpen Up\n"
    L"F4\tOpen Down\n"
    L"F2 or Ctrl+S\tSave the marked range as\n"
    L"F9\tClose files...\n"
    L"Ctrl+W\tClose all\n"
    L"Ctrl+A\tReload all\n"
    L"Alt+X\tExit\n"
    L"### Playing\n"
    L"F5\tPlay all\n"
    L"F6\tPlay Up\n"
    L"F7\tPlay Down\n"
    L"F8\tStop\n"
    L"### View\n"
    L"Ctrl+F7\tZoom in\n"
    L"Ctrl+F8\tZoom out\n"
    L"Ctrl+F9\tView all\n"
    L"Ctrl+F4\tLook at the marked block\n"
    L"Ctrl+F5\tExpand the amplitude\n"
    L"Ctrl+F6\tCompress the amplitude\n"
    L"Ctrl+F2\tAdd Down to Up: overlay the lower waveform in red\n"
    L"Ctrl+F10\tSync panels: copy frame length, count and start\n"
    L"Ctrl+L\tLock starting frame\n"
    L"### Spectrogram\n"
    L"Ctrl+U\tContrast up (narrower dB range)\n"
    L"Ctrl+D\tContrast down (wider dB range)\n"
    L"Ctrl+P\tNext colour map: Viridis, Inferno, Jet, Grey\n"
    L"### Help\n"
    L"F1\tHelp Topics\n"
    L"Alt+?\tAbout WinSap\n"

    L"## Settings\n"
    L"The raw sampling rate (with Options > Save Raw Samplingrate), spectrogram contrast, colour map, "
    L"window position and open-file warnings are kept in HKEY_CURRENT_USER\\Software\\WangTian\\WinSap.\n";
  return text;
}

inline wavedsp::HelpTheme WinsapHelpTheme()
{
  return { RGB(255, 255, 255), RGB(32, 32, 32), RGB(0, 84, 147), RGB(0, 102, 180), RGB(110, 110, 110),
    RGB(240, 244, 249), false };
}
