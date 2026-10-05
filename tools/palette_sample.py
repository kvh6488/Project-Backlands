#!/usr/bin/env python3
"""Propose a master palette from every sheet the game draws or might draw.

Usage:
    python tools/palette_sample.py [--config plan|yellow|yellow12] [--out assets/palette.json]
                                   [--swatch assets/palette_swatch.png] [--strip assets/palette_strip.png]

Why: assets/ holds 828 distinct colours from five artists with five different
greens, greys and blacks, so the sheets read as five places. The plan calls for
~46 colours in named ramps that every asset is later quantized to. This tool
proposes those colours from the art we already have rather than inventing them:
every hex it emits is a colour that exists on a sheet today. The output is a
proposal for a human to approve, not a decision.

How, in four steps:

1. Sample. Every PNG in assets/ and in the listed packs, weighted by pixel
   count, alpha >= 128 only (shadows and antialias fringes are not colours we
   want to keep). Each source group is normalised to a fixed share so the
   shipping assets decide most of the ramp, and no pack can swamp it just by
   having more sheets. Files that are previews (thousands of colours) or mockups
   are skipped.

2. Convert to OKLab. Euclidean distance in sRGB is not how eyes see - two
   greens 30 units apart can look identical while two greys 30 apart look like
   different materials. OKLab is (almost) perceptually uniform, so "nearest
   colour" and "evenly spaced steps" mean what they say. L is lightness,
   chroma C = |(a, b)| is saturation, hue h = atan2(b, a).

3. Assign each colour to a family by chroma and hue (the CONFIGS table).
   Neutral is anything nearly grey; grey is a cool-tinted grey (a warm tint
   at low chroma is a dusty brown and belongs to that ramp); the chromatic
   ramps are hue bands; anything more saturated than ACCENT_CHROMA, or in the red /
   magenta gap between the ramps, is an accent.

4. Build each ramp as a straight line through OKLab: fit (a, b) as a linear
   function of L over the family by weighted least squares, sample it at
   equal L steps across the family's central range, and snap each sample to
   the nearest real sheet colour - the line is where the ramp passes, the
   snap keeps every hex authentic. A line gives what a hand-drawn ramp has, a
   single hue that shifts steadily from shadow to highlight, and it is immune
   to the few odd colours at either end of the L range. A k-means here would
   spend its steps where the pixels are (mid-tones) and leave the ends of the
   ramp with nothing, and it would split a family by hue, giving a sorted
   "ramp" that zig-zags between ochre and red-brown. Accents are not a
   ramp, so they get a plain weighted k-means, medoid-snapped, with k-means++
   seeding on a fixed RNG so the run is reproducible.

Reports the weighted share of pixels in each family and the quantization
error the whole corpus would suffer against the proposal (mean OKLab distance
to the nearest palette entry; 0.02 is about a just-noticeable difference).

Once assets/ has been quantized, a plain re-run samples the quantized sheets
and drifts the palette by a few hexes - it is no longer the same input. To
re-derive, point SOURCES at the original sheets (docs/palette.md lists them).
To redraw the images from the approved JSON without sampling, use --render.

The approved palette is no longer only this tool's output: the overworld's 16
colours were added by hand on top of its 56 (docs/palette.md). A run without
--render overwrites assets/palette.json and drops them - write a proposal to
another --out and compare instead.

Requires numpy and Pillow. Deterministic for a given input set and config.
"""
import argparse
import glob
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS = os.path.join(ROOT, "..", "Asset packs")

# (label, glob, share of total weight). assets/ is what ships, so it carries
# half; the packs split the other half evenly so a big pack cannot outvote a
# small one.
SOURCES = [
    ("assets", os.path.join(ROOT, "assets", "*.png")),
    ("backrooms_lvl1", os.path.join(PACKS, "Backrooms Lvl1 {Tileset}", "*.png")),
    ("backrooms_items", os.path.join(PACKS, "Backrooms Items", "sprites", "*.png")),
    ("workshop", os.path.join(PACKS, "PostApoc_Workshop", "*.png")),
    ("ritual", os.path.join(PACKS, "BigWander_TheRitual", "**", "*.png")),
    ("scifi", os.path.join(PACKS, "sci-fi-facility-asset-pack", "*.png")),
    ("mobs", os.path.join(PACKS, "Mobs", "**", "*.png")),
    ("crawler", os.path.join(PACKS, "Pixel Crawler - Free Pack 2.11", "**", "*.png")),
]
ASSETS_SHARE = 0.5
SKIP_NAME = ("mockup", "preview", "czz0uu")   # promo renders, not sprites
SKIP_COLOURS = 2000                             # more than this is not pixel art
MIN_ALPHA = 128

