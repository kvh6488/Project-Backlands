#pragma once

#include "world/terrain_field.hpp"
#include <vector>

// ============================================================================
// IslandMap — the whole-island passes, on the coarse grid
// ============================================================================
// One coarse cell covers 8x8 tiles (~384x384 cells at the default size).
// Anything that needs to see the whole island at once - which sea a cell
// drains to, where the lakes are, how much land feeds a river - is solved
// here, once, at world creation. Per-tile detail is NOT stored: tiles are
// classified on demand against these arrays (see Island::sample).
//
// All per-cell vectors are n*n, indexed y*n + x, and wrap toroidally like
// every other grid in the game.
// ============================================================================
struct IslandMap {
  int n = 0; // coarse cells per side

  std::vector<float> height;   // terrain height at the cell centre
  std::vector<float> filled;   // height after depression filling (>= height)
  std::vector<uint8_t> ocean;  // below sea and connected to the open sea
  std::vector<int> lake;       // lake id, or -1
  std::vector<int> receiver;   // the downhill neighbour water flows to; -1 in the sea
  std::vector<float> flow;     // cells of land draining through here (incl. itself)
  std::vector<uint8_t> river;
  std::vector<float> waterDist; // coarse cells to the nearest ocean/lake/river

  // Per lake id.
  std::vector<float> lakeLevel; // water surface: the depression's spill height
  std::vector<int> lakeInflow;  // river cells flowing straight in; 0 = a lone lake

  // A river reach: a straight run from one cell's node to its receiver's, in
  // tile coordinates. Indexed per coarse cell in CSR form (segStart/segIds),
  // under BOTH endpoint cells, so a tile only searches its 3x3 neighbourhood.
  struct Segment {
    float ax, ay, bx, by;
    float halfWidth; // tiles
  };
  std::vector<Segment> segments;
  std::vector<int> segStart; // n*n + 1 offsets into segIds
  std::vector<int> segIds;

  int index(int x, int y) const {
    x = (x % n + n) % n;
    y = (y % n + n) % n;
    return y * n + x;
  }
};

// ============================================================================
// buildIslandMap — ocean, lakes and rivers from the height field
// ============================================================================
//   1. Sample   H = height at every cell centre.                       O(N)
//   2. Ocean    BFS from a corner (always open sea) through cells below
//               sea level. Inland cells below sea level are NOT ocean -
//               they are depressions, and become lakes or marsh.       O(N)
//   3. Fill     Priority-flood (Barnes, Lehman & Mulla 2014): grow inward
//               from the ocean through a min-heap, raising each newly
//               reached cell to at least its parent's level + epsilon.
//               Every pit fills to its spill height, and every land cell
//               ends with a strictly lower neighbour, so water always has
//               a way to the sea.                                O(N log N)
//   4. Lakes    Connected cells the fill raised noticeably are filled pits.
//               Pits of at least kMinLakeCells become lakes, their water
//               level the spill height.                                O(N)
//   5. Drain    Each land cell's receiver is its lowest neighbour on the
//               filled surface (D8 steepest descent). Strictly downhill,
//               so the receivers form a forest rooted in the ocean - no
//               cycles, by construction.                               O(N)
//   6. Flow     Visit cells highest-first, adding each one's flow to its
//               receiver: the upstream area of every cell.       O(N log N)
//   7. Rivers   Cells with flow >= kRiverFlow. Flow only grows
//               downstream, so a river's receiver is always another river
//               cell, a lake or the ocean: every river ends in water, and
//               where two meet they merge. A river into a lake continues
//               from the lake's outlet, because the lake's whole inflow
//               leaves through it.                                     O(N)
//   8. Distance Euclidean distance to the nearest water, by nearest-source
//               propagation, for the moisture bonus.               ~O(N)
//
// N = n^2 coarse cells. ~150k at the default size, well under a second.
// ============================================================================
IslandMap buildIslandMap(const TerrainField &field, const IslandConfig &cfg,
                         uint32_t seed);

namespace island {
inline constexpr int kMinLakeCells = 3;
inline constexpr float kRiverFlow = 160.0f; // ~10k tiles of catchment
inline constexpr float kPitDepth = 0.002f;  // fill below this is a flat, not a pit
} // namespace island
