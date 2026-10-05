# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Project Backrooms is a 2D top-down psychological horror maze game with a custom C++20 engine on Raylib 5.0 + Dear ImGui (via rlImGui). Single-player, no networking, no scripting layer — everything is C++ compiled into one executable.

Design intent lives in `docs/`: [roadmap.md](docs/roadmap.md) is the authoritative phased plan (Phases 0–3 built the maze; Phase 4 — overworld island generation — is built and awaiting a hand-play) and [the_wilderness_update.md](docs/the_wilderness_update.md) is the design reference for the two-world game (overworld survival hub + maze). The roadmap says what gets built when; the wilderness doc says why. Read both before adding a gameplay system — most features are already specced there.

## Build & Test

The `build/` directory is configured for **MinGW Makefiles** with g++ at `C:/ProgramData/mingw64/mingw64/bin/g++.exe` (Debug). All dependencies are pulled by `FetchContent` at configure time, so the first configure needs network access.

**Configure through the preset, never bare `cmake -S . -B build`.** `CMakePresets.json` pins the generator, both compilers and the build type, so the configure is correct even when the `cmake` first on `PATH` is Visual Studio's own (`Common7/IDE/CommonExtensions/.../cmake.exe`) — which is the usual case on this machine and, without the preset, silently corrupts the build tree (see "Generator corruption" below). VS Code's CMake Tools picks the preset up automatically.

```bash
cmake --preset mingw-debug
```

```bash
cmake --build --preset mingw-debug
```

(`cmake --build build -j` is equivalent once configured.)

Run the game — **must run with `build/` as the working directory**, because assets are loaded via relative paths like `assets/magic_trip.fs`:

```bash
cd build && ./Backrooms.exe
```

Tests (Google Test, registered with CTest via `gtest_discover_tests`):

```bash
./build/BackroomsTests.exe
```

A single test or suite:

```bash
./build/BackroomsTests.exe --gtest_filter=MazeTest.ToroidalWrapping
```

Headless run — scripted input, hidden window, no frame pacing; writes screenshots and telemetry per checkpoint (see "Headless harness" below):

```bash
cd build && ./Backrooms.exe --headless ../scenarios/pickup.txt
```

Artifacts land in `artifacts/<scenario>/<checkpoint>/` (gitignored). `--out <dir>` overrides, `--ticks N` caps the run.

### Build gotchas

- **From Git Bash the executables need MinGW on `PATH`.** Both `.exe`s link `libstdc++-6.dll` and `libgcc_s_seh-1.dll` dynamically. PowerShell finds them; Git Bash does not, and the process dies with exit code 127 and no output. Prefix with `PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"` or run from PowerShell. The same applies to **building** the test target from Git Bash: `gtest_discover_tests` runs the freshly linked exe to list tests, and without MinGW on `PATH` that step fails and make deletes `BackroomsTests.exe`.
- **Generator corruption (the most common breakage).** `CMakePresets.json` pins the generator, but a bare `cmake -S . -B build` (or any tool that ignores presets) still bypasses it. When such a reconfigure runs with the VS-bundled cmake, it rewrites the top-level `build/CMakeCache.txt` to `Visual Studio <N>` while every FetchContent sub-build under `build/_deps/*-subbuild/` keeps its `MinGW Makefiles` cache. A generator is immutable once written to a cache, so the nested raylib configure aborts with *"Does not match the generator used previously"*, the whole configure dies before emitting any `.vcxproj`, and the next build fails with `MSBUILD : error MSB1009: Project file does not exist. Switch: ALL_BUILD.vcxproj`. Recover by deleting **only** the top-level cache and re-running `cmake --preset mingw-debug`:
  ```bash
  rm -rf "build/CMakeCache.txt" "build/CMakeFiles"
  ```
  Leave `build/_deps/` in place — its sub-build caches are already MinGW and its downloaded sources (~150 MB of raylib/ImGui/rlImGui/googletest) are reused, so nothing re-downloads.
