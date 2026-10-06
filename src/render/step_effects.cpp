#include "render/step_effects.hpp"
#include "core/asset_load.hpp"
#include "core/grid.hpp"
#include <algorithm>
#include <cmath>

namespace {

// assets/ow_steps.png: row 0 a print per FacingDirection (column = the
// enum's value), row 1 the same half filled in, row 2 the squish frames,
// row 3 the sand kick's.
// Every pattern is centred on its tile, which is centred on the foot.
constexpr int kPrintRow = 0, kFaintRow = 1, kSquishRow = 2, kKickRow = 3, kPaleKickRow = 4;

bool sprays(StepEffects::Mark m) {
  return m == StepEffects::Mark::SQUISH || m == StepEffects::Mark::KICK ||
         m == StepEffects::Mark::KICK_PALE;
}

// Where a foot lands, in canvas px from the player's position (the sprite's
// centre): kFootDrop down, and kFootSpread either side when walking up or
// down. Walking sideways the feet pass one above the other, kFootSpread / 2
// apart in y. Whole art px, so a print sits on the snow's own pixel grid.
constexpr float kFootDrop = 12.0f, kFootSpread = 4.0f;
// The player's sole is this far below its position (the sprite's bottom
// edge, PlayerRenderer::collect's base). A squish sorts one row past the
// sole that made it, so it splashes over those boots and the player walks
// in front of it on the way down.
constexpr float kSole = 16.0f;

float snapToArt(float v) {
  return std::floor(v / grid::WORLD_SCALE) * grid::WORLD_SCALE;
}

// The tile drawn centred on `foot`.
void drawCentred(Texture2D sheet, int col, int row, Vector2 foot) {
  const Rectangle src = grid::srcTile(col, row);
  DrawTexturePro(sheet, src, grid::destFor(src, foot.x - grid::CELL / 2.0f, foot.y - grid::CELL / 2.0f),
                 {0, 0}, 0.0f, WHITE);
}

} // namespace

StepEffects::~StepEffects() {
  if (IsWindowReady() && m_sheet.id != 0)
    UnloadTexture(m_sheet);
}

void StepEffects::loadTextures() {
  m_sheet = assets::loadTexture("assets/ow_steps.png", "StepEffects");
}

StepEffects::Mark StepEffects::markFor(Biome ground, uint8_t shade) {
  switch (ground) {
  case Biome::SNOW: return Mark::PRINT;
  case Biome::WETLAND: return Mark::SQUISH;
  case Biome::BEACH: return Mark::KICK;
  case Biome::COASTAL: return shade == 1 ? Mark::KICK : Mark::KICK_PALE; // 1 = sand patch
  default: return Mark::NONE;
  }
}

void StepEffects::update(const Overworld &world, const Player &player, bool footfall,
                         float dt) {
  advance(dt);
  const Vector2 pos = player.getPosition();
  if (!footfall || (m_stepped && std::hypot(pos.x - m_lastStep.x, pos.y - m_lastStep.y) < kMinStepPx))
    return;
  m_lastStep = pos;
  m_stepped = true;

  const FacingDirection facing = player.getFacingDirection();
  const bool sideways = facing == FacingDirection::LEFT || facing == FacingDirection::RIGHT;
  const float side = m_foot == 0 ? -1.0f : 1.0f;
  m_foot ^= 1;
  Vector2 foot = {pos.x, pos.y + kFootDrop};
  if (sideways)
    foot.y += side * kFootSpread / 2.0f;
  else
    foot.x += side * kFootSpread;
  foot = {snapToArt(foot.x), snapToArt(foot.y)};
  const int fx = world.toGridX(foot.x), fy = world.toGridY(foot.y);
  leave(markFor(world.biomeAt(fx, fy), world.shadeAt(fx, fy)), foot, facing,
        (int)std::floor(pos.y + kSole) + 1);
}

void StepEffects::leave(Mark mark, Vector2 foot, FacingDirection facing, int sortY) {
  if (mark == Mark::NONE)
    return;
  m_marks[m_next] = {foot, m_time, sortY, mark, facing};
  m_next = (m_next + 1) % kCapacity;
}

bool StepEffects::showing(const Entry &e) const {
  const float age = m_time - e.made;
  switch (e.mark) {
  case Mark::PRINT: return age < kPrintSeconds;
  case Mark::SQUISH: case Mark::KICK: case Mark::KICK_PALE: return age < kSquishFrames * kSquishFrameSeconds;
  default: return false;
  }
}

int StepEffects::liveCount() const {
  int n = 0;
  for (const Entry &e : m_marks)
    n += showing(e);
  return n;
}

void StepEffects::drawFlat() const {
  for (const Entry &e : m_marks) {
    if (e.mark != Mark::PRINT || !showing(e))
      continue;
    const int row = m_time - e.made >= kPrintFaintAfter ? kFaintRow : kPrintRow;
    drawCentred(m_sheet, (int)e.facing, row, e.foot);
  }
}

// Sprays live a third of a second at the player's feet, so they are always
// in view: no cull before the queue.
void StepEffects::collect(DrawQueue &queue) const {
  for (int i = 0; i < kCapacity; ++i) {
    const Entry &e = m_marks[i];
    if (sprays(e.mark) && showing(e))
      queue.push(e.sortY, *this, i);
  }
}

void StepEffects::drawQueued(int index, int) const {
  const Entry &e = m_marks[index];
  const int frame = std::min(kSquishFrames - 1, (int)((m_time - e.made) / kSquishFrameSeconds));
  const int row = e.mark == Mark::KICK ? kKickRow : e.mark == Mark::KICK_PALE ? kPaleKickRow : kSquishRow;
  drawCentred(m_sheet, frame, row, e.foot);
}
