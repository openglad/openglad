#pragma once

#include <openglad/platform/curses/clock.h>

namespace og::curses {

// Deterministic test clock. now_ms() returns the current virtual time;
// sleep_ms() advances it. advance() lets tests move time without sleeping.
class FakeClock final : public IClock
{
public:
    explicit FakeClock(std::uint64_t start_ms = 0) : now_(start_ms) {}
    std::uint64_t now_ms() override { return now_; }
    void sleep_ms(std::uint32_t ms) override { now_ += ms; }
    void advance(std::uint64_t ms) { now_ += ms; }
    void set(std::uint64_t ms) { now_ = ms; }

private:
    std::uint64_t now_ = 0;
};

} // namespace og::curses
