/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/effect_family_descriptor.h>
#include <openglad/entities/effect.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/level_runtime_data.h>

short hits(short x,  short y,  short xsize,  short ysize,
           short x2, short y2, short xsize2, short ysize2);

static bool cloud_on_act(effect* self)
{
    std::int32_t temp = 0;

    if (self->lifetime > 0)
        self->lifetime--;
    else
    {
        self->dead = 1;
        self->death();
        return true;
    }
    if (self->lifetime < 8)
        self->invisibility_left +=3;
    if (self->invisibility_left > 0)
        self->invisibility_left--;
    // Hit any nearby foes (not friends, for now)
    auto foelist = current_game->world->find_foes_in_range(
        current_game->world->oblist, self->sizex, &temp, self);

    for(auto* w : foelist)
    {
        if (hits(self->xpos, self->ypos, self->sizex, self->sizey,
                 w->xpos, w->ypos, w->sizex, w->sizey))
        {
            self->attack(w);
        }
    }

    // Are we performing some action?
    if (self->stats()->has_commands())
        self->stats()->do_command();
    else
    {
        float xd = 0, yd = 0;
        while (xd == 0 && yd == 0)
        {
            xd = static_cast<float>(static_cast<std::int32_t>(current_game->world->rng_.next(3)) - 1);
            yd = static_cast<float>(static_cast<std::int32_t>(current_game->world->rng_.next(3)) - 1);
        }
        self->stats()->add_command(COMMAND_WALK, static_cast<short>(current_game->world->rng_.next(20)),
            static_cast<short>(xd), static_cast<short>(yd));
    }
    return true;
}

const EffectFamilyDescriptor& describe_effect_cloud()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_CLOUD,
        .name = "CLOUD",
        .loops_animation = true,
        .creates_hit_effect = false,
        .init_bit_flags = BIT_NO_COLLIDE | BIT_FLYING,
        .on_act = cloud_on_act,
        .on_death = nullptr,
    };
    return desc;
}
