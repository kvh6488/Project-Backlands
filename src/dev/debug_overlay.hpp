#pragma once

#include "core/render_settings.hpp"
#include "entities/player.hpp"
#include "raylib.h"
#include "world/maze.hpp"
#include <string>

// ============================================================================
// DebugOverlay — Development tooling, kept out of the shipping UI
// ============================================================================
// A single ImGui window of development instruments: maze statistics, a god-view
// minimap, lighting and camera tuning, and forced triggers for systems that are
// otherwise hard to reach (the mushroom trip, the magic book's spawn lottery).
//
// Two rules keep it from leaking into the game:
//
//   1. It is a *view*. Settings the game needs regardless live in
//      RenderSettings, which MazeState owns; the overlay edits them by
//      reference. Only debug-only state (the minimap texture, the forcing
//      flags, the status strings) is owned here.
//   2. Its buttons call the same public entry points the real systems will use.
//      "Regenerate Tic-Tac-Toe Zones" raises the same request flag the eventual
//      in-game trigger will, so the button keeps exercising the shipping path.
//
// Gating is runtime only: --dev arms it, F1 shows and hides it. See
// dev/dev_mode.hpp. The code still ships inside the binary; a release build
// should also gate this file at compile time.
// ============================================================================

class DebugOverlay {
public:
  // devToolsAvailable comes from the --dev command line flag. When false the
  // panel can never be opened and none of its work is done.
  explicit DebugOverlay(bool devToolsAvailable = false);
  ~DebugOverlay();

  // Toggling is a no-op unless --dev armed the tools.
  void toggle() {
    if (m_available)
      m_visible = !m_visible;
  }
  bool isVisible() const { return m_visible; }

  // Draws the panel. Call last, after all game UI — ImGui must own the final
  // draw of the frame. Regenerates the minimap first when dirty, so it is safe
  // to call inside BeginDrawing but not inside a BeginTextureMode block.
  void render(Player &player, Maze &maze, RenderSettings &settings,
              float scale);

  // The minimap caches the whole maze as a texture. Anything that changes
  // layout must mark it dirty.
  void markMapDirty() { m_mapDirty = true; }

  // --- Request flags (polled and cleared by MazeState) ---
  // The overlay never touches the world; it only raises requests, matching the
  // mailbox convention UIManager uses.
  bool triggerTicTacToeRegen() const { return m_triggerTicTacToeRegen; }
  void clearTicTacToeRegen() { m_triggerTicTacToeRegen = false; }

  bool triggerMagicBookSpawn() const { return m_triggerMagicBookSpawn; }
  void clearMagicBookSpawn() { m_triggerMagicBookSpawn = false; }

  bool triggerForceTrip() const { return m_triggerForceTrip; }
  void clearForceTrip() { m_triggerForceTrip = false; }
  bool triggerEndTrip() const { return m_triggerEndTrip; }
  void clearEndTrip() { m_triggerEndTrip = false; }

  // --- Magic book inspection ---
  // Pinning suppresses the trip-decay despawn so the book stays put for as
  // long as it takes to look at it.
  bool isMagicBookPinned() const { return m_pinMagicBook; }
  // 1.0 glues the book to the table's distorted position; 0.0 leaves it still
  // while the scene swims around it.
  float getBookTripFollow() const { return m_bookTripFollow; }
  float getBookGlowScale() const { return m_bookGlowScale; }

  // Last spawn outcome, surfaced in the panel. This is what separates
  // "never spawned" from "spawned but not drawn".
  void setMagicBookStatus(const std::string &status) {
    m_magicBookStatus = status;
  }

  // Current world seed, shown for easy copying into --seed.
  void setSeed(unsigned int seed) { m_seed = seed; }

private:
  // --- Panel sections ---
  void drawWorldSection(Maze &maze);
  void drawViewSection(RenderSettings &settings);
  void drawLightingSection(RenderSettings &settings);
  void drawGenerationSection(RenderSettings &settings);
  void drawTripSection(Player &player);
  void drawMagicBookSection(Maze &maze);
  void drawMinimapSection(Player &player, Maze &maze);

  // Redraws the cached god-view minimap, one pixel per cell.
  void generateMap(Maze &maze, const RenderSettings &settings);

  // Scoped ImGui theme, so the panel's look does not bleed into any other
  // ImGui window added later.
  void pushTheme();
  void popTheme();

  bool m_available = false;
  bool m_visible = false;

  // Minimap cache
  RenderTexture2D m_mapTexture;
  bool m_mapDirty = true;
  bool m_showRadiation = true;

  // Request flags
  bool m_triggerTicTacToeRegen = false;
  bool m_triggerMagicBookSpawn = false;
  bool m_triggerForceTrip = false;
  bool m_triggerEndTrip = false;

  // Magic book inspection state
  bool m_pinMagicBook = false;
  float m_bookTripFollow = 0.8f;
  float m_bookGlowScale = 1.0f;
  std::string m_magicBookStatus = "(no attempt yet)";

  unsigned int m_seed = 0;
};
