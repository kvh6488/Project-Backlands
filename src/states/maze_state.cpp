#include "states/maze_state.hpp"
#include "render/theme.hpp"
#include "items/item_database.hpp"
#include "world/generators/bsp_generator.hpp"
#include "world/generators/loop_generator.hpp"
#include "world/generators/prims_generator.hpp"
#include "world/generators/tunnel_borer.hpp"
#include <algorithm>
#include <cmath>
#include "core/asset_load.hpp"
#include "render/view_bounds.hpp"
#include "dev/debug_log.hpp"
#include <iostream>

MazeState::MazeState(Run &run, UIManager &uiManager,
                     DebugOverlay &debugOverlay, CaptureSink *capture,
                     float blitScale)
    : m_uiManager(uiManager), m_debugOverlay(debugOverlay), m_capture(capture),
      m_run(run), m_seed(run.seed), m_rng(m_seed),
      m_maze(250, 150, grid::CELL, m_seed), m_player(run.player),
      m_itemSpawner(m_rng), m_totalTime(0.0f) {
  m_renderSettings.blitScale = blitScale;
}

MazeState::~MazeState() {}

void MazeState::generateWorld() {
  debuglog::log("MAZE", "generating %dx%d world from seed %u",
                m_maze.getWidth(), m_maze.getHeight(), m_seed);
  m_debugOverlay.setSeed(m_seed);

  BSPGenerator bsp;
  bsp.generate(m_maze, m_rng);
  int midRoomIdx = bsp.getMiddleRoomIndex();

  PrimsGenerator prims;
  prims.generate(m_maze, m_rng, midRoomIdx);

  LoopGenerator loops;
  loops.generate(m_maze, m_rng);

  TunnelBorer borer;
  borer.ensureConnectivity(m_maze);

  prims.pruneSmallAlcoves(m_maze, 5);
  m_itemSpawner.spawnInitialItems(m_maze);

  m_debugOverlay.markMapDirty();
  m_uiManager.markMagicBookMapDirty();

  Vector2 playerStartPos = {0.0f, 0.0f};
  if (!m_maze.getRooms().empty() && midRoomIdx >= 0) {
    const auto &closestRoom = m_maze.getRooms()[midRoomIdx];
    playerStartPos.x =
        (closestRoom.x + closestRoom.width / 2.0f) * m_maze.getCellSize();
    playerStartPos.y =
        (closestRoom.y + closestRoom.height / 2.0f) * m_maze.getCellSize();
  }
  // Place, never rebuild: the player belongs to the Run, bag and all.
  m_player.teleport(playerStartPos, AreaState::ROOM);
}

void MazeState::onEnter() {
  // 1. Load Textures
  m_renderer.loadTextures();
  m_itemRenderer.loadTextures();
  m_playerRenderer.loadTextures();

  // 2. Generate Initial Maze and place the player
  generateWorld();

  // 3. Initialize Camera (canvas space; see core/viewport.hpp)
  m_canvas = Viewport::canvasFor(GetScreenWidth(), GetScreenHeight(),
                                 m_renderSettings.blitScale);
  m_camera = {0};
  m_camera.target = m_player.getPosition();
  m_camera.offset = m_canvas.center();
  m_camera.rotation = 0.0f;
  m_camera.zoom = 1.0f;

  // 5. Load Shaders
  m_tripShader = assets::loadShader(0, "assets/magic_trip.fs", "MazeState");
  m_tripTimeLoc = GetShaderLocation(m_tripShader, "time");
  m_tripStrengthLoc = GetShaderLocation(m_tripShader, "strength");
  m_screenTarget = LoadRenderTexture(m_canvas.width, m_canvas.height);
}

void MazeState::onExit() {
  UnloadRenderTexture(m_screenTarget);
  UnloadShader(m_tripShader);
}

