#include "SDL.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
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

class ToggleConnectedTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId, const std::uint8_t*, std::size_t) override {}

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        return {};
    }

    void accept_connections() override {}

    void disconnect(og::sim::PeerId) override
    {
        connected_ = false;
    }

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return connected_ ? std::vector<og::sim::PeerId>{kPeerId}
                          : std::vector<og::sim::PeerId>{};
    }

    void set_connected(bool connected) noexcept
    {
        connected_ = connected;
    }

private:
    static constexpr og::sim::PeerId kPeerId = 7u;
    bool connected_ = true;
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

static void expect_browser_wrapper_immediate_step_runs_one_tick(
    const char* save_name,
    std::uint32_t sim_interval_ms)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed_guard(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario(save_name));

    const std::uint32_t tick_count_before = game_screen->world().tick_count_;
    const int framecount_before = game_screen->framecount;
    int tick_count = 0;

    GameLoopFrameState st;
    // Pick an accumulator/delta that always crosses the sim interval so
    // should_run_frame is true regardless of caller-supplied sim_interval_ms.
    // For sim_interval_ms == 0 (fast-mode) any (acc, delta) works.
    const std::uint32_t delta = std::max<std::uint32_t>(16u, sim_interval_ms);
    const std::uint32_t accumulated = sim_interval_ms > 0u
        ? sim_interval_ms - 1u
        : 33u;
    const og::core::BrowserFramePacingResult pacing =
        og::core::step_browser_frame_pacing(accumulated, delta, sim_interval_ms);
    ASSERT_TRUE(pacing.should_run_frame);

    GameLoopDeps render_deps;
    render_deps.enable_render = false;
    render_deps.enable_event_poll = false;

    GameLoopDeps tick_deps;
    tick_deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    run_browser_wrapper_frame(
        *game_screen,
        st,
        1016u,
        pacing,
        render_deps,
        tick_deps);

    EXPECT_EQ(1, tick_count);
    EXPECT_EQ(framecount_before + 1, game_screen->framecount);
    EXPECT_EQ(tick_count_before + 1u, game_screen->world().tick_count_);
    EXPECT_TRUE(st.initialized);
    EXPECT_EQ(1016u, st.last_frame_time);
    EXPECT_EQ(pacing.accumulated_after_step_ms, st.accumulated_time);

    game_screen->world().delete_objects();
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

using namespace std::chrono_literals;

template <typename Predicate>
bool wait_until(Predicate&& predicate,
                std::chrono::milliseconds timeout = 2s,
                std::chrono::milliseconds poll_interval =
                    std::chrono::milliseconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(poll_interval);
    }
    return predicate();
}

static std::unique_ptr<guy> make_named_soldier(std::string_view name, short team)
{
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = std::string(name);
    member->teamnum = team;
    return member;
}

static og::sim::LobbyCharacterData make_lobby_character_data(const guy& source)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = source.id;
    character.name = source.name;
    character.family = static_cast<std::int8_t>(source.family);
    character.strength = source.strength;
    character.dexterity = source.dexterity;
    character.constitution = source.constitution;
    character.intelligence = source.intelligence;
    character.armor = source.armor;
    character.exp = source.exp;
    character.kills = source.kills;
    character.level_kills = source.level_kills;
    character.total_damage = source.total_damage;
    character.total_hits = source.total_hits;
    character.total_shots = source.total_shots;
    character.teamnum = source.teamnum;
    character.scen_damage = source.scen_damage;
    character.scen_kills = source.scen_kills;
    character.scen_damage_taken = source.scen_damage_taken;
    character.scen_min_hp = source.scen_min_hp;
    character.scen_shots = source.scen_shots;
    character.scen_hits = source.scen_hits;
    character.level = source.level;
    return character;
}

static void prepare_dense_allied_alpha_bravo_charlie_save(SaveData& save)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.allied_mode = 1;
    save.my_team = 0;
    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = make_named_soldier("Alpha", 0);
    save.team_list[1] = make_named_soldier("Bravo", 0);
    save.team_list[2] = make_named_soldier("Charlie", 0);
    save.team_size = 3;
}

static og::ui::PickerLobbyGameStartConfig make_one_view_lobby_start_config(
    const SaveData& save)
{
    og::ui::PickerLobbyGameStartConfig config;
    config.difficulty = 1;
    config.my_team = 0;
    config.save_data.current_campaign = save.current_campaign;
    config.save_data.scen_num = save.scen_num;
    config.save_data.numplayers = save.numplayers;
    config.save_data.allied_mode = save.allied_mode;

    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        const auto& member = save.team_list[slot_index];
        if (member == nullptr)
            continue;
        config.save_data.team_list.push_back(og::sim::LobbyCharacterSlot{
            .slot_index = static_cast<std::uint8_t>(slot_index),
            .character = make_lobby_character_data(*member),
        });
    }

    return config;
}

