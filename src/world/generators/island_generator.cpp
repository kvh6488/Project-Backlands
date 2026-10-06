#include "world/generators/island_generator.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <queue>

namespace {

constexpr int kDX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDY[8] = {0, 0, 1, -1, 1, -1, 1, -1};

// Raise per step of the fill. Small enough to be invisible as terrain, large
// enough to survive float rounding across a few hundred cells of flat.
constexpr float kEpsilon = 1e-5f;

void markOcean(IslandMap &m) {
  std::deque<int> q;
  q.push_back(0); // the (0,0) corner is deep in the open-sea margin
  m.ocean[0] = 1;
  while (!q.empty()) {
    int c = q.front();
    q.pop_front();
    int x = c % m.n, y = c / m.n;
    for (int d = 0; d < 4; ++d) {
      int nb = m.index(x + kDX[d], y + kDY[d]);
      if (!m.ocean[nb] && m.height[nb] < 0.0f) {
        m.ocean[nb] = 1;
        q.push_back(nb);
      }
    }
  }
}

void priorityFlood(IslandMap &m) {
  const int N = m.n * m.n;
  using Entry = std::pair<float, int>; // (level, cell), min-heap on level
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
  std::vector<uint8_t> closed(N, 0);

  for (int c = 0; c < N; ++c) {
    if (m.ocean[c]) {
      m.filled[c] = m.height[c];
      closed[c] = 1;
      open.push({m.filled[c], c});
    }
  }
  while (!open.empty()) {
    int c = open.top().second;
    open.pop();
    int x = c % m.n, y = c / m.n;
    for (int d = 0; d < 8; ++d) {
      int nb = m.index(x + kDX[d], y + kDY[d]);
      if (closed[nb])
        continue;
      closed[nb] = 1;
      m.filled[nb] = std::max(m.height[nb], m.filled[c] + kEpsilon);
      open.push({m.filled[nb], nb});
    }
  }
}

void findLakes(IslandMap &m) {
  const int N = m.n * m.n;
  std::vector<uint8_t> seen(N, 0);
  std::vector<int> component;
  for (int start = 0; start < N; ++start) {
    if (seen[start] || m.ocean[start] ||
        m.filled[start] - m.height[start] < island::kPitDepth)
      continue;
    // Flood one pit (8-connected).
    component.clear();
    std::deque<int> q{start};
    seen[start] = 1;
    while (!q.empty()) {
      int c = q.front();
      q.pop_front();
      component.push_back(c);
      int x = c % m.n, y = c / m.n;
      for (int d = 0; d < 8; ++d) {
        int nb = m.index(x + kDX[d], y + kDY[d]);
        if (!seen[nb] && !m.ocean[nb] &&
            m.filled[nb] - m.height[nb] >= island::kPitDepth) {
          seen[nb] = 1;
          q.push_back(nb);
        }
      }
    }
    if ((int)component.size() < island::kMinLakeCells)
      continue;
    int id = (int)m.lakeLevel.size();
    float level = 0.0f;
    for (int c : component) {
      m.lake[c] = id;
      level = std::max(level, m.filled[c]);
    }
    m.lakeLevel.push_back(level);
    m.lakeInflow.push_back(0);
  }
}

void drain(IslandMap &m) {
  const int N = m.n * m.n;
  for (int c = 0; c < N; ++c) {
    if (m.ocean[c])
      continue;
    int x = c % m.n, y = c / m.n;
    int best = -1;
    float bestLevel = m.filled[c];
    for (int d = 0; d < 8; ++d) {
      int nb = m.index(x + kDX[d], y + kDY[d]);
      if (m.filled[nb] < bestLevel) {
        bestLevel = m.filled[nb];
        best = nb;
      }
    }
    m.receiver[c] = best; // never -1 on land: the fill guarantees a lower neighbour
  }

  // Highest first, so every cell has received all its upstream flow before
  // it passes the total on.
  std::vector<int> order;
  order.reserve(N);
  for (int c = 0; c < N; ++c)
    if (!m.ocean[c])
      order.push_back(c);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return m.filled[a] > m.filled[b]; });
  for (int c : order) {
    m.flow[c] += 1.0f;
    if (m.receiver[c] >= 0)
      m.flow[m.receiver[c]] += m.flow[c];
  }
}