void MazeState::update(float dt, const InputState &in) {
  if (in.toggleFullscreen) {
    ToggleFullscreen();
  }

  if (m_debugOverlay.triggerTicTacToeRegen()) {
    regenerateTicTacToeZones();
    m_debugOverlay.clearTicTacToeRegen();
  }

  if (m_renderSettings.lightSettingsChanged) {
    m_renderer.updateLightSettings(m_renderSettings.lightConeAngle,
                                   m_renderSettings.lightFadeStrength,
                                   m_renderSettings.lightSizeScale);
    m_renderSettings.lightSettingsChanged = false;
  }

  handleInput(in);

  m_player.update(m_maze, dt, in,
                  !m_uiManager.isInventoryOpen() &&
                      !m_uiManager.isFullscreenMapOpen());

  int playerGridX = m_maze.toGridX(m_player.getPosition().x);
  int playerGridY = m_maze.toGridY(m_player.getPosition().y);

  m_maze.updateVisibility(playerGridX, playerGridY, m_player.getAreaState());
  m_playerRenderer.update(dt, m_player);

  // --- Cupboard Interaction Logic ---
  m_focusedCupboardX = -1;
  m_focusedCupboardY = -1;

  Vector2 lookPos = m_player.getPosition();
  switch (m_player.getFacingDirection()) {
  case FacingDirection::UP:
    lookPos.y -= 20.0f;
    break;
  case FacingDirection::DOWN:
    lookPos.y += 20.0f;
    break;
  case FacingDirection::LEFT:
    lookPos.x -= 20.0f;
    break;
  case FacingDirection::RIGHT:
    lookPos.x += 20.0f;
    break;
  }

  int lookGridX = m_maze.toGridX(lookPos.x);
  int lookGridY = m_maze.toGridY(lookPos.y);

  if (m_maze.getItem(lookGridX, lookGridY) == ItemType::CUPBOARD) {
    bool wallAbove =
        m_maze.getCell(lookGridX, lookGridY - 1) == Maze::CELL_WALL;
    bool wallRight =
        m_maze.getCell(lookGridX + 1, lookGridY) == Maze::CELL_WALL;
    bool wallLeft = m_maze.getCell(lookGridX - 1, lookGridY) == Maze::CELL_WALL;

    bool canAccess = false;
    if (wallAbove) {
      canAccess = (m_player.getFacingDirection() == FacingDirection::UP);
    } else if (wallRight) {
      canAccess = (m_player.getFacingDirection() == FacingDirection::RIGHT);
    } else if (wallLeft) {
      canAccess = (m_player.getFacingDirection() == FacingDirection::LEFT);
    }

    if (canAccess) {
      m_focusedCupboardX = lookGridX;
      m_focusedCupboardY = lookGridY;
      if (!m_uiManager.isCupboardInventoryOpen()) {
        m_uiManager.showPopup("Press 'o' to search cupboard",
                              PopupType::SUBTLE_BOTTOM, 0.5f);
      }
    }
  }

  if (m_focusedCupboardX == -1 && m_uiManager.isCupboardInventoryOpen()) {
    m_maze.setItemState(m_uiManager.getOpenedCupboardX(),
                        m_uiManager.getOpenedCupboardY(), 0);
    m_uiManager.closeCupboard();
    if (m_uiManager.isInventoryOpen()) {
      m_uiManager.toggleInventory();
    }
  }

  // Integer target + integer offset + zoom 1.0 = every tile lands on whole
  // canvas texels, which the integer blit then preserves.
  m_canvas = Viewport::canvasFor(GetScreenWidth(), GetScreenHeight(),
                                 m_renderSettings.blitScale);
  m_camera.target = {std::round(m_player.getPosition().x),
                     std::round(m_player.getPosition().y)};
  m_camera.zoom = 1.0f;
  m_camera.offset = {std::floor(m_canvas.width / 2.0f),
                     std::floor(m_canvas.height / 2.0f)};

  // --- Popups Logic (via UIManager) ---
  if (m_player.getAreaState() == AreaState::CORRIDOR &&
      !m_hasSeenCorridorPopup) {
    m_hasSeenCorridorPopup = true;
    m_uiManager.showPopup("Dam it's dark in the corridor",
                          PopupType::BOXED_BOTTOM, 4.0f);
  }

  bool currentlyInRadiation =
      m_maze.getRadiationLevel(playerGridX, playerGridY) > 0;
  if (currentlyInRadiation && !m_inRadiationZone) {
    m_inRadiationZone = true;
    m_uiManager.showPopup("Radiation!", PopupType::HEADER_GREEN, 3.0f);
  } else if (!currentlyInRadiation && m_inRadiationZone) {
    m_inRadiationZone = false;
  }

  // --- Radiation Flicker Logic ---
  if (m_maze.getRadiationLevel(playerGridX, playerGridY) > 0) {
    m_radiationFlickerTimer += dt;
    if (m_radiationFlickerTimer > m_radiationNextFlickerTime) {
      float flickerDuration = 2.0f;
      float t = m_radiationFlickerTimer - m_radiationNextFlickerTime;
      if (t < flickerDuration) {
        float envelope = sinf((t / flickerDuration) * PI);
        float jitter = ((float)GetRandomValue(0, 100) / 100.0f) * 0.4f;
        m_radiationDarknessAlpha = envelope * 0.7f + jitter * envelope;
        if (m_radiationDarknessAlpha > 0.98f)
          m_radiationDarknessAlpha = 0.98f;
        if (m_radiationDarknessAlpha < 0.0f)
          m_radiationDarknessAlpha = 0.0f;
      } else {
        m_radiationDarknessAlpha = 0.0f;
        m_radiationFlickerTimer = 0.0f;
        m_radiationNextFlickerTime = (float)GetRandomValue(350, 550) / 100.0f;
      }
    } else {
      m_radiationDarknessAlpha = 0.0f;
    }
  } else {
    if (m_radiationDarknessAlpha > 0.0f) {
      m_radiationDarknessAlpha -= dt * 2.0f;
      if (m_radiationDarknessAlpha < 0.0f)
        m_radiationDarknessAlpha = 0.0f;
    }
    m_radiationFlickerTimer = 0.0f;
    m_radiationNextFlickerTime = (float)GetRandomValue(350, 550) / 100.0f;
  }

  // --- Check Player Events ---
  if (m_player.pollEventMushroomFullTripStarted()) {
    if (m_player.hasPickedUpMagicBook()) {
      debuglog::log("BOOK", "skipped - already picked up this run");
      m_debugOverlay.setMagicBookStatus("skipped: book already picked up");
    } else if (GetRandomValue(0, 2) != 0) { // 33% chance
      debuglog::log("BOOK", "skipped - lost the 1-in-3 roll");
      m_debugOverlay.setMagicBookStatus("skipped: lost the 1-in-3 roll");
    } else {
      attemptMagicBookSpawn();
    }
  }

  // Debug: force a spawn from the ImGui panel, bypassing the whole ritual.
  if (m_debugOverlay.triggerMagicBookSpawn()) {
    m_debugOverlay.clearMagicBookSpawn();
    attemptMagicBookSpawn();
  }

  // --- Debug trip controls ---
  if (m_debugOverlay.triggerForceTrip()) {
    m_debugOverlay.clearForceTrip();
    m_player.debugForceTrip(60.0f);
    debuglog::log("TRIP", "forced full trip for 60s");
  }
  if (m_debugOverlay.triggerEndTrip()) {
    m_debugOverlay.clearEndTrip();
    m_player.debugEndTrip();
    debuglog::log("TRIP", "trip ended by debug panel");
  }

  if (m_maze.isMagicBookSpawned() && !m_debugOverlay.isMagicBookPinned() &&
      m_player.getMushroomEffectStrength() < 1.0f) {
    debuglog::log("BOOK", "despawned - trip strength fell below 1.0");
    m_maze.despawnMagicBook();
    m_debugOverlay.setMagicBookStatus("despawned: trip strength fell below 1.0");
    m_debugOverlay.markMapDirty();
    m_uiManager.markMagicBookMapDirty();
  }

  if (m_player.pollEventMapCrafted()) {
    int gridX = m_maze.toGridX(m_player.getPosition().x);
    int gridY = m_maze.toGridY(m_player.getPosition().y);
    m_uiManager.markMapDrawn(m_player.getLastConsumedMapId(), m_maze, gridX,
                             gridY);
  }

  if (m_player.pollEventMapOpened()) {
    m_uiManager.openFullscreenMap(m_player.getLastConsumedMapId());
    if (m_uiManager.isInventoryOpen())
      m_uiManager.toggleInventory();
  }

  if (m_player.pollEventMagicBookOpened()) {
    m_uiManager.openFullscreenMap(-1);
    if (m_uiManager.isInventoryOpen())
      m_uiManager.toggleInventory();
  }

  if (m_player.pollEventMushroomConsumed()) {
    m_uiManager.showPopup("Magic mushroom consumed!", PopupType::HEADER_RAINBOW,
                          3.0f);
  }
  if (m_player.pollEventMushroomThree()) {
    m_uiManager.showPopup("I hope I don't trip for too long",
                          PopupType::BOXED_BOTTOM, 4.0f);
  }
  if (m_player.pollEventMushroomWeird()) {
    m_uiManager.showPopup("Wow I feel weird", PopupType::BOXED_BOTTOM, 4.0f);
  }
  if (m_player.pollEventMushroomOver()) {
    m_uiManager.showPopup("I'm glad that is over", PopupType::BOXED_BOTTOM,
                          4.0f);
  }
  if (m_player.pollEventMushroomFirstPickup()) {
    m_uiManager.showPopup("Wonder what these do?", PopupType::BOXED_BOTTOM,
                          4.0f);
  }
  if (m_player.pollEventPassOutComplete()) {
    const auto &rooms = m_maze.getRooms();
    if (!rooms.empty()) {
      int randomIdx = GetRandomValue(0, rooms.size() - 1);
      const auto &room = rooms[randomIdx];
      Vector2 newPos = {(room.x + room.width / 2.0f) * m_maze.getCellSize(),
                        (room.y + room.height / 2.0f) * m_maze.getCellSize()};
      m_player.teleport(newPos, AreaState::ROOM);
    }
    m_uiManager.showPopup("What a trip, where am I?", PopupType::BOXED_BOTTOM,
                          4.0f);
  }

  m_totalTime += dt;
  m_uiManager.update(dt);
}

