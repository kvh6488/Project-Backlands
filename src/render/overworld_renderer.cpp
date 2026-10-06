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
  // Coastal scrub's floor is the meadow edge, so it blends into grassland
  // and forest through the same overlays.
  case MEADOW:
    return (b == Biome::GRASSLAND && shade >= 1) || b == Biome::FOREST || b == Biome::COASTAL;
  case FOREST: return b == Biome::FOREST;
  case FOREST_DEEP: return b == Biome::FOREST && shade == 3;
  default: return false;
  }
}

// assets/ow_terrain.png: grass (the corner shape in column `mask`, full fills
// in columns kFillCol..), sand fills, then the bank rows - mud, and one step
// paler per tile nearer a beach, kBankSteps of them, ending at the sand's tan. GROUND draws a bank row's
// edges and its fill is sand or mud.
// assets/ow_shades.png: one row per overlay, 81 three-state corner tiles
// (TL*27 + TR*9 + BL*3 + BR; 0 off the grass, 1 grass, 2 shaded), then fills.
constexpr int kGrassRow = 0, kSandRow = 1, kBankRow = 2; // + 0..kBankSteps toward sand
constexpr int kBankSteps = 4;
constexpr int kShadeFillCol = 81;
constexpr int kFillCol = 16, kFillCount = 4;

// The fade materials: rows of assets/ow_fades.png, and the `layer` the fade
// shader is told. Land fades draw over the grass, clipped to its shape; swamp
// water draws over the water through corner masks, like a shade overlay.
// Dune sand is the beach running into coastal scrub, and its sand patches.
// Ice, like swamp water, draws over the water from a texture of its own, so
// it has no row.
enum FadeMaterial { FADE_SWAMP, FADE_WETLAND, FADE_GRAVEL, FADE_SNOW, FADE_DRIFT, FADE_DUNE, FADE_ICE };

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
constexpr float kCoastFrameSeconds = 0.4f / 0.675f; // the foam washes at 0.675 of the pack's pace

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

using namespace owsprite;
using Look = OverworldRenderer::Look;
constexpr int kLooks = (int)Look::COUNT;

// A SPECIES is what grows on the tile all year; its LOOK is what the date
// does to it. The prop's hash byte picks the species from a per-biome pool
// once, so a tree never changes kind with the seasons - only its frame.
// Looks a pack never drew fall back to the nearest one it did.
struct Species {
  Id look[kLooks]; // IN_LEAF, TURNING, AUTUMN, BARE, SNOWED
  bool deciduous;
};
constexpr Species leafy(Id green, Id turning, Id autumn, Id bare, Id snow) {
  return {{green, turning, autumn, bare, snow}, true};
}
// Evergreens keep their needles; snow is their only change.
constexpr Species evergreen(Id green, Id snow) { return {{green, green, green, green, snow}, false}; }
// One frame all year: palms, willows, dead snags, bushes, rocks, reeds.
constexpr Species fixed(Id id) { return {{id, id, id, id, id}, false}; }

