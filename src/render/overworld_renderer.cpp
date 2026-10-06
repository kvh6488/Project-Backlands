#include "render/overworld_renderer.hpp"
#include "core/asset_load.hpp"
#include "core/grid.hpp"
#include "render/theme.hpp"
#include "render/view_bounds.hpp"
#include "world/noise.hpp"
#include <algorithm>
#include <span>

namespace {

// ---- terrain ---------------------------------------------------------------

// Bottom to top. Each layer covers its own cells; the ones below show through
// its transparent edges, so a layer also covers the cells of any layer that
// should sit on IT - gravel runs under snow, or a snowfield on a mountain
// would show a grass rim.
//
// SWAMP_WATER sits on the open water. GROUND is all land: its edge is the
// sand-and-foam coast where the ocean is, a mud bank on lakes and rivers.
// MEADOW, FOREST and FOREST_DEEP are SHADE OVERLAYS on grass, DRIFT on snow:
// the ground fading by the tile's shade step (TileSample::shade).
enum Layer {
  SWAMP_WATER, GROUND, GRASS, MEADOW, FOREST, FOREST_DEEP,
  WETLAND, GRAVEL, SNOW, DRIFT, LAYER_COUNT
};

bool isOpenWater(Biome b) { return isWater(b) && b != Biome::SWAMP; }

bool inLayer(int layer, Biome b, uint8_t shade) {
  switch (layer) {
  case GROUND: return !isWater(b);
  case GRASS: return !isWater(b) && b != Biome::BEACH;
  case MEADOW: return (b == Biome::GRASSLAND && shade >= 1) || b == Biome::FOREST;
  case FOREST: return b == Biome::FOREST;
  case FOREST_DEEP: return b == Biome::FOREST && shade == 3;
  case WETLAND: return b == Biome::WETLAND;
  case GRAVEL: return b == Biome::MOUNTAIN || b == Biome::SNOW;
  case SNOW: return b == Biome::SNOW;
  case DRIFT: return b == Biome::SNOW && shade >= 1;
  default: return false;
  }
}

// A shade overlay's base layer: the overlay dithers toward it and cuts
// cleanly, on the base's outline, against everything else.
int overlayBase(int layer) {
  switch (layer) {
  case MEADOW: case FOREST: case FOREST_DEEP: return GRASS;
  case DRIFT: return SNOW;
  default: return -1;
  }
}

// assets/ow_terrain.png: one row per layer, the corner shape in column `mask`,
// full fills in columns kFillCol.. Sand has fills only (the coast is its
// edge); GROUND draws the bank row's edges and its fill is sand or mud.
// assets/ow_shades.png: one row per overlay, 81 three-state corner tiles
// (TL*27 + TR*9 + BL*3 + BR; 0 off the base, 1 base, 2 shaded), then fills.
constexpr int kTerrainRow[LAYER_COUNT] = {-1, 5, 0, -1, -1, -1, 1, 2, 3, -1};
constexpr int kSandRow = 4, kBankRow = 5;
constexpr int kShadeFillCol = 81;
constexpr int kFillCol = 16, kFillCount = 4;
// assets/ow_water.png rows.
constexpr int kOpenWaterRow = 0, kSwampFillRow = 1, kSwampEdgeRow = 2;

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

constexpr int kWaterVariants = 8;

// Per-purpose salts, so the water, fill and prop hashes are unrelated.
constexpr uint32_t kWaterSalt = 0x5bd1e995u, kFillSalt = 0x27d4eb2fu;

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
constexpr Pick kWetlandTrees[] = {{WILLOW_LIT, 1}, {WILLOW, 2},     {WILLOW_S_A, 2},
                                  {WILLOW_S_B, 2}, {WILLOW_S_C, 2}};
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
  for (Texture2D t : {m_water, m_coast, m_terrain, m_shades, m_props, m_propsWet})
    if (t.id != 0)
      UnloadTexture(t);
}

