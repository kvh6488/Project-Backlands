#pragma once

#include "core/viewport.hpp"
#include "world/maze.hpp"
#include "world/world.hpp"
#include <cmath>
#include <raylib.h>

// ============================================================================
// ViewBounds — the shared frustum cull for every world-space pass
// ============================================================================
// Every renderer that walks the grid needs the same answer: which range of
// cells can reach the screen this frame. MazeRenderer, ItemRenderer::render
// and ItemRenderer::renderMagicBookOverlay each derived it independently, with
// the same unexplained margins copied three times. A mob pass would have been
// the fourth.
//
// WHY THE MARGINS ARE NOT SYMMETRIC:
// Rooms draw a Zelda-style wall face that projects TWO cells upward from the
// floor tile below it (see MazeRenderer's belowIsVisibleFloor branch), so a
// wall whose base sits just above the top of the screen still paints into
// view. startY therefore reaches 3 cells further out than the other three
// edges, which only need a 1-cell cushion against rounding.
//
// Time complexity: O(1) to build, O(1) per containment test.
// ============================================================================
struct ViewBounds {
  int startX, endX, startY, endY;

  // `view` is the rectangle `camera` projects into, in the camera's own
  // pixels - the canvas for the scene passes, the window for overlays drawn
  // after the blit. It is never GetScreenWidth(): that is the window, and
  // the scene camera does not see the window.
  static ViewBounds fromCamera(const World &world, const Camera2D &camera,
                               const Viewport &view) {
    Vector2 topLeft = GetScreenToWorld2D({0.0f, 0.0f}, camera);
    Vector2 bottomRight = GetScreenToWorld2D(view.size(), camera);

    int cellSize = world.getCellSize();
    return ViewBounds{(int)std::floor(topLeft.x / cellSize) - 1,
                      (int)std::ceil(bottomRight.x / cellSize) + 1,
                      (int)std::floor(topLeft.y / cellSize) - 3,
                      (int)std::ceil(bottomRight.y / cellSize) + 1};
  }

  bool contains(int x, int y) const {
    return x >= startX && x <= endX && y >= startY && y <= endY;
  }

  // The base-Y rows (world px) a sprite standing in these cells can have:
  // the bottom edge of the top row through the bottom edge of the last.
  int minBaseY(int cellSize) const { return (startY + 1) * cellSize; }
  int maxBaseY(int cellSize) const { return (endY + 1) * cellSize; }
};

// ============================================================================
// isCellRenderable — should this cell's CONTENTS be drawn?
// ============================================================================
// The rule for anything sitting on the floor (items today, mobs later). It is
// deliberately not the rule for terrain: MazeRenderer has its own richer logic
// for doors and room/corridor boundaries, because walls have to keep drawing
// when their cell is out of context.
//
//   ROOM     — respect the BFS field of view; you see your room and no other.
//   CORRIDOR — draw corridor contents and let the screen-space light mask hide
//              them. Room contents stay hidden: the player is outside looking
//              along a corridor, and a room's interior is not visible from it.
// ============================================================================
inline bool isCellRenderable(const Maze &maze, int x, int y, AreaState state) {
  int cell = maze.getCell(x, y);
  if (cell != Maze::CELL_ROOM && cell != Maze::CELL_CORRIDOR) {
    return false; // walls hold no items
  }

  if (state == AreaState::ROOM) {
    return maze.isVisible(x, y);
  }
  return cell != Maze::CELL_ROOM;
}