// LightBorne oak and birch: no turning frame (they hold green, then go
// orange); "winter" is a frosted canopy, so it is their snow look.
constexpr Species kOakA = leafy(OAK_SUMMER_A, OAK_SUMMER_A, OAK_AUTUMN_A, OAK_BARE_A, OAK_WINTER_A);
constexpr Species kOakB = leafy(OAK_SUMMER_B, OAK_SUMMER_B, OAK_AUTUMN_B, OAK_BARE_B, OAK_WINTER_B);
constexpr Species kBirchA = leafy(BIRCH_SUMMER_A, BIRCH_SUMMER_A, BIRCH_AUTUMN_A, BIRCH_BARE_A, BIRCH_WINTER_A);
constexpr Species kBirchB = leafy(BIRCH_SUMMER_B, BIRCH_SUMMER_B, BIRCH_AUTUMN_B, BIRCH_BARE_B, BIRCH_WINTER_B);
// Pixel Crawler broadleaf: tan while turning, then orange or amber; frozen
// (icicles) under snow.
constexpr Species kPc1S2 = leafy(PC1_S2_GREEN, PC1_S2_TAN, PC1_S2_ORANGE, PC1_S2_BARE, PC1_S2_FROZEN);
constexpr Species kPc1S2Amber = leafy(PC1_S2_GREEN, PC1_S2_TAN, PC1_S2_AMBER, PC1_S2_BARE, PC1_S2_FROZEN);
constexpr Species kPc1S3 = leafy(PC1_S3_GREEN, PC1_S3_TAN, PC1_S3_ORANGE, PC1_S3_BARE, PC1_S3_FROZEN);
constexpr Species kPc1S3Amber = leafy(PC1_S3_GREEN, PC1_S3_TAN, PC1_S3_AMBER, PC1_S3_BARE, PC1_S3_FROZEN);
constexpr Species kPc1S4 = leafy(PC1_S4_GREEN, PC1_S4_TAN, PC1_S4_ORANGE, PC1_S4_BARE, PC1_S4_FROZEN);
constexpr Species kPc1S5 = leafy(PC1_S5_GREEN, PC1_S5_TAN, PC1_S5_AMBER, PC1_S5_BARE, PC1_S5_FROZEN);
// Pixel Crawler larch: a deciduous conifer - tan, then rust, then bare. No
// frozen frame, so bare under snow; the smallest has no bare frame either.
constexpr Species kLarchS2 = leafy(PC3_S2_GREEN, PC3_S2_TAN, PC3_S2_RUST, PC1_S2_BARE, PC1_S2_BARE);
constexpr Species kLarchS2Olive = leafy(PC3_S2_OLIVE, PC3_S2_TAN, PC3_S2_RUST, PC1_S2_BARE, PC1_S2_BARE);
constexpr Species kLarchS3 = leafy(PC3_S3_GREEN, PC3_S3_TAN, PC3_S3_RUST, PC3_S3_BARE, PC3_S3_BARE);
constexpr Species kLarchS3Olive = leafy(PC3_S3_OLIVE, PC3_S3_TAN, PC3_S3_RUST, PC3_S3_BARE, PC3_S3_BARE);
constexpr Species kLarchS4 = leafy(PC3_S4_GREEN, PC3_S4_TAN, PC3_S4_RUST, PC3_S4_BARE, PC3_S4_BARE);
constexpr Species kLarchS4Olive = leafy(PC3_S4_OLIVE, PC3_S4_TAN, PC3_S4_RUST, PC3_S4_BARE, PC3_S4_BARE);
constexpr Species kLarchS5 = leafy(PC3_S5_GREEN, PC3_S5_TAN, PC3_S5_RUST, PC3_S5_BARE, PC3_S5_BARE);
constexpr Species kLarchS5Olive = leafy(PC3_S5_OLIVE, PC3_S5_TAN, PC3_S5_RUST, PC3_S5_BARE, PC3_S5_BARE);

struct Pick {
  Species species;
  int weight;
};

constexpr Pick kGrasslandTrees[] = {
    {kOakA, 3},       {kOakB, 3},   {kBirchA, 3},      {kBirchB, 3},   {kPc1S2, 3},
    {kPc1S2Amber, 1}, {kPc1S3, 2},  {kPc1S3Amber, 1},  {kLarchS2, 3},  {kLarchS3, 1}};
constexpr Pick kForestTrees[] = {
    {kOakA, 3},         {kOakB, 3},        {kBirchA, 2},       {kBirchB, 2},
    {kPc1S3, 3},        {kPc1S3Amber, 2},  {kPc1S4, 2},        {kPc1S5, 1},
    {kLarchS2Olive, 1}, {kLarchS3, 4},     {kLarchS3Olive, 1}, {kLarchS4, 1},
    {kLarchS4Olive, 1}, {kLarchS5, 1},     {kLarchS5Olive, 1}, {evergreen(FIR_0, FIR_0_SNOW), 2},
    {evergreen(FIR_1, FIR_1_SNOW), 2}, {evergreen(FIR_2, FIR_2_SNOW), 2}};
constexpr Pick kWetlandTrees[] = {{fixed(WILLOW), 2}, {fixed(WILLOW_S_A), 2},
                                  {fixed(WILLOW_S_B), 2}, {fixed(WILLOW_S_C), 2}};
constexpr Pick kBeachTrees[] = {{fixed(PALM_TALL), 1}, {fixed(PALM_SHORT), 1}};
// Coastal scrub: no tree over 7 tiles, and palms the most common. The sea
// keeps the coast mild, so its trees stay in leaf all year.
constexpr Pick kCoastalTrees[] = {
    {fixed(PALM_TALL), 4},    {fixed(PALM_SHORT), 4},   {fixed(PC1_S2_GREEN), 4},
    {fixed(PC3_S2_GREEN), 3}, {fixed(PC1_S3_GREEN), 2}, {fixed(OAK_SUMMER_A), 1},
    {fixed(OAK_SUMMER_B), 1}};
