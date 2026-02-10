#include "combat_math.h"
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
