#include "world/generators/bsp_generator.hpp"
#include "world/generators/loop_generator.hpp"
#include "world/generators/prims_generator.hpp"
#include "world/generators/tunnel_borer.hpp"
#include "world/item_spawner.hpp"
#include "states/maze_state.hpp"
#include "world/maze.hpp"
#include "render/view_bounds.hpp"
#include "dev/debug_overlay.hpp"
#include "items/item_database.hpp"
#include "ui/ui_manager.hpp"
#include <chrono>
#include <ctime>
#include <thread>
#include <gtest/gtest.h>
#include <queue>

// 1. Initialize a small 10x10 maze
TEST(MazeTest, Constructor) {
  Maze maze(10, 10, 32, 12345);
  EXPECT_EQ(maze.getWidth(), 10);
  EXPECT_EQ(maze.getHeight(), 10);
  EXPECT_EQ(maze.getCellSize(), 32);
}

// 2. Test getIndex
TEST(MazeTest, GetIndexMath) {
  Maze maze(10, 10, 32, 12345);
  EXPECT_EQ(maze.getIndex(5, 2), 25);
  EXPECT_EQ(maze.getIndex(0, 0), 0);
  EXPECT_EQ(maze.getIndex(9, 9), 99);
}

// 4. Test setCell and getCell
TEST(MazeTest, CellReadWrite) {
  Maze maze(10, 10, 32, 12345);
  EXPECT_EQ(maze.getCell(3, 3), Maze::CELL_WALL);

  maze.setCell(3, 3, Maze::CELL_CORRIDOR);
  EXPECT_EQ(maze.getCell(3, 3), Maze::CELL_CORRIDOR);

  maze.setCell(8, 8, Maze::CELL_ROOM);
  EXPECT_EQ(maze.getCell(8, 8), Maze::CELL_ROOM);
}

// 5. Test Toroidal Modulo Wrapping
TEST(MazeTest, ToroidalWrapping) {
  Maze maze(10, 10, 32, 12345);

  // Write to a coordinate physically outside the 10x10 grid.
  // X = -5 should wrap to X = 5
  // Y = 20 should wrap to Y = 0
  maze.setCell(-5, 20, Maze::CELL_CORRIDOR);
  
  // Verify that the mathematical wrap successfully mapped it to (5, 0)
  EXPECT_EQ(maze.getCell(5, 0), Maze::CELL_CORRIDOR);

  // Verify that reading from (-5, 20) also fetches the wrapped cell (5, 0)
  EXPECT_EQ(maze.getCell(-5, 20), Maze::CELL_CORRIDOR);
}

// 5b. The wrap helpers, which every coordinate offset in the codebase now
// goes through. The multiple-of-width cases are the ones the old hand-rolled
// `(x + dx % w + w) % w` got wrong: % bound to dx alone, so any offset at or
// beyond the grid width fell out of range.
TEST(MazeTest, WrapHelpersNormaliseAnyOffset) {
  Maze maze(10, 10, 32, 12345);

  EXPECT_EQ(maze.wrapX(3), 3);
  EXPECT_EQ(maze.wrapY(3), 3);

  EXPECT_EQ(maze.wrapX(-1), 9);
  EXPECT_EQ(maze.wrapY(-1), 9);

  EXPECT_EQ(maze.wrapX(10), 0);
  EXPECT_EQ(maze.wrapY(10), 0);

  // Offsets larger than the grid must still land in range.
  EXPECT_EQ(maze.wrapX(23), 3);
  EXPECT_EQ(maze.wrapY(-23), 7);

  // The helpers and getIndex must agree, or the item and cell layers drift.
  EXPECT_EQ(maze.getIndex(-5, 20), maze.wrapY(20) * maze.getWidth() + maze.wrapX(-5));
}

