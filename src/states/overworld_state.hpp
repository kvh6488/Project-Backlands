#pragma once

#include "core/render_settings.hpp"
#include "core/viewport.hpp"
#include "dev/debug_overlay.hpp"
#include "render/draw_queue.hpp"
#include "render/item_renderer.hpp"
#include "render/overworld_renderer.hpp"
#include "render/player_renderer.hpp"
#include "states/game_state.hpp"
#include "states/run.hpp"
#include "ui/ui_manager.hpp"
#include "world/overworld.hpp"
#include <raylib.h>

// ============================================================================
// OverworldState — the surface: walking the island
// ============================================================================
// The hub world. Phase 4 builds it up to the maze's level as a WORLD - seeded,
// rendered, walkable, observable - with no survival systems yet: no day
// counter, weather or meters, and no way down into the maze (Phase 6).
//
// Borrows the Run's seed and player, exactly like MazeState, so the two
// states are interchangeable behind GameState. The bag and the hotbar work
// here; maze furniture (cupboards, doors, the magic book) simply is not.
//
// Frame pipeline (a cut-down MazeState::render - no light mask, no trip
// shader, no radiation):
//   1. Scene into m_screenTarget: terrain, then the Y-sorted queue (player,
//      dropped items, props).
//   2. Blit to the window at blitScale, then UIManager, then DebugOverlay.
// ============================================================================
class OverworldState : public GameState {
public:
  // spawnOverride: a dev-only start tile (the --headless `spawn` line), so a
  // scenario can begin at the wrap seam instead of walking there. Null = the
  // island's own spawn.
  struct SpawnOverride {
    int x, y;
  };
  OverworldState(Run &run, UIManager &uiManager, DebugOverlay &debugOverlay,
                 CaptureSink *capture = nullptr,
                 float blitScale = RenderSettings{}.blitScale,
                 const SpawnOverride *spawnOverride = nullptr);

  void onEnter() override;
  void onExit() override;
  void update(float dt, const InputState &in) override;
  void render(const InputState &in) override;
  void snapshot(Telemetry &out) const override;

  // Puts the Run's player on the spawn tile. The world-building half of
  // onEnter, public so a test can run it without a window.
  void placePlayer();

  // Builds a new island from `seed` and respawns on it - the shipping entry
  // point the debug panel's "new seed" button goes through.
  void regenerate(unsigned int seed);

  const Overworld &getWorld() const { return m_world; }

  // Chunks further than this from the camera are dropped from the cache.
  static constexpr int kRetainChunks = 3;

private:
  void handleInput(const InputState &in);
  void updateCamera();

  Run &m_run;
  UIManager &m_uiManager;
  DebugOverlay &m_debugOverlay;
  CaptureSink *m_capture;
  RenderSettings m_renderSettings;

  Overworld m_world;
  Player &m_player; // m_run.player
  bool m_hasSpawnOverride;
  SpawnOverride m_spawnOverride;

  Camera2D m_camera{};
  Viewport m_canvas;
  RenderTexture2D m_screenTarget{};
  OverworldRenderer m_renderer;
  ItemRenderer m_itemRenderer;
  PlayerRenderer m_playerRenderer;
  DrawQueue m_drawQueue;
  float m_totalTime = 0.0f;
};
