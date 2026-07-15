/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_loop.h>
#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/sim_cadence.h>
#include <openglad/core/util.h>
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
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>

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
    // SDL3 SDL_GetTicks returns Uint64; the frame pacer works in uint32
    // modulo-2^32 elapsed arithmetic, so truncate at the seam.
    return static_cast<std::uint32_t>(SDL_GetTicks());
}

namespace
{
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
    const float speed_factor =
        (og::runtime::current_session != nullptr)
            ? og::runtime::current_session->g_game_speed_factor_
            : 1.0f;
    const og::core::SimCadenceInputs inputs{
        static_cast<short>(s.world().timer_wait),
        speed_factor,
        deps.fixed_tick_ms,
        deps.enable_frame_timing,
    };
    const og::core::SimCadenceResult result =
        og::core::compute_sim_interval_ms(inputs);

    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_loop", result.trace_event));

    return {
        .interval_ms = result.interval_ms,
        .caller_manages_timing = result.caller_manages_timing,
        .immediate_tick = result.immediate_tick,
    };
}

// Legacy entry point retained for the browser wrapper (which sets
// enable_frame_timing = false and relies on caller_manages_timing). The
// desktop main loop bypasses this function and uses FrameDeadlinePacer
// directly inside game_frame_with_result(). By contract this function
// returns 0 or 1 — the multi-tick catch-up burst has been removed.
std::uint32_t ticks_to_run_this_call(GameLoopFrameState& st,
                                     const TickSchedule& schedule,
                                     const GameLoopDeps& deps)
{
    (void)st;
    (void)deps;
    if (schedule.caller_manages_timing)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "ticks_caller_managed"));
        return 1;
    }

    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_loop", "ticks_single_ready"));
    return 1;
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

