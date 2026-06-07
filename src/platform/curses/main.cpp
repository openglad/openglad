/* openglad_curses: a zero-SDL, ncurses-based client for OpenGlad.
 *
 * A different renderer and input backend for the same game: it reuses the
 * deterministic simulation, the data-driven menu model, save/level/campaign
 * loading, and the server-authoritative networking, rendering the world as a
 * roguelike in the terminal. See docs/ncurses-client.md.
 *
 * Copyright (C) 1995-2002 FSGames. Ported by Sean Ford and Yan Shosh.
 * Licensed under GPL v2.
 */
#include <openglad/platform/curses/curses_app.h>

int main(int argc, char* argv[])
{
    og::curses::AppOptions options;
    bool should_exit = false;
    if (!og::curses::parse_app_options(argc, argv, options, &should_exit))
        return should_exit ? 0 : 2;
    return og::curses::run_curses_app(options, argc, argv);
}