void MazeState::handleInput(const InputState &in) {
  if (in.toggleInventory) {
    m_uiManager.toggleInventory();
    if (!m_uiManager.isInventoryOpen()) {
      m_uiManager.setHeldSlotIndex(-1);
      if (m_uiManager.getActiveHotbarSlot() >= HOTBAR_SLOTS) {
        m_uiManager.setActiveHotbarSlot(m_uiManager.getActiveHotbarSlot() %
                                        HOTBAR_SLOTS);
      }
      if (m_uiManager.isCupboardInventoryOpen()) {
        m_maze.setItemState(m_uiManager.getOpenedCupboardX(),
                            m_uiManager.getOpenedCupboardY(), 0);
        m_uiManager.closeCupboard();
      }
    }
  }

  if (in.openCupboard && m_focusedCupboardX != -1) {
    if (m_uiManager.isCupboardInventoryOpen()) {
      m_maze.setItemState(m_focusedCupboardX, m_focusedCupboardY, 0);
      m_uiManager.closeCupboard();
      if (m_uiManager.isInventoryOpen()) {
        m_uiManager.toggleInventory();
      }
    } else {
      m_maze.setItemState(m_focusedCupboardX, m_focusedCupboardY, 1);
      m_uiManager.openCupboard(m_focusedCupboardX, m_focusedCupboardY);
      if (!m_uiManager.isInventoryOpen()) {
        m_uiManager.toggleInventory();
      }
    }
  }

  // Mouse input for the inventory, cupboard and crafting panels. It runs here,
  // in the input phase, so that render() can stay read-only - the UI used to
  // resolve clicks mid-draw.
  m_uiManager.handleInventoryInput(m_player, m_maze, in);

  if (m_uiManager.isInventoryOpen()) {
    // The bag is a HOTBAR_SLOTS-wide grid: row 0 is the hotbar, the rows below
    // are the bag proper. Up/down wraps between the hotbar and the BOTTOM row
    // only - stepping down out of the hotbar into row 1 is deliberately a
    // no-op, so the wrap is one unambiguous move rather than two directions
    // that both leave the hotbar.
    constexpr int kCols = HOTBAR_SLOTS;
    constexpr int kLastRowStart = INVENTORY_SLOTS - kCols;

    int hotbar = m_uiManager.getActiveHotbarSlot();
    if (in.navRight && (hotbar % kCols != kCols - 1))
      hotbar++;
    if (in.navLeft && (hotbar % kCols != 0))
      hotbar--;
    if (in.navDown) {
      if (hotbar >= kCols && hotbar < kLastRowStart)
        hotbar += kCols;
      else if (hotbar >= kLastRowStart)
        hotbar -= kLastRowStart;
    }
    if (in.navUp) {
      if (hotbar >= 2 * kCols)
        hotbar -= kCols;
      else if (hotbar < kCols)
        hotbar += kLastRowStart;
    }
    m_uiManager.setActiveHotbarSlot(hotbar);
  } else {
    static_assert(InputState::kHotbarKeys == HOTBAR_SLOTS,
                  "one number key per hotbar slot");
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
      if (in.hotbar[i])
        m_uiManager.setActiveHotbarSlot(i);
    }
  }

  if (in.use) {
    if (m_uiManager.isFullscreenMapOpen()) {
      m_uiManager.closeFullscreenMap();
    } else {
      m_player.consumeItem(m_uiManager.getActiveHotbarSlot());
    }
  }

  if (in.place && !m_uiManager.isInventoryOpen()) {
    if (m_isDroppingItem) {
      m_isDroppingItem = false;
    } else {
      ItemType activeType =
          m_player.getInventory()[m_uiManager.getActiveHotbarSlot()].type;
      if (activeType != ItemType::NONE) {
        if (ItemDatabase::getDef(activeType).isPlaceable) {
          m_isDroppingItem = true;
        } else {
          m_uiManager.showPopup("Item cannot be placed",
                                PopupType::SUBTLE_BOTTOM, 2.0f);
        }
      }
    }
  }

  if (m_isDroppingItem && in.mouseLeftPressed) {
    Vector2 mouseWorld = GetScreenToWorld2D(windowToCanvas(in.mouse), m_camera);
    int gridX = m_maze.toGridX(mouseWorld.x);
    int gridY = m_maze.toGridY(mouseWorld.y);

    if (m_maze.getItem(gridX, gridY) == ItemType::NONE) {
      int cellType = m_maze.getCell(gridX, gridY);
      if (cellType == Maze::CELL_ROOM || cellType == Maze::CELL_CORRIDOR) {
        if (m_maze.isVisible(gridX, gridY)) {
          m_maze.setItem(
              gridX, gridY,
              m_player.getInventory()[m_uiManager.getActiveHotbarSlot()].type);
          m_player.destroyItem(m_uiManager.getActiveHotbarSlot());
          m_isDroppingItem = false;
        } else {
          m_uiManager.showPopup("Cannot place there!", PopupType::SUBTLE_BOTTOM,
                                2.0f);
        }
      }
    }
  }
}

