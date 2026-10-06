#include "world/island.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

Island::Island(uint32_t seed, const IslandConfig &cfg)
    : m_seed(seed), m_cfg(cfg), m_field(seed, cfg),
      m_map(buildIslandMap(m_field, cfg, seed)) {
  chooseSpawn();
}

// Bilinear between the four nearest cell centres, so a distance field's
// contours are smooth curves instead of 8-tile steps.
float Island::distAt(const std::vector<float> &d, float tx, float ty) const {
  const float k = (float)IslandConfig::kCoarse;
  float gx = tx / k - 0.5f, gy = ty / k - 0.5f;
  int x0 = (int)std::floor(gx), y0 = (int)std::floor(gy);
  float fx = gx - x0, fy = gy - y0;
  float a = d[m_map.index(x0, y0)], b = d[m_map.index(x0 + 1, y0)];
  float c = d[m_map.index(x0, y0 + 1)], e = d[m_map.index(x0 + 1, y0 + 1)];
  float top = a + (b - a) * fx, bottom = c + (e - c) * fx;
  return top + (bottom - top) * fy;
}

static float distToSegment(float px, float py, const IslandMap::Segment &s) {
  float vx = s.bx - s.ax, vy = s.by - s.ay;
  float wx = px - s.ax, wy = py - s.ay;
  float len2 = vx * vx + vy * vy;
  float t = len2 > 0.0f ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f)
                        : 0.0f;
  float dx = wx - t * vx, dy = wy - t * vy;
  return std::sqrt(dx * dx + dy * dy);
}