- **Game sources are listed once, in the `BACKROOMS_GAME_SOURCES` variable**, and shared by both `add_executable` calls (`Backrooms` adds `main.cpp`, `BackroomsTests` adds the `tests/*.cpp`). Adding a new `.cpp` is one edit to that variable. Header-only additions need no edit at all — which is why `debug_log.hpp` and `asset_load.hpp` are headers.
- **Assets are copied at configure time** (`file(COPY assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})`), not at build time. After editing or adding anything in `assets/`, re-run the cmake configure step — a plain `cmake --build` will not refresh `build/assets/`.
- `target_compile_definitions(... IsTextureValid=IsTextureReady)` exists because rlImGui's `main` branch expects a Raylib API name newer than the pinned 5.0. Do not remove it while raylib stays at 5.0.
- rlImGui is fetched from `main` and ImGui from the `docking` branch head — neither is pinned, so an upstream change can break the build without any local edit.

## Architecture

### Source layout

```
src/
  core/       Application, main, and the things everything may depend on
              (asset_load, render_settings, grid). Nothing here may include a
              gameplay header.
  dev/        Developer tooling - the ImGui panel, seed table, console logger.
              Dropped wholesale from a release build.
  ui/         UIManager - the shipping HUD, inventory and map overlays.
  render/     All presentation: the renderers, the DrawQueue and the shared
              view_bounds cull. Renderers own their textures; no game state.
  world/      World (the shared base), Maze, ItemSpawner, the overworld
              (noise, terrain_field, island, overworld) and world/generators/.
  entities/   Player today; mobs land here.
  items/      ItemType, ItemDatabase, CraftingSystem - data, no drawing.
  states/     GameState and the concrete states.
```

Two rules keep this honest. **Dependencies point inward toward `core/` and
`items/`**: `ui/` and `render/` may include `world/` and `entities/`, never the
reverse - a renderer knowing about the UI is the bug this layout is shaped to
prevent. And **`render/` is grouped by role, not by subject**: `maze_renderer`
does not sit beside `maze.hpp`, because all four renderers (mobs included)
share `view_bounds.hpp` and change together when the draw pipeline changes.

### Ownership chain

`main.cpp` → `Application` (owns the Raylib window, the `Run`, the `UIManager`, and a `unique_ptr<GameState>`; its `run()` is the frame loop) → one of two concrete states: `MazeState` (owns the `Maze`, `Camera2D`, its renderers, the `ItemSpawner` and the shared `std::mt19937`) or `OverworldState` (owns the `Overworld` and its renderers). `--world overworld` picks the second; there is no in-game transition until Phase 6.

**`Run` (`src/states/run.hpp`) owns what outlives either world: the seed and the `Player` (bag included).** States borrow it and hold `Player &m_player`. A state places the player with `Player::teleport` — never `m_player = Player(...)`, which would wipe the bag on every world change. The day counter joins `Run` in Phase 5. Inside a gtest body write `::Run`: gtest's `Test::Run()` shadows the name.

`main.cpp` decides everything that must be known before the window exists and hands it over as one `AppConfig` (seed, dev mode, headless, window size, blit scale, and two non-owning pointers: an `InputSource` and a `CaptureSink`, both null in the shipping game). `Application` borrows those; `main` keeps them alive longer than the `Application`.

`GameState` (`src/states/game_state.hpp`) is the extension point for future states (main menu, death screen). `Application` has no game logic — everything gameplay-side belongs in a state.

### World: the seam between the two worlds

