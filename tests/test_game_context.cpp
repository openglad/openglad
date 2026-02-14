#include <openglad/runtime/game_context.h>
#include <openglad/core/combat_math.h>
#include <openglad/legacy/graph.h>
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
    TEST_ASSERT(!state.players[0].held[static_cast<int>(InputKey::Fire)],
                "default player fire should be false");
    TEST_ASSERT_EQ(0, state.players[0].move_x(),
                    "default player move_x should be 0");
    TEST_ASSERT_EQ(0, state.players[0].move_y(),
                    "default player move_y should be 0");
}
REGISTER_TEST(test_input_state_default);

void test_input_state_clear()
{
    InputState state;
    state.players[0].held[static_cast<int>(InputKey::Fire)] = true;
    state.players[1].pressed[static_cast<int>(InputKey::Special)] = true;
    state.quit_requested = true;

    state.clear();

    TEST_ASSERT(!state.players[0].held[static_cast<int>(InputKey::Fire)],
                "clear() should reset held keys");
    TEST_ASSERT(!state.players[1].pressed[static_cast<int>(InputKey::Special)],
                "clear() should reset pressed keys");
    TEST_ASSERT(!state.quit_requested, "clear() should reset quit_requested");
}
REGISTER_TEST(test_input_state_clear);

void test_player_input_move_directions()
{
    PlayerInput p = {};

    // Left only
    p.held[static_cast<int>(InputKey::Left)] = true;
    TEST_ASSERT_EQ(-1, p.move_x(), "Left key should give move_x=-1");
    TEST_ASSERT_EQ(0, p.move_y(), "Left key should give move_y=0");

    // Reset and test diagonal
    for (auto& h : p.held) h = false;
    p.held[static_cast<int>(InputKey::DownRight)] = true;
    TEST_ASSERT_EQ(1, p.move_x(), "DownRight should give move_x=1");
    TEST_ASSERT_EQ(1, p.move_y(), "DownRight should give move_y=1");

    // UpLeft
    for (auto& h : p.held) h = false;
    p.held[static_cast<int>(InputKey::UpLeft)] = true;
    TEST_ASSERT_EQ(-1, p.move_x(), "UpLeft should give move_x=-1");
    TEST_ASSERT_EQ(-1, p.move_y(), "UpLeft should give move_y=-1");

    // Opposing directions cancel
    for (auto& h : p.held) h = false;
    p.held[static_cast<int>(InputKey::Left)] = true;
    p.held[static_cast<int>(InputKey::Right)] = true;
    TEST_ASSERT_EQ(0, p.move_x(), "Left+Right should cancel to move_x=0");
}
REGISTER_TEST(test_player_input_move_directions);

void test_input_state_from_sdl_captures_held()
{
    // This test verifies input_state_from_sdl() populates from the
    // actual SDL keyboard state. Since no keys are pressed in the test
    // environment, all should be false.
    InputState state;
    input_state_from_sdl(state);

    for (int p = 0; p < MAX_PLAYERS; p++) {
        for (int k = 0; k < NUM_INPUT_KEYS; k++) {
            TEST_ASSERT(!state.players[p].held[k],
                        "no keys should be held in test environment");
        }
    }
}
REGISTER_TEST(test_input_state_from_sdl_captures_held);

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

void test_deterministic_rng_via_game_context()
{
    // Demonstrate that injecting a SeededRandom into the GameContext
    // produces deterministic combat results across multiple runs
    SeededRandom rng1(99999);
    SeededRandom rng2(99999);

    GameContext test_ctx;
    test_ctx.game_screen = myscreen;
    test_ctx.rng = &rng1;
    set_global_context(&test_ctx);

    // Run several damage calculations
    float results1[5];
    for (int i = 0; i < 5; i++)
        results1[i] = compute_base_damage(20.0f, *ctx().rng);

    // Reset and replay with same seed
    test_ctx.rng = &rng2;
    float results2[5];
    for (int i = 0; i < 5; i++)
        results2[i] = compute_base_damage(20.0f, *ctx().rng);

    set_global_context(nullptr);

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ(static_cast<int>(results1[i] * 100),
                        static_cast<int>(results2[i] * 100),
                        "deterministic RNG should reproduce combat results");
    }
}
REGISTER_TEST(test_deterministic_rng_via_game_context);
