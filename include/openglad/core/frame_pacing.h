#pragma once

#include <cstdint>

namespace og::core {

float render_tick_interval_ms(short timer_wait, float speed_factor);

// Returns the per-frame interval the browser pacer should target, derived from
// the configured target_fps. The browser cannot present faster than the host
// rAF cadence (~16 ms), so values below that floor are clamped up. When
// `fps <= 0`, the legacy "fast mode" sentinel is preserved by returning 0,
// which tells the pacer to run every callback without accumulating.
std::uint32_t browser_frame_target_interval_ms(int fps);

struct BrowserFramePacingResult
{
    std::uint32_t target_interval_ms = 0;
    bool should_run_frame = false;
    // True on every browser callback that did not run a sim tick — i.e.
    // equals !should_run_frame whenever sim_interval_ms > 0. Browser render
    // is bounded by the host rAF (~16 ms), so presenting on every non-sim
    // callback gives smoothest render fps without exceeding host rate.
    bool should_present_frame = false;
    std::uint32_t accumulated_after_add_ms = 0;
    std::uint32_t accumulated_after_step_ms = 0;
};

BrowserFramePacingResult step_browser_frame_pacing(
    std::uint32_t accumulated_time_ms,
    std::uint32_t delta_ms,
    std::uint32_t sim_interval_ms);

struct FrameDeadlineDecision
{
    bool run_tick = false;
    bool run_render = false;
    std::uint32_t sleep_ms = 0;
    std::uint32_t next_deadline_ms = 0;
};

class FrameDeadlinePacer
{
public:
    void configure(std::uint32_t interval_ms, std::uint32_t now_ms);
    FrameDeadlineDecision tick(std::uint32_t now_ms);
    void reset(std::uint32_t now_ms);
    std::uint32_t interval_ms() const { return interval_ms_; }

private:
    std::uint32_t interval_ms_ = 0;
    std::uint32_t next_deadline_ms_ = 0;
    bool initialized_ = false;
};

} // namespace og::core
