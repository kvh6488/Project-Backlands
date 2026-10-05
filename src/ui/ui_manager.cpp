#include "ui/ui_manager.hpp"
#include "render/theme.hpp"
#include "imgui.h"
#include "items/crafting_system.hpp"
#include "items/item_database.hpp"
#include "rlImGui.h"
#include <algorithm>
#include <cmath>


UIManager::UIManager(int screenWidth, int screenHeight)
    : m_screenWidth(screenWidth), m_screenHeight(screenHeight) {
  m_magicBookMapTexture.id = 0;
}

UIManager::~UIManager() {
  if (m_magicBookMapTexture.id != 0) {
    UnloadRenderTexture(m_magicBookMapTexture);
  }
  for (auto &pair : m_drawnMaps) {
    UnloadRenderTexture(pair.second.texture);
  }
}

void UIManager::update(float dt) {
  m_simTime += dt;

  // Update active popups
  for (auto it = m_activePopups.begin(); it != m_activePopups.end();) {
    it->timer -= dt;
    if (it->timer <= 0.0f) {
      it = m_activePopups.erase(it);
    } else {
      ++it;
    }
  }
}

void UIManager::showPopup(const std::string &text, PopupType type,
                          float duration) {
  // Check if we already have this exact popup active, if so, just refresh the
  // timer
  for (auto &popup : m_activePopups) {
    if (popup.text == text && popup.type == type) {
      popup.timer = duration;
      popup.maxDuration = duration;
      return;
    }
  }

  // Otherwise add a new popup
  m_activePopups.push_back({text, type, duration, duration});
}

void UIManager::render(Player &player, World &world,
                       ItemRenderer &itemRenderer, bool isDroppingItem,
                       float totalTime, const InputState &in) {
  Maze *maze = world.asMaze();
  float scale = std::min((float)GetScreenWidth() / m_screenWidth,
                         (float)GetScreenHeight() / m_screenHeight);
  int screenW = GetScreenWidth();
  int screenH = GetScreenHeight();

  // 1. Check if we need to regenerate the magic book's map
  if (maze && (m_magicBookMapTexture.id == 0 || m_magicBookMapDirty)) {
    if (m_magicBookMapTexture.id == 0) {
      m_magicBookMapTexture = LoadRenderTexture(kBookMapW, kBookMapH);
    }
    generateMagicBookMap(*maze);
    m_magicBookMapDirty = false;
  }

  // 2. Door prompt (special hardcoded logic for now)
  int doorCount = maze ? player.getAvailableDoors(*maze) : 0;
  if (doorCount == 1) {
    const char *msg = "Press 'K' to use door";
    int textWidth = MeasureText(msg, 30 * scale);
    DrawText(msg, (screenW - textWidth) / 2, screenH - (115 * scale),
             30 * scale, WHITE);
  } else if (doorCount >= 2) {
    const char *msg = "Press 'K' for door 1, press 'L' for door 2";
    int textWidth = MeasureText(msg, 30 * scale);
    DrawText(msg, (screenW - textWidth) / 2, screenH - (115 * scale),
             30 * scale, WHITE);
  }

  // 3. Drop prompt
  if (isDroppingItem) {
    const char *msg = "Select location to place (Q to cancel)";
    int textWidth = MeasureText(msg, 30 * scale);
    int x = (screenW - textWidth) / 2;
    int y = screenH - (190 * scale);

    DrawRectangle(x - (15 * scale), y - (5 * scale), textWidth + (30 * scale),
                  40 * scale, Fade(theme::ground, 0.6f));
    DrawText(msg, x, y, 30 * scale, WHITE);
  }

  // 4. Standardized Popups
  renderPopups(scale, screenW, screenH, totalTime);

  // 5. Inventory
  renderInventory(player, maze, itemRenderer,
                  InventoryLayout::compute(scale, screenW, screenH), screenW,
                  screenH, in);

  // 5.5 Map Rendering
  if (m_showFullscreenMap && (m_openedMapId == -1 || m_drawnMaps.find(m_openedMapId) != m_drawnMaps.end())) {
    
    RenderTexture2D texToDraw;
    int texWidth, texHeight;
    int startX, startY;
    bool isMagicBookMap = (m_openedMapId == -1);

    if (isMagicBookMap) {
      texToDraw = m_magicBookMapTexture;
      texWidth = kBookMapW;
      texHeight = kBookMapH;
      startX = kBookMapX;
      startY = kBookMapY;
    } else {
      const auto &mapData = m_drawnMaps[m_openedMapId];
      texToDraw = mapData.texture;
      texWidth = texToDraw.texture.width;
      texHeight = texToDraw.texture.height;
      startX = kDrawnMapX(mapData.centerX);
      startY = kDrawnMapY(mapData.centerY);
    }

    // The texture is flipped vertically by OpenGL, so use negative height
    Rectangle srcRec = {0, 0, (float)texWidth, -(float)texHeight};

    // Calculate relative position for player dot
    Vector2 pPos = player.getPosition();
    int gridX = world.toGridX(pPos.x);
    int gridY = world.toGridY(pPos.y);

    int relX = gridX - startX;
    int relY = gridY - startY;

    // Handle wrapping (toroidal)
    if (relX < -world.getWidth() / 2)
      relX += world.getWidth();
    else if (relX > world.getWidth() / 2)
      relX -= world.getWidth();
    if (relY < -world.getHeight() / 2)
      relY += world.getHeight();
    else if (relY > world.getHeight() / 2)
      relY -= world.getHeight();

    bool inBounds = (relX >= 0 && relX < texWidth && relY >= 0 && relY < texHeight);

    // Fullscreen Overlay
    DrawRectangle(0, 0, screenW, screenH, Fade(theme::ground, 0.85f));

    float fsScale = std::min(screenW / (texWidth * 1.2f), screenH / (texHeight * 1.2f));
    float fsWidth = texWidth * fsScale;
    float fsHeight = texHeight * fsScale;
    float fsX = (screenW - fsWidth) / 2.0f;
    float fsY = (screenH - fsHeight) / 2.0f;

    DrawTexturePro(texToDraw.texture, srcRec,
                    Rectangle{fsX, fsY, fsWidth, fsHeight}, Vector2{0, 0},
                    0.0f, WHITE);

    if (inBounds) {
      float px = fsX + relX * fsScale + (fsScale / 2.0f);
      float py = fsY + relY * fsScale + (fsScale / 2.0f);
      // Ring first, dot on top: the ring separates the dot from any map colour.
      DrawCircle(px, py, 7.0f * scale, theme::ground);
      DrawCircle(px, py, 5.0f * scale, theme::mapPlayer);
    }
    DrawRectangleLinesEx(Rectangle{fsX, fsY, fsWidth, fsHeight}, 4.0f * scale,
                          WHITE);
  }

}

