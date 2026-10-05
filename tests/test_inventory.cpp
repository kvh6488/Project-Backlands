// ============================================================================
// test_inventory.cpp — Inventory, pickup/drop and crafting invariants
// ============================================================================
// Everything here is deterministic, window-free logic: Player's inventory
// methods touch only m_inventory and the Maze item layer, and Player::update
// takes its clock and its input as arguments, so a test can drive a whole
// tick with a hand-built InputState and no window.
//
// The reason this file exists: a miscounted stack or a swallowed ingredient
// looks like nothing on screen. There is no visual tell for "the craft ate two
// papers instead of one", so these rules can only be defended by assertion.
// ============================================================================

#include "entities/player.hpp"
#include "items/crafting_system.hpp"
#include "items/item_database.hpp"
#include "world/maze.hpp"

#include <gtest/gtest.h>

namespace {

class InventoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Normally done by the Application constructor. A test binary builds no
    // Application, so it must initialise the static registries itself.
    ItemDatabase::init();
    CraftingSystem::init();
  }

  static constexpr int CELL = 32;

  // A maze whose centre is an open room, big enough that the radius-2 drop
  // search can never wrap around the torus and land back on itself.
  static Maze makeRoomMaze() {
    Maze maze(20, 20, CELL, 12345);
    for (int y = 6; y < 14; ++y) {
      for (int x = 6; x < 14; ++x) {
        maze.setCell(x, y, Maze::CELL_ROOM);
      }
    }
    return maze;
  }

  // Player standing dead centre of tile (x, y).
  static Player playerAt(int x, int y) {
    return Player(Vector2{x * (float)CELL + CELL / 2.0f,
                          y * (float)CELL + CELL / 2.0f},
                  AreaState::ROOM);
  }

  static void fillSlot(Player &player, int slot, ItemType type, int count) {
    player.getInventoryRef()[slot].type = type;
    player.getInventoryRef()[slot].count = count;
  }

  // Number of slots holding a given type, summed by count.
  static int totalOf(const Player &player, ItemType type) {
    int total = 0;
    for (const auto &slot : player.getInventory()) {
      if (slot.type == type)
        total += slot.count;
    }
    return total;
  }
};

// --- 1. Pickup ----------------------------------------------------------

TEST_F(InventoryTest, PickupTakesItemFromGroundIntoAnEmptySlot) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  maze.setItem(10, 10, ItemType::MUSHROOM);

  player.pickupItem(maze);

  EXPECT_EQ(maze.getItem(10, 10), ItemType::NONE);
  EXPECT_EQ(totalOf(player, ItemType::MUSHROOM), 1);
}

TEST_F(InventoryTest, PickupReachesOneTileDiagonally) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  // The scan is a 3x3 box around the player, so a corner tile counts.
  maze.setItem(9, 9, ItemType::MUSHROOM);

  player.pickupItem(maze);

  EXPECT_EQ(maze.getItem(9, 9), ItemType::NONE);
  EXPECT_EQ(totalOf(player, ItemType::MUSHROOM), 1);
}

TEST_F(InventoryTest, PickupIgnoresUnpickableWorldFeatures) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  maze.setItem(10, 10, ItemType::CUPBOARD);

  player.pickupItem(maze);

  EXPECT_EQ(maze.getItem(10, 10), ItemType::CUPBOARD)
      << "a cupboard is world furniture, not loot";
  EXPECT_EQ(totalOf(player, ItemType::CUPBOARD), 0);
}

TEST_F(InventoryTest, PickupTopsUpAnExistingStackBeforeUsingAnEmptySlot) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  fillSlot(player, 3, ItemType::MUSHROOM, 2); // slots 0-2 left empty
  maze.setItem(10, 10, ItemType::MUSHROOM);

  player.pickupItem(maze);

  EXPECT_EQ(player.getInventory()[3].count, 3);
  EXPECT_EQ(player.getInventory()[0].type, ItemType::NONE)
      << "stacking must win over opening a new slot";
}