static walker* find_named_team_member(GameWorld& world,
                                      std::string_view name,
                                      short team = 0)
{
    for (auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->dead() ||
            entity->query_order() != Order::Living || entity->myguy == nullptr)
        {
            continue;
        }
        if (entity->team_num() == team && entity->myguy->name == name)
            return entity;
    }

    return nullptr;
}

static std::set<std::uint32_t> non_zero_controlled_entity_ids(
    const og::sim::GameClient& client)
{
    std::set<std::uint32_t> ids;
    for (const std::uint32_t entity_id : client.controlled_entity_ids())
    {
        if (entity_id != 0u)
            ids.insert(entity_id);
    }
    return ids;
}

static const og::sim::EntitySnapshot* find_snapshot_entity(
    const og::sim::WorldSnapshot& snapshot,
    std::uint32_t entity_id)
{
    const auto find_in = [entity_id](const auto& entities)
        -> const og::sim::EntitySnapshot* {
        const auto it = std::find_if(
            entities.begin(),
            entities.end(),
            [entity_id](const og::sim::EntitySnapshot& entity) {
                return entity.entity_id == entity_id;
            });
        return it != entities.end() ? &*it : nullptr;
    };

    if (const auto* entity = find_in(snapshot.oblist); entity != nullptr)
        return entity;
    if (const auto* entity = find_in(snapshot.fxlist); entity != nullptr)
        return entity;
    return find_in(snapshot.weaplist);
}

static std::string controlled_entity_name(const og::sim::GameClient& client,
                                          std::uint32_t entity_id)
{
    if (entity_id == 0u || !client.baseline().has_value())
        return {};

    const og::sim::EntitySnapshot* const entity =
        find_snapshot_entity(*client.baseline(), entity_id);
    if (entity == nullptr || entity->guy_id == og::sim::kNoGuyId)
        return {};

    const auto guy_it = std::find_if(
        client.baseline()->guy_snapshots.begin(),
        client.baseline()->guy_snapshots.end(),
        [guy_id = entity->guy_id](const og::sim::GuySnapshot& guy) {
            return guy.guy_id == guy_id;
        });
    return guy_it != client.baseline()->guy_snapshots.end()
        ? guy_it->name
        : std::string();
}

struct AlliedClaimObservation
{
    walker* alpha = nullptr;
    walker* bravo = nullptr;
    walker* charlie = nullptr;
    walker* orphaned = nullptr;
    std::set<std::uint32_t> mapped_ids;
    std::set<std::uint32_t> claimed_ids;
};

static std::string named_entity_label(const walker* entity)
{
    if (entity == nullptr)
        return "missing";
    if (entity->myguy == nullptr)
        return std::to_string(entity->entity_id());
    return entity->myguy->name + "(" + std::to_string(entity->entity_id()) + ")";
}

static std::string world_entity_label(GameWorld& world, std::uint32_t entity_id)
{
    if (entity_id == 0u)
        return "0";

    walker* const entity = world.find_by_id(entity_id);
    if (entity == nullptr || entity->myguy == nullptr)
        return std::to_string(entity_id);
    return entity->myguy->name + "(" + std::to_string(entity_id) + ")";
}

static std::string format_entity_id_set(GameWorld& world,
                                        const std::set<std::uint32_t>& ids)
{
    std::string text = "{";
    bool first = true;
    for (const std::uint32_t entity_id : ids)
    {
        if (!first)
            text += ", ";
        first = false;
        text += world_entity_label(world, entity_id);
    }
    text += "}";
    return text;
}

static std::string claim_mapping_details(screen& gameplay_screen,
                                         const AlliedClaimObservation& observation)
{
    return "Alpha=" + named_entity_label(observation.alpha) +
        " Bravo=" + named_entity_label(observation.bravo) +
        " Charlie=" + named_entity_label(observation.charlie) +
        " claimed=" +
        format_entity_id_set(gameplay_screen.world(), observation.claimed_ids) +
        " mapped=" +
        format_entity_id_set(gameplay_screen.world(), observation.mapped_ids) +
        " orphaned=" + named_entity_label(observation.orphaned);
}

static AlliedClaimObservation observe_allied_claim_mapping(
    screen& gameplay_screen,
    const og::sim::GameClient& display_client)
{
    AlliedClaimObservation observation;
    observation.alpha =
        find_named_team_member(gameplay_screen.world(), "Alpha");
    observation.bravo =
        find_named_team_member(gameplay_screen.world(), "Bravo");
    observation.charlie =
        find_named_team_member(gameplay_screen.world(), "Charlie");
    observation.mapped_ids = non_zero_controlled_entity_ids(display_client);

    for (auto& uptr : gameplay_screen.world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->dead() ||
            entity->query_order() != Order::Living || entity->myguy == nullptr ||
            entity->team_num() != 0 || entity->user() == -1)
        {
            continue;
        }

        observation.claimed_ids.insert(entity->entity_id());
        if (observation.orphaned == nullptr &&
            !observation.mapped_ids.contains(entity->entity_id()))
        {
            observation.orphaned = entity;
        }
    }

    return observation;
}

