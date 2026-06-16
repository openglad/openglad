/*
 * Pure-ish combat math helpers extracted for unit testing.
 *
 * Keep this module free of `og::runtime::current_session->myscreen_`/rendering/FX spawning. Randomness is
 * injected so tests can be deterministic.
 */
#pragma once

#include <cstdint>

class IRandom;

using RandomU32 = std::uint32_t(*)(std::uint32_t);

// Original behavior from walker.cpp:
//   d - sqrt(d)/2 + random(floor(sqrt(d)))
[[nodiscard]] float compute_base_damage(float base_damage, RandomU32 rng);

// Original behavior from walker.cpp:
//   reduction = armor/2, clamped to at most (damage - 1) so at least 1 gets through.
[[nodiscard]] float compute_damage_reduction(float incoming_damage, float target_armor);

// Convenience helper.
[[nodiscard]] float compute_post_reduction_damage(float incoming_damage, float target_armor);

// IRandom-based overload: allows injection via GameContext's RNG
[[nodiscard]] float compute_base_damage(float base_damage, IRandom& rng);

// Compute faerie freeze duration on a target.
// constitution: target's constitution stat (0 if no myguy)
// level: attacker's level
// Returns: freeze delay in ticks (clamped to >= 0)
[[nodiscard]] std::int32_t compute_freeze_duration(std::int32_t level, std::int32_t constitution, IRandom& rng);

// Compute cleric heal amount for one target.
// magicpoints: caster's current MP
// level: caster's level
// Returns: {heal_amount, mp_cost}
struct HealResult {
    std::int32_t amount;
    std::int32_t cost;
};
[[nodiscard]] HealResult compute_heal_amount(std::int32_t magicpoints, std::int32_t level, IRandom& rng);

// Compute charm duration.
// level_diff: caster_level - target_level (can be negative)
// Returns: charm duration in ticks
[[nodiscard]] std::int32_t compute_charm_duration(std::int32_t level_diff, IRandom& rng);

// Regeneration tick result.
struct RegenTickResult {
    float new_value;        // new HP or MP
    std::int32_t new_delay; // updated current delay counter
};

// Compute one tick of regeneration (HP or MP).
// current: current HP/MP, max_val: cap, per_round: amount added each tick,
// current_delay: current delay counter, max_delay: delay threshold for bonus +1.
// frozen: if true, no regen occurs.
// Returns new value and delay counter.
[[nodiscard]] RegenTickResult compute_regen_tick(float current, float max_val, float per_round,
                                   std::int32_t current_delay, std::int32_t max_delay, bool frozen);

// Compute HP regen considering regen_delay (post-damage cooldown).
// regen_delay: ticks remaining before regen starts (decremented each tick).
// Returns: updated regen_delay (caller should store it back).
struct HpRegenResult {
    float new_hp;
    std::int32_t new_heal_delay;
    std::int32_t new_regen_delay;
};
[[nodiscard]] HpRegenResult compute_hp_regen_tick(float current_hp, float max_hp, float heal_per_round,
                                     std::int32_t current_heal_delay, std::int32_t max_heal_delay,
                                     std::int32_t regen_delay, bool frozen);

// XP from dealing damage: quintic polynomial based on level difference.
// level_diff: attacker_level - target_level
// damage: damage dealt
// Returns: XP earned (clamped to >= 0)
[[nodiscard]] short compute_xp_from_attack(std::int32_t level_diff, float damage);

// XP from killing a target (equivalent to dealing 20 damage worth of XP).
[[nodiscard]] short compute_xp_from_kill(std::int32_t level_diff);

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
[[nodiscard]] short compute_xp_from_action(ExpAction action, std::int32_t attacker_level, std::int32_t target_level,
                             short value, IRandom& rng);

