/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_loop.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/legacy/colors.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/frame_pacing.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/obmap_debug_draw.h>
#include <openglad/interface/replay_runtime.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>

#include <algorithm>
#include <cmath>

#ifdef TESTING
void picker_testing_mark_frame_advance();
#endif

// Declared elsewhere (glad.cpp).
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
short score_panel(screen* scr);
short score_panel(screen* scr, short do_it);


static int default_poll(SDL_Event* e)
{
    return SDL_PollEvent(e);
}

static void default_handle(const SDL_Event& e)
{
    handle_events(e);
}

static std::uint32_t default_now_ms()
{
    return SDL_GetTicks();
}

namespace
{
constexpr std::uint32_t kMaxTicksPerCall = 4;

struct TickSchedule {
    std::uint32_t interval_ms = 0;
    bool caller_manages_timing = false;
    bool immediate_tick = false;
};

GameFrameResult finish_done(GameLoopFrameState& st)
{
    st.done = true;
    st.has_pending_input = false;
    st.pending_input = {};
    og::runtime::finish_replay_recording();
    return GameFrameResult::Done;
}

TickSchedule compute_tick_schedule(const screen& s, const GameLoopDeps& deps)
{
    if (!deps.enable_frame_timing)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "schedule_external_timing"));
        return {
            .interval_ms = 0,
            .caller_manages_timing = true,
            .immediate_tick = true,
        };
    }

    const float speed_factor = (og::runtime::current_session != nullptr)
        ? og::runtime::current_session->g_game_speed_factor_
        : 1.0f;
    const float interval_ms = deps.fixed_tick_ms > 0
        ? (speed_factor > 0.0f
               ? static_cast<float>(deps.fixed_tick_ms) / speed_factor
               : 0.0f)
        : og::runtime::render_tick_interval_ms(
            s.world().timer_wait, speed_factor);
    if (interval_ms <= 0.0f)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "schedule_immediate_tick"));
        return {.interval_ms = 0, .caller_manages_timing = false, .immediate_tick = true};
    }

    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_loop",
            deps.fixed_tick_ms > 0 ? "schedule_fixed_interval"
                                   : "schedule_timer_wait_interval"));
    return {
        .interval_ms = std::max<std::uint32_t>(
            1u, static_cast<std::uint32_t>(std::lround(interval_ms))),
        .caller_manages_timing = false,
        .immediate_tick = false,
    };
}

void reset_frame_timing(GameLoopFrameState& st, std::uint32_t current_time_ms)
{
    st.initialized = true;
    st.last_frame_time = current_time_ms;
    st.accumulated_time = 0;
}

std::uint32_t ticks_to_run_this_call(GameLoopFrameState& st,
                                     const TickSchedule& schedule,
                                     const GameLoopDeps& deps)
{
    if (schedule.caller_manages_timing)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "ticks_caller_managed"));
        return 1;
    }

    const std::function<std::uint32_t()> now_ms =
        deps.now_ms ? deps.now_ms
                    : std::function<std::uint32_t()>(default_now_ms);
    const std::uint32_t current_time_ms = now_ms();
    if (schedule.immediate_tick)
    {
        reset_frame_timing(st, current_time_ms);
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "ticks_immediate"));
        return 1;
    }

    if (!st.initialized)
    {
        st.initialized = true;
        st.last_frame_time = current_time_ms;
        st.accumulated_time = schedule.interval_ms;
    }
    else
    {
        st.accumulated_time += current_time_ms - st.last_frame_time;
        st.last_frame_time = current_time_ms;
    }

    if (st.accumulated_time < schedule.interval_ms)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "ticks_waiting_for_interval"));
        return 0;
    }

    std::uint32_t ticks_to_run = st.accumulated_time / schedule.interval_ms;
    if (ticks_to_run > kMaxTicksPerCall)
    {
        st.accumulated_time = 0;
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "ticks_clamped_to_cap"));
        return kMaxTicksPerCall;
    }

    st.accumulated_time -= ticks_to_run * schedule.interval_ms;
    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_loop",
            ticks_to_run > 1u ? "ticks_multiple_ready" : "ticks_single_ready"));
    return ticks_to_run;
}

void latch_polled_input(GameLoopFrameState& st, const InputState& sampled_input)
{
    if (!st.has_pending_input)
    {
        st.pending_input = sampled_input;
        st.has_pending_input = true;
        return;
    }

    st.pending_input.quit_requested =
        st.pending_input.quit_requested || sampled_input.quit_requested;
    if (sampled_input.timer_wait_request != kNoTimerWaitRequest)
        st.pending_input.timer_wait_request = sampled_input.timer_wait_request;

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            st.pending_input.players[player].held[key] =
                sampled_input.players[player].held[key];
            st.pending_input.players[player].pressed[key] =
                st.pending_input.players[player].pressed[key] ||
                sampled_input.players[player].pressed[key];
        }
    }
}

void clear_pending_input(GameLoopFrameState& st)
{
    st.has_pending_input = false;
    st.pending_input = {};
}

og::runtime::GameSession* require_local_transport_session()
{
    og::runtime::GameSession* const gameplay_session =
        og::runtime::current_game_session;
    if (gameplay_session == nullptr)
    {
        LogError("game_frame_missing_game_session\n");
        return nullptr;
    }
    if (!og::runtime::local_transport_active(*gameplay_session))
    {
        LogError("game_frame_missing_local_transport_runtime\n");
        return nullptr;
    }
    return gameplay_session;
}

