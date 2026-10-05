#pragma once

#include "core/input_state.hpp"
#include "entities/player.hpp"
#include "render/item_renderer.hpp"
#include "raylib.h"
#include "world/maze.hpp"
#include "world/world.hpp"
#include <string>
#include <vector>
#include <unordered_map>

struct DrawnMapData {
  int id;
  int centerX;
  int centerY;
  RenderTexture2D texture;
};

enum class PopupType {
  BOXED_BOTTOM, // Black box, white text (e.g., "Press K to use door", "Dam it's
                // dark")
  SUBTLE_BOTTOM, // Plain white text slightly above hotbar (e.g., "Cannot place
                 // there!")
  HEADER_GREEN,  // Large text at top, green with shadow (e.g., "Radiation!")
  HEADER_RAINBOW // Large text at top, color cycling (e.g., "Magic mushroom
                 // consumed!")
};

struct ActivePopup {
  std::string text;
  PopupType type;
  float timer;
  float maxDuration;
};

// ============================================================================
// InventoryLayout - the single source of inventory geometry
// ============================================================================
// Input handling and drawing must agree, to the pixel, on where every slot
// sits. They used to agree by accident: both lived inside renderInventory, so
// a click was hit-tested against the rectangle being drawn a line earlier.
// Splitting them means the geometry has to become an explicit, shared value -
// derived once per frame from the window size and handed to both passes.
//
// Everything here is a pure function of (scale, screenW, screenH), so it holds
// no state and can be recomputed freely.
// ============================================================================
struct InventoryLayout {
  // The bag is a HOTBAR_SLOTS-wide grid; the hotbar is its first row, drawn
  // detached at the bottom of the screen. The cupboard is the same slot count
  // in a wider, shallower grid so it does not collide with the bag below it.
  static constexpr int COLUMNS = HOTBAR_SLOTS;
  static constexpr int BAG_ROWS = (INVENTORY_SLOTS - HOTBAR_SLOTS) / COLUMNS;
  static constexpr int CUPBOARD_COLUMNS = 10;
  static constexpr int CUPBOARD_ROWS = INVENTORY_SLOTS / CUPBOARD_COLUMNS;

  float scale;
  float slotSize;
  float padding;
  float hotbarX, hotbarY;     // top-left of hotbar slot 0
  float bagX, bagY;           // top-left of bag slot HOTBAR_SLOTS
  float cupboardX, cupboardY; // top-left of cupboard slot 0
  float craftingY;            // top of the crafting icon row

  static InventoryLayout compute(float scale, int screenW, int screenH);

  Rectangle playerSlot(int index) const;   // 0..INVENTORY_SLOTS-1
  Rectangle cupboardSlot(int index) const; // 0..INVENTORY_SLOTS-1
  Rectangle craftingSlot(int visibleIdx) const;
  Rectangle craftButton() const;
};

class UIManager {
public:
  UIManager(int screenWidth, int screenHeight);
  ~UIManager();

  // Setup and input handling
  void update(float dt);

  // Applies this frame's mouse input to the inventory, cupboard and crafting
  // menu. MUST be called from the state's input phase, before render(), and is
  // the ONLY place UIManager mutates a Player or a World - render() is now
  // read-only with respect to game state. Cupboards, doors and the magic
  // book's map are maze furniture: they only appear when world.asMaze().
  void handleInventoryInput(Player &player, World &world, const InputState &in);

  // Renders the shipping UI: popups, inventory, cupboards and map overlays.
  // The debug panel is DebugOverlay's job and draws after this.
  // `in` is read only for the mouse position (hover highlights, tooltips);
  // the read-only-with-respect-to-game-state rule above still holds.
  void render(Player &player, World &world, ItemRenderer &itemRenderer,
              bool isDroppingItem, float totalTime, const InputState &in);

  // The scale UIManager lays its own widgets out with. DebugOverlay reuses it
  // so the panel's text tracks the window size the same way the game UI does.
  float getUIScale() const {
    float sx = (float)GetScreenWidth() / m_screenWidth;
    float sy = (float)GetScreenHeight() / m_screenHeight;
    return sx < sy ? sx : sy;
  }