constexpr Pick kMountainPines[] = {
    {evergreen(PC2_S2_TEAL, PC2_S2_TEAL_SNOW), 2},   {evergreen(PC2_S2_TEAL_B, PC2_S2_TEAL_B_SNOW), 2},
    {evergreen(PC2_S2_GREEN, PC2_S2_GREEN_SNOW), 2}, {evergreen(PC2_S2_GREEN_B, PC2_S2_GREEN_B_SNOW), 2},
    {evergreen(PC2_S3_TEAL, PC2_S3_TEAL_SNOW), 2},   {evergreen(PC2_S3_TEAL_B, PC2_S3_TEAL_B_SNOW), 2},
    {evergreen(PC2_S3_GREEN, PC2_S3_GREEN_SNOW), 2}, {evergreen(PC2_S3_GREEN_B, PC2_S3_GREEN_B_SNOW), 2},
    {evergreen(PC2_S4_TEAL, PC2_S4_TEAL_SNOW), 1},   {evergreen(PC2_S4_TEAL_B, PC2_S4_TEAL_B_SNOW), 1},
    {evergreen(PC2_S4_GREEN, PC2_S4_GREEN_SNOW), 1}, {evergreen(PC2_S4_GREEN_B, PC2_S4_GREEN_B_SNOW), 1},
    {evergreen(PC2_S5_TEAL, PC2_S5_TEAL_SNOW), 1},   {evergreen(PC2_S5_TEAL_B, PC2_S5_TEAL_B_SNOW), 1},
    {evergreen(PC2_S5_GREEN, PC2_S5_GREEN_SNOW), 1}, {evergreen(PC2_S5_GREEN_B, PC2_S5_GREEN_B_SNOW), 1},
    {evergreen(FIR_0, FIR_0_SNOW), 1}, {evergreen(FIR_1, FIR_1_SNOW), 1}, {evergreen(FIR_2, FIR_2_SNOW), 1},
    {evergreen(FIR_3, FIR_3_SNOW), 1}, {evergreen(FIR_4, FIR_4_SNOW), 1},
    {fixed(PC2_S3_BARE), 1}, {fixed(PC2_S4_BARE), 1}, {fixed(PC2_S5_BARE), 1},
    {fixed(PC3_S4_BARE), 1}, {fixed(PC3_S5_BARE), 1}};
// Alpine peaks: hardy broadleaf that leafs out in summer, dark firs, and
// dead snags that stand bare all year.
constexpr Pick kAlpinePines[] = {
    {kPc1S2, 2}, {kPc1S3, 2}, {kPc1S4, 1}, {kPc1S5, 1},
    {evergreen(FIR_DARK_0, FIR_DARK_0_SNOW), 1}, {evergreen(FIR_DARK_1, FIR_DARK_1_SNOW), 1},
    {evergreen(FIR_DARK_2, FIR_DARK_2_SNOW), 1}, {evergreen(FIR_DARK_3, FIR_DARK_3_SNOW), 1},
    {kOakA, 1}, {kOakB, 1}, {kBirchA, 1}, {kBirchB, 1},
    {fixed(BIRCH_BARE_A), 1}, {fixed(BIRCH_BARE_B), 1}, {fixed(OAK_BARE_A), 1},
    {fixed(OAK_BARE_B), 1},   {fixed(PC1_S2_BARE), 1},  {fixed(PC1_S3_BARE), 1},
    {fixed(PC1_S4_BARE), 1},  {fixed(PC1_S5_BARE), 1},  {fixed(PC3_S3_BARE), 1}};
// Bushes drop their leaves too and stand as bare shrubs all winter, snowed
// on or not. LightBorne's hold green through autumn; Pixel Crawler's go olive,
// then tan or rust.
constexpr Species shrub(Id green, Id bare) { return leafy(green, green, green, bare, bare); }
constexpr Species kLbBushA = shrub(BUSH_A, SHRUB_BARE_A);
constexpr Species kLbBushC = shrub(BUSH_C, SHRUB_BARE_A);
constexpr Pick kBushes[] = {
    {kLbBushA, 2},
    {shrub(BUSH_B, SHRUB_BARE_A), 2},
    {kLbBushC, 2},
    {shrub(BUSH_D, SHRUB_BARE_A), 2},
    {shrub(BUSH_LOW, SHRUB_BARE_A), 1},
    {shrub(BUSH_SMALL, SHRUB_TWIG), 1},
    {leafy(PCB_S1_GREEN, PCB_S1_OLIVE, PCB_S1_TAN, SHRUB_BARE_A, SHRUB_BARE_A), 1},
    {leafy(PCB_S1_GREEN, PCB_S1_OLIVE, PCB_S1_RUST, SHRUB_BARE_A, SHRUB_BARE_A), 1},
    {leafy(PCB_S2_GREEN, PCB_S2_OLIVE, PCB_S2_TAN, SHRUB_BARE_A, SHRUB_BARE_A), 1},
    {leafy(PCB_S2_GREEN, PCB_S2_OLIVE, PCB_S2_RUST, SHRUB_BARE_A, SHRUB_BARE_A), 1},
    {leafy(PCB_S3_GREEN, PCB_S3_OLIVE, PCB_S3_TAN, SHRUB_BARE_A, SHRUB_BARE_A), 1},
    {leafy(PCB_S4_GREEN, PCB_S4_OLIVE, PCB_S4_RUST, SHRUB_BARE_B, SHRUB_BARE_B), 1}};