void UIManager::renderPopups(float scale, int screenW, int screenH,
                             float totalTime) {
  for (const auto &popup : m_activePopups) {
    float alpha = std::min(1.0f, popup.timer * 2.0f);

    if (popup.type == PopupType::BOXED_BOTTOM) {
      int textWidth = MeasureText(popup.text.c_str(), 30 * scale);
      int x = (screenW - textWidth) / 2;
      int y = screenH - (147 * scale);

      DrawRectangle(x - (15 * scale), y - (5 * scale), textWidth + (30 * scale),
                    40 * scale, Fade(theme::ground, 0.6f));
      DrawText(popup.text.c_str(), x, y, 30 * scale, Fade(theme::ink, alpha));
    } else if (popup.type == PopupType::SUBTLE_BOTTOM) {
      int textWidth = MeasureText(popup.text.c_str(), 30 * scale);
      int x = (screenW - textWidth) / 2;
      int y = screenH - (150 * scale);
      DrawText(popup.text.c_str(), x, y, 30 * scale, Fade(theme::ink, alpha));
    } else if (popup.type == PopupType::HEADER_GREEN) {
      int textWidth = MeasureText(popup.text.c_str(), 60 * scale);
      int x = (screenW - textWidth) / 2;
      int y = screenH / 8;

      DrawText(popup.text.c_str(), x + 2 * scale, y + 2 * scale, 60 * scale,
               Fade(theme::ground, alpha * 0.7f));
      DrawText(popup.text.c_str(), x, y, 60 * scale, Fade(theme::good, alpha));
    } else if (popup.type == PopupType::HEADER_RAINBOW) {
      int textWidth = MeasureText(popup.text.c_str(), 40 * scale);
      int x = (screenW - textWidth) / 2;
      int y = 50 * scale;

      Color rainbow =
          ColorFromHSV(fmodf(totalTime * 100.0f, 360.0f), 1.0f, 1.0f);

      DrawText(popup.text.c_str(), x + 2 * scale, y + 2 * scale, 40 * scale,
               Fade(theme::ground, alpha * 0.7f));
      DrawText(popup.text.c_str(), x, y, 40 * scale, Fade(rainbow, alpha));
    }
  }
}
namespace {

// A recipe is shown once the player has unlocked it, or is holding any one of
// its ingredients - so the menu teaches recipes by acquisition rather than
// listing everything up front. Input and drawing must filter identically, or a
// click would select a different recipe than the one under the cursor.
bool isRecipeVisible(const Player &player, const Recipe &recipe) {
  if (player.hasUnlockedRecipe(recipe.result)) {
    return true;
  }
  for (const auto &ing : recipe.ingredients) {
    if (player.hasIngredient(ing.type, 1)) {
      return true;
    }
  }
  return false;
}

} // namespace

