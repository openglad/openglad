/* Copyright (C) 1995-2002 FSGames. Ported by Sean Ford and Yan Shosh.
 *
 * Process-level platform functions for the SDL-free text client. Keeping
 * these outside main.cpp lets the headless unit binary link and verify the
 * same implementations used by the production executable.
 */

#include <cstdint>
#include <cstdio>

namespace
{
void write_popup_message(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t advance_legacy_random(std::uint32_t& state, std::uint32_t bound)
{
    state = state * 1103515245u + 12345u;
    return (state >> 16) % bound;
}
} // namespace

// SDL builds show a modal dialog; the text client reports it on stderr.
void popup_dialog(const char* title, const char* message)
{
    write_popup_message(title, message);
}

// Legacy entropy source used by ProductionRandom. Deterministic simulation
// uses its per-world RNG; this only backs non-authoritative scratch randomness.
std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 12345;
    if (x == 0)
        return 0;
    return advance_legacy_random(state, x);
}
