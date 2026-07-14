/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Depth-effect selector for floors seen BELOW the camera through air holes:
// the cfg setting "effects" / "depth_fx" picks how the below-floor layer
// composite is treated on top of the unconditional depth fade (the fade is
// not part of this setting — Off keeps it). The treatments run inside
// sdl_video::floor_layer_end on the already-scaled layer, so they apply to
// tiles, decor and entities of that floor together and can never leak into
// the camera floor or the ghost floors above.

#include <cstdint>
#include <string>

enum class DepthFxMode : unsigned char
{
    Off,  // fade only — byte-identical to no depth effect
    Tint, // legacy cold blue-grey blend toward (58,74,140)
    Haze, // aerial perspective: blend toward pale steel (150,160,175)
    Mist, // checkerboard dither of the haze color — no alpha blending
    Fog,  // haze + drifting value-noise fog patches (the default)
};

// Per-composite parameters threaded from the floor loop (viewscreen::redraw)
// through screen into the video backend. `stories` is how many floors the
// composited floor sits below the camera (>= 1 whenever mode != Off; the
// camera floor and ghost floors above always pass a default-constructed
// params object). `frame` is the render-only effects frame tick — it drives
// the fog drift and nothing else, so Tint/Haze/Mist are static by
// construction.
struct DepthFxParams
{
    DepthFxMode mode = DepthFxMode::Off;
    int stories = 0;
    std::uint32_t frame = 0;
};

// Map the persisted "effects"/"depth_fx" setting string to a mode. "off",
// "tint", "haze" and "mist" select those modes; anything else — including
// the empty string get_setting returns for an absent key — reads as the
// default, Fog.
DepthFxMode depth_fx_mode_from_setting(const std::string& value);
