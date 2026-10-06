# Project Backlands — Roadmap & Reference

> **Living document** — updated as decisions are made. Last updated: **06-10-2026**.
>
> This is the **plan**: what gets built, and in what order. The **design** — why the game is shaped the way it is — lives in [the_wilderness_update.md](the_wilderness_update.md), referenced below as *wilderness §N*.

---

## 1. Project Vision

A **2D top-down survival game in two worlds** — a pristine wilderness island above, and a psychological-horror maze below — built as a **portfolio-grade engineering showcase**. Every gameplay mechanic maps to a specific CS/math concept.

**Premise (wilderness §1).** The year is 2100. You live alone with your wife on a wild island — hunting, fishing, foraging. One day she goes missing. You keep surviving, range further, and eventually find a way into something underneath.

**Hub and spoke (wilderness §2).**
- **The Overworld** — the persistent hub. Beautiful, peaceful, never corrupted. Threats are the elements, hunger, injury, weather. Time is **cyclical** (seasons). Tech is bushcraft. Terrain never moves.
- **The Underworld (the maze)** — the dangerous spoke you dive into and retreat from. Liminal, hostile, claustrophobic. Threats are radiation, mobs, darkness and a shifting layout. Time is **monotonic** — it only ever gets worse. Tech is old-world salvage.

**The core loop (wilderness §3).** Survive on the surface → range outward → prepare a dive → find an entrance (never before Day 20) → dive for what the surface cannot give you → return and reinvest → repeat, deeper. **The score is days survived.** Pure surface survival plateaus — the maze is the only path to a long run.

**Escalation (wilderness §4.2).** Maze difficulty does not read the calendar. It is anchored to player events in three stages: dormant until you **find** an entrance, slow and linear from discovery, exponential from your first time **inside**. Escalation drives maze shift aggression, layout confusion, radiation strength/spread and mob spawn rates.

**Gameplay pillars:**
- **Two worlds, two toolkits** — fire above, batteries below; abundant-but-slow bushcraft vs scarce-but-powerful salvage.
- **Navigation under pressure** — limited visibility, corner-peeking, player-made maps that rot as the maze shifts. The maze cannot be paused.
- **Escalating survival** — the maze gets permanently worse; the surface gets worse periodically (seasons, extreme weather).
- **Harsh permadeath** — any death, in either world, resets the run to Day 0 on a new world seed.
- **Radiation expansion** — hostile radiation spreads by BFS, underground only. The surface never degrades.

**Dual purpose:**
- **Portfolio piece** (primary) — demonstrate systems engineering, algorithm design, and performance optimization to recruiters/senior engineers.
- **Real game** (secondary) — a genuinely fun, atmospheric survival-horror experience.

**Developer context:** Built by a 2nd-year CS student as a first major project. All decisions should be well-documented for learning purposes.

---

## 2. Tech Stack

```
┌─────────────────────────────────────────────┐
│           The Compute Core (C++20)          │
│                                             │
│  Engine Logic ─── World Gen ─── Entities    │
│       │              │             │        │
│       └──────────────┴─────────────┘        │
│                      │                      │
│              Raylib 5.0 (Rendering)         │
│              Dear ImGui (Debug UI)          │
│              rlImGui (Raylib↔ImGui bridge)  │
│                      │                      │
│   Headless harness (scenarios, telemetry)   │
├─────────────────────────────────────────────┤
│       The Data Science Workspace            │
│                                             │
│  Python ─── NumPy ─── Matplotlib            │
│  Jupyter Notebook (parse + plot logs)       │
│  Asset tools (quantize, downsample, palette)│
├─────────────────────────────────────────────┤
│            Build & Version Control          │
│                                             │
│  CMake + CMakePresets.json (mingw-debug)    │
│  MinGW-w64 g++ (compiler)                   │
│  Google Test via CTest                      │
│  Git + GitHub (version control)             │
│  FetchContent (Raylib, ImGui, rlImGui, GTest)│
└─────────────────────────────────────────────┘
```

---

## 3. Full Phased Roadmap

Phases 0–3 built the maze. **Phases 4–6 bring the overworld up to par with it and then connect the two**, so the core loop can be played and tested before further mechanics are built on top. Day estimates have been dropped — at two-world scope this is a multi-year project, and phases are ordered by dependency, not by date.

---

### Phase 0 (COMPLETE) — Project Scaffold
Get the project compiling, a window open, and the debug overlay working.
- CMake project with FetchContent (Raylib, ImGui, rlImGui)
- Window opens, renders a colored background
- Basic game loop: input → update → render at 60 FPS
- Dear ImGui debug overlay showing FPS + frame time
- Git repo initialized with `.gitignore`, pushed to GitHub