// ============================================================================
// InventoryLayout
// ============================================================================
InventoryLayout InventoryLayout::compute(float scale, int screenW,
                                         int screenH) {
  InventoryLayout l{};
  l.scale = scale;
  l.slotSize = 60 * scale;
  l.padding = 10 * scale;

  float step = l.slotSize + l.padding;
  float rowWidth = (COLUMNS * l.slotSize) + ((COLUMNS - 1) * l.padding);

  // The hotbar anchors to the bottom of the screen; everything else stacks
  // upward from it, so the whole panel stays put when the window resizes.
  l.hotbarX = (screenW - rowWidth) / 2.0f;
  l.hotbarY = screenH - l.slotSize - (20 * scale);

  l.bagX = l.hotbarX;
  l.bagY = l.hotbarY - (BAG_ROWS * step) - (20 * scale);

  float cupboardWidth =
      (CUPBOARD_COLUMNS * l.slotSize) + ((CUPBOARD_COLUMNS - 1) * l.padding);
  l.cupboardX = (screenW - cupboardWidth) / 2.0f;
  l.cupboardY = l.bagY - (CUPBOARD_ROWS * step) - (40 * scale);

  // The crafting row needs headroom for the selected recipe's detail panel,
  // which draws below it and would otherwise land on top of the bag.
  l.craftingY = l.bagY - (260 * scale);
  return l;
}

Rectangle InventoryLayout::playerSlot(int index) const {
  float step = slotSize + padding;
  if (index < HOTBAR_SLOTS) {
    return Rectangle{hotbarX + index * step, hotbarY, slotSize, slotSize};
  }
  int bagIndex = index - HOTBAR_SLOTS;
  return Rectangle{bagX + (bagIndex % COLUMNS) * step,
                   bagY + (bagIndex / COLUMNS) * step, slotSize, slotSize};
}

Rectangle InventoryLayout::cupboardSlot(int index) const {
  float step = slotSize + padding;
  return Rectangle{cupboardX + (index % CUPBOARD_COLUMNS) * step,
                   cupboardY + (index / CUPBOARD_COLUMNS) * step, slotSize,
                   slotSize};
}

Rectangle InventoryLayout::craftingSlot(int visibleIdx) const {
  return Rectangle{bagX + visibleIdx * (slotSize + padding), craftingY,
                   slotSize, slotSize};
}

Rectangle InventoryLayout::craftButton() const {
  float detailsY = craftingY + slotSize + (10 * scale);
  float btnW = 100 * scale;
  float btnH = 40 * scale;
  float rowWidth = (COLUMNS * slotSize) + ((COLUMNS - 1) * padding);
  return Rectangle{bagX + rowWidth - btnW, detailsY + (15 * scale), btnW, btnH};
}

