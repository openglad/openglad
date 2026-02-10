#include "combat_math.h"
#include "game_context.h"
#include "test_framework.h"

static Uint32 rng_return(Uint32 x)
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
    Sint32 result = compute_freeze_duration(5, 0, fixed);
    TEST_ASSERT_EQ(10, (int)result, "freeze duration with no constitution");
}
REGISTER_TEST(test_freeze_duration_basic);

void test_freeze_duration_with_constitution()
{
    FixedRandom fixed(10);
    // level=5, constitution=42 -> max_time = 40 + 10 - 2 = 48 -> rng.next(48) = 10
    Sint32 result = compute_freeze_duration(5, 42, fixed);
    TEST_ASSERT_EQ(10, (int)result, "freeze duration with constitution");
}
REGISTER_TEST(test_freeze_duration_with_constitution);

void test_freeze_duration_high_constitution_clamps()
{
    FixedRandom fixed(999);
    // level=1, constitution=2100 -> max_time = 40 + 2 - 100 = -58 -> clamps to 0
    Sint32 result = compute_freeze_duration(1, 2100, fixed);
    TEST_ASSERT_EQ(0, (int)result, "freeze duration clamps to 0 when constitution overwhelms");
}
REGISTER_TEST(test_freeze_duration_high_constitution_clamps);

void test_freeze_duration_zero_level()
{
    FixedRandom fixed(5);
    // level=0, constitution=0 -> max_time = 40 -> rng.next(40) = 5
    Sint32 result = compute_freeze_duration(0, 0, fixed);
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
    Sint32 result = compute_charm_duration(3, fixed);
    TEST_ASSERT_EQ(35, (int)result, "charm duration with positive level diff");
}
REGISTER_TEST(test_charm_duration_positive_diff);

void test_charm_duration_zero_diff()
{
    FixedRandom fixed(99);
    // level_diff=0 -> generic = 0 -> rng(0) = 0 -> result = 25
    Sint32 result = compute_charm_duration(0, fixed);
    TEST_ASSERT_EQ(25, (int)result, "charm duration with zero level diff");
}
REGISTER_TEST(test_charm_duration_zero_diff);

void test_charm_duration_negative_diff()
{
    FixedRandom fixed(99);
    // level_diff=-5 -> generic = 0 (clamped) -> rng(0) = 0 -> result = 25
    Sint32 result = compute_charm_duration(-5, fixed);
    TEST_ASSERT_EQ(25, (int)result, "charm duration with negative level diff clamps to base");
}
REGISTER_TEST(test_charm_duration_negative_diff);
