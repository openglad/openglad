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

// The cfg `graphics/zoom` + `graphics/smoothing` keys: how the WORLD canvas
// derives from the zoom level, and which present filter smooths it.
//
//   graphics/zoom value   | world canvas
//   ----------------------+---------------------------------------------------
//   missing / garbage /   | classic 320x200 (shared with the UI canvas when
//   "1.0"                 | smoothing is off — byte-identical to the
//                         | historical single-canvas renderer)
//   "0.1" .. "0.9"        | classic / zoom (window-INDEPENDENT; width rounded
//                         | up to a multiple of 4, e.g. 0.5 -> 640x400)
//
//   graphics/smoothing    | world present path
//   ----------------------+---------------------------------------------------
//   missing / "off"       | GPU nearest stretch to the viewport
//   "sai" / "eagle"       | software 2x smart scaler into render2, then GPU
//
// Smoothing applies to WORLD scenery ONLY. The fixed menu canvas and the
// transparent gameplay HUD/radar overlay present nearest, so UI pixels never
// enter the SAI/Eagle pass.
//
// Header-only and SDL-free so headless unit tests can pin the parsing and
// the canvas-dimension math directly.

#include <algorithm>
#include <string_view>

namespace og
{

enum class WorldScaleMode
{
    // Classic zoom (1.0) with smoothing off: the fixed 320x200 world canvas
    // shared with the UI surface. Byte-identical to the historical
    // single-canvas renderer.
    Legacy,
    // smoothing "off" at a non-classic zoom: split world canvas, presented
    // with an unsmoothed GPU stretch.
    Integer,
    // smoothing "sai": Super2xSaI software 2x into the doubling scratch,
    // then GPU-stretched to the viewport.
    Sai,
    // smoothing "eagle": as Sai but with the SuperEagle scaler.
    Eagle
};

struct WorldScaleSetting
{
    WorldScaleMode mode = WorldScaleMode::Legacy;
};

// The world canvas never shrinks below the classic dimensions. The classic
// 320x200 panes are the smallest geometry the game was written against: the
// sprite clipper, the fixed 60x44 radar block, the score-panel HUD blocks and
// the PREF_VIEW chrome insets (up to 106/60 px per side in 1p) all assume at
// least this much room. A physical window smaller than the selected logical
// canvas simply downscales it during presentation.
inline constexpr int kMinWorldCanvasW = 320;
inline constexpr int kMinWorldCanvasH = 200;


struct WorldCanvasDims
{
    int w = kMinWorldCanvasW;
    int h = kMinWorldCanvasH;
};


// --- Zoom model (cfg graphics/zoom + graphics/smoothing) ---------------------
// The window-independent successor to graphics/scale: zoom 1.0 is the classic
// 320x200 view, smaller zoom shows proportionally more world
// (canvas = classic / zoom), in 0.1 steps down to 0.1 (10x the tiles per
// axis). Smoothing selects the WORLD-canvas-only present filter; the UI
// canvas (menus, text) always presents unsmoothed.

inline constexpr int kZoomStepsMax = 10; // zoom = steps/10, steps in 1..10

// Absent/garbage cfg reads as the classic 1.0. Values clamp into [0.1, 1.0]
// and quantize to the 0.1 grid, so the cycler and the renderer always agree.
inline int parse_zoom_steps(std::string_view value)
{
    if (value.empty())
        return kZoomStepsMax;
    int whole = 0;
    int tenths = 0;
    int hundredths = 0;
    int fractional_digits = 0;
    bool any = false;
    bool dot = false;
    for (const char c : value)
    {
        if (c == '.' && !dot) { dot = true; continue; }
        if (c < '0' || c > '9')
            return kZoomStepsMax;
        any = true;
        if (!dot)
        {
            // Saturate at 2: every value >= 2 clamps to classic anyway,
            // and this keeps arbitrarily long hand-edited strings safe.
            whole = std::min(2, whole * 10 + (c - '0'));
        }
        else
        {
            if (fractional_digits == 0)
                tenths = c - '0';
            else if (fractional_digits == 1)
                hundredths = c - '0';
            ++fractional_digits;
        }
    }
    if (!any)
        return kZoomStepsMax;
    const int steps = whole * 10 + tenths + (hundredths >= 5 ? 1 : 0);
    return std::clamp(steps, 1, kZoomStepsMax);
}

// Canvas for a zoom step count: classic/zoom, width to a multiple of 4 (the
// software 2x scalers and the legacy partial-present path require it).
inline WorldCanvasDims compute_zoom_canvas_dims(int zoom_steps)
{
    const int steps = std::clamp(zoom_steps, 1, kZoomStepsMax);
    if (steps == kZoomStepsMax)
        return {kMinWorldCanvasW, kMinWorldCanvasH}; // classic, byte-identical
    const int w = ((kMinWorldCanvasW * kZoomStepsMax / steps) + 3) & ~3;
    const int h = kMinWorldCanvasH * kZoomStepsMax / steps;
    return {w, h};
}

// graphics/smoothing: "off" (GPU nearest), "sai", "eagle" (software 2x smart
// scalers over the world canvas before the GPU stretch). Unknown reads Off.
inline WorldScaleMode parse_smoothing_setting(std::string_view value)
{
    if (value == "sai")
        return WorldScaleMode::Sai;
    if (value == "eagle")
        return WorldScaleMode::Eagle;
    return WorldScaleMode::Integer; // "off"/absent: plain nearest stretch
}

} // namespace og
