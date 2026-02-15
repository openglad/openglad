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
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>

namespace
{
inline screen* active_screen()
{
    if(ctx().game_screen != nullptr)
        return ctx().game_screen;
    return myscreen;
}
} // namespace

Sint32 compute_explosion_range(Sint32 level, short skip_exit);

static bool bomb_on_death(effect* self)
{
    if (!self->owner || self->owner->dead)
        self->owner = self;
    og::sim::emit_sound(SOUND_EXPLODE);
    walker* newob = active_screen()->level_data.add_ob(Order::FX, FAMILY_EXPLOSION, 1);
    newob->owner = self->owner;
    newob->stats()->hitpoints = 0;
    newob->stats()->level = self->owner->stats()->level;
    newob->ani_type = ANI_EXPLODE;
    newob->center_on(self);
    newob->damage = self->damage;
    return true;
}

static bool explosion_on_death(effect* self)
{
    if (!self->owner || self->owner->dead)
        self->owner = self;
    Sint32 generic = compute_explosion_range(self->owner->stats()->level, self->skip_exit);
    Sint32 howmany = 0;
    auto foelist = active_screen()->find_in_range(active_screen()->level_data.oblist, 15+generic,
        &howmany, self);

    // Damage our tile location ..
    active_screen()->damage_tile( static_cast<short>(self->xpos+(self->sizex/2)),
        static_cast<short>(self->ypos+(self->sizey/2)) );
    if (howmany < 1)
        return false;

    for(auto* w : foelist)
    {
        if (w && !w->dead &&
                (w->query_order() != Order::Treasure) &&
                (w->query_order() != Order::FX) &&
                (!self->skip_exit || w != self->owner)
           )
        {
            Sint32 xdelta = w->xpos - self->xpos;
            if (xdelta)
                xdelta = xdelta/abs(xdelta);
            Sint32 ydelta = w->ypos - self->ypos;
            if (ydelta)
                ydelta = ydelta/abs(ydelta);
            generic = 2+self->owner->stats()->level/15;
            if (generic > 8)
                generic = 8;
            w->stats()->force_command(COMMAND_WALK,generic,
                static_cast<short>(xdelta),static_cast<short>(ydelta));
            if (w == self->owner)
            {
                self->damage /= 4.0f;
                self->attack(w);
                self->damage *= 4.0f;
            }
            else if (!self->owner->dead && self->owner->is_friendly(w))
            {
                self->damage /= 2.0f;
                self->attack(w);
                self->damage *= 2.0f;
            }
            else
                self->attack(w);
        }
    }
    return true;
}

const EffectFamilyDescriptor& describe_effect_bomb()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_BOMB,
        .name = "BOMB",
        .loops_animation = false,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = nullptr,
        .on_death = bomb_on_death,
    };
    return desc;
}

const EffectFamilyDescriptor& describe_effect_explosion()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_EXPLOSION,
        .name = "EXPLOSION",
        .loops_animation = false,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = nullptr,
        .on_death = explosion_on_death,
    };
    return desc;
}
