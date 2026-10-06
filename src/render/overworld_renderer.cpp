#include "render/overworld_renderer.hpp"
#include "core/asset_load.hpp"
#include "core/grid.hpp"
#include "render/theme.hpp"
#include "render/view_bounds.hpp"
#include "world/noise.hpp"
#include <algorithm>
#include <cmath>
#include <span>

namespace {

// ---- terrain ---------------------------------------------------------------

// The tiled layers, bottom to top. Each covers its own cells; the ones below
// show through its transparent edges, so a layer also covers the cells of any
// layer that sits on IT.
//
// GROUND is all land: its edge is the sand-and-foam coast where the ocean is,
// a mud bank on lakes and rivers. MEADOW, FOREST and FOREST_DEEP are SHADE
// OVERLAYS on grass: the ground fading by the tile's shade step
// (TileSample::shade). Wetland, mountain, snow and swamp water are not
// drawn as tiles - they go through the fade shader (see drawFade).
enum Layer { GROUND, GRASS, MEADOW, FOREST, FOREST_DEEP, LAYER_COUNT };

bool inLayer(int layer, Biome b, uint8_t shade) {
  switch (layer) {
  case GROUND: return !isWater(b);
  case GRASS: return !isWater(b) && b != Biome::BEACH;
  case MEADOW: return (b == Biome::GRASSLAND && shade >= 1) || b == Biome::FOREST;
  case FOREST: return b == Biome::FOREST;
  case FOREST_DEEP: return b == Biome::FOREST && shade == 3;
  default: return false;
  }
}

// assets/ow_terrain.png: grass (the corner shape in column `mask`, full fills
// in columns kFillCol..), sand fills, then the bank rows - mud, and two steps
// paler toward the sand for banks near a beach. GROUND draws a bank row's
// edges and its fill is sand or mud.
// assets/ow_shades.png: one row per overlay, 81 three-state corner tiles
// (TL*27 + TR*9 + BL*3 + BR; 0 off the grass, 1 grass, 2 shaded), then fills.
constexpr int kGrassRow = 0, kSandRow = 1, kBankRow = 2; // + 0..2 toward sand
constexpr int kShadeFillCol = 81;
constexpr int kFillCol = 16, kFillCount = 4;

// The fade materials: rows of assets/ow_fades.png, and the `layer` the fade
// shader is told. Land fades draw over the grass, clipped to its shape; swamp
// water draws over the water through corner masks, like a shade overlay.
enum FadeMaterial { FADE_SWAMP, FADE_WETLAND, FADE_GRAVEL, FADE_SNOW, FADE_DRIFT };

// assets/ow_coast.png: High Tides' sand coast. The island block (sand blob in
// water) holds the shapes with 1-2 sand corners, the lake block (water hole in
// sand) the 3-corner ones; -1 = no tile (empty, full, and the two diagonals).
struct CoastTile {
  int col, row;
};
constexpr CoastTile kCoast[16] = {
    {-1, -1}, {5, 0}, {7, 0}, {6, 0},  {5, 2}, {5, 1}, {-1, -1}, {3, 2},
    {7, 2},   {-1, -1}, {7, 1}, {1, 2}, {6, 2}, {3, 0}, {1, 0},  {-1, -1}};
constexpr int kCoastFrameRows = 4; // frame f starts at row 1 + 4f
constexpr int kCoastFrames = 3;
constexpr float kCoastFrameSeconds = 0.4f;

// River water's speed, in art px per second along its flow step.
constexpr float kFlowSpeed = 6.0f;
// Still water (lakes, swamps) sways diagonally: up to this many art px either
// way, once per period. Whole px, so the art stays crisp.
constexpr float kSwayPx = 1.5f, kSwaySeconds = 5.0f;
// A glint: on this share of open-water tiles, each flashing once per cycle
// at a moment of its own, for its 3 frames (assets/ow_glints.png).
constexpr uint32_t kGlintPerMille = 28;
constexpr float kGlintCycle = 3.0f, kGlintFrameSeconds = 0.12f;
constexpr int kGlintFrames = 3;

// Per-purpose salts, so the fill, glint and prop hashes are unrelated.
constexpr uint32_t kFillSalt = 0x27d4eb2fu, kGlintSalt = 0x9e3779b9u;

// The still water's sway offset now, in art px (the same on both axes).
int swayAt(float time) {
  return (int)std::lround(kSwayPx * std::sin(time * 2.0f * 3.14159265f / kSwaySeconds));
}

// ---- props -----------------------------------------------------------------

struct Pick {
  owsprite::Id id;
  int weight;
};
using namespace owsprite;

// Full-orange autumn (LightBorne autumn oak/birch, Pixel Crawler orange,
// amber and rust) waits for Phase 5's seasons; the light-brown autumn trees
// (*_TAN) already appear, rarely.
constexpr Pick kGrasslandTrees[] = {
    {OAK_SUMMER_A, 3},  {OAK_SUMMER_B, 3},  {BIRCH_SUMMER_A, 3}, {BIRCH_SUMMER_B, 3},
    {PC1_S2_GREEN, 3},  {PC1_S3_GREEN, 3},  {PC3_S2_GREEN, 2},   {PC3_S3_GREEN, 1},
    {PC1_S2_TAN, 1},    {PC3_S2_TAN, 1}};
constexpr Pick kForestTrees[] = {
    {OAK_SUMMER_A, 3},  {OAK_SUMMER_B, 3},  {BIRCH_SUMMER_A, 2}, {BIRCH_SUMMER_B, 2},
    {PC1_S3_GREEN, 4},  {PC1_S4_GREEN, 2},  {PC1_S5_GREEN, 1},   {PC3_S2_OLIVE, 1},
    {PC3_S3_GREEN, 3},  {PC3_S3_OLIVE, 1},  {PC3_S4_GREEN, 1},   {PC3_S4_OLIVE, 1},
    {PC3_S5_GREEN, 1},  {PC3_S5_OLIVE, 1},  {FIR_0, 2},          {FIR_1, 2},
    {FIR_2, 2},         {PC1_S3_TAN, 1},    {PC3_S3_TAN, 1}};
constexpr Pick kWetlandTrees[] = {{WILLOW, 2}, {WILLOW_S_A, 2}, {WILLOW_S_B, 2},
                                  {WILLOW_S_C, 2}};
constexpr Pick kBeachTrees[] = {{PALM_TALL, 1}, {PALM_SHORT, 1}};
constexpr Pick kMountainPines[] = {
    {PC2_S2_TEAL, 2},   {PC2_S2_TEAL_B, 2}, {PC2_S2_GREEN, 2},   {PC2_S2_GREEN_B, 2},
    {PC2_S3_TEAL, 2},   {PC2_S3_TEAL_B, 2}, {PC2_S3_GREEN, 2},   {PC2_S3_GREEN_B, 2},
    {PC2_S4_TEAL, 1},   {PC2_S4_TEAL_B, 1}, {PC2_S4_GREEN, 1},   {PC2_S4_GREEN_B, 1},
    {PC2_S5_TEAL, 1},   {PC2_S5_TEAL_B, 1}, {PC2_S5_GREEN, 1},   {PC2_S5_GREEN_B, 1},
    {FIR_0, 1},         {FIR_1, 1},         {FIR_2, 1},          {FIR_3, 1},
    {FIR_4, 1},         {PC2_S3_BARE, 1},   {PC2_S4_BARE, 1},    {PC2_S5_BARE, 1},
    {PC3_S4_BARE, 1},   {PC3_S5_BARE, 1}};
constexpr Pick kSnowPines[] = {
    {PC1_S2_FROZEN, 2}, {PC1_S3_FROZEN, 2}, {PC1_S4_FROZEN, 1},  {PC1_S5_FROZEN, 1},
    {FIR_DARK_0, 1},    {FIR_DARK_1, 1},    {FIR_DARK_2, 1},     {FIR_DARK_3, 1},
    {OAK_WINTER_A, 1},  {OAK_WINTER_B, 1},  {BIRCH_WINTER_A, 1}, {BIRCH_WINTER_B, 1},
    {BIRCH_BARE_A, 1},  {BIRCH_BARE_B, 1},  {OAK_BARE_A, 1},     {OAK_BARE_B, 1},
    {PC1_S2_BARE, 1},   {PC1_S3_BARE, 1},   {PC1_S4_BARE, 1},    {PC1_S5_BARE, 1},
    {PC3_S3_BARE, 1}};
constexpr Pick kBushes[] = {{BUSH_A, 2}, {BUSH_B, 2},   {BUSH_C, 2},
                            {BUSH_D, 2}, {BUSH_LOW, 1}, {BUSH_SMALL, 1}};
constexpr Pick kWetlandBushes[] = {{SWAMP_PLANT, 2}, {BUSH_A, 1}, {BUSH_C, 1}};
constexpr Pick kRocks[] = {{ROCK_GREY_0, 1}, {ROCK_GREY_1, 1}, {ROCK_GREY_2, 1},
                           {ROCK_GREY_3, 1}, {ROCK_GREY_4, 1}, {ROCK_GREY_BIG, 1},
                           {ROCK_MOSS_1, 1}, {ROCK_MOSS_2, 1}, {ROCK_MOSS_3, 1}};
constexpr Pick kBeachRocks[] = {{ROCK_GREY_0, 1}, {ROCK_GREY_1, 1}, {ROCK_GREY_2, 1},
                                {ROCK_GREY_3, 1}, {ROCK_GREY_4, 1}};
constexpr Pick kMountainRocks[] = {{BOULDER_BROWN, 2}, {BOULDER_BROWN_LOW, 2},
                                   {BOULDER_GREY, 2},  {BOULDER_GREY_LOW, 2},
                                   {ROCK_GREY_BIG, 1}, {ROCK_GREY_2, 1}};
constexpr Pick kSnowRocks[] = {{BOULDER_GREY, 2}, {BOULDER_GREY_LOW, 2},
                               {ROCK_GREY_BIG, 1}, {ROCK_GREY_2, 1}};
constexpr Pick kReeds[] = {{CATTAIL_TALL, 2}, {CATTAIL_SHORT, 2}, {SWAMP_PLANT, 1}};

std::span<const Pick> poolFor(PropType type, Biome b) {
  switch (type) {
  case PropType::TREE:
    if (b == Biome::WETLAND) return kWetlandTrees;
    if (b == Biome::BEACH) return kBeachTrees;
    if (b == Biome::GRASSLAND) return kGrasslandTrees;
    return kForestTrees;
  case PropType::PINE:
    return b == Biome::SNOW ? std::span<const Pick>(kSnowPines) : kMountainPines;
  case PropType::BUSH:
    return b == Biome::WETLAND ? std::span<const Pick>(kWetlandBushes) : kBushes;
  case PropType::ROCK:
    if (b == Biome::MOUNTAIN) return kMountainRocks;
    if (b == Biome::SNOW) return kSnowRocks;
    if (b == Biome::BEACH) return kBeachRocks;
    return kRocks;
  case PropType::REEDS: return kReeds;
  default: return {};
  }
}

// Ground details: flat, walk-over, picked per tile from a hash like the
// species, and drawn with the terrain. (chance per tile, pool)
constexpr Pick kGrassDecals[] = {{DECAL_FLOWERS_A, 2}, {DECAL_FLOWERS_B, 2}, {DECAL_FLOWER, 3},
                                 {DECAL_STARS_A, 2},   {DECAL_STARS_B, 2},   {DECAL_TUFT_A, 3},
                                 {DECAL_TUFT_B, 3},    {DECAL_PEBBLE_GREY, 1}};
constexpr Pick kMeadowDecals[] = {{DECAL_TUFT_A, 3},  {DECAL_TUFT_B, 3},   {DECAL_FLOWER, 2},
                                  {DECAL_PATCH_A, 2}, {DECAL_PATCH_B, 2}, {DECAL_STARS_A, 1}};
constexpr Pick kForestDecals[] = {{DECAL_PATCH_A, 3},      {DECAL_PATCH_B, 3},
                                  {DECAL_PATCH_C, 2},      {DECAL_TWIGS, 3},
                                  {DECAL_FERN, 3},         {DECAL_MUSHROOM_RED, 1},
                                  {DECAL_MUSHROOM_BROWN, 2}, {DECAL_MUSHROOMS, 1},
                                  {DECAL_PEBBLE_MOSS, 1}};
constexpr Pick kWetlandDecals[] = {{DECAL_PATCH_A, 3}, {DECAL_PATCH_C, 3}, {DECAL_FERN, 3},
                                   {DECAL_MUSHROOMS, 1}, {DECAL_PEBBLE_MOSS, 1}};
constexpr Pick kMountainDecals[] = {{DECAL_PEBBLE_GREY, 3}, {DECAL_PEBBLES_GREY, 3},
                                    {DECAL_PEBBLE_BROWN, 2}, {DECAL_DEAD_TUFT_B, 1}};
constexpr Pick kSnowDecals[] = {{DECAL_PEBBLE_SNOW, 3}, {DECAL_PEBBLES_SNOW, 2},
                                {DECAL_STICK, 2},       {DECAL_MOUND_A, 3},
                                {DECAL_MOUND_B, 3},     {DECAL_ICE_A, 1},
                                {DECAL_ICE_B, 1}};
constexpr Pick kBeachDecals[] = {{DECAL_SHELL, 3}, {DECAL_SHELL_PINK, 2}, {DECAL_PEBBLE_GREY, 1}};
constexpr Pick kFreshWaterDecals[] = {{DECAL_STONE_WATER_A, 2}, {DECAL_STONE_WATER_B, 2},
                                      {DECAL_STONE_WATER_C, 2}, {DECAL_STONE_WATER_MOSS, 1}};

struct DecalOdds {
  float chance;
  std::span<const Pick> pool;
};

DecalOdds decalOdds(Biome b, uint8_t shade) {
  switch (b) {
  case Biome::GRASSLAND: return shade ? DecalOdds{0.10f, kMeadowDecals} : DecalOdds{0.10f, kGrassDecals};
  case Biome::FOREST: return {0.14f, kForestDecals};
  case Biome::WETLAND: return {0.07f, kWetlandDecals};
  case Biome::MOUNTAIN: return {0.10f, kMountainDecals};
  case Biome::SNOW: return {0.08f, kSnowDecals};
  case Biome::BEACH: return {0.04f, kBeachDecals};
  // Of the water tiles near a shore only (drawDecals): stones in mid-lake
  // read as litter.
  case Biome::LAKE: case Biome::RIVER: return {0.01f, kFreshWaterDecals};
  default: return {0.0f, {}};
  }
}

owsprite::Id pickFrom(std::span<const Pick> pool, uint32_t r) {
  int total = 0;
  for (const Pick &p : pool)
    total += p.weight;
  int k = (int)(r % (uint32_t)total);
  for (const Pick &p : pool) {
    if (k < p.weight)
      return p.id;
    k -= p.weight;
  }
  return pool.back().id;
}

constexpr uint32_t kDecalSalt = 0x68e31da4u;

// Roots in a river lap through four stages, each this long; a tree starts at
// a stage of its own so the bank does not pulse in step.
constexpr float kLapSeconds = 0.6f;
constexpr uint32_t kLapSalt = 0x3c6ef372u;

constexpr float kCoverAlpha = 0.45f;

constexpr int widestFrame() {
  int w = 0;
  for (const owsprite::Frame &f : owsprite::kFrames)
    w = std::max(w, f.w);
  return w;
}
// A frame is centred on its tile, so half the widest reaches sideways.
constexpr int kReachSideTiles = widestFrame() / 2 + 1;

} // namespace

