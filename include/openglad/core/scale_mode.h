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
//   missing / garbage /   | master's shipped classic-density baseline,
//   "1.0"                 | aspect-expanded without stretching
//   "0.1" .. "0.9"        | baseline / zoom (width rounded down to a multiple
//                         | of 4); unsafe depths clamp to a pixel budget
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
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace og
{

enum class WorldScaleMode
{
    // The special shared 320x200 world/UI target. This is used only when the
    // aspect-matched zoom result is itself classic-sized and smoothing is off.
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
// Zoom 1.0 restores master's shipped classic-density baseline, expanding the
// needed axis to avoid aspect distortion. Smaller zoom values show
// proportionally more world (canvas = baseline / zoom). Smoothing selects the
// WORLD-canvas-only present filter; UI always presents unsmoothed.

inline constexpr int kZoomStepsMax = 10; // zoom = steps/10, steps in 1..10

// Absent/garbage cfg reads as the classic-density 1.0 default. Values clamp into
// [0.1, 1.0] and quantize to the 0.1 grid, so the cycler and renderer agree.
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
            // Saturate at 2: every value >= 2 clamps to 1.0 anyway,
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

// Bound a split world surface plus its streaming texture to roughly 64 MiB at
// ARGB8888. Deeper zoom steps must fit it; zoom 1.0 uses the separate absolute
// cap below so ordinary display aspects keep master's exact pixel density.
inline constexpr std::int64_t kWorldCanvasPixelBudget = 8'388'608;
// Absolute allocation ceiling for the baseline as well. Physical resolution
// does not increase this canvas; the cap protects hostile extreme aspects and
// bad dimensions before they can request huge surfaces and textures.
inline constexpr std::int64_t kWorldCanvasAbsolutePixelBudget = 16'777'216;

inline int saturating_canvas_dimension(std::int64_t value)
{
    return static_cast<int>(std::min<std::int64_t>(
        value, static_cast<std::int64_t>(std::numeric_limits<int>::max() - 3)));
}

// Readable gameplay chrome uses classic pixel density while matching the
// zoom-1 World aspect. Start from 320x200 and expand only the needed axis:
// 16:10 stays 320x200, 16:9 becomes about 356x200, and 4:3 becomes 320x240.
// The scenery can therefore use every logical/HiDPI output pixel without
// shrinking the bitmap font and HUD to one physical pixel per source pixel.
inline WorldCanvasDims compute_gameplay_ui_canvas_dims(int world_w, int world_h)
{
    if (world_w <= 0 || world_h <= 0)
        return {};
    if (static_cast<std::int64_t>(world_w) * kMinWorldCanvasH >=
        static_cast<std::int64_t>(world_h) * kMinWorldCanvasW)
    {
        const std::int64_t rounded_w =
            static_cast<std::int64_t>(world_w) * kMinWorldCanvasH +
            world_h / 2;
        return {std::max(kMinWorldCanvasW,
                         saturating_canvas_dimension(rounded_w / world_h)),
                kMinWorldCanvasH};
    }

    const std::int64_t rounded_h =
        static_cast<std::int64_t>(world_h) * kMinWorldCanvasW + world_w / 2;
    return {kMinWorldCanvasW,
            std::max(kMinWorldCanvasH,
                     saturating_canvas_dimension(rounded_h / world_w))};
}

// --- Per-view zoom composition (§7.1 pause-menu design) ---------------------
// A view's zoom override is a scale numerator in tenths: 10 = GAME (follow
// graphics/zoom alone), 9..5 = 0.9x..0.5x. The world canvas derives from the
// MINIMUM effective zoom over the live views, expressed as a percent:
//   effective pct = zoom_steps * view_scale_num   (10..100)
// so global-only (num 10) reproduces the steps math exactly (pct = steps*10).
inline constexpr int kViewScaleNumMax = 10; // 1.0x — the GAME default
inline constexpr int kViewScaleNumMin = 5;  // 0.5x — the deepest override
inline constexpr int kZoomPctMax = 100;

inline constexpr int clamp_view_scale_num(int num)
{
    return std::clamp(num, kViewScaleNumMin, kViewScaleNumMax);
}

// Composed zoom percent for a global step count and a per-view minimum scale.
inline constexpr int compose_zoom_pct(int zoom_steps, int view_scale_num)
{
    return std::clamp(zoom_steps, 1, kZoomStepsMax) *
           clamp_view_scale_num(view_scale_num);
}

// Canvas for a window and a composed zoom percent (5..100). Zoom 100 keeps
// master's default classic-density world (320x200 at 16:10) instead of
// master's optional graphics/scale=1 window-sized canvas. One axis expands
// only enough to match the window aspect, so widescreen/tall displays reveal
// more world without stretching sprites. Lower percents grow that baseline by
// 100/pct. Integer division rounds down and width rounds down to a multiple
// of four (required by the software scalers and partial-present paths).
inline WorldCanvasDims compute_zoom_canvas_dims_pct(int window_w, int window_h,
                                                    int zoom_pct)
{
    const WorldCanvasDims base = compute_gameplay_ui_canvas_dims(
        std::max(kMinWorldCanvasW, window_w),
        std::max(kMinWorldCanvasH, window_h));
    const std::int64_t base_w = base.w;
    const std::int64_t base_h = base.h;
    // The deepest composable percent is global 0.1 x view 0.5 = 5. Budget
    // protection is the caller's fits/constrain machinery, never a silent
    // clamp here — a clamped canvas under unclamped view windows would
    // desynchronize the two.
    const int pct = std::clamp(zoom_pct, kViewScaleNumMin, kZoomPctMax);
    const std::int64_t scaled_w = base_w * kZoomPctMax / pct;
    const std::int64_t scaled_h = base_h * kZoomPctMax / pct;
    const int w = saturating_canvas_dimension(scaled_w) & ~3;
    const int h = saturating_canvas_dimension(scaled_h);
    return {w, h};
}

// Canvas for a window and zoom step count: the global-only view of the same
// math (pct = steps*10; base*100/(steps*10) and base*10/steps share the same
// rational, so the floor results are identical — pinned by unit tests).
inline WorldCanvasDims compute_zoom_canvas_dims(int window_w, int window_h,
                                                int zoom_steps)
{
    return compute_zoom_canvas_dims_pct(
        window_w, window_h,
        std::clamp(zoom_steps, 1, kZoomStepsMax) * kZoomStepsMax);
}

// Uniformly bound an otherwise-valid canvas while retaining its aspect. This
// is the last resort for hostile extreme aspects/dimensions or a renderer
// texture limit; ordinary display aspects pass through unchanged.
inline WorldCanvasDims constrain_world_canvas_dims(
    WorldCanvasDims requested, int max_texture_dimension,
    std::int64_t pixel_budget = kWorldCanvasAbsolutePixelBudget)
{
    const std::int64_t pixels =
        static_cast<std::int64_t>(requested.w) * requested.h;
    long double factor = 1.0L;
    if (pixel_budget > 0 && pixels > pixel_budget)
    {
        factor = std::min(
            factor, std::sqrt(static_cast<long double>(pixel_budget) /
                              static_cast<long double>(pixels)));
    }
    if (max_texture_dimension > 0)
    {
        factor = std::min(
            factor, static_cast<long double>(max_texture_dimension) /
                        static_cast<long double>(requested.w));
        factor = std::min(
            factor, static_cast<long double>(max_texture_dimension) /
                        static_cast<long double>(requested.h));
    }
    if (factor >= 1.0L)
        return requested;

    int w = static_cast<int>(static_cast<long double>(requested.w) * factor);
    int h = static_cast<int>(static_cast<long double>(requested.h) * factor);
    w = std::max(kMinWorldCanvasW, w & ~3);
    h = std::max(kMinWorldCanvasH, h);
    while (static_cast<std::int64_t>(w) * h > pixel_budget &&
           w > kMinWorldCanvasW)
        w -= 4;
    return {w, h};
}

// Composed-percent budget check (§7.1 per-view zoom): whether the canvas for
// `zoom_pct` fits the split-canvas pixel budget and the renderer's texture
// limit. pct == 100 is the always-selectable baseline (very large desktops
// are uniformly constrained rather than refused).
inline bool zoom_canvas_fits_budget_pct(int window_w, int window_h,
                                        int zoom_pct,
                                        int max_texture_dimension = 0)
{
    const int pct = std::clamp(zoom_pct, kViewScaleNumMin, kZoomPctMax);
    if (pct == kZoomPctMax)
    {
        return max_texture_dimension <= 0 ||
               (max_texture_dimension >= kMinWorldCanvasW &&
                max_texture_dimension >= kMinWorldCanvasH);
    }
    const WorldCanvasDims dims =
        compute_zoom_canvas_dims_pct(window_w, window_h, pct);
    if (max_texture_dimension > 0 &&
        (dims.w > max_texture_dimension || dims.h > max_texture_dimension))
    {
        return false;
    }
    return static_cast<std::int64_t>(dims.w) * dims.h <=
           kWorldCanvasPixelBudget;
}

inline bool zoom_canvas_fits_budget(int window_w, int window_h, int zoom_steps,
                                    int max_texture_dimension = 0)
{
    return zoom_canvas_fits_budget_pct(
        window_w, window_h,
        std::clamp(zoom_steps, 1, kZoomStepsMax) * kZoomStepsMax,
        max_texture_dimension);
}

// Deepest safe selector step for this window. Values below it are omitted so
// repeated clicks wrap instead of retrying a multi-gigabyte allocation.
inline int minimum_zoom_steps_for_window(int window_w, int window_h,
                                         int max_texture_dimension = 0)
{
    for (int steps = 1; steps <= kZoomStepsMax; ++steps)
        if (zoom_canvas_fits_budget(window_w, window_h, steps,
                                    max_texture_dimension))
            return steps;
    return kZoomStepsMax;
}

struct CanvasViewport
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Center-crop a source canvas to a target aspect before scaling. Fixed-size
// modal screens use this when seeding their 320x200 backdrop from a widescreen
// world; fitting the final UI alone cannot undo a squeeze baked into that copy.
inline CanvasViewport crop_canvas_to_aspect(int source_w, int source_h,
                                            int target_w, int target_h)
{
    if (source_w <= 0 || source_h <= 0 || target_w <= 0 || target_h <= 0)
        return {0, 0, std::max(0, source_w), std::max(0, source_h)};

    int crop_w = source_w;
    int crop_h = source_h;
    if (static_cast<std::int64_t>(source_w) * target_h >
        static_cast<std::int64_t>(source_h) * target_w)
    {
        crop_w = static_cast<int>(
            static_cast<std::int64_t>(source_h) * target_w / target_h);
    }
    else
    {
        crop_h = static_cast<int>(
            static_cast<std::int64_t>(source_w) * target_h / target_w);
    }
    return {(source_w - crop_w) / 2, (source_h - crop_h) / 2,
            crop_w, crop_h};
}

// Aspect-fit a logical canvas inside the overscan viewport. This keeps fixed
// UI/HUD geometry and independently rounded World canvases from stretching on
// displays whose aspect does not exactly match their integer dimensions.
inline CanvasViewport fit_canvas_in_viewport(int canvas_w, int canvas_h,
                                             int viewport_x, int viewport_y,
                                             int viewport_w, int viewport_h)
{
    if (canvas_w <= 0 || canvas_h <= 0 || viewport_w <= 0 || viewport_h <= 0)
        return {viewport_x, viewport_y, std::max(0, viewport_w),
                std::max(0, viewport_h)};

    int fitted_w = viewport_w;
    int fitted_h = viewport_h;
    if (static_cast<std::int64_t>(viewport_w) * canvas_h >
        static_cast<std::int64_t>(viewport_h) * canvas_w)
    {
        fitted_w = static_cast<int>(
            static_cast<std::int64_t>(viewport_h) * canvas_w / canvas_h);
    }
    else
    {
        fitted_h = static_cast<int>(
            static_cast<std::int64_t>(viewport_w) * canvas_h / canvas_w);
    }
    return {viewport_x + (viewport_w - fitted_w) / 2,
            viewport_y + (viewport_h - fitted_h) / 2,
            fitted_w, fitted_h};
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
