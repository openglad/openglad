/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Behavior lives in packs/core/scripts/weapon_wave.lua (design doc §9a);
// this file now carries descriptor DATA only.
#include <cstdint>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/constants.h>

static constexpr std::int32_t WAVE_BIT_FLAGS = BIT_IMMORTAL | BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING | BIT_MAGICAL;

const WeaponFamilyDescriptor& describe_weapon_wave()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_WAVE,
        .name = "WAVE",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = WAVE_BIT_FLAGS,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_wave2()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_WAVE2,
        .name = "WAVE2",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = WAVE_BIT_FLAGS,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
