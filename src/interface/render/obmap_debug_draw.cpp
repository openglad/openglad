/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Debug visualization for the collision map (obmap).
// Extracted from obmap::draw() in entities module to keep rendering
// out of the entity layer.

#include <openglad/interface/render/obmap_debug_draw.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/text.h>
#include <openglad/core/colors.h>

namespace {
struct IntRect {
    int x = 0;
    int y = 0;
    int w = 1;
    int h = 1;
};
} // namespace

static constexpr int OBRES = 32;

void obmap_debug_draw(obmap& map, screen* scr)
{
    text& t = scr->text_normal;
    const Sint32 offsetx = scr->viewob[0]->topx;
    const Sint32 offsety = scr->viewob[0]->topy;

    // Draw the number of obs in each pile
    for (auto& [pos, walkers] : map.pos_to_walker)
    {
        const Sint32 cx = static_cast<Sint32>(obmap::unhash(pos.first)) - offsetx + OBRES / 2;
        const Sint32 cy = static_cast<Sint32>(obmap::unhash(pos.second)) - offsety + OBRES / 2;
        scr->draw_box(cx - OBRES / 2, cy - OBRES / 2, cx + OBRES / 2, cy + OBRES / 2, YELLOW, false);
        t.write_xy_center(cx, cy, YELLOW, "%d", walkers.size());
    }

    // Draw a box for each walker
    for (auto& [w, positions] : map.walker_to_pos)
    {
        bool unset = true;
        IntRect r{};
        for (auto& [px, py] : positions)
        {
            if (unset)
            {
                r.x = px;
                r.y = py;
                unset = false;
                continue;
            }
            if (px < r.x)
            {
                r.w += r.x - px;
                r.x = px;
            }
            if (py < r.y)
            {
                r.h += r.y - py;
                r.y = py;
            }
            if (px > r.x + r.w)
            {
                r.w += px - (r.x + r.w);
            }
            if (py > r.y + r.h)
            {
                r.h += py - (r.y + r.h);
            }
        }

        if (!unset)
        {
            const Sint32 x = static_cast<Sint32>(obmap::unhash(static_cast<short>(r.x))) - offsetx;
            const Sint32 y = static_cast<Sint32>(obmap::unhash(static_cast<short>(r.y))) - offsety;
            const Sint32 bw = static_cast<Sint32>(obmap::unhash(static_cast<short>(r.w)));
            const Sint32 bh = static_cast<Sint32>(obmap::unhash(static_cast<short>(r.h)));
            scr->draw_box(x, y, x + bw, y + bh, w->query_team_color(), false);
        }
    }
}
