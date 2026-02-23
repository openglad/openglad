/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>

#include <cmath>
#include <format>
#include <string>
#include <list>

#define BASE_GUY_HP 30

// ---- Difficulty scaling ----
static void monk_set_difficulty(living* self, std::uint32_t level)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += 10.0f * levmult;
    self->stats()->max_magicpoints += 8.0f * levmult;
    self->damage += 4.0f * level_f;
    self->stats()->armor += levmult / 3.0f;
}

// ---- Level-up stat growth ----
// Monks gain DEX fastest (1.5x), INT slowest (0.75x)
static void monk_level_up(guy* self, std::int32_t level_diff)
{
    std::int32_t s  = 8 * level_diff;         // STR: normal
    std::int32_t d  = 6 * level_diff;         // DEX base
    std::int32_t c  = 8 * level_diff;         // CON: normal
    std::int32_t it = 8 * level_diff;         // INT base
    std::int32_t a  = 1 * level_diff;         // ARMOR: normal
    d = (d * 3) / 2;  // 1.5x DEX growth
    it = (it * 3) / 4; // 0.75x INT growth
    self->strength     = static_cast<short>(static_cast<std::int32_t>(self->strength) + s);
    self->dexterity    = static_cast<short>(static_cast<std::int32_t>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<std::int32_t>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<std::int32_t>(self->intelligence) + it);
    self->armor        = static_cast<short>(static_cast<std::int32_t>(self->armor) + a);
}

// ---- Melee hit callback: knockback + brief stun ----
static void monk_on_melee_hit(walker* self, walker* target)
{
    if (!target || target->dead)
        return;

    // Brief stun: force target to stumble for a few frames
    std::int32_t stun_time = 3 + self->stats()->level;
    float dx = target->xpos - self->xpos;
    float dy = target->ypos - self->ypos;
    float dist = dx * dx + dy * dy;
    std::int32_t push_x = 0;
    std::int32_t push_y = 0;
    if (dist > 0.01f)
    {
        dist = std::sqrt(dist);
        push_x = static_cast<std::int32_t>((dx / dist) * 2.0f);
        push_y = static_cast<std::int32_t>((dy / dist) * 2.0f);
    }
    // Force the target to stumble backward
    target->stats()->force_command(COMMAND_WALK, stun_time, push_x, push_y);
}

// ---- AI special check ----
static bool monk_check_special_ai(living* self)
{
    // Special 1 (Silence): use when a foe is close and not already silenced
    if (self->current_special == 1)
    {
        if (self->foe)
        {
            std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe));
            return (distance < 60 && self->foe->silence_left <= 0);
        }
        return false;
    }
    // Special 2 (Meditate): use when hurt
    if (self->current_special == 2)
    {
        return (self->stats()->hitpoints < self->stats()->max_hitpoints * 0.5f);
    }
    // Special 3 (Evasion): use when surrounded
    if (self->current_special == 3)
    {
        std::int32_t howmany = 0;
        self->sim_level->find_foes_in_range(self->sim_level->oblist,
                                     80, &howmany, self);
        return (howmany >= 2);
    }
    // Special 4 (Whirlwind Kick): use when many foes close
    if (self->current_special == 4)
    {
        std::int32_t howmany = 0;
        self->sim_level->find_foes_in_range(self->sim_level->oblist,
                                     60, &howmany, self);
        return (howmany >= 3);
    }
    return true;
}

