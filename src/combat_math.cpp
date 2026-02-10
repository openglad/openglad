#include "combat_math.h"
#include "game_context.h"

#include <cmath>

float compute_base_damage(float base_damage, RandomU32 rng)
{
    if (!rng)
        rng = [](Uint32 x) -> Uint32 { return (x == 0) ? 0u : 0u; };

    float d = base_damage;
    float sqrtd = sqrtf(d);
    // floor(sqrtd) from original implementation; random(0) should produce 0.
    return d - sqrtd / 2.0f + static_cast<float>(rng(static_cast<Uint32>(floorf(sqrtd))));
}

float compute_damage_reduction(float incoming_damage, float target_armor)
{
    if (incoming_damage <= 0)
        return 0;

    float reduction = target_armor / 2.0f;
    if (reduction > incoming_damage - 1)
        return incoming_damage - 1; // Always do at least 1 damage
    return reduction;
}

float compute_post_reduction_damage(float incoming_damage, float target_armor)
{
    float result = incoming_damage - compute_damage_reduction(incoming_damage, target_armor);
    if (result < 0)
        return 0;
    return result;
}

float compute_base_damage(float base_damage, IRandom& rng)
{
    float d = base_damage;
    float sqrtd = sqrtf(d);
    return d - sqrtd / 2.0f + static_cast<float>(rng.next(static_cast<Uint32>(floorf(sqrtd))));
}

// FAERIE_FREEZE_TIME is 40 (from stats.h)
static constexpr Sint32 FREEZE_BASE_TIME = 40;

Sint32 compute_freeze_duration(Sint32 level, Sint32 constitution, IRandom& rng)
{
    Sint32 max_time;
    if (constitution > 0)
        max_time = FREEZE_BASE_TIME + (level * 2) - (constitution / 21);
    else
        max_time = FREEZE_BASE_TIME + (level * 2);

    if (max_time <= 0)
        return 0;

    Sint32 result = static_cast<Sint32>(rng.next(static_cast<Uint32>(max_time)));
    return (result < 0) ? 0 : result;
}

HealResult compute_heal_amount(Sint32 magicpoints, Sint32 level, IRandom& rng)
{
    Sint32 base = magicpoints / 4 + static_cast<Sint32>(rng.next(static_cast<Uint32>(magicpoints / 4)));
    Sint32 cost = base / 2;
    // Add bonus healing from level
    Sint32 amount = base + level * 5;
    return {amount, cost};
}

Sint32 compute_charm_duration(Sint32 level_diff, IRandom& rng)
{
    Sint32 generic = (level_diff > 0) ? level_diff : 0;
    return 25 + static_cast<Sint32>(rng.next(static_cast<Uint32>(generic * 20)));
}

short compute_xp_from_attack(Sint32 level_diff, float damage)
{
    float x = static_cast<float>(level_diff);
    float poly = -0.00246795f*powf(x,5) + 0.013243f*powf(x,4)
                 + 0.223208f*powf(x,3) - 1.16091f*powf(x,2)
                 - 5.54277f*x + 30.2923f;
    float result = 6.0f * damage * poly / 20.0f;
    if (result <= 0)
        return 0;
    return static_cast<short>(result);
}

short compute_xp_from_kill(Sint32 level_diff)
{
    return compute_xp_from_attack(level_diff, 20.0f);
}

short compute_xp_from_action(ExpAction action, Sint32 attacker_level, Sint32 target_level,
                             short value, IRandom& rng)
{
    Sint32 level_diff = attacker_level - target_level;
    switch (action) {
    case ExpAction::Attack:
        return compute_xp_from_attack(level_diff, static_cast<float>(value));
    case ExpAction::Kill:
        return compute_xp_from_kill(level_diff);
    case ExpAction::Heal:
        return static_cast<short>(rng.next(static_cast<Uint32>(20 * value)) / attacker_level);
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

