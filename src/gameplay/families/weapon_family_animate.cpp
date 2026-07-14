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
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/test_trace.h>
// Advances `self`'s animation by one frame with full bounds checking. curdir,
// ani_type and cycle may originate from an untrusted snapshot, so the facing,
// the animation type (against this family's actual table length) and the cycle
// (against the sequence sentinel) are all bounded before indexing — otherwise a
// short animation table would be read out of bounds and a wild pointer
// dereferenced. Returns true if the advanced cycle reached the end sentinel.
static bool weapon_animate_step(weap* self)
{
    if (!self->ani)
        return true;
    int dir_index = static_cast<int>(static_cast<unsigned char>(self->curdir()));
    if (dir_index < 0 || dir_index >= NUM_FACINGS)
        dir_index = 0;
    int type_index = static_cast<int>(self->ani_type());
    if (type_index < 0)
        type_index = 0;
    // Callers (tree/blood, glow) already clamp ani_type to the small range their
    // table supports, and dir_index is bounded above, so dir+type*NUM_FACINGS stays
    // within the table. The real untrusted inputs here are curdir and cycle, which
    // are bounded by the facing clamp above and the sentinel scan below.
    const int ani_index = dir_index + type_index * NUM_FACINGS;
    // For real entities the loader records this family's table length (ani_count).
    // If a snapshot-driven ani_type drives ani_index past the end, treat it like
    // "no such sequence" (stop) instead of dereferencing a wild pointer. ani_count
    // is 0 only for test-constructed weapons that assign `ani` directly, which keep
    // the legacy direct-index behavior. Mirrors walker::animate/effect::animate.
    if (self->ani_count > 0 && ani_index >= self->ani_count) return true;
    const signed char* seq = self->ani[ani_index];
    if (!seq)
        return true;
    int seq_len = 0;
    while (seq_len < 128 && seq[seq_len] != -1)
        seq_len++;
    if (seq_len <= 0 || seq_len >= 128)
        return true;
    int c = static_cast<int>(self->cycle());
    if (c < 0 || c >= seq_len)
        c = 0;
    self->set_frame(seq[c]);
    c++;                                // advance into [1, seq_len]; seq[seq_len] is the -1 sentinel
    const bool ended = (seq[c] == -1);
    self->set_cycle(static_cast<signed char>(c));
    return ended;
}

// --- TREE and BLOOD: simple animation cycling ---

static bool tree_blood_on_animate(weap* self)
{
    if (self->ani_type() > 1)
        self->set_ani_type(0);
    if (weapon_animate_step(self))
    {
        self->set_ani_type(0);
        self->set_cycle(0);
    }
    return true;
}

// --- CIRCLE_PROTECTION: orbit owner ---

static bool circle_protection_on_animate(weap* self)
{
    if (!self->owner() || self->owner()->dead() || self->stats()->hitpoints() <= 0)
    {
        self->set_dead(1);
        return false; // let default death handling proceed
    }
    self->center_on(self->owner());
    return true;
}

// --- GLOW: pulse animation with lifetime ---

static bool glow_on_animate(weap* self)
{
    if (self->ani_type() > 2) // illegal case
        self->set_ani_type(2); // pulse case
    if (weapon_animate_step(self))
    {
        self->set_ani_type(2); // pulse
        self->set_cycle(0);
    }
    const auto lifetime = self->lifetime();
    self->set_lifetime(lifetime - 1);
    if (lifetime < 1)
    {
        self->set_dead(1);
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
        // Runaway-specials §2.6: the roll is ALWAYS drawn (RNG stream
        // identical to master at every level); only the SET below is gated.
        const auto roll = static_cast<short>(compute_freeze_duration(owner->stats()->level(), con, current_game->world->rng_));
        // §2.6b refresh gate: a high-level faerie may not re-SET a freeze
        // that is still running (mirrors the archmage charm_left<=10 gate).
        // Level-gated >= 21 because the L20 weapon_sprinkle_emission
        // golden's soldier thaw schedule must stay byte-identical.
        const bool refresh_gated =
            owner->stats()->level() >= og::combat::kSprinkleRefreshOwnerLevel
            && target->stats()->frozen_delay() > og::combat::kSprinkleRefreshFloor;
        // §3.3 thaw immunity: never re-freeze inside the negative immunity
        // phase (only the player-side drain writes it — golden-safe, since
        // the sprinkle golden's victim is an AI soldier).
        const bool thaw_immune = target->stats()->frozen_delay_raw() < 0;
        if (refresh_gated) { TRACE("combat", "sprinkle SET skipped: refresh gate (owner L%d, victim frozen %d)", owner->stats()->level(), target->stats()->frozen_delay()); }
        if (thaw_immune) { TRACE("combat", "sprinkle SET skipped: thaw immunity (raw %d)", target->stats()->frozen_delay_raw()); }
        if (!refresh_gated && !thaw_immune)
            target->stats()->set_frozen_delay(roll);
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