void OverworldRenderer::loadTextures() {
  m_water = assets::loadTexture("assets/ow_water.png", "OverworldRenderer");
  m_coast = assets::loadTexture("assets/ow_coast.png", "OverworldRenderer");
  m_terrain = assets::loadTexture("assets/ow_terrain.png", "OverworldRenderer");
  m_shades = assets::loadTexture("assets/ow_shades.png", "OverworldRenderer");
  m_props = assets::loadTexture("assets/ow_props.png", "OverworldRenderer");
  m_propsWet = assets::loadTexture("assets/ow_props_wet.png", "OverworldRenderer");
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
  if (pool.empty())
    return owsprite::COUNT;
  int total = 0;
  for (const Pick &p : pool)
    total += p.weight;
  int r = variant % total;
  for (const Pick &p : pool) {
    if (r < p.weight)
      return p.id;
    r -= p.weight;
  }
  return pool.back().id;
}

void OverworldRenderer::renderTerrain(const Overworld &world,
                                      const Camera2D &camera,
                                      const Viewport &canvas, float time) {
  const ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);
  const int x0 = view.startX, y0 = view.startY;
  const int w = view.endX - x0 + 1, h = view.endY - y0 + 1;
  const uint32_t seed = world.island().seed();

  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      int v = (int)(noise::hash(world.wrapX(x), world.wrapY(y),
                                seed ^ kWaterSalt) % kWaterVariants);
      Rectangle src = grid::srcTile(v, kOpenWaterRow);
      DrawTexturePro(m_water, src, grid::destFor(src, x * grid::CELL, y * grid::CELL),
                     {0, 0}, 0.0f, WHITE);
    }
  }

  // Dual-grid vertices x0..x0+w each read the cells on both sides, so the
  // cache starts one cell up-left of the view and is two cells wider.
  m_cellsW = w + 2;
  m_cells.resize((size_t)m_cellsW * (h + 2));
  for (int j = 0; j < h + 2; ++j)
    for (int i = 0; i < m_cellsW; ++i)
      m_cells[j * m_cellsW + i] = {world.biomeAt(x0 - 1 + i, y0 - 1 + j),
                                   world.shadeAt(x0 - 1 + i, y0 - 1 + j)};

  const int frame = (int)(time / kCoastFrameSeconds) % kCoastFrames;
  m_world = &world;
  for (int layer = 0; layer < LAYER_COUNT; ++layer)
    drawLayer(layer, x0, y0, w, h, frame);
}