OverworldRenderer::~OverworldRenderer() {
  if (!IsWindowReady())
    return;
  for (Texture2D t : {m_water, m_river, m_swampWater, m_glints, m_coast, m_terrain, m_shades,
                      m_fades, m_props, m_propsWet, m_weights, m_info})
    if (t.id != 0)
      UnloadTexture(t);
  if (m_fade.id != 0)
    UnloadShader(m_fade);
}

void OverworldRenderer::loadTextures() {
  m_water = assets::loadTexture("assets/ow_water.png", "OverworldRenderer");
  m_river = assets::loadTexture("assets/ow_river.png", "OverworldRenderer");
  // Sampled past their edges on purpose: water is read by world position.
  SetTextureWrap(m_water, TEXTURE_WRAP_REPEAT);
  SetTextureWrap(m_river, TEXTURE_WRAP_REPEAT);
  m_swampWater = assets::loadTexture("assets/ow_swamp_water.png", "OverworldRenderer");
  m_glints = assets::loadTexture("assets/ow_glints.png", "OverworldRenderer");
  m_coast = assets::loadTexture("assets/ow_coast.png", "OverworldRenderer");
  m_terrain = assets::loadTexture("assets/ow_terrain.png", "OverworldRenderer");
  m_shades = assets::loadTexture("assets/ow_shades.png", "OverworldRenderer");
  m_fades = assets::loadTexture("assets/ow_fades.png", "OverworldRenderer");
  m_props = assets::loadTexture("assets/ow_props.png", "OverworldRenderer");
  m_propsWet = assets::loadTexture("assets/ow_props_wet.png", "OverworldRenderer");
  m_fade = assets::loadShader(0, "assets/ow_fade.fs", "OverworldRenderer");
  m_locWeights = GetShaderLocation(m_fade, "weights");
  m_locInfo = GetShaderLocation(m_fade, "info");
  m_locOrigin = GetShaderLocation(m_fade, "cellOrigin");
  m_locLayer = GetShaderLocation(m_fade, "layer");
  m_locSwampWater = GetShaderLocation(m_fade, "swampWater");
  m_locSway = GetShaderLocation(m_fade, "sway");
}