`World` (`src/world/world.hpp`) is the base both `Maze` and `Overworld` derive from. It holds the shared toroidal geometry as plain data (`getWidth`, `wrapX`, `toGridX`… non-virtual, so generator inner loops stay fast) and a narrow virtual contract: `isSolid`, `getItem`/`setItem`/`getItemState`, `findNearestEmptyItemCell`. `Player`, `UIManager`, `ItemRenderer` and `ViewBounds` take `World&`. Anything only one world has is **not** on the interface: maze-only rules (room/corridor seal via `Maze::isSealedFrom`, doors, cupboards, the magic book) run behind `if (Maze *maze = world.asMaze())`, and surface-only data behind `asOverworld()`. Add to the virtual contract only what *both* worlds genuinely answer.

### Maze: one flat array, toroidal, with parallel layers

`Maze` (`src/world/maze.hpp`) is an implicit grid graph in a 1D `std::vector<int>`; neighbours are implied by geometry, never stored.

**`getIndex(x, y)` wraps toroidally** — `(x % w + w) % w`. There is no bounds checking anywhere, and out-of-range coordinates silently wrap to the far edge rather than returning a wall. Code that walks off the grid produces wrong-but-valid results, not crashes. Keep this in mind for any neighbour scan near an edge.

Several vectors are indexed identically and must stay the same length: `m_grid` (cell type), `m_visible`, `m_lightLevel`, `m_radiationMap`, `m_items`. Sparser per-cell data uses `std::map<int, T>` keyed by the same 1D index (`m_itemStates`, `m_cupboardInventories`).

`m_items` is the **single source of truth for placed items** — there are no per-item-type lists. `setCell` maintains `m_nonWallCount` / `m_corridorCount` incrementally, so the stat getters are O(1); write cells through `setCell`, not by touching `m_grid`.

Cell types are `CELL_WALL` / `CELL_CORRIDOR` / `CELL_ROOM`, and the corridor/room split drives real behaviour: `AreaState::ROOM` means fully lit (BFS flood), `AreaState::CORRIDOR` means the renderer draws everything and hides it with a screen-space light mask instead of an FOV check. The player switches between them through explicit door transitions (`K`/`L`), not by walking.

### Generation pipeline

Run in this order in `MazeState::onEnter()`, all sharing one seeded `std::mt19937` so a seed fully determines the world:

1. `BSPGenerator` — recursive space partition, carves rooms, merges adjacent ones, returns a middle-room index used as both the Prim's start and the player spawn.
2. `PrimsGenerator` — grows the corridor web through the space between rooms.
3. `LoopGenerator` — smashes walls to break the perfect maze into loops.
4. `TunnelBorer` — BFS-based connectivity guarantee; bores tunnels to any room Prim's missed.
5. `PrimsGenerator::pruneSmallAlcoves`.
6. `ItemSpawner::spawnInitialItems`.

Each generator exposes both `generate()` (whole maze) and `generateZone(startX, startY, w, h)` (a rectangle). The zone variants exist for the "Tic-Tac-Toe" shifting-zone regeneration in `MazeState::regenerateTicTacToeZones()`, which clears eight strips of the maze, re-runs the pipeline inside them, and asks the `ItemSpawner` to replenish exactly what `Maze::clearItemsInZone` reported destroyed. **Those zone rectangles are hardcoded to the 250×150 maze size** set in the `MazeState` constructor; changing maze dimensions requires updating them.

Doorway punching is shared in `GeneratorUtils::punchDoorways` and enforces one door per room (`Maze::isValidDoorPlacement`).

### Rendering

Data and presentation are strictly separated: `MazeRenderer` (terrain), `PlayerRenderer` (sprite + animation timer), `ItemRenderer` (world items *and* inventory icons). Renderers own their textures; `loadTextures()` is called from `MazeState::onEnter`.

`MazeState::render()` has a fixed, order-sensitive pipeline:

