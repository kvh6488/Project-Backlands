#pragma once

#include "world/noise.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// ============================================================================
// IslandConfig — the overworld's compile-time shape
// ============================================================================
// Not live settings: the island is built once, before the player loads in.
// `size` is the only number a test may shrink; everything else is fixed.
// ============================================================================
struct IslandConfig {
  static constexpr int kChunk = 32; // tiles per chunk side
  static constexpr int kCoarse = 8; // tiles per coarse cell side

  // Tiles across the whole wrapping world (square). The island fits inside
  // a circle of diameter size * kIslandFraction, leaving open ocean on every
  // side: at 3072 that is a 2048-tile island with 512 tiles of sea to the
  // wrap seam, so no generator has to be seamless across it.
  int size = 3072;
  static constexpr float kIslandFraction = 2.0f / 3.0f;

  int radius() const { return (int)(size * kIslandFraction / 2.0f); }
  int coarseSize() const { return size / kCoarse; }
  int chunksAcross() const { return size / kChunk; }
};

// ============================================================================
// TerrainField — height, temperature and moisture as pure functions
// ============================================================================
// HEIGHT IS THE LAND SCORE: noise minus a falloff from the island centre.
// Land is where it clears sea level (0); the coast is where it crosses.
//
//   d       distance from the centre in island radii, with the input space
//           domain-warped first so the coast is ragged rather than a circle
//   base    0.30 - 0.42 d^2: a dome that sinks under the sea near d ~ 0.85
//   hills   fbm, +-0.28: the bumps that give rivers somewhere to run and
//           lakes somewhere to pool
//   ridges  ridged noise, faded out toward the coast: the mountain spines
//   edge    a steep cliff in UNWARPED distance past 0.92 radii, the hard
//           guarantee that nothing - not even a warped islet - reaches the
//           open-ocean margin around the wrap seam
//
// Never stored per tile: the whole-island passes sample it on a coarse grid,
// and chunks evaluate it per tile on demand. Islets and outlying islands fall
// out of the same sum wherever hills poke above the falloff.
//
// Temperature = base - height + noise (higher is colder). Moisture here is
// only the noise part; the near-water bonus needs hydrology and is added by
// the Overworld.
// ============================================================================
class TerrainField {
public:
  TerrainField(uint32_t seed, const IslandConfig &cfg)
      : m_centre(cfg.size / 2.0f), m_radius((float)cfg.radius()),
        m_coastSeed(noise::hash(1, 0, seed)), m_hillSeed(noise::hash(2, 0, seed)),
        m_ridgeSeed(noise::hash(3, 0, seed)), m_tempSeed(noise::hash(4, 0, seed)),
        m_moistSeed(noise::hash(5, 0, seed)) {}

  float height(float x, float y) const {
    // Wavelengths in tiles. The warp is broad so it bends whole coastlines.
    noise::Vec2f w = noise::warp(x / 700.0f, y / 700.0f, m_coastSeed, 3);
    float ux = (x - m_centre) / m_radius, uy = (y - m_centre) / m_radius;
    float nx = ux + 0.35f * w.x, ny = uy + 0.35f * w.y;
    float d = std::sqrt(nx * nx + ny * ny);

    float hills = noise::fbm(x / 300.0f, y / 300.0f, m_hillSeed, 6);
    float ridge = noise::ridged(x / 380.0f + w.x * 0.6f, y / 380.0f + w.y * 0.6f,
                                m_ridgeSeed, 5);
    float inland = std::clamp(1.0f - d / 0.8f, 0.0f, 1.0f);

    float h = 0.30f - 0.42f * d * d + 0.28f * hills +
              1.0f * ridge * inland * std::sqrt(inland);

    float dTrue = std::sqrt(ux * ux + uy * uy);
    h -= 12.0f * std::max(0.0f, dTrue - 0.92f);
    return h;
  }

  // ~[0, 1]; 1 is hot. Falls with height, so peaks are cold.
  float temperature(float x, float y, float height) const {
    return 0.85f - 0.8f * std::max(0.0f, height) +
           0.12f * noise::fbm(x / 700.0f, y / 700.0f, m_tempSeed, 2);
  }

  // The noise part of moisture, ~[0.2, 0.8].
  float moistureNoise(float x, float y) const {
    return 0.5f + 0.3f * noise::fbm(x / 500.0f, y / 500.0f, m_moistSeed, 3);
  }

private:
  float m_centre;
  float m_radius;
  uint32_t m_coastSeed, m_hillSeed, m_ridgeSeed, m_tempSeed, m_moistSeed;
};
