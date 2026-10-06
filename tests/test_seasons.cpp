#include "render/overworld_renderer.hpp"
#include "world/calendar.hpp"
#include "world/climate.hpp"
#include "world/noise.hpp"
#include "world/overworld.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <memory>

// The calendar, the climate it drives, and what the renderer does with both.
// One island for the climate suites: generation costs ~0.5 s in Debug.
namespace {
constexpr uint32_t kSeed = 1;

const Overworld &seasonWorld() {
  static std::unique_ptr<Overworld> world = std::make_unique<Overworld>(kSeed);
  return *world;
}

Calendar onDay(int day, float hour = 12.0f) {
  Calendar c;
  c.setDay(day, hour);
  return c;
}
} // namespace

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------
TEST(CalendarTest, ARunStartsOnDayZeroInMidSpringMorning) {
  Calendar c;
  EXPECT_EQ(c.day(), 0);
  EXPECT_EQ(c.season(), Season::SPRING);
  EXPECT_EQ(c.dayOfSeason(), Calendar::kDaysPerSeason / 2 + 1);
  EXPECT_FLOAT_EQ(c.timeOfDay(), (float)Calendar::kStartHour);
}

TEST(CalendarTest, ADayIsFifteenMinutesOfTicks) {
  Calendar c;
  c.advance(15 * 60 * 60);
  EXPECT_EQ(c.day(), 1);
  EXPECT_FLOAT_EQ(c.timeOfDay(), (float)Calendar::kStartHour);
}

// The earliest a maze entrance can open (Day 20) is mid-summer.
TEST(CalendarTest, DayTwentyIsMidSummerAndWinterStartsOnDayFifty) {
  EXPECT_EQ(onDay(20, 0.0f).season(), Season::SUMMER);
  EXPECT_NEAR(onDay(20, 0.0f).yearDay(), 30.0, 1e-9);
  EXPECT_EQ(onDay(49, 23.9f).season(), Season::AUTUMN);
  EXPECT_EQ(onDay(50, 0.0f).season(), Season::WINTER);
}

TEST(CalendarTest, TheYearWrapsButTheDayCounterDoesNot) {
  Calendar a = onDay(5), b = onDay(5 + Calendar::kDaysPerYear);
  EXPECT_NEAR(a.yearDay(), b.yearDay(), 1e-9);
  EXPECT_EQ(b.day(), 5 + Calendar::kDaysPerYear);
}

TEST(CalendarTest, DriftMovesTheSeasonsButNotTheScore) {
  Calendar c = onDay(10);
  const double before = c.yearDay();
  c.driftTicks = 5 * Calendar::kTicksPerDay;
  EXPECT_EQ(c.day(), 10);
  EXPECT_NEAR(c.yearDay(), before + 5.0, 1e-9);
}

TEST(CalendarTest, SetDayClampsToTheStartOfTheRun) {
  EXPECT_EQ(onDay(0, 2.0f).ticks, 0); // 02:00 on Day 0 is before the run began
}

// The debug panel's year slider: it lands on the date asked for, in the
// summer-to-spring year the run is in, and never before the run began.
TEST(CalendarTest, SetYearDayScrubsSummerToSpring) {
  Calendar c = onDay(100); // year day 30.5; this scrub year began on Day 90
  c.setYearDay(65.25);
  EXPECT_NEAR(c.yearDay(), 65.25, 1e-4);
  EXPECT_EQ(c.day(), 135);
  c.setYearDay(3.0); // spring comes after winter, at the scrub year's end
  EXPECT_NEAR(c.yearDay(), 3.0, 1e-4);
  EXPECT_EQ(c.day(), 153);
  c.setYearDay(20.0); // day 1 of summer opens it
  EXPECT_EQ(c.day(), 90);
  Calendar first;
  first.setYearDay(2.0); // before the run began: a year on
  EXPECT_NEAR(first.yearDay(), 2.0, 1e-4);
  EXPECT_GT(first.ticks, 0);
}