static InputState make_switch_char_input(std::size_t player_index)
{
    InputState input{};
    if (player_index < static_cast<std::size_t>(MAX_PLAYERS))
    {
        input.players[player_index]
            .held[static_cast<int>(InputAction::SwitchChar)] = true;
        input.players[player_index]
            .pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    }
    return input;
}

static bool drive_host_and_remote_tick(og::runtime::GameSession& session,
                                       og::sim::GameClient& remote_client,
                                       const InputState& host_input,
                                       const InputState& remote_input,
                                       std::uint32_t tick)
{
    og::runtime::local_transport_shadow_send_input(session, host_input, tick);
    remote_client.send_input(remote_input, tick);
    return wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(session);
        remote_client.poll_messages();
        const og::sim::GameClient* const display_client =
            session.myscreen_ != nullptr
                ? session.myscreen_->render_interpolation_client()
                : nullptr;
        return display_client != nullptr &&
            display_client->last_seen_server_tick() >= tick &&
            remote_client.last_seen_server_tick() >= tick;
    });
}

static bool drive_bounded_switch_char_attempt(
    og::runtime::GameSession& session,
    og::sim::GameClient& remote_client,
    bool host_switch,
    bool remote_switch,
    std::uint32_t& next_tick)
{
    const InputState host_input =
        host_switch ? make_switch_char_input(0u) : InputState{};
    const InputState remote_input =
        remote_switch ? make_switch_char_input(1u) : InputState{};
    if (!drive_host_and_remote_tick(
            session, remote_client, host_input, remote_input, next_tick++))
    {
        return false;
    }

    const InputState neutral{};
    return drive_host_and_remote_tick(
        session, remote_client, neutral, neutral, next_tick++);
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
    // Bypass the deadline pacer so the synthesized F11/F12 events get
    // processed in a single call without waiting for the next deadline.
    deps.enable_frame_timing = false;
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
    save.my_team = 0;

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
    EXPECT_EQ(0, lobby_config->my_team);
    picker_lobby_shutdown();

    // Corrupt both memory and save0 so glad_init must use the explicit lobby
    // config instead of the stale save state.
    save.current_campaign = "definitely.not.a.campaign";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 9999;
    save.scen_num = 9999;
    save.numplayers = 1;
    save.allied_mode = 1;
    save.my_team = 1;
    save.team_list[0].reset();
    save.team_list[1].reset();
    save.team_size = 0;
    og::runtime::current_session->current_difficulty_ = 0;
    ASSERT_TRUE(save.save("save0"));

    ready_screen_for_game_start(*game_screen, &*lobby_config);
    ASSERT_EQ(2, game_screen->numviews);

    glad_init(false, &*lobby_config);
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    EXPECT_EQ("org.openglad.gladiator", game_screen->save_data.current_campaign);
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.scen_num));
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.numplayers));
    EXPECT_EQ(0, static_cast<int>(game_screen->save_data.allied_mode));
    EXPECT_EQ(0, static_cast<int>(game_screen->save_data.my_team));
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
    ASSERT_TRUE(game_screen->viewob[0] != nullptr);
    ASSERT_TRUE(game_screen->viewob[1] != nullptr);
    EXPECT_EQ("Leader", game_screen->save_data.team_list[0]->name);
    EXPECT_EQ("Scout", game_screen->save_data.team_list[1]->name);
    EXPECT_EQ(0, static_cast<int>(game_screen->viewob[0]->my_team));
    EXPECT_EQ(1, static_cast<int>(game_screen->viewob[1]->my_team));

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

TEST(GameLoop, local_transport_shadow_guard_paths_without_runtime)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    og::runtime::GameSession session(session_cfg);

    EXPECT_EQ(0u, og::runtime::local_transport_client_count(session));
    EXPECT_FALSE(og::runtime::local_transport_shadow_is_paused(session));
    EXPECT_FALSE(og::runtime::local_transport_shadow_toggle_pause(session));

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 3;
    input.players[0].pressed[static_cast<int>(InputKey::Fire)] = true;
    og::runtime::local_transport_shadow_send_input(session, input, 7u);
    og::runtime::local_transport_shadow_finish_tick(session);

    og::runtime::clear_local_transport_shadow(session);
    EXPECT_FALSE(og::runtime::local_transport_active(session));
}

