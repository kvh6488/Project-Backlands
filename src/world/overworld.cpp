#include "world/overworld.hpp"
#include "core/grid.hpp"
#include "world/shoreline.hpp"
#include <cstdlib>
#include <deque>

Overworld::Overworld(uint32_t seed, const IslandConfig &cfg)
    : World(cfg.size, cfg.size, grid::CELL), m_island(seed, cfg), m_climate(m_island),
      m_chunksAcross(cfg.chunksAcross()) {}

int Overworld::chunkKey(int cx, int cy) const {
  cx = (cx % m_chunksAcross + m_chunksAcross) % m_chunksAcross;
  cy = (cy % m_chunksAcross + m_chunksAcross) % m_chunksAcross;
  return cy * m_chunksAcross + cx;
}

const Overworld::Chunk &Overworld::chunk(int cx, int cy) const {
  int key = chunkKey(cx, cy);
  auto it = m_chunks.find(key);
  if (it != m_chunks.end())
    return it->second;
  return m_chunks.emplace(key, build(cx, cy)).first->second;
}

// Density rolls per biome: (prop, chance) pairs tried in order against one
// uniform number, so the chances partition [0, 1). The shade steps between
// grassland and forest have their own rows, so the woods thin out gradually
// instead of starting at a wall of trees.
PropType Overworld::rollProp(Biome b, uint8_t shade, int x, int y,
                             uint8_t &variant) const {
  struct Odds {
    PropType type;
    float chance;
  };
  static const std::vector<Odds> kTable[(int)Biome::COUNT] = {
      /* OCEAN     */ {},
      /* LAKE      */ {},
      /* RIVER     */ {},
      /* SWAMP     */ {},
      /* BEACH     */ {{PropType::ROCK, 0.03f}}, // palms only on a dune beach
      /* GRASSLAND */ {{PropType::TREE, 0.07f}, {PropType::BUSH, 0.10f},
                       {PropType::ROCK, 0.02f}},
      /* FOREST    */ {{PropType::TREE, 0.70f}, {PropType::BUSH, 0.10f}},
      /* WETLAND   */ {{PropType::REEDS, 0.28f}, {PropType::TREE, 0.16f},
                       {PropType::BUSH, 0.06f}},
      /* MOUNTAIN  */ {{PropType::ROCK, 0.16f}, {PropType::PINE, 0.08f}},
      // ~65 % of the forest's trees, short ones and palms (spriteFor).
      /* COASTAL   */ {{PropType::TREE, 0.45f}, {PropType::BUSH, 0.12f},
                       {PropType::ROCK, 0.02f}},
  };
  static const std::vector<Odds> kDuneBeach = {{PropType::TREE, 0.08f},
                                               {PropType::ROCK, 0.03f}};
  static const std::vector<Odds> kSandPatch = {{PropType::TREE, 0.12f},
                                               {PropType::BUSH, 0.06f}};
  static const std::vector<Odds> kMeadowEdge = {
      {PropType::TREE, 0.22f}, {PropType::BUSH, 0.12f}, {PropType::ROCK, 0.02f}};
  // Alpine peaks are sparse: a few hardy trees among the boulders.
  static const std::vector<Odds> kAlpine = {{PropType::PINE, 0.04f},
                                            {PropType::ROCK, 0.04f}};
  static const std::vector<Odds> kForestEdge = {{PropType::TREE, 0.45f},
                                                {PropType::BUSH, 0.12f}};
  const std::vector<Odds> *odds = &kTable[(int)b];
  if (b == Biome::GRASSLAND && shade == 1)
    odds = &kMeadowEdge;
  else if (b == Biome::FOREST && shade == 2)
    odds = &kForestEdge;
  else if (b == Biome::BEACH && shade == 1)
    odds = &kDuneBeach;
  else if (b == Biome::COASTAL && shade == 1)
    odds = &kSandPatch;
  else if (b == Biome::MOUNTAIN && shade == 1)
    odds = &kAlpine;
  uint32_t seed = m_island.seed();
  float roll = noise::unit(x, y, seed ^ 0x7a3e11c5u);
  variant = (uint8_t)(noise::hash(x, y, seed ^ 0x1b873593u) & 0xffu);
  for (const Odds &o : *odds) {
    if (roll < o.chance)
      return o.type;
    roll -= o.chance;
  }
  return PropType::NONE;
}