// 6. Test BSP Generation
TEST(MazeTest, BSPGenerationCarvesRooms) {
  // We use a fixed seed (12345) so the test is fully deterministic
  Maze maze(40, 22, 32, 12345);

  // 1. Before generation, the entire maze should be solid rock (0 rooms)
  int roomCountBefore = 0;
  for (int y = 0; y < 22; ++y) {
    for (int x = 0; x < 40; ++x) {
      if (maze.getCell(x, y) == Maze::CELL_ROOM) {
        roomCountBefore++;
      }
    }
  }
  EXPECT_EQ(roomCountBefore, 0);

  // 2. Execute the BSP algorithm
  std::mt19937 rng(12345);
  BSPGenerator bsp;
  bsp.generate(maze, rng);

  // 3. After generation, we should have successfully carved hundreds of room
  // tiles
  int roomCountAfter = 0;
  for (int y = 0; y < 22; ++y) {
    for (int x = 0; x < 40; ++x) {
      if (maze.getCell(x, y) == Maze::CELL_ROOM) {
        roomCountAfter++;
      }
    }
  }

  // We expect greater than 0 room tiles to exist
  EXPECT_GT(roomCountAfter, 0);
}

// 7. Test Corridor Generation (Prim's Algorithm)
TEST(MazeTest, CorridorGenerationCarvesFloors) {
  Maze maze(40, 22, 32, 12345);

  // Generate rooms first (corridors need rooms to grow from)
  std::mt19937 rng(12345);
  BSPGenerator bsp;
  bsp.generate(maze, rng);

  // 1. Before corridors, there should be zero CELL_CORRIDOR tiles
  //    (BSP only creates CELL_ROOM tiles, not CELL_CORRIDOR)
  int floorCountBefore = 0;
  for (int y = 0; y < 22; ++y) {
    for (int x = 0; x < 40; ++x) {
      if (maze.getCell(x, y) == Maze::CELL_CORRIDOR) {
        ++floorCountBefore;
      }
    }
  }
  EXPECT_EQ(floorCountBefore, 0);

  // 2. Execute Prim's corridor generation
  PrimsGenerator prims;
  prims.generate(maze, rng, bsp.getMiddleRoomIndex());

  // 3. After corridors, we should have many CELL_CORRIDOR tiles
  int floorCountAfter = 0;
  for (int y = 0; y < 22; ++y) {
    for (int x = 0; x < 40; ++x) {
      if (maze.getCell(x, y) == Maze::CELL_CORRIDOR) {
        ++floorCountAfter;
      }
    }
  }

  // The mold should have carved a significant number of corridor tiles
  EXPECT_GT(floorCountAfter, 0);
}

// 8. Test 100% Connectivity (Tunnel Borer)
TEST(MazeTest, FullConnectivityEnsured) {
  // We use a random seed so the test validates a new maze structure every
  // single run! If a specific test breaks, you can manually hardcode the seed
  // here to reproduce it:
  // unsigned int seed = 7;
  // unsigned int seed = 1781431085;
  unsigned int seed = std::time(nullptr);

  // Record the seed in the GTest XML results for CI/CD tracking
  testing::Test::RecordProperty("RandomSeed", std::to_string(seed));

  Maze maze(250, 150, 32, seed);

  // Run the full generation pipeline
  std::mt19937 rng(seed);
  BSPGenerator bsp;
  bsp.generate(maze, rng);
  PrimsGenerator prims;
  prims.generate(maze, rng, bsp.getMiddleRoomIndex());
  LoopGenerator loops;
  loops.generate(maze, rng);
  TunnelBorer borer;
  borer.ensureConnectivity(maze);

  // 1. Get total valid cells (O(1) lookup!)
  int totalValidCells = maze.getNonWallCount();
  int startX = -1, startY = -1;

  // Find the first valid cell to use as our BFS starting point
  for (int y = 0; y < maze.getHeight(); ++y) {
    for (int x = 0; x < maze.getWidth(); ++x) {
      if (maze.getCell(x, y) != Maze::CELL_WALL) {
        startX = x;
        startY = y;
        break; // Found it, stop searching!
      }
    }
    if (startX != -1)
      break;
  }

  // Ensure we actually generated a maze!
  EXPECT_GT(totalValidCells, 0);
  EXPECT_NE(startX, -1);

  // 2. Run a BFS to see how many cells are actually reachable
  int reachableCells = 0;
  std::vector<bool> visited(maze.getWidth() * maze.getHeight(), false);
  std::queue<std::pair<int, int>> q;

  q.push({startX, startY});
  visited[maze.getIndex(startX, startY)] = true;
  reachableCells++;

  const int dx[] = {0, 1, 0, -1};
  const int dy[] = {-1, 0, 1, 0};

  while (!q.empty()) {
    auto [cx, cy] = q.front();
    q.pop();

    for (int i = 0; i < 4; ++i) {
      int nx = cx + dx[i];
      int ny = cy + dy[i];

      int nIndex = maze.getIndex(nx, ny);
      if (!visited[nIndex] && maze.getCell(nx, ny) != Maze::CELL_WALL) {
        visited[nIndex] = true;
        q.push({nx, ny});
        reachableCells++;
      }
    }
  }

  // 3. The true test of connectivity:
  // Are the cells we can walk to equal to the total number of walk-able cells?
  // If not, it means there are isolated islands we couldn't reach!
  EXPECT_EQ(reachableCells, totalValidCells)
      << "\n============================================\n"
      << "ISOLATED ROOMS DETECTED!\n"
      << "To reproduce this exact maze, hardcode this seed:\n"
      << "unsigned int seed = " << seed << ";\n"
      << "============================================\n";
}

