/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/effect_family_descriptor.h>
#include <openglad/entities/effect.h>
#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

namespace
{
inline screen* active_screen()
{
    if(ctx().game_screen != nullptr)
        return ctx().game_screen;
    return myscreen;
}
} // namespace

short hits(short x,  short y,  short xsize,  short ysize,
           short x2, short y2, short xsize2, short ysize2);

static bool chain_on_act(effect* self)
{
    if (!self->leader || self->lineofsight<1 || !self->owner)
    {
        self->dead = 1;
        self->death();
        return true;
    }
    // Are we at our leader? If so, attack him :)
    if (hits(self->xpos, self->ypos, self->sizex, self->sizey,
             self->leader->xpos, self->leader->ypos, self->leader->sizex, self->leader->sizey))
    {
        walker* newob = active_screen()->level_data.add_ob(Order::FX, FAMILY_EXPLOSION);
        if (!newob)
        {
            self->dead = 1;
            self->death();
            return true;
        }
        newob->owner = self->owner;
        newob->team_num = self->team_num;
        newob->stats()->level = self->stats()->level;
        newob->damage = self->damage;
        newob->ani_type = ANI_EXPLODE;
        newob->center_on(self);
        self->leader->skip_exit = self->leader->skip_exit + 3;
        if (self->on_screen())
            og::sim::emit_sound(SOUND_EXPLODE);
        // Now make new objects to seek out foes ..
        float generic = self->damage * 0.5f;
        Sint32 temp = 0;
        std::list<walker*> foelist;
        if (self->owner->myguy)
            foelist = active_screen()->find_foes_in_range(active_screen()->level_data.oblist,
                240+(self->owner->myguy->intelligence/2), &temp, self);
        else
            foelist = active_screen()->find_foes_in_range(active_screen()->level_data.oblist,
                240+self->stats()->level*5, &temp, self);
        if (temp && generic>20)
        {
            Sint32 numfoes = static_cast<Sint32>(rng(static_cast<Uint32>(self->owner->stats()->level))) + 1;
            for(auto* w : foelist)
            {
                if (numfoes <= 0) break;
                if (w != self->leader && w->skip_exit<1)
                {
                    newob = active_screen()->level_data.add_ob(Order::FX, FAMILY_CHAIN);
                    if (!newob)
                        return true;
                    newob->owner = self->owner;
                    newob->leader = w;
                    newob->stats()->level = self->stats()->level;
                    newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                    newob->damage = generic;
                    newob->team_num = self->team_num;
                    newob->center_on(self);
                }
                numfoes--;
            }
        }

        self->dead = 1;
        self->death();
        return true;
    }
    // Move toward our leader ..
    self->lineofsight--;
    Sint32 distance = self->distance_to_ob_center(self->leader);
    if (static_cast<float>(distance) > self->stepsize*2)
    {
        float xd = 0, yd = 0;
        if (self->leader->xpos > self->xpos)
        {
            if ( (self->leader->xpos - self->xpos) > self->stepsize )
                xd = self->stepsize;
            else
                xd = self->leader->xpos - self->xpos;
        }
        else if (self->leader->xpos < self->xpos)
        {
            if ( (self->xpos - self->leader->xpos) > self->stepsize )
                xd = -self->stepsize;
            else
                xd = self->leader->xpos - self->xpos;
        }
        if (self->leader->ypos > self->ypos)
        {
            if ( (self->leader->ypos - self->ypos) > self->stepsize )
                yd = self->stepsize;
            else
                yd = self->leader->ypos - self->ypos;
        }
        else if (self->leader->ypos < self->ypos)
        {
            if ( (self->ypos - self->leader->ypos) > self->stepsize )
                yd = -self->stepsize;
            else
                yd = self->leader->ypos - self->ypos;
        }
        self->curdir = static_cast<signed char>(self->facing(xd, yd));
        self->set_frame(self->ani[self->curdir][0]);
        self->setworldxy(self->worldx()+xd, self->worldy()+yd);
    }
    else
    {
        self->center_on(self->leader);
    }
    return true; // skip default animate/die
}

const EffectFamilyDescriptor& describe_effect_chain()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_CHAIN,
        .name = "CHAIN",
        .loops_animation = false,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = chain_on_act,
        .on_death = nullptr,
    };
    return desc;
}