// Where a river passes through a cell: its centre, nudged up to 2 tiles by a
// hash so consecutive reaches bend instead of running on a 45-degree lattice.
void riverNode(const IslandMap &m, int c, uint32_t seed, float &x, float &y) {
  const float k = (float)IslandConfig::kCoarse;
  int cx = c % m.n, cy = c / m.n;
  x = (cx + 0.5f) * k + (noise::unit(cx, cy, seed) - 0.5f) * 4.0f;
  y = (cy + 0.5f) * k + (noise::unit(cx, cy, seed ^ 0x51ed27u) - 0.5f) * 4.0f;
}

void traceRivers(IslandMap &m, uint32_t seed) {
  const int N = m.n * m.n;
  const float k = (float)IslandConfig::kCoarse;
  for (int c = 0; c < N; ++c)
    m.river[c] = !m.ocean[c] && m.lake[c] < 0 && m.flow[c] >= island::kRiverFlow;

  std::vector<std::vector<int>> perCell(N);
  for (int c = 0; c < N; ++c) {
    if (!m.river[c])
      continue;
    int r = m.receiver[c];
    if (m.lake[r] >= 0)
      m.lakeInflow[m.lake[r]]++;

    IslandMap::Segment s;
    riverNode(m, c, seed, s.ax, s.ay);
    // The receiver is a neighbour; place it relative to c rather than by its
    // own index so a reach never jumps the wrap seam.
    int dx = r % m.n - c % m.n, dy = r / m.n - c / m.n;
    if (dx > 1) dx -= m.n; else if (dx < -1) dx += m.n;
    if (dy > 1) dy -= m.n; else if (dy < -1) dy += m.n;
    if (m.river[r]) {
      float rx, ry;
      riverNode(m, r, seed, rx, ry);
      // riverNode gave r's absolute node; re-anchor it next to c.
      s.bx = ((c % m.n) + dx + 0.5f) * k + (rx - ((r % m.n) + 0.5f) * k);
      s.by = ((c / m.n) + dy + 0.5f) * k + (ry - ((r / m.n) + 0.5f) * k);
    } else {
      // Into a lake or the sea: run to the centre of the water cell.
      s.bx = ((c % m.n) + dx + 0.5f) * k;
      s.by = ((c / m.n) + dy + 0.5f) * k;
    }
    s.halfWidth = std::min(
        3.4f, 0.9f + 0.55f * std::log2(m.flow[c] / island::kRiverFlow + 1.0f));
    s.cell = c;
    int id = (int)m.segments.size();
    m.segments.push_back(s);
    perCell[c].push_back(id);
    perCell[r].push_back(id);
  }

  m.segStart.assign(N + 1, 0);
  for (int c = 0; c < N; ++c)
    m.segStart[c + 1] = m.segStart[c] + (int)perCell[c].size();
  m.segIds.reserve(m.segStart[N]);
  for (int c = 0; c < N; ++c)
    m.segIds.insert(m.segIds.end(), perCell[c].begin(), perCell[c].end());
}

// Euclidean, not step-counted: a plain BFS measures Chebyshev distance, whose
// contours are squares, and the moisture bonus then paints square forests.
// Each cell instead inherits its neighbour's nearest source cell and measures
// the true distance to it, re-queueing whenever that improves - nearest-
// source propagation, a close approximation of the exact Euclidean distance
// transform (Danielsson 1980).
template <typename IsSource>
std::vector<float> distanceFrom(const IslandMap &m, IsSource isSource) {
  const int N = m.n * m.n;
  std::vector<float> dist(N, 1e9f);
  std::vector<int> source(N, -1);
  std::deque<int> q;
  for (int c = 0; c < N; ++c) {
    if (isSource(c)) {
      dist[c] = 0.0f;
      source[c] = c;
      q.push_back(c);
    }
  }
  auto torusDist = [&](int a, int b) {
    int dx = std::abs(a % m.n - b % m.n), dy = std::abs(a / m.n - b / m.n);
    dx = std::min(dx, m.n - dx);
    dy = std::min(dy, m.n - dy);
    return std::sqrt((float)(dx * dx + dy * dy));
  };
  while (!q.empty()) {
    int c = q.front();
    q.pop_front();
    int x = c % m.n, y = c / m.n;
    for (int d = 0; d < 8; ++d) {
      int nb = m.index(x + kDX[d], y + kDY[d]);
      float dd = torusDist(nb, source[c]);
      if (dd < dist[nb] - 1e-4f) {
        dist[nb] = dd;
        source[nb] = source[c];
        q.push_back(nb);
      }
    }
  }
  return dist;
}

