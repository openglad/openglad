#include <openglad/sim/simulator.h>

namespace og::sim {

Simulator::Simulator(std::uint32_t seed)
    : rng_state_(seed)
{
    st_.tick = 0;
    st_.acc = 0;
}

std::uint32_t Simulator::next_u32()
{
    // Simple LCG: deterministic across platforms (same integer operations).
    rng_state_ = rng_state_ * 1103515245u + 12345u;
    return rng_state_;
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
    Event ev;
    ev.tick = st_.tick;
    ev.kind = in.cmd;
    ev.a = in.value;
    ev.b = r;
    events_.push_back(ev);
}

} // namespace og::sim

