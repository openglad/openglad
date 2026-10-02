#include <openglad/interface/game_context.h>
#include <openglad/core/combat_math.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/walker.h>
#include <openglad/platform/soundob_sdl.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace og::runtime {
void install_sdl_context_services();
}

void popup_dialog(const char* title, const char* message);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();

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

// sound/bow.wav as shipped in the runtime assets.
static constexpr Uint32 kBowSampleBytes = 5270u;

TEST(GameContext, default_sdl_sound_initializes_loaded_audio)
{
    // The shipped sample is a fixed asset: pin its exact decoded length, so a
    // loader that silently truncates (or hands back a 1-byte stub) fails here.
    sdl_soundob sound;
    EXPECT_EQ(0, sound.silence);
    EXPECT_EQ(kBowSampleBytes, sound.sound[SOUND_BOW].len)
        << "the loaded bow sample keeps its full decoded length";
    EXPECT_NE(nullptr, sound.sound[SOUND_BOW].buf);
}

// The two tests below run the sound object in a death-test child because the
// defect they pin ended the whole process: every failure arm in sound.cpp used
// to exit(0). The child re-executes this binary ("threadsafe" style), so it
// starts from fresh statics.
#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif
namespace {
// The child leaves through integration_main's own exit idiom, not std::exit:
// the harness's statics are not exit-safe (std::exit in this child aborts in
// static teardown with "double free or corruption" on the ci-test preset),
// which is why main() itself ends in __gcov_dump + _exit. The explicit dump
// is what keeps the child's coverage -- the reason a bare _Exit is wrong.
// It also removes the child's own per-PID config dir (integration_main makes
// one per process) so a death-test child leaves nothing behind in /tmp.
[[noreturn]] void exit_child_with(int code)
{
    if (const char* dir = std::getenv("OPENGLAD_CONFIG_DIR"))
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    std::fflush(nullptr);
    _exit(code);
}

[[noreturn]] void exit_child(bool rule_held)
{
    exit_child_with(rule_held ? 42 : 43);
}
} // namespace

// Rule (sound.cpp init): a machine whose audio subsystem will not start runs
// the game SILENT -- silence == 1, no clip loaded -- instead of quitting at
// startup with status 0 and nothing on screen. Positive control:
// default_sdl_sound_initializes_loaded_audio above (a working driver loads
// bow.wav at full length with silence == 0).
TEST(GameContext, an_audio_driver_that_will_not_start_leaves_the_game_silent)
{
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    EXPECT_EXIT(
        {
            // The harness's global screen already holds the audio subsystem
            // on the dummy driver; release it through the product's own
            // shutdown so the next init is a real first open and reads the
            // override below (the environment's SDL_AUDIODRIVER=dummy would
            // outrank a normal-priority hint).
            auto* const global_sound = dynamic_cast<sdl_soundob*>(
                og::runtime::current_session->myscreen_->soundp.get());
            if (global_sound == nullptr)
                std::exit(44);
            global_sound->shutdown();
            SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "og-no-such-driver",
                                    SDL_HINT_OVERRIDE);
            sdl_soundob sound;
            exit_child(sound.silence == 1 &&
                       sound.sound[SOUND_BOW].buf == nullptr);
        },
        ::testing::ExitedWithCode(42), "");
}