// ---------------------------------------------------------------------------
// Climate
// ---------------------------------------------------------------------------
TEST(ClimateTest, ZeroDegreesIsTheSnowLine) {
  const Climate &c = seasonWorld().climate();
  for (double d = 0.0; d < 80.0; d += 3.7)
    EXPECT_NEAR(c.meanCelsius(c.snowLine(d), d), 0.0f, 1e-4f);
}

// The design targets, as areas of this island.
TEST(ClimateTest, SnowCoverHitsTheSeasonalTargets) {
  const Climate &c = seasonWorld().climate();
  // Start of spring and end of autumn: 70 % of the mountains.
  EXPECT_NEAR(c.mountainSnowShare(0.0), Climate::kShoulderMountainShare, 0.02f);
  EXPECT_NEAR(c.mountainSnowShare(60.0), Climate::kShoulderMountainShare, 0.02f);
  // Summer: none at all.
  for (double d = 20.0; d <= 40.0; d += 0.5)
    EXPECT_EQ(c.landSnowShare(d), 0.0f) << "day " << d;
  // Midwinter: the most of the year, out over the grass and forest.
  const float winter = c.landSnowShare(70.0);
  for (double d = 0.0; d < 80.0; d += 0.5)
    EXPECT_LE(c.landSnowShare(d), winter + 1e-6f) << "day " << d;
  EXPECT_GT(winter, 0.6f * Climate::kWinterLandShare);
  EXPECT_LE(winter, Climate::kWinterLandShare + 0.01f);
  // Late spring and early autumn look like the generated map: the alpine
  // peaks under snow, and only those.
  EXPECT_LT(c.landSnowShare(16.0), c.landSnowShare(0.0));
  EXPECT_GT(c.landSnowShare(16.0), 0.0f);
}

// PCHIP never overshoots: between two knots the line stays between them.
TEST(ClimateTest, SnowLineIsMonotoneBetweenKnots) {
  const Climate &c = seasonWorld().climate();
  const auto &k = c.knots();
  for (int i = 0; i < Climate::kKnots; ++i) {
    const auto &a = k[i];
    const auto &b = k[(i + 1) % Climate::kKnots];
    const float end = b.day > a.day ? b.day : b.day + 80.0f;
    const float lo = std::min(a.z, b.z) - 1e-5f, hi = std::max(a.z, b.z) + 1e-5f;
    EXPECT_NEAR(c.snowLine(a.day), a.z, 1e-5f);
    for (float d = a.day; d <= end; d += 0.1f) {
      const float z = c.snowLine(d);
      EXPECT_GE(z, lo) << "day " << d;
      EXPECT_LE(z, hi) << "day " << d;
    }
  }
}

TEST(ClimateTest, SnowLineIsContinuousAcrossTheNewYear) {
  const Climate &c = seasonWorld().climate();
  EXPECT_NEAR(c.snowLine(79.999), c.snowLine(0.0), 1e-3f);
}

// Midwinter snow fills the island's cold heart - the middle of the island,
// where the mountains are - but never lies on wetland, beach or water.
TEST(ClimateTest, MidwinterCoversTheHeartAndSparesTheWetland) {
  const Overworld &w = seasonWorld();
  const Island &isl = w.island();
  const float mid = isl.config().size / 2.0f, r = (float)isl.config().radius();
  const double winter = 70.0;
  int heart = 0, heartSnow = 0;
  for (int y = 0; y < isl.config().size; y += 24)
    for (int x = 0; x < isl.config().size; x += 24) {
      const TileSample s = isl.sample(x, y);
      const bool snow = w.climate().snow(s.temperature, s.biome, winter);
      if (!Climate::takesSnow(s.biome)) {
        ASSERT_FALSE(snow) << biomeId(s.biome);
        continue;
      }
      if (std::hypot(x - mid, y - mid) < 0.3f * r) {
        ++heart;
        heartSnow += snow;
      }
    }
  ASSERT_GT(heart, 50);
  EXPECT_GT((float)heartSnow / heart, 0.8f);
}

