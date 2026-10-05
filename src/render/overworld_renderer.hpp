#pragma once

#include "core/viewport.hpp"
#include "render/draw_queue.hpp"
#include "world/overworld.hpp"
#include <raylib.h>

// ============================================================================
// OverworldRenderer — the surface: flat biome ground plus standing props
// ============================================================================
// Two passes, like the maze. Terrain is flat, so it is drawn first in one
// sweep over the visible tiles; props stand up, so they go into the shared
// DrawQueue and interleave with the player by base Y.
//
// PLACEHOLDER ART. Tiles are flat biome colours and props are simple shapes
// until the surface sheets are chosen and quantized; the draw sites already
// take the cell and the variant, so the art pass only swaps what is drawn.
//
// Wrap-aware culling: the camera works in unwrapped world pixels (the player
// can walk past x = size forever), and every Overworld query wraps, so tiles
// on the far side of the seam draw where the camera expects them with no
// special case here.
// ============================================================================
class OverworldRenderer : public Drawer {
public:
  // The flat fill for a biome; also what the debug island map paints.
  static Color biomeColour(Biome b);

  void renderTerrain(const Overworld &world, const Camera2D &camera,
                     const Viewport &canvas) const;

  // Queues every prop the camera can see, rooted on its tile. Binds `world`
  // until the queue is flushed.
  void collect(const Overworld &world, const Camera2D &camera,
               const Viewport &canvas, DrawQueue &queue);

  // Drawer: the prop on tile (x, y), unwrapped coordinates.
  void drawQueued(int x, int y) const override;

private:
  const Overworld *m_world = nullptr;
};
