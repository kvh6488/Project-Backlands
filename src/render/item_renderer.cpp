#include "render/item_renderer.hpp"
#include "render/theme.hpp"
#include "items/item_database.hpp"
#include "core/asset_load.hpp"
#include "core/grid.hpp"
#include "render/view_bounds.hpp"
#include <cmath>
#include <iostream>

ItemRenderer::ItemRenderer() {
  m_postApocWorkshopTextures = {0};
  m_mushroomTexture = {0};
  m_postApocIconsTexture = {0};
  m_workshopPropIcons = {0};
}

void ItemRenderer::loadTextures() {
  if (IsWindowReady()) {
    m_postApocWorkshopTextures = assets::loadTexture(
        "assets/PostApoc_Workshop_16px.png", "ItemRenderer");
    m_mushroomTexture = assets::loadTexture(
        "assets/mushrooms_pixel_asset.png", "ItemRenderer");
    m_postApocIconsTexture = assets::loadTexture(
        "assets/PostApoc_Workshop_Icons.png", "ItemRenderer");
    m_ritualTexture = assets::loadTexture(
        "assets/Spritesheet_TheDarkRitual_BigWander.png", "ItemRenderer");
    m_workshopPropIcons =
        assets::loadTexture("assets/workshop_prop_icons.png", "ItemRenderer");
  } else {
    std::cerr << "[ERROR] Window not ready. Cannot load item textures!"
              << std::endl;
  }
}

ItemRenderer::~ItemRenderer() {
  if (IsWindowReady()) {
    UnloadTexture(m_postApocWorkshopTextures);
    UnloadTexture(m_mushroomTexture);
    UnloadTexture(m_postApocIconsTexture);
    UnloadTexture(m_ritualTexture);
    UnloadTexture(m_workshopPropIcons);
  }
}

// ============================================================================
// collect — World-Space Item Pass
// ============================================================================
// Uses the same frustum cull as MazeRenderer — only items within the visible
// screen region are queued. Every item stands on its own floor cell, so its
// base Y is that cell's bottom edge.
//
// Time Complexity: O(V) where V = number of visible cells on screen.
// Each cell is an O(1) lookup into the grid-parallel item array.
// ============================================================================
void ItemRenderer::collect(const World &world, const Camera2D &camera,
                           const Viewport &canvas, AreaState state,
                           DrawQueue &queue) {
  m_world = &world;
  const Maze *maze = world.asMaze();
  ViewBounds view = ViewBounds::fromCamera(world, camera, canvas);

  for (int y = view.startY; y <= view.endY; ++y) {
    for (int x = view.startX; x <= view.endX; ++x) {
      if (world.getItem(x, y) == ItemType::NONE)
        continue;
      // Floor check plus the room/corridor visibility rule, shared with the
      // magic book pass so the two cannot drift apart.
      if (maze && !isCellRenderable(*maze, x, y, state))
        continue;
      queue.push((y + 1) * grid::CELL, *this, x, y);
    }
  }
}

