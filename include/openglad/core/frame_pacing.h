#pragma once

#include <cstdint>

namespace og::core {

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
