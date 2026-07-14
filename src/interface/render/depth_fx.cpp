/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/interface/render/depth_fx.h>

DepthFxMode depth_fx_mode_from_setting(const std::string& value)
{
    if (value == "off")
        return DepthFxMode::Off;
    if (value == "tint")
        return DepthFxMode::Tint;
    if (value == "haze")
        return DepthFxMode::Haze;
    if (value == "mist")
        return DepthFxMode::Mist;
    // "fog", the empty string of an absent key, and any unrecognized value
    // all read as the default.
    return DepthFxMode::Fog;
}