TEST(MazeTest, ZoneRegenerationConnectivity) {
  unsigned int seed = 123456789;
  std::mt19937 rng(seed);
  Maze maze(250, 150, 32, seed);

  // 1. Initial full generation
  BSPGenerator bsp;
  bsp.generate(maze, rng);
  PrimsGenerator prims;
  prims.generate(maze, rng, bsp.getMiddleRoomIndex());
  LoopGenerator loops;
  loops.generate(maze, rng);
  TunnelBorer borer;
  borer.ensureConnectivity(maze);

  // 2. Erase and regenerate the Tic-Tac-Toe zones
  std::vector<Maze::Room> zones = {
      {55, 0, 14, 150},   // V-Left
      {180, 0, 14, 150},  // V-Right
      {0, 30, 55, 14},    // H-Top-Left
      {69, 30, 111, 14},  // H-Top-Mid
      {194, 30, 56, 14},  // H-Top-Right
      {0, 105, 55, 14},   // H-Bot-Left
      {69, 105, 111, 14}, // H-Bot-Mid
      {194, 105, 56, 14}  // H-Bot-Right
  };

  for (const auto& z : zones) {
    maze.eraseZone(z.x, z.y, z.width, z.height);
  }
  for (const auto& z : zones) {
    bsp.generateZone(maze, rng, z.x, z.y, z.width, z.height);
  }
  for (const auto& z : zones) {
    prims.generateZone(maze, rng, z.x, z.y, z.width, z.height);
  }
  for (const auto& z : zones) {
    loops.generateZone(maze, rng, z.x, z.y, z.width, z.height);
  }
  borer.ensureConnectivity(maze);

  // 4. Test connectivity
  int totalValidCells = maze.getNonWallCount();
  int startX = -1, startY = -1;

  for (int y = 0; y < maze.getHeight(); ++y) {
    for (int x = 0; x < maze.getWidth(); ++x) {
      if (maze.getCell(x, y) != Maze::CELL_WALL) {
        startX = x;
        startY = y;
        break;
      }
    }
    if (startX != -1) break;
  }

  EXPECT_GT(totalValidCells, 0);
  EXPECT_NE(startX, -1);

  int reachableCells = 0;
  std::vector<bool> visited(maze.getWidth() * maze.getHeight(), false);
  std::queue<std::pair<int, int>> q;

  q.push({startX, startY});
  visited[maze.getIndex(startX, startY)] = true;
  reachableCells++;

  const int dx[] = {0, 1, 0, -1};
  const int dy[] = {-1, 0, 1, 0};

  while (!q.empty()) {
    auto [cx, cy] = q.front();
    q.pop();

    for (int i = 0; i < 4; ++i) {
      int nx = cx + dx[i];
      int ny = cy + dy[i];

      int nIndex = maze.getIndex(nx, ny);
      if (!visited[nIndex] && maze.getCell(nx, ny) != Maze::CELL_WALL) {
        visited[nIndex] = true;
        q.push({nx, ny});
        reachableCells++;
      }
    }
  }

  EXPECT_EQ(reachableCells, totalValidCells)
      << "\n============================================\n"
      << "ZONE REGENERATION BROKE CONNECTIVITY!\n"
      << "Seed: " << seed << "\n"
      << "============================================\n";
}

