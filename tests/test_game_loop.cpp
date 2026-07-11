#include "SDL.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
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
#include <openglad/core/runtime_trace.h>
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

// Playtest bug A (display side): while a CTF respawn is pending the mapped id
// is 0, but the view must keep following the dead corpse so the camera holds
// and the RESPAWN IN countdown renders. A nonzero mapped id always wins, an
// already-revived walker wearing this player's user tag is kept through the
// one-tick reclaim window, and a corpse whose pending entry disappeared falls
// back to nullptr.
TEST(GameLoop, select_control_for_view_keeps_pending_ctf_respawn_corpse)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(game_screen->viewob[0] != nullptr);

    GameWorld& world = game_screen->world();
    world.delete_objects();
    const char saved_type = world.type;
    world.type |= GameWorld::TYPE_CTF;
    world.ctf = og::sim::CtfState{};
    world.ctf.active = true;

    walker* const corpse = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const other = world.add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, corpse);
    ASSERT_NE(nullptr, other);
    corpse->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    corpse->set_team_num(0);
    corpse->set_user(0);
    corpse->set_dead(1);
    other->set_team_num(3); // off the view's team: no same-team fallback hits

    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60;
    entry.walker_entity_id = corpse->entity_id();
    world.ctf.respawn_queue.push_back(entry);

    viewscreen* const view = game_screen->viewob[0].get();
    walker* const saved_control = view->control;
    view->my_team = 0;
    std::array<std::uint32_t, MAX_PLAYERS> controlled_entity_ids = {};

    // Dead corpse + pending entry: retained.
    view->control = corpse;
    EXPECT_EQ(corpse,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    // A nonzero mapped id beats the keep-alive (body switch never loses).
    controlled_entity_ids[0] = other->entity_id();
    EXPECT_EQ(other,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));
    controlled_entity_ids[0] = 0;

    // Revived (alive, user tag intact), entry consumed, ControlChange not yet
    // applied: still retained through the one-tick window.
    corpse->set_dead(0);
    world.ctf.respawn_queue.clear();
    EXPECT_EQ(corpse,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    // Dead again with NO pending entry (live-duplicate cancel): falls back
    // to nullptr.
    corpse->set_dead(1);
    EXPECT_EQ(nullptr,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    // Outside an active CTF match the keep-alive never engages.
    world.ctf.respawn_queue.push_back(entry);
    world.ctf.active = false;
    EXPECT_EQ(nullptr,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    view->control = saved_control;
    world.ctf = og::sim::CtfState{};
    world.type = saved_type;
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

    // With enable_render = false the render pacer is never configured and
    // remains uninitialized. Only the sim pacer participates.
    EXPECT_EQ(0u, st.render_pacer.interval_ms())
        << "render_pacer must stay uninitialized when enable_render is false";

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
    EXPECT_EQ(0u, st.render_pacer.interval_ms())
        << "render_pacer must stay uninitialized after all sim-only frames";

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
// Locks in master sim cadence under high-fps render. Regresses the bug fixed
// across phases 1-4: sim used to be paced from target_fps, so raising
// target_fps would speed up gameplay. The contract now is sim cadence is
// derived from world.timer_wait * TIMER_WAIT_TO_MS (master semantics) and is
// independent of target_fps; render runs at target_fps.
// ---------------------------------------------------------------------------

TEST(MasterSpeedRegression, sim_runs_at_timer_wait_cadence)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_master_speed_regression"))
        << "load_saved_game should succeed for master-speed regression test";

    // Reset target_fps to 72 for this test, restoring it on exit.
    const int saved_target_fps = og::runtime::current_session->target_fps_;
    og::runtime::current_session->target_fps_ = 72;
    struct TargetFpsRestore {
        int saved;
        ~TargetFpsRestore() {
            og::runtime::current_session->target_fps_ = saved;
        }
    } target_fps_restore{saved_target_fps};

    // Force the master cadence: world.timer_wait = DEFAULT_TIMER_WAIT (=6),
    // so sim_interval = 6 * 13.6 ≈ 82 ms. Confirmed in assertions below.
    game_screen->world().timer_wait = og::sim::DEFAULT_TIMER_WAIT;

    constexpr int kFrames = 600;
    const std::uint32_t render_interval =
        og::core::target_frame_interval_ms(72);
    ASSERT_EQ(14u, render_interval);
    const std::uint32_t sim_interval =
        static_cast<std::uint32_t>(std::lround(
            og::sim::DEFAULT_TIMER_WAIT * og::sim::TIMER_WAIT_TO_MS));
    ASSERT_EQ(82u, sim_interval);

    og::runtime::set_runtime_trace_enabled(true);
    og::runtime::clear_runtime_trace();

    GameLoopFrameState st;
    std::uint32_t fake_now = 200000u;
    int tick_count = 0;
    int render_count = 0;
    std::vector<std::uint32_t> sleeps;
    // Pre-configure both pacers so the loop's interval-mismatch checks are
    // no-ops and the first call does not emit a startup sleep.
    st.sim_pacer.configure(sim_interval, fake_now);
    st.render_pacer.configure(render_interval, fake_now);

    GameLoopDeps deps;
    deps.enable_render = true;
    deps.enable_event_poll = false;
    // Master-cadence default branch: deps.fixed_tick_ms == 0 forces
    // compute_tick_schedule() to read from world.timer_wait.
    deps.fixed_tick_ms = 0u;
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [&sleeps](std::uint32_t ms) { sleeps.push_back(ms); };
    deps.after_act = [&tick_count](screen&) { ++tick_count; };
    deps.on_render = [&render_count](screen&) { ++render_count; };

    for (int i = 0; i < kFrames; ++i)
    {
        fake_now += render_interval;
        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        ASSERT_EQ(GameFrameResult::Continue, result)
            << "scenario must keep running across all 600 master-cadence "
               "frames";
    }

    // 600 calls × 14 ms / 82 ms ≈ 102 sim ticks. Allow ±1 to absorb the
    // initial deadline phase between the two pacers.
    const int expected_sim_ticks = static_cast<int>(
        (static_cast<std::uint64_t>(kFrames) * render_interval) / sim_interval);
    EXPECT_EQ(102, expected_sim_ticks)
        << "expected_sim_ticks formula must match the documented constant";
    EXPECT_NEAR(expected_sim_ticks, tick_count, 1)
        << "sim must run at master cadence (~82 ms / tick), not at the "
           "render rate";

    // Render fires once per call (drive clock advances by exactly
    // render_interval). Allow ±2 to absorb pacer phase.
    EXPECT_NEAR(kFrames, render_count, 2)
        << "render must run at target_fps (~14 ms / frame at 72 fps)";

    // No call should sleep — every call has at least the render pacer
    // firing, so the desktop loop never enters the idle sleep branch.
    EXPECT_TRUE(sleeps.empty())
        << "no sleeps expected when fake_now exactly tracks the render "
           "deadline";
    const auto traces = og::runtime::copy_runtime_trace();
    const int sleep_traces = static_cast<int>(std::count_if(
        traces.begin(), traces.end(),
        [](const og::runtime::RuntimeTraceRecord& r) {
            return r.category == "game_loop" &&
                r.event == "desktop_loop_sleep_ms";
        }));
    EXPECT_EQ(0, sleep_traces)
        << "desktop_loop_sleep_ms must not fire when render fires every call";

    // Sanity: world.timer_wait must remain at DEFAULT_TIMER_WAIT throughout —
    // any drift would mean the test is implicitly retuning master cadence.
    EXPECT_EQ(og::sim::DEFAULT_TIMER_WAIT, game_screen->world().timer_wait)
        << "world.timer_wait must not drift during the regression run";

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

// Regression: in a local split-screen game, a NON-display player (here player 2)
// triggering the "Exit Field?" prompt at a level exit must have that prompt
// answered so the authoritative sim resumes. Player 2 is a *background* client
// sharing player 1's display; background clients previously had no exit-prompt
// callback installed, so player 2's prompt went unanswered and the whole game
// hung (see configure_background_game_client in local_transport_shadow.cpp).
TEST(GameLoop, local_split_screen_background_player_exit_prompt_does_not_hang)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
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
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    // Bring both local clients online (initial setup + a few authoritative steps).
    std::uint32_t tick = 0;
    for (int i = 0; i < 8; ++i)
    {
        og::runtime::local_transport_shadow_send_input(
            session, InputState{}, tick++);
        og::runtime::local_transport_shadow_finish_tick(session);
    }

    // Player 2 (index 1) is the background client. Make it touch the exit and emit
    // the confirmation request the sim would have produced. Queue a decline so the
    // level is not actually exited (no load side effects) — the point is only that
    // the prompt gets *answered*.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_open_exit_prompt(
        session, /*player_index=*/1u, /*destination_level=*/2))
        << "player 2 must have a server-side control to trigger the exit prompt";

    // Pump enough authoritative steps for the round trip: server broadcasts the
    // prompt to player 2 -> background client answers it -> server processes the
    // response and clears the pending prompt. With the fix this resolves; without
    // it the prompt stays pending forever and the local game hangs.
    for (int i = 0; i < 30; ++i)
        og::runtime::local_transport_shadow_finish_tick(session);

    EXPECT_FALSE(
        og::runtime::local_transport_shadow_testing_server_pending_exit_prompt(
            session))
        << "player 2's exit prompt was never answered -> local game hangs";

    picker_testing_yes_or_no_queue_clear();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

// --- Taking an exit ALWAYS returns to the team-build "Continue" menu ---
//
// Touching an exit portal (and confirming) must END the current level on every
// peer and return to the team-build menu — in EVERY mode. It must NEVER advance
// straight into the next level in-session. These tests drive a real local game
// to an *accepted* exit and assert the display session ended (world().end != 0,
// which makes glad_main return to go_menu / the Continue menu). With the
// auto-advance bug the next level loads in place and world().end stays 0.

static int drive_accepted_exit_and_return_display_end(
    og::runtime::GameSession& session, std::size_t exiting_player_index)
{
    // Bring the local client(s) online.
    std::uint32_t tick = 0;
    for (int i = 0; i < 8; ++i)
    {
        og::runtime::local_transport_shadow_send_input(
            session, InputState{}, tick++);
        og::runtime::local_transport_shadow_finish_tick(session);
    }

    // Accept the exit prompt, then have the given player take an exit to level 2.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_TRUE(og::runtime::local_transport_shadow_testing_open_exit_prompt(
        session, exiting_player_index, /*destination_level=*/2))
        << "player " << exiting_player_index
        << " must have a server-side control to take the exit";

    // Round trip: server broadcasts the prompt -> player accepts -> server
    // forwards a terminal EndGame -> the display session ends.
    for (int i = 0; i < 40; ++i)
        og::runtime::local_transport_shadow_finish_tick(session);

    picker_testing_yes_or_no_queue_clear();
    return static_cast<int>(
        og::runtime::current_session->myscreen_->world().end);
}

