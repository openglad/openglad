#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace og::parity {

enum EventKind : std::uint32_t
{
    kEventNone                    = 0,
    kEventPlaySound               = 1,
    kEventNotification            = 2,
    kEventSetPalette              = 3,
    kEventRequestRedraw           = 4,
    kEventEndGame                 = 5,
    kEventSetEnd                  = 6,
    kEventRequestExitConfirmation = 7,
    kEventWithdrawToLevel         = 8,
    kEventScoreChange             = 9,
};

struct RecordedEvent
{
    std::uint32_t kind     = 0;
    std::uint32_t tick     = 0;
    std::uint32_t a        = 0;
    std::uint32_t b        = 0;
    std::string   text;
    std::uint32_t sequence = 0;
};

void reset_event_log();
void set_event_tick(std::uint32_t tick);
std::uint32_t current_event_tick();
void record_event(std::uint32_t kind,
                  std::uint32_t a = 0,
                  std::uint32_t b = 0,
                  std::string text = {});
const std::vector<RecordedEvent>& event_log();

void seed_rng_observable(std::uint32_t seed);
void observe_random(std::uint32_t bound, std::uint32_t value);
std::uint32_t rng_observable_state();

} // namespace og::parity
