#include "entities/player.hpp"
#include "items/item_database.hpp"
#include "render/overworld_renderer.hpp"
#include "render/player_renderer.hpp"
#include "render/step_effects.hpp"
#include "world/generators/poisson.hpp"
#include "world/noise.hpp"
#include "world/overworld.hpp"
#include "world/shoreline.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <memory>

// Island generation costs ~0.5 s in a Debug build, so the suites share one
// world per seed rather than building it per test.
namespace {
constexpr uint32_t kSeed = 1;

const Overworld &sharedWorld() {
  static std::unique_ptr<Overworld> world = std::make_unique<Overworld>(kSeed);
  return *world;
}
} // namespace

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------
TEST(NoiseTest, IsAPureFunctionOfItsInputs) {
  EXPECT_EQ(noise::gradient(12.3f, -4.5f, 7), noise::gradient(12.3f, -4.5f, 7));
  EXPECT_NE(noise::gradient(12.3f, -4.5f, 7), noise::gradient(12.3f, -4.5f, 8));
  EXPECT_EQ(noise::hash(3, 4, 9), noise::hash(3, 4, 9));
  EXPECT_NE(noise::hash(3, 4, 9), noise::hash(4, 3, 9)); // axes not symmetric
}

TEST(NoiseTest, GradientNoiseIsZeroOnTheLatticeAndBounded) {
  for (int i = -20; i < 20; ++i)
    EXPECT_FLOAT_EQ(noise::gradient((float)i, (float)(i * 3), 5), 0.0f);
  float lo = 0.0f, hi = 0.0f;
  for (int y = 0; y < 200; ++y) {
    for (int x = 0; x < 200; ++x) {
      float v = noise::fbm(x * 0.137f, y * 0.071f, 11, 5);
      lo = std::min(lo, v);
      hi = std::max(hi, v);
      float r = noise::ridged(x * 0.137f, y * 0.071f, 11, 4);
      ASSERT_GE(r, 0.0f);
      ASSERT_LE(r, 1.0f);
    }
  }
  EXPECT_GE(lo, -1.05f);
  EXPECT_LE(hi, 1.05f);
  EXPECT_LT(lo, -0.3f) << "fbm should actually vary";
  EXPECT_GT(hi, 0.3f);
}

// ---------------------------------------------------------------------------
// Poisson-disc sampling
// ---------------------------------------------------------------------------
TEST(PoissonTest, KeepsMinimumSpacingAndStaysInBounds) {
  const float r = 2.0f;
  auto pts = poisson::sample(32.0f, 32.0f, r, 1234);
  ASSERT_GT(pts.size(), 100u) << "a 32x32 square at r=2 holds ~150 points";
  for (size_t i = 0; i < pts.size(); ++i) {
    EXPECT_GE(pts[i].x, 0.0f);
    EXPECT_LT(pts[i].x, 32.0f);
    for (size_t j = i + 1; j < pts.size(); ++j) {
      float dx = pts[i].x - pts[j].x, dy = pts[i].y - pts[j].y;
      ASSERT_GE(dx * dx + dy * dy, r * r);
    }
  }
  auto again = poisson::sample(32.0f, 32.0f, r, 1234);
  ASSERT_EQ(pts.size(), again.size());
  EXPECT_EQ(pts.back().x, again.back().x);
}

// ---------------------------------------------------------------------------
// Island: shape, spawn, hydrology
// ---------------------------------------------------------------------------
TEST(IslandTest, SameSeedSameIsland) {
  const Island &a = sharedWorld().island();
  Island b(kSeed);
  EXPECT_EQ(a.map().height, b.map().height);
  EXPECT_EQ(a.map().lake, b.map().lake);
  EXPECT_EQ(a.map().river, b.map().river);
  EXPECT_EQ(a.spawnX(), b.spawnX());
  EXPECT_EQ(a.spawnY(), b.spawnY());
  for (int i = 0; i < 200; ++i) {
    int x = 1000 + i * 7, y = 1500 - i * 3;
    EXPECT_EQ(a.sample(x, y).biome, b.sample(x, y).biome);
  }

  Island other(kSeed + 1);
  EXPECT_NE(a.map().height, other.map().height);
}

TEST(IslandTest, SpawnIsInlandMidHeightAndDry) {
  const Island &isl = sharedWorld().island();
  TileSample s = isl.sample(isl.spawnX(), isl.spawnY());
  EXPECT_EQ(s.biome, Biome::GRASSLAND);
  EXPECT_GE(s.height, Island::kSpawnMinHeight);
  EXPECT_LE(s.height, Island::kSpawnMaxHeight);
  // No water within a few tiles of the first step.
  for (int dy = -3; dy <= 3; ++dy)
    for (int dx = -3; dx <= 3; ++dx)
      EXPECT_FALSE(isWater(isl.sample(isl.spawnX() + dx, isl.spawnY() + dy).biome));
}

