/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#pragma once

// Pure geometry for the scrolling text viewer (SCENARIO INFORMATION /
// campaign intro) scroll chrome — SDL-free and unit-testable (issue #156).
// The scrollbar gutter appears ONLY when the text overflows the 14-line
// window, so short briefings render byte-identically to the legacy dialog.

namespace og::ui {

struct ScrollRect
{
    int x = 0, y = 0, w = 0, h = 0;
    // Inclusive of the left/top edge, exclusive of the right/bottom edge.
    bool contains(int px, int py) const;
    // Same, with the rect grown by `pad` px on every side (touch coords can
    // land a pixel or two off the UI canvas at fractional zoom).
    bool contains_padded(int px, int py, int pad) const;
};

struct ScrollViewLayout
{
    bool scrollable = false;                    // text overflows the window
    int frame_x1 = 0, frame_y1 = 0;             // draw_button() frame args
    int frame_x2 = 0, frame_y2 = 0;
    int blit_x = 0, blit_y = 0;                 // buffer_to_screen() args
    int blit_w = 0, blit_h = 0;
    ScrollRect up, down, track, thumb;          // zero rects when !scrollable
};

// num_lines: flowed display line count; box_width: the caller's frame width
// (200 scenario / 240 intro); linesdown/bottomrow: current scroll state in
// scanlines (bottomrow <= 0 means everything fits); caller_blit_*: the
// caller's legacy buffer_to_screen rect, widened only when the gutter needs
// to be visible.
ScrollViewLayout compute_scroll_view_layout(int num_lines, int box_width,
                                            int linesdown, int bottomrow,
                                            int caller_blit_x,
                                            int caller_blit_y,
                                            int caller_blit_w,
                                            int caller_blit_h);

} // namespace og::ui
