#include <openglad/runtime/frame_timing.h>

#include "unit.h"

using namespace og::runtime;

// ---------------------------------------------------------------------------
// target_frame_ms
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_target_frame_ms_default_timer_wait)
{
    // Default timer_wait=6, speed_factor=1.0 => 6 * 13.6 = 81.6 => 81
    auto ms = target_frame_ms(6, 1.0f);
    OG_ASSERT(ms == 81);
}

OG_UNIT_TEST(test_target_frame_ms_max_speed)
{
    // speed_factor=0 means max speed: run every browser frame
    auto ms = target_frame_ms(6, 0.0f);
    OG_ASSERT(ms == 0);
}

OG_UNIT_TEST(test_target_frame_ms_double_speed)
{
    // speed_factor=2.0 => 6 * 13.6 / 2 = 40.8 => 40
    auto ms = target_frame_ms(6, 2.0f);
    OG_ASSERT(ms == 40);
}

OG_UNIT_TEST(test_target_frame_ms_half_speed)
{
    // speed_factor=0.5 => 6 * 13.6 / 0.5 = 163.2 => 163
    auto ms = target_frame_ms(6, 0.5f);
    OG_ASSERT(ms == 163);
}

OG_UNIT_TEST(test_target_frame_ms_clamps_to_16ms_minimum)
{
    // Very small timer_wait with large speed factor => should clamp to 16ms
    auto ms = target_frame_ms(1, 10.0f);
    // 1 * 13.6 / 10 = 1.36 => rounds to 1 => clamped to 16
    OG_ASSERT(ms == 16);
}

OG_UNIT_TEST(test_target_frame_ms_timer_wait_1_normal_speed)
{
    // timer_wait=1, speed=1 => 13.6 => 13 => clamped to 16
    auto ms = target_frame_ms(1, 1.0f);
    OG_ASSERT(ms == 16);
}

// ---------------------------------------------------------------------------
// frame_tick
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_frame_tick_not_enough_time)
{
    auto r = frame_tick(50, 81);
    OG_ASSERT(!r.should_run);
    OG_ASSERT(r.new_accumulated == 50);
}

OG_UNIT_TEST(test_frame_tick_exact_time)
{
    auto r = frame_tick(81, 81);
    OG_ASSERT(r.should_run);
    OG_ASSERT(r.new_accumulated == 0);
}

OG_UNIT_TEST(test_frame_tick_with_remainder)
{
    auto r = frame_tick(100, 81);
    OG_ASSERT(r.should_run);
    OG_ASSERT(r.new_accumulated == 19);
}

OG_UNIT_TEST(test_frame_tick_spiral_of_death_clamp)
{
    // accumulated > target * 3 (which is > target * 2 remainder)
    // 300 - 81 = 219, and 219 > 81*2=162, so clamp to 0
    auto r = frame_tick(300, 81);
    OG_ASSERT(r.should_run);
    OG_ASSERT(r.new_accumulated == 0);
}

OG_UNIT_TEST(test_frame_tick_just_under_spiral_clamp)
{
    // target=100, accumulated=250 => remainder=150, 150 <= 200 => no clamp
    auto r = frame_tick(250, 100);
    OG_ASSERT(r.should_run);
    OG_ASSERT(r.new_accumulated == 150);
}

OG_UNIT_TEST(test_frame_tick_just_over_spiral_clamp)
{
    // target=100, accumulated=351 => remainder=251, 251 > 200 => clamp to 0
    auto r = frame_tick(351, 100);
    OG_ASSERT(r.should_run);
    OG_ASSERT(r.new_accumulated == 0);
}

OG_UNIT_TEST(test_frame_tick_max_speed_zero_target)
{
    // target=0 (max speed) => always runs, remainder=accumulated-0=accumulated
    auto r = frame_tick(0, 0);
    OG_ASSERT(r.should_run);
    OG_ASSERT(r.new_accumulated == 0);

    // With some accumulated time, target=0: 16 >= 0 so should_run
    auto r2 = frame_tick(16, 0);
    OG_ASSERT(r2.should_run);
    // remainder = 16, 16 > 0*2=0 so spiral clamp triggers => 0
    OG_ASSERT(r2.new_accumulated == 0);
}

