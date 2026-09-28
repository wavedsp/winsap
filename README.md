# WinSap

WinSap is the native Windows speech and audio analysis application.

Open `src\winsap.sln` in Visual Studio 2022. The solution provides:

| Platform | Data model | Output |
|----------|------------|--------|
| x86 | File-backed access using small buffers | `bin\x86\<Configuration>\winsap32.exe` |
| x64 | Loads binary audio data into memory | `bin\x64\<Configuration>\winsap.exe` |

## Windows support

The current builds are supported on Windows 10 and Windows 11:

| Build | Supported Windows installations |
|-------|---------------------------------|
| `winsap32.exe` | 32-bit Windows 10, or 64-bit Windows 10/11 through WOW64 |
| `winsap.exe` | 64-bit Windows 10/11 |

Restrictions:

- Windows 7, Windows 8, and Windows 8.1 are not supported. The code contains
  fallbacks for some older Windows APIs, but the current MSVC v143 toolchain
  and Visual C++ v14 runtime do not provide a supported deployment there.
- The matching [Microsoft Visual C++ v14 Redistributable](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)
  must be installed: x86 for `winsap32.exe` and x64 for `winsap.exe`.
- Neither executable is ARM64-native. Running under Windows on ARM emulation
  has not been tested and is not currently supported.

### File opening

The first file opened in a panel starts in View All. If a later file is longer
than every other file in that panel, WinSap expands from frame zero to show its
full duration. Opening a shorter or equal-length file returns to frame zero
while preserving the current zoom span. This keeps every newly opened track
aligned at the beginning. Every track is drawn against that shared panel
timeline, so a pulse at frame 150 remains at frame 150 when a 3200-frame track
is added. The behavior is shared with XSap and MSap.

The chooser warns when Parameter mode is used without `.par`, or Binary/FFT
mode is used without `.wav`, `.pcm`, `.dat`, `.raw`, `.exc`, `.res`, or `.cod`.
Continuing opens the file and disables only that warning category in
`HKCU\Software\WangTian\WinSap`; cancelling preserves the warning.

### Spectrum display

The spectrum window has two modes, chosen automatically from how much audio the
view spans:

- A view of 1024 samples or fewer is drawn as an amplitude against frequency
  line, as it always has been. The analysis window is every frame the frame
  count asks for, not just the first one, so raising the frame count lengthens
  the window and buys frequency resolution at the cost of time resolution. At
  16 kHz with a frame size of 160, one frame is a 10 ms window and six frames is
  60 ms; past 1024 samples the display switches to the spectrogram.
- A longer view is drawn as a spectrogram, frequency against time. Ctrl+U and
  Ctrl+D narrow and widen the level ramp, which by default spans 60 dB, and
  Ctrl+P steps through the colour maps. The dB range and colour map are saved
  immediately under `HKCU\Software\WangTian\WinSap` and restored at startup.

The vertical axis follows the mode: dB for the line spectrum, Hz from zero to
the top of the plot for the spectrogram.

The status bar readout follows it too, since a sample index against an amplitude
means nothing on either spectrum:

| Display | Readout |
| --- | --- |
| Waveform | `(sample, amplitude)` |
| Line spectrum | `(3520 Hz, -42 dB)` |
| Spectrogram | `(00:00:01.234, 3520 Hz)` |

Spectrogram time is measured from the start of the file, not the start of the
view, because that is what you are usually trying to locate.

Every spectrum display now reaches Nyquist. `FFT_CUT_OFF` used to be 900 bins of
a 2048-point transform, which stopped the axis at 0.44 x sample rate - 7031 Hz
instead of 8000 at 16 kHz. That limit dated from the anti-aliasing filters of
period A/D hardware, whose passband ended well below Nyquist, so the top of the
band was filter roll-off rather than signal. Modern converters are flat much
closer to Nyquist, and the transform measures clean up there (peak level within
0.1 dB of the rest of the band, spurious content below -64 dB), so `FFT_CUT_OFF`
is now `FFT_LENGTH_HALF` and the line spectrum, the x64 cached spectrogram and
the x86 streaming spectrogram all share one ceiling. The original DOS wsap drew
the full half-spectrum too; the cut-off was introduced in the Windows port.

