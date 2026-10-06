# Overworld rendering

How `OverworldRenderer` (`src/render/overworld_renderer.*`) and `StepEffects` turn the
island's per-tile classification into the picture. The world side (terrain, `IslandMap`,
chunks) is summarised in CLAUDE.md; every `ow_*.png` sheet named here is built by
`tools/build_overworld_sheets.py`, whose banner explains how each was cut or generated.

## Tiled layers on a dual grid

`OverworldRenderer` draws the ground in two ways. **Tiled layers** (ground, grass, the shade overlays) are autotiled on a **dual grid**: each tile sits on a cell corner and picks one of 16 shapes from the 4 cells meeting there (`cornerMask`, TL=8 TR=4 BL=2 BR=1). The ground layer's edge is chosen per corner: High Tides' sand-and-foam coast (animated off `m_totalTime`) where ocean or beach meets it, else a generated mud bank (LightBorne's shapes grown 4 px into lakes and rivers) that pales step by step toward tan near a beach or coastal scrub (whose banks are sand). A layer must also cover the cells of any layer that sits on it, or a rim of the layer below shows.

## Shade overlays

**Shade overlays** (`ow_shades.png`: grass → meadow → forest → deep forest) are recoloured grass that dithers out along LightBorne's wobble; their corners have three states (off grass / grass / shaded, 81 tiles per overlay) so they fade into grass but cut cleanly against water.

## Fades and swamp water

**Fades** (wetland, gravel, snow, drifts, dune sand) have no tiles: `drawFade` draws each as one quad over the view through `assets/ow_fade.fs`, which keeps a pixel where the material's per-cell weight, read bilinear, beats a clumpy noise-plus-Bayer threshold - so any two blend over ~5 tiles. The weights are rebuilt per frame into two small textures (`buildFades`): their share of the grass cells within `kFadeRadius` (`shareWithin`, summed-area tables). Swamp water goes through the same shader in four tint passes, lake blue toward murk (`ow_swamp_water.png`, one square per tint): its weight is a cell's depth through water from open water (`swampDepth`, capped BFS, over `kSwampReach` = 8 tiles), read bilinear and nudged by broad noise, and pass k draws the k-th quarter of it under the land fades' clumpy threshold - so the colour drifts in patches. Land fades are clipped to the grass's LightBorne shape at the shore and take their own outline colour there (`ow_fades.png` holds the fills, outline colours and coded corner shapes). Weights only ever nest (snow ≤ gravel, drift ≤ snow), so snow never draws off its gravel. Dune sand is the exception to "share of the grass": its weight is dune beach and sand-patch cells' share of all dry cells within `kDuneRadius`, doubled and capped, so it is solid at the beach's edge and the grass's cut edge vanishes under it - coastal scrub dithers into its beach while every other beach keeps a sharp edge. COASTAL sits in the MEADOW overlay, so its floor fades into grassland and forest with no extra tiles.

## Water, glints, decals and wet roots

River water is `ow_river.png`, repeat-wrapped and sampled at world position shifted downstream by time along the tile's `flow`; still water (`ow_water.png`, and `ow_swamp_water.png` in the shader) is sampled the same way, and lakes, swamps and the sea sway it a px or two along the diagonal (`swayAt`). A few open-water tiles glint (`drawGlints`, `ow_glints.png`). Ground details (`decalFor`: flowers, tufts, mushrooms, pebbles, snow-capped stones, sticks, snow mounds, ice, shells, river stones near a shore) are render-only, picked per tile by hash and drawn flat after the layers. Where one of the widest-rooted trees (willow, the biggest Pixel Crawler oak - `LAPPING` in the build script) spills its roots onto a river, that slice laps through dry / foam line / sunk / foam line (`ow_props_wet.png` holds the foam and sunk copies); on still water roots draw as they are.

## Props

Props go through the shared `DrawQueue`; the world only says TREE/PINE/BUSH/ROCK/REEDS plus a hash byte, and `OverworldRenderer::spriteFor` picks the species from a per-(kind, biome) weighted pool — species becomes world data when gameplay needs it. Sprites are up to 16 tiles tall, so `collect` scans `kReachBelowTiles` rows below the screen and the queue's range is widened to match; a prop covering the player draws faded (`setFocus`). `theme::ocean`…`theme::snow` now only paint the debug island map.

## Step effects

**Step effects** (`render/step_effects.hpp`, owned by `OverworldState`): `PlayerRenderer::pollFootfall()` is raised on every walk frame, and `StepEffects` drains it, leaving a boot print (toe toward the way walked) on snow, a water squish on wetland, or a kick of sand grains on a beach or in coastal scrub (pale grains on its grass), from `ow_steps.png`. Marks sit in a fixed 256-slot ring buffer stamped with their birth time, so a tick is O(1); prints half fill at 20 s and go at 30 s. They are presentation only - not world data - and draw flat after the terrain, while a spray (squish or kick) joins the `DrawQueue` one row past the sole that made it.
