#pragma once

#include "items/item.hpp"
#include <cmath>

class Maze;
class Overworld;

// ============================================================================
// World — what the player, the UI and the item pass need from either world
// ============================================================================
// The maze and the overworld share exactly one thing: both are a fixed W x H
// grid of cells that wraps toroidally. That geometry lives here as plain data,
// so wrapping and world-to-grid conversion stay non-virtual and inline - the
// maze generators call them in their innermost loops.
//
// Everything else is a narrow virtual contract: is this cell solid, and the
// one-item-per-cell layer. Anything only one world has (rooms and corridors,
// cupboards, the magic book; biomes and props) is NOT on it. Code that needs
// those asks for the concrete world through asMaze()/asOverworld(), which
// return null in the other world - so a maze-only rule reads as an explicit
// "if we are in the maze" at the call site, rather than as a method the
// overworld has to stub out.
//
// Pattern: an abstract base class with a downcast hook - the visitor-free
// cousin of dynamic_cast, chosen because the set of worlds is closed (two).
// ============================================================================
class World {
public:
  virtual ~World() = default;

  int getWidth() const { return m_width; }
  int getHeight() const { return m_height; }
  int getCellSize() const { return m_cellSize; }

  // C++ '%' is a remainder, not a modulo: -1 % 250 == -1. Adding the width
  // back before the second '%' makes it a true modulo for any int. Never
  // hand-roll this at a call site - `(x + dx % w + w) % w` binds the % to dx
  // alone and is only accidentally correct while |dx| < w.
  int wrapX(int x) const { return (x % m_width + m_width) % m_width; }
  int wrapY(int y) const { return (y % m_height + m_height) % m_height; }

  // World pixels to grid cells. Deliberately does NOT wrap: accessors wrap at
  // the point of use, so callers comparing raw coordinates (camera bounds,
  // click targets, the player's own tile) see monotonic values across the
  // seam. floor(), not a cast, so negative positions round the right way.
  int toGridX(float worldX) const {
    return static_cast<int>(std::floor(worldX / m_cellSize));
  }
  int toGridY(float worldY) const {
    return static_cast<int>(std::floor(worldY / m_cellSize));
  }

  // Does this cell stop the player outright? Context-dependent solidity (a
  // room's floor seen from its corridor) is a maze rule and stays in Maze.
  virtual bool isSolid(int x, int y) const = 0;

  // --- The item layer: at most one item per cell ---
  virtual ItemType getItem(int x, int y) const = 0;
  virtual void setItem(int x, int y, ItemType type) = 0;
  // 0 = default; meaning is per item (cupboard open, table root tile...).
  virtual int getItemState(int x, int y) const = 0;
  // Nearest cell within maxRadius steps that can take a dropped item.
  virtual bool findNearestEmptyItemCell(int startX, int startY, int maxRadius,
                                        int &outX, int &outY) const = 0;

  virtual Maze *asMaze() { return nullptr; }
  virtual const Maze *asMaze() const { return nullptr; }
  virtual Overworld *asOverworld() { return nullptr; }
  virtual const Overworld *asOverworld() const { return nullptr; }

protected:
  World(int width, int height, int cellSize)
      : m_width(width), m_height(height), m_cellSize(cellSize) {}

  // Not const, so a concrete world stays movable.
  int m_width;    // cells across
  int m_height;   // cells down
  int m_cellSize; // world px per cell; grid::CELL in the shipping game
};
