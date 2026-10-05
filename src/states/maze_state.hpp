#pragma once

#include "dev/debug_overlay.hpp"
#include "core/render_settings.hpp"
#include "core/viewport.hpp"
#include "ui/ui_manager.hpp"
#include "entities/player.hpp"
#include "render/player_renderer.hpp"
#include "render/item_renderer.hpp"
#include "raylib.h"
#include "states/game_state.hpp"
#include "states/run.hpp"
#include "world/item_spawner.hpp"
#include "world/maze.hpp"
#include "render/maze_renderer.hpp"
#include <random>
#include <vector>


// The underworld: exploring the maze. Borrows the Run for its seed and its
// player; owns everything that exists only down here.
class MazeState : public GameState {
public:
  // capture is the headless harness's hook set (null in the shipping game);
  // blitScale pins the canvas size so a scripted run's screenshots stay
  // diffable.
  MazeState(Run &run, UIManager &uiManager, DebugOverlay &debugOverlay,
            CaptureSink *capture = nullptr,
            float blitScale = RenderSettings{}.blitScale);
  ~MazeState() override;

  void onEnter() override;
  void onExit() override;
  void update(float dt, const InputState &in) override;
  void render(const InputState &in) override;
  void snapshot(Telemetry &out) const override;

  // The world-building half of onEnter, split out so a test can run it (and
  // a regeneration) without a window. Both are the shipping entry points -
  // the debug panel's regen button goes through regenerateTicTacToeZones too.
  void generateWorld();
  void regenerateTicTacToeZones();
  const Maze &getMaze() const { return m_maze; }
  unsigned int getSeed() const { return m_seed; }

  // The eight shifting strips, derived from the world's dimensions rather than
  // hardcoded to 250x150. A pure function of its arguments — no instance state
  // — so the layout can be pinned by a test without standing up a window.
  static std::vector<Maze::Room> buildTicTacToeZones(int width, int height,
                                                     int thickness);

private:
  void handleInput(const InputState &in);

  // Mouse arrives in window pixels; the scene camera thinks in canvas pixels.
  Vector2 windowToCanvas(Vector2 p) const {
    float s = m_renderSettings.blitScale;
    return {p.x / s, p.y / s};
  }

  // Width of every shifting strip, in cells. The roadmap plans to vary this
  // per night as the run escalates.
  // Keep it >= 6: BSPLeaf::createRooms assumes a leaf at least 6 cells across
  // and its room-size distribution inverts below that.
  int m_zoneThickness = 14;

  // Reference to the global UI Manager
  UIManager &m_uiManager;
  // Development panel. It only edits m_renderSettings and raises request
  // flags; it never touches the world directly.
  DebugOverlay &m_debugOverlay;

  // Presentation values the debug panel tunes but the game owns.
  RenderSettings m_renderSettings;
  // Headless capture hooks; see core/capture.hpp. Null when nobody watches.
  CaptureSink *m_capture;

  // --- Core Systems ---
  Run &m_run;
  unsigned int m_seed; // m_run.seed, copied for the RNG initialisers below
  std::mt19937 m_rng;
  // Each regeneration seeds its own RNG from (m_seed, m_regenCount), so the
  // Nth regen of a seed is the same world no matter what happened in between.
  int m_regenCount = 0;
  Maze m_maze;
  Player &m_player; // m_run.player
  // Scene camera, in CANVAS space: zoom pinned at 1.0, offset = canvas centre.
  Camera2D m_camera{};
  // Canvas = window / blitScale, recomputed each tick (the window resizes).
  Viewport m_canvas;
  MazeRenderer m_renderer;
  ItemRenderer m_itemRenderer;
  PlayerRenderer m_playerRenderer;
  DrawQueue m_drawQueue; // the Y-sorted pass: player + furniture
  ItemSpawner m_itemSpawner;

  // --- Visual Effects ---
  Shader m_tripShader;
  int m_tripTimeLoc;
  int m_tripStrengthLoc;
  float m_totalTime;
  RenderTexture2D m_screenTarget;

  // Interaction State
  int m_focusedCupboardX = -1;
  int m_focusedCupboardY = -1;

  // Radiation rendering state
  float m_radiationFlickerTimer = 0.0f;
  float m_radiationDarknessAlpha = 0.0f;
  float m_radiationNextFlickerTime = 4.0f;

  // Attempts a magic book spawn at the player's current tile and records the
  // outcome in the UI. Shared by the mushroom-trip path and the debug button.
  void attemptMagicBookSpawn();

  // Apparent world-space shift the trip shader gives the scene at the magic
  // book's position this frame. See the implementation for the derivation.
  Vector2 computeTripFollowOffset() const;

  // --- State flags for Popups ---
  bool m_hasSeenCorridorPopup = false;
  bool m_inRadiationZone = false;
  bool m_isDroppingItem = false;
};
