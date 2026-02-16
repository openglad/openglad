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
#include <openglad/data/level_data.h>

void orbit_offset(int drawcycle, float &xd, float &yd);

static bool magic_shield_on_act(effect* self)
{
    float xd, yd;
    Sint32 temp = 0;

    if (!self->owner || self->owner->dead)
    {
        self->dead = 1;
        self->death();
        return true;
    }
    orbit_offset(self->drawcycle, xd, yd);
    self->center_on(self->owner);
    self->setworldxy(self->worldx()+xd, self->worldy()+yd);

    auto foelist = self->sim_level->find_foe_weapons_in_range(
        self->sim_level->oblist, self->sizex, &temp, self);
    for(auto* w : foelist)
    {
        self->stats()->hitpoints -= w->damage;
        w->dead = 1;
        w->death();
    }

    foelist = self->sim_level->find_foes_in_range(
        self->sim_level->oblist, self->sizex, &temp, self);
    for(auto* w : foelist)
    {
        self->stats()->hitpoints -= w->damage;
        self->attack(w);
        self->dead = 0;
    }

    if ( (self->stats()->hitpoints <= 0) || (self->lifetime-- < 0) )
    {
        self->dead = 1;
        self->death();
    }
    return true;
}

static bool boomerang_on_act(effect* self)
{
    float xd, yd;
    Sint32 temp = 0;

    if (!self->owner || self->owner->dead || self->drawcycle > 253)
    {
        self->dead = 1;
        self->death();
        return true;
    }
    orbit_offset(self->drawcycle, xd, yd);
    xd *= (self->drawcycle+4);
    xd /= 48;
    yd *= (self->drawcycle+4);
    yd /= 48;
    self->center_on(self->owner);
    self->setworldxy(self->worldx()+xd, self->worldy()+yd);

    auto foelist = self->sim_level->find_foe_weapons_in_range(
        self->sim_level->oblist, self->sizex*2, &temp, self);
    for(auto* w : foelist)
    {
        self->stats()->hitpoints -= w->damage;
        w->dead = 1;
        w->death();
    }

    foelist = self->sim_level->find_foes_in_range(
        self->sim_level->oblist, self->sizex, &temp, self);
    for(auto* w : foelist)
    {
        self->stats()->hitpoints -= w->damage;
        self->attack(w);
        self->dead = 0;
    }

    if ( (self->stats()->hitpoints <= 0) || (self->lifetime-- < 0) )
    {
        self->dead = 1;
        self->death();
    }
    return true;
}

const EffectFamilyDescriptor& describe_effect_magic_shield()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_MAGIC_SHIELD,
        .name = "MAGIC_SHIELD",
        .loops_animation = true,
        .creates_hit_effect = false,
        .init_bit_flags = BIT_PHANTOM,
        .on_act = magic_shield_on_act,
        .on_death = nullptr,
    };
    return desc;
}

const EffectFamilyDescriptor& describe_effect_boomerang()
{
    static const EffectFamilyDescriptor desc = {
        .family_id = FAMILY_BOOMERANG,
        .name = "BOOMERANG",
        .loops_animation = true,
        .creates_hit_effect = false,
        .init_bit_flags = 0,
        .on_act = boomerang_on_act,
        .on_death = nullptr,
    };
    return desc;
}
