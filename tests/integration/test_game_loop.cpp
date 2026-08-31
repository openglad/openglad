#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/sound_ids.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/interface/replay_runtime.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/ui/pause_menu.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/core/test_trace.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/input.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <openglad/core/util.h>
#include "test_save_state_guard.h"

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
    const bool* old_keystates = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake_keystates{};

    SessionKeyStateGuard()
        : old_keystates(og::runtime::current_session->keystates_)
    {
        fake_keystates.fill(false);
        og::runtime::current_session->keystates_ = fake_keystates.data();
    }

    ~SessionKeyStateGuard()
    {
        og::runtime::current_session->keystates_ = old_keystates;
    }

    void set(SDL_Keycode key, bool pressed)
    {
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(key, nullptr);
        if (scancode >= 0 && scancode < SDL_SCANCODE_COUNT)
            fake_keystates[static_cast<std::size_t>(scancode)] = pressed;
    }
};

// Restores every walker's dead bit on scope exit. A test that stages a
// wiped-out world must put the world back exactly as it found it: the
// blanket set_dead(0) this replaces also RESURRECTED bodies that were
// already corpses when the scope opened.
struct DeadBitGuard
{
    std::vector<std::pair<walker*, short>> saved;

    explicit DeadBitGuard(GameWorld& world)
    {
        for (const auto& uptr : world.oblist)
        {
            if (uptr)
                saved.emplace_back(uptr.get(), uptr->dead());
        }
    }

    ~DeadBitGuard()
    {
        for (const auto& [body, was_dead] : saved)
            body->set_dead(was_dead);
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
    const int framecount_before = static_cast<int>(game_screen->framecount);
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
    save.current_campaign = "gladiator";
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

static std::vector<short> start_marker_teams_at(const GameWorld& world,
                                                const walker& hero)
{
    std::vector<short> teams;
    for (const auto& uptr : world.oblist)
    {
        const walker* const marker = uptr.get();
        if (marker == nullptr || marker->query_order() != Order::Special ||
            marker->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        if (marker->floor() == hero.floor() &&
            marker->xpos() == hero.xpos() && marker->ypos() == hero.ypos())
        {
            teams.push_back(marker->team_num());
        }
    }
    return teams;
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
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_F11;
    script.events.push_back(e);
    e.key.key = SDLK_F12;
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
    game_screen->save_data.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 3;
    ASSERT_TRUE(game_screen->save_data.save("save0"));
    EXPECT_EQ(3, static_cast<int>(game_screen->save_data.numplayers));

    const std::filesystem::path save_path =
        std::filesystem::path(get_user_path()) / "save" / "save0.gtl";
    std::ifstream save_file(save_path, std::ios::binary);
    ASSERT_TRUE(save_file.is_open());
    save_file.seekg(132);
    char legacy_player_count = 0;
    ASSERT_TRUE(save_file.read(&legacy_player_count, 1));
    EXPECT_EQ(1, static_cast<unsigned char>(legacy_player_count))
        << "the GTL carries only the compatibility marker, not the live count";

    game_screen->ready_for_battle(1);
    ASSERT_EQ(1, game_screen->numviews);

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    EXPECT_EQ(3u,
              og::runtime::local_transport_client_count(gameplay_session));

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    EXPECT_NE(game_screen, server_screen)
        << "the authority must use its own freshly loaded screen";
    EXPECT_EQ(3, static_cast<int>(server_screen->save_data.numplayers));
    EXPECT_EQ(3, server_screen->numviews);
    for (int view_index = 0; view_index < 3; ++view_index)
    {
        EXPECT_NE(nullptr, server_screen->viewob[view_index])
            << "missing authoritative view " << view_index;
    }
    for (int view_index = 3; view_index < MAX_VIEWS; ++view_index)
    {
        EXPECT_EQ(nullptr, server_screen->viewob[view_index])
            << "unexpected authoritative view " << view_index;
    }

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
}

// #207: the shadow install must seed the DISPLAY save's replay arm into the
// authoritative server screen BEFORE that screen's own load_saved_game —
// the arm is transient session state the disk round-trip drops, and the
// server world's completed-level purge reads it. Without the seed the
// display shows a restored level while the authority simulates an empty
// one.
TEST(GameLoop, local_transport_shadow_seeds_replay_arm_into_server_screen)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels["gladiator"] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.add_level_completed("gladiator", 1);
    ASSERT_TRUE(save.save("save0"));

    save.arm_replay(1);
    ASSERT_NE(0, load_saved_game("save0", game_screen));
    ASSERT_TRUE(save.replay_armed_for(1))
        << "the display load must carry the arm (its own pinned behavior)";

    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    og::runtime::reset_local_transport_shadow(gameplay_session, *game_screen);
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    EXPECT_EQ(1, static_cast<int>(server_screen->save_data.replay_level))
        << "the install must seed the arm before the authoritative load";
    EXPECT_EQ(1, static_cast<int>(server_screen->save_data.replay_origin));

    // And the seed did its job: the authoritative world loaded RESTORED.
    int server_hostiles = 0;
    for (auto& uptr : server_screen->world().oblist)
    {
        walker* const w = uptr.get();
        if (w == nullptr || w->dead())
            continue;
        if (w->query_order() == Order::Living && w->team_num() != 0 &&
            w->myguy == nullptr)
            ++server_hostiles;
    }
    EXPECT_EQ(12, server_hostiles)
        << "the authoritative purge must skip for the armed level";

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
    save.reset();
}

namespace {

// One-soldier lobby inputs for `scen_num`, the minimal stageable key.
og::server::MatchStageInputs make_gladiator_stage_inputs(std::int16_t scen_num)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = 100;
    character.name = "Stage Soldier";
    character.family = FAMILY_SOLDIER;
    character.strength = 10;
    character.dexterity = 11;
    character.constitution = 12;
    character.intelligence = 13;
    character.armor = 14;
    character.level = 3;
    character.teamnum = 0;

    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "gladiator";
    inputs.equivalent.scen_num = scen_num;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.allied_mode = 0;
    inputs.equivalent.team_list = {
        og::sim::LobbyCharacterSlot{.slot_index = 0u, .character = character},
    };
    inputs.difficulty = 1;
    inputs.match_seed = 1234u;
    return inputs;
}

bool world_has_living_named(const GameWorld& world, const std::string& name)
{
    for (const auto& entry : world.oblist)
    {
        const walker* const entity = entry.get();
        if (entity == nullptr || entity->dead() ||
            entity->query_order() != Order::Living)
            continue;
        if (entity->myguy != nullptr && entity->myguy->name == name)
            return true;
        if (entity->stats() != nullptr && entity->stats()->name == name)
            return true;
    }
    return false;
}

// Load `scen_num` into the display screen through the save0 round-trip.
void load_display_level_for_stage_test(screen& game_screen,
                                       std::int16_t scen_num)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SaveData& save = game_screen.save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels["gladiator"] = scen_num;
    save.scen_num = scen_num;
    save.numplayers = 1;
    ASSERT_TRUE(save.save("save0"));
    ASSERT_NE(0, load_saved_game("save0", &game_screen));
}

} // namespace

// #218 web-jitter regression: GO can consume a start config that names
// a DIFFERENT level than the staged world (the emscripten jitter-capture
// profile overrides scen_num AFTER the lobby staged). Adopting that stage is
// a guaranteed soft-lock — the display runs one map, the authority another,
// every full-grid keyframe is rejected (size mismatch) and the client
// fatally desyncs into the "Connection Lost" popup. The install must detect
// the identity mismatch, dispose the stage, and take the legacy
// display-seed path, which cannot diverge.
TEST(GameLoop, local_transport_shadow_rejects_stage_for_different_level)
{
    trace_clear();
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    load_display_level_for_stage_test(*game_screen, 2);

    // The lobby staged scen 1; the launch config (already applied to the
    // display save above) names scen 2.
    og::server::MatchStageConfig stage_config;
    stage_config.networked = false;
    og::server::MatchStage stage(stage_config);
    stage.observe_inputs(make_gladiator_stage_inputs(1),
                         og::server::stage_clock_now_ms());
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());

    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    og::runtime::reset_local_transport_shadow(
        gameplay_session, *game_screen, &stage);
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));

    EXPECT_TRUE(trace_contains("net", "stage_level_mismatch"))
        << "the install must refuse a stage for a different level";
    EXPECT_EQ(og::server::StageStatus::Empty, stage.status())
        << "the mismatched stage must be disposed, not left staged";

    // The fallback seeded the authoritative world FROM the display: the two
    // sides run the same map, so keyframes apply instead of desyncing.
    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    EXPECT_EQ(2, static_cast<int>(server_screen->save_data.scen_num));
    EXPECT_EQ(game_screen->world().grid.w, server_screen->world().grid.w);
    EXPECT_EQ(game_screen->world().grid.h, server_screen->world().grid.h);
    EXPECT_FALSE(
        world_has_living_named(server_screen->world(), "Stage Soldier"))
        << "no half-adopted roster: the fallback world is display-seeded";

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
    game_screen->save_data.reset();
}

// Companion pin: a stage for the SAME level the display loaded still adopts
// (the guard must never push a healthy launch off the staged path).
TEST(GameLoop, local_transport_shadow_adopts_stage_for_matching_level)
{
    trace_clear();
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    load_display_level_for_stage_test(*game_screen, 1);

    og::server::MatchStageConfig stage_config;
    stage_config.networked = false;
    og::server::MatchStage stage(stage_config);
    stage.observe_inputs(make_gladiator_stage_inputs(1),
                         og::server::stage_clock_now_ms());
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    const GameWorld* const staged_world = stage.world();
    ASSERT_NE(nullptr, staged_world);
    ASSERT_TRUE(world_has_living_named(*staged_world, "Stage Soldier"));

    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    og::runtime::reset_local_transport_shadow(
        gameplay_session, *game_screen, &stage);
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));

    EXPECT_FALSE(trace_contains("net", "stage_level_mismatch"))
        << "a matching stage must not trip the mismatch guard";
    EXPECT_EQ(og::server::StageStatus::Empty, stage.status())
        << "adoption consumes the stage";

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    EXPECT_TRUE(
        world_has_living_named(server_screen->world(), "Stage Soldier"))
        << "the authoritative world is the adopted staged world (it carries "
           "the lobby roster's spawn, which the display-seed path cannot)";

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
    game_screen->save_data.reset();
}

TEST(GameLoop, glad_init_applies_lobby_start_config_before_level_load)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
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
    EXPECT_EQ("gladiator", lobby_config->save_data.current_campaign);
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
    EXPECT_EQ("gladiator", game_screen->save_data.current_campaign);
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.scen_num));
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.numplayers));
    EXPECT_EQ(0, static_cast<int>(game_screen->save_data.allied_mode));
    EXPECT_EQ(0, static_cast<int>(game_screen->save_data.my_team));
    EXPECT_EQ("gladiator", get_mounted_campaign());
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

TEST(GameLoop, local_lobby_spawns_every_deployed_team_and_abort_preserves_company)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.save_name = "Four Teams";
    save.current_campaign = "gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.my_team = 0;

    constexpr std::array<std::string_view, 8> names = {
        "Red Active", "Red Benched", "Yellow One", "Yellow Two",
        "Green One", "Green Two", "Blue One", "Blue Two",
    };
    constexpr std::array<short, 8> teams = {0, 0, 1, 1, 2, 2, 3, 3};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        save.team_list[index] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[index]->name = names[index];
        save.team_list[index]->teamnum = teams[index];
        save.team_list[index]->deployed = index != 1;
    }
    save.team_size = static_cast<unsigned char>(names.size());
    ASSERT_TRUE(save.save(og::data::active_company_slot()));

    picker_lobby_shutdown();
    picker_lobby_initialize_from_save();
    ASSERT_TRUE(picker_lobby_request_start());
    std::optional<og::ui::PickerLobbyGameStartConfig> config =
        picker_lobby_consume_game_start_config();
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(names.size(), config->save_data.team_list.size());
    EXPECT_EQ("Red Active", config->save_data.team_list[0].character.name);
    EXPECT_TRUE(config->save_data.team_list[0].deployed);
    EXPECT_EQ("Red Benched", config->save_data.team_list[1].character.name);
    EXPECT_FALSE(config->save_data.team_list[1].deployed);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        EXPECT_EQ(names[index],
                  config->save_data.team_list[index].character.name);
        EXPECT_EQ(teams[index],
                  config->save_data.team_list[index].character.teamnum);
    }
    picker_lobby_shutdown();

    ready_screen_for_game_start(*game_screen, &*config);
    glad_init(false, &*config);
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    EXPECT_TRUE(og::runtime::current_session->isolated_company_session_);

    std::set<std::string> launched_company_members;
    for (const auto& entity : game_screen->world().oblist)
    {
        if (entity != nullptr && entity->myguy != nullptr)
            launched_company_members.insert(entity->myguy->name);
    }
    EXPECT_EQ(7u, launched_company_members.size());
    EXPECT_TRUE(launched_company_members.contains("Red Active"));
    EXPECT_FALSE(launched_company_members.contains("Red Benched"));
    for (std::size_t index = 2; index < names.size(); ++index)
        EXPECT_TRUE(launched_company_members.contains(std::string(names[index])))
            << names[index] << " must spawn even without a player seat on its team";

    SaveData still_private_after_launch;
    ASSERT_EQ(SaveDataIoError::None,
              still_private_after_launch.load_with_error(
                  og::data::active_company_slot()));
    ASSERT_EQ(names.size(), still_private_after_launch.team_size);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        ASSERT_NE(nullptr, still_private_after_launch.team_list[index]);
        EXPECT_EQ(names[index], still_private_after_launch.team_list[index]->name);
        EXPECT_EQ(teams[index], still_private_after_launch.team_list[index]->teamnum);
        EXPECT_EQ(index != 1,
                  still_private_after_launch.team_list[index]->deployed);
    }

    ASSERT_TRUE(og::runtime::local_transport_shadow_abort_level(
        *og::runtime::current_game_session));
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    ASSERT_EQ(SaveDataIoError::None,
              game_screen->save_data.load_with_error(
                  og::data::active_company_slot()));
    ASSERT_EQ(names.size(), game_screen->save_data.team_size);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        ASSERT_NE(nullptr, game_screen->save_data.team_list[index]);
        EXPECT_EQ(names[index], game_screen->save_data.team_list[index]->name);
        EXPECT_EQ(teams[index], game_screen->save_data.team_list[index]->teamnum);
        EXPECT_EQ(index != 1, game_screen->save_data.team_list[index]->deployed);
    }
    EXPECT_FALSE(og::runtime::current_session->isolated_company_session_);
    game_screen->world().delete_objects();
}

TEST(GameLoop, local_two_player_ally_mode_claims_two_team_one_heroes)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);
    if (og::runtime::current_game_session != nullptr)
        og::runtime::clear_local_transport_shadow(
            *og::runtime::current_game_session);
    game_screen->world().delete_objects();

    SaveData& save = game_screen->save_data;
    save.reset();
    save.save_name = "Ally Seats";
    save.current_campaign = "gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 2;
    save.allied_mode = 1;
    save.my_team = 0;
    // The hostile-color hero is deliberately between the two Team 1 heroes.
    // Roster order must never make Player 2 claim it.
    save.team_list[0] = make_named_soldier("Red One", 0);
    save.team_list[1] = make_named_soldier("Yellow Mid", 1);
    save.team_list[2] = make_named_soldier("Red Two", 0);
    save.team_size = 3;

    picker_lobby_shutdown();
    picker_lobby_initialize_from_save();
    ASSERT_TRUE(picker_lobby_request_start());
    std::optional<og::ui::PickerLobbyGameStartConfig> config =
        picker_lobby_consume_game_start_config();
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(3u, config->save_data.team_list.size());
    EXPECT_EQ((std::vector<short>{0, 0}), config->local_seat_teams);
    picker_lobby_shutdown();

    ready_screen_for_game_start(*game_screen, &*config);
    glad_init(false, &*config);
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    walker* const yellow = find_named_team_member(
        game_screen->world(), "Yellow Mid", 1);
    ASSERT_NE(nullptr, yellow)
        << "the non-player color still belongs in the mission";
    ASSERT_NE(nullptr, game_screen->viewob[0]);
    ASSERT_NE(nullptr, game_screen->viewob[1]);
    EXPECT_EQ(0, game_screen->viewob[0]->my_team);
    EXPECT_EQ(0, game_screen->viewob[1]->my_team);
    ASSERT_NE(nullptr, game_screen->viewob[0]->control);
    ASSERT_NE(nullptr, game_screen->viewob[1]->control);
    ASSERT_NE(nullptr, game_screen->viewob[0]->control->myguy);
    ASSERT_NE(nullptr, game_screen->viewob[1]->control->myguy);
    EXPECT_NE(game_screen->viewob[0]->control,
              game_screen->viewob[1]->control);
    EXPECT_EQ("Red One", game_screen->viewob[0]->control->myguy->name);
    EXPECT_EQ("Red Two", game_screen->viewob[1]->control->myguy->name);
    EXPECT_NE("Yellow Mid",
              game_screen->viewob[1]->control->myguy->name);
    EXPECT_FALSE(game_screen->viewob[0]->control->is_friendly(yellow))
        << "a yellow company hero must remain attackable by red in Together mode";
    EXPECT_FALSE(yellow->is_friendly(game_screen->viewob[0]->control))
        << "hostility must be symmetric";
    EXPECT_TRUE(game_screen->viewob[0]->control->is_friendly(
        game_screen->viewob[1]->control))
        << "the two red player heroes must remain friendly";

    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

// The local picker lobby's slot builder must pre-clamp guy names to the
// 11-char wire width: the wire readers truncate anyway, and an unclamped
// writer makes every roster message fail the VALIDATE_SERIALIZATION
// round-trip. The consumed game-start config is where the clamp lands.
TEST(GameLoop, local_lobby_clamps_oversized_guy_names_to_wire_width)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    save.reset();
    save.save_name = "Clamp Co";
    save.current_campaign = "gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = 0;
    save.team_list[0] = make_named_soldier("Maximilian Longname", 0);
    save.team_size = 1;

    picker_lobby_shutdown();
    picker_lobby_initialize_from_save();
    ASSERT_TRUE(picker_lobby_request_start());
    std::optional<og::ui::PickerLobbyGameStartConfig> config =
        picker_lobby_consume_game_start_config();
    picker_lobby_shutdown();
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(1u, config->save_data.team_list.size());
    EXPECT_EQ("Maximilian ",
              config->save_data.team_list[0].character.name)
        << "the wire copy of the roster must carry the clamped name";
}

