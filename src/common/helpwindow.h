/************************************************************************
 *
 *  WaveDSP - Help Topics window
 *
 *  FILE:      helpwindow.h
 *
 *  PURPOSE:   A resizable, modeless window that shows an application's help
 *             with sized, coloured headings and shortcut tables. WinSap,
 *             WPlayer, WVista and WinMDV carry identical copies.
 *
 *  Update:    2026.11 Created
 *
 ************************************************************************/

#pragma once

#include <windows.h>

#include <string>

namespace wavedsp {

struct HelpTheme {
  COLORREF background;
  COLORREF text;
  COLORREF title;     // the "# " title
  COLORREF heading;   // "## " sections and the keys in shortcut tables
  COLORREF muted;     // "> " notes
  COLORREF rule;      // shading of every other shortcut row; lifted to RGB(62,62,62) at least
  bool dark;
};

// Opens the help window, or brings the open one forward with the new text and
// colours. `icon` is an icon resource of the executable. The markup has one
// item per line:
//   "# text"        title                 "## text"   section heading
//   "### text"      subheading            "- text"    bullet
//   "> text"        note                  "keys\ttext" shortcut row; adjacent rows form a table
//   anything else is a paragraph; blank lines are ignored. "**text**" is bold.
void ShowHelp(HWND owner, const std::wstring& caption, const std::wstring& markup, const HelpTheme& theme,
  const wchar_t* icon);

// True for messages to the help window, which a message loop must not run
// through the application's accelerators or shortcut keys.
bool IsHelpMessage(const MSG& message);

// The RTF that the markup becomes for a text column `widthTwips` wide.
std::string HelpRtf(const std::wstring& markup, const HelpTheme& theme, int widthTwips);

} // namespace wavedsp