// ============================================================================
// handleInventoryInput - the UI's only write path into the game
// ============================================================================
// Runs in the state's input phase. Slots are tested in draw order, and the
// first hit consumes the click, so a stack picked up here cannot be put back
// down by a later slot in the same frame.
// ============================================================================
void UIManager::handleInventoryInput(Player &player, World &world,
                                     const InputState &in) {
  Maze *maze = world.asMaze();
  // Closed panels swallow no clicks: the hotbar is on screen permanently, but
  // has never been clickable while the bag is shut.
  if (!m_inventoryOpen && !m_cupboardInventoryOpen) {
    return;
  }

  bool leftClick = in.mouseLeftPressed;
  bool rightClick = in.mouseRightPressed;
  if (!leftClick && !rightClick) {
    return;
  }

  InventoryLayout layout = InventoryLayout::compute(
      getUIScale(), GetScreenWidth(), GetScreenHeight());
  Vector2 mouse = in.mouse;

  // 1. Player slots - the hotbar row plus the bag when it is open.
  int playerSlotCount = m_inventoryOpen ? INVENTORY_SLOTS : HOTBAR_SLOTS;
  for (int i = 0; i < playerSlotCount; ++i) {
    if (!CheckCollisionPointRec(mouse, layout.playerSlot(i))) {
      continue;
    }
    if (leftClick) {
      applySlotClick(i, false, player, maze);
    } else if (m_heldSlotIndex == -1) {
      player.consumeItem(i); // Only the player's own slots can be consumed.
    }
    return;
  }

  // 2. Cupboard slots.
  if (m_cupboardInventoryOpen) {
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
      if (!CheckCollisionPointRec(mouse, layout.cupboardSlot(i))) {
        continue;
      }
      if (leftClick) {
        applySlotClick(i, true, player, maze);
      }
      return;
    }
  }

  // 3. Crafting menu. Hidden while a cupboard is open - the two panels would
  // occupy the same screen space.
  if (!m_inventoryOpen || m_cupboardInventoryOpen || !leftClick) {
    return;
  }

  const auto &recipes = CraftingSystem::getRecipes();
  int visibleIdx = 0;
  for (size_t i = 0; i < recipes.size(); ++i) {
    if (!isRecipeVisible(player, recipes[i])) {
      continue;
    }
    if (CheckCollisionPointRec(mouse, layout.craftingSlot(visibleIdx))) {
      // Clicking the selected recipe again closes its detail panel.
      m_selectedCraftingRecipeIdx =
          (m_selectedCraftingRecipeIdx == (int)i) ? -1 : (int)i;
      return;
    }
    visibleIdx++;
  }

  if (m_selectedCraftingRecipeIdx >= 0 &&
      m_selectedCraftingRecipeIdx < (int)recipes.size() &&
      CheckCollisionPointRec(mouse, layout.craftButton())) {
    const auto &recipe = recipes[m_selectedCraftingRecipeIdx];
    if (player.canCraft(recipe)) {
      player.craftItem(recipe);
    } else {
      // Flash the missing ingredient counts rather than raise a popup - the
      // reason the craft failed is already on screen, it just needs pointing
      // at.
      m_craftFlashEndTime = m_simTime + 0.5f;
    }
  }
}

// ============================================================================
// applySlotClick - pick up, put down, merge or swap
// ============================================================================
void UIManager::applySlotClick(int index, bool isCupboardSlot, Player &player,
                               Maze *maze) {
  auto &playerInv = player.getInventoryRef();
  auto *cupboardInv =
      m_cupboardInventoryOpen && maze
          ? &maze->getCupboardInventory(m_openedCupboardX, m_openedCupboardY)
          : nullptr;
  if (isCupboardSlot && cupboardInv == nullptr) {
    return;
  }

  auto &targetInv = isCupboardSlot ? *cupboardInv : playerInv;

  if (m_heldSlotIndex == -1) {
    // Nothing in hand: lift this slot, if it holds anything.
    if (targetInv[index].type != ItemType::NONE) {
      m_heldSlotIndex = index;
      m_heldFromCupboard = isCupboardSlot;
    }
    return;
  }

  auto &heldInv = m_heldFromCupboard ? *cupboardInv : playerInv;
  InventorySlot &held = heldInv[m_heldSlotIndex];
  InventorySlot &target = targetInv[index];

  if (held.type != ItemType::NONE && held.type == target.type) {
    // Same type: pour as much as the destination stack will take. Any
    // remainder stays in the source slot rather than overflowing.
    int spaceInTarget =
        ItemDatabase::getDef(target.type).maxStackSize - target.count;
    int amountToMove =
        spaceInTarget > 0 ? std::min(held.count, spaceInTarget) : 0;
    target.count += amountToMove;
    held.count -= amountToMove;
    if (held.count <= 0) {
      held.type = ItemType::NONE;
      held.count = 0;
    }
  } else {
    std::swap(held, target);
  }

  m_heldSlotIndex = -1;
  m_heldFromCupboard = false;
}

