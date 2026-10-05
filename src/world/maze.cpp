#include "world/maze.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <queue>

// ============================================================================
// Constructor
// ============================================================================
Maze::Maze(int width, int height, int cellSize, unsigned int seed)
    : World(width, height, cellSize), m_nonWallCount(0),
      m_corridorCount(0), m_grid(width * height, CELL_WALL),
      m_visible(width * height, false), m_radiationMap(width * height, 0),
      m_items(width * height, ItemType::NONE) {}

Maze::~Maze() {}

// ============================================================================
// getIndex — The heart of 1D-to-2D Mapping
// ============================================================================
int Maze::getIndex(int x, int y) const {
  return wrapY(y) * m_width + wrapX(x);
}

// wrapX/wrapY are World's: see world/world.hpp for the modulo note.

// ============================================================================
// getCell — Read a Cell
// ============================================================================
int Maze::getCell(int x, int y) const { return m_grid[getIndex(x, y)]; }

bool Maze::isSolid(int x, int y) const {
  int i = getIndex(x, y);
  return m_grid[i] == CELL_WALL || m_items[i] != ItemType::NONE;
}

bool Maze::isSealedFrom(int x, int y, AreaState side) const {
  int cell = getCell(x, y);
  return (side == AreaState::CORRIDOR && cell == CELL_ROOM) ||
         (side == AreaState::ROOM && cell == CELL_CORRIDOR);
}

// ============================================================================
// setCell — Write a Cell
// ============================================================================
void Maze::setCell(int x, int y, int cellType) {
  int index = getIndex(x, y);
  int oldType = m_grid[index];

  if (oldType == CELL_WALL && cellType != CELL_WALL) {
    m_nonWallCount++;
  } else if (oldType != CELL_WALL && cellType == CELL_WALL) {
    m_nonWallCount--;
  }

  if (oldType == CELL_CORRIDOR && cellType != CELL_CORRIDOR) {
    m_corridorCount--;
  } else if (oldType != CELL_CORRIDOR && cellType == CELL_CORRIDOR) {
    m_corridorCount++;
  }

  m_grid[index] = cellType;
}

// ============================================================================
// updateVisibility - the room field of view
// ============================================================================
// See maze.hpp for the contract. ROOM mode floods the room; CORRIDOR mode
// deliberately computes nothing, because the renderer hides corridors with the
// screen-space light mask instead of a per-cell FOV.
// ============================================================================
void Maze::updateVisibility(int playerX, int playerY, AreaState state) {
  // Clear visibility every frame
  std::fill(m_visible.begin(), m_visible.end(), false);

  if (state == AreaState::ROOM) {
    // --- ROOMS ARE ALWAYS FULLY LIT ---
    // Simple BFS flood fill through CELL_ROOM tiles only.
    std::queue<std::pair<int, int>> q;
    q.push({playerX, playerY});
    int startIdx = getIndex(playerX, playerY);
    m_visible[startIdx] = true;

    int dx[] = {0, 0, -1, 1, -1, 1, -1, 1};
    int dy[] = {-1, 1, 0, 0, -1, -1, 1, 1};

    while (!q.empty()) {
      auto [x, y] = q.front();
      q.pop();

      for (int i = 0; i < 8; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        int nIndex = getIndex(nx, ny);

        if (!m_visible[nIndex]) {
          m_visible[nIndex] = true;

          // Only flood through room tiles (walls become visible but stop the
          // flood)
          if (getCell(nx, ny) == CELL_ROOM) {
            q.push({nx, ny});
          }
        }
      }
    }
  }
  // For corridors, we don't need to compute visibility, because the renderer
  // now completely ignores the FOV check and draws all corridor tiles,
  // relying entirely on the screen-space darkness overlay to hide unseen areas.
}

bool Maze::isVisible(int x, int y) const { return m_visible[getIndex(x, y)]; }

// ============================================================================
// PHASE 2.3: RUBIK'S TORUS
// ============================================================================

void Maze::clearShiftingZones() { m_shiftingZones.clear(); }

void Maze::addShiftingZone(int x, int y, int w, int h) {
  m_shiftingZones.push_back({x, y, w, h});
}

bool Maze::isShiftingZone(int x, int y) const {
  int wrappedX = wrapX(x);
  int wrappedY = wrapY(y);
  for (const auto &zone : m_shiftingZones) {
    if (wrappedX >= zone.x && wrappedX < zone.x + zone.width &&
        wrappedY >= zone.y && wrappedY < zone.y + zone.height) {
      return true;
    }
  }
  return false;
}