TEST_F(InventoryTest, PickupStartsANewStackWhenTheExistingOneIsFull) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  const int maxStack = ItemDatabase::getDef(ItemType::MUSHROOM).maxStackSize;
  fillSlot(player, 0, ItemType::MUSHROOM, maxStack);
  maze.setItem(10, 10, ItemType::MUSHROOM);

  player.pickupItem(maze);

  EXPECT_EQ(player.getInventory()[0].count, maxStack);
  EXPECT_EQ(player.getInventory()[1].type, ItemType::MUSHROOM);
  EXPECT_EQ(player.getInventory()[1].count, 1);
}

TEST_F(InventoryTest, PickupLeavesTheItemOnTheGroundWhenTheBagIsFull) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  // A pencil stacks to 1, so twenty pencils is a genuinely full inventory.
  for (int i = 0; i < 20; ++i)
    fillSlot(player, i, ItemType::PENCIL, 1);
  maze.setItem(10, 10, ItemType::MUSHROOM);

  player.pickupItem(maze);

  EXPECT_EQ(maze.getItem(10, 10), ItemType::MUSHROOM)
      << "a failed pickup must not delete the item";
  EXPECT_EQ(totalOf(player, ItemType::MUSHROOM), 0);
}

// The one-shot flag convention: MazeState drains each event exactly once
// per occurrence. A poll that stayed true would replay the popup every frame.
TEST_F(InventoryTest, FirstMagicMushroomPickupRaisesItsEventExactlyOnce) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  maze.setItem(10, 10, ItemType::MAGIC_MUSHROOM);

  player.pickupItem(maze);
  EXPECT_TRUE(player.pollEventMushroomFirstPickup());
  EXPECT_FALSE(player.pollEventMushroomFirstPickup());

  maze.setItem(10, 10, ItemType::MAGIC_MUSHROOM);
  player.pickupItem(maze);
  EXPECT_FALSE(player.pollEventMushroomFirstPickup())
      << "only the first magic mushroom of a run is a discovery";
}

// --- 2. Drop ------------------------------------------------------------

TEST_F(InventoryTest, DropPlacesTheItemOnTheFloorAndDecrementsTheStack) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  fillSlot(player, 0, ItemType::MUSHROOM, 2);

  player.dropItem(maze, 0);

  EXPECT_EQ(player.getInventory()[0].count, 1);
  EXPECT_EQ(maze.getItem(10, 10), ItemType::MUSHROOM)
      << "the player's own tile is empty, so it is the nearest candidate";
}

TEST_F(InventoryTest, DroppingTheLastOfAStackClearsTheSlot) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  fillSlot(player, 0, ItemType::MUSHROOM, 1);

  player.dropItem(maze, 0);

  EXPECT_EQ(player.getInventory()[0].type, ItemType::NONE);
  EXPECT_EQ(player.getInventory()[0].count, 0);
}

TEST_F(InventoryTest, DropKeepsTheItemWhenNoFloorCellIsFree) {
  // A one-tile room: the player's own cell is taken and everything within
  // the radius-2 search is solid wall, so the BFS finds nothing.
  Maze maze(20, 20, CELL, 12345);
  maze.setCell(10, 10, Maze::CELL_ROOM);
  maze.setItem(10, 10, ItemType::CUPBOARD);

  Player player = playerAt(10, 10);
  fillSlot(player, 0, ItemType::MUSHROOM, 1);

  player.dropItem(maze, 0);

  EXPECT_EQ(player.getInventory()[0].type, ItemType::MUSHROOM)
      << "a drop with nowhere to go must not destroy the item";
  EXPECT_EQ(player.getInventory()[0].count, 1);
  EXPECT_EQ(maze.getItem(10, 10), ItemType::CUPBOARD);
}

// --- 3. Slot manipulation ----------------------------------------------

