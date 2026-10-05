#pragma once

#include "core/capture.hpp"
#include "core/input_state.hpp"
#include "core/render_settings.hpp"
#include "dev/debug_overlay.hpp"
#include "ui/ui_manager.hpp"
#include "states/game_state.hpp"
#include "states/run.hpp"
#include <memory>

// Which world the run starts in. Until Phase 6 connects them, the surface is
// reachable only by asking for it (--world overworld).
enum class StartWorld { MAZE, OVERWORLD };

// Everything main() decides before the window exists. Defaults are the
// shipping game; the headless harness (dev/headless_mode.hpp) fills in the
// rest. The two pointers are non-owning: main keeps the objects alive for
// longer than the Application.
struct AppConfig {
  unsigned int seed = 0;
  // Human-readable origin of the seed, purely for the startup log.
  const char *seedNote = nullptr;
  // Arms the debug overlay; without it F1 does nothing. See dev/dev_mode.hpp.
  bool devMode = false;
  StartWorld startWorld = StartWorld::MAZE;
  // Overworld only: start on this tile instead of the island's spawn. Set by
  // a scenario's `spawn` line, never by the shipping game.
  bool hasSpawnOverride = false;
  int spawnX = 0, spawnY = 0;

  // Hidden window, no frame pacing, no ImGui. Ticks run flat out.
  bool headless = false;
  int windowW = 1280;
  int windowH = 720;
  float blitScale = RenderSettings{}.blitScale;

  InputSource *input = nullptr;   // null = the keyboard
  CaptureSink *capture = nullptr; // null = nobody is watching
};

// How run() should behave.
struct RunConfig {
  int maxTicks = -1; // -1 = until the window closes or the input runs out
};


// The Application class encapsulates the window and state machine.
class Application {
public:
  explicit Application(const AppConfig &config);
  ~Application();

  // The frame loop. Every tick advances the simulation by exactly kFixedDt
  // of game time regardless of wall-clock frame time: SetTargetFPS paces the
  // loop to real time when a window is up, and a headless run simply omits
  // the pacing to fast-forward. One code path for play and test.
  // Returns the number of ticks that ran.
  int run(const RunConfig &config = RunConfig{});

  static constexpr float kFixedDt = 1.0f / 60.0f;

private:
  const bool m_headless;

  // Where each tick's InputState comes from. Hardware in the shipping game;
  // m_input points at m_hardwareInput unless the config supplied a source.
  HardwareInput m_hardwareInput;
  InputSource *m_input;
  CaptureSink *m_capture;
  // Declared before every state so it outlives them: states borrow it.
  Run m_run;
  UIManager m_uiManager;
  // Owned here rather than by a state so the panel survives future state
  // switches (menu, death screen) and F1 keeps working across them.
  DebugOverlay m_debugOverlay;
  std::unique_ptr<GameState> m_currentState;
};
