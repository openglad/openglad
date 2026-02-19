#include <openglad/core/combat_math.h>
#include <openglad/runtime/game_context.h>
#include "test_framework.h"
#include <cstdint>

static std::uint32_t rng_return(std::uint32_t x)
{
    // Deterministic stub: pretend random(x) returned (x ? x-1 : 0).
    return (x == 0) ? 0u : (x - 1);
}

void test_compute_damage_reduction()
{
    TEST_ASSERT_EQ(0, (int)compute_damage_reduction(0.0f, 100.0f), "no damage -> no reduction");
    TEST_ASSERT_EQ(0, (int)compute_damage_reduction(-5.0f, 100.0f), "negative damage -> no reduction");

    // armor/2
    TEST_ASSERT_EQ(3, (int)compute_damage_reduction(10.0f, 6.0f), "armor should reduce damage by armor/2");

    // Clamp so at least 1 damage gets through.
    TEST_ASSERT_EQ(9, (int)compute_damage_reduction(10.0f, 1000.0f), "reduction should clamp to damage-1");
}
REGISTER_TEST(test_compute_damage_reduction);

void test_compute_base_damage_deterministic()
{
    // base_damage = 9 -> sqrt = 3 -> floor = 3 -> rng returns 2.
    // expected = 9 - 1.5 + 2 = 9.5
    float d = compute_base_damage(9.0f, rng_return);
    TEST_ASSERT((d > 9.49f && d < 9.51f), "base damage should match extracted formula (deterministic RNG)");

    // base_damage = 0 -> sqrt = 0 -> rng(0)=0 -> expected 0.
    float d0 = compute_base_damage(0.0f, rng_return);
    TEST_ASSERT((d0 > -0.001f && d0 < 0.001f), "base damage should handle 0");
}
REGISTER_TEST(test_compute_base_damage_deterministic);

void test_compute_base_damage_null_rng_and_irandom_overload()
{
    RandomU32 null_rng = nullptr;
    float d = compute_base_damage(9.0f, null_rng);
    TEST_ASSERT((d > 7.49f && d < 7.51f), "null RandomU32 should fall back to deterministic zero RNG");

    FixedRandom fixed(2);
    float d_irandom = compute_base_damage(9.0f, fixed);
    TEST_ASSERT((d_irandom > 9.49f && d_irandom < 9.51f), "IRandom overload should use rng.next()");
}
REGISTER_TEST(test_compute_base_damage_null_rng_and_irandom_overload);

void test_compute_post_reduction_damage_clamps()
{
    TEST_ASSERT_EQ(0, (int)compute_post_reduction_damage(-1.0f, 0.0f), "negative incoming damage clamps to 0");
    // At least 1 damage should get through when incoming_damage > 0.
    TEST_ASSERT_EQ(1, (int)compute_post_reduction_damage(1.0f, 1000.0f), "at least 1 damage should remain for positive incoming damage");
    TEST_ASSERT_EQ(1, (int)compute_post_reduction_damage(10.0f, 18.0f), "at least 1 damage should remain");
}
REGISTER_TEST(test_compute_post_reduction_damage_clamps);

// ---------------------------------------------------------------------------
// compute_freeze_duration tests
// ---------------------------------------------------------------------------

void test_freeze_duration_basic()
{
    FixedRandom fixed(10);
    // level=5, constitution=0 -> max_time = 40 + 10 = 50 -> rng.next(50) = 10
    std::int32_t result = compute_freeze_duration(5, 0, fixed);
    TEST_ASSERT_EQ(10, (int)result, "freeze duration with no constitution");
}
REGISTER_TEST(test_freeze_duration_basic);

void test_freeze_duration_with_constitution()
{
    FixedRandom fixed(10);
    // level=5, constitution=42 -> max_time = 40 + 10 - 2 = 48 -> rng.next(48) = 10
    std::int32_t result = compute_freeze_duration(5, 42, fixed);
    TEST_ASSERT_EQ(10, (int)result, "freeze duration with constitution");
}
REGISTER_TEST(test_freeze_duration_with_constitution);