# Family rules. Order matters: the first matching rule wins. Hue in degrees of
# OKLab (red ~30, orange ~60, yellow ~110, green ~140, cyan ~195, blue ~265,
# magenta ~330); ranges may wrap past 360.
ACCENT_CHROMA = 0.17
CONFIGS = {
    # The plan's table: six families, ~46 colours.
    "plan": [
        ("neutral", dict(steps=8, chroma=(0.0, 0.02))),
        ("grey", dict(steps=8, chroma=(0.02, 0.06), hue=(150, 330))),
        ("brown", dict(steps=8, hue=(20, 120))),
        ("green", dict(steps=8, hue=(120, 185))),
        ("blue", dict(steps=8, hue=(185, 290))),
        ("accent", dict(steps=6, accent=True)),
    ],
    # Same, with the Backrooms ochre split out of brown: yellow is a third of
    # every chromatic pixel in assets/ and the wallpaper is the game's face.
    "yellow": [
        ("neutral", dict(steps=8, chroma=(0.0, 0.02))),
        ("grey", dict(steps=8, chroma=(0.02, 0.06), hue=(150, 330))),
        ("brown", dict(steps=8, hue=(20, 75))),
        ("yellow", dict(steps=8, hue=(75, 120))),
        ("green", dict(steps=8, hue=(120, 185))),
        ("blue", dict(steps=6, hue=(185, 290))),
        ("accent", dict(steps=6, accent=True)),
    ],
    # As "yellow" but the wallpaper/carpet ramp gets 12 steps: the Backrooms
    # floor texture lives in shades ~0.03 L apart, and an 8-step ramp (0.08 per
    # step) flattens it to one tone.
    "yellow12": [
        ("neutral", dict(steps=8, chroma=(0.0, 0.02))),
        ("grey", dict(steps=8, chroma=(0.02, 0.06), hue=(150, 330))),
        ("brown", dict(steps=8, hue=(20, 75))),
        ("yellow", dict(steps=12, hue=(75, 120))),
        ("green", dict(steps=8, hue=(120, 185))),
        ("blue", dict(steps=6, hue=(185, 290))),
        ("accent", dict(steps=6, accent=True)),
    ],
}


# ---- colour maths ---------------------------------------------------------

_M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
                [0.2119034982, 0.6806995451, 0.1073969566],
                [0.0883024619, 0.2817188376, 0.6299787005]])
_M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
                [1.9779984951, -2.4285922050, 0.4505937099],
                [0.0259040371, 0.7827717662, -0.8086757660]])


def srgb_to_oklab(rgb):
    c = np.asarray(rgb, dtype=float) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return np.cbrt(lin @ _M1.T) @ _M2.T


def polar(lab):
    """(L, chroma, hue in degrees 0-360) for an (N, 3) OKLab array."""
    L = lab[:, 0]
    C = np.hypot(lab[:, 1], lab[:, 2])
    h = (np.degrees(np.arctan2(lab[:, 2], lab[:, 1])) + 360.0) % 360.0
    return L, C, h


# ---- sampling -------------------------------------------------------------

def sample():
    """Weighted colour histogram: {(r, g, b): weight}, groups normalised."""
    groups = []
    for label, pattern in SOURCES:
        counts = {}
        for path in sorted(glob.glob(pattern, recursive=True)):
            if any(s in path.lower() for s in SKIP_NAME):
                continue
            px = np.array(Image.open(path).convert("RGBA")).reshape(-1, 4)
            px = px[px[:, 3] >= MIN_ALPHA][:, :3]
            if len(px) == 0:
                continue
            cols, n = np.unique(px, axis=0, return_counts=True)
            if len(cols) > SKIP_COLOURS:
                print(f"  skip {os.path.relpath(path, ROOT)} ({len(cols)} colours)")
                continue
            for c, k in zip(map(tuple, cols.tolist()), n.tolist()):
                counts[c] = counts.get(c, 0) + k
        if counts:
            groups.append((label, counts))
            print(f"  {label:16s} {sum(counts.values()):9d} px  {len(counts):5d} colours")

    hist = {}
    packs = [g for g in groups if g[0] != "assets"]
    for label, counts in groups:
        share = ASSETS_SHARE if label == "assets" else (1 - ASSETS_SHARE) / len(packs)
        total = sum(counts.values())
        for c, k in counts.items():
            hist[c] = hist.get(c, 0.0) + share * k / total
    return hist