// Rule (#336, sound.cpp init/shutdown): the audio subsystem reference init()
// takes is released by shutdown() whether or not a device was ever opened. A
// driver that starts but cannot open its device (the disk driver writing into
// a directory that does not exist) leaves the game silent AND leaves
// SDL_WasInit(SDL_INIT_AUDIO) at 0, and every set_sound off->on retry that
// fails the same way releases its own reference too. Positive control, in the
// same child: after the harness's global sound is shut down the subsystem
// reads 0 (nothing else holds a reference, and the observable works). Exit
// codes: 42 rule held; 43 the failed open leaked the subsystem; 47 a failed
// set_sound retry leaked it; 44 no global sound; 45 control failed; 46 the
// SDL build has no disk audio driver (a loud red, never a skip).
TEST(GameContext, an_audio_device_that_will_not_open_releases_the_audio_subsystem)
{
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    EXPECT_EXIT(
        {
            auto* const global_sound = dynamic_cast<sdl_soundob*>(
                og::runtime::current_session->myscreen_->soundp.get());
            if (global_sound == nullptr)
                exit_child_with(44);
            global_sound->shutdown();
            if (SDL_WasInit(SDL_INIT_AUDIO) != 0)
                exit_child_with(45);

            bool have_disk = false;
            for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i)
                if (std::string_view(SDL_GetAudioDriver(i)) == "disk")
                    have_disk = true;
            if (!have_disk)
                exit_child_with(46);

            // A missing parent directory makes the path unopenable on every
            // platform without drive or permission tricks.
            const std::filesystem::path missing_parent =
                std::filesystem::temp_directory_path() /
                ("og-336-no-such-dir-" + std::to_string(::getpid()));
            std::error_code ec;
            std::filesystem::remove_all(missing_parent, ec);
            const std::string out_file = (missing_parent / "out.raw").string();
            SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "disk",
                                    SDL_HINT_OVERRIDE);
            SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_OUTPUT_FILE,
                                    out_file.c_str(), SDL_HINT_OVERRIDE);

            sdl_soundob sound;
            if (sound.silence != 1 || SDL_WasInit(SDL_INIT_AUDIO) != 0)
                exit_child_with(43);
            for (int toggle = 0; toggle < 2; ++toggle)
            {
                sound.set_sound(true);
                sound.set_sound(false);
            }
            if (sound.silence != 1 || SDL_WasInit(SDL_INIT_AUDIO) != 0)
                exit_child_with(47);
            exit_child_with(42);
        },
        ::testing::ExitedWithCode(42), "");
}