TEST(GameLoop, exit_returns_to_continue_menu_single_player)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.allied_mode = 1;
    save.my_team = 0;

    auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
    leader->name = "Leader";
    leader->teamnum = 0;
    save.team_list[0] = std::move(leader);
    save.team_size = 1;
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));

    EXPECT_NE(0, drive_accepted_exit_and_return_display_end(session, 0u))
        << "single-player exit must return to the Continue menu, not auto-advance";

    picker_testing_yes_or_no_queue_clear();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

static void setup_local_two_player_save(SaveData& save)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
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
}

TEST(GameLoop, exit_returns_to_continue_menu_local_two_player_host_player)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    setup_local_two_player_save(game_screen->save_data);
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    // Player 1 (the display-driving player) takes the exit.
    EXPECT_NE(0, drive_accepted_exit_and_return_display_end(session, 0u))
        << "local split-screen exit (player 1) must return to the Continue menu";

    picker_testing_yes_or_no_queue_clear();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, exit_returns_to_continue_menu_local_two_player_second_player)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    setup_local_two_player_save(game_screen->save_data);
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    // Player 2 (the background split-screen player) takes the exit — the exact
    // case reported: it must return to the menu, not jump to the next level.
    EXPECT_NE(0, drive_accepted_exit_and_return_display_end(session, 1u))
        << "local split-screen exit (player 2) must return to the Continue menu";

    picker_testing_yes_or_no_queue_clear();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}


// ---------------------------------------------------------------------------
// FX-capture: live-simulation recordings for the visual review site
// (scripts/fx_review). All three tests skip unless OG_FX_CAPTURE_DIR is set,
// so they cost nothing in normal ctest runs. Frames land in
// $OG_FX_CAPTURE_DIR/<scene>/NNN.ppm as P6 PPMs.
//
// Everything here runs the REAL game: glad_init + game_frame_with_result
// (AI acts, walk animations advance, palette cycling runs, the transport
// shadow syncs client and server worlds every tick).
// ---------------------------------------------------------------------------
#include <cstring>
#include <openglad/core/weather.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/button.h>

namespace gameplay_rec {

void dump_dir_for(const char* scene, char* dir, size_t n)
{
    const char* base = getenv("OG_FX_CAPTURE_DIR");
    snprintf(dir, n, "%s/%s", base, scene);
    std::filesystem::create_directories(dir);
}

// Primary viewport only (single-player gameplay scenes).
void dump_viewport(screen* s, const char* scene, int frame)
{
    viewscreen* vs = s->viewob[0].get();
    const int x0 = vs->xloc, y0 = vs->yloc, w = vs->xview, h = vs->yview;
    char dir[512];
    dump_dir_for(scene, dir, sizeof(dir));
    char path[544];
    snprintf(path, sizeof(path), "%s/%03d.ppm", dir, frame);
    FILE* fp = fopen(path, "wb");
    ASSERT_NE(nullptr, fp);
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
        {
            Uint8 r, g, b;
            s->get_pixel(x0 + i, y0 + j, &r, &g, &b);
            fputc(r, fp);
            fputc(g, fp);
            fputc(b, fp);
        }
    fclose(fp);
}

// Full 320x200 screen (split-screen and cinematic scenes).
void dump_screen(screen* s, const char* scene, int frame)
{
    char dir[512];
    dump_dir_for(scene, dir, sizeof(dir));
    char path[544];
    snprintf(path, sizeof(path), "%s/%03d.ppm", dir, frame);
    FILE* fp = fopen(path, "wb");
    ASSERT_NE(nullptr, fp);
    fprintf(fp, "P6\n320 200\n255\n");
    for (int j = 0; j < 200; j++)
        for (int i = 0; i < 320; i++)
        {
            Uint8 r, g, b;
            s->get_pixel(i, j, &r, &g, &b);
            fputc(r, fp);
            fputc(g, fp);
            fputc(b, fp);
        }
    fclose(fp);
}

void build_save(screen* s, const char* campaign, int scen, int numplayers,
                const std::vector<int>& roster, int level)
{
    s->save_data.reset();
    s->save_data.current_campaign = campaign;
    s->save_data.current_levels[s->save_data.current_campaign] =
        static_cast<short>(scen);
    s->save_data.scen_num = static_cast<short>(scen);
    s->save_data.numplayers = static_cast<unsigned char>(numplayers);
    for (size_t i = 0; i < roster.size(); i++)
    {
        auto g = std::make_unique<guy>(roster[i]);
        g->upgrade_to_level(static_cast<short>(level));
        g->teamnum = 0;
        s->save_data.team_list[i] = std::move(g);
    }
    s->save_data.team_size = static_cast<unsigned char>(roster.size());
    ASSERT_TRUE(s->save_data.save("save0"));
}

void all_capture_effects_on()
{
    for (const char* key : {"shadows", "reflections", "weather", "ripples",
                            "trails", "dust", "fire_glow", "screen_shake"})
        cfg.apply_setting("effects", key, "on");
    cfg.apply_setting("effects", "depth_fx", "fog"); // the selector's default
    // The DEFAULT floor presentation (overhang/blob shadows) films itself:
    // the ghost view exists only while a player holds the look-up key.
}

// The per-level weather roll is world state; force the requested kind on
// BOTH worlds (authoritative server + display mirror) so the scene shows it.
void force_weather(WeatherKind kind)
{
    screen* const s = og::runtime::current_session->myscreen_;
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(
            *og::runtime::current_game_session);
    ASSERT_NE(nullptr, server);
    server->world().set_weather(kind);
    s->world().set_weather(kind);
}

void record(const char* name, int scen, WeatherKind kind, int frames,
            int warmup_ticks)
{
    screen* const s = og::runtime::current_session->myscreen_;
    build_save(s, "org.openglad.gladiator", scen, 1,
               {FAMILY_SOLDIER, FAMILY_ELF, FAMILY_MAGE}, 4);
    glad_init();
    all_capture_effects_on();
    force_weather(kind);
    // Wind the deterministic weather clock (rain's lightning flash is
    // scheduled; 560 ticks puts it early in the recorded loop).
    for (int t = 0; t < warmup_ticks; t++)
        effects_advance_frame();

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int f = 0; f < frames; f++)
    {
        if (game_frame_with_result(*s, st, deps) != GameFrameResult::Continue)
            break;
        s->redraw();
        s->swap();
        dump_viewport(s, name, f);
        if (::testing::Test::HasFatalFailure())
            break;
    }
    s->world().end = 0;
    s->world().delete_objects();
}

} // namespace gameplay_rec

