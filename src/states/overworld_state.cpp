#include "states/overworld_state.hpp"
#include "core/grid.hpp"
#include "dev/debug_log.hpp"
#include "render/theme.hpp"
#include "render/view_bounds.hpp"
#include <cmath>

OverworldState::OverworldState(Run &run, UIManager &uiManager,
                               DebugOverlay &debugOverlay, CaptureSink *capture,
                               float blitScale,
                               const SpawnOverride *spawnOverride)
    : m_run(run), m_uiManager(uiManager), m_debugOverlay(debugOverlay),
      m_capture(capture), m_world(run.seed), m_player(run.player),
      m_hasSpawnOverride(spawnOverride != nullptr),
      m_spawnOverride(spawnOverride ? *spawnOverride : SpawnOverride{0, 0}) {
  m_renderSettings.blitScale = blitScale;
  debuglog::log("ISLAND", "seed %u: %d tiles across, %zu lakes, spawn (%d, %d)",
                run.seed, m_world.getWidth(),
                m_world.island().map().lakeLevel.size(), m_world.spawnX(),
                m_world.spawnY());
}

void OverworldState::placePlayer() {
  int tx = m_hasSpawnOverride ? m_spawnOverride.x : m_world.spawnX();
  int ty = m_hasSpawnOverride ? m_spawnOverride.y : m_world.spawnY();
  const float cell = (float)grid::CELL;
  // Place, never rebuild: the player belongs to the Run, bag and all.
  m_player.teleport({tx * cell + cell / 2.0f, ty * cell + cell / 2.0f},
                    AreaState::ROOM);
  m_debugOverlay.setSeed(m_run.seed);
  m_debugOverlay.markIslandMapDirty();
}

void OverworldState::regenerate(unsigned int seed) {
  m_run.seed = seed;
  m_world = Overworld(seed);
  m_hasSpawnOverride = false; // a dev start tile means nothing on a new island
  placePlayer();
  debuglog::log("ISLAND", "regenerated: seed %u, spawn (%d, %d)", seed,
                m_world.spawnX(), m_world.spawnY());
  debuglog::log("SEED", "reproduce with:  Backrooms.exe --seed %u --world overworld",
                seed);
}

void OverworldState::onEnter() {
  m_itemRenderer.loadTextures();
  m_playerRenderer.loadTextures();
  m_renderer.loadTextures();
  m_stepEffects.loadTextures();
  placePlayer();
  updateCamera();
  m_screenTarget = LoadRenderTexture(m_canvas.width, m_canvas.height);
}

void OverworldState::onExit() { UnloadRenderTexture(m_screenTarget); }

// Integer target + integer offset + zoom 1.0 = every tile lands on whole
// canvas texels, which the integer blit then preserves (as in MazeState).
void OverworldState::updateCamera() {
  m_canvas = Viewport::canvasFor(GetScreenWidth(), GetScreenHeight(),
                                 m_renderSettings.blitScale);
  m_camera.target = {std::round(m_player.getPosition().x),
                     std::round(m_player.getPosition().y)};
  m_camera.zoom = 1.0f;
  m_camera.offset = {std::floor(m_canvas.width / 2.0f),
                     std::floor(m_canvas.height / 2.0f)};
}

void OverworldState::update(float dt, const InputState &in) {
  if (in.toggleFullscreen) {
    ToggleFullscreen();
  }

  if (m_debugOverlay.triggerNewIsland()) {
    m_debugOverlay.clearNewIsland();
    // Deterministic successor, so a chain of rerolls is reproducible.
    regenerate(noise::hash((int)m_run.seed, 1, 0x1517a4d3u) | 1u);
  }
  if (m_debugOverlay.triggerRemoveProp()) {
    m_debugOverlay.clearRemoveProp();
    // Nearest prop in a small square around the player, through the same
    // change record a felling tool will use.
    int px = m_world.toGridX(m_player.getPosition().x);
    int py = m_world.toGridY(m_player.getPosition().y);
    for (int r = 0; r <= 3; ++r) {
      bool done = false;
      for (int dy = -r; dy <= r && !done; ++dy)
        for (int dx = -r; dx <= r && !done; ++dx)
          if (std::max(std::abs(dx), std::abs(dy)) == r)
            done = m_world.removeProp(px + dx, py + dy);
      if (done) {
        m_debugOverlay.markIslandMapDirty();
        break;
      }
    }
  }

  // Minimap click; a tree or rock tile snaps to the nearest open one.
  if (m_debugOverlay.triggerTeleport()) {
    m_debugOverlay.clearTeleport();
    int tx = m_debugOverlay.teleportX(), ty = m_debugOverlay.teleportY();
    if (m_world.nearestOpenCell(tx, ty, 8)) {
      const float cell = (float)grid::CELL;
      m_player.teleport({tx * cell + cell / 2.0f, ty * cell + cell / 2.0f},
                        AreaState::ROOM);
      debuglog::log("DEV", "teleported to (%d, %d) %s", tx, ty,
                    biomeId(m_world.biomeAt(tx, ty)));
    }
  }

  handleInput(in);
  m_player.update(m_world, dt, in,
                  !m_uiManager.isInventoryOpen() &&
                      !m_uiManager.isFullscreenMapOpen());
  m_playerRenderer.update(dt, m_player);
  m_stepEffects.update(m_world, m_player, m_playerRenderer.pollFootfall(), dt);
  updateCamera();

  // Chunks follow the camera; everything further out is regenerated on
  // demand if the player comes back.
  m_world.retainAround(m_world.toGridX(m_player.getPosition().x),
                       m_world.toGridY(m_player.getPosition().y),
                       kRetainChunks);

  // Player events (trip stages, maps) are maze-side: they stay raised until
  // MazeState drains them.
  m_totalTime += dt;
  m_uiManager.update(dt);
}

