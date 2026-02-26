/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/weap.h>
#include <openglad/core/combat_math.h>
#include <openglad/core/stats.h>
#include <openglad/gameplay/guy.h>
#include <openglad/legacy/soundob.h>

// --- TREE and BLOOD: simple animation cycling ---

static bool tree_blood_on_animate(weap* self)
{
    if (self->ani_type > 1)
        self->ani_type = 0;
    self->set_frame(self->ani[self->curdir+self->ani_type*NUM_FACINGS][self->cycle]);
    self->cycle++;
    if (self->ani[self->curdir+self->ani_type*NUM_FACINGS][self->cycle] == -1)
    {
        self->ani_type = 0;
        self->cycle = 0;
    }
    return true;
}

// --- CIRCLE_PROTECTION: orbit owner ---

static bool circle_protection_on_animate(weap* self)
{
    if (!self->owner || self->owner->dead || self->stats()->hitpoints <= 0)
    {
        self->dead = 1;
        return false; // let default death handling proceed
    }
    self->center_on(self->owner);
    return true;
}

// --- GLOW: pulse animation with lifetime ---

static bool glow_on_animate(weap* self)
{
    if (self->ani_type > 2) // illegal case
        self->ani_type = 2; // pulse case
    self->set_frame(self->ani[self->curdir+self->ani_type*NUM_FACINGS][self->cycle]);
    self->cycle++;
    if (self->ani[self->curdir+self->ani_type*NUM_FACINGS][self->cycle] == -1)
    {
        self->ani_type = 2; // pulse
        self->cycle = 0;
    }
    if (self->lifetime-- < 1)
    {
        self->dead = 1;
        self->death();
    }
    return true;
}

// --- SPRINKLE: freeze foes on hit ---

static bool sprinkle_on_hit_target([[maybe_unused]] walker* weapon, walker* target, walker* owner)
{
    if (target->query_order() == Order::Living)
    {
        std::int32_t con = target->myguy ? target->myguy->constitution : 0;
        target->stats()->frozen_delay =
            static_cast<short>(compute_freeze_duration(owner->stats()->level, con, og::gameplay::current_game->world->rng_));
    }
    return true;
}

// --- Descriptor providers ---

const WeaponFamilyDescriptor& describe_weapon_animate_tree()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_TREE,
        .name = "TREE",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = true,
        .is_auto_attackable = true,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = tree_blood_on_animate,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_blood()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_BLOOD,
        .name = "BLOOD",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = true,
        .is_auto_attackable = false,
        .init_bit_flags = 0,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = tree_blood_on_animate,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_circle_protection()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_CIRCLE_PROTECTION,
        .name = "CIRCLE_PROTECTION",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = BIT_IMMORTAL | BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING,
        .init_lifetime = 0,
        .init_ani_type = 5,
        .on_death = nullptr,
        .on_animate = circle_protection_on_animate,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_glow()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_GLOW,
        .name = "GLOW",
        .fire_sound = SOUND_FWIP,
        .skip_sit_notify = false,
        .is_auto_attackable = true,
        .init_bit_flags = 0,
        .init_lifetime = 350,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = glow_on_animate,
        .on_hit_target = nullptr,
    };
    return desc;
}

const WeaponFamilyDescriptor& describe_weapon_animate_sprinkle()
{
    static const WeaponFamilyDescriptor desc = {
        .family_id = FAMILY_SPRINKLE,
        .name = "SPRINKLE",
        .fire_sound = SOUND_SPARKLE,
        .skip_sit_notify = false,
        .is_auto_attackable = false,
        .init_bit_flags = BIT_FLYING,
        .init_lifetime = 0,
        .init_ani_type = 0,
        .on_death = nullptr,
        .on_animate = nullptr,
        .on_hit_target = sprinkle_on_hit_target,
    };
    return desc;
}