### Phase 1 (COMPLETE) — Procedural Maze & Player
Generate a maze, render it, and let the player walk through it.
- **Maze generation algorithm** — Hybrid BSP + Prim's Algorithm + Random Loops + Connected Component Pruning.
- Maze data structure (cell/wall representation in a flat array).
- Render maze as colored tiles (placeholder).
- Player entity: position, WASD movement, wall collision.
- **Dynamic 2D Camera** — Camera follows the player through a massively expanded maze grid.
- **Room vs. Corridor Separation** — Rooms and corridors are strictly separated via pre-spawned doors. This constrains rendering complexity and establishes choke points for gameplay.

### Phase 2 (COMPLETE) — Dynamic Maze & Textures
Make the maze visually distinct and physically wrap around.
- Toroidal wrapping (modulo addressing for "infinite" feel).
- Maze mutation/regeneration sections.
- Load proper tile textures for rooms and corridors.
- Add door textures.
- Animate player movement.
- **Fog-of-war / visibility system:**
  - Corridors are inherently dark. First entering yields a message: *"Damn it's dark in these corridors"*, with heavily restricted sight distance (raycasting).
  - Rooms are always fully lit.
  - Equipping a torch extends corridor visibility distance (implemented in a later phase).
- Possibly resize game window, and gameview to fit screen etc, add temporary application icon.

### Phase 3 (COMPLETE) — Radiation & Base Game Development
The mathematical heart — radiation systems spreading across the grid.
- **Radiation Sources (Barrels):**
  - Random barrels spawn in random rooms across the map.
  - Emit a radiation zone around them (starting at ~15 tiles radius).
  - Radiated tiles turn green, spreading calculated using Breadth-First Search (BFS).
- **Debug & UI:**
  - Add an ImGui toggle to visualize radiation barrels and their BFS spread on the minimap.
- **Domain Separation:** Standard doors do not block radiation spread (it flows like a fluid through empty corridors and rooms). Players can upgrade doors to block radiation later.
- BFS for radiation territory expansion.
- Radiation causes lighting flickering.
- **Radiation Spread rules:**
  - If any part of a room gets radiated, the whole room does.
  - Corridors act as the linking arteries that spread radiation between rooms.
- **Resource Spawning:**
  - Special items and resources (like Mushrooms) only spawn in radiated rooms.
  - Two types of mushrooms (magic and normal)
- **Inventory System**
  - Allows player to pick up and hold items, 20 slots in total, 5 active slots.
- **Magic Mushrooms & Map**
  - Visual distortion when player eats mushrooms: screen fades between colours, and wavy effect applied over screen.
  - Eating magic mushroom allows player to find magic map randomly in maze drawers/cabinets.
  - Magic Map UI.
  - Magic map maybe also only found near spawn (still rare to find), and is instantly set to show just up to the regen zone square border no further.
- **Pens and paper spawns** in drawers, and basic crafting system for the classic map — shows the maze as it is now for a given rectangle radius when opened, permanently fixed at that spot.
- **Base Furniture spawns & fundamental materials**: Searching desks, chairs, filing cabinets for raw materials and lore.
  - **Base Crafting System**: Basic crafting system developed
  - **Classic Map**: Crafted with pen and paper. Upgradeable (one time only) to increase its range. *Conflict note: May remain a fullscreen overlay (leaving player vulnerable) OR change to a minimap.*

### Phase 4 (COMPLETE) — The Overworld: Island Generation
The surface now matches the maze's standard: generated from a seed, rendered on the shared grid, walkable, and covered by tests and headless runs. Generation only — no entrances, seasons or survival systems.

- **Two-world seam.** `Run` owns the seed and `Player` (bag included) above the states. `World` is the shared base of `Maze` and `Overworld` (toroidal geometry plus a narrow virtual contract), so `Player`, `UIManager` and `ItemRenderer` no longer depend on `Maze&`. `PlayingState` became `MazeState`; `OverworldState` sits beside it, started with `--world overworld`. No in-game transition yet (Phase 6).
- **Island shape and storage.** A 4000×4000 wrapping world with a ~2.7k-tile island and ≥500 tiles of open ocean to the seam, so no generator has to be seamless across the wrap. Height, temperature and moisture are pure functions of (x, y, seed) on our own noise. Whole-island passes run once on a coarse grid (1 cell = 8×8 tiles); fine detail lives in 32×32 chunks built on demand and evicted by distance, with a sparse change record so a removed prop stays removed.
- **Generation pipeline.** Ocean flood → priority-flood depression filling → lakes → D8 drainage and flow accumulation → rivers (cells with enough upstream area, so each ends in a lake or the ocean by construction; this replaced walking from hand-picked sources) → swamps and coastal scrub → distance fields → Whittaker biome lookup. Coarse data reaches tiles only through smooth fields. Spawn is inland on grassland. Props come from per-chunk Bridson Poisson-disc points thinned by biome density; spacing holds across chunk borders. Points are memoised and one chunk is prefetched per tick.
- **Rendering.** `OverworldRenderer` autotiles on a dual grid (16 corner shapes; the 47-tile blob set moves to Phase 9), blends biomes with a dithered fade shader, and flows water by world position. Props go through the Y-sorted `DrawQueue`, now shared with the maze. The master palette grew to 72 colours (since 77) and the surface sheets are built from four packs by `tools/build_overworld_sheets.py`. Footprints (`StepEffects`) are presentation only.
- **Tooling and tests.** `OverworldState::snapshot` telemetry, four headless scenarios (walk, wrap seam, coast, coastal scrub), a debug-panel island map with new-seed and remove-prop buttons, and `tests/test_overworld.cpp` covering determinism, spawn, rivers, the open-ocean seam, prop spacing, the change record, prefetch and the renderer's pure parts.
- **Deferred.** Water is walkable until Phase 9 (no swimming or boat). Height has no effect on movement.

