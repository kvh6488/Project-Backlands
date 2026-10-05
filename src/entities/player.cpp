#include "entities/player.hpp"
#include "items/item_database.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// Constructor
// ============================================================================
Player::Player(Vector2 startPosition, AreaState startState)
    : m_position(startPosition), m_speed(130.0f), m_radius(10.0f),
      m_areaState(startState), m_facingDirection(FacingDirection::DOWN),
      m_isMoving(false) {}

// ============================================================================
// Update - Kinematics and Input
// ============================================================================
void Player::update(World &world, float dt, const InputState &in,
                    bool canMove) {
  // 1. FRAMERATE INDEPENDENCE (Delta Time)
  // dt is the seconds elapsed since the last frame (e.g. 0.016s at 60 FPS).
  // Multiplying speed by it means we move a fixed number of pixels per real
  // second regardless of how fast the machine runs. It is passed in rather
  // than read from raylib here, so the frame loop owns the clock.

  if (m_isPassingOut) {
    float prevTimer = m_passOutTimer;
    m_passOutTimer -= dt;

    if (prevTimer > 5.0f && m_passOutTimer <= 5.0f) {
      m_eventPassOutComplete = true;
      m_kickInTimers.clear();
      m_tripDurationRemaining = 0.0f;
      m_mushroomsEatenThisTrip = 0;
      m_mushroomsKickedInThisTrip = 0;
      m_isFadingIn = false;
    }

    if (m_passOutTimer <= 0.0f) {
      m_isPassingOut = false;
    }
  } else {
    // Process kick in timers
    for (int i = 0; i < (int)m_kickInTimers.size(); i++) {
      m_kickInTimers[i] -= dt;
      if (m_kickInTimers[i] <= 0.0f) {
        m_tripDurationRemaining += 90.0f;
        m_mushroomsKickedInThisTrip++;

        if (m_mushroomsKickedInThisTrip == 1) {
            m_eventMushroomFullTripStarted = true;
        }

        // Remove from list
        m_kickInTimers.erase(m_kickInTimers.begin() + i);
        i--;

        if (m_mushroomsKickedInThisTrip >= 5) {
          m_isPassingOut = true;
          m_passOutTimer = 10.0f; // 10 second blackout
          break;                  // Stop processing further
        }
      }
    }

    // Process trip duration
    if (!m_isPassingOut && m_tripDurationRemaining > 0.0f) {
      m_tripDurationRemaining -= dt;
      if (m_tripDurationRemaining <= 0.0f) {
        m_tripDurationRemaining = 0.0f;
        m_eventMushroomOver = true;
        m_mushroomsEatenThisTrip = 0;
        m_mushroomsKickedInThisTrip = 0;
        m_isFadingIn = false;
      }
    }

    // Check for fade in trigger
    if (!m_isPassingOut && !m_isFadingIn && m_tripDurationRemaining <= 0.0f &&
        !m_kickInTimers.empty()) {
      float minTimer = 999.0f;
      for (float t : m_kickInTimers)
        if (t < minTimer)
          minTimer = t;

      if (minTimer <= 20.0f) {
        m_isFadingIn = true;
        m_eventMushroomWeird = true;
      }
    }
  }

  // Create a velocity vector to store how much we WANT to move this frame.
  Vector2 velocity = {0.0f, 0.0f};

  // WASD and Arrow Key Input
  if (canMove) {
    if (in.moveUp)
      velocity.y -= m_speed;
    if (in.moveDown)
      velocity.y += m_speed;
    if (in.moveLeft)
      velocity.x -= m_speed;
    if (in.moveRight)
      velocity.x += m_speed;
  }

  // Normalize velocity if moving diagonally to prevent speed boost
  if (velocity.x != 0.0f && velocity.y != 0.0f) {
    float length = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    velocity.x = (velocity.x / length) * m_speed;
    velocity.y = (velocity.y / length) * m_speed;
  }

  // FACING DIRECTION (Velocity-based)
  // The player faces the direction they are moving the most.
  if (std::abs(velocity.x) > std::abs(velocity.y)) {
    m_facingDirection =
        (velocity.x > 0.0f) ? FacingDirection::RIGHT : FacingDirection::LEFT;
  } else if (std::abs(velocity.y) > std::abs(velocity.x)) {
    m_facingDirection =
        (velocity.y > 0.0f) ? FacingDirection::DOWN : FacingDirection::UP;
  } else if (velocity.x != 0.0f || velocity.y != 0.0f) {
    // Exact diagonal movement. Keep the current facing direction if valid,
    // otherwise default to the X-axis direction.
    bool validDirection = false;
    if (m_facingDirection == FacingDirection::RIGHT && velocity.x > 0.0f)
      validDirection = true;
    if (m_facingDirection == FacingDirection::LEFT && velocity.x < 0.0f)
      validDirection = true;
    if (m_facingDirection == FacingDirection::DOWN && velocity.y > 0.0f)
      validDirection = true;
    if (m_facingDirection == FacingDirection::UP && velocity.y < 0.0f)
      validDirection = true;

    if (!validDirection) {
      m_facingDirection =
          (velocity.x > 0.0f) ? FacingDirection::RIGHT : FacingDirection::LEFT;
    }
  }

  // Track whether the player is moving this frame (used by PlayerRenderer
  // to decide whether to animate or show idle frame).
  m_isMoving = (velocity.x != 0.0f || velocity.y != 0.0f);

  // 1.5 DOOR TRANSITIONS (KEY_K and KEY_L)
  int doorIndexToEnter = -1;
  if (canMove) {
    if (in.door1)
      doorIndexToEnter = 0;
    if (in.door2)
      doorIndexToEnter = 1;

    // INVENTORY PICKUP
    if (in.pickup) {
      pickupItem(world);
    }
  }

  // Doors exist only in the maze.
  Maze *maze = world.asMaze();
  if (doorIndexToEnter != -1 && maze) {
    int gridX = maze->toGridX(m_position.x);
    int gridY = maze->toGridY(m_position.y);

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    int validDoorCount = 0;

    for (int i = 0; i < 4; ++i) {
      int nx = gridX + dx[i];
      int ny = gridY + dy[i];
      int cell = maze->getCell(nx, ny);

      bool isDoor = false;
      if (m_areaState == AreaState::CORRIDOR && cell == Maze::CELL_ROOM) {
        isDoor = true;
      } else if (m_areaState == AreaState::ROOM &&
                 cell == Maze::CELL_CORRIDOR) {
        isDoor = true;
      }

      if (isDoor) {
        if (validDoorCount == doorIndexToEnter) {
          if (m_areaState == AreaState::CORRIDOR) {
            m_areaState = AreaState::ROOM;
          } else {
            m_areaState = AreaState::CORRIDOR;
          }
          // Snap player over the threshold
          m_position.x = nx * maze->getCellSize() + maze->getCellSize() / 2.0f;
          m_position.y = ny * maze->getCellSize() + maze->getCellSize() / 2.0f;
          break; // Only enter one door per frame
        }
        validDoorCount++;
      }
    }
  }

  // 2. THE "SLIDE" COLLISION METHOD
  // Instead of updating X and Y simultaneously and getting stuck on corners,
  // we step one axis at a time.

  // Step 1: Move on the X axis, then check if we hit a wall and resolve it.
  m_position.x += velocity.x * dt;
  resolveCollision(world);

  // Step 2: Move on the Y axis, then check if we hit a wall and resolve it.
  m_position.y += velocity.y * dt;
  resolveCollision(world);


}

