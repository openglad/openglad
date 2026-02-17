/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include "SDL_stdinc.h"
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/weap.h>
#include <openglad/data/level_data.h>
#include <openglad/legacy/soundob.h>

static bool knife_on_death(weap* self)
{
    const auto* owner_fd = self->owner ? get_family_descriptor(self->owner->query_family()) : nullptr;
    if (!owner_fd || !owner_fd->has_returning_weapon)
        return false; // no special handling

    walker* newob = self->sim_level->add_ob(Order::FX, FAMILY_KNIFE_BACK);
    newob->owner = self->owner;
    newob->center_on(self);
    newob->lastx = self->lastx;
    newob->lasty = self->lasty;
    newob->stepsize = self->stepsize;
    newob->ani_type = ANI_ATTACK;
    newob->damage = self->damage;
    return true;
}

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
        .on_death = knife_on_death,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