TEST(GameLoop, local_gameplay_spawns_and_controls_every_selected_team_color)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    for (const short allied_mode : {short{0}, short{1}})
    {
        for (short team = 0; team < 4; ++team)
        {
            if (og::runtime::current_game_session != nullptr)
                og::runtime::clear_local_transport_shadow(
                    *og::runtime::current_game_session);
            game_screen->world().delete_objects();

            SaveData& save = game_screen->save_data;
            save.reset();
            save.current_campaign = "gladiator";
            save.current_levels[save.current_campaign] = 1;
            save.scen_num = 1;
            save.numplayers = 1;
            save.allied_mode = allied_mode;
            save.my_team = team;
            save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_list[0]->name = "Color Guard";
            save.team_list[0]->teamnum = team;
            save.team_size = 1;

            og::ui::PickerLobbyGameStartConfig config =
                make_one_view_lobby_start_config(save);
            config.my_team = team;
            config.is_networked = false;

            ready_screen_for_game_start(*game_screen, &config);
            glad_init(false, &config);

            ASSERT_NE(nullptr, og::runtime::current_game_session);
            ASSERT_NE(nullptr, game_screen->save_data.team_list[0]);
            ASSERT_NE(nullptr, game_screen->viewob[0]);
            EXPECT_EQ(team, game_screen->save_data.my_team)
                << "allied=" << allied_mode << " team=" << team;
            EXPECT_EQ(team, game_screen->save_data.team_list[0]->teamnum)
                << "allied=" << allied_mode << " team=" << team;
            EXPECT_EQ(team, game_screen->viewob[0]->my_team)
                << "allied=" << allied_mode << " team=" << team;

            walker* const hero = find_named_team_member(
                game_screen->world(), "Color Guard", team);
            ASSERT_NE(nullptr, hero)
                << "allied=" << allied_mode << " team=" << team;
            const std::vector<short> spawn_marker_teams =
                start_marker_teams_at(game_screen->world(), *hero);
            if (team == 0)
            {
                ASSERT_FALSE(spawn_marker_teams.empty());
            }
            for (const short marker_team : spawn_marker_teams)
                EXPECT_EQ(team, marker_team)
                    << "a hero must never borrow another team's start marker; "
                    << "allied=" << allied_mode << " team=" << team;
            ASSERT_NE(nullptr, game_screen->viewob[0]->control);
            EXPECT_EQ(team, game_screen->viewob[0]->control->team_num())
                << "allied=" << allied_mode << " team=" << team;
        }
    }

    if (og::runtime::current_game_session != nullptr)
        og::runtime::clear_local_transport_shadow(
            *og::runtime::current_game_session);
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
    save.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
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

// Multi-local-player seat mapping: a networked machine's view can follow a
// GLOBAL player index above the per-machine MAX_PLAYERS (e.g. joiner B's one
// seat is player 6 of a 7-player lobby). The widened kMaxGlobalPlayers
// controlled-ids table must resolve those high indices, and an out-of-table
// index must fall back instead of reading out of bounds.
TEST(GameLoop, select_control_for_view_resolves_high_global_player_indices)
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

    walker* const seat_control = make_player(FAMILY_SOLDIER, 0);
    walker* const other_control = make_player(FAMILY_ARCHER, 0);
    ASSERT_NE(nullptr, seat_control);
    ASSERT_NE(nullptr, other_control);

    viewscreen* const view = game_screen->viewob[0].get();
    view->my_team = 0;

    og::sim::ControlledEntityIds controlled_entity_ids = {};
    controlled_entity_ids[0] = other_control->entity_id();
    controlled_entity_ids[6] = seat_control->entity_id();
    controlled_entity_ids[og::sim::kMaxGlobalPlayers - 1] =
        other_control->entity_id();

    // A seat following global player 6 resolves through the widened table.
    EXPECT_EQ(seat_control,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 6u));
    // The last valid global index still resolves.
    EXPECT_EQ(other_control,
              og::runtime::detail::select_control_for_view(
                  view,
                  controlled_entity_ids,
                  &world,
                  static_cast<std::size_t>(og::sim::kMaxGlobalPlayers - 1)));
    // An index past the table never reads out of bounds; it falls back to
    // the same-team controlled-ids scan (first mapped team walker: slot 0).
    EXPECT_EQ(other_control,
              og::runtime::detail::select_control_for_view(
                  view,
                  controlled_entity_ids,
                  &world,
                  static_cast<std::size_t>(og::sim::kMaxGlobalPlayers)));

    world.delete_objects();
}

// Playtest bug A (display side): while a mode respawn is pending the mapped id
// is 0, but the view must keep following the dead corpse so the camera holds
// and the RESPAWN IN countdown renders. A nonzero mapped id always wins, an
// already-revived walker wearing this player's user tag is kept through the
// one-tick reclaim window, and a corpse whose pending entry disappeared falls
// back to nullptr.
TEST(GameLoop, select_control_for_view_keeps_pending_mode_respawn_corpse)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(game_screen->viewob[0] != nullptr);

    GameWorld& world = game_screen->world();
    world.delete_objects();
    const char saved_type = world.type;
    world.type |= GameWorld::TYPE_SCRIPTED;
    world.mode = og::sim::ModeState{};
    world.mode.active = true;
    world.mode.init_attempted = true;

    walker* const corpse = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const other = world.add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, corpse);
    ASSERT_NE(nullptr, other);
    corpse->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    corpse->set_team_num(0);
    corpse->set_user(0);
    corpse->set_dead(1);
    other->set_team_num(3); // off the view's team: no same-team fallback hits

    og::sim::RespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60;
    entry.walker_entity_id = corpse->entity_id();
    world.respawn.respawn_queue.push_back(entry);

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
    world.respawn.respawn_queue.clear();
    EXPECT_EQ(corpse,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    // Dead again with NO pending entry (live-duplicate cancel): falls back
    // to nullptr.
    corpse->set_dead(1);
    EXPECT_EQ(nullptr,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    // Outside an active scripted match the keep-alive never engages.
    world.respawn.respawn_queue.push_back(entry);
    world.mode.active = false;
    EXPECT_EQ(nullptr,
              og::runtime::detail::select_control_for_view(
                  view, controlled_entity_ids, &world, 0u));

    view->control = saved_control;
    world.mode = og::sim::ModeState{};
    world.type = saved_type;
    world.delete_objects();
}

// §4.5 follow camera, detail level: engagement when the seat maps to entity
// 0, the lowest-player default target, cycling through the shared og::sim
// selectors (preferred targets first, any-living fallback), dead-target
// auto-advance, live-mapped disengage — and the [NET-R6] rule that an
// engaged view NEVER stamps user tags locally (select_control_for_view
// returns the target before the mapped-entity stamp).
TEST(GameLoop, display_view_follow_engages_cycles_and_never_stamps_user_tags)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(game_screen->viewob[0] != nullptr);

    GameWorld& world = game_screen->world();
    world.delete_objects();
    const char saved_type = world.type;

    auto make_hero = [&world](int family, short team, int user) {
        walker* const actor = world.add_ob(Order::Living, family);
        actor->set_owned_myguy(std::make_unique<guy>(family));
        actor->myguy->teamnum = team;
        actor->set_team_num(static_cast<unsigned char>(team));
        actor->set_real_team_num(255);
        actor->set_user(static_cast<signed char>(user));
        return actor;
    };

    walker* const hero_a = make_hero(FAMILY_SOLDIER, 0, 0);
    walker* const hero_b = make_hero(FAMILY_ARCHER, 0, 1);
    walker* const hero_c = make_hero(FAMILY_ELF, 0, -1); // roster-owned, unclaimed
    walker* const troop = world.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, hero_a);
    ASSERT_NE(nullptr, hero_b);
    ASSERT_NE(nullptr, hero_c);
    ASSERT_NE(nullptr, troop);
    troop->set_team_num(1);
    troop->set_user(-1);

    viewscreen* const view = game_screen->viewob[0].get();
    walker* const saved_control = view->control;
    view->my_team = 0;
    view->control = nullptr;

    og::sim::ControlledEntityIds ids = {};
    ids[0] = hero_a->entity_id();
    ids[1] = hero_b->entity_id();

    // Seat = global player 3, mapped to entity 0: engagement + the
    // lowest-player-index default target.
    og::runtime::DisplayFollowState follow;
    og::runtime::detail::update_display_view_follow(follow, view, 3u, ids, &world);
    EXPECT_TRUE(follow.engaged);
    EXPECT_EQ(hero_a->entity_id(), follow.target_entity_id);
    EXPECT_EQ(hero_a,
              og::runtime::detail::select_control_for_view(
                  view, ids, &world, 3u, &follow));
    EXPECT_EQ(0, static_cast<int>(hero_a->user()))
        << "[NET-R6] the follow path must not restamp the watched hero with "
           "the follower's player tag";

    // SwitchChar cycling: preferred targets (user tag or roster-owned) in
    // oblist order, both directions; the anonymous troop is skipped.
    EXPECT_EQ(hero_b->entity_id(),
              og::sim::next_follow_target_id(world, hero_a, false));
    EXPECT_EQ(hero_c->entity_id(),
              og::sim::next_follow_target_id(world, hero_b, false));
    EXPECT_EQ(hero_a->entity_id(),
              og::sim::next_follow_target_id(world, hero_c, false));
    EXPECT_EQ(hero_c->entity_id(),
              og::sim::next_follow_target_id(world, hero_a, true));

    // A selected foreign hero stays selected through its respawn window.
    // The view points at the watched body, but that must not be mistaken for
    // this spectator seat's own retained corpse and clear follow state.
    world.type |= GameWorld::TYPE_SCRIPTED;
    world.mode.active = true;
    world.mode.init_attempted = true;
    view->control = hero_a;
    hero_a->set_dead(1);
    og::sim::RespawnEntry respawn;
    respawn.kind = 0;
    respawn.team = 0;
    respawn.ticks_left = 30;
    respawn.walker_entity_id = hero_a->entity_id();
    world.respawn.respawn_queue.push_back(respawn);
    og::runtime::detail::update_display_view_follow(
        follow, view, 3u, ids, &world);
    EXPECT_TRUE(follow.engaged);
    EXPECT_EQ(hero_a->entity_id(), follow.target_entity_id);
    EXPECT_EQ(nullptr, og::runtime::detail::select_control_for_view(
                           view, ids, &world, 3u, &follow));
    hero_a->set_dead(0);
    world.respawn.respawn_queue.clear();
    og::runtime::detail::update_display_view_follow(
        follow, view, 3u, ids, &world);
    EXPECT_TRUE(follow.engaged);
    EXPECT_EQ(hero_a->entity_id(), follow.target_entity_id);
    EXPECT_EQ(hero_a, og::runtime::detail::select_control_for_view(
                          view, ids, &world, 3u, &follow));
    view->control = hero_a;

    // Dead-target auto-advance: the maintenance pass re-targets the next
    // preferred walker.
    follow.target_entity_id = hero_c->entity_id();
    hero_c->set_dead(1);
    og::runtime::detail::update_display_view_follow(follow, view, 3u, ids, &world);
    EXPECT_TRUE(follow.engaged);
    EXPECT_EQ(hero_a->entity_id(), follow.target_entity_id);

    // Every preferred target dead: the any-living fallback reaches the
    // anonymous troop, still with no user-tag stamp.
    hero_a->set_dead(1);
    hero_b->set_dead(1);
    ids[0] = 0;
    ids[1] = 0;
    og::runtime::detail::update_display_view_follow(follow, view, 3u, ids, &world);
    EXPECT_TRUE(follow.engaged);
    EXPECT_EQ(troop->entity_id(), follow.target_entity_id);
    EXPECT_EQ(troop,
              og::runtime::detail::select_control_for_view(
                  view, ids, &world, 3u, &follow));
    EXPECT_EQ(-1, static_cast<int>(troop->user()))
        << "[NET-R6] an AI follow target keeps user() == -1";

    // A live mapped walker disengages, and the normal mapped path (with its
    // deliberate one-tick tag stamp) resumes.
    ids[3] = troop->entity_id();
    og::runtime::detail::update_display_view_follow(follow, view, 3u, ids, &world);
    EXPECT_FALSE(follow.engaged);
    EXPECT_EQ(troop,
              og::runtime::detail::select_control_for_view(
                  view, ids, &world, 3u, &follow));
    EXPECT_EQ(3, static_cast<int>(troop->user()))
        << "a genuinely mapped seat keeps today's authoritative tag stamp";

    view->control = saved_control;
    world.mode = og::sim::ModeState{};
    world.type = saved_type;
    world.delete_objects();
}

// §4.5 [NET-F1] + true-zero-seat regression: a networked spectator machine's
// empty seat vector means NO local player binding even when remote player 0
// has a live control. Its view engages the follow camera, SwitchChar cycles
// the watched target CROSS-TEAM through the networked follow branch (the
// legacy spectator block is my_team-filtered, so this outcome proves the
// gate), and the choice survives a forced full snapshot resync. Without the
// networked shadow the legacy spectator block still owns the key (demo/local
// unchanged).
TEST(GameLoop, networked_zero_seat_joiner_follow_cycles_and_survives_full_resync)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_follow_spectator"));
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    og::runtime::clear_local_transport_shadow(session);

    game_screen->save_data.numplayers = 0; // spectator machine
    GameWorld& world = game_screen->world();
    world.delete_objects();

    auto make_hero = [&world](int family, short team) {
        walker* const actor = world.add_ob(Order::Living, family);
        actor->set_owned_myguy(std::make_unique<guy>(family));
        actor->myguy->teamnum = team;
        actor->set_team_num(static_cast<unsigned char>(team));
        actor->set_real_team_num(255);
        actor->set_user(-1);
        return actor;
    };
    walker* const home_hero = make_hero(FAMILY_SOLDIER, 0);
    walker* const foreign_hero = make_hero(FAMILY_ARCHER, 1);
    walker* const home_troop = world.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, home_hero);
    ASSERT_NE(nullptr, foreign_hero);
    ASSERT_NE(nullptr, home_troop);
    home_troop->set_team_num(0);
    home_troop->set_user(-1);
    const std::uint32_t home_hero_id = home_hero->entity_id();
    const std::uint32_t foreign_hero_id = foreign_hero->entity_id();
    const std::uint32_t home_troop_id = home_troop->entity_id();

    viewscreen* const view = game_screen->viewob[0].get();
    ASSERT_NE(nullptr, view);
    view->my_team = 0;
    view->control = nullptr;
    reset_viewscreen_input_debounce();

    // Networked client shadow over an in-process pair; the test plays the
    // server side by pushing keyframes captured from the display world. The
    // empty vector is the production true-zero-seat joiner install path.
    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    auto client_transport = server_transport->create_client_transport();
    const og::sim::PeerId peer_id = client_transport->local_peer_id();
    session.networked_session_ = true;
    og::runtime::reset_network_client_transport_shadow(
        session,
        *game_screen,
        client_transport,
        peer_id,
        std::vector<og::runtime::LocalSeatBinding>{});
    ASSERT_TRUE(og::runtime::local_transport_active(session));
    ASSERT_TRUE(og::runtime::local_transport_active(*og::runtime::current_session))
        << "the view input path must see the same session as the shadow";

    og::sim::ControlChangeMessage remote_player_zero;
    remote_player_zero.player_index = 0u;
    remote_player_zero.entity_id = home_hero_id;
    server_transport->send_control_change(
        peer_id,
        std::make_shared<og::sim::ControlChangeMessage>(remote_player_zero));

    const auto push_keyframe = [&] {
        server_transport->send_snapshot(
            peer_id,
            std::make_shared<og::sim::WorldSnapshot>(
                og::sim::capture_keyframe_snapshot(game_screen->world())));
        og::runtime::local_transport_shadow_finish_tick(session);
    };

    // First control re-sync: the follow camera engages on the first
    // preferred walker, with no local user-tag stamp.
    push_keyframe();
    ASSERT_TRUE(view->following_);
    EXPECT_EQ(-1, static_cast<int>(view->global_player_index_))
        << "an empty network seat vector must not inherit remote player 0";
    ASSERT_NE(nullptr, view->control);
    EXPECT_EQ(home_hero_id, view->control->entity_id());
    EXPECT_EQ(-1, static_cast<int>(view->control->user()));

    // [NET-F1] positive: the networked follow branch owns SwitchChar and
    // cycles CROSS-TEAM to the foreign hero — the legacy spectator block
    // (team-filtered) could never produce this. gameplay_active_ mirrors the
    // production game loop (glad.cpp) so the shadow guard keeps the local
    // sim input path off, exactly as during networked gameplay.
    const bool saved_gameplay_active = session.gameplay_active_;
    session.gameplay_active_ = true;
    view->process_input(make_switch_char_input(0u));
    ASSERT_NE(nullptr, view->control);
    EXPECT_EQ(foreign_hero_id, view->control->entity_id());
    EXPECT_EQ(-1, static_cast<int>(view->control->user()));

    // Stomp survival: a forced full snapshot resync re-runs the control
    // sync; the runtime-held follow choice survives it.
    push_keyframe();
    ASSERT_TRUE(view->following_);
    ASSERT_NE(nullptr, view->control);
    EXPECT_EQ(foreign_hero_id, view->control->entity_id());
    walker* const foreign_after = game_screen->world().find_by_id(foreign_hero_id);
    ASSERT_NE(nullptr, foreign_after);
    EXPECT_EQ(-1, static_cast<int>(foreign_after->user()))
        << "[NET-R6] the resync must not stamp the watched foreign hero";

    // #223 path 4: a cycle with nowhere to go refuses. The camera keeps the
    // target it had (it used to be blanked onto a static view with no
    // explanation) and THIS view is told why. Two worlds reach the refusal.
    walker* const watched_before = view->control;
    ASSERT_NE(nullptr, watched_before);
    const auto follow_cue_shown = [view] {
        for (const std::string& line : view->textlist)
        {
            if (line == "NO ONE TO FOLLOW")
                return true;
        }
        return false;
    };

    {
        // (a) One survivor, and it is the watched target itself: the cycle
        // resolves back onto the current target rather than dropping to a
        // static camera. That is still a key that did nothing, so it must be
        // voiced — this is the shape a real end-of-level world reaches long
        // before every last body is a corpse.
        DeadBitGuard corpses(game_screen->world());
        for (auto& uptr : game_screen->world().oblist)
        {
            if (uptr && uptr.get() != watched_before)
                uptr->set_dead(1);
        }
        view->clear_text();
        reset_viewscreen_input_debounce();
        view->process_input(make_switch_char_input(0u));
        EXPECT_EQ(watched_before, view->control)
            << "a cycle onto the current target must keep the camera put";
        EXPECT_TRUE(follow_cue_shown())
            << "a cycle that cannot leave its own target must say so";
    }

    // (b) Nobody left alive at all.
    {
        DeadBitGuard corpses(game_screen->world());
        for (auto& uptr : game_screen->world().oblist)
        {
            if (uptr)
                uptr->set_dead(1);
        }
        view->clear_text();
        reset_viewscreen_input_debounce();
        view->process_input(make_switch_char_input(0u));
        EXPECT_EQ(watched_before, view->control)
            << "a refused follow cycle must keep the previous camera target";
        EXPECT_TRUE(follow_cue_shown()) << "a refused follow cycle must say so";
    }
    view->clear_text();

    // [NET-F1] negative: without the networked shadow the legacy spectator
    // block owns the key again and cycles within my_team only (demo/local
    // behavior unchanged).
    og::runtime::clear_local_transport_shadow(session);
    ASSERT_FALSE(session.networked_session_);
    view->following_ = false;
    view->follow_company_.clear();
    walker* const legacy_start = game_screen->world().find_by_id(home_hero_id);
    ASSERT_NE(nullptr, legacy_start);
    view->control = legacy_start;
    reset_viewscreen_input_debounce();
    view->process_input(make_switch_char_input(0u));
    ASSERT_NE(nullptr, view->control);
    EXPECT_EQ(home_troop_id, view->control->entity_id())
        << "the legacy spectator cycle stays on the view's own team";

    session.gameplay_active_ = saved_gameplay_active;
    game_screen->save_data.numplayers = 1;
    game_screen->world().delete_objects();
}