// Coastal scrub's bushes are the same plants, kept in leaf like its trees.
constexpr Pick kCoastalBushes[] = {
    {fixed(BUSH_A), 2},       {fixed(BUSH_B), 2},       {fixed(BUSH_C), 2},
    {fixed(BUSH_D), 2},       {fixed(BUSH_LOW), 1},     {fixed(BUSH_SMALL), 1},
    {fixed(PCB_S1_GREEN), 2}, {fixed(PCB_S2_GREEN), 2}, {fixed(PCB_S3_GREEN), 1},
    {fixed(PCB_S4_GREEN), 1}};
constexpr Pick kWetlandBushes[] = {{fixed(SWAMP_PLANT), 2}, {kLbBushA, 1}, {kLbBushC, 1}};
constexpr Pick kRocks[] = {{fixed(ROCK_GREY_0), 1}, {fixed(ROCK_GREY_1), 1}, {fixed(ROCK_GREY_2), 1},
                           {fixed(ROCK_GREY_3), 1}, {fixed(ROCK_GREY_4), 1}, {fixed(ROCK_GREY_BIG), 1},
                           {fixed(ROCK_MOSS_1), 1}, {fixed(ROCK_MOSS_2), 1}, {fixed(ROCK_MOSS_3), 1}};
constexpr Pick kBeachRocks[] = {{fixed(ROCK_GREY_0), 1}, {fixed(ROCK_GREY_1), 1}, {fixed(ROCK_GREY_2), 1},
                                {fixed(ROCK_GREY_3), 1}, {fixed(ROCK_GREY_4), 1}};
constexpr Pick kMountainRocks[] = {{fixed(BOULDER_BROWN), 2}, {fixed(BOULDER_BROWN_LOW), 2},
                                   {fixed(BOULDER_GREY), 2},  {fixed(BOULDER_GREY_LOW), 2},
                                   {fixed(ROCK_GREY_BIG), 1}, {fixed(ROCK_GREY_2), 1}};
constexpr Pick kAlpineRocks[] = {{fixed(BOULDER_GREY), 2}, {fixed(BOULDER_GREY_LOW), 2},
                                 {fixed(ROCK_GREY_BIG), 1}, {fixed(ROCK_GREY_2), 1}};
constexpr Pick kReeds[] = {{fixed(CATTAIL_TALL), 2}, {fixed(CATTAIL_SHORT), 2}, {fixed(SWAMP_PLANT), 1}};

std::span<const Pick> poolFor(PropType type, Biome b, uint8_t shade) {
  const bool alpine = b == Biome::MOUNTAIN && shade == 1;
  switch (type) {
  case PropType::TREE:
    if (b == Biome::WETLAND) return kWetlandTrees;
    if (b == Biome::BEACH) return kBeachTrees;
    if (b == Biome::COASTAL) return kCoastalTrees;
    if (b == Biome::GRASSLAND) return kGrasslandTrees;
    return kForestTrees;
  case PropType::PINE:
    return alpine ? std::span<const Pick>(kAlpinePines) : kMountainPines;
  case PropType::BUSH:
    if (b == Biome::WETLAND) return kWetlandBushes;
    if (b == Biome::COASTAL) return kCoastalBushes;
    return kBushes;
  case PropType::ROCK:
    if (alpine) return kAlpineRocks;
    if (b == Biome::MOUNTAIN) return kMountainRocks;
    if (b == Biome::BEACH) return kBeachRocks;
    return kRocks;
  case PropType::REEDS: return kReeds;
  default: return {};
  }
}

const Species *speciesFor(PropType type, Biome biome, uint8_t shade, uint8_t variant) {
  std::span<const Pick> pool = poolFor(type, biome, shade);
  if (pool.empty())
    return nullptr;
  int total = 0;
  for (const Pick &p : pool)
    total += p.weight;
  int k = (int)(variant % total);
  for (const Pick &p : pool) {
    if (k < p.weight)
      return &p.species;
    k -= p.weight;
  }
  return &pool.back().species;
}

// ---- the deciduous year -----------------------------------------------------
// Each tree keeps a calendar of its own, from a per-tile hash, so a wood
// turns over a week or two rather than all on one day. In year days
// (Calendar::yearDay; autumn is 40-60):
//
//   leaf-out   1-6     bare -> green (early spring)
//   turn       41-50   green -> turning
//   colour     turn+4  turning -> full autumn colour
//   leaf-fall  56-62   -> bare, until next spring
//
// kStayGreen of trees skip the colour and simply drop their leaves.
constexpr float kStayGreen = 0.2f;
constexpr uint32_t kLeafSalt = 0x5bd1e995u;

// ---- ground details ---------------------------------------------------------