// ============================================================================
// Collision Resolution
// ============================================================================
void Player::resolveCollision(const World &world) {
  // To avoid checking every wall in the giant maze (O(V)), we only check the
  // grid cells immediately surrounding the player's current bounding box.

  const Maze *maze = world.asMaze();
  int cellSize = world.getCellSize();

  // Calculate the player's bounding box in pixel coordinates
  float minX = m_position.x - m_radius;
  float maxX = m_position.x + m_radius;
  float minY = m_position.y - m_radius;
  float maxY = m_position.y + m_radius;

  // Convert pixel coordinates to Maze Grid Coordinates
  // We use floor() to safely handle negative values if the player goes out of
  // bounds.
  int startGridX = world.toGridX(minX);
  int endGridX = world.toGridX(maxX);
  int startGridY = world.toGridY(minY);
  int endGridY = world.toGridY(maxY);

  // Loop through these nearby grid cells
  for (int y = startGridY; y <= endGridY; ++y) {
    for (int x = startGridX; x <= endGridX; ++x) {

      // Walls, props and placed items, plus - in the maze - whichever side
      // of the room/corridor seal the player is not on.
      bool isSolid = world.isSolid(x, y) ||
                     (maze && maze->isSealedFrom(x, y, m_areaState));

      if (isSolid) {

        // Define the AABB (Axis-Aligned Bounding Box) for this specific wall
        // cell
        Rectangle wallRect = {
            static_cast<float>(x * cellSize), static_cast<float>(y * cellSize),
            static_cast<float>(cellSize), static_cast<float>(cellSize)};

        // Dynamically shrink the bounding box for cupboards so the player can
        // walk closer to the front
        if (maze && maze->getItem(x, y) == ItemType::CUPBOARD) {
          bool wallAbove = maze->getCell(x, y - 1) == Maze::CELL_WALL;
          bool wallRight = maze->getCell(x + 1, y) == Maze::CELL_WALL;
          bool wallLeft = maze->getCell(x - 1, y) == Maze::CELL_WALL;

          float shrinkAmount = 14.0f;
          if (wallAbove) {
            // Facing down (front is bottom edge) -> shrink height
            wallRect.height -= shrinkAmount;
          } else if (wallRight) {
            // Facing left (front is left edge) -> shift right, shrink width
            wallRect.x += shrinkAmount;
            wallRect.width -= shrinkAmount;
          } else if (wallLeft) {
            // Facing right (front is right edge) -> shrink width
            wallRect.width -= shrinkAmount;
          }
        }

        // 3. CIRCLE-TO-AABB MATH (Radial Push)
        // Find the closest point on the wall's rectangle to the player's center
        float closestX =
            std::clamp(m_position.x, wallRect.x, wallRect.x + wallRect.width);
        float closestY =
            std::clamp(m_position.y, wallRect.y, wallRect.y + wallRect.height);

        // Calculate the distance vector from the closest point to the player's
        // center
        float dx = m_position.x - closestX;
        float dy = m_position.y - closestY;

        // Use squared distance to avoid expensive sqrt() unless necessary
        float distanceSq = dx * dx + dy * dy;

        // If the squared distance is less than the squared radius, we have a
        // collision!
        if (distanceSq > 0.0f && distanceSq < m_radius * m_radius) {
          float distance = std::sqrt(distanceSq);
          float overlap = m_radius - distance;

          // Push the player out exactly along the penetration vector
          m_position.x += (dx / distance) * overlap;
          m_position.y += (dy / distance) * overlap;
        } else if (distanceSq == 0.0f) {
          // Extremely rare edge case: player center is perfectly inside the
          // wall. Just push them up out of it to avoid a divide-by-zero.
          m_position.y -= m_radius;
        }
      }
    }
  }
}