// ============================================================================
// renderInventory - drawing only
// ============================================================================
// Read-only with respect to the Player and the Maze. The only state it writes
// is its own hover bookkeeping, which exists purely to place the tooltip.
// Every click was already resolved by handleInventoryInput earlier this frame.
// ============================================================================
void UIManager::renderInventory(Player &player, Maze *maze,
                                ItemRenderer &itemRenderer,
                                const InventoryLayout &layout, int screenW,
                                int screenH, const InputState &in) {
  const float scale = layout.scale;
  const float slotSize = layout.slotSize;
  const float padding = layout.padding;

  ItemType hoveredItemType = ItemType::NONE;
  int hoveredInstanceId = 0;
  bool hoveredIsCrafting = false;

  const auto &playerInv = player.getInventory();
  const std::array<InventorySlot, INVENTORY_SLOTS> *cupboardInv =
      m_cupboardInventoryOpen && maze
          ? &maze->getCupboardInventory(m_openedCupboardX, m_openedCupboardY)
          : nullptr;

  auto drawSlot = [&](int index, Rectangle slotRect, bool isHotbar,
                      bool isCupboardSlot) {
    bool isHeldSlot =
        (index == m_heldSlotIndex) && (m_heldFromCupboard == isCupboardSlot);
    Color bgColor = isHeldSlot ? Fade(theme::highlight, 0.3f) : Fade(theme::ground, 0.7f);
    DrawRectangleRec(slotRect, bgColor);

    if (!isCupboardSlot && index == m_activeHotbarSlot) {
      DrawRectangleLinesEx(slotRect, 4.0f * scale, WHITE);
    } else {
      DrawRectangleLinesEx(slotRect, 2.0f * scale, theme::border);
    }

    const auto &currentInv = isCupboardSlot ? *cupboardInv : playerInv;

    if (CheckCollisionPointRec(in.mouse, slotRect)) {
      DrawRectangleLinesEx(slotRect, 3.0f * scale, WHITE);
      if (currentInv[index].type != ItemType::NONE && !isHeldSlot) {
        hoveredItemType = currentInv[index].type;
        hoveredInstanceId = currentInv[index].instanceId;
      }
    }

    const InventorySlot &slot = currentInv[index];
    if (slot.type != ItemType::NONE && !isHeldSlot) {
      Rectangle destRect = {slotRect.x + padding, slotRect.y + padding,
                            slotSize - 2 * padding, slotSize - 2 * padding};
      itemRenderer.renderItemUI(slot.type, destRect, WHITE);

      if (slot.count > 1) {
        DrawText(TextFormat("%d", slot.count),
                 slotRect.x + slotSize - 20 * scale,
                 slotRect.y + slotSize - 20 * scale, 20 * scale, WHITE);
      }
    }

    if (isHotbar) {
      DrawText(TextFormat("%d", index + 1), slotRect.x + 5 * scale,
               slotRect.y + 5 * scale, 15 * scale, theme::inkDim);
    }
  };

  if (m_inventoryOpen || m_cupboardInventoryOpen) {
    DrawRectangle(0, 0, screenW, screenH, Fade(theme::ground, 0.75f));
  }

  // 1. Hotbar (always on screen).
  for (int i = 0; i < HOTBAR_SLOTS; ++i) {
    drawSlot(i, layout.playerSlot(i), true, false);
  }

  if (!m_inventoryOpen && !m_cupboardInventoryOpen) {
    return;
  }

  // 2. Bag.
  for (int i = HOTBAR_SLOTS; i < INVENTORY_SLOTS; ++i) {
    drawSlot(i, layout.playerSlot(i), false, false);
  }

  // 3. Cupboard.
  if (m_cupboardInventoryOpen) {
    DrawText("Cupboard", layout.cupboardX, layout.cupboardY - 30 * scale,
             20 * scale, WHITE);
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
      drawSlot(i, layout.cupboardSlot(i), false, true);
    }
  }

  // 4. Crafting menu.
  if (m_inventoryOpen && !m_cupboardInventoryOpen) {
    DrawText("Crafting", layout.bagX, layout.craftingY - 25 * scale, 20 * scale,
             WHITE);

    const auto &recipes = CraftingSystem::getRecipes();
    int visibleIdx = 0;

    for (size_t i = 0; i < recipes.size(); ++i) {
      const auto &recipe = recipes[i];
      if (!isRecipeVisible(player, recipe)) {
        continue;
      }

      Rectangle slotRect = layout.craftingSlot(visibleIdx);
      bool isSelected = (m_selectedCraftingRecipeIdx == (int)i);
      DrawRectangleRec(slotRect,
                       isSelected ? Fade(theme::highlight, 0.3f) : Fade(theme::ground, 0.7f));
      DrawRectangleLinesEx(slotRect, 2.0f * scale, isSelected ? theme::ink : theme::border);

      if (CheckCollisionPointRec(in.mouse, slotRect)) {
        DrawRectangleLinesEx(slotRect, 3.0f * scale, WHITE);
        hoveredItemType = recipe.result;
        hoveredIsCrafting = true;
      }

      Rectangle destRect = {slotRect.x + padding, slotRect.y + padding,
                            slotSize - 2 * padding, slotSize - 2 * padding};
      itemRenderer.renderItemUI(recipe.result, destRect, WHITE);

      visibleIdx++;
    }

    if (visibleIdx == 0) {
      DrawText("Go get yourself some items", layout.bagX, layout.craftingY,
               20 * scale, theme::border);
    }

    // Selected recipe details.
    if (m_selectedCraftingRecipeIdx >= 0 &&
        m_selectedCraftingRecipeIdx < (int)recipes.size()) {
      const auto &recipe = recipes[m_selectedCraftingRecipeIdx];
      const auto &resultDef = ItemDatabase::getDef(recipe.result);

      float detailsX = layout.bagX;
      float detailsY = layout.craftingY + slotSize + (10 * scale);

      DrawText(TextFormat("Recipe: %s", resultDef.name.c_str()), detailsX,
               detailsY, 20 * scale, WHITE);

      float ingX = detailsX;
      float ingY = detailsY + 25 * scale;
      for (size_t i = 0; i < recipe.ingredients.size(); ++i) {
        const auto &ing = recipe.ingredients[i];
        Rectangle ingSlotRect = {ingX, ingY, slotSize * 0.8f, slotSize * 0.8f};

        DrawRectangleRec(ingSlotRect, Fade(theme::ground, 0.7f));
        DrawRectangleLinesEx(ingSlotRect, 2.0f * scale, theme::border);

        Rectangle ingDestRect = {ingX + padding * 0.8f, ingY + padding * 0.8f,
                                 (slotSize - 2 * padding) * 0.8f,
                                 (slotSize - 2 * padding) * 0.8f};
        itemRenderer.renderItemUI(ing.type, ingDestRect, WHITE);

        int playerHas = 0;
        for (const auto &slot : playerInv)
          if (slot.type == ing.type)
            playerHas += slot.count;

        bool hasEnough = playerHas >= ing.count;
        Color textColor = hasEnough ? theme::good : theme::bad;

        // handleInventoryInput sets the flash deadline when a craft is
        // refused; this only reads the clock against it.
        if (!hasEnough && m_simTime < m_craftFlashEndTime) {
          if ((int)(m_simTime * 15) % 2 == 0) {
            textColor = theme::ink;
          }
        }

        DrawText(TextFormat("%d/%d", playerHas, ing.count),
                 ingX + slotSize * 0.8f + 5 * scale, ingY + slotSize * 0.3f,
                 20 * scale, textColor);

        if (CheckCollisionPointRec(in.mouse, ingSlotRect)) {
          hoveredItemType = ing.type;
          hoveredIsCrafting = true;
        }

        ingX += slotSize * 0.8f + 60 * scale;
      }

      // Craft button.
      Rectangle btnRect = layout.craftButton();
      bool canCraft = player.canCraft(recipe);
      DrawRectangleRec(btnRect, canCraft ? Fade(theme::good, 0.6f) : Fade(theme::bad, 0.6f));
      DrawRectangleLinesEx(btnRect, 2.0f * scale, WHITE);

      int textW = MeasureText("Craft", 20 * scale);
      DrawText("Craft", btnRect.x + (btnRect.width - textW) / 2,
               btnRect.y + (btnRect.height - 20 * scale) / 2, 20 * scale,
               WHITE);

      if (CheckCollisionPointRec(in.mouse, btnRect)) {
        DrawRectangleLinesEx(btnRect, 3.0f * scale, theme::highlight);
      }
    }
  }

  // 5. The stack riding the cursor.
  if (m_heldSlotIndex != -1) {
    Vector2 mousePos = in.mouse;
    const auto &heldInv = m_heldFromCupboard ? *cupboardInv : playerInv;
    const InventorySlot &slot = heldInv[m_heldSlotIndex];

    Rectangle destRect = {mousePos.x - slotSize / 2 + padding,
                          mousePos.y - slotSize / 2 + padding,
                          slotSize - 2 * padding, slotSize - 2 * padding};
    itemRenderer.renderItemUI(slot.type, destRect, Fade(WHITE, 0.8f));

    if (slot.count > 1) {
      DrawText(TextFormat("%d", slot.count),
               mousePos.x + slotSize / 2 - 20 * scale,
               mousePos.y + slotSize / 2 - 20 * scale, 20 * scale, WHITE);
    }
  }

  // 6. Tooltip for whatever the cursor is over.
  if (hoveredItemType != ItemType::NONE && m_heldSlotIndex == -1) {
    const auto &def = ItemDatabase::getDef(hoveredItemType);
    std::string desc = def.description;

    // Simple line break measurement estimation
    int maxDescW = 0;
    int lines = 1;
    std::string currentLineForMeasure;
    for (char c : desc) {
      if (c == '\n') {
        int w = MeasureText(currentLineForMeasure.c_str(), 15 * scale);
        if (w > maxDescW)
          maxDescW = w;
        currentLineForMeasure = "";
        lines++;
      } else {
        currentLineForMeasure += c;
      }
    }
    if (!currentLineForMeasure.empty()) {
      int w = MeasureText(currentLineForMeasure.c_str(), 15 * scale);
      if (w > maxDescW)
        maxDescW = w;
    }

    int nameW = MeasureText(def.name.c_str(), 20 * scale);
    int boxW = std::max(nameW, maxDescW) + 40 * scale;
    int boxH = 40 * scale + (lines * 20 * scale);

    Vector2 mousePos = in.mouse;
    DrawRectangle(mousePos.x + 10 * scale, mousePos.y + 10 * scale, boxW, boxH,
                  Fade(theme::ground, 0.9f));
    DrawRectangleLines(mousePos.x + 10 * scale, mousePos.y + 10 * scale, boxW,
                       boxH, theme::border);
    DrawText(def.name.c_str(), mousePos.x + 20 * scale, mousePos.y + 15 * scale,
             20 * scale, theme::highlight);

    int lineY = mousePos.y + 40 * scale;
    std::string currentLine;
    for (char c : desc) {
      if (c == '\n') {
        DrawText(currentLine.c_str(), mousePos.x + 20 * scale, lineY,
                 15 * scale, theme::inkDim);
        currentLine = "";
        lineY += 20 * scale;
      } else {
        currentLine += c;
      }
    }
    if (!currentLine.empty()) {
      DrawText(currentLine.c_str(), mousePos.x + 20 * scale, lineY, 15 * scale,
               theme::inkDim);
    }
  }
}