TEST(GameLoop, local_transport_shadow_invalid_reset_paths_clear_runtime)
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

    auto* const saved_sim_events = gameplay_session.game_.sim_events;
    gameplay_session.game_.sim_events = nullptr;
    og::runtime::reset_local_transport_shadow(gameplay_session, *game_screen);
    EXPECT_FALSE(og::runtime::local_transport_active(gameplay_session));
    gameplay_session.game_.sim_events = saved_sim_events;

    gameplay_session.relay_transport_active_ = true;
    og::runtime::reset_network_host_transport_shadow(
        gameplay_session,
        *game_screen,
        nullptr,
        nullptr,
        {});
    EXPECT_FALSE(og::runtime::local_transport_active(gameplay_session));
    EXPECT_FALSE(gameplay_session.relay_transport_active_);

    gameplay_session.relay_transport_active_ = true;
    og::runtime::reset_network_client_transport_shadow(
        gameplay_session,
        *game_screen,
        nullptr,
        0u,
        0u);
    EXPECT_FALSE(og::runtime::local_transport_active(gameplay_session));
    EXPECT_FALSE(gameplay_session.relay_transport_active_);

    game_screen->world().delete_objects();
}

TEST(GameLoop,
     network_host_runtime_clears_stale_preclaimed_allied_claim_before_binding_players)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    prepare_dense_allied_alpha_bravo_charlie_save(save);
    ASSERT_EQ(3, static_cast<int>(save.team_size));
    ASSERT_EQ(1, static_cast<int>(save.numplayers));
    ASSERT_EQ(1, static_cast<int>(save.allied_mode));
    ASSERT_TRUE(save.save("save0"));

    const og::ui::PickerLobbyGameStartConfig start_config =
        make_one_view_lobby_start_config(save);
    ASSERT_EQ(save.current_campaign, start_config.save_data.current_campaign);
    ASSERT_EQ(save.scen_num, start_config.save_data.scen_num);
    ASSERT_EQ(save.numplayers, start_config.save_data.numplayers);
    ASSERT_EQ(save.allied_mode, start_config.save_data.allied_mode);
    ASSERT_EQ(3u, start_config.save_data.team_list.size());
    EXPECT_EQ("Alpha", start_config.save_data.team_list[0].character.name);
    EXPECT_EQ("Bravo", start_config.save_data.team_list[1].character.name);
    EXPECT_EQ("Charlie", start_config.save_data.team_list[2].character.name);

    ready_screen_for_game_start(*game_screen, &start_config);
    ASSERT_EQ(1, game_screen->numviews);

    glad_init(false, &start_config);
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));
    ASSERT_NE(nullptr, game_screen->viewob[0]);

    walker* const pre_runtime_alpha =
        find_named_team_member(game_screen->world(), "Alpha");
    walker* const pre_runtime_bravo =
        find_named_team_member(game_screen->world(), "Bravo");
    walker* const pre_runtime_charlie =
        find_named_team_member(game_screen->world(), "Charlie");
    ASSERT_NE(nullptr, pre_runtime_alpha);
    ASSERT_NE(nullptr, pre_runtime_bravo);
    ASSERT_NE(nullptr, pre_runtime_charlie);
    ASSERT_EQ(pre_runtime_alpha, game_screen->viewob[0]->control);
    EXPECT_EQ(0, static_cast<int>(pre_runtime_alpha->user()));
    EXPECT_EQ(-1, static_cast<int>(pre_runtime_bravo->user()));
    EXPECT_EQ(-1, static_cast<int>(pre_runtime_charlie->user()));

    auto server_transport = og::sim::InProcessTransport::create_server();
    auto host_local_client_transport =
        server_transport->create_client_transport();
    auto remote_player_transport =
        server_transport->create_client_transport();
    std::vector<og::sim::LobbyPlayerBinding> player_bindings;
    player_bindings.push_back(og::sim::LobbyPlayerBinding{
        .peer_id = host_local_client_transport->local_peer_id(),
        .player_index = 0u,
        .team = 0,
    });
    player_bindings.push_back(og::sim::LobbyPlayerBinding{
        .peer_id = remote_player_transport->local_peer_id(),
        .player_index = 1u,
        .team = 0,
    });

    og::sim::GameClient remote_client(
        *remote_player_transport,
        remote_player_transport->local_peer_id());

    og::runtime::reset_network_host_transport_shadow(
        gameplay_session,
        *game_screen,
        server_transport,
        host_local_client_transport,
        player_bindings);

    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(gameplay_session);
        remote_client.poll_messages();
        const og::sim::GameClient* const display_client =
            game_screen->render_interpolation_client();
        return display_client != nullptr &&
            display_client->initial_setup().has_value() &&
            display_client->baseline().has_value() &&
            remote_client.initial_setup().has_value() &&
            remote_client.baseline().has_value();
    })) << "host display and test-owned remote client should receive the "
           "initial network handoff";

    const og::sim::GameClient* const display_client =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);
    ASSERT_NE(nullptr, game_screen->viewob[0]);
    const AlliedClaimObservation observation =
        observe_allied_claim_mapping(*game_screen, *display_client);
    ASSERT_NE(nullptr, observation.alpha);
    ASSERT_NE(nullptr, observation.bravo);
    ASSERT_NE(nullptr, observation.charlie);
    ASSERT_EQ(observation.claimed_ids, observation.mapped_ids)
        << claim_mapping_details(*game_screen, observation);
    EXPECT_EQ(nullptr, observation.orphaned)
        << claim_mapping_details(*game_screen, observation);
    const std::uint32_t alpha_id = observation.alpha->entity_id();
    const std::uint32_t bravo_id = observation.bravo->entity_id();
    const std::set<std::uint32_t> expected_mapped_ids = {
        alpha_id,
        bravo_id,
    };
    EXPECT_EQ(expected_mapped_ids, observation.mapped_ids);
    EXPECT_EQ(alpha_id,
              display_client->controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id,
              display_client->controlled_entity_ids()[1]);
    EXPECT_EQ("Alpha",
              controlled_entity_name(
                  *display_client,
                  display_client->controlled_entity_ids()[0]));
    EXPECT_EQ("Bravo",
              controlled_entity_name(
                  *display_client,
                  display_client->controlled_entity_ids()[1]));
    EXPECT_EQ(alpha_id,
              remote_client.controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id,
              remote_client.controlled_entity_ids()[1]);
    EXPECT_EQ("Alpha",
              controlled_entity_name(
                  remote_client,
                  remote_client.controlled_entity_ids()[0]));
    EXPECT_EQ("Bravo",
              controlled_entity_name(
                  remote_client,
                  remote_client.controlled_entity_ids()[1]));

    std::uint32_t next_tick = std::max(display_client->last_seen_server_tick(),
                                       remote_client.last_seen_server_tick()) +
        1u;
    ASSERT_TRUE(drive_bounded_switch_char_attempt(
        gameplay_session, remote_client, true, false, next_tick));

    const og::sim::GameClient* const display_client_after_switch =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client_after_switch);
    const AlliedClaimObservation observation_after_switch =
        observe_allied_claim_mapping(*game_screen, *display_client_after_switch);
    ASSERT_NE(nullptr, observation_after_switch.alpha);
    ASSERT_NE(nullptr, observation_after_switch.bravo);
    ASSERT_NE(nullptr, observation_after_switch.charlie);
    ASSERT_NE(nullptr, game_screen->viewob[0]);
    ASSERT_NE(nullptr, game_screen->viewob[0]->control);
    ASSERT_EQ(observation_after_switch.claimed_ids,
              observation_after_switch.mapped_ids)
        << claim_mapping_details(*game_screen, observation_after_switch);
    EXPECT_EQ(nullptr, observation_after_switch.orphaned)
        << claim_mapping_details(*game_screen, observation_after_switch);
    const std::uint32_t charlie_id =
        observation_after_switch.charlie->entity_id();
    const std::set<std::uint32_t> expected_ids_after_switch = {
        bravo_id,
        charlie_id,
    };
    EXPECT_EQ(expected_ids_after_switch, observation_after_switch.mapped_ids);
    EXPECT_EQ(charlie_id, display_client_after_switch->controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id, display_client_after_switch->controlled_entity_ids()[1]);
    EXPECT_EQ(display_client_after_switch->controlled_entity_ids()[0],
              game_screen->viewob[0]->control->entity_id());
    EXPECT_EQ("Charlie",
              controlled_entity_name(
                  *display_client_after_switch,
                  display_client_after_switch->controlled_entity_ids()[0]));
    EXPECT_EQ("Bravo",
              controlled_entity_name(
                  *display_client_after_switch,
                  display_client_after_switch->controlled_entity_ids()[1]));
    EXPECT_EQ(charlie_id, remote_client.controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id, remote_client.controlled_entity_ids()[1]);
    EXPECT_EQ("Charlie",
              controlled_entity_name(
                  remote_client,
                  remote_client.controlled_entity_ids()[0]));
    EXPECT_EQ("Bravo",
              controlled_entity_name(
                  remote_client,
                  remote_client.controlled_entity_ids()[1]));
    EXPECT_FALSE(observation_after_switch.mapped_ids.contains(alpha_id));

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop,
     select_control_for_view_prefers_explicit_player_index_before_same_team_fallback)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(game_screen->viewob[0] != nullptr);

    GameWorld& world = game_screen->world();
    world.delete_objects();

    auto make_player = [&world](int family, short team) {
        walker* const actor = world.add_ob(Order::Living, family);
        actor->set_owned_myguy(std::make_unique<guy>(family));
        actor->myguy->teamnum = team;
        actor->set_team_num(static_cast<unsigned char>(team));
        actor->set_real_team_num(255);
        return actor;
    };

    walker* const host_control = make_player(FAMILY_SOLDIER, 0);
    walker* const guest_control = make_player(FAMILY_ARCHER, 0);
    ASSERT_NE(nullptr, host_control);
    ASSERT_NE(nullptr, guest_control);

    viewscreen* const view = game_screen->viewob[0].get();
    view->my_team = 0;

    std::array<std::uint32_t, MAX_PLAYERS> controlled_entity_ids = {};
    controlled_entity_ids[0] = host_control->entity_id();
    controlled_entity_ids[1] = guest_control->entity_id();

    EXPECT_EQ(
        guest_control,
        og::runtime::detail::select_control_for_view(
            view,
            controlled_entity_ids,
            &world,
            1u));
    EXPECT_EQ(
        host_control,
        og::runtime::detail::select_control_for_view(
            view,
            controlled_entity_ids,
            &world));

    world.delete_objects();
}

