# Project Backrooms

A 2D top-down maze survival game built in modern C++20, using Raylib for rendering and Dear ImGui for in-engine debug tooling. The game systems — procedural generation, visibility, lighting, hazards, items and crafting — are written from scratch rather than taken from an engine.

> **Status: work in progress.** Phases 0–3 (the maze) are complete, and Phase 4 — a procedurally generated overworld island — is built and awaiting a hand-play (`--world overworld`). The maze generation, visibility, lighting, radiation, item and crafting systems are implemented and playable; the island is walkable but has no gameplay yet, and there is no in-game transition between the two worlds. Combat, mobs and progression are not yet built. See [Current State](#current-state) for the honest breakdown.

<!-- TODO: add a GIF here showing maze generation and the flashlight/FOV in motion -->

---

## What it is

You explore a procedurally generated, infinitely wrapping maze. Rooms are lit; corridors are dark, cut only by the beam of your torch. Radiation spreads through the level and pushes you toward safer ground. You scavenge containers for items, craft tools from what you find, and the world periodically reshapes itself around you — corridors you mapped ten minutes ago may no longer exist.

The design goal was to make each system a real implementation of a computer science concept rather than a shortcut. The maze is an implicit graph. Room visibility is a flood fill; corridor darkness is a generated screen-space light mask. Hazard spread is a BFS flood fill. Connectivity is a guarantee enforced by an algorithm, not an outcome hoped for from tuning.

---

## Current state

**Implemented and working**

| System | Detail |
|---|---|
| Procedural generation | BSP room partitioning → Prim's algorithm corridors → loop injection → connected-component pruning |
| Connectivity guarantee | Tunnel borer repairs any disconnected region, constraint-aware, runs until the grid is fully connected |
| Toroidal world | Maze wraps on both axes via modulo indexing; pathfinding and generation respect the wrap |
| Visibility & lighting | Flood-fill room visibility; corridors darkened by a generated directional torch mask with radial and angular falloff |
| Radiation | BFS zone expansion with per-room uniform intensity and flickering light response |
| Items & crafting | Grid-parallel item layer, item database, spawner, 20-slot inventory, container inventories, recipe-based crafting |
| Map mechanics | Craftable paper map (pencil + paper), discoverable magic book of maps, minimap rendering |
| Zone regeneration | Sections of the maze erase and regenerate at runtime while preserving global connectivity |
| Overworld island | 4000×4000-tile wrapping world: noise-built terrain, priority-flood lakes, D8 drainage and rivers, Whittaker biomes, chunked on demand with Poisson-disc props |
| Overworld rendering | Dual-grid autotiling, a dithered fade shader blending biomes, flowing and swaying water, footprints; every sheet built from the packs by `tools/build_overworld_sheets.py` |
| Master palette | One 77-colour palette for both worlds, enforced on every sheet by `tools/quantize.py --check` |
| Architecture | State machine (`GameState` → `MazeState` / `OverworldState`), a `World` base shared by both worlds, layered source layout |
| Debug tooling | Dear ImGui overlay for camera zoom, flashlight radius and world inspection |
| Headless harness | `--headless <scenario>` drives the game from a script and writes screenshots and telemetry, byte-identical across runs |
| Tests | 109 GoogleTest cases covering generation invariants, inventory/crafting, magic book spawning, the headless harness and the overworld |

**Not yet implemented**

Mobs and AI navigation (Dijkstra flow fields), combat and weapons, permadeath and progression, wave/spawn logic, the Level 2 descent, per-tile line of sight with true wall shadows, audio, telemetry and analytics, main menu and GUI polish.

Full phase-by-phase plan in [`docs/roadmap.md`](docs/roadmap.md).

---

## Technical design

### Maze as an implicit graph

The world is stored as a single flat `std::vector<int>` indexed `y * width + x`, not as a graph of pointer-linked nodes. Neighbours are derived arithmetically rather than dereferenced, which keeps traversal cache-friendly — BFS over the grid walks contiguous memory instead of chasing pointers across the heap.

Coordinate lookups run through `getIndex()`, which applies modulo wrapping on both axes. This means the torus topology is a property of the indexing layer: generation, pathfinding and the tunnel borer all wrap correctly without any special-case code.

### Generation pipeline

Four stages compose to produce a layout:

1. **BSP** recursively partitions the space and carves rooms.
2. **Prim's algorithm** grows a corridor tree connecting them.
3. **Loop injection** adds extra edges, so the result isn't a perfect maze with exactly one route between any two points.
4. **Connected-component pruning** strips orphan alcoves and dead-end stubs.

Doorways are constrained: a door cell must border exactly one room and one corridor, and diagonal-only gaps between open cells are rejected so the player can never squeeze through a corner.

### Guaranteed connectivity

Generation can still strand a region, so `TunnelBorer::ensureConnectivity` repairs it rather than regenerating and hoping:

1. Flood-fill from any open cell to map the main network.
2. Find any open cell outside it.
3. BFS from that cell **through solid rock**, tracking parents, until it breaks into the main network.
4. Retrace the parent chain and carve the shortest tunnel.
5. Repeat until nothing is isolated.

The borer is constraint-aware — it checks door validity before boring through a wall, and repairs diagonal leaks recursively as it carves, since fixing connectivity can otherwise violate rules established earlier in the pipeline.

### Visibility and lighting

Two modes, chosen by the area the player occupies.

**Rooms** are treated as fully lit. An 8-directional BFS flood fill runs from the player through room tiles, marking each reached cell visible. Walls are marked visible but stop the flood, so the fill stays bounded to the room the player is standing in.

**Corridors** skip per-tile visibility entirely and are darkened in screen space instead. A directional torch mask is generated once into a texture — only the forward half is filled, giving a cone rather than a disc — combining a radial falloff with an angular falloff whose curve is driven by a configurable exponent. Each frame the mask is drawn onto a black render target at the player's screen position, rotated to their facing, and composited over the world. Cone angle, radius and fade strength are all tunable live through the ImGui debug panel.

This is a rendering-side approach rather than a per-tile field-of-view algorithm: it produces a convincing torch beam, but walls do not cast true geometric shadows. Per-tile line of sight with real occlusion (recursive shadowcasting) is a planned improvement, not a current feature.

---

## Testing

```bash
cd build
ctest --output-on-failure
```

The suite asserts invariants rather than getters — the properties that must hold for *any* seed:

- `FullConnectivityEnsured` — every open cell is reachable from every other
- `ZoneRegenerationConnectivity` — connectivity survives erasing and regenerating eight zones on a 250×150 grid
- `NoDiagonalLeaks` — no diagonal-only gaps between open cells
- `ToroidalWrapping` — index arithmetic wraps correctly on both axes
- `UniformRoomRadiation` — radiation intensity is uniform within a room
- Plus construction, cell read/write, BSP carving and corridor carving

Most tests pin a fixed seed so they stay deterministic. `FullConnectivityEnsured` deliberately does the opposite — it draws a fresh seed from the clock on every run, so it validates a newly generated maze each time rather than one memorised layout. It records that seed into the GoogleTest XML output:

```bash
./BackroomsTests --gtest_output=xml:results.xml
```

which yields `<property name="RandomSeed" value="1788478678"/>`. Hardcode that value back into the test to reproduce a failure exactly.

---

## Building

**Requirements:** CMake 3.14+ and a C++20 compiler. Developed and tested with GCC 15.2; MSVC 2019+ and Clang 12+ are expected to work but are not currently verified. Raylib 5.0, Dear ImGui, rlImGui and GoogleTest are fetched automatically via CMake `FetchContent` — the first configure needs network access, but there is no manual dependency setup.

```bash
git clone https://github.com/kvh6488/Project-Backrooms.git
cd Project-Backrooms
cmake -S . -B build
cmake --build build -j
```

If you have more than one toolchain installed, name the generator explicitly — `-G Ninja`, `-G "Unix Makefiles"`, `-G "MinGW Makefiles"` — rather than relying on CMake's default, which depends on which `cmake` resolves first on `PATH`. A generator is fixed permanently once written into `build/CMakeCache.txt`, so switching toolchains means a fresh build directory, not a reconfigure of the existing one.

The game **must be run with `build/` as the working directory**, since assets are loaded via relative paths such as `assets/magic_trip.fs`:

```bash
cd build
./Backrooms        # the game
./BackroomsTests   # the test suite
```

Assets are copied into the build directory at *configure* time, so re-run the configure step after editing anything under `assets/`.

---

## Project layout

```
src/
├── core/         # entry point, application loop, ImGui debug UI
├── states/       # game state machine
├── world/        # maze, renderer, item spawning
│   └── generators/   # BSP, Prim's, loops, tunnel borer
├── entities/     # player and rendering
└── items/        # item database, crafting, rendering
tests/            # GoogleTest suite
docs/             # roadmap and phase design documents
```

---

## Tech stack

C++20 · [Raylib 5.0](https://www.raylib.com/) · [Dear ImGui](https://github.com/ocornut/imgui) (docking) · [rlImGui](https://github.com/raylib-extras/rlImGui) · CMake · GoogleTest

## Credits

Sprite and tileset assets are third-party packs used under their respective licences. All game code and systems are my own.