void Maze::eraseZone(int startX, int startY, int width, int height) {
  // Overwrite cells with solid rock
  for (int y = startY; y < startY + height; ++y) {
    for (int x = startX; x < startX + width; ++x) {
      setCell(x, y, CELL_WALL);
    }
  }

  // Remove any rooms that intersect this 2D zone
  auto it = std::remove_if(
      m_rooms.begin(), m_rooms.end(),
      [startX, startY, width, height](const Room &r) {
        bool outsideX = (r.x + r.width <= startX) || (r.x >= startX + width);
        bool outsideY = (r.y + r.height <= startY) || (r.y >= startY + height);
        return !(outsideX ||
                 outsideY); // It intersects if it is NOT completely outside
      });
  m_rooms.erase(it, m_rooms.end());
}

bool Maze::hasDiagonalLeak(int x, int y) const {
  const int ddx[] = {-1, 1, -1, 1};
  const int ddy[] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; ++i) {
    int nx = x + ddx[i];
    int ny = y + ddy[i];
    int tDiag = getCell(nx, ny);
    if (tDiag == Maze::CELL_CORRIDOR || tDiag == Maze::CELL_ROOM) {
      int t1 = getCell(nx, y);
      int t2 = getCell(x, ny);
      if (t1 == Maze::CELL_WALL && t2 == Maze::CELL_WALL) {
        return true;
      }
    }
  }
  return false;
}

// ============================================================================
// isValidDoorPlacement - 1-to-1 Door Isolation
// ============================================================================
bool Maze::isValidDoorPlacement(int x, int y) const {
  int roomNeighbors = 0;
  const int dx[] = {1, -1, 0, 0};
  const int dy[] = {0, 0, 1, -1};

  for (int d = 0; d < 4; ++d) {
    int nx = x + dx[d];
    int ny = y + dy[d];
    if (nx >= 0 && nx < m_width && ny >= 0 && ny < m_height) {
      if (getCell(nx, ny) == CELL_ROOM) {
        roomNeighbors++;
      }
    }
  }
  // Rule 1: A corridor cannot border > 1 room cell (prevents inner corner
  // doors)
  if (roomNeighbors > 1)
    return false;

  // Rule 2: A room cell cannot border > 1 corridor cell (prevents adjacent
  // doors)
  for (int d = 0; d < 4; ++d) {
    int nx = x + dx[d];
    int ny = y + dy[d];
    if (nx >= 0 && nx < m_width && ny >= 0 && ny < m_height) {
      if (getCell(nx, ny) == CELL_ROOM) {
        int currentCorridorNeighbors = 0;
        for (int dd = 0; dd < 4; ++dd) {
          int nnx = nx + dx[dd];
          int nny = ny + dy[dd];
          if (nnx >= 0 && nnx < m_width && nny >= 0 && nny < m_height) {
            if (getCell(nnx, nny) == CELL_CORRIDOR) {
              currentCorridorNeighbors++;
            }
          }
        }
        if (currentCorridorNeighbors >= 1)
          return false;
      }
    }
  }

  // Rule 3: Prevents more than 2 contiguous doors from forming.
  // A cell is considered a door if it is a corridor and borders a room.
  if (roomNeighbors > 0) {
    auto isDoor = [this](int cx, int cy) {
      if (getCell(cx, cy) != CELL_CORRIDOR) return false;
      const int ddx[] = {1, -1, 0, 0};
      const int ddy[] = {0, 0, 1, -1};
      for (int i = 0; i < 4; ++i) {
        int nx = cx + ddx[i];
        int ny = cy + ddy[i];
        if (nx >= 0 && nx < m_width && ny >= 0 && ny < m_height) {
          if (getCell(nx, ny) == CELL_ROOM) return true;
        }
      }
      return false;
    };

    std::vector<std::pair<int, int>> doorNeighbors;
    for (int d = 0; d < 4; ++d) {
      int nx = x + dx[d];
      int ny = y + dy[d];
      if (nx >= 0 && nx < m_width && ny >= 0 && ny < m_height) {
        if (isDoor(nx, ny)) {
          doorNeighbors.push_back({nx, ny});
        }
      }
    }

    if (doorNeighbors.size() >= 2) return false;

    if (doorNeighbors.size() == 1) {
      int nx = doorNeighbors[0].first;
      int ny = doorNeighbors[0].second;
      for (int d = 0; d < 4; ++d) {
        int nnx = nx + dx[d];
        int nny = ny + dy[d];
        if (nnx == x && nny == y) continue;
        if (nnx >= 0 && nnx < m_width && nny >= 0 && nny < m_height) {
          if (isDoor(nnx, nny)) return false;
        }
      }
    }
  }

  return true;
}

