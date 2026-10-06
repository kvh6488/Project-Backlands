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
//
// Every lookup touches at most nine coarse cells plus their river reaches:
// O(1) per tile.
// ============================================================================
struct TileSample {
  Biome biome = Biome::OCEAN;
  float height = 0.0f;
  float temperature = 0.0f;
  float moisture = 0.0f;
};

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
