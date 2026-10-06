#include "world/climate.hpp"
#include "world/island.hpp"
#include <algorithm>
#include <cmath>

namespace {

// The value below which `share` of a sorted sample lies.
float quantile(const std::vector<float> &sorted, float share) {
  if (sorted.empty())
    return 0.0f;
  size_t i = (size_t)std::clamp(share * (float)sorted.size(), 0.0f,
                                (float)(sorted.size() - 1));
  return sorted[i];
}

// Share of a sorted sample below `z`.
float shareBelow(const std::vector<float> &sorted, float z, int outOf) {
  if (outOf == 0)
    return 0.0f;
  auto it = std::lower_bound(sorted.begin(), sorted.end(), z);
  return (float)(it - sorted.begin()) / (float)outOf;
}

} // namespace

Climate::Climate(const Island &island) {
  const int size = island.config().size;
  std::vector<float> land;
  for (int y = kSampleStep / 2; y < size; y += kSampleStep) {
    for (int x = kSampleStep / 2; x < size; x += kSampleStep) {
      const TileSample s = island.sample(x, y);
      if (isWater(s.biome))
        continue;
      land.push_back(s.temperature);
      if (takesSnow(s.biome))
        m_snowable.push_back(s.temperature);
      if (s.biome == Biome::MOUNTAIN)
        m_mountain.push_back(s.temperature);
    }
  }
  m_landCount = (int)land.size();
  std::sort(land.begin(), land.end());
  std::sort(m_snowable.begin(), m_snowable.end());
  std::sort(m_mountain.begin(), m_mountain.end());

  // The anchors, forced into order so the curve's shape survives an odd seed
  // (a flat island with no alpine peaks, a tiny mountain range).
  constexpr float kGap = 0.02f;
  const float alpine = biome::kAlpineTemperature;
  const float shoulder = std::max(quantile(m_mountain, kShoulderMountainShare), alpine + kGap);
  const float winter = std::max(quantile(land, kWinterLandShare), shoulder + kGap);
  const float summer = std::min(land.empty() ? 0.0f : land.front() - kGap, alpine - kGap);
  const float midsummer = summer - kMidsummerHeat;
  m_knots = {{{0.0f, shoulder},
              {16.0f, alpine},
              {20.0f, summer},
              {30.0f, midsummer},
              {40.0f, summer},
              {44.0f, alpine},
              {60.0f, shoulder},
              {70.0f, winter}}};
  buildCurve();
}

// Fritsch-Butland slopes: zero at a knot where the curve turns (a peak or a
// trough), else a weighted harmonic mean of the two secants - which never
// exceeds 3x either one, the bound that keeps each cubic piece monotone.
void Climate::buildCurve() {
  const float year = (float)Calendar::kDaysPerYear;
  auto span = [&](int k) { // knot k to k+1, wrapping
    float h = m_knots[(k + 1) % kKnots].day - m_knots[k].day;
    return h > 0.0f ? h : h + year;
  };
  auto secant = [&](int k) {
    return (m_knots[(k + 1) % kKnots].z - m_knots[k].z) / span(k);
  };
  for (int i = 0; i < kKnots; ++i) {
    const int prev = (i + kKnots - 1) % kKnots;
    const float d0 = secant(prev), d1 = secant(i);
    if (d0 * d1 <= 0.0f) {
      m_slope[i] = 0.0f;
      continue;
    }
    const float h0 = span(prev), h1 = span(i);
    const float w0 = 2.0f * h1 + h0, w1 = h1 + 2.0f * h0;
    m_slope[i] = (w0 + w1) / (w0 / d0 + w1 / d1);
  }
}

float Climate::snowLine(double yearDay) const {
  const float year = (float)Calendar::kDaysPerYear;
  float d = std::fmod((float)yearDay, year);
  if (d < 0.0f)
    d += year;
  int k = kKnots - 1;
  while (k > 0 && d < m_knots[k].day)
    --k;
  const int next = (k + 1) % kKnots;
  float h = m_knots[next].day - m_knots[k].day;
  if (h <= 0.0f)
    h += year;
  const float t = (d - m_knots[k].day) / h;
  // Cubic Hermite basis on [0, 1].
  const float t2 = t * t, t3 = t2 * t;
  const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
  const float h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
  return h00 * m_knots[k].z + h10 * h * m_slope[k] + h01 * m_knots[next].z +
         h11 * h * m_slope[next];
}

float Climate::celsius(float t0, const Calendar &cal) const {
  constexpr float kTwoPi = 6.2831853f;
  const float swing = kDiurnalSwing * std::cos(kTwoPi * (cal.timeOfDay() - 15.0f) / 24.0f);
  return meanCelsius(t0, cal.yearDay()) + swing;
}

float Climate::landSnowShare(double yearDay) const {
  return shareBelow(m_snowable, snowLine(yearDay), m_landCount);
}

float Climate::mountainSnowShare(double yearDay) const {
  return shareBelow(m_mountain, snowLine(yearDay), (int)m_mountain.size());
}
