/*
 * Pure-ish combat math helpers extracted for unit testing.
 *
 * Keep this module free of `myscreen`/rendering/FX spawning. Randomness is
 * injected so tests can be deterministic.
 */
#pragma once

#include "SDL.h"

class IRandom;

using RandomU32 = Uint32(*)(Uint32);

// Original behavior from walker.cpp:
//   d - sqrt(d)/2 + random(floor(sqrt(d)))
float compute_base_damage(float base_damage, RandomU32 rng);

// Original behavior from walker.cpp:
//   reduction = armor/2, clamped to at most (damage - 1) so at least 1 gets through.
float compute_damage_reduction(float incoming_damage, float target_armor);

// Convenience helper.
float compute_post_reduction_damage(float incoming_damage, float target_armor);

// IRandom-based overload: allows injection via GameContext's RNG
float compute_base_damage(float base_damage, IRandom& rng);

// Compute faerie freeze duration on a target.
// constitution: target's constitution stat (0 if no myguy)
// level: attacker's level
// Returns: freeze delay in ticks (clamped to >= 0)
Sint32 compute_freeze_duration(Sint32 level, Sint32 constitution, IRandom& rng);

// Compute cleric heal amount for one target.
// magicpoints: caster's current MP
// level: caster's level
// Returns: {heal_amount, mp_cost}
struct HealResult {
    Sint32 amount;
    Sint32 cost;
};
HealResult compute_heal_amount(Sint32 magicpoints, Sint32 level, IRandom& rng);

// Compute charm duration.
// level_diff: caster_level - target_level (can be negative)
// Returns: charm duration in ticks
Sint32 compute_charm_duration(Sint32 level_diff, IRandom& rng);

// Regeneration tick result.
struct RegenTickResult {
    float new_value;        // new HP or MP
    Sint32 new_delay;       // updated current delay counter
};

// Compute one tick of regeneration (HP or MP).
// current: current HP/MP, max_val: cap, per_round: amount added each tick,
// current_delay: current delay counter, max_delay: delay threshold for bonus +1.
// frozen: if true, no regen occurs.
// Returns new value and delay counter.
RegenTickResult compute_regen_tick(float current, float max_val, float per_round,
                                   Sint32 current_delay, Sint32 max_delay, bool frozen);

// Compute HP regen considering regen_delay (post-damage cooldown).
// regen_delay: ticks remaining before regen starts (decremented each tick).
// Returns: updated regen_delay (caller should store it back).
struct HpRegenResult {
    float new_hp;
    Sint32 new_heal_delay;
    Sint32 new_regen_delay;
};
HpRegenResult compute_hp_regen_tick(float current_hp, float max_hp, float heal_per_round,
                                     Sint32 current_heal_delay, Sint32 max_heal_delay,
                                     Sint32 regen_delay, bool frozen);

// XP from dealing damage: quintic polynomial based on level difference.
// level_diff: attacker_level - target_level
// damage: damage dealt
// Returns: XP earned (clamped to >= 0)
short compute_xp_from_attack(Sint32 level_diff, float damage);

// XP from killing a target (equivalent to dealing 20 damage worth of XP).
short compute_xp_from_kill(Sint32 level_diff);

// XP action types for compute_xp_from_action.
enum class ExpAction {
    Attack, Kill, Heal, TurnUndead,
    RaiseSkeleton, RaiseGhost, Resurrect, ResurrectPenalty,
    Protection, EatCorpse
};

// Compute XP for any action type.
// attacker_level/target_level: the relevant entity levels
// value: action-specific value (damage for Attack, heal amount for Heal, etc.)
// rng: needed only for Heal action
short compute_xp_from_action(ExpAction action, Sint32 attacker_level, Sint32 target_level,
                             short value, IRandom& rng);

