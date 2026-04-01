#include <openglad/platform/frame_pacing.h>
#include <openglad/core/util.h>

#include <algorithm>

namespace og::runtime {

namespace {
} // namespace

float render_tick_interval_ms(short timer_wait, float speed_factor)
{
    return og::core::rounded_render_tick_interval_ms(timer_wait, speed_factor);
}

std::uint32_t browser_frame_target_interval_ms(
    short timer_wait,
    float speed_factor)
{
    const float target_interval_ms =
        render_tick_interval_ms(timer_wait, speed_factor);
    if (target_interval_ms <= 0.0f)
        return 0u;

    return std::max<std::uint32_t>(
        16u, static_cast<std::uint32_t>(target_interval_ms));
}

BrowserFramePacingResult step_browser_frame_pacing(
    std::uint32_t accumulated_time_ms,
    std::uint32_t delta_ms,
    short timer_wait,
    float speed_factor)
{
    BrowserFramePacingResult result;
    result.target_interval_ms =
        browser_frame_target_interval_ms(timer_wait, speed_factor);
    if (result.target_interval_ms == 0u)
    {
        result.should_run_frame = true;
        result.accumulated_after_add_ms = accumulated_time_ms + delta_ms;
        result.accumulated_after_step_ms = 0u;
        return result;
    }

    result.accumulated_after_add_ms = accumulated_time_ms + delta_ms;
    result.should_run_frame =
        result.target_interval_ms > 0u &&
        result.accumulated_after_add_ms >= result.target_interval_ms;
    const std::uint32_t presentation_threshold_ms =
        std::max<std::uint32_t>(1u, (result.target_interval_ms + 1u) / 2u);
    result.should_present_frame =
        result.target_interval_ms > 0u &&
        !result.should_run_frame &&
        accumulated_time_ms < presentation_threshold_ms &&
        result.accumulated_after_add_ms >= presentation_threshold_ms;
    result.accumulated_after_step_ms = result.accumulated_after_add_ms;

    if (result.should_run_frame)
        result.accumulated_after_step_ms = 0u;

    return result;
}

} // namespace og::runtime
