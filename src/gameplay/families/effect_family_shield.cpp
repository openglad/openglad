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

const EffectFamilyDescriptor& describe_effect_magic_shield()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_MAGIC_SHIELD,
        .name = "MAGIC_SHIELD",
        .loops_animation = true,
        .creates_hit_effect = false,
        .init_bit_flags = BIT_PHANTOM,
        .on_act = nullptr,
        .on_death = nullptr,
    };
    return desc;
}

const EffectFamilyDescriptor& describe_effect_boomerang()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_BOOMERANG,
        .name = "BOOMERANG",
        .loops_animation = true,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = nullptr,
        .on_death = nullptr,
    };
    return desc;
}
