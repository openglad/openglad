/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// GameWorld::roll_weather lives in its own TU (not game_world.cpp) on
// purpose: the parity mutation canary pins exact line numbers inside
// game_world.cpp, so that file must not gain lines above its pins.

#include <openglad/gameplay/game_world.h>

#include <openglad/core/pixdefs.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/weather.h>

#include <cstdint>

void GameWorld::roll_weather()
{
    // Seed = level id mixed with the process-wide nonce (default 0, so test
    // builds reproduce the same kind for the same level id across runs).
    // Pure hash — the sim RNG stream is untouched by design (parity).
    // The golden-ratio-strided roll SEQUENCE varies per roll, so retrying a
    // level re-rolls fresh weather instead of repeating the launch's one
    // outcome. Same (id, nonce, sequence) => same kind.
    const std::uint32_t seq = og::next_weather_roll_sequence();
    const std::uint32_t seed = static_cast<std::uint32_t>(id) ^
        og::weather_roll_nonce() ^ (seq * 0x9E3779B9u);
    weather_ = og::roll_weather_kind(seed);
    // Terrain override AFTER the roll: blizzard maps are always snowing.
    // The hash roll and sequence bump above run identically either way
    // (reproducibility contract, parity goldens); counting grid bytes
    // touches no RNG, and clients never roll (snapshot-fed).
    int snow_tiles = 0;
    for (int f = 0;
         f < floor_count() && snow_tiles < og::kSnowWeatherTileThreshold; f++)
    {
        const PixieData& g = grid_for_floor(f);
        if (!g.valid())
            continue;
        const int n = static_cast<int>(g.w) * static_cast<int>(g.h);
        for (int i = 0;
             i < n && snow_tiles < og::kSnowWeatherTileThreshold; i++)
        {
            const unsigned char t = static_cast<unsigned char>(g.data[i]);
            if (t == PIX_SNOW1 || t == PIX_SNOW2)
                snow_tiles++;
        }
    }
    if (snow_tiles >= og::kSnowWeatherTileThreshold)
        weather_ = WeatherKind::Snow;
    TRACE("game", "weather roll=%d seed=%u seq=%u",
          static_cast<int>(weather_), seed, seq);
}
