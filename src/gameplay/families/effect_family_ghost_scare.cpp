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
#include <openglad/gameplay/guy.h>
#include <openglad/core/combat_math.h>

static bool ghost_scare_on_act(effect* self)
{
    if (self->owner())
        self->center_on(self->owner());
    return false; // delegate to effect::act default animate/die path
}

static bool ghost_scare_on_death(effect* self)
{
    if (!self->owner() || self->owner()->dead())
        return false;
    std::int32_t howmany = 0;
    // Runaway-specials §2.3: radius capped at 250 px (legacy 50 + 10*L goes
    // off-screen at L27); binds only at L21+, draw-free.
    auto foelist = current_game->world->find_foes_in_range(current_game->world->oblist,
        og::combat::scare_radius(self->owner()->stats()->level()), &howmany, self->owner());
    if (howmany < 1)
        return false;

    for(auto* w : foelist)
    {
        if (w && w->query_order() == Order::Living)
        {
            std::int32_t tempx = w->xpos() - self->xpos();
            if (tempx)
                tempx = tempx / (abs(tempx));
            std::int32_t tempy = w->ypos() - self->ypos();
            if (tempy)
                tempy = tempy / (abs(tempy));
            // Runaway-specials §2.2: duration soft-capped (legacy 25*L,
            // knee 325 = L13, ceiling 375) BEFORE the legacy myguy-only
            // constitution resist draw — draw order/count identical, and
            // both scare goldens (L1 = 25, L5 = 125) sit below the knee.
            std::int32_t generic = og::combat::scare_duration(self->owner()->stats()->level());
            if (w->myguy)
                generic -= current_game->world->rng_.next(w->myguy->constitution);
            if (generic > 0)
                w->stats()->force_fright(generic, tempx, tempy);
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