struct DecalPick {
  Id id;
  int weight;
};
constexpr DecalPick kGrassDecals[] = {{DECAL_FLOWERS_A, 2}, {DECAL_FLOWERS_B, 2}, {DECAL_FLOWER, 3},
                                      {DECAL_STARS_A, 2},   {DECAL_STARS_B, 2},   {DECAL_TUFT_A, 3},
                                      {DECAL_TUFT_B, 3},    {DECAL_PEBBLE_GREY, 1}};
constexpr DecalPick kMeadowDecals[] = {{DECAL_TUFT_A, 3},  {DECAL_TUFT_B, 3},  {DECAL_FLOWER, 2},
                                       {DECAL_PATCH_A, 2}, {DECAL_PATCH_B, 2}, {DECAL_STARS_A, 1}};
constexpr DecalPick kForestDecals[] = {{DECAL_PATCH_A, 3},        {DECAL_PATCH_B, 3},
                                       {DECAL_PATCH_C, 2},        {DECAL_TWIGS, 3},
                                       {DECAL_FERN, 3},           {DECAL_MUSHROOM_RED, 1},
                                       {DECAL_MUSHROOM_BROWN, 2}, {DECAL_MUSHROOMS, 1},
                                       {DECAL_PEBBLE_MOSS, 1}};
constexpr DecalPick kWetlandDecals[] = {{DECAL_PATCH_A, 3}, {DECAL_PATCH_C, 3}, {DECAL_FERN, 3},
                                        {DECAL_MUSHROOMS, 1}, {DECAL_PEBBLE_MOSS, 1}};
constexpr DecalPick kMountainDecals[] = {{DECAL_PEBBLE_GREY, 3}, {DECAL_PEBBLES_GREY, 3},
                                         {DECAL_PEBBLE_BROWN, 2}, {DECAL_DEAD_TUFT_B, 1}};
constexpr DecalPick kSnowDecals[] = {{DECAL_PEBBLE_SNOW, 3}, {DECAL_PEBBLES_SNOW, 2},
                                     {DECAL_STICK, 2},       {DECAL_MOUND_A, 3},
                                     {DECAL_MOUND_B, 3},     {DECAL_ICE_A, 1},
                                     {DECAL_ICE_B, 1}};
constexpr DecalPick kBeachDecals[] = {{DECAL_SHELL, 3}, {DECAL_SHELL_PINK, 2}, {DECAL_PEBBLE_GREY, 1}};
constexpr DecalPick kFreshWaterDecals[] = {{DECAL_STONE_WATER_A, 2}, {DECAL_STONE_WATER_B, 2},
                                           {DECAL_STONE_WATER_C, 2}, {DECAL_STONE_WATER_MOSS, 1}};
// Spring's extra bloom.
constexpr DecalPick kSpringFlowers[] = {{DECAL_FLOWERS_A, 2}, {DECAL_FLOWERS_B, 2}, {DECAL_FLOWER, 3},
                                        {DECAL_STARS_A, 1},   {DECAL_STARS_B, 1}};

struct DecalOdds {
  float chance;
  std::span<const DecalPick> pool;
};

DecalOdds decalOdds(Biome b, uint8_t shade, bool snow) {
  if (snow)
    return {0.08f, kSnowDecals};
  switch (b) {
  case Biome::GRASSLAND: return shade ? DecalOdds{0.10f, kMeadowDecals} : DecalOdds{0.10f, kGrassDecals};
  case Biome::FOREST: return {0.14f, kForestDecals};
  case Biome::WETLAND: return {0.07f, kWetlandDecals};
  case Biome::MOUNTAIN: return {0.10f, kMountainDecals};
  case Biome::BEACH: return {0.04f, kBeachDecals};
  case Biome::COASTAL: return shade ? DecalOdds{0.04f, kBeachDecals} : DecalOdds{0.10f, kMeadowDecals};
  // Of the water tiles near a shore only (drawDecals): stones in mid-lake
  // read as litter.
  case Biome::LAKE: case Biome::RIVER: return {0.01f, kFreshWaterDecals};
  default: return {0.0f, {}};
  }
}

// How many more tiles flower at the height of spring, on top of the usual.
float springFlowerChance(Biome b, uint8_t shade) {
  switch (b) {
  case Biome::GRASSLAND: return 0.14f;
  case Biome::COASTAL: return shade ? 0.0f : 0.12f; // not on the sand patches
  case Biome::WETLAND: return 0.08f;
  default: return 0.0f;
  }
}

// Flowers wither from mid-autumn until spring, leaving brown tufts.
bool wiltingAt(double yearDay) { return yearDay >= 48.0 || yearDay < 2.0; }
bool isFlower(Id id) {
  return id == DECAL_FLOWERS_A || id == DECAL_FLOWERS_B || id == DECAL_FLOWER ||
         id == DECAL_STARS_A || id == DECAL_STARS_B;
}

