#include "world/overworld.hpp"
#include "core/grid.hpp"
#include "world/generators/poisson.hpp"
#include <cstdlib>
#include <deque>

Overworld::Overworld(uint32_t seed, const IslandConfig &cfg)
    : World(cfg.size, cfg.size, grid::CELL), m_island(seed, cfg),
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
      /* BEACH     */ {{PropType::TREE, 0.04f}, {PropType::ROCK, 0.03f}},
      /* GRASSLAND */ {{PropType::TREE, 0.07f}, {PropType::BUSH, 0.10f},
                       {PropType::ROCK, 0.02f}},
      /* FOREST    */ {{PropType::TREE, 0.70f}, {PropType::BUSH, 0.10f}},
      /* WETLAND   */ {{PropType::REEDS, 0.28f}, {PropType::TREE, 0.16f},
                       {PropType::BUSH, 0.06f}},
      /* MOUNTAIN  */ {{PropType::ROCK, 0.16f}, {PropType::PINE, 0.08f}},
      /* SNOW      */ {{PropType::PINE, 0.04f}, {PropType::ROCK, 0.04f}},
  };
  static const std::vector<Odds> kMeadowEdge = {
      {PropType::TREE, 0.22f}, {PropType::BUSH, 0.12f}, {PropType::ROCK, 0.02f}};
  static const std::vector<Odds> kForestEdge = {{PropType::TREE, 0.45f},
                                                {PropType::BUSH, 0.12f}};
  const std::vector<Odds> *odds = &kTable[(int)b];
  if (b == Biome::GRASSLAND && shade == 1)
    odds = &kMeadowEdge;
  else if (b == Biome::FOREST && shade == 2)
    odds = &kForestEdge;
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
  for (int y = 0; y < kChunk; ++y) {
    for (int x = 0; x < kChunk; ++x) {
      TileSample s = m_island.sample(ox + x, oy + y);
      c.biome[y * kChunk + x] = s.biome;
      c.height[y * kChunk + x] = s.height;
      c.shade[y * kChunk + x] = s.shade;
      c.flow[y * kChunk + x] = s.flow;
    }
  }
  c.propIndex.fill(-1);

  // Candidate points for this chunk and its neighbours, the neighbours'
  // shifted into this chunk's frame. A chunk's seed is its wrapped key, so
  // every chunk regenerates the same points for a given neighbour.
  const uint32_t seed = m_island.seed();
  auto candidates = [&](int ncx, int ncy) {
    return poisson::sample((float)kChunk, (float)kChunk, kPropSpacing,
                           noise::hash(ncx, ncy, seed ^ 0x2545f491u));
  };
  const int myKey = chunkKey(cx, cy);
  std::vector<poisson::Point> rivals; // points of higher-priority neighbours
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if ((dx == 0 && dy == 0) || chunkKey(cx + dx, cy + dy) > myKey)
        continue;
      int nx = (cx + dx + m_chunksAcross) % m_chunksAcross;
      int ny = (cy + dy + m_chunksAcross) % m_chunksAcross;
      for (poisson::Point p : candidates(nx, ny))
        rivals.push_back({p.x + dx * kChunk, p.y + dy * kChunk});
    }
  }

  const int sx = spawnX(), sy = spawnY();
  for (poisson::Point p : candidates(cx, cy)) {
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
  for (auto it = m_chunks.begin(); it != m_chunks.end();) {
    int kx = it->first % n, ky = it->first / n;
    int dx = std::abs(kx - ccx), dy = std::abs(ky - ccy);
    dx = std::min(dx, n - dx);
    dy = std::min(dy, n - dy);
    if (std::max(dx, dy) > radius)
      it = m_chunks.erase(it);
    else
      ++it;
  }
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
