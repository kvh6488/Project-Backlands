#include "core/application.hpp"
#include "core/asset_load.hpp"
#include "core/grid.hpp"
#include "dev/debug_log.hpp"
#include "items/item_database.hpp"
#include "items/crafting_system.hpp"
#include "imgui.h"
#include "rlImGui.h"
#include "states/maze_state.hpp"


Application::Application(const AppConfig &config)
    : m_headless(config.headless),
      m_input(config.input ? config.input : &m_hardwareInput),
      m_capture(config.capture), m_run(config.seed),
      m_uiManager(config.windowW, config.windowH),
      // The panel needs ImGui, which a headless run never sets up.
      m_debugOverlay(config.devMode && !config.headless) {
  // 0. main() has already armed the logger (it logs before we exist).
  // Raylib's own chatter is also tagged "[INFO]", which drowns our messages.
  // Warnings and errors still come through, so real failures (a texture that
  // did not load) remain visible.
  SetTraceLogLevel(LOG_WARNING);
  debuglog::log("SEED", "%u  (%s)", m_run.seed,
                config.seedNote ? config.seedNote : "unspecified");
  debuglog::log("SEED", "reproduce with:  Backrooms.exe --seed %u", m_run.seed);
  if (m_debugOverlay.isVisible()) {
    debuglog::log("DEV", "debug tools armed  (F1 toggles the panel)");
  }
  if (m_headless) {
    debuglog::log("HEADLESS", "hidden %dx%d window, blit x%.1f, no pacing",
                  config.windowW, config.windowH, config.blitScale);
  }

  // 1. Initialize Raylib System. Headless keeps the GL context (the render
  // texture and the trip shader need one) but hides the window and drops
  // SetTargetFPS so ticks run back-to-back; the fixed dt keeps the simulation
  // seeing 1/60 s per tick regardless.
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | (m_headless ? FLAG_WINDOW_HIDDEN : 0));
  InitWindow(config.windowW, config.windowH, "Project Backrooms");
  SetWindowMinSize(config.windowW, config.windowH);
  if (!m_headless) {
    SetTargetFPS(60);
  }
  // Raylib seeds GetRandomValue from the clock at InitWindow. The radiation
  // flicker and the magic-book roll use it, so pin it to the world seed or
  // two runs of one seed diverge the first time the lights flicker.
  SetRandomSeed(m_run.seed);

  // 1.5 Set Window Icon
  Image iconImage =
      assets::loadImage("assets/guard_yellow_spritesheet.png", "Application");
  if (IsImageReady(iconImage)) {
    ImageCrop(&iconImage, grid::srcTile(0, 0));
    SetWindowIcon(iconImage);
    UnloadImage(iconImage);
  }

  // 2. Initialize ImGui and Textures
  if (!m_headless) {
    rlImGuiSetup(true);
  }

  // 2.5 Initialize Item Database
  ItemDatabase::init();
  CraftingSystem::init();

  // 3. Set Initial State
  m_currentState = std::make_unique<MazeState>(
      m_run, m_uiManager, m_debugOverlay, m_capture, config.blitScale);
  m_currentState->onEnter();
}

Application::~Application() {
  if (m_currentState) {
    m_currentState->onExit();
  }
  if (!m_headless) {
    rlImGuiShutdown();
  }
  CloseWindow();
}

int Application::run(const RunConfig &config) {
  int tick = 0;
  for (; !WindowShouldClose(); ++tick) {
    if (config.maxTicks >= 0 && tick >= config.maxTicks) {
      break;
    }
    if (m_input->finished(tick)) {
      break;
    }

    // F1 is deliberately outside InputState: it is a dev-tool switch, not a
    // player action, and a scripted scenario must not be able to reach it.
    if (!m_headless && IsKeyPressed(KEY_F1)) {
      m_debugOverlay.toggle();
    }

    InputState in = m_input->sample(tick);
    // A click on the debug panel is the panel's, not the game's. Without this
    // gate a slider drag also placed items and picked up inventory.
    if (m_debugOverlay.isVisible() && ImGui::GetIO().WantCaptureMouse) {
      in.mouseLeftPressed = false;
      in.mouseRightPressed = false;
    }

    if (m_currentState) {
      if (m_capture) {
        m_capture->beginTick(tick);
      }
      m_currentState->update(kFixedDt, in);
      m_currentState->render(in);
      if (m_capture) {
        Telemetry t;
        m_currentState->snapshot(t);
        m_capture->endTick(tick, t);
      }

      // Honour a transition only here, after the frame is fully drawn. The
      // state that raised the request is still live during update/render, so
      // destroying it any earlier would pull the object out from under itself.
      if (m_currentState->hasPendingTransition()) {
        if (m_currentState->wantsQuit()) {
          break; // ~Application calls onExit on the way out
        }
        std::unique_ptr<GameState> next = m_currentState->takeNextState();
        m_currentState->onExit();
        m_currentState = std::move(next);
        m_currentState->onEnter();
      }
    }
  }
  return tick;
}
