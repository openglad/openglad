#pragma once

#include <cstdint>

namespace og::runtime {

// Pure-logic helpers for the Emscripten frame wrapper's timing/state machine.
// Extracted here so the algorithms can be unit-tested without an SDL or
// Emscripten dependency.

// Calculate the target frame time in milliseconds from the game's timer_wait
// value (in ticks, 1 tick ~= 13.6 ms) and a speed factor.
// Returns 0 when speed_factor == 0 (max-speed mode).
inline constexpr std::uint32_t target_frame_ms(short timer_wait, float speed_factor)
{
    if (speed_factor == 0.0f)
        return 0;
    auto raw = static_cast<std::uint32_t>(timer_wait * 13.6f / speed_factor);
    return raw < 16 ? 16 : raw;  // min ~60 FPS cap
}

// Determine whether the accumulated time is enough to run a logic frame,
// and update the accumulator accordingly.  Returns true when a frame should run.
// Also applies the spiral-of-death clamp: if the accumulator exceeds
// 2x the target, it is reset to 0.
struct FrameTickResult {
    bool should_run = false;
    std::uint32_t new_accumulated = 0;
};

inline constexpr FrameTickResult frame_tick(std::uint32_t accumulated_time,
                                            std::uint32_t target_frame_time)
{
    FrameTickResult r;
    if (accumulated_time >= target_frame_time) {
        r.should_run = true;
        std::uint32_t remaining = accumulated_time - target_frame_time;
        // Spiral-of-death clamp
        if (remaining > target_frame_time * 2)
            remaining = 0;
        r.new_accumulated = remaining;
    } else {
        r.should_run = false;
        r.new_accumulated = accumulated_time;
    }
    return r;
}

// State machine used by the Emscripten frame wrapper.
// Mirrors the GameState enum in glad.cpp but is available for headless testing.
enum class WebGameState : std::uint8_t {
    Intro   = 0,
    Picker  = 1,
    Playing = 2,
    Quit    = 3,
};

// Transition rules (pure logic, no side effects):
//   Picker  + picker_frame_done  -> Playing  (g_state_initialized = false)
//   Playing + frame_done         -> Picker   (g_state_initialized = false)
//   Playing + missing_screen     -> Quit
struct StateTransition {
    WebGameState next_state;
    bool state_initialized;
};

inline constexpr StateTransition transition_from_picker(bool picker_frame_done,
                                                        bool currently_initialized)
{
    if (picker_frame_done)
        return {WebGameState::Playing, false};
    return {WebGameState::Picker, currently_initialized};
}

inline constexpr StateTransition transition_from_playing(bool frame_done,
                                                         bool missing_screen,
                                                         bool currently_initialized)
{
    if (missing_screen)
        return {WebGameState::Quit, false};
    if (frame_done)
        return {WebGameState::Picker, false};
    return {WebGameState::Playing, currently_initialized};
}

} // namespace og::runtime