Color OverworldRenderer::biomeColour(Biome b) {
  switch (b) {
  case Biome::OCEAN: return theme::ocean;
  case Biome::LAKE: return theme::lake;
  case Biome::RIVER: return theme::river;
  case Biome::SWAMP: return theme::swamp;
  case Biome::BEACH: return theme::beach;
  case Biome::GRASSLAND: return theme::grassland;
  case Biome::FOREST: return theme::forest;
  case Biome::WETLAND: return theme::wetland;
  case Biome::MOUNTAIN: return theme::mountain;
  case Biome::SNOW: return theme::snow;
  default: return theme::ground;
  }
}

owsprite::Id OverworldRenderer::spriteFor(PropType type, Biome biome,
                                          uint8_t variant) {
  std::span<const Pick> pool = poolFor(type, biome);
  return pool.empty() ? owsprite::COUNT : pickFrom(pool, variant);
}

owsprite::Id OverworldRenderer::decalFor(Biome biome, uint8_t shade, uint32_t hash) {
  DecalOdds odds = decalOdds(biome, shade);
  // Low 16 bits roll the chance, the rest pick from the pool.
  if (odds.pool.empty() || (hash & 0xffffu) >= (uint32_t)(odds.chance * 65536.0f))
    return owsprite::COUNT;
  return pickFrom(odds.pool, hash >> 16);
}

