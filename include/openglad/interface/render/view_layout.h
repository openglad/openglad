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

// Pure viewscreen layout math, parameterized on the WORLD canvas dimensions.
//
// This generalizes the historical hardcoded 320x200 viewscreen tables
// (T_WIDTH/T_HALF_WIDTH/... constants plus viewscreen::resize(whatmode)'s
// absolute-coordinate switch). At canvas_w=320, canvas_h=200 every rect below
// reproduces the legacy table VERBATIM (pinned by tests/unit/test_view_layout
// and the pre-existing ViewResize integration pins), so classic-pinned
// layouts stay byte-identical. The window-relative graphics/zoom setting
// sizes the logical canvas and its panes while fixed-pixel chrome stays put:
//
//  * The PREF_VIEW inset modes (PANELS/1/2/3) carve out fixed-size margins
//    for the score panel HUD, which draws fixed-size text/bar blocks anchored
//    to viewob->xloc/yloc/endx/endy. Those blocks do not grow with the
//    canvas, so the insets must not either.
//  * The radar is a fixed 60x44 block anchored to endx/endy; any pane at
//    least as large as the classic pane fits it. This is one of the reasons
//    the world canvas is clamped to a 320x200 MINIMUM (see
//    og::kMinWorldCanvasW/H): the classic panes are the smallest geometry the
//    sprite clipper, the radar, and the score-panel HUD were written against.
//
// Header-only and dependency-free so headless unit tests can pin the formulas
// without linking the interface library.

namespace og::view_layout
{

// Mode values mirror the PREF_VIEW_* constants in
// openglad/interface/render/view.h (not included here to stay
// dependency-free): 0 FULL, 1 PANELS, 2 VIEW_1, 3 VIEW_2, 4 VIEW_3.
inline constexpr int kModeFull = 0;
inline constexpr int kModePanels = 1;
inline constexpr int kModeView1 = 2;
inline constexpr int kModeView2 = 3;
inline constexpr int kModeView3 = 4;

struct ViewLayout
{
    // False reproduces the legacy switch arms that had NO table entry (2p
    // with mynum outside {0,1}, 3p with mynum outside {0,1,2}): the caller
    // must keep the previous geometry untouched.
    bool applies = false;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Legacy 1-player chrome insets per mode (FULL, PANELS, VIEW_1..3), applied
// symmetrically on both sides. At 320x200: (44,12,232,176), (64,28,192,144),
// (86,44,148,112), (106,60,108,80).
inline constexpr int kOnePlayerInsetX[5] = {0, 44, 64, 86, 106};
inline constexpr int kOnePlayerInsetY[5] = {0, 12, 28, 44, 60};

// Legacy 2p/3p vertical chrome inset per mode: 16/32/48/64 for
// PANELS/VIEW_1/VIEW_2/VIEW_3 (0 for FULL).
inline constexpr int split_vertical_inset(int mode)
{
    return 16 * mode;
}

// The world-canvas viewscreen layout: numviews panes on a canvas_w x canvas_h
// canvas. Derivation notes per player count (legacy values in parens):
//  * split panes leave a 2px seam: half_w = W/2-1 (159), right x = W/2+1
//    (161); half_h = H/2-1 (99), bottom y = H/2+1 (101).
//  * 2p/3p inset modes keep a 4px outer / 3px seam-side horizontal margin
//    inside each half pane (w = half_w-7 = 152; x = 4 or right+3 = 164).
//  * 3p inset modes use three fixed-margin columns instead: width (W-20)/3
//    (100) at x = 4, 4+cw+8 (112), 4+2*cw+12 (216); mynum 0/2/1 sit
//    left/middle/right.
//  * 4p (and any numviews outside 1..3, matching the legacy `default:`)
//    is always quadrants, whatever the mode.
inline ViewLayout compute_view_layout(int numviews, int mynum, int mode,
                                      int canvas_w, int canvas_h)
{
    // Out-of-range modes fall back to FULL, matching the legacy inner
    // `default:` arms.
    if (mode < kModeFull || mode > kModeView3)
        mode = kModeFull;

    const int half_w = canvas_w / 2 - 1;
    const int half_h = canvas_h / 2 - 1;
    const int right_x = canvas_w / 2 + 1;
    const int bottom_y = canvas_h / 2 + 1;

    switch (numviews)
    {
        case 1:
        {
            const int ix = kOnePlayerInsetX[mode];
            const int iy = kOnePlayerInsetY[mode];
            return {true, ix, iy, canvas_w - 2 * ix, canvas_h - 2 * iy};
        }
        case 2:
        {
            if (mynum != 0 && mynum != 1)
                return {};
            if (mode == kModeFull)
                return {true, mynum == 0 ? 0 : right_x, 0, half_w, canvas_h};
            const int iy = split_vertical_inset(mode);
            return {true, mynum == 0 ? 4 : right_x + 3, iy, half_w - 7,
                    canvas_h - 2 * iy};
        }
        case 3:
        {
            if (mynum < 0 || mynum > 2)
                return {};
            if (mode == kModeFull)
            {
                // Left pane full height; the right half splits top/bottom.
                if (mynum == 0)
                    return {true, 0, 0, half_w, canvas_h};
                return {true, right_x, mynum == 1 ? 0 : bottom_y, half_w,
                        half_h};
            }
            const int col_w = (canvas_w - 20) / 3;
            const int col_x[3] = {4,            // mynum 0: left column
                                  4 + 2 * col_w + 12, // mynum 1: right column
                                  4 + col_w + 8};     // mynum 2: middle column
            const int iy = split_vertical_inset(mode);
            return {true, col_x[mynum], iy, col_w, canvas_h - 2 * iy};
        }
        default: // 4 players — and any unexpected count, as in the legacy switch
        {
            // Any mynum outside 0..3 lands on the fourth quadrant (the legacy
            // `case 3: default:` arm).
            if (mynum < 0 || mynum > 3)
                mynum = 3;
            const bool right = (mynum == 1 || mynum == 3);
            const bool bottom = (mynum == 2 || mynum == 3);
            return {true, right ? right_x : 0, bottom ? bottom_y : 0, half_w,
                    half_h};
        }
    }
}

} // namespace og::view_layout
