#pragma once

// ============================================================================
// Scenario — the plain-text grammar a headless run is driven by
// ============================================================================
// Line-oriented, one command per line, so no JSON parser and no fourth
// FetchContent dependency. Telemetry goes OUT as JSON; scenarios come IN as
// text. Whitespace-separated tokens, '#' starts a comment, blank lines are
// skipped, keywords and key names are case-insensitive.
//
//   seed 1788480606      pin the world  (else --seed, else the clock)
//   world overworld      start world: maze (default) or overworld
//   spawn 3060 1536      start on this tile, not the world's spawn
//   window 1280 720      pin the window (default 1280x720)
//   scale 1.5            blit scale: one of 1 1.5 2 3 (default 1.5)
//   wait N               N idle ticks
//   hold KEY N           KEY down for N ticks; "pressed" fires on the first
//   press KEY            = hold KEY 1
//   mouse X Y            move the cursor (window pixels); it stays there
//   click X Y            one tick: cursor at X Y, left button pressed
//   rclick X Y           one tick: cursor at X Y, right button pressed
//   checkpoint NAME      one idle tick whose frame and state are recorded
//   end                  stop here (implicit at end of file)
//
//   KEY: W A S D UP DOWN LEFT RIGHT K L P I O U Q 1 2 3 4 5
//
// A checkpoint is an idle tick on purpose: the frame it records shows the
// world after every command before it has been applied and settled for one
// tick, and nothing from the command after it has leaked in. Ticks are 1/60 s
// of game time each (Application::kFixedDt), so "hold W 60" is one second of
// walking however fast the machine runs it.
//
// This header only turns text into a command list; dev/scripted_input.hpp
// turns the command list into one InputState per tick.
// ============================================================================

#include "core/render_settings.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace scenario {

struct Command {
  enum class Kind { Wait, Hold, Mouse, Click, RClick, Checkpoint, End };
  Kind kind = Kind::Wait;
  std::string key;  // Hold
  int ticks = 0;    // Wait, Hold
  int x = 0, y = 0; // Mouse, Click, RClick
  std::string name; // Checkpoint
  int line = 0;     // source line, for error messages downstream
};

struct Scenario {
  unsigned int seed = 0; // 0 = not pinned
  std::string world;     // "MAZE", "OVERWORLD", or empty = not pinned
  bool hasSpawn = false;
  int spawnX = 0, spawnY = 0;
  int windowW = 1280;
  int windowH = 720;
  float blitScale = RenderSettings{}.blitScale;
  std::vector<Command> commands;
};

inline const char *const KEYS[] = {"W",  "A", "S", "D", "UP", "DOWN", "LEFT",
                                   "RIGHT", "K", "L", "P", "I",  "O",    "U",
                                   "Q",  "1", "2", "3", "4",  "5"};
inline constexpr int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);

inline std::string upper(std::string s) {
  for (char &c : s)
    c = (char)std::toupper((unsigned char)c);
  return s;
}

inline bool isKnownKey(const std::string &upperKey) {
  for (int i = 0; i < KEY_COUNT; ++i) {
    if (upperKey == KEYS[i])
      return true;
  }
  return false;
}

// Checkpoint names become directory names, so keep them boring.
inline bool isSafeName(const std::string &s) {
  if (s.empty())
    return false;
  for (char c : s) {
    if (!std::isalnum((unsigned char)c) && c != '_' && c != '-')
      return false;
  }
  return true;
}