void test_freeze_duration_high_constitution_clamps()
{
    FixedRandom fixed(999);
    // level=1, constitution=2100 -> max_time = 40 + 2 - 100 = -58 -> clamps to 0
    std::int32_t result = compute_freeze_duration(1, 2100, fixed);
    TEST_ASSERT_EQ(0, (int)result, "freeze duration clamps to 0 when constitution overwhelms");
}
REGISTER_TEST(test_freeze_duration_high_constitution_clamps);

void test_freeze_duration_zero_level()
{
    FixedRandom fixed(5);
    // level=0, constitution=0 -> max_time = 40 -> rng.next(40) = 5
    std::int32_t result = compute_freeze_duration(0, 0, fixed);
    TEST_ASSERT_EQ(5, (int)result, "freeze duration at level 0");
}
REGISTER_TEST(test_freeze_duration_zero_level);

// ---------------------------------------------------------------------------
// compute_heal_amount tests
// ---------------------------------------------------------------------------

void test_heal_amount_basic()
{
    FixedRandom fixed(0);
    // magicpoints=100, level=1 -> base = 25 + rng(25) = 25 + 0 = 25
    // cost = 25/2 = 12, amount = 25 + 5 = 30
    HealResult r = compute_heal_amount(100, 1, fixed);
    TEST_ASSERT_EQ(30, (int)r.amount, "heal amount at level 1 with 100 MP (rng=0)");
    TEST_ASSERT_EQ(12, (int)r.cost, "heal cost at level 1 with 100 MP (rng=0)");
}
REGISTER_TEST(test_heal_amount_basic);

void test_heal_amount_with_rng()
{
    FixedRandom fixed(10);
    // magicpoints=100, level=2 -> base = 25 + rng(25) = 25 + 10 = 35
    // cost = 35/2 = 17, amount = 35 + 10 = 45
    HealResult r = compute_heal_amount(100, 2, fixed);
    TEST_ASSERT_EQ(45, (int)r.amount, "heal amount at level 2 with 100 MP (rng=10)");
    TEST_ASSERT_EQ(17, (int)r.cost, "heal cost at level 2 with 100 MP (rng=10)");
}
REGISTER_TEST(test_heal_amount_with_rng);

void test_heal_amount_low_mp()
{
    FixedRandom fixed(0);
    // magicpoints=4, level=3 -> base = 1 + rng(1) = 1 + 0 = 1
    // cost = 1/2 = 0, amount = 1 + 15 = 16
    HealResult r = compute_heal_amount(4, 3, fixed);
    TEST_ASSERT_EQ(16, (int)r.amount, "heal amount low MP");
    TEST_ASSERT_EQ(0, (int)r.cost, "heal cost low MP");
}
REGISTER_TEST(test_heal_amount_low_mp);

void test_heal_amount_zero_mp()
{
    FixedRandom fixed(0);
    // magicpoints=0, level=5 -> base = 0 + rng(0) = 0
    // cost = 0, amount = 0 + 25 = 25
    HealResult r = compute_heal_amount(0, 5, fixed);
    TEST_ASSERT_EQ(25, (int)r.amount, "heal amount zero MP gets level bonus only");
    TEST_ASSERT_EQ(0, (int)r.cost, "heal cost zero MP");
}
REGISTER_TEST(test_heal_amount_zero_mp);

// ---------------------------------------------------------------------------
// compute_charm_duration tests
// ---------------------------------------------------------------------------

void test_charm_duration_positive_diff()
{
    FixedRandom fixed(10);
    // level_diff=3 -> generic = 3 -> rng(60) = 10 -> result = 25 + 10 = 35
    std::int32_t result = compute_charm_duration(3, fixed);
    TEST_ASSERT_EQ(35, (int)result, "charm duration with positive level diff");
}
REGISTER_TEST(test_charm_duration_positive_diff);