TEST_F(InventoryTest, SwapSlotsMergesSameTypeUpToMaxStack) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  const int maxStack = ItemDatabase::getDef(ItemType::PAPER).maxStackSize;
  fillSlot(player, 0, ItemType::PAPER, maxStack);
  fillSlot(player, 1, ItemType::PAPER, maxStack - 1);

  player.swapSlots(0, 1);

  // Only what fits moves; the remainder stays behind rather than vanishing.
  EXPECT_EQ(player.getInventory()[1].count, maxStack);
  EXPECT_EQ(player.getInventory()[0].type, ItemType::PAPER);
  EXPECT_EQ(player.getInventory()[0].count, maxStack - 1);
}

TEST_F(InventoryTest, SwapSlotsExchangesDifferentTypes) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  fillSlot(player, 0, ItemType::PAPER, 2);
  fillSlot(player, 1, ItemType::PENCIL, 1);

  player.swapSlots(0, 1);

  EXPECT_EQ(player.getInventory()[0].type, ItemType::PENCIL);
  EXPECT_EQ(player.getInventory()[1].type, ItemType::PAPER);
  EXPECT_EQ(player.getInventory()[1].count, 2);
}

// --- 4. Crafting --------------------------------------------------------

TEST_F(InventoryTest, CraftConsumesExactlyTheIngredientsAndUnlocksTheRecipe) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  fillSlot(player, 0, ItemType::PENCIL, 1);
  fillSlot(player, 1, ItemType::PAPER, 3);

  const Recipe &mapRecipe = CraftingSystem::getRecipes()[0];
  ASSERT_EQ(mapRecipe.result, ItemType::MAP);
  ASSERT_TRUE(player.canCraft(mapRecipe));
  EXPECT_TRUE(player.craftItem(mapRecipe));

  EXPECT_EQ(totalOf(player, ItemType::PENCIL), 0);
  EXPECT_EQ(totalOf(player, ItemType::PAPER), 2) << "one sheet, not the stack";
  EXPECT_EQ(totalOf(player, ItemType::MAP), 1);
  EXPECT_TRUE(player.hasUnlockedRecipe(ItemType::MAP));
}

TEST_F(InventoryTest, EachCraftedMapGetsItsOwnInstanceId) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  const Recipe &mapRecipe = CraftingSystem::getRecipes()[0];

  fillSlot(player, 0, ItemType::PENCIL, 1);
  fillSlot(player, 1, ItemType::PAPER, 1);
  ASSERT_TRUE(player.craftItem(mapRecipe));
  int firstId = player.getLastConsumedMapId();

  fillSlot(player, 5, ItemType::PENCIL, 1);
  fillSlot(player, 6, ItemType::PAPER, 1);
  ASSERT_TRUE(player.craftItem(mapRecipe));
  int secondId = player.getLastConsumedMapId();

  // UIManager caches one drawn map texture per instance id, so a collision
  // would make the second map show the first map's snapshot.
  EXPECT_NE(firstId, 0);
  EXPECT_NE(firstId, secondId);
}

TEST_F(InventoryTest, CraftFailsWhenAnIngredientIsMissing) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  fillSlot(player, 0, ItemType::PAPER, 4); // no pencil

  const Recipe &mapRecipe = CraftingSystem::getRecipes()[0];
  EXPECT_FALSE(player.canCraft(mapRecipe));
  EXPECT_FALSE(player.craftItem(mapRecipe));
  EXPECT_EQ(totalOf(player, ItemType::PAPER), 4)
      << "a rejected craft must not consume anything";
}

