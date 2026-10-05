#pragma once

#include "core/input_state.hpp"
#include "world/maze.hpp"
#include "world/world.hpp"
#include "raylib.h"
#include "items/crafting_system.hpp"
#include <array>
#include <vector>

// ============================================================================
// FacingDirection Enum
// ============================================================================
// Encodes the four cardinal directions the player can face.
// The integer values (0–3) map directly to spritesheet rows, so the renderer
// can use them as a direct index: sourceY = facingDirection * tileHeight.
// ============================================================================
enum class FacingDirection {
  DOWN  = 0,  // Row 0 of spritesheet
  UP    = 1,  // Row 1
  LEFT  = 2,  // Row 2
  RIGHT = 3   // Row 3
};

// ============================================================================
// Player Class
// ============================================================================
// Handles the player's position, movement (kinematics), and collision detection
// against whichever World it stands in. Maze-only rules (doors, the
// room/corridor seal, cupboard hitboxes, the magic book) run only when
// world.asMaze() is non-null; on the surface they do not exist.
// ============================================================================



class Player {
public:
  // --- Constructor ---
  // Initializes the player at a starting position with a defined size and
  // speed.
  Player(Vector2 startPosition, AreaState startState = AreaState::CORRIDOR);

  // --- Core Methods ---

  // Updates player logic every tick (Input -> Kinematics -> Collision).
  // Both dt and the input come from the caller rather than raylib, so there
  // is one authoritative clock and one input source per tick - which is what
  // lets a test drive this with a hand-built InputState.
  void update(World &world, float dt, const InputState &in, bool canMove = true);

  // Getters
  Vector2 getPosition() const { return m_position; }
  AreaState getAreaState() const { return m_areaState; }
  FacingDirection getFacingDirection() const { return m_facingDirection; }
  bool isMoving() const { return m_isMoving; }


  // Get the number of available doors near the player
  int getAvailableDoors(const Maze& maze) const;

  // --- Inventory System ---
  void pickupItem(World &world);
  void dropItem(World &world, int slotIndex);
  void consumeItem(int slotIndex);
  void destroyItem(int slotIndex);
  void swapSlots(int slotIndex1, int slotIndex2);
  const std::array<InventorySlot, INVENTORY_SLOTS>& getInventory() const { return m_inventory; }
  std::array<InventorySlot, INVENTORY_SLOTS>& getInventoryRef() { return m_inventory; }
  float getMushroomEffectStrength() const;

  // --- Debug Overrides ---
  // Drive the trip state machine directly so the visual effects that only
  // exist at full trip strength (the shader, the magic book) can be inspected
  // without eating mushrooms and waiting out the 40s kick-in timer.
  void debugForceTrip(float seconds);
  void debugEndTrip();

  // --- Crafting System ---
  bool hasUnlockedRecipe(ItemType type) const;
  void unlockRecipe(ItemType type);
  bool hasIngredient(ItemType type, int count = 1) const;
  bool canCraft(const Recipe& recipe) const;
  bool craftItem(const Recipe& recipe);

  // --- Mushroom Event Polling ---
  bool pollEventMushroomConsumed() { bool v = m_eventMushroomConsumed; m_eventMushroomConsumed = false; return v; }
  bool pollEventMushroomThree() { bool v = m_eventMushroomThree; m_eventMushroomThree = false; return v; }
  bool pollEventMushroomWeird() { bool v = m_eventMushroomWeird; m_eventMushroomWeird = false; return v; }
  bool pollEventMushroomFullTripStarted() { bool v = m_eventMushroomFullTripStarted; m_eventMushroomFullTripStarted = false; return v; }
  bool pollEventMushroomOver() { bool v = m_eventMushroomOver; m_eventMushroomOver = false; return v; }
  bool pollEventMushroomFirstPickup() { bool v = m_eventMushroomFirstPickup; m_eventMushroomFirstPickup = false; return v; }
  bool pollEventPassOutComplete() { bool v = m_eventPassOutComplete; m_eventPassOutComplete = false; return v; }

  // --- Map Event Polling ---
  bool pollEventMagicBookOpened() { bool v = m_eventMagicBookOpened; m_eventMagicBookOpened = false; return v; }
  bool pollEventMapCrafted() { bool v = m_eventMapCrafted; m_eventMapCrafted = false; return v; }
  bool pollEventMapOpened() { bool v = m_eventMapOpened; m_eventMapOpened = false; return v; }
  int getLastConsumedMapId() const { return m_lastConsumedMapId; }

  bool isPassingOut() const { return m_isPassingOut; }
  float getPassOutTimer() const { return m_passOutTimer; }
  bool hasPickedUpMagicBook() const { return m_hasPickedUpMagicBook; }

  void teleport(Vector2 newPos, AreaState newState);

private:
  std::array<InventorySlot, INVENTORY_SLOTS> m_inventory;
  std::vector<ItemType> m_unlockedRecipes;

  Vector2 m_position; // Center of the player in world space (pixels)
  float m_speed;      // Movement speed in pixels per second
  float m_radius;     // Size of the player for the Circle Hitbox

  AreaState m_areaState;
  FacingDirection m_facingDirection; // Which direction the player sprite faces
  bool m_isMoving;                   // True when velocity is non-zero this frame

  // --- Magic Mushroom Mechanics ---
  std::vector<float> m_kickInTimers;
  float m_tripDurationRemaining = 0.0f;
  int m_mushroomsEatenThisTrip = 0;
  int m_mushroomsKickedInThisTrip = 0;
  bool m_isFadingIn = false;
  bool m_isPassingOut = false;
  float m_passOutTimer = 0.0f;
  bool m_hasPickedUpMushroomEver = false;
  bool m_hasPickedUpMagicBook = false;


  // --- Events for Application to read ---
  bool m_eventMushroomConsumed = false;
  bool m_eventMushroomThree = false;
  bool m_eventMushroomWeird = false;
  bool m_eventMushroomFullTripStarted = false;
  bool m_eventMushroomOver = false;
  bool m_eventMushroomFirstPickup = false;
  bool m_eventPassOutComplete = false;

  bool m_eventMagicBookOpened = false;
  bool m_eventMapCrafted = false;
  bool m_eventMapOpened = false;
  int m_lastConsumedMapId = 0;
  int m_nextMapInstanceId = 1;


  // --- Internal Collision Helpers ---

  // Checks if the player's circle hitbox overlaps any solid cell in the world.
  // We use the "Slide" method and radial push to keep the player out of walls.
  void resolveCollision(const World &world);
};