// ============================================================================
// computeTripFollowOffset - Make the book ride the shader's distortion
// ============================================================================
// The book is drawn after EndShaderMode() so it never warps. The table under
// it IS warped, so with no correction the book is the only stationary object
// on a swimming screen - which the eye reads as the book floating in circles.
//
// magic_trip.fs does an INVERSE uv lookup: the pixel at uv displays whatever
// lives at (uv + wave). So scene content at uv appears shifted by -wave. The
// blit uses a negative source height, flipping V, which cancels the sign on
// the vertical axis - hence -waveX but +waveY below.
//
// Evaluating the wave at the content's own uv rather than the screen uv is a
// first-order approximation, exact enough here: peak displacement is ~3% of
// the screen, well inside the range where the two agree.
// ============================================================================
Vector2 MazeState::computeTripFollowOffset() const {
  float strength = m_player.getMushroomEffectStrength();
  if (strength <= 0.0f || !m_maze.isMagicBookSpawned()) {
    return {0.0f, 0.0f};
  }

  int cellSize = m_maze.getCellSize();
  Vector2 bookWorld = {(m_maze.getMagicBookX() + 0.5f) * cellSize,
                       (m_maze.getMagicBookY() + 0.5f) * cellSize};
  Vector2 screen = GetWorldToScreen2D(bookWorld, m_camera);

  // The shader's UVs run over the canvas texture, so normalise by the canvas.
  float w = (float)m_canvas.width;
  float h = (float)m_canvas.height;
  if (w <= 0.0f || h <= 0.0f) {
    return {0.0f, 0.0f};
  }

  float u = screen.x / w;
  float v = 1.0f - (screen.y / h); // V is flipped by the render-texture blit
  float t = m_totalTime;

  // These four terms must mirror assets/magic_trip.fs exactly.
  float waveX = sinf(v * 10.0f + t * 2.5f) * 0.01f * strength +
                sinf(u * 3.0f - t * 1.5f) * 0.02f * strength;
  float waveY = cosf(u * 12.0f + t * 2.0f) * 0.015f * strength +
                cosf(v * 4.0f + t * 1.2f) * 0.02f * strength;

  float follow = m_debugOverlay.getBookTripFollow();
  float zoom = m_camera.zoom != 0.0f ? m_camera.zoom : 1.0f;

  // UV -> screen pixels -> world units (BeginMode2D scales by zoom).
  return {(-waveX * w * follow) / zoom, (waveY * h * follow) / zoom};
}

