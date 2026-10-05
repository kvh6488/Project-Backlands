#include "render/overworld_renderer.hpp"
#include "core/grid.hpp"
#include "render/theme.hpp"
#include "render/view_bounds.hpp"

Color OverworldRenderer::biomeColour(Biome b) {
  switch (b) {
  case Biome::OCEAN: return theme::ocean;
  case Biome::LAKE: return theme::lake;
  case Biome::RIVER: return theme::river;
  case Biome::BEACH: return theme::beach;
  case Biome::GRASSLAND: return theme::grassland;
  case Biome::FOREST: return theme::forest;
  case Biome::WETLAND: return theme::wetland;
  case Biome::MOUNTAIN: return theme::mountain;
  case Biome::SNOW: return theme::snow;
  default: return theme::ground;
  }
}

void OverworldRenderer::renderTerrain(const Overworld &world,
                                      const Camera2D &camera,
                                      const Viewport &canvas) const {
  ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);
  for (int y = view.startY; y <= view.endY; ++y)
    for (int x = view.startX; x <= view.endX; ++x)
      DrawRectangleRec(grid::cellRect(x, y), biomeColour(world.biomeAt(x, y)));
}

void OverworldRenderer::collect(const Overworld &world, const Camera2D &camera,
                                const Viewport &canvas, DrawQueue &queue) {
  m_world = &world;
  // ViewBounds already reaches 3 cells above the screen for tall sprites.
  ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);
  for (int y = view.startY; y <= view.endY; ++y)
    for (int x = view.startX; x <= view.endX; ++x)
      if (world.propAt(x, y) != PropType::NONE)
        queue.push((y + 1) * grid::CELL, *this, x, y);
}

// Shapes are placeholders; sizes are whole art pixels (multiples of
// WORLD_SCALE) so they sit on the same pixel grid as the real sprites will.
void OverworldRenderer::drawQueued(int x, int y) const {
  const Prop *prop = m_world->findProp(x, y);
  if (!prop)
    return;
  const float px = 2.0f; // one art pixel, in canvas px
  const Rectangle cell = grid::cellRect(x, y);
  const float cx = cell.x + cell.width / 2.0f;
  const float base = cell.y + cell.height;
  const float v = (float)prop->variant;

  switch (prop->type) {
  case PropType::TREE: {
    // Trunk on its own tile, canopy two cells tall above it.
    DrawRectangleRec({cx - 2 * px, base - 8 * px, 4 * px, 8 * px}, theme::trunk);
    float r = (11 + v) * px;
    DrawCircleV({cx, base - 8 * px - r * 0.8f}, r, theme::canopy);
    break;
  }
  case PropType::PINE: {
    DrawRectangleRec({cx - 1 * px, base - 5 * px, 2 * px, 5 * px}, theme::trunk);
    float h = (24 + 2 * v) * px, w = 9 * px;
    DrawTriangle({cx, base - 4 * px - h}, {cx - w, base - 4 * px},
                 {cx + w, base - 4 * px}, theme::pine);
    break;
  }
  case PropType::ROCK:
    DrawEllipse((int)cx, (int)(base - 5 * px), (6 + v) * px, 5 * px, theme::rock);
    break;
  case PropType::BUSH:
    DrawCircleV({cx, base - 5 * px}, (5 + v * 0.5f) * px, theme::bush);
    break;
  case PropType::REEDS:
    for (int i = -2; i <= 2; ++i)
      DrawRectangleRec({cx + i * 2 * px, base - (7 + (i & 1) * 3) * px, px,
                        (7 + (i & 1) * 3) * px},
                       theme::reeds);
    break;
  default:
    break;
  }
}