TEST(GameLoop, local_transport_shadow_send_input_and_finish_tick_cover_active_paths)
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
    ASSERT_EQ(1u, og::runtime::local_transport_client_count(gameplay_session));

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 4;
    input.players[0].held[static_cast<int>(InputKey::Right)] = true;
    input.players[0].pressed[static_cast<int>(InputKey::Fire)] = true;
    og::runtime::local_transport_shadow_send_input(gameplay_session, input, 9u);

    game_screen->world().end = 1;
    og::runtime::local_transport_shadow_finish_tick(gameplay_session);
    EXPECT_EQ(1, static_cast<int>(game_screen->world().end));

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().end = 0;
    game_screen->world().delete_objects();
}

TEST(GameLoop, network_client_shadow_ends_session_after_connection_loss_timeout)
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

    auto transport = std::make_shared<ToggleConnectedTransport>();
    og::runtime::clear_local_transport_shadow(gameplay_session);
    og::runtime::reset_network_client_transport_shadow(
        gameplay_session,
        *game_screen,
        transport,
        7u,
        0u);
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));

    transport->set_connected(false);
    game_screen->world().end = 0;
    og::runtime::local_transport_shadow_finish_tick(gameplay_session);
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end));

    const og::sim::GameClient* const display_client =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);
    const_cast<og::sim::GameClient*>(display_client)
        ->testing_set_transport_disconnect_elapsed_ms(
            static_cast<float>(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS + 1u));

    og::runtime::local_transport_shadow_finish_tick(gameplay_session);
    EXPECT_EQ(1, static_cast<int>(game_screen->world().end));

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().end = 0;
    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_runs_at_most_one_tick_per_call)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);

    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_tick_cap"))
        << "load_saved_game should succeed for single-tick contract test";

    // Inject a now_ms that has already advanced well past the deadline. The
    // deadline pacer must still run exactly one tick — no catch-up bursts.
    std::uint32_t fake_now = 10000u;
    int tick_count = 0;
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [](std::uint32_t) {};
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // First call configures the pacer; subsequent calls should never run
    // more than one tick even when the clock has slipped multiple intervals.
    // First call configures the pacer; this call should not yet tick.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);

    // Even after slipping six intervals worth of clock time, the next call
    // must run exactly one sim tick — no burst catch-up.
    fake_now += og::sim::DEFAULT_SIM_TICK_MS * 6u;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(1, tick_count);

    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_sleeps_when_deadline_not_reached)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    disablePlayerJoystick(0);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_zero_tick"))
        << "load_saved_game should succeed for idle-deadline test";

    // The pacer skips event/input polling on idle wakeups, so we no longer
    // observe accumulated input here. Instead, verify that the call sleeps
    // toward the next deadline without running a tick.
    std::uint32_t fake_now = 5000u;
    int tick_count = 0;
    std::vector<std::uint32_t> sleeps;
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = 1000;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [&sleeps](std::uint32_t ms) { sleeps.push_back(ms); };
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // First call configures the pacer with deadline = 5000 + 1000 = 6000.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);
    ASSERT_EQ(sleeps.size(), 1u);
    EXPECT_EQ(sleeps[0], 1000u);

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