Overworld::Chunk Overworld::build(int cx, int cy) const {
  cx = (cx % m_chunksAcross + m_chunksAcross) % m_chunksAcross;
  cy = (cy % m_chunksAcross + m_chunksAcross) % m_chunksAcross;
  Chunk c;
  const int ox = cx * kChunk, oy = cy * kChunk;
  // Sampled with an apron as wide as the shore tidy reads, so the tidied
  // core matches every neighbour's (shoreline.hpp).
  constexpr int A = shoreline::kPasses, W = kChunk + 2 * A;
  std::vector<TileSample> tiles((size_t)W * W);
  for (int y = 0; y < W; ++y)
    for (int x = 0; x < W; ++x)
      tiles[y * W + x] = m_island.sample(ox + x - A, oy + y - A);
  shoreline::tidy(tiles, W, W);
  for (int y = 0; y < kChunk; ++y) {
    for (int x = 0; x < kChunk; ++x) {
      const TileSample &s = tiles[(y + A) * W + (x + A)];
      c.biome[y * kChunk + x] = s.biome;
      c.height[y * kChunk + x] = s.height;
      c.shade[y * kChunk + x] = s.shade;
      c.flow[y * kChunk + x] = s.flow;
      c.drift[y * kChunk + x] = s.drift;
      c.temperature[y * kChunk + x] = s.temperature;
    }
  }
  c.propIndex.fill(-1);

  // Candidate points for this chunk and its neighbours, the neighbours'
  // shifted into this chunk's frame.
  const int myKey = chunkKey(cx, cy);
  std::vector<poisson::Point> rivals; // points of higher-priority neighbours
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if ((dx == 0 && dy == 0) || chunkKey(cx + dx, cy + dy) > myKey)
        continue;
      for (poisson::Point p : points(cx + dx, cy + dy))
        rivals.push_back({p.x + dx * kChunk, p.y + dy * kChunk});
    }
  }

  const int sx = spawnX(), sy = spawnY();
  for (poisson::Point p : points(cx, cy)) {
    bool yields = false;
    for (const poisson::Point &q : rivals) {
      float ddx = p.x - q.x, ddy = p.y - q.y;
      if (ddx * ddx + ddy * ddy < kPropSpacing * kPropSpacing) {
        yields = true;
        break;
      }
    }
    if (yields)
      continue;

    int lx = (int)p.x, ly = (int)p.y;
    int tx = ox + lx, ty = oy + ly;
    if (std::abs(tx - sx) <= kSpawnClearance &&
        std::abs(ty - sy) <= kSpawnClearance)
      continue;
    uint8_t variant = 0;
    PropType type = rollProp(c.biome[ly * kChunk + lx], c.shade[ly * kChunk + lx],
                             tx, ty, variant);
    if (type == PropType::NONE)
      continue;
    c.propIndex[ly * kChunk + lx] = (int16_t)c.props.size();
    c.props.push_back({tx, ty, ox + p.x, oy + p.y, type, variant});
  }

  applyChanges(cx, cy, c);
  return c;
}

// A chunk's seed is its wrapped position, so every build reads the same points
// for a given neighbour. The reference stays valid until retainAround evicts
// the set: unordered_map never moves an element on insert.
const std::vector<poisson::Point> &Overworld::points(int cx, int cy) const {
  const int key = chunkKey(cx, cy);
  auto it = m_points.find(key);
  if (it != m_points.end())
    return it->second;
  const int wx = key % m_chunksAcross, wy = key / m_chunksAcross;
  return m_points
      .emplace(key, poisson::sample((float)kChunk, (float)kChunk, kPropSpacing,
                                    noise::hash(wx, wy, m_island.seed() ^ 0x2545f491u)))
      .first->second;
}

void Overworld::applyChanges(int cx, int cy, Chunk &c) const {
  if (m_removedProps.empty())
    return;
  const int ox = cx * kChunk, oy = cy * kChunk;
  std::vector<Prop> kept;
  kept.reserve(c.props.size());
  c.propIndex.fill(-1);
  for (const Prop &p : c.props) {
    if (m_removedProps.count(tileIndex(p.x, p.y)))
      continue;
    c.propIndex[(p.y - oy) * kChunk + (p.x - ox)] = (int16_t)kept.size();
    kept.push_back(p);
  }
  c.props.swap(kept);
}

Biome Overworld::biomeAt(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  return c.biome[(y % kChunk) * kChunk + (x % kChunk)];
}

float Overworld::heightAt(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  return c.height[(y % kChunk) * kChunk + (x % kChunk)];
}

uint8_t Overworld::shadeAt(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  return c.shade[(y % kChunk) * kChunk + (x % kChunk)];
}

uint8_t Overworld::flowAt(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  return c.flow[(y % kChunk) * kChunk + (x % kChunk)];
}

uint8_t Overworld::driftAt(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  return c.drift[(y % kChunk) * kChunk + (x % kChunk)];
}

float Overworld::temperatureAt(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  return c.temperature[(y % kChunk) * kChunk + (x % kChunk)];
}