// ============================================================================
// attemptMagicBookSpawn - Single entry point for placing the magic book
// ============================================================================
// Both the mushroom-trip path and the debug button funnel through here so the
// outcome is reported identically. The status string is the diagnostic that
// tells a blank screen apart from a failed placement.
void MazeState::attemptMagicBookSpawn() {
  int gridX = m_maze.toGridX(m_player.getPosition().x);
  int gridY = m_maze.toGridY(m_player.getPosition().y);

  ItemSpawner::BookSpawnResult result =
      m_itemSpawner.spawnMagicBookOfMaps(m_maze, gridX, gridY);

  if (result == ItemSpawner::BookSpawnResult::SPAWNED) {
    debuglog::log("BOOK", "SPAWNED at (%d, %d), player at (%d, %d)",
                  m_maze.getMagicBookX(), m_maze.getMagicBookY(), gridX, gridY);
    m_debugOverlay.setMagicBookStatus(
        TextFormat("SPAWNED at (%d, %d), player at (%d, %d)",
                   m_maze.getMagicBookX(), m_maze.getMagicBookY(), gridX,
                   gridY));
    // The debug map draws a purple marker at the book's cell - that marker is
    // the bisector: visible on the map but absent on screen means the bug is
    // in the renderer, not the spawner.
    m_debugOverlay.markMapDirty();
    m_uiManager.markMagicBookMapDirty();
  } else {
    debuglog::log("BOOK", "FAILED - no table within 20 tiles of (%d, %d)",
                  gridX, gridY);
    m_debugOverlay.setMagicBookStatus(TextFormat(
        "FAILED: no table within 20 tiles of (%d, %d)", gridX, gridY));
  }
}

