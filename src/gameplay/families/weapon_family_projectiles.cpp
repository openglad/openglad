/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/constants.h>

const WeaponFamilyDescriptor& describe_weapon_projectiles_fire_arrow()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_FIRE_ARROW,
        .name = "FIRE_ARROW",
        .fire_sound = SOUND_BOW,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_projectiles_boulder()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_BOULDER,
        .name = "BOULDER",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
