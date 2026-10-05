#include "render/player_renderer.hpp"
#include "core/asset_load.hpp"
#include "core/grid.hpp"
#include <cmath>
#include <iostream>

PlayerRenderer::PlayerRenderer()
    : m_playerTexture({0}), m_currentFrame(0), m_frameTimer(0.0f) {}

void PlayerRenderer::loadTextures() {
  if (IsWindowReady()) {
    m_playerTexture = assets::loadTexture(
        "assets/inspector_spritesheet.png", "PlayerRenderer");
  } else {
    std::cerr << "[ERROR] Window not ready. Cannot load textures!" << std::endl;
  }
}

PlayerRenderer::~PlayerRenderer() {
  if (IsWindowReady()) {
    UnloadTexture(m_playerTexture);
  }
}

// ============================================================================
// Update — Animation Timer
// ============================================================================
// Advances the walk cycle frame based on a fixed-rate timer.
// This is the standard approach in 2D top-down pixel games: the animation
// plays at a constant cadence (~6.7 FPS) regardless of player velocity.
//
// When the player stops moving, we snap back to frame 0 (idle pose) and
// reset the timer. The facing direction is preserved so the idle sprite
// faces the direction the player was last walking.
// ============================================================================
void PlayerRenderer::update(float dt, const Player &player) {
  if (player.isMoving()) {
    // Accumulate time and advance frames when the budget is exceeded.
    // Using subtraction (not reset to 0) preserves fractional overflow,
    // keeping the animation smooth even if dt varies between frames.
    m_frameTimer += dt;
    while (m_frameTimer >= FRAME_DURATION) {
      m_currentFrame = (m_currentFrame + 1) % FRAME_COUNT;
      m_frameTimer -= FRAME_DURATION;
    }
  } else {
    // Player stopped — snap to idle pose (frame 0), reset timer.
    m_currentFrame = 0;
    m_frameTimer = 0.0f;
  }
}

// ============================================================================
// Render — Draw Player Sprite
// ============================================================================
// Pure read-only: reads the current animation frame and facing direction
// from state, computes the source rectangle into the spritesheet, and draws.
//
// The FacingDirection enum values (0–3) map directly to spritesheet rows,
// so the enum value is the tile row.
// ============================================================================
Rectangle PlayerRenderer::destFor(const Player &player, int frame) {
  // Source tile: column = animation frame (0-3), row = facing direction (0-3).
  int row = static_cast<int>(player.getFacingDirection());
  return grid::destFor(grid::srcTile(frame, row), player.getPosition().x,
                       player.getPosition().y);
}

void PlayerRenderer::collect(const Player &player, DrawQueue &queue) {
  m_subject = &player;
  // The sprite is drawn centred on the position, so its feet are half a
  // sprite below it.
  Rectangle dest = destFor(player, m_currentFrame);
  queue.push((int)std::floor(player.getPosition().y + dest.height / 2.0f),
             *this);
}

void PlayerRenderer::drawQueued(int, int) const {
  const Player &player = *m_subject;
  int row = static_cast<int>(player.getFacingDirection());
  Rectangle source = grid::srcTile(m_currentFrame, row);
  Rectangle dest = destFor(player, m_currentFrame);
  Vector2 origin = {dest.width / 2.0f, dest.height / 2.0f};

  DrawTexturePro(m_playerTexture, source, dest, origin, 0.0f, WHITE);
}