TEST(GameLoop, zz_capture_real_gameplay)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    gameplay_rec::record("gameplay_clouds", 1, WeatherKind::Clouds, 200, 0);
    gameplay_rec::record("gameplay_rain", 1, WeatherKind::Rain, 200, 560);
    gameplay_rec::record("gameplay_combat", 3, WeatherKind::None, 200, 0);
    // Level 6 is the wateriest early level (terrain-scanned): shoreline
    // ripples and reflections show up in real combat there.
    gameplay_rec::record("gameplay_water", 6, WeatherKind::Clouds, 200, 0);
}

TEST(GameLoop, zz_capture_splitscreen_gameplay)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";

    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 2;
    save.scen_num = 2;
    save.numplayers = 2;
    save.allied_mode = 1;
    save.my_team = 0;
    for (auto& member : save.team_list)
        member.reset();
    struct RosterEntry { int family; const char* name; };
    const RosterEntry roster[4] = {
        {FAMILY_SOLDIER, "Blade"},
        {FAMILY_BARBARIAN, "Crusher"},
        {FAMILY_ELF, "Sharpeye"},
        {FAMILY_MAGE, "Zapp"},
    };
    for (int i = 0; i < 4; ++i)
    {
        auto member = std::make_unique<guy>(roster[i].family);
        member->name = roster[i].name;
        member->teamnum = 0;
        member->upgrade_to_level(6);
        save.team_list[static_cast<std::size_t>(i)] = std::move(member);
    }
    save.team_size = 4;
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));
    ASSERT_EQ(2, game_screen->numviews);
    ASSERT_TRUE(game_screen->viewob[0] != nullptr);
    ASSERT_TRUE(game_screen->viewob[1] != nullptr);

    gameplay_rec::all_capture_effects_on();
    gameplay_rec::force_weather(WeatherKind::Clouds);

    // Rebind the two local players' direction keys to keycodes no default
    // layout uses, then drive them via faked SDL keystates so the two claimed
    // heroes walk apart (proving the viewports track independent cameras).
    SessionKeyStateGuard keystates;
    const SDL_Keycode p0_up = SDLK_F13, p0_down = SDLK_F14,
                      p0_left = SDLK_F15, p0_right = SDLK_F16;
    const SDL_Keycode p1_up = SDLK_F17, p1_down = SDLK_F18,
                      p1_left = SDLK_F19, p1_right = SDLK_F20;
    og::runtime::current_session->player_keys_[0][KEY_UP] = p0_up;
    og::runtime::current_session->player_keys_[0][KEY_DOWN] = p0_down;
    og::runtime::current_session->player_keys_[0][KEY_LEFT] = p0_left;
    og::runtime::current_session->player_keys_[0][KEY_RIGHT] = p0_right;
    og::runtime::current_session->player_keys_[1][KEY_UP] = p1_up;
    og::runtime::current_session->player_keys_[1][KEY_DOWN] = p1_down;
    og::runtime::current_session->player_keys_[1][KEY_LEFT] = p1_left;
    og::runtime::current_session->player_keys_[1][KEY_RIGHT] = p1_right;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;

    const int total_frames = 280;
    for (int frame = 0; frame < total_frames; ++frame)
    {
        // Steering script: split apart, then drift back while battle rages.
        keystates.set(p0_left, frame >= 10 && frame < 55);
        keystates.set(p0_up, frame >= 55 && frame < 90);
        keystates.set(p1_right, frame >= 10 && frame < 55);
        keystates.set(p1_down, frame >= 55 && frame < 90);
        keystates.set(p0_right, frame >= 150 && frame < 190);
        keystates.set(p1_left, frame >= 150 && frame < 190);

        if (game_frame_with_result(*game_screen, st, deps) !=
            GameFrameResult::Continue)
            break;

        game_screen->redraw();
        score_panel(game_screen);
        game_screen->refresh();
        game_screen->swap();
        gameplay_rec::dump_screen(game_screen, "splitscreen_gameplay", frame);
        if (::testing::Test::HasFatalFailure())
            break;
    }

    // Both viewports must be tracking different heroes.
    ASSERT_TRUE(game_screen->viewob[0]->control != nullptr);
    ASSERT_TRUE(game_screen->viewob[1]->control != nullptr);
    EXPECT_NE(game_screen->viewob[0]->control,
              game_screen->viewob[1]->control);

    game_screen->world().end = 0;
    og::runtime::clear_local_transport_shadow(session);
    game_screen->world().delete_objects();
}

// ---------------------------------------------------------------------------
// Epic spectator battles: the War of the Westlands war levels (6/7/8/14/15/17,
// moved from the Concept Playground's 605-610) with a hero crew deployed at
// the level start markers and a mode-seeking cinematic camera. The centroid of two distant fights is the empty field between
// them — so the camera seeks the densest hostile NEIGHBORHOOD instead, with
// scripted keyframes for floor cuts and establishing shots.
// ---------------------------------------------------------------------------
namespace epic_rec {

struct Key { int frame; int cx; int cy; int floor; bool forced; };

bool action_center(GameWorld& w, int floor, float& out_x, float& out_y)
{
    std::vector<walker*> alive;
    for (auto& up : w.oblist)
        if (walker* a = up.get();
            a != nullptr && !a->dead() && !a->dormant() &&
            a->query_order() == Order::Living &&
            static_cast<int>(a->floor()) == floor)
            alive.push_back(a);
    if (alive.empty())
        return false;
    walker* best = nullptr;
    int best_score = -1;
    for (walker* a : alive)
    {
        int score = 0;
        for (walker* b : alive)
        {
            if (a == b)
                continue;
            const long dx = static_cast<long>(a->xpos()) - b->xpos();
            const long dy = static_cast<long>(a->ypos()) - b->ypos();
            if (dx * dx + dy * dy < 160L * 160L)
                score += a->is_friendly(b) ? 1 : 3;
        }
        if (score > best_score)
        {
            best_score = score;
            best = a;
        }
    }
    long sx = 0, sy = 0;
    int n = 0;
    for (walker* b : alive)
    {
        const long dx = static_cast<long>(best->xpos()) - b->xpos();
        const long dy = static_cast<long>(best->ypos()) - b->ypos();
        if (dx * dx + dy * dy < 160L * 160L)
        {
            sx += static_cast<long>(b->xpos());
            sy += static_cast<long>(b->ypos());
            n++;
        }
    }
    out_x = static_cast<float>(sx) / static_cast<float>(n > 0 ? n : 1);
    out_y = static_cast<float>(sy) / static_cast<float>(n > 0 ? n : 1);
    return true;
}

void record(const char* scene, int scen, WeatherKind kind,
            const std::vector<int>& roster,
            const std::vector<Key>& keys,
            const char* campaign = "org.openglad.westlands")
{
    screen* const s = og::runtime::current_session->myscreen_;
    // The crew deploys at the level's team-0 start position markers.
    gameplay_rec::build_save(s, campaign, scen, 1, roster, 7);
    glad_init();
    gameplay_rec::all_capture_effects_on();
    gameplay_rec::force_weather(kind);
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(
            *og::runtime::current_game_session);
    // Free the player-controlled hero to fight with the rest of the crew.
    for (auto& uptr : server->world().oblist)
        if (walker* wk = uptr.get(); wk != nullptr && wk->user() != -1)
        {
            wk->set_act_type(ACT_RANDOM);
            // Film-armor the player's lead: a full team-0 wipe is an
            // EndGame loss (the Bridge of Shadow's undead tide ate the
            // whole crew at tick ~327 and cut the film). One immortal
            // extra keeps the bound team alive; everyone else stays
            // mortal — the deaths ARE the battle.
            wk->stats()->set_max_hitpoints(30000.0f);
            wk->stats()->set_hitpoints(30000.0f);
        }
    // A named hero's death ends a SAVE_ALL level (EndGame loss) — right in
    // gameplay, wrong for a fixed-length spectator film (the Mountain of
    // Fire cut at tick ~366 when the Ranger-King fell): strip the bit so
    // the war plays to the last scheduled frame. Server first — the mirror
    // re-syncs type from the authoritative snapshots.
    server->world().type = static_cast<char>(
        server->world().type & ~SCEN_TYPE_SAVE_ALL);
    s->world().type = static_cast<char>(
        s->world().type & ~SCEN_TYPE_SAVE_ALL);
    viewscreen* vs = s->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const int world_w = static_cast<int>(s->world().grid.w) * GRID_SIZE;
    const int world_h = static_cast<int>(s->world().grid.h) * GRID_SIZE;
    const int total = keys.back().frame;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    size_t seg = 0;
    float cam_x = static_cast<float>(keys[0].cx);
    float cam_y = static_cast<float>(keys[0].cy);
    int last_floor = keys[0].floor;
    for (int f = 0; f < total; f++)
    {
        while (seg + 1 < keys.size() && keys[seg + 1].frame <= f)
            seg++;
        const Key& a = keys[seg];
        float tx = static_cast<float>(a.cx), ty = static_cast<float>(a.cy);
        if (!a.forced)
            action_center(s->world(), a.floor, tx, ty);
        if (a.floor != last_floor || a.forced)
        {
            cam_x = tx; // snap on a floor cut or scripted anchor
            cam_y = ty;
            last_floor = a.floor;
        }
        cam_x += (tx - cam_x) * 0.05f; // eased drift toward the action
        cam_y += (ty - cam_y) * 0.05f;
        const int cx = static_cast<int>(cam_x);
        const int cy = static_cast<int>(cam_y);
        int topx = cx - 160, topy = cy - 100;
        if (topx < 0) topx = 0;
        if (topy < 0) topy = 0;
        if (topx > world_w - 320) topx = world_w - 320;
        if (topy > world_h - 200) topy = world_h - 200;
        // The transport shadow re-assigns viewport control every tick; the
        // manual camera only holds while control is null, so clear it both
        // before the sim tick and before the redraw.
        vs->editor_floor_override_ = static_cast<Sint32>(a.floor);
        vs->control = nullptr;
        s->set_level_draw_pos(topx, topy);
        if (game_frame_with_result(*s, st, deps) != GameFrameResult::Continue)
            break;
        vs->control = nullptr;
        s->set_level_draw_pos(topx, topy);
        s->redraw();
        s->swap();
        gameplay_rec::dump_screen(s, scene, f);
        if (::testing::Test::HasFatalFailure())
            break;
    }
    vs->editor_floor_override_ = -1;
    s->world().end = 0;
    s->world().delete_objects();
}

} // namespace epic_rec

