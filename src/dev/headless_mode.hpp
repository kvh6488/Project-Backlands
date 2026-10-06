#pragma once

// ============================================================================
// Headless Mode — command line for a scripted, windowless run
// ============================================================================
//   Backlands.exe --headless <scenario.txt> [--out <dir>] [--ticks N]
//
// The window is created hidden, frame pacing is off, ImGui is skipped, and
// the keyboard is replaced by the scenario (dev/scenario.hpp). At each
// checkpoint the harness (dev/headless_harness.hpp) writes
//
//   <out>/<checkpoint>/scene.png       the canvas: pre-shader, pre-UI
//   <out>/<checkpoint>/frame.png       the window: shader, UI, everything
//   <out>/<checkpoint>/telemetry.json  the facts (see core/capture.hpp)
//
// and <out>/run.json when the run ends. --out defaults to a sibling of the
// scenario's directory: scenarios/foo.txt -> artifacts/foo/, which is the
// repo layout and is gitignored. --ticks caps the run; the scenario's own
// length is the normal stop.
//
// Same shape as dev_mode.hpp and debug_seeds.hpp: header-only, argv in,
// a plain struct out.
// ============================================================================

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace headless {

struct Options {
  bool enabled = false;
  const char *scenarioPath = nullptr;
  std::string outDir;
  int maxTicks = -1;
  std::string error; // non-empty = refuse to run
};

inline std::string defaultOutDir(const char *scenarioPath) {
  namespace fs = std::filesystem;
  fs::path p(scenarioPath);
  fs::path dir = p.has_parent_path() ? p.parent_path() : fs::path(".");
  return (dir / ".." / "artifacts" / p.stem()).lexically_normal().string();
}

inline Options parseArgs(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    const bool hasValue = i + 1 < argc;
    if (std::strcmp(a, "--headless") == 0) {
      if (!hasValue) {
        o.error = "--headless needs a scenario file";
        return o;
      }
      o.enabled = true;
      o.scenarioPath = argv[++i];
    } else if (std::strcmp(a, "--out") == 0) {
      if (!hasValue) {
        o.error = "--out needs a directory";
        return o;
      }
      o.outDir = argv[++i];
    } else if (std::strcmp(a, "--ticks") == 0) {
      char *end = nullptr;
      long n = hasValue ? std::strtol(argv[i + 1], &end, 10) : 0;
      if (!hasValue || end == argv[i + 1] || *end != '\0' || n <= 0) {
        o.error = "--ticks needs a positive integer";
        return o;
      }
      o.maxTicks = (int)n;
      ++i;
    }
  }
  if (o.enabled && o.outDir.empty()) {
    o.outDir = defaultOutDir(o.scenarioPath);
  }
  return o;
}

} // namespace headless
