---
name: headless-test
description: Build the game, drive it through a scripted scenario with no window, and judge a feature or asset by its telemetry.json and screenshots. Use to verify a gameplay change, a renderer change, or a new sheet in assets/ without anyone at the keyboard.
---

# /headless-test <feature-or-asset>

Verify `$ARGUMENTS` by running the game headless and reading what it wrote.
A pass cites a telemetry field and an image; "looks fine" is not a result.

## 1. Guard the build tree

```bash
grep "CMAKE_GENERATOR:" build/CMakeCache.txt
```

Must say `MinGW Makefiles`. If it says anything else (usually `Visual Studio`),
the VS-bundled cmake hijacked the tree. Recover — delete **only** the top-level
cache, leave `build/_deps/` alone — then configure through the preset:

```bash
rm -rf build/CMakeCache.txt build/CMakeFiles && cmake --preset mingw-debug
```

If `assets/` changed since the last configure, run `cmake --preset mingw-debug`
regardless: assets are copied at configure time, a build alone will not
refresh `build/assets/`.

## 2. Build

```bash
PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH" cmake --build --preset mingw-debug
```

The `PATH` prefix matters from Git Bash: both executables link MinGW's
`libstdc++-6.dll`, and `gtest_discover_tests` runs the test exe at link time.
Without it the build silently deletes `BacklandsTests.exe` and the game dies
with exit 127 and no output.

## 3. Pick or write a scenario

Existing scenarios in `scenarios/` and what each proves:

| scenario | seed | proves |
|---|---|---|
| `smoke` | default | walking, inventory toggle, nothing crashes |
| `pickup` | 3 `mushroom_room` | 3×3 pickup scan, stacking, a miss leaves the bag alone |
| `corridor` | default | door transition, corridor light mask |
| `furniture` | 1 `furniture_room` | tables and cupboards for a `scene.png` presentation check |
| `barrel` | 38 `barrel_room` | toxic barrel glow and vertical table, presentation only |

Reuse one when it already exercises the feature. Otherwise write
`scenarios/<name>.txt` against the grammar in `src/dev/scenario.hpp`
(`seed`, `window`, `scale`, `wait N`, `hold KEY N`, `press KEY`, `mouse X Y`,
`click X Y`, `checkpoint NAME`, `end`; keys `W A S D UP DOWN LEFT RIGHT K L P
I O U Q 1-5`). Rules that decide whether a scenario is worth anything:

- **Pin the world**: `seed`, `window 1280 720`, `scale 1.5`. Named fixtures
  are in `src/dev/debug_seeds.hpp`; add one there when you find a world worth
  returning to.
- **Write the prediction beside each checkpoint** as a `#` comment
  (`# slot 0 MUSHROOM x1; four left visible`), the way `pickup.txt` does.
  Step 5 checks those predictions; a checkpoint without one is a screenshot,
  not a test.
- A `checkpoint` is one idle tick, so it records the world after the previous
  command has settled and before the next one starts. Movement is 130 px/s at
  1/60 s per tick: `hold S 16` is about one cell.

## 4. Run

```bash
cd build && PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH" ./Backlands.exe --headless ../scenarios/<name>.txt --out ../artifacts/<name>
```

Outputs land in `artifacts/<name>/`: `run.json` (seed, ticks, checkpoint
list) and per checkpoint `telemetry.json`, `scene.png` (canvas, pre-shader,
pre-UI), `frame.png` (window, everything). A 200-tick scenario takes well
under a second; a run that hangs is a bug, not a slow machine.

## 5. Assert against telemetry

Read each `telemetry.json` and check the scenario's predictions as exact
values. The fields:

- `player.cell` `[x, y]`, `player.areaState` `ROOM|CORRIDOR`,
  `player.facing`, `player.mushroomEffect`, `player.passingOut`
- `camera.rect` (world px), `camera.canvas`, `camera.blitScale`
- `ui.inventoryOpen` / `cupboardOpen` / `fullscreenMapOpen`
- `inventory[]` `{slot, type, count}`
- `visibleItems[]` `{cell, type}` — what the canvas would draw this tick
- `maze.nonWallCount`, `corridorCount`, `regenCount`
- `frameCaptured` — `false` means the window read-back failed; judge on
  `scene.png` only

Compare numbers, not impressions: the bag has `count: 2`, or it does not.
For a presentation-only change, telemetry must be **byte-identical** to the
previous run — that is the test that no gameplay moved:

```bash
diff -r artifacts/<name>_before artifacts/<name> --exclude=*.png
```

For a determinism check run the scenario twice into two `--out` dirs and
`diff -r` them; any difference at all is a failure.

## 6. Look at the images

Open `scene.png` and `frame.png` with the Read tool for every checkpoint that
matters and say what is actually in them. Things worth knowing before
judging:

- `scene.png` is canvas-sized (`window / scale`, so 854×480 by default) and
  every art pixel is a 2×2 texel block; a smeared or half-pixel edge is a grid
  bug in a draw site.
- A near-black corridor `scene.png` is usually the radiation flicker peaking
  on that tick, not a broken light mask. Check the checkpoint before or
  after it.
- The trip shader, UI, popups and the magic-book overlay appear only in
  `frame.png`.
- New sheets must already be on the palette:
  `python tools/quantize.py --check assets/` must report 100 %.

## 7. Report

One line per checkpoint: PASS/FAIL, the telemetry field that decides it, the
image that shows it. Then a verdict for `$ARGUMENTS` as a whole. If something
failed, say which prediction was wrong and what the run produced instead — do
not edit the scenario's prediction to match the output unless the prediction
was the bug.