A real tone within a bin or two of Nyquist will read a few dB high, because its
negative-frequency image folds back and overlaps the main lobe. That is inherent
to sampled real signals, not a defect in the analysis.

Colour maps live in `src\common\SpectrogramPalette.h` and are shared by both
builds. Viridis is the default because it is perceptually uniform, so equal
steps in level look like equal steps in brightness and it shows none of the
false ridges jet produces at its cyan and yellow transitions; it is also legible
with the common forms of colour blindness. Jet is offered because a good deal of
commercial speech software uses it, inferno because it suits a dark room, and
grey because it is the original wsap picture, entry for entry.

The two builds reach the same picture by different routes, because they differ
in how they hold audio:

- x64 loads whole files into memory, so it analyses the whole file once into a
  quantised-dB matrix on background threads, prioritising the chunk under the
  view. Scrolling, contrast changes and colour map changes then only re-render
  cached values and never re-run an FFT.
- x86 streams audio from disk in small buffers and deliberately has no cache. It
  reads one analysis window per pixel column, straight from the file, for every
  repaint. Its peak working set is a single window no matter how large the file
  is, and it is correspondingly slower — the same trade this build makes
  everywhere else.

### High-DPI behavior

- Windows 10 version 1703 and later, including Windows 11, use Per-Monitor V2
  DPI awareness.
- Earlier Windows 10 versions fall back to Per-Monitor DPI awareness.
- `GetDpiForWindow` is loaded dynamically. If unavailable, WinSap calculates
  its initial scale from the window device context instead of failing to start.
- WinSap handles `WM_DPICHANGED` and rescales its window, font, toolbar,
  status bar, and toolbar/menu images when the active monitor DPI changes.
- Window restoration remembers the monitor device and work-area-relative
  position. If that monitor is unavailable, WinSap opens on the primary
  monitor instead.

See Microsoft's [High-DPI desktop application guidance](https://learn.microsoft.com/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows)
for the Windows-version behavior of these DPI modes.

Source layout:

- `src\common`: shared Windows UI, dialogs, toolbars, entry point, and resources
- `src\common\icons`: multi-resolution 32-bit toolbar and menu ICO files
- `src\x86`: 32-bit buffered file implementation
- `src\x64`: 64-bit in-memory implementation

Each toolbar ICO contains 16, 20, 24, 32, 40, 48, and 64-pixel images. Replace
an ICO with the same filename to update that command in both the toolbar and
menus.

## Using WinSap

- Open Up/Down supports binary-short audio, parameter text, and FFT-amplitude
  data. Waveforms use connected display. Mono 16-bit PCM WAV files use their
  embedded sample rate; raw PCM uses the rate configured under Options.
- Right-click twice in a waveform to mark a range; right-click again to clear
  it. Save As exports slot 0 from the upper marked panel, or the lower panel
  when the upper panel is not marked.
- Navigation, goto, frame-count, frame-length changes, and zoom are clamped to
  the longest open track in each panel. Lock Starting Frame makes the lower
  start follow the resulting clamped upper start; Sync Panels also copies frame
  length and visible-frame count.
- Play Up/Down plays the marked range from slot 0. Play All queues every open
  binary-short slot in upper-then-lower order. Stop or right-click cancels the
  active item and clears the queue.
- Spectrogram Contrast changes and Colour Map selections become the next-launch
  defaults immediately. Close Files selects individual slots, Close All clears
  both panels, and Reload All refreshes open files.

Command-line release builds from a Visual Studio Developer PowerShell:

```powershell
msbuild src\winsap.sln /m /p:Configuration=Release /p:Platform=x86
msbuild src\winsap.sln /m /p:Configuration=Release /p:Platform=x64
```

## License

WinSap is licensed under the [MIT License](LICENSE). The Viridis and Inferno
spectrogram palettes contain sampled colour data derived from Matplotlib; see
`THIRD_PARTY_NOTICES.md` for its copyright notice, license terms and a summary
of WinSap's changes.

Copyright 2026 Tian Wang.