// The host install has the same empty-seat meaning as the joiner install. A
// zero-seat host still owns match authority, but its display must not borrow
// remote player 0 merely because the legacy local-shadow fallback also uses an
// empty vector. An additional unbound remote spectator must receive the real
// InitialSetup/snapshot handshake too; it has no binding that could register it
// by accident. Drive the host shadow with a separate remote P0 so the first
// controlled-id entry is deliberately nonzero.
TEST(GameLoop, networked_zero_seat_host_and_remote_spectator_install)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    prepare_dense_allied_alpha_bravo_charlie_save(save);
    ASSERT_TRUE(save.save("save0"));

    og::ui::PickerLobbyGameStartConfig start_config =
        make_one_view_lobby_start_config(save);
    start_config.is_networked = true;
    start_config.local_player_index = 0xffu;
    start_config.local_player_indices.clear();
    start_config.local_seat_teams.clear();
    start_config.save_data.numplayers = 0;
    for (std::size_t slot = 0;
         slot < start_config.save_data.team_list.size();
         ++slot)
    {
        start_config.save_data.team_list[slot].owner_player_index = 0u;
        start_config.save_data.team_list[slot].owner_save_slot =
            static_cast<std::uint8_t>(slot);
    }

    ready_screen_for_game_start(*game_screen, &start_config);
    glad_init(false, &start_config);
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(gameplay_session.networked_session_);
    ASSERT_EQ(0, static_cast<int>(game_screen->save_data.numplayers));
    ASSERT_NE(nullptr, game_screen->viewob[0]);

    auto server_transport = og::sim::InProcessTransport::create_server();
    auto host_local_client_transport =
        server_transport->create_client_transport();
    auto remote_player_transport =
        server_transport->create_client_transport();
    auto remote_spectator_transport =
        server_transport->create_client_transport();
    og::sim::GameClient remote_spectator(
        *remote_spectator_transport,
        remote_spectator_transport->local_peer_id());
    // The bound remote player must be a LIVE client: the level-start launch
    // gate (#239) holds tick 1 until every seeded client confirms ready, so a
    // binding whose peer never polls would (correctly) freeze the level start
    // until its ready deadline. Production bound peers always poll.
    og::sim::GameClient remote_player(
        *remote_player_transport,
        remote_player_transport->local_peer_id());
    const std::vector<og::sim::LobbyPlayerBinding> player_bindings = {
        og::sim::LobbyPlayerBinding{
            .peer_id = remote_player_transport->local_peer_id(),
            .local_slot = 0u,
            .player_index = 0u,
            .team = 0,
        },
    };

    og::runtime::reset_network_host_transport_shadow(
        gameplay_session,
        *game_screen,
        server_transport,
        host_local_client_transport,
        player_bindings);
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));

    viewscreen* const view = game_screen->viewob[0].get();
    ASSERT_NE(nullptr, view);
    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(gameplay_session);
        remote_player.poll_messages();
        remote_spectator.poll_messages();
        const og::sim::GameClient* const display_client =
            game_screen->render_interpolation_client();
        return display_client != nullptr &&
            display_client->controlled_entity_ids()[0] != 0u &&
            view->following_ && view->control != nullptr &&
            remote_spectator.initial_setup().has_value() &&
            remote_spectator.baseline().has_value();
    })) << "both zero-seat displays should receive the production handoff";

    const og::sim::GameClient* const display_client =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);
    ASSERT_NE(0u, display_client->controlled_entity_ids()[0]);
    EXPECT_EQ(-1, static_cast<int>(view->global_player_index_))
        << "host authority must not manufacture a local seat";
    EXPECT_EQ(display_client->controlled_entity_ids()[0],
              view->control->entity_id());
    EXPECT_TRUE(view->following_);
    EXPECT_NE(0u, remote_spectator.controlled_entity_ids()[0])
        << "the unbound remote spectator receives the live control map";

    const std::uint32_t spectator_initial_tick =
        remote_spectator.last_seen_server_tick();
    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(gameplay_session);
        remote_player.poll_messages();
        remote_spectator.poll_messages();
        return remote_spectator.last_seen_server_tick() >
                spectator_initial_tick &&
            !remote_spectator_transport->connected_peers().empty();
    })) << "the zero-seat remote survives Hello and receives later ticks";

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
}

// §4.5 all-dead auto-enter, end to end over the real networked host shadow:
// when every walker a machine's seat can claim is gone (its heroes dead, the
// only survivor claimed by another player — the [NET-R1] allied suppression
// shape), the seat stays null, the level keeps running, and the machine's
// view auto-enters follow mode on the teammate's hero WITHOUT stamping its
// user tag; the §2.8 caption company resolves through the stamped
// player-company table.
TEST(GameLoop, networked_host_view_auto_enters_follow_when_all_its_heroes_die)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    prepare_dense_allied_alpha_bravo_charlie_save(save);
    ASSERT_TRUE(save.save("save0"));

    // A genuine networked start config: glad_init stamps the owner tags onto
    // the live roster, marks the session networked, and seeds the transient
    // "netsession" slot the host shadow's server loads from. The host
    // machine (player 0) owns Alpha + Charlie; the remote machine (player 1)
    // owns Bravo — the §2.8 caption resolves the followed hero's owning
    // company through these tags.
    og::ui::PickerLobbyGameStartConfig start_config =
        make_one_view_lobby_start_config(save);
    start_config.is_networked = true;
    start_config.local_player_index = 0;
    start_config.local_player_indices = {0};
    start_config.local_seat_teams = {0};
    ASSERT_EQ(3u, start_config.save_data.team_list.size());
    start_config.save_data.team_list[0].owner_player_index = 0; // Alpha
    start_config.save_data.team_list[1].owner_player_index = 1; // Bravo
    start_config.save_data.team_list[2].owner_player_index = 0; // Charlie
    ready_screen_for_game_start(*game_screen, &start_config);
    glad_init(false, &start_config);
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));
    ASSERT_TRUE(gameplay_session.networked_session_);
    ASSERT_NE(nullptr, game_screen->viewob[0]);

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
    og::runtime::local_transport_shadow_set_player_companies(
        gameplay_session,
        std::array<std::pair<std::uint8_t, std::string>, 2>{
            std::pair<std::uint8_t, std::string>{0u, "Home Co"},
            std::pair<std::uint8_t, std::string>{1u, "Wolfpack"},
        });

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
    })) << "host display and remote client should receive the initial handoff";

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    const og::sim::GameClient* const display_client =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);
    std::uint32_t next_tick = std::max(display_client->last_seen_server_tick(),
                                       remote_client.last_seen_server_tick()) +
        1u;
    const auto pump_neutral_ticks = [&](int ticks) {
        const InputState neutral{};
        for (int i = 0; i < ticks; ++i)
        {
            if (!drive_host_and_remote_tick(
                    gameplay_session, remote_client, neutral, neutral,
                    next_tick++))
            {
                return false;
            }
        }
        return true;
    };
    ASSERT_TRUE(pump_neutral_ticks(2));

    // Sanity: the host seat (player 0) claimed Alpha; not following.
    viewscreen* const view = game_screen->viewob[0].get();
    ASSERT_NE(nullptr, view);
    EXPECT_FALSE(view->following_);
    ASSERT_NE(nullptr, view->control);

    // Isolate the bound team: any scenario troop on team 0 would be claimed
    // by the reacquire (the [NET-F3] troop rule for this deployed machine —
    // same outcome the shared pool gave) and keep the seat alive forever.
    for (auto& uptr : server_screen->world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living &&
            entity->team_num() == 0 && entity->myguy == nullptr)
        {
            entity->set_dead(1);
        }
    }
    ASSERT_TRUE(pump_neutral_ticks(2));

    // Kill the host's claimed hero: the reacquire claims the unclaimed
    // Charlie (same-machine under the owner-locked policy the install now
    // derives from this cross-control-OFF config) — still a live seat,
    // still not following.
    walker* const server_alpha =
        find_named_team_member(server_screen->world(), "Alpha");
    ASSERT_NE(nullptr, server_alpha);
    server_alpha->set_dead(1);
    ASSERT_TRUE(wait_until([&] {
        const InputState neutral{};
        if (!drive_host_and_remote_tick(
                gameplay_session, remote_client, neutral, neutral, next_tick++))
            return false;
        const auto& ids = display_client->controlled_entity_ids();
        return ids[0] != 0u &&
            controlled_entity_name(*display_client, ids[0]) == "Charlie";
    })) << "the shared-pool reacquire should claim Charlie for the host seat";
    EXPECT_FALSE(view->following_);

    // Kill Charlie too: the only survivor is the remote player's claimed
    // Bravo — unclaimable, the seat goes null (the [NET-R1] suppression
    // keeps the level running) and the view auto-enters follow on Bravo.
    walker* const server_charlie =
        find_named_team_member(server_screen->world(), "Charlie");
    ASSERT_NE(nullptr, server_charlie);
    server_charlie->set_dead(1);
    ASSERT_TRUE(wait_until([&] {
        const InputState neutral{};
        if (!drive_host_and_remote_tick(
                gameplay_session, remote_client, neutral, neutral, next_tick++))
            return false;
        return display_client->controlled_entity_ids()[0] == 0u &&
            view->following_;
    })) << "the null host seat should auto-enter follow mode";

    ASSERT_NE(nullptr, view->control);
    walker* const mirror_bravo =
        find_named_team_member(game_screen->world(), "Bravo");
    ASSERT_NE(nullptr, mirror_bravo);
    EXPECT_EQ(mirror_bravo, view->control)
        << "default follow target = the lowest player index with a live "
           "controlled walker (the remote player's Bravo)";
    EXPECT_EQ(1, static_cast<int>(mirror_bravo->user()))
        << "[NET-R6] the followed teammate keeps its OWNER's snapshot-synced "
           "tag; the follower's seat never restamps it";
    EXPECT_EQ("Wolfpack", view->follow_company_)
        << "the caption resolves the owner machine's company";
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end))
        << "the [NET-R1] suppression keeps the level running under follow";

    // The follow view survives further ticks (per-delta control re-syncs).
    ASSERT_TRUE(pump_neutral_ticks(3));
    EXPECT_TRUE(view->following_);
    EXPECT_EQ(mirror_bravo, view->control);

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
}

// §4.4 install e2e over the real network-host shadow, cross-control OFF:
// the install derives the OWNER-LOCKED policy from the game-start config,
// the machine map reaches the display mirror through snapshot v9, a
// deployed machine whose hero dies claims a scenario troop ([NET-F3])
// instead of a foreign hero, and once only foreign heroes remain its seat
// goes null — the joiner cannot steal the host's walker via death-rebind.
TEST(GameLoop, network_host_install_owner_locked_denies_death_rebind_steal)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    prepare_dense_allied_alpha_bravo_charlie_save(save);
    ASSERT_TRUE(save.save("save0"));

    og::ui::PickerLobbyGameStartConfig start_config =
        make_one_view_lobby_start_config(save);
    start_config.is_networked = true;
    start_config.local_player_index = 0;
    start_config.local_player_indices = {0};
    start_config.local_seat_teams = {0};
    ASSERT_EQ(3u, start_config.save_data.team_list.size());
    start_config.save_data.team_list[0].owner_player_index = 0; // Alpha
    start_config.save_data.team_list[1].owner_player_index = 1; // Bravo
    start_config.save_data.team_list[2].owner_player_index = 0; // Charlie
    ASSERT_EQ(0, static_cast<int>(start_config.save_data.cross_control))
        << "cross-control defaults OFF: the install must derive owner-locked";
    ready_screen_for_game_start(*game_screen, &start_config);
    glad_init(false, &start_config);
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));
    ASSERT_TRUE(gameplay_session.networked_session_);

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

    // The install derived owner-locked and stamped the machine map on the
    // authoritative world (both machines deployed: owner tags 0 and 1).
    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    EXPECT_EQ(og::sim::kControlPolicyOwnerLocked,
              server_screen->world().control_policy);
    EXPECT_EQ(og::sim::encode_player_machine(0, true),
              server_screen->world().player_machine[0]);
    EXPECT_EQ(og::sim::encode_player_machine(1, true),
              server_screen->world().player_machine[1]);
    EXPECT_EQ(og::sim::kPlayerMachineNone,
              server_screen->world().player_machine[2]);

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
    })) << "host display and remote client should receive the initial handoff";

    // Snapshot v9 consumption: the display mirror renders the policy the
    // wire carried, scalar for scalar.
    EXPECT_EQ(og::sim::kControlPolicyOwnerLocked,
              game_screen->world().control_policy);
    EXPECT_EQ(server_screen->world().player_machine,
              game_screen->world().player_machine);

    const og::sim::GameClient* const display_client =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);
    std::uint32_t next_tick = std::max(display_client->last_seen_server_tick(),
                                       remote_client.last_seen_server_tick()) +
        1u;
    const auto pump_neutral_ticks = [&](int ticks) {
        const InputState neutral{};
        for (int i = 0; i < ticks; ++i)
        {
            if (!drive_host_and_remote_tick(
                    gameplay_session, remote_client, neutral, neutral,
                    next_tick++))
            {
                return false;
            }
        }
        return true;
    };
    ASSERT_TRUE(pump_neutral_ticks(2));

    // Same bind outcome as legacy for this roster: each machine's seat
    // claimed its own hero.
    walker* const server_alpha =
        find_named_team_member(server_screen->world(), "Alpha");
    walker* const server_bravo =
        find_named_team_member(server_screen->world(), "Bravo");
    walker* const server_charlie =
        find_named_team_member(server_screen->world(), "Charlie");
    ASSERT_NE(nullptr, server_alpha);
    ASSERT_NE(nullptr, server_bravo);
    ASSERT_NE(nullptr, server_charlie);
    const std::uint32_t alpha_id = server_alpha->entity_id();
    const std::uint32_t bravo_id = server_bravo->entity_id();
    const std::uint32_t charlie_id = server_charlie->entity_id();
    EXPECT_EQ(alpha_id, display_client->controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id, display_client->controlled_entity_ids()[1]);

    // [NET-F3] through the real install: Bravo dies; machine 1 deployed, so
    // its reacquire claims an unowned scenario troop — never Alpha/Charlie.
    server_bravo->set_dead(1);
    ASSERT_TRUE(wait_until([&] {
        const InputState neutral{};
        if (!drive_host_and_remote_tick(
                gameplay_session, remote_client, neutral, neutral, next_tick++))
            return false;
        const std::uint32_t id = display_client->controlled_entity_ids()[1];
        return id != 0u && id != bravo_id;
    })) << "the deployed joiner machine should reacquire a scenario troop";
    const std::uint32_t troop_id = display_client->controlled_entity_ids()[1];
    EXPECT_NE(alpha_id, troop_id);
    EXPECT_NE(charlie_id, troop_id);
    walker* const claimed_troop = server_screen->world().find_by_id(troop_id);
    ASSERT_NE(nullptr, claimed_troop);
    EXPECT_EQ(nullptr, claimed_troop->myguy)
        << "[NET-F3]: the reacquired walker is an unowned scenario troop";

    // Remove every unowned team-0 troop: only foreign heroes remain for
    // machine 1 — the death-rebind steal is DENIED and the seat goes null
    // while the level keeps running.
    for (auto& uptr : server_screen->world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living &&
            entity->team_num() == 0 && entity->myguy == nullptr)
        {
            entity->set_dead(1);
        }
    }
    ASSERT_TRUE(wait_until([&] {
        const InputState neutral{};
        if (!drive_host_and_remote_tick(
                gameplay_session, remote_client, neutral, neutral, next_tick++))
            return false;
        return display_client->controlled_entity_ids()[1] == 0u;
    })) << "with only foreign heroes left the joiner's seat must go null";
    ASSERT_TRUE(pump_neutral_ticks(2));
    EXPECT_EQ(0u, display_client->controlled_entity_ids()[1]);
    EXPECT_EQ(alpha_id, display_client->controlled_entity_ids()[0])
        << "the host seat is untouched by the joiner's denial";
    EXPECT_EQ(-1, static_cast<int>(server_charlie->user()))
        << "the host's unclaimed Charlie is never stolen";
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end))
        << "the level keeps running under the null seat";

    og::runtime::clear_local_transport_shadow(gameplay_session);

    // ---- WP7 must-fix regression: the owner-locked round must NOT leak its
    // policy into the next LOCAL round of this same process. Teardown resets
    // nothing on the process-lifetime display world (only level load does),
    // so at this point the mirror still carries the networked policy —
    // worsen its leaked map to the [NET-R9] spectator-host shape (entry[0]
    // UNDEPLOYED) that used to deny every solo seat-0 claim and leave the
    // player spectating their own game. ----
    ASSERT_EQ(og::sim::kControlPolicyOwnerLocked,
              game_screen->world().control_policy)
        << "precondition: teardown alone does not reset the mirror's policy";
    game_screen->world().player_machine[0] =
        og::sim::encode_player_machine(0, false);

    // A plain LOCAL install from save0 (the dense 3-hero company saved at
    // the top of this test). The install must stamp the fresh authoritative
    // world back to LEGACY control (§4.4 "policy off in every local
    // session") even though its seed snapshot came from the poisoned
    // display world.
    glad_init();
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& local_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(local_session));
    ASSERT_FALSE(local_session.networked_session_);

    screen* const local_server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            local_session);
    ASSERT_NE(nullptr, local_server_screen);
    EXPECT_EQ(og::sim::kControlPolicyLegacy,
              local_server_screen->world().control_policy)
        << "the local authoritative world must run the legacy shared pool";
    for (std::size_t i = 0;
         i < local_server_screen->world().player_machine.size(); ++i)
    {
        EXPECT_EQ(og::sim::kPlayerMachineNone,
                  local_server_screen->world().player_machine[i])
            << "stale machine-map entry leaked into the local world at index "
            << i;
    }

    // The display mirror heals from the legacy server's snapshots.
    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(local_session);
        return game_screen->world().control_policy ==
            og::sim::kControlPolicyLegacy;
    })) << "the display world's stale policy must heal from the local "
           "install's snapshots";

    // And a seat-0 claim on the local world succeeds (the leaked undeployed
    // entry[0] used to fail this through the [NET-F3] troop-rule
    // fall-through).
    walker* const local_alpha =
        find_named_team_member(local_server_screen->world(), "Alpha");
    ASSERT_NE(nullptr, local_alpha);
    EXPECT_TRUE(og::sim::control_claim_allowed(
        local_server_screen->world(), local_alpha, 0))
        << "solo seat 0 must be claimable again after a networked round";

    og::runtime::clear_local_transport_shadow(local_session);
    game_screen->world().delete_objects();
}