// A summed-area table (integral image): S(x, y) is the sum over the cells
// above and left of (x, y), so any rectangle's sum is four reads.
void OverworldRenderer::shareWithin(const std::vector<uint8_t> &in,
                                    const std::vector<uint8_t> &count, int w, int h,
                                    int radius, std::vector<float> &out) {
  const int sw = w + 1;
  std::vector<int> sumIn((size_t)sw * (h + 1), 0), sumCount(sumIn.size(), 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int k = y * w + x, a = (y + 1) * sw + x + 1;
      sumIn[a] = (in[k] && count[k]) + sumIn[a - 1] + sumIn[a - sw] - sumIn[a - sw - 1];
      sumCount[a] = (count[k] != 0) + sumCount[a - 1] + sumCount[a - sw] - sumCount[a - sw - 1];
    }
  }
  auto box = [&](const std::vector<int> &s, int xa, int ya, int xb, int yb) {
    return s[yb * sw + xb] - s[ya * sw + xb] - s[yb * sw + xa] + s[ya * sw + xa];
  };
  out.resize((size_t)w * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int xa = std::max(0, x - radius), xb = std::min(w, x + radius + 1);
      const int ya = std::max(0, y - radius), yb = std::min(h, y + radius + 1);
      const int c = box(sumCount, xa, ya, xb, yb);
      out[y * w + x] = c > 0 ? (float)box(sumIn, xa, ya, xb, yb) / (float)c : 0.0f;
    }
  }
}

