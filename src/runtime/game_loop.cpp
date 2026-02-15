/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_loop.h>
#include <openglad/legacy/colors.h>
#include <openglad/runtime/game_context.h>
#include <openglad/input/input.h>
#include <openglad/render/pal32.h>
#include <openglad/ui/results_screen.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/entities/obmap.h>
#include <openglad/core/util.h>
#include <openglad/sim/simulator.h>

// Declared elsewhere (glad.cpp).
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
short score_panel(screen* scr);
short score_panel(screen* scr, short do_it);

extern bool debug_draw_paths;
extern bool debug_draw_obmap;

static int default_poll(SDL_Event* e)
{
    return SDL_PollEvent(e);
}

static void default_handle(const SDL_Event& e)
{
    handle_events(e);
}

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    const std::function<int(SDL_Event*)> poll_event =
        deps.poll_event ? deps.poll_event : std::function<int(SDL_Event*)>(default_poll);
    const std::function<void(const SDL_Event&)> handle_event =
        deps.handle_event ? deps.handle_event : std::function<void(const SDL_Event&)>(default_handle);

    // Reset the timer count to zero ...
    reset_timer();

    if (s.redrawme)
    {
#ifndef TESTING
        if (deps.enable_render) {
            s.draw_panels(s.numviews);
            score_panel(&s, 1);
            s.refresh();
        }
#endif
        s.redrawme = 0;
    }

    if (s.end)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

    s.act();
    s.framecount++;

    if (s.end)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

#ifndef TESTING
    if (deps.enable_render) {
        s.redraw();

        if (debug_draw_obmap)
            s.level_data.myobmap->draw();  // debug drawing for object collision map

#ifdef USE_TOUCH_INPUT
        draw_touch_controls(&s);
#endif
        score_panel(&s);
        s.refresh();
    }
#endif

    if (deps.enable_event_poll) {
        SDL_Event event;
        while (poll_event(&event))
        {
            handle_event(event);
            if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_F11)
                    debug_draw_paths = !debug_draw_paths;
                else if (event.key.keysym.sym == SDLK_F12)
                    debug_draw_obmap = !debug_draw_obmap;
                else if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    bool result = yes_or_no_prompt("Abort Mission", "Quit this mission?", false);
                    s.redrawme = 1;
                    if (result) // player wants to quit
                    {
                        st.done = true;
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

            s.input(event);
        }
    }

    if (s.end || st.done)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

    // Snapshot current input state for the frame
    ctx().poll_input();

    // Shadow sim step: feed the deterministic simulator alongside the legacy
    // game loop. This builds up the CommandSnapshot from polled input and
    // runs a parallel sim tick. As sim logic matures, game rules will migrate
    // from screen::act() to Simulator::step().
    if (ctx().rng) {
        static og::sim::Simulator shadow_sim(0);
        og::sim::CommandSnapshot cmd_snap;
        const InputState* input = ctx().active_input();
        if (input) {
            for (int p = 0; p < og::sim::CommandSnapshot::MAX_PLAYERS && p < MAX_PLAYERS; ++p) {
                og::sim::PlayerCommand cmds = og::sim::PlayerCommand::None;
                const auto& pi = input->players[p];
                if (pi.held[static_cast<int>(InputKey::Up)])      cmds = cmds | og::sim::PlayerCommand::MoveUp;
                if (pi.held[static_cast<int>(InputKey::Down)])    cmds = cmds | og::sim::PlayerCommand::MoveDown;
                if (pi.held[static_cast<int>(InputKey::Left)])    cmds = cmds | og::sim::PlayerCommand::MoveLeft;
                if (pi.held[static_cast<int>(InputKey::Right)])   cmds = cmds | og::sim::PlayerCommand::MoveRight;
                if (pi.held[static_cast<int>(InputKey::UpLeft)])    cmds = cmds | og::sim::PlayerCommand::MoveUp | og::sim::PlayerCommand::MoveLeft;
                if (pi.held[static_cast<int>(InputKey::UpRight)])   cmds = cmds | og::sim::PlayerCommand::MoveUp | og::sim::PlayerCommand::MoveRight;
                if (pi.held[static_cast<int>(InputKey::DownLeft)])  cmds = cmds | og::sim::PlayerCommand::MoveDown | og::sim::PlayerCommand::MoveLeft;
                if (pi.held[static_cast<int>(InputKey::DownRight)]) cmds = cmds | og::sim::PlayerCommand::MoveDown | og::sim::PlayerCommand::MoveRight;
                if (pi.held[static_cast<int>(InputKey::Fire)])    cmds = cmds | og::sim::PlayerCommand::Fire;
                if (pi.held[static_cast<int>(InputKey::Special)]) cmds = cmds | og::sim::PlayerCommand::Special;
                if (pi.held[static_cast<int>(InputKey::Shifter)]) cmds = cmds | og::sim::PlayerCommand::Shifter;
                if (pi.held[static_cast<int>(InputKey::Yell)])    cmds = cmds | og::sim::PlayerCommand::Yell;
                if (pi.held[static_cast<int>(InputKey::Switch)])         cmds = cmds | og::sim::PlayerCommand::SwitchWeap;
                if (pi.held[static_cast<int>(InputKey::SpecialSwitch)])  cmds = cmds | og::sim::PlayerCommand::SwitchSpec;
                cmd_snap.players[p].commands = cmds;
            }
            cmd_snap.quit_requested = input->quit_requested;
        }
        shadow_sim.step(cmd_snap, 1.0f / 60.0f);
        shadow_sim.clear_events(); // discard for now
    }

    s.continuous_input();

    if (s.end)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

    // Now cycle palette ..
    if (s.cyclemode)
        s.do_cycle(st.currentcycle++, st.cycletime);

#ifndef __EMSCRIPTEN__
    // FPS cap
    if (g_game_speed_factor == 0.0f) {
        // Max speed: no delay
    } else if (g_game_speed_factor != 1.0f) {
        Sint32 adjusted_wait = static_cast<Sint32>(s.timer_wait / g_game_speed_factor);
        time_delay(adjusted_wait - query_timer());
    } else {
        time_delay(s.timer_wait - query_timer());
    }
#endif

    return st.done ? GameFrameResult::Done : GameFrameResult::Continue;
}

bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    return game_frame_with_result(s, st, deps) != GameFrameResult::Continue;
}
