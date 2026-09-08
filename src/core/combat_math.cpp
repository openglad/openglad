/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/combat_math.h>
#include <openglad/core/irandom.h>
#include <cmath>

float compute_base_damage(float base_damage, RandomU32 rng)
{
    if (!rng)
        rng = [](std::uint32_t x) -> std::uint32_t { return (x == 0) ? 0u : 0u; };

    float d = base_damage;
    float sqrtd = sqrtf(d);
    // floor(sqrtd) from original implementation; random(0) should produce 0.
    return d - sqrtd / 2.0f + static_cast<float>(rng(static_cast<std::uint32_t>(floorf(sqrtd))));
}

float compute_damage_reduction(float incoming_damage, float target_armor)
{
    if (incoming_damage <= 0)
        return 0;

    // 2002 Gladiator: `tempdamage -= random(armor)`, a uniform roll on
    // 0..armor-1 (random(0) and random(1) both give 0), floored at zero
    // damage. The 2013 port replaced the roll with a flat armor/2 as "the
    // same average value" -- true only while armor/2 stays below damage-1;
    // past that its clamp, "// Always do at least 1 damage" in Jonathan
    // Dearborn's 2013 words, turned every high-armor target into a
    // one-point sponge (a level-8 thief on the
    // 2*level^2 curve took exactly 1 from a 45-damage blow instead of the
    // original's ~8). This is the exact expectation of the 2002 roll for
    // every armor value: with k = min(armor, ceil(damage)) roll values that
    // still leave damage, E[max(0, d - r)] = (k*d - k(k-1)/2) / armor.
    // The parity companion's get_damage_reduction mirrors these float
    // operations in the same order; keep them identical or the goldens move.
    const float a = floorf(target_armor);
    if (a < 1.0f)
        return 0;
    const float k = (ceilf(incoming_damage) < a) ? ceilf(incoming_damage) : a;
    const float expected = (k * incoming_damage - k * (k - 1.0f) / 2.0f) / a;
    return incoming_damage - expected;
}

float compute_post_reduction_damage(float incoming_damage, float target_armor)
{
    float result = incoming_damage - compute_damage_reduction(incoming_damage, target_armor);
    if (result < 0)
        return 0;
    return result;
}

short damage_to_hit_points(float damage)
{
    if (damage <= 0)
        return 0;
    return static_cast<short>(floorf(damage + 0.5f));
}

float compute_base_damage(float base_damage, IRandom& rng)
{
    float d = base_damage;
    float sqrtd = sqrtf(d);
    return d - sqrtd / 2.0f + static_cast<float>(rng.next(static_cast<std::uint32_t>(floorf(sqrtd))));
}

// Faerie's fire freezes foes :)
// FAERIE_FREEZE_TIME is 40 (from stats.h).
static constexpr std::int32_t FREEZE_BASE_TIME = 40;

std::int32_t compute_freeze_duration(std::int32_t level, std::int32_t constitution, IRandom& rng)
{
    std::int32_t max_time;
    if (constitution > 0)
        max_time = FREEZE_BASE_TIME + (level * 2) - (constitution / 21);
    else
        max_time = FREEZE_BASE_TIME + (level * 2);

    if (max_time <= 0)
        return 0;

    std::int32_t result = static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(max_time)));
    if (result < 0)
        result = 0;
    // Post-draw soft cap (runaway-specials spec §2.6a): the rng draw above
    // keeps its legacy bound at ALL levels — only the resulting roll is
    // softened. Every roll 0..79 (all an L20-or-below con-0 wielder can
    // produce) maps to itself, so below-knee behavior is bit-identical.
    return og::combat::soften(result, og::combat::kSprinkleRollKnee,
                              og::combat::kSprinkleRollCeiling);
}

HealResult compute_heal_amount(std::int32_t magicpoints, std::int32_t level, IRandom& rng)
{
    std::int32_t base = magicpoints / 4 + static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(magicpoints / 4)));
    // Get the cost first: the level bonus increases healing, not its price.
    std::int32_t cost = base / 2;
    // Add bonus healing from level
    std::int32_t amount = base + level * 5;
    return {amount, cost};
}