void MazeState::render(const InputState &in) {
  if (m_screenTarget.texture.width != m_canvas.width ||
      m_screenTarget.texture.height != m_canvas.height) {
    UnloadRenderTexture(m_screenTarget);
    m_screenTarget = LoadRenderTexture(m_canvas.width, m_canvas.height);
  }

  if (m_renderSettings.flashlightEnabled) {
    m_renderer.buildLightMask(m_player.getPosition(), m_camera, m_canvas,
                              m_player.getAreaState(),
                              m_player.getFacingDirection());
  }

  BeginTextureMode(m_screenTarget);
  ClearBackground(theme::ground);

  BeginMode2D(m_camera);
  m_renderer.render(m_maze, m_camera, m_canvas, m_player.getAreaState(),
                    m_renderSettings.showGenerationZones);
  m_itemRenderer.render(m_maze, m_camera, m_canvas, m_player.getAreaState(),
                        ItemRenderer::Layer::BEHIND_PLAYER);
  m_playerRenderer.render(m_player);
  m_itemRenderer.render(m_maze, m_camera, m_canvas, m_player.getAreaState(),
                        ItemRenderer::Layer::IN_FRONT_OF_PLAYER);
  EndMode2D();

  if (m_renderSettings.flashlightEnabled &&
      m_player.getAreaState() == AreaState::CORRIDOR) {
    m_renderer.drawLightMask(m_canvas);
  }

  if (m_radiationDarknessAlpha > 0.0f) {
    DrawRectangle(0, 0, m_canvas.width, m_canvas.height,
                  Fade(theme::ground, m_radiationDarknessAlpha));
  }
  EndTextureMode();
  if (m_capture) {
    m_capture->onSceneReady(m_screenTarget);
  }

  BeginDrawing();
  ClearBackground(theme::ground);

  float tripStrength = m_player.getMushroomEffectStrength();
  if (tripStrength > 0.0f) {
    SetShaderValue(m_tripShader, m_tripTimeLoc, &m_totalTime,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(m_tripShader, m_tripStrengthLoc, &tripStrength,
                   SHADER_UNIFORM_FLOAT);
    BeginShaderMode(m_tripShader);
  }

  // canvas * blitScale covers the window exactly, or bleeds less than one
  // canvas pixel past an edge that did not divide - see core/viewport.hpp.
  const float scale = m_renderSettings.blitScale;
  Rectangle sourceRec = {0.0f, 0.0f, (float)m_screenTarget.texture.width,
                         -(float)m_screenTarget.texture.height};
  Rectangle destRec = {0.0f, 0.0f, m_canvas.width * scale,
                       m_canvas.height * scale};
  DrawTexturePro(m_screenTarget.texture, sourceRec, destRec,
                 Vector2{0.0f, 0.0f}, 0.0f, WHITE);

  if (tripStrength > 0.0f) {
    EndShaderMode();
  }

  // Overlay layer (exempt from main shader). We are in WINDOW space now, so
  // the scene camera is rescaled: same target, offset and zoom multiplied by
  // the blit scale, which lands world points exactly where the blit put them.
  Camera2D windowCamera = m_camera;
  windowCamera.offset = {m_camera.offset.x * scale, m_camera.offset.y * scale};
  windowCamera.zoom = scale;
  Viewport window{GetScreenWidth(), GetScreenHeight()};
  BeginMode2D(windowCamera);
  m_itemRenderer.renderMagicBookOverlay(m_maze, windowCamera, window,
                                        m_player.getAreaState(),
                                        computeTripFollowOffset(),
                                        m_debugOverlay.getBookGlowScale(),
                                        m_totalTime);
  EndMode2D();

  if (m_player.isPassingOut()) {
    float passOutTimer = m_player.getPassOutTimer();
    float fadeAlpha = 0.0f;
    if (passOutTimer > 5.0f) {
      fadeAlpha = 1.0f - ((passOutTimer - 5.0f) / 5.0f);
    } else {
      fadeAlpha = passOutTimer / 5.0f;
    }

    if (fadeAlpha > 1.0f)
      fadeAlpha = 1.0f;
    if (fadeAlpha < 0.0f)
      fadeAlpha = 0.0f;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  Fade(theme::ground, fadeAlpha));
  }

  // Hand off UI rendering to UIManager, then the debug panel. ImGui must own
  // the last draw of the frame, so the overlay goes after the game UI.
  m_uiManager.render(m_player, m_maze, m_itemRenderer, m_isDroppingItem,
                     m_totalTime, in);
  m_debugOverlay.render(m_player, m_maze, m_renderSettings,
                        m_uiManager.getUIScale());

  if (m_capture) {
    m_capture->onFrameReady();
  }
  EndDrawing();
}

