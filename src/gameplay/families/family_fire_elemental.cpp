/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>

#include <iterator>

#define BASE_GUY_HP 30

static bool fire_elemental_check_special_ai(living* self)
{
    return check_special_ai_distance(self, 130);
}

static bool fire_elemental_on_death(walker* self)
{
    self->set_dead(0);
    self->stats()->set_magicpoints(self->stats()->magicpoints() + self->stats()->special_cost(1));
    self->special();
    self->set_dead(1);
    return true;
}

static void fire_elemental_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {12, 6, 4, 8, 1});
}

static void fire_elemental_on_act_living(living* self)
{
    // Summoned fire elemental drains owner HP/MP to heal itself
    if (self->lifetime() && self->owner() && !self->owner()->dead())
    {
        if (self->stats()->hitpoints() < self->stats()->max_hitpoints())
        {
            std::int32_t temp = 0;
            if (self->owner()->stats()->hitpoints() >= (self->owner()->stats()->max_hitpoints() / 3))
            {
                temp = 1;
                self->owner()->stats()->set_hitpoints(self->owner()->stats()->hitpoints() - 1);
            }
            if (temp && (self->owner()->stats()->magicpoints() >= 3))
            {
                temp += 1;
                self->owner()->stats()->set_magicpoints(self->owner()->stats()->magicpoints() - 3);
            }
            if (temp == 2)
                self->stats()->set_hitpoints(self->stats()->hitpoints() + 1);
            else
                self->set_lifetime(self->lifetime() - 1);
        }
    }
}

static bool fire_elemental_do_special(walker* self)
{
    std::int32_t tempx = static_cast<std::int32_t>(self->lastx());
    std::int32_t tempy = static_cast<std::int32_t>(self->lasty());
    self->stats()->set_magicpoints(self->stats()->magicpoints() + 8.0f * static_cast<float>(self->stats()->weapon_cost()));
    for (std::int32_t i = -1; i < 2; i++)
        for (std::int32_t j = -1; j < 2; j++)
        {
            if (i || j)
            {
                self->set_lastx(static_cast<float>(i));
                self->set_lasty(static_cast<float>(j));
                self->fire();
            }
        }
    self->set_lastx(static_cast<float>(tempx));
    self->set_lasty(static_cast<float>(tempy));
    return true;
}

static const char* const elemental_names[] = {"Furnace", "Molten", "Burns", "Fire Eli", "Fireball", "Sunny", "Lava", "Heatwave", "Torch", "Scorch"};

const FamilyDescriptor& describe_family_fire_elemental()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_FIREELEMENTAL,
        .name = "ELEMENTAL",
        .short_name = "ELEMENT.",
        .base_stats = {14, 10, 14, 14, 9, 1},
        .hiring_cost = 600,
        .derived_bonuses = {BASE_GUY_HP+70, 0, 28, 0, 0, 0, 4, 5},
        .stat_costs = {7, 10, 14, 12, 50, 200},
        .special_cost = {5000, 50, 5000, 5000, 5000, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_METEOR,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 150,
        .special_names = {"NONE", "STARBURST", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "FIRE ELEMENTAL EXTINGUISHED",
        .do_special = fire_elemental_do_special,
        .check_special_ai = fire_elemental_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = fire_elemental_level_up,
        .on_death = fire_elemental_on_death,
        .on_act_living = fire_elemental_on_act_living,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "firelem.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 10,
        .description = "Strong and quick, fire    \n"
                       "elementals can expel      \n"
                       "flaming meteors in all    \n"
                       "directions to decimate    \n"
                       "enemies.                  \n"
                       "\n"
                       "Special: Starburst",
        .name_pool = elemental_names,
        .name_pool_size = std::size(elemental_names),
        .is_playable = true,
        .playable_order = 11,
    };
    return desc;
}