TEST(GameLoop, zz_capture_epic_battles)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    using epic_rec::Key;
    // OG_FX_CAPTURE_ONLY=<level id> records a single level (fast iteration).
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // The six war stories live in War of the Westlands now (moved from the
    // concept package: 605->15, 606->14, 607->8, 608->6, 609->17, 610->7);
    // the keyframe scripts are unchanged — the battlefields moved intact.
    // The Deeping Wall — and at frame ~500, look to the east: the White
    // Rider arrives behind the flank in a teleport flash (delayed spawn).
    if (want("15"))
        epic_rec::record("epic_15", 15, WeatherKind::Rain,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_CLERIC, FAMILY_BARBARIAN, FAMILY_MAGE},
            {{0, 610, 660, 0, false}, {80, 610, 420, 0, false},
             {84, 260, 310, 1, false}, {170, 1020, 310, 1, false},
             {174, 632, 330, 0, false}, {495, 632, 360, 0, false},
             {500, 1216, 540, 0, true}, {575, 1216, 540, 0, true},
             {580, 900, 420, 0, false}, {800, 700, 380, 0, false}});
    if (want("14"))
        epic_rec::record("epic_14", 14, WeatherKind::Clouds,
            {FAMILY_DRUID, FAMILY_DRUID, FAMILY_ELF, FAMILY_ELF, FAMILY_ELF,
             FAMILY_BARBARIAN, FAMILY_BARBARIAN, FAMILY_ARCHMAGE},
            {{0, 120, 480, 0, false}, {110, 480, 470, 0, false},
             {114, 480, 480, 1, false}, {190, 480, 480, 1, false},
             {194, 480, 480, 2, false}, {270, 480, 480, 2, false},
             {274, 480, 480, 3, false}, {370, 480, 480, 3, false},
             {374, 480, 300, 0, false}, {480, 480, 640, 0, false}});
    if (want("8"))
        epic_rec::record("epic_8", 8, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
             FAMILY_BARBARIAN, FAMILY_BARBARIAN, FAMILY_CLERIC, FAMILY_ELF,
             FAMILY_ELF},
            {{0, 120, 320, 1, false}, {250, 560, 320, 1, false},
             {350, 560, 320, 1, false},
             {354, 560, 320, 0, false}, {440, 760, 320, 0, false}});
    if (want("6"))
        epic_rec::record("epic_6", 6, WeatherKind::None,
            {FAMILY_THIEF, FAMILY_THIEF, FAMILY_ELF, FAMILY_ELF,
             FAMILY_BARBARIAN, FAMILY_BARBARIAN, FAMILY_CLERIC,
             FAMILY_SOLDIER},
            {{0, 180, 180, 2, false}, {130, 760, 240, 2, false},
             {134, 760, 240, 1, false}, {270, 200, 660, 1, false},
             {274, 280, 660, 0, false}, {460, 480, 620, 0, false}});
    if (want("17"))
        epic_rec::record("epic_17", 17, WeatherKind::Clouds,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_MAGE, FAMILY_MAGE, FAMILY_CLERIC},
            {{0, 160, 300, 0, false}, {110, 400, 450, 0, false},
             {240, 730, 400, 0, false}, {400, 730, 400, 0, false},
             {404, 730, 300, 1, false}, {500, 730, 300, 1, false}});
    if (want("7"))
        epic_rec::record("epic_7", 7, WeatherKind::Rain,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
             FAMILY_ELF, FAMILY_ELF, FAMILY_BARBARIAN, FAMILY_BARBARIAN},
            {{0, 160, 330, 2, false}, {250, 1120, 330, 2, false},
             {254, 640, 180, 0, false}, {350, 640, 180, 0, false},
             {354, 640, 330, 2, false}, {480, 640, 330, 2, false}});
    // --- The new Westlands showpiece levels (built for this campaign). ---
    // The White City: open on the torn gate where the war-beast columns push
    // the breach, follow the street fighting, cut up to the wall-top
    // ramparts, hold the beacon on the tower's glass crown, then return to
    // the battle until the Horse-lord's dawn relief flashes in on the
    // north-west plain at tick 700.
    if (want("16"))
        epic_rec::record("epic_16", 16, WeatherKind::Clouds,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_CLERIC, FAMILY_MAGE, FAMILY_BARBARIAN},
            {{0, 672, 392, 0, true}, {70, 672, 392, 0, false},
             {150, 672, 100, 1, false}, {240, 1320, 408, 2, true},
             {330, 672, 392, 0, false}, {690, 168, 80, 0, true},
             {760, 168, 80, 0, true}});
    // The Mountain of Fire: establish the war host's wedge (the Bearer's
    // warded cleft sits at the west map edge), follow the plain battle,
    // hold the Undergate throat, then climb the cone — terrace ring, the
    // north lava fall, the summit rim over the caldera and the twin summit
    // cracks — and come back down to the war. The wild-men relief wakes at
    // tick 400 on the south-west plain.
    if (want("24"))
        epic_rec::record("epic_24", 24, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN,
             FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF, FAMILY_CLERIC,
             FAMILY_MAGE},
            {{0, 130, 380, 0, true}, {80, 400, 390, 0, false},
             {250, 880, 392, 0, true}, {320, 1064, 240, 1, false},
             {400, 150, 530, 0, true}, {460, 400, 390, 0, false},
             {560, 1064, 200, 1, true}, {610, 1064, 392, 2, false},
             {700, 1008, 392, 2, true}, {760, 500, 390, 0, false},
             {800, 500, 390, 0, false}});
    // The High Pass tiles showcase: the new SNOW ground under the new Snow
    // weather kind (on this level snowfall is the terrain override —
    // blizzard country is always snowing; the force below just makes the
    // scene independent of that heuristic). Establish the muster on the
    // south apron by the frozen tarn, follow the tarn-pack fight, cut to
    // the chief's band at the mine gate, then drift back to the action.
    if (want("5"))
        epic_rec::record("epic_5", 5, WeatherKind::Snow,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN,
             FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF, FAMILY_CLERIC,
             FAMILY_THIEF},
            {{0, 808, 888, 0, true}, {60, 808, 888, 0, false},
             {180, 392, 168, 0, true}, {240, 808, 880, 0, false},
             {300, 808, 880, 0, false}});
}