void MazeState::snapshot(Telemetry &out) const {
  const Vector2 pos = m_player.getPosition();
  out.playerWorldPos = pos;
  out.playerCellX = m_maze.toGridX(pos.x);
  out.playerCellY = m_maze.toGridY(pos.y);
  out.areaState =
      m_player.getAreaState() == AreaState::ROOM ? "ROOM" : "CORRIDOR";
  switch (m_player.getFacingDirection()) {
  case FacingDirection::UP: out.facing = "UP"; break;
  case FacingDirection::DOWN: out.facing = "DOWN"; break;
  case FacingDirection::LEFT: out.facing = "LEFT"; break;
  case FacingDirection::RIGHT: out.facing = "RIGHT"; break;
  }
  out.mushroomEffect = m_player.getMushroomEffectStrength();
  out.passingOut = m_player.isPassingOut();

  const Vector2 topLeft = GetScreenToWorld2D({0.0f, 0.0f}, m_camera);
  const Vector2 bottomRight = GetScreenToWorld2D(m_canvas.size(), m_camera);
  out.cameraTarget = m_camera.target;
  out.cameraZoom = m_camera.zoom;
  out.cameraRect = {topLeft.x, topLeft.y, bottomRight.x - topLeft.x,
                    bottomRight.y - topLeft.y};
  out.canvasW = m_canvas.width;
  out.canvasH = m_canvas.height;
  out.blitScale = m_renderSettings.blitScale;

  out.inventoryOpen = m_uiManager.isInventoryOpen();
  out.cupboardOpen = m_uiManager.isCupboardInventoryOpen();
  out.fullscreenMapOpen = m_uiManager.isFullscreenMapOpen();

  out.inventory.clear();
  const auto &bag = m_player.getInventory();
  for (int i = 0; i < (int)bag.size(); ++i) {
    if (bag[i].type != ItemType::NONE) {
      out.inventory.push_back({i, itemTypeId(bag[i].type), bag[i].count});
    }
  }

  // Exactly the cells the canvas shows - no cull margin - filtered by the
  // same rule ItemRenderer uses, so "visible" means "would have been drawn".
  out.visibleItems.clear();
  const int cell = m_maze.getCellSize();
  const int x0 = (int)std::floor(topLeft.x / cell);
  const int x1 = (int)std::ceil(bottomRight.x / cell);
  const int y0 = (int)std::floor(topLeft.y / cell);
  const int y1 = (int)std::ceil(bottomRight.y / cell);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      ItemType item = m_maze.getItem(x, y);
      if (item != ItemType::NONE &&
          isCellRenderable(m_maze, x, y, m_player.getAreaState())) {
        out.visibleItems.push_back({x, y, itemTypeId(item)});
      }
    }
  }

  out.mazeWidth = m_maze.getWidth();
  out.mazeHeight = m_maze.getHeight();
  out.nonWallCount = m_maze.getNonWallCount();
  out.corridorCount = m_maze.getCorridorCount();
  out.regenCount = m_regenCount;
}