TEST(ClimateTest, OnlyLakesAndMountainRiversFreeze) {
  constexpr float kLowland = 0.3f, kWarm = 0.6f;
  EXPECT_TRUE(Climate::canFreeze(Biome::LAKE, kLowland, kWarm));
  EXPECT_FALSE(Climate::canFreeze(Biome::RIVER, kLowland, kWarm));
  EXPECT_TRUE(Climate::canFreeze(Biome::RIVER, biome::kMountainLine + 0.05f, kWarm));
  EXPECT_TRUE(Climate::canFreeze(Biome::RIVER, kLowland, biome::kAlpineTemperature - 0.01f));
  for (Biome b : {Biome::SWAMP, Biome::OCEAN, Biome::WETLAND, Biome::GRASSLAND, Biome::MOUNTAIN})
    EXPECT_FALSE(Climate::canFreeze(b, 0.9f, 0.0f)) << biomeId(b);
}

// Lakes freeze in winter and thaw by summer; a river freezes only in the
// mountains, so the lowland rivers run all winter.
TEST(ClimateTest, IceComesAndGoesWithTheSeasons) {
  const Overworld &w = seasonWorld();
  const Island &isl = w.island();
  int lakes = 0, winterLakeIce = 0, summerIce = 0, lowRivers = 0, lowRiverIce = 0;
  for (int y = 0; y < isl.config().size; y += 4)
    for (int x = 0; x < isl.config().size; x += 4) {
      const TileSample s = isl.sample(x, y);
      if (s.biome != Biome::LAKE && s.biome != Biome::RIVER)
        continue;
      summerIce += w.climate().frozen(s.temperature, s.height, s.biome, 30.0);
      const bool ice = w.climate().frozen(s.temperature, s.height, s.biome, 70.0);
      if (s.biome == Biome::LAKE) {
        ++lakes;
        winterLakeIce += ice;
      } else if (!biome::mountainous(s.height, s.temperature)) {
        ++lowRivers;
        lowRiverIce += ice;
      }
    }
  ASSERT_GT(lakes, 100);
  ASSERT_GT(lowRivers, 100);
  EXPECT_EQ(summerIce, 0);
  EXPECT_GT(winterLakeIce, 0);
  EXPECT_EQ(lowRiverIce, 0);
}

TEST(ClimateTest, TemperaturesAreThoseOfATemperateIsland) {
  const Climate &c = seasonWorld().climate();
  constexpr float kCoast = 0.85f, kPeak = 0.05f; // t0 at sea level and at a ~1.0 peak
  EXPECT_GT(c.meanCelsius(kCoast, 30.0), 18.0f);  // a warm summer shore
  EXPECT_LT(c.meanCelsius(kCoast, 30.0), 32.0f);
  EXPECT_GT(c.meanCelsius(kCoast, 70.0), 0.0f);   // the coast never freezes
  EXPECT_LT(c.meanCelsius(kPeak, 70.0), -5.0f);   // a hard winter on the tops
  EXPECT_GT(c.meanCelsius(kCoast, 30.0), c.meanCelsius(kCoast, 70.0) + 8.0f);
}

TEST(ClimateTest, NightsAreColderThanAfternoons) {
  const Climate &c = seasonWorld().climate();
  Calendar afternoon = onDay(10, 15.0f), night = onDay(10, 3.0f);
  const float t0 = 0.5f;
  EXPECT_NEAR(c.celsius(t0, afternoon) - c.meanCelsius(t0, afternoon.yearDay()),
              Climate::kDiurnalSwing, 1e-3f);
  EXPECT_NEAR(c.celsius(t0, night) - c.meanCelsius(t0, night.yearDay()),
              -Climate::kDiurnalSwing, 1e-3f);
}

