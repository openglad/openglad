/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "SDL.h"
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/legacy/colors.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/input/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/obmap_debug_draw.h>
#include <openglad/core/util.h>
#include <cassert>


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

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    assert(og::runtime::current_session);
    if (!og::runtime::current_session)
        return GameFrameResult::Done;

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

    if (s.world().end)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

    s.act();
    s.framecount++;

    if (s.world().end)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

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

    if (s.world().end || st.done)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

    // Snapshot current input state for the frame
    ctx().poll_input();

    // Process input through semantic InputState (SDL-independent path)
    s.process_input(ctx().input);

    s.continuous_input();

    if (s.world().end)
    {
        st.done = true;
        return GameFrameResult::Done;
    }

    // Now cycle palette ..
    if (s.cyclemode)
        s.do_cycle(st.currentcycle++, st.cycletime);

#ifndef __EMSCRIPTEN__
    // FPS cap — skipped when the caller manages timing externally
    // (e.g. multi-session demos that tick many sessions per display frame).
    if (deps.enable_frame_timing) {
        if (og::runtime::current_session->g_game_speed_factor_ == 0.0f) {
            // Max speed: no delay
        } else if (og::runtime::current_session->g_game_speed_factor_ != 1.0f) {
            Sint32 adjusted_wait = static_cast<Sint32>(s.world().timer_wait / og::runtime::current_session->g_game_speed_factor_);
            time_delay(adjusted_wait - query_timer());
        } else {
            time_delay(s.world().timer_wait - query_timer());
        }
    }
#endif

    return st.done ? GameFrameResult::Done : GameFrameResult::Continue;
}

bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps)
{
    return game_frame_with_result(s, st, deps) != GameFrameResult::Continue;
}