// ---------------------------------------------------------------------------
// War of the Westlands story gameplay: the two Bearer's-road levels captured
// as REAL gameplay (single viewport, the camera bound to the company's
// thief). The crew deploys at the level start markers; the claimed hero is
// freed to AI because no live input arrives in a capture and a statue lead
// makes a dull chase.
// ---------------------------------------------------------------------------
namespace gameplay_rec {

void record_story(const char* name, int scen, WeatherKind kind,
                  const std::vector<int>& roster, int level, int frames,
                  const char* campaign = "org.openglad.westlands")
{
    screen* const s = og::runtime::current_session->myscreen_;
    build_save(s, campaign, scen, 1, roster, level);
    glad_init();
    all_capture_effects_on();
    force_weather(kind);
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(
            *og::runtime::current_game_session);
    ASSERT_NE(nullptr, server);
    for (auto& uptr : server->world().oblist)
        if (walker* wk = uptr.get(); wk != nullptr && wk->user() != -1)
        {
            wk->set_act_type(ACT_RANDOM);
            // Camera armor: when the lead falls, the display rebinds
            // control and the viewport has been seen drifting onto the
            // map-border bricks. The camera walker survives the scene;
            // the escort remains mortal (their deaths are the story).
            wk->stats()->set_max_hitpoints(30000.0f);
            wk->stats()->set_hitpoints(30000.0f);
        }
    // Roster guys carry default names ("SOLDIER", family names), and on a
    // SAVE_ALL level ANY named team-0 death is an EndGame loss — the Forest
    // Road film cut at tick ~150 when an escort fell. Strip the bit so the
    // scene runs its scheduled length (server first; the mirror re-syncs
    // type from the authoritative snapshots).
    server->world().type = static_cast<char>(
        server->world().type & ~SCEN_TYPE_SAVE_ALL);
    s->world().type = static_cast<char>(
        s->world().type & ~SCEN_TYPE_SAVE_ALL);
    // The Bearer is story cargo, parked far from the escort: the road
    // pickets can wear the level-3 thief down while the camera is with the
    // fighting, and "THE BEARER DIED" in the kill feed rather spoils the
    // campaign's premise. Film-armor him.
    for (auto& uptr : server->world().oblist)
        if (walker* wk = uptr.get();
            wk != nullptr && wk->stats() != nullptr &&
            wk->stats()->name == "The Bearer")
        {
            wk->stats()->set_max_hitpoints(30000.0f);
            wk->stats()->set_hitpoints(30000.0f);
        }

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int f = 0; f < frames; f++)
    {
        if (game_frame_with_result(*s, st, deps) != GameFrameResult::Continue)
            break;
        s->redraw();
        s->swap();
        dump_viewport(s, name, f);
        if (::testing::Test::HasFatalFailure())
            break;
    }
    s->world().end = 0;
    s->world().delete_objects();
}

} // namespace gameplay_rec

TEST(GameLoop, zz_capture_westlands)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // 2 The Forest Road — THE FLIGHT: rain over the corridor maze, the
    // Pale Riders waking behind the crew in waves (250/550/900). The
    // camera rides with the thief; the Bearer runs in the column with him
    // (Wave E1). 700 frames so the film reaches past the tick-500 wave —
    // a 420-frame cut ended before any pursuit orc woke, so the card
    // structurally could not show the chase (forest-pathing RCA note).
    if (want("2"))
        gameplay_rec::record_story(
            "westlands_flight", 2, WeatherKind::Rain,
            {FAMILY_THIEF, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_BARBARIAN, FAMILY_CLERIC, FAMILY_MAGE},
            6, 700);
    // 19 The Dead Marshes: the new MARSH tiles (shimmer + ripples) with
    // ghost-lights drifting off every mere at the firm-shelf line.
    if (want("19"))
        gameplay_rec::record_story(
            "westlands_marshes", 19, WeatherKind::Clouds,
            {FAMILY_THIEF, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_BARBARIAN, FAMILY_CLERIC, FAMILY_MAGE},
            6, 360);
}

// Wave E7 decor-ambience spot checks: short forced-camera establishing
// shots of the four representative dressings — marsh bones on the drowned
// battlefield, cinder grit + the march's dead on the ash, war-road pebbles
// and trampled-swathe bones on the plains, and forest-shrub undergrowth in
// the Golden Wood. Each scene is a 20-frame hold so sprites settle; the
// point is to LOOK at the frames.
TEST(GameLoop, zz_capture_westlands_decor)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    using epic_rec::Key;
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    const std::vector<int> crew = {FAMILY_SOLDIER, FAMILY_SOLDIER,
                                   FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF,
                                   FAMILY_CLERIC, FAMILY_MAGE, FAMILY_THIEF};
    if (want("d19")) // bog island I1, the sunken dead among the meres
        epic_rec::record("decor_19_marshes", 19, WeatherKind::Clouds, crew,
                         {{0, 288, 272, 0, true}, {20, 288, 272, 0, true}});
    if (want("d23")) // the central lane: banner torches, grit, old bones
        epic_rec::record("decor_23_ash", 23, WeatherKind::None, crew,
                         {{0, 720, 320, 0, true}, {20, 720, 320, 0, true}});
    if (want("d13")) // the war-road through the column's trampled swathes
        epic_rec::record("decor_13_plains", 13, WeatherKind::Clouds, crew,
                         {{0, 672, 384, 0, true}, {20, 672, 384, 0, true}});
    if (want("d10")) // the west road under the eaves, undergrowth off it
        epic_rec::record("decor_10_wood", 10, WeatherKind::Clouds, crew,
                         {{0, 320, 320, 0, true}, {20, 320, 320, 0, true}});
}

