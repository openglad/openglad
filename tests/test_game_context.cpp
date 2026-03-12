#include <openglad/platform/game_context.h>
#include <openglad/core/combat_math.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// GameContext basic tests
// ---------------------------------------------------------------------------

TEST(GameContext, ctx_returns_valid_context)
{
    GameContext& c = ctx();
    ASSERT_TRUE(c.rng != nullptr) << "ctx().rng should be non-null (production RNG)";
}


TEST(GameContext, push_test_context_overrides_rng)
{
    FixedRandom fixed(42);
    GameContext test_ctx;
    test_ctx.rng = &fixed;

    push_test_context(&test_ctx);
    ASSERT_TRUE(ctx().rng == &fixed) << "push_test_context should override active RNG";

    // Restore
    pop_test_context();
    ASSERT_TRUE(ctx().rng != &fixed) << "pop_test_context should restore default context";
}


// ---------------------------------------------------------------------------
// IRandom implementations
// ---------------------------------------------------------------------------

TEST(GameContext, production_rng_stays_in_bounds)
{
    ProductionRandom rng;
    for (int i = 0; i < 100; i++) {
        Uint32 val = rng.next(10);
        ASSERT_TRUE(val < 10) << "ProductionRandom::next(10) should return [0,9]";
    }
    ASSERT_EQ(0, static_cast<int>(rng.next(0))) << "ProductionRandom::next(0) should return 0";
}


TEST(GameContext, fixed_rng_returns_value_mod_max)
{
    FixedRandom rng(7);
    ASSERT_EQ(7, static_cast<int>(rng.next(10))) << "FixedRandom(7).next(10) should return 7";
    ASSERT_EQ(2, static_cast<int>(rng.next(5))) << "FixedRandom(7).next(5) should return 7%5=2";
    ASSERT_EQ(0, static_cast<int>(rng.next(0))) << "FixedRandom.next(0) should return 0";
}


TEST(GameContext, seeded_rng_deterministic)
{
    SeededRandom rng1(12345);
    SeededRandom rng2(12345);

    // Two RNGs with same seed should produce identical sequences
    for (int i = 0; i < 50; i++) {
        Uint32 a = rng1.next(1000);
        Uint32 b = rng2.next(1000);
        ASSERT_EQ(static_cast<int>(a), static_cast<int>(b)) << "SeededRandom with same seed should produce identical values";
    }
}


TEST(GameContext, seeded_rng_different_seeds)
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
    ASSERT_TRUE(found_difference) << "Different seeds should produce different sequences";
}


TEST(GameContext, seeded_rng_reset)
{
    SeededRandom rng(42);
    Uint32 first = rng.next(100);
    rng.next(100); // advance
    rng.next(100);

    rng.state_ = 42;
    Uint32 after_reset = rng.next(100);
    ASSERT_EQ(static_cast<int>(first), static_cast<int>(after_reset)) << "reset(42) should reproduce the same first value";
}


// ---------------------------------------------------------------------------
// InputState tests
// ---------------------------------------------------------------------------

TEST(GameContext, input_state_default)
{
    InputState state;
    ASSERT_TRUE(!state.quit_requested) << "default InputState should not have quit_requested";
    ASSERT_TRUE(!state.players[0].held[static_cast<int>(InputKey::Fire)]) << "default player fire should be false";
    ASSERT_EQ(0, state.players[0].move_x()) << "default player move_x should be 0";
    ASSERT_EQ(0, state.players[0].move_y()) << "default player move_y should be 0";
}


TEST(GameContext, input_state_clear)
{
    InputState state;
    state.players[0].held[static_cast<int>(InputKey::Fire)] = true;
    state.players[1].pressed[static_cast<int>(InputKey::Special)] = true;
    state.quit_requested = true;

    state.clear();

    ASSERT_TRUE(!state.players[0].held[static_cast<int>(InputKey::Fire)]) << "clear() should reset held keys";
    ASSERT_TRUE(!state.players[1].pressed[static_cast<int>(InputKey::Special)]) << "clear() should reset pressed keys";
    ASSERT_TRUE(!state.quit_requested) << "clear() should reset quit_requested";
}


