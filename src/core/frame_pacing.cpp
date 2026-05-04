#include <openglad/core/frame_pacing.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/util.h>

#include <algorithm>

namespace og::core {

float render_tick_interval_ms(short timer_wait, float speed_factor)
{
    return rounded_render_tick_interval_ms(timer_wait, speed_factor);
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

void FrameDeadlinePacer::configure(std::uint32_t interval_ms, std::uint32_t now_ms)
{
    interval_ms_ = std::max<std::uint32_t>(1u, interval_ms);
    next_deadline_ms_ = now_ms + interval_ms_;
    initialized_ = true;
}

void FrameDeadlinePacer::reset(std::uint32_t now_ms)
{
    next_deadline_ms_ = now_ms + std::max<std::uint32_t>(1u, interval_ms_);
}

FrameDeadlineDecision FrameDeadlinePacer::tick(std::uint32_t now_ms)
{
    if (!initialized_)
    {
        configure(1u, now_ms);
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record("frame_pacing", "pacer_resync"));
        return FrameDeadlineDecision{false, false, 1u, now_ms + 1u};
    }

    if (now_ms < next_deadline_ms_)
    {
        return FrameDeadlineDecision{
            false,
            false,
            next_deadline_ms_ - now_ms,
            next_deadline_ms_};
    }

    const std::uint32_t slip = now_ms - next_deadline_ms_;
    if (slip <= interval_ms_)
    {
        next_deadline_ms_ += interval_ms_;
        return FrameDeadlineDecision{true, true, 0u, next_deadline_ms_};
    }

    next_deadline_ms_ = now_ms + interval_ms_;
    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record("frame_pacing", "pacer_resync"));
    return FrameDeadlineDecision{true, true, 0u, next_deadline_ms_};
}

} // namespace og::core