GameFrameResult run_game_tick(screen& s,
                              GameLoopFrameState& st,
                              const GameLoopDeps& deps,
                              const InputState& input)
{
    og::runtime::record_replay_input(s, input);
    og::runtime::GameSession* const gameplay_session =
        require_local_transport_session();
    if (gameplay_session == nullptr)
        return finish_done(st);

    // The per-frame order is:
    // 1-2. sample input in game_frame_with_result(), then enqueue it here;
    // 3-14. local_transport_shadow_finish_tick() runs the authoritative
    // server step and then drains the client mirror before render.
    og::runtime::local_transport_shadow_send_input(
        *gameplay_session,
        input,
        s.world().tick_count_ + 1);

    s.process_input(input);
    s.continuous_input();

    if (s.world().end)
        return finish_done(st);

    og::runtime::local_transport_shadow_finish_tick(*gameplay_session);
    s.framecount++;
#ifdef TESTING
    picker_testing_mark_frame_advance();
#endif
    if (deps.after_act)
        deps.after_act(s);

    if (s.world().end)
        return finish_done(st);

    if (s.cyclemode)
        s.do_cycle(st.currentcycle++, st.cycletime);

    if (st.done)
    {
        og::runtime::finish_replay_recording();
        return GameFrameResult::Done;
    }

    return GameFrameResult::Continue;
}
} // namespace

namespace og::runtime::detail {

void render_pending_redraw(screen& s, bool enable_render)
{
    if (!s.redrawme)
        return;

    if (enable_render)
    {
        s.draw_panels(s.numviews);
        score_panel(&s, 1);
        // Present once after the HUD overlay has been redrawn; otherwise
        // the overlay visibly flashes off for the intermediate frame.
        s.buffer_to_screen(0, 0, 320, 200);
    }

    s.redrawme = 0;
}

} // namespace og::runtime::detail

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    const std::function<int(SDL_Event*)> poll_event =
        deps.poll_event ? deps.poll_event : std::function<int(SDL_Event*)>(default_poll);
    const std::function<void(const SDL_Event&)> handle_event =
        deps.handle_event ? deps.handle_event : std::function<void(const SDL_Event&)>(default_handle);

    if (s.redrawme)
    {
#ifndef TESTING
        og::runtime::detail::render_pending_redraw(s, deps.enable_render);
#else
        s.redrawme = 0;
#endif
    }

    if (s.world().end || st.done)
        return finish_done(st);

    og::runtime::GameSession* const gameplay_session =
        require_local_transport_session();
    if (gameplay_session == nullptr)
        return finish_done(st);

    if (deps.enable_event_poll) {
        SDL_Event event;
        while (poll_event(&event))
        {
            handle_event(event);
            if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_F11)
                    og::runtime::current_session->debug_draw_paths_ = !og::runtime::current_session->debug_draw_paths_;
                else if (event.key.keysym.sym == SDLK_F12)
                    og::runtime::current_session->debug_draw_obmap_ = !og::runtime::current_session->debug_draw_obmap_;
                else if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    if (!og::runtime::local_transport_shadow_is_paused(
                            *gameplay_session) &&
                        og::runtime::local_transport_shadow_toggle_pause(
                            *gameplay_session))
                    {
                        s.redrawme = 1;
                        break;
                    }

                    bool result = yes_or_no_prompt("Abort Mission", "Quit this mission?", false);
                    s.redrawme = 1;
                    if (result) // player wants to quit
                    {
                        og::runtime::local_transport_shadow_abort_level(
                            *gameplay_session);
                        st.done = true;
                        og::runtime::finish_replay_recording();
                        results_screen(2, -1); // Should not show an extra popup
                        return GameFrameResult::AbortedMission;
                    }
                    else
                    {
                        (void)og::runtime::local_transport_shadow_toggle_pause(
                            *gameplay_session);
                        set_palette(s.ourpalette);  // restore normal palette
                        adjust_palette(s.ourpalette, s.viewob[0]->gamma);
                    }
                    break;
                }
            }

            s.input(&event);
        }
    }

    if (s.world().end || st.done)
        return finish_done(st);

    const TickSchedule schedule = compute_tick_schedule(s, deps);
    ctx().poll_input();
    latch_polled_input(st, ctx().input);

    const std::uint32_t ticks_to_run = ticks_to_run_this_call(st, schedule, deps);
    for (std::uint32_t tick = 0; tick < ticks_to_run; ++tick)
    {
        const GameFrameResult tick_result =
            run_game_tick(s, st, deps, st.pending_input);
        if (tick_result != GameFrameResult::Continue)
        {
            clear_pending_input(st);
            return tick_result;
        }
    }

    if (ticks_to_run > 0)
        clear_pending_input(st);

#ifndef TESTING
    if (deps.enable_render) {
        s.redraw();

        if (og::runtime::current_session->debug_draw_obmap_)
            obmap_debug_draw(*s.world().myobmap, &s);  // debug drawing for object collision map

#ifdef USE_TOUCH_INPUT
        draw_touch_controls(&s);
#endif
        score_panel(&s);
        s.refresh();
    }
#endif

    if (s.world().end || st.done)
        return finish_done(st);

    return GameFrameResult::Continue;
}

bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    return game_frame_with_result(s, st, deps) != GameFrameResult::Continue;
}
