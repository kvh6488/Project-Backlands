#pragma once

#include "core/viewport.hpp"
#include "render/draw_queue.hpp"
#include "render/overworld_sprites.hpp"
#include "world/overworld.hpp"
#include <raylib.h>
#include <vector>

// ============================================================================
// OverworldRenderer — the surface: autotiled ground plus standing props
// ============================================================================
// Two passes, like the maze. Terrain is flat, so it is drawn first in one
// sweep; props stand up, so they go into the shared DrawQueue and interleave
// with the player by base Y.
//
// TERRAIN: LAYERS ON A DUAL GRID. The ground is a stack of materials, drawn
// bottom to top: water everywhere, then sand on all land, grass on all land
// but beaches, then one overlay per biome (forest floor, wetland, gravel,
// snow). Each land layer is autotiled on the DUAL grid: its tiles sit on the
// cell CORNERS, half a cell off the world grid, and each one shows the four
// cells that meet there. Four in/out bits give 16 shapes (cornerMask) - the
// "corner" or "Wang" tile set - against 47 for the blob sets that tile per
// cell and must look at all eight neighbours. The shape boundary falls on the
// cell boundary, so what you see matches what isSolid and biomeAt say.
// A layer's tiles are transparent outside its shape, so the layer beneath
// shows through and a three-biome corner needs no special tile.
//
// The coast is High Tides' hand-drawn sand set (foam, 3 animation frames),
// which has 14 of the 16 shapes; the two diagonals are drawn as two single
// corners. Inland layers use generated sets with all 16 (see
// tools/build_overworld_sheets.py).
//
// PROPS: the world says what grows on a tile (TREE, PINE, ...) and a hash
// byte; this renderer decides which picture. A per-(kind, biome) weighted
// pool turns the byte into a sprite, so a TREE is a willow in the wetland and
// a palm on the beach. Species moves into the world when gameplay needs to
// tell trees apart (wood types).
//
// Wrap-aware: the camera works in unwrapped world pixels and every Overworld
// query wraps, so the seam needs no special case here.
// ============================================================================
class OverworldRenderer : public Drawer {
public:
  OverworldRenderer() = default;
  ~OverworldRenderer();
  OverworldRenderer(const OverworldRenderer &) = delete;
  OverworldRenderer &operator=(const OverworldRenderer &) = delete;

  void loadTextures();

  // The flat fill for a biome; what the debug island map paints.
  static Color biomeColour(Biome b);

  // The 4-bit shape of a dual-grid tile: TL=8, TR=4, BL=2, BR=1, each set
  // when that corner's cell is inside the layer.
  static int cornerMask(bool tl, bool tr, bool bl, bool br) {
    return (tl ? 8 : 0) | (tr ? 4 : 0) | (bl ? 2 : 0) | (br ? 1 : 0);
  }

  // The sprite a prop draws as. Pure: same prop, same sprite.
  static owsprite::Id spriteFor(PropType type, Biome biome, uint8_t variant);

  // Props are rooted on their base tile and grow up, so a tall tree rooted
  // below the screen can still reach into view: collect() scans this many
  // rows past the bottom, and the DrawQueue's range must include them.
  static constexpr int kReachBelowTiles = owsprite::kTallestTiles;

  // `time` drives the coast's foam animation (seconds, fixed-step).
  void renderTerrain(const Overworld &world, const Camera2D &camera,
                     const Viewport &canvas, float time);

  // Queues every prop the camera can see, rooted on its tile. Binds `world`
  // until the queue is flushed.
  void collect(const Overworld &world, const Camera2D &camera,
               const Viewport &canvas, DrawQueue &queue);

  // Drawer: the prop on tile (x, y), unwrapped coordinates.
  void drawQueued(int x, int y) const override;

  // The player's world position. A prop that stands in front of it and
  // covers it draws faded, so a canopy never hides the player (canopies
  // here are up to 5 cells wide).
  void setFocus(Vector2 worldPos) { m_focus = worldPos; }

private:
  void drawLayer(int layer, int x0, int y0, int w, int h, int frame) const;

  Texture2D m_water{}, m_coast{}, m_terrain{}, m_props{};
  const Overworld *m_world = nullptr;
  Vector2 m_focus{};
  // Biomes of the cells around the view, (w+2) x (h+2), rebuilt per frame so
  // each layer's corner lookups are array reads rather than chunk queries.
  std::vector<Biome> m_cells;
  int m_cellsW = 0;
};