TEST(GameContext, player_input_move_directions)
{
    PlayerInput p = {};

    // Left only
    p.held[static_cast<int>(InputKey::Left)] = true;
    ASSERT_EQ(-1, p.move_x()) << "Left key should give move_x=-1";
    ASSERT_EQ(0, p.move_y()) << "Left key should give move_y=0";

    // Reset and test diagonal
    for (auto& h : p.held) h = false;
    p.held[static_cast<int>(InputKey::DownRight)] = true;
    ASSERT_EQ(1, p.move_x()) << "DownRight should give move_x=1";
    ASSERT_EQ(1, p.move_y()) << "DownRight should give move_y=1";

    // UpLeft
    for (auto& h : p.held) h = false;
    p.held[static_cast<int>(InputKey::UpLeft)] = true;
    ASSERT_EQ(-1, p.move_x()) << "UpLeft should give move_x=-1";
    ASSERT_EQ(-1, p.move_y()) << "UpLeft should give move_y=-1";

    // Opposing directions cancel
    for (auto& h : p.held) h = false;
    p.held[static_cast<int>(InputKey::Left)] = true;
    p.held[static_cast<int>(InputKey::Right)] = true;
    ASSERT_EQ(0, p.move_x()) << "Left+Right should cancel to move_x=0";
}


TEST(GameContext, input_state_from_sdl_captures_held)
{
    // This test verifies input_state_from_sdl() populates from the
    // actual SDL keyboard state. Since no keys are pressed in the test
    // environment, all should be false.
    InputState state;
    input_state_from_sdl(state);

    for (int p = 0; p < MAX_PLAYERS; p++) {
        for (int k = 0; k < NUM_INPUT_KEYS; k++) {
            ASSERT_TRUE(!state.players[p].held[k]) << "no keys should be held in test environment";
        }
    }
}


// ---------------------------------------------------------------------------
// IRandom-based combat math overload
// ---------------------------------------------------------------------------

TEST(GameContext, compute_base_damage_with_irandom)
{
    // FixedRandom(0) always returns 0 — should give d - sqrt(d)/2
    FixedRandom zero_rng(0);
    float d = compute_base_damage(9.0f, zero_rng);
    // 9 - 3/2 + 0 = 7.5
    ASSERT_TRUE((d > 7.49f && d < 7.51f)) << "compute_base_damage with IRandom(0) should match formula";

    // SeededRandom should give reproducible results
    SeededRandom rng1(42);
    SeededRandom rng2(42);
    float d1 = compute_base_damage(25.0f, rng1);
    float d2 = compute_base_damage(25.0f, rng2);
    ASSERT_EQ(static_cast<int>(d1 * 100), static_cast<int>(d2 * 100)) << "compute_base_damage with same seed should be deterministic";
}


TEST(GameContext, deterministic_rng_via_game_context)
{
    // Demonstrate that injecting a SeededRandom into the GameContext
    // produces deterministic combat results across multiple runs
    SeededRandom rng1(99999);
    SeededRandom rng2(99999);

    GameContext test_ctx;
    test_ctx.rng = &rng1;
    push_test_context(&test_ctx);

    // Run several damage calculations
    float results1[5];
    for (int i = 0; i < 5; i++)
        results1[i] = compute_base_damage(20.0f, *ctx().rng);

    // Reset and replay with same seed
    test_ctx.rng = &rng2;
    push_test_context(&test_ctx);
    float results2[5];
    for (int i = 0; i < 5; i++)
        results2[i] = compute_base_damage(20.0f, *ctx().rng);

    pop_test_context();

    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(static_cast<int>(results1[i] * 100), static_cast<int>(results2[i] * 100)) << "deterministic RNG should reproduce combat results";
    }
}