void OverworldRenderer::renderTerrain(const Overworld &world,
                                      const Camera2D &camera,
                                      const Viewport &canvas, float time) {
  const ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);
  const int x0 = view.startX, y0 = view.startY;
  const int w = view.endX - x0 + 1, h = view.endY - y0 + 1;
  const uint32_t seed = world.island().seed();
  m_time = time;
  m_world = &world;

  // Dual-grid vertices x0..x0+w each read the cells on both sides, and a
  // fade weight reads kFadeRadius further: the cache reaches that far out.
  constexpr int kMargin = 1 + kFadeRadius;
  m_cellsX0 = x0 - kMargin;
  m_cellsY0 = y0 - kMargin;
  m_cellsW = w + 2 * kMargin;
  m_cellsH = h + 2 * kMargin;
  m_cells.resize((size_t)m_cellsW * m_cellsH);
  for (int j = 0; j < m_cellsH; ++j) {
    for (int i = 0; i < m_cellsW; ++i) {
      const int x = m_cellsX0 + i, y = m_cellsY0 + j;
      m_cells[j * m_cellsW + i] = {world.biomeAt(x, y), world.shadeAt(x, y),
                                   world.flowAt(x, y), 0};
    }
  }
  buildFades(x0, y0, w, h);

  // Whole art px travelled, so the water steps crisply; a diagonal step
  // covers sqrt(2) px, so it advances that much less often.
  const int run = (int)(time * kFlowSpeed), runDiagonal = (int)(time * kFlowSpeed * 0.7071f);
  const int sway = swayAt(time);
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      const Cell &c = cellAt(x, y);
      if (c.flow != 0) {
        int dx, dy;
        flowStep(c.flow, dx, dy);
        const int s = dx != 0 && dy != 0 ? runDiagonal : run;
        // Sampling upstream of the tile moves the picture downstream.
        Rectangle src = {(float)(x * grid::SOURCE_TILE - dx * s),
                         (float)(y * grid::SOURCE_TILE - dy * s), (float)grid::SOURCE_TILE,
                         (float)grid::SOURCE_TILE};
        DrawTexturePro(m_river, src, grid::destFor(src, x * grid::CELL, y * grid::CELL),
                       {0, 0}, 0.0f, WHITE);
        continue;
      }
      // Still water, by world position; lakes sway, the sea does not.
      const int s = c.biome == Biome::OCEAN ? 0 : sway;
      Rectangle src = {(float)(x * grid::SOURCE_TILE + s), (float)(y * grid::SOURCE_TILE + s),
                       (float)grid::SOURCE_TILE, (float)grid::SOURCE_TILE};
      DrawTexturePro(m_water, src, grid::destFor(src, x * grid::CELL, y * grid::CELL),
                     {0, 0}, 0.0f, WHITE);
    }
  }
  drawGlints(x0, y0, w, h);

  const int frame = (int)(time / kCoastFrameSeconds) % kCoastFrames;
  drawFade(FADE_SWAMP, x0, y0, w, h);
  for (int layer = 0; layer < LAYER_COUNT; ++layer)
    drawLayer(layer, x0, y0, w, h, frame);
  for (int fade : {FADE_WETLAND, FADE_GRAVEL, FADE_SNOW, FADE_DRIFT})
    drawFade(fade, x0, y0, w, h);
  drawDecals(x0, y0, w, h);
}