TEST(IslandTest, EveryRiverEndsInALakeOrTheOcean) {
  const IslandMap &m = sharedWorld().island().map();
  const int N = m.n * m.n;
  int rivers = 0;
  for (int c = 0; c < N; ++c) {
    if (!m.river[c])
      continue;
    ++rivers;
    // A river's next cell is more river, a lake or the sea - never dry land.
    int r = m.receiver[c];
    ASSERT_GE(r, 0);
    EXPECT_TRUE(m.river[r] || m.lake[r] >= 0 || m.ocean[r]);
    // And following it downstream always terminates in water.
    int cur = c, steps = 0;
    while (m.river[cur] && steps < N) {
      cur = m.receiver[cur];
      ++steps;
    }
    ASSERT_LT(steps, N) << "drainage has a cycle";
    EXPECT_TRUE(m.lake[cur] >= 0 || m.ocean[cur]);
  }
  EXPECT_GT(rivers, 50) << "the island should have a river network";
}

TEST(IslandTest, EveryReachIsWaterEndToEnd) {
  // Every reach - including a lake's outlet - is water along its whole
  // centre line once tidied, so rivers meet lakes, the sea and each other
  // with no strip of land between.
  const Overworld &w = sharedWorld();
  int outlets = 0;
  for (const IslandMap::Segment &s : w.island().map().segments) {
    outlets += w.island().map().lake[w.island().map().index(
                   (int)s.ax / IslandConfig::kCoarse, (int)s.ay / IslandConfig::kCoarse)] >= 0;
    const int steps = (int)(std::hypot(s.bx - s.ax, s.by - s.ay) * 4) + 1;
    for (int i = 0; i <= steps; ++i) {
      const float t = (float)i / steps;
      const int x = (int)std::floor(s.ax + t * (s.bx - s.ax));
      const int y = (int)std::floor(s.ay + t * (s.by - s.ay));
      ASSERT_TRUE(isWater(w.biomeAt(x, y))) << x << "," << y;
    }
  }
  EXPECT_GT(outlets, 20) << "lake outlets should have reaches";
}

TEST(IslandTest, LoneLakesAndFedLakesBothExist) {
  const IslandMap &m = sharedWorld().island().map();
  int lone = 0, fed = 0;
  for (int inflow : m.lakeInflow)
    (inflow == 0 ? lone : fed)++;
  EXPECT_GT(lone, 0);
  EXPECT_GT(fed, 0);
}

TEST(IslandTest, SwampWaterIsASwampLakeAndTheRiversBesideIt) {
  const IslandMap &m = sharedWorld().island().map();
  int swampLakes = 0;
  for (uint8_t s : m.lakeSwamp)
    swampLakes += s;
  EXPECT_GT(swampLakes, 0);
  EXPECT_LT(swampLakes, (int)m.lakeSwamp.size()); // a fraction, not all

  // A swamp river cell is a river within the reach of a swamp lake, along the
  // flow in one direction or the other.
  auto swampLake = [&](int c) { return m.lake[c] >= 0 && m.lakeSwamp[m.lake[c]]; };
  const int N = m.n * m.n;
  for (int c = 0; c < N; ++c) {
    if (!m.swampRiver[c])
      continue;
    ASSERT_TRUE(m.river[c]);
    bool near = false;
    for (int r = c, s = 0; r >= 0 && s <= island::kSwampRiverReach && !near; ++s) {
      near = swampLake(r);
      r = m.receiver[r];
    }
    // Downstream of the outlet: walk up through swamp river cells instead.
    for (int u = 0; u < N && !near; ++u)
      near = m.receiver[u] == c && (swampLake(u) || m.swampRiver[u]);
    EXPECT_TRUE(near) << "cell " << c;
  }
}

TEST(IslandTest, NoOpenLakeSitsInAMarsh) {
  // Wetland is swamp country: a lake whose banks are mostly wetland is a
  // swamp. Check every open lake's banks, tile by tile.
  const Island &isl = sharedWorld().island();
  const IslandMap &m = isl.map();
  const int k = IslandConfig::kCoarse;
  std::vector<int> bank(m.lakeSwamp.size(), 0), wet(m.lakeSwamp.size(), 0);
  for (int c = 0; c < m.n * m.n; ++c) {
    const int id = m.lake[c];
    if (id < 0 || m.lakeSwamp[id])
      continue;
    for (int y = (c / m.n) * k; y < (c / m.n + 1) * k; y += 2)
      for (int x = (c % m.n) * k; x < (c % m.n + 1) * k; x += 2) {
        if (isl.sample(x, y).biome != Biome::LAKE)
          continue;
        for (auto [dx, dy] : {std::pair{3, 0}, {-3, 0}, {0, 3}, {0, -3}}) {
          Biome b = isl.sample(x + dx, y + dy).biome;
          if (isWater(b))
            continue;
          bank[id]++;
          wet[id] += b == Biome::WETLAND;
        }
      }
  }
  for (size_t id = 0; id < bank.size(); ++id)
    if (bank[id] >= 20)
      EXPECT_LE(2 * wet[id], bank[id]) << "lake " << id;
}

TEST(IslandTest, SwampsKeepToTheirInlandBand) {
  // Each swamp lake's mean distance from the sea lies 15-25 % of the way to
  // the island's most inland point.
  const IslandMap &m = sharedWorld().island().map();
  const size_t lakes = m.lakeSwamp.size();
  std::vector<float> sum(lakes, 0.0f);
  std::vector<int> cells(lakes, 0);
  float deepest = 0.0f;
  for (int c = 0; c < m.n * m.n; ++c) {
    if (!m.ocean[c])
      deepest = std::max(deepest, m.oceanDist[c]);
    if (m.lake[c] >= 0) {
      sum[m.lake[c]] += m.oceanDist[c];
      cells[m.lake[c]]++;
    }
  }
  int swamps = 0;
  for (size_t id = 0; id < lakes; ++id) {
    if (!m.lakeSwamp[id])
      continue;
    ++swamps;
    const float f = sum[id] / cells[id] / deepest;
    EXPECT_GE(f, island::kSwampBandFrom) << "lake " << id;
    EXPECT_LE(f, island::kSwampBandTo) << "lake " << id;
  }
  EXPECT_GE(swamps, 1);
}

