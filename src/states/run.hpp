#pragma once

#include "core/capture.hpp"
#include "entities/player.hpp"
#include "world/calendar.hpp"
#include <ctime>

// ============================================================================
// Run — everything that outlives either world
// ============================================================================
// One run is one life: Day 0 to death. The maze and the overworld are states
// that come and go; the seed that built them and the player walking through
// them do not. Application owns the Run and every state borrows it, so a
// world transition (Phase 6) moves the player by handing over a reference,
// not by copying an inventory between states.
//
// A state places the player in its world with Player::teleport - it never
// constructs a fresh Player, which would silently wipe the bag on every
// world change.
//
// The Calendar is the run's one clock (world/calendar.hpp): Day 0 at the
// start, the score, and the surface's season. Each state advances it one
// tick per update, so it runs at the same rate in both worlds.
// ============================================================================
struct Run {
  // seed 0 means "pick one from the clock". Resolved here, once, so both
  // worlds of a run are built from the same number.
  explicit Run(unsigned int requestedSeed)
      : seed(requestedSeed != 0 ? requestedSeed
                                : (unsigned int)std::time(nullptr)),
        player(Vector2{0.0f, 0.0f}, AreaState::ROOM) {}

  unsigned int seed;
  Player player;
  Calendar calendar;

  // The clock's half of a state's snapshot - the same in both worlds.
  void snapshotClock(Telemetry &out) const {
    out.day = calendar.day();
    out.season = seasonId(calendar.season());
    out.dayOfSeason = calendar.dayOfSeason();
    out.timeOfDay = calendar.timeOfDay();
    out.yearDay = (float)calendar.yearDay();
  }
};
