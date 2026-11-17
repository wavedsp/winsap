/************************************************************************
 *
 *  WaveDSP - Help Topics window
 *
 *  FILE:      helpwindow.cpp
 *
 *  PURPOSE:   Turns the help markup (see helpwindow.h) into RTF and shows
 *             it in a read-only RichEdit (Msftedit.dll, part of Windows)
 *             that fills a resizable, owned top-level window. The title,
 *             section and subheading sizes, the shortcut tables and the
 *             notes are all done in RTF. The text column width is measured
 *             from the window, so tables reflow when it is resized. Keys
 *             scroll the page rather than move a caret; Escape closes it.
 *
 *  Update:    2026.11 Created
 *
 ************************************************************************/

#include "helpwindow.h"

#include <richedit.h>

#undef min
#undef max

#include <algorithm>
#include <cstdlib>

namespace wavedsp {

namespace {

const wchar_t* const ClassName = L"WaveDSPHelpWindow";
const UINT_PTR ReflowTimer = 1;
const int MarginDip = 24;

struct State {
  HWND window = nullptr;
  HWND edit = nullptr;
  WNDPROC editProc = nullptr;
  HICON bigIcon = nullptr;
  HICON smallIcon = nullptr;
  std::wstring markup;
  HelpTheme theme{};
  int widthTwips = 0;
};

State state;

// Dark title bars and scroll bars are optional extras; load them by name so
// no project needs dwmapi.lib or uxtheme.lib for them.
template <typename Function>
Function Proc(const wchar_t* module, const char* name)
{
  HMODULE library = GetModuleHandleW(module);
  if (!library) library = LoadLibraryW(module);
  return library ? reinterpret_cast<Function>(GetProcAddress(library, name)) : nullptr;
}

UINT WindowDpi(HWND window)
{
  typedef UINT(WINAPI * DpiForWindow)(HWND);
  static const DpiForWindow dpiForWindow = Proc<DpiForWindow>(L"user32.dll", "GetDpiForWindow");
  if (window && dpiForWindow) return dpiForWindow(window);
  const HDC screen = GetDC(nullptr);
  const UINT dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
  ReleaseDC(nullptr, screen);
  return dpi ? dpi : 96;
}

void AppendColor(std::string& rtf, COLORREF color)
{
  rtf += "\\red" + std::to_string(GetRValue(color)) + "\\green" + std::to_string(GetGValue(color)) + "\\blue" +
    std::to_string(GetBValue(color)) + ";";
}

// RichEdit draws the text of a cell shaded darker than about RGB(55,55,55)
// in the inverse of the shading, whatever colour the text asks for, so a
// darker row shade is lifted to just above that.
COLORREF ReadableShade(COLORREF color)
{
  const int r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
  const int lift = 62 - (299 * r + 587 * g + 114 * b) / 1000;
  if (lift <= 0) return color;
  const auto up = [lift](int value) { return static_cast<BYTE>(std::min(255, value + lift)); };
  return RGB(up(r), up(g), up(b));
}

// RTF-escapes one line and turns **text** into bold.
std::string Inline(const std::wstring& text)
{
  std::string out;
  bool bold = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const wchar_t c = text[i];
    if (c == L'*' && i + 1 < text.size() && text[i + 1] == L'*') {
      out += bold ? "\\b0 " : "\\b ";
      bold = !bold;
      ++i;
    }
    else if (c == L'\\' || c == L'{' || c == L'}') {
      out += '\\';
      out += static_cast<char>(c);
    }
    else if (c >= 0x20 && c < 0x80) out += static_cast<char>(c);
    else if (c >= 0x80) out += "\\u" + std::to_string(static_cast<int>(static_cast<short>(c))) + "?";
  }
  if (bold) out += "\\b0 ";
  return out;
}

void Layout();
void Render(bool top);

void ApplyTheme()
{
  const HelpTheme& theme = state.theme;
  SendMessageW(state.edit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.background));
  typedef HRESULT(WINAPI * SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
  static const SetAttribute setAttribute = Proc<SetAttribute>(L"dwmapi.dll", "DwmSetWindowAttribute");
  if (setAttribute) {
    const BOOL dark = theme.dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20, and 19 before Windows 10 20H1.
    if (FAILED(setAttribute(state.window, 20, &dark, sizeof(dark)))) setAttribute(state.window, 19, &dark, sizeof(dark));
  }
  typedef HRESULT(WINAPI * SetTheme)(HWND, LPCWSTR, LPCWSTR);
  static const SetTheme setTheme = Proc<SetTheme>(L"uxtheme.dll", "SetWindowTheme");
  if (setTheme) setTheme(state.edit, theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  SetWindowPos(state.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// The edit's formatting rectangle keeps a margin round the text.
void Layout()
{
  RECT client{};
  GetClientRect(state.window, &client);
  MoveWindow(state.edit, 0, 0, client.right, client.bottom, TRUE);
  RECT inside{};
  GetClientRect(state.edit, &inside);
  const UINT dpi = WindowDpi(state.window);
  const int margin = MulDiv(MarginDip, dpi, 96);
  RECT format{ inside.left + margin, inside.top, inside.right - margin, inside.bottom };
  if (format.right <= format.left) format.right = format.left + 1;
  SendMessageW(state.edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&format));
}

// Sets the RTF for the current text width. A reflow keeps the first visible
// line in place; `top` starts at the beginning.
void Render(bool top)
{
  RECT format{};
  SendMessageW(state.edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
  const int width = std::max(2000, MulDiv(format.right - format.left, 1440, static_cast<int>(WindowDpi(state.window))) - 60);
  if (!top && std::abs(width - state.widthTwips) < 60) return;
  state.widthTwips = width;
  LONG first = 0;
  if (!top) {
    const LRESULT line = SendMessageW(state.edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    first = static_cast<LONG>(SendMessageW(state.edit, EM_LINEINDEX, line, 0));
  }
  const std::string rtf = HelpRtf(state.markup, state.theme, width);
  SendMessageW(state.edit, WM_SETREDRAW, FALSE, 0);
  SETTEXTEX set{ ST_DEFAULT, CP_ACP };
  SendMessageW(state.edit, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&set), reinterpret_cast<LPARAM>(rtf.c_str()));
  SendMessageW(state.edit, EM_SETSEL, 0, 0);
  SendMessageW(state.edit, WM_VSCROLL, SB_TOP, 0);
  if (first > 0) {
    const LRESULT line = SendMessageW(state.edit, EM_EXLINEFROMCHAR, 0, first);
    SendMessageW(state.edit, EM_LINESCROLL, 0, line);
  }
  SendMessageW(state.edit, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(state.edit, nullptr, TRUE);
}

// A reader, not an editor: the keys scroll the page, the caret stays hidden
// and Escape closes the window.
LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_KEYDOWN) {
    const bool control = GetKeyState(VK_CONTROL) < 0;
    int code = -1;
    switch (wParam) {
    case VK_ESCAPE: PostMessageW(state.window, WM_CLOSE, 0, 0); return 0;
    case VK_UP: code = SB_LINEUP; break;
    case VK_DOWN: code = SB_LINEDOWN; break;
    case VK_PRIOR: code = SB_PAGEUP; break;
    case VK_NEXT: code = SB_PAGEDOWN; break;
    case VK_HOME: code = SB_TOP; break;
    case VK_END: code = SB_BOTTOM; break;
    case VK_LEFT:
    case VK_RIGHT:
      if (!control) return 0;
      break;
    }
    if (code >= 0) {
      SendMessageW(edit, WM_VSCROLL, static_cast<WPARAM>(code), 0);
      return 0;
    }
  }
  if (message == WM_CHAR && wParam == 27) return 0;
  const LRESULT result = CallWindowProcW(state.editProc, edit, message, wParam, lParam);
  // RichEdit recreates its caret on focus, activation, clicks and selection changes.
  if (message != WM_NCDESTROY && GetFocus() == edit) DestroyCaret();
  return result;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_SIZE:
    if (state.edit && wParam != SIZE_MINIMIZED) {
      Layout();
      SetTimer(window, ReflowTimer, 120, nullptr);
    }
    return 0;
  case WM_TIMER:
    if (wParam == ReflowTimer) {
      KillTimer(window, ReflowTimer);
      Render(false);
      return 0;
    }
    break;
  case WM_DPICHANGED: {
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
      suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    state.widthTwips = 0;
    return 0;
  }
  case WM_GETMINMAXINFO: {
    const UINT dpi = WindowDpi(window);
    MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
    info->ptMinTrackSize.x = MulDiv(380, dpi, 96);
    info->ptMinTrackSize.y = MulDiv(260, dpi, 96);
    return 0;
  }
  case WM_SETFOCUS:
    if (state.edit) SetFocus(state.edit);
    return 0;
  case WM_ACTIVATE:
    // DefWindowProc would give the focus to the frame instead.
    if (state.edit && LOWORD(wParam) != WA_INACTIVE && !HIWORD(wParam)) {
      SetFocus(state.edit);
      return 0;
    }
    break;
  case WM_ERASEBKGND: {
    RECT client{};
    GetClientRect(window, &client);
    const HBRUSH brush = CreateSolidBrush(state.theme.background);
    FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
    DeleteObject(brush);
    return 1;
  }
  case WM_CLOSE:
    DestroyWindow(window);
    return 0;
  case WM_DESTROY:
    KillTimer(window, ReflowTimer);
    // The edit is destroyed after its parent; it must not call back into the cleared state.
    if (state.edit && state.editProc) SetWindowLongPtrW(state.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state.editProc));
    if (state.bigIcon) DestroyIcon(state.bigIcon);
    if (state.smallIcon) DestroyIcon(state.smallIcon);
    state = State();
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

/*****************************************************************************
 * HelpRtf
 *
 * Sizes are in half points and spacing in twips. Shortcut keys take a third
 * of the column, at least 1.4 inches, so the actions line up.
 *
 *****************************************************************************/
std::string HelpRtf(const std::wstring& markup, const HelpTheme& theme, int widthTwips)
{
  std::string rtf = "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0\\fswiss Segoe UI;}{\\f1\\fswiss Segoe UI Semibold;}}{\\colortbl;";
  AppendColor(rtf, theme.text);
  AppendColor(rtf, theme.title);
  AppendColor(rtf, theme.heading);
  AppendColor(rtf, theme.muted);
  AppendColor(rtf, ReadableShade(theme.rule));
  rtf += "}\\viewkind4\n";
  const int keys = std::max(2000, widthTwips / 3);
  bool first = true;
  bool table = false;
  int rows = 0;
  size_t start = 0;
  while (start < markup.size()) {
    size_t end = markup.find(L'\n', start);
    if (end == std::wstring::npos) end = markup.size();
    std::wstring line = markup.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    if (line.empty()) continue;
    const size_t tab = line.find(L'\t');
    const bool row = tab != std::wstring::npos;
    // Space above whatever follows a table, so it does not touch the last rule.
    const std::string after = table && !row ? "\\sb160" : "";
    if (line.compare(0, 2, L"# ") == 0) {
      rtf += "\\pard" + after + (first ? "\\sb240" : "") + "\\sa160\\keepn\\f1\\fs44\\cf2 " + Inline(line.substr(2)) + "\\par\n";
    }
    else if (line.compare(0, 3, L"## ") == 0) {
      rtf += std::string("\\pard\\sb") + (first ? "0" : "320") + "\\sa100\\keepn\\f1\\fs30\\cf3 " + Inline(line.substr(3)) +
        "\\par\n";
    }
    else if (line.compare(0, 4, L"### ") == 0) {
      rtf += std::string("\\pard\\sb") + (first ? "0" : "200") + "\\sa80\\keepn\\f1\\fs23\\cf1 " + Inline(line.substr(4)) +
        "\\par\n";
    }
    else if (line.compare(0, 2, L"- ") == 0) {
      rtf += "\\pard" + after + "\\li420\\fi-260\\tx420\\sa70\\sl276\\slmult1\\f0\\fs21\\cf1 \\u8226?\\tab " +
        Inline(line.substr(2)) + "\\par\n";
    }
    else if (line.compare(0, 2, L"> ") == 0) {
      rtf += "\\pard" + after + "\\sb40\\sa120\\sl276\\slmult1\\f0\\fs20\\cf4\\i " + Inline(line.substr(2)) + "\\i0\\par\n";
    }
    else if (row) {
      // Every other row is shaded, starting with the first.
      rows = table ? rows + 1 : 0;
      const std::string rule = rows % 2 ? "" : "\\clcbpat5";
      rtf += "\\trowd\\trgaph100\\trleft0" + rule + "\\cellx" + std::to_string(keys) + rule + "\\cellx" +
        std::to_string(widthTwips) + "\n\\pard\\intbl\\sb50\\sa50\\f1\\fs20\\cf3 " + Inline(line.substr(0, tab)) +
        "\\cell\\pard\\intbl\\sb50\\sa50\\f0\\fs20\\cf1 " + Inline(line.substr(tab + 1)) + "\\cell\\row\n";
    }
    else {
      rtf += "\\pard" + after + "\\sa120\\sl276\\slmult1\\f0\\fs21\\cf1 " + Inline(line) + "\\par\n";
    }
    table = row;
    first = false;
  }
  rtf += "}";
  return rtf;
}

bool IsHelpMessage(const MSG& message)
{
  return state.window && message.hwnd && (message.hwnd == state.window || IsChild(state.window, message.hwnd));
}

/*****************************************************************************
 * ShowHelp
 *
 * The window is owned by `owner`, so it stays above it and minimizes with
 * it, but it is modeless: the application can be used while reading.
 *
 *****************************************************************************/
void ShowHelp(HWND owner, const std::wstring& caption, const std::wstring& markup, const HelpTheme& theme, const wchar_t* icon)
{
  static const HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
  if (!richEdit) {
    MessageBoxW(owner, L"The help needs Msftedit.dll, which is missing from Windows.", caption.c_str(), MB_OK | MB_ICONERROR);
    return;
  }
  state.markup = markup;
  state.theme = theme;
  if (state.window) {
    SetWindowTextW(state.window, caption.c_str());
    ApplyTheme();
    InvalidateRect(state.window, nullptr, TRUE);
    Render(true);
    if (IsIconic(state.window)) ShowWindow(state.window, SW_RESTORE);
    SetForegroundWindow(state.window);
    SetFocus(state.edit);
    return;
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = ClassName;
    registered = RegisterClassExW(&windowClass) != 0;
  }

  // 720 x 800 DIPs centred on the owner, kept inside its monitor's work area.
  const UINT dpi = WindowDpi(owner);
  MONITORINFO monitor{ sizeof(monitor) };
  GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT& work = monitor.rcWork;
  const int width = std::min(MulDiv(720, static_cast<int>(dpi), 96), static_cast<int>(work.right - work.left));
  const int height = std::min(MulDiv(800, static_cast<int>(dpi), 96), static_cast<int>(work.bottom - work.top));
  RECT around = work;
  if (owner) GetWindowRect(owner, &around);
  int x = (around.left + around.right - width) / 2;
  int y = (around.top + around.bottom - height) / 2;
  x = std::max(static_cast<int>(work.left), std::min(x, static_cast<int>(work.right) - width));
  y = std::max(static_cast<int>(work.top), std::min(y, static_cast<int>(work.bottom) - height));

  state.window = CreateWindowExW(0, ClassName, caption.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
    owner, nullptr, instance, nullptr);
  if (!state.window) return;
  state.edit = CreateWindowExW(0, L"RICHEDIT50W", L"",
    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_DISABLENOSCROLL | ES_NOOLEDRAGDROP, 0, 0, 0, 0,
    state.window, nullptr, instance, nullptr);
  if (!state.edit) {
    DestroyWindow(state.window);
    return;
  }
  state.editProc = reinterpret_cast<WNDPROC>(
    SetWindowLongPtrW(state.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
  SendMessageW(state.edit, EM_SETTARGETDEVICE, 0, 0);
  SendMessageW(state.edit, EM_SETEDITSTYLE, SES_HIDEGRIDLINES, SES_HIDEGRIDLINES);
  if (icon) {
    state.bigIcon = static_cast<HICON>(LoadImageW(instance, icon, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
      GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    state.smallIcon = static_cast<HICON>(LoadImageW(instance, icon, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (state.bigIcon) SendMessageW(state.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(state.bigIcon));
    if (state.smallIcon) SendMessageW(state.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(state.smallIcon));
  }
  ApplyTheme();
  Layout();
  Render(true);
  ShowWindow(state.window, SW_SHOW);
  SetFocus(state.edit);
}

} // namespace wavedsp