// ============================================================================
// Door Interaction Check
// ============================================================================
int Player::getAvailableDoors(const Maze &maze) const {
  int gridX = maze.toGridX(m_position.x);
  int gridY = maze.toGridY(m_position.y);

  int dx[] = {0, 0, -1, 1};
  int dy[] = {-1, 1, 0, 0};

  int count = 0;
  for (int i = 0; i < 4; ++i) {
    int nx = gridX + dx[i];
    int ny = gridY + dy[i];
    int cell = maze.getCell(nx, ny);

    if (m_areaState == AreaState::CORRIDOR && cell == Maze::CELL_ROOM)
      count++;
    else if (m_areaState == AreaState::ROOM && cell == Maze::CELL_CORRIDOR)
      count++;
  }
  return count;
}

// ============================================================================
// INVENTORY SYSTEM
// ============================================================================

void Player::pickupItem(World &world) {
  int px = world.toGridX(m_position.x);
  int py = world.toGridY(m_position.y);
  Maze *maze = world.asMaze();

  ItemType typeToPickup = ItemType::NONE;
  int targetX = px;
  int targetY = py;

  // Search a 3x3 area around the player for a pickupable item
  for (int y = py - 1; y <= py + 1 && typeToPickup == ItemType::NONE; ++y) {
    for (int x = px - 1; x <= px + 1 && typeToPickup == ItemType::NONE; ++x) {
      ItemType type = world.getItem(x, y);
      if (type != ItemType::NONE && ItemDatabase::getDef(type).isPickable) {
        typeToPickup = type;
        targetX = x;
        targetY = y;

        if (type == ItemType::MAGIC_MUSHROOM && !m_hasPickedUpMushroomEver) {
          m_hasPickedUpMushroomEver = true;
          m_eventMushroomFirstPickup = true;
        }
      }
    }
  }

  // Check for Magic Book of Maps in front of player
  if (typeToPickup == ItemType::NONE && maze && maze->isMagicBookSpawned()) {
    int faceX = px, faceY = py;
    if (m_facingDirection == FacingDirection::UP) faceY--;
    else if (m_facingDirection == FacingDirection::DOWN) faceY++;
    else if (m_facingDirection == FacingDirection::LEFT) faceX--;
    else if (m_facingDirection == FacingDirection::RIGHT) faceX++;
    
    int bookX = maze->getMagicBookX();
    int bookY = maze->getMagicBookY();
    
    // The table occupies (bookX, bookY) and another adjacent tile.
    // If state == 1 (Horizontal Right), the table is at (bookX-1, bookY) and (bookX, bookY)
    // If state == 3 (Vertical Bottom), the table is at (bookX, bookY-1) and (bookX, bookY)
    int state = maze->getItemState(bookX, bookY);
    bool facingBook = false;
    
    if (faceX == bookX && faceY == bookY) {
      facingBook = true;
    } else if (state == 1 && faceX == bookX - 1 && faceY == bookY) {
      facingBook = true;
    } else if (state == 3 && faceX == bookX && faceY == bookY - 1) {
      facingBook = true;
    }
    
    if (facingBook) {
      typeToPickup = ItemType::MAGIC_BOOK_OF_MAPS;
      targetX = -1; // Special flag so we don't clear from m_items
    }
  }

  if (typeToPickup == ItemType::NONE)
    return;

  int maxStack = ItemDatabase::getDef(typeToPickup).maxStackSize;
  // 1. Try to find an existing stack that isn't full
  for (int i = 0; i < INVENTORY_SLOTS; ++i) {
    if (m_inventory[i].type == typeToPickup && m_inventory[i].count < maxStack) {
      m_inventory[i].count++;
      if (targetX != -1) world.setItem(targetX, targetY, ItemType::NONE);
      if (typeToPickup == ItemType::MAGIC_BOOK_OF_MAPS) {
        maze->despawnMagicBook();
        m_hasPickedUpMagicBook = true;
      }
      return;
    }
  }

  // 2. Try to find an empty slot
  for (int i = 0; i < INVENTORY_SLOTS; ++i) {
    if (m_inventory[i].type == ItemType::NONE) {
      m_inventory[i].type = typeToPickup;
      m_inventory[i].count = 1;
      if (targetX != -1) world.setItem(targetX, targetY, ItemType::NONE);
      if (typeToPickup == ItemType::MAGIC_BOOK_OF_MAPS) {
        maze->despawnMagicBook();
        m_hasPickedUpMagicBook = true;
      }
      return;
    }
  }
}

