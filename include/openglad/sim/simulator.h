#pragma once

#include <cstdint>
#include <vector>

#include <openglad/sim/event.h>

namespace og::sim {

// Per-entity input for a single simulation step.
struct Input final {
    std::uint32_t cmd = 0;
    std::uint32_t value = 0;
};

// Snapshot of all player inputs for a single simulation step.
struct InputSnapshot final {
    static constexpr int MAX_PLAYERS = 4;
    Input players[MAX_PLAYERS] = {};
    bool quit_requested = false;
};

// Minimal simulation state. Stable fields are compared across runs
// to verify determinism.
struct State final {
    std::uint32_t tick = 0;
    std::uint32_t acc = 0;
    std::uint32_t entity_count = 0;
};

// Deterministic, headless simulator:
// - step() consumes InputSnapshot + dt
// - emits Events into an internal event stream
// - produces a stable State
//
// This is the foundational abstraction for separating game logic
// from SDL/rendering. Currently a skeleton; real game logic in
// screen::act() will be migrated here incrementally.
class Simulator final {
public:
    explicit Simulator(std::uint32_t seed);

    const State& state() const { return st_; }
    const std::vector<Event>& events() const { return events_; }
    void clear_events() { events_.clear(); }

    // Legacy single-input step (kept for existing tests).
    void step(const Input& in);

    // Full snapshot step with delta time.
    void step(const InputSnapshot& snapshot, float dt);

private:
    std::uint32_t rng_state_ = 0;
    State st_{};
    std::vector<Event> events_;

    std::uint32_t next_u32();
    void emit(EventKind kind, std::uint32_t a, std::uint32_t b);
};

} // namespace og::sim
