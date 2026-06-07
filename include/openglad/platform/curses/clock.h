/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

namespace og::curses {

// Abstract monotonic clock so gameplay pacing and input-decay are deterministic
// in tests. Real client uses SteadyClock; tests use FakeClock.
class IClock
{
public:
    virtual ~IClock() = default;
    // Monotonic milliseconds since an arbitrary epoch.
    virtual std::uint64_t now_ms() = 0;
    // Sleep for the given duration. Tests override to advance virtual time.
    virtual void sleep_ms(std::uint32_t ms) = 0;
};

// Real clock backed by std::chrono::steady_clock.
class SteadyClock final : public IClock
{
public:
    std::uint64_t now_ms() override;
    void sleep_ms(std::uint32_t ms) override;
};

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