TEST(MazeTest, NoDiagonalLeaks) {
  unsigned int seed = 987654321;
  std::mt19937 rng(seed);
  Maze maze(250, 150, 32, seed);

  auto scanForLeaks = [&](const std::string& stepName) {
    bool foundDiagonalLeak = false;
    int leakX = -1, leakY = -1;

    for (int y = 0; y < maze.getHeight(); ++y) {
      for (int x = 0; x < maze.getWidth(); ++x) {
        int tMain = maze.getCell(x, y);
        if (tMain == Maze::CELL_CORRIDOR || tMain == Maze::CELL_ROOM) {
          if (maze.hasDiagonalLeak(x, y)) {
            foundDiagonalLeak = true;
            leakX = x;
            leakY = y;
            break;
          }
        }
      }
      if (foundDiagonalLeak) break;
    }

    EXPECT_FALSE(foundDiagonalLeak) << "Diagonal leak found at (" << leakX << ", " << leakY << ") after " << stepName << "!";
  };

  // 1. Initial full generation
  BSPGenerator bsp;
  bsp.generate(maze, rng);
  PrimsGenerator prims;
  prims.generate(maze, rng, bsp.getMiddleRoomIndex());
  LoopGenerator loops;
  loops.generate(maze, rng);
  TunnelBorer borer;
  borer.ensureConnectivity(maze);

  // 2. Scan after initial generation
  scanForLeaks("Initial Generation");

  // 3. Erase and regenerate the Tic-Tac-Toe zones to test sleep mutation
  std::vector<Maze::Room> zones = {
      {55, 0, 14, 150},   // V-Left
      {180, 0, 14, 150},  // V-Right
      {0, 30, 55, 14},    // H-Top-Left
      {69, 30, 111, 14},  // H-Top-Mid
      {194, 30, 56, 14},  // H-Top-Right
      {0, 105, 55, 14},   // H-Bot-Left
      {69, 105, 111, 14}, // H-Bot-Mid
      {194, 105, 56, 14}  // H-Bot-Right
  };

  for (const auto& z : zones) {
    maze.eraseZone(z.x, z.y, z.width, z.height);
  }
  for (const auto& z : zones) {
    bsp.generateZone(maze, rng, z.x, z.y, z.width, z.height);
  }
  for (const auto& z : zones) {
    prims.generateZone(maze, rng, z.x, z.y, z.width, z.height);
  }
  for (const auto& z : zones) {
    loops.generateZone(maze, rng, z.x, z.y, z.width, z.height);
  }
  borer.ensureConnectivity(maze);

  // 4. Final scan after regeneration
  scanForLeaks("Zone Regeneration");
}