// Rule (sound.cpp load_sound): one sample that will not decode stays an empty
// clip (play_sound skips it) and the rest of the bank still loads; it is not
// a reason to quit. The user data directory is mounted ahead of the stock
// sound/ directory (io_init), so a corrupt sound/twang.wav there is what the
// loader reads for SOUND_BOW.
TEST(GameContext, an_unreadable_sound_file_leaves_only_that_clip_empty)
{
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    EXPECT_EXIT(
        {
            const std::filesystem::path dir =
                std::filesystem::path(get_user_path()) / "sound";
            std::filesystem::create_directories(dir);
            {
                std::ofstream corrupt(dir / "twang.wav", std::ios::binary);
                corrupt << "not a RIFF/WAVE file";
            }
            sdl_soundob sound;
            exit_child(sound.silence == 0 &&
                       sound.sound[SOUND_BOW].buf == nullptr &&
                       sound.sound[SOUND_BOW].len == 0u &&
                       sound.sound[SOUND_CLANG].buf != nullptr);
        },
        ::testing::ExitedWithCode(42), "");
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

TEST(GameContext, production_rng_stays_in_bounds_and_actually_varies)
{
    ProductionRandom rng;
    std::set<Uint32> seen;
    for (int i = 0; i < 100; i++) {
        Uint32 val = rng.next(10);
        ASSERT_TRUE(val < 10) << "ProductionRandom::next(10) should return [0,9]";
        seen.insert(val);
    }
    // A `next` that always returns 0 satisfies the bound, and a `next` that
    // alternates between two values satisfies "at least 2 distinct". 100 draws
    // over 10 buckets miss more than five buckets only with probability far
    // below any flake budget, so a real generator clears this and a degenerate
    // one (constant, toggling, stuck low bit) does not.
    ASSERT_GE(seen.size(), 5u)
        << "ProductionRandom::next(10) must spread across 100 draws, not "
           "return a constant or cycle a couple of values";
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


TEST(GameContext, input_state_from_sdl_overwrites_every_held_bit_from_sdl)
{
    // The sampler OWNS the held array: it writes every bit from
    // isPlayerHoldingKey and resets timer_wait_request, so stale bits from the
    // previous frame cannot survive. Pre-dirty the state first -- asserting
    // "everything is false" on a fresh InputState is also satisfied by a
    // sampler with an empty body.
    InputState state;
    state.players[0].held[static_cast<int>(InputKey::Fire)] = true;
    state.players[1].held[static_cast<int>(InputKey::Left)] = true;
    state.players[MAX_PLAYERS - 1].held[static_cast<int>(InputKey::Up)] = true;
    state.timer_wait_request = 4;

    input_state_from_sdl(state);

    ASSERT_EQ(kNoTimerWaitRequest, state.timer_wait_request)
        << "the sampler must reset the timer_wait request every frame";
    ASSERT_FALSE(state.players[0].held[static_cast<int>(InputKey::Fire)])
        << "a stale held bit must be overwritten by the SDL sample";
    ASSERT_FALSE(state.players[1].held[static_cast<int>(InputKey::Left)])
        << "a stale held bit must be overwritten by the SDL sample";
    ASSERT_FALSE(state.players[MAX_PLAYERS - 1].held[static_cast<int>(InputKey::Up)])
        << "a stale held bit must be overwritten by the SDL sample";

    // No keys are pressed in the test environment, so the whole sample is
    // false once it has actually been taken.
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

// The exact A* answers on an empty grid: GRID_SIZE is 32, so (32,32)->(64,64)
// is two diagonal cell steps and (32,32)->(80,32) three orthogonal ones.
// og::pathfinding::AStar over GameplayPathfindingState prices an orthogonal
// step at 1 and a diagonal at sqrt(2), and solve() reports the start cell plus
// every cell stepped onto.
static constexpr std::size_t kDiagonalRouteNodes = 3u;
static constexpr float kDiagonalRouteCost = 2.8284271f;  // 2 * sqrt(2)
static constexpr std::size_t kStraightRouteNodes = 4u;
static constexpr float kStraightRouteCost = 3.0f;

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

    // On an empty grid every route below is a straight run of whole cells, so
    // the node count and the cost are exactly knowable: A* answers start plus
    // one node per cell stepped, and each step costs 1. "at least 2 nodes,
    // cost above zero" accepted a solver that wandered or priced the route
    // wrongly, which is the whole output of this class.
    GameplayPathfindingState source;
    std::vector<void*> path;
    float total_cost = 0.0f;
    source.solve_for_point(actor, 64, 64, make_state(32, 32),
                           make_state(64, 64), path, total_cost);
    ASSERT_EQ(kDiagonalRouteNodes, path.size())
        << "(32,32) -> (64,64) is two diagonal steps: start + 2 nodes";
    EXPECT_FLOAT_EQ(kDiagonalRouteCost, total_cost)
        << "two diagonal steps cost exactly 2 * sqrt(2)";
    EXPECT_EQ(make_state(32, 32), path.front())
        << "the path opens on the start cell";
    EXPECT_EQ(make_state(64, 64), path.back())
        << "the path closes on the goal cell";

    // A moved-from solver keeps no search state: it answers an EMPTY path.
    GameplayPathfindingState moved(std::move(source));
    path.assign(1, reinterpret_cast<void*>(1));
    total_cost = 99.0f;
    source.solve_for_point(actor, 64, 64, make_state(32, 32),
                           make_state(64, 64), path, total_cost);
    EXPECT_TRUE(path.empty());
    EXPECT_FLOAT_EQ(0.0f, total_cost);

    // ... and the move TARGET answers exactly what the original would have.
    moved.solve_for_point(actor, 80, 32, make_state(32, 32),
                          make_state(80, 32), path, total_cost);
    ASSERT_EQ(kStraightRouteNodes, path.size())
        << "(32,32) -> (80,32) is three cells east: start + 3 nodes";
    EXPECT_FLOAT_EQ(kStraightRouteCost, total_cost)
        << "three orthogonal steps cost exactly " << kStraightRouteCost;
    EXPECT_EQ(make_state(80, 32), path.back())
        << "the moved-to solver closes on the goal cell";

    GameplayPathfindingState assigned;
    assigned = std::move(moved);
    assigned.solve_for_point(actor, 32, 80, make_state(32, 32),
                             make_state(32, 80), path, total_cost);
    ASSERT_EQ(kStraightRouteNodes, path.size())
        << "(32,32) -> (32,80) is three cells south: start + 3 nodes";
    EXPECT_FLOAT_EQ(kStraightRouteCost, total_cost)
        << "three orthogonal steps cost exactly " << kStraightRouteCost;
    EXPECT_EQ(make_state(32, 80), path.back())
        << "the move-assigned solver closes on the goal cell";
}

// ---------------------------------------------------------------------------
// #335: a non-interactive session (openglad_demo's workers) answers its own
// modals. The product check sits ABOVE each dialog's TESTING bypass, so a
// flagged test executes the product line and the bypass's trace / override
// queue is the observable that it pre-empted the dialog.
// ---------------------------------------------------------------------------
namespace {
struct NonInteractiveGuard
{
    og::runtime::SessionState* session = og::runtime::current_session;
    bool saved = session->non_interactive_;
    explicit NonInteractiveGuard(bool on) { session->non_interactive_ = on; }
    ~NonInteractiveGuard()
    {
        session->non_interactive_ = saved;
        picker_testing_yes_or_no_queue_clear();
    }
    void set(bool on) { session->non_interactive_ = on; }
};
} // namespace

// Rule: popup_dialog in a non-interactive session logs its text and returns
// before the dialog (here: before the TESTING bypass that writes the popup
// trace). Positive control: the same call with the flag off reaches the
// bypass and writes "T: M".
TEST(NonInteractiveSession, popup_dialog_returns_before_any_dialog)
{
    NonInteractiveGuard guard(true);
    trace_clear();
    popup_dialog("T", "M");
    EXPECT_FALSE(trace_contains("popup", "T: M"))
        << "a non-interactive popup must return before the dialog path";

    guard.set(false);
    popup_dialog("T", "M");
    EXPECT_TRUE(trace_contains("popup", "T: M"))
        << "control: an interactive popup reaches the dialog path";
}

// Rule: both prompt shapes decline in a non-interactive session, without
// reaching the dialog and without consuming a queued answer. The queued
// `true` is the proof: the flagged calls return false and leave it queued,
// and the first interactive call after them reads it (control and queue
// proof in one).
TEST(NonInteractiveSession, yes_no_prompts_decline_without_a_dialog)
{
    NonInteractiveGuard guard(true);
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);

    EXPECT_FALSE(yes_or_no_prompt("T", "M", true))
        << "a non-interactive yes/no prompt answers no";
    EXPECT_FALSE(no_or_yes_prompt("T", "N", true))
        << "a non-interactive no/yes prompt answers no";
    EXPECT_FALSE(trace_contains("confirm", "T: M"));
    EXPECT_FALSE(trace_contains("confirm", "T: N"));
    EXPECT_EQ(1, picker_testing_yes_or_no_queue_remaining())
        << "the flagged prompts must not consume the queued answer";

    guard.set(false);
    EXPECT_TRUE(yes_or_no_prompt("T", "M", false))
        << "control: the interactive prompt reads the queued yes";
    EXPECT_TRUE(trace_contains("confirm", "T: M"));
    EXPECT_EQ(0, picker_testing_yes_or_no_queue_remaining());
}

// Rule: the 4-arg results_screen in a non-interactive session shows (logs)
// the ending popup and returns false without running the panel. The force
// seam is ON, so without the product check the REAL panel would run (its
// TESTING bypass is byte-identical to the product arm and could never go
// red). The escape thread, started first, records whether the panel loop ever
// went live and, if it did, ends it through world().end (the panel's own
// exit check), so a broken rule reads as a failed assertion, never a hang.
// Positive control: the ResultsScreenFullUi force-ON tests (og_test_menu_ui)
// drive the same real panel and see its loop live and its results traces.
// Kept here, not beside them: og_test_menu_ui sits at the coverage-lane
// ceiling (R-std-5) and this pin costs milliseconds.
namespace {
struct ResultsEscapeState
{
    std::atomic<bool> main_returned{false};
    std::atomic<bool> panel_went_live{false};
    std::atomic<bool> escape_ended_panel{false};
};

template <typename Pred>
bool wait_bounded(Pred pred, int timeout_ms)
{
    const Uint64 start = SDL_GetTicks();
    while (!pred())
    {
        if (SDL_GetTicks() - start > static_cast<Uint64>(timeout_ms))
            return false;
        SDL_Delay(2);
    }
    return true;
}

int results_escape_thread(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    auto* st = static_cast<ResultsEscapeState*>(data);
    // Failure bound only: on the passing path main_returned ends this wait
    // at once.
    (void)wait_bounded(
        [st] { return st->main_returned.load() || results_screen_testing_loop_live(); },
        10000);
    if (results_screen_testing_loop_live())
    {
        st->panel_went_live = true;
        og::runtime::current_session->myscreen_->world().end = 1;
        st->escape_ended_panel =
            wait_bounded([] { return !results_screen_testing_loop_live(); }, 10000);
    }
    return 0;
}
} // namespace

namespace {
struct ResultsLeg
{
    bool retry = true;
    bool panel_went_live = false;
    bool escape_ended_panel = false;
    bool mvp_traced = false;
};

// One forced-full results_screen call with the flag as given; the escape
// thread ends a live panel through world().end.
ResultsLeg run_results_leg(bool non_interactive)
{
    auto* const session = og::runtime::current_session;
    const char saved_end = session->myscreen_->world().end;
    session->myscreen_->world().end = 0;
    session->myscreen_->save_data.current_campaign = "gladiator";
    session->myscreen_->save_data.scen_num = 1;
    session->myscreen_->save_data.current_levels.clear();

    std::map<int, guy*> before;
    std::map<int, walker*> after;
    ResultsEscapeState st;
    ResultsLeg leg;
    NonInteractiveGuard guard(non_interactive);
    trace_clear();
    results_screen_testing_set_force_full(true);
    SDL_Thread* thread = SDL_CreateThread(results_escape_thread, "results_escape", &st);
    if (thread != nullptr)
    {
        leg.retry = results_screen(0, 2, before, after);
        st.main_returned = true;
        SDL_WaitThread(thread, nullptr);
    }
    results_screen_testing_set_force_full(false);
    session->myscreen_->world().end = saved_end;
    leg.panel_went_live = st.panel_went_live.load();
    leg.escape_ended_panel = st.escape_ended_panel.load();
    leg.mvp_traced = trace_contains("results", "mvp_");
    return leg;
}
} // namespace

// Positive control in the same test: the identical call with the flag off
// runs the real panel (live loop, MVP trace) and the escape thread ends it.
TEST(NonInteractiveSession, results_screen_skips_the_panel_and_returns_no_retry)
{
    ASSERT_NE(nullptr, og::runtime::current_session);

    const ResultsLeg flagged = run_results_leg(true);
    EXPECT_FALSE(flagged.panel_went_live)
        << "a non-interactive session must never run the results panel loop";
    EXPECT_FALSE(flagged.mvp_traced)
        << "the panel (which picks the MVP before its loop) must not run";
    EXPECT_FALSE(flagged.retry) << "a non-interactive session never asks for a retry";

    const ResultsLeg control = run_results_leg(false);
    EXPECT_TRUE(control.panel_went_live)
        << "control: with the flag off the forced panel loop goes live";
    EXPECT_TRUE(control.escape_ended_panel)
        << "control: the escape thread ends the live panel";
    EXPECT_TRUE(control.mvp_traced) << "control: the real panel picks an MVP";
    EXPECT_FALSE(control.retry) << "control: a panel ended by world().end asks no retry";
}
