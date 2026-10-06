#pragma once

#include "world/biome.hpp"
#include "world/generators/island_generator.hpp"
#include "world/terrain_field.hpp"

// ============================================================================
// Island — one seed's terrain: the pure height field plus its hydrology
// ============================================================================
// Answers "what is tile (x, y)?" for any tile, in any order, without storing
// tiles. sample() combines the per-tile height with the coarse IslandMap:
//
//   ocean   below sea level, within kOceanReach of open sea (a smooth,
//           bilinear distance field, so the edge is not 8x8 blocky)
//   lake    below the spill level of a lake in the 3x3 neighbourhood - the
//           fine height decides the shoreline, so it is not 8x8 blocky
//   marsh   below sea level but neither: a hollow too small to be a lake,
//           which reads as wetland
//   river   within a reach's half-width (wobbled by noise, so banks are not
//           ruler-straight)
//   swamp   a lake or river the IslandMap marked as swamp water
//   land    Biome rules (biome.hpp), with moisture raised near water and
//           raised further, over a wider halo, near swamp water; beach only
//           within a wobbling reach of the ocean
//   shade   how far moisture is past the forest line (grassland and forest),
//           or a drift field (snow), cut into steps - so the ground can
//           fade across a border the biome itself switches at
//   flow    river water's downstream direction: its nearest reach's, which
//           runs from the upstream cell's node to its receiver's
//
// Every lookup touches at most nine coarse cells plus their river reaches:
// O(1) per tile.
// ============================================================================
struct TileSample {
  Biome biome = Biome::OCEAN;
  float height = 0.0f;
  float temperature = 0.0f;
  float moisture = 0.0f;
  // A step inside the biome, for the renderer's fades and the prop density:
  // grassland 0 open / 1 meadow edge; forest 2 edge / 3 deep; snow 0 / 1
  // drift. 0 elsewhere.
  uint8_t shade = 0;
  // River water's downstream direction (flowStep), 0 for still water and land.
  uint8_t flow = 0;
};

// A flow byte as a unit step: 1 = east, then clockwise in screen space
// (y down) to 8 = north-east. 0 is still: (0, 0).
inline void flowStep(uint8_t flow, int &dx, int &dy) {
  static constexpr int kDx[9] = {0, 1, 1, 0, -1, -1, -1, 0, 1};
  static constexpr int kDy[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
  dx = kDx[flow < 9 ? flow : 0];
  dy = kDy[flow < 9 ? flow : 0];
}

class Island {
public:
  Island(uint32_t seed, const IslandConfig &cfg = IslandConfig{});

  // x, y in tiles; wrapped onto the world first.
  TileSample sample(int x, int y) const;

  const IslandConfig &config() const { return m_cfg; }
  const IslandMap &map() const { return m_map; }
  const TerrainField &field() const { return m_field; }
  uint32_t seed() const { return m_seed; }

  // Spawn tile: grassland in the mid-height band between the coast and the
  // mountains, away from water, nearest the island centre.
  int spawnX() const { return m_spawnX; }
  int spawnY() const { return m_spawnY; }

  // Height band a spawn may sit in.
  static constexpr float kSpawnMinHeight = 0.08f;
  static constexpr float kSpawnMaxHeight = 0.45f;

  // Distances in coarse cells, from cell centres.
  static constexpr float kOceanReach = 1.5f; // below sea level counts as sea
  static constexpr float kBeachReach = 1.5f; // +-0.5 by noise: ~4-12 tiles of sand
  static constexpr float kSwampWet = 0.55f;    // moisture added at swamp water
  static constexpr float kSwampSpread = 4.0f;  // e-folding distance (~32 tiles)
  // Moisture past the forest line where the shade steps (jittered per tile).
  static constexpr float kMeadowMargin = -0.04f;    // grassland 0 -> 1
  static constexpr float kDeepForestMargin = 0.05f; // forest 2 -> 3

private:
  void chooseSpawn();
  // A coarse distance field read at tile (tx, ty), bilinear, in coarse cells.
  float distAt(const std::vector<float> &field, float tx, float ty) const;

  uint32_t m_seed;
  IslandConfig m_cfg;
  TerrainField m_field;
  IslandMap m_map;
  int m_spawnX = 0, m_spawnY = 0;
};
