#pragma once

#include "raylib.h"

// ============================================================================
// InputState — one tick's worth of player intent, as a plain value
// ============================================================================
// Nothing below core/ reads the keyboard or mouse. Once per tick the frame
// loop asks an InputSource for an InputState and hands that same value to
// update() and render(). Gameplay code reads named actions ("pickup"), never
// keys ("KEY_P"), so a rebinding is a one-line change here and a test can
// drive Player::update by filling in the struct by hand.
//
// Two kinds of field, mirroring raylib's IsKeyDown / IsKeyPressed:
//   held    - true for every tick the key is down (movement)
//   pressed - true only on the tick the key went down (everything else)
//
// Same shape as RenderSettings: a POD, owned by one place, passed by
// const reference. Header-only so BACKLANDS_GAME_SOURCES needs no edit.
// ============================================================================
struct InputState {
  static constexpr int kHotbarKeys = 5; // KEY_ONE .. KEY_FIVE

  // --- held ---
  bool moveUp = false;
  bool moveDown = false;
  bool moveLeft = false;
  bool moveRight = false;

  // --- pressed ---
  bool door1 = false;           // K
  bool door2 = false;           // L
  bool pickup = false;          // P
  bool toggleInventory = false; // I
  bool openCupboard = false;    // O
  bool use = false;             // U
  bool place = false;           // Q
  bool toggleFullscreen = false; // F11
  bool hotbar[kHotbarKeys] = {};
  // Arrow keys as edges, for inventory navigation. The same keys also feed
  // moveUp/moveDown/... as levels; which one a system reads is its gate.
  bool navUp = false;
  bool navDown = false;
  bool navLeft = false;
  bool navRight = false;

  // --- mouse, in window pixels ---
  Vector2 mouse = {0.0f, 0.0f};
  bool mouseLeftPressed = false;
  bool mouseRightPressed = false;
};

// The only place in the shipping build that touches raylib's input API.
inline InputState pollHardwareInput() {
  InputState in;
  in.moveUp = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
  in.moveDown = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
  in.moveLeft = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
  in.moveRight = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);

  in.door1 = IsKeyPressed(KEY_K);
  in.door2 = IsKeyPressed(KEY_L);
  in.pickup = IsKeyPressed(KEY_P);
  in.toggleInventory = IsKeyPressed(KEY_I);
  in.openCupboard = IsKeyPressed(KEY_O);
  in.use = IsKeyPressed(KEY_U);
  in.place = IsKeyPressed(KEY_Q);
  in.toggleFullscreen = IsKeyPressed(KEY_F11);
  for (int i = 0; i < InputState::kHotbarKeys; ++i) {
    in.hotbar[i] = IsKeyPressed(KEY_ONE + i); // KEY_ONE..KEY_NINE are contiguous
  }
  in.navUp = IsKeyPressed(KEY_UP);
  in.navDown = IsKeyPressed(KEY_DOWN);
  in.navLeft = IsKeyPressed(KEY_LEFT);
  in.navRight = IsKeyPressed(KEY_RIGHT);

  in.mouse = GetMousePosition();
  in.mouseLeftPressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
  in.mouseRightPressed = IsMouseButtonPressed(MOUSE_RIGHT_BUTTON);
  return in;
}

// Where a tick's InputState comes from. The frame loop owns one of these and
// does not care whether it is the keyboard or a scripted scenario file.
class InputSource {
public:
  virtual ~InputSource() = default;
  virtual InputState sample(int tick) = 0;
  // True once the source has nothing left to say; the frame loop stops. The
  // keyboard never runs out, a scripted scenario does.
  virtual bool finished(int /*tick*/) const { return false; }
};

class HardwareInput : public InputSource {
public:
  InputState sample(int /*tick*/) override { return pollHardwareInput(); }
};
