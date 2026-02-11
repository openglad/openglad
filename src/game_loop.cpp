#include "game_loop.h"

#include "colors.h"
#include "game_context.h"
#include "graph.h"
#include "input.h"
#include "pal32.h"
#include "results_screen.h"
#include "screen.h"
#include "util.h"

// Declared elsewhere (glad.cpp).
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
short score_panel(screen* myscreen);
short score_panel(screen* myscreen, short do_it);

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
    const auto poll_event = deps.poll_event ? deps.poll_event : default_poll;
    const auto handle_event = deps.handle_event ? deps.handle_event : default_handle;

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
    input_state_from_sdl(ctx().input);

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
