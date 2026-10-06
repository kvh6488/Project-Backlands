#pragma once

#include "world/biome.hpp"
#include "world/calendar.hpp"
#include <array>
#include <vector>

class Island;

// ============================================================================
// Climate — temperature in degrees C, and where the snow lies, on any day
// ============================================================================
// THE MODEL. Every tile has a fixed base temperature t0 (TerrainField: falls
// with height, plus broad noise). The season slides ONE number, the snow
// line z(yearDay), along that scale:
//
//   celsius = kDegreesPerUnit * (t0 - z(yearDay))  +  a daily swing
//   snow    = the tile takes snow (takesSnow)  and  t0 < z(yearDay)
//
// So 0 C sits exactly on the snow line, and a whole season is one shift of
// the climate - nothing per tile changes, and generation never reads the date.
// Snow follows the daily mean (the swing is left out), so it creeps with the
// season rather than flickering with the night. Water under the line freezes
// (canFreeze): lakes anywhere, rivers only in the mountains.
//
// CALIBRATION. The design targets are areas, not temperatures - "70 % of the
// mountains snowed at the end of autumn", "the coldest third of the island in
// midwinter". So the snow line's key values are read off THIS island's own
// distribution of t0, sampled on a grid once at build: a quantile of a sorted
// sample (the inverse of its empirical CDF). Every seed then hits the targets,
// where a fixed line would over-snow a mountainous island and miss a flat one.
//
//   yearDay   snow line z                    reads as
//   0 / 60    70 % of mountain t0            start of spring / end of autumn
//   16 / 44   biome::kAlpineTemperature      late spring / early autumn: the
//                                            generated map (the alpine peaks)
//   20 / 40   just under the coldest land    summer: no snow anywhere
//   30        lower still                    midsummer: the warmest day
//   70        kWinterLandShare of land t0    midwinter: the island's cold heart
//
// INTERPOLATION. Between knots z follows a periodic PCHIP curve (piecewise
// cubic Hermite, Fritsch-Butland slopes): smooth, and MONOTONE between knots,
// so the line never overshoots a target - a plain cubic spline would swing
// past "no snow" in summer and drop a dusting on the peaks.
//
// Cost: O(n log n) once (sort), O(1) per query (8 knots), and a coverage
// share is a binary search over the sorted sample.
// ============================================================================
class Climate {
public:
  explicit Climate(const Island &island);

  // Degrees per unit of t0. Coast to peak is ~0.8 units, so ~20 C apart.
  static constexpr float kDegreesPerUnit = 25.0f;
  // Daily swing either side of the mean; warmest at 15:00, coldest at 03:00.
  static constexpr float kDiurnalSwing = 4.0f;
  // Share of the island's land colder than the midwinter snow line.
  static constexpr float kWinterLandShare = 0.32f;
  // Share of the mountains under snow at the end of autumn / start of spring.
  static constexpr float kShoulderMountainShare = 0.70f;
  // How far past "no snow" the line drops by midsummer, in t0 units.
  static constexpr float kMidsummerHeat = 0.14f;
  // Sample spacing for the calibration, in tiles (every other coarse cell).
  static constexpr int kSampleStep = 16;

  // Only grass, forest and bare mountain hold snow: wetland, beach and
  // coastal sand never do, whatever the temperature.
  static bool takesSnow(Biome b) {
    return b == Biome::GRASSLAND || b == Biome::FOREST || b == Biome::MOUNTAIN;
  }

  // The snow line, in t0 units: land colder than this is snowed.
  float snowLine(double yearDay) const;
  float meanCelsius(float t0, double yearDay) const {
    return kDegreesPerUnit * (t0 - snowLine(yearDay));
  }
  // What a thermometer reads now, the daily swing included.
  float celsius(float t0, const Calendar &cal) const;
  bool snow(float t0, Biome b, double yearDay) const {
    return takesSnow(b) && t0 < snowLine(yearDay);
  }

  // Lakes freeze; rivers keep running except in the mountains (moving water
  // freezes last); swamps and the sea never do.
  static bool canFreeze(Biome b, float height, float t0) {
    return b == Biome::LAKE || (b == Biome::RIVER && biome::mountainous(height, t0));
  }
  // Ice follows the snow line: water freezes where snow would lie.
  bool frozen(float t0, float height, Biome b, double yearDay) const {
    return canFreeze(b, height, t0) && t0 < snowLine(yearDay);
  }

  // Coverage, over the calibration sample: the share of all dry land, and of
  // the mountains, under snow on `yearDay`.
  float landSnowShare(double yearDay) const;
  float mountainSnowShare(double yearDay) const;

  struct Knot {
    float day, z;
  };
  static constexpr int kKnots = 8;
  const std::array<Knot, kKnots> &knots() const { return m_knots; }

private:
  void buildCurve();

  std::array<Knot, kKnots> m_knots{};
  std::array<float, kKnots> m_slope{}; // dz/dday at each knot
  // Sorted t0 of the sampled tiles: those that take snow, and the mountains.
  std::vector<float> m_snowable, m_mountain;
  int m_landCount = 0;
};
