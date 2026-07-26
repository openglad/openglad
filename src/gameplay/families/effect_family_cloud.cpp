/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>

short hits(short x,  short y,  short xsize,  short ysize,
           short x2, short y2, short xsize2, short ysize2);

const EffectFamilyDescriptor& describe_effect_cloud()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_CLOUD,
        .name = "CLOUD",
        .loops_animation = true,
        .creates_hit_effect = false,
        .init_bit_flags = BIT_NO_COLLIDE | BIT_FLYING,
        .on_act = nullptr,
        .on_death = nullptr,
    };
    return desc;
}
