// Headless harness pieces that need no window: the scenario grammar, the
// tick timeline it compiles to, the JSON emitter, and the telemetry snapshot.
#include "dev/headless_mode.hpp"
#include "dev/json_writer.hpp"
#include "dev/scenario.hpp"
#include "dev/scripted_input.hpp"
#include "dev/debug_overlay.hpp"
#include "items/crafting_system.hpp"
#include "items/item_database.hpp"
#include "render/view_bounds.hpp"
#include "states/maze_state.hpp"
#include "ui/ui_manager.hpp"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Scenario grammar
// ---------------------------------------------------------------------------
static scenario::Scenario parseOk(const char *text) {
  scenario::Scenario sc;
  std::string err;
  EXPECT_TRUE(scenario::parseText(text, sc, err)) << err;
  return sc;
}

static std::string parseFail(const char *text) {
  scenario::Scenario sc;
  std::string err;
  EXPECT_FALSE(scenario::parseText(text, sc, err));
  return err;
}

TEST(ScenarioTest, ParsesHeaderAndCommands) {
  scenario::Scenario sc = parseOk("# comment\n"
                                  "seed 42\n"
                                  "window 640 360   # trailing comment\n"
                                  "scale 2\n"
                                  "\n"
                                  "wait 30\n"
                                  "hold w 45\n"
                                  "press P\n"
                                  "mouse 10 20\n"
                                  "click 640 360\n"
                                  "rclick 1 2\n"
                                  "checkpoint after_pickup\n"
                                  "end\n");
  EXPECT_EQ(sc.seed, 42u);
  EXPECT_EQ(sc.windowW, 640);
  EXPECT_EQ(sc.windowH, 360);
  EXPECT_FLOAT_EQ(sc.blitScale, 2.0f);
  ASSERT_EQ(sc.commands.size(), 8u);

  using K = scenario::Command::Kind;
  EXPECT_EQ(sc.commands[0].kind, K::Wait);
  EXPECT_EQ(sc.commands[0].ticks, 30);
  EXPECT_EQ(sc.commands[1].kind, K::Hold);
  EXPECT_EQ(sc.commands[1].key, "W"); // upper-cased
  EXPECT_EQ(sc.commands[1].ticks, 45);
  EXPECT_EQ(sc.commands[2].kind, K::Hold); // press == hold 1
  EXPECT_EQ(sc.commands[2].ticks, 1);
  EXPECT_EQ(sc.commands[3].kind, K::Mouse);
  EXPECT_EQ(sc.commands[4].kind, K::Click);
  EXPECT_EQ(sc.commands[4].x, 640);
  EXPECT_EQ(sc.commands[5].kind, K::RClick);
  EXPECT_EQ(sc.commands[6].kind, K::Checkpoint);
  EXPECT_EQ(sc.commands[6].name, "after_pickup");
  EXPECT_EQ(sc.commands[7].kind, K::End);
}

TEST(ScenarioTest, DefaultsWhenHeaderAbsent) {
  scenario::Scenario sc = parseOk("wait 1\n");
  EXPECT_EQ(sc.seed, 0u);
  EXPECT_EQ(sc.windowW, 1280);
  EXPECT_EQ(sc.windowH, 720);
  EXPECT_FLOAT_EQ(sc.blitScale, RenderSettings{}.blitScale);
}