// 10. Test Uniform Room Radiation
TEST(MazeTest, UniformRoomRadiation) {
  // Test with 5 different seeds
  unsigned int seeds[5] = {101, 202, 303, 404, 505};
  
  for (int i = 0; i < 5; ++i) {
    unsigned int seed = seeds[i];
    std::mt19937 rng(seed);
    Maze maze(100, 100, 32, seed);
    
    // Generate full maze
    BSPGenerator bsp;
    bsp.generate(maze, rng);
    PrimsGenerator prims;
    prims.generate(maze, rng, bsp.getMiddleRoomIndex());
    LoopGenerator loops;
    loops.generate(maze, rng);
    TunnelBorer borer;
    borer.ensureConnectivity(maze);
    prims.pruneSmallAlcoves(maze, 5);
    
    // Spawn barrels using ItemSpawner
    std::mt19937 testRng(42);
    ItemSpawner spawner(testRng);
    // Manually spawn 15 barrels across the full maze for thorough testing
    for (int i = 0; i < 15; ++i) {
      // Use rejection sampling to place barrels in valid room cells
      for (int attempt = 0; attempt < 100; ++attempt) {
        int rx = testRng() % maze.getWidth();
        int ry = testRng() % maze.getHeight();
        if (maze.getCell(rx, ry) == Maze::CELL_ROOM &&
            maze.getItem(rx, ry) == ItemType::NONE) {
          maze.setItem(rx, ry, ItemType::TOXIC_WASTE);
          break;
        }
      }
    }
    maze.calculateRadiationZones();
    
    // Check uniform radiation in every contiguous CELL_ROOM component
    std::vector<bool> visited(maze.getWidth() * maze.getHeight(), false);
    
    for (int y = 0; y < maze.getHeight(); ++y) {
      for (int x = 0; x < maze.getWidth(); ++x) {
        int startIdx = maze.getIndex(x, y);
        if (maze.getCell(x, y) == Maze::CELL_ROOM && !visited[startIdx]) {
          
          bool isComponentRadiated = maze.getRadiationLevel(x, y) > 0;
          
          // Flood fill to find all contiguous room tiles
          std::deque<std::pair<int, int>> q;
          q.push_back({x, y});
          visited[startIdx] = true;
          
          while (!q.empty()) {
            auto [cx, cy] = q.front();
            q.pop_front();
            
            bool currentRadiated = maze.getRadiationLevel(cx, cy) > 0;
            EXPECT_EQ(currentRadiated, isComponentRadiated) 
                << "Mismatch in contiguous room radiation! Seed: " << seed 
                << " at (" << cx << "," << cy << ")";
            
            const int dx[] = {1, -1, 0, 0};
            const int dy[] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
              int nx = (cx + dx[d]) % maze.getWidth();
              if (nx < 0) nx += maze.getWidth();
              int ny = (cy + dy[d]) % maze.getHeight();
              if (ny < 0) ny += maze.getHeight();
              
              int nIdx = maze.getIndex(nx, ny);
              if (maze.getCell(nx, ny) == Maze::CELL_ROOM && !visited[nIdx]) {
                visited[nIdx] = true;
                q.push_back({nx, ny});
              }
            }
          }
        }
      }
    }
  }
}

// ============================================================================
// Zone-regeneration item bookkeeping
// ============================================================================
// MazeState::regenerateTicTacToeZones erases eight strips of the world and
// asks the ItemSpawner to replenish exactly what was destroyed. That contract
// is only as good as clearItemsInZone's report — an undercount silently thins
// the world out a little more on every regeneration.
// ============================================================================

TEST(MazeItemLayerTest, ClearItemsInZoneReportsExactlyWhatItRemoved) {
  Maze maze(20, 20, 32, 12345);

  maze.setItem(5, 5, ItemType::TOXIC_WASTE);
  maze.setItem(6, 5, ItemType::MUSHROOM);
  maze.setItem(7, 5, ItemType::MUSHROOM);
  maze.setItem(5, 6, ItemType::CUPBOARD);

  auto removed = maze.clearItemsInZone(4, 4, 5, 5);

  EXPECT_EQ(removed[ItemType::MUSHROOM], 2);
  EXPECT_EQ(removed[ItemType::TOXIC_WASTE], 1);
  EXPECT_EQ(removed[ItemType::CUPBOARD], 1);
  EXPECT_EQ(removed.count(ItemType::NONE), 0u)
      << "empty cells are not items and must not be replenished";

  EXPECT_EQ(maze.getItem(5, 5), ItemType::NONE);
  EXPECT_EQ(maze.getItem(6, 5), ItemType::NONE);
  EXPECT_EQ(maze.getItem(7, 5), ItemType::NONE);
  EXPECT_EQ(maze.getItem(5, 6), ItemType::NONE);
}

