/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/sound_ids.h>

static bool rock_on_death(weap* self)
{
    if (!self->do_bounce() || !self->lineofsight() || self->collide_ob()) // died of natural causes
        return false;
    self->set_dead(0); // first, un-dead us so we can collide ..
    // Did we hit a barrier?
    if (current_game->world->query_grid_passable(self->xpos()+self->lastx(), self->ypos()+self->lasty(), self))
    {
        self->set_dead(1);
        return false; // if not, die like normal
    }
    if (current_game->world->query_grid_passable(self->xpos()-self->lastx(), self->ypos()+self->lasty(), self))
    {
        self->setxy(self->xpos()-self->lastx(), self->ypos()+self->lasty());  // bounce 'down-left'
        self->set_lastx(-self->lastx());
        self->set_death_called(0);
        return true;
    }
    if (current_game->world->query_grid_passable(self->xpos()+self->lastx(), self->ypos()-self->lasty(), self))
    {
        self->setxy(self->xpos()+self->lastx(), self->ypos()-self->lasty()); // bounce 'up-right'
        self->set_lasty(-self->lasty());
        self->set_death_called(0);
        return true;
    }
    if (current_game->world->query_grid_passable(self->xpos()-self->lastx(), self->ypos()-self->lasty(), self))
    {
        self->setxy(self->xpos()-self->lastx(), self->ypos()-self->lasty());
        self->set_lastx(-self->lastx());
        self->set_lasty(-self->lasty());
        self->set_death_called(0);
        return true;
    }
    // Else we're really stuck, so die :)
    self->set_dead(1);
    return false;
}

const WeaponFamilyDescriptor& describe_weapon_rock()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_ROCK,
        .name = "ROCK",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = BIT_FORESTWALK,
        .init_lifetime = 0,
        .init_ani_type = 0,
        // Elf rocks launch upward and arc back down under gravity; drop through pits.
        .init_vz = 0.7f,
        .gravity = 0.09f,
        .can_drop_floors = true,
        .on_death = rock_on_death,
        .on_animate = nullptr,
        .on_hit_target = nullptr,
    };
    return desc;
}