// §4.4 install e2e, cross-control ON twin: the identical staging derives
// the LEGACY policy (control_policy 0, machine map still stamped and
// replicated), and the v7 shared pool is preserved — after its hero dies
// the joiner's machine CAN take the host's unclaimed hero.
TEST(GameLoop, network_host_install_cross_control_on_keeps_shared_pool_steal)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game_screen);

    SaveData& save = game_screen->save_data;
    prepare_dense_allied_alpha_bravo_charlie_save(save);
    ASSERT_TRUE(save.save("save0"));

    og::ui::PickerLobbyGameStartConfig start_config =
        make_one_view_lobby_start_config(save);
    start_config.is_networked = true;
    start_config.local_player_index = 0;
    start_config.local_player_indices = {0};
    start_config.local_seat_teams = {0};
    ASSERT_EQ(3u, start_config.save_data.team_list.size());
    start_config.save_data.team_list[0].owner_player_index = 0; // Alpha
    start_config.save_data.team_list[1].owner_player_index = 1; // Bravo
    start_config.save_data.team_list[2].owner_player_index = 0; // Charlie
    start_config.save_data.cross_control = 1;
    ready_screen_for_game_start(*game_screen, &start_config);
    glad_init(false, &start_config);
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& gameplay_session =
        *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(gameplay_session));
    ASSERT_TRUE(gameplay_session.networked_session_);

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

    // Cross-control ON derives the legacy policy; the machine map is still
    // stamped (legacy claims ignore it) and replicated.
    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            gameplay_session);
    ASSERT_NE(nullptr, server_screen);
    EXPECT_EQ(og::sim::kControlPolicyLegacy,
              server_screen->world().control_policy);
    EXPECT_EQ(og::sim::encode_player_machine(0, true),
              server_screen->world().player_machine[0]);
    EXPECT_EQ(og::sim::encode_player_machine(1, true),
              server_screen->world().player_machine[1]);

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
    })) << "host display and remote client should receive the initial handoff";
    EXPECT_EQ(og::sim::kControlPolicyLegacy,
              game_screen->world().control_policy);
    EXPECT_EQ(server_screen->world().player_machine,
              game_screen->world().player_machine);

    const og::sim::GameClient* const display_client =
        game_screen->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);
    std::uint32_t next_tick = std::max(display_client->last_seen_server_tick(),
                                       remote_client.last_seen_server_tick()) +
        1u;
    const auto pump_neutral_ticks = [&](int ticks) {
        const InputState neutral{};
        for (int i = 0; i < ticks; ++i)
        {
            if (!drive_host_and_remote_tick(
                    gameplay_session, remote_client, neutral, neutral,
                    next_tick++))
            {
                return false;
            }
        }
        return true;
    };
    ASSERT_TRUE(pump_neutral_ticks(2));

    walker* const server_alpha =
        find_named_team_member(server_screen->world(), "Alpha");
    walker* const server_bravo =
        find_named_team_member(server_screen->world(), "Bravo");
    walker* const server_charlie =
        find_named_team_member(server_screen->world(), "Charlie");
    ASSERT_NE(nullptr, server_alpha);
    ASSERT_NE(nullptr, server_bravo);
    ASSERT_NE(nullptr, server_charlie);
    const std::uint32_t alpha_id = server_alpha->entity_id();
    const std::uint32_t bravo_id = server_bravo->entity_id();
    const std::uint32_t charlie_id = server_charlie->entity_id();
    EXPECT_EQ(alpha_id, display_client->controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id, display_client->controlled_entity_ids()[1]);

    // Isolate the bound team so the pool reacquire lands on a hero, then
    // kill Bravo: under cross-control the joiner's machine takes the host's
    // unclaimed Charlie (deliberate v7 shared-pool stealing preserved).
    for (auto& uptr : server_screen->world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living &&
            entity->team_num() == 0 && entity->myguy == nullptr)
        {
            entity->set_dead(1);
        }
    }
    ASSERT_TRUE(pump_neutral_ticks(2));
    server_bravo->set_dead(1);
    ASSERT_TRUE(wait_until([&] {
        const InputState neutral{};
        if (!drive_host_and_remote_tick(
                gameplay_session, remote_client, neutral, neutral, next_tick++))
            return false;
        return display_client->controlled_entity_ids()[1] == charlie_id;
    })) << "cross-control ON must let the joiner steal the host's Charlie";
    EXPECT_EQ(1, static_cast<int>(server_charlie->user()))
        << "the stolen hero carries the joiner's seat tag";
    EXPECT_EQ(alpha_id, display_client->controlled_entity_ids()[0]);

    og::runtime::clear_local_transport_shadow(gameplay_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, local_transport_shadow_send_input_and_finish_tick_cover_active_paths)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
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
    game_screen->save_data.current_campaign = "gladiator";
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
    KeyBindingGuard bind_yell(0, KEY_YELL, SDLK_Y);
    keystates.set(SDLK_Y, true);
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

// Records every sound the interface layer routes through the platform bridge
// while still forwarding to whatever bridge was installed.
struct PlaySoundRecorderGuard
{
    PlatformBridge previous;
    std::vector<int> played;

    PlaySoundRecorderGuard()
        : previous(platform_bridge())
    {
        PlatformBridge recording = previous;
        recording.play_sound = [this](int sound_id) {
            played.push_back(sound_id);
            if (previous.play_sound)
                previous.play_sound(sound_id);
        };
        set_platform_bridge(std::move(recording));
    }

    ~PlaySoundRecorderGuard()
    {
        set_platform_bridge(previous);
    }

    bool saw(int sound_id) const
    {
        return std::find(played.begin(), played.end(), sound_id) != played.end();
    }
};

// Issue #145: yelling used to be silent and invisible. Input is processed
// authoritatively inside GameServer, which drops the SimInputResult cosmetics,
// and the render-layer consumer of those fields is unreachable once the local
// transport shadow is live. The cues only arrive if the sim layer emits them as
// sim events that ride the tick's batch out to the mirror.
TEST(GameLoop, game_frame_yell_delivers_sound_and_notification_to_mirror)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    disablePlayerJoystick(0);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_game_loop_yell_cue"))
        << "load_saved_game should succeed for the yell-cue test";

    viewscreen* const view = game_screen->viewob[0].get();
    ASSERT_TRUE(view != nullptr);
    ASSERT_TRUE(view->control != nullptr);
    view->clear_text();

    SessionKeyStateGuard keystates;
    KeyBindingGuard bind_yell(0, KEY_YELL, SDLK_Y);
    keystates.set(SDLK_Y, true);
    ctx().input = {};
    view->control->set_yo_delay(0);

    PlaySoundRecorderGuard sounds;

    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    deps.fixed_tick_ms = og::sim::DEFAULT_SIM_TICK_MS;

    GameLoopFrameState st;
    bool saw_text = false;
    for (int frame = 0; frame < 4 && !saw_text; ++frame)
    {
        EXPECT_EQ(GameFrameResult::Continue,
                  game_frame_with_result(*game_screen, st, deps));
        for (int slot = 0; slot < MAX_MESSAGES; ++slot)
        {
            if (view->textlist[slot] == "Yo!")
                saw_text = true;
        }
    }

    EXPECT_TRUE(saw_text)
        << "the yell should print \"Yo!\" on the mirror's HUD";
    EXPECT_TRUE(sounds.saw(SOUND_YO))
        << "the yell should play the yo clip through the platform bridge";

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

TEST(MasterSpeedRegression, uncapped_render_renders_every_call_at_master_cadence)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    GameSpeedGuard speed(1.0f);
    ASSERT_TRUE(load_minimal_game_loop_scenario("test_uncapped_render"))
        << "load_saved_game should succeed for uncapped-render regression test";

    // Set the uncapped sentinel for this test, restoring target_fps on exit.
    const int saved_target_fps = og::runtime::current_session->target_fps_;
    og::runtime::current_session->target_fps_ = og::core::kUncappedTargetFps;
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
    // Pre-configure only the sim pacer; the uncapped path must never touch
    // render_pacer, so it stays uninitialized (interval_ms() == 0).
    st.sim_pacer.configure(sim_interval, fake_now);

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
        fake_now += 1u;
        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        ASSERT_EQ(GameFrameResult::Continue, result)
            << "scenario must keep running across all 600 uncapped frames";
    }

    // Render must fire on every call — no pacer gating. If the capped branch
    // consumed the sentinel instead, target_frame_interval_ms(0) == 1 ms
    // would gate exactly the first of the 600 one-millisecond steps (599
    // renders), so exact equality still discriminates — by one frame, with
    // the pacer-unconfigured and no-sleep pins below carrying the rest.
    EXPECT_EQ(kFrames, render_count)
        << "uncapped render must fire on every game_frame_with_result call";

    // 600 calls × 1 ms / 82 ms ≈ 7 sim ticks. Allow ±1 to absorb the initial
    // deadline phase. Sim cadence must be identical to the capped sibling.
    const int expected_sim_ticks =
        static_cast<int>(static_cast<std::uint32_t>(kFrames) / sim_interval);
    EXPECT_EQ(7, expected_sim_ticks)
        << "expected_sim_ticks formula must match the documented constant";
    EXPECT_NEAR(expected_sim_ticks, tick_count, 1)
        << "sim must stay at master cadence (~82 ms / tick) under uncapped "
           "render";

    // Uncapped render fires every call, so the idle sleep branch (which on
    // master fires on every same-deadline call) must never be reached.
    EXPECT_TRUE(sleeps.empty())
        << "no sleeps expected when render fires on every call";
    const auto traces = og::runtime::copy_runtime_trace();
    const int sleep_traces = static_cast<int>(std::count_if(
        traces.begin(), traces.end(),
        [](const og::runtime::RuntimeTraceRecord& r) {
            return r.category == "game_loop" &&
                r.event == "desktop_loop_sleep_ms";
        }));
    EXPECT_EQ(0, sleep_traces)
        << "desktop_loop_sleep_ms must not fire when render fires every call";

    // The uncapped path bypasses render_pacer entirely: it must never have
    // been configured across 600 rendered frames.
    EXPECT_EQ(0u, st.render_pacer.interval_ms())
        << "render_pacer must stay unconfigured when target_fps is uncapped";

    // Sanity: world.timer_wait must remain at DEFAULT_TIMER_WAIT throughout —
    // any drift would mean the test is implicitly retuning master cadence.
    EXPECT_EQ(og::sim::DEFAULT_TIMER_WAIT, game_screen->world().timer_wait)
        << "world.timer_wait must not drift during the regression run";

    game_screen->world().delete_objects();
}

// Esc now opens the PAUSED menu (docs/pause-menu-design.md §2.1): one Esc
// pauses the world under the menu, a key-repeat Esc opens nothing, and a
// RESUME outcome releases the pause. The scripted-outcome queue stands in for
// the blocking menu loop (the real loop is driven by PauseMenuFlow tests).
TEST(GameLoop, game_frame_escape_opens_pause_menu_and_resume_releases_pause)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
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
    const auto run_escape_frame = [&](bool repeat = false) -> EscapeFrameOutcome {
        EventScript script;
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
        e.key.repeat = repeat;
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

    // Esc #1: the menu opens and pauses; the scripted outcome keeps the
    // pause pending (the "menu is open" state).
    trace_clear();
    og::ui::pause_menu_testing_clear_queue();
    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Resumed, /*release_pause=*/false);
    const EscapeFrameOutcome pause_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, pause_frame.result);
    EXPECT_FALSE(pause_frame.done);
    EXPECT_EQ(1, pause_frame.redrawme);
    EXPECT_TRUE(trace_contains("pause_menu", "open"));
    EXPECT_EQ(0, og::ui::pause_menu_testing_queue_remaining());
    EXPECT_TRUE(game_screen->world().paused);
    EXPECT_EQ(0u, game_screen->world().pause_player_index);
    ASSERT_FALSE(primary_view->textlist[0].empty());
    EXPECT_EQ(0, primary_view->textlist[0].compare(0, 6, "PAUSED"));
    // The old "ESC again: Quit?" hint is gone for the LOCAL pauser — the
    // menu IS the hint (remote peers keep an "ESC: Menu" overlay line).
    EXPECT_TRUE(primary_view->textlist[1].empty())
        << "unexpected second overlay line: " << primary_view->textlist[1];

    // A key-repeat Esc must not open (or close) anything: web Backspace
    // autorepeats aggressively.
    trace_clear();
    const EscapeFrameOutcome repeat_frame = run_escape_frame(true);
    EXPECT_EQ(GameFrameResult::Continue, repeat_frame.result);
    EXPECT_FALSE(repeat_frame.done);
    EXPECT_EQ(1, repeat_frame.redrawme);
    EXPECT_FALSE(trace_contains("pause_menu", "open"));
    EXPECT_TRUE(game_screen->world().paused);
    EXPECT_EQ(0u, game_screen->world().pause_player_index);

    // Esc #2 with a RESUME outcome: the pause releases and the owner index
    // resets.
    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Resumed, /*release_pause=*/true);
    const EscapeFrameOutcome resume_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, resume_frame.result);
    EXPECT_FALSE(resume_frame.done);
    EXPECT_EQ(1, resume_frame.redrawme);
    EXPECT_FALSE(game_screen->world().paused);
    EXPECT_EQ(og::sim::kNoPausePlayerIndex, game_screen->world().pause_player_index);
    EXPECT_EQ(0, og::ui::pause_menu_testing_queue_remaining());

    og::ui::pause_menu_testing_clear_queue();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

// A QUIT outcome from the menu keeps the old abort semantics verbatim:
// authoritative end, st.done, AbortedMission — from ONE Esc press (no more
// "second Esc" stage). A RESTART outcome additionally leaves world().retry
// set so the picker's `do { glad_main } while (retry)` relaunches.
TEST(GameLoop, game_frame_escape_pause_menu_quit_and_restart_outcomes)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels[game_screen->save_data.current_campaign] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    GameSpeedGuard speed_guard(0.0f);
    struct EscapeFrameOutcome {
        GameFrameResult result = GameFrameResult::Continue;
        bool done = false;
        int redrawme = 0;
    };
    const auto run_escape_frame = [&]() -> EscapeFrameOutcome {
        EventScript script;
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
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

    // --- QUIT ---
    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(og::runtime::local_transport_active(*og::runtime::current_session));

    og::ui::pause_menu_testing_clear_queue();
    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Quit, /*release_pause=*/false);
    const EscapeFrameOutcome abort_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::AbortedMission, abort_frame.result);
    EXPECT_TRUE(abort_frame.done);
    EXPECT_EQ(1, abort_frame.redrawme);
    EXPECT_FALSE(game_screen->world().retry)
        << "a plain quit must not arm the retry relaunch loop";

    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;

    // --- RESTART ---
    glad_init();
    ASSERT_TRUE(og::runtime::local_transport_active(*og::runtime::current_session));

    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Restart, /*release_pause=*/false);
    const EscapeFrameOutcome restart_frame = run_escape_frame();
    EXPECT_EQ(GameFrameResult::AbortedMission, restart_frame.result);
    EXPECT_TRUE(restart_frame.done);
    EXPECT_TRUE(game_screen->world().retry)
        << "restart must arm the display world's retry for the picker loop";
    EXPECT_TRUE(trace_contains("pause_menu", "restart_level"));

    // The picker relaunch latches world().retry right AFTER glad_main's
    // teardown (clear shadow + delete_objects) — prove the flag survives it,
    // exactly where picker_team_build.cpp's retry latch reads it. (The latch
    // must read it there and NOT in the while() condition: the loop-tail
    // reset(1) runs GameWorld::clear, which wipes the flag. The full picker
    // relaunch is covered by PauseMenuFlow.restart_mission_relaunches_level_
    // through_picker_loop in test_pause_menu.cpp.)
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
    EXPECT_TRUE(game_screen->world().retry)
        << "world().retry must survive glad_main teardown to reach the loop";

    // ...and the next iteration's ready_for_battle clears it so the relaunch
    // is not sticky.
    game_screen->ready_for_battle(1);
    EXPECT_FALSE(game_screen->world().retry);

    og::ui::pause_menu_testing_clear_queue();
    game_screen->world().end = 0;
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
    save.current_campaign = "gladiator";
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
    save.current_campaign = "gladiator";
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

