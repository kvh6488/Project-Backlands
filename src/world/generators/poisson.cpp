#include "world/generators/poisson.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace poisson {

std::vector<Point> sample(float w, float h, float r, uint32_t seed, int k) {
  std::mt19937 rng(seed);
  auto rand01 = [&]() { return (rng() >> 8) * (1.0f / 16777216.0f); };

  const float cell = r / std::sqrt(2.0f);
  const int gw = (int)std::ceil(w / cell), gh = (int)std::ceil(h / cell);
  std::vector<int> grid(gw * gh, -1); // index into `points`, or -1
  std::vector<Point> points;
  std::vector<int> active;

  auto gridOf = [&](const Point &p) {
    return (int)(p.y / cell) * gw + (int)(p.x / cell);
  };
  auto farEnough = [&](const Point &p) {
    int gx = (int)(p.x / cell), gy = (int)(p.y / cell);
    for (int y = std::max(0, gy - 2); y <= std::min(gh - 1, gy + 2); ++y) {
      for (int x = std::max(0, gx - 2); x <= std::min(gw - 1, gx + 2); ++x) {
        int i = grid[y * gw + x];
        if (i < 0)
          continue;
        float dx = points[i].x - p.x, dy = points[i].y - p.y;
        if (dx * dx + dy * dy < r * r)
          return false;
      }
    }
    return true;
  };
  auto accept = [&](const Point &p) {
    grid[gridOf(p)] = (int)points.size();
    active.push_back((int)points.size());
    points.push_back(p);
  };

  accept({rand01() * w, rand01() * h});
  while (!active.empty()) {
    int slot = (int)(rand01() * active.size());
    const Point origin = points[active[slot]];
    bool placed = false;
    for (int attempt = 0; attempt < k && !placed; ++attempt) {
      // Uniform by area over the annulus, not by radius, so candidates do
      // not crowd its inner edge.
      float angle = rand01() * 6.2831853f;
      float dist = r * std::sqrt(1.0f + 3.0f * rand01());
      Point p{origin.x + std::cos(angle) * dist,
              origin.y + std::sin(angle) * dist};
      if (p.x < 0.0f || p.y < 0.0f || p.x >= w || p.y >= h)
        continue;
      if (farEnough(p)) {
        accept(p);
        placed = true;
      }
    }
    if (!placed) {
      active[slot] = active.back();
      active.pop_back();
    }
  }
  return points;
}

} // namespace poisson
