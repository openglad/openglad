#include "SDL.h"
#include <array>
#include <filesystem>
#include <thread>
#include <vector>

#include <openglad/gameplay/replay.h>
#include <openglad/interface/replay_runtime.h>
#include <openglad/platform/game_loop.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/input.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <openglad/core/util.h>

// myscreen is now a macro defined in base.h (via game_session.h)

short load_saved_game(const char* filename, screen* scr);
void glad_init();
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);


struct EventScript {
    std::vector<SDL_Event> events;
    size_t idx = 0;
};

static int scripted_poll(void* userdata, SDL_Event* out)
{
    EventScript* s = static_cast<EventScript*>(userdata);
    if (s->idx >= s->events.size())
        return 0;
    *out = s->events[s->idx++];
    return 1;
}

// Adapter to match GameLoopDeps signature.
static EventScript* g_script = nullptr;
static int scripted_poll_adapter(SDL_Event* out)
{
    return scripted_poll(g_script, out);
}

static void expect_snapshot_bytes_match(const og::sim::WorldSnapshot& expected,
                                        const og::sim::WorldSnapshot& actual)
{
    const std::vector<std::uint8_t> expected_bytes =
        og::sim::serialize_snapshot(expected);
    const std::vector<std::uint8_t> actual_bytes =
        og::sim::serialize_snapshot(actual);
    const std::optional<og::sim::ReplayVerificationFailure> divergence =
        og::sim::find_first_snapshot_difference(actual.tick_count,
                                                expected,
                                                actual,
                                                false);
    ASSERT_EQ(expected_bytes, actual_bytes)
        << (divergence.has_value()
                ? divergence->field + " expected=" + divergence->expected_value +
                      " actual=" + divergence->actual_value
                : "snapshot bytes diverged");
}