void ItemRenderer::drawQueued(int x, int y) const {
  const World &world = *m_world;
  const Maze *maze = world.asMaze();
  // --- Item Rendering (Grid-Parallel Switch) ---
  // O(1) lookup per cell. Adding a new item type means adding a
  // new case here — no need to touch any other rendering code.
  switch (world.getItem(x, y)) {
  case ItemType::TOXIC_WASTE: {
    // The plain workshop barrel, sold as toxic by a fluorescent green
    // glow rather than a badge. Additive halo behind, then the barrel,
    // then a tighter halo over it so the drum itself reads as lit.
    Rectangle cell = grid::cellRect(x, y);
    int cx = (int)(cell.x + cell.width / 2.0f);
    int cy = (int)(cell.y + cell.height / 2.0f);

    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(cx, cy, grid::CELL * 1.1f, Fade(theme::radiationGlow, 45 / 255.0f),
                       Fade(theme::radiationGlow, 0.0f));
    EndBlendMode();

    DrawTexturePro(m_postApocWorkshopTextures, grid::srcTile(2, 6), cell,
                   {0, 0}, 0.0f, WHITE);

    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(cx, cy, grid::CELL * 0.45f, Fade(theme::radiationGlow, 55 / 255.0f),
                       Fade(theme::radiationGlow, 0.0f));
    EndBlendMode();
    break;
  }
  case ItemType::MUSHROOM:
  case ItemType::MAGIC_MUSHROOM: {
    // Pseudo-random consistent hash picks one of the six variants: the
    // sheet is 3 columns wide, normal mushrooms on rows 0-1, magic ones
    // on rows 2-3.
    int tileIndex = (x * 73 + y * 37) % 6;
    int tx = tileIndex % 3;
    int ty = (world.getItem(x, y) == ItemType::MUSHROOM)
                 ? (tileIndex / 3)
                 : (tileIndex / 3) + 2;

    DrawTexturePro(m_mushroomTexture, grid::srcTile(tx, ty),
                   grid::cellRect(x, y), {0, 0}, 0.0f, WHITE);
    break;
  }
  case ItemType::CUPBOARD: {
    if (!maze)
      break; // furniture of the maze; the surface never holds one
    // --- Context-Aware Texture Selection ---
    // The cupboard's appearance depends on which wall it leans against.
    // Priority: wall above > wall right > wall left.
    // This is a simplified version of the bitmasking autotile pattern
    // used for walls, but only checking 3 directions.
    bool wallAbove = maze->getCell(x, y - 1) == Maze::CELL_WALL;
    bool wallRight = maze->getCell(x + 1, y) == Maze::CELL_WALL;
    bool wallLeft = maze->getCell(x - 1, y) == Maze::CELL_WALL;

    // Every cupboard is one 1x2 tile column on the workshop sheet. The
    // sheet lays its variants out on a grid: open states are the next
    // columns along, the red colourway is four rows down.
    //   front, blue  (14, 6)   side, blue  (15, 8)
    //   front, red   (14, 10)  side, red   (15, 12)
    int col = 14, row = 6;
    bool isSideways = !wallAbove && (wallRight || wallLeft);
    bool flipH = false;

    // Cupboards against the top wall are nudged upward so they sit
    // flush against the rendered wall face (which projects downward via
    // the Zelda-style wall system). Measured in art pixels.
    int liftArtPx = 0;

    if (wallAbove) {
      liftArtPx = 7;
    } else if (isSideways) {
      // The side sprite hugs the right edge of its tile, so it leans on
      // a wall to the right as drawn; a negative source width flips it
      // (Raylib convention) to lean left instead.
      col = 15;
      row = 8;
      flipH = wallLeft && !wallRight;
    }
    // No wall at all cannot happen given the spawn rules; falls through
    // to the front-facing sprite.

    // --- Color Variant Logic ---
    // Deterministically pick red or blue based on coordinates
    unsigned int hash = (unsigned int)(x * 73856093 ^ y * 19349663);
    if (hash % 2 == 0) {
      row += 4;
    }

    // --- Open Cupboard Logic ---
    if (maze->getItemState(x, y) == 1) { // 1 = open
      // Sideways cupboards have one open sprite regardless of contents;
      // front-facing ones show either stocked shelves or bare ones.
      col += (!isSideways && maze->isCupboardEmpty(x, y)) ? 2 : 1;
    }

    Rectangle cupSrc = grid::srcTile(col, row, 1, 2);
    Rectangle dest = grid::standingOn(cupSrc, x, y);
    dest.y -= liftArtPx * grid::WORLD_SCALE;
    if (flipH) {
      cupSrc.width = -cupSrc.width;
    }
    DrawTexturePro(m_postApocWorkshopTextures, cupSrc, dest, {0, 0}, 0.0f,
                   WHITE);
    break;
  }
  case ItemType::TABLE: {
    TableSprite table = computeTableSprite(world, x, y);
    if (!table.valid) {
      break; // Non-root tile (state 0 / 2) - the root draws the whole sprite
    }
    DrawTexturePro(m_postApocWorkshopTextures, table.src, table.dest,
                   {0, 0}, 0.0f, WHITE);
    // NOTE: the magic book is deliberately NOT drawn here - see
    // renderMagicBookOverlay(), which runs in a later, shader-exempt pass.
    break;
  }
  case ItemType::NONE:
  default:
    break;
  }
}

// ============================================================================
// computeTableSprite - Resolve a table root tile to its atlas + world rect
// ============================================================================
// The grey/non-grey variant is picked by a spatial hash of the coordinates:
// a deterministic, storage-free way to give each tile a stable pseudo-random
// look. The same (x, y) always hashes to the same variant, so the table does
// not flicker between frames and nothing needs to be persisted in the maze.
// The two variants have DIFFERENT pixel dimensions, which is exactly why this
// must be computed in one place only.
// ============================================================================
ItemRenderer::TableSprite
ItemRenderer::computeTableSprite(const World &world, int x, int y) const {
  TableSprite out = {};
  out.valid = false;

  int state = world.getItemState(x, y);
  // Only the "root" tiles draw (1 = horizontal right, 3 = vertical bottom).
  if (state != 1 && state != 3) {
    return out;
  }

  bool isGrey = ((unsigned int)(x * 73856093 ^ y * 19349663) % 2 == 0);

  // Source rectangles in art pixels. The workshop sheet is an 8px atlas once
  // halved, so a table top can be a half-tile tall; the density is still
  // exactly WORLD_SCALE because the dest is derived from the src.
  if (state == 1) { // Horizontal Right: 2 cells wide, 1.5 tall
    out.src = isGrey ? Rectangle{176, 8, 32, 24} : Rectangle{176, 200, 32, 24};
    out.dest = grid::standingOn(out.src, x, y, 2);
  } else { // state == 3, Vertical Bottom: 1 cell wide, 2 tall
    out.src = isGrey ? grid::srcTile(13, 0, 1, 2) : grid::srcTile(10, 12, 1, 2);
    out.dest = grid::standingOn(out.src, x, y);
  }

  out.valid = true;
  return out;
}

