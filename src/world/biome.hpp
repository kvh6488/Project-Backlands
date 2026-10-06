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
//   2. Temperature - snow where it is cold. Temperature falls with height, so
//      peaks get snow with no special case, and Phase 5's seasons move the
//      snow line by shifting temperature alone.
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
  MOUNTAIN,
  SNOW,
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
  case Biome::SNOW: return "SNOW";
  case Biome::COASTAL: return "COASTAL";
  default: return "NONE";
  }
}

namespace biome {

// Height is measured from sea level (0); a typical inland peak is ~1.
inline constexpr float kBeachTop = 0.03f;    // dry sand above the waterline
inline constexpr float kMountainLine = 0.60f; // bare rock above this
inline constexpr float kSnowTemperature = 0.12f;

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
inline Biome classifyLand(float height, float temperature, float moisture,
                          bool nearOcean) {
  if (nearOcean && height < kBeachTop)
    return Biome::BEACH;
  if (temperature < kSnowTemperature)
    return Biome::SNOW;
  if (height > kMountainLine)
    return Biome::MOUNTAIN;
  return whittaker(temperature, moisture);
}

} // namespace biome
