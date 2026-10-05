#pragma once
#include <map>
#include <raylib.h>
#include <vector>
#include <array>

enum class AreaState { CORRIDOR, ROOM };

#include "core/grid.hpp"
#include "items/item.hpp"
#include "world/world.hpp"


// Forward declarations
class Player;
enum class FacingDirection;

// ============================================================================
// Maze Class
// ============================================================================
// Represents the entire maze as an implicit 2D grid graph stored in a
// contiguous 1D flat array for maximum cache locality.
//
// Key Concept: IMPLICIT GRAPH
// We don't store explicit Node objects with pointers to neighbors.
// Instead, for any cell at position (x, y), we KNOW its neighbors are at
// (x+1, y), (x-1, y), (x, y+1), (x, y-1) — the edges are implied by
// the grid geometry. This is much faster and uses far less memory.
// ============================================================================

class Maze final : public World {
public:
  // --- Cell Type Constants ---
  // These are the integer values stored in our 1D grid array.
  // Each cell in the maze is one of these types.
  static constexpr int CELL_WALL = 0; // Solid wall — player cannot pass through
  static constexpr int CELL_CORRIDOR = 1; // Open floor — player can walk here
  static constexpr int CELL_ROOM =
      2; // Room floor — part of a BSP-carved office room

  // --- Constructor ---
  // Creates a maze of the given dimensions.
  // seed determines the random layout. Same seed = same maze.
  Maze(int width, int height, int cellSize = grid::CELL,
       unsigned int seed = 12345);
  ~Maze();

  // --- Core Accessors ---

  // Convert 2D grid coordinates (x, y) to a 1D array index.
  // Formula: index = y * width + x
  // This is the heart of our 1D-to-2D mapping.
  int getIndex(int x, int y) const;

  // wrapX/wrapY and toGridX/toGridY come from World.

  // Get the cell type at grid position (x, y).
  // Returns CELL_WALL, CELL_CORRIDOR, or CELL_ROOM.
  int getCell(int x, int y) const;

  // Set the cell type at grid position (x, y).
  void setCell(int x, int y, int cellType);

  // --- World contract ---
  // Walls and placed items. The room/corridor rule is isSealedFrom, below.
  bool isSolid(int x, int y) const override;

  // The door rule: from inside a room every corridor cell is a wall, and from
  // a corridor every room cell is. Doors are crossed by an explicit K/L
  // transition, never by walking.
  bool isSealedFrom(int x, int y, AreaState side) const;

  Maze *asMaze() override { return this; }
  const Maze *asMaze() const override { return this; }

  // ============================================================================
  // updateVisibility - the room field of view
  // ============================================================================
  // ROOM mode: an 8-way BFS flood through CELL_ROOM tiles marks the room the
  //   player stands in, and its bounding walls, visible. Rooms are fully lit,
  //   so there is no line-of-sight test to do - you see your room, and only
  //   your room.
  // CORRIDOR mode: does nothing but clear the buffer. The renderer draws every
  //   corridor tile and hides the unlit ones behind the screen-space light
  //   mask (MazeRenderer::buildLightMask), so a per-cell FOV would be computed
  //   and then ignored.
  //
  // NOTE: an earlier design used recursive shadowcasting for a forward-facing
  // corridor cone. The light mask replaced it; if true wall shadows are ever
  // needed, that is the algorithm to reach for
  // (http://www.roguebasin.com/index.php/FOV_using_recursive_shadowcasting).
  //
  // Time complexity: O(cells in the room) in ROOM mode, O(w*h) for the buffer
  // clear in both.
  // ============================================================================
  void updateVisibility(int playerX, int playerY, AreaState state);

  // Check if cell is visible (used by MazeRenderer)
  bool isVisible(int x, int y) const;

  // --- Phase 2.3: Rubik's Torus ---
  // Check if a coordinate is within the designated shifting slice
  bool isShiftingZone(int x, int y) const;

