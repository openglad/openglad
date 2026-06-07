/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/platform/curses/terminal.h>

#include <cstdint>
#include <deque>
#include <string>

class GameWorld;

namespace og::curses {

// Renders the mirror GameWorld to a terminal as a roguelike: a viewport of tile
// glyphs with entity glyphs overlaid on the nearest tile to each entity's pixel
// position, plus a HUD and a scrolling message log. Pure given an ITerminal and
// a const GameWorld, so it is fully assertable with HeadlessTerminal.
class CursesRenderer
{
public:
    struct Options {
        bool allow_unicode = true;
        bool allow_color = true;
        int max_log_lines = 6;
    };

    CursesRenderer();
    explicit CursesRenderer(Options opt);

    // Append a line to the message log (called by the runtime on notifications).
    void log_message(std::string line);
    void clear_log();
    const std::deque<std::string>& log() const { return log_; }

    // Draw a full frame. followed_id is the entity id to center the camera on and
    // describe in the HUD; 0 means "no followed entity" (camera stays at origin).
    void draw(ITerminal& term, const GameWorld& world, std::uint32_t followed_id);

    // Convert an entity pixel position to a tile cell (GRID_SIZE == 16).
    static void world_to_tile(int xpos, int ypos, int& tile_x, int& tile_y);

private:
    // Sub-regions (computed from terminal size each frame).
    void draw_viewport(ITerminal& term, const GameWorld& world,
                       std::uint32_t followed_id,
                       int top, int left, int height, int width);
    void draw_hud(ITerminal& term, const GameWorld& world,
                  std::uint32_t followed_id, int top, int left, int width);
    void draw_log(ITerminal& term, int top, int left, int height, int width);

    Options opt_;
    std::deque<std::string> log_;
};

} // namespace og::curses