// Glints on open water (not swamp): a few tiles each flash once per cycle,
// at a hashed moment and spot. Drawn under the banks, which hide any that
// fall on a shore.
void OverworldRenderer::drawGlints(int x0, int y0, int w, int h) const {
  const uint32_t seed = m_world->island().seed();
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      const Biome b = cellAt(x, y).biome;
      if (b != Biome::LAKE && b != Biome::RIVER && b != Biome::OCEAN)
        continue;
      const uint32_t hash = noise::hash(m_world->wrapX(x), m_world->wrapY(y), seed ^ kGlintSalt);
      if (hash % 1000 >= kGlintPerMille)
        continue;
      const float phase = (float)((hash >> 10) % 1024) / 1024.0f * kGlintCycle;
      const int frame = (int)(std::fmod(m_time + phase, kGlintCycle) / kGlintFrameSeconds);
      if (frame >= kGlintFrames)
        continue;
      // Off the tile's centre by up to 4 art px each way.
      const int ox = (int)((hash >> 20) % 9) - 4, oy = (int)((hash >> 26) % 9) - 4;
      const Rectangle src = grid::srcTile(frame, 0);
      DrawTexturePro(m_glints, src,
                     grid::destFor(src, (float)(x * grid::CELL + ox * grid::WORLD_SCALE),
                                   (float)(y * grid::CELL + oy * grid::WORLD_SCALE)),
                     {0, 0}, 0.0f, WHITE);
    }
  }
}

// Per cell of the view (plus the one-cell rim the dual grid reads): how much
// of each land fade material is there - its share of the grass's cells
// within kFadeRadius, so a shore never thins a fade - and which cells are
// land and which swamp water. Uploaded as two small textures; the shader
// reads the weights bilinear and the flags per cell. Also marks how near a
// beach each cache cell is, for the bank rows.
void OverworldRenderer::buildFades(int x0, int y0, int w, int h) {
  const int n = m_cellsW * m_cellsH;
  std::vector<uint8_t> land(n), all(n, 1);
  std::vector<uint8_t> in[6]; // wetland, gravel, snow, drift, swamp, beach
  for (auto &v : in)
    v.resize(n);
  for (int k = 0; k < n; ++k) {
    const Cell &c = m_cells[k];
    land[k] = inLayer(GRASS, c.biome, c.shade);
    in[0][k] = c.biome == Biome::WETLAND;
    in[1][k] = c.biome == Biome::MOUNTAIN || c.biome == Biome::SNOW; // gravel runs under snow
    in[2][k] = c.biome == Biome::SNOW;
    in[3][k] = c.biome == Biome::SNOW && c.shade >= 1;
    in[4][k] = c.biome == Biome::SWAMP;
    in[5][k] = c.biome == Biome::BEACH;
  }
  std::vector<float> share[4], beach1, beach2;
  for (int f = 0; f < 4; ++f)
    shareWithin(in[f], land, m_cellsW, m_cellsH, kFadeRadius, share[f]);
  shareWithin(in[5], all, m_cellsW, m_cellsH, 1, beach1);
  shareWithin(in[5], all, m_cellsW, m_cellsH, 2, beach2);
  for (int k = 0; k < n; ++k)
    m_cells[k].bank = beach1[k] > 0.0f ? 2 : beach2[k] > 0.0f ? 1 : 0;

  // The data covers cells x0-1 .. x0+w (and the same in y).
  const int tw = w + 2, th = h + 2;
  m_fadeX0 = x0 - 1;
  m_fadeY0 = y0 - 1;
  m_weightPx.resize((size_t)tw * th);
  m_infoPx.resize((size_t)tw * th);
  auto byte = [](float s) { return (unsigned char)std::lround(s * 255.0f); };
  for (bool &u : m_fadeUsed)
    u = false;
  for (int j = 0; j < th; ++j) {
    for (int i = 0; i < tw; ++i) {
      const int k = (j + kFadeRadius) * m_cellsW + (i + kFadeRadius);
      Color &wpx = m_weightPx[j * tw + i];
      wpx = {byte(share[0][k]), byte(share[1][k]), byte(share[2][k]), byte(share[3][k])};
      m_infoPx[j * tw + i] = {(unsigned char)(land[k] ? 255 : 0),
                              (unsigned char)(in[4][k] ? 255 : 0), 0, 255};
      m_fadeUsed[FADE_SWAMP] |= in[4][k] != 0;
      m_fadeUsed[FADE_WETLAND] |= wpx.r > 0;
      m_fadeUsed[FADE_GRAVEL] |= wpx.g > 0;
      m_fadeUsed[FADE_SNOW] |= wpx.b > 0;
      m_fadeUsed[FADE_DRIFT] |= wpx.a > 0;
    }
  }
  for (Texture2D *t : {&m_weights, &m_info}) {
    if (t->width != tw || t->height != th) {
      if (t->id != 0)
        UnloadTexture(*t);
      Image img = GenImageColor(tw, th, BLANK);
      *t = LoadTextureFromImage(img);
      UnloadImage(img);
      SetTextureFilter(*t, TEXTURE_FILTER_BILINEAR);
    }
  }
  UpdateTexture(m_weights, m_weightPx.data());
  UpdateTexture(m_info, m_infoPx.data());
}