TEST(OverworldTest, ShoreHasNoNubsOrCornerJoins) {
  // No land tile with water on 3+ sides or on two opposite sides, and no
  // water tiles that meet only at a corner - over a lake-and-river country.
  const Overworld &w = sharedWorld();
  auto wet = [&](int x, int y) { return isWater(w.biomeAt(x, y)); };
  for (int y = 900; y < 1400; ++y)
    for (int x = 2200; x < 2700; ++x) {
      const bool a = wet(x, y), b = wet(x + 1, y), c = wet(x, y + 1), d = wet(x + 1, y + 1);
      ASSERT_FALSE((a && d && !b && !c) || (b && c && !a && !d)) << x << "," << y;
      if (a)
        continue;
      const bool n = wet(x, y - 1), e = b, s = c, west = wet(x - 1, y);
      ASSERT_LT(n + e + s + west, 3) << x << "," << y;
      ASSERT_FALSE((n && s) || (e && west)) << x << "," << y;
    }
}

TEST(OverworldTest, ShoreTidyIsExactInsideItsApron) {
  // A chunk is tidied with a kPasses-tile apron; its core must match the
  // whole grid tidied at once, or chunks would disagree at their borders.
  const Island &isl = sharedWorld().island();
  constexpr int A = shoreline::kPasses, W = 120, x0 = 2440, y0 = 1000;
  std::vector<TileSample> whole(W * W);
  for (int y = 0; y < W; ++y)
    for (int x = 0; x < W; ++x)
      whole[y * W + x] = isl.sample(x0 + x, y0 + y);
  std::vector<TileSample> tidied = whole;
  shoreline::tidy(tidied, W, W);
  constexpr int cx = 40, cy = 40, n = 32, P = n + 2 * A;
  std::vector<TileSample> part(P * P);
  for (int y = 0; y < P; ++y)
    for (int x = 0; x < P; ++x)
      part[y * P + x] = whole[(cy - A + y) * W + (cx - A + x)];
  shoreline::tidy(part, P, P);
  int changedTiles = 0;
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const Biome got = part[(y + A) * P + (x + A)].biome;
      ASSERT_EQ(got, tidied[(cy + y) * W + (cx + x)].biome) << x << "," << y;
      changedTiles += got != whole[(cy + y) * W + (cx + x)].biome;
    }
  EXPECT_GT(changedTiles, 0) << "the window should hold something to tidy";
}

TEST(IslandTest, OnlyRiversFlowAndTheyFlowDownstream) {
  const Island &isl = sharedWorld().island();
  const IslandMap &m = isl.map();
  int checked = 0;
  for (const IslandMap::Segment &s : m.segments) {
    // A reach's midpoint is river (or swamp) water; if it is river, its flow
    // step points the way the reach runs, from upstream node to receiver.
    int x = (int)std::floor((s.ax + s.bx) / 2.0f), y = (int)std::floor((s.ay + s.by) / 2.0f);
    TileSample t = isl.sample(x, y);
    if (t.biome != Biome::RIVER)
      continue;
    ASSERT_NE(t.flow, 0);
    int dx, dy;
    flowStep(t.flow, dx, dy);
    EXPECT_GT(dx * (s.bx - s.ax) + dy * (s.by - s.ay), 0.0f) << x << " " << y;
    ++checked;
  }
  EXPECT_GT(checked, 50);
  // Everything else lies still.
  for (int y = 0; y < isl.config().size; y += 37)
    for (int x = 0; x < isl.config().size; x += 37) {
      TileSample t = isl.sample(x, y);
      EXPECT_EQ(t.flow != 0, t.biome == Biome::RIVER) << x << " " << y;
    }
}

TEST(IslandTest, BeachesHugTheOcean) {
  // Beach is capped by distance to the open sea, so no tile of it lies more
  // than the widest reach (in coarse cells) from an ocean cell.
  const Island &isl = sharedWorld().island();
  const IslandMap &m = isl.map();
  const int k = IslandConfig::kCoarse;
  for (int y = 0; y < isl.config().size; y += 7) {
    for (int x = 0; x < isl.config().size; x += 7) {
      if (isl.sample(x, y).biome == Biome::BEACH) {
        ASSERT_LT(m.oceanDist[m.index(x / k, y / k)], Island::kBeachReach + 2.0f)
            << x << "," << y;
      }
    }
  }
}

TEST(IslandTest, TheWrapSeamLiesInOpenOcean) {
  const Island &isl = sharedWorld().island();
  const int size = isl.config().size;
  // Every tile within the open-sea margin is ocean - so the world can wrap
  // there without any generator being seamless.
  const int margin = (size - 2 * isl.config().radius()) / 2;
  for (int i = 0; i < size; i += 5) {
    for (int b = 0; b < margin; b += 37) {
      EXPECT_EQ(isl.sample(b, i).biome, Biome::OCEAN);
      EXPECT_EQ(isl.sample(size - 1 - b, i).biome, Biome::OCEAN);
      EXPECT_EQ(isl.sample(i, b).biome, Biome::OCEAN);
      EXPECT_EQ(isl.sample(i, size - 1 - b).biome, Biome::OCEAN);
    }
  }
  EXPECT_GE(margin, 500);
}