1. `buildLightMask(...)` **before** `BeginTextureMode` — it uses its own render texture.
2. Scene into `m_screenTarget`: maze terrain, then the **Y-sorted `DrawQueue`** (`src/render/draw_queue.hpp`): the player and every visible item are pushed with their base Y (the world-pixel row their feet touch) and drawn back to front by a stable counting sort. The player is pushed first so an exact tie goes to the furniture, as under the old fixed layers. `scenarios/ysort.txt` exercises the cases; on every other maze scenario the output is byte-identical to the layered version. A renderer that draws into the queue implements `Drawer`: it binds its subject in `collect()` and draws one entry in `drawQueued(a, b)`.
3. `drawLightMask()` (corridors only), then the radiation darkness rectangle, still inside the render texture.
4. `EndTextureMode`, then blit `m_screenTarget` to the screen, wrapped in `m_tripShader` when `Player::getMushroomEffectStrength() > 0`.
5. The magic-book overlay and all `UIManager` output draw *after* `EndShaderMode` — they are intentionally exempt from the trip distortion. `DebugOverlay::render` goes last of all, because ImGui must own the final draw of the frame.

**Two pixel spaces — canvas and window.** The scene renders into `m_screenTarget` (the *canvas*) at `camera.zoom = 1.0`, so a 32px cell is exactly 32 texels, and the canvas is then blitted to the window at `RenderSettings::blitScale`. The invariant is an integer **art** pixel, not an integer blit: all art is 16px drawn at 2× onto the canvas, so an art pixel covers `2 × blitScale` window pixels and that product must be whole — hence the allowed set {1, 1.5, 2, 3} (art at 2×/3×/4×/6×; default 1.5, ~27 tiles across a 1280 window) and why the old 1.2 shimmered. Canvas size is `ceil(window / blitScale)` (`Viewport::canvasFor` in `src/core/viewport.hpp`) — a bigger window is a bigger canvas showing more tiles; sprites never resize. Every render call takes a `Viewport` alongside the camera instead of calling `GetScreenWidth()`: the scene passes, `ViewBounds`, and the light mask all work in **canvas** space; `UIManager`, the pass-out fade and the magic-book overlay (drawn after the blit, through a `windowCamera` with `zoom = blitScale`) work in **window** space. Mouse input arrives in window pixels — convert through `MazeState::windowToCanvas` before `GetScreenToWorld2D`. `GetScreenWidth()` is only ever the window; if you find yourself calling it inside the render texture, you are in the wrong space.

`m_screenTarget` and `MazeRenderer::m_lightMask` are reallocated whenever the canvas size changes; anything else caching canvas-sized textures needs the same check.

**One pixel density: `src/core/grid.hpp`.** Every sheet in `assets/` is 16 art px per cell (`SOURCE_TILE`) drawn at 2× (`WORLD_SCALE`), so a cell is `CELL = 32` canvas px. Draw sites address sheets in whole tiles with `grid::srcTile(col, row, w, h)` and derive the destination from the source with `grid::destFor` / `grid::standingOn` — a destination rectangle never carries a scale of its own, and a hand-measured off-grid source rectangle is a bug. Tall furniture stands on its floor cell and grows upward (`standingOn`); a 2-cell table is rooted on its right-hand tile. `Maze::getCellSize()` still exists for world-to-grid maths, but renderers use `grid::CELL`.