  // Standardized popup system
  void showPopup(const std::string &text, PopupType type,
                 float duration = 3.0f);

  // --- State Toggles ---
  bool isInventoryOpen() const { return m_inventoryOpen; }
  void toggleInventory() { m_inventoryOpen = !m_inventoryOpen; }

  int getActiveHotbarSlot() const { return m_activeHotbarSlot; }
  void setActiveHotbarSlot(int slot) { m_activeHotbarSlot = slot; }

  int getHeldSlotIndex() const { return m_heldSlotIndex; }
  void setHeldSlotIndex(int index) {
    m_heldSlotIndex = index;
    m_heldFromCupboard = false;
  }

  // --- Cupboard State ---
  bool isCupboardInventoryOpen() const { return m_cupboardInventoryOpen; }
  int getOpenedCupboardX() const { return m_openedCupboardX; }
  int getOpenedCupboardY() const { return m_openedCupboardY; }
  void openCupboard(int x, int y) {
    m_cupboardInventoryOpen = true;
    m_openedCupboardX = x;
    m_openedCupboardY = y;
  }
  void closeCupboard() {
    m_cupboardInventoryOpen = false;
    m_openedCupboardX = -1;
    m_openedCupboardY = -1;
  }

  // --- Map Overlay State ---
  void openFullscreenMap(int mapId) {
    m_showFullscreenMap = true;
    m_openedMapId = mapId;
  }
  void closeFullscreenMap() { m_showFullscreenMap = false; }
  bool isFullscreenMapOpen() const { return m_showFullscreenMap; }
  int getOpenedMapId() const { return m_openedMapId; }
  void markMapDrawn(int mapId, Maze& maze, int centerX, int centerY);



  // The magic book's map is a cached texture of a fixed slice of the world,
  // so any layout change invalidates it.
  void markMagicBookMapDirty() { m_magicBookMapDirty = true; }

private:
  void renderInventory(Player &player, Maze *maze, ItemRenderer &itemRenderer,
                       const InventoryLayout &layout, int screenW,
                       int screenH, const InputState &in);

  // Resolves one click on one slot: pick up, put down, merge stacks or swap.
  // isCupboardSlot selects which of the two open containers `index` addresses,
  // which is also how a stack crosses between them.
  void applySlotClick(int index, bool isCupboardSlot, Player &player,
                      Maze *maze);
  void renderPopups(float scale, int screenW, int screenH, float totalTime);
  void generateMagicBookMap(Maze &maze);

  // Screen dimensions
  int m_screenWidth;
  int m_screenHeight;

  // Popups
  std::vector<ActivePopup> m_activePopups;

  // Inventory State
  bool m_inventoryOpen = false;
  int m_heldSlotIndex = -1; // -1 if no item is held
  bool m_heldFromCupboard = false;
  int m_activeHotbarSlot = 0;

  // Cupboard State
  bool m_cupboardInventoryOpen = false;
  int m_openedCupboardX = -1;
  int m_openedCupboardY = -1;

  // Crafting State
  int m_selectedCraftingRecipeIdx = -1;
  // Both in simulation seconds (accumulated in update), never GetTime(), so a
  // replayed session flashes on the same frames as the original.
  float m_simTime = 0.0f;
  float m_craftFlashEndTime = 0.0f;

  // Drawn Maps Cache
  std::unordered_map<int, DrawnMapData> m_drawnMaps;
  bool m_showFullscreenMap = false;
  int m_openedMapId = 0;

  // Magic Book Map: a fixed window of the maze, one texel per cell. The
  // same numbers size the texture, fill it, and place the player dot.
  static constexpr int kBookMapX = 55, kBookMapY = 30;
  static constexpr int kBookMapW = 139, kBookMapH = 89;
  RenderTexture2D m_magicBookMapTexture;
  bool m_magicBookMapDirty = true;

  // Hand-drawn maps: a window this size centred on where the map was made.
  static constexpr int kDrawnMapW = 53, kDrawnMapH = 35;
  static constexpr int kDrawnMapX(int centerX) { return centerX - kDrawnMapW / 2; }
  static constexpr int kDrawnMapY(int centerY) { return centerY - kDrawnMapH / 2; }
};
