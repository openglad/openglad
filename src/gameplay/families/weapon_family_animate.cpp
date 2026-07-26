/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Behavior lives in packs/core/scripts/weapon_animate.lua (design doc §9a);
// this file now carries descriptor DATA only. The animation-row walk that
// tree/blood and glow shared is og.ani_row() there — the bound accessor
// applies the same walker::ani_count invariant this file used to enforce
// inline.
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/constants.h>

const WeaponFamilyDescriptor& describe_weapon_animate_tree()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_TREE,
        .name = "TREE",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = true,
        .is_auto_attackable = true,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_blood()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_BLOOD,
        .name = "BLOOD",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = true,
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

const WeaponFamilyDescriptor& describe_weapon_animate_circle_protection()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_CIRCLE_PROTECTION,
        .name = "CIRCLE_PROTECTION",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = BIT_IMMORTAL | BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING,
        .init_lifetime = 0,
        .init_ani_type = 5,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_glow()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_GLOW,
        .name = "GLOW",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = true,
        .init_bit_flags = 0,
        .init_lifetime = 350,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_sprinkle()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_SPRINKLE,
        .name = "SPRINKLE",
        .fire_sound = SOUND_SPARKLE,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = BIT_FLYING,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