// Shore cells as chooseCoastal sees them: dry land beside the sea.
static std::vector<int> shoreCells(const IslandMap &m) {
  std::vector<int> out;
  for (int c = 0; c < m.n * m.n; ++c)
    if (!m.ocean[c] && m.lake[c] < 0 && m.oceanDist[c] <= 1.5f)
      out.push_back(c);
  return out;
}

TEST(IslandTest, CoastalScrubTakesAboutAThirdOfTheCoast) {
  const IslandMap &m = sharedWorld().island().map();
  const std::vector<int> shore = shoreCells(m);
  ASSERT_FALSE(shore.empty());
  int chosen = 0, covered = 0;
  for (int c : shore) {
    chosen += m.coastalShore[c];
    covered += m.coastalDist[c] < island::kCoastalReach;
  }
  // A quantile: the chosen share is exact up to ties.
  EXPECT_NEAR((float)chosen / shore.size(), island::kCoastalShare, 0.01f);
  // The zone spreads along the shore from each pick to ~30 % of the coast.
  EXPECT_GT((float)covered / shore.size(), 0.22f);
  EXPECT_LT((float)covered / shore.size(), 0.40f);
  for (int c = 0; c < m.n * m.n; ++c)
    if (m.coastalShore[c])
      ASSERT_FALSE(m.ocean[c]) << c;
}

TEST(IslandTest, CoastalScrubAndDuneBeachesKeepToAChosenShore) {
  const Island &isl = sharedWorld().island();
  const IslandMap &m = isl.map();
  const int k = IslandConfig::kCoarse;
  // The zone's widest reach (gradient noise stays within +-1), plus a cell
  // for reading the field per cell rather than bilinear.
  const float reach = island::kCoastalReach + island::kCoastalWobble + 1.5f;
  int coastal = 0, sand = 0, dune = 0;
  for (int y = 0; y < isl.config().size; y += 5) {
    for (int x = 0; x < isl.config().size; x += 5) {
      const TileSample s = isl.sample(x, y);
      const bool zoned = s.biome == Biome::COASTAL || (s.biome == Biome::BEACH && s.shade == 1);
      if (!zoned)
        continue;
      ASSERT_LT(m.coastalDist[m.index(x / k, y / k)], reach) << x << "," << y;
      coastal += s.biome == Biome::COASTAL;
      sand += s.biome == Biome::COASTAL && s.shade == 1;
      dune += s.biome == Biome::BEACH;
    }
  }
  EXPECT_GT(coastal, 0);
  EXPECT_GT(dune, 0);
  // Sand patches are occasional, not the floor.
  EXPECT_GT(sand, 0);
  EXPECT_LT(sand * 4, coastal);
}

TEST(OverworldTest, PalmsGrowOnlyBehindShelteredBeaches) {
  // Every tree on a beach stands on a dune beach. Checked in the chunks over
  // a spread of shore cells, chosen and not.
  const Overworld &w = sharedWorld();
  const IslandMap &m = w.island().map();
  const std::vector<int> shore = shoreCells(m);
  const int k = IslandConfig::kCoarse;
  int duneTrees = 0;
  for (size_t i = 0; i < shore.size(); i += shore.size() / 60 + 1) {
    const int c = shore[i];
    const int cx = Overworld::chunkOf((c % m.n) * k), cy = Overworld::chunkOf((c / m.n) * k);
    for (const Prop &p : w.chunkProps(cx, cy)) {
      if (p.type != PropType::TREE || w.biomeAt(p.x, p.y) != Biome::BEACH)
        continue;
      ASSERT_EQ(w.shadeAt(p.x, p.y), 1) << p.x << "," << p.y;
      ++duneTrees;
    }
  }
  EXPECT_GT(duneTrees, 0);
}

// ---------------------------------------------------------------------------
// Overworld: chunks, scatter, change record, wrap
// ---------------------------------------------------------------------------
TEST(OverworldTest, PropSpacingHoldsAcrossChunkBorders) {
  const Overworld &w = sharedWorld();
  const int cx = Overworld::chunkOf(w.spawnX()), cy = Overworld::chunkOf(w.spawnY());
  std::vector<Prop> all;
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx)
      for (const Prop &p : w.chunkProps(cx + dx, cy + dy))
        all.push_back(p);
  ASSERT_GT(all.size(), 30u);
  const float r = Overworld::kPropSpacing;
  for (size_t i = 0; i < all.size(); ++i) {
    for (size_t j = i + 1; j < all.size(); ++j) {
      float dx = all[i].px - all[j].px, dy = all[i].py - all[j].py;
      ASSERT_GE(dx * dx + dy * dy, r * r - 1e-3f)
          << propId(all[i].type) << " at (" << all[i].x << "," << all[i].y
          << ") vs (" << all[j].x << "," << all[j].y << ")";
    }
  }
}