void UIManager::generateMagicBookMap(Maze &maze) {
  const int startX = kBookMapX;
  const int startY = kBookMapY;
  const int width = kBookMapW;
  const int height = kBookMapH;

  BeginTextureMode(m_magicBookMapTexture);
  ClearBackground(BLANK);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int gridX = maze.wrapX(startX + x);
      int gridY = maze.wrapY(startY + y);

      if (maze.getCell(gridX, gridY) == Maze::CELL_WALL) {
        DrawPixel(x, y, theme::mapWall);
      } else {
        if (maze.isShiftingZone(gridX, gridY)) {
          DrawPixel(x, y, theme::mapZone);
        } else {
          DrawPixel(x, y, theme::groundCool);
        }
      }
    }
  }
  EndTextureMode();
}

void UIManager::markMapDrawn(int mapId, Maze &maze, int centerX, int centerY) {
  if (m_drawnMaps.find(mapId) != m_drawnMaps.end())
    return;

  RenderTexture2D tex = LoadRenderTexture(kDrawnMapW, kDrawnMapH);
  BeginTextureMode(tex);
  ClearBackground(theme::groundCool); // default off-map color

  int startX = kDrawnMapX(centerX);
  int startY = kDrawnMapY(centerY);

  for (int y = 0; y < kDrawnMapH; ++y) {
    for (int x = 0; x < kDrawnMapW; ++x) {
      int gridX = maze.wrapX(startX + x);
      int gridY = maze.wrapY(startY + y);

      if (maze.getCell(gridX, gridY) == Maze::CELL_WALL) {
        DrawPixel(x, y, theme::mapWall);
      } else {
        DrawPixel(x, y, theme::groundCool);
      }
    }
  }
  EndTextureMode();

  m_drawnMaps[mapId] = {mapId, centerX, centerY, tex};
}