TEST(GameLoop, accepted_withdraw_ends_level_and_persists_destination_cursor)
{
    screen* const ambient_screen = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, ambient_screen);
    GameWorld& ambient_world = ambient_screen->world();
    ASSERT_TRUE(ambient_world.oblist.empty());
    ASSERT_TRUE(ambient_world.fxlist.empty());
    ASSERT_TRUE(ambient_world.weaplist.empty());
    ASSERT_TRUE(ambient_world.dead_list.empty());

    struct ScopedAmbientWorldRestore
    {
        explicit ScopedAmbientWorldRestore(GameWorld& world_)
            : world(world_)
            , snapshot(og::sim::peek_keyframe_snapshot(world))
            , serialized(og::sim::serialize_snapshot(snapshot))
            , id(world.id)
            , title(world.title)
            , type(world.type)
            , par_value(world.par_value)
            , time_bonus_limit(world.time_bonus_limit)
            , current_scenario(world.current_scenario)
            , completion_events_emitted(world.completion_events_emitted)
            , keep_fallen_heroes(world.keep_fallen_heroes)
            , completed_levels(world.completed_levels)
            , applying_snapshot(world.applying_snapshot_)
            , grid_data(world.grid.data.get())
            , decor_data(world.decor.data.get())
            , obmap_ptr(world.myobmap.get())
            , floor_count(world.floor_count())
            , pixmaxx(world.pixmaxx)
            , pixmaxy(world.pixmaxy)
        {
        }

        ~ScopedAmbientWorldRestore()
        {
            const std::vector<std::uint8_t> after =
                og::sim::serialize_snapshot(
                    og::sim::peek_keyframe_snapshot(world));
            const bool snapshot_changed = after != serialized;
            EXPECT_EQ(serialized, after)
                << "isolated withdraw test mutated the ambient synced world";
            EXPECT_EQ(id, world.id);
            EXPECT_EQ(title, world.title);
            EXPECT_EQ(type, world.type);
            EXPECT_EQ(par_value, world.par_value);
            EXPECT_EQ(time_bonus_limit, world.time_bonus_limit);
            EXPECT_EQ(current_scenario, world.current_scenario);
            EXPECT_EQ(completion_events_emitted,
                      world.completion_events_emitted);
            EXPECT_EQ(keep_fallen_heroes, world.keep_fallen_heroes);
            EXPECT_EQ(completed_levels, world.completed_levels);
            EXPECT_EQ(applying_snapshot, world.applying_snapshot_);
            EXPECT_EQ(grid_data, world.grid.data.get());
            EXPECT_EQ(decor_data, world.decor.data.get());
            EXPECT_EQ(obmap_ptr, world.myobmap.get());
            EXPECT_EQ(floor_count, world.floor_count());
            EXPECT_EQ(pixmaxx, world.pixmaxx);
            EXPECT_EQ(pixmaxy, world.pixmaxy);
            EXPECT_TRUE(world.dead_list.empty());

            if (snapshot_changed || !world.dead_list.empty())
            {
                world.delete_objects();
                if (!og::sim::apply_snapshot(world, snapshot))
                    ADD_FAILURE() << "failed to restore ambient world snapshot";
            }
            world.id = id;
            world.title = title;
            world.type = type;
            world.par_value = par_value;
            world.time_bonus_limit = time_bonus_limit;
            world.current_scenario = current_scenario;
            world.completion_events_emitted = completion_events_emitted;
            world.keep_fallen_heroes = keep_fallen_heroes;
            world.completed_levels = completed_levels;
            world.applying_snapshot_ = applying_snapshot;
        }

        GameWorld& world;
        og::sim::WorldSnapshot snapshot;
        std::vector<std::uint8_t> serialized;
        int id;
        std::string title;
        char type;
        short par_value;
        short time_bonus_limit;
        short current_scenario;
        bool completion_events_emitted;
        short keep_fallen_heroes;
        std::set<int> completed_levels;
        bool applying_snapshot;
        unsigned char* grid_data;
        unsigned char* decor_data;
        obmap* obmap_ptr;
        int floor_count;
        std::int32_t pixmaxx;
        std::int32_t pixmaxy;
    } ambient_restore(ambient_world);

    og::test::ScopedCampaignMountState mount_restore;
    const std::filesystem::path save0_path =
        std::filesystem::path(get_user_path()) / "save" / "save0.gtl";
    og::test::ScopedPhysicalFileState save0_restore(save0_path);
    ASSERT_TRUE(save0_restore.ready())
        << "failed to snapshot save0: " << save0_restore.error().message();
    const std::filesystem::path save0_staging_path =
        std::filesystem::path(get_user_path()) / "save" / "save0.tmp.gtl";
    og::test::ScopedPhysicalFileState save0_staging_restore(
        save0_staging_path);
    ASSERT_TRUE(save0_staging_restore.ready())
        << "failed to snapshot save0 staging file: "
        << save0_staging_restore.error().message();

    og::runtime::GameSession::Config session_config;
    session_config.numviews = 1;
    session_config.allocate_screen = true;
    session_config.create_display = false;
    session_config.allocate_prefs = true;
    session_config.install_legacy_globals = false;
    og::runtime::GameSession isolated_session(session_config);
    auto isolated_scope = isolated_session.activate();
    screen* const game_screen = isolated_session.screen_ptr();
    ASSERT_NE(nullptr, game_screen);
    ASSERT_NE(ambient_screen, game_screen);
    SaveData& save = game_screen->save_data;

    struct ScopedWithdrawCleanup
    {
        og::runtime::GameSession& session;
        screen& game_screen;

        ~ScopedWithdrawCleanup()
        {
            picker_testing_yes_or_no_queue_clear();
            og::runtime::clear_local_transport_shadow(session);

            for (auto& view : game_screen.viewob)
            {
                if (view != nullptr)
                    view->control = nullptr;
            }
            game_screen.level_runtime_data().clear();
            GameWorld& world = game_screen.world();
            world.reset_level_progress();
            world.delete_objects();
            world.game_ended = false;
            world.completion_events_emitted = false;
            world.end = 0;
            world.retry = false;
            world.next_level = -1;
            world.ending = 0;
            world.level_done = 0;
            world.withdraw_requested = false;
            world.withdraw_level = -1;
        }
    } withdraw_cleanup{isolated_session, *game_screen};

    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.allied_mode = 1;
    save.my_team = 0;

    auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
    leader->name = "Withdrawer";
    leader->teamnum = 0;
    save.team_list[0] = std::move(leader);
    save.team_size = 1;
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(session));

    std::uint32_t tick = 0;
    for (int frame = 0; frame < 8; ++frame)
    {
        og::runtime::local_transport_shadow_send_input(
            session, InputState{}, tick++);
        og::runtime::local_transport_shadow_finish_tick(session);
    }

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_open_exit_prompt(
        session,
        /*player_index=*/0u,
        /*destination_level=*/2,
        /*withdraw=*/true));

    for (int frame = 0; frame < 40; ++frame)
        og::runtime::local_transport_shadow_finish_tick(session);

    EXPECT_NE(0, game_screen->world().end);

    SaveData persisted;
    ASSERT_TRUE(persisted.load("save0"));
    EXPECT_EQ("gladiator", persisted.current_campaign);
    EXPECT_EQ(2, persisted.scen_num);
    EXPECT_EQ(2, persisted.current_levels[persisted.current_campaign]);

}

static void setup_local_two_player_save(SaveData& save)
{
    save.reset();
    save.current_campaign = "gladiator";
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
                            "trails", "dust", "fire_glow", "screen_shake",
                            "floor_glide"})
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
    build_save(s, "gladiator", scen, 1,
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
    save.current_campaign = "gladiator";
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
            const char* campaign = "westlands")
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
                  const char* campaign = "westlands")
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
// The Long Season (longseason): four scenes from the Brass
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
    const char* const ls = "longseason";
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

TEST(GameLoop, zz_capture_imaginations)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // 1 The Raspberry Isle — the dream-log's island assault. Open on the
    // south landing beach, ride the charge up the paved causeway past its
    // sentry, breach the bailey's south gate, hold the two-ring castle
    // fight, then cut out to the mage tower and the bone tent on the
    // field ring before settling on the keep for the throne battle and
    // the tick-800 wave flash.
    if (want("imag_1"))
        epic_rec::record("imag_1", 1, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN},
            {{0, 504, 880, 0, true}, {80, 504, 760, 0, false},
             {200, 504, 688, 0, true}, {320, 504, 624, 0, false},
             {460, 504, 504, 0, true}, {560, 704, 240, 0, true},
             {660, 240, 704, 0, true}, {780, 504, 504, 0, true},
             {950, 504, 504, 0, false}},
            "imaginations");
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
    save.current_campaign = "gladiator";
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
// cfg graphics/zoom in REAL gameplay: zoom=0.5 doubles the classic-density
// world baseline. It must carry real rendered world (not clipped 320x200
// content plus black filler), split-screen panes must use its logical
// dimensions, and a mid-game aspect change must recompute the canvas and
// layout. The guard restores the prior window, zoom and smoothing settings.
// ---------------------------------------------------------------------------
#include <openglad/platform/video_sdl.h> // apply_world_scale_from_cfg (zoom/smoothing)
#include <openglad/platform/sai2x.h> // E_Screen native window for resize completion
#include <openglad/interface/render/view_layout.h>
#include <openglad/interface/render/radar.h>

namespace canvas_zoom_gameplay {

// RAII: install the test zoom, then tear the game down and restore cfg + UI
// routing even when an assertion aborts the test body.
struct WorldZoomGameGuard
{
    screen* s;
    std::string old_zoom;
    std::string old_smoothing;
    float old_window_w;
    float old_window_h;
    int old_physical_w = 0;
    int old_physical_h = 0;

    explicit WorldZoomGameGuard(screen* scr)
        : s(scr),
          old_zoom(cfg.get_setting("graphics", "zoom")),
          old_smoothing(cfg.get_setting("graphics", "smoothing")),
          old_window_w(og::runtime::current_session->window_w_),
          old_window_h(og::runtime::current_session->window_h_)
    {
        SDL_GetWindowSize(E_Screen->window, &old_physical_w, &old_physical_h);
        (void)SDL_SetWindowSize(E_Screen->window, 640, 400);
        (void)SDL_SyncWindow(E_Screen->window);
        og::runtime::current_session->window_w_ = 640;
        og::runtime::current_session->window_h_ = 400;
        update_overscan_setting();
        cfg.apply_setting("graphics", "zoom", "0.5");
        cfg.apply_setting("graphics", "smoothing", "off");
        apply_world_scale_from_cfg();
        // The production precondition for glad_init's fade: the picker's UI
        // frame is on the window. An earlier test may have left World as the
        // last presented canvas, and the zoom above just REALLOCATED that
        // surface — glad_init would fade a canvas the window never showed.
        s->set_active_canvas(CanvasTarget::UI);
        s->refresh();
    }

    ~WorldZoomGameGuard()
    {
        if (og::runtime::current_game_session)
            og::runtime::clear_local_transport_shadow(
                *og::runtime::current_game_session);
        s->world().end = 0;
        s->world().delete_objects();
        (void)SDL_SetWindowSize(E_Screen->window,
                                old_physical_w, old_physical_h);
        (void)SDL_SyncWindow(E_Screen->window);
        og::runtime::current_session->window_w_ = old_window_w;
        og::runtime::current_session->window_h_ = old_window_h;
        update_overscan_setting();
        cfg.apply_setting("graphics", "zoom", old_zoom);
        cfg.apply_setting("graphics", "smoothing", old_smoothing);
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

// Pane geometry is defined in the fixed gameplay-UI canvas, then projected
// into the CURRENT world canvas, plus containment inside the canvas.
void expect_pane_matches_layout(screen* s, int i, int canvas_w, int canvas_h)
{
    viewscreen* vs = s->viewob[static_cast<size_t>(i)].get();
    ASSERT_TRUE(vs) << "view " << i;
    const int ui_w = s->gameplay_ui_canvas_w();
    const int ui_h = s->gameplay_ui_canvas_h();
    const og::view_layout::ViewLayout baseline =
        og::view_layout::compute_view_layout(
            s->numviews, vs->mynum, vs->prefs[PREF_VIEW], ui_w, ui_h);
    const og::view_layout::ViewLayout want =
        og::view_layout::project_view_layout(
            baseline, ui_w, ui_h, canvas_w, canvas_h);
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

// Palette index of one pixel on the ACTIVE canvas.
int pixel_index(screen* s, int x, int y)
{
    int index = -1;
    s->get_pixel(x, y, &index);
    return index;
}

struct MiniHpBarCfgGuard
{
    std::string old_value;

    MiniHpBarCfgGuard()
        : old_value(cfg.get_setting("effects", "mini_hp_bar"))
    {
        cfg.apply_setting("effects", "mini_hp_bar", "on");
    }

    ~MiniHpBarCfgGuard()
    {
        cfg.apply_setting("effects", "mini_hp_bar", old_value);
    }
};

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

} // namespace canvas_zoom_gameplay

TEST(GameLoop, zoom_half_gameplay_draws_real_world_on_the_grown_canvas)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr);

    canvas_zoom_gameplay::WorldZoomGameGuard guard(s);
    const og::WorldCanvasDims initial_world =
        og::compute_zoom_canvas_dims(640, 400, 5);
    ASSERT_EQ(640, initial_world.w);
    ASSERT_EQ(400, initial_world.h);
    ASSERT_EQ(initial_world.w, s->world_canvas_w());
    ASSERT_EQ(initial_world.h, s->world_canvas_h());

    gameplay_rec::build_save(s, "gladiator", 1, 1,
                             {FAMILY_SOLDIER, FAMILY_ELF, FAMILY_MAGE}, 4);
	s->set_active_canvas(CanvasTarget::UI);
    glad_init();
	ASSERT_EQ(CanvasTarget::World, s->active_canvas())
		<< "gameplay init must leave the picker UI canvas before its first redraw";
    ASSERT_EQ(1, static_cast<int>(s->numviews));
    viewscreen* vs = s->viewob[0].get();
    ASSERT_TRUE(vs != nullptr);
    canvas_zoom_gameplay::expect_pane_matches_layout(
        s, 0, initial_world.w, initial_world.h);

    // More world visible than the classic canvas would show in the same
    // mode: the pane's world window grew with the canvas (tiles smaller
    // relative to the frame).
    const og::view_layout::ViewLayout classic =
        og::view_layout::compute_view_layout(1, 0, vs->prefs[PREF_VIEW], 320, 200);
    EXPECT_GT(vs->xview, classic.w);
    EXPECT_GT(vs->yview, classic.h);

    canvas_zoom_gameplay::run_frames(s, 8);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    score_panel(s);
    s->refresh();
    s->swap();

    // Real rendered world beyond the classic 320x200 area on both axes —
    // right of x=320 and below y=200 (a clipped legacy draw would leave
    // black filler there).
    EXPECT_GT(canvas_zoom_gameplay::nonblack_samples(s, 330, 20, 636, 190), 60)
        << "the canvas right of the classic 320px must carry rendered world";
    EXPECT_GT(canvas_zoom_gameplay::nonblack_samples(s, 20, 210, 310, 396), 60)
        << "the canvas below the classic 200px must carry rendered world";
    canvas_zoom_gameplay::expect_radar_inside_pane(vs);

    // Optional frame dump for the visual review flow.
    if (getenv("OG_FX_CAPTURE_DIR"))
        canvas_zoom_gameplay::dump_canvas(s, "zoom_half_640", 0);
}

TEST(GameLoop, zoom_half_splitscreen_layout_tracks_window_resizes)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr);

    canvas_zoom_gameplay::WorldZoomGameGuard guard(s);
    const og::WorldCanvasDims initial_world =
        og::compute_zoom_canvas_dims(640, 400, 5);
    ASSERT_EQ(640, initial_world.w);
    ASSERT_EQ(400, initial_world.h);
    ASSERT_EQ(initial_world.w, s->world_canvas_w());
    ASSERT_EQ(initial_world.h, s->world_canvas_h());

    // ---- 2 players on the grown canvas ------------------------------------
    gameplay_rec::build_save(s, "gladiator", 2, 2,
                             {FAMILY_SOLDIER, FAMILY_BARBARIAN,
                              FAMILY_ELF, FAMILY_MAGE}, 6);
    glad_init();
    s->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(2, static_cast<int>(s->numviews));
    for (int i = 0; i < 2; i++)
        canvas_zoom_gameplay::expect_pane_matches_layout(
            s, i, initial_world.w, initial_world.h);
    // The fixed two-pixel HUD seam projects into World without overlap.
    EXPECT_LE(s->viewob[0]->endx, s->viewob[1]->xloc)
        << "left pane must end before the right pane starts";

    canvas_zoom_gameplay::run_frames(s, 8);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    score_panel(s);
    s->refresh();
    s->swap();
    // Both panes carry rendered world; the right pane lives ENTIRELY beyond
    // the classic 320px boundary.
    EXPECT_GT(canvas_zoom_gameplay::nonblack_samples(
                  s, s->viewob[0]->xloc + 8, s->viewob[0]->yloc + 8,
                  s->viewob[0]->endx - 8, s->viewob[0]->endy - 8), 60);
    EXPECT_GT(canvas_zoom_gameplay::nonblack_samples(
                  s, s->viewob[1]->xloc + 8, s->viewob[1]->yloc + 8,
                  s->viewob[1]->endx - 8, s->viewob[1]->endy - 8), 60);
    canvas_zoom_gameplay::expect_radar_inside_pane(s->viewob[0].get());
    canvas_zoom_gameplay::expect_radar_inside_pane(s->viewob[1].get());
    if (getenv("OG_FX_CAPTURE_DIR"))
        canvas_zoom_gameplay::dump_canvas(s, "zoom_half_640_2p", 0);

    // ---- Mid-game aspect change: logical canvas/layout follow it -------------
    SDL_Event ev{};
    ev.type = SDL_EVENT_WINDOW_RESIZED;
    ASSERT_TRUE(SDL_SetWindowSize(E_Screen->window, 960, 540));
    ASSERT_TRUE(SDL_SyncWindow(E_Screen->window));
    ev.window.data1 = 960;
    ev.window.data2 = 540;
    handle_window_event(ev);
    const og::WorldCanvasDims wide_world =
        og::compute_zoom_canvas_dims(960, 540, 5);
    ASSERT_EQ(712, wide_world.w);
    ASSERT_EQ(400, wide_world.h);
    EXPECT_EQ(wide_world.w, s->world_canvas_w());
    EXPECT_EQ(wide_world.h, s->world_canvas_h());
    for (int i = 0; i < 2; i++)
        canvas_zoom_gameplay::expect_pane_matches_layout(
            s, i, wide_world.w, wide_world.h);
    canvas_zoom_gameplay::run_frames(s, 2);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    s->swap();
    canvas_zoom_gameplay::expect_radar_inside_pane(s->viewob[1].get());
    // Returning to 16:10 restores the original zoom-derived canvas.
    ASSERT_TRUE(SDL_SetWindowSize(E_Screen->window, 640, 400));
    ASSERT_TRUE(SDL_SyncWindow(E_Screen->window));
    ev.window.data1 = 640;
    ev.window.data2 = 400;
    handle_window_event(ev);
    EXPECT_EQ(initial_world.w, s->world_canvas_w());
    EXPECT_EQ(initial_world.h, s->world_canvas_h());
    for (int i = 0; i < 2; i++)
        canvas_zoom_gameplay::expect_pane_matches_layout(
            s, i, initial_world.w, initial_world.h);
    // The resize reallocated the World canvas; present a frame on it as the
    // running game loop would, so the next glad_init's fade-out reads a
    // canvas the window has shown (the fade-ownership invariant).
    s->redraw();
    s->swap();

    // ---- 4 players: quadrants on the grown canvas --------------------------
    s->world().end = 0;
    s->world().delete_objects();
    gameplay_rec::build_save(s, "gladiator", 2, 4,
                             {FAMILY_SOLDIER, FAMILY_BARBARIAN,
                              FAMILY_ELF, FAMILY_MAGE}, 6);
    glad_init();
    s->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(4, static_cast<int>(s->numviews));
    for (int i = 0; i < 4; i++)
        canvas_zoom_gameplay::expect_pane_matches_layout(
            s, i, initial_world.w, initial_world.h);
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

    canvas_zoom_gameplay::run_frames(s, 4);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    s->redraw();
    s->swap();
    // The fourth quadrant lives entirely beyond the classic 320x200 corner
    // and still carries rendered world.
    EXPECT_GT(canvas_zoom_gameplay::nonblack_samples(
                  s, s->viewob[3]->xloc + 8, s->viewob[3]->yloc + 8,
                  s->viewob[3]->endx - 8, s->viewob[3]->endy - 8), 40);
    for (int i = 0; i < 4; i++)
        canvas_zoom_gameplay::expect_radar_inside_pane(
            s->viewob[static_cast<size_t>(i)].get());
    if (getenv("OG_FX_CAPTURE_DIR"))
        canvas_zoom_gameplay::dump_canvas(s, "zoom_half_640_4p", 0);
}

