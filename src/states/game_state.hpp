#pragma once

#include "core/capture.hpp"
#include "core/input_state.hpp"
#include <memory>

// Forward declaration if we ever need the Application to be passed down
class Application;

// A dev-only start tile (the --headless `spawn` line), so a scenario can begin
// where the thing under test is instead of scripting a walk there. Passed as
// a nullable pointer; null = the world's own spawn.
struct SpawnOverride {
  int x, y;
};

// ============================================================================
// GameState — one screen's worth of game logic
// ============================================================================
// Application owns exactly one GameState at a time and drives it through
// update/render. MazeState is the only concrete state today; the main menu
// and death screen are Phase 4.
//
// TRANSITIONS ARE A MAILBOX, NOT A CALL.
// A state never swaps itself out. It raises a request (requestState /
// requestQuit) and Application honours it *between* frames, once render() has
// returned. Swapping inside update() would destroy the object still executing
// on the stack — the classic use-after-free in a naive state machine.
// ============================================================================
class GameState {
public:
    virtual ~GameState() = default;

    // Called once when the state is pushed/switched to
    virtual void onEnter() {}

    // Called once when the state is removed/switched away from
    virtual void onExit() {}

    // Called every tick. dt is the fixed simulation step and `in` is this
    // tick's input - both come from Application, never from raylib directly,
    // so a state behaves identically under a keyboard or a scripted replay.
    virtual void update(float dt, const InputState &in) = 0;

    // Called every tick after update. Takes the same InputState so hover
    // effects can read the mouse without polling hardware mid-draw.
    virtual void render(const InputState &in) = 0;

    // Fills in what a headless checkpoint should record about this state.
    // A value out, mirroring InputState in: no file I/O here, the harness
    // serialises it. Default reports nothing.
    virtual void snapshot(Telemetry & /*out*/) const {}

    // --- Transition mailbox, read by Application after render() ---

    bool hasPendingTransition() const {
      return m_nextState != nullptr || m_wantsQuit;
    }
    bool wantsQuit() const { return m_wantsQuit; }

    // Hands ownership of the successor to the caller, clearing the request.
    std::unique_ptr<GameState> takeNextState() { return std::move(m_nextState); }

protected:
    // Ask Application to switch to `next` after this frame.
    void requestState(std::unique_ptr<GameState> next) {
      m_nextState = std::move(next);
    }

    // Ask Application to leave the main loop after this frame.
    void requestQuit() { m_wantsQuit = true; }

private:
    std::unique_ptr<GameState> m_nextState;
    bool m_wantsQuit = false;
};
