# Master palette

**Status:** approved 2026-09-11 (`tools/palette_sample.py --config yellow12`, 56
colours); extended for the overworld 2026-10-06 (+16, 72 colours, see
"Overworld additions") and for the grass-to-forest fade the same day (+2, 74).
Every PNG in `assets/` is quantized to it; `python tools/quantize.py --check assets/`
is the acceptance test and must stay at 100 %.

## Working with it

- **The palette is [palette.json](../assets/palette.json).** The table below is a copy for
  reading; the JSON is what `quantize.py` reads. `palette_swatch.png` is the
  labelled picture of it; `palette_strip.png` (one pixel per colour) is the
  file to import into Aseprite or GIMP so you paint with it.
- **Adding a sheet:** `python tools/quantize.py <src.png> assets/<name>.png`, then
  reconfigure cmake. Sources stay outside `assets/` (the packs, or a generated
  file) so a later re-quantize starts from the original, not from a
  quantized copy.
- **Hand-editing a sheet:** paint with palette colours only, then `--check`.
  A stray colour is a build defect, not a style choice.
- **The C++ side** never spells a colour. `src/core/palette.hpp` is generated
  from the JSON by `python tools/palette_header.py`; `src/render/theme.hpp`
  maps roles (`ink`, `border`, `highlight`, `good`, `bad`, `radiationGlow`…)
  onto ramp steps by hand. UI and renderers name a role. The test
  `PaletteHeader.MatchesStrip` fails if the header and the strip disagree.
- **Changing the palette** — a new hex, a moved step: edit `palette.json`, run
  `palette_sample.py --render` and `palette_header.py`, then re-quantize every
  sheet from its source and reconfigure. The sources are
  listed in the "Provenance" section below. To *re-derive* the palette from
  new material (a new pack): add it to `SOURCES` in `palette_sample.py`,
  re-run, and compare the swatch against this one before adopting it — the
  sampler proposes, it does not decide. Mind that `assets/` is now quantized:
  a re-run that samples it reads the palette back and drifts by a few hexes,
  so a re-derivation must sample the original sheets. To redraw the images
  from the JSON without sampling: `python tools/palette_sample.py --render`. Adding an entry is cheap; renumbering
  a ramp is not once the §16.6 lighting depends on step indices.
- **Overworld sheets** (`ow_*.png` + `src/render/overworld_sprites.hpp`) are built, not quantized one by one:
  `python tools/build_overworld_sheets.py` crops the chosen pack regions,
  applies the recorded edits (High Tides' sea cut out, the red pine
  recoloured) and quantizes. Change the selection there, never in `assets/`.
- **Generated art** (`/generate-asset`, Phase 7): PixelLab takes a forced
  palette, then the result goes through `quantize.py` anyway so the check
  cannot be argued with.

### Provenance of the shipping sheets

| Sheet | Source |
|---|---|
| `BCKRMlv1_*_set.png` | `../Asset packs/Backrooms Lvl1 {Tileset}/` |
| `PostApoc_Workshop_16px.png` | `tools/downsample_sheet.py` over `../Asset packs/PostApoc_Workshop/PostApoc_Workshop_WithShadow.png` |
| `PostApoc_Workshop_Icons.png` | `../Asset packs/PostApoc_Workshop/` |
| `Spritesheet_TheDarkRitual_BigWander.png` | `../Asset packs/BigWander_TheRitual/` |
| `guard_yellow_spritesheet.png`, `inspector_spritesheet.png` | `../Asset packs/sci-fi-facility-asset-pack/` |
| `mushrooms_pixel_asset.png` | PixelLab pass over the `mushrooms` pack (Phase 5); the pre-quantize file is the git version at `d82ab87` |
| `workshop_prop_icons.png` | cut from the workshop furniture sheet (Phase 5); pre-quantize at `d82ab87` |
| `ow_water/coast/terrain/props.png` | `tools/build_overworld_sheets.py` over `../Asset packs/{Pixel Crawler, High Tides, LightBorne 1.1v, Tiny Wonder Swamp}/`: the water is generated, the inland edge tiles are each material's fill cut to LightBorne's corner masks, and every prop is repacked onto the 16px grid (`--dump <dir>` writes the individual quantized crops for review) |

Evidence from the gate:

- [palette_swatch.png](../assets/palette_swatch.png) — the ramps.
- `artifacts/palette_preview/side_by_side.png` — three scenes rendered through
  the current sheets and through sheets quantized to each candidate palette.
