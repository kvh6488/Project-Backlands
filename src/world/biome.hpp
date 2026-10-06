#pragma once

#include <cstdint>

// ============================================================================
// Biome — what one overworld tile is
// ============================================================================
// The starter set from the roadmap, plus the two kinds of inland water. Water
// is decided first by hydrology (ocean flood, lakes, rivers); only dry land
// reaches the biome rules below.
//
//   1. Height bands - beach at the waterline, mountain above the tree line.
//   2. Temperature - bare ALPINE mountain where it is cold, even below the
//      tree line. Temperature falls with height, so peaks are alpine with no
//      special case. Snow is not a biome: it is seasonal cover laid over the
//      land by Climate (world/climate.hpp), so the ground under it - and every
//      tree on it - stays put all year.
//   3. A Whittaker lookup for the rest: temperature x moisture -> biome. The
//      real Whittaker diagram plots climate zones on exactly these two axes;
//      a small table of it is the standard game shorthand. New biomes (taiga,
//      desert) slot in as new cells, not new branches.
//   4. Coastal scrub - not climate but place: grassland or forest inside a
//      coastal zone (IslandMap::coastalDist) becomes COASTAL. Applied after
//      the lookup, so snow, rock and wetland still win on a chosen shore.
// ============================================================================
enum class Biome : uint8_t {
  OCEAN,
  LAKE,
  RIVER,
  SWAMP, // swamp water: a swamp lake and the rivers near it
  BEACH,
  GRASSLAND,
  FOREST,
  WETLAND,
  MOUNTAIN, // shade 1: alpine, colder than kAlpineTemperature
  COASTAL, // dune scrub behind a sheltered beach: meadow grass, palms, sand
  COUNT
};

inline bool isWater(Biome b) {
  return b == Biome::OCEAN || b == Biome::LAKE || b == Biome::RIVER ||
         b == Biome::SWAMP;
}

inline const char *biomeId(Biome b) {
  switch (b) {
  case Biome::OCEAN: return "OCEAN";
  case Biome::LAKE: return "LAKE";
  case Biome::RIVER: return "RIVER";
  case Biome::SWAMP: return "SWAMP";
  case Biome::BEACH: return "BEACH";
  case Biome::GRASSLAND: return "GRASSLAND";
  case Biome::FOREST: return "FOREST";
  case Biome::WETLAND: return "WETLAND";
  case Biome::MOUNTAIN: return "MOUNTAIN";
  case Biome::COASTAL: return "COASTAL";
  default: return "NONE";
  }
}

namespace biome {

// Height is measured from sea level (0); a typical inland peak is ~1.
inline constexpr float kBeachTop = 0.03f;    // dry sand above the waterline
inline constexpr float kMountainLine = 0.60f; // bare rock above this
// Colder than this is alpine. It is also where the snow lies in late spring
// and early autumn (Climate's shoulder-season snow line).
inline constexpr float kAlpineTemperature = 0.12f;

// Rows: temperature band (cold, temperate, warm). Columns: moisture band
// (dry, moderate, wet, waterlogged).
inline constexpr Biome kWhittaker[3][4] = {
    {Biome::GRASSLAND, Biome::FOREST, Biome::FOREST, Biome::WETLAND},
    {Biome::GRASSLAND, Biome::GRASSLAND, Biome::FOREST, Biome::WETLAND},
    {Biome::GRASSLAND, Biome::GRASSLAND, Biome::FOREST, Biome::WETLAND},
};

// The moisture where land turns from grassland to forest, at a temperature;
// the shade of the ground (Island::sample) fades either side of it.
inline float forestMoisture(float temperature) {
  return temperature < 0.35f ? 0.45f : 0.58f;
}

// Moisture at which land turns to wetland, at any temperature.
inline constexpr float kWetlandMoisture = 0.8f;

inline Biome whittaker(float temperature, float moisture) {
  int t = temperature < 0.35f ? 0 : temperature < 0.6f ? 1 : 2;
  int m = moisture < 0.45f ? 0 : moisture < 0.58f ? 1 : moisture < kWetlandMoisture ? 2 : 3;
  return kWhittaker[t][m];
}

// Dry land only. nearOcean = within beach width of the open sea, so a low
// tile here is a beach rather than a lakeshore.
// Bare rock or alpine cold: mountain country, whether the tile is dry or a
// river running through it.
inline bool mountainous(float height, float temperature) {
  return temperature < kAlpineTemperature || height > kMountainLine;
}

inline Biome classifyLand(float height, float temperature, float moisture,
                          bool nearOcean) {
  if (nearOcean && height < kBeachTop)
    return Biome::BEACH;
  if (mountainous(height, temperature))
    return Biome::MOUNTAIN;
  return whittaker(temperature, moisture);
}

} // namespace biome