// The subtle branch in Player::canCraft: a full bag is still craftable when
// consuming an ingredient empties a slot the result can move into.
TEST_F(InventoryTest, CraftSucceedsOnAFullBagWhenAnIngredientSlotIsFreed) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  const int mushroomMax = ItemDatabase::getDef(ItemType::MUSHROOM).maxStackSize;

  fillSlot(player, 0, ItemType::PENCIL, 1);
  fillSlot(player, 1, ItemType::PAPER, 1);
  for (int i = 2; i < 20; ++i)
    fillSlot(player, i, ItemType::MUSHROOM, mushroomMax);

  const Recipe &mapRecipe = CraftingSystem::getRecipes()[0];
  EXPECT_TRUE(player.canCraft(mapRecipe));
  EXPECT_TRUE(player.craftItem(mapRecipe));
  EXPECT_EQ(totalOf(player, ItemType::MAP), 1);
}

TEST_F(InventoryTest, CraftFailsOnAFullBagWhenNoIngredientSlotWouldEmpty) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  const int mushroomMax = ItemDatabase::getDef(ItemType::MUSHROOM).maxStackSize;
  for (int i = 0; i < 20; ++i)
    fillSlot(player, i, ItemType::MUSHROOM, mushroomMax);

  // A hand-built recipe, because no shipping recipe can reach this branch:
  // the map needs a pencil, and a pencil stack is always exactly 1, so it
  // always frees its slot. Taking one mushroom from a stack of six frees
  // nothing, leaving the result nowhere to land.
  Recipe dryingRack;
  dryingRack.result = ItemType::PAPER;
  dryingRack.ingredients.push_back({ItemType::MUSHROOM, 1});

  EXPECT_FALSE(player.canCraft(dryingRack));
  EXPECT_FALSE(player.craftItem(dryingRack));
  EXPECT_EQ(totalOf(player, ItemType::MUSHROOM), 20 * mushroomMax)
      << "the craft was rejected, so no mushroom should have been eaten";
}

} // namespace

// --- 6. Driving a tick through Player::update -----------------------------
// The point of InputState: "hold W for a second" is a value, not a keyboard.
// Both tests pin the fixed-tick contract - 60 ticks of 1/60 s is exactly one
// second of game time however fast the loop actually ran.

TEST_F(InventoryTest, HeldMoveUpAdvancesExactlySpeedTimesSeconds) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  const Vector2 start = player.getPosition();

  InputState in;
  in.moveUp = true;
  const float dt = 1.0f / 60.0f;
  for (int tick = 0; tick < 60; ++tick) {
    player.update(maze, dt, in);
  }

  // 130 px/s (Player::m_speed) for 1 s, with float accumulation slack.
  EXPECT_NEAR(player.getPosition().y, start.y - 130.0f, 0.5f);
  EXPECT_FLOAT_EQ(player.getPosition().x, start.x);
  EXPECT_EQ(player.getFacingDirection(), FacingDirection::UP);
}

TEST_F(InventoryTest, PressedPickupThroughUpdateTakesTheItem) {
  Maze maze = makeRoomMaze();
  Player player = playerAt(10, 10);
  maze.setItem(10, 10, ItemType::MUSHROOM);

  InputState idle;
  player.update(maze, 1.0f / 60.0f, idle);
  EXPECT_EQ(maze.getItem(10, 10), ItemType::MUSHROOM)
      << "nothing pressed, nothing picked up";

  InputState press;
  press.pickup = true;
  player.update(maze, 1.0f / 60.0f, press);
  EXPECT_EQ(maze.getItem(10, 10), ItemType::NONE);
  EXPECT_EQ(totalOf(player, ItemType::MUSHROOM), 1);

  // canMove=false is the inventory-open gate: keys still arrive, but the
  // player must neither move nor interact.
  maze.setItem(10, 10, ItemType::MUSHROOM);
  press.moveLeft = true;
  const Vector2 before = player.getPosition();
  player.update(maze, 1.0f / 60.0f, press, /*canMove=*/false);
  EXPECT_EQ(maze.getItem(10, 10), ItemType::MUSHROOM);
  EXPECT_FLOAT_EQ(player.getPosition().x, before.x);
}