// One fade material over the whole view, as a single quad whose texture
// coordinates are world art pixels; assets/ow_fade.fs does the rest. Each
// call is its own shader-mode block: raylib batches draws, and a uniform set
// mid-batch would apply to quads queued before it.
void OverworldRenderer::drawFade(int fade, int x0, int y0, int w, int h) const {
  if (!m_fadeUsed[fade] || m_fade.id == 0)
    return;
  BeginShaderMode(m_fade);
  SetShaderValueTexture(m_fade, m_locWeights, m_weights);
  SetShaderValueTexture(m_fade, m_locInfo, m_info);
  const int origin[2] = {m_fadeX0, m_fadeY0};
  SetShaderValue(m_fade, m_locOrigin, origin, SHADER_UNIFORM_IVEC2);
  SetShaderValue(m_fade, m_locLayer, &fade, SHADER_UNIFORM_INT);
  SetShaderValueTexture(m_fade, m_locSwampWater, m_swampWater);
  const int sway[2] = {swayAt(m_time), swayAt(m_time)};
  SetShaderValue(m_fade, m_locSway, sway, SHADER_UNIFORM_IVEC2);
  const Rectangle src = {(float)(x0 * grid::SOURCE_TILE), (float)(y0 * grid::SOURCE_TILE),
                         (float)(w * grid::SOURCE_TILE), (float)(h * grid::SOURCE_TILE)};
  DrawTexturePro(m_fades, src, grid::destFor(src, x0 * grid::CELL, y0 * grid::CELL),
                 {0, 0}, 0.0f, WHITE);
  EndShaderMode();
}

// Ground details sit flat on the terrain, so they draw here rather than in
// the DrawQueue. A detail wider than its tile needs row neighbours of its own
// biome - a tuft would otherwise hang over a shore or a biome edge - and none
// goes under a prop. Stones in water keep near a shore, but off the bank.
void OverworldRenderer::drawDecals(int x0, int y0, int w, int h) const {
  const uint32_t seed = m_world->island().seed();
  // Land within `r` tiles; r <= 3 stays inside the cell cache.
  auto landWithin = [&](int x, int y, int r) {
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        if (!isWater(cellAt(x + dx, y + dy).biome))
          return true;
    return false;
  };
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      const Cell &c = cellAt(x, y);
      owsprite::Id id = decalFor(c.biome, c.shade,
                                 noise::hash(m_world->wrapX(x), m_world->wrapY(y), seed ^ kDecalSalt));
      if (id == owsprite::COUNT || m_world->propAt(x, y) != PropType::NONE)
        continue;
      if (owsprite::kFrames[id].w > 1 &&
          (cellAt(x - 1, y).biome != c.biome || cellAt(x + 1, y).biome != c.biome))
        continue;
      if (isWater(c.biome) && (landWithin(x, y, 1) || !landWithin(x, y, 3)))
        continue;
      const owsprite::Frame &f = owsprite::kFrames[id];
      Rectangle src = grid::srcTile(f.col, f.row, f.w, f.h);
      DrawTexturePro(m_props, src, grid::standingOn(src, x, y), {0, 0}, 0.0f, WHITE);
    }
  }
}

void OverworldRenderer::drawLayer(int layer, int x0, int y0, int w, int h,
                                  int frame) const {
  const uint32_t seed = m_world->island().seed();
  auto draw = [](Texture2D tex, Rectangle src, int vx, int vy) {
    // A dual tile is centred on the corner shared by four cells.
    DrawTexturePro(tex, src,
                   grid::destFor(src, vx * grid::CELL - grid::CELL / 2.0f,
                                 vy * grid::CELL - grid::CELL / 2.0f),
                   {0, 0}, 0.0f, WHITE);
  };
  const int coastRow0 = 1 + frame * kCoastFrameRows;

  for (int vy = y0; vy <= y0 + h; ++vy) {
    for (int vx = x0; vx <= x0 + w; ++vx) {
      // Vertex (vx, vy) is the top-left corner of cell (vx, vy).
      const Cell *q[4] = {&cellAt(vx - 1, vy - 1), &cellAt(vx, vy - 1), &cellAt(vx - 1, vy),
                          &cellAt(vx, vy)};
      const uint32_t hash =
          noise::hash(m_world->wrapX(vx), m_world->wrapY(vy), seed ^ kFillSalt);
      const int v = (int)(hash % kFillCount);

      if (layer >= MEADOW) {
        // A shade overlay: dithers toward plain grass, cuts against the rest.
        int idx = 0;
        bool any = false, all = true;
        for (const Cell *k : q) {
          int st = inLayer(layer, k->biome, k->shade) ? 2 : inLayer(GRASS, k->biome, k->shade) ? 1 : 0;
          idx = idx * 3 + st;
          any = any || st == 2;
          all = all && st == 2;
        }
        if (any)
          draw(m_shades, grid::srcTile(all ? kShadeFillCol + v : idx, layer - MEADOW), vx, vy);
        continue;
      }
      bool in[4];
      bool beach = false, sandy = false;
      int bank = 0;
      for (int k = 0; k < 4; ++k) {
        in[k] = inLayer(layer, q[k]->biome, q[k]->shade);
        beach = beach || q[k]->biome == Biome::BEACH;
        // Sand coast where the sea or a beach is involved (a river mouth on
        // a beach would otherwise show a mud bank's body on the sand).
        sandy = sandy || q[k]->biome == Biome::OCEAN || q[k]->biome == Biome::BEACH;
        bank = std::max(bank, (int)q[k]->bank);
      }
      int mask = cornerMask(in[0], in[1], in[2], in[3]);
      if (mask == 0)
        continue;
      if (mask == 15) {
        const int fillRow = layer == GRASS ? kGrassRow : beach ? kSandRow : kBankRow;
        draw(m_terrain, grid::srcTile(kFillCol + v, fillRow), vx, vy);
      } else if (layer == GRASS) {
        draw(m_terrain, grid::srcTile(mask, kGrassRow), vx, vy);
      } else if (!sandy) {
        // A bank paler the nearer a beach is, so the mud runs into the sand.
        draw(m_terrain, grid::srcTile(mask, kBankRow + bank), vx, vy);
      } else if (mask == 6 || mask == 9) {
        // The coast set has no diagonal: two single corners make one.
        for (int part : {mask & 0b1100, mask & 0b0011})
          draw(m_coast, grid::srcTile(kCoast[part].col, coastRow0 + kCoast[part].row), vx, vy);
      } else {
        draw(m_coast, grid::srcTile(kCoast[mask].col, coastRow0 + kCoast[mask].row), vx, vy);
      }
    }
  }
}

