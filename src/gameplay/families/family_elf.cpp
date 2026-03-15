/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>

#define BASE_GUY_HP 30

namespace
{
IRandom& elf_rng()
{
    if (IRandom* override_rng = gameplay_rng_override())
        return *override_rng;

    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->rng_;

    assert(false && "elf special requires an injected RNG");
    std::abort();
}

float next_spread_multiplier(IRandom& rng)
{
    return 0.8f + 0.4f * static_cast<float>(rng.next(101)) / 100.0f;
}
}

static bool elf_do_special(walker* self)
{
    weap* fireob;
    std::int32_t i;
    IRandom& rng = elf_rng();

    switch (self->current_special())
    {
        case 1: // some rocks (normal)
            self->stats()->set_magicpoints(self->stats()->magicpoints() + 2.0f * static_cast<float>(self->stats()->weapon_cost()));
            fireob = static_cast<weap*>(self->fire());
            if (!fireob)
                return false;
            fireob->set_lastx(fireob->lastx() * next_spread_multiplier(rng));
            fireob->set_lasty(fireob->lasty() * next_spread_multiplier(rng));
            fireob = static_cast<weap*>(self->fire());
            if (!fireob)
                return false;
            fireob->set_lastx(fireob->lastx() * next_spread_multiplier(rng));
            fireob->set_lasty(fireob->lasty() * next_spread_multiplier(rng));
            break;
        case 2: // more rocks, and bouncing
            self->stats()->set_magicpoints(self->stats()->magicpoints() + 3.0f * static_cast<float>(self->stats()->weapon_cost()));
            for (i = 0; i < 2; i++)
            {
                fireob = static_cast<weap*>(self->fire());
                if (!fireob)
                    return false;
                fireob->set_lineofsight((fireob->lineofsight() * 3) / 2);
                fireob->set_do_bounce(1);
                fireob->set_lastx(fireob->lastx() * next_spread_multiplier(rng));
                fireob->set_lasty(fireob->lasty() * next_spread_multiplier(rng));
            }
            break;
        case 3:
            self->stats()->set_magicpoints(self->stats()->magicpoints() + 4.0f * static_cast<float>(self->stats()->weapon_cost()));
            for (i = 0; i < 3; i++)
            {
                fireob = static_cast<weap*>(self->fire());
                if (!fireob)
                    return false;
                fireob->set_lineofsight(fireob->lineofsight() * 2);
                fireob->set_do_bounce(1);
                fireob->set_lastx(fireob->lastx() * next_spread_multiplier(rng));
                fireob->set_lasty(fireob->lasty() * next_spread_multiplier(rng));
            }
            break;
        case 4:
        default:
            self->stats()->set_magicpoints(self->stats()->magicpoints() + 5.0f * static_cast<float>(self->stats()->weapon_cost()));
            for (i = 0; i < 4; i++)
            {
                fireob = static_cast<weap*>(self->fire());
                if (!fireob)
                    return false;
                fireob->set_lineofsight((fireob->lineofsight() * 5) / 2);
                fireob->set_do_bounce(1);
                fireob->set_lastx(fireob->lastx() * next_spread_multiplier(rng));
                fireob->set_lasty(fireob->lasty() * next_spread_multiplier(rng));
            }
            break;
    }
    return true;
}

static void elf_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {6, 9, 6, 8, 1});
}

static const char* const elf_names[] = {"Legolas", "Took", "Elrond", "Tanis", "Acorn", "Lightfoot", "Treewee"};

const FamilyDescriptor& describe_family_elf()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ELF,
        .name = "ELF",
        .short_name = nullptr,
        .base_stats = {5, 14, 5, 12, 8, 1},
        .hiring_cost = 150,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 12, 0, 0, 0, 4, 5},
        .stat_costs = {25, 6, 12, 8, 50, 200},
        .special_cost = {5000, 10, 20, 30, 40, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_ROCK,
        .init_bit_flags = BIT_FORESTWALK,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "ROCKS", "BOUNCING ROCKS", "LOTS OF ROCKS", "MEGA ROCKS", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "ELF KILLED",
        .do_special = elf_do_special,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = elf_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "elf.png",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 8,
        .description = "Elves are small and weak, \n"
                       "but are harder to hit than\n"
                       "most classes. Alone of all\n"
                       "the classes, elves possess\n"
                       "the 'ForestWalk' ability. \n"
                       "\n"
                       "Special: Rocks",
        .name_pool = elf_names,
        .name_pool_size = sizeof(elf_names) / sizeof(elf_names[0]),
        .is_playable = true,
        .playable_order = 2,
    };
    return desc;
}