TEST(OverworldTest, ChunksDoNotDependOnBuildOrder) {
  Overworld a(kSeed), b(kSeed);
  const int cx = Overworld::chunkOf(a.spawnX()), cy = Overworld::chunkOf(a.spawnY());
  // a builds the centre first, b builds it last.
  auto sig = [](const std::vector<Prop> &ps) {
    std::string s;
    for (const Prop &p : ps)
      s += std::to_string(p.x) + "," + std::to_string(p.y) + ":" +
           propId(p.type) + ";";
    return s;
  };
  std::string first = sig(a.chunkProps(cx, cy));
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      b.chunkProps(cx + dx, cy + dy);
  EXPECT_EQ(first, sig(b.chunkProps(cx, cy)));
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      EXPECT_EQ(sig(a.chunkProps(cx + dx, cy + dy)),
                sig(b.chunkProps(cx + dx, cy + dy)));
}

TEST(OverworldTest, SpawnIsClearOfProps) {
  const Overworld &w = sharedWorld();
  for (int dy = -Overworld::kSpawnClearance; dy <= Overworld::kSpawnClearance; ++dy)
    for (int dx = -Overworld::kSpawnClearance; dx <= Overworld::kSpawnClearance; ++dx)
      EXPECT_EQ(w.propAt(w.spawnX() + dx, w.spawnY() + dy), PropType::NONE);
}

TEST(OverworldTest, ARemovedPropStaysRemovedAfterItsChunkRegenerates) {
  Overworld w(kSeed);
  const int cx = Overworld::chunkOf(w.spawnX()), cy = Overworld::chunkOf(w.spawnY());
  // Any prop near spawn.
  const Prop *target = nullptr;
  for (int dy = -1; dy <= 1 && !target; ++dy)
    for (int dx = -1; dx <= 1 && !target; ++dx)
      for (const Prop &p : w.chunkProps(cx + dx, cy + dy))
        if (!target)
          target = &p;
  ASSERT_NE(target, nullptr);
  const int px = target->x, py = target->y;
  const PropType before = target->type;
  const size_t propsBefore = w.chunkProps(Overworld::chunkOf(px), Overworld::chunkOf(py)).size();

  ASSERT_TRUE(w.removeProp(px, py));
  EXPECT_EQ(w.propAt(px, py), PropType::NONE);
  EXPECT_FALSE(w.removeProp(px, py));

  // Walk the camera across the world: everything near spawn is evicted.
  w.retainAround(px + w.getWidth() / 2, py + w.getHeight() / 2, 1);
  EXPECT_EQ(w.cachedChunkCount(), 0);

  // Regenerated on demand - and the change is re-applied.
  EXPECT_EQ(w.propAt(px, py), PropType::NONE);
  EXPECT_EQ(w.chunkProps(Overworld::chunkOf(px), Overworld::chunkOf(py)).size(),
            propsBefore - 1);
  EXPECT_EQ(w.changeCount(), 1);

  // A fresh world of the same seed still has it: the record is per world.
  Overworld fresh(kSeed);
  EXPECT_EQ(fresh.propAt(px, py), before);
}

TEST(OverworldTest, TilesAreContinuousAcrossTheWrapSeam) {
  const Overworld &w = sharedWorld();
  const int size = w.getWidth();
  EXPECT_EQ(Overworld::chunkOf(-1), -1);
  EXPECT_EQ(Overworld::chunkOf(0), 0);
  EXPECT_EQ(Overworld::chunkOf(31), 0);
  EXPECT_EQ(Overworld::chunkOf(-33), -2);
  for (int i = 0; i < 50; ++i) {
    int x = w.spawnX() + i * 13 - 300, y = w.spawnY() - i * 7 + 100;
    EXPECT_EQ(w.biomeAt(x, y), w.biomeAt(x + size, y));
    EXPECT_EQ(w.biomeAt(x, y), w.biomeAt(x - size, y - size));
    EXPECT_EQ(w.propAt(x, y), w.propAt(x + size, y + size));
  }
  // Walking off the east edge lands on the west edge.
  EXPECT_EQ(w.biomeAt(size, 10), w.biomeAt(0, 10));
  EXPECT_EQ(w.biomeAt(-1, 10), w.biomeAt(size - 1, 10));
}

TEST(OverworldTest, CacheIsBoundedByTheViewNotTheWorld) {
  Overworld w(kSeed);
  for (int step = 0; step < 40; ++step) {
    int x = w.spawnX() + step * 64;
    w.biomeAt(x, w.spawnY());
    w.retainAround(x, w.spawnY(), 2);
    ASSERT_LE(w.cachedChunkCount(), 25);
  }
}

TEST(OverworldTest, ItemLayerAndDrops) {
  ItemDatabase::init();
  Overworld w(kSeed);
  const int sx = w.spawnX(), sy = w.spawnY();
  EXPECT_FALSE(w.isSolid(sx, sy));
  w.setItem(sx, sy, ItemType::MUSHROOM);
  EXPECT_EQ(w.getItem(sx, sy), ItemType::MUSHROOM);
  EXPECT_EQ(w.getItem(sx + w.getWidth(), sy), ItemType::MUSHROOM); // wraps
  EXPECT_TRUE(w.isSolid(sx, sy));

  int ox = 0, oy = 0;
  ASSERT_TRUE(w.findNearestEmptyItemCell(sx, sy, 2, ox, oy));
  EXPECT_FALSE(ox == sx && oy == sy);
  EXPECT_FALSE(w.isSolid(ox, oy));
  w.setItem(sx, sy, ItemType::NONE);
  EXPECT_FALSE(w.isSolid(sx, sy));
}

