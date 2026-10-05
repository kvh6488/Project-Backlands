#pragma once

#include <cstdint>
#include <vector>

// ============================================================================
// Poisson-disc sampling (Bridson 2007)
// ============================================================================
// Random points with a guaranteed minimum spacing r - "blue noise". Uniform
// random scatter clumps (trees growing through each other) and leaves bald
// patches; a jittered grid looks planted. Poisson-disc sits between: no two
// points closer than r, no large gaps.
//
// Bridson's algorithm:
//   1. A background grid with cells of side r / sqrt(2), so each cell holds
//      at most one point and a spacing check only looks at the 5x5 cells
//      around a candidate.
//   2. Seed one random point; it goes on the ACTIVE list.
//   3. Pick a random active point and try k candidates in the annulus
//      [r, 2r) around it. The first candidate far enough from everything
//      becomes a new active point; if all k fail, retire the active point.
//   4. Repeat until nothing is active.
// Each point is accepted once and retired once, and each attempt is an O(1)
// grid check, so the whole run is O(n k) for n points - linear in the output.
//
// Deterministic: the only randomness is a 32-bit generator seeded from
// `seed`, drawn as raw bits so no standard-library distribution (whose
// output differs between implementations) is involved.
// ============================================================================
namespace poisson {

struct Point {
  float x, y;
};

// Points in [0, w) x [0, h), pairwise at least r apart.
std::vector<Point> sample(float w, float h, float r, uint32_t seed, int k = 30);

} // namespace poisson
