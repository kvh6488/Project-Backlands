#pragma once

#include <cmath>
#include <cstdint>

// ============================================================================
// noise — smooth, seeded randomness for terrain
// ============================================================================
// White noise (a fresh random number per tile) is static; terrain needs
// randomness where nearby points get similar values. Everything here is a
// PURE function of (x, y, seed): no tables, no state, so any tile of any world
// can be evaluated in any order and a seed always rebuilds the same island.
//
// LAYER 1 — hash(x, y, seed): integer lattice point -> 32 random bits.
//   An avalanche mixer (xor-shift / odd-multiply rounds, after Wellons'
//   "lowbias32"): flipping any input bit flips each output bit with ~50%
//   probability, so neighbouring lattice points are uncorrelated. The two
//   coordinates are folded in through separate odd multipliers before
//   mixing, so (x, y) and (y, x) do not collide. The constants are this
//   game's own; changing them re-rolls every world.
//
// LAYER 2 — gradient(x, y): Perlin-style gradient noise.
//   Each lattice corner gets a random unit vector (one of 16, picked by the
//   hash). A point's value is the four corners' dot products with the offset
//   to the point, blended by the quintic fade 6t^5 - 15t^4 + 10t^3, whose
//   first AND second derivatives vanish at the corners - so no visible grid
//   creases. Output is roughly [-1, 1] and exactly 0 on lattice points.
//   Gradient noise rather than value noise (random heights at corners)
//   because value noise shows the lattice as blocky plateaus.
//
// LAYER 3 — fbm: fractal Brownian motion.
//   Sum `octaves` copies, each at double the frequency (lacunarity 2) and half
//   the amplitude (gain 0.5). Low octaves shape continents, high ones roughen
//   coastlines - the 1/f spectrum natural terrain has. Normalised by the
//   amplitude total so the range stays about [-1, 1]. Each octave gets its own
//   seed so they do not line up at the origin.
//
// LAYER 4 — ridged: 1 - |noise|, squared. |noise| folds the zero crossings of
//   gradient noise into sharp creases; inverting makes them ridgelines. Used
//   for mountain ranges, which are spines, not domes.
//
// LAYER 5 — warp: domain warping. Sample fbm at p + k * (fbm(p), fbm(p)) with
//   two decorrelated offsets. Bending the input space by noise turns round
//   blobs into twisted, organic coastlines - the signature look of this
//   island. (Quilez, "domain warping", 2002.)
//
// Cost: gradient() is O(1) - one hash and a dot product per corner. fbm is
// O(octaves).
// ============================================================================
namespace noise {

inline uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

inline uint32_t hash(int x, int y, uint32_t seed) {
  uint32_t h = seed ^ 0x9e3779b9u;
  h = mix(h ^ ((uint32_t)x * 0x85ebca6bu));
  h = mix(h ^ ((uint32_t)y * 0xc2b2ae35u));
  return h;
}

// Uniform in [0, 1), for scatter rolls that need a number, not a field.
inline float unit(int x, int y, uint32_t seed) {
  return (hash(x, y, seed) >> 8) * (1.0f / 16777216.0f);
}

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Dot product of the corner's gradient with the offset (dx, dy). The 16
// directions are evenly spaced round the unit circle.
inline float cornerDot(int ix, int iy, uint32_t seed, float dx, float dy) {
  static constexpr float kDirs[16][2] = {
      {1.0f, 0.0f},       {0.92388f, 0.38268f}, {0.70711f, 0.70711f},
      {0.38268f, 0.92388f}, {0.0f, 1.0f},       {-0.38268f, 0.92388f},
      {-0.70711f, 0.70711f}, {-0.92388f, 0.38268f}, {-1.0f, 0.0f},
      {-0.92388f, -0.38268f}, {-0.70711f, -0.70711f}, {-0.38268f, -0.92388f},
      {0.0f, -1.0f},      {0.38268f, -0.92388f}, {0.70711f, -0.70711f},
      {0.92388f, -0.38268f}};
  const float *g = kDirs[hash(ix, iy, seed) & 15u];
  return g[0] * dx + g[1] * dy;
}

inline float gradient(float x, float y, uint32_t seed) {
  float fx = std::floor(x), fy = std::floor(y);
  int ix = (int)fx, iy = (int)fy;
  float dx = x - fx, dy = y - fy;

  float n00 = cornerDot(ix, iy, seed, dx, dy);
  float n10 = cornerDot(ix + 1, iy, seed, dx - 1.0f, dy);
  float n01 = cornerDot(ix, iy + 1, seed, dx, dy - 1.0f);
  float n11 = cornerDot(ix + 1, iy + 1, seed, dx - 1.0f, dy - 1.0f);

  float u = fade(dx), v = fade(dy);
  float nx0 = n00 + u * (n10 - n00);
  float nx1 = n01 + u * (n11 - n01);
  // 2D gradient noise peaks at sqrt(1/2); scaling by sqrt(2) maps it to ~[-1, 1].
  return 1.41421f * (nx0 + v * (nx1 - nx0));
}

inline float fbm(float x, float y, uint32_t seed, int octaves) {
  float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = 1.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * gradient(x * freq, y * freq, seed + (uint32_t)i * 0x632be5abu);
    norm += amp;
    amp *= 0.5f;
    freq *= 2.0f;
  }
  return sum / norm;
}

// In [0, 1]; 1 on a ridgeline.
inline float ridged(float x, float y, uint32_t seed, int octaves) {
  float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = 1.0f;
  for (int i = 0; i < octaves; ++i) {
    float r = 1.0f - std::fabs(
        gradient(x * freq, y * freq, seed + (uint32_t)i * 0x5bd1e995u));
    sum += amp * r * r;
    norm += amp;
    amp *= 0.5f;
    freq *= 2.0f;
  }
  return sum / norm;
}

// The displacement domain warping applies at (x, y), about [-1, 1] per axis.
// The two fbm calls use far-apart offsets so x and y bend independently.
struct Vec2f {
  float x, y;
};
inline Vec2f warp(float x, float y, uint32_t seed, int octaves) {
  return {fbm(x + 17.3f, y - 41.9f, seed ^ 0xa511e9b3u, octaves),
          fbm(x - 63.1f, y + 28.7f, seed ^ 0x3c6ef372u, octaves)};
}

} // namespace noise