// The seam: Player collides against the overworld through World alone.
TEST(OverworldTest, PlayerIsStoppedByATree) {
  Overworld w(kSeed);
  const int cx = Overworld::chunkOf(w.spawnX()), cy = Overworld::chunkOf(w.spawnY());
  const Prop *tree = nullptr;
  for (int dy = -2; dy <= 2 && !tree; ++dy)
    for (int dx = -2; dx <= 2 && !tree; ++dx)
      for (const Prop &p : w.chunkProps(cx + dx, cy + dy))
        if (!tree && propIsSolid(p.type) && !w.isSolid(p.x - 1, p.y) &&
            !w.isSolid(p.x - 2, p.y))
          tree = &p;
  ASSERT_NE(tree, nullptr);
  const int CELL = w.getCellSize();
  const float treeLeft = (float)(tree->x * CELL);
  Player player({(tree->x - 2) * CELL + CELL / 2.0f, tree->y * CELL + CELL / 2.0f},
                AreaState::ROOM);
  InputState right;
  right.moveRight = true;
  for (int i = 0; i < 120; ++i)
    player.update(w, 1.0f / 60.0f, right);
  EXPECT_LE(player.getPosition().x, treeLeft - 9.0f); // radius 10, minus rounding
  EXPECT_GT(player.getPosition().x, treeLeft - 20.0f) << "it should have walked up to it";
}

// ---------------------------------------------------------------------------
// OverworldRenderer: the pure parts (no window, no textures)
// ---------------------------------------------------------------------------
TEST(OverworldRendererTest, CornerMaskBitsAreTlTrBlBr) {
  EXPECT_EQ(OverworldRenderer::cornerMask(false, false, false, false), 0);
  EXPECT_EQ(OverworldRenderer::cornerMask(true, false, false, false), 8);
  EXPECT_EQ(OverworldRenderer::cornerMask(false, true, false, false), 4);
  EXPECT_EQ(OverworldRenderer::cornerMask(false, false, true, false), 2);
  EXPECT_EQ(OverworldRenderer::cornerMask(false, false, false, true), 1);
  EXPECT_EQ(OverworldRenderer::cornerMask(true, true, true, true), 15);
}

TEST(OverworldRendererTest, EveryPropKindHasASpriteInEveryLandBiome) {
  for (PropType t : {PropType::TREE, PropType::PINE, PropType::BUSH,
                     PropType::ROCK, PropType::REEDS})
    for (int b = (int)Biome::BEACH; b < (int)Biome::COUNT; ++b)
      for (int v = 0; v < 256; ++v)
        ASSERT_LT(OverworldRenderer::spriteFor(t, (Biome)b, (uint8_t)v),
                  owsprite::COUNT)
            << propId(t) << " in " << biomeId((Biome)b);
  EXPECT_EQ(OverworldRenderer::spriteFor(PropType::NONE, Biome::FOREST, 0),
            owsprite::COUNT);
}

TEST(OverworldRendererTest, SpeciesFollowTheBiome) {
  using namespace owsprite;
  for (int v = 0; v < 256; ++v) {
    Id palm = OverworldRenderer::spriteFor(PropType::TREE, Biome::BEACH, (uint8_t)v);
    EXPECT_TRUE(palm == PALM_TALL || palm == PALM_SHORT);
    Id willow = OverworldRenderer::spriteFor(PropType::TREE, Biome::WETLAND, (uint8_t)v);
    EXPECT_TRUE(willow >= WILLOW && willow <= WILLOW_S_C);
    // Coastal scrub keeps its trees short; palms grow nowhere else.
    Id scrub = OverworldRenderer::spriteFor(PropType::TREE, Biome::COASTAL, (uint8_t)v);
    EXPECT_LE(kFrames[scrub].h, 7) << v;
    for (Biome b : {Biome::GRASSLAND, Biome::FOREST, Biome::WETLAND, Biome::MOUNTAIN, Biome::SNOW})
      for (PropType t : {PropType::TREE, PropType::PINE, PropType::BUSH}) {
        Id id = OverworldRenderer::spriteFor(t, b, (uint8_t)v);
        EXPECT_TRUE(id != PALM_TALL && id != PALM_SHORT) << biomeId(b);
      }
  }
  int palms = 0;
  for (int v = 0; v < 256; ++v) {
    Id scrub = OverworldRenderer::spriteFor(PropType::TREE, Biome::COASTAL, (uint8_t)v);
    palms += scrub == PALM_TALL || scrub == PALM_SHORT;
  }
  EXPECT_GT(palms, 256 / 4); // the commonest tree there
}

// Full-orange autumn waits for Phase 5's seasons; only the light-brown
// autumn trees may appear now.
TEST(OverworldRendererTest, FullOrangeAutumnTreesAreHeldBack) {
  using namespace owsprite;
  const Id heldBack[] = {OAK_AUTUMN_A,   OAK_AUTUMN_B,   BIRCH_AUTUMN_A,
                         BIRCH_AUTUMN_B, PC1_S2_ORANGE,  PC1_S3_ORANGE,
                         PC1_S4_ORANGE,  PC1_S5_ORANGE,  PC1_S2_AMBER,
                         PC1_S3_AMBER,   PC1_S4_AMBER,   PC1_S5_AMBER,
                         PC3_S2_RUST,    PC3_S3_RUST,    PC3_S4_RUST,
                         PC3_S5_RUST};
  for (PropType t : {PropType::TREE, PropType::PINE})
    for (int b = (int)Biome::BEACH; b < (int)Biome::COUNT; ++b)
      for (int v = 0; v < 256; ++v) {
        Id id = OverworldRenderer::spriteFor(t, (Biome)b, (uint8_t)v);
        for (Id h : heldBack)
          ASSERT_NE(id, h) << biomeId((Biome)b);
      }
}