**Not in Phase 4:** maze entrances and set pieces, the overworld ↔ maze transition, seasons, weather, survival meters, the day counter.

### Phase 5 (COMPLETE) — Seasons & the Day Counter
The surface's clock (wilderness §4).

**The calendar (built).** `Calendar` (`world/calendar.hpp`) lives in `Run` and is one integer: fixed-step ticks since the run began. Day, time of day, season and year day are all derived from it. A day lasts **15 real minutes**; a season is **20 days**, so a year is **80**. A run starts on **Day 0 at 08:00 in mid-spring** (year day 10). That puts Day 20, the earliest a maze entrance can open, in **mid-summer**, and the first winter starts on Day 50. Both states advance the clock one tick per update, so it runs at one rate in both worlds. `seasonalDrift` is a field on the calendar, stubbed at 0 until Phase 6.

**Temperature and snow (built).** Every tile keeps a fixed base temperature t0 (it falls with height, plus broad noise). The season slides one number, the **snow line**, along that scale: `°C = 25 × (t0 − snowLine(yearDay))` plus a ±4 °C daily swing. Snow lies where the daily mean is below 0 °C, on grass, forest and mountain only; wetland, beach and coastal sand never take snow. `Climate` (`world/climate.hpp`) sets the snow line's key values from **this island's own temperature distribution**, so every seed hits the targets:

| Year day | Snow line | Seed 1 |
|---|---|---|
| 0 / 60 — start of spring, end of autumn | 70 % of the mountains | 70 % of mountains, 8 % of land |
| 16 / 44 — late spring, early autumn | the alpine peaks (the generated map) | 2 % of mountains |
| 20–40 — summer | below the coldest land | none; coast 25 °C at midsummer |
| 70 — midwinter | the coldest 32 % of land | all mountains, 32 % of land; peaks −12 °C, coast 7.5 °C |

Between knots the line follows a periodic monotone cubic (PCHIP), so it never overshoots a target.

**Snow is cover, not a biome.** `Biome::SNOW` is gone: cold peaks are `MOUNTAIN` with an alpine shade, and the renderer lays snow over whatever ground lies under the snow line today. Chunks never read the date, so props stand on the same tiles all year.

**Trees through the year (built).** A prop's hash picks a **species** once, and the date picks its **look**: in leaf, turning, autumn colour, bare, or snowed under. Each deciduous tree keeps its own leaf calendar: it leafs out on spring days 1–6, turns on year days 41–50, colours 4 days later, and drops its leaves on days 56–62. One tree in five stays green and then drops its leaves without colouring. Evergreens change only under snow: every PC2 pine and fir has a snow-laden frame, drawn by `snow_laden` in `tools/build_overworld_sheets.py` (snow on the top of each bough, the outline untouched). The full-orange autumn frames held back in Phase 4 are live. Bushes follow the same calendar: Pixel Crawler's go olive and then tan or rust, LightBorne's hold green, and every bush stands as a bare shrub all winter. Coastal scrub's trees and bushes stay in leaf all year: the sea keeps the coast mild.

**Ground (built).** Spring adds flowers to grassland, coastal scrub and wetland on a bell curve that peaks mid-spring. From mid-autumn, flowers wither to brown tufts. Snowed tiles take snow decals, and footprints follow the snow.

**Ice (built).** Water freezes under the same snow line: lakes anywhere, rivers only where the land they run through is mountain (moving water freezes last), and swamps and the sea never. A lake's shallows are colder than its middle, so it freezes from the shore in. Ice is one more fade over the water (`ow_ice.png`, a 16-tile square generated by `ice_sheet` in `tools/build_overworld_sheets.py`), so it ends ragged against open water, and frozen tiles lose their glints. It is presentation only for now: water is walkable whether frozen or not until Phase 9 adds swimming.