void Player::dropItem(World &world, int slotIndex) {
  if (slotIndex < 0 || slotIndex >= INVENTORY_SLOTS)
    return;
  if (m_inventory[slotIndex].type == ItemType::NONE)
    return;

  int gridX = world.toGridX(m_position.x);
  int gridY = world.toGridY(m_position.y);

  int outX, outY;
  // Try to find nearest empty cell up to radius 2
  if (world.findNearestEmptyItemCell(gridX, gridY, 2, outX, outY)) {
    world.setItem(outX, outY, m_inventory[slotIndex].type);

    m_inventory[slotIndex].count--;
    if (m_inventory[slotIndex].count <= 0) {
      m_inventory[slotIndex].type = ItemType::NONE;
      m_inventory[slotIndex].count = 0;
    }
  }
}

void Player::consumeItem(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= INVENTORY_SLOTS)
    return;
  if (m_inventory[slotIndex].type == ItemType::NONE)
    return;

  ItemType type = m_inventory[slotIndex].type;

  if (type == ItemType::MAGIC_MUSHROOM) {
    m_kickInTimers.push_back(40.0f);
    m_mushroomsEatenThisTrip++;
    m_eventMushroomConsumed = true;

    if (m_mushroomsEatenThisTrip == 3) {
      m_eventMushroomThree = true;
    }

    m_inventory[slotIndex].count--;
    if (m_inventory[slotIndex].count <= 0) {
      m_inventory[slotIndex].type = ItemType::NONE;
      m_inventory[slotIndex].count = 0;
    }
  } else if (type == ItemType::MAP) {
    if (m_inventory[slotIndex].instanceId == 0) {
      // It's a new map, draw it instantly
      m_inventory[slotIndex].instanceId = m_nextMapInstanceId++;
      m_lastConsumedMapId = m_inventory[slotIndex].instanceId;
      m_eventMapCrafted = true; // Use same event to trigger instant draw
    } else {
      // It's already drawn, open it
      m_lastConsumedMapId = m_inventory[slotIndex].instanceId;
      m_eventMapOpened = true;
    }
  } else if (type == ItemType::MAGIC_BOOK_OF_MAPS) {
    m_eventMagicBookOpened = true;
  }
}