// Issue #149: the mini HP bar is painted on the gameplay-UI overlay, which is
// pinned at zoom-1.0 density while the world canvas grows by 1/zoom. Its anchor
// was projected between the two panes but its WIDTH was taken raw from the
// sprite, so at zoom 0.5 the bar came out twice as wide as the walker it
// belongs to.
TEST(GameLoop, zoom_half_mini_hp_bar_matches_projected_sprite_width)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr);

    canvas_zoom_gameplay::WorldZoomGameGuard guard(s);
    canvas_zoom_gameplay::MiniHpBarCfgGuard hp_bar_on;

    gameplay_rec::build_save(s, "gladiator", 1, 1,
                             {FAMILY_SOLDIER}, 4);
    s->set_active_canvas(CanvasTarget::UI);
    glad_init();
    ASSERT_TRUE(s->gameplay_ui_canvas_available())
        << "the fixed overlay must exist for the pane projection to apply";
    ASSERT_EQ(1, static_cast<int>(s->numviews));

    viewscreen* const vs = s->viewob[0].get();
    ASSERT_TRUE(vs != nullptr);
    walker* const w = vs->control;
    ASSERT_TRUE(w != nullptr);

    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(50.0f);
    w->set_last_hitpoints(50.0f);

    // Settle the camera and allocate/clear the overlay before sampling it.
    s->redraw();

    const og::view_layout::ViewLayout ui =
        og::view_layout::compute_view_layout(
            s->numviews, vs->mynum, vs->prefs[PREF_VIEW],
            s->gameplay_ui_canvas_w(), s->gameplay_ui_canvas_h());
    ASSERT_TRUE(ui.applies);
    const Sint32 sprite_w = w->sizex();
    const Sint32 expected_bar_w = sprite_w * ui.w / vs->xview;
    ASSERT_EQ(sprite_w, expected_bar_w * 2)
        << "the harness must produce a 2x world canvas (window 640x400, zoom 0.5)";

    // project_world_point_to_gameplay_ui only projects while the gameplay-UI
    // canvas is the active one (otherwise it falls back to identity for the
    // overlay-allocation case), so enter the same scope the renderer uses
    // before computing the anchor.
    ScopedGameplayUiCanvas gameplay_ui(*s);

    const WalkerRenderPosition draw_pos =
        resolve_walker_render_position(*w, vs->interpolation_alpha);
    const float world_x = draw_pos.xpos - static_cast<float>(vs->topx) +
        static_cast<float>(vs->xloc);
    const float world_y = draw_pos.ypos - static_cast<float>(vs->topy) +
        static_cast<float>(vs->yloc);
    const auto [bar_x, walker_bottom] =
        vs->project_world_point_to_gameplay_ui(
            world_x, world_y + static_cast<float>(w->sizey()));
    const Sint32 bar_y = walker_bottom + 1;
    ASSERT_LT(bar_x + sprite_w + 8, ui.x + ui.w)
        << "the sampled columns must stay inside the gameplay-UI pane";
    ASSERT_GT(bar_x, ui.x) << "the bar must start inside the pane";
    ASSERT_GT(bar_y, ui.y) << "the bar must sit inside the pane";
    ASSERT_LT(bar_y, ui.y + ui.h) << "the bar must sit inside the pane";

    s->clearbuffer();
    const int background_index =
        canvas_zoom_gameplay::pixel_index(s, bar_x + sprite_w + 8, bar_y);
    draw_small_health_bar(w, vs);

    const int hp_index = canvas_zoom_gameplay::pixel_index(s, bar_x, bar_y);
    ASSERT_NE(background_index, hp_index) << "the bar must have been drawn";

    int run = 0;
    while (bar_x + run < ui.x + ui.w &&
           canvas_zoom_gameplay::pixel_index(s, bar_x + run, bar_y) == hp_index)
    {
        ++run;
    }

    // Same expression the renderer uses: (Sint32)((float)bar_w * ratio).
    const Sint32 expected_cur_w =
        static_cast<Sint32>(static_cast<float>(expected_bar_w) * (50.0f / 100.0f));
    EXPECT_EQ(expected_cur_w + 1, run)
        << "the filled HP run must track the PROJECTED sprite footprint, not "
           "the raw world-canvas sprite width";

    // The 1-px black outline hugs the projected footprint too. Before the fix
    // its right edge sat at bar_x + sprite_w + 1, twice as far out.
    const int outline_index =
        canvas_zoom_gameplay::pixel_index(s, bar_x - 1, bar_y);
    ASSERT_NE(background_index, outline_index) << "the outline must be drawn";
    EXPECT_EQ(outline_index,
              canvas_zoom_gameplay::pixel_index(
                  s, bar_x + expected_bar_w + 1, bar_y))
        << "the outline's right edge must sit one pixel past the projected width";
    EXPECT_EQ(background_index,
              canvas_zoom_gameplay::pixel_index(s, bar_x + sprite_w + 1, bar_y))
        << "nothing may be drawn out at the unprojected sprite width";
}

// Issue #244: the mini HP bar's HEIGHT was hardcoded (bar_h = 1, a 2-row fill
// through draw_box's inclusive corners) while its width and anchor were
// pane-projected, so at zoom 0.5 the bar towered 2x over the shrunken sprite
// it labels. The fill now scales by the vertical pane ratio, floored at one
// drawable UI row.
TEST(GameLoop, zoom_half_mini_hp_bar_height_matches_pane_ratio)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr);

    canvas_zoom_gameplay::WorldZoomGameGuard guard(s);
    canvas_zoom_gameplay::MiniHpBarCfgGuard hp_bar_on;

    gameplay_rec::build_save(s, "gladiator", 1, 1,
                             {FAMILY_SOLDIER}, 4);
    s->set_active_canvas(CanvasTarget::UI);
    glad_init();
    ASSERT_TRUE(s->gameplay_ui_canvas_available())
        << "the fixed overlay must exist for the pane projection to apply";
    ASSERT_EQ(1, static_cast<int>(s->numviews));

    viewscreen* const vs = s->viewob[0].get();
    ASSERT_TRUE(vs != nullptr);
    walker* const w = vs->control;
    ASSERT_TRUE(w != nullptr);

    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(50.0f);
    w->set_last_hitpoints(50.0f);

    // Settle the camera and allocate/clear the overlay before sampling it.
    s->redraw();

    const og::view_layout::ViewLayout ui =
        og::view_layout::compute_view_layout(
            s->numviews, vs->mynum, vs->prefs[PREF_VIEW],
            s->gameplay_ui_canvas_w(), s->gameplay_ui_canvas_h());
    ASSERT_TRUE(ui.applies);
    const Sint32 sprite_w = w->sizex();
    const Sint32 expected_bar_w = sprite_w * ui.w / vs->xview;
    ASSERT_EQ(sprite_w, expected_bar_w * 2)
        << "the harness must produce a 2x world canvas (window 640x400, zoom 0.5)";

    // project_world_point_to_gameplay_ui only projects while the gameplay-UI
    // canvas is the active one, so enter the same scope the renderer uses
    // before computing the anchor.
    ScopedGameplayUiCanvas gameplay_ui(*s);

    const WalkerRenderPosition draw_pos =
        resolve_walker_render_position(*w, vs->interpolation_alpha);
    const float world_x = draw_pos.xpos - static_cast<float>(vs->topx) +
        static_cast<float>(vs->xloc);
    const float world_y = draw_pos.ypos - static_cast<float>(vs->topy) +
        static_cast<float>(vs->yloc);
    const auto [bar_x, walker_bottom] =
        vs->project_world_point_to_gameplay_ui(
            world_x, world_y + static_cast<float>(w->sizey()));
    const Sint32 bar_y = walker_bottom + 1;
    ASSERT_LT(bar_x + sprite_w + 8, ui.x + ui.w)
        << "the sampled columns must stay inside the gameplay-UI pane";
    ASSERT_GT(bar_x, ui.x) << "the bar must start inside the pane";
    ASSERT_GT(bar_y - 1, ui.y)
        << "the outline's top row must sit inside the pane";
    ASSERT_LT(bar_y + 2, ui.y + ui.h)
        << "the sampled rows below the bar must stay inside the pane";

    s->clearbuffer();
    const int background_index =
        canvas_zoom_gameplay::pixel_index(s, bar_x + sprite_w + 8, bar_y);
    draw_small_health_bar(w, vs);

    const int hp_index = canvas_zoom_gameplay::pixel_index(s, bar_x, bar_y);
    ASSERT_NE(background_index, hp_index) << "the bar must have been drawn";
    int vrun = 0;
    while (bar_y + vrun < ui.y + ui.h &&
           canvas_zoom_gameplay::pixel_index(s, bar_x, bar_y + vrun) ==
               hp_index)
        ++vrun;
    EXPECT_EQ(1, vrun)
        << "at zoom 0.5 the fill must be one UI row — half the classic "
           "2-row fill, within rounding — not the unscaled 2 (issue #244)";
    const int outline_index =
        canvas_zoom_gameplay::pixel_index(s, bar_x - 1, bar_y);
    ASSERT_NE(background_index, outline_index) << "the outline must be drawn";
    EXPECT_EQ(outline_index,
              canvas_zoom_gameplay::pixel_index(s, bar_x, bar_y - 1))
        << "the outline's top row must sit directly above the fill";
    EXPECT_EQ(outline_index,
              canvas_zoom_gameplay::pixel_index(s, bar_x, bar_y + 1))
        << "the bottom outline must sit directly under the single fill row "
           "(pre-#244 this row was fill)";
    EXPECT_EQ(background_index,
              canvas_zoom_gameplay::pixel_index(s, bar_x, bar_y + 2))
        << "the block must end after 3 UI rows at zoom 0.5";
}

// --- Mid-game local player add/remove (pause-menu design §5) ---------------
//
// These drive the real local shadow built by glad_init (the idiom of
// local_split_screen_background_player_exit_prompt_does_not_hang above):
// one in-process peer per seat, display client 0, authoritative server
// session. The new seat functions live in local_transport_shadow.cpp.

#include <openglad/core/test_trace.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/interface/render/view_layout.h>

namespace {

// One display frame's worth of shadow traffic: input for every seat, then
// the authoritative step + mirror drain.
void midgame_pump(og::runtime::GameSession& session, int frames,
                  std::uint32_t& tick)
{
    for (int i = 0; i < frames; ++i)
    {
        og::runtime::local_transport_shadow_send_input(
            session, InputState{}, tick++);
        og::runtime::local_transport_shadow_finish_tick(session);
    }
}

walker* midgame_find_walker_by_user(screen* which_screen, int user)
{
    if (which_screen == nullptr)
        return nullptr;
    for (const auto& uptr : which_screen->world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && entity->user() == user)
            return entity;
    }
    return nullptr;
}

walker* midgame_find_living_on_team(screen* which_screen, short team)
{
    if (which_screen == nullptr)
        return nullptr;
    for (const auto& uptr : which_screen->world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living &&
            static_cast<short>(entity->team_num()) == team)
        {
            return entity;
        }
    }
    return nullptr;
}

// Feed held movement on ONE input slot until its walker moves (four
// directions tried — the seat's spawn spot is probe-clear but a wall may
// still block a given side).
bool midgame_slot_moves_walker(og::runtime::GameSession& session,
                               std::size_t slot, walker* w,
                               std::uint32_t& tick)
{
    static constexpr int kDirections[4] = {
        KEY_RIGHT, KEY_DOWN, KEY_LEFT, KEY_UP};
    for (const int direction : kDirections)
    {
        const short before_x = w->xpos();
        const short before_y = w->ypos();
        for (int i = 0; i < 15; ++i)
        {
            InputState input{};
            input.players[slot].held[direction] = true;
            if (i == 0)
                input.players[slot].pressed[direction] = true;
            og::runtime::local_transport_shadow_send_input(
                session, input, tick++);
            og::runtime::local_transport_shadow_finish_tick(session);
        }
        midgame_pump(session, 1, tick); // release the key
        if (w->xpos() != before_x || w->ypos() != before_y)
            return true;
    }
    return false;
}

// Every live display view must sit exactly where viewscreen::resize projects
// it for the current numviews (compute_view_layout at gameplay-UI dims,
// projected onto the world canvas).
void midgame_expect_view_layouts(screen* game_screen)
{
    for (int i = 0; i < game_screen->numviews; ++i)
    {
        viewscreen* const view = game_screen->viewob[i].get();
        ASSERT_NE(nullptr, view) << "view " << i << " must be live";
        const int ui_w = game_screen->gameplay_ui_canvas_w();
        const int ui_h = game_screen->gameplay_ui_canvas_h();
        const og::view_layout::ViewLayout baseline =
            og::view_layout::compute_view_layout(
                game_screen->numviews, i, view->prefs[PREF_VIEW], ui_w, ui_h);
        const og::view_layout::ViewLayout expected =
            og::view_layout::project_view_layout(
                baseline, ui_w, ui_h,
                game_screen->world_canvas_w(), game_screen->world_canvas_h());
        EXPECT_EQ(expected.x, view->xloc) << "view " << i;
        EXPECT_EQ(expected.y, view->yloc) << "view " << i;
        EXPECT_EQ(expected.w, view->xview) << "view " << i;
        EXPECT_EQ(expected.h, view->yview) << "view " << i;
    }
}

} // namespace

