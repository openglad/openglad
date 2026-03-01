/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/entities/weap.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>

static bool projectile_explode_on_death(weap* self)
{
    if (!self->skip_exit)
        return false;  // skip_exit means we're supposed to explode :)
    if (!self->owner || self->owner->dead)
        self->owner = self;
    walker* newob = current_game->world->add_ob(Order::FX, FAMILY_EXPLOSION, 1);
    if (!newob)
        return false; // failsafe
    og::sim::emit_sound(current_game->sim_events, SOUND_EXPLODE);
    newob->owner = self->owner;
    newob->stats()->hitpoints = 0;
    newob->stats()->level = self->owner->stats()->level;
    newob->ani_type = ANI_EXPLODE;
    newob->center_on(self);
    newob->damage = self->damage*2;
    return true;
}

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
        .on_death = projectile_explode_on_death,
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
        .on_death = projectile_explode_on_death,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