// The world is built without a date: a tile's props and ground are the same
// whatever day it is first visited - only the Climate's reading moves.
TEST(ClimateTest, TheSameTileReadsDifferentlyThroughTheYear) {
  const Overworld &w = seasonWorld();
  const int x = w.spawnX(), y = w.spawnY();
  EXPECT_GT(w.celsiusAt(x, y, onDay(20)), w.celsiusAt(x, y, onDay(60)));
  EXPECT_FALSE(w.snowAt(x, y, onDay(20)));
}

// ---------------------------------------------------------------------------
// Trees and flowers through the year
// ---------------------------------------------------------------------------
namespace {
using Look = OverworldRenderer::Look;
}

// Bare until leaf-out, in leaf all summer, then (unless it stays green)
// turning, full colour, and bare again - never out of that order.
TEST(SeasonalLookTest, ADeciduousYearRunsInOrder) {
  int stayedGreen = 0;
  constexpr int kTrees = 2000;
  for (int t = 0; t < kTrees; ++t) {
    const uint32_t h = noise::hash(t, 7, 3);
    int stage = 0; // 0 bare, 1 leaf, 2 turning, 3 autumn, 4 bare again
    bool coloured = false;
    for (double d = 0.0; d < 80.0; d += 0.25) {
      const Look l = OverworldRenderer::leafLook(true, d, false, h);
      int next = stage;
      if (l == Look::IN_LEAF) next = std::max(stage, 1);
      if (l == Look::TURNING) next = std::max(stage, 2);
      if (l == Look::AUTUMN) next = std::max(stage, 3);
      if (l == Look::BARE) next = stage == 0 ? 0 : 4;
      ASSERT_GE(next, stage) << "tree " << t << " day " << d;
      ASSERT_NE(l, Look::SNOWED);
      if (d >= 20.0 && d < 40.0)
        ASSERT_EQ(l, Look::IN_LEAF) << "summer is green, day " << d;
      if (d >= 6.0 && d < 20.0)
        ASSERT_EQ(l, Look::IN_LEAF) << "leafed out by spring day 6";
      if (d >= 62.0)
        ASSERT_EQ(l, Look::BARE) << "bare all winter";
      coloured |= l == Look::AUTUMN;
      stage = next;
    }
    stayedGreen += !coloured;
  }
  EXPECT_NEAR((float)stayedGreen / kTrees, 0.2f, 0.05f);
}

TEST(SeasonalLookTest, SnowWinsAndEvergreensChangeOnlyUnderIt) {
  for (double d = 0.0; d < 80.0; d += 1.0) {
    EXPECT_EQ(OverworldRenderer::leafLook(true, d, true, 123), Look::SNOWED);
    EXPECT_EQ(OverworldRenderer::leafLook(false, d, true, 123), Look::SNOWED);
    EXPECT_EQ(OverworldRenderer::leafLook(false, d, false, 123), Look::IN_LEAF);
  }
}

// Every mountain evergreen has a snowed frame of its own, and it is the
// green frame's twin in size - snow_laden never moves the outline.
TEST(SeasonalLookTest, EvergreensHaveTheirOwnSnowFrame) {
  using namespace owsprite;
  for (uint8_t shade : {0, 1})
    for (int v = 0; v < 256; ++v) {
      const Id leaf = OverworldRenderer::spriteFor(PropType::TREE, Biome::MOUNTAIN, shade, (uint8_t)v);
      const Id snow = OverworldRenderer::spriteFor(PropType::TREE, Biome::MOUNTAIN, shade, (uint8_t)v,
                                                   Look::SNOWED);
      const bool evergreen = (leaf >= FIR_0 && leaf <= FIR_DARK_3) ||
                             (leaf >= PC2_S2_TEAL && leaf <= PC2_S5_GREEN_B &&
                              leaf != PC2_S3_BARE && leaf != PC2_S4_BARE && leaf != PC2_S5_BARE);
      if (!evergreen)
        continue;
      EXPECT_NE(snow, leaf) << v;
      EXPECT_EQ(kFrames[snow].w, kFrames[leaf].w) << v;
      EXPECT_EQ(kFrames[snow].h, kFrames[leaf].h) << v;
    }
}

