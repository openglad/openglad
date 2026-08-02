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

// XP action types for compute_xp_from_action. The payload keeps the original
// gameplay meanings: Attack value is damage dealt, Heal value is hitpoints
// healed, and TurnUndead value is the number turned. The target is the new
// skeleton/ghost, revived character, protected friend, or eaten remains for
// the corresponding action; target_level matters to attack/kill,
// ResurrectPenalty, and EatCorpse.
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

namespace og::combat {

// ---------------------------------------------------------------------------
// Runaway-specials soft caps (docs/runaway-effects-design.md).
//
// Integer soft-knee asymptote — the single curve primitive.
//
// Closed form:
//   raw <= knee:  soften(raw) = raw                          (bit-exact legacy)
//   raw >  knee:  soften(raw) = knee + round_half_up(g*G / (g + G))
//                 where g = raw - knee and G = ceiling - knee.
//
// Properties (unit-pinned in tests/unit/test_combat_softcap.cpp):
//   * identity at and below the knee (parity/golden byte-identity);
//   * slope ~1 just above the knee: soften(knee+1) == knee+1;
//   * non-decreasing in raw;
//   * bounded by ceiling; the asymptote is reached only at absurd inputs
//     (g >= 2*G*G - G).
//
// Integer mul/div only — no float log/sqrt/pow in sim formulas (determinism
// commandment: not correctly-rounded across GCC15/CI-GCC/wasm).
//
// Designer note: raising a ceiling can never break parity (only above-knee
// values move); half of the remaining headroom is consumed roughly G/slope
// levels past the knee.
[[nodiscard]] inline constexpr int soften(int raw, int knee, int ceiling)
{
    if (raw <= knee)
        return raw;
    const long long g = raw - knee, G = ceiling - knee;
    return knee + static_cast<int>((g * G + (g + G) / 2) / (g + G));
}

// ---------------------------------------------------------------------------
// Named per-effect knees / ceilings / caps (spec §2 policy table).
// Knee construction rule: knee = the exact legacy formula value at the max
// golden-exercised level for that effect, so every below-knee input maps to
// itself bit-for-bit.

// 1. Mage freeze time (player branch): per-cast 20 + 11*L is UNCHANGED at all
//    levels; the world enemy_freeze bank is clamped post-cast. Golden casts:
//    L7 = 97, L12 = 152, L13 = 163 — all <= 300 so the clamp is the identity
//    map on every golden. Single-cast identity actually holds through L25
//    (295); cap = 24.5 s of pending time-stop.
inline constexpr int kEnemyFreezeBankCap = 300;

// 4. Orc yell stun accumulator (frozen_delay adds): per-cast add unchanged;
//    the victim's TOTAL from yells is bounded (150 ticks = 12.2 s). Orc
//    goldens are L1/L4 single casts on fresh victims (add << 150) → identity.
inline constexpr int kFrozenStunStackCap = 150;

// 8. Thief cloak accumulator (invisibility_left): per-cast gain
//    20 + rng(20)*L unchanged where it fits; TOTAL <= 350 (28.6 s). Golden
//    thief is L4 (max 96); the max single below-knee cast (L13 max = 267)
//    never clamps. Invisibility POTIONS share the field and are NOT capped —
//    the monotonic form never reduces a potion-granted 450.
inline constexpr int kInvisibilityCloakCap = 350;

// 6c. Thaw immunity: the player-side drain writes -kFreezeThawImmunityTicks
//     on the 1 → 0 transition, guaranteeing a controlled hero >= 12 actable
//     ticks per freeze cycle (negative frozen_delay = immunity phase).
inline constexpr int kFreezeThawImmunityTicks = 12;

// 6b. Sprinkle refresh gate: the roll is ALWAYS drawn (stream identical); the
//     SET is skipped when owner_level >= 21 && victim frozen_delay() > 10.
//     Level-gated >= 21 because the L20 weapon_sprinkle_emission golden's
//     soldier thaw schedule must stay byte-identical.
inline constexpr int kSprinkleRefreshOwnerLevel = 21;
inline constexpr int kSprinkleRefreshFloor = 10;

// 2. Ghost scare duration: raw = 25*L ticks. L5 golden = 125, L13 = 325
//    (knee); L20 = 500 → 364; ceiling 375 (30.6 s).
inline constexpr int kScareDurationKnee = 325;
inline constexpr int kScareDurationCeiling = 375;

// 3. Ghost scare radius: 50 + 10*L px, flat cap. Binds only at L21+ →
//    identity through L20, stronger than the L13 rule requires.
inline constexpr int kScareRadiusCap = 250;

// 7. Thief bomb damage: raw = 15*(L+1). L5 golden = 90, L13 = 210 (knee);
//    L20 = 315 → 258; L50 = 765 → 287. Ints <= 300 are exactly representable
//    in float (the call site casts) so below-knee bit-identity holds.
inline constexpr int kBombDamageKnee = 210;
inline constexpr int kBombDamageCeiling = 300;

// 5. Orc yell radius: 160 + 20*L px, flat cap; 420 = f(13) → identity <= L13.
inline constexpr int kYellRadiusCap = 420;

// 6a. Faerie sprinkle freeze roll (compute_freeze_duration): the rng draw
//     bound 40 + 2L - con/21 is untouched at ALL levels; the resulting ROLL
//     is softened post-draw. Knee 79: the L20 con-0 bound is 80, so every
//     roll 0..79 maps to itself — the L20 weapon_sprinkle_emission golden
//     cannot observe the curve at any roll. Max rolls: L30 99 → 91,
//     L50 139 → 99.
inline constexpr int kSprinkleRollKnee = 79;
inline constexpr int kSprinkleRollCeiling = 110;

// 9. Archmage charm (compute_charm_duration): draw 25 + rng(20*diff)
//    unchanged; the result is softened. Knee 264 = exact max result at
//    diff 12 (caster L13 vs L1) → the blanket L<=13 rule holds even in the
//    distribution tail. Golden is diff 9 (max 204) → identity.
//    Max by diff: d19 404 → 317; d29 604 → 333; d49 1004 → 341.
inline constexpr int kCharmKnee = 264;
inline constexpr int kCharmCeiling = 350;

// 9. Thief charm (draw-free 75 + 25*diff at the cast site): knee 375 forced
//    by diff-12 identity. d14 425 → 410; d19 550 → 444; d49 1300 → 477.
//    (Thief charm being stronger per diff than archmage is legacy design.)
inline constexpr int kThiefCharmKnee = 375;
inline constexpr int kThiefCharmCeiling = 490;

// 10. Cleric glow lifetime bonus: 110*L, FLAT cap — NOT a knee-13 soften.
//     The L20 weapon_glow_emission golden serializes weapon lifetime per
//     tick and 110*20 == 2200 == cap exactly: knee-20 identity is FORCED.
//     Above-L20 glow growth is pure MAXOBS pressure with no gameplay reward.
inline constexpr int kGlowBonusCap = 2200;

// 11. Summon lifetimes in ticks (counts NOT capped in v1 — MAXOBS backstop).
//     Every knee = f(13) of the legacy linear formula.
// Druid faerie 50 + 40*L: goldens L1 (=90) and L4 (=210). L20 850 → 696.
inline constexpr int kFaerieLifeKnee = 570;
inline constexpr int kFaerieLifeCeiling = 800;
// Archmage elemental 200 + 60*L: L20 1400 → 1177; L50 3200 → 1297.
inline constexpr int kElementalLifeKnee = 980;
inline constexpr int kElementalLifeCeiling = 1350;
// Archmage image 100 + 20*L: golden L7 illusion (=240). L50 1100 → 492.
inline constexpr int kImageLifeKnee = 360;
inline constexpr int kImageLifeCeiling = 520;
// Cleric skeleton 125 + 40*L: goldens L4/L7. L20 925 → 778; L50 2125 → 863.
inline constexpr int kSkeletonLifeKnee = 645;
inline constexpr int kSkeletonLifeCeiling = 900;
// Cleric raise-ghost 150 + 40*L: L20 950 → 803; L50 2150 → 888.
inline constexpr int kGhostRaiseLifeKnee = 670;
inline constexpr int kGhostRaiseLifeCeiling = 925;

// 12. MP-pool damage caps (draw-free min() wraps at the cast sites; covered
//     spawn-MP values sit far below every bind point; see design §2.12).
// Heartburst pool / chain-lightning initial bolt (MP-cost)/2: binds only
// above ~1280-1300 MP.
inline constexpr int kMpPoolDamageCap = 600;
// Mage starburst per-fireball add (MP-60)/15: binds above 660 MP; the
// derived lineofsight += add/3 inherits the bound.
inline constexpr int kStarburstAddCap = 40;
// Cleric mystic mace lifetime 100 + (MP-2)/2: binds above 738 MP.
inline constexpr int kMaceLifeCap = 468;
// Archmage per-shot MP/20 drain bonus: binds above 1000 MP.
inline constexpr int kShotDrainCap = 50;

// ---------------------------------------------------------------------------
// Named per-effect wrappers — call sites read as policy-by-name.

// Ghost scare forced-flee duration in ticks. Legacy: 25*L.
[[nodiscard]] inline constexpr int scare_duration(int level)
{
    return soften(25 * level, kScareDurationKnee, kScareDurationCeiling);
}

// Ghost scare radius in px. Legacy: 50 + 10*L.
[[nodiscard]] inline constexpr int scare_radius(int level)
{
    const int raw = 50 + 10 * level;
    return raw < kScareRadiusCap ? raw : kScareRadiusCap;
}

// Thief bomb damage. Legacy: 15*(L+1). Call site casts to float; every
// output <= 300 is exactly representable.
[[nodiscard]] inline constexpr int bomb_damage(int level)
{
    return soften(15 * (level + 1), kBombDamageKnee, kBombDamageCeiling);
}

// Orc yell radius in px. Legacy: 160 + 20*L.
[[nodiscard]] inline constexpr int yell_radius(int level)
{
    const int raw = 160 + 20 * level;
    return raw < kYellRadiusCap ? raw : kYellRadiusCap;
}

// Thief cloak accumulator: new = max(cur, min(cur + gain, cap)).
// Monotonic — never reduces an existing value (a potion-granted 450 stays
// 450; legacy saves/snapshots above the cap are never sanitized).
[[nodiscard]] inline constexpr int cloak_total(int cur, int gain)
{
    const int summed = cur + gain;
    const int capped = summed < kInvisibilityCloakCap ? summed : kInvisibilityCloakCap;
    return capped > cur ? capped : cur;
}

// Orc yell stun accumulator over raw frozen_delay. cur_raw < 0 is the
// thaw-immunity phase: the add is DISCARDED and the raw value returned
// unchanged. Otherwise monotonic cap; negative adds are treated as 0.
[[nodiscard]] inline constexpr int stun_total(int cur_raw, int add)
{
    if (cur_raw < 0)
        return cur_raw;
    const int gain = add > 0 ? add : 0;
    const int summed = cur_raw + gain;
    const int capped = summed < kFrozenStunStackCap ? summed : kFrozenStunStackCap;
    return capped > cur_raw ? capped : cur_raw;
}

// Summon lifetimes in ticks.
// Druid summoned faerie. Legacy: 50 + 40*L.
[[nodiscard]] inline constexpr int druid_faerie_lifetime(int level)
{
    return soften(50 + 40 * level, kFaerieLifeKnee, kFaerieLifeCeiling);
}

// Archmage fire elemental. Legacy: 200 + 60*L.
[[nodiscard]] inline constexpr int elemental_lifetime(int level)
{
    return soften(200 + 60 * level, kElementalLifeKnee, kElementalLifeCeiling);
}

// Archmage summoned image/illusion. Legacy: 100 + 20*L.
[[nodiscard]] inline constexpr int image_lifetime(int level)
{
    return soften(100 + 20 * level, kImageLifeKnee, kImageLifeCeiling);
}

// Cleric raised skeleton. Legacy: 125 + 40*L.
[[nodiscard]] inline constexpr int skeleton_lifetime(int level)
{
    return soften(125 + 40 * level, kSkeletonLifeKnee, kSkeletonLifeCeiling);
}

// Cleric raised ghost. Legacy: 150 + 40*L.
[[nodiscard]] inline constexpr int ghost_raise_lifetime(int level)
{
    return soften(150 + 40 * level, kGhostRaiseLifeKnee, kGhostRaiseLifeCeiling);
}

// Cleric glow projectile lifetime bonus. Legacy: 110*L (flat cap; see
// kGlowBonusCap for why this is NOT a knee-13 soften).
[[nodiscard]] inline constexpr int glow_bonus(int level)
{
    const int raw = 110 * level;
    return raw < kGlowBonusCap ? raw : kGlowBonusCap;
}

} // namespace og::combat