TEST(GameLoop, game_frame_with_result_runs_one_tick_per_call_even_when_clock_slipped)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_multi_tick"))
        << "load_saved_game should succeed for single-tick contract test";

    std::uint32_t fake_now = 1000u;
    int tick_count = 0;
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [](std::uint32_t) {};
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // Configure pacer.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);

    // Slip two full intervals; the next call must still run exactly one tick.
    fake_now += og::sim::DEFAULT_SIM_TICK_MS * 2u;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(1, tick_count);

    game_screen->world().delete_objects();
}

TEST(GameLoop, game_frame_with_result_uses_fixed_tick_ms_for_pacer_interval)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_fixed_tick"))
        << "load_saved_game should succeed for fixed-tick test";

    // With deps.fixed_tick_ms == DEFAULT_SIM_TICK_MS the pacer must wait
    // exactly that many ms before firing the next sim tick, regardless of
    // world.timer_wait or target_fps_.
    const signed char old_timer_wait = game_screen->world().timer_wait;
    game_screen->world().timer_wait = 10;

    std::uint32_t fake_now = 2000u;
    int tick_count = 0;
    std::vector<std::uint32_t> sleeps;
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [&sleeps](std::uint32_t ms) { sleeps.push_back(ms); };
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // First call configures the pacer (deadline = now + DEFAULT_SIM_TICK_MS)
    // and returns immediately with a sleep equal to the full interval.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);
    ASSERT_EQ(sleeps.size(), 1u);
    EXPECT_EQ(sleeps[0],
              static_cast<std::uint32_t>(og::sim::DEFAULT_SIM_TICK_MS));

    // Advance the clock to exactly the deadline; one tick should run.
    fake_now += og::sim::DEFAULT_SIM_TICK_MS;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(1, tick_count);

    game_screen->world().timer_wait = old_timer_wait;
    game_screen->world().delete_objects();
}

