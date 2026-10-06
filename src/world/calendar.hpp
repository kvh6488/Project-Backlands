#pragma once

#include <cmath>
#include <cstdint>

// ============================================================================
// Calendar — one counter, two readings (wilderness §4)
// ============================================================================
// The run's clock is a single integer: fixed-step ticks since the run began.
// Everything else is DERIVED from it, never stored beside it, so nothing can
// drift out of step:
//
//   day        ticks / ticksPerDay, counted from Day 0 - the score and every
//              unlock gate. Never resets, runs at one rate in both worlds.
//   timeOfDay  the remainder, in hours. A run starts at kStartHour, so Day 0
//              is a short day and Day 1 begins at midnight.
//   yearDay    the CYCLICAL reading the surface uses: where in the 80-day
//              year we are, continuous (12.5 = half way through day 12 of
//              the year). Offset so the run starts in mid-spring, and pushed
//              on by seasonalDrift - extra season time accrued in the maze
//              (wilderness §4.3), which only ever grows. Stubbed at 0 until
//              Phase 6 connects the worlds.
//
// The year: spring 0-20, summer 20-40, autumn 40-60, winter 60-80. A run
// begins at yearDay 10 (mid-spring), so Day 20 - the earliest a maze entrance
// can open - falls in mid-summer, and the first winter starts on Day 50.
//
// Integer ticks, not a float of seconds: a float loses whole ticks after a
// few hours of play, and headless replays must be byte-identical.
// ============================================================================
enum class Season : uint8_t { SPRING, SUMMER, AUTUMN, WINTER };

inline const char *seasonId(Season s) {
  switch (s) {
  case Season::SPRING: return "SPRING";
  case Season::SUMMER: return "SUMMER";
  case Season::AUTUMN: return "AUTUMN";
  default: return "WINTER";
  }
}

struct Calendar {
  static constexpr int64_t kTicksPerSecond = 60; // Application::kFixedDt
  static constexpr int64_t kTicksPerDay = 15 * 60 * kTicksPerSecond; // 15 real minutes
  static constexpr int64_t kTicksPerHour = kTicksPerDay / 24;
  static constexpr int kDaysPerSeason = 20;
  static constexpr int kDaysPerYear = 4 * kDaysPerSeason;
  static constexpr int kStartYearDay = kDaysPerSeason / 2; // mid-spring
  static constexpr int kStartHour = 8;

  int64_t ticks = 0;      // since the run began
  int64_t driftTicks = 0; // seasonalDrift, in ticks (wilderness §4.3)

  void advance(int64_t n = 1) { ticks += n; }

  // Jumps to `hour` (0-24) on run day `day`; Day 0 cannot go before the start.
  void setDay(int day, float hour) {
    int64_t t = (int64_t)day * kTicksPerDay + (int64_t)(hour * kTicksPerHour) -
                kStartHour * kTicksPerHour;
    ticks = t < 0 ? 0 : t;
  }

  // The year the debug slider scrubs: day 1 of summer to the end of spring,
  // the order a run lives through once its opening spring is over.
  static constexpr int kScrubYearStart = kDaysPerSeason;

  // Moves to `target` (a yearDay, 0-80) within the current summer-to-spring
  // year, its fraction setting the time of day. A date before the run began
  // goes to that date a year on.
  void setYearDay(double target) {
    const double year = kDaysPerYear;
    // Year-days since yearDay 0 of the run's first year, drift included.
    const double now = kStartYearDay + (double)(clockTicks() + driftTicks) / kTicksPerDay;
    const double start = std::floor((now - kScrubYearStart) / year) * year + kScrubYearStart;
    const double into = std::fmod(std::fmod(target - kScrubYearStart, year) + year, year);
    const double days = start + into - kStartYearDay; // since Day 0's midnight
    int64_t t = (int64_t)std::llround(days * kTicksPerDay) - driftTicks - kStartHour * kTicksPerHour;
    if (t < 0)
      t += (int64_t)kDaysPerYear * kTicksPerDay;
    ticks = t;
  }

  // Ticks since midnight before Day 0.
  int64_t clockTicks() const { return ticks + kStartHour * kTicksPerHour; }
  int day() const { return (int)(clockTicks() / kTicksPerDay); }
  float timeOfDay() const { // hours, [0, 24)
    return (float)(clockTicks() % kTicksPerDay) / (float)kTicksPerHour;
  }

  // [0, kDaysPerYear), continuous. Double: ticks reach the tens of millions.
  double yearDay() const {
    double days = (double)(clockTicks() + driftTicks) / (double)kTicksPerDay;
    return std::fmod(kStartYearDay + days, (double)kDaysPerYear);
  }
  Season season() const { return (Season)((int)yearDay() / kDaysPerSeason); }
  // 1-based, as a player would say it: "day 3 of autumn".
  int dayOfSeason() const { return (int)yearDay() % kDaysPerSeason + 1; }
};