void OverworldRenderer::collect(const Overworld &world, const Camera2D &camera,
                                const Viewport &canvas, DrawQueue &queue) {
  m_world = &world;
  ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);
  for (int y = view.startY; y <= view.endY + kReachBelowTiles; ++y)
    for (int x = view.startX - kReachSideTiles; x <= view.endX + kReachSideTiles; ++x)
      if (world.propAt(x, y) != PropType::NONE)
        queue.push((y + 1) * grid::CELL, *this, x, y);
}

void OverworldRenderer::drawQueued(int x, int y) const {
  const Prop *prop = m_world->findProp(x, y);
  if (!prop)
    return;
  owsprite::Id id = spriteFor(prop->type, m_world->biomeAt(x, y), prop->variant);
  if (id == owsprite::COUNT)
    return;
  const owsprite::Frame &f = owsprite::kFrames[id];
  Rectangle src = grid::srcTile(f.col, f.row, f.w, f.h);
  Rectangle dest = grid::standingOn(src, x, y);
  // In front = rooted lower than the player; the queue already drew it later.
  bool covers = (y + 1) * grid::CELL > m_focus.y &&
                CheckCollisionPointRec(m_focus, dest);
  const Color tint = covers ? Fade(WHITE, kCoverAlpha) : WHITE;

  // The widest roots spill onto the cells beside their tile. Over a river,
  // that slice of the bottom row laps: dry, foam, sunk, foam (stage 0-3).
  // Still water stays still. Frames are centred on the tile, so a slice
  // boundary can fall mid-column; slices are cut at world cell edges, in
  // canvas px.
  const float bottomY = dest.y + dest.height - grid::CELL;
  const int cx0 = (int)std::floor(dest.x / grid::CELL);
  const int cx1 = (int)std::ceil((dest.x + dest.width) / grid::CELL) - 1;
  auto laps = [&](int cx) { return cx != x && m_world->flowAt(cx, y) != 0; };
  bool wet = false;
  for (int cx = cx0; cx <= cx1 && f.wetRow >= 0 && !wet; ++cx)
    wet = laps(cx);
  const int phase = (int)(noise::hash(m_world->wrapX(x), m_world->wrapY(y),
                                      m_world->island().seed() ^ kLapSalt) % 4);
  const int stage = ((int)(m_time / kLapSeconds) + phase) % 4;
  if (!wet || stage == 0) {
    DrawTexturePro(m_props, src, dest, {0, 0}, 0.0f, tint);
    return;
  }
  Rectangle upper = src;
  upper.height -= grid::SOURCE_TILE;
  DrawTexturePro(m_props, upper, {dest.x, dest.y, dest.width, dest.height - grid::CELL},
                 {0, 0}, 0.0f, tint);
  for (int cx = cx0; cx <= cx1; ++cx) {
    const float left = std::max(dest.x, (float)cx * grid::CELL);
    const float right = std::min(dest.x + dest.width, (float)(cx + 1) * grid::CELL);
    if (right <= left)
      continue;
    const bool sunk = laps(cx);
    const float sx = (left - dest.x) / grid::WORLD_SCALE;
    const int wetRow = f.wetRow + (stage == 2 ? 1 : 0);
    Rectangle piece = {src.x + sx,
                       sunk ? (float)(wetRow * grid::SOURCE_TILE)
                            : src.y + src.height - grid::SOURCE_TILE,
                       (right - left) / grid::WORLD_SCALE, (float)grid::SOURCE_TILE};
    DrawTexturePro(sunk ? m_propsWet : m_props, piece,
                   {left, bottomY, right - left, (float)grid::CELL}, {0, 0}, 0.0f, tint);
  }
}
