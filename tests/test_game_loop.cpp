#include "SDL.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <thread>
#include <vector>

#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/replay_runtime.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/platform/local_transport_shadow.h>
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
void glad_init(bool preserve_frame_timing = false);
void glad_init(
    bool preserve_frame_timing,
    const og::ui::PickerLobbyGameStartConfig* lobby_config);
void ready_screen_for_game_start(
    screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig* lobby_config);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);


struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;

    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_)
        , key_enum(key_enum_)
        , old_key(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }

    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old_key;
    }
};

struct SessionKeyStateGuard
{
    const Uint8* old_keystates = nullptr;
    std::array<Uint8, SDL_NUM_SCANCODES> fake_keystates{};

    SessionKeyStateGuard()
        : old_keystates(og::runtime::current_session->keystates_)
    {
        fake_keystates.fill(0);
        og::runtime::current_session->keystates_ = fake_keystates.data();
    }

    ~SessionKeyStateGuard()
    {
        og::runtime::current_session->keystates_ = old_keystates;
    }

    void set(SDL_Keycode key, bool pressed)
    {
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
        if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
            fake_keystates[static_cast<std::size_t>(scancode)] = pressed ? 1 : 0;
    }
};

struct GameSpeedGuard
{
    float old_speed = 1.0f;

    explicit GameSpeedGuard(float new_speed)
        : old_speed(og::runtime::current_session->g_game_speed_factor_)
    {
        set_game_speed(new_speed);
    }

    ~GameSpeedGuard()
    {
        set_game_speed(old_speed);
    }
};

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

static bool load_minimal_game_loop_scenario(const char* save_name)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    if (game_screen == nullptr ||
        og::runtime::current_game_session == nullptr)
        return false;

    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    game_screen->save_data.save(save_name);
    game_screen->save_data.save("save0");
    if (load_saved_game(save_name, game_screen) == 0)
        return false;

    og::runtime::reset_local_transport_shadow(
        *og::runtime::current_game_session,
        *game_screen);
    return og::runtime::local_transport_active(
        *og::runtime::current_game_session);
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
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_save"))
        << "load_saved_game should succeed for scenario 1";

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

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*og::runtime::current_session->myscreen_,
                                     st,
                                     deps));

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
    ASSERT_EQ(0u, og::runtime::current_session->replay_recorder_->frame_count());
    EXPECT_TRUE(og::runtime::current_session->replay_recorder_->has_initial_snapshot());

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
    ASSERT_EQ(1u, og::runtime::current_session->replay_recorder_->frame_count());
    EXPECT_EQ(1u, og::runtime::current_session->replay_recorder_->frames().back().tick);
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
    ASSERT_EQ(1u, player.frame_count());
    EXPECT_EQ(1u, player.frames().front().tick);
    EXPECT_EQ(og::sim::serialize_input(1u, InputState{}),
              og::sim::serialize_input(player.frames().front().tick,
                                       player.frames().front().input));

    ASSERT_TRUE(og::runtime::initialize_replay_screen(*game_screen, player));
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::reset_local_transport_shadow(*og::runtime::current_game_session,
                                              *game_screen);
    auto first_frame = player.next_frame();
    ASSERT_TRUE(first_frame.has_value());
    EXPECT_EQ(1u, first_frame->tick);
    EXPECT_EQ(og::sim::serialize_input(1u, InputState{}),
              og::sim::serialize_input(first_frame->tick, first_frame->input));

    GameLoopFrameState replay_state;
    GameLoopDeps replay_deps;
    replay_deps.enable_render = false;
    replay_deps.enable_event_poll = false;
    replay_deps.enable_frame_timing = false;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, replay_state, replay_deps));
    expect_snapshot_bytes_match(*expected_after_tick_one,
                                og::sim::peek_keyframe_snapshot(game_screen->world()));

    game_screen->world().end = 0;
    game_screen->world().delete_objects();
    std::filesystem::remove(replay_path, ec);
}