std::uint32_t render_interval_ms_for_session()
{
    const int target_fps =
        (og::runtime::current_session != nullptr)
            ? og::runtime::current_session->target_fps_
            : og::core::kDefaultTargetFps;
    return std::max<std::uint32_t>(
        1u, og::core::target_frame_interval_ms(target_fps));
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
        // Gameplay draws and presents on the WORLD canvas (menus present the
        // fixed 320x200 UI canvas); while the world canvas is also 320x200
        // the two share one surface, so this is a pure routing no-op.
        s.set_active_canvas(CanvasTarget::World);
        s.draw_panels(s.numviews);
        score_panel(&s, 1);
        // Present once after the HUD overlay has been redrawn; otherwise
        // the overlay visibly flashes off for the intermediate frame.
        s.buffer_to_screen(0, 0, s.canvas_w(), s.canvas_h());
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

    // Two-pacer split: sim_pacer gates event poll, input latching, and sim
    // tick on the master cadence (world.timer_wait), while render_pacer gates
    // the render path on target_fps. enable_frame_timing == false (browser
    // tick path) and enable_tick == false (browser render path) bypass both.
    bool render_this_frame = true;
    bool sim_tick_this_frame = true;
    const std::function<std::uint32_t()> now_ms_fn =
        deps.now_ms ? deps.now_ms
                    : std::function<std::uint32_t()>(default_now_ms);
    if (deps.enable_tick && deps.enable_frame_timing)
    {
        const TickSchedule schedule = compute_tick_schedule(s, deps);
        const std::uint32_t sim_interval = schedule.interval_ms;
        const std::uint32_t render_interval = render_interval_ms_for_session();
        const std::uint32_t now = now_ms_fn();
        if (st.sim_pacer.interval_ms() != sim_interval)
            st.sim_pacer.configure(sim_interval, now);
        if (deps.enable_render &&
            st.render_pacer.interval_ms() != render_interval)
            st.render_pacer.configure(render_interval, now);

        const og::core::FrameDeadlineDecision sim_decision =
            st.sim_pacer.tick(now);
        const og::core::FrameDeadlineDecision render_decision =
            deps.enable_render
                ? st.render_pacer.tick(now)
                : og::core::FrameDeadlineDecision{
                      false, false, UINT32_MAX, 0u};

        if (!sim_decision.run_tick && !render_decision.run_render)
        {
            og::runtime::emit_runtime_trace(
                og::runtime::make_runtime_trace_record(
                    "game_loop", "desktop_loop_sleep_ms"));
            const std::function<void(std::uint32_t)> sleep_fn =
                deps.sleep_ms
                    ? deps.sleep_ms
                    : std::function<void(std::uint32_t)>(
                          [](std::uint32_t ms) { SDL_Delay(ms); });
            sleep_fn(std::min(sim_decision.sleep_ms, render_decision.sleep_ms));
            return GameFrameResult::Continue;
        }
        sim_tick_this_frame = sim_decision.run_tick;
        render_this_frame = render_decision.run_render;
    }

    if (deps.enable_event_poll) {
        SDL_Event event;
        while (poll_event(&event))
        {
            handle_event(event);
            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_F11)
                    og::runtime::current_session->debug_draw_paths_ = !og::runtime::current_session->debug_draw_paths_;
                else if (event.key.key == SDLK_F12)
                    og::runtime::current_session->debug_draw_obmap_ = !og::runtime::current_session->debug_draw_obmap_;
                else if (event.key.key == SDLK_ESCAPE)
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
                        if (og::runtime::local_transport_shadow_abort_level(
                                *gameplay_session))
                        {
                            // Host / local / single-player: the mission ended
                            // authoritatively here.
                            st.done = true;
                            og::runtime::finish_replay_recording();
                            results_screen(2, -1); // Should not show an extra popup
                            return GameFrameResult::AbortedMission;
                        }

                        // Networked client: the server was asked to withdraw ALL
                        // players. Keep this display loop running until the
                        // server's terminal broadcast ends it (world.end != 0),
                        // so this client does not just leave with its character
                        // converted to AI.
                        break;
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

    ctx().poll_input();
    latch_polled_input(st, ctx().input);

    std::uint32_t ticks_to_run = 0;
    if (deps.enable_tick)
    {
        if (deps.enable_frame_timing)
        {
            // sim_pacer gates sim ticks independently of render_pacer; run
            // exactly one sim tick per call only when sim_decision fired.
            ticks_to_run = sim_tick_this_frame ? 1u : 0u;
        }
        else
        {
            const TickSchedule schedule = compute_tick_schedule(s, deps);
            ticks_to_run = ticks_to_run_this_call(st, schedule, deps);
        }
    }
    else
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_loop", "ticks_disabled"));
    }
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
    if (deps.enable_render && render_this_frame) {
        s.redraw();

        if (og::runtime::current_session->debug_draw_obmap_)
            obmap_debug_draw(*s.world().myobmap, &s);  // debug drawing for object collision map

#ifdef USE_TOUCH_INPUT
        {
            ScopedGameplayUiCanvas gameplay_ui(s);
            draw_touch_controls(&s);
        }
#endif
        score_panel(&s);
        s.refresh();
    }
#endif

    if (deps.on_render && render_this_frame)
        deps.on_render(s);

    if (s.world().end || st.done)
        return finish_done(st);

    return GameFrameResult::Continue;
}

bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    return game_frame_with_result(s, st, deps) != GameFrameResult::Continue;
}

void run_browser_wrapper_frame(screen& s,
                               GameLoopFrameState& st,
                               std::uint32_t current_time_ms,
                               const og::core::BrowserFramePacingResult& pacing,
                               const GameLoopDeps& render_deps,
                               const GameLoopDeps& tick_deps)
{
    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "browser_pacing", "browser_frame_step"));

    GameLoopFrameState render_state = st;
    render_state.initialized = true;
    render_state.last_frame_time = current_time_ms;
    render_state.accumulated_time = 0;

    GameLoopDeps effective_render_deps = render_deps;
    effective_render_deps.enable_tick = false;
    game_frame(s, render_state, effective_render_deps);

    st.done = render_state.done;
    st.has_pending_input = render_state.has_pending_input;
    st.pending_input = render_state.pending_input;

    if (!st.done && pacing.should_run_frame)
    {
        GameLoopDeps effective_tick_deps = tick_deps;
        effective_tick_deps.enable_render = false;
        effective_tick_deps.enable_event_poll = false;
        effective_tick_deps.enable_frame_timing = false;
        game_frame(s, st, effective_tick_deps);
    }

    st.initialized = true;
    st.last_frame_time = current_time_ms;
    st.accumulated_time = pacing.accumulated_after_step_ms;
}

namespace og::runtime {

void run_native_game_loop(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    while (!st.done)
    {
        game_frame(s, st, deps);
    }
}

} // namespace og::runtime
