#pragma once
#include "core/viewport.hpp"
#include "render/draw_queue.hpp"
#include "world/maze.hpp"
#include "world/world.hpp"
#include <raylib.h>

// ============================================================================
// ItemRenderer — Decoupled Item Rendering System
// ============================================================================
// Owns all item-related textures and handles both world-space item drawing
// and screen-space inventory icon drawing. Extracted from MazeRenderer to
// enforce Single Responsibility: MazeRenderer handles terrain/walls,
// ItemRenderer handles items on top.
//
// Design Pattern: Strategy (separating rendering concerns by domain).
//
// World items do not draw themselves in a fixed order any more: collect()
// pushes each visible one into the frame's DrawQueue at the bottom edge of its
// floor cell, and the queue interleaves them with the player by base Y. So the
// player walks behind a cupboard from above and in front of it from below.
// ============================================================================
class ItemRenderer : public Drawer {
public:
  ItemRenderer();
  ~ItemRenderer();

  void loadTextures();

  // --- World-Space Rendering ---
  // Pushes every item the camera can see into `queue`, keyed by the bottom
  // edge of its floor cell. In the maze the room/corridor visibility rule
  // applies (isCellRenderable); on the surface every item in view draws.
  // Binds `world` until the queue is flushed.
  void collect(const World &world, const Camera2D &camera,
               const Viewport &canvas, AreaState state, DrawQueue &queue);

  // Drawer: draws the item standing on cell (x, y) of the bound world.
  void drawQueued(int x, int y) const override;

  // Draws the magic book of maps on its table, as a SEPARATE pass.
  //
  // Why it is not part of render(): the trip shader is a screen-space
  // post-process over the whole scene render texture, so the only way to
  // exempt an object from the distortion is to draw it in a later pass,
  // after EndShaderMode(). The book is a stable ritual object and must not
  // warp with the hallucination.
  // tripOffset shifts the book in WORLD units to track the apparent motion
  // the trip shader gives the table underneath it. Without it the book is the
  // only stationary thing on a swimming screen, which reads as the book
  // floating in circles above the table. MazeState computes the value
  // because it owns the shader; the renderer stays ignorant of it.
  //
  // glowScale multiplies the halo radius, for tuning by eye.
  //
  // simTime is accumulated game time, not GetTime(): a scripted replay must
  // produce the same frame regardless of how long the process has been up.
  //
  // This pass draws AFTER the canvas is blitted, so `camera` and `view` are
  // the WINDOW-space pair, not the scene's canvas pair.
  void renderMagicBookOverlay(const Maze &maze, const Camera2D &camera,
                              const Viewport &view, AreaState state,
                              Vector2 tripOffset, float glowScale,
                              float simTime) const;

  // --- Screen-Space UI Rendering ---
  // Draws an item icon for the inventory/hotbar UI.
  void renderItemUI(ItemType type, Rectangle destRect,
                    Color tint = WHITE) const;

private:
  // Resolved sprite for a table root tile: which atlas rect to sample and
  // where it lands in world space.
  struct TableSprite {
    Rectangle src;  // Source rect within m_postApocWorkshopTextures
    Rectangle dest; // World-space destination rect
    bool valid;     // False when (x, y) is not a table ROOT tile (state 1 or 3)
  };

  // Single source of truth for table geometry. Both the item pass and the
  // magic book overlay pass need the destination rect, and the two variants
  // (grey / non-grey, chosen by a spatial hash) have DIFFERENT pixel
  // dimensions - so computing it in two places silently misplaces the book.
  TableSprite computeTableSprite(const World &world, int x, int y) const;

  // Maps the atlas name an ItemDefinition carries to the loaded handle.
  Texture2D atlasFor(UiTexture which) const;

  Texture2D m_postApocWorkshopTextures;
  Texture2D m_mushroomTexture;
  Texture2D m_postApocIconsTexture;
  Texture2D m_workshopPropIcons; // the two 1:1 prop cut-outs the bag shows
  Texture2D m_ritualTexture;

  const World *m_world = nullptr; // bound by collect(), read by drawQueued()
};