TEST(GameLoop, glad_init_preserves_existing_timing_when_requested)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    GameLoopFrameState& st = og::runtime::current_session->frame_state_;
    st.done = true;
    st.initialized = true;
    st.currentcycle = 9;
    st.cycletime = 11;
    st.last_frame_time = 1234u;
    st.accumulated_time = 567u;
    st.has_pending_input = true;
    st.pending_input.players[0].held[KEY_YELL] = true;

    glad_init(true);

    EXPECT_FALSE(st.done);
    EXPECT_FALSE(st.initialized);
    EXPECT_EQ(0, st.currentcycle);
    EXPECT_EQ(3, st.cycletime);
    EXPECT_EQ(1234u, st.last_frame_time);
    EXPECT_EQ(567u, st.accumulated_time);
    EXPECT_FALSE(st.has_pending_input);
    EXPECT_FALSE(st.pending_input.players[0].held[KEY_YELL]);

    game_screen->world().delete_objects();
}

TEST(GameLoop, glad_init_clears_stale_view_text_when_tick_count_restarts)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    viewscreen* const view = game_screen->viewob[0].get();
    ASSERT_TRUE(view != nullptr);
    view->set_display_text("stale pause text", 10);
    ASSERT_EQ(std::string("stale pause text"), view->textlist[0]);

    glad_init();

    ASSERT_TRUE(game_screen->viewob[0] != nullptr);
    EXPECT_TRUE(game_screen->viewob[0]->textlist[0].empty());

    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, clear_local_transport_shadow_deactivates_session_runtime)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(og::runtime::local_transport_active(*og::runtime::current_session));

    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    EXPECT_FALSE(og::runtime::local_transport_active(*og::runtime::current_session));

    game_screen->world().delete_objects();
}

TEST(GameLoop, glad_init_uses_save_data_numplayers_for_local_transport_clients)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 3;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    game_screen->ready_for_battle(1);
    ASSERT_EQ(1, game_screen->numviews);

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    EXPECT_EQ(3u,
              og::runtime::local_transport_client_count(
                  *og::runtime::current_game_session));

    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, glad_init_applies_lobby_start_config_before_level_load)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 2;
    save.scen_num = 2;
    save.numplayers = 2;
    save.allied_mode = 0;

    auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
    leader->name = "Leader";
    leader->teamnum = 0;
    auto scout = std::make_unique<guy>(FAMILY_ARCHER);
    scout->name = "Scout";
    scout->teamnum = 1;
    save.team_list[0] = std::move(leader);
    save.team_list[1] = std::move(scout);
    save.team_size = 2;
    og::runtime::current_session->current_difficulty_ = 3;

    picker_lobby_shutdown();
    picker_lobby_initialize_from_save();
    ASSERT_TRUE(picker_lobby_request_start());
    std::optional<og::ui::PickerLobbyGameStartConfig> lobby_config =
        picker_lobby_consume_game_start_config();
    ASSERT_TRUE(lobby_config.has_value());
    EXPECT_EQ("org.openglad.gladiator", lobby_config->save_data.current_campaign);
    EXPECT_EQ(2, lobby_config->save_data.scen_num);
    EXPECT_EQ(3, lobby_config->difficulty);
    picker_lobby_shutdown();

    // Corrupt both memory and save0 so glad_init must use the explicit lobby
    // config instead of the stale save state.
    save.current_campaign = "definitely.not.a.campaign";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 9999;
    save.scen_num = 9999;
    save.numplayers = 1;
    save.allied_mode = 1;
    save.team_list[0].reset();
    save.team_list[1].reset();
    save.team_size = 0;
    og::runtime::current_session->current_difficulty_ = 0;
    ASSERT_TRUE(save.save("save0"));

    game_screen->ready_for_battle(1);
    ASSERT_EQ(1, game_screen->numviews);

    glad_init(false, &*lobby_config);
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    EXPECT_EQ("org.openglad.gladiator", game_screen->save_data.current_campaign);
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.scen_num));
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.numplayers));
    EXPECT_EQ(0, static_cast<int>(game_screen->save_data.allied_mode));
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign());
    EXPECT_EQ(2, game_screen->world().id);
    EXPECT_EQ(3, og::runtime::current_session->current_difficulty_);
    EXPECT_EQ(og::ui::difficulty_percent(3),
              static_cast<int>(game_screen->world().difficulty));
    EXPECT_EQ(2u,
              og::runtime::local_transport_client_count(
                  *og::runtime::current_game_session));
    ASSERT_TRUE(game_screen->save_data.team_list[0] != nullptr);
    ASSERT_TRUE(game_screen->save_data.team_list[1] != nullptr);
    EXPECT_EQ("Leader", game_screen->save_data.team_list[0]->name);
    EXPECT_EQ("Scout", game_screen->save_data.team_list[1]->name);

    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, ready_screen_for_game_start_uses_lobby_player_count_for_numviews)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.numplayers = 1;
    game_screen->ready_for_battle(1);
    ASSERT_EQ(1, game_screen->numviews);

    og::ui::PickerLobbyGameStartConfig config;
    config.save_data.numplayers = 3;

    ready_screen_for_game_start(*game_screen, &config);

    EXPECT_EQ(3, game_screen->numviews);
}