// ============================================================================
// PHASE 3: RADIATION MECHANICS
// ============================================================================

// --- Item Layer Accessors ---
// O(1) lookup into the grid-parallel item array. Same toroidal index math
// as getCell/setCell, ensuring items wrap correctly on the torus.

ItemType Maze::getItem(int x, int y) const {
  return m_items[getIndex(x, y)];
}

void Maze::setItem(int x, int y, ItemType type) {
  m_items[getIndex(x, y)] = type;
}

// ============================================================================
// clearItemsInZone — Zone Regeneration Support
// ============================================================================
// When a shifting zone regenerates, any items inside it become orphaned
// (their cells are overwritten with walls). This method wipes them and
// returns a map of { ItemType -> count } so the ItemSpawner knows exactly
// what to replenish.
//
// Time Complexity: O(width * height) for the zone — a small constant
// compared to the full grid.
// ============================================================================
std::map<ItemType, int> Maze::clearItemsInZone(int startX, int startY,
                                                int width, int height) {
  std::map<ItemType, int> removed;
  for (int y = startY; y < startY + height; ++y) {
    for (int x = startX; x < startX + width; ++x) {
      int idx = getIndex(x, y);
      ItemType item = m_items[idx];
      if (item != ItemType::NONE) {
        removed[item]++;
        m_items[idx] = ItemType::NONE;
        // m_itemStates and m_cupboardInventories are keyed by the same index.
        // Leaving an entry behind hands it to whatever spawns here next — a
        // cupboard inheriting a table root's state 1 renders permanently open —
        // and the maps would grow without bound as zones regenerate each night.
        m_itemStates.erase(idx);
        m_cupboardInventories.erase(idx);
      }
    }
  }
  return removed;
}

// ============================================================================
// calculateRadiationZones — BFS Radiation Spread
// ============================================================================
// Scans the entire m_items grid to find all BARREL cells, then runs a
// 0-1 BFS from each barrel to flood radiation through rooms and corridors.
//
// This replaces the old approach of iterating a separate m_barrels list.
// The scan is O(W*H) but only happens when barrels change, not every frame.
// ============================================================================
void Maze::calculateRadiationZones() {
  std::fill(m_radiationMap.begin(), m_radiationMap.end(), 0);

  // Collect all barrel positions by scanning the item grid
  struct BarrelPos { int x, y; };
  std::vector<BarrelPos> barrelPositions;
  for (int y = 0; y < m_height; ++y) {
    for (int x = 0; x < m_width; ++x) {
      if (getItem(x, y) == ItemType::TOXIC_WASTE) {
        barrelPositions.push_back({x, y});
      }
    }
  }

  for (const auto &barrel : barrelPositions) {
    // 0-1 BFS for each barrel up to depth 10.
    // Moving ROOM -> ROOM costs 0, effectively infecting the whole room
    // instantly.
    std::deque<std::pair<int, int>> q;
    std::vector<int> distances(m_width * m_height, -1);

    int startIdx = getIndex(barrel.x, barrel.y);
    q.push_back({barrel.x, barrel.y});
    distances[startIdx] = 0;

    const int dx[] = {1, -1, 0, 0};
    const int dy[] = {0, 0, 1, -1};

    while (!q.empty()) {
      auto [x, y] = q.front();
      q.pop_front();

      int currentIdx = getIndex(x, y);
      int d = distances[currentIdx];
      m_radiationMap[currentIdx] = 1; // Mark as radiated

      if (d >= 10)
        continue; // Max radius (reduced to 10)

      int currentType = m_grid[currentIdx];

      for (int i = 0; i < 4; ++i) {
        int nx = wrapX(x + dx[i]);
        int ny = wrapY(y + dy[i]);

        int nIdx = getIndex(nx, ny);
        int nextType = m_grid[nIdx];

        // Fluid flow: Only pass through open spaces (ROOM or CORRIDOR). Walls
        // block.
        if (nextType == CELL_ROOM || nextType == CELL_CORRIDOR) {
          // If we are in a room and moving to another room tile, cost is 0.
          // This causes the entire room to instantly flood at the same depth.
          int cost =
              (currentType == CELL_ROOM && nextType == CELL_ROOM) ? 0 : 1;

          if (distances[nIdx] == -1 || distances[nIdx] > d + cost) {
            distances[nIdx] = d + cost;
            if (cost == 0) {
              q.push_front({nx, ny});
            } else {
              q.push_back({nx, ny});
            }
          }
        }
      }
    }
  }

  // Post-pass: If ANY tile in a contiguous room block is radiated, mark the
  // ENTIRE block as radiated. This fixes the edge case where BFS enters a room
  // at max depth and can't spread further (even at cost 0) due to the depth
  // cap, or when rooms are non-rectangular due to merging/generation.
  std::vector<bool> roomVisited(m_width * m_height, false);
  for (int y = 0; y < m_height; ++y) {
    for (int x = 0; x < m_width; ++x) {
      int idx = getIndex(x, y);
      if (m_grid[idx] == CELL_ROOM && m_radiationMap[idx] > 0 &&
          !roomVisited[idx]) {
        // We found a radiated room tile. Flood fill all contiguous room tiles.
        std::deque<std::pair<int, int>> rq;
        rq.push_back({x, y});
        roomVisited[idx] = true;

        while (!rq.empty()) {
          auto [cx, cy] = rq.front();
          rq.pop_front();
          int cIdx = getIndex(cx, cy);

          m_radiationMap[cIdx] = 1; // Paint it radiated

          const int rdx[] = {1, -1, 0, 0};
          const int rdy[] = {0, 0, 1, -1};
          for (int d = 0; d < 4; ++d) {
            int nx = wrapX(cx + rdx[d]);
            int ny = wrapY(cy + rdy[d]);

            int nIdx = getIndex(nx, ny);
            if (m_grid[nIdx] == CELL_ROOM && !roomVisited[nIdx]) {
              roomVisited[nIdx] = true;
              rq.push_back({nx, ny});
            }
          }
        }
      }
    }
  }
}


