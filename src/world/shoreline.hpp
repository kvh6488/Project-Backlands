#pragma once

#include "world/island.hpp"
#include <vector>

// ============================================================================
// shoreline::tidy - removes the shore shapes the corner tiles cannot draw
// ============================================================================
// Island::sample classifies each tile alone, so its water edges can leave two
// shapes the dual-grid tiles draw badly:
//   - a land tile with water on 3 or 4 sides, or on two opposite sides: a
//     one-tile nub of bank, or a strip one tile wide (whose end is a nub,
//     and which would otherwise wear away one tile per pass);
//   - water tiles that touch only at a corner (land on the other diagonal of
//     a 2x2 block): the bank tiles meet across the join and cut the channel.
// Each pass reads the grid as it was before the pass (so the order tiles are
// visited in cannot matter) and turns the offending land into water, copying
// the water tile beside it. A pass reads 1 tile around each tile, so a grid
// tidied P times is exact everywhere at least P tiles inside its edge - which
// is how a chunk is built: sampled with a P-tile apron, tidied, apron dropped,
// and every chunk agrees across its borders. A cellular automaton, O(w*h*P).
// ============================================================================
namespace shoreline {

inline constexpr int kPasses = 5;

inline void tidy(std::vector<TileSample> &g, int w, int h, int passes = kPasses) {
  auto wet = [&](const std::vector<TileSample> &s, int x, int y) {
    return isWater(s[y * w + x].biome);
  };
  for (int pass = 0; pass < passes; ++pass) {
    const std::vector<TileSample> before = g;
    auto flood = [&](int x, int y, int fromX, int fromY) {
      TileSample &t = g[y * w + x];
      const TileSample &from = before[fromY * w + fromX];
      t.biome = from.biome;
      t.flow = from.flow;
      t.shade = 0;
    };
    bool changed = false;
    for (int y = 1; y < h - 1; ++y) {
      for (int x = 1; x < w - 1; ++x) {
        if (wet(before, x, y))
          continue;
        // A nub or a strip: take the water of the first wet side.
        constexpr int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        bool side[4];
        int sides = 0, first = -1;
        for (int d = 0; d < 4; ++d)
          if ((side[d] = wet(before, x + dx[d], y + dy[d]))) {
            ++sides;
            if (first < 0)
              first = d;
          }
        if (sides >= 3 || (side[0] && side[2]) || (side[1] && side[3])) {
          flood(x, y, x + dx[first], y + dy[first]);
          changed = true;
          continue;
        }
        // A corner join, opened through its top land tile. This tile is the
        // block's TR, with water TL and BR and land BL ...
        if (wet(before, x - 1, y) && wet(before, x, y + 1) && !wet(before, x - 1, y + 1)) {
          flood(x, y, x - 1, y);
          changed = true;
        } else if (wet(before, x + 1, y) && wet(before, x, y + 1) &&
                   !wet(before, x + 1, y + 1)) {
          // ... or its TL, with water TR and BL and land BR.
          flood(x, y, x + 1, y);
          changed = true;
        }
      }
    }
    if (!changed)
      break;
  }
}

} // namespace shoreline
