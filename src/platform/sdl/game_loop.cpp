/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_loop.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/legacy/colors.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/obmap_debug_draw.h>
#include <openglad/interface/replay_runtime.h>
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
        return {
            .interval_ms = 0,
            .caller_manages_timing = true,
            .immediate_tick = true,
        };
    }

    float interval_ms = deps.fixed_tick_ms > 0
        ? static_cast<float>(deps.fixed_tick_ms)
        : static_cast<float>(std::max<short>(s.world().timer_wait, 0)) *
            og::sim::TIMER_WAIT_TO_MS;

    const float speed_factor = (og::runtime::current_session != nullptr)
        ? og::runtime::current_session->g_game_speed_factor_
        : 1.0f;
    if (speed_factor <= 0.0f || interval_ms <= 0.0f)
        return {.interval_ms = 0, .caller_manages_timing = false, .immediate_tick = true};

    interval_ms /= speed_factor;
    if (interval_ms <= 0.0f)
        return {.interval_ms = 0, .caller_manages_timing = false, .immediate_tick = true};

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
                                     const TickSchedule& schedule)
{
    if (schedule.caller_manages_timing)
        return 1;

    const std::uint32_t current_time_ms = SDL_GetTicks();
    if (schedule.immediate_tick)
    {
        reset_frame_timing(st, current_time_ms);
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
        return 0;

    std::uint32_t ticks_to_run = st.accumulated_time / schedule.interval_ms;
    if (ticks_to_run > kMaxTicksPerCall)
    {
        st.accumulated_time = 0;
        return kMaxTicksPerCall;
    }

    st.accumulated_time -= ticks_to_run * schedule.interval_ms;
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

GameFrameResult run_game_tick(screen& s,
                              GameLoopFrameState& st,
                              const GameLoopDeps& deps,
                              const InputState& input)
{
    og::runtime::record_replay_input(s, input);
    const bool use_local_transport =
        og::runtime::current_session != nullptr &&
        og::runtime::local_transport_active(*og::runtime::current_session);
    if (use_local_transport)
    {
        og::runtime::local_transport_shadow_send_input(
            *og::runtime::current_session,
            input,
            s.world().tick_count_ + 1);
    }

    s.process_input(input);
    s.continuous_input();

    if (s.world().end)
        return finish_done(st);

    if (use_local_transport)
        og::runtime::local_transport_shadow_finish_tick(
            *og::runtime::current_session);
    else
        s.act();
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

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    const std::function<int(SDL_Event*)> poll_event =
        deps.poll_event ? deps.poll_event : std::function<int(SDL_Event*)>(default_poll);
    const std::function<void(const SDL_Event&)> handle_event =
        deps.handle_event ? deps.handle_event : std::function<void(const SDL_Event&)>(default_handle);

    if (s.redrawme)
    {
#ifndef TESTING
        if (deps.enable_render) {
            s.draw_panels(s.numviews);
            score_panel(&s, 1);
            s.buffer_to_screen(0, 0, 320, 200);
        }
#endif
        s.redrawme = 0;
    }

    if (s.world().end || st.done)
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
                    bool result = yes_or_no_prompt("Abort Mission", "Quit this mission?", false);
                    s.redrawme = 1;
                    if (result) // player wants to quit
                    {
                        st.done = true;
                        og::runtime::finish_replay_recording();
                        results_screen(2, -1); // Should not show an extra popup
                        return GameFrameResult::AbortedMission;
                    }
                    else
                    {
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

    const std::uint32_t ticks_to_run = ticks_to_run_this_call(st, schedule);
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