The workshop furniture sheet is the one asset that was not authored at this density: the pack draws a locker on 2×4 tiles where the game places it on 1×2 cells. `assets/PostApoc_Workshop_16px.png` is the pack sheet halved by `tools/downsample_sheet.py` (box-average, snap to the sheet's own colours, and a three-way alpha vote that keeps the drop shadows partial — flattening them turns table undersides into slabs). Regenerate it from `../Asset packs/PostApoc_Workshop/PostApoc_Workshop_WithShadow.png` rather than editing it by hand; a 32-based pack (`Mobs/`) goes through the same tool before it enters `assets/`. Once halved, that sheet is an 8px atlas, so some of its rectangles are written in art pixels rather than tiles. `PostApoc_Workshop_Icons.png` is already 16px inventory icons and is used as-is.

**Every sheet in `assets/` is quantized to the master palette** (`assets/palette.json`, 72 colours in 7 ramps, one palette for both worlds; reasoning and maintenance rules in `docs/palette.md`). `python tools/quantize.py <src> assets/<name>.png` is how a sheet enters `assets/` (the four overworld sheets `ow_water/coast/terrain/props.png` and the generated `src/render/overworld_sprites.hpp` are built instead by `tools/build_overworld_sheets.py`, which records every crop and edit), and `python tools/quantize.py --check assets/` must report 100 % — a stray colour is a defect. Sources stay outside `assets/` so a palette change re-quantizes from the original. `tools/palette_sample.py` re-derives a proposal from the packs; it proposes, a human approves - and a run without `--render` overwrites `palette.json` with its 56-colour proposal, dropping the hand-added overworld colours. C++ never spells a colour: `src/core/palette.hpp` is the palette as `constexpr Color` ramps, **generated** by `tools/palette_header.py` from the JSON (re-run after any palette edit; `PaletteHeader.MatchesStrip` fails if header and strip drift), and `src/render/theme.hpp` maps hand-chosen roles (`theme::ink`, `theme::bad`, `theme::radiationGlow`…) onto ramp steps. UI and renderers name a role; only `DrawTexture` tints stay `WHITE`.

Two more derived assets: `mushrooms_pixel_asset.png` is no longer the pack sheet but a PixelLab img2img pass over a 0.7× shrink of it, forced to the pack's palette and then hand-trimmed to 3 columns (six variants per kind; the renderer's hash is `% 6`), so mushrooms stay small on the 2× grid; `workshop_prop_icons.png` holds the paper and pencil inventory icons cut at native 1:1 from the pack's furniture sheet — UI icons are stretched into a slot in window space, so they sit outside the world-density rule. Two deliberate exceptions to "everything at 2×": those two icons, and the magic book, drawn at 1.5× because at a full cell it swallowed its table.

The corridor light mask is not on the grid: its gradient is a 512px stamp (`MazeRenderer::kLightGradientDiameter`) scaled to `3 * CELL * lightSizeScale`, and its three tunables have exactly one home, `RenderSettings` — `MazeRenderer` seeds from it. A dark `scene.png` in a corridor is usually the radiation flicker peaking on that tick, not the mask.

### Items, crafting, spawning

`ItemType` (`src/items/item.hpp`) is one unified enum for both grid interactables and inventory items. Behaviour is data, not switch statements: `ItemDatabase` is a static registry of `ItemDefinition` (`isPlaceable` / `isPickable` / `isConsumable`, stack size, UI sprite rect) — query `ItemDatabase::getDef(type)` rather than special-casing an enum value. `CraftingSystem` is the equivalent static registry of `Recipe`.

Both need `ItemDatabase::init()` and `CraftingSystem::init()` before use; these are called in the `Application` constructor, so any test or tool that does not build an `Application` must call them itself.

`ItemSpawner` owns *all* placement rules (barrels avoid doorways, mushrooms grow in clumps in radiated room corners, cupboards hug walls, tables need two adjacent room tiles). Adding an item type means adding a spawn method there — `Maze` and the renderers only need to recognise the new enum value.

### Player ↔ UI communication

`Player` never touches the UI. It sets one-shot boolean flags which `MazeState` drains each frame via the `pollEventX()` methods (`pollEventMushroomConsumed`, `pollEventMapCrafted`, …) — each poll returns the flag and clears it — and translates them into `UIManager::showPopup(text, PopupType, duration)` calls.

`UIManager` is a state holder and mailbox, not a caller: it owns inventory/cupboard/map-overlay open state and the popup queue.