// ---------------------------------------------------------------------------
// The Long Season (org.openglad.longseason): four scenes from the Brass
// Kettle Company's year. Ferry Right is real gameplay (the camera rides the
// lead soldier plugging the causeway mouth); the other three are keyframed
// spectator films per the epic_rec pattern. Keyframe pixel anchors come
// straight from the tools/longseason_mapgen builders (tile * 16).
// ---------------------------------------------------------------------------
TEST(GameLoop, zz_capture_longseason)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    using epic_rec::Key;
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    const char* const ls = "org.openglad.longseason";
    // 2 The Ferry Right — spring flood, rain over the drowned river. Real
    // gameplay: the company holds the causeway's east mouth so the toll
    // runs. Knifemen open on the span; the boat wave beaches on the north
    // shallows at tick 400 and hits the landing's rear rank on camera.
    if (want("ls_2"))
        gameplay_rec::record_story(
            "ls_2", 2, WeatherKind::Rain,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ELF,
             FAMILY_BARBARIAN, FAMILY_THIEF, FAMILY_CLERIC, FAMILY_MAGE},
            4, 460, ls);
    // 14 The Long Toll — the Grey Tolls fort in winter, forced blizzard.
    // Establish the courtyard (braziers, the strongroom, the watch), follow
    // the wolf probe, cut to the west mouth for wave 1's wake flash (300),
    // ride that fight up the road, cut east for the 700 wave, then back to
    // the fort for the hold.
    if (want("ls_14"))
        epic_rec::record("ls_14", 14, WeatherKind::Snow,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN,
             FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF, FAMILY_CLERIC,
             FAMILY_MAGE},
            {{0, 480, 480, 0, true}, {60, 480, 480, 0, false},
             {270, 64, 464, 0, true}, {340, 300, 470, 0, false},
             {660, 912, 464, 0, true}, {730, 700, 460, 0, false},
             {860, 480, 480, 0, false}, {960, 480, 480, 0, false}},
            ls);
    // 17 Ashfall Gate — the creditors' camps on the ash plain. Open on the
    // company's wedge under the banner, tour the three camps (palisades,
    // cook-fires, the tent and tower trickles), hold the gate throat's
    // golem wards and slag runnels, then settle on the wagon-corridor spine
    // fight; at 660 cut back west onto the north cut-purse trio (tiles
    // 10-14, y 6-13) for their tick-700 wake flash behind the crew's line.
    if (want("ls_17"))
        epic_rec::record("ls_17", 17, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_MAGE, FAMILY_CLERIC, FAMILY_BARBARIAN},
            {{0, 96, 336, 0, true}, {60, 352, 128, 0, true},
             {130, 352, 544, 0, true}, {200, 976, 336, 0, true},
             {270, 640, 336, 0, false}, {430, 640, 336, 0, false},
             {660, 192, 160, 0, true}, {760, 400, 340, 0, false},
             {860, 640, 336, 0, false}, {940, 640, 336, 0, false}},
            ls);
    // 18 The Warm Mint — the keyframed three-floor foundry climb. Floor 0:
    // the gatehall door war (Kettle holds the west wall at tile 3,21), the
    // casting floor's lava channels, the golem-warded stair chamber. Cut UP
    // at the aligned stair (57,21): the vault floor's collapse holes, melt
    // pools and warded heaps, then the counting rooms. Cut UP again (9,21):
    // the crucible lake, the rim elementals and ghosts, and The Founder on
    // the dais beside the master ledger — brazier-lit end to end.
    if (want("ls_18"))
        epic_rec::record("ls_18", 18, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN, FAMILY_ELF,
             FAMILY_ELF, FAMILY_CLERIC, FAMILY_MAGE, FAMILY_ARCHMAGE},
            {{0, 96, 336, 0, true}, {70, 96, 336, 0, false},
             {200, 560, 336, 0, true}, {270, 560, 336, 0, false},
             {430, 912, 336, 0, true}, {500, 912, 336, 1, true},
             {570, 560, 320, 1, true}, {640, 560, 320, 1, false},
             {710, 144, 336, 1, true}, {780, 144, 336, 2, true},
             {850, 504, 344, 2, true}, {920, 832, 336, 2, true},
             {990, 832, 336, 2, false}, {1050, 832, 336, 2, false}},
            ls);
}

// ---------------------------------------------------------------------------
// Classic respawn e2e (difficulty submenu "Respawns: Heroes"): on a real
// level driven through the full local-transport game loop (authoritative
// server sim + display mirror), a hero killed through the sim damage path
// must schedule a classic respawn, hold the camera on the pending corpse,
// revive at its recorded level-entry point, and get its player control
// reclaimed by the server and rebound on the display.
// ---------------------------------------------------------------------------

#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/statistics.h>

namespace {

// Install a gameplay context bound to the authoritative server world so
// direct sim calls (attack, setxy) run against the right world/obmap and
// have an event log to write to (the shadow's own server context is
// installed only inside finish_tick).
struct ScopedServerWorldContext
{
    GameplayContext context;
    GameplayContext* previous;
    og::sim::SimEventLog events;

    explicit ScopedServerWorldContext(screen& server_screen)
        : previous(current_game)
    {
        context.world = &server_screen.world();
        context.save = &server_screen.save_data;
        context.sim_events = &events;
        current_game = &context;
    }
    ~ScopedServerWorldContext() { current_game = previous; }

    ScopedServerWorldContext(const ScopedServerWorldContext&) = delete;
    ScopedServerWorldContext& operator=(const ScopedServerWorldContext&) = delete;
};

walker* find_server_hero(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->myguy != nullptr)
        {
            return w;
        }
    }
    return nullptr;
}

walker* find_server_enemy(GameWorld& world, unsigned char hero_team)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->team_num() != hero_team && w->myguy == nullptr)
        {
            return w;
        }
    }
    return nullptr;
}

} // namespace

TEST(GameLoop, classic_respawn_e2e_revives_hero_at_entry_point_and_reclaims_control)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.allied_mode = 1;
    save.my_team = 0;
    save.respawn_mode = 1;       // Respawns: Heroes
    save.ctf_respawn_ticks = 12; // fastest legal delay: quick revive
    auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
    leader->name = "Leader";
    leader->teamnum = 0;
    save.team_list[0] = std::move(leader);
    save.team_size = 1;
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));
    ASSERT_NE(nullptr, game_screen->viewob[0]);

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server_screen);
    GameWorld& server_world = server_screen->world();
    ASSERT_EQ(1, static_cast<int>(server_world.respawn_mode))
        << "SaveData.respawn_mode must reach the authoritative world";

    walker* const hero = find_server_hero(server_world);
    ASSERT_NE(nullptr, hero);
    const std::uint32_t hero_id = hero->entity_id();
    const short entry_x = hero->spawn_x();
    const short entry_y = hero->spawn_y();
    const std::uint8_t entry_floor = hero->spawn_floor();
    ASSERT_GE(entry_x, 0) << "the level deploy must record the entry point";
    ASSERT_EQ(entry_x, hero->xpos())
        << "the recorded entry point is where the hero was deployed";
    ASSERT_EQ(entry_y, hero->ypos());

    walker* const enemy = find_server_enemy(server_world, hero->team_num());
    ASSERT_NE(nullptr, enemy) << "the level must field an enemy living";

    // Kill the hero through the real combat damage path, after moving it off
    // its entry point so the revive-at-entry move is observable.
    {
        ScopedServerWorldContext server_ctx(*server_screen);
        // Make the hero the SOLE living of its team (the solo shape): with a
        // fallback body the player would simply switch bodies on death and
        // the null-control -> revive -> reclaim path would never engage.
        for (const auto& uptr : server_world.oblist)
        {
            walker* const w = uptr.get();
            if (w != nullptr && w != hero && !w->dead() &&
                w->query_order() == Order::Living &&
                w->team_num() == hero->team_num())
            {
                w->set_team_num(enemy->team_num());
            }
        }
        hero->setxy(static_cast<short>(entry_x + 64),
                    static_cast<short>(entry_y + 48));
        enemy->set_damage(500.0f);
        hero->stats()->set_hitpoints(1.0f);
        ASSERT_TRUE(enemy->attack(hero)) << "the attack must land";
        ASSERT_TRUE(hero->dead()) << "sim damage must kill the 1-hp hero";
    }

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;

    // The level must keep running through the death (team-wipe endgame is
    // suppressed while a classic respawn mode is active) until the revive.
    bool saw_pending_corpse_keepalive = false;
    bool revived = false;
    for (int frame = 0; frame < 400 && !revived; ++frame)
    {
        ASSERT_EQ(GameFrameResult::Continue,
                  game_frame_with_result(*game_screen, st, deps))
            << "a pending classic respawn must not end the level (frame "
            << frame << ")";
        walker* const server_hero = server_world.find_by_id(hero_id);
        ASSERT_NE(nullptr, server_hero)
            << "the myguy corpse must survive the dead sweep";
        revived = !server_hero->dead();
        walker* const view_control = game_screen->viewob[0]->control;
        if (!revived && view_control != nullptr && view_control->dead() &&
            view_control->entity_id() == hero_id)
        {
            saw_pending_corpse_keepalive = true;
        }
    }
    ASSERT_TRUE(revived) << "the classic respawn must revive the hero";
    EXPECT_TRUE(saw_pending_corpse_keepalive)
        << "the view must hold on the corpse while the respawn is pending";

    walker* const server_hero = server_world.find_by_id(hero_id);
    ASSERT_NE(nullptr, server_hero);
    ASSERT_EQ(hero, server_hero) << "player revive keeps the same walker";
    EXPECT_EQ(entry_x, server_hero->xpos())
        << "the revive must pull the hero back to its level-entry point";
    EXPECT_EQ(entry_y, server_hero->ypos());
    EXPECT_EQ(static_cast<int>(entry_floor),
              static_cast<int>(server_hero->floor()));
    EXPECT_EQ(server_hero->stats()->max_hitpoints(),
              server_hero->stats()->hitpoints())
        << "the revive restores full hitpoints";

    // Control reclaim: the server rebinds the revived walker by user tag and
    // the ControlChange rebinds the display view to the live hero.
    bool reclaimed = false;
    for (int frame = 0; frame < 60 && !reclaimed; ++frame)
    {
        ASSERT_EQ(GameFrameResult::Continue,
                  game_frame_with_result(*game_screen, st, deps));
        walker* const view_control = game_screen->viewob[0]->control;
        reclaimed = view_control != nullptr && !view_control->dead() &&
            view_control->entity_id() == hero_id;
    }
    ASSERT_TRUE(reclaimed)
        << "player control must be reclaimed after the classic revive";
    EXPECT_EQ(0, static_cast<int>(server_hero->user()))
        << "the revive preserves the player's user tag";

    og::runtime::clear_local_transport_shadow(session);
    game_screen->world().end = 0;
    game_screen->world().delete_objects();
    // SaveData::reset() deliberately leaves the match-rule fields alone (the
    // CTF-trio precedent), so scrub them here — and rewrite save0 — to keep
    // later tests (and other binaries loading save0) on classic behavior.
    save.respawn_mode = 0;
    save.ctf_respawn_ticks = 0;
    game_screen->world().respawn_mode = 0;
    EXPECT_TRUE(save.save("save0"));
}