**Tooling (built).** The debug panel has a calendar (day, season, time, a year slider that scrubs the clock from day 1 of summer to the end of spring, buttons that jump to each season's peak, and a ×1–×1440 clock speed). It also has a climate section: °C, snow and ice at the player, and the share of land under snow. The island map gains a snow and ice overlay and a °C view. The command line takes `--day N`, scenarios take `day N [hour]`, and telemetry carries the clock plus °C and snow at the player. `tests/test_seasons.cpp` covers the calendar, the climate targets, ice and the leaf year; `scenarios/overworld_seasons.txt`, `overworld_snowline.txt` and `overworld_ice.txt` are the visual checks.

**Deferred:** the player-facing HUD clock moves to Phase 13's HUD pass; ice bearing weight comes with swimming in Phase 9.

### Phase 5.5 — Pre-Connection Refactor
Structural work that Phase 6 depends on, from the 06-10-2026 architecture review. No new gameplay.
- **Worlds move into `Run`.** Today `OverworldState` owns the `Overworld` (change record included) and `MazeState` owns the `Maze`, so a state switch would destroy them. `Run` owns both worlds; states only borrow and draw them. The maze must also keep advancing while the player is on the surface (sleep shifts it), so its simulation cannot live only inside `MazeState::update`.
- **Per-system RNG streams.** Gameplay still draws from raylib's global `GetRandomValue` (pass-out teleport, magic-book roll, radiation flicker) beside the seeded `mt19937`s. Give each system its own stream, seeded from the run seed and owned by `Run`, so adding a system never shifts another's rolls and headless replays stay byte-identical.
- **Tooling hardening** *(cheap, any time before Phase 8)*: `-Wall -Wextra` (and `-Werror` once clean) in CMake; pin ImGui and rlImGui to commits; build the game sources once as a static library shared by both executables; a CI job that builds and runs the tests and the headless scenarios; a sanitizer build; move `DebugOverlay` behind an interface so `states/` no longer includes `dev/` and a release build can drop it.

### Phase 6 — Two Worlds Connected
The overworld ↔ maze round trip, and with it a playable core loop.
- **World transition** — `Run` carries the player and inventory between `OverworldState` and `MazeState`.
- **Entrances** (wilderness §5) — 2–4 per maze spawn, **never before Day 20**. Sinkhole (one-way fall) and seasonal entrances first; ventilation stacks need the screwdriver and the freight lift needs power, so those land with their items.
- **Set pieces** — hand-authored prefabs (ruins, standing stones, abandoned cabins, entrances) sited by constraint: biome, height, distance from siblings and water.
- **Escalation anchors** — `discoveryDay` and `entryDay` recorded on first find and first entry (the curve itself is Phase 8).
- **Seeding curiosity** (wilderness §18.4) — let the player know something is underground long before Day 20: a sealed structure, a document, a sound.
- Open: do entrance locations survive the maze's shifting zones?

### Phase 7 — Core Loop: Survival, Sleep, Death & Main Menu
- **First: split `Player` and add an event queue** *(from the 06-10-2026 review)*. `Player` currently holds movement, collision, inventory, recipes, the trip state machine and ten `pollEventX` flags; survival meters and mobs would pile onto it. Pull out a shared entity core (position, collision) that mobs reuse in Phase 8, with vitals and the trip as their own parts; replace the per-flag polling with one typed event queue that states drain. Thin `MazeState::update` the same way (popups and debug triggers out of the gameplay path).
- **Save layer** — a serialization boundary for `Run` (seed, day, player, both worlds' change records). The change-record design keeps this small; it is the prerequisite for the camp save below.
- **Main menu** — start a run, see stats like longest run survived.
- **Health & death** — player damage, death animations, ability/disability icon HUD. Radiation slows health regen.
- **Permadeath in both worlds** — any death resets to Day 0 on a new world seed. Stats screen on death (days survived, % mapped, kills).
- **Hunger, thirst, temperature** — core meters, carried into the maze at an elevated drain rate (wilderness §8). Carry capacity is a balance lever, not a convenience (wilderness §18.2).
- **Sleep** — the bed is on the surface. Sleeping advances the day and shifts the maze's regeneration zone even from home. Sleeping in the maze is a separate, high-risk mechanic. Failing to sleep causes sluggishness, hallucinations, passing out.
- **First surface activity prototype** — one activity (hunting or fishing) built for feel before any breadth (wilderness §18.1: the biggest risk in the design).
- Open: single-slot save at a camp (wilderness §6).

### Phase 8 — Mobs & Maze Escalation
- **First: fixed-step accumulator.** The loop runs one 1/60 s tick per rendered frame, so a frame over 16.7 ms slows the game rather than catching up. Fine at today's ~0.9 ms; switch to an accumulator (still deterministic) before flow fields and mob pools raise the cost.
- **Escalation curve** — three stages anchored on `discoveryDay` / `entryDay` (wilderness §4.2), driving shift aggression, layout confusion, radiation strength/spread and mob spawn rates.
- **Navigation & combat:**
  - Dijkstra flow-field generation.
  - Mob state machine: idle → patrol → track-scent → chase → attack.
  - Scent trail system (per-cell decay value)? — *may remove if too complex.*
  - Basic melee combat (hit detection, health, death), with one starting weapon.
  - Deadly mobs, each with unique characteristics — list to be determined. The `Mobs/` pack is drawn at 32px and must be halved with `tools/downsample_sheet.py` first.
  - Spawning: probably a single jumpscare mob type in corridors, all others in rooms.
  - Flora & fauna — cockroaches, moss, rats, potential bird/bat swarms, a small food chain with territorial fighting.
- **Maze forward camps** (wilderness §11) — must be placed in a room; entrance/exit count is a trade-off; no camps in radiation zones; "There is something off with this room" warning in a regeneration zone.
- **Shifting zone size** — grows as escalation climbs.
- **Cure mechanic** *(may be removed)* — BFS safe-zone spreaders that slightly reduce mobs, radiation and shifting; potential chemical-lab upgrades.

### Phase 9 — Surface Survival Depth
- **Shelter tiers** (wilderness §9) — camp → basic shelter → log cabin, cabins degrade without maintenance. No mega-base; a camp per season.
- **Hunting, fishing, foraging, processing** — drying, curing, tanning, smoking.
- **Surface ecosystem** — discretised Lotka-Volterra predator/prey (wolves and deer) rising and falling across seasons, so over-hunting a valley depletes it.
- **Coast & ocean** — boat, deep-water fishing, scurvy from a poor diet. **Crossing the open ocean** is a survival challenge, and the wrap is a discoverable secret.
- **Visual polish** (wilderness §16.6) — banded lighting, day/night colour ramp, weather (rain, fog, snow — the Tier 0 warning channel), wind sway, animated water flowing downhill, ambient particles, per-biome grading.

### Phase 10 — Two-Tier Crafting & Arsenal
- **Two trees** (wilderness §12) — surface bushcraft (cordage, flint, hide, bone, wood, clay; slow, process-driven) and underworld old-world tech (batteries, wire, scrap, circuit boards, chemicals; fast to assemble, risky to gather). Every required item has a recipe; crafting table vs no table decided here.
- **Durability & repair table.**
- **All fundamental materials & furniture spawns** — scrap metal, wires, wood, cloth, chemicals, electronic components.
- **Light** — fire above; torch/lantern with battery drain below; batteries made only in the maze's **chemical lab**.
- **Barricades & door upgrades** (modify BFS map and radiation spread).
- **Weapons & gear** — melee weapons (baseball bat, nailed bat, machete, throwing spear), armour (protection vs speed), rope (physical link, severable by a mob hit), gas mask / hazmat suit (upgradeable), tripwire flashbangs *(may be removed)*, Geiger counter *(likely removed)*.
- **Cure spreaders** *(if kept)* — alter differential-equation variables in a sector. Strategy: a few heavily upgraded vs dozens of weak ones.
- **Basic audio** — including directional trippy audio under the influence of mushrooms, to help locate the Magic Book of Maps.

### Phase 11 — Extreme Weather, the Dog & Survey Maps
- **Extreme weather** (wilderness §10) — roughly monthly flood / blizzard / heatwave-into-bushfire / storm / drought, each with a built counterplay. Floods fill the lowest ground first — height makes this direct.
- **Weather station** — computer + satellite dish salvaged from the maze, installed in a shelter; Tier 1 prediction, Tier 2 a 10-minute warning readable only at the terminal; draws on the battery economy.
- **The dog** (wilderness §13) — found mid-game on the surface; hunts and tracks above, early warning below, can die permanently.
- **Survey maps & the archive** (wilderness §14–15) — old-world maps of the surface found below; research logs, incident reports, competing apocalypse rumours, and a sparse drip of hints about your wife.
- **Hydroponics** — renewable maze food production.

### Phase 12 — Level 2 Descent & Advanced Maze Systems
Now a third layer below the maze.
- **Level transitions**: screwdriver (found) required to access Level 2 vents.
- **Level 2 architecture**:
  - Located underneath the *inner, non-shifting* sections of Level 1, accessed via corner vents.
  - **[TBD]** What happens if the shifting zone expands inward and swallows a corner vent?
  - Structurally a 1D circular ring. Visually plays as a side-scrolling, never-ending corridor (no floor tiles rendered, only walls/doors).
  - Equally spaced doors with lights above them lead to equally sized "lucky dip" rooms containing unique features, items, or lore.
  - **[TBD] Room shuffling** on sleep — permutation groups or linear congruential generators to showcase a CS concept.
- **Advanced Level 2 items**:
  - Night Vision Goggles (fully lights up all corridors).
  - Pulsar Radar Gun (tracks base, frontal shield; built using L2 crystals).
  - Glitched VHS Tape (circular ring buffer, O(1) state rewind). *Possible mechanic.*
  - Potential toxic waste destroyer?
  - Infected Map — normal map + screwdriver + punctured radiation barrel; temporarily increases radiation effects. *(May be removed.)*
  - Computer Map — full map, fixed to base location, or portable with a live player-tracking minimap?
- **Advanced chemical lab**:
  - Speed drug (faster run, paranoid hallucinations / fake mobs).
  - Parasitic Syringe (rat + cockroach serum; boosts cure spread; severe decay if unmaintained).
- **Symbiotic Infection** — voluntary mutation for buffs at the cost of emitting a biological hum. *(Potential feature.)*

> **Archive Note (Old Level 3 Concepts):**
> Originally, the game planned a "Level 3" with 6:1 spatial compression (like Minecraft's Nether), heavy darkness, no map/radar reliability, and ropes as the only lifeline. To constrain project scope, Level 3 was scrapped. Its unique items (Night Vision, VHS Tape, Pulsar Crystals) were moved into the Level 2 "lucky dip" rooms, and level 2 rooms changed from narrow vents when player forced to crawl to the new level 2.

### Phase 13 — GUI, Graphical Polish & Licensing
*(Finalisation — most should be completed already.)*
- Final tile textures and sprite art (player, mobs, items, UI icons) for both worlds; maybe carpet bitmasking, ritual candles and lighting spawns in the maze.
- HUD design (health, meters, inventory bar, day counter, menus, text pop-ups), consistent across both worlds. Includes the player-facing clock (day, season, time), deferred from Phase 5.
- Screen distortion effects (VHS aesthetic) below.
- Audio polish: ambient buzz, flickering lights, disturbing messages, mechanical maze shifting sounds, hallucination triggers, cryptic clues; surface ambience per biome and season.
- Micro-animations and visual feedback (damage, pickups, cure spreading).
- Menu screens (title, death stats, level transition); finalise fullscreen and window resizing.
- **Licensing** (before the project is shown around or released — the repo is currently public with no licence):
  - `LICENSE` at the repo root: a proprietary, all-rights-reserved notice covering source, **assets, design docs and the game concept**. Not MIT/Apache/GPL — those grant exactly the redistribution rights being withheld.
  - `THIRD_PARTY_NOTICES.md` shipped with builds — Raylib (zlib), Dear ImGui (MIT), rlImGui, GoogleTest (BSD-3) are permissive, but MIT and BSD-3 require their notices in binary distributions.
  - A copyright line in `README.md`. Consider making the repository private until release — a licence deters, it does not prevent forking a public repo.

### Phase 14 — Telemetry & Python Analytics *(Stretch)*
Data science showcase, building on the headless harness's telemetry.
- Logger: frame time, entity count, radiation %, cure %, maze entropy, days survived.
- Export triggers (death, manual).
- Jupyter notebook: parse logs, plot time series.
- Data on algorithm run time (for Big O analysis in report).
- Discrete derivatives (radiation rate of change).
- Riemann sum (cumulative exposure integral).

### Phase 15 — README, Report & Portfolio Polish
Presentation layer.
- README.md with GIF hook, architecture diagrams, 3-min summary.
- Academic report: Big O proofs & analysis, Python charts, algorithm analysis.
- Code cleanup, commenting, final profiling pass.
- **Thorough code review**

---

## 4. Full Items Reference

All items organized by category. **World** says where an item is found or used: *Surface*, *Maze* (Level 1), *Maze L2*, or *Both*.

### Navigation Items
| Item | Description | World |
|---|---|---|
| Torch/Lantern | Light source. Requires batteries. Upgradeable range (circle size 3.5 → 4.5 → 6; potentially blinds mobs at max level). | Maze |
| Fire | Renewable, trivial light and warmth. Nothing burns below. | Surface |
| Classic Map | Crafted with pen & paper. Upgradeable once to increase range. Permanently true on the surface; rots in the maze as it shifts. | Both |
| Magic Map | Dynamically updates to reflect maze changes at night. Rare — only found under the effects of Magic Mushrooms. Fixed region. | Maze (rare) |
| Survey Map | Old-world map of the surface found in the bunker archive — resource caches, water, old trails. | Found in Maze, used on Surface |
| Infected Map | (Potential) Shows radiation zones on map. | Maze |
| Computer Map | Final map, all maps combined. | Maze |
| Rope | Physical link between locations. Can be severed by direct mob attacks. | Maze |
| Pulsar Radar Gun | Tracks base signals through shifting walls. Pings enemies from afar. Frontal shield. Built using crystals. | Maze L2 (crafted) |
| Geiger Counter | Passive belt item. Clicks near un-destroyed radiators. *(Likely to be removed)*. | Maze |

### Weapons & Defence
| Item | Description | World |
|---|---|---|
| Flint Spear | Cheap, renewable, weak. | Surface (crafted) |
| Baseball Bat | Basic melee weapon. | Maze |
| Bat with Nails | Upgraded melee weapon. | Maze |
| Sword / Machete | Superb melee weapon; wears down and is maintained only with maze parts. | Maze |
| Throwing Spear / Trident Fork | Ranged melee / throwing weapon. | Both |
| Armour | Increases protection, reduces movement speed. | Maze |
| Gas Mask / Hazmat Suit | Protects player from infection/gases. Uses gas mask icon, turns player texture into gas suit man. | Maze |
| Tripwire Flashbangs | Defensive traps. *(May be removed)*. | Maze |
| Barricades | Wood / reinforced wood. Blocks enemy BFS pathfinding. Upgradeable. | Maze (crafted) |
| Doors | Wood / Metal / Reinforced. Placed in room exits to secure camps. Upgradeable to block radiation. | Maze (crafted) |

### Unique Gear & Companions
| Item | Description | World |
|---|---|---|
| Dog | Found mid-game. Hunts and tracks above; alerts to flanking enemies and tracks scent below. Can die permanently. (Pitbull variant actively attacks.) | Surface, taken below |
| Boat | Flood counterplay, deep-water fishing, open-ocean crossing. | Surface (crafted) |
| Weather Station | Computer + satellite dish salvaged below, installed in a shelter. Tier 1 prediction; Tier 2 10-minute warning at the terminal. Draws battery power. | Salvaged in Maze, used on Surface |
| Screwdriver | Required to open ventilation-stack entrances and access Level 2 vents. | Maze |
| Night Vision Goggles | Fully lights up all sections of corridors. | Maze L2 (found) |
| Glitched VHS Tape | Consumable. 30s–10m recording. Rewinds player state to recording start. O(1) ring buffer. *(Possible mechanic)*. | Maze L2 (found) |

### Chemicals, Drugs & Bio-Boosters (Crafted in the Chemical Lab)
| Item | Description | World |
|---|---|---|
| Batteries | Made only in the maze's chemical lab. Light is life below. | Maze (crafted) |
| Speed | Faster run speed. Mobs slightly faster. Increases paranoid hallucinations (fake mob spawns). | Maze (crafted) |
| Magic Mushrooms | Maze regenerates at shifting zone every 1.5 mins even if awake. Required state to find the Magic Map. | Maze (found/farmed) |
| Parasitic Syringe | Crafted from rat + cockroach serum. Injected into friendly dispersers for faster and wider cure spread. Severe decay rate if unmaintained. | Maze (crafted) |
| Potential LSD | | Maze |

### Structures
| Item | Description | World |
|---|---|---|
| Camp / Shelter / Log Cabin | Surface shelter tiers. Cabins must be maintained across seasons. Likely save points. | Surface (built) |
| Bed | Sleeping advances the day and shifts the maze's regeneration zone. Mandatory — failing to sleep causes sluggishness, hallucinations, passing out. | Surface (crafted); bedroll for risky maze sleep |
| Forward Camp | Stash, barricaded room, light, bedroll — securing a route, not a home. | Maze |
| Cure Spreaders (Chemical Dispersers) | Alters differential equation variables in a sector, slowing/halting radiation spread and enemy spawns. Upgradeable. *(May be removed.)* | Maze (crafted) |
| Hydroponic Farm | Renewable food production, only possible underground. | Maze (crafted) |
| Chemical Lab | Produces batteries and chemicals. | Maze (built) |
| Repair Table | Repairs tools rather than losing them to durability. | Both |

---

## 5. Algorithmic Ideas & Theoretical Showcase

These are **starting points, not final decisions** — each will be researched and evaluated before implementation. Algorithms are grouped by the system they serve.

### Memory & Data Architecture
| Concept | Approach | Used For |
|---|---|---|
| **Implicit Grid & DOD** | Store maze state as an implicit graph in a 1D flat array (`std::vector<int>`). Edges are inferred mathematically (`y * width + x`) rather than pointers, maximizing CPU cache locality. Entity pools use SoA. | Maze grid, entity pools |
| **Toroidal Wrapping (Modulo Math)** | Modulo arithmetic `(x % width + width) % width` turns a fixed grid into a torus. | Maze "infinite" grid, the overworld's wrapping ocean, Level 2 circular buffer |
| **Two-Resolution World** | A coarse grid (1 cell per 8×8 tiles) holds whole-island data built once; fine detail is regenerated per chunk from `hash(seed, chunkX, chunkY)` and never stored. Player changes live in a sparse per-tile map re-applied on regeneration. | Overworld storage — memory independent of world size |
| **Zero-Allocation Ring Buffer** | Fixed-size circular buffer continuously tracks minimal struct of player state (position, health, inventory flags). Instant rollback in O(1). | VHS Tape time-rewind mechanic |

### World Generation & Topology
| Concept | Approach | Used For |
|---|---|---|
| **Hybrid Procedural Generation** | BSP ($O(R \log R)$) for office rooms + Prim's ($O(V \log V)$) for highly-branched spanning tree corridors + Loop Injection + Connected Component Pruning (identifying and stripping dead-end alcoves). | Maze layout |
| **Falloff Island Mask** | Land score = noise − falloff(distance from centre), thresholded. The score doubles as height: mountains in the middle, coast where it crosses the threshold, islets for free. | Overworld island shape and height |
| **Downhill Drainage & Priority-Flood** | Rivers walk to their lowest neighbour; pits are filled into lakes that overflow at their lowest rim, so every river ends at a lake or the ocean. | Rivers and lakes |
| **Whittaker Biome Lookup** | A 2D table: temperature × moisture → biome. Temperature falls with height, so alpine peaks need no special case. | Overworld biomes |
| **Quantile-Calibrated Season Curve** | Snow-line key values read off the island's empirical temperature CDF (a sorted sample), joined by a periodic monotone cubic (PCHIP, Fritsch–Butland slopes) that cannot overshoot. | Seasonal snow line and °C |
| **Poisson-Disc Sampling (Bridson)** | Random placement with a guaranteed minimum spacing (blue noise), O(n). Chunk-boundary aware by regenerating neighbours' points. | Trees, rocks, resources, set pieces |
| **Doorway Node Graphs** | Pre-spawned doors isolate rooms and corridors, abstracting the cellular grid into a graph of connected sectors. | Faster pathfinding, localized radiation spread |
| **Mathematical Shuffling** | (TBD) Permutation groups or linear congruential generators to predictably shuffle the Level 2 room order based on the day index. | Level 2 room logic |

### Macro Simulations & Territory
| Concept | Approach | Used For |
|---|---|---|
| **Event-Anchored Escalation** | Piecewise curve: zero until discovery, linear from `discoveryDay`, banked linear + `B(e^{C(day−entryDay)} − 1)` from `entryDay` — continuous by construction. | Maze difficulty |
| **Mathematical Population Modelling** | Discretised non-linear Lotka-Volterra (predator-prey) differential equations. | Surface wolves vs deer across seasons; possibly radiation vs cure |
| **Frontier Expansion Search** | Bidirectional BFS as spatial execution engine. Maps the boundary of competing forces across the grid, modifying local cost-maps. | Radiation/cure territory expansion |

### Agent Routing & Navigation
| Concept | Approach | Used For |
|---|---|---|
| **Deterministic Flow-Field Routing** | Centralized discrete vector field generated via Dijkstra's algorithm. Agent updates via discrete approximations (Euler's method). Each agent gets optimal movement vector via O(1) matrix lookup. | Mob navigation, entity pathfinding |
| **Dynamic Shortest Path Routing** | Real-time graph traversal and edge-weight evaluation in a continuously mutating network. Vector magnitude calculations and dot products for relative positioning and alignment. | Pulsar radar gun, base tracking through shifting walls |

### Rendering
| Concept | Approach | Used For |
|---|---|---|
| **Y-Sorted Render Queue** | One drawable queue sorted by base Y in world pixels; bucket sort since base Y is bounded by canvas height, O(k). | Interleaving player, trees, mobs, furniture in both worlds |
| **8-bit Autotiling** | Corner-aware 47-tile "blob" bitmask, extending the maze's 4-bit cardinal mask. | Biome and coast borders |

### Telemetry & Analysis
| Concept | Approach | Used For |
|---|---|---|
| **Discrete Derivative** | Calculate growth rate (first derivative) and acceleration (second derivative) from time-series data. | Runtime performance analysis |
| **Riemann Sum Integration** | Area under the curve representing total cumulative radiation exposure over a run. | Cumulative risk evaluation |
| **Heuristic vs. AI Comparison** | Empirically compare hand-coded search algorithms against trained Neural Network pathing models. | Portfolio showcase (future) |

### Mathematical Foundation Summary
The engine is driven by the intersection of **discrete mathematics**, **applied linear algebra**, and **numerical calculus**:
- Graph theory and modulo arithmetic form the structural foundation
- Noise, falloff and drainage turn a seed into terrain; lookup tables turn terrain into biomes
- Discretised non-linear differential equations (Lotka-Volterra) and centralized discrete vector fields govern real-time behaviour
- Continuous mathematical models bridged to discrete execution via Euler's method
- Deterministic probability theory (PRNG state spaces, seeded chunk hashing) underpins state generation
- Discrete derivatives and numerical integration (Riemann sums) evaluate runtime telemetry

---

## 6. Future Features

Features planned for after the core game is complete. Not in the current scope.

| Feature | Notes |
|---|---|
| **In-game achievements** | First: *cross the open ocean*. Others to be decided (days survived milestones, first dive, a winter in the open…). |
| **Controller support** | Gamepad input alongside keyboard |
| **Cross-platform (Linux/Mac)** | CMake already supports this; needs testing and CI setup |
| **Advanced AI opponents** | Reinforcement learning for dynamic difficulty |
| **LAN multiplayer** | Two-player co-op and versus modes |
| **Concurrency & parallelism** | Multi-threading, mutex design, parallel computation for performance |
| **ONNX / CUDA AI integration** | Neural network decision logic offloaded to GPU |
| **Heuristic vs. AI comparison** | Empirical comparison of hand-written pathfinding vs. trained NN models |