// ============================================================================
// buildTicTacToeZones - the eight shifting strips, derived from maze size
// ============================================================================
// Two full-height vertical strips and two horizontal bands, the bands cut into
// three segments by the verticals - a noughts-and-crosses board. These used to
// be eight hardcoded rectangles that were only correct at 250x150.
//
// The strip positions are fractions of each dimension (22%/72% across, 20%/70%
// down), computed with integer arithmetic so they reproduce the original
// hand-tuned numbers exactly at 250x150 rather than drifting by a rounding
// error. The asymmetry is deliberate: it makes the centre band the largest, so
// the middle of the world shifts least.
// ============================================================================
std::vector<Maze::Room> MazeState::buildTicTacToeZones(int width, int height,
                                                          int thickness) {
  const int w = width;
  const int h = height;
  const int t = thickness;

  const int vx[2] = {w * 22 / 100, w * 72 / 100}; // 55, 180 at width 250
  const int hy[2] = {h * 20 / 100, h * 70 / 100}; // 30, 105 at height 150

  // Where each horizontal segment starts, and how wide it is: the gaps the
  // vertical strips leave behind.
  const int segX[3] = {0, vx[0] + t, vx[1] + t};
  const int segW[3] = {vx[0], vx[1] - (vx[0] + t), w - (vx[1] + t)};

  std::vector<Maze::Room> zones;
  zones.push_back({vx[0], 0, t, h});
  zones.push_back({vx[1], 0, t, h});

  for (int band = 0; band < 2; ++band) {
    for (int seg = 0; seg < 3; ++seg) {
      if (segW[seg] > 0) {
        zones.push_back({segX[seg], hy[band], segW[seg], t});
      }
    }
  }
  return zones;
}

void MazeState::regenerateTicTacToeZones() {
  std::vector<Maze::Room> zones =
      buildTicTacToeZones(m_maze.getWidth(), m_maze.getHeight(), m_zoneThickness);

  std::vector<std::map<ItemType, int>> removedPerZone(zones.size());
  for (size_t i = 0; i < zones.size(); ++i) {
    const auto &z = zones[i];
    int startX = std::max(0, z.x - 1);
    int startY = std::max(0, z.y - 1);
    int w = std::min(m_maze.getWidth() - startX, z.width + 2);
    int h = std::min(m_maze.getHeight() - startY, z.height + 2);
    removedPerZone[i] = m_maze.clearItemsInZone(startX, startY, w, h);
  }

  m_maze.clearShiftingZones();
  for (const auto &z : zones) {
    m_maze.addShiftingZone(z.x, z.y, z.width, z.height);
    m_maze.eraseZone(z.x, z.y, z.width, z.height);
  }

  // Golden-ratio stride spreads consecutive counts across the seed space so
  // regen N and N+1 do not share a prefix of their random streams.
  std::mt19937 regenRng(m_seed + 0x9E3779B9u * (unsigned int)(++m_regenCount));
  BSPGenerator bsp;
  PrimsGenerator prims;
  LoopGenerator loops;

  for (const auto &z : zones) {
    bsp.generateZone(m_maze, regenRng, z.x, z.y, z.width, z.height);
  }
  for (const auto &z : zones) {
    prims.generateZone(m_maze, regenRng, z.x, z.y, z.width, z.height);
  }
  for (const auto &z : zones) {
    loops.generateZone(m_maze, regenRng, z.x, z.y, z.width, z.height);
  }

  TunnelBorer borer;
  borer.ensureConnectivity(m_maze);
  prims.pruneSmallAlcoves(m_maze, 5);

  for (size_t i = 0; i < zones.size(); ++i) {
    const auto &z = zones[i];
    int startX = std::max(0, z.x - 1);
    int startY = std::max(0, z.y - 1);
    int w = std::min(m_maze.getWidth() - startX, z.width + 2);
    int h = std::min(m_maze.getHeight() - startY, z.height + 2);
    m_itemSpawner.respawnItems(m_maze, removedPerZone[i], startX, startY, w, h);
  }

  m_maze.calculateRadiationZones();
  m_debugOverlay.markMapDirty();
  m_uiManager.markMagicBookMapDirty();
}