// A10 pin: dormant (delayed-spawn) allies are absent from snapshots, so if
// the server-side SwitchChar cycle ever hands control to one (bug A1), the
// display mirror cannot resolve the ControlChange id, viewob[0]->control goes
// null, and the whole HUD (name, HP/MP, SPC line, FOES) vanishes. Drive
// switches through the REAL local-transport-shadow stack with a dormant ally
// on the roster and pin that the view control stays live and HUD-eligible on
// every tick.
TEST(GameLoop, switch_char_over_dormant_ally_never_blanks_view_control)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    prepare_dense_allied_alpha_bravo_charlie_save(save);
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(session));
    ASSERT_NE(nullptr, game_screen->viewob[0]);

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server_screen);
    GameWorld& server_world = server_screen->world();

    walker* const alpha = find_named_team_member(server_world, "Alpha");
    walker* const bravo = find_named_team_member(server_world, "Bravo");
    walker* const charlie = find_named_team_member(server_world, "Charlie");
    ASSERT_NE(nullptr, alpha);
    ASSERT_NE(nullptr, bravo);
    ASSERT_NE(nullptr, charlie);
    ASSERT_EQ(0, static_cast<int>(alpha->user())) << "player 0 claims Alpha";
    const std::uint32_t alpha_id = alpha->entity_id();
    const std::uint32_t bravo_id = bravo->entity_id();
    const std::uint32_t charlie_id = charlie->entity_id();

    // Turn Bravo into a delayed-entry (dormant) ally, like the westlands
    // levels author with spawn_delay. Server-world mutation needs the server
    // gameplay context (obmap re-bucketing on set_dormant).
    {
        ScopedServerWorldContext server_ctx(*server_screen);
        bravo->set_spawn_delay(60000);
        bravo->set_dormant(true);
    }

    std::set<std::uint32_t> seen_control_ids;
    const auto drive_tick = [&](bool press_switch) {
        const InputState input =
            press_switch ? make_switch_char_input(0u) : InputState{};
        og::runtime::local_transport_shadow_send_input(
            session, input, game_screen->world().tick_count_ + 1u);
        og::runtime::local_transport_shadow_finish_tick(session);

        walker* const control = game_screen->viewob[0]->control;
        ASSERT_NE(nullptr, control)
            << "the view control (and with it the HUD) must never blank";
        ASSERT_FALSE(control->dormant())
            << "the view must never follow a dormant walker";
        ASSERT_NE(bravo_id, control->entity_id())
            << "the switch cycle must skip the dormant ally";
        ASSERT_NE(-1, static_cast<int>(control->user()))
            << "score-panel HUD gate: control must stay human-claimed on the "
               "switch tick itself (the ControlChange mapping is authoritative; "
               "the user tag must not lag it)";
        ASSERT_FALSE(control->dead());
        seen_control_ids.insert(control->entity_id());
    };

    // A couple of neutral warm-up ticks, then several full switch cycles
    // (press + release per switch).
    for (int i = 0; i < 2; ++i)
        drive_tick(false);
    for (int i = 0; i < 8 && !::testing::Test::HasFatalFailure(); ++i)
    {
        drive_tick(true);
        if (::testing::Test::HasFatalFailure())
            break;
        drive_tick(false);
    }
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    EXPECT_TRUE(seen_control_ids.contains(alpha_id))
        << "the cycle should pass through Alpha";
    EXPECT_TRUE(seen_control_ids.contains(charlie_id))
        << "the cycle should reach the awake ally Charlie";
    EXPECT_FALSE(seen_control_ids.contains(bravo_id));
    EXPECT_TRUE(bravo->dormant())
        << "the delayed ally must still be pending at test end";

    og::runtime::clear_local_transport_shadow(session);
    game_screen->world().end = 0;
    game_screen->world().delete_objects();
}

// ---------------------------------------------------------------------------
// cfg graphics/scale in REAL gameplay: the grown world canvas must carry real
// rendered world (not clipped 320x200 content plus black filler), the panes
// must be laid out from the canvas dims in split-screen, and a mid-game
// window resize must retrack everything. All of it is non-default behavior:
// the guard restores the Legacy classic canvas so the rest of the binary
// keeps the byte-identical default setup.
// ---------------------------------------------------------------------------
#include <openglad/platform/video_sdl.h> // apply_world_scale_from_cfg
#include <openglad/interface/render/view_layout.h>
#include <openglad/interface/render/radar.h>

namespace canvas_scale_gameplay {

// RAII: tear the game down and restore the Legacy classic canvas + UI routing.
struct WorldScaleGameGuard
{
    screen* s;
    explicit WorldScaleGameGuard(screen* scr) : s(scr) {}
    ~WorldScaleGameGuard()
    {
        if (og::runtime::current_game_session)
            og::runtime::clear_local_transport_shadow(
                *og::runtime::current_game_session);
        s->world().end = 0;
        s->world().delete_objects();
        cfg.apply_setting("graphics", "scale", "off");
        apply_world_scale_from_cfg();
        s->set_active_canvas(CanvasTarget::UI);
        s->relayout_views();
    }
};

// Sparse-sampled count of non-black pixels in [x0,x1) x [y0,y1).
int nonblack_samples(screen* s, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y += 8)
        for (int x = x0; x < x1; x += 8)
        {
            Uint8 r = 0, g = 0, b = 0;
            s->get_pixel(x, y, &r, &g, &b);
            if ((r | g | b) != 0)
                n++;
        }
    return n;
}

void run_frames(screen* s, int frames)
{
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int f = 0; f < frames; f++)
        ASSERT_EQ(GameFrameResult::Continue, game_frame_with_result(*s, st, deps));
}

// Pane geometry pinned against compute_view_layout at the CURRENT world
// canvas dims, plus containment inside the canvas.
void expect_pane_matches_layout(screen* s, int i, int canvas_w, int canvas_h)
{
    viewscreen* vs = s->viewob[static_cast<size_t>(i)].get();
    ASSERT_TRUE(vs) << "view " << i;
    const og::view_layout::ViewLayout want = og::view_layout::compute_view_layout(
        s->numviews, vs->mynum, vs->prefs[PREF_VIEW], canvas_w, canvas_h);
    ASSERT_TRUE(want.applies) << "view " << i;
    EXPECT_EQ(want.x, vs->xloc) << "view " << i;
    EXPECT_EQ(want.y, vs->yloc) << "view " << i;
    EXPECT_EQ(want.w, vs->xview) << "view " << i;
    EXPECT_EQ(want.h, vs->yview) << "view " << i;
    EXPECT_GE(vs->xloc, 0) << "view " << i;
    EXPECT_GE(vs->yloc, 0) << "view " << i;
    EXPECT_LE(vs->endx, canvas_w) << "view " << i;
    EXPECT_LE(vs->endy, canvas_h) << "view " << i;
}

// The fixed 60x44 radar block must land inside its pane at any pane size.
void expect_radar_inside_pane(viewscreen* vs)
{
    vs->myradar->start();
    EXPECT_GE(vs->myradar->xloc, vs->xloc);
    EXPECT_GE(vs->myradar->yloc, vs->yloc);
    EXPECT_LE(vs->myradar->xloc + vs->myradar->xview, vs->endx);
    EXPECT_LE(vs->myradar->yloc + vs->myradar->yview, vs->endy);
}

