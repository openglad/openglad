/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/weather.h>

namespace {

// xorshift-multiply finalizer (same family as the render effects' hash):
// decorrelates neighboring level ids so campaign level N and N+1 do not walk
// the residue table in lockstep. No libc rand(), no sim RNG.
std::uint32_t hash_u32(std::uint32_t v) noexcept
{
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v;
}

std::uint32_t g_weather_roll_nonce = 0;
std::uint32_t g_weather_roll_sequence = 0;

} // namespace

namespace og {

WeatherKind roll_weather_kind(std::uint32_t seed) noexcept
{
    return weather_kind_from_residue(hash_u32(seed) % 10u);
}

void set_weather_roll_nonce(std::uint32_t nonce) noexcept
{
    g_weather_roll_nonce = nonce;
}

std::uint32_t weather_roll_nonce() noexcept
{
    return g_weather_roll_nonce;
}

std::uint32_t next_weather_roll_sequence() noexcept
{
    return g_weather_roll_sequence++;
}

void set_weather_roll_sequence(std::uint32_t value) noexcept
{
    g_weather_roll_sequence = value;
}

std::uint32_t weather_roll_sequence() noexcept
{
    return g_weather_roll_sequence;
}

} // namespace og