TEST(GameLoopJitter, browser_wrapper_helper_uses_shared_rounded_interval)
{
    constexpr std::uint32_t kSimInterval = 100u;

    // Phase 3: half-interval present heuristic was removed. Under decoupled
    // sim/render intervals, every non-sim browser callback now requests a
    // present, so `waiting` flips should_present_frame false -> true.
    const og::core::BrowserFramePacingResult waiting =
        og::core::step_browser_frame_pacing(0u, 20u, kSimInterval);
    EXPECT_EQ(kSimInterval, waiting.target_interval_ms);
    EXPECT_FALSE(waiting.should_run_frame);
    EXPECT_TRUE(waiting.should_present_frame);
    EXPECT_EQ(20u, waiting.accumulated_after_add_ms);
    EXPECT_EQ(20u, waiting.accumulated_after_step_ms);

    const og::core::BrowserFramePacingResult midpoint =
        og::core::step_browser_frame_pacing(40u, 20u, kSimInterval);
    EXPECT_EQ(kSimInterval, midpoint.target_interval_ms);
    EXPECT_FALSE(midpoint.should_run_frame);
    EXPECT_TRUE(midpoint.should_present_frame);
    EXPECT_EQ(60u, midpoint.accumulated_after_add_ms);
    EXPECT_EQ(60u, midpoint.accumulated_after_step_ms);

    const og::core::BrowserFramePacingResult ready =
        og::core::step_browser_frame_pacing(80u, 20u, kSimInterval);
    EXPECT_EQ(kSimInterval, ready.target_interval_ms);
    EXPECT_TRUE(ready.should_run_frame);
    EXPECT_FALSE(ready.should_present_frame);
    EXPECT_EQ(100u, ready.accumulated_after_add_ms);
    EXPECT_EQ(0u, ready.accumulated_after_step_ms);

    const og::core::BrowserFramePacingResult no_carry =
        og::core::step_browser_frame_pacing(85u, 20u, kSimInterval);
    EXPECT_EQ(kSimInterval, no_carry.target_interval_ms);
    EXPECT_TRUE(no_carry.should_run_frame);
    EXPECT_FALSE(no_carry.should_present_frame);
    EXPECT_EQ(105u, no_carry.accumulated_after_add_ms);
    EXPECT_EQ(0u, no_carry.accumulated_after_step_ms);
}

TEST(GameLoopJitter, browser_wrapper_target_interval_reflects_sim_interval)
{
    constexpr std::uint32_t kSimInterval = 100u;

    const og::core::BrowserFramePacingResult result =
        og::core::step_browser_frame_pacing(0u, 0u, kSimInterval);
    EXPECT_EQ(kSimInterval, result.target_interval_ms);
    EXPECT_FALSE(result.should_run_frame);
    // Phase 3: with sim_interval > 0, every non-sim callback presents.
    EXPECT_TRUE(result.should_present_frame);
    EXPECT_EQ(0u, result.accumulated_after_add_ms);
    EXPECT_EQ(0u, result.accumulated_after_step_ms);
}

TEST(GameLoopJitter, browser_wrapper_target_interval_floor_for_render_helper)
{
    EXPECT_EQ(og::core::target_frame_interval_ms(10),
              og::core::browser_frame_target_interval_ms(10));

    // Browser cannot present faster than the rAF floor (~16 ms), so even a
    // very high target_fps clamps to the floor.
    EXPECT_EQ(16u, og::core::browser_frame_target_interval_ms(120));
    EXPECT_EQ(16u, og::core::browser_frame_target_interval_ms(240));
    EXPECT_EQ(0u, og::core::browser_frame_target_interval_ms(0));
}

TEST(GameLoopJitter, browser_wrapper_honors_zero_sim_interval_fast_mode)
{
    const og::core::BrowserFramePacingResult immediate =
        og::core::step_browser_frame_pacing(33u, 16u, 0u);
    EXPECT_EQ(0u, immediate.target_interval_ms);
    EXPECT_TRUE(immediate.should_run_frame);
    EXPECT_FALSE(immediate.should_present_frame);
    EXPECT_EQ(49u, immediate.accumulated_after_add_ms);
    EXPECT_EQ(0u, immediate.accumulated_after_step_ms);
}