void chooseSwamps(IslandMap &m, const TerrainField &field, uint32_t seed) {
  const int N = m.n * m.n;
  const int lakes = (int)m.lakeLevel.size();
  const float k = (float)IslandConfig::kCoarse;
  m.lakeSwamp.assign(lakes, 0);
  m.swampRiver.assign(N, 0);
  if (lakes == 0)
    return;

  // Mean temperature over each lake's cells, at its water level.
  std::vector<float> temp(lakes, 0.0f);
  std::vector<int> cells(lakes, 0);
  for (int c = 0; c < N; ++c) {
    int id = m.lake[c];
    if (id < 0)
      continue;
    temp[id] += field.temperature((c % m.n + 0.5f) * k, (c / m.n + 0.5f) * k,
                                  m.lakeLevel[id]);
    cells[id]++;
  }
  int fallback = -1; // the warmest lake, in case no roll succeeds
  for (int id = 0; id < lakes; ++id) {
    temp[id] /= cells[id];
    if (fallback < 0 || temp[id] > temp[fallback])
      fallback = id;
  }
  bool any = false;
  for (int id = 0; id < lakes; ++id) {
    if (temp[id] >= island::kSwampMinTemperature &&
        noise::unit(id, 0, seed) < island::kSwampChance) {
      m.lakeSwamp[id] = 1;
      any = true;
    }
  }
  if (!any)
    m.lakeSwamp[fallback] = 1;

  auto inSwampLake = [&](int c) { return m.lake[c] >= 0 && m.lakeSwamp[m.lake[c]]; };
  for (int c = 0; c < N; ++c) {
    // Upstream: a river cell that reaches a swamp lake within the reach.
    if (m.river[c]) {
      int r = c;
      for (int step = 0; step < island::kSwampRiverReach && r >= 0; ++step) {
        r = m.receiver[r];
        if (r >= 0 && inSwampLake(r)) {
          m.swampRiver[c] = 1;
          break;
        }
        if (r < 0 || !m.river[r])
          break;
      }
    }
    // Downstream: the run of river leaving a swamp lake's outlet.
    if (inSwampLake(c) && m.receiver[c] >= 0 && m.lake[m.receiver[c]] != m.lake[c]) {
      int r = m.receiver[c];
      for (int step = 0; step < island::kSwampRiverReach && r >= 0 && m.river[r];
           ++step) {
        m.swampRiver[r] = 1;
        r = m.receiver[r];
      }
    }
  }
}

} // namespace

IslandMap buildIslandMap(const TerrainField &field, const IslandConfig &cfg,
                         uint32_t seed) {
  IslandMap m;
  m.n = cfg.coarseSize();
  const int N = m.n * m.n;
  m.height.resize(N);
  m.filled.assign(N, 0.0f);
  m.ocean.assign(N, 0);
  m.lake.assign(N, -1);
  m.receiver.assign(N, -1);
  m.flow.assign(N, 0.0f);
  m.river.assign(N, 0);

  const float k = (float)IslandConfig::kCoarse;
  for (int y = 0; y < m.n; ++y)
    for (int x = 0; x < m.n; ++x)
      m.height[y * m.n + x] = field.height((x + 0.5f) * k, (y + 0.5f) * k);

  markOcean(m);
  priorityFlood(m);
  findLakes(m);
  drain(m);
  traceRivers(m, noise::hash(6, 0, seed));
  chooseSwamps(m, field, noise::hash(7, 0, seed));
  m.waterDist = distanceFrom(
      m, [&](int c) { return m.ocean[c] || m.lake[c] >= 0 || m.river[c]; });
  m.oceanDist = distanceFrom(m, [&](int c) { return m.ocean[c] != 0; });
  m.swampDist = distanceFrom(m, [&](int c) {
    return m.swampRiver[c] || (m.lake[c] >= 0 && m.lakeSwamp[m.lake[c]]);
  });
  return m;
}
