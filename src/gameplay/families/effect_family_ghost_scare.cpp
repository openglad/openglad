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
#include <openglad/core/stats.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/game_world.h>

static bool ghost_scare_on_act(effect* self)
{
    if (self->owner)
        self->center_on(self->owner);
    return true; // handled, fall through to animate/die
}

static bool ghost_scare_on_death(effect* self)
{
    if (!self->owner || self->owner->dead)
        return false;
    std::int32_t howmany = 0;
    auto foelist = og::gameplay::current_game->world->find_foes_in_range(og::gameplay::current_game->world->oblist,
        50+(10*self->owner->stats()->level), &howmany, self->owner);
    if (howmany < 1)
        return false;

    for(auto* w : foelist)
    {
        if (w && w->query_order() == Order::Living)
        {
            std::int32_t tempx = w->xpos - self->xpos;
            if (tempx)
                tempx = tempx / (abs(tempx));
            std::int32_t tempy = w->ypos - self->ypos;
            if (tempy)
                tempy = tempy / (abs(tempy));
            std::int32_t generic = (self->owner->stats()->level*25);
            if (w->myguy)
                generic -= og::gameplay::current_game->world->rng_.next(w->myguy->constitution);
            if (generic > 0)
                w->stats()->force_command(COMMAND_WALK,
                    static_cast<short>(generic), static_cast<short>(tempx), static_cast<short>(tempy));
        }
    }
    return true;
}

const EffectFamilyDescriptor& describe_effect_ghost_scare()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_GHOST_SCARE,
        .name = "GHOST_SCARE",
        .loops_animation = false,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = ghost_scare_on_act,
        .on_death = ghost_scare_on_death,
    };
    return desc;
}