# ---- ramp building --------------------------------------------------------

def assign_families(config, lab, L, C, h):
    fam = np.full(len(lab), "", dtype=object)
    for name, rule in config:
        free = fam == ""
        if "accent" in rule:
            m = free
        else:
            m = free & (C < ACCENT_CHROMA)
            if "chroma" in rule:
                lo, hi = rule["chroma"]
                m &= (C >= lo) & (C < hi)
            if "hue" in rule:
                lo, hi = rule["hue"]
                hh = np.where(h < lo, h + 360.0, h) if hi > 360 else h
                m &= (hh >= lo) & (hh < hi)
        fam[m] = name
    return fam


def medoid(lab, w):
    """Weighted medoid: the member minimising weighted distance to the rest."""
    d = np.linalg.norm(lab[:, None, :] - lab[None, :, :], axis=2)
    return int(np.argmin(d @ w))


def build_ramp(lab, w, steps):
    """A straight line through OKLab, fitted to the family by weighted least
    squares of (a, b) on L, sampled at `steps` equal L intervals across the
    central range and snapped to the nearest real sheet colour. Sorted dark ->
    light."""
    L = lab[:, 0]
    lo, hi = np.percentile(L, [2, 98])          # ignore stray extremes
    X = np.stack([np.ones_like(L), L], axis=1) * np.sqrt(w)[:, None]
    coef = np.linalg.lstsq(X, lab[:, 1:] * np.sqrt(w)[:, None], rcond=None)[0]
    centres = lo + (np.arange(steps) + 0.5) * (hi - lo) / steps
    out = []
    for Lc in centres:
        target = np.array([Lc, *(np.array([1.0, Lc]) @ coef)])
        out.append(int(np.argmin(np.linalg.norm(lab - target, axis=1))))
    return sorted(set(out), key=lambda i: L[i])


def kmeans_medoids(lab, w, k, seed=0):
    rng = np.random.default_rng(seed)
    p = w / w.sum()
    centres = [lab[rng.choice(len(lab), p=p)]]
    for _ in range(1, k):                        # k-means++ seeding
        d2 = np.min([np.sum((lab - c) ** 2, axis=1) for c in centres], axis=0)
        centres.append(lab[rng.choice(len(lab), p=d2 * p / np.sum(d2 * p))])
    centres = np.array(centres)
    for _ in range(50):
        lbl = np.argmin(np.linalg.norm(lab[:, None] - centres[None], axis=2), axis=1)
        new = np.array([np.average(lab[lbl == j], axis=0, weights=w[lbl == j])
                        if (lbl == j).any() else centres[j] for j in range(k)])
        if np.allclose(new, centres):
            break
        centres = new
    out = []
    for j in range(k):
        idx = np.flatnonzero(lbl == j)
        if len(idx):
            out.append(idx[medoid(lab[idx], w[idx])])
    return sorted(set(out), key=lambda i: lab[i, 0])


def propose(config, hist):
    cols = np.array(list(hist.keys()), dtype=int)
    w = np.array(list(hist.values()))
    lab = srgb_to_oklab(cols)
    L, C, h = polar(lab)
    fam = assign_families(config, lab, L, C, h)

    ramps = []
    for name, rule in config:
        idx = np.flatnonzero(fam == name)
        if len(idx) == 0:
            print(f"  {name}: no pixels")
            continue
        picks = (kmeans_medoids(lab[idx], w[idx], rule["steps"]) if "accent" in rule
                 else build_ramp(lab[idx], w[idx], rule["steps"]))
        ramps.append({
            "name": name,
            "share": float(w[idx].sum()),
            "colours": [{"hex": "#%02x%02x%02x" % tuple(cols[idx[i]]),
                         "rgb": cols[idx[i]].tolist(),
                         "L": round(float(L[idx[i]]), 3),
                         "C": round(float(C[idx[i]]), 3),
                         "h": round(float(h[idx[i]]), 1)} for i in picks],
        })
    return ramps, cols, w, lab


