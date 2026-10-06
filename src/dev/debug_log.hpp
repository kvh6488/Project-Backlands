#pragma once

// ============================================================================
// DebugLog — Visually distinct developer logging
// ============================================================================
// Raylib writes its own diagnostics as "[INFO] ...", which is impossible to
// scan past when our messages use the same tag. This header prints a bold
// magenta "[BL]" tag plus a fixed-width category column, so our output forms
// a vertical stripe down the terminal that the eye locks onto immediately.
//
// Header-only on purpose: adding a .cpp would mean editing BOTH executable
// source lists in CMakeLists.txt (see the build notes), and this is not worth
// that footgun.
//
// NOTE: <windows.h> is deliberately NOT included. It defines Rectangle,
// CloseWindow, ShowCursor and LoadImage, every one of which collides with a
// raylib symbol of the same name. Since this header is pulled into raylib
// translation units, the three console calls we need are forward-declared by
// hand instead.
// ============================================================================

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
extern "C" {
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long nStdHandle);
__declspec(dllimport) int __stdcall GetConsoleMode(void *hConsoleHandle,
                                                   unsigned long *lpMode);
__declspec(dllimport) int __stdcall SetConsoleMode(void *hConsoleHandle,
                                                   unsigned long dwMode);
}
#endif

namespace debuglog {

// ANSI SGR codes. Bold + bright magenta for the tag, reset afterwards.
inline constexpr const char *TAG = "\x1b[1;95m[BL]\x1b[0m";
inline constexpr const char *CAT = "\x1b[1;96m"; // bright cyan category
inline constexpr const char *RESET = "\x1b[0m";

// Whether escape codes will actually be interpreted. False when output is
// redirected to a file or a pipe, where raw escapes would just be noise.
inline bool &ansiEnabled() {
  static bool enabled = false;
  return enabled;
}

// Windows consoles do not interpret ANSI escapes until virtual-terminal
// processing is switched on. Called once at startup; a no-op elsewhere.
inline void enableAnsiColors() {
#ifdef _WIN32
  constexpr unsigned long STD_OUT = (unsigned long)-11;
  constexpr unsigned long VT_PROCESSING = 0x0004;

  void *h = GetStdHandle(STD_OUT);
  if (h == nullptr) {
    return;
  }
  unsigned long mode = 0;
  if (!GetConsoleMode(h, &mode)) {
    return; // Redirected to a file or pipe - no console to configure.
  }
  SetConsoleMode(h, mode | VT_PROCESSING);
  ansiEnabled() = true;
#else
  ansiEnabled() = true;
#endif
}

// Prints "[BL] CATEGORY | message". Category is padded to 7 columns so the
// pipe separators line up across every message.
inline void log(const char *category, const char *fmt, ...) {
  if (ansiEnabled()) {
    std::printf("%s %s%-7s%s | ", TAG, CAT, category, RESET);
  } else {
    std::printf("[BL] %-7s | ", category);
  }
  va_list args;
  va_start(args, fmt);
  std::vprintf(fmt, args);
  va_end(args);
  std::printf("\n");
  std::fflush(stdout);
}

} // namespace debuglog