void test_charm_duration_zero_diff()
{
    FixedRandom fixed(99);
    // level_diff=0 -> generic = 0 -> rng(0) = 0 -> result = 25
    std::int32_t result = compute_charm_duration(0, fixed);
    TEST_ASSERT_EQ(25, (int)result, "charm duration with zero level diff");
}
REGISTER_TEST(test_charm_duration_zero_diff);

void test_charm_duration_negative_diff()
{
    FixedRandom fixed(99);
    // level_diff=-5 -> generic = 0 (clamped) -> rng(0) = 0 -> result = 25
    std::int32_t result = compute_charm_duration(-5, fixed);
    TEST_ASSERT_EQ(25, (int)result, "charm duration with negative level diff clamps to base");
}
REGISTER_TEST(test_charm_duration_negative_diff);

// ---------------------------------------------------------------------------
// compute_xp_from_attack tests
// ---------------------------------------------------------------------------

void test_xp_from_attack_same_level()
{
    // level_diff=0, damage=20 -> poly ≈ 30.2923 -> result = 6*20*30.2923/20 ≈ 181
    short xp = compute_xp_from_attack(0, 20.0f);
    TEST_ASSERT(xp > 170 && xp < 190, "XP at same level should be ~181");
}
REGISTER_TEST(test_xp_from_attack_same_level);

void test_xp_from_attack_higher_level_attacker()
{
    // level_diff=5 (attacker is 5 levels higher) -> less XP
    short xp = compute_xp_from_attack(5, 20.0f);
    TEST_ASSERT(xp >= 0 && xp < 30, "XP should be very low when attacker is much higher level");
}
REGISTER_TEST(test_xp_from_attack_higher_level_attacker);

void test_xp_from_attack_lower_level_attacker()
{
    // level_diff=-2 (attacker 2 levels below target) -> more XP
    short xp_below = compute_xp_from_attack(-2, 20.0f);
    short xp_same = compute_xp_from_attack(0, 20.0f);
    TEST_ASSERT(xp_below > xp_same, "XP should be higher when fighting above your level");
}
REGISTER_TEST(test_xp_from_attack_lower_level_attacker);

void test_xp_from_attack_scales_with_damage()
{
    short xp_low = compute_xp_from_attack(0, 5.0f);
    short xp_high = compute_xp_from_attack(0, 20.0f);
    TEST_ASSERT(xp_high > xp_low, "XP should scale with damage dealt");
}
REGISTER_TEST(test_xp_from_attack_scales_with_damage);

void test_xp_from_attack_zero_damage()
{
    short xp = compute_xp_from_attack(0, 0.0f);
    TEST_ASSERT_EQ(0, (int)xp, "zero damage should give zero XP");
}
REGISTER_TEST(test_xp_from_attack_zero_damage);

void test_xp_from_kill()
{
    // Kill XP equals attack XP for 20 damage
    short xp_kill = compute_xp_from_kill(0);
    short xp_attack = compute_xp_from_attack(0, 20.0f);
    TEST_ASSERT_EQ((int)xp_attack, (int)xp_kill, "kill XP should equal 20-damage attack XP");
}
REGISTER_TEST(test_xp_from_kill);

// ---------------------------------------------------------------------------
// compute_xp_from_action tests
// ---------------------------------------------------------------------------

void test_xp_from_action_attack()
{
    FixedRandom fixed(0);
    short xp = compute_xp_from_action(ExpAction::Attack, 5, 5, 20, fixed);
    short xp_direct = compute_xp_from_attack(0, 20.0f);
    TEST_ASSERT_EQ((int)xp_direct, (int)xp, "ExpAction::Attack should match compute_xp_from_attack");
}
REGISTER_TEST(test_xp_from_action_attack);

