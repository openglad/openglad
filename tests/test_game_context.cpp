#include "game_context.h"
#include "combat_math.h"
#include "graph.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// GameContext basic tests
// ---------------------------------------------------------------------------

void test_ctx_returns_valid_context()
{
    GameContext& c = ctx();
    TEST_ASSERT(c.valid(), "global context should be valid after screen init");
    TEST_ASSERT(c.game_screen == myscreen, "ctx().game_screen should equal myscreen");
    TEST_ASSERT(c.config != nullptr, "ctx().config should be non-null");
    TEST_ASSERT(c.rng != nullptr, "ctx().rng should be non-null (production RNG)");
}
REGISTER_TEST(test_ctx_returns_valid_context);

void test_set_global_context_overrides()
{
    FixedRandom fixed(42);
    GameContext test_ctx;
    test_ctx.game_screen = myscreen;
    test_ctx.rng = &fixed;

    set_global_context(&test_ctx);
    TEST_ASSERT(ctx().rng == &fixed, "set_global_context should override the active context");

    // Restore
    set_global_context(nullptr);
    TEST_ASSERT(ctx().rng != &fixed, "restoring nullptr should return to default context");
}
REGISTER_TEST(test_set_global_context_overrides);

// ---------------------------------------------------------------------------
// IRandom implementations
// ---------------------------------------------------------------------------

void test_production_rng_stays_in_bounds()
{
    ProductionRandom rng;
    for (int i = 0; i < 100; i++) {
        Uint32 val = rng.next(10);
        TEST_ASSERT(val < 10, "ProductionRandom::next(10) should return [0,9]");
    }
    TEST_ASSERT_EQ(0, static_cast<int>(rng.next(0)), "ProductionRandom::next(0) should return 0");
}
REGISTER_TEST(test_production_rng_stays_in_bounds);

void test_fixed_rng_returns_value_mod_max()
{
    FixedRandom rng(7);
    TEST_ASSERT_EQ(7, static_cast<int>(rng.next(10)), "FixedRandom(7).next(10) should return 7");
    TEST_ASSERT_EQ(2, static_cast<int>(rng.next(5)), "FixedRandom(7).next(5) should return 7%5=2");
    TEST_ASSERT_EQ(0, static_cast<int>(rng.next(0)), "FixedRandom.next(0) should return 0");
}
REGISTER_TEST(test_fixed_rng_returns_value_mod_max);

void test_seeded_rng_deterministic()
{
    SeededRandom rng1(12345);
    SeededRandom rng2(12345);

    // Two RNGs with same seed should produce identical sequences
    for (int i = 0; i < 50; i++) {
        Uint32 a = rng1.next(1000);
        Uint32 b = rng2.next(1000);
        TEST_ASSERT_EQ(static_cast<int>(a), static_cast<int>(b),
                        "SeededRandom with same seed should produce identical values");
    }
}
REGISTER_TEST(test_seeded_rng_deterministic);

void test_seeded_rng_different_seeds()
{
    SeededRandom rng1(11111);
    SeededRandom rng2(22222);

    // Different seeds should eventually produce different values
    bool found_difference = false;
    for (int i = 0; i < 20; i++) {
        if (rng1.next(1000) != rng2.next(1000)) {
            found_difference = true;
            break;
        }
    }
    TEST_ASSERT(found_difference, "Different seeds should produce different sequences");
}
REGISTER_TEST(test_seeded_rng_different_seeds);

void test_seeded_rng_reset()
{
    SeededRandom rng(42);
    Uint32 first = rng.next(100);
    rng.next(100); // advance
    rng.next(100);

    rng.reset(42);
    Uint32 after_reset = rng.next(100);
    TEST_ASSERT_EQ(static_cast<int>(first), static_cast<int>(after_reset),
                    "reset(42) should reproduce the same first value");
}
REGISTER_TEST(test_seeded_rng_reset);

// ---------------------------------------------------------------------------
// InputState tests
// ---------------------------------------------------------------------------

void test_input_state_default()
{
    InputState state;
    TEST_ASSERT(!state.quit_requested, "default InputState should not have quit_requested");
    TEST_ASSERT(!state.players[0].fire, "default player input should not have fire");
    TEST_ASSERT_EQ(0, static_cast<int>(state.players[0].move_x),
                    "default player move_x should be 0");
}
REGISTER_TEST(test_input_state_default);

// ---------------------------------------------------------------------------
// IRandom-based combat math overload
// ---------------------------------------------------------------------------

void test_compute_base_damage_with_irandom()
{
    // FixedRandom(0) always returns 0 — should give d - sqrt(d)/2
    FixedRandom zero_rng(0);
    float d = compute_base_damage(9.0f, zero_rng);
    // 9 - 3/2 + 0 = 7.5
    TEST_ASSERT((d > 7.49f && d < 7.51f),
                "compute_base_damage with IRandom(0) should match formula");

    // SeededRandom should give reproducible results
    SeededRandom rng1(42);
    SeededRandom rng2(42);
    float d1 = compute_base_damage(25.0f, rng1);
    float d2 = compute_base_damage(25.0f, rng2);
    TEST_ASSERT_EQ(static_cast<int>(d1 * 100), static_cast<int>(d2 * 100),
                    "compute_base_damage with same seed should be deterministic");
}
REGISTER_TEST(test_compute_base_damage_with_irandom);
