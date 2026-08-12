#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/util.h>

#include <algorithm>

namespace og::core {

float render_tick_interval_ms(short timer_wait, float speed_factor)
{
    return rounded_render_tick_interval_ms(timer_wait, speed_factor);
}

std::uint32_t browser_frame_target_interval_ms(int fps)
{
    if (fps <= 0)
        return 0u;
    return std::max<std::uint32_t>(16u, target_frame_interval_ms(fps));
}

BrowserFramePacingResult step_browser_frame_pacing(
    std::uint32_t accumulated_time_ms,
    std::uint32_t delta_ms,
    std::uint32_t sim_interval_ms)
{
    BrowserFramePacingResult result;
    result.target_interval_ms = sim_interval_ms;
    result.accumulated_after_add_ms = accumulated_time_ms + delta_ms;
    if (sim_interval_ms == 0u)
    {
        result.should_run_frame = true;
        result.should_present_frame = false;
        result.accumulated_after_step_ms = 0u;
        return result;
    }

    result.should_run_frame =
        result.accumulated_after_add_ms >= sim_interval_ms;
    result.should_present_frame = !result.should_run_frame;
    result.accumulated_after_step_ms =
        result.should_run_frame ? 0u : result.accumulated_after_add_ms;

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

FixedStepAccumulator::FixedStepAccumulator(std::chrono::microseconds period,
                                           int max_catchup)
    : period_(std::max(std::chrono::microseconds{1}, period)),
      max_catchup_(max_catchup),
      accumulated_(period_)
{
}

FixedStepAccumulator::Result FixedStepAccumulator::advance(
    std::chrono::microseconds elapsed)
{
    accumulated_ += elapsed;

    // Duration division is exact integer math; the remainder stays in
    // accumulated_ so fractional periods carry across calls without drift.
    const auto due = accumulated_ / period_;
    if (due > max_catchup_)
    {
        accumulated_ = std::chrono::microseconds::zero();
        return Result{max_catchup_, static_cast<int>(due) - max_catchup_};
    }

    accumulated_ -= due * period_;
    return Result{static_cast<int>(due), 0};
}

void FixedStepAccumulator::reset()
{
    accumulated_ = period_;
}

} // namespace og::core
