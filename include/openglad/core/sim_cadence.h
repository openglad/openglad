#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <openglad/core/util.h>

namespace og::core {

struct SimCadenceInputs {
    short        timer_wait;
    float        speed_factor;
    std::uint32_t fixed_tick_ms;
    bool         enable_frame_timing;
};

struct SimCadenceResult {
    std::uint32_t interval_ms;
    bool          caller_manages_timing;
    bool          immediate_tick;
    const char*   trace_event;
};

[[nodiscard]] inline SimCadenceResult compute_sim_interval_ms(const SimCadenceInputs& in)
{
    if (!in.enable_frame_timing) {
        return { 0u, true, true, "schedule_external_timing" };
    }
    if (in.fixed_tick_ms > 0u) {
        return { in.fixed_tick_ms, false, false, "schedule_fixed_interval" };
    }
    if (in.speed_factor <= 0.0f) {
        return { 1u, false, false, "schedule_timer_wait_interval" };
    }
    const float scaled = rounded_render_tick_interval_ms(in.timer_wait, in.speed_factor);
    const std::uint32_t interval = std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::lround(scaled)));
    return { interval, false, false, "schedule_timer_wait_interval" };
}

} // namespace og::core