TileSample Island::sample(int x, int y) const {
  const int size = m_cfg.size;
  x = (x % size + size) % size;
  y = (y % size + size) % size;
  const float px = x + 0.5f, py = y + 0.5f;

  TileSample out;
  out.height = m_field.height(px, py);

  const int cx = x / IslandConfig::kCoarse, cy = y / IslandConfig::kCoarse;
  // A smooth field, not "any ocean cell in the 3x3 block": that yes/no answer
  // changed only at coarse-cell borders and cut beaches into 8-tile squares.
  const float oceanDist = distAt(m_map.oceanDist, px, py);

  if (out.height < 0.0f && oceanDist < kOceanReach) {
    out.biome = Biome::OCEAN;
    return out;
  }
  // The water surface sags with distance from each lake cell's centre, so the
  // shoreline closes up smoothly inside the 3x3 search box instead of being
  // cut off square at its edge wherever the fine terrain dips below the spill
  // height.
  const float gx = px / IslandConfig::kCoarse, gy = py / IslandConfig::kCoarse;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int lake = m_map.lake[m_map.index(cx + dx, cy + dy)];
      if (lake < 0)
        continue;
      float ox = gx - (cx + dx + 0.5f), oy = gy - (cy + dy + 0.5f);
      float sag = 0.06f * std::max(0.0f, std::sqrt(ox * ox + oy * oy) - 0.6f);
      if (out.height < m_map.lakeLevel[lake] - sag) {
        out.biome = m_map.lakeSwamp[lake] ? Biome::SWAMP : Biome::LAKE;
        return out;
      }
    }
  }
  if (out.height < 0.0f) {
    out.biome = Biome::WETLAND;
    return out;
  }

  // Coordinates here are unwrapped tile space, which is what the reaches
  // are stored in; rivers never come near the seam. Where reaches overlap (a
  // confluence, a bend) the nearest one decides, so the flow direction
  // changes along the centre line between them rather than mid-bank.
  float wobble = 1.0f + 0.25f * noise::gradient(px / 9.0f, py / 9.0f, m_seed);
  const IslandMap::Segment *reach = nullptr;
  float nearest = 0.0f;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int c = m_map.index(cx + dx, cy + dy);
      for (int i = m_map.segStart[c]; i < m_map.segStart[c + 1]; ++i) {
        const IslandMap::Segment &s = m_map.segments[m_map.segIds[i]];
        float d = distToSegment(px, py, s);
        if (d < s.halfWidth * wobble && (!reach || d < nearest)) {
          reach = &s;
          nearest = d;
        }
      }
    }
  }
  if (reach) {
    if (m_map.swampRiver[reach->cell]) {
      out.biome = Biome::SWAMP; // swamp water lies still
      return out;
    }
    out.biome = Biome::RIVER;
    // The reach's direction rounded to one of 8 steps; atan2's y is down.
    float a = std::atan2(reach->by - reach->ay, reach->bx - reach->ax);
    int octant = (int)std::lround(a / (std::numbers::pi_v<float> / 4.0f));
    out.flow = (uint8_t)(((octant % 8) + 8) % 8 + 1);
    return out;
  }

  out.temperature = m_field.temperature(px, py, out.height);
  // Up to +0.35 at the water's edge, fading over ~3 coarse cells (24 tiles),
  // and a wider, wetter halo round swamp water that grows the wetland.
  out.moisture = m_field.moistureNoise(px, py) +
                 0.35f * std::exp(-distAt(m_map.waterDist, px, py) / 3.0f) +
                 kSwampWet * std::exp(-distAt(m_map.swampDist, px, py) /
                                      kSwampSpread);
  // Beach width wobbles along the coast, measured from ocean cell centres.
  // Two octaves: the bilinear field alone leaves straight runs inside a cell.
  float beachReach =
      kBeachReach + 0.5f * noise::gradient(px / 40.0f, py / 40.0f, m_seed ^ 0xbeac4u) +
      0.25f * noise::gradient(px / 11.0f, py / 11.0f, m_seed ^ 0x5a4d1u);
  out.biome = biome::classifyLand(out.height, out.temperature, out.moisture,
                                  oceanDist < beachReach);
  if (out.biome == Biome::GRASSLAND || out.biome == Biome::FOREST) {
    // The jitter roughens the step contours, which on broad moisture noise
    // would otherwise be long smooth curves.
    float margin = out.moisture - biome::forestMoisture(out.temperature) +
                   0.02f * noise::gradient(px / 6.0f, py / 6.0f, m_seed ^ 0x5ade1u);
    out.shade = out.biome == Biome::GRASSLAND ? (margin > kMeadowMargin ? 1 : 0)
                                              : (margin > kDeepForestMargin ? 3 : 2);
  } else if (out.biome == Biome::SNOW) {
    float drift = noise::gradient(px / 19.0f, py / 13.0f, m_seed ^ 0xd51f7u) +
                  0.4f * noise::gradient(px / 6.0f, py / 5.0f, m_seed ^ 0x2f00du);
    out.shade = drift > 0.12f ? 1 : 0;
  }
  return out;
}

// Rings of coarse cells outward from the centre; the first cell whose centre
// tile qualifies wins. Ring order (Chebyshev) then exact distance inside a
// ring keeps the choice "nearest the centre" and deterministic.
void Island::chooseSpawn() {
  const int n = m_map.n;
  const int mid = n / 2;
  const int k = IslandConfig::kCoarse;
  for (int r = 0; r < n / 2; ++r) {
    int bestD2 = -1, bestX = 0, bestY = 0;
    for (int dy = -r; dy <= r; ++dy) {
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r)
          continue;
        int c = m_map.index(mid + dx, mid + dy);
        if (m_map.waterDist[c] < 2.0f)
          continue; // keep the first steps dry
        int tx = (mid + dx) * k + k / 2, ty = (mid + dy) * k + k / 2;
        TileSample s = sample(tx, ty);
        if (s.biome != Biome::GRASSLAND || s.height < kSpawnMinHeight ||
            s.height > kSpawnMaxHeight)
          continue;
        int d2 = dx * dx + dy * dy;
        if (bestD2 < 0 || d2 < bestD2) {
          bestD2 = d2;
          bestX = tx;
          bestY = ty;
        }
      }
    }
    if (bestD2 >= 0) {
      m_spawnX = bestX;
      m_spawnY = bestY;
      return;
    }
  }
  // No grassland anywhere (not seen in practice): the centre, whatever it is.
  m_spawnX = mid * k + k / 2;
  m_spawnY = mid * k + k / 2;
}
