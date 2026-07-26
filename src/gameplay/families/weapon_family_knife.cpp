/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/core/sound_ids.h>

const WeaponFamilyDescriptor& describe_weapon_knife()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_KNIFE,
        .name = "KNIFE",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        // Thrown knives drift up then settle (slight arc) and can drop through pits.
        .init_vz = 0.35f,
        .gravity = 0.05f,
        .can_drop_floors = true,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