Id pickDecal(std::span<const DecalPick> pool, uint32_t r) {
  int total = 0;
  for (const DecalPick &p : pool)
    total += p.weight;
  int k = (int)(r % (uint32_t)total);
  for (const DecalPick &p : pool) {
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
  for (Texture2D t : {m_water, m_river, m_swampWater, m_ice, m_glints, m_coast, m_terrain, m_shades,
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
  m_ice = assets::loadTexture("assets/ow_ice.png", "OverworldRenderer");
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
  m_locIce = GetShaderLocation(m_fade, "ice");
  m_locSway = GetShaderLocation(m_fade, "sway");
  m_locLevel = GetShaderLocation(m_fade, "level");
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
  case Biome::COASTAL: return theme::coastal;
  default: return theme::ground;
  }
}

owsprite::Id OverworldRenderer::spriteFor(PropType type, Biome biome, uint8_t shade,
                                          uint8_t variant, Look look) {
  const Species *sp = speciesFor(type, biome, shade, variant);
  return sp ? sp->look[(int)look] : owsprite::COUNT;
}

bool OverworldRenderer::isDeciduous(PropType type, Biome biome, uint8_t shade,
                                    uint8_t variant) {
  const Species *sp = speciesFor(type, biome, shade, variant);
  return sp && sp->deciduous;
}

OverworldRenderer::Look OverworldRenderer::leafLook(bool deciduous, double yearDay,
                                                    bool snow, uint32_t hash) {
  if (snow)
    return Look::SNOWED;
  if (!deciduous)
    return Look::IN_LEAF;
  auto unit = [&](int shift) { return (float)((hash >> shift) & 0xffu) / 256.0f; };
  const float d = (float)yearDay;
  const float leafOut = 1.0f + 5.0f * unit(0);
  const float leafFall = 56.0f + 6.0f * unit(8);
  if (d < leafOut || d >= leafFall)
    return Look::BARE;
  if (unit(16) < kStayGreen)
    return Look::IN_LEAF;
  const float turn = 41.0f + 9.0f * unit(24);
  if (d >= turn + 4.0f)
    return Look::AUTUMN;
  return d >= turn ? Look::TURNING : Look::IN_LEAF;
}

float OverworldRenderer::bloomAt(double yearDay) {
  constexpr float kStart = 1.0f, kEnd = 21.0f; // peaks mid-spring, day 11
  const float d = (float)yearDay;
  if (d <= kStart || d >= kEnd)
    return 0.0f;
  return 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * (d - kStart) / (kEnd - kStart));
}

// One roll (the low 16 bits) against the usual chance, then against the
// spring bloom stacked above it: a tile that flowers at half bloom still
// flowers at full, so spring fills in rather than flickering.
owsprite::Id OverworldRenderer::decalFor(Biome biome, uint8_t shade, bool snow,
                                         uint32_t hash, double yearDay) {
  const DecalOdds odds = decalOdds(biome, shade, snow);
  const uint32_t roll = hash & 0xffffu;
  const uint32_t base = (uint32_t)(odds.chance * 65536.0f);
  if (!odds.pool.empty() && roll < base) {
    const Id id = pickDecal(odds.pool, hash >> 16);
    if (!snow && isFlower(id) && wiltingAt(yearDay))
      return (hash >> 16) & 1 ? DECAL_DEAD_TUFT_A : DECAL_DEAD_TUFT_B;
    return id;
  }
  const float extra = snow ? 0.0f : springFlowerChance(biome, shade) * bloomAt(yearDay);
  if (roll < base + (uint32_t)(extra * 65536.0f))
    return pickDecal(kSpringFlowers, hash >> 16);
  return owsprite::COUNT;
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

void OverworldRenderer::swampDepth(const std::vector<uint8_t> &water,
                                   const std::vector<uint8_t> &swamp, int w, int h,
                                   std::vector<float> &out) {
  const int n = w * h;
  std::vector<int> d(n, kSwampReach), queue;
  for (int k = 0; k < n; ++k)
    if (water[k] && !swamp[k]) {
      d[k] = 0;
      queue.push_back(k);
    }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int k = queue[head], x = k % w, y = k / w;
    if (d[k] + 1 >= kSwampReach)
      continue;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx, ny = y + dy, nk = ny * w + nx;
        if (nx < 0 || ny < 0 || nx >= w || ny >= h || !swamp[nk] || d[nk] <= d[k] + 1)
          continue;
        d[nk] = d[k] + 1;
        queue.push_back(nk);
      }
  }
  out.assign(n, 0.0f);
  for (int k = 0; k < n; ++k)
    if (water[k])
      out[k] = (float)d[k];
  for (int k = 0; k < n; ++k) {
    if (water[k])
      continue;
    float sum = 0.0f;
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = k % w + dx, ny = k / w + dy;
        if (nx >= 0 && ny >= 0 && nx < w && ny < h && water[ny * w + nx]) {
          sum += out[ny * w + nx];
          ++count;
        }
      }
    out[k] = count > 0 ? sum / (float)count : 0.0f;
  }
}