TEST(GameLoop, game_frame_toggles_debug_hotkeys)
{
    // Load a minimal scenario so screen::act() is safe to call.
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_game_loop_save");
    short load_result = load_saved_game("test_game_loop_save", og::runtime::current_session->myscreen_);
    ASSERT_TRUE(load_result != 0) << "load_saved_game should succeed for scenario 1";

    // Ensure no frame delays.
    float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    og::runtime::current_session->debug_draw_paths_ = false;
    og::runtime::current_session->debug_draw_obmap_ = false;

    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_F11;
    script.events.push_back(e);
    e.key.keysym.sym = SDLK_F12;
    script.events.push_back(e);

    g_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    (void)game_frame(*og::runtime::current_session->myscreen_, st, deps);

    ASSERT_TRUE(og::runtime::current_session->debug_draw_paths_) << "F11 should toggle debug_draw_paths";
    ASSERT_TRUE(og::runtime::current_session->debug_draw_obmap_) << "F12 should toggle debug_draw_obmap";

    // Cleanup.
    g_script = nullptr;
    set_game_speed(old_speed);
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(GameLoop, game_frame_with_result_done_when_end_is_set)
{
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;

    const char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;

    const GameFrameResult result = game_frame_with_result(*og::runtime::current_session->myscreen_, st, deps);
    ASSERT_EQ(static_cast<int>(GameFrameResult::Done), static_cast<int>(result)) << "game_frame_with_result should report Done when screen end is set";
    ASSERT_TRUE(st.done) << "state.done should be set when end is set";

    og::runtime::current_session->myscreen_->world().end = old_end;
}

TEST(GameLoop, glad_init_and_game_frame_record_live_replay_to_file)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    const std::filesystem::path replay_path =
        std::filesystem::path(get_user_path()) / "replays" / "last-replay.ogr";
    std::error_code ec;
    std::filesystem::remove(replay_path, ec);

    glad_init();
    ASSERT_TRUE(og::runtime::current_session->replay_recorder_.has_value());
    EXPECT_EQ(replay_path, og::runtime::current_session->replay_output_path_);
    ASSERT_EQ(1u, og::runtime::current_session->replay_recorder_->frame_count());
    EXPECT_TRUE(og::runtime::current_session->replay_recorder_->has_initial_snapshot());
    EXPECT_EQ(1u, og::runtime::current_session->replay_recorder_->frames().front().tick);

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    std::optional<og::sim::WorldSnapshot> expected_after_tick_one;
    deps.after_act = [&expected_after_tick_one](screen& loop_screen) {
        expected_after_tick_one =
            og::sim::peek_keyframe_snapshot(loop_screen.world());
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    ASSERT_TRUE(og::runtime::current_session->replay_recorder_.has_value());
    ASSERT_EQ(2u, og::runtime::current_session->replay_recorder_->frame_count());
    EXPECT_EQ(2u, og::runtime::current_session->replay_recorder_->frames().back().tick);
    ASSERT_TRUE(expected_after_tick_one.has_value());

    game_screen->world().end = 1;
    EXPECT_EQ(GameFrameResult::Done,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_FALSE(og::runtime::current_session->replay_recorder_.has_value());
    ASSERT_TRUE(std::filesystem::exists(replay_path));

    og::sim::ReplayPlayer player;
    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    ASSERT_TRUE(player.load_file(replay_path, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);
    EXPECT_EQ(game_screen->world().id, player.header().level_id);
    EXPECT_EQ(game_screen->save_data.current_campaign, player.header().campaign_id);
    ASSERT_EQ(2u, player.frame_count());
    EXPECT_EQ(1u, player.frames().front().tick);
    EXPECT_EQ(og::sim::serialize_input(1u, InputState{}),
              og::sim::serialize_input(player.frames().front().tick,
                                       player.frames().front().input));

    ASSERT_TRUE(og::runtime::initialize_replay_screen(*game_screen, player));
    auto first_frame = player.next_frame();
    ASSERT_TRUE(first_frame.has_value());
    EXPECT_EQ(1u, first_frame->tick);
    EXPECT_EQ(og::sim::serialize_input(1u, InputState{}),
              og::sim::serialize_input(first_frame->tick, first_frame->input));
    ASSERT_TRUE(game_screen->act());
    expect_snapshot_bytes_match(*expected_after_tick_one,
                                og::sim::peek_keyframe_snapshot(game_screen->world()));

    game_screen->world().end = 0;
    game_screen->world().delete_objects();
    std::filesystem::remove(replay_path, ec);
}


TEST(GameLoop, game_frame_bool_wrapper_matches_typed_result)
{
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;

    const char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;

    const GameFrameResult typed = game_frame_with_result(*og::runtime::current_session->myscreen_, st, deps);
    st.done = false;
    const bool wrapped = game_frame(*og::runtime::current_session->myscreen_, st, deps);

    ASSERT_EQ(static_cast<int>(typed != GameFrameResult::Continue), static_cast<int>(wrapped)) << "bool wrapper should map Continue/non-Continue exactly";

    og::runtime::current_session->myscreen_->world().end = old_end;
}


// ---------------------------------------------------------------------------
// Regression test: options_menu via game_frame_with_result call chain.
//
// This exercises the exact path that hangs on Emscripten:
//   game_frame_with_result -> s.input(KEY_PREFS event) -> options_menu()
//
// On Emscripten, options_menu() has a blocking while-loop that must call
// emscripten_sleep() (via YIELD_SLEEP) to yield to the browser.  If the
// ASYNCIFY compile flag is missing, YIELD_SLEEP is a no-op and the browser
// hangs.  Natively, YIELD_SLEEP is always a no-op so options_menu() is a
// tight busy-loop driven by keystates.  A background thread presses ESC to
// let it exit.
// ---------------------------------------------------------------------------

TEST(GameLoop, game_frame_options_menu_via_key_prefs_completes)
{
    // Load a minimal scenario so screen::act() is safe.
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_game_loop_optmenu_save");
    short load_result = load_saved_game("test_game_loop_optmenu_save", og::runtime::current_session->myscreen_);
    ASSERT_TRUE(load_result != 0) << "load_saved_game should succeed";

    // Ensure a player-controlled walker exists so options_menu() doesn't
    // early-return via its missing-control guard.
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewob[0] should exist";
    if (!vs)
        return;

    walker* saved_control = vs->control;
    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control walker created";
        if (!w)
            return;
        w->set_team_num(0);
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    // Override keystates so we can inject ESC from a background thread.
    const Uint8* saved_keystates = og::runtime::current_session->keystates_;
    std::array<Uint8, SDL_NUM_SCANCODES> fake_keystates{};
    fake_keystates.fill(0);
    og::runtime::current_session->keystates_ = fake_keystates.data();

    // Background thread: press ESC after a short delay to exit options_menu().
    std::thread esc_thread([&fake_keystates]() {
        SDL_Delay(50);
        fake_keystates[SDL_SCANCODE_ESCAPE] = 1;
        SDL_Delay(30);
        fake_keystates[SDL_SCANCODE_ESCAPE] = 0;
    });

    // Build a scripted KEY_PREFS event (SDLK_1 for player 0).
    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = og::runtime::current_session->player_keys_[0][KEY_PREFS];
    e.key.keysym.scancode = SDL_GetScancodeFromKey(e.key.keysym.sym);
    e.key.repeat = 0;
    script.events.push_back(e);

    g_script = &script;

    float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    // This call chain goes through the std::function indirection in
    // game_frame_with_result() and must not hang.
    (void)game_frame(*og::runtime::current_session->myscreen_, st, deps);

    esc_thread.join();

    // Cleanup.
    g_script = nullptr;
    set_game_speed(old_speed);
    og::runtime::current_session->keystates_ = saved_keystates;
    vs->control = saved_control;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(GameLoop, game_frame_escape_abort_returns_aborted_mission_when_confirmed)
{
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_game_loop_abort_yes");
    short load_result = load_saved_game("test_game_loop_abort_yes", og::runtime::current_session->myscreen_);
    ASSERT_TRUE(load_result != 0) << "load_saved_game should succeed for abort test";

    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_ESCAPE;
    script.events.push_back(e);
    g_script = &script;

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);

    og::runtime::current_session->myscreen_->redrawme = 0;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    const GameFrameResult result = game_frame_with_result(*og::runtime::current_session->myscreen_, st, deps);

    ASSERT_EQ(static_cast<int>(GameFrameResult::AbortedMission), static_cast<int>(result)) << "confirmed abort should return AbortedMission";
    ASSERT_TRUE(st.done) << "confirmed abort should mark frame state done";
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->redrawme) << "abort prompt path should request redraw";

    picker_testing_yes_or_no_queue_clear();
    g_script = nullptr;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(GameLoop, game_frame_escape_abort_decline_continues_game)
{
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_game_loop_abort_no");
    short load_result = load_saved_game("test_game_loop_abort_no", og::runtime::current_session->myscreen_);
    ASSERT_TRUE(load_result != 0) << "load_saved_game should succeed for abort-decline test";

    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_ESCAPE;
    script.events.push_back(e);
    g_script = &script;

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);

    og::runtime::current_session->myscreen_->redrawme = 0;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    const GameFrameResult result = game_frame_with_result(*og::runtime::current_session->myscreen_, st, deps);

    ASSERT_EQ(static_cast<int>(GameFrameResult::Continue), static_cast<int>(result)) << "declined abort should keep the game running";
    ASSERT_TRUE(!st.done) << "declined abort should leave frame state active";
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->redrawme) << "declined abort should still request redraw";

    picker_testing_yes_or_no_queue_clear();
    g_script = nullptr;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
