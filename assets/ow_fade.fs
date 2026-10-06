#version 330

// ============================================================================
// ow_fade - one fade material (swamp water, wetland, gravel, snow, drift,
// dune sand)
// drawn pixel by pixel over the view. OverworldRenderer::drawFade.
//
// SWAMP WATER (layer 0) is drawn in passes, one per tint `level` (1 faint ..
// 4 full murk), each over the last. Its weight is the swamp depth, read
// bilinear and nudged by broad noise so the bands wander; pass k covers the
// k-th quarter of it, under the same clumpy threshold as the land fades.
// Every pass shares that threshold, so a darker tint never draws where a
// fainter one did not.
// ============================================================================
// COVERAGE. `weights` / `info` hold, per cell, the share of nearby cells that
// are this material. Sampled bilinear, that is a smooth 0..1 ramp across a
// border several tiles wide. A pixel draws where the ramp beats a 4x4 Bayer
// threshold - an ordered dither, crisp on the art-pixel grid - with value
// noise added so the border wobbles instead of running along the ramp's
// straight contours. The noise is scaled by w(1 - w): none where a material
// is solid (no holes) or absent (no specks), and small enough that coverage
// still rises with w, so snow (whose weight never exceeds gravel's) only
// ever draws on gravel.
//
// LAND CLIP. Land materials stop at the shore. Each pixel finds its dual-grid
// corner, reads which of the four cells there are land, and looks up that
// corner shape (LightBorne's, in the sheet's last row): outside it the pixel
// is dropped, on its outline the pixel takes the material's outline colour.
// ============================================================================

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0; // assets/ow_fades.png
uniform sampler2D weights;  // per cell: wetland, gravel, snow, drift
uniform sampler2D info;     // per cell: r = land (grass) flag, g = swamp depth 0..1, b = dune sand
uniform sampler2D swampWater; // assets/ow_swamp_water.png: a tiling square per level
uniform int level;          // swamp water's tint pass, 1..4
uniform ivec2 sway;         // the still water's offset now, art px
uniform ivec2 cellOrigin;   // world cell of texel (0, 0) in weights and info
uniform int layer;          // the sheet row: 0 swamp, 1 wetland, 2 gravel, 3 snow, 4 drift, 5 dune

out vec4 finalColor;

const int T = 16;        // art px per cell
const int kFills = 8;    // fill variants per row
const int kMaskRow = 6;  // the corner shapes

const int kBayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);

uint hash(ivec2 p, uint salt) {
  uint h = uint(p.x) * 0x8da6b343u ^ uint(p.y) * 0xd8163841u ^ salt;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return h;
}

// Smooth value noise in [0, 1], lattice spacing 1.
float valueNoise(vec2 p, uint salt) {
  ivec2 i = ivec2(floor(p));
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = float(hash(i, salt) & 0xffffu) / 65535.0;
  float b = float(hash(i + ivec2(1, 0), salt) & 0xffffu) / 65535.0;
  float c = float(hash(i + ivec2(0, 1), salt) & 0xffffu) / 65535.0;
  float d = float(hash(i + ivec2(1, 1), salt) & 0xffffu) / 65535.0;
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

bool isLand(ivec2 cell) {
  return texelFetch(info, cell - cellOrigin, 0).r > 0.5;
}

// The threshold a coverage must beat: clumpy value noise (blobs a few art px
// across) with a little Bayer to break the blobs' smooth outlines into pixels.
float clumpyThreshold(ivec2 px, uint salt) {
  vec2 centre = vec2(px) + 0.5;
  float blob = 0.7 * valueNoise(centre / 3.0, salt ^ 0xb10bu) + 0.3 * valueNoise(centre / 1.5, salt ^ 0x5eedu);
  blob = clamp((blob - 0.5) * 2.2 + 0.5, 0.0, 1.0);
  float bayer = (float(kBayer[(px.y & 3) * 4 + (px.x & 3)]) + 0.5) / 16.0;
  return clamp(0.8 * blob + 0.2 * bayer, 0.02, 0.98);
}

void main() {
  // Texture coordinates were set up as world art pixels over the sheet's size.
  vec2 art = fragTexCoord * vec2(textureSize(texture0, 0));
  ivec2 px = ivec2(floor(art));

  vec2 uv = (art / float(T) - vec2(cellOrigin)) / vec2(textureSize(weights, 0));
  if (layer == 0) {
    float d = texture(info, uv).g;
    // Broad wander (a few tiles across) plus finer ragging; none where the
    // water is fully open or fully swamp, so neither gets specks.
    float n = 0.22 * (2.0 * valueNoise(art / 40.0, 0x51a3u) - 1.0) +
              0.08 * (2.0 * valueNoise(art / 9.0, 0x7e11u) - 1.0);
    d = clamp(d + n * 4.0 * d * (1.0 - d), 0.0, 1.0);
    float c = clamp(d * 4.0 - float(level - 1), 0.0, 1.0);
    if (c <= clumpyThreshold(px, 0x5a3fu))
      discard;
    int side = textureSize(swampWater, 0).x; // the squares are stacked down the sheet
    ivec2 at = ivec2(mod(vec2(px + sway), float(side))); // GLSL % is undefined below 0
    finalColor = vec4(texelFetch(swampWater, at + ivec2(0, (level - 1) * side), 0).rgb, 1.0);
    return;
  }

  // Coverage. Cell (i) of the data covers art px [i, i+1) * T, its texel
  // centre at the cell's centre, so `uv` lands bilinear between cell centres.
  vec4 ws = texture(weights, uv);
  float w = layer == 1 ? ws.r : layer == 2 ? ws.g : layer == 3 ? ws.b
          : layer == 4 ? min(ws.a, ws.b) : texture(info, uv).b;
  uint salt = uint(layer) * 0x9e3779b9u;
  float n = 0.18 * (2.0 * valueNoise(art / 7.0, salt) - 1.0) +
            0.06 * (2.0 * valueNoise(art / 2.5, salt ^ 0x51edu) - 1.0);
  float c = w + n * 4.0 * w * (1.0 - w);
  if (c <= clumpyThreshold(px, salt))
    discard;

  // The dual-grid corner this pixel is drawn at, and where in its tile.
  ivec2 v = ivec2(floor((art + vec2(T / 2)) / float(T)));
  ivec2 local = px + ivec2(T / 2) - v * T;
  int variant = int(hash(v, 0x27d4eb2fu) % uint(kFills));
  vec4 colour = texelFetch(texture0, ivec2(variant * T + local.x, layer * T + local.y), 0);

  int mask = (isLand(v + ivec2(-1, -1)) ? 8 : 0) | (isLand(v + ivec2(0, -1)) ? 4 : 0) |
             (isLand(v + ivec2(-1, 0)) ? 2 : 0) | (isLand(v) ? 1 : 0);
  if (mask == 0)
    discard;
  if (mask != 15) {
    vec4 code = texelFetch(texture0, ivec2(mask * T + local.x, kMaskRow * T + local.y), 0);
    if (code.a < 0.5)
      discard;
    // Body is light; the outline's two shades sit at x = 128 (dark), 129 (mid).
    if (code.r < 0.27)
      colour = texelFetch(texture0, ivec2(8 * T, layer * T), 0);
    else if (code.r < 0.69)
      colour = texelFetch(texture0, ivec2(8 * T + 1, layer * T), 0);
  }
  finalColor = vec4(colour.rgb, 1.0);
}
