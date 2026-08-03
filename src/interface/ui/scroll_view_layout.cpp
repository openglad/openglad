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
#include <openglad/interface/ui/scroll_view_layout.h>

namespace og::ui {
namespace {

// Mirrors of help.cpp's frame constants (the viewer's geometry contract).
inline constexpr int kHelptextLeft = 40;
inline constexpr int kHelptextTop = 40;
inline constexpr int kGutterW = 18;   // scrollbar gutter appended to the frame
inline constexpr int kScreenW = 320;
inline constexpr int kVisibleLines = 14;  // slots visible above the bottom bar

} // namespace

bool ScrollRect::contains(int px, int py) const
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

bool ScrollRect::contains_padded(int px, int py, int pad) const
{
    return px >= x - pad && px < x + w + pad && py >= y - pad &&
           py < y + h + pad;
}

ScrollViewLayout compute_scroll_view_layout(int num_lines, int box_width,
                                            int linesdown, int bottomrow,
                                            int caller_blit_x,
                                            int caller_blit_y,
                                            int caller_blit_w,
                                            int caller_blit_h)
{
    ScrollViewLayout out;
    // The legacy frame, exactly: (36, 28)-(40+box_width, 147).
    out.frame_x1 = kHelptextLeft - 4;
    out.frame_y1 = kHelptextTop - 4 - 8;
    out.frame_x2 = kHelptextLeft + box_width;
    out.frame_y2 = kHelptextTop + 107;
    out.blit_x = caller_blit_x;
    out.blit_y = caller_blit_y;
    out.blit_w = caller_blit_w;
    out.blit_h = caller_blit_h;

    out.scrollable = bottomrow > 0;
    if (!out.scrollable)
        return out;  // short text: byte-identical to the legacy dialog

    out.frame_x2 += kGutterW;
    // Widen the caller's blit to cover the gutter (the campaign intro blits
    // only its own 244px rect), clamped to the screen.
    if (out.blit_w < out.frame_x2 + 1 - out.blit_x)
        out.blit_w = out.frame_x2 + 1 - out.blit_x;
    if (out.blit_x + out.blit_w > kScreenW)
        out.blit_w = kScreenW - out.blit_x;

    const int gutter_x = kHelptextLeft + box_width + 2;
    out.up = {gutter_x, kHelptextTop - 2, 14, 14};
    out.down = {gutter_x, kHelptextTop + 83, 14, 14};
    out.track = {gutter_x, kHelptextTop + 14, 14, 67};

    int thumb_h = num_lines > 0 ? out.track.h * kVisibleLines / num_lines
                                : out.track.h;
    if (thumb_h < 6)
        thumb_h = 6;
    if (thumb_h > out.track.h)
        thumb_h = out.track.h;
    int rel = linesdown;
    if (rel < 0)
        rel = 0;
    if (rel > bottomrow)
        rel = bottomrow;
    out.thumb.x = out.track.x + 2;
    out.thumb.w = out.track.w - 4;
    out.thumb.h = thumb_h;
    out.thumb.y = out.track.y + (out.track.h - thumb_h) * rel / bottomrow;
    return out;
}

} // namespace og::ui