void OverworldRenderer::renderTerrain(const Overworld &world,
                                      const Camera2D &camera,
                                      const Viewport &canvas, float time,
                                      const Calendar &cal) {
  const ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);
  const int x0 = view.startX, y0 = view.startY;
  const int w = view.endX - x0 + 1, h = view.endY - y0 + 1;
  const uint32_t seed = world.island().seed();
  m_time = time;
  m_world = &world;
  m_yearDay = cal.yearDay();
  m_snowLine = world.climate().snowLine(m_yearDay);

  constexpr int kMargin = kCacheMargin;
  m_cellsX0 = x0 - kMargin;
  m_cellsY0 = y0 - kMargin;
  m_cellsW = w + 2 * kMargin;
  m_cellsH = h + 2 * kMargin;
  m_cells.resize((size_t)m_cellsW * m_cellsH);
  for (int j = 0; j < m_cellsH; ++j) {
    for (int i = 0; i < m_cellsW; ++i) {
      const int x = m_cellsX0 + i, y = m_cellsY0 + j;
      const Biome b = world.biomeAt(x, y);
      const float t0 = world.temperatureAt(x, y);
      const bool cold = t0 < m_snowLine;
      const bool snow = cold && Climate::takesSnow(b);
      const bool ice = cold && Climate::canFreeze(b, world.heightAt(x, y), t0);
      m_cells[j * m_cellsW + i] = {b, world.shadeAt(x, y), world.flowAt(x, y), 0, snow,
                                   world.driftAt(x, y), ice};
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
      // Still water, by world position, swaying.
      Rectangle src = {(float)(x * grid::SOURCE_TILE + sway),
                       (float)(y * grid::SOURCE_TILE + sway),
                       (float)grid::SOURCE_TILE, (float)grid::SOURCE_TILE};
      DrawTexturePro(m_water, src, grid::destFor(src, x * grid::CELL, y * grid::CELL),
                     {0, 0}, 0.0f, WHITE);
    }
  }
  drawGlints(x0, y0, w, h);

  const int frame = (int)(time / kCoastFrameSeconds) % kCoastFrames;
  drawFade(FADE_SWAMP, x0, y0, w, h);
  drawFade(FADE_ICE, x0, y0, w, h);
  for (int layer = 0; layer < LAYER_COUNT; ++layer)
    drawLayer(layer, x0, y0, w, h, frame);
  for (int fade : {FADE_DUNE, FADE_WETLAND, FADE_GRAVEL, FADE_SNOW, FADE_DRIFT})
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
      const Cell &c = cellAt(x, y);
      if ((c.biome != Biome::LAKE && c.biome != Biome::RIVER && c.biome != Biome::OCEAN) || c.ice)
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
// within kFadeRadius, so a shore never thins a fade - which cells are land,
// and each water cell's swamp step. Uploaded as two small textures; the
// shader reads the weights bilinear and the rest per cell. Also marks how near a
// beach each cache cell is, for the bank rows.
void OverworldRenderer::buildFades(int x0, int y0, int w, int h) {
  const int n = m_cellsW * m_cellsH;
  std::vector<uint8_t> land(n), water(n), ground(n), all(n, 1);
  std::vector<float> depth;
  std::vector<uint8_t> in[8]; // wetland, gravel, snow, drift, swamp, sandy bank, dune, ice
  for (auto &v : in)
    v.resize(n);
  for (int k = 0; k < n; ++k) {
    const Cell &c = m_cells[k];
    land[k] = inLayer(GRASS, c.biome, c.shade);
    water[k] = isWater(c.biome);
    ground[k] = !water[k];
    in[0][k] = c.biome == Biome::WETLAND;
    // Snow lies over whatever ground is there, so its edge fades to gravel
    // on a mountain and to grass below the tree line.
    in[1][k] = c.biome == Biome::MOUNTAIN;
    in[2][k] = c.snow;
    in[3][k] = c.snow && c.drift;
    in[4][k] = c.biome == Biome::SWAMP;
    // Coastal scrub's banks are sand too, like a beach's: the bank rows pale
    // to sand within it and fade back to mud over kBankSteps tiles outside.
    in[5][k] = c.biome == Biome::BEACH || c.biome == Biome::COASTAL;
    in[6][k] = (c.biome == Biome::BEACH || c.biome == Biome::COASTAL) && c.shade == 1;
    in[7][k] = c.ice;
  }
  std::vector<float> share[4], beach, dune, ice;
  for (int f = 0; f < 4; ++f)
    shareWithin(in[f], land, m_cellsW, m_cellsH, kFadeRadius, share[f]);
  // Out of all dry cells, not just the grass, and doubled: a share is only
  // ~0.5 at the beach's edge, and the grass's edge must vanish under solid
  // sand there, or the dither stops along a visible line.
  shareWithin(in[6], ground, m_cellsW, m_cellsH, kDuneRadius, dune);
  for (float &d : dune)
    d = std::min(1.0f, 2.0f * d);
  swampDepth(water, in[4], m_cellsW, m_cellsH, depth);
  // Ice's share of the water one cell around, so it ends between a frozen
  // cell and an open one, dithered over about a tile.
  shareWithin(in[7], water, m_cellsW, m_cellsH, 1, ice);
  // Bank step: kBankSteps with a beach in the 3x3 block, one fewer per ring out.
  for (int k = 0; k < n; ++k)
    m_cells[k].bank = 0;
  for (int r = kBankSteps; r >= 1; --r) {
    shareWithin(in[5], all, m_cellsW, m_cellsH, r, beach);
    for (int k = 0; k < n; ++k)
      if (beach[k] > 0.0f)
        m_cells[k].bank = (uint8_t)(kBankSteps + 1 - r);
  }

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
      const int k = (j + kCacheMargin - 1) * m_cellsW + (i + kCacheMargin - 1);
      Color &wpx = m_weightPx[j * tw + i];
      wpx = {byte(share[0][k]), byte(share[1][k]), byte(share[2][k]), byte(share[3][k])};
      // g: swamp depth, 0..1 over kSwampReach tiles; b: dune sand; a: ice.
      // All but r read bilinear.
      m_infoPx[j * tw + i] = {(unsigned char)(land[k] ? 255 : 0),
                              byte(depth[k] / (float)kSwampReach), byte(dune[k]), byte(ice[k])};
      m_fadeUsed[FADE_SWAMP] |= in[4][k] != 0;
      m_fadeUsed[FADE_WETLAND] |= wpx.r > 0;
      m_fadeUsed[FADE_GRAVEL] |= wpx.g > 0;
      m_fadeUsed[FADE_SNOW] |= wpx.b > 0;
      m_fadeUsed[FADE_DRIFT] |= wpx.a > 0;
      m_fadeUsed[FADE_DUNE] |= m_infoPx[j * tw + i].b > 0;
      m_fadeUsed[FADE_ICE] |= m_infoPx[j * tw + i].a > 0;
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
  // Swamp water is one pass per tint step, each over the one before.
  const int passes = fade == FADE_SWAMP ? kSwampSteps : 1;
  for (int level = 1; level <= passes; ++level)
    drawFadePass(fade, level, x0, y0, w, h);
}