TEST(ScenarioTest, RejectsBadInput) {
  EXPECT_NE(parseFail("hold F11 3\n").find("unknown key"), std::string::npos);
  EXPECT_NE(parseFail("wait 0\n").find("line 1"), std::string::npos);
  EXPECT_NE(parseFail("scale 1.2\n").find("scale"), std::string::npos);
  EXPECT_NE(parseFail("checkpoint a\ncheckpoint a\n").find("duplicate"),
            std::string::npos);
  EXPECT_NE(parseFail("checkpoint bad/name\n").find("checkpoint"),
            std::string::npos);
  EXPECT_NE(parseFail("end\nwait 1\n").find("after 'end'"),
            std::string::npos);
  EXPECT_NE(parseFail("jump 3\n").find("unknown command"),
            std::string::npos);
  EXPECT_NE(parseFail("seed 0\n").find("seed"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Timeline: one InputState per tick, hardware semantics
// ---------------------------------------------------------------------------
TEST(ScriptedInputTest, HeldAndPressedMirrorHardware) {
  ScriptedInput in(parseOk("hold W 3\nhold P 2\npress K\n"));
  EXPECT_EQ(in.tickCount(), 6);

  // Movement is a level: every tick of the hold.
  for (int t = 0; t < 3; ++t)
    EXPECT_TRUE(in.sample(t).moveUp) << t;
  EXPECT_FALSE(in.sample(3).moveUp);

  // Actions are an edge: first tick only, as IsKeyPressed reports.
  EXPECT_TRUE(in.sample(3).pickup);
  EXPECT_FALSE(in.sample(4).pickup);
  EXPECT_TRUE(in.sample(5).door1);

  // Nothing leaks across ticks.
  EXPECT_FALSE(in.sample(0).pickup);
  EXPECT_FALSE(in.sample(5).moveUp);
}

TEST(ScriptedInputTest, ArrowsFeedBothMovementAndNav) {
  ScriptedInput in(parseOk("hold UP 2\n"));
  EXPECT_TRUE(in.sample(0).moveUp);
  EXPECT_TRUE(in.sample(0).navUp);
  EXPECT_TRUE(in.sample(1).moveUp);
  EXPECT_FALSE(in.sample(1).navUp);
}

TEST(ScriptedInputTest, HotbarKeys) {
  ScriptedInput in(parseOk("press 1\npress 5\n"));
  EXPECT_TRUE(in.sample(0).hotbar[0]);
  EXPECT_FALSE(in.sample(0).hotbar[4]);
  EXPECT_TRUE(in.sample(1).hotbar[4]);
}

TEST(ScriptedInputTest, MousePersistsAndClicksAreOneTick) {
  ScriptedInput in(parseOk("mouse 10 20\nwait 2\nclick 30 40\nwait 1\n"));
  EXPECT_EQ(in.tickCount(), 4); // mouse consumes no tick
  EXPECT_FLOAT_EQ(in.sample(0).mouse.x, 10.0f);
  EXPECT_FLOAT_EQ(in.sample(1).mouse.y, 20.0f);
  EXPECT_FALSE(in.sample(1).mouseLeftPressed);
  EXPECT_TRUE(in.sample(2).mouseLeftPressed);
  EXPECT_FLOAT_EQ(in.sample(2).mouse.x, 30.0f);
  EXPECT_FALSE(in.sample(3).mouseLeftPressed);
  EXPECT_FLOAT_EQ(in.sample(3).mouse.x, 30.0f); // cursor stays put
}

TEST(ScriptedInputTest, CheckpointIsAnIdleTick) {
  ScriptedInput in(parseOk("hold D 2\ncheckpoint a\npress P\n"));
  EXPECT_EQ(in.tickCount(), 4);
  EXPECT_EQ(in.checkpointAt(1), nullptr);
  ASSERT_NE(in.checkpointAt(2), nullptr);
  EXPECT_EQ(*in.checkpointAt(2), "a");
  EXPECT_FALSE(in.sample(2).moveRight); // idle: nothing before leaks in
  EXPECT_FALSE(in.sample(2).pickup);    // nothing after leaks in either
  EXPECT_EQ(in.checkpointNames(), std::vector<std::string>{"a"});
}

TEST(ScriptedInputTest, EndStopsTheTimeline) {
  ScriptedInput in(parseOk("wait 5\nend\n"));
  EXPECT_FALSE(in.finished(4));
  EXPECT_TRUE(in.finished(5));
  EXPECT_FALSE(in.sample(99).moveUp); // past the end is neutral, not UB
}

// ---------------------------------------------------------------------------
// JSON emitter
// ---------------------------------------------------------------------------
TEST(JsonWriterTest, NestsAndEscapes) {
  JsonWriter j;
  j.beginObject();
  j.field("n", 3);
  j.field("s", "a\"b\\c\n");
  j.key("pos");
  j.beginArray(true);
  j.value(1.5f);
  j.value(-2);
  j.endArray();
  j.key("list");
  j.beginArray();
  j.beginObject();
  j.field("ok", true);
  j.endObject();
  j.endArray();
  j.key("empty");
  j.beginArray();
  j.endArray();
  j.endObject();

  EXPECT_EQ(j.str(), "{\n"
                     "  \"n\": 3,\n"
                     "  \"s\": \"a\\\"b\\\\c\\n\",\n"
                     "  \"pos\": [1.5, -2],\n"
                     "  \"list\": [\n"
                     "    {\n"
                     "      \"ok\": true\n"
                     "    }\n"
                     "  ],\n"
                     "  \"empty\": []\n"
                     "}\n");
}

TEST(JsonWriterTest, FloatsRoundTrip) {
  JsonWriter j;
  j.value(4182.0f);
  EXPECT_EQ(j.str(), "4182"); // only a closed container ends a document
  JsonWriter k;
  k.value(0.1f);
  EXPECT_EQ(std::strtof(k.str().c_str(), nullptr), 0.1f);
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
TEST(HeadlessModeTest, ParsesArgsAndDefaultsOutDir) {
  const char *argv[] = {"Backrooms.exe", "--headless", "../scenarios/smoke.txt",
                        "--ticks", "500"};
  headless::Options o = headless::parseArgs(5, (char **)argv);
  EXPECT_TRUE(o.error.empty()) << o.error;
  EXPECT_TRUE(o.enabled);
  EXPECT_STREQ(o.scenarioPath, "../scenarios/smoke.txt");
  EXPECT_EQ(o.maxTicks, 500);
  // scenarios/foo.txt -> artifacts/foo, a sibling directory.
  std::filesystem::path out(o.outDir);
  EXPECT_EQ(out.filename(), "smoke");
  EXPECT_EQ(out.parent_path().filename(), "artifacts");

  const char *bad[] = {"Backrooms.exe", "--headless"};
  EXPECT_FALSE(headless::parseArgs(2, (char **)bad).error.empty());
  const char *none[] = {"Backrooms.exe", "--seed", "3"};
  EXPECT_FALSE(headless::parseArgs(3, (char **)none).enabled);
}

// ---------------------------------------------------------------------------
// Telemetry snapshot, straight off a generated world (no window)
// ---------------------------------------------------------------------------
TEST(TelemetryTest, SnapshotAgreesWithTheWorld) {
  ItemDatabase::init();
  CraftingSystem::init();
  UIManager ui(1280, 720);
  DebugOverlay overlay(false);
  ::Run run(1788480606u); // qualified: gtest's Test::Run() shadows it
  MazeState state(run, ui, overlay);
  state.generateWorld();

  Telemetry t;
  state.snapshot(t);

  const Maze &maze = state.getMaze();
  EXPECT_EQ(t.mazeWidth, maze.getWidth());
  EXPECT_EQ(t.nonWallCount, maze.getNonWallCount());
  EXPECT_EQ(t.corridorCount, maze.getCorridorCount());
  EXPECT_EQ(t.regenCount, 0);
  EXPECT_EQ(t.areaState, "ROOM");
  EXPECT_EQ(t.facing, "DOWN");
  EXPECT_EQ(t.playerCellX, maze.toGridX(t.playerWorldPos.x));
  EXPECT_EQ(t.playerCellY, maze.toGridY(t.playerWorldPos.y));
  EXPECT_EQ(maze.getCell(t.playerCellX, t.playerCellY), Maze::CELL_ROOM);
  EXPECT_TRUE(t.inventory.empty());
  EXPECT_FALSE(t.inventoryOpen);

  // Every reported item really is at that cell and really is visible.
  for (const Telemetry::WorldItem &w : t.visibleItems) {
    EXPECT_EQ(itemTypeId(maze.getItem(w.x, w.y)), w.type);
    EXPECT_TRUE(isCellRenderable(maze, w.x, w.y, AreaState::ROOM));
  }
}

TEST(TelemetryTest, ItemTypeIdsAreStable) {
  EXPECT_STREQ(itemTypeId(ItemType::MUSHROOM), "MUSHROOM");
  EXPECT_STREQ(itemTypeId(ItemType::MAGIC_BOOK_OF_MAPS), "MAGIC_BOOK_OF_MAPS");
  EXPECT_STREQ(itemTypeId(ItemType::NONE), "NONE");
}
