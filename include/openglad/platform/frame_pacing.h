#pragma once

#include <cstdint>

namespace og::runtime {

float render_tick_interval_ms(short timer_wait, float speed_factor);
std::uint32_t browser_frame_target_interval_ms(
    short timer_wait,
    float speed_factor);

struct BrowserFramePacingResult
{
    std::uint32_t target_interval_ms = 0;
    bool should_run_frame = false;
    bool should_present_frame = false;
    std::uint32_t accumulated_after_add_ms = 0;
    std::uint32_t accumulated_after_step_ms = 0;
};

BrowserFramePacingResult step_browser_frame_pacing(
    std::uint32_t accumulated_time_ms,
    std::uint32_t delta_ms,
    short timer_wait,
    float speed_factor);

} // namespace og::runtime