- `artifacts/palette_preview/carpet_zoom.png` — 4× of the carpet and a table,
  which is where the candidates differ most.

## The palette (74 colours, 7 ramps)

Every hex is a colour that exists on a source sheet - nothing is invented.
Steps run dark → light, step 0 first. 56 came from the sampler; the 16 marked
in "Overworld additions" were added for the surface, and two more for the
grass-to-forest fade (the only colours here not taken from a source sheet).

| Ramp | Steps | Hexes |
|---|---|---|
| neutral | 9 | `#141517` `#2e2e2e` `#464646` `#626262` `#7e7e7e` `#9c9c9c` `#bdbdbd` `#d9d9da` `#dfe3ed` |
| grey | 12 | `#1d1c27` `#2b2b45` `#383847` `#474858` `#41564b` `#4b4d71` `#576477` `#717d8f` `#829da5` `#a6b7c6` `#b4c4e4` `#bdd5de` |
| brown | 13 | `#2f190b` `#4a2a19` `#663b27` `#7d4c2e` `#936340` `#ad5226` `#927e65` `#b17a4e` `#d06732` `#c98321` `#cc9770` `#f4a568` `#d8b289` |
| yellow | 13 | `#382e16` `#483a20` `#544527` `#665932` `#74653c` `#87784a` `#96894e` `#a79757` `#b4ac6a` `#ccbe68` `#e8d282` `#edd19a` `#f6e998` |
| green | 14 | `#0f390f` `#164e19` `#065c39` `#3c6723` `#1b7758` `#53763e` `#3d7f41` `#55834c` `#6a8657` `#76963c` `#7b9664` `#69a754` `#91ca51` `#b7f074` |
| blue | 7 | `#152d5c` `#2f4876` `#0f5f5b` `#3b6590` `#4e91af` `#6ea7c6` `#91d6e8` |
| accent | 6 | `#6b2643` `#063ee6` `#b02a2a` `#f43636` `#cc99ff` `#95da41` |

Machine-readable copy: [palette.json](../assets/palette.json) (OKLab L/C/h per entry).

### One palette for both worlds

