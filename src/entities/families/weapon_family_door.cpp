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
#include <openglad/data/level_data.h>
#include <openglad/core/terrain_types.h>
#include <openglad/legacy/soundob.h>

static bool door_on_death(weap* self)
{
    walker* newob = current_game->world->add_weap_ob(Order::FX, FAMILY_DOOR_OPEN);
    if (!newob)
        return false;
    newob->ani_type = ANI_DOOR_OPEN;
    newob->setxy(self->xpos, self->ypos);
    newob->stats()->level = self->stats()->level;
    newob->team_num = self->team_num;
    // What way are we 'facing'?
    if (current_game->world->mysmoother.query_genre_x_y((self->xpos/GRID_SIZE),(self->ypos/GRID_SIZE)-1)
            == TYPE_WALL) // a wall above us?
    {
        newob->curdir = FACE_RIGHT;
    }
    else
    {
        self->curdir = FACE_UP;
    }
    return true;
}

const WeaponFamilyDescriptor& describe_weapon_door()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_DOOR,
        .name = "DOOR",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = true,
        .is_auto_attackable = true,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = door_on_death,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
