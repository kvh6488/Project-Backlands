#version 330

// ============================================================================
// ow_fade - one fade material (swamp water, wetland, gravel, snow, drift)
// drawn pixel by pixel over the view. OverworldRenderer::drawFade.
//
// SWAMP WATER (layer 0) is not a weighted fade: at each dual-grid corner it
// reads which of the four cells are open water and keeps the pixels that
// corner's mask keeps (the sheet's first row, dithered in along LightBorne's
// shape) - the shade overlays' fade, done here because the swamp texture
// sways and so cannot be baked into tiles.
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
uniform sampler2D info;     // per cell: r = land (grass) flag, g = swamp water flag
uniform sampler2D swampWater; // assets/ow_swamp_water.png, a square that tiles
uniform ivec2 sway;         // the still water's offset now, art px
uniform ivec2 cellOrigin;   // world cell of texel (0, 0) in weights and info
uniform int layer;          // the sheet row: 0 swamp, 1 wetland, 2 gravel, 3 snow, 4 drift

out vec4 finalColor;

const int T = 16;        // art px per cell
const int kFills = 8;    // fill variants per row
const int kMaskRow = 5;  // the corner shapes

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

// Open water: neither land nor swamp.
int openBit(ivec2 cell, int bit, inout bool swamp) {
  vec4 i = texelFetch(info, cell - cellOrigin, 0);
  swamp = swamp || i.g > 0.5;
  return i.r < 0.5 && i.g < 0.5 ? bit : 0;
}

void main() {
  // Texture coordinates were set up as world art pixels over the sheet's size.
  vec2 art = fragTexCoord * vec2(textureSize(texture0, 0));
  ivec2 px = ivec2(floor(art));

  if (layer == 0) {
    ivec2 v = ivec2(floor((art + vec2(T / 2)) / float(T)));
    ivec2 local = px + ivec2(T / 2) - v * T;
    bool swamp = false;
    int open = openBit(v + ivec2(-1, -1), 8, swamp) | openBit(v + ivec2(0, -1), 4, swamp) |
               openBit(v + ivec2(-1, 0), 2, swamp) | openBit(v, 1, swamp);
    if (!swamp || texelFetch(texture0, ivec2(open * T + local.x, local.y), 0).a < 0.5)
      discard;
    ivec2 size = textureSize(swampWater, 0);
    ivec2 at = ivec2(mod(vec2(px + sway), vec2(size))); // GLSL % is undefined below 0
    finalColor = vec4(texelFetch(swampWater, at, 0).rgb, 1.0);
    return;
  }

  // Coverage. Cell (i) of the data covers art px [i, i+1) * T, its texel
  // centre at the cell's centre, so this lands bilinear between cell centres.
  vec2 uv = (art / float(T) - vec2(cellOrigin)) / vec2(textureSize(weights, 0));
  vec4 ws = texture(weights, uv);
  float w = layer == 1 ? ws.r : layer == 2 ? ws.g : layer == 3 ? ws.b : min(ws.a, ws.b);
  uint salt = uint(layer) * 0x9e3779b9u;
  float n = 0.18 * (2.0 * valueNoise(art / 7.0, salt) - 1.0) +
            0.06 * (2.0 * valueNoise(art / 2.5, salt ^ 0x51edu) - 1.0);
  float c = w + n * 4.0 * w * (1.0 - w);
  // Threshold: clumpy value noise (blobs a few art px across) with a little
  // Bayer mixed in to break the blobs' smooth outlines into pixels.
  vec2 centre = vec2(px) + 0.5;
  float blob = 0.7 * valueNoise(centre / 3.0, salt ^ 0xb10bu) + 0.3 * valueNoise(centre / 1.5, salt ^ 0x5eedu);
  blob = clamp((blob - 0.5) * 2.2 + 0.5, 0.0, 1.0);
  float bayer = (float(kBayer[(px.y & 3) * 4 + (px.x & 3)]) + 0.5) / 16.0;
  float threshold = clamp(0.8 * blob + 0.2 * bayer, 0.02, 0.98);
  if (c <= threshold)
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
