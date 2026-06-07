/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/curses/clock.h>

#include <chrono>
#include <thread>

namespace og::curses {

std::uint64_t SteadyClock::now_ms()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void SteadyClock::sleep_ms(std::uint32_t ms)
{
    if (ms == 0)
        return;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace og::curses