TEST(GameLoopJitter, browser_wrapper_runs_one_tick_for_zero_sim_interval_fast_mode)
{
    expect_browser_wrapper_immediate_step_runs_one_tick(
        "test_game_loop_browser_zero_sim_interval",
        0u);
}

TEST(GameLoopJitter, browser_wrapper_runs_one_tick_for_default_sim_interval)
{
    const std::uint32_t default_sim_interval = static_cast<std::uint32_t>(
        std::lround(og::core::rounded_render_tick_interval_ms(
            og::sim::DEFAULT_TIMER_WAIT, 1.0f)));
    expect_browser_wrapper_immediate_step_runs_one_tick(
        "test_game_loop_browser_default_sim_interval",
        default_sim_interval);
}

TEST(GameLoopJitter, game_frame_pacer_uses_injected_now_ms)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_jitter_now_ms"))
        << "load_saved_game should succeed for injected clock test";

    GameLoopFrameState st;
    std::uint32_t fake_now_ms = 1000u;
    int tick_count = 0;
    std::vector<std::uint32_t> sleeps;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = 50u;
    deps.now_ms = [&fake_now_ms]() {
        return fake_now_ms;
    };
    deps.sleep_ms = [&sleeps](std::uint32_t ms) { sleeps.push_back(ms); };
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // First call configures the pacer (deadline = 1050) and returns sleep.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);
    ASSERT_EQ(sleeps.size(), 1u);
    EXPECT_EQ(sleeps[0], 50u);

    // One ms before deadline: still no tick.
    fake_now_ms = 1049u;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);

    // At deadline: exactly one tick.
    fake_now_ms = 1050u;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(1, tick_count);

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

    int yo_delay_after_act = -1;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    // Bypass the deadline pacer so the call runs a sim tick immediately and
    // exercises the input-before-tick contract.
    deps.enable_frame_timing = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;
    deps.after_act = [&yo_delay_after_act, view](screen&) {
        yo_delay_after_act = view->control ? view->control->yo_delay() : -1;
    };

    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(30, yo_delay_after_act);

    game_screen->world().delete_objects();
}

TEST(GameLoop, single_tick_per_call)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_single_tick"))
        << "load_saved_game should succeed for single-tick contract test";

    constexpr std::uint32_t interval_ms = 20u;
    constexpr int kFrames = 100;

    GameLoopFrameState st;
    std::uint32_t fake_now = 1000u;
    int tick_count = 0;
    std::vector<std::uint32_t> sleeps;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = interval_ms;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [&sleeps](std::uint32_t ms) { sleeps.push_back(ms); };
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // Configure the pacer (deadline = fake_now + interval_ms). This call
    // sleeps the full interval and does not tick.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);

    // N=100 calls, each advancing the clock by exactly one interval. The
    // pacer must fire exactly one tick per call — never more.
    for (int i = 0; i < kFrames; ++i)
    {
        fake_now += interval_ms;
        const int before = tick_count;
        EXPECT_EQ(GameFrameResult::Continue,
                  game_frame_with_result(*game_screen, st, deps));
        EXPECT_EQ(tick_count - before, 1)
            << "iteration " << i << " ran " << (tick_count - before)
            << " ticks; expected exactly 1";
    }
    EXPECT_EQ(tick_count, kFrames);

    game_screen->world().delete_objects();
}

TEST(GameLoop, idle_call_sleeps_remaining_interval)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_idle_sleep"))
        << "load_saved_game should succeed for idle-sleep test";

    constexpr std::uint32_t interval_ms = 20u;

    GameLoopFrameState st;
    std::uint32_t fake_now = 5000u;
    int tick_count = 0;
    std::vector<std::uint32_t> sleeps;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.fixed_tick_ms = interval_ms;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [&sleeps](std::uint32_t ms) { sleeps.push_back(ms); };
    deps.after_act = [&tick_count](screen&) {
        ++tick_count;
    };

    // Configure pacer; first call sleeps the full interval.
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);
    ASSERT_EQ(sleeps.size(), 1u);
    EXPECT_EQ(sleeps[0], interval_ms);

    // Advance the clock to interval_ms / 2 past the configured anchor (i.e.
    // halfway to the deadline). The idle call must sleep exactly the
    // remaining half-interval and must not run a tick.
    fake_now += interval_ms / 2u;
    EXPECT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*game_screen, st, deps));
    EXPECT_EQ(0, tick_count);
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[1], interval_ms - (interval_ms / 2u));

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
    viewscreen* const primary_view =
        og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(primary_view != nullptr);

    GameSpeedGuard speed_guard(0.0f);
    primary_view->clear_text();
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
    ASSERT_FALSE(primary_view->textlist[0].empty());
    EXPECT_EQ(0, primary_view->textlist[0].compare(0, 6, "PAUSED"));
    EXPECT_EQ(std::string("ESC again: Quit?"), primary_view->textlist[1]);

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