// ---- Special abilities ----
static bool monk_do_special(walker* self)
{
    std::string message;

    switch (self->current_special)
    {
        case 1: // SILENCE — disable target's specials
        {
            if (self->busy > 0)
                return false;

            std::int32_t howmany;
            std::list<walker*> foes = self->sim_level->find_foes_in_range(
                self->sim_level->oblist, 40 + self->stats()->level * 2,
                &howmany, self);
            if (howmany < 1)
                return false;

            // Silence the nearest foe (find_foes_in_range doesn't sort by distance)
            walker* target = nullptr;
            std::int32_t best_dist = INT32_MAX;
            for (auto* f : foes)
            {
                if (!f) continue;
                std::int32_t d = self->distance_to_ob(f);
                if (d < best_dist)
                {
                    best_dist = d;
                    target = f;
                }
            }
            if (target)
            {
                std::int32_t duration = 30 + self->stats()->level * 10;
                target->silence_left = static_cast<short>(
                    target->silence_left + duration);

                if (self->myguy)
                    message = std::format("{}: Silence!", self->myguy->name);
                else
                    message = "MONK: Silence!";
                og::sim::emit_notification(self->sim_events, message);
                og::sim::emit_sound(self->sim_events, SOUND_CHARGE);
            }
            self->busy += 5;
            break;
        }

        case 2: // MEDITATE — heal self
        {
            if (self->busy > 0)
                return false;

            float heal_amount = 20.0f + static_cast<float>(self->stats()->level) * 15.0f;
            self->stats()->hitpoints += heal_amount;
            if (self->stats()->hitpoints > self->stats()->max_hitpoints)
                self->stats()->hitpoints = self->stats()->max_hitpoints;

            if (self->myguy)
                message = std::format("{} meditates...", self->myguy->name);
            else
                message = "Monk meditates...";
            og::sim::emit_notification(self->sim_events, message);
            og::sim::emit_sound(self->sim_events, SOUND_HEAL);
            self->busy += 8;
            break;
        }

        case 3: // EVASION / COUNTER STANCE
        {
            if (self->busy > 0)
                return false;

            if (!self->shifter_down) // Normal: phantom mode (attacks pass through)
            {
                std::int32_t duration = 20 + self->stats()->level * 10;
                self->invisibility_left = static_cast<short>(
                    self->invisibility_left + duration);
                if (self->myguy)
                    message = std::format("{} becomes elusive!", self->myguy->name);
                else
                    message = "Monk becomes elusive!";
            }
            else // Shifted: invulnerability (shorter duration)
            {
                std::int32_t duration = 15 + self->stats()->level * 8;
                self->invulnerable_left = static_cast<short>(
                    self->invulnerable_left + duration);
                if (self->myguy)
                    message = std::format("{} takes a counter stance!", self->myguy->name);
                else
                    message = "Monk takes a counter stance!";
            }
            og::sim::emit_notification(self->sim_events, message);
            self->busy += 5;
            break;
        }

        case 4: // WHIRLWIND KICK — AoE damage around self
        {
            if (self->busy > 0)
                return false;

            std::int32_t howmany;
            std::int32_t didhit = 0;
            std::list<walker*> foes = self->sim_level->find_foes_in_range(
                self->sim_level->oblist, 60, &howmany, self);
            if (howmany < 1)
                return false;

            float kick_damage = static_cast<float>(self->stats()->level + 1) * 12.0f;

            for (auto* ob : foes)
            {
                if (ob)
                {
                    self->do_combat_damage(self, ob, static_cast<short>(kick_damage));

                    // Knockback: shove enemy away from monk
                    float dx = static_cast<float>(ob->xpos - self->xpos);
                    float dy = static_cast<float>(ob->ypos - self->ypos);
                    float dist = (dx * dx + dy * dy);
                    if (dist > 0.01f)
                    {
                        dist = std::sqrt(dist);
                        float push = 16.0f;
                        ob->setxy(
                            ob->xpos + static_cast<short>((dx / dist) * push),
                            ob->ypos + static_cast<short>((dy / dist) * push));
                    }
                    didhit++;
                }
            }
            if (!didhit)
                return false;

            if (didhit == 1)
                message = "Monk kicks 1 enemy!";
            else
                message = std::format("Monk kicks {} enemies!", didhit);
            if (self->team_num == 0 || self->myguy)
                og::sim::emit_notification(self->sim_events, message);
            self->busy += 8;
            break;
        }

        default:
            return false;
    }
    return true;
}

// Block ranged projectiles — monk is melee only
static bool monk_on_fire_weapon(walker* /*self*/, walker* weapon)
{
    weapon->dead = 1;
    return false;
}

static const char* const monk_names[] = {"Shaolin", "Kenshin", "Zen", "Wushu", "Bodhi", "Ryu", "Zendo", "Fist"};

const FamilyDescriptor& describe_family_monk()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MONK,
        .name = "MONK",
        .short_name = nullptr,
        .base_stats = {10, 12, 10, 10, 6, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+70, 0, 16, 0, 0, 0, 5, 3.5f},
        .stat_costs = {10, 6, 10, 15, 50, 200},
        .special_cost = {5000, 25, 60, 80, 120, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "SILENCE", "MEDITATE", "EVASION", "WHIRLWIND KICK", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "COUNTER STANCE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "MONK FALLEN",
        .do_special = monk_do_special,
        .check_special_ai = monk_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = monk_set_difficulty,
        .level_up = monk_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = monk_on_fire_weapon,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = monk_on_melee_hit,
        .pix_filename = "monk.pix",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 5,
        .description = "Monks are melee-only      \n" "fighters whose fists stun \n" "and knock back on every   \n" "hit. Their Silence ability\n" "shuts down enemy specials.\n" "\n" "Special: Silence",
        .name_pool = monk_names,
        .name_pool_size = sizeof(monk_names) / sizeof(monk_names[0]),
        .is_playable = true,
        .playable_order = 6,
    };
    return desc;
}
