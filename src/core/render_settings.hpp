#pragma once

// ============================================================================
// RenderSettings — Tunable presentation values
// ============================================================================
// These are read every frame by MazeState and MazeRenderer, so they must
// exist whether or not the debug overlay does. DebugOverlay edits them through
// a reference; it does not own them. That keeps the overlay a pure view and
// makes it removable without taking rendering config with it.
//
// The defaults are the shipping values — the ones the game uses when nobody
// touches a slider.
// ============================================================================

struct RenderSettings {
  // --- Flashlight / corridor light mask ---
  bool flashlightEnabled = true;
  float lightConeAngle = 235.0f;   // degrees of the visible cone
  float lightFadeStrength = 1.5f;  // falloff sharpness at the cone edges
  float lightSizeScale = 3.5f;     // cone radius, in tiles

  // Raised when any of the three light values above changes. MazeState
  // polls and clears it, then rebuilds the mask texture — regenerating it
  // every frame would be wasteful.
  bool lightSettingsChanged = false;

  // --- Camera ---
  // Canvas-to-window blit factor. See core/viewport.hpp for the model.
  //
  // The invariant is not "integer blit" but "integer ART pixel": every sheet
  // is 16px art drawn at 2x onto the canvas, so one art pixel covers
  // 2 * blitScale window pixels, and THAT product must be whole or the art
  // shimmers. 1.5 therefore is fine (an art pixel is 3x3 on screen) while
  // the old 1.2 was not (2.4 - some pixels fat, some thin).
  //
  // Allowed values, with tiles across a 1280-wide window:
  //   1.0 -> art 2x, 40 tiles    1.5 -> art 3x, 27 tiles (default)
  //   2.0 -> art 4x, 20 tiles    3.0 -> art 6x, 13 tiles
  float blitScale = 1.5f;
  static constexpr float kBlitScales[] = {1.0f, 1.5f, 2.0f, 3.0f};
  static constexpr int kBlitScaleCount = 4;

  // --- Debug visualisation ---
  // Tints the Tic-Tac-Toe regeneration zones in the world and on the minimap.
  bool showGenerationZones = false;
};