TEST(GameLoop, midgame_add_third_local_player)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    reset_default_player_controls();

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
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

    std::uint32_t tick = 0;
    midgame_pump(session, 8, tick);

    ASSERT_TRUE(og::runtime::local_transport_shadow_can_add_player(session));

    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    ASSERT_TRUE(trace_contains("seats", "add_player index=2"));

    // Display: seat count, live third view, re-projected split layout.
    EXPECT_EQ(3, static_cast<int>(game_screen->save_data.numplayers));
    ASSERT_EQ(3, static_cast<int>(game_screen->numviews));
    ASSERT_EQ(3u, og::runtime::local_transport_client_count(session));
    viewscreen* const new_view = game_screen->viewob[2].get();
    ASSERT_NE(nullptr, new_view);
    EXPECT_EQ(2, static_cast<int>(new_view->mynum));
    EXPECT_EQ(2, static_cast<int>(new_view->global_player_index_));
    EXPECT_EQ(game_screen->viewob[0]->my_team, new_view->my_team)
        << "the joiner plays view 0's team";
    midgame_expect_view_layouts(game_screen);

    // The new seat's key profile is whatever slot 2 of the pool holds — the
    // rotation seeded IJKL there and the add must not reset it.
    EXPECT_EQ(SDLK_I,
              get_player_key_binding_for_mode(
                  2, static_cast<int>(ControlDirectionMode::FourDirection),
                  KEY_UP));

    // Server: seat 2 bound to a live walker wearing user tag 2.
    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server_screen);
    walker* const seat2_walker = midgame_find_walker_by_user(server_screen, 2);
    ASSERT_NE(nullptr, seat2_walker);
    EXPECT_FALSE(seat2_walker->dead());
    EXPECT_EQ(game_screen->viewob[0]->my_team,
              static_cast<short>(seat2_walker->team_num()));
    EXPECT_EQ(ACT_CONTROL, static_cast<int>(seat2_walker->act_type()));

    // After a few frames the ControlChange + snapshot reach the display: the
    // new view renders a control wearing the seat's tag.
    midgame_pump(session, 8, tick);
    ASSERT_NE(nullptr, new_view->control);
    EXPECT_EQ(2, static_cast<int>(new_view->control->user()));

    // Input routing: slot-2 keys move seat 2's walker (they were sampled all
    // along — the add routed them).
    EXPECT_TRUE(midgame_slot_moves_walker(session, 2u, seat2_walker, tick))
        << "slot-2 input must drive the new seat's walker";

    // Roster purity: whether the seat claimed an existing walker or spawned a
    // stock one, the level-end roster rebuild must still hold exactly the two
    // company heroes (a spawned joiner carries no myguy).
    const bool spawned = trace_contains("seats", "source=spawned");
    if (spawned)
    {
        EXPECT_EQ(nullptr, seat2_walker->myguy);
    }
    server_screen->save_data.update_guys(server_screen->world().oblist);
    EXPECT_EQ(2, static_cast<int>(server_screen->save_data.team_size));
    int roster_members = 0;
    for (const auto& member : server_screen->save_data.team_list)
    {
        if (member != nullptr)
            ++roster_members;
    }
    EXPECT_EQ(2, roster_members)
        << "a mid-game joiner must not recruit itself into the company";

    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, midgame_remove_middle_player_of_three)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    reset_default_player_controls();

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = 3;
    save.allied_mode = 0;
    save.my_team = 0;

    auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
    leader->name = "Leader";
    leader->teamnum = 0;
    auto scout = std::make_unique<guy>(FAMILY_ARCHER);
    scout->name = "Scout";
    scout->teamnum = 1;
    auto third = std::make_unique<guy>(FAMILY_ARCHER);
    third->name = "Third";
    third->teamnum = 2;
    save.team_list[0] = std::move(leader);
    save.team_list[1] = std::move(scout);
    save.team_list[2] = std::move(third);
    save.team_size = 3;
    ASSERT_TRUE(save.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));
    ASSERT_EQ(3u, og::runtime::local_transport_client_count(session));

    std::uint32_t tick = 0;
    midgame_pump(session, 8, tick);

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server_screen);
    walker* const seat0_walker = midgame_find_walker_by_user(server_screen, 0);
    walker* const seat1_walker = midgame_find_walker_by_user(server_screen, 1);
    walker* const seat2_walker = midgame_find_walker_by_user(server_screen, 2);
    ASSERT_NE(nullptr, seat0_walker);
    ASSERT_NE(nullptr, seat1_walker);
    ASSERT_NE(nullptr, seat2_walker);
    const short seat2_team = game_screen->viewob[2]->my_team;

    ASSERT_TRUE(
        og::runtime::local_transport_shadow_can_remove_player(session, 1));

    trace_clear();
    ASSERT_TRUE(
        og::runtime::local_transport_shadow_remove_local_player(session, 1));
    ASSERT_TRUE(trace_contains("seats", "remove_player index=1"));

    // Display: two seats, two live views, re-projected split; the surviving
    // top seat's view renumbered down with its team.
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.numplayers));
    ASSERT_EQ(2, static_cast<int>(game_screen->numviews));
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));
    ASSERT_NE(nullptr, game_screen->viewob[1].get());
    EXPECT_EQ(nullptr, game_screen->viewob[2].get());
    EXPECT_EQ(seat2_team, game_screen->viewob[1]->my_team)
        << "old seat 2's team must follow it down to view 1";
    midgame_expect_view_layouts(game_screen);

    // Server: renumbered tags — seat 0 untouched, old seat 2 is player 1 now,
    // the removed walker lives on as AI.
    EXPECT_EQ(0, static_cast<int>(seat0_walker->user()));
    EXPECT_EQ(1, static_cast<int>(seat2_walker->user()));
    EXPECT_EQ(-1, static_cast<int>(seat1_walker->user()));
    EXPECT_FALSE(seat1_walker->dead());
    EXPECT_NE(ACT_CONTROL, static_cast<int>(seat1_walker->act_type()))
        << "the removed seat's walker must fall back to its AI act type";

    // Key profiles compacted: seat 1 now holds old seat 2's IJKL profile and
    // the freed ARROWS profile rotated to the first inactive slot (2).
    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    EXPECT_EQ(SDLK_I, get_player_key_binding_for_mode(1, kFour, KEY_UP));
    EXPECT_EQ(SDLK_UP, get_player_key_binding_for_mode(2, kFour, KEY_UP));

    // Input routing after the renumber: each surviving slot drives ITS walker.
    midgame_pump(session, 4, tick);
    EXPECT_TRUE(midgame_slot_moves_walker(session, 0u, seat0_walker, tick))
        << "slot-0 input must still drive seat 0's walker";
    EXPECT_TRUE(midgame_slot_moves_walker(session, 1u, seat2_walker, tick))
        << "slot-1 input must drive the renumbered (old seat 2) walker";

    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

TEST(GameLoop, midgame_seat_gating_networked_and_count_limits)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    reset_default_player_controls();

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
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

    std::uint32_t tick = 0;
    midgame_pump(session, 8, tick);

    // One seat: no removal, but room to add.
    EXPECT_FALSE(
        og::runtime::local_transport_shadow_can_remove_player(session, 0));
    EXPECT_TRUE(og::runtime::local_transport_shadow_can_add_player(session));

    // #249: a single-seat device (a phone) is already full at that one seat
    // — the mid-game door reads the same og::input::local_seat_cap() as Base
    // Camp's [+], so no gamepad means no second seat.
    {
        const bool saved_device_class =
            input_hardware_state().single_seat_device;
        input_hardware_state().single_seat_device = true;
        EXPECT_FALSE(
            og::runtime::local_transport_shadow_can_add_player(session));
        EXPECT_FALSE(
            og::runtime::local_transport_shadow_add_local_player(session));
        EXPECT_EQ(1u, og::runtime::local_transport_client_count(session));
        input_hardware_state().single_seat_device = saved_device_class;
    }

    // Networked sessions are out of scope (§5): both gates close.
    session.networked_session_ = true;
    EXPECT_FALSE(og::runtime::local_transport_shadow_can_add_player(session));
    EXPECT_FALSE(
        og::runtime::local_transport_shadow_can_remove_player(session, 0));
    EXPECT_FALSE(og::runtime::local_transport_shadow_add_local_player(session));
    session.networked_session_ = false;

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server_screen);
    GameWorld& server_world = server_screen->world();

    // Spawn-failure path: point view 0 at a team with no live walkers — the
    // claim scan and the stock spawn (no teammate to anchor the ring probe
    // on) both come up empty, so the add must fail cleanly with the seat
    // count unchanged.
    short empty_team = -1;
    for (short t = 0; t < 4 && empty_team < 0; ++t)
    {
        if (midgame_find_living_on_team(server_screen, t) == nullptr)
            empty_team = t;
    }
    ASSERT_NE(-1, static_cast<int>(empty_team))
        << "gladiator scen 1 must leave at least one of the four seat teams "
           "unpopulated";
    const short lead_team = game_screen->viewob[0]->my_team;
    game_screen->viewob[0]->my_team = empty_team;
    trace_clear();
    EXPECT_TRUE(og::runtime::local_transport_shadow_can_add_player(session));
    EXPECT_FALSE(og::runtime::local_transport_shadow_add_local_player(session));
    EXPECT_TRUE(trace_contains("seats", "add_player_failed"));
    ASSERT_EQ(1u, og::runtime::local_transport_client_count(session));
    game_screen->viewob[0]->my_team = lead_team;

    // Every unclaimed walker on the lead team gets parked on the empty team:
    // each add below must SPAWN its stock joiner (never claim) so the ring
    // and anchor placement paths both run deterministically.
    for (const auto& uptr : server_world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living && entity->user() == -1 &&
            static_cast<short>(entity->team_num()) == lead_team)
        {
            entity->set_team_num(static_cast<unsigned char>(empty_team));
        }
    }
    walker* const seat0_walker = midgame_find_walker_by_user(server_screen, 0);
    ASSERT_NE(nullptr, seat0_walker);

    // 1 -> 2 seats: no anchors published on a classic level, so the joiner
    // lands on the ring probe around the live teammate.
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    ASSERT_TRUE(trace_contains("seats", "source=spawned"));

    // 2 -> 3 seats: publish a respawn anchor at a probed-clear spot; the
    // joiner must land exactly on it (the anchor path precedes the ring).
    short anchor_x = -1;
    short anchor_y = -1;
    static constexpr short kProbeRing[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
    };
    for (short radius = 3; radius >= 1 && anchor_x < 0; --radius)
    {
        for (const auto& dir : kProbeRing)
        {
            const short x = static_cast<short>(
                seat0_walker->xpos() + dir[0] * radius * GRID_SIZE);
            const short y = static_cast<short>(
                seat0_walker->ypos() + dir[1] * radius * GRID_SIZE);
            if (og::sim::respawn_spot_clear(server_world, seat0_walker, x, y,
                                            /*floor=*/0))
            {
                anchor_x = x;
                anchor_y = y;
                break;
            }
        }
    }
    ASSERT_GE(anchor_x, 0) << "no probe-clear spot near the level 1 start";
    server_world.respawn.anchor_count[lead_team] = 1;
    server_world.respawn.anchor_x[lead_team][0] = anchor_x;
    server_world.respawn.anchor_y[lead_team][0] = anchor_y;
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    ASSERT_TRUE(trace_contains("seats", "source=spawned"));
    walker* const seat2_walker = midgame_find_walker_by_user(server_screen, 2);
    ASSERT_NE(nullptr, seat2_walker);
    EXPECT_EQ(anchor_x, seat2_walker->xpos());
    EXPECT_EQ(anchor_y, seat2_walker->ypos());

    // 3 -> 4 seats: the published anchor is occupied now, so this joiner
    // falls back to the ring again. Then the cap closes the add gate.
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    ASSERT_TRUE(trace_contains("seats", "source=spawned"));
    ASSERT_EQ(4u, og::runtime::local_transport_client_count(session));
    EXPECT_EQ(4, static_cast<int>(game_screen->numviews));
    EXPECT_FALSE(og::runtime::local_transport_shadow_can_add_player(session));
    EXPECT_FALSE(og::runtime::local_transport_shadow_add_local_player(session));

    // Removal index bounds at four seats.
    EXPECT_FALSE(
        og::runtime::local_transport_shadow_can_remove_player(session, -1));
    EXPECT_FALSE(
        og::runtime::local_transport_shadow_can_remove_player(session, 4));
    EXPECT_TRUE(
        og::runtime::local_transport_shadow_can_remove_player(session, 3));
    EXPECT_FALSE(
        og::runtime::local_transport_shadow_remove_local_player(session, 4));

    // Removing seat 0 keeps peer 0 (the display client) alive: bindings shift
    // down across the fixed peers and only the vacated TOP peer disconnects.
    walker* const old_seat1_walker =
        midgame_find_walker_by_user(server_screen, 1);
    ASSERT_NE(nullptr, old_seat1_walker);
    ASSERT_TRUE(
        og::runtime::local_transport_shadow_remove_local_player(session, 0));
    ASSERT_EQ(3u, og::runtime::local_transport_client_count(session));
    EXPECT_EQ(3, static_cast<int>(game_screen->numviews));
    EXPECT_EQ(old_seat1_walker, midgame_find_walker_by_user(server_screen, 0))
        << "old seat 1's walker must renumber down to player 0";
    EXPECT_EQ(-1, static_cast<int>(seat0_walker->user()))
        << "the removed seat's walker must be released to AI";
    midgame_pump(session, 6, tick);
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end))
        << "the display session must survive removing seat 0";

    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

// PR #198 user report: local game, pause menu ADD PLAYER, resume, play, then
// re-pause into the added seat's player screen — the app died with
// "InProcessTransport peer 1 is not connected" (throw at resolve_peer, caught
// only at main). This drives the reported sequence at frame level as a broad
// safety net: the display peer stays transport-connected through a paused
// stock-spawn add, resumed play across several periodic snapshot-hash
// checkpoints, and a re-pause dwell. The minimized failing shape of the
// crash lives in midgame_seat_churn_while_paused_must_not_desync_display_peer
// below.
TEST(GameLoop, midgame_add_player_resume_play_repause_keeps_transport_alive)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    reset_default_player_controls();

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
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
    ASSERT_EQ(1u, og::runtime::local_transport_client_count(session));

    std::uint32_t tick = 0;
    // Land the pause exactly on a periodic hash-checkpoint tick
    // (KEYFRAME_INTERVAL_TICKS) so the frozen-tick dwell also covers the
    // checkpoint-adjacent bookkeeping shape.
    midgame_pump(session, og::sim::KEYFRAME_INTERVAL_TICKS, tick);

    // Esc: the pause menu opens its pause and pumps while blocking.
    og::runtime::local_transport_shadow_request_pause_keepalive(session);
    for (int i = 0;
         i < 4 && !og::runtime::local_transport_shadow_is_paused(session); ++i)
    {
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
    }
    ASSERT_TRUE(og::runtime::local_transport_shadow_is_paused(session));

    // The reporter's roster had one deployed hero and no claimable teammate,
    // so the add SPAWNED a stock soldier (a brand-new server entity created
    // between ticks — the shape that must survive the delta stream). Park
    // every unclaimed lead-team walker on an unused team to force that path.
    {
        screen* const server_screen =
            og::runtime::local_transport_shadow_testing_server_screen(session);
        ASSERT_NE(nullptr, server_screen);
        const short lead_team = game_screen->viewob[0]->my_team;
        short empty_team = -1;
        for (short t = 0; t < 4 && empty_team < 0; ++t)
        {
            if (t != lead_team &&
                midgame_find_living_on_team(server_screen, t) == nullptr)
            {
                empty_team = t;
            }
        }
        ASSERT_NE(-1, static_cast<int>(empty_team));
        for (const auto& uptr : server_screen->world().oblist)
        {
            walker* const entity = uptr.get();
            if (entity != nullptr && !entity->dead() &&
                entity->query_order() == Order::Living &&
                entity->user() == -1 &&
                static_cast<short>(entity->team_num()) == lead_team)
            {
                entity->set_team_num(static_cast<unsigned char>(empty_team));
            }
        }
    }

    // ADD PLAYER from the open menu.
    trace_clear();
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    ASSERT_TRUE(trace_contains("seats", "add_player index=1"));
    ASSERT_TRUE(trace_contains("seats", "source=spawned"))
        << "this regression needs the stock-spawn shape (the reporter's)";
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    // The menu stays open while the user reads it: more paused pumps than
    // the 120-strike desync bound, so a wedged expectation queue (the
    // pre-fix defect) cannot hide inside the dwell.
    for (int i = 0; i < 140; ++i)
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
    EXPECT_FALSE(trace_contains("net", "server_desync_disconnect"))
        << "the display peer must survive the open pause menu after ADD";

    // RESUME, then real play on both seats. 420 ticks crosses seven
    // KEYFRAME_INTERVAL_TICKS (=60) hash checkpoints — pre-fix the display
    // mirror desyncs and the server cuts peer 1, after which the next
    // send_input throws the reported transport error.
    ASSERT_TRUE(og::runtime::local_transport_shadow_toggle_pause(session));
    trace_clear();
    midgame_pump(session, 420, tick);
    EXPECT_FALSE(trace_contains("net", "server_desync_disconnect"))
        << "the display peer must never be cut as desynced after ADD PLAYER";
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end))
        << "the session must survive resumed play after ADD PLAYER";
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    // Re-pause into the added seat's player screen: the pause pump keeps
    // running while the user cycles INPUT there.
    og::runtime::local_transport_shadow_request_pause_keepalive(session);
    for (int i = 0; i < 10; ++i)
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
    ASSERT_TRUE(og::runtime::local_transport_shadow_is_paused(session));
    og::runtime::local_transport_shadow_request_pause_keepalive(session);
    for (int i = 0; i < 10; ++i)
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));

    // Resume once more and keep playing: the transport must still be whole.
    ASSERT_TRUE(og::runtime::local_transport_shadow_toggle_pause(session));
    midgame_pump(session, 60, tick);
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end));

    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    game_screen->world().delete_objects();
}

// Shared boot/teardown for the mid-game seat regressions below: a 1-hero
// local save through glad_init (the PR #198 reporter's session shape).
namespace {
void midgame_one_hero_boot(screen* game_screen)
{
    reset_default_player_controls();
    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "gladiator";
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
}

void midgame_one_hero_teardown(screen* game_screen)
{
    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
}

} // namespace

// The reporter's crash, minimized (fuzz-derived): seat churn against a
// PAUSED world. While the pause pins the tick, the server keeps answering
// the display's keyframe requests at that same frozen tick; a transient
// hash mismatch (the display drains a seat mutation's ControlChange stamp
// ahead of its snapshot) leaves an expected-hash entry that a paused tick
// can neither match nor prune, so every later check strikes the stale
// front until kMaxConsecutiveSnapshotHashMismatches cuts the DISPLAY peer
// (net "server_desync_disconnect") and the next send dies with the
// reported "InProcessTransport peer 1 is not connected" throw.
TEST(GameLoop, midgame_seat_churn_while_paused_must_not_desync_display_peer)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    midgame_one_hero_boot(game_screen);
    trace_clear();
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));

    std::uint32_t tick = 0;
    midgame_pump(session, 21, tick);

    // ADD while running, a few frames, then pause (menu opens).
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    midgame_pump(session, 3, tick);
    og::runtime::local_transport_shadow_request_pause_keepalive(session);
    midgame_pump(session, 2, tick);

    // REMOVE seat 0 and ADD again with the world frozen under the menu, the
    // pause pump running between clicks.
    ASSERT_TRUE(
        og::runtime::local_transport_shadow_remove_local_player(session, 0));
    for (int i = 0; i < 20; ++i)
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));

    // The menu stays open: enough paused pumps for the pre-fix
    // orphaned-expectation loop to reach the 120-strike disconnect.
    for (int i = 0; i < 140; ++i)
    {
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session))
            << "paused pump " << i;
        ASSERT_FALSE(trace_contains("net", "server_desync_disconnect"))
            << "the display peer was cut as desynced at paused pump " << i
            << " (the reporter's crash: the next send then throws "
               "'InProcessTransport peer 1 is not connected')";
    }

    // The frozen tick means the hash bookkeeping can only heal, never age
    // out: a transient blip may log a mismatch or two, but the pre-fix
    // wedged-queue loop racked up one PER PAUSED PUMP (>=120 here).
    {
        og::sim::GameServer* const server =
            og::runtime::local_transport_shadow_testing_server(session);
        ASSERT_NE(nullptr, server);
        EXPECT_LT(server->snapshot_hash_mismatch_count(),
                  static_cast<std::size_t>(
                      og::sim::kMaxConsecutiveSnapshotHashMismatches))
            << "paused seat churn must not grind hash mismatches";
    }

    // RESUME and keep playing: transport whole, both seats alive.
    ASSERT_TRUE(og::runtime::local_transport_shadow_toggle_pause(session));
    midgame_pump(session, 60, tick);
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end));
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    midgame_one_hero_teardown(game_screen);
}

// PR #198 report (b): P1 cycled to ARROWS, mid-game ADD PLAYER — the new P2
// came up as ARROWS too (profile-pool slot 1's live keymap IS factory arrows;
// the cycler's factory-name claim moves only the RESET identity). The add
// must land the new seat on the first factory mapping no other active seat
// answers to — WASD here.
TEST(GameLoop, midgame_add_player_gets_an_unchosen_mapping)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    midgame_one_hero_boot(game_screen);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));

    // The reporter's setup: P1 on ARROWS via the INPUT cycler (LOCAL store —
    // the cfg clobber hazard).
    cfg_store local_config;
    ASSERT_TRUE(og::input::assign_mapping_to_player(
        0, og::input::resolve_mapping(local_config, "ARROWS")));
    ASSERT_EQ("ARROWS", og::input::current_mapping_name(0));
    // The collision in waiting: slot 1's live keymap is ALSO factory arrows.
    ASSERT_EQ("ARROWS", og::input::current_mapping_name(1));

    std::uint32_t tick = 0;
    midgame_pump(session, 8, tick);
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));

    EXPECT_EQ("ARROWS", og::input::current_mapping_name(0))
        << "P1 keeps the mapping it chose";
    EXPECT_EQ("WASD", og::input::current_mapping_name(1))
        << "the added seat must come up on the first factory mapping no "
           "other active seat holds, never on P1's duplicate";

    midgame_one_hero_teardown(game_screen);
}