void test_xp_from_action_kill()
{
    FixedRandom fixed(0);
    short xp = compute_xp_from_action(ExpAction::Kill, 3, 1, 0, fixed);
    short xp_direct = compute_xp_from_kill(2);
    TEST_ASSERT_EQ((int)xp_direct, (int)xp, "ExpAction::Kill should match compute_xp_from_kill");
}
REGISTER_TEST(test_xp_from_action_kill);

void test_xp_from_action_heal()
{
    FixedRandom fixed(50);
    // value=10 (healed 10 hp), attacker_level=5
    // rng.next(200) = 50%200 = 50 -> 50/5 = 10
    short xp = compute_xp_from_action(ExpAction::Heal, 5, 1, 10, fixed);
    TEST_ASSERT_EQ(10, (int)xp, "Heal XP should be rng(20*value)/level");
}
REGISTER_TEST(test_xp_from_action_heal);

void test_xp_from_action_turn_undead()
{
    FixedRandom fixed(0);
    short xp = compute_xp_from_action(ExpAction::TurnUndead, 5, 3, 4, fixed);
    TEST_ASSERT_EQ(12, (int)xp, "TurnUndead XP should be value*3");
}
REGISTER_TEST(test_xp_from_action_turn_undead);

void test_xp_from_action_constants()
{
    FixedRandom fixed(0);
    TEST_ASSERT_EQ(45, (int)compute_xp_from_action(ExpAction::RaiseSkeleton, 1, 1, 0, fixed), "RaiseSkeleton XP");
    TEST_ASSERT_EQ(60, (int)compute_xp_from_action(ExpAction::RaiseGhost, 1, 1, 0, fixed), "RaiseGhost XP");
    TEST_ASSERT_EQ(90, (int)compute_xp_from_action(ExpAction::Resurrect, 1, 1, 0, fixed), "Resurrect XP");
}
REGISTER_TEST(test_xp_from_action_constants);

void test_xp_from_action_resurrect_penalty()
{
    FixedRandom fixed(0);
    // target_level=5 -> 5*5*100 = 2500
    short xp = compute_xp_from_action(ExpAction::ResurrectPenalty, 1, 5, 0, fixed);
    TEST_ASSERT_EQ(2500, (int)xp, "ResurrectPenalty should be target_level^2 * 100");
}
REGISTER_TEST(test_xp_from_action_resurrect_penalty);

void test_xp_from_action_protection()
{
    FixedRandom fixed(0);
    short xp = compute_xp_from_action(ExpAction::Protection, 7, 1, 0, fixed);
    TEST_ASSERT_EQ(7, (int)xp, "Protection XP should be attacker_level");
}
REGISTER_TEST(test_xp_from_action_protection);

void test_xp_from_action_eat_corpse()
{
    FixedRandom fixed(0);
    short xp = compute_xp_from_action(ExpAction::EatCorpse, 1, 4, 0, fixed);
    TEST_ASSERT_EQ(20, (int)xp, "EatCorpse XP should be target_level*5");
}
REGISTER_TEST(test_xp_from_action_eat_corpse);

void test_xp_from_action_unknown_enum_defaults_to_zero()
{
    FixedRandom fixed(7);
    const ExpAction unknown = static_cast<ExpAction>(999);
    short xp = compute_xp_from_action(unknown, 5, 2, 10, fixed);
    TEST_ASSERT_EQ(0, (int)xp, "unknown ExpAction should default to zero XP");
}
REGISTER_TEST(test_xp_from_action_unknown_enum_defaults_to_zero);

// ---------------------------------------------------------------------------
// compute_regen_tick tests
// ---------------------------------------------------------------------------

void test_regen_tick_basic()
{
    // current=50, max=100, per_round=0.5, delay=0/10, not frozen
    RegenTickResult r = compute_regen_tick(50.0f, 100.0f, 0.5f, 0, 10, false);
    TEST_ASSERT(r.new_value > 50.4f && r.new_value < 50.6f, "should add per_round");
    TEST_ASSERT_EQ(1, (int)r.new_delay, "delay should increment");
}
REGISTER_TEST(test_regen_tick_basic);

