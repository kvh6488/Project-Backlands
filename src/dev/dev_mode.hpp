#pragma once

// ============================================================================
// Dev Mode — Runtime gate for the debug tooling
// ============================================================================
// The ImGui debug panel (maze stats, minimap, flashlight sliders, magic-book
// and trip forcing) is a development tool, not a game feature. Two switches
// keep it out of the way:
//
//   Backrooms.exe            -> tools unavailable; F1 does nothing
//   Backrooms.exe --dev      -> tools armed and visible; F1 hides/shows them
//
// This is a *runtime* gate, so the code still ships inside the binary. When
// the game gets a real release build, wrap the DebugOverlay in a compile-time
// flag as well so the panel is absent rather than merely hidden.
//
// --world overworld starts the run on the surface. A dev flag until Phase 6
// gives the game a way up and down; --world maze (the default) is accepted
// for symmetry.
//
// Header-only so no CMakeLists.txt source-list edits are needed.
// ============================================================================

#include <cstring>

namespace devmode {

// True when "--dev" appears anywhere on the command line.
inline bool enabledFromArgs(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dev") == 0) {
      return true;
    }
  }
  return false;
}

inline bool overworldFromArgs(int argc, char **argv) {
  for (int i = 1; i < argc - 1; ++i) {
    if (std::strcmp(argv[i], "--world") == 0) {
      return std::strcmp(argv[i + 1], "overworld") == 0;
    }
  }
  return false;
}

} // namespace devmode
