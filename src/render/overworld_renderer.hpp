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
// bottom to top: water everywhere (swamp water fading over open water), then
// ground on all land (sand, or a mud bank on lakes and rivers), grass on all
// land but beaches, then wetland, gravel, snow and drifts fading over it.
// Grass fades into forest floor through SHADE OVERLAYS: recoloured copies of
// the grass that dither out over a few pixels, placed by the tile's shade
// step (TileSample::shade). The tiled layers sit on the DUAL grid: tiles on the
// cell CORNERS, half a cell off the world grid, and each one shows the four
// cells that meet there. Four in/out bits give 16 shapes (cornerMask) - the
// "corner" or "Wang" tile set - against 47 for the blob sets that tile per
// cell and must look at all eight neighbours. The shape boundary falls on the
// cell boundary, so what you see matches what isSolid and biomeAt say.
// A layer's tiles are transparent outside its shape, so the layer beneath
// shows through and a three-biome corner needs no special tile.
//
// FADES: wetland, gravel, snow and drifts have no tiles. Each is one quad
// over the view through a shader (assets/ow_fade.fs) that draws a pixel
// where the material's share of the nearby cells (shareWithin), read
// bilinear, beats an ordered-dither threshold - so any two of them blend over
// several tiles. Land fades are clipped to the grass's corner shapes at the
// shore and take their own outline colour there. Swamp water goes through
// the same shader but fades like a shade overlay: per dual-grid corner, a
// mask chosen by which cells are open water dithers it out over a few px.
//
// STILL WATER is one repeating texture sampled at world position, like the
// river; lakes and swamps sway it a pixel or two along the diagonal, and a
// few open-water tiles glint.
//
// ROOTS IN WATER: where one of the widest-rooted props (willow, the big
// oak) spills onto a river tile, that slice laps through dry, foam line,
// sunk, foam line. On still water roots draw as they are.
//
// RIVERS FLOW: river water is one repeating texture sampled at the tile's
// world position, shifted downstream by time along the tile's flow step. The
// banks drawn over it stay put, so the water slides past them.
//
// The coast is High Tides' hand-drawn sand set (foam, 3 animation frames),
// which has 14 of the 16 shapes; the two diagonals are drawn as two single
// corners. Inland layers use generated sets with all 16 (see
// tools/build_overworld_sheets.py).
//
// GROUND DETAILS (flowers, tufts, pebbles, ice) are render-only: a tile's
// hash picks one from a per-biome pool, drawn flat with the terrain.
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

  // Per cell of a w x h grid: the share of the cells within `radius`
  // (a square) that are `in`, among those that `count`; 0 where none count.
  // A normalized box filter, O(1) per cell from summed-area tables. Pure.
  static void shareWithin(const std::vector<uint8_t> &in,
                          const std::vector<uint8_t> &count, int w, int h,
                          int radius, std::vector<float> &out);
  // How far (tiles) a fade reaches either side of a border.
  static constexpr int kFadeRadius = 2;

  // The sprite a prop draws as. Pure: same prop, same sprite.
  static owsprite::Id spriteFor(PropType type, Biome biome, uint8_t variant);
  // The ground detail (a DECAL_* sprite) on a tile with this biome, shade
  // and hash, or COUNT for none. Pure, like spriteFor.
  static owsprite::Id decalFor(Biome biome, uint8_t shade, uint32_t hash);

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
  struct Cell {
    Biome biome;
    uint8_t shade;
    uint8_t flow;
    uint8_t bank; // 0 far from a beach; 1, 2 nearer - the paler bank rows
  };

  void drawLayer(int layer, int x0, int y0, int w, int h, int frame) const;
  void buildFades(int x0, int y0, int w, int h);
  void drawFade(int fade, int x0, int y0, int w, int h) const;
  void drawDecals(int x0, int y0, int w, int h) const;
  void drawGlints(int x0, int y0, int w, int h) const;
  // A cached cell, by world (unwrapped) tile.
  const Cell &cellAt(int x, int y) const {
    return m_cells[(y - m_cellsY0) * m_cellsW + (x - m_cellsX0)];
  }

  Texture2D m_water{}, m_river{}, m_swampWater{}, m_glints{}, m_coast{}, m_terrain{},
      m_shades{}, m_fades{}, m_props{}, m_propsWet{};
  Shader m_fade{};
  int m_locWeights = -1, m_locInfo = -1, m_locOrigin = -1, m_locLayer = -1,
      m_locSwampWater = -1, m_locSway = -1;
  // The fade shader's per-cell data, rebuilt per frame: textures, their
  // pixels, the world cell of texel (0, 0), and which fades are in view.
  Texture2D m_weights{}, m_info{};
  std::vector<Color> m_weightPx, m_infoPx;
  int m_fadeX0 = 0, m_fadeY0 = 0;
  bool m_fadeUsed[5] = {};
  const Overworld *m_world = nullptr;
  Vector2 m_focus{};
  float m_time = 0.0f; // renderTerrain's, for the lapping roots, glints and sway
  // The cells around the view, kFadeRadius + 1 past it on every side,
  // rebuilt per frame so the layers' lookups are array reads rather than
  // chunk queries.
  std::vector<Cell> m_cells;
  int m_cellsX0 = 0, m_cellsY0 = 0, m_cellsW = 0, m_cellsH = 0;
};