void test_regen_tick_delay_threshold()
{
    // At delay 9/10, threshold triggers bonus +1 and resets delay
    RegenTickResult r = compute_regen_tick(50.0f, 100.0f, 0.5f, 9, 10, false);
    // Should get per_round (0.5) + bonus (1.0) = 51.5
    TEST_ASSERT(r.new_value > 51.4f && r.new_value < 51.6f, "should add per_round + bonus at threshold");
    TEST_ASSERT_EQ(0, (int)r.new_delay, "delay should reset at threshold");
}
REGISTER_TEST(test_regen_tick_delay_threshold);

void test_regen_tick_caps_at_max()
{
    // current=99.8, max=100, per_round=0.5 -> should cap at 100
    RegenTickResult r = compute_regen_tick(99.8f, 100.0f, 0.5f, 0, 10, false);
    TEST_ASSERT(r.new_value > 99.9f && r.new_value < 100.1f, "should cap at max_val");
}
REGISTER_TEST(test_regen_tick_caps_at_max);

void test_regen_tick_frozen()
{
    // Frozen: no regen
    RegenTickResult r = compute_regen_tick(50.0f, 100.0f, 0.5f, 5, 10, true);
    TEST_ASSERT(r.new_value > 49.9f && r.new_value < 50.1f, "frozen should prevent regen");
    TEST_ASSERT_EQ(5, (int)r.new_delay, "delay should not change when frozen");
}
REGISTER_TEST(test_regen_tick_frozen);

void test_regen_tick_already_full()
{
    // At max: no regen
    RegenTickResult r = compute_regen_tick(100.0f, 100.0f, 0.5f, 5, 10, false);
    TEST_ASSERT(r.new_value > 99.9f && r.new_value < 100.1f, "full HP should not regen");
    TEST_ASSERT_EQ(5, (int)r.new_delay, "delay should not change when full");
}
REGISTER_TEST(test_regen_tick_already_full);

// ---------------------------------------------------------------------------
// compute_hp_regen_tick tests
// ---------------------------------------------------------------------------

void test_hp_regen_with_regen_delay()
{
    // regen_delay=5: should just decrement delay, no healing
    HpRegenResult r = compute_hp_regen_tick(50.0f, 100.0f, 0.5f, 0, 10, 5, false);
    TEST_ASSERT(r.new_hp > 49.9f && r.new_hp < 50.1f, "should not heal during regen delay");
    TEST_ASSERT_EQ(0, (int)r.new_heal_delay, "heal delay unchanged during regen delay");
    TEST_ASSERT_EQ(4, (int)r.new_regen_delay, "regen delay should decrement");
}
REGISTER_TEST(test_hp_regen_with_regen_delay);

void test_hp_regen_delay_expires()
{
    // regen_delay=1: last tick of delay, still no healing this tick
    HpRegenResult r = compute_hp_regen_tick(50.0f, 100.0f, 0.5f, 0, 10, 1, false);
    TEST_ASSERT(r.new_hp > 49.9f && r.new_hp < 50.1f, "no healing on last delay tick");
    TEST_ASSERT_EQ(0, (int)r.new_regen_delay, "regen delay should reach 0");
}
REGISTER_TEST(test_hp_regen_delay_expires);

void test_hp_regen_no_delay()
{
    // regen_delay=0: normal regen
    HpRegenResult r = compute_hp_regen_tick(50.0f, 100.0f, 0.5f, 0, 10, 0, false);
    TEST_ASSERT(r.new_hp > 50.4f && r.new_hp < 50.6f, "should heal normally when no delay");
    TEST_ASSERT_EQ(1, (int)r.new_heal_delay, "heal delay should increment");
    TEST_ASSERT_EQ(0, (int)r.new_regen_delay, "regen delay should remain 0");
}
REGISTER_TEST(test_hp_regen_no_delay);