// Parses `text`. On failure returns false and leaves a "line N: reason"
// message in `error`; `out` is then only partially filled.
inline bool parseText(const std::string &text, Scenario &out,
                      std::string &error) {
  std::istringstream lines(text);
  std::string line;
  int lineNo = 0;
  bool ended = false;

  auto fail = [&](const std::string &why) {
    error = "line " + std::to_string(lineNo) + ": " + why;
    return false;
  };
  auto parseInt = [](const std::string &tok, int &v) {
    char *end = nullptr;
    long n = std::strtol(tok.c_str(), &end, 10);
    if (end == tok.c_str() || *end != '\0')
      return false;
    v = (int)n;
    return true;
  };

  while (std::getline(lines, line)) {
    ++lineNo;
    if (auto hash = line.find('#'); hash != std::string::npos)
      line.erase(hash);

    std::istringstream words(line);
    std::vector<std::string> tok;
    for (std::string w; words >> w;)
      tok.push_back(w);
    if (tok.empty())
      continue;
    if (ended)
      return fail("command after 'end'");

    const std::string op = upper(tok[0]);
    auto argc = [&](size_t n) { return tok.size() == n + 1; };
    Command cmd;
    cmd.line = lineNo;

    if (op == "SEED") {
      if (!argc(1))
        return fail("seed takes one number");
      char *end = nullptr;
      unsigned long v = std::strtoul(tok[1].c_str(), &end, 10);
      if (end == tok[1].c_str() || *end != '\0' || v == 0)
        return fail("seed must be a positive integer");
      out.seed = (unsigned int)v;
      continue;
    }
    if (op == "WORLD") {
      std::string w = argc(1) ? upper(tok[1]) : "";
      if (w != "MAZE" && w != "OVERWORLD")
        return fail("world takes maze or overworld");
      out.world = w;
      continue;
    }
    if (op == "SPAWN") {
      if (!argc(2) || !parseInt(tok[1], out.spawnX) ||
          !parseInt(tok[2], out.spawnY))
        return fail("spawn takes a tile x and y");
      out.hasSpawn = true;
      continue;
    }
    if (op == "WINDOW") {
      if (!argc(2) || !parseInt(tok[1], out.windowW) ||
          !parseInt(tok[2], out.windowH) || out.windowW <= 0 ||
          out.windowH <= 0)
        return fail("window takes a positive width and height");
      continue;
    }
    if (op == "SCALE") {
      if (!argc(1))
        return fail("scale takes one value");
      float s = std::strtof(tok[1].c_str(), nullptr);
      bool ok = false;
      for (int i = 0; i < RenderSettings::kBlitScaleCount; ++i)
        ok = ok || s == RenderSettings::kBlitScales[i];
      if (!ok)
        return fail("scale must be one of 1, 1.5, 2, 3");
      out.blitScale = s;
      continue;
    }
    if (op == "WAIT") {
      if (!argc(1) || !parseInt(tok[1], cmd.ticks) || cmd.ticks <= 0)
        return fail("wait takes a positive tick count");
      cmd.kind = Command::Kind::Wait;
    } else if (op == "HOLD" || op == "PRESS") {
      const bool hold = op == "HOLD";
      if (!argc(hold ? 2 : 1))
        return fail(hold ? "hold takes a key and a tick count"
                         : "press takes one key");
      cmd.key = upper(tok[1]);
      if (!isKnownKey(cmd.key))
        return fail("unknown key '" + tok[1] + "'");
      cmd.ticks = 1;
      if (hold && (!parseInt(tok[2], cmd.ticks) || cmd.ticks <= 0))
        return fail("hold takes a positive tick count");
      cmd.kind = Command::Kind::Hold;
    } else if (op == "MOUSE" || op == "CLICK" || op == "RCLICK") {
      if (!argc(2) || !parseInt(tok[1], cmd.x) || !parseInt(tok[2], cmd.y))
        return fail(tok[0] + " takes an x and a y in window pixels");
      cmd.kind = op == "MOUSE"   ? Command::Kind::Mouse
                 : op == "CLICK" ? Command::Kind::Click
                                 : Command::Kind::RClick;
    } else if (op == "CHECKPOINT") {
      if (!argc(1) || !isSafeName(tok[1]))
        return fail("checkpoint takes one name of [A-Za-z0-9_-]");
      for (const Command &prev : out.commands) {
        if (prev.kind == Command::Kind::Checkpoint && prev.name == tok[1])
          return fail("duplicate checkpoint '" + tok[1] + "'");
      }
      cmd.name = tok[1];
      cmd.kind = Command::Kind::Checkpoint;
    } else if (op == "END") {
      if (!argc(0))
        return fail("end takes no arguments");
      cmd.kind = Command::Kind::End;
      ended = true;
    } else {
      return fail("unknown command '" + tok[0] + "'");
    }
    out.commands.push_back(cmd);
  }
  return true;
}

inline bool parseFile(const char *path, Scenario &out, std::string &error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = std::string("cannot open '") + path + "'";
    return false;
  }
  std::stringstream buf;
  buf << in.rdbuf();
  return parseText(buf.str(), out, error);
}

} // namespace scenario