// ============================================================================
// renderMagicBookOverlay - Shader-exempt pass for the magic book
// ============================================================================
// Runs AFTER EndShaderMode() so the book does not warp with the trip shader.
// Because it is outside render()'s per-cell scan it still has to apply the two
// things that loop provides for free — the visibility rule and the frustum
// cull — but both now come from view_bounds.hpp rather than being restated
// here, so they cannot drift from the item pass. Table geometry likewise comes
// from computeTableSprite().
// ============================================================================
void ItemRenderer::renderMagicBookOverlay(const Maze &maze,
                                          const Camera2D &camera,
                                          const Viewport &view,
                                          AreaState state, Vector2 tripOffset,
                                          float glowScale,
                                          float simTime) const {
  if (!maze.isMagicBookSpawned()) {
    return;
  }

  int x = maze.getMagicBookX();
  int y = maze.getMagicBookY();

  if (!isCellRenderable(maze, x, y, state)) {
    return;
  }
  if (!ViewBounds::fromCamera(maze, camera, view).contains(x, y)) {
    return; // Off screen
  }

  TableSprite table = computeTableSprite(maze, x, y);
  if (!table.valid) {
    return; // The book is only ever placed on a table root tile.
  }

  Rectangle bookSrc = grid::srcTile(3, 6);
  const float bookW = bookSrc.width;
  const float bookH = bookSrc.height;

  // Sit the book on the table surface rather than its bounding-box center,
  // then ride along with whatever apparent motion the trip shader is giving
  // the table this frame.
  float centerX = table.dest.x + (table.dest.width / 2.0f) + tripOffset.x;
  float centerY = table.dest.y + (table.dest.height / 2.0f) -
                  4 * grid::WORLD_SCALE + tripOffset.y;

  // Drawn at 1.5x rather than the grid's 2x, by request: at a full cell the
  // book swallowed the table it sits on. The one sprite off the 2x rule.
  const float bookScale = 1.5f;
  float drawBookW = bookW * bookScale;
  float drawBookH = bookH * bookScale;

  // --- Pulsing glow ---
  // Map sin from [-1, 1] into [0, 1] so it can drive a lerp directly.
  float pulse = (sinf(simTime * 5.0f) + 1.0f) * 0.5f;

  BeginBlendMode(BLEND_ADDITIVE);

  // Soft radial halo. A scaled copy of the SPRITE would inherit the book's
  // silhouette and its hard pixel-art edges; real light scatter is radially
  // symmetric with a smooth falloff, so a gradient reads far better and costs
  // one draw call instead of two.
  float haloRadius = drawBookW * (0.95f + 0.15f * pulse) * glowScale;
  Color haloInner = {255, 80, 140, (unsigned char)(70 + 55 * pulse)};
  Color haloOuter = {255, 40, 90, 0};
  DrawCircleGradient((int)centerX, (int)centerY, haloRadius, haloInner,
                     haloOuter);

  // One tight sprite-shaped rim so the book's outline still reads as lit.
  float rimScale = bookScale * (1.04f + 0.03f * pulse);
  Rectangle destRim = {centerX, centerY, bookW * rimScale, bookH * rimScale};
  Vector2 originRim = {destRim.width / 2.0f, destRim.height / 2.0f};
  Color rimColor = {255, 105, 180, (unsigned char)(110 + 70 * pulse)};
  DrawTexturePro(m_ritualTexture, bookSrc, destRim, originRim, 0.0f, rimColor);

  EndBlendMode();

  // --- The book itself ---
  Rectangle destRectBook = {centerX, centerY, drawBookW, drawBookH};
  Vector2 origin = {drawBookW / 2.0f, drawBookH / 2.0f};
  DrawTexturePro(m_ritualTexture, bookSrc, destRectBook, origin, 0.0f, WHITE);
}

// ============================================================================
// atlasFor - resolve a UiTexture name to the loaded handle
// ============================================================================
// This is still a switch, but note what it switches ON. It used to be a switch
// over ItemType, which grows with every item added; this one is over the atlas
// set, which the renderer already owns and which changes only when an artist
// hands over a new sheet. Adding an item is now one edit in ItemDatabase.
// ============================================================================
Texture2D ItemRenderer::atlasFor(UiTexture which) const {
  switch (which) {
  case UiTexture::WORKSHOP_PROPS:
    return m_workshopPropIcons;
  case UiTexture::WORKSHOP_ICONS:
    return m_postApocIconsTexture;
  case UiTexture::RITUAL:
    return m_ritualTexture;
  case UiTexture::MUSHROOMS:
  default:
    return m_mushroomTexture;
  }
}

void ItemRenderer::renderItemUI(ItemType type, Rectangle destRect,
                                Color tint) const {
  const auto &def = ItemDatabase::getDef(type);

  // The barrel has no inventory icon: it is drawn as a flat green swatch, so
  // it deliberately never reaches the atlas path.
  if (type == ItemType::TOXIC_WASTE) {
    DrawRectangleRec(destRect, theme::good);
    return;
  }

  if (def.uiSpriteRect.width > 0 && def.uiSpriteRect.height > 0) {
    DrawTexturePro(atlasFor(def.uiTexture), def.uiSpriteRect, destRect, {0, 0},
                   0.0f, tint);
  } else {
    DrawRectangleRec(destRect, tint);
  }
}
