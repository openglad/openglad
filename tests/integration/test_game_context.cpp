#include <openglad/interface/game_context.h>
#include <openglad/core/combat_math.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/walker.h>
#include <openglad/platform/soundob_sdl.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_data_hooks.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace og::runtime {
void install_sdl_context_services();
}

// ---------------------------------------------------------------------------
// GameContext basic tests
// ---------------------------------------------------------------------------

TEST(GameContext, sdl_service_install_is_state_preserving_compatibility_hook)
{
    GameContext& context = ctx();
    ASSERT_NE(nullptr, context.rng);
    ASSERT_NE(nullptr, og::runtime::current_session);

    GameContext* const context_address = &context;
    IRandom* const rng = context.rng;
    og::sim::SimEventLog* const sim_events = context.sim_events.get();
    const InputState input = context.input;
    og::runtime::SessionState* const session = og::runtime::current_session;
    screen* const active_screen = session->myscreen_;
    options* const prefs = session->theprefs_;
    loader* const entity_loader = sdl_entity_loader();
    const LevelDataHooks* const level_hooks = &sdl_level_data_hooks();

    og::runtime::install_sdl_context_services();

    EXPECT_EQ(context_address, &ctx());
    EXPECT_EQ(rng, context.rng);
    EXPECT_EQ(sim_events, context.sim_events.get());
    EXPECT_EQ(session, og::runtime::current_session);
    EXPECT_EQ(active_screen, session->myscreen_);
    EXPECT_EQ(prefs, session->theprefs_);
    EXPECT_EQ(entity_loader, sdl_entity_loader());
    EXPECT_EQ(level_hooks, &sdl_level_data_hooks());
    EXPECT_EQ(input.quit_requested, context.input.quit_requested);
    EXPECT_EQ(input.timer_wait_request, context.input.timer_wait_request);
    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            EXPECT_EQ(input.players[player].held[key],
                      context.input.players[player].held[key]);
            EXPECT_EQ(input.players[player].pressed[key],
                      context.input.players[player].pressed[key]);
        }
    }
}

TEST(GameContext, default_sdl_sound_initializes_loaded_audio)
{
    sdl_soundob sound;
    EXPECT_EQ(0, sound.silence);
    EXPECT_GT(sound.sound[SOUND_BOW].len, 0u);
    EXPECT_NE(nullptr, sound.sound[SOUND_BOW].buf);
}

TEST(GameContext, inprocess_mismatch_diagnostics_render_enum_and_signed_byte)
{
    const auto diagnostics =
        og::sim::inprocess_transport_validation_diagnostics_for_testing();

    EXPECT_NE(std::string::npos, diagnostics[0].find("diagnostic probe"));
    EXPECT_NE(std::string::npos, diagnostics[0].find("event.kind"));
    EXPECT_NE(std::string::npos, diagnostics[0].find("expected"));
    EXPECT_NE(std::string::npos, diagnostics[0].find("got"));

    EXPECT_NE(std::string::npos, diagnostics[1].find("signed_byte"));
    EXPECT_NE(std::string::npos, diagnostics[1].find("expected -2"));
    EXPECT_NE(std::string::npos, diagnostics[1].find("got 7"));

    const std::array<std::string_view, 14> remaining_fields{
        "boolean", "integer", "tick", "quit_requested",
        "timer_wait_request", "players[0].held[0]",
        "players[0].pressed[0]", "sequence", "events.size",
        "events[0].tick", "events[0].kind", "events[0].a",
        "events[0].b", "events[0].text",
    };
    for (std::size_t index = 0; index < remaining_fields.size(); ++index)
    {
        EXPECT_NE(std::string::npos,
                  diagnostics[index + 2].find(remaining_fields[index]))
            << "diagnostic " << index + 2;
        EXPECT_NE(std::string::npos,
                  diagnostics[index + 2].find("expected"))
            << "diagnostic " << index + 2;
        EXPECT_NE(std::string::npos, diagnostics[index + 2].find("got"))
            << "diagnostic " << index + 2;
    }
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

    rng.seed(42);
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

TEST(GameContext, pathfinding_state_supports_move_construction_and_assignment)
{
    GameWorld world(0u);
    sdl_level_data_hooks().wire_world_entity_services(&world, nullptr);
    world.clear();
    world.create_new_grid();
    ASSERT_NE(nullptr, world.myobmap);

    struct ScopedGameplayWorld
    {
        GameplayContext context{};
        GameplayContext* previous = current_game;

        explicit ScopedGameplayWorld(GameWorld& active_world)
        {
            context.world = &active_world;
            current_game = &context;
        }
        ~ScopedGameplayWorld() { current_game = previous; }
    } scoped_world(world);

    walker* const actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->set_sizex(GRID_SIZE - 1);
    actor->set_sizey(GRID_SIZE - 1);
    ASSERT_TRUE(actor->setxy(32, 32));
    ASSERT_TRUE(world.myobmap->remove(actor));

    const auto make_state = [](int x, int y) -> PathState {
        return reinterpret_cast<PathState>(static_cast<intptr_t>(
            ((y / GRID_SIZE) * MAP_WIDTH) + (x / GRID_SIZE)));
    };

    GameplayPathfindingState source;
    std::vector<void*> path;
    float total_cost = 0.0f;
    source.solve_for_point(actor, 64, 64, make_state(32, 32),
                           make_state(64, 64), path, total_cost);
    ASSERT_GE(path.size(), 2u);
    EXPECT_GT(total_cost, 0.0f);

    GameplayPathfindingState moved(std::move(source));
    path.assign(1, reinterpret_cast<void*>(1));
    total_cost = 99.0f;
    source.solve_for_point(actor, 64, 64, make_state(32, 32),
                           make_state(64, 64), path, total_cost);
    EXPECT_TRUE(path.empty());
    EXPECT_FLOAT_EQ(0.0f, total_cost);

    moved.solve_for_point(actor, 80, 32, make_state(32, 32),
                          make_state(80, 32), path, total_cost);
    ASSERT_GE(path.size(), 2u);
    EXPECT_GT(total_cost, 0.0f);

    GameplayPathfindingState assigned;
    assigned = std::move(moved);
    assigned.solve_for_point(actor, 32, 80, make_state(32, 32),
                             make_state(32, 80), path, total_cost);
    ASSERT_GE(path.size(), 2u);
    EXPECT_GT(total_cost, 0.0f);
}