TEST(MazeItemLayerTest, ClearItemsInZoneLeavesItemsOutsideTheZoneAlone) {
  Maze maze(20, 20, 32, 12345);

  maze.setItem(5, 5, ItemType::MUSHROOM);  // inside
  maze.setItem(10, 10, ItemType::MUSHROOM); // outside

  auto removed = maze.clearItemsInZone(4, 4, 5, 5);

  EXPECT_EQ(removed[ItemType::MUSHROOM], 1);
  EXPECT_EQ(maze.getItem(10, 10), ItemType::MUSHROOM);
}

// m_itemStates is keyed by the same 1D index as m_items, so clearing one and
// not the other leaves an orphaned state behind. The next item to occupy that
// index inherits it: a cupboard landing on a former table root reads as
// state 1 = open, and ItemRenderer draws it permanently ajar.
TEST(MazeItemLayerTest, ClearItemsInZoneAlsoResetsItemState) {
  Maze maze(20, 20, 32, 12345);

  maze.setItem(5, 5, ItemType::TABLE);
  maze.setItemState(5, 5, 1); // horizontal-right table root

  maze.clearItemsInZone(4, 4, 5, 5);

  EXPECT_EQ(maze.getItem(5, 5), ItemType::NONE);
  EXPECT_EQ(maze.getItemState(5, 5), 0)
      << "a cleared cell must not hand its old state to the next occupant";
}

// m_cupboardInventories is keyed by the same 1D index too. A cupboard wiped by
// a shifting zone that keeps its 20-slot array leaves hasCupboardInventory()
// reporting true for a cell with no cupboard on it, and the map grows a little
// on every night's regeneration.
TEST(MazeItemLayerTest, ClearItemsInZoneAlsoDropsTheCupboardInventory) {
  Maze maze(20, 20, 32, 12345);

  maze.setItem(5, 5, ItemType::CUPBOARD);
  maze.getCupboardInventory(5, 5)[0] = {ItemType::PAPER, 2, 0};
  ASSERT_TRUE(maze.hasCupboardInventory(5, 5));
  ASSERT_FALSE(maze.isCupboardEmpty(5, 5));

  maze.clearItemsInZone(4, 4, 5, 5);

  EXPECT_EQ(maze.getItem(5, 5), ItemType::NONE);
  EXPECT_FALSE(maze.hasCupboardInventory(5, 5))
      << "a cleared cell must not keep an inventory for a cupboard that is gone";
  EXPECT_TRUE(maze.isCupboardEmpty(5, 5));
}

// ============================================================================
// Shifting-zone layout
// ============================================================================
// The eight tic-tac-toe strips used to be eight hardcoded rectangles that were
// only correct at 250x150. They are now derived from the world's dimensions so
// the maze can be resized and the strip thickness can vary per night, which is
// what the roadmap plans. This pins the derivation to the exact rectangles the
// hardcoded list held, so the refactor cannot have moved the world.
// ============================================================================
TEST(TicTacToeZoneTest, DerivedLayoutMatchesTheOriginalHardcodedRectangles) {
  auto zones = MazeState::buildTicTacToeZones(250, 150, 14);

  const int expected[8][4] = {
      {55, 0, 14, 150},   {180, 0, 14, 150}, {0, 30, 55, 14},
      {69, 30, 111, 14},  {194, 30, 56, 14}, {0, 105, 55, 14},
      {69, 105, 111, 14}, {194, 105, 56, 14}};

  ASSERT_EQ(zones.size(), 8u);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(zones[i].x, expected[i][0]) << "zone " << i << " x";
    EXPECT_EQ(zones[i].y, expected[i][1]) << "zone " << i << " y";
    EXPECT_EQ(zones[i].width, expected[i][2]) << "zone " << i << " width";
    EXPECT_EQ(zones[i].height, expected[i][3]) << "zone " << i << " height";
  }
}

