#pragma once

#include "entities/player.hpp"
#include "render/draw_queue.hpp"
#include "world/overworld.hpp"
#include <array>
#include <raylib.h>

// ============================================================================
// StepEffects — what the player's feet leave on the surface
// ============================================================================
// On each footfall (PlayerRenderer::pollFootfall, raised on every frame of
// the walk cycle) the ground under that foot picks the mark: a boot
// print in snow, a squish of water in the wetland, a kick of sand on a
// beach or in coastal scrub, nothing elsewhere. Feet
// alternate, so prints run in two lines, each toe pointing the way walked.
//
// RING BUFFER. Marks live in a fixed array of kCapacity; the newest
// overwrites the oldest, so a long walk never grows memory. Each mark stores
// the time it was made, not an age, so a tick is O(1) - nothing walks the
// buffer to age it - and drawing reads how long ago that was. A print fills
// in by half after kPrintFaintAfter and is gone at kPrintSeconds; a squish
// or a kick plays its frames and is gone. One footfall per 0.2 s of walking makes 256
// marks 51 s of it, past a print's life, so a print is never cut short.
//
// Presentation only, like PlayerRenderer's walk timer: nothing in gameplay
// reads a mark, and they go with the state. Anything that tracks the player
// by prints (the dog, mobs) would move them into the world.
//
// Prints lie flat, drawn with the terrain (drawFlat). A squish or a kick
// SPRAYS up round the boot, so it joins the DrawQueue one row past the sole
// that made it: over those boots, and under the player once they walk down
// past it.
// ============================================================================
class StepEffects : public Drawer {
public:
  // KICK_PALE: the kick in pale sand, on coastal grass (dark grains read as dirt there).
  enum class Mark : uint8_t { NONE, PRINT, SQUISH, KICK, KICK_PALE };

  StepEffects() = default;
  ~StepEffects();
  StepEffects(const StepEffects &) = delete;
  StepEffects &operator=(const StepEffects &) = delete;

  void loadTextures();

  // The mark a foot leaves on this ground (and its shade step). Pure.
  static Mark markFor(Biome ground, uint8_t shade = 0);

  // One tick. `footfall` is PlayerRenderer::pollFootfall(). A footfall
  // within kMinStepPx of the last one (walking into a tree) leaves nothing.
  void update(const Overworld &world, const Player &player, bool footfall,
              float dt);

  // update()'s two halves, public so a test can drive them without a
  // player: leave `mark` at `foot` (world px) made walking `facing`, sorting
  // at base row `sortY`, and let time pass.
  void leave(Mark mark, Vector2 foot, FacingDirection facing, int sortY = 0);
  void advance(float dt) { m_time += dt; }
  // Marks still showing. O(kCapacity).
  int liveCount() const;

  // The prints, flat on the ground: after the terrain, before the queue.
  void drawFlat() const;
  // Queues every live spray (squish, kick), after the player. Binds this
  // until the queue is flushed.
  void collect(DrawQueue &queue) const;
  // Drawer: the spray in slot `index`.
  void drawQueued(int index, int) const override;

  static constexpr int kCapacity = 256;
  static constexpr float kPrintSeconds = 30.0f, kPrintFaintAfter = 20.0f;
  // A spray's frames, the same count and pace for squish and kick.
  static constexpr int kSquishFrames = 4;
  static constexpr float kSquishFrameSeconds = 0.08f;
  static constexpr float kMinStepPx = 8.0f;

private:
  struct Entry {
    Vector2 foot{};
    float made = 0.0f;
    int sortY = 0; // a spray's DrawQueue base
    Mark mark = Mark::NONE;
    FacingDirection facing = FacingDirection::DOWN; // a print's toe points this way
  };
  bool showing(const Entry &e) const;

  std::array<Entry, kCapacity> m_marks{};
  int m_next = 0;       // the slot the next mark overwrites
  float m_time = 0.0f;  // seconds since the state began, fixed-step
  int m_foot = 0;       // which foot comes down next
  Vector2 m_lastStep{};
  bool m_stepped = false;
  Texture2D m_sheet{};
};