void Player::destroyItem(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= INVENTORY_SLOTS)
    return;
  if (m_inventory[slotIndex].type == ItemType::NONE)
    return;

  m_inventory[slotIndex].count--;
  if (m_inventory[slotIndex].count <= 0) {
    m_inventory[slotIndex].type = ItemType::NONE;
    m_inventory[slotIndex].count = 0;
  }
}

void Player::swapSlots(int slotIndex1, int slotIndex2) {
  if (slotIndex1 < 0 || slotIndex1 >= INVENTORY_SLOTS ||
      slotIndex2 < 0 || slotIndex2 >= INVENTORY_SLOTS)
    return;

  // If same type, try to merge stacks
  if (m_inventory[slotIndex1].type != ItemType::NONE &&
      m_inventory[slotIndex1].type == m_inventory[slotIndex2].type) {

    int maxStack = ItemDatabase::getDef(m_inventory[slotIndex2].type).maxStackSize;
    int spaceInSlot2 = maxStack - m_inventory[slotIndex2].count;
    
    if (spaceInSlot2 > 0) {
        int amountToMove = std::min(m_inventory[slotIndex1].count, spaceInSlot2);

        m_inventory[slotIndex2].count += amountToMove;
        m_inventory[slotIndex1].count -= amountToMove;

        if (m_inventory[slotIndex1].count <= 0) {
          m_inventory[slotIndex1].type = ItemType::NONE;
          m_inventory[slotIndex1].count = 0;
        }
    }
  } else {
    // Standard swap
    std::swap(m_inventory[slotIndex1], m_inventory[slotIndex2]);
  }
}

// ============================================================================
// debugForceTrip / debugEndTrip - Developer overrides for the trip state
// ============================================================================
// Jumps straight to the "full trip" branch of getMushroomEffectStrength() by
// clearing the pending kick-in timers and setting the duration directly.
// Raising m_eventMushroomFullTripStarted means the real downstream chain still
// fires (including the 1-in-3 magic book roll), so this exercises the genuine
// code path rather than a parallel one.
// ============================================================================
void Player::debugForceTrip(float seconds) {
  m_kickInTimers.clear();
  m_isPassingOut = false;
  m_passOutTimer = 0.0f;
  m_isFadingIn = false;
  m_tripDurationRemaining = seconds;
  m_eventMushroomFullTripStarted = true;
}