// The strips must not overlap each other, or clearItemsInZone would double
// count the items it reports and the spawner would over-replenish.
TEST(TicTacToeZoneTest, StripsCoverDistinctCells) {
  const int w = 250, h = 150;
  auto zones = MazeState::buildTicTacToeZones(w, h, 14);

  std::vector<int> hits(w * h, 0);
  for (const auto &z : zones) {
    for (int y = z.y; y < z.y + z.height; ++y) {
      for (int x = z.x; x < z.x + z.width; ++x) {
        ASSERT_GE(x, 0);
        ASSERT_LT(x, w);
        ASSERT_GE(y, 0);
        ASSERT_LT(y, h);
        hits[y * w + x]++;
      }
    }
  }

  for (int i = 0; i < w * h; ++i) {
    ASSERT_LE(hits[i], 1) << "cell " << i << " is covered by two strips";
  }
}

// A different world size must still produce a well-formed board rather than
// strips that run off the edge or invert.
TEST(TicTacToeZoneTest, StaysInBoundsAtOtherWorldSizes) {
  const int w = 120, h = 80, t = 8;
  auto zones = MazeState::buildTicTacToeZones(w, h, t);

  ASSERT_FALSE(zones.empty());
  for (const auto &z : zones) {
    EXPECT_GT(z.width, 0);
    EXPECT_GT(z.height, 0);
    EXPECT_GE(z.x, 0);
    EXPECT_GE(z.y, 0);
    EXPECT_LE(z.x + z.width, w) << "strip runs off the right edge";
    EXPECT_LE(z.y + z.height, h) << "strip runs off the bottom edge";
  }
}

// ============================================================================
// isCellRenderable — the shared visibility rule
// ============================================================================
// Extracted from three copies so the item pass, the magic book pass and any
// future mob pass agree. Walls hold nothing; rooms respect the BFS field of
// view; from a corridor you see corridor contents but never a room's interior.
// ============================================================================
TEST(ViewBoundsTest, IsCellRenderableAppliesTheRoomAndCorridorRules) {
  Maze maze(20, 20, 32, 12345);

  // A 3x3 room at (5,5), and a corridor cell away from it.
  for (int y = 5; y < 8; ++y)
    for (int x = 5; x < 8; ++x)
      maze.setCell(x, y, Maze::CELL_ROOM);
  maze.setCell(12, 12, Maze::CELL_CORRIDOR);

  // Walls never hold contents, in either context.
  EXPECT_FALSE(isCellRenderable(maze, 0, 0, AreaState::ROOM));
  EXPECT_FALSE(isCellRenderable(maze, 0, 0, AreaState::CORRIDOR));

  // Standing in the room: its cells are lit by the BFS flood, so they draw.
  maze.updateVisibility(6, 6, AreaState::ROOM);
  EXPECT_TRUE(isCellRenderable(maze, 6, 6, AreaState::ROOM));
  EXPECT_FALSE(isCellRenderable(maze, 12, 12, AreaState::ROOM))
      << "a corridor cell outside the lit room must not draw its contents";

  // Standing in a corridor: corridor contents draw, room interiors do not.
  maze.updateVisibility(12, 12, AreaState::CORRIDOR);
  EXPECT_TRUE(isCellRenderable(maze, 12, 12, AreaState::CORRIDOR));
  EXPECT_FALSE(isCellRenderable(maze, 6, 6, AreaState::CORRIDOR))
      << "room interiors stay hidden while the player is in the corridors";
}

// ============================================================================
// Seed determinism through the full shipping path
// ============================================================================
// "Same seed => same world" is the assumption every scripted-replay test will
// rest on. It has to survive a zone regeneration, which used to seed its RNG
// from the wall clock - so two runs a second apart diverged. The sleep is
// what makes this a real regression test: without it two clock-seeded regens
// inside one second would still agree.
// ============================================================================
namespace {
std::string worldFingerprint(const Maze &maze) {
  std::string out;
  out.reserve((size_t)maze.getWidth() * maze.getHeight() * 2);
  for (int y = 0; y < maze.getHeight(); ++y) {
    for (int x = 0; x < maze.getWidth(); ++x) {
      out.push_back((char)('0' + maze.getCell(x, y)));
      out.push_back((char)('A' + (int)maze.getItem(x, y)));
    }
  }
  return out;
}
} // namespace