Its one write path into the game is `handleInventoryInput(Player&, World&)` (plus `handleSlotNavigation(in)` for the keyboard), called from `MazeState::handleInput` — the input phase, before any drawing. `UIManager::render` and `renderInventory` are read-only with respect to the `Player` and the `Maze`; they only write hover bookkeeping for the tooltip. Both passes take their geometry from `InventoryLayout::compute`, so a click is hit-tested against exactly the rectangle that gets drawn. Keep that direction: new UI widgets resolve their clicks in `handleInventoryInput` and add their rectangle to `InventoryLayout`, never mid-draw.

`DebugOverlay` (`src/dev/debug_overlay.hpp`) is the development panel, split out of `UIManager` and owned by `Application` so it survives future state switches. It is a *view*: presentation values the game needs regardless (torch on/off, camera zoom, the three light-cone numbers, show-zones) live in `RenderSettings`, which `MazeState` owns and the overlay edits by reference; only debug-only state (the god-view minimap texture, magic-book and trip forcing, status strings) belongs to the overlay. It uses the same mailbox convention — `MazeState` reads and clears `triggerTicTacToeRegen`, `triggerMagicBookSpawn`, `triggerForceTrip`, `triggerEndTrip`. Debug buttons must call the same public entry points the real systems will use, so they keep exercising the shipping path.

Gating is runtime only: `Backrooms.exe --dev` arms the panel (`src/dev/dev_mode.hpp`) and `F1` shows/hides it. The code still ships inside the binary — a release build should drop `BACKROOMS_DEV_SOURCES` from the executable and guard the `dev/` includes, which is why the dev tooling is its own directory and its own CMake list.

### Headless harness

`Backrooms.exe --headless <scenario>` replaces the keyboard with a text file and the screen with a directory. The game side is two small things in `core/capture.hpp`, and everything else lives in `dev/`:

- **`Telemetry`** is a POD a state fills on request — `GameState::snapshot(Telemetry&)`, the mirror image of `InputState`: a value out, no JSON and no file I/O in `states/`. `MazeState::snapshot` reports player cell/facing/area, the camera's world rect, the bag, the items the canvas would draw (same `isCellRenderable` rule as `ItemRenderer`), and the maze counts.
- **`CaptureSink`** is four hooks per tick. `Application::run` brackets the tick with `beginTick` / `endTick(tick, telemetry)`; `MazeState::render` calls `onSceneReady(m_screenTarget)` **immediately after `EndTextureMode`** and `onFrameReady()` **immediately before `EndDrawing`**. Those two positions are the whole point: the canvas is complete only there, and the back buffer is undefined after the swap — so the frame capture cannot be done from `Application`.