// Full world-canvas frame dump for the visual review flow (P6 PPM), gated on
// OG_FX_CAPTURE_DIR like the other capture scenes.
void dump_canvas(screen* s, const char* scene, int frame)
{
    char dir[512];
    gameplay_rec::dump_dir_for(scene, dir, sizeof(dir));
    char path[544];
    snprintf(path, sizeof(path), "%s/%03d.ppm", dir, frame);
    FILE* fp = fopen(path, "wb");
    ASSERT_NE(nullptr, fp);
    const int w = s->canvas_w();
    const int h = s->canvas_h();
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
        {
            Uint8 r, g, b;
            s->get_pixel(i, j, &r, &g, &b);
            fputc(r, fp);
            fputc(g, fp);
            fputc(b, fp);
        }
    fclose(fp);
}

} // namespace canvas_scale_gameplay

TEST(GameLoop, world_scale_gameplay_draws_real_world_on_the_grown_canvas)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr);

    // scale=1 on the default 640x400 test window: the world canvas IS the
    // window — four times the classic pixel count.
    cfg.apply_setting("graphics", "scale", "1");
    apply_world_scale_from_cfg();
    if (s->world_canvas_w() != 640 || s->world_canvas_h() != 400)
        GTEST_SKIP() << "test window is not the expected default 640x400";
    canvas_scale_gameplay::WorldScaleGameGuard guard(s);

    gameplay_rec::build_save(s, "org.openglad.gladiator", 1, 1,
                             {FAMILY_SOLDIER, FAMILY_ELF, FAMILY_MAGE}, 4);
    glad_init();
    s->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(1, static_cast<int>(s->numviews));
    viewscreen* vs = s->viewob[0].get();
    ASSERT_TRUE(vs != nullptr);
    canvas_scale_gameplay::expect_pane_matches_layout(s, 0, 640, 400);

    // More world visible than the classic canvas would show in the same
    // mode: the pane's world window grew with the canvas (tiles smaller
    // relative to the frame).
    const og::view_layout::ViewLayout classic =
        og::view_layout::compute_view_layout(1, 0, vs->prefs[PREF_VIEW], 320, 200);
    EXPECT_GT(vs->xview, classic.w);
    EXPECT_GT(vs->yview, classic.h);

    canvas_scale_gameplay::run_frames(s, 8);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    score_panel(s);
    s->refresh();
    s->swap();

    // Real rendered world beyond the classic 320x200 area on both axes —
    // right of x=320 and below y=200 (a clipped legacy draw would leave
    // black filler there).
    EXPECT_GT(canvas_scale_gameplay::nonblack_samples(s, 330, 20, 636, 190), 60)
        << "the canvas right of the classic 320px must carry rendered world";
    EXPECT_GT(canvas_scale_gameplay::nonblack_samples(s, 20, 210, 310, 396), 60)
        << "the canvas below the classic 200px must carry rendered world";
    canvas_scale_gameplay::expect_radar_inside_pane(vs);

    // Optional frame dump for the visual review flow.
    if (getenv("OG_FX_CAPTURE_DIR"))
        canvas_scale_gameplay::dump_canvas(s, "worldscale_640", 0);
}

TEST(GameLoop, world_scale_splitscreen_panes_fit_the_grown_canvas)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr);

    cfg.apply_setting("graphics", "scale", "1");
    apply_world_scale_from_cfg();
    if (s->world_canvas_w() != 640 || s->world_canvas_h() != 400)
        GTEST_SKIP() << "test window is not the expected default 640x400";
    canvas_scale_gameplay::WorldScaleGameGuard guard(s);

    // ---- 2 players on the grown canvas ------------------------------------
    gameplay_rec::build_save(s, "org.openglad.gladiator", 2, 2,
                             {FAMILY_SOLDIER, FAMILY_BARBARIAN,
                              FAMILY_ELF, FAMILY_MAGE}, 6);
    glad_init();
    s->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(2, static_cast<int>(s->numviews));
    for (int i = 0; i < 2; i++)
        canvas_scale_gameplay::expect_pane_matches_layout(s, i, 640, 400);
    // The two panes may not overlap (the classic 2px seam scales its position,
    // not its width).
    EXPECT_LE(s->viewob[0]->endx, s->viewob[1]->xloc)
        << "left pane must end before the right pane starts";

    canvas_scale_gameplay::run_frames(s, 8);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    score_panel(s);
    s->refresh();
    s->swap();
    // Both panes carry rendered world; the right pane lives ENTIRELY beyond
    // the classic 320px boundary.
    EXPECT_GT(canvas_scale_gameplay::nonblack_samples(
                  s, s->viewob[0]->xloc + 8, s->viewob[0]->yloc + 8,
                  s->viewob[0]->endx - 8, s->viewob[0]->endy - 8), 60);
    EXPECT_GT(canvas_scale_gameplay::nonblack_samples(
                  s, s->viewob[1]->xloc + 8, s->viewob[1]->yloc + 8,
                  s->viewob[1]->endx - 8, s->viewob[1]->endy - 8), 60);
    canvas_scale_gameplay::expect_radar_inside_pane(s->viewob[0].get());
    canvas_scale_gameplay::expect_radar_inside_pane(s->viewob[1].get());
    if (getenv("OG_FX_CAPTURE_DIR"))
        canvas_scale_gameplay::dump_canvas(s, "worldscale_640_2p", 0);

    // ---- Mid-game window resize during split-screen ------------------------
    SDL_Event ev{};
    ev.type = SDL_WINDOWEVENT;
    ev.window.event = SDL_WINDOWEVENT_RESIZED;
    ev.window.data1 = 1280;
    ev.window.data2 = 800;
    handle_window_event(ev);
    EXPECT_EQ(1280, s->world_canvas_w());
    EXPECT_EQ(800, s->world_canvas_h());
    for (int i = 0; i < 2; i++)
        canvas_scale_gameplay::expect_pane_matches_layout(s, i, 1280, 800);
    canvas_scale_gameplay::run_frames(s, 2);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    s->swap();
    canvas_scale_gameplay::expect_radar_inside_pane(s->viewob[1].get());
    // ...and back down.
    ev.window.data1 = 640;
    ev.window.data2 = 400;
    handle_window_event(ev);
    EXPECT_EQ(640, s->world_canvas_w());
    for (int i = 0; i < 2; i++)
        canvas_scale_gameplay::expect_pane_matches_layout(s, i, 640, 400);

    // ---- 4 players: quadrants on the grown canvas --------------------------
    s->world().end = 0;
    s->world().delete_objects();
    gameplay_rec::build_save(s, "org.openglad.gladiator", 2, 4,
                             {FAMILY_SOLDIER, FAMILY_BARBARIAN,
                              FAMILY_ELF, FAMILY_MAGE}, 6);
    glad_init();
    s->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(4, static_cast<int>(s->numviews));
    for (int i = 0; i < 4; i++)
        canvas_scale_gameplay::expect_pane_matches_layout(s, i, 640, 400);
    // Quadrants are pairwise disjoint.
    for (int a = 0; a < 4; a++)
        for (int b = a + 1; b < 4; b++)
        {
            viewscreen* va = s->viewob[static_cast<size_t>(a)].get();
            viewscreen* vb = s->viewob[static_cast<size_t>(b)].get();
            const bool disjoint = va->endx <= vb->xloc || vb->endx <= va->xloc ||
                                  va->endy <= vb->yloc || vb->endy <= va->yloc;
            EXPECT_TRUE(disjoint) << "panes " << a << " and " << b << " overlap";
        }

    canvas_scale_gameplay::run_frames(s, 4);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    s->swap();
    // The fourth quadrant lives entirely beyond the classic 320x200 corner
    // and still carries rendered world.
    EXPECT_GT(canvas_scale_gameplay::nonblack_samples(
                  s, s->viewob[3]->xloc + 8, s->viewob[3]->yloc + 8,
                  s->viewob[3]->endx - 8, s->viewob[3]->endy - 8), 40);
    for (int i = 0; i < 4; i++)
        canvas_scale_gameplay::expect_radar_inside_pane(
            s->viewob[static_cast<size_t>(i)].get());
    if (getenv("OG_FX_CAPTURE_DIR"))
        canvas_scale_gameplay::dump_canvas(s, "worldscale_640_4p", 0);
}