TEST(DeterminismTest, SameSeedSurvivesZoneRegeneration) {
  ItemDatabase::init();
  const unsigned int seed = 1788480606u;

  auto run = [&]() {
    UIManager ui(1280, 720);
    DebugOverlay overlay(false);
    ::Run r(seed); // qualified: gtest's Test::Run() shadows it here
    MazeState state(r, ui, overlay);
    state.generateWorld();
    std::string initial = worldFingerprint(state.getMaze());
    state.regenerateTicTacToeZones();
    std::string regen = worldFingerprint(state.getMaze());
    EXPECT_NE(initial, regen) << "regeneration should actually change the world";
    return regen;
  };

  std::string first = run();
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  std::string second = run();

  EXPECT_EQ(first, second)
      << "a zone regeneration must depend only on the seed, not the clock";
}

// ============================================================================
// grid.hpp — the one pixel density
// ============================================================================
// Every destination rectangle is derived from its source, so a sprite that is
// N tiles wide covers N cells and never anything else.
TEST(GridTest, OneTileIsOneCell) {
  Rectangle src = grid::srcTile(3, 6);
  EXPECT_EQ(src.x, 48.0f);
  EXPECT_EQ(src.y, 96.0f);
  Rectangle dest = grid::destFor(src, 0, 0);
  EXPECT_EQ(dest.width, (float)grid::CELL);
  EXPECT_EQ(dest.height, (float)grid::CELL);
}

TEST(GridTest, StandingFurnitureGrowsUpwardFromItsFloorCell) {
  // A 1x2 tile cupboard on cell (5, 7) fills cells (5,6) and (5,7).
  Rectangle cup = grid::standingOn(grid::srcTile(14, 6, 1, 2), 5, 7);
  EXPECT_EQ(cup.x, 5.0f * grid::CELL);
  EXPECT_EQ(cup.y, 6.0f * grid::CELL);
  EXPECT_EQ(cup.width, 1.0f * grid::CELL);
  EXPECT_EQ(cup.height, 2.0f * grid::CELL);

  // A 2-cell-wide table rooted at x spans cells x-1 and x (its root is the
  // right-hand tile - see ItemSpawner), and sits on the floor of row y.
  Rectangle table = grid::standingOn(Rectangle{176, 8, 32, 24}, 10, 4, 2);
  EXPECT_EQ(table.x, 9.0f * grid::CELL);
  EXPECT_EQ(table.width, 2.0f * grid::CELL);
  EXPECT_EQ(table.y + table.height, 5.0f * grid::CELL);
}

// ==== Palette header <-> strip ====
// core/palette.hpp is generated from assets/palette.json; palette_strip.png is
// too. If someone edits the JSON and regenerates one but not the other, the
// UI and the sheets stop agreeing on what a colour is. LoadImage needs no
// window, so the strip is readable here.
#include "core/palette.hpp"

TEST(PaletteHeader, MatchesStrip) {
  Image strip = LoadImage("assets/palette_strip.png");
  ASSERT_EQ(strip.width, pal::totalColours) << "strip and header hold different colour counts";
  ASSERT_EQ(strip.height, 1);
  int i = 0;
  for (int r = 0; r < pal::rampCount; ++r) {
    int steps = 0;
    const Color* ramp = pal::rampByIndex(r, steps);
    for (int s = 0; s < steps; ++s, ++i) {
      Color c = GetImageColor(strip, i, 0);
      EXPECT_EQ(c.r, ramp[s].r) << "ramp " << r << " step " << s;
      EXPECT_EQ(c.g, ramp[s].g) << "ramp " << r << " step " << s;
      EXPECT_EQ(c.b, ramp[s].b) << "ramp " << r << " step " << s;
    }
  }
  UnloadImage(strip);
}
