/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/statistics.h>

static bool door_open_on_act(effect* self)
{
    if (self->ani_type() != ANI_WALK)
        return false; // let default animate() handle it

    walker* newob = current_game->world->add_fx_ob(Order::FX, FAMILY_DOOR_OPEN);
    if (!newob)
        return true; // handled (nothing to spawn)
    newob->set_ani_type(ANI_WALK);
    newob->set_floor(self->floor());  // opened door stays on its floor (A8)
    newob->setworldxy(self->worldx(), self->worldy());
    newob->stats()->set_level(self->stats()->level());
    newob->set_team_num(self->team_num());
    newob->set_ignore(1);
    newob->set_curdir(self->curdir());
    // set correct frame
    newob->animate();
    self->set_dead(1);
    self->death();
    return true;
}

const EffectFamilyDescriptor& describe_effect_door_open()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_DOOR_OPEN,
        .name = "DOOR_OPEN",
        .loops_animation = false,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = door_open_on_act,
        .on_death = nullptr,
    };
    return desc;
}
