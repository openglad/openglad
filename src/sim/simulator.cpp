/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/sim/simulator.h>

namespace og::sim {

Simulator::Simulator(std::uint32_t seed)
    : rng_state_(seed)
{
    st_.tick = 0;
    st_.acc = 0;
    st_.entity_count = 0;
}

std::uint32_t Simulator::next_u32()
{
    // Simple LCG: deterministic across platforms (same integer operations).
    rng_state_ = rng_state_ * 1103515245u + 12345u;
    return rng_state_;
}

void Simulator::emit(EventKind kind, std::uint32_t a, std::uint32_t b)
{
    Event ev;
    ev.tick = st_.tick;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    events_.push_back(ev);
}

void Simulator::step(const Input& in)
{
    // Determinism contract: given the same seed + same input sequence,
    // state + event stream must be identical.
    st_.tick++;

    // A tiny, stable "state update" rule.
    const std::uint32_t r = next_u32();
    st_.acc = st_.acc + (in.cmd * 3u) + (in.value * 7u) + (r >> 16);

    // Emit one event per step for now.
    emit(static_cast<EventKind>(in.cmd), in.value, r);
}

void Simulator::step(const InputSnapshot& snapshot, float /*dt*/)
{
    st_.tick++;

    const std::uint32_t r = next_u32();

    // Accumulate all player inputs deterministically.
    std::uint32_t input_hash = 0;
    for (int p = 0; p < InputSnapshot::MAX_PLAYERS; ++p) {
        input_hash += snapshot.players[p].cmd * (3u + static_cast<std::uint32_t>(p));
        input_hash += snapshot.players[p].value * (7u + static_cast<std::uint32_t>(p));
    }

    st_.acc = st_.acc + input_hash + (r >> 16);

    // Emit a tick event.
    emit(EventKind::None, st_.tick, r);
}

void Simulator::step(const CommandSnapshot& commands, float /*dt*/)
{
    st_.tick++;

    const std::uint32_t r = next_u32();

    // Accumulate all player command bits deterministically.
    std::uint32_t input_hash = 0;
    for (int p = 0; p < CommandSnapshot::MAX_PLAYERS; ++p) {
        input_hash += static_cast<std::uint32_t>(commands.players[p].commands) *
                      (3u + static_cast<std::uint32_t>(p));
    }

    st_.acc = st_.acc + input_hash + (r >> 16);

    // Emit a tick event.
    emit(EventKind::None, st_.tick, r);
}

} // namespace og::sim