def error_report(ramps, cols, w, lab):
    pal = srgb_to_oklab(np.array([c["rgb"] for r in ramps for c in r["colours"]]))
    d = np.min(np.linalg.norm(lab[:, None] - pal[None], axis=2), axis=1)
    return {"mean": float(np.average(d, weights=w)),
            "p90": float(np.percentile(np.repeat(d, np.maximum(1, (w * 1e5).astype(int))), 90)),
            "over_0.05": float(w[d > 0.05].sum() / w.sum())}


# ---- swatch ---------------------------------------------------------------

def draw_swatch(ramps, path):
    """Labelled ramps. Text uses palette colours, but the ground is
    deliberately off-palette (a dull plum no ramp contains) so the darkest
    neutral reads as a block against it. The labels are anti-aliased anyway,
    so quantize.py --check skips this file by name; it is documentation, not
    a sheet the game loads."""
    cell, pad, label_w = 48, 8, 88
    font = ImageFont.load_default()
    by = {r["name"]: r["colours"] for r in ramps}
    ink = tuple(by["neutral"][-1]["rgb"]); dim = tuple(by["neutral"][-3]["rgb"])
    ground = (38, 26, 44)  # off-palette on purpose: see docstring
    width = label_w + max(len(r["colours"]) for r in ramps) * (cell + pad) + pad
    height = pad + len(ramps) * (cell + 22 + pad)
    img = Image.new("RGB", (width, height), ground)
    dr = ImageDraw.Draw(img)
    y = pad
    for r in ramps:
        dr.text((pad, y + cell // 2 - 6), r["name"], fill=ink, font=font)
        for i, c in enumerate(r["colours"]):
            x = label_w + i * (cell + pad)
            dr.rectangle([x, y, x + cell - 1, y + cell - 1], fill=tuple(c["rgb"]))
            dr.text((x, y + cell + 2), c["hex"][1:], fill=ink, font=font)
            dr.text((x, y + cell + 11), f"L{c['L']:.2f}", fill=dim, font=font)
        y += cell + 22 + pad
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


def write_images(ramps, swatch, strip):
    draw_swatch(ramps, swatch)
    rgb = [tuple(c["rgb"]) for r in ramps for c in r["colours"]]
    Image.frombytes("RGB", (len(rgb), 1), bytes(v for c in rgb for v in c)).save(strip)
    print(f"wrote {os.path.relpath(swatch, ROOT)} and {os.path.relpath(strip, ROOT)}")


# ---- main -----------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", choices=CONFIGS, default="plan")
    ap.add_argument("--out", default=os.path.join(ROOT, "assets", "palette.json"))
    ap.add_argument("--swatch", default=os.path.join(ROOT, "assets", "palette_swatch.png"))
    ap.add_argument("--strip", default=os.path.join(ROOT, "assets", "palette_strip.png"),
                    help="one pixel per colour, ramp by ramp; import into Aseprite/GIMP as a palette")
    ap.add_argument("--render", action="store_true",
                    help="skip sampling; redraw the swatch and strip from the existing --out JSON")
    a = ap.parse_args()

    if a.render:
        with open(a.out) as f:
            ramps = json.load(f)["ramps"]
        write_images(ramps, a.swatch, a.strip)
        return 0

    print("sampling")
    hist = sample()
    print(f"  {len(hist)} distinct colours")
    ramps, cols, w, lab = propose(CONFIGS[a.config], hist)
    err = error_report(ramps, cols, w, lab)

    n = sum(len(r["colours"]) for r in ramps)
    print(f"\n{a.config}: {n} colours   quantization error mean {err['mean']:.3f}  "
          f"p90 {err['p90']:.3f}  share>0.05: {err['over_0.05']:.1%}\n")
    print("| Ramp | Share | Steps (dark -> light) |\n|---|---|---|")
    for r in ramps:
        print(f"| {r['name']} | {r['share']:.1%} | " + " ".join(f"`{c['hex']}`" for c in r["colours"]) + " |")

    doc = {"config": a.config, "accent_chroma": ACCENT_CHROMA,
           "error": err, "ramps": ramps}
    with open(a.out, "w") as f:
        json.dump(doc, f, indent=1)
    draw_swatch(ramps, a.swatch)
    print(f"\nwrote {os.path.relpath(a.out, ROOT)} and {os.path.relpath(a.swatch, ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