TEST(GameLoop, glad_init_preserves_cached_spectator_lobby_start_config)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 0;
    save.allied_mode = 1;

    auto spectator_team = std::make_unique<guy>(FAMILY_SOLDIER);
    spectator_team->name = "Spectator";
    spectator_team->teamnum = 0;
    save.team_list[0] = std::move(spectator_team);
    save.team_size = 1;

    picker_lobby_shutdown();
    picker_lobby_initialize_from_save();
    ASSERT_TRUE(picker_lobby_request_start());

    // Corrupt both memory and save0 so glad_init must use the cached lobby config.
    save.numplayers = 1;
    save.allied_mode = 0;
    save.team_list[0].reset();
    save.team_size = 0;
    ASSERT_TRUE(save.save("save0"));

    game_screen->ready_for_battle(1);
    ASSERT_EQ(1, game_screen->numviews);

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    EXPECT_EQ(0, static_cast<int>(game_screen->save_data.numplayers));
    EXPECT_EQ(1, static_cast<int>(game_screen->save_data.allied_mode));
    EXPECT_EQ(1u,
              og::runtime::local_transport_client_count(
                  *og::runtime::current_game_session));
    ASSERT_TRUE(game_screen->save_data.team_list[0] != nullptr);
    EXPECT_EQ("Spectator", game_screen->save_data.team_list[0]->name);
    EXPECT_FALSE(picker_lobby_consume_game_start_config().has_value());

    picker_lobby_shutdown();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop,
     reset_local_transport_shadow_uses_gameplay_session_for_client_palette_sync)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& gameplay_session = *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));

    og::runtime::clear_local_transport_shadow(gameplay_session);
    std::fill(std::begin(gameplay_session.curpal_),
              std::end(gameplay_session.curpal_),
              0u);
    game_screen->world().current_palette_id = 1;

    og::runtime::GameSession::Config other_cfg;
    other_cfg.allocate_screen = false;
    other_cfg.allocate_prefs = false;
    other_cfg.install_legacy_globals = false;
    og::runtime::GameSession other_session(other_cfg);
    std::fill(std::begin(other_session.curpal_),
              std::end(other_session.curpal_),
              0u);

    {
        auto other_scope = other_session.activate();
        og::runtime::reset_local_transport_shadow(gameplay_session, *game_screen);
        EXPECT_EQ(&other_session, og::runtime::current_session);
    }

    EXPECT_TRUE(og::runtime::local_transport_active(gameplay_session));
    EXPECT_EQ(1, game_screen->world().current_palette_id);
    EXPECT_TRUE(std::equal(std::begin(game_screen->bluepalette),
                           std::end(game_screen->bluepalette),
                           std::begin(gameplay_session.curpal_)));
    EXPECT_TRUE(std::all_of(std::begin(other_session.curpal_),
                            std::end(other_session.curpal_),
                            [](unsigned char value) { return value == 0u; }));

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_caps_accumulator_to_four_ticks_per_call)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);

    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_tick_cap"))
        << "load_saved_game should succeed for tick-cap test";

    GameLoopFrameState st;
    st.initialized = true;
    st.last_frame_time = SDL_GetTicks();
    st.accumulated_time = og::sim::DEFAULT_SIM_TICK_MS * 6u;

    int tick_count = 0;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(4, tick_count);
    EXPECT_EQ(0u, st.accumulated_time);

    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_accumulates_input_without_ticking_when_interval_not_reached)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    disablePlayerJoystick(0);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_zero_tick"))
        << "load_saved_game should succeed for zero-tick test";

    SessionKeyStateGuard keystates;
    KeyBindingGuard bind_yell(0, KEY_YELL, SDLK_y);
    keystates.set(SDLK_y, true);
    ctx().input = {};

    GameLoopFrameState st;
    st.initialized = true;
    st.last_frame_time = SDL_GetTicks();
    st.accumulated_time = 0;

    int tick_count = 0;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = 1000;
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);
    EXPECT_TRUE(st.has_pending_input);
    EXPECT_TRUE(st.pending_input.players[0].held[KEY_YELL]);
    EXPECT_TRUE(st.pending_input.players[0].pressed[KEY_YELL]);

    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_leaves_external_timing_state_untouched)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_external_timing"))
        << "load_saved_game should succeed for external-timing test";

    GameLoopFrameState st;
    st.initialized = true;
    st.last_frame_time = 1234u;
    st.accumulated_time = 567u;

    int tick_count = 0;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(1, tick_count);
    EXPECT_EQ(1234u, st.last_frame_time);
    EXPECT_EQ(567u, st.accumulated_time);

    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_runs_multiple_ticks_when_accumulator_has_multiple_intervals)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_multi_tick"))
        << "load_saved_game should succeed for multi-tick test";

    GameLoopFrameState st;
    st.initialized = true;
    st.last_frame_time = SDL_GetTicks();
    st.accumulated_time = og::sim::DEFAULT_SIM_TICK_MS * 2u;

    int tick_count = 0;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(2, tick_count);
    EXPECT_LT(st.accumulated_time, og::sim::DEFAULT_SIM_TICK_MS);

    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_uses_fixed_tick_ms_instead_of_timer_wait_when_requested)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_fixed_tick"))
        << "load_saved_game should succeed for fixed-tick test";

    const signed char old_timer_wait = game_screen->world().timer_wait;
    game_screen->world().timer_wait = 10;

    int derived_ticks = 0;
    GameLoopFrameState derived_st;
    derived_st.initialized = true;
    derived_st.last_frame_time = SDL_GetTicks();
    derived_st.accumulated_time = 100;

    GameLoopDeps derived_deps;
    derived_deps.enable_render = false;
    derived_deps.enable_event_poll = false;
    derived_deps.after_act = [&derived_ticks](screen&) {
        ++derived_ticks;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, derived_st, derived_deps));
    EXPECT_EQ(0, derived_ticks);

    int fixed_ticks = 0;
    GameLoopFrameState fixed_st;
    fixed_st.initialized = true;
    fixed_st.last_frame_time = SDL_GetTicks();
    fixed_st.accumulated_time = 100;

    GameLoopDeps fixed_deps;
    fixed_deps.enable_render = false;
    fixed_deps.enable_event_poll = false;
    fixed_deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    fixed_deps.after_act = [&fixed_ticks](screen&) {
        ++fixed_ticks;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, fixed_st, fixed_deps));
    EXPECT_EQ(1, fixed_ticks);

    game_screen->world().timer_wait = old_timer_wait;
    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_processes_input_before_same_call_tick)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    disablePlayerJoystick(0);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_input_timing"))
        << "load_saved_game should succeed for input-timing test";

    viewscreen* const view = game_screen->viewob[0].get();
    ASSERT_TRUE(view != nullptr);
    ASSERT_TRUE(view->control != nullptr);

    SessionKeyStateGuard keystates;
    KeyBindingGuard bind_yell(0, KEY_YELL, SDLK_y);
    keystates.set(SDLK_y, true);
    ctx().input = {};
    view->control->set_yo_delay(0);

    GameLoopFrameState st;
    st.initialized = true;
    st.last_frame_time = SDL_GetTicks();
    st.accumulated_time = og::sim::DEFAULT_SIM_TICK_MS;

    int yo_delay_after_act = -1;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.after_act = [&yo_delay_after_act, view](screen&) {
        yo_delay_after_act = view->control ? view->control->yo_delay() : -1;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(30, yo_delay_after_act);

    game_screen->world().delete_objects();
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
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_optmenu_save"))
        << "load_saved_game should succeed";

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
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*og::runtime::current_session->myscreen_,
                                     st,
                                     deps));

    esc_thread.join();

    // Cleanup.
    g_script = nullptr;
    set_game_speed(old_speed);
    og::runtime::current_session->keystates_ = saved_keystates;
    vs->control = saved_control;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(GameLoop, game_frame_escape_toggles_network_pause_when_local_transport_is_active)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(og::runtime::local_transport_active(*og::runtime::current_session));

    GameSpeedGuard speed_guard(0.0f);
    struct EscapeFrameOutcome {
        GameFrameResult result = GameFrameResult::Continue;
        bool done = false;
        int redrawme = 0;
    };
    const auto run_escape_frame = [&]() -> EscapeFrameOutcome {
        EventScript script;
        SDL_Event e{};
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = SDLK_ESCAPE;
        script.events.push_back(e);
        g_script = &script;
        game_screen->redrawme = 0;

        GameLoopFrameState st;
        GameLoopDeps deps;
        deps.enable_render = false;
        deps.enable_event_poll = true;
        deps.enable_frame_timing = false;
        deps.poll_event = scripted_poll_adapter;

        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        g_script = nullptr;
        return {
            .result = result,
            .done = st.done,
            .redrawme = static_cast<int>(game_screen->redrawme),
        };
    };

    const EscapeFrameOutcome pause_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, pause_frame.result);
    EXPECT_FALSE(pause_frame.done);
    EXPECT_EQ(1, pause_frame.redrawme);
    EXPECT_TRUE(game_screen->world().paused);
    EXPECT_EQ(0u, game_screen->world().pause_player_index);

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    const EscapeFrameOutcome resume_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, resume_frame.result);
    EXPECT_FALSE(resume_frame.done);
    EXPECT_EQ(1, resume_frame.redrawme);
    EXPECT_FALSE(game_screen->world().paused);
    EXPECT_EQ(og::sim::kNoPausePlayerIndex, game_screen->world().pause_player_index);

    picker_testing_yes_or_no_queue_clear();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_escape_abort_returns_aborted_mission_when_network_pause_confirmed)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "org.openglad.gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(og::runtime::local_transport_active(*og::runtime::current_session));

    GameSpeedGuard speed_guard(0.0f);
    struct EscapeFrameOutcome {
        GameFrameResult result = GameFrameResult::Continue;
        bool done = false;
        int redrawme = 0;
    };
    const auto run_escape_frame = [&]() -> EscapeFrameOutcome {
        EventScript script;
        SDL_Event e{};
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = SDLK_ESCAPE;
        script.events.push_back(e);
        g_script = &script;
        game_screen->redrawme = 0;

        GameLoopFrameState st;
        GameLoopDeps deps;
        deps.enable_render = false;
        deps.enable_event_poll = true;
        deps.enable_frame_timing = false;
        deps.poll_event = scripted_poll_adapter;

        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        g_script = nullptr;
        return {
            .result = result,
            .done = st.done,
            .redrawme = static_cast<int>(game_screen->redrawme),
        };
    };

    const EscapeFrameOutcome pause_frame = run_escape_frame();
    ASSERT_EQ(GameFrameResult::Continue, pause_frame.result);
    ASSERT_FALSE(pause_frame.done);
    ASSERT_EQ(1, pause_frame.redrawme);
    ASSERT_TRUE(game_screen->world().paused);

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    const EscapeFrameOutcome abort_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::AbortedMission, abort_frame.result);
    EXPECT_TRUE(abort_frame.done);
    EXPECT_EQ(1, abort_frame.redrawme);

    picker_testing_yes_or_no_queue_clear();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}