std::int32_t compute_charm_duration(std::int32_t level_diff, IRandom& rng)
{
    std::int32_t generic = (level_diff > 0) ? level_diff : 0;
    std::int32_t result = 25 + static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(generic * 20)));
    // Post-draw soft cap (runaway-specials spec §2.9): the draw bound 20*diff
    // is untouched. Knee 264 = the exact max result at diff 12 (caster L13 vs
    // L1), so caster level <= 13 is identity even in the distribution tail.
    return og::combat::soften(result, og::combat::kCharmKnee, og::combat::kCharmCeiling);
}

RegenTickResult compute_regen_tick(float current, float max_val, float per_round,
                                   std::int32_t current_delay, std::int32_t max_delay, bool frozen)
{
    if (frozen || current >= max_val)
        return {current, current_delay};

    float val = current + per_round;
    std::int32_t delay = current_delay + 1;
    if (delay >= max_delay) {
        val += 1.0f;
        delay = 0;
    }
    if (val > max_val)
        val = max_val;
    return {val, delay};
}

HpRegenResult compute_hp_regen_tick(float current_hp, float max_hp, float heal_per_round,
                                     std::int32_t current_heal_delay, std::int32_t max_heal_delay,
                                     std::int32_t regen_delay, bool frozen)
{
    if (regen_delay > 0)
        return {current_hp, current_heal_delay, regen_delay - 1};

    RegenTickResult r = compute_regen_tick(current_hp, max_hp, heal_per_round,
                                           current_heal_delay, max_heal_delay, frozen);
    return {r.new_value, r.new_delay, 0};
}

short compute_xp_from_attack(std::int32_t level_diff, float damage)
{
    // Whooo-ee! An interpolated (quintic) polynomial fitting
    // {{0,30},{1,25},{2,15},{3,10},{4,5},{5,2.5},{6,1.25},{7,0.5},
    //  {8,0.25},{9,-10}} for 20 damage done.
    // The odd order matters: it rises toward infinity to the left and falls
    // toward negative infinity to the right. The factor was tuned so level
    // ups happen at a good rate.
    float x = static_cast<float>(level_diff);
    float poly = -0.00246795f*powf(x,5) + 0.013243f*powf(x,4)
                 + 0.223208f*powf(x,3) - 1.16091f*powf(x,2)
                 - 5.54277f*x + 30.2923f;
    float result = 6.0f * damage * poly / 20.0f;
    if (result <= 0)
        return 0;
    // The odd order is deliberate (see above), so a big enough level gap runs
    // the fit off to several tens of thousands. Classic returned that straight
    // out of a `short` function, which is undefined behavior past 32767 and in
    // practice wrapped NEGATIVE -- and walker::attack casts this to uint32_t
    // before adding it, so a wrapped -20425 became +4294946871 exp. Saturating
    // keeps the intent ("killing far above your level is worth a lot") without
    // the wrap. Reachable at a level gap around -20.
    if (result > 32767.0f)
        return 32767;
    return static_cast<short>(result);
}

short compute_xp_from_kill(std::int32_t level_diff)
{
    return compute_xp_from_attack(level_diff, 20.0f);
}

short compute_xp_from_action(ExpAction action, std::int32_t attacker_level, std::int32_t target_level,
                             short value, IRandom& rng)
{
    std::int32_t level_diff = attacker_level - target_level;
    switch (action) {
    case ExpAction::Attack:
        return compute_xp_from_attack(level_diff, static_cast<float>(value));
    case ExpAction::Kill:
        return compute_xp_from_kill(level_diff);
    case ExpAction::Heal: {
        const std::int32_t denom = (attacker_level > 0) ? attacker_level : 1;
        return static_cast<short>(rng.next(static_cast<std::uint32_t>(20 * value)) / static_cast<std::uint32_t>(denom));
    }
    case ExpAction::TurnUndead:
        return static_cast<short>(value * 3);
    case ExpAction::RaiseSkeleton:
        return 45;
    case ExpAction::RaiseGhost:
        return 60;
    case ExpAction::Resurrect:
        return 90;
    case ExpAction::ResurrectPenalty:
        return static_cast<short>(target_level * target_level * 100);
    case ExpAction::Protection:
        return static_cast<short>(attacker_level);
    case ExpAction::EatCorpse:
        return static_cast<short>(target_level * 5);
    }
    return 0;
}