int Maze::getRadiationLevel(int x, int y) const {
  return m_radiationMap[getIndex(x, y)];
}

// O(1) lookup — the whole point of the grid-parallel item layer.
bool Maze::hasBarrel(int x, int y) const {
  return getItem(x, y) == ItemType::TOXIC_WASTE;
}

bool Maze::isBarrelNear(int x, int y, int radius) const {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      int nx = wrapX(x + dx);
      int ny = wrapY(y + dy);
      if (getItem(nx, ny) == ItemType::TOXIC_WASTE) {
        return true;
      }
    }
  }
  return false;
}

// ============================================================================
// PHASE 5: INVENTORY SYSTEM
// ============================================================================



bool Maze::findNearestEmptyItemCell(int startX, int startY, int maxRadius, int& outX, int& outY) const {
  struct BFSNode { int x, y, depth; };
  std::deque<BFSNode> q;
  std::vector<bool> visited(m_width * m_height, false);

  q.push_back({startX, startY, 0});
  visited[getIndex(startX, startY)] = true;

  const int dx[] = {0, 1, -1, 0, 0};
  const int dy[] = {0, 0, 0, 1, -1};

  while (!q.empty()) {
    auto node = q.front();
    q.pop_front();

    if (node.depth > maxRadius) continue;

    int cellType = getCell(node.x, node.y);
    if ((cellType == CELL_ROOM || cellType == CELL_CORRIDOR) && getItem(node.x, node.y) == ItemType::NONE) {
      outX = node.x;
      outY = node.y;
      return true;
    }

    for (int i = 1; i < 5; ++i) {
      int nx = wrapX(node.x + dx[i]);
      int ny = wrapY(node.y + dy[i]);

      int idx = getIndex(nx, ny);
      if (!visited[idx]) {
        visited[idx] = true;
        q.push_back({nx, ny, node.depth + 1});
      }
    }
  }
  return false;
}


// ============================================================================
// PHASE 6: CUPBOARD INVENTORIES & ITEM STATES
// ============================================================================

int Maze::getItemState(int x, int y) const {
  int idx = getIndex(x, y);
  auto it = m_itemStates.find(idx);
  return (it != m_itemStates.end()) ? it->second : 0;
}

void Maze::setItemState(int x, int y, int state) {
  int idx = getIndex(x, y);
  if (state == 0) {
      m_itemStates.erase(idx);
  } else {
      m_itemStates[idx] = state;
  }
}

std::array<InventorySlot, INVENTORY_SLOTS>& Maze::getCupboardInventory(int x, int y) {
  int idx = getIndex(x, y);
  return m_cupboardInventories[idx];
}

bool Maze::hasCupboardInventory(int x, int y) const {
  int idx = getIndex(x, y);
  return m_cupboardInventories.count(idx) > 0;
}

bool Maze::isCupboardEmpty(int x, int y) const {
  int idx = getIndex(x, y);
  auto it = m_cupboardInventories.find(idx);
  if (it != m_cupboardInventories.end()) {
      for (const auto& slot : it->second) {
          if (slot.type != ItemType::NONE) return false;
      }
  }
  return true;
}
