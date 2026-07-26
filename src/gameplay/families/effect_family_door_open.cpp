/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/core/constants.h>

const EffectFamilyDescriptor& describe_effect_door_open()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_DOOR_OPEN,
        .name = "DOOR_OPEN",
        .loops_animation = false,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = nullptr,
        .on_death = nullptr,
    };
    return desc;
}
