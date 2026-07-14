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

// The cfg `graphics/scale` key: how the WORLD canvas derives from the window.
//
//   graphics/scale value | world canvas               | world present path
//   ---------------------+----------------------------+---------------------------
//   missing / "off" /    | classic 320x200            | legacy `graphics/render`
//   unrecognized         | (shared with the UI canvas)| engine, exactly as today
//   "1","2","3","4","8"  | window / N (clamped)       | GPU nearest stretch
//   "sai" / "eagle"      | window / 2 (clamped)       | software 2x smooth scaler
//                        |                            | into render2, then GPU
//
// Interaction with the legacy `graphics/render` key: when `scale` is absent
// (every pre-existing cfg) the renderer behaves EXACTLY as today — one
// 320x200 canvas presented via the `render` engine (normal/sai/eagle; the
// historical `double` value was parsed but never handled, so it behaves as
// normal). When `scale` is set it takes over the WORLD present path only;
// the UI canvas (menus, picker, intro — pinned at 320x200) keeps presenting
// via the `render` engine. See docs/resolution-and-scaling.md.
//
// The UI canvas is unaffected by this key on purpose: every menu layout,
// mouse translation (logical 320x200) and pixel pin stays valid.
//
// Header-only and SDL-free so headless unit tests can pin the parsing and
// the canvas-dimension math directly.

#include <algorithm>
#include <string_view>

namespace og
{

enum class WorldScaleMode
{
    // Key missing/unrecognized/"off": the classic fixed 320x200 world canvas,
    // presented through the legacy graphics/render engine. Byte-identical to
    // the historical single-canvas renderer.
    Legacy,
    // "1"/"2"/"3"/"4"/"8": world canvas = window/factor (clamped), presented
    // with an unsmoothed GPU stretch (each canvas pixel covers ~factor^2
    // window pixels).
    Integer,
    // "sai": world canvas = window/2 (clamped), Super2xSaI software 2x into
    // the doubling scratch, then GPU-stretched to the viewport.
    Sai,
    // "eagle": as Sai but with the SuperEagle scaler.
    Eagle
};

struct WorldScaleSetting
{
    WorldScaleMode mode = WorldScaleMode::Legacy;
    int factor = 1; // window->canvas divisor (2 for Sai/Eagle; unused for Legacy)
};

// The world canvas never shrinks below the classic dimensions. The classic
// 320x200 panes are the smallest geometry the game was written against: the
// sprite clipper, the fixed 60x44 radar block, the score-panel HUD blocks and
// the PREF_VIEW chrome insets (up to 106/60 px per side in 1p) all assume at
// least this much room. A window smaller than scale*320 x scale*200 simply
// gets a coarser-than-requested stretch.
inline constexpr int kMinWorldCanvasW = 320;
inline constexpr int kMinWorldCanvasH = 200;

// Accepted values: "1", "2", "3", "4", "8", "sai", "eagle". Anything else —
// including the empty string a missing key reads back as, and the documented
// explicit "off" — is Legacy.
inline WorldScaleSetting parse_world_scale_setting(std::string_view value)
{
    if (value == "1")
        return {WorldScaleMode::Integer, 1};
    if (value == "2")
        return {WorldScaleMode::Integer, 2};
    if (value == "3")
        return {WorldScaleMode::Integer, 3};
    if (value == "4")
        return {WorldScaleMode::Integer, 4};
    if (value == "8")
        return {WorldScaleMode::Integer, 8};
    if (value == "sai")
        return {WorldScaleMode::Sai, 2};
    if (value == "eagle")
        return {WorldScaleMode::Eagle, 2};
    return {WorldScaleMode::Legacy, 1};
}

struct WorldCanvasDims
{
    int w = kMinWorldCanvasW;
    int h = kMinWorldCanvasH;
};

// World canvas dimensions for a window under a scale setting: window/factor,
// width rounded down to a multiple of 4 (the software 2x scalers and the
// legacy partial-present path require multiple-of-4 widths), clamped to the
// classic 320x200 minimum. Legacy is always exactly the classic canvas.
inline WorldCanvasDims compute_world_canvas_dims(int window_w, int window_h,
                                                 const WorldScaleSetting& setting)
{
    if (setting.mode == WorldScaleMode::Legacy)
        return {kMinWorldCanvasW, kMinWorldCanvasH};
    const int factor = std::max(1, setting.factor);
    const int w = (window_w / factor) & ~3;
    const int h = window_h / factor;
    return {std::max(kMinWorldCanvasW, w), std::max(kMinWorldCanvasH, h)};
}

} // namespace og