The `dev/` side: `scenario.hpp` (grammar + parser, documented in its banner), `scripted_input.hpp` (compiles the command list into one `InputState` per tick up front, mirroring `pollHardwareInput`'s held/pressed semantics exactly), `headless_mode.hpp` (CLI), `json_writer.hpp` (emit only — the project deliberately has no JSON parser), and `headless_harness.hpp` (the `CaptureSink` that writes `scene.png`, `frame.png`, `telemetry.json` per checkpoint and `run.json` per run). A `checkpoint` is an idle tick: it records the world after every command before it has settled for one tick, with nothing from the next command leaked in.

Headless still needs a GL context (render texture, trip shader), so the window is created with `FLAG_WINDOW_HIDDEN` rather than not at all; `SetTargetFPS` and `rlImGuiSetup` are skipped. Two things the harness depends on that are easy to break: **`SetRandomSeed(seed)` in the `Application` constructor** (raylib seeds `GetRandomValue` from the clock otherwise, and the radiation flicker uses it), and **`rlDrawRenderBatchActive()` before `LoadImageFromScreen`** (rlgl only flushes its batch at `EndDrawing`, so without it the UI drawn last is missing from `frame.png`). The contract is that one scenario run twice produces byte-identical artifacts; `diff -r` two `--out` directories to check.

Scenarios live in `scenarios/` at the repo root — they are not assets and do not go through the configure-time copy. Named seed fixtures for them are in `dev/debug_seeds.hpp` (`mushroom_room` = seed 3 has a mushroom in pickup range at spawn; `furniture_room` = seed 1 and `barrel_room` = seed 38 put tables, cupboards and a toxic barrel in view for presentation checks — `scenarios/furniture.txt` and `barrel.txt` are single-checkpoint scenarios that exist for their `scene.png`). `ysort.txt` walks the player in front of and behind furniture. `overworld_walk.txt`, `overworld_wrap.txt` and `overworld_coast.txt` use the `world overworld` header line; the wrap and coast scenarios also use `spawn X Y` (a dev-only start tile) to begin three tiles short of the seam / on a sand spit.

Cached render textures (`DebugOverlay::m_mapTexture`, `UIManager::m_magicBookMapTexture`, per-instance drawn maps) are regenerated only when marked dirty. Any code that changes maze layout must call **both** `DebugOverlay::markMapDirty()` and `UIManager::markMagicBookMapDirty()`.

### Overworld: island generation

The surface is a 3072×3072-tile world that wraps like the maze, with a ~2k-tile island centred in it and ≥500 tiles of open ocean to the seam, so no generator has to be seamless across the wrap (`IslandTest.TheWrapSeamLiesInOpenOcean` guards this). Three layers:

- **`TerrainField`** (`world/terrain_field.hpp`) — height, temperature and moisture as **pure functions** of (x, y, seed), built on our own noise (`world/noise.hpp`: hash → gradient noise → fBm / ridged / domain warp). Height is the land score: warped-radial falloff + hills + ridged mountains, with a hard cliff past 0.92 radii. Never stored per tile. `IslandConfig` holds the compile-time shape; generation constants are not live settings.
- **`IslandMap`** (`world/generators/island_generator.*`) — whole-island passes on a coarse grid (1 cell = 8×8 tiles): ocean flood, priority-flood depression filling, lakes, D8 drainage, flow accumulation, rivers (cells with enough upstream area, so every river ends in a lake or the ocean by construction), Euclidean distance to water. `Island` (`world/island.*`) combines both: `sample(x, y)` classifies any tile in O(1) (ocean → lake → marsh → river → `biome::classifyLand`, a Whittaker lookup in `world/biome.hpp`) and picks the spawn.
- **`Overworld`** (`world/overworld.*`) — the `World` implementation. Tiles live in 32×32 **chunks** generated on demand and dropped by `retainAround` (memory scales with the view, not the world). Props come from per-chunk Bridson Poisson-disc points (`world/generators/poisson.*`), thinned by a biome density roll; a chunk yields any point within `kPropSpacing` of a higher-priority (lower-index) neighbour's point, so spacing holds across borders whatever the build order. Player changes (a removed prop) live in a sparse **change record** outside the chunks and are re-applied on every rebuild. Water is walkable for now (no swimming/boat until Phase 9).

`OverworldRenderer` draws the ground as stacked layers (water, sand, grass, then one overlay per biome) autotiled on a **dual grid**: each tile sits on a cell corner and picks one of 16 shapes from the 4 cells meeting there (`cornerMask`, TL=8 TR=4 BL=2 BR=1). The coast is High Tides' hand-drawn set (foam animated off `m_totalTime`); inland edges are generated by the build script from LightBorne's corner masks. A layer must also cover the cells of any layer that sits on it (gravel runs under snow), or a rim of the layer below shows. Props go through the shared `DrawQueue`; the world only says TREE/PINE/BUSH/ROCK/REEDS plus a hash byte, and `OverworldRenderer::spriteFor` picks the species from a per-(kind, biome) weighted pool — species becomes world data when gameplay needs it. Sprites are up to 16 tiles tall, so `collect` scans `kReachBelowTiles` rows below the screen and the queue's range is widened to match; a prop covering the player draws faded (`setFocus`). `theme::ocean`…`theme::snow` now only paint the debug island map. The debug panel's overworld view (`DebugOverlay::render(Player&, Overworld&, …)`) shows island stats, a biome/height map, and raises `triggerNewIsland` / `triggerRemoveProp`, which `OverworldState` drains.

### Input

Input is a value, not a global. `pollHardwareInput()` in `src/core/input_state.hpp` is the **only** place raylib's keyboard/mouse API is called (plus the `F1` dev-panel toggle in `Application::run`, deliberately kept outside the struct). It fills an `InputState` POD — named actions (`pickup`, `door1`, `moveUp`…) split into held and pressed, plus mouse position and buttons — which `Application::run` obtains from an `InputSource` each tick and passes to both `GameState::update(dt, in)` and `render(in)`. Gameplay reads `in.pickup`, never `KEY_P`; a rebinding is one line in `pollHardwareInput`, and a test drives `Player::update` by filling the struct by hand. Movement and door/pickup handling live in `Player::update`; everything else is in `MazeState::handleInput`.

Time is fixed, not measured: every tick advances the simulation by `Application::kFixedDt` (1/60 s). `SetTargetFPS(60)` paces the loop to real time; nothing reads `GetFrameTime()` or `GetTime()`. `Application::run(RunConfig)` takes `maxTicks` so a harness can bound a run. A click that lands on the debug panel (`ImGui::GetIO().WantCaptureMouse`) is stripped from the `InputState` before the game sees it.

WASD/arrows move · `K`/`L` door 1 / door 2 · `P` pick up · `I` inventory · `O` open focused cupboard · `U` use/consume (or close fullscreen map) · `Q` enter placement mode, then left-click a visible floor tile · `1`–`5` hotbar · `F11` fullscreen · `F1` debug panel (only with `--dev`).

Command line: `--seed <name|number>` pins the world (`src/dev/debug_seeds.hpp`), `--dev` arms the debug panel, `--world overworld` starts on the surface (`src/dev/dev_mode.hpp`), `--headless <scenario> [--out <dir>] [--ticks N]` runs a scripted scenario with no window (`src/dev/headless_mode.hpp`; a `seed` line in the scenario overrides `--seed`).

## Tests

Five files, split by subject:

- `tests/test_maze.cpp` — maze indexing, toroidal wrapping, generator invariants (rooms carved, connectivity, no diagonal leaks), the derived Tic-Tac-Toe zone layout, `isCellRenderable`, and the `grid.hpp` geometry.
- `tests/test_inventory.cpp` — pickup/drop/stack/swap rules and crafting, including the full-bag edge cases.
- `tests/test_magic_book.cpp` — book spawn candidate selection and its search radius.
- `tests/test_harness.cpp` — the scenario grammar, the tick timeline it compiles to (held vs pressed, checkpoints as idle ticks, mouse persistence), the JSON emitter's exact output, the `--headless` CLI, and both states' `snapshot` against a generated world.
- `tests/test_overworld.cpp` — noise, Poisson spacing, island determinism, spawn rules, river termination, lone lakes, the open-ocean seam, cross-border prop spacing and build-order independence, the change record surviving eviction, wrap continuity, the player colliding with a tree, and the renderer's pure parts (corner masks, every prop kind having a sprite in every land biome, full-orange autumn held back, atlas frames not overlapping). The suites share one generated island (`sharedWorld()`) because generation costs ~0.2 s in Debug.

The test target links the whole game including Raylib and ImGui, so tests can construct real game objects, but must not open a window. Anything needing `ItemDatabase` or `CraftingSystem` must call their `init()` itself — only the `Application` constructor does that in the shipping path.

## Conventions

- Members are `m_camelCase`; classes are `PascalCase`; files are `snake_case.cpp/.hpp`.
- Includes are project-root relative (`#include "world/maze.hpp"`) — `src` is on the include path.
- Headers carry banner comments (`// ==== ClassName ====`) explaining the algorithm and its complexity. This is a portfolio project; new systems are expected to document the CS concept behind them the same way.