Both worlds may draw any colour. An earlier version reserved the darkest
quarter of each ramp for the maze and the brightest for the surface ("world
windows"). It was dropped on 2026-10-06: nothing enforced it, the surface art
already broke it (every Pixel Crawler tree is outlined in `#2f190b`), and
because a window was a fraction of ramp *position*, adding colours to a ramp
silently moved colours across the line. The difference between the worlds is
lighting's job - the maze is dark because of its torch mask, and the surface's
day/night tint lands with Phase 5 - not the palette's.

Measured instead of prescribed: maze sheets use 55 colours, overworld sheets
68, both 51, and every one of the 72 is used by some shipped sheet (measured
before the fade greens; those two are used only by ow_shades.png).

## How it was built

1. **Sample.** Every PNG in `assets/` and in seven packs (`Backrooms Lvl1`,
   `Backrooms Items`, `PostApoc_Workshop`, `BigWander_TheRitual`,
   `sci-fi-facility`, `Mobs`, `Pixel Crawler`), pixels with alpha ≥ 128.
   `assets/` carries 50 % of the weight; the packs split the rest evenly, so
   Pixel Crawler's 2.6 M pixels cannot outvote the 115 k that ship. Promo
   renders (thousands of colours) are skipped. 2 984 distinct colours in.
2. **OKLab.** Distances and "evenly spaced" are measured there, not in sRGB,
   because sRGB distance does not match what the eye sees.
3. **Families** by chroma and hue: neutral (C < 0.02), grey (cool tint,
   C < 0.06), then hue bands brown 20–75°, yellow 75–120°, green 120–185°,
   blue 185–290°. Anything with C ≥ 0.17, or in the red/magenta gap, is an
   accent.
4. **Ramps** are straight lines through OKLab, fitted per family by weighted
   least squares of (a, b) on L, sampled at equal L steps, each sample snapped
   to the nearest real sheet colour. Accents are a weighted k-means (fixed
   seed), medoid-snapped.

## What the sampler found

**Yellow is the game.** In `assets/`, hue 75–120° (the wallpaper and carpet
ochres) is a third of every chromatic pixel; green and blue together are under
5 %. The plan's table had no yellow ramp — it folded ochre into brown. Tried
that (`--config plan`, 46 colours): the brown line then runs from red-brown to
ochre and is right for neither, and the carpet quantizes brown. The proposal
splits yellow out and gives blue 6 steps instead of 8 to pay for it.

**8 steps flatten the carpet.** The Backrooms floor texture is built from
shades 0.02–0.04 L apart (`#6e5d3a` `#796a3e` `#817042` `#87784a` `#8a7e4b`).
An 8-step ramp across L 0.31–0.89 is 0.08 per step, so those five collapse to
one and the weave disappears — `carpet_zoom.png`, columns 2 and 3. This is the
"quantization can flatten intentional gradients" risk from the plan, and it
is real on the single most visible tile in the game. 12 yellow steps (0.05
each) keep the weave and most of the table grain. Hence 56, not 46.

**Quantization error, `assets/` only** (mean OKLab distance to nearest palette
entry; 0.02 ≈ just noticeable):

| Config | Colours | Mean ΔE | Pixels > 0.05 | Pixels > 0.10 |
|---|---|---|---|---|
| plan | 46 | 0.041 | 23 % | 12 % |
| yellow | 52 | 0.046 | 20 % | 12 % |
| yellow12 | 56 | 0.045 | 18 % | 12 % |

The 12 % over 0.10 is the same in all three: it is the workshop sheet's
saturated oranges and the sci-fi guards' uniforms, which no 50-colour palette
built from ochre and grey will hold. They will shift; that is the point.

**Accents are the data's, not the plan's.** The k-means picked maroon, pure
blue, dark red, red, lavender, lime. The plan asked for fire, radiation, UI
alert, item highlight. Lime (`#95da41`) is the radiation green; red is the
alert; there is no fire orange and the blue/lavender come from the elevator
set and the ritual sheet. Hand-picking the six accents is reasonable — they
are semantic, and six colours is not a sampling problem.

**Telemetry is unchanged** across all preview runs (12/12 checkpoint files
byte-identical to the current build), as a presentation-only change must be.

## Overworld additions (2026-10-06)

Sources: Pixel Crawler (ground, water, most trees), High Tides (coast and
sand), LightBorne (forest floor, oak/birch/fir), Tiny Wonder Swamp (wetland,
willows). Every tree variant of every pack ships. Colours were judged per
sheet as original | 56-colour | proposed triptychs plus mean OKLab error and
the share of pixels over 0.05 (`artifacts/phase4_review/final/`, gitignored).

Rules a new colour had to pass: it fixes something visible in a sheet that
ships; it is ≥ 0.04 OKLab from every other entry (two deliberate exceptions:
`#dfe3ed` snow white and `#edd19a` sand, ~0.03 from `#d9d9da` / `#e8d282`,
which are the distinct tints asked for); and dropping it visibly breaks a
sheet. The existing 56 were not moved, so every maze scenario is
byte-identical.

| Added | Ramp | For |
|---|---|---|
| `#2b2b45` | grey | dark navy outlines (LightBorne, willows) |
| `#41564b` `#4b4d71` `#b4c4e4` | grey | LightBorne fir; LightBorne winter slate; snow / winter blue |
| `#dfe3ed` | neutral | snow white - keeps the blue-white snow texture |
| `#edd19a` `#d8b289` | yellow, brown | sand, a quarter of the way from High Tides' peach to `#e8d282`; its darker edge |
| `#927e65` | brown | mountain gravel (taupe, was khaki) |
| `#ad5226` `#d06732` `#c98321` | brown | autumn rust, orange, amber |
| `#065c39` `#1b7758` | green | deep green; the teal LightBorne canopies take |
| `#6a8657` `#7b9664` | green | LightBorne olive greens |
| `#0f5f5b` | blue | Pixel Crawler's teal pines |
| `#55834c` `#53763e` | green | the grass-to-forest fade (ow_shades.png): the OKLab midpoint of grass `#3d7f41` and forest floor `#6a8657`, and of grass detail `#3c6723` and `#6a8657`. Mixed, not sampled; approved over a no-new-colours version after side-by-side screenshots |

Deliberate effects, kept by leaving colours *out*: Pixel Crawler grass and
water quantize exactly as on the 56 (no bright grass green, no extra water
blue); the swamp willows go lime (no emerald). Water texture is a generated
sheet, not a palette colour. The autumn red was removed by editing the art
instead - Pixel Crawler's red pine is recoloured to `#ad5226` in the build
script - so the palette carries no colour that only one sheet wanted.

## Gate outcome

Approved as proposed after a hand-play with the quantized sheets swapped into
`build/assets/`: 7 ramps / 56 colours, sampled accents kept. Extended to 72
for the overworld on 2026-10-06 (above).
