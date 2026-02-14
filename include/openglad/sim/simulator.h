#pragma once

#include <cstdint>
#include <vector>

#include <openglad/sim/event.h>

namespace og::sim {

struct Input final {
    std::uint32_t cmd = 0;
    std::uint32_t value = 0;
};

struct State final {
    std::uint32_t tick = 0;
    std::uint32_t acc = 0;
};

// Deterministic, headless simulator skeleton:
// - step() consumes Input
// - emits Events into an internal event stream
// - produces a stable State
class Simulator final {
public:
    explicit Simulator(std::uint32_t seed);

    const State& state() const { return st_; }
    const std::vector<Event>& events() const { return events_; }
    void clear_events() { events_.clear(); }

    void step(const Input& in);

private:
    std::uint32_t rng_state_ = 0;
    State st_{};
    std::vector<Event> events_;

    std::uint32_t next_u32();
};

} // namespace og::sim

