/* IRandom: injectable RNG interface for deterministic simulation.
 *
 * Lives in the sim layer so entity code can use it without pulling in
 * runtime/game_context.h.  Production code uses ProductionRandom
 * (wraps the legacy global random()); tests can substitute FixedRandom
 * or SeededRandom for reproducibility.
 */
#pragma once

#include <cstdint>

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual std::uint32_t next(std::uint32_t max_exclusive) = 0;
};

// Test RNG: returns a fixed value or cycles through a sequence
class FixedRandom : public IRandom {
public:
    explicit FixedRandom(std::uint32_t value) : value_(value) {}
    std::uint32_t next(std::uint32_t max_exclusive) override {
        return (max_exclusive == 0) ? 0 : (value_ % max_exclusive);
    }
private:
    std::uint32_t value_;
};

// Seeded RNG: uses a simple LCG for reproducible sequences
class SeededRandom : public IRandom {
public:
    explicit SeededRandom(std::uint32_t seed) : state_(seed) {}
    std::uint32_t next(std::uint32_t max_exclusive) override {
        if (max_exclusive == 0) return 0;
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) % max_exclusive;
    }
    std::uint32_t state_;
};
