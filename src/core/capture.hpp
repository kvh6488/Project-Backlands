#pragma once

#include "raylib.h"
#include <string>
#include <vector>

// ============================================================================
// Capture — how a headless run observes the game
// ============================================================================
// Two things leave the game during a scripted run: pictures and facts. Both go
// out through here, and the game never learns what is done with them.
//
//   Telemetry    - a plain value a state fills in on request. InputState is a
//                  value IN; this is the value OUT. No JSON, no file I/O in
//                  states/ - the dev/ harness serialises it however it likes.
//   CaptureSink  - the four hook points of one tick. MazeState::render
//                  calls the middle two at the only moments the canvas and
//                  the back buffer are complete; Application brackets the
//                  tick with the outer two. Null in the shipping game, so the
//                  cost is one pointer test per hook.
//
// Kept in core/ so a state depends on nothing under dev/ to be observable.
// ============================================================================

struct Telemetry {
  struct Slot {
    int slot = 0;
    std::string type; // ItemType identifier, e.g. "MUSHROOM"
    int count = 0;
  };
  struct WorldItem {
    int x = 0, y = 0;
    std::string type;
  };

  // --- player ---
  Vector2 playerWorldPos = {0.0f, 0.0f};
  int playerCellX = 0, playerCellY = 0;
  std::string areaState; // "ROOM" / "CORRIDOR"
  std::string facing;    // "UP" / "DOWN" / "LEFT" / "RIGHT"
  float mushroomEffect = 0.0f;
  bool passingOut = false;

  // --- camera, canvas space ---
  Vector2 cameraTarget = {0.0f, 0.0f};
  float cameraZoom = 1.0f;
  Rectangle cameraRect = {0, 0, 0, 0}; // world rect the canvas shows
  int canvasW = 0, canvasH = 0;
  float blitScale = 1.0f;

  // --- ui ---
  bool inventoryOpen = false;
  bool cupboardOpen = false;
  bool fullscreenMapOpen = false;

  // --- world ---
  std::vector<Slot> inventory;       // occupied slots only
  std::vector<WorldItem> visibleItems; // items inside cameraRect that would draw
  int mazeWidth = 0, mazeHeight = 0;
  int nonWallCount = 0, corridorCount = 0;
  int regenCount = 0;
};

class CaptureSink {
public:
  virtual ~CaptureSink() = default;

  // Before update(). The sink decides here whether this tick is worth
  // recording, so the two render hooks below stay cheap on every other tick.
  virtual void beginTick(int tick) = 0;

  // Inside render(), immediately after EndTextureMode(): the scene canvas is
  // complete - pre-shader, pre-UI, pure gameplay.
  virtual void onSceneReady(const RenderTexture2D &canvas) = 0;

  // Inside render(), immediately before EndDrawing(): the whole frame is in
  // the back buffer, trip shader and UI included. It is undefined after the
  // swap, which is why this cannot be done from Application.
  virtual void onFrameReady() = 0;

  // After render(), same tick. `telemetry` is whatever the state reported.
  virtual void endTick(int tick, const Telemetry &telemetry) = 0;
};
