#include "parity_event_log.h"

#include <cstdio>
#include <cstdlib>

namespace og::parity {
namespace {

std::vector<RecordedEvent> g_events;
std::uint32_t              g_tick = 0;
std::uint32_t              g_rng_state = 0;

} // namespace

void reset_event_log()
{
    g_events.clear();
    g_tick = 0;
    g_rng_state = 0;
}

void set_event_tick(std::uint32_t tick)
{
    g_tick = tick;
}

std::uint32_t current_event_tick()
{
    return g_tick;
}

void record_event(std::uint32_t kind,
                  std::uint32_t a,
                  std::uint32_t b,
                  std::string text)
{
    RecordedEvent ev;
    ev.kind     = kind;
    ev.tick     = g_tick;
    ev.a        = a;
    ev.b        = b;
    ev.text     = std::move(text);
    ev.sequence = static_cast<std::uint32_t>(g_events.size());
    g_events.push_back(std::move(ev));
}

const std::vector<RecordedEvent>& event_log()
{
    return g_events;
}

void seed_rng_observable(std::uint32_t seed)
{
    g_rng_state = seed;
}

void observe_random(std::uint32_t bound, std::uint32_t value)
{
    g_rng_state ^= bound + 0x9E3779B9u + (g_rng_state << 6) + (g_rng_state >> 2);
    g_rng_state ^= value + 0x85EBCA6Bu + (g_rng_state << 13) + (g_rng_state >> 7);
}

std::uint32_t rng_observable_state()
{
    return g_rng_state;
}

} // namespace og::parity