#include "../../src/interface/ui/picker_sdl_defs.h"

#include <atomic>

// The mid-game ZOOM crash, minimized (2 local seats, real shadow): the pause
// menu blocks run_game_tick, so local_transport_shadow_send_input never runs
// while it is open — and the paused server force-keyframes every pump, whose
// hash-check replies count as client outbound activity and suppress the
// heartbeats that were supposed to keep last_received_input_ms fresh. The
// starvation check is masked only while the pause is pending: the very server
// step that processes the RESUME (run_pause_menu sends the response, then
// pumps once before returning to the game loop) saw every in-process peer
// >DISCONNECT_TIMEOUT_MS stale and cut them all; the display's next send then
// threw "InProcessTransport peer 1 is not connected" (peer 1 IS the display —
// in-process peer ids start at 1).
TEST(GameLoop, long_pause_menu_visit_then_resume_keeps_local_peers_connected)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    midgame_one_hero_boot(game_screen);
    trace_clear();
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));

    // The reporter's session: two local seats.
    std::uint32_t tick = 0;
    midgame_pump(session, 21, tick);
    ASSERT_TRUE(og::runtime::local_transport_shadow_add_local_player(session));
    midgame_pump(session, 3, tick);
    ASSERT_EQ(2u, og::runtime::local_transport_client_count(session));

    // A controllable wall clock on the authoritative server, continuous with
    // the real one (update_timeouts skips stamps that sit in the future).
    og::sim::GameServer* const server =
        og::runtime::local_transport_shadow_testing_server(session);
    ASSERT_NE(nullptr, server);
    std::atomic<std::uint64_t> fake_now{static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count())};
    server->set_wall_clock_ms_source([&fake_now] { return fake_now.load(); });
    midgame_pump(session, 2, tick); // stamps move onto the mock timeline

    // Esc: the menu opens; frame_tick degrades to pump_paused while it blocks.
    ASSERT_TRUE(og::runtime::local_transport_shadow_toggle_pause(session));
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
    ASSERT_TRUE(og::runtime::local_transport_shadow_is_paused(session));

    // The user browses the player screen well past DISCONNECT_TIMEOUT_MS...
    fake_now += og::sim::DISCONNECT_TIMEOUT_MS + 1500u;
    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
        ASSERT_FALSE(trace_contains("net", "server_timeout_disconnect"))
            << "a peer was cut while the menu was still open (pump " << i
            << ")";
    }

    // ...and changes P2's zoom (the real handler: viewscreen + cfg only —
    // nothing sim-visible, so the server hash ledger must stay clean).
    (void)og::ui::cycle_player_view_zoom(1);
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));

    // RESUME: run_pause_menu's exit dance is resume THEN one paused pump,
    // before the game loop's next send_input can run.
    ASSERT_TRUE(og::runtime::local_transport_shadow_toggle_pause(session));
    ASSERT_TRUE(og::runtime::local_transport_shadow_pump_paused(session));
    EXPECT_FALSE(trace_contains("net", "server_timeout_disconnect"))
        << "the resume step starvation-cut in-process peers whose only "
           "silence was the open menu";

    // The next display-side sends must not throw and play must continue.
    bool threw = false;
    std::string what;
    try
    {
        midgame_pump(session, 30, tick);
    }
    catch (const std::exception& e)
    {
        threw = true;
        what = e.what();
    }
    EXPECT_FALSE(threw) << "post-resume send died: " << what;
    EXPECT_FALSE(trace_contains("net", "server_timeout_disconnect"));
    EXPECT_FALSE(trace_contains("net", "server_desync_disconnect"));
    EXPECT_EQ(2u, og::runtime::local_transport_client_count(session));
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end));

    // Restore P2's zoom to GAME: the cycle above persists player2_view_zoom
    // into cfg, and under the sliced-canvas zoom pipeline a leaked override
    // composes a larger world canvas for every later multi-seat test.
    while (og::ui::cycle_player_view_zoom(1) != 0)
        continue;

    midgame_one_hero_teardown(game_screen);
}

// ---------------------------------------------------------------------------
// Camera viewscreens (issue #224, docs/camera-views-design.md §9 "Real-session
// proofs" (c), WP7): the owed host-and-join proof. A real soccer session over
// the local transport shadow — the GameServer authority on its own screen, the
// in-process display client on the session screen — drives the whole camera
// channel end to end: mode Lua declares the ball camera on the AUTHORITY, the
// declaration rides the ModeState into the mirror's snapshots, and only the
// DISPLAY screen materializes a pane from it. The two seat counts are the
// presentation-divergence proof: docked-vs-inset is resolved per machine from
// local seat count and never touches the wire (constraint 5), so a 3-seat
// machine and a 1-seat machine render the same replicated declaration
// differently and neither costs a snapshot-hash strike.
// ---------------------------------------------------------------------------
#include <format>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>

namespace {

// One seat's live window, captured so the inset proof can pin that the seat
// layout never moved.
struct SeatRectPin
{
    Sint32 xloc = 0;
    Sint32 yloc = 0;
    Sint32 xview = 0;
    Sint32 yview = 0;
};

// campaigns/modes THE PITCH (mode_levels.lua [820] = soccer, 2 teams).
constexpr short kCameraSoccerLevel = 820;
// S.BALL_ENTITY in campaigns/modes/packs/modes.core/lib/mode_soccer_impl.lua:
// the mode-var slot the ball's id is banked in, right beside the camera
// declaration in on_mode_init. A silent re-map in the Lua turns this pin.
constexpr std::size_t kCameraSoccerBallEntityVar = 14;

// A soccer save the picker could have written: this machine's crew on team 0,
// one seat per fighter, and a FAIR bot squad on team 1 (the LINEUP FILL code
// — save_data.fill[team] — which sync_world_from_save_data hands the sim).
void camera_soccer_boot(screen* game_screen, int seats)
{
    reset_default_player_controls();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    SaveData& save = game_screen->save_data;
    save.reset();
    save.current_campaign = "modes";
    save.current_levels[save.current_campaign] = kCameraSoccerLevel;
    save.scen_num = kCameraSoccerLevel;
    save.numplayers = static_cast<unsigned char>(seats);
    save.my_team = 0;
    // SaveData::reset deliberately preserves lobby match settings. Establish
    // the whole lineup baseline before opting team 1 into its FAIR bot squad,
    // so a shuffled predecessor cannot change this fixture's battle shape.
    save.fill.fill(og::sim::kFillNone);
    save.map_units.fill(og::sim::kMapUnitsOn);
    save.fill[0] = og::sim::kFillNone;   // team 0 is this machine's crew
    save.fill[1] = og::sim::kFillFair;   // the opposing side, bot-filled
    for (int i = 0; i < seats; ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = std::format("STRIKER{}", i);
        member->teamnum = 0;
        member->deployed = true;
        member->upgrade_to_level(3, true);
        save.team_list[static_cast<std::size_t>(i)] = std::move(member);
    }
    save.team_size = static_cast<unsigned char>(seats);
    ASSERT_TRUE(save.save("save0"));

    game_screen->ready_for_battle(static_cast<short>(seats));
    glad_init();
}

// Pump the shadow until the declaration has crossed the wire (or give up):
// the authority declares on its first mode tick, the mirror learns it on the
// snapshot that follows.
void camera_soccer_pump_until_declared(og::runtime::GameSession& session,
                                       screen* game_screen, int max_frames)
{
    std::uint32_t tick = 0;
    for (int i = 0; i < max_frames; ++i)
    {
        if (game_screen->world().mode.cameras[0].entity_id != 0)
            return;
        midgame_pump(session, 1, tick);
    }
}

void camera_soccer_teardown(screen* game_screen)
{
    // Retire the declaration through the real destroy branch so no camera
    // pane (or docked pane count) leaks into the next test in this binary.
    game_screen->world().mode.cameras[0] = og::sim::ModeCameraView{};
    game_screen->world().mode.active = false;
    game_screen->sync_camera_views();
    EXPECT_EQ(nullptr, game_screen->camera_view_.get());
    EXPECT_FALSE(game_screen->camera_docked_);

    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    // The fixture switched both the mounted package and the saved campaign,
    // and opted into a FAIR fill. Put the process-wide display save back on a
    // complete classic baseline: reset() clears the campaign/roster half but
    // intentionally carries lobby match settings, so scrub the coupled
    // lineup arrays explicitly before rewriting save0.
    SaveData& save = game_screen->save_data;
    save.reset();
    save.numplayers = 1;
    save.fill.fill(og::sim::kFillNone);
    save.map_units.fill(og::sim::kMapUnitsOn);
    EXPECT_TRUE(save.save("save0"));
    game_screen->ready_for_battle(1);
    game_screen->relayout_views();
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("gladiator");
}

// The shared half of the proof, run at both seat counts: the authority
// declares, the display mirror learns it only over the wire, the display
// materializes the pane on its next redraw, and the authority's own screen
// stays camera-free.
void camera_soccer_expect_replicated(screen* display, screen* server,
                                     og::runtime::GameSession& session)
{
    // Before the first snapshot carrying the declaration: mirrors never run
    // mode Lua (§5 "Mirrors, first frame"), so the display's slot is empty
    // and a full redraw — sync_camera_views is its first statement —
    // materializes nothing.
    EXPECT_EQ(0, display->world().mode.cameras[0].entity_id)
        << "the display mirror declared a camera without a snapshot";
    ASSERT_TRUE(display->redraw());
    ASSERT_EQ(nullptr, display->camera_view_.get())
        << "a mirror materialized a camera before its first apply";

    camera_soccer_pump_until_declared(session, display, 400);

    const std::int32_t declared = server->world().mode.cameras[0].entity_id;
    ASSERT_NE(0, declared)
        << "the authority never declared the ball camera (mode Lua is "
           "host-only: no declaration means soccer never inited)";
    EXPECT_EQ(declared,
              static_cast<std::int32_t>(
                  server->world().mode.vars[kCameraSoccerBallEntityVar]))
        << "the declaration must point at the banked ball (S.BALL_ENTITY)";
    EXPECT_EQ(og::sim::kCameraStyleAuto,
              server->world().mode.cameras[0].style);

    // Populated after: the same id, over the wire, into the mirror.
    EXPECT_EQ(declared, display->world().mode.cameras[0].entity_id)
        << "the camera declaration never reached the display mirror";
    EXPECT_EQ(server->world().mode.cameras[0].style,
              display->world().mode.cameras[0].style);

    // The AUTHORITY never materializes a camera (constraint 1) — the
    // test_game_loop no-extra-views property, re-asserted on the server
    // screen while a camera is live on the wire.
    EXPECT_EQ(nullptr, server->camera_view_.get())
        << "the authoritative screen materialized a camera";
    EXPECT_FALSE(server->camera_docked_);
    EXPECT_EQ(server->numviews, server->layout_pane_count());
    for (int view_index = server->numviews; view_index < MAX_VIEWS;
         ++view_index)
        EXPECT_EQ(nullptr, server->viewob[view_index].get())
            << "unexpected authoritative view " << view_index;

    // The display's pane comes up on the next redraw and follows the
    // REPLICATED ball — the mirror's own copy of the entity, resolved from
    // the wire id.
    ASSERT_TRUE(display->redraw());
    ASSERT_NE(nullptr, display->camera_view_.get())
        << "the display never materialized the replicated camera";
    EXPECT_TRUE(display->camera_view_->camera_view_);
    EXPECT_EQ(-1, display->camera_view_->mynum);
    walker* const ball = display->world().find_by_id(
        static_cast<std::uint32_t>(declared));
    ASSERT_NE(nullptr, ball) << "the ball itself never replicated";
    EXPECT_EQ(ball, display->camera_view_->control)
        << "the camera resolved to something other than the replicated ball";
    EXPECT_EQ(static_cast<std::uint32_t>(declared), ball->entity_id());

    // The camera pane rides no seat slot: the display's slots above its human
    // seat count stay null too.
    for (int view_index = display->numviews; view_index < MAX_VIEWS;
         ++view_index)
        EXPECT_EQ(nullptr, display->viewob[view_index].get())
            << "camera leaked into display viewob[" << view_index << "]";

    // Zero desync strikes: the camera slot is replicated state on both sides
    // of the hash, so learning it costs nothing.
    og::sim::GameServer* const game_server =
        og::runtime::local_transport_shadow_testing_server(session);
    ASSERT_NE(nullptr, game_server);
    EXPECT_EQ(0u, game_server->snapshot_hash_mismatch_count())
        << "the camera channel cost the display mirror a hash strike";
    EXPECT_FALSE(trace_contains("net", "server_desync_disconnect"));
}

} // namespace

// Three local seats: style "auto" resolves DOCKED here — the camera takes the
// free fourth quadrant and the three seats re-lay through the same
// compute_view_layout(4, i, ...) pipeline, while numviews stays 3.
TEST(GameLoop, host_and_join_soccer_camera_docks_on_a_three_seat_machine)
{
    screen* const display = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, display);
    trace_clear();
    camera_soccer_boot(display, 3);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(
        *og::runtime::current_session));
    ASSERT_EQ(3u, og::runtime::local_transport_client_count(session));
    ASSERT_EQ(3, display->numviews);
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server);
    ASSERT_NE(display, server);

    camera_soccer_expect_replicated(display, server, session);
    if (!::testing::Test::HasFatalFailure())
    {
        EXPECT_TRUE(display->camera_docked_)
            << "3 seats + auto must dock the camera";
        EXPECT_EQ(4, display->layout_pane_count());
        EXPECT_EQ(3, display->numviews)
            << "numviews stays a pure human seat count";
        // The docked pane IS quadrant 3 of the real four-pane layout.
        const og::view_layout::ViewLayout baseline =
            og::view_layout::compute_view_layout(
                4, 3, og::view_layout::kModeFull,
                display->gameplay_ui_canvas_w(),
                display->gameplay_ui_canvas_h());
        const og::view_layout::ViewLayout quadrant =
            og::view_layout::project_view_layout(
                baseline, display->gameplay_ui_canvas_w(),
                display->gameplay_ui_canvas_h(),
                display->world_canvas_w(), display->world_canvas_h());
        ASSERT_TRUE(quadrant.applies);
        EXPECT_EQ(quadrant.x, display->camera_view_->xloc);
        EXPECT_EQ(quadrant.y, display->camera_view_->yloc);
        EXPECT_EQ(quadrant.w, display->camera_view_->xview);
        EXPECT_EQ(quadrant.h, display->camera_view_->yview);
    }

    camera_soccer_teardown(display);
}

// One local seat, the same replicated declaration: style "auto" resolves
// INSET — a GameplayUI-canvas overlay stacked above the seat's radar block
// (the one-seat second-minimap ruling), the seat layout untouched
// (layout_pane_count() == numviews). Same wire, different presentation: the
// divergence the constraint-5 rule exists to allow.
TEST(GameLoop, host_and_join_soccer_camera_insets_on_a_one_seat_machine)
{
    screen* const display = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, display);
    trace_clear();
    camera_soccer_boot(display, 1);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_NE(nullptr, og::runtime::current_game_session);
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    ASSERT_TRUE(og::runtime::local_transport_active(
        *og::runtime::current_session));
    ASSERT_EQ(1u, og::runtime::local_transport_client_count(session));
    ASSERT_EQ(1, display->numviews);
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(session);
    ASSERT_NE(nullptr, server);
    ASSERT_NE(display, server);

    const SeatRectPin seat_before{display->viewob[0]->xloc,
                                  display->viewob[0]->yloc,
                                  display->viewob[0]->xview,
                                  display->viewob[0]->yview};

    camera_soccer_expect_replicated(display, server, session);
    if (!::testing::Test::HasFatalFailure())
    {
        EXPECT_FALSE(display->camera_docked_)
            << "1 seat + auto must draw the camera as an inset";
        EXPECT_EQ(1, display->layout_pane_count())
            << "an inset camera never joins the seat layout";
        // At one seat the inset is the SECOND MINIMAP (maintainer ruling):
        // the radar block mirrored — derived here from the same shared
        // placement rule the radar itself uses over this real level's grid,
        // never a typed rect.
        const int ui_w = display->gameplay_ui_canvas_w();
        const int ui_h = display->gameplay_ui_canvas_h();
        const og::view_layout::ViewLayout pane =
            og::view_layout::compute_view_layout(
                display->layout_pane_count(), 0,
                static_cast<int>(display->viewob[0]->prefs[PREF_VIEW]),
                ui_w, ui_h);
        ASSERT_TRUE(pane.applies);
        const auto [block_w, block_h] = radar_block_extents(
            display->world().grid.w, display->world().grid.h);
        const RadarBlock block = radar_block_for_pane(
            pane.y, pane.x + pane.w, pane.y + pane.h, block_w, block_h,
            /*force_lower=*/false);
        EXPECT_EQ(block.x, display->camera_view_->xloc);
        EXPECT_EQ(block.y - block.margin - block.h,
                  display->camera_view_->yloc);
        EXPECT_EQ(block.w, display->camera_view_->xview);
        EXPECT_EQ(block.h, display->camera_view_->yview);
        EXPECT_EQ(block.y, display->camera_view_->yloc +
                               display->camera_view_->yview + block.margin)
            << "the pane must sit one radar margin above the radar block";
        // And it draws at 0.25 zoom (maintainer ruling): the radar-sized rect
        // shows the kCameraMinimapZoomDenominator-times world window through
        // the camera_scale layer, so the ball arrives with its surroundings.
        EXPECT_TRUE(display->camera_minimap_zoom_)
            << "the one-seat second minimap must resolve to the 0.25 zoom";
        // The seat is byte-identical to its pre-camera geometry.
        EXPECT_EQ(seat_before.xloc, display->viewob[0]->xloc);
        EXPECT_EQ(seat_before.yloc, display->viewob[0]->yloc);
        EXPECT_EQ(seat_before.xview, display->viewob[0]->xview);
        EXPECT_EQ(seat_before.yview, display->viewob[0]->yview);
    }

    camera_soccer_teardown(display);
}