void OverworldRenderer::drawLayer(int layer, int x0, int y0, int w, int h,
                                  int frame) const {
  const uint32_t seed = m_world->island().seed();
  auto cell = [&](int i, int j) { return m_cells[j * m_cellsW + i]; };
  auto draw = [](Texture2D tex, Rectangle src, int vx, int vy) {
    // A dual tile is centred on the corner shared by four cells.
    DrawTexturePro(tex, src,
                   grid::destFor(src, vx * grid::CELL - grid::CELL / 2.0f,
                                 vy * grid::CELL - grid::CELL / 2.0f),
                   {0, 0}, 0.0f, WHITE);
  };
  const int row = kTerrainRow[layer];
  const int coastRow0 = 1 + frame * kCoastFrameRows;

  for (int j = 0; j < h + 1; ++j) {
    for (int i = 0; i < w + 1; ++i) {
      // Vertex (vx, vy) is the top-left corner of cell (vx, vy); cell
      // (vx - 1, vy - 1) sits at cache index (i, j).
      const Cell q[4] = {cell(i, j), cell(i + 1, j), cell(i, j + 1), cell(i + 1, j + 1)};
      const Biome c[4] = {q[0].biome, q[1].biome, q[2].biome, q[3].biome};
      const int vx = x0 + i, vy = y0 + j;
      const uint32_t hash =
          noise::hash(m_world->wrapX(vx), m_world->wrapY(vy), seed ^ kFillSalt);
      const int v = (int)(hash % kFillCount);

      if (const int base = overlayBase(layer); base >= 0) {
        int idx = 0;
        bool any = false, all = true;
        for (const Cell &k : q) {
          int st = inLayer(layer, k.biome, k.shade) ? 2 : inLayer(base, k.biome, k.shade) ? 1 : 0;
          idx = idx * 3 + st;
          any = any || st == 2;
          all = all && st == 2;
        }
        const int orow = layer == DRIFT ? 3 : layer - MEADOW;
        if (any)
          draw(m_shades, grid::srcTile(all ? kShadeFillCol + v : idx, orow), vx, vy);
        continue;
      }
      bool in[4];
      if (layer == SWAMP_WATER) {
        // Swamp water fades into open water over a dithered band on its own
        // side. Where no open water meets this corner, land counts as swamp
        // too - it is drawn over anyway, and the band would otherwise show
        // blue specks along every swamp shore.
        bool open = false, swamp = false;
        for (Biome b : c) {
          open = open || isOpenWater(b);
          swamp = swamp || b == Biome::SWAMP;
        }
        if (!swamp)
          continue; // all-land corners would draw swamp nobody sees
        for (int k = 0; k < 4; ++k)
          in[k] = c[k] == Biome::SWAMP || (!open && !isWater(c[k]));
      } else {
        for (int k = 0; k < 4; ++k)
          in[k] = inLayer(layer, q[k].biome, q[k].shade);
      }
      int mask = cornerMask(in[0], in[1], in[2], in[3]);
      if (mask == 0)
        continue;
      if (layer == SWAMP_WATER) {
        draw(m_water,
             mask == 15 ? grid::srcTile((int)(hash % kWaterVariants), kSwampFillRow)
                        : grid::srcTile(mask, kSwampEdgeRow),
             vx, vy);
        continue;
      }
      if (mask == 15) {
        int fillRow = row;
        if (layer == GROUND) {
          bool beach = false;
          for (Biome b : c)
            beach = beach || b == Biome::BEACH;
          fillRow = beach ? kSandRow : kBankRow;
        }
        draw(m_terrain, grid::srcTile(kFillCol + v, fillRow), vx, vy);
        continue;
      }
      // Sand coast where the sea or a beach is involved (a river mouth on a
      // beach would otherwise show a mud bank's body on the sand).
      bool sandy = false;
      for (Biome b : c)
        sandy = sandy || b == Biome::OCEAN || b == Biome::BEACH;
      if (layer != GROUND || !sandy) {
        draw(m_terrain, grid::srcTile(mask, row), vx, vy);
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

  // Roots wider than the tile spill onto the cells beside it. Over water,
  // that slice of the bottom row draws from the submerged copy instead.
  // Frames are centred on the tile, so a slice boundary can fall mid-column;
  // slices are cut at world cell edges, in canvas px.
  const float bottomY = dest.y + dest.height - grid::CELL;
  const int cx0 = (int)std::floor(dest.x / grid::CELL);
  const int cx1 = (int)std::ceil((dest.x + dest.width) / grid::CELL) - 1;
  bool wet = false;
  for (int cx = cx0; cx <= cx1 && !wet; ++cx)
    wet = cx != x && isWater(m_world->biomeAt(cx, y));
  if (!wet) {
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
    const bool sunk = cx != x && isWater(m_world->biomeAt(cx, y));
    const float sx = (left - dest.x) / grid::WORLD_SCALE;
    Rectangle piece = {src.x + sx,
                       sunk ? (float)(f.wetRow * grid::SOURCE_TILE)
                            : src.y + src.height - grid::SOURCE_TILE,
                       (right - left) / grid::WORLD_SCALE, (float)grid::SOURCE_TILE};
    DrawTexturePro(sunk ? m_propsWet : m_props, piece,
                   {left, bottomY, right - left, (float)grid::CELL}, {0, 0}, 0.0f, tint);
  }
}