// ---------------------------------------------------------------------------
// State machine transitions
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_transition_from_picker_not_done)
{
    auto t = transition_from_picker(false, true);
    OG_ASSERT(t.next_state == WebGameState::Picker);
    OG_ASSERT(t.state_initialized == true);
}

OG_UNIT_TEST(test_transition_from_picker_done)
{
    auto t = transition_from_picker(true, true);
    OG_ASSERT(t.next_state == WebGameState::Playing);
    OG_ASSERT(t.state_initialized == false);
}

OG_UNIT_TEST(test_transition_from_playing_not_done)
{
    auto t = transition_from_playing(false, false, true);
    OG_ASSERT(t.next_state == WebGameState::Playing);
    OG_ASSERT(t.state_initialized == true);
}

OG_UNIT_TEST(test_transition_from_playing_done)
{
    auto t = transition_from_playing(true, false, true);
    OG_ASSERT(t.next_state == WebGameState::Picker);
    OG_ASSERT(t.state_initialized == false);
}

OG_UNIT_TEST(test_transition_from_playing_missing_screen)
{
    auto t = transition_from_playing(false, true, true);
    OG_ASSERT(t.next_state == WebGameState::Quit);
    OG_ASSERT(t.state_initialized == false);
}

OG_UNIT_TEST(test_transition_from_playing_missing_screen_takes_priority)
{
    // Even if frame_done is true, missing_screen should take priority => Quit
    auto t = transition_from_playing(true, true, true);
    OG_ASSERT(t.next_state == WebGameState::Quit);
    OG_ASSERT(t.state_initialized == false);
}

// ---------------------------------------------------------------------------
// Full state machine cycle simulation
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_state_machine_full_cycle)
{
    // Simulate a full web game lifecycle:
    // Picker -> Playing -> Picker -> Quit-eligible
    WebGameState state = WebGameState::Picker;
    bool initialized = true;

    // Picker running, not done yet
    auto t1 = transition_from_picker(false, initialized);
    OG_ASSERT(t1.next_state == WebGameState::Picker);
    state = t1.next_state;
    initialized = t1.state_initialized;

    // Picker done, transition to Playing
    auto t2 = transition_from_picker(true, initialized);
    state = t2.next_state;
    initialized = t2.state_initialized;
    OG_ASSERT(state == WebGameState::Playing);
    OG_ASSERT(initialized == false); // needs initialization

    // Playing, not done
    initialized = true; // simulate init
    auto t3 = transition_from_playing(false, false, initialized);
    state = t3.next_state;
    initialized = t3.state_initialized;
    OG_ASSERT(state == WebGameState::Playing);

    // Playing done, back to Picker
    auto t4 = transition_from_playing(true, false, initialized);
    state = t4.next_state;
    initialized = t4.state_initialized;
    OG_ASSERT(state == WebGameState::Picker);
    OG_ASSERT(initialized == false);
}

// ---------------------------------------------------------------------------
// Timing simulation: multiple frames at 60 FPS browser callback rate
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_timing_simulation_60fps_browser)
{
    // Simulate a browser calling at ~60 FPS (16-17ms per call)
    // with game running at ~12 FPS (timer_wait=6 => ~82ms target)
    const std::uint32_t target = target_frame_ms(6, 1.0f);
    OG_ASSERT(target == 81);

    std::uint32_t accumulated = 0;
    int frames_run = 0;

    // Simulate 300ms of browser time at ~60 FPS
    for (int i = 0; i < 18; ++i) {
        accumulated += 17; // ~60 FPS
        auto r = frame_tick(accumulated, target);
        if (r.should_run) {
            ++frames_run;
        }
        accumulated = r.new_accumulated;
    }

    // 300ms / 81ms per frame ~= 3.7 => should get 3 or 4 game frames
    OG_ASSERT(frames_run >= 3 && frames_run <= 4);
}

OG_UNIT_TEST(test_timing_simulation_max_speed)
{
    // Max speed: every browser frame should produce a game frame
    const std::uint32_t target = target_frame_ms(6, 0.0f);
    OG_ASSERT(target == 0);

    std::uint32_t accumulated = 0;
    int frames_run = 0;

    for (int i = 0; i < 10; ++i) {
        accumulated += 16;
        auto r = frame_tick(accumulated, target);
        if (r.should_run) {
            ++frames_run;
        }
        accumulated = r.new_accumulated;
    }

    OG_ASSERT(frames_run == 10);
}