float Overworld::celsiusAt(int x, int y, const Calendar &cal) const {
  return m_climate.celsius(temperatureAt(x, y), cal);
}

bool Overworld::snowAt(int x, int y, const Calendar &cal) const {
  return m_climate.snow(temperatureAt(x, y), biomeAt(x, y), cal.yearDay());
}

bool Overworld::frozenAt(int x, int y, const Calendar &cal) const {
  return m_climate.frozen(temperatureAt(x, y), heightAt(x, y), biomeAt(x, y), cal.yearDay());
}

const Prop *Overworld::findProp(int x, int y) const {
  x = wrapX(x);
  y = wrapY(y);
  const Chunk &c = chunk(x / kChunk, y / kChunk);
  int i = c.propIndex[(y % kChunk) * kChunk + (x % kChunk)];
  return i < 0 ? nullptr : &c.props[i];
}

PropType Overworld::propAt(int x, int y) const {
  const Prop *p = findProp(x, y);
  return p ? p->type : PropType::NONE;
}

const std::vector<Prop> &Overworld::chunkProps(int cx, int cy) const {
  return chunk(cx, cy).props;
}

bool Overworld::removeProp(int x, int y) {
  if (propAt(x, y) == PropType::NONE)
    return false;
  x = wrapX(x);
  y = wrapY(y);
  m_removedProps.insert(tileIndex(x, y));
  // Patch the cached chunk in place rather than rebuilding it.
  int cx = x / kChunk, cy = y / kChunk;
  auto it = m_chunks.find(chunkKey(cx, cy));
  if (it != m_chunks.end())
    applyChanges(cx, cy, it->second);
  return true;
}

void Overworld::retainAround(int x, int y, int radius) {
  const int n = m_chunksAcross;
  const int ccx = ((chunkOf(x) % n) + n) % n, ccy = ((chunkOf(y) % n) + n) % n;
  auto beyond = [&](int key, int r) {
    int dx = std::abs(key % n - ccx), dy = std::abs(key / n - ccy);
    return std::max(std::min(dx, n - dx), std::min(dy, n - dy)) > r;
  };
  std::erase_if(m_chunks, [&](const auto &kv) { return beyond(kv.first, radius); });
  std::erase_if(m_points, [&](const auto &kv) { return beyond(kv.first, radius + 1); });
}

int Overworld::prefetchAround(int x, int y, int radius, int budget) {
  const int cx = chunkOf(x), cy = chunkOf(y);
  int built = 0;
  for (int r = 0; r <= radius && built < budget; ++r)
    for (int dy = -r; dy <= r && built < budget; ++dy)
      for (int dx = -r; dx <= r && built < budget; ++dx)
        if (std::max(std::abs(dx), std::abs(dy)) == r &&
            !m_chunks.count(chunkKey(cx + dx, cy + dy))) {
          chunk(cx + dx, cy + dy);
          ++built;
        }
  return built;
}

bool Overworld::isSolid(int x, int y) const {
  return propIsSolid(propAt(x, y)) || getItem(x, y) != ItemType::NONE;
}

ItemType Overworld::getItem(int x, int y) const {
  auto it = m_items.find(tileIndex(x, y));
  return it == m_items.end() ? ItemType::NONE : it->second;
}

void Overworld::setItem(int x, int y, ItemType type) {
  if (type == ItemType::NONE) {
    m_items.erase(tileIndex(x, y));
    m_itemStates.erase(tileIndex(x, y));
  } else {
    m_items[tileIndex(x, y)] = type;
  }
}

int Overworld::getItemState(int x, int y) const {
  auto it = m_itemStates.find(tileIndex(x, y));
  return it == m_itemStates.end() ? 0 : it->second;
}

// Breadth-first over walkable dry tiles, so a dropped item lands on the
// player's side of a tree rather than through it.
bool Overworld::findNearestEmptyItemCell(int startX, int startY, int maxRadius,
                                         int &outX, int &outY) const {
  struct Node {
    int x, y, depth;
  };
  std::deque<Node> q{{startX, startY, 0}};
  std::unordered_set<int> seen{tileIndex(startX, startY)};
  const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
  while (!q.empty()) {
    Node n = q.front();
    q.pop_front();
    if (!isSolid(n.x, n.y) && !isWater(biomeAt(n.x, n.y))) {
      outX = n.x;
      outY = n.y;
      return true;
    }
    if (n.depth == maxRadius)
      continue;
    for (int d = 0; d < 4; ++d) {
      int nx = n.x + dx[d], ny = n.y + dy[d];
      if (seen.insert(tileIndex(nx, ny)).second && !propIsSolid(propAt(nx, ny)))
        q.push_back({nx, ny, n.depth + 1});
    }
  }
  return false;
}
