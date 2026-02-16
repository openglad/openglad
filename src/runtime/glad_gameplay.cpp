/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Extracted from src/glad.cpp so non-app binaries (tests/tools) can link
 * gameplay entrypoints without pulling in main().
 */

#include <openglad/runtime/game_loop.h>

#include <openglad/core/util.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>

extern options* theprefs;

GameLoopFrameState g_frame_state{};

#ifdef TESTING
// Remove exits so levels can auto-complete when all enemies die.
bool g_test_remove_exits = false;
#endif

namespace
{
inline screen* active_screen()
{
    if (ctx().game_screen != nullptr)
        return ctx().game_screen;
    return myscreen;
}
} // namespace

// Initialize the game for playing (called before game loop starts).
void glad_init()
{
    screen* current_screen = active_screen();
    if (current_screen == nullptr)
    {
        LogError("glad_init_failed reason=missing_screen\n");
        return;
    }

    clear_keyboard();
    current_screen->fadeblack(0);
    current_screen->clearbuffer();

    // Load the default saved-game.
    load_saved_game("save0", current_screen);

#ifdef TESTING
    if (g_test_remove_exits) {
        for (auto& uptr : current_screen->level_data.fxlist) {
            walker* w = uptr.get();
            if (w && w->query_order() == Order::Treasure && w->query_family() == FAMILY_EXIT)
                w->dead = 1;
        }
    }
#endif

    current_screen->continuous_input();

    current_screen->redraw();
    current_screen->fadeblack(1);

    current_screen->redraw();
    current_screen->refresh();
    read_scenario(current_screen);
    current_screen->redrawme = 1;
    current_screen->framecount = 0;
    current_screen->timerstart = query_timer_control();

    g_frame_state.done = false;
    g_frame_state.currentcycle = 0;
    g_frame_state.cycletime = 3;
}

void glad_main(Sint32 playermode)
{
    screen* current_screen = active_screen();
    if (current_screen == nullptr)
    {
        LogError("glad_main_failed mode={} reason=missing_screen\n", playermode);
        return;
    }

    glad_init();

#ifdef __EMSCRIPTEN__
    // For Emscripten, the unified main loop in main() handles game_frame() calls
    // via the state machine. We just need to initialize - don't start another loop.
#else
    while (!g_frame_state.done)
    {
        game_frame(*current_screen, g_frame_state);
    }

    clear_keyboard();
    current_screen->level_data.delete_objects();
#endif
}

// remaining_foes (LevelData overload) is in level_data.cpp.
// This screen* overload delegates to it for backward compatibility.
short remaining_foes(LevelData& level, walker* myguy);

short remaining_foes(screen* s, walker* myguy)
{
    return remaining_foes(s->level_data, myguy);
}

// remaining_team returns # of livings left on team myteam.
short remaining_team(screen* s, char myteam)
{
    short myfoes = 0;

    const auto& foelist = s->level_data.oblist;
    for (auto& uptr : foelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Living) &&
            (myteam == w->team_num))
            myfoes++;
    }

    return myfoes;
}
