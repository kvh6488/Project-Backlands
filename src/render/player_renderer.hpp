#pragma once

#include "entities/player.hpp"
#include "render/draw_queue.hpp"
#include <raylib.h>

// ============================================================================
// PlayerRenderer Class
// ============================================================================
// Responsible for drawing the Player to the screen using Raylib.
// This implements the Strategy Pattern by separating the presentation logic 
// from the core data structure (Player).
//
// Animation is driven by a fixed-rate timer (not velocity-dependent).
// Standard practice in 2D top-down pixel games like The Escapists, Stardew
// Valley, and classic Zelda — the walk cycle plays at a constant cadence
// regardless of movement speed.
// ============================================================================
class PlayerRenderer : public Drawer {
public:
  PlayerRenderer();
  ~PlayerRenderer();

  void loadTextures();

  // Advance the animation timer and frame index based on player state.
  // Called once per frame from Application::update().
  void update(float dt, const Player &player);

  // True once per walk frame advanced, then cleared - a step for StepEffects.
  // Every frame, not only the two foot-down ones: the cycle covers ~3 tiles
  // at walking speed, so prints on contact frames alone fall 1.6 tiles apart
  // and read as dots, not a trail. A mailbox like Player's pollEvent*s; the
  // maze has no use for it.
  bool pollFootfall() {
    bool f = m_footfall;
    m_footfall = false;
    return f;
  }

  // Queues the player at the bottom edge of its sprite - its feet - and binds
  // it until the queue is flushed.
  void collect(const Player &player, DrawQueue &queue);

  // Drawer: draws the bound player. Pure read-only — no state mutation.
  void drawQueued(int, int) const override;

private:
  // One cell, centred on the player's position.
  static Rectangle destFor(const Player &player, int frame);

  Texture2D m_playerTexture;
  const Player *m_subject = nullptr; // bound by collect()

  // --- Animation State ---
  int m_currentFrame;    // Current frame index in the walk cycle (0–3)
  float m_frameTimer;    // Accumulator tracking time since last frame advance
  bool m_footfall = false;

  // Walk cycle plays at ~6.7 FPS (0.15s per frame).
  // This is a tunable constant — increase for slower animation, decrease for
  // faster. Standard range for pixel walk cycles is 0.10–0.20s.
  static constexpr float FRAME_DURATION = 0.20f;

  // Spritesheet constants
  static constexpr int FRAME_COUNT = 4;  // 4 frames per walk cycle row
};