void Player::debugEndTrip() {
  m_kickInTimers.clear();
  m_tripDurationRemaining = 0.0f;
  m_isPassingOut = false;
  m_passOutTimer = 0.0f;
}

float Player::getMushroomEffectStrength() const {
  if (m_isPassingOut) {
    if (m_passOutTimer > 5.0f)
      return 1.0f;
    else
      return 0.0f;
  }

  if (m_tripDurationRemaining > 0.0f) {
    if (m_tripDurationRemaining < 20.0f && m_kickInTimers.empty()) {
      return m_tripDurationRemaining / 20.0f; // Fade out
    }
    return 1.0f; // Full trip
  }

  if (!m_kickInTimers.empty()) {
    float minTimer = 999.0f;
    for (float t : m_kickInTimers)
      if (t < minTimer)
        minTimer = t;

    if (minTimer <= 20.0f) {
      return 1.0f - (minTimer / 20.0f); // Fade in
    }
  }

  return 0.0f;
}

void Player::teleport(Vector2 newPos, AreaState newState) {
  m_position = newPos;
  m_areaState = newState;
}

// ============================================================================
// Crafting System Methods
// ============================================================================

bool Player::hasUnlockedRecipe(ItemType type) const {
    for (auto rt : m_unlockedRecipes) {
        if (rt == type) return true;
    }
    return false;
}

void Player::unlockRecipe(ItemType type) {
    if (!hasUnlockedRecipe(type)) {
        m_unlockedRecipes.push_back(type);
    }
}

bool Player::hasIngredient(ItemType type, int count) const {
    int totalFound = 0;
    for (const auto& slot : m_inventory) {
        if (slot.type == type) {
            totalFound += slot.count;
            if (totalFound >= count) return true;
        }
    }
    return false;
}

bool Player::canCraft(const Recipe& recipe) const {
    for (const auto& req : recipe.ingredients) {
        if (!hasIngredient(req.type, req.count)) {
            return false;
        }
    }
    
    // Check if there's an empty slot in inventory.
    // If not, the craft should fail.
    bool hasSpace = false;
    for (const auto& slot : m_inventory) {
        if (slot.type == ItemType::NONE) {
            hasSpace = true;
            break;
        }
    }
    if (!hasSpace) {
        // Also check if consuming ingredients frees up an entire slot.
        int freedSlots = 0;
        for (const auto& req : recipe.ingredients) {
            int needed = req.count;
            for (const auto& slot : m_inventory) {
                if (slot.type == req.type && slot.count == needed) {
                    freedSlots++;
                    break;
                }
            }
        }
        if (freedSlots == 0) return false;
    }

    return true;
}

bool Player::craftItem(const Recipe& recipe) {
    if (!canCraft(recipe)) return false;

    // Consume ingredients
    for (const auto& req : recipe.ingredients) {
        int remainingToConsume = req.count;
        for (auto& slot : m_inventory) {
            if (slot.type == req.type) {
                if (slot.count >= remainingToConsume) {
                    slot.count -= remainingToConsume;
                    if (slot.count == 0) slot.type = ItemType::NONE;
                    remainingToConsume = 0;
                    break;
                } else {
                    remainingToConsume -= slot.count;
                    slot.count = 0;
                    slot.type = ItemType::NONE;
                }
            }
        }
    }

    // Give result
    for (auto& slot : m_inventory) {
        if (slot.type == ItemType::NONE) {
            slot.type = recipe.result;
            slot.count = 1;
            if (recipe.result == ItemType::MAP) {
                slot.instanceId = m_nextMapInstanceId++;
                m_lastConsumedMapId = slot.instanceId;
                m_eventMapCrafted = true; // Use event to trigger instant draw
            }
            break;
        }
    }

    unlockRecipe(recipe.result);
    return true;
}