void OverworldRenderer::drawFadePass(int fade, int level, int x0, int y0, int w,
                                     int h) const {
  BeginShaderMode(m_fade);
  SetShaderValueTexture(m_fade, m_locWeights, m_weights);
  SetShaderValueTexture(m_fade, m_locInfo, m_info);
  const int origin[2] = {m_fadeX0, m_fadeY0};
  SetShaderValue(m_fade, m_locOrigin, origin, SHADER_UNIFORM_IVEC2);
  SetShaderValue(m_fade, m_locLayer, &fade, SHADER_UNIFORM_INT);
  SetShaderValueTexture(m_fade, m_locSwampWater, m_swampWater);
  SetShaderValueTexture(m_fade, m_locIce, m_ice);
  const int sway[2] = {swayAt(m_time), swayAt(m_time)};
  SetShaderValue(m_fade, m_locSway, sway, SHADER_UNIFORM_IVEC2);
  SetShaderValue(m_fade, m_locLevel, &level, SHADER_UNIFORM_INT);
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
      owsprite::Id id = decalFor(c.biome, c.shade, c.snow,
                                 noise::hash(m_world->wrapX(x), m_world->wrapY(y), seed ^ kDecalSalt),
                                 m_yearDay);
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
                                const Viewport &canvas, const Calendar &cal,
                                DrawQueue &queue) {
  m_world = &world;
  m_yearDay = cal.yearDay();
  m_snowLine = world.climate().snowLine(m_yearDay);
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
  const Biome biome = m_world->biomeAt(x, y);
  const uint8_t shade = m_world->shadeAt(x, y);
  const bool snow = Climate::takesSnow(biome) && m_world->temperatureAt(x, y) < m_snowLine;
  const uint32_t leaf =
      noise::hash(m_world->wrapX(x), m_world->wrapY(y), m_world->island().seed() ^ kLeafSalt);
  const Look look = leafLook(isDeciduous(prop->type, biome, shade, prop->variant), m_yearDay,
                             snow, leaf);
  owsprite::Id id = spriteFor(prop->type, biome, shade, prop->variant, look);
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