TEST(OverworldRendererTest, GroundDetailsAreFlatDecalsAtAModestDensity) {
  for (int b = 0; b < (int)Biome::COUNT; ++b) {
    for (uint8_t shade = 0; shade < 4; ++shade) {
      int shown = 0;
      for (uint32_t h = 0; h < 4000; ++h) {
        owsprite::Id id = OverworldRenderer::decalFor((Biome)b, shade, noise::hash(h, b, 9));
        if (id == owsprite::COUNT)
          continue;
        ++shown;
        // Flat: one tile tall, so drawing under the props is never wrong.
        EXPECT_EQ(owsprite::kFrames[id].h, 1);
        EXPECT_GE(id, owsprite::DECAL_FLOWERS_A); // decals only, no props
      }
      EXPECT_LT(shown, 4000 / 5) << biomeId((Biome)b); // at most ~20% of tiles
    }
  }
  EXPECT_EQ(OverworldRenderer::decalFor(Biome::OCEAN, 0, 0), owsprite::COUNT);
}

// A 12 x 5 grid split at x = 6: `in` to the left.
static std::vector<uint8_t> leftHalf() {
  std::vector<uint8_t> v(12 * 5);
  for (int k = 0; k < (int)v.size(); ++k)
    v[k] = k % 12 < 6;
  return v;
}

TEST(OverworldRendererTest, FadeShareRampsAcrossABorder) {
  std::vector<uint8_t> all(12 * 5, 1);
  std::vector<float> s;
  OverworldRenderer::shareWithin(leftHalf(), all, 12, 5, 2, s);
  const int row = 2 * 12;
  EXPECT_FLOAT_EQ(s[row + 2], 1.0f);
  EXPECT_FLOAT_EQ(s[row + 5], 0.6f); // 3 of the 5 columns in reach are in
  EXPECT_FLOAT_EQ(s[row + 6], 0.4f);
  EXPECT_FLOAT_EQ(s[row + 9], 0.0f);
  for (int x = 1; x < 12; ++x)
    EXPECT_LE(s[row + x], s[row + x - 1]);

  // Cells that do not count are left out of the share, so a shore does not
  // thin a fade: with column 4 uncounted, column 5 sees 2 in of 4 counted.
  std::vector<uint8_t> count = all;
  for (int y = 0; y < 5; ++y)
    count[y * 12 + 4] = 0;
  OverworldRenderer::shareWithin(leftHalf(), count, 12, 5, 2, s);
  EXPECT_FLOAT_EQ(s[row + 5], 0.5f);
}

TEST(OverworldRendererTest, SwampDepthRisesAwayFromOpenWater) {
  // 14 x 5, all water: open in columns 0-1, swamp from column 2 on.
  constexpr int W = 14, H = 5, R = OverworldRenderer::kSwampReach;
  std::vector<uint8_t> water(W * H, 1), swamp(W * H);
  for (int k = 0; k < W * H; ++k)
    swamp[k] = k % W >= 2;
  std::vector<float> d;
  OverworldRenderer::swampDepth(water, swamp, W, H, d);
  const int row = 2 * W;
  for (int x = 0; x < W; ++x)
    EXPECT_FLOAT_EQ(d[row + x], (float)std::min(std::max(x - 1, 0), R)) << "column " << x;

  // A land column at 4 cuts the swamp beyond it off from the open water:
  // depth is counted through water only. The land takes its neighbours' mean.
  for (int y = 0; y < H; ++y)
    water[y * W + 4] = swamp[y * W + 4] = 0;
  OverworldRenderer::swampDepth(water, swamp, W, H, d);
  EXPECT_FLOAT_EQ(d[row + 3], 2.0f);
  EXPECT_FLOAT_EQ(d[row + 5], (float)R);
  EXPECT_FLOAT_EQ(d[row + 4], (2.0f + R) / 2.0f);
}

TEST(OverworldRendererTest, SpriteFramesDoNotOverlapInTheAtlas) {
  using owsprite::kFrames;
  for (int i = 0; i < owsprite::COUNT; ++i) {
    ASSERT_GT(kFrames[i].w, 0);
    ASSERT_GT(kFrames[i].h, 0);
    ASSERT_LE(kFrames[i].h, owsprite::kTallestTiles);
    for (int j = i + 1; j < owsprite::COUNT; ++j) {
      const auto &a = kFrames[i], &b = kFrames[j];
      bool apart = a.col + a.w <= b.col || b.col + b.w <= a.col ||
                   a.row + a.h <= b.row || b.row + b.h <= a.row;
      ASSERT_TRUE(apart) << i << " overlaps " << j;
    }
  }
}

