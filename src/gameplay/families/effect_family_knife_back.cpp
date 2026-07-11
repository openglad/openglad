/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/statistics.h>

static bool knife_back_on_act(effect* self)
{
    if (!self->owner() || self->owner()->dead())
    {
        self->set_dead(1);
        return true;
    }
    std::int32_t distance = self->distance_to_ob(self->owner());
    if (distance > 10)
    {
        float xd = 0, yd = 0;
        if (self->owner()->xpos() > self->xpos())
        {
            if ( (self->owner()->xpos() - self->xpos()) > self->stepsize() )
                xd = self->stepsize();
            else
                xd = self->owner()->xpos() - self->xpos();
        }
        else if (self->owner()->xpos() < self->xpos())
        {
            if ( (self->xpos() - self->owner()->xpos()) > self->stepsize() )
                xd = -self->stepsize();
            else
                xd = self->owner()->xpos() - self->xpos();
        }
        if (self->owner()->ypos() > self->ypos())
        {
            if ( (self->owner()->ypos() - self->ypos()) > self->stepsize() )
                yd = self->stepsize();
            else
                yd = self->owner()->ypos() - self->ypos();
        }
        else if (self->owner()->ypos() < self->ypos())
        {
            if ( (self->ypos() - self->owner()->ypos()) > self->stepsize() )
                yd = -self->stepsize();
            else
                yd = self->owner()->ypos() - self->ypos();
        }
        self->setworldxy(self->worldx()+xd, self->worldy()+yd);
        walker* newob = current_game->world->add_ob(Order::Weapon, FAMILY_KNIFE);
        if (!newob)
        {
            // GCOVR_EXCL_START -- add_ob only returns null on allocation failure
            self->set_ani_type(ANI_WALK);
            self->set_dead(1);
            return true;
            // GCOVR_EXCL_STOP
        }
        newob->set_damage(self->damage());
        newob->set_owner(self->owner());
        newob->set_team_num(self->team_num());
        newob->set_death_called(1); // to ensure no spawning of more ..
        newob->set_floor(self->floor());  // collision probe on our floor (A8)
        newob->setworldxy(self->worldx(), self->worldy());
        if (!current_game->world->query_object_passable(self->xpos()+xd, self->ypos()+yd, newob))
        {
            newob->attack(newob->collide_ob());
            self->set_damage(self->damage() / 4.0f);
        }
        newob->set_dead(1);
    }
    else
    {
        self->owner()->set_weapons_left(self->owner()->weapons_left() + 1);
        self->set_ani_type(ANI_WALK);
        self->set_dead(1);
    }
    return true;
}

const EffectFamilyDescriptor& describe_effect_knife_back()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_KNIFE_BACK,
        .name = "KNIFE_BACK",
        .loops_animation = true,
        .creates_hit_effect = true,
        .init_bit_flags = 0,
        .on_act = knife_back_on_act,
        .on_death = nullptr,
    };
    return desc;
}