  // Phase 2.3: Zone Regeneration
  void clearShiftingZones();
  void addShiftingZone(int x, int y, int w, int h);
  void eraseZone(int startX, int startY, int width, int height);

  // --- Real-time Statistics ---
  // Returns the total number of cells that are NOT walls (O(1) time).
  int getNonWallCount() const { return m_nonWallCount; }
  // Returns the total number of corridor cells (O(1) time).
  int getCorridorCount() const { return m_corridorCount; }

  bool hasDiagonalLeak(int x, int y) const;

  // Enforces 1-to-1 Door Isolation Constraint
  bool isValidDoorPlacement(int x, int y) const;

  struct Room {
    int x, y, width, height;
    bool isRadiated = false;
  };

  void addRoom(const Room &room) { m_rooms.push_back(room); }
  const std::vector<Room> &getRooms() const { return m_rooms; }

  // --- Item Layer (Grid-Parallel) ---
  // Each cell can hold one item. O(1) read/write via the same index math
  // as m_grid. This replaces per-item-type lists (m_barrels, etc.).
  ItemType getItem(int x, int y) const override;
  void setItem(int x, int y, ItemType type) override;

  // Wipe all items in a rectangular zone and return a count of each type
  // that was removed. Used during Tic-Tac-Toe zone regeneration so the
  // ItemSpawner knows exactly what to replenish.
  std::map<ItemType, int> clearItemsInZone(int startX, int startY, int width,
                                           int height);

  // --- Phase 3: Radiation Mechanics ---
  void calculateRadiationZones();
  int getRadiationLevel(int x, int y) const;
  bool hasBarrel(int x, int y) const;
  bool isBarrelNear(int x, int y, int radius = 1) const;

  // --- Phase 5: Inventory System ---
  bool findNearestEmptyItemCell(int startX, int startY, int maxRadius,
                                int &outX, int &outY) const override;
  
  // --- Generic Item States ---
  // 0 = default, 1 = open, 2 = broken, etc.
  int getItemState(int x, int y) const override;
  void setItemState(int x, int y, int state);

  // --- Cupboard Inventories ---
  std::array<InventorySlot, INVENTORY_SLOTS>& getCupboardInventory(int x, int y);
  bool hasCupboardInventory(int x, int y) const;
  bool isCupboardEmpty(int x, int y) const;

  // --- Magic Book of Maps Overlay ---
  bool isMagicBookSpawned() const { return m_isMagicBookSpawned; }
  void spawnMagicBook(int x, int y) { m_isMagicBookSpawned = true; m_magicBookTableX = x; m_magicBookTableY = y; }
  void despawnMagicBook() { m_isMagicBookSpawned = false; m_magicBookTableX = -1; m_magicBookTableY = -1; }
  int getMagicBookX() const { return m_magicBookTableX; }
  int getMagicBookY() const { return m_magicBookTableY; }

private:
  // Real-time stat tracking
  int m_nonWallCount;
  int m_corridorCount;

  // THE 1D FLAT ARRAY
  std::vector<int> m_grid;
  std::vector<bool> m_visible;
  std::vector<int> m_radiationMap; // 0 for safe, >0 for radiated

  // Item layer — parallel to m_grid. Each index stores the ItemType
  // occupying that cell (NONE if empty). This is the single source of
  // truth for all placed items.
  std::vector<ItemType> m_items;

  // Helper to place walls and floors
  void setRoomTiles(int startX, int startY, int width, int height);

  // List of all carved rooms
  std::vector<Room> m_rooms;

  // List of shifting zones for Minimap rendering
  std::vector<Room> m_shiftingZones;

  // Cupboard inventories map (Key: 1D grid index)
  std::map<int, std::array<InventorySlot, INVENTORY_SLOTS>> m_cupboardInventories;

  // Item states map (Key: 1D grid index, Value: state integer)
  std::map<int, int> m_itemStates;

  // Magic book of maps overlay state
  bool m_isMagicBookSpawned = false;
  int m_magicBookTableX = -1;
  int m_magicBookTableY = -1;
};