// ---------------------------------------------------------------------------
// StepEffects and the footfall that drives them (no window, no textures)
// ---------------------------------------------------------------------------
namespace {
// The tile nearest spawn (on a coarse lattice) whose 5 x 5 block is `want`.
bool findBlockOf(const Overworld &w, Biome want, int &tx, int &ty) {
  for (int r = 0; r < 1200; r += 8)
    for (int dy = -r; dy <= r; dy += 8)
      for (int dx = -r; dx <= r; dx += 8) {
        if (std::max(std::abs(dx), std::abs(dy)) != r)
          continue;
        const int x = w.spawnX() + dx, y = w.spawnY() + dy;
        bool all = true;
        for (int j = -2; j <= 2 && all; ++j)
          for (int i = -2; i <= 2 && all; ++i)
            all = w.biomeAt(x + i, y + j) == want;
        if (all) {
          tx = x;
          ty = y;
          return true;
        }
      }
  return false;
}
} // namespace

TEST(StepEffectsTest, SnowTakesPrintsWetlandSquishesSandKicksTheRestNothing) {
  EXPECT_EQ(StepEffects::markFor(Biome::SNOW), StepEffects::Mark::PRINT);
  EXPECT_EQ(StepEffects::markFor(Biome::WETLAND), StepEffects::Mark::SQUISH);
  EXPECT_EQ(StepEffects::markFor(Biome::BEACH), StepEffects::Mark::KICK);
  EXPECT_EQ(StepEffects::markFor(Biome::COASTAL, 1), StepEffects::Mark::KICK); // sand patch
  EXPECT_EQ(StepEffects::markFor(Biome::COASTAL, 0), StepEffects::Mark::KICK_PALE); // grass
  for (Biome b : {Biome::GRASSLAND, Biome::FOREST, Biome::MOUNTAIN,
                  Biome::SWAMP, Biome::LAKE})
    EXPECT_EQ(StepEffects::markFor(b), StepEffects::Mark::NONE) << biomeId(b);
}

TEST(StepEffectsTest, APrintLastsItsLifeAndASprayItsFrames) {
  StepEffects fx;
  fx.leave(StepEffects::Mark::PRINT, {0, 0}, FacingDirection::DOWN);
  fx.leave(StepEffects::Mark::SQUISH, {0, 0}, FacingDirection::DOWN);
  fx.leave(StepEffects::Mark::KICK, {0, 0}, FacingDirection::DOWN);
  EXPECT_EQ(fx.liveCount(), 3);
  fx.advance(StepEffects::kSquishFrames * StepEffects::kSquishFrameSeconds + 0.01f);
  EXPECT_EQ(fx.liveCount(), 1) << "the squish and the kick have played out";
  fx.advance(StepEffects::kPrintSeconds - 0.5f);
  EXPECT_EQ(fx.liveCount(), 1);
  fx.advance(0.5f);
  EXPECT_EQ(fx.liveCount(), 0);
}

TEST(StepEffectsTest, TheRingKeepsTheNewestMarks) {
  StepEffects fx;
  for (int i = 0; i < StepEffects::kCapacity + 40; ++i)
    fx.leave(StepEffects::Mark::PRINT, {(float)i, 0}, FacingDirection::DOWN);
  EXPECT_EQ(fx.liveCount(), StepEffects::kCapacity);
  fx.leave(StepEffects::Mark::NONE, {0, 0}, FacingDirection::DOWN);
  EXPECT_EQ(fx.liveCount(), StepEffects::kCapacity) << "NONE takes no slot";
}

// Through update(): the world under the foot picks the mark, and a footfall
// that has not moved since the last one (walking into a tree) leaves none.
TEST(StepEffectsTest, FootfallsInSnowLeavePrintsButNotOnTheSpot) {
  const Overworld &w = sharedWorld();
  int tx, ty;
  ASSERT_TRUE(findBlockOf(w, Biome::SNOW, tx, ty));
  const float cell = (float)w.getCellSize();
  Player player({tx * cell + cell / 2.0f, ty * cell}, AreaState::ROOM);
  StepEffects fx;
  fx.update(w, player, false, 0.1f);
  EXPECT_EQ(fx.liveCount(), 0) << "no footfall, no print";
  fx.update(w, player, true, 0.1f);
  EXPECT_EQ(fx.liveCount(), 1);
  fx.update(w, player, true, 0.1f);
  EXPECT_EQ(fx.liveCount(), 1) << "stood still: no second print";
  player.teleport({player.getPosition().x, player.getPosition().y + 20.0f}, AreaState::ROOM);
  fx.update(w, player, true, 0.1f);
  EXPECT_EQ(fx.liveCount(), 2);
}

// A footfall on every walk frame (0.2 s each), so a snow trail stays dense.
TEST(StepEffectsTest, TheWalkCycleRaisesAFootfallOnEveryFrame) {
  Overworld w(kSeed);
  const float cell = (float)w.getCellSize();
  Player player({w.spawnX() * cell + cell / 2.0f, w.spawnY() * cell + cell / 2.0f},
                AreaState::ROOM);
  PlayerRenderer renderer;
  InputState walk;
  walk.moveDown = true;
  int footfalls = 0;
  const float dt = 1.0f / 60.0f;
  for (int tick = 0; tick < 54; ++tick) { // 0.9 s: four whole 0.2 s frames
    player.update(w, dt, walk);
    renderer.update(dt, player);
    footfalls += renderer.pollFootfall();
  }
  EXPECT_EQ(footfalls, 4);
  EXPECT_FALSE(renderer.pollFootfall()) << "polling clears it";
}
