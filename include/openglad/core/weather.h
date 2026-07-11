/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

// Per-level weather. GAMEPLAY-INERT world state: rolled once per level by the
// AUTHORITATIVE side (the server / local-shadow server world), synced to every
// client through WorldSnapshot, and consumed ONLY by the renderer — no sim
// logic may branch on it, and the roll must never touch the sim RNG stream
// (both would break the deterministic parity goldens).
enum class WeatherKind : unsigned char {
    None = 0,
    Clouds = 1,
    Rain = 2,
};

namespace og {

// Decile residue -> kind: 0-4 None (50%), 5-7 Clouds (30%), 8-9 Rain (20%).
// Split out so tests can pin the boundary mapping directly.
[[nodiscard]] constexpr WeatherKind weather_kind_from_residue(
    std::uint32_t residue) noexcept
{
    if (residue < 5u)
        return WeatherKind::None;
    if (residue < 8u)
        return WeatherKind::Clouds;
    return WeatherKind::Rain;
}

// Pure roll: an integer-hash finalizer over the seed, reduced mod 10 through
// weather_kind_from_residue. Deterministic for a given seed; consumes no RNG.
[[nodiscard]] WeatherKind roll_weather_kind(std::uint32_t seed) noexcept;

// Process-wide nonce mixed into the roll seed alongside the level id. It
// DEFAULTS TO 0 so every test build (and any embedding that never sets it)
// rolls deterministically from the level id alone; the embedding mains (SDL
// client, dedicated server, text client) inject a one-shot clock sample at
// startup so real sessions vary between launches. Replay note: replays that
// record snapshots carry the recorded kind; input-only replays re-roll under
// the replayer's own nonce — accepted cosmetic variance (weather is
// gameplay-inert).
void set_weather_roll_nonce(std::uint32_t nonce) noexcept;
[[nodiscard]] std::uint32_t weather_roll_nonce() noexcept;

// Monotonic per-process roll sequence, mixed into every roll seed so each
// authoritative level load re-rolls FRESH weather: replaying a level within
// one session is not glued to the launch nonce's single outcome (a launch
// that rolled a dry level 1 would otherwise stay dry forever).
// Reproducibility contract: same (level id, nonce, sequence) => same kind —
// tests reset the sequence to pin rolls.
std::uint32_t next_weather_roll_sequence() noexcept; // returns, then bumps
void set_weather_roll_sequence(std::uint32_t value) noexcept;
[[nodiscard]] std::uint32_t weather_roll_sequence() noexcept;

} // namespace og