void OverworldState::handleInput(const InputState &in) {
  if (in.toggleInventory) {
    m_uiManager.toggleInventory();
    if (!m_uiManager.isInventoryOpen()) {
      m_uiManager.setHeldSlotIndex(-1);
      m_uiManager.setActiveHotbarSlot(m_uiManager.getActiveHotbarSlot() %
                                      HOTBAR_SLOTS);
    }
  }
  m_uiManager.handleInventoryInput(m_player, m_world, in);
  m_uiManager.handleSlotNavigation(in);

  if (in.use) {
    if (m_uiManager.isFullscreenMapOpen()) {
      m_uiManager.closeFullscreenMap();
    } else {
      m_player.consumeItem(m_uiManager.getActiveHotbarSlot());
    }
  }
  if (in.place && !m_uiManager.isInventoryOpen()) {
    m_uiManager.showPopup("Nothing can be placed out here yet",
                          PopupType::SUBTLE_BOTTOM, 2.0f);
  }
}

void OverworldState::render(const InputState &in) {
  if (m_screenTarget.texture.width != m_canvas.width ||
      m_screenTarget.texture.height != m_canvas.height) {
    UnloadRenderTexture(m_screenTarget);
    m_screenTarget = LoadRenderTexture(m_canvas.width, m_canvas.height);
  }

  BeginTextureMode(m_screenTarget);
  ClearBackground(theme::ocean);
  BeginMode2D(m_camera);
  m_renderer.renderTerrain(m_world, m_camera, m_canvas, m_totalTime);
  m_stepEffects.drawFlat();

  ViewBounds view = ViewBounds::fromCamera(m_world, m_camera, m_canvas);
  // Props rooted below the screen still reach up into it.
  m_drawQueue.begin(view.minBaseY(grid::CELL),
                    view.maxBaseY(grid::CELL) +
                        OverworldRenderer::kReachBelowTiles * grid::CELL);
  m_playerRenderer.collect(m_player, m_drawQueue);
  m_stepEffects.collect(m_drawQueue);
  m_itemRenderer.collect(m_world, m_camera, m_canvas, AreaState::ROOM,
                         m_drawQueue);
  m_renderer.setFocus(m_player.getPosition());
  m_renderer.collect(m_world, m_camera, m_canvas, m_drawQueue);
  m_drawQueue.flush();
  EndMode2D();
  EndTextureMode();
  if (m_capture) {
    m_capture->onSceneReady(m_screenTarget);
  }

  BeginDrawing();
  ClearBackground(theme::ground);
  const float scale = m_renderSettings.blitScale;
  Rectangle src = {0.0f, 0.0f, (float)m_screenTarget.texture.width,
                   -(float)m_screenTarget.texture.height};
  Rectangle dest = {0.0f, 0.0f, m_canvas.width * scale, m_canvas.height * scale};
  DrawTexturePro(m_screenTarget.texture, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);

  m_uiManager.render(m_player, m_world, m_itemRenderer, false, m_totalTime, in);
  m_debugOverlay.render(m_player, m_world, m_renderSettings,
                        m_uiManager.getUIScale());

  if (m_capture) {
    m_capture->onFrameReady();
  }
  EndDrawing();
}

void OverworldState::snapshot(Telemetry &out) const {
  out.world = "overworld";
  const Vector2 pos = m_player.getPosition();
  out.playerWorldPos = pos;
  out.playerCellX = m_world.toGridX(pos.x);
  out.playerCellY = m_world.toGridY(pos.y);
  out.areaState = "SURFACE";
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
  out.cupboardOpen = false;
  out.fullscreenMapOpen = m_uiManager.isFullscreenMapOpen();
  out.inventory.clear();
  const auto &bag = m_player.getInventory();
  for (int i = 0; i < (int)bag.size(); ++i) {
    if (bag[i].type != ItemType::NONE) {
      out.inventory.push_back({i, itemTypeId(bag[i].type), bag[i].count});
    }
  }

  // Items in the canvas rect; on the surface nothing hides them.
  out.visibleItems.clear();
  const int cell = m_world.getCellSize();
  const int x0 = (int)std::floor(topLeft.x / cell);
  const int x1 = (int)std::ceil(bottomRight.x / cell);
  const int y0 = (int)std::floor(topLeft.y / cell);
  const int y1 = (int)std::ceil(bottomRight.y / cell);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      if (m_world.getItem(x, y) != ItemType::NONE)
        out.visibleItems.push_back({x, y, itemTypeId(m_world.getItem(x, y))});

  out.worldSize = m_world.getWidth();
  out.wrappedCellX = m_world.wrapX(out.playerCellX);
  out.wrappedCellY = m_world.wrapY(out.playerCellY);
  out.chunkX = Overworld::chunkOf(out.wrappedCellX);
  out.chunkY = Overworld::chunkOf(out.wrappedCellY);
  out.height = m_world.heightAt(out.playerCellX, out.playerCellY);
  out.biome = biomeId(m_world.biomeAt(out.playerCellX, out.playerCellY));
  out.nearbyProps.clear();
  const int r = Telemetry::kPropRadius;
  for (int y = out.playerCellY - r; y <= out.playerCellY + r; ++y)
    for (int x = out.playerCellX - r; x <= out.playerCellX + r; ++x)
      if (m_world.propAt(x, y) != PropType::NONE)
        out.nearbyProps.push_back({x, y, propId(m_world.propAt(x, y))});
  out.cachedChunks = m_world.cachedChunkCount();
  out.spawnX = m_world.spawnX();
  out.spawnY = m_world.spawnY();
}