// A tree's species is fixed by its tile: across every look its frames come
// from one species, so the oak in summer is the same oak, bare, in winter.
TEST(SeasonalLookTest, AnOakStaysAnOak) {
  using namespace owsprite;
  for (int v = 0; v < 256; ++v) {
    const Id leaf = OverworldRenderer::spriteFor(PropType::TREE, Biome::FOREST, 2, (uint8_t)v);
    const Id bare = OverworldRenderer::spriteFor(PropType::TREE, Biome::FOREST, 2, (uint8_t)v,
                                                 Look::BARE);
    if (leaf == OAK_SUMMER_A)
      EXPECT_EQ(bare, OAK_BARE_A);
    if (leaf == BIRCH_SUMMER_B)
      EXPECT_EQ(bare, BIRCH_BARE_B);
  }
}

TEST(SeasonalLookTest, SpringBloomsAndWinterHasNoFlowers) {
  EXPECT_NEAR(OverworldRenderer::bloomAt(11.0), 1.0f, 1e-3f);
  EXPECT_EQ(OverworldRenderer::bloomAt(30.0), 0.0f);
  EXPECT_EQ(OverworldRenderer::bloomAt(70.0), 0.0f);
  using namespace owsprite;
  auto flowers = [](double day) {
    int n = 0;
    for (uint32_t h = 0; h < 20000; ++h) {
      const Id id = OverworldRenderer::decalFor(Biome::GRASSLAND, 0, false,
                                                noise::hash(h, 1, 2), day);
      n += id == DECAL_FLOWERS_A || id == DECAL_FLOWERS_B || id == DECAL_FLOWER ||
           id == DECAL_STARS_A || id == DECAL_STARS_B;
    }
    return n;
  };
  EXPECT_GT(flowers(11.0), 2 * flowers(30.0));
  EXPECT_EQ(flowers(70.0), 0);
  // Every tile flowering at half bloom still flowers at full.
  for (uint32_t h = 0; h < 5000; ++h) {
    const uint32_t hash = noise::hash(h, 4, 5);
    const Id half = OverworldRenderer::decalFor(Biome::GRASSLAND, 0, false, hash, 4.5);
    if (half != COUNT)
      EXPECT_NE(OverworldRenderer::decalFor(Biome::GRASSLAND, 0, false, hash, 11.0), COUNT);
  }
}

// Every bush stands bare in winter (as a shrub or a twig), and none keeps a
// leafy frame under snow.
TEST(SeasonalLookTest, BushesAreBareShrubsInWinter) {
  using namespace owsprite;
  for (Biome b : {Biome::GRASSLAND, Biome::FOREST, Biome::WETLAND, Biome::COASTAL})
    for (int v = 0; v < 256; ++v) {
      if (!OverworldRenderer::isDeciduous(PropType::BUSH, b, 0, (uint8_t)v))
        continue; // the swamp plant
      for (Look l : {Look::BARE, Look::SNOWED}) {
        const Id id = OverworldRenderer::spriteFor(PropType::BUSH, b, 0, (uint8_t)v, l);
        EXPECT_TRUE(id == SHRUB_BARE_A || id == SHRUB_BARE_B || id == SHRUB_TWIG) << v;
      }
    }
}

TEST(SeasonalLookTest, CoastalPlantsStayInLeafAllYear) {
  for (PropType type : {PropType::TREE, PropType::BUSH})
    for (int v = 0; v < 256; ++v) {
      EXPECT_FALSE(OverworldRenderer::isDeciduous(type, Biome::COASTAL, 0, (uint8_t)v));
      const owsprite::Id leaf = OverworldRenderer::spriteFor(type, Biome::COASTAL, 0, (uint8_t)v);
      for (int l = 0; l < (int)Look::COUNT; ++l)
        EXPECT_EQ(OverworldRenderer::spriteFor(type, Biome::COASTAL, 0, (uint8_t)v, (Look)l), leaf);
    }
}
