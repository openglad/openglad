#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/zlib_api.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/legacy/base.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/picker_lobby_network_runtime.h>
#include <openglad/platform/net_transport_relay_ws.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/win_shares.h>

#include <gtest/gtest.h>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern bool g_start_game_requested;
#ifdef TESTING
extern bool g_test_remove_exits;
namespace og::sim { extern std::int32_t g_test_level_tick_limit_override; }
#endif

Sint32 go_menu(Sint32 arg1);
// The §3.8/§4.3 base-camp mutation tail (picker_team_build.cpp): roster
// re-sync + optimistic ready drop + company autosave ([SAVE-F1] merge write
// in networked lobbies). External linkage; production callers are the deploy
// dispatch, hire, train-accept and rename sites.
void picker_base_camp_after_roster_mutation();
void glad_init(bool preserve_frame_timing,
               const og::ui::PickerLobbyGameStartConfig* lobby_config);
void ready_screen_for_game_start(
    screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig* lobby_config);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

namespace og::ui {

std::vector<std::string> build_host_picker_status_lines(
    const std::string& direct_address,
    bool has_direct_transport,
    int port,
    const std::string& direct_status_message,
    const std::string& relay_room_code,
    const std::string& relay_status_message,
    std::optional<std::size_t> player_count);

} // namespace og::ui

namespace og::ui::detail {

og::sim::LobbyMessage make_join_message(
    const SaveData& save,
    std::string_view player_name,
    short local_team,
    const std::array<bool, MAX_TEAM_SIZE>* excluded_slots,
    std::size_t seat_count = 1);

int picker_lobby_network_testing_exercise_internal_helpers();

} // namespace og::ui::detail

namespace {

using namespace std::chrono_literals;

class IxNetSystemScope
{
public:
    IxNetSystemScope()
    {
        if (!ix::initNetSystem())
            throw std::runtime_error("failed to initialize IXWebSocket network system");
    }

    ~IxNetSystemScope()
    {
        (void)ix::uninitNetSystem();
    }
};

struct PickerSaveStateGuard
{
    SaveData& save;
    std::string current_campaign;
    short scen_num = 0;
    unsigned char team_size = 0;
    unsigned char numplayers = 0;
    short allied_mode = 0;
    short my_team = 0;
    // Difficulty-submenu + lobby-negotiated session fields: tests that cycle
    // these (e.g. host_and_join_difficulty_settings_sync_to_joiner_save leaves
    // respawn_mode=2 "Everyone") must not leak them into a later test's level
    // start — respawning foes make a win undetectable (shuffle-order trap).
    short respawn_mode = 0;
    short generator_rate = 0;
    short ctf_respawn_ticks = 0;
    short keep_fallen_heroes = 0;
    short cross_control = 0;
    std::unique_ptr<guy> team_list[MAX_TEAM_SIZE];

    explicit PickerSaveStateGuard(SaveData& save_in)
        : save(save_in)
        , current_campaign(save.current_campaign)
        , scen_num(save.scen_num)
        , team_size(save.team_size)
        , numplayers(save.numplayers)
        , allied_mode(save.allied_mode)
        , my_team(save.my_team)
        , respawn_mode(save.respawn_mode)
        , generator_rate(save.generator_rate)
        , ctf_respawn_ticks(save.ctf_respawn_ticks)
        , keep_fallen_heroes(save.keep_fallen_heroes)
        , cross_control(save.cross_control)
    {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[i] = std::move(save.team_list[i]);
    }

    ~PickerSaveStateGuard()
    {
        save.current_campaign = current_campaign;
        save.scen_num = scen_num;
        save.team_size = team_size;
        save.numplayers = numplayers;
        save.allied_mode = allied_mode;
        save.my_team = my_team;
        save.respawn_mode = respawn_mode;
        save.generator_rate = generator_rate;
        save.ctf_respawn_ticks = ctf_respawn_ticks;
        save.keep_fallen_heroes = keep_fallen_heroes;
        save.cross_control = cross_control;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[i] = std::move(team_list[i]);
    }
};

struct PickerRuntimeGuard
{
    bool start_requested = g_start_game_requested;
    int difficulty = og::runtime::current_session != nullptr
        ? og::runtime::current_session->current_difficulty_
        : 1;

    ~PickerRuntimeGuard()
    {
        g_start_game_requested = start_requested;
        if (og::runtime::current_session != nullptr)
            og::runtime::current_session->current_difficulty_ = difficulty;
    }
};

struct ActivePickerLobbyClientGuard
{
    og::ui::IPickerLobbyClient* saved = og::ui::active_picker_lobby_client();

    explicit ActivePickerLobbyClientGuard(og::ui::IPickerLobbyClient* client)
    {
        og::ui::install_active_picker_lobby_client(client);
    }

    ~ActivePickerLobbyClientGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

struct GameplayRunGuard
{
    float speed = og::runtime::current_session != nullptr
        ? og::runtime::current_session->g_game_speed_factor_
        : 1.0f;
#ifdef TESTING
    bool remove_exits = g_test_remove_exits;
    std::int32_t tick_limit = og::sim::g_test_level_tick_limit_override;
#endif

    ~GameplayRunGuard()
    {
        set_game_speed(speed);
#ifdef TESTING
        g_test_remove_exits = remove_exits;
        og::sim::g_test_level_tick_limit_override = tick_limit;
#endif
    }
};

struct EventScript
{
    std::vector<SDL_Event> events;
    std::size_t idx = 0;
};

int scripted_poll(void* userdata, SDL_Event* out)
{
    auto* const script = static_cast<EventScript*>(userdata);
    if (script == nullptr || script->idx >= script->events.size())
        return 0;

    *out = script->events[script->idx++];
    return 1;
}

EventScript* g_script = nullptr;

int scripted_poll_adapter(SDL_Event* out)
{
    return scripted_poll(g_script, out);
}

template <typename Predicate>
bool wait_until(Predicate&& predicate,
                std::chrono::milliseconds timeout = 3s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

bool status_lines_contain_prefix(const std::vector<std::string>& lines,
                                 std::string_view prefix)
{
    return std::any_of(
        lines.begin(),
        lines.end(),
        [prefix](const std::string& line) {
            return line.rfind(prefix, 0) == 0;
        });
}

bool status_lines_contain_exact(const std::vector<std::string>& lines,
                                std::string_view expected)
{
    return std::any_of(
        lines.begin(),
        lines.end(),
        [expected](const std::string& line) {
            return line == expected;
        });
}

// §4.3 ready gate: a networked host's StartGame is denied until every non-host
// machine is ready. Each joiner is a (session, client) pair; this readies them
// all, then drives the host server + every joiner in lockstep until each joiner
// observes its own ready. Once all joiners report ready the host's start passes
// (checking the joiners' own local_ready avoids the host-machine multi-seat
// ambiguity of scanning the host's non-host lobby rows).
struct JoinerReadyPeer
{
    og::runtime::GameSession* session = nullptr;
    og::ui::IPickerLobbyClient* client = nullptr;
};

bool ready_up_joiners(og::ui::IPickerLobbyClient& host_client,
                      const std::vector<JoinerReadyPeer>& joiners)
{
    for (const JoinerReadyPeer& joiner : joiners)
    {
        auto scope = joiner.session->activate();
        (void)joiner.client->set_ready(true);
    }
    return wait_until([&] {
        host_client.poll_and_apply();
        bool all_ready = true;
        for (const JoinerReadyPeer& joiner : joiners)
        {
            auto scope = joiner.session->activate();
            joiner.client->poll_and_apply();
            if (!joiner.client->local_ready())
                all_ready = false;
        }
        return all_ready;
    });
}

bool save_contains_named_member(const SaveData& save, std::string_view name)
{
    return std::any_of(
        save.team_list.begin(),
        save.team_list.end(),
        [name](const std::unique_ptr<guy>& member) {
            return member != nullptr && member->name == name;
        });
}

void prepare_single_member_network_save(SaveData& save,
                                        short team,
                                        const char* name,
                                        std::size_t slot_index = 0)
{
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = save.scen_num;
    save.team_size = static_cast<unsigned char>(1);
    save.numplayers = static_cast<unsigned char>(1);
    save.allied_mode = static_cast<short>(0);
    save.my_team = team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i].reset();

    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = name ? std::string(name) : std::string("Network Tester");
    member->teamnum = team;
    save.team_list[std::min<std::size_t>(slot_index, save.team_list.size() - 1)] =
        std::move(member);
}

void prepare_allied_host_network_save(SaveData& save)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = static_cast<unsigned char>(1);
    save.allied_mode = static_cast<short>(1);
    save.my_team = 0;
    for (auto& member : save.team_list)
        member.reset();

    auto alpha = std::make_unique<guy>(FAMILY_SOLDIER);
    alpha->name = "Alpha";
    alpha->teamnum = 0;
    save.team_list[0] = std::move(alpha);

    auto bravo = std::make_unique<guy>(FAMILY_SOLDIER);
    bravo->name = "Bravo";
    bravo->teamnum = 0;
    save.team_list[1] = std::move(bravo);

    save.team_size = static_cast<unsigned char>(2);
}

void prepare_allied_join_network_save(SaveData& save)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = static_cast<unsigned char>(1);
    save.allied_mode = static_cast<short>(1);
    save.my_team = 1;
    for (auto& member : save.team_list)
        member.reset();

    auto charlie = std::make_unique<guy>(FAMILY_SOLDIER);
    charlie->name = "Charlie";
    charlie->teamnum = 1;
    save.team_list[2] = std::move(charlie);

    save.team_size = static_cast<unsigned char>(1);
}

void reset_network_save_shell(SaveData& save,
                              unsigned char numplayers,
                              short my_team,
                              short allied_mode)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;
    save.numplayers = numplayers;
    save.allied_mode = allied_mode;
    save.my_team = my_team;
    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;
}

void put_named_member(SaveData& save,
                      std::size_t slot,
                      const char* name,
                      short team)
{
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = name;
    member->teamnum = team;
    save.team_list[slot] = std::move(member);
    ++save.team_size;
}

// The seven-player fixture: each machine's members sit at DISTINCT original
// save slots (host 0-1, joiner A 2-5, joiner B 6), one member per local seat
// team, so per-seat owner_save_slot tags never collide across the machines
// sharing this test process's one save0 file.
void prepare_seven_player_host_save(SaveData& save)
{
    reset_network_save_shell(save, /*numplayers=*/2, /*my_team=*/0,
                             /*allied_mode=*/1);
    put_named_member(save, 0, "HostAce", 0);
    put_named_member(save, 1, "HostBee", 1);
    save.m_totalcash[0] = 5000u;
}

void prepare_seven_player_join_a_save(SaveData& save)
{
    reset_network_save_shell(save, /*numplayers=*/4, /*my_team=*/0,
                             /*allied_mode=*/1);
    put_named_member(save, 2, "JoinAOne", 0);
    put_named_member(save, 3, "JoinATwo", 1);
    put_named_member(save, 4, "JoinAThree", 2);
    put_named_member(save, 5, "JoinAFour", 3);
    save.m_totalcash[0] = 6000u;
}

void prepare_seven_player_join_b_save(SaveData& save)
{
    reset_network_save_shell(save, /*numplayers=*/1, /*my_team=*/0,
                             /*allied_mode=*/1);
    put_named_member(save, 6, "JoinBSolo", 0);
    save.m_totalcash[0] = 7000u;
}

// Pre-session save0 for the seven-player run: stale-named members at every
// slot a machine will merge into. After the win, EVERY stale name must have
// been overlaid by the session copy — proof each machine persisted ALL of its
// seats' characters (a machine that merged only seat 0 leaves stale names).
void write_stale_seven_member_save0()
{
    SaveData stale;
    reset_network_save_shell(stale, /*numplayers=*/2, /*my_team=*/0,
                             /*allied_mode=*/1);
    put_named_member(stale, 0, "Stale0", 0);
    put_named_member(stale, 1, "Stale1", 0);
    put_named_member(stale, 2, "Stale2", 0);
    put_named_member(stale, 3, "Stale3", 0);
    put_named_member(stale, 4, "Stale4", 0);
    put_named_member(stale, 5, "Stale5", 0);
    put_named_member(stale, 6, "Stale6", 0);
    ASSERT_TRUE(stale.save("save0"));
}

std::uint32_t server_entity_id_for_user(screen& server_screen, int user)
{
    for (auto& uptr : server_screen.world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living &&
            static_cast<int>(entity->user()) == user)
        {
            return entity->entity_id();
        }
    }
    return 0u;
}

int count_free_team_livings(screen& server_screen, short team)
{
    int count = 0;
    for (auto& uptr : server_screen.world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() && !entity->dormant() &&
            entity->query_order() == Order::Living &&
            entity->team_num() == team && entity->user() == -1)
        {
            ++count;
        }
    }
    return count;
}

InputState make_seat_switch_char_input(std::size_t slot)
{
    InputState input{};
    input.players[slot].held[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[slot].pressed[static_cast<int>(InputAction::SwitchChar)] =
        true;
    return input;
}

og::sim::LobbyCharacterData make_lobby_character_data(const guy& source)
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

og::sim::LobbyCharacterSlot make_lobby_slot(std::uint8_t slot_index,
                                            std::string_view name,
                                            short team)
{
    guy member(FAMILY_SOLDIER);
    member.name = std::string(name);
    member.teamnum = team;
    return og::sim::LobbyCharacterSlot{
        .slot_index = slot_index,
        .character = make_lobby_character_data(member),
    };
}

walker* find_named_team_member(GameWorld& world,
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

std::set<std::uint32_t> non_zero_controlled_entity_ids(
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

const og::sim::EntitySnapshot* find_snapshot_entity(
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

std::string controlled_entity_name(const og::sim::GameClient& client,
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

std::string named_entity_label(const walker* entity)
{
    if (entity == nullptr)
        return "missing";
    if (entity->myguy == nullptr)
        return std::to_string(entity->entity_id());
    return entity->myguy->name + "(" + std::to_string(entity->entity_id()) + ")";
}

std::string world_entity_label(GameWorld& world, std::uint32_t entity_id)
{
    if (entity_id == 0u)
        return "0";

    walker* const entity = world.find_by_id(entity_id);
    if (entity == nullptr || entity->myguy == nullptr)
        return std::to_string(entity_id);
    return entity->myguy->name + "(" + std::to_string(entity_id) + ")";
}

std::string format_entity_id_set(GameWorld& world,
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

std::string claim_mapping_details(screen& gameplay_screen,
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

AlliedClaimObservation observe_allied_claim_mapping(
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

InputState make_single_local_switch_char_input()
{
    InputState input{};
    input.players[0].held[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    return input;
}

bool drive_host_and_join_tick(og::runtime::GameSession& host_session,
                              og::runtime::GameSession& join_session,
                              const InputState& host_input,
                              const InputState& join_input,
                              std::uint32_t tick)
{
    {
        auto host_scope = host_session.activate();
        og::runtime::local_transport_shadow_send_input(
            host_session,
            host_input,
            tick);
    }
    {
        auto join_scope = join_session.activate();
        og::runtime::local_transport_shadow_send_input(
            join_session,
            join_input,
            tick);
    }

    return wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = host_session.activate();
            og::runtime::local_transport_shadow_finish_tick(host_session);
            const og::sim::GameClient* const display_client =
                host_session.myscreen_->render_interpolation_client();
            host_ready = display_client != nullptr &&
                display_client->last_seen_server_tick() >= tick;
        }

        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const display_client =
                join_session.myscreen_->render_interpolation_client();
            join_ready = display_client != nullptr &&
                display_client->last_seen_server_tick() >= tick;
        }

        return host_ready && join_ready;
    });
}

bool drive_bounded_switch_char_attempt(og::runtime::GameSession& host_session,
                                       og::runtime::GameSession& join_session,
                                       bool host_switch,
                                       bool join_switch,
                                       std::uint32_t& next_tick)
{
    const InputState host_input =
        host_switch ? make_single_local_switch_char_input() : InputState{};
    const InputState join_input =
        join_switch ? make_single_local_switch_char_input() : InputState{};
    if (!drive_host_and_join_tick(
            host_session, join_session, host_input, join_input, next_tick++))
    {
        return false;
    }

    const InputState neutral{};
    return drive_host_and_join_tick(
        host_session, join_session, neutral, neutral, next_tick++);
}

void send_lobby_message(og::sim::ITransport& transport,
                        og::sim::PeerId peer_id,
                        og::sim::LobbyMessage message)
{
    transport.send_lobby_message(
        peer_id,
        std::make_shared<og::sim::LobbyMessage>(std::move(message)));
}

void send_start_game_message(og::sim::ITransport& transport,
                             og::sim::PeerId peer_id,
                             std::uint8_t player_index)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyStartGameMessage{
        .player_index = player_index,
    };
    send_lobby_message(transport, peer_id, std::move(message));
}

std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>>
poll_lobby_messages(og::sim::ITransport& transport)
{
    std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>> messages;

    if (transport.supports_typed_messages())
    {
        for (const auto& typed_message : transport.poll_typed())
        {
            if (typed_message.kind !=
                    og::sim::TypedReceivedMessageKind::LobbyMessage ||
                !typed_message.lobby_message)
            {
                continue;
            }

            messages.emplace_back(
                typed_message.peer_id, *typed_message.lobby_message);
        }
        return messages;
    }

    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope) ||
            envelope.message_type != og::sim::kLobbyMessageType)
        {
            continue;
        }

        const auto decoded =
            og::sim::deserialize_lobby_message(message.data);
        if (!decoded.has_value())
            continue;

        messages.emplace_back(message.peer_id, *decoded);
    }

    return messages;
}

bool wait_until_host_owns_room(og::sim::RelayWebSocketTransport& transport,
                               std::chrono::milliseconds timeout = 3s)
{
    return wait_until(
        [&] {
            (void)transport.poll();
            return transport.local_peer_id().has_value() &&
                transport.host_peer_id() == transport.local_peer_id();
        },
        timeout);
}

og::runtime::GameSession* active_game_session()
{
    if (og::runtime::current_game_session != nullptr)
        return og::runtime::current_game_session;
    return dynamic_cast<og::runtime::GameSession*>(og::runtime::current_session);
}

bool install_gameplay_runtime_from_handoff(og::ui::IPickerLobbyClient& client)
{
    og::runtime::GameSession* const session = active_game_session();
    if (session == nullptr || session->myscreen_ == nullptr)
        return false;
    return og::platform::install_picker_lobby_gameplay_runtime(
        &client,
        *session,
        *session->myscreen_);
}

std::filesystem::path campaign_archive_path_for_testing(
    std::string_view campaign_id)
{
    return std::filesystem::path(get_user_path()) / "campaigns" /
        (std::string(campaign_id) + ".glad");
}

std::string crc32_hex_for_bytes(std::string_view bytes)
{
    const auto* const data =
        reinterpret_cast<const Bytef*>(bytes.data());
    const uLong crc = crc32(0UL, data, static_cast<uInt>(bytes.size()));
    return std::format("{:08x}", static_cast<std::uint32_t>(crc));
}

std::optional<std::string> extract_query_param(std::string_view uri,
                                               std::string_view key)
{
    const std::size_t query_pos = uri.find('?');
    if (query_pos == std::string_view::npos)
        return std::nullopt;

    const std::string needle = std::format("{}=", key);
    std::size_t value_pos = uri.find(needle, query_pos + 1);
    if (value_pos == std::string_view::npos)
        return std::nullopt;
    value_pos += needle.size();

    const std::size_t value_end = uri.find('&', value_pos);
    return std::string(uri.substr(
        value_pos,
        value_end == std::string_view::npos
            ? std::string_view::npos
            : value_end - value_pos));
}

class ScopedCampaignArchive
{
public:
    explicit ScopedCampaignArchive(std::string campaign_id)
        : archive_path_(campaign_archive_path_for_testing(campaign_id))
        , campaigns_root_(std::filesystem::path(get_user_path()) / "campaigns")
    {
    }

    ~ScopedCampaignArchive()
    {
        std::error_code ec;
        std::filesystem::remove(archive_path_, ec);

        std::filesystem::path current = archive_path_.parent_path();
        while (!current.empty() && current != campaigns_root_)
        {
            if (!std::filesystem::remove(current, ec))
                break;
            current = current.parent_path();
        }
    }

    void write(std::string_view bytes) const
    {
        std::error_code ec;
        std::filesystem::create_directories(archive_path_.parent_path(), ec);

        std::FILE* const file = std::fopen(archive_path_.string().c_str(), "wb");
        if (file == nullptr)
        {
            throw std::runtime_error(std::format(
                "failed to create campaign archive fixture at {}",
                archive_path_.string()));
        }

        if (!bytes.empty())
        {
            const std::size_t written =
                std::fwrite(bytes.data(), 1, bytes.size(), file);
            if (written != bytes.size())
            {
                std::fclose(file);
                throw std::runtime_error(std::format(
                    "failed to write campaign archive fixture at {}",
                    archive_path_.string()));
            }
        }

        std::fclose(file);
    }

private:
    std::filesystem::path archive_path_;
    std::filesystem::path campaigns_root_;
};

class FakeRelayServer
{
public:
    explicit FakeRelayServer(int port,
                             int create_status_code = 200,
                             std::string create_response_body =
                                 R"({"room_code":"glad-xkcd"})",
                             std::string room_list_response_body = "[]")
        : server_(port, "127.0.0.1", 5, 16)
        , create_status_code_(create_status_code)
        , create_response_body_(std::move(create_response_body))
        , room_list_response_body_(std::move(room_list_response_body))
    {
        server_.setOnConnectionCallback(
            [this](const ix::HttpRequestPtr& request,
                   const std::shared_ptr<ix::ConnectionState>&) {
                if (request && request->method == "POST" &&
                    request->uri.rfind("/api/create", 0) == 0)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    last_create_uri_ = request->uri;

                    ix::WebSocketHttpHeaders headers;
                    headers["Content-Type"] = "application/json";
                    return std::make_shared<ix::HttpResponse>(
                        create_status_code_,
                        create_status_code_ >= 200 && create_status_code_ < 300
                            ? "OK"
                            : "ERROR",
                        ix::HttpErrorCode::Ok,
                        headers,
                        create_response_body_);
                }
                if (request && request->method == "GET" &&
                    request->uri.rfind("/api/rooms", 0) == 0)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    last_room_list_uri_ = request->uri;

                    ix::WebSocketHttpHeaders headers;
                    headers["Content-Type"] = "application/json";
                    return std::make_shared<ix::HttpResponse>(
                        200,
                        "OK",
                        ix::HttpErrorCode::Ok,
                        headers,
                        room_list_response_body_);
                }

                return std::make_shared<ix::HttpResponse>(
                    404,
                    "Not Found",
                    ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders(),
                    std::string());
            });
        server_.setOnClientMessageCallback(
            [this](std::shared_ptr<ix::ConnectionState> connection_state,
                   ix::WebSocket& websocket,
                   const ix::WebSocketMessagePtr& message) {
                handle_client_message(
                    std::move(connection_state), websocket, message);
            });

        if (!server_.listenAndStart())
            throw std::runtime_error("failed to start fake relay server");
    }

    ~FakeRelayServer()
    {
        server_.stop();
    }

    [[nodiscard]] std::string last_create_uri() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_create_uri_;
    }

    [[nodiscard]] std::string last_room_list_uri() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_room_list_uri_;
    }

private:
    struct PeerState {
        og::sim::PeerId peer_id = 0;
        std::weak_ptr<ix::WebSocket> socket;
    };

    static constexpr std::uint8_t kSendToPeerTag = 1u;
    static constexpr std::uint8_t kReceiveFromPeerTag = 2u;
    static constexpr std::uint8_t kBroadcastTag = 3u;

    void handle_client_message(
        const std::shared_ptr<ix::ConnectionState>& connection_state,
        ix::WebSocket& websocket,
        const ix::WebSocketMessagePtr& message)
    {
        if (!connection_state || !message)
            return;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
            handle_open(connection_state->getId(), websocket);
            break;

        case ix::WebSocketMessageType::Message:
            if (message->binary)
                handle_binary(connection_state->getId(), message->str);
            break;

        case ix::WebSocketMessageType::Close:
        case ix::WebSocketMessageType::Error:
            handle_close(connection_state->getId());
            break;

        default:
            break;
        }
    }

    void handle_open(const std::string& connection_id, ix::WebSocket& websocket)
    {
        const std::shared_ptr<ix::WebSocket> socket = resolve_socket(websocket);
        if (!socket)
            return;

        og::sim::PeerId peer_id = 0;
        og::sim::PeerId host_peer_id = 0;
        std::vector<og::sim::PeerId> peer_ids;
        std::vector<std::shared_ptr<ix::WebSocket>> peer_joined_targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            peer_id = next_peer_id_++;
            peers_.emplace(connection_id,
                           PeerState{
                               .peer_id = peer_id,
                               .socket = socket,
                           });
            if (!host_peer_id_.has_value())
                host_peer_id_ = peer_id;
            host_peer_id = *host_peer_id_;

            peer_ids.reserve(peers_.size());
            for (const auto& [other_connection_id, other_peer] : peers_)
            {
                peer_ids.push_back(other_peer.peer_id);
                if (other_connection_id == connection_id)
                    continue;

                if (const std::shared_ptr<ix::WebSocket> other_socket =
                        other_peer.socket.lock())
                {
                    peer_joined_targets.push_back(other_socket);
                }
            }
        }

        std::sort(peer_ids.begin(), peer_ids.end());
        send_text(socket, make_joined_text(peer_id, host_peer_id));
        send_text(socket, make_peer_list_text(peer_ids, host_peer_id));

        const std::string peer_joined_text =
            make_peer_joined_text(peer_id, host_peer_id);
        for (const auto& other_socket : peer_joined_targets)
            send_text(other_socket, peer_joined_text);
    }

    void handle_binary(const std::string& connection_id, const std::string& payload)
    {
        og::sim::PeerId source_peer_id = 0;
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(payload.data()),
            payload.size());
        if (bytes.empty())
            return;

        if (bytes[0] == kBroadcastTag)
        {
            std::vector<std::shared_ptr<ix::WebSocket>> target_sockets;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto peer_it = peers_.find(connection_id);
                if (peer_it == peers_.end())
                    return;
                source_peer_id = peer_it->second.peer_id;

                target_sockets.reserve(peers_.size());
                for (const auto& [other_connection_id, other_peer] : peers_)
                {
                    if (other_connection_id == connection_id)
                        continue;
                    if (const std::shared_ptr<ix::WebSocket> socket =
                            other_peer.socket.lock())
                    {
                        target_sockets.push_back(socket);
                    }
                }
            }

            std::string outbound;
            outbound.reserve(payload.size() + 4u);
            outbound.push_back(static_cast<char>(kReceiveFromPeerTag));
            append_peer_id(outbound, source_peer_id);
            outbound.append(payload.begin() + 1, payload.end());
            for (const auto& socket : target_sockets)
                (void)socket->sendBinary(outbound);
            return;
        }

        if (bytes.size() < 5u || bytes[0] != kSendToPeerTag)
            return;

        std::shared_ptr<ix::WebSocket> target_socket;
        const og::sim::PeerId target_peer_id =
            static_cast<og::sim::PeerId>(bytes[1]) |
            (static_cast<og::sim::PeerId>(bytes[2]) << 8) |
            (static_cast<og::sim::PeerId>(bytes[3]) << 16) |
            (static_cast<og::sim::PeerId>(bytes[4]) << 24);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto peer_it = peers_.find(connection_id);
            if (peer_it == peers_.end())
                return;
            source_peer_id = peer_it->second.peer_id;

            const auto target_it = std::find_if(
                peers_.begin(),
                peers_.end(),
                [target_peer_id](const auto& entry) {
                    return entry.second.peer_id == target_peer_id;
                });
            if (target_it == peers_.end())
                return;

            target_socket = target_it->second.socket.lock();
        }
        if (!target_socket)
            return;

        std::string outbound;
        outbound.reserve(payload.size());
        outbound.push_back(static_cast<char>(kReceiveFromPeerTag));
        append_peer_id(outbound, source_peer_id);
        outbound.append(payload.begin() + 5, payload.end());
        (void)target_socket->sendBinary(outbound);
    }

    void handle_close(const std::string& connection_id)
    {
        og::sim::PeerId peer_id = 0;
        bool was_host = false;
        std::optional<og::sim::PeerId> new_host_peer_id;
        std::vector<std::shared_ptr<ix::WebSocket>> recipients;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto peer_it = peers_.find(connection_id);
            if (peer_it == peers_.end())
                return;

            peer_id = peer_it->second.peer_id;
            was_host =
                host_peer_id_.has_value() && *host_peer_id_ == peer_id;
            peers_.erase(peer_it);

            if (was_host)
            {
                host_peer_id_.reset();
                for (const auto& [_, peer] : peers_)
                {
                    if (!host_peer_id_.has_value() ||
                        peer.peer_id < *host_peer_id_)
                    {
                        host_peer_id_ = peer.peer_id;
                    }
                }
                new_host_peer_id = host_peer_id_;
            }

            recipients.reserve(peers_.size());
            for (const auto& [_, peer] : peers_)
            {
                if (const std::shared_ptr<ix::WebSocket> socket =
                        peer.socket.lock())
                {
                    recipients.push_back(socket);
                }
            }
        }

        send_text_to_sockets(recipients, make_peer_left_text(peer_id));
        if (was_host && new_host_peer_id.has_value())
        {
            send_text_to_sockets(
                recipients,
                make_host_changed_text(*new_host_peer_id));
        }
    }

    static std::string make_joined_text(og::sim::PeerId peer_id,
                                        og::sim::PeerId host_peer_id)
    {
        return std::format(
            "{{\"type\":\"joined\",\"peer_id\":{},\"host\":{}}}",
            peer_id,
            host_peer_id);
    }

    static std::string make_peer_joined_text(og::sim::PeerId peer_id,
                                             og::sim::PeerId host_peer_id)
    {
        return std::format(
            "{{\"type\":\"peer_joined\",\"peer_id\":{},\"is_host\":{}}}",
            peer_id,
            peer_id == host_peer_id ? "true" : "false");
    }

    static std::string make_peer_left_text(og::sim::PeerId peer_id)
    {
        return std::format(
            "{{\"type\":\"peer_left\",\"peer_id\":{}}}",
            peer_id);
    }

    static std::string make_host_changed_text(og::sim::PeerId host_peer_id)
    {
        return std::format(
            "{{\"type\":\"host_changed\",\"new_host\":{}}}",
            host_peer_id);
    }

    static std::string make_peer_list_text(
        const std::vector<og::sim::PeerId>& peer_ids,
        og::sim::PeerId host_peer_id)
    {
        std::string peers_json = "[";
        for (std::size_t index = 0; index < peer_ids.size(); ++index)
        {
            if (index != 0)
                peers_json.push_back(',');
            peers_json.append(std::to_string(peer_ids[index]));
        }
        peers_json.push_back(']');

        return std::format(
            "{{\"type\":\"peer_list\",\"peers\":{},\"host\":{}}}",
            peers_json,
            host_peer_id);
    }

    static void send_text_to_sockets(
        const std::vector<std::shared_ptr<ix::WebSocket>>& sockets,
        const std::string& text)
    {
        for (const auto& socket : sockets)
            send_text(socket, text);
    }

    static void send_text(const std::shared_ptr<ix::WebSocket>& socket,
                          const std::string& text)
    {
        if (!socket)
            return;
        (void)socket->send(text);
    }

    static void append_peer_id(std::string& payload, og::sim::PeerId peer_id)
    {
        payload.push_back(static_cast<char>(peer_id & 0xffu));
        payload.push_back(static_cast<char>((peer_id >> 8) & 0xffu));
        payload.push_back(static_cast<char>((peer_id >> 16) & 0xffu));
        payload.push_back(static_cast<char>((peer_id >> 24) & 0xffu));
    }

    std::shared_ptr<ix::WebSocket> resolve_socket(ix::WebSocket& websocket)
    {
        for (const auto& client : server_.getClients())
        {
            if (client.get() == &websocket)
                return client;
        }
        return {};
    }

    ix::HttpServer server_;
    mutable std::mutex mutex_;
    og::sim::PeerId next_peer_id_ = 1;
    std::optional<og::sim::PeerId> host_peer_id_;
    std::map<std::string, PeerState> peers_;
    int create_status_code_ = 200;
    std::string create_response_body_;
    std::string room_list_response_body_;
    std::string last_create_uri_;
    std::string last_room_list_uri_;
};

} // namespace

TEST(PickerNetworkClient, host_direct_flow_syncs_save_and_builds_start_config)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 2, "Host");
    save.my_team = MAX_PLAYERS;
    g_start_game_requested = false;
    og::runtime::current_session->current_difficulty_ = 4;

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    ASSERT_NE(nullptr, active_game_session());
    ASSERT_NE(nullptr, active_game_session()->myscreen_);
    EXPECT_FALSE(install_gameplay_runtime_from_handoff(*host_client));
    host_client->initialize_from_save();

    EXPECT_EQ(2, save.my_team);
    EXPECT_EQ(1u, save.team_size);
    ASSERT_TRUE(save.team_list[0] != nullptr);
    EXPECT_EQ("Host", save.team_list[0]->name);
    EXPECT_TRUE(status_lines_contain_prefix(host_client->status_lines(), "LAN: "));
    EXPECT_TRUE(
        status_lines_contain_exact(host_client->status_lines(), "Lobby: 1 player"));
    EXPECT_TRUE(host_client->host_controls_visible());

    save.scen_num = 2;
    save.allied_mode = 1;
    save.team_list[0]->name = "Host Prime";
    og::runtime::current_session->current_difficulty_ = 6;
    host_client->sync_from_save();
    host_client->sync_settings_from_save();
    host_client->sync_roster_from_save();
    host_client->set_player_mode(0);

    ASSERT_TRUE(host_client->request_start_game());
    EXPECT_FALSE(host_client->start_request_pending());
    ASSERT_TRUE(host_client->has_game_start_config());
    ASSERT_TRUE(host_client->build_game_start_config().has_value());

    const auto start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ("org.openglad.gladiator", start_config->save_data.current_campaign);
    EXPECT_EQ(2, start_config->save_data.scen_num);
    EXPECT_EQ(0u, start_config->save_data.numplayers);
    EXPECT_EQ(1, start_config->save_data.allied_mode);
    EXPECT_EQ(6, start_config->difficulty);
    EXPECT_EQ(0, start_config->my_team);
    ASSERT_EQ(1u, start_config->save_data.team_list.size());
    EXPECT_EQ("Host Prime", start_config->save_data.team_list[0].character.name);
    EXPECT_EQ(0, start_config->save_data.team_list[0].character.teamnum);
    EXPECT_EQ(0, start_config->save_data.team_list[0].owner_player_index);
    EXPECT_EQ(0, start_config->save_data.team_list[0].owner_save_slot);
    EXPECT_FALSE(host_client->consume_game_start_config().has_value());

    ASSERT_TRUE(save.save("save0"));
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*host_client));
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));
    og::runtime::clear_local_transport_shadow(*active_game_session());

    host_client->shutdown();
}

TEST(PickerNetworkClient, host_relay_flow_uses_campaign_content_hash)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    save.current_campaign = "relay test/space";
    ScopedCampaignArchive archive(save.current_campaign);
    archive.write("relay hash fixture v1");
    g_start_game_requested = false;

    const int relay_port = ix::getFreePort();
    FakeRelayServer relay_server(
        relay_port,
        200,
        R"({"code":"glad-xkcd","owner_token":"owner-secret-token"})");

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    options.enable_relay = true;
    options.relay_base_url = std::format("ws://127.0.0.1:{}", relay_port);
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    host_client->initialize_from_save();

    const auto status = host_client->status_lines();
    EXPECT_TRUE(status_lines_contain_prefix(status, "LAN: "));
    EXPECT_TRUE(status_lines_contain_exact(status, "Room: GLAD-XKCD"));
    EXPECT_TRUE(status_lines_contain_exact(status, "Lobby: 1 player"));

    const std::string create_uri = relay_server.last_create_uri();
    ASSERT_NE(std::string::npos, create_uri.find("/api/create?campaign="));
    const auto first_campaign_hash = extract_query_param(create_uri, "campaign");
    ASSERT_TRUE(first_campaign_hash.has_value());
    EXPECT_EQ(crc32_hex_for_bytes("relay hash fixture v1"), *first_campaign_hash);
    EXPECT_NE(
        std::string::npos,
        create_uri.find("&campaign_name=relay%20test%2Fspace"));
    EXPECT_NE(std::string::npos, create_uri.find("&host=Host"));
    EXPECT_NE(std::string::npos, create_uri.find("%20"));
    EXPECT_NE(std::string::npos, create_uri.find("%2F"));
    const auto parsed_create =
        og::ui::detail::parse_relay_room_create_response(
            R"({"code":"glad-xkcd","owner_token":"owner-secret-token"})");
    EXPECT_EQ("glad-xkcd", parsed_create.room_code);
    EXPECT_EQ("owner-secret-token", parsed_create.owner_token);
    EXPECT_EQ(
        std::format(
            "ws://127.0.0.1:{}/api/room/GLAD-XKCD?owner_token=owner-secret-token",
            relay_port),
        og::ui::detail::build_relay_room_websocket_url(
            std::format("ws://127.0.0.1:{}", relay_port),
            "GLAD-XKCD",
            "owner-secret-token"));

    host_client->shutdown();

    archive.write("relay hash fixture v2");
    auto host_client_restarted = og::ui::create_host_picker_lobby_client(options);
    host_client_restarted->initialize_from_save();

    ASSERT_TRUE(wait_until([&] {
        return relay_server.last_create_uri() != create_uri;
    }));

    const auto second_campaign_hash =
        extract_query_param(relay_server.last_create_uri(), "campaign");
    ASSERT_TRUE(second_campaign_hash.has_value());
    EXPECT_EQ(crc32_hex_for_bytes("relay hash fixture v2"), *second_campaign_hash);
    EXPECT_NE(*first_campaign_hash, *second_campaign_hash);

    host_client_restarted->shutdown();
}

TEST(PickerNetworkClient,
     go_menu_host_handoff_reinitializes_host_lobby_without_port_conflict)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_guard;
    prepare_single_member_network_save(save, 0, "Host");
    g_start_game_requested = false;
#ifdef TESTING
    g_test_remove_exits = true;
    og::sim::g_test_level_tick_limit_override = 15;
#endif
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    ActivePickerLobbyClientGuard active_guard(host_client.get());

    host_client->initialize_from_save();
    ASSERT_EQ(button_action_id(ButtonAction::CreateTeamMenu), go_menu(0));
    EXPECT_FALSE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_TRUE(
        status_lines_contain_prefix(host_client->status_lines(), "LAN: "));

    host_client->shutdown();
}

// P2: between levels the host returns to the team-build menu over the SAME
// connection. glad_main tears down the per-level runtime (clear_local_transport
// _shadow); picker_reinitialize_lobby_after_game then RESUMES the lobby on the
// surviving transport (no port rebind, no reconnect) and the host is ready to
// start the next level.
TEST(PickerNetworkClient,
     picker_reinitialize_after_game_resumes_host_lobby_on_surviving_connection)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    struct TestCleanupGuard
    {
        og::ui::IPickerLobbyClient* client = nullptr;

        ~TestCleanupGuard()
        {
            if (og::runtime::GameSession* const session = active_game_session())
                og::runtime::clear_local_transport_shadow(*session);
            if (client != nullptr)
                client->shutdown();
        }
    } cleanup{host_client.get()};
    ActivePickerLobbyClientGuard active_guard(host_client.get());

    host_client->initialize_from_save();
    ASSERT_TRUE(save.save("save0"));
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*host_client));
    ASSERT_NE(nullptr, active_game_session());
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));

    // glad_main exit tears down the per-level runtime (the connection survives
    // because the host client kept its own transport refs).
    og::runtime::clear_local_transport_shadow(*active_game_session());
    EXPECT_FALSE(og::runtime::local_transport_active(*active_game_session()));

    // Resume the lobby on the surviving connection — must not reconnect/rebind,
    // must leave the host hosting and ready for the next level.
    EXPECT_NO_THROW(picker_reinitialize_lobby_after_game());
    EXPECT_FALSE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_TRUE(
        status_lines_contain_prefix(host_client->status_lines(), "LAN: "));
    EXPECT_TRUE(
        status_lines_contain_exact(host_client->status_lines(), "Lobby: 1 player"));
    EXPECT_FALSE(picker_lobby_start_request_pending());

    // The host can immediately start the next level over the SAME connection.
    EXPECT_TRUE(host_client->request_start_game());
}

TEST(PickerNetworkClient, relay_room_listing_fetches_matching_rooms)
{
    IxNetSystemScope net_system;

    const std::string campaign_id = "relay/filter hash";
    ScopedCampaignArchive archive(campaign_id);
    archive.write("relay room listing fixture");
    const std::string expected_hash =
        crc32_hex_for_bytes("relay room listing fixture");

    const int relay_port = ix::getFreePort();
    FakeRelayServer relay_server(
        relay_port,
        200,
        R"({"code":"glad-xkcd"})",
        std::format(
            R"([{{"code":"glad-xkcd","campaign_hash":"{}","campaign_name":"Relay Filter","host_name":"Host","player_count":2,"created_at":1234}}])",
            expected_hash));

    const auto rooms = og::ui::list_relay_rooms(
        std::format("http://127.0.0.1:{}", relay_port),
        campaign_id);

    const auto requested_hash =
        extract_query_param(relay_server.last_room_list_uri(), "campaign");
    ASSERT_TRUE(requested_hash.has_value());
    EXPECT_EQ(expected_hash, *requested_hash);

    ASSERT_EQ(1u, rooms.size());
    EXPECT_EQ("GLAD-XKCD", rooms.front().code);
    EXPECT_EQ(expected_hash, rooms.front().campaign_hash);
    EXPECT_EQ("Host", rooms.front().host_name);
    EXPECT_EQ(2u, rooms.front().player_count);
    EXPECT_EQ(1234, rooms.front().created_at_ms);
}

TEST(PickerNetworkClient, host_status_builder_reports_direct_and_relay_errors)
{
    const auto lines = og::ui::build_host_picker_status_lines(
        "192.168.1.5",
        false,
        12345,
        "bind failed",
        "",
        "room service down",
        2u);

    EXPECT_TRUE(status_lines_contain_exact(lines, "Direct: bind failed"));
    EXPECT_TRUE(status_lines_contain_exact(lines, "Relay: room service down"));
    EXPECT_TRUE(status_lines_contain_exact(lines, "Lobby: 2 players"));
}

TEST(PickerNetworkClient, join_direct_flow_receives_remote_host_start_and_syncs_roster)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Joiner");
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    auto server_transport =
        std::make_shared<og::sim::WebSocketServerTransport>(port);
    server_transport->accept_connections();
    SaveData remote_host_save;
    prepare_single_member_network_save(remote_host_save, 0, "Remote Host");
    remote_host_save.scen_num = 5;
    remote_host_save.allied_mode = 1;

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Direct;
    options.direct_endpoint = std::format("127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    ASSERT_NE(nullptr, active_game_session());
    ASSERT_NE(nullptr, active_game_session()->myscreen_);
    EXPECT_FALSE(install_gameplay_runtime_from_handoff(*join_client));
    join_client->initialize_from_save();

    og::sim::PeerId join_peer_id = 0;
    og::sim::LobbyMessage join_message;
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (message.kind() != og::sim::LobbyMessageKind::Join)
                continue;

            join_peer_id = peer_id;
            join_message = std::move(message);
            return true;
        }
        return false;
    })) << "direct join client should connect and send its join message";

    ASSERT_EQ(og::sim::LobbyMessageKind::Join, join_message.kind());
    og::sim::LobbyPlayer join_player =
        std::get<og::sim::LobbyJoinMessage>(join_message.payload).player;
    join_player.player_index = 1u;
    join_player.team = 1;
    for (auto& slot : join_player.character_slots)
        slot.character.teamnum = 1;
    join_player.ready = false;
    join_player.is_host = false;

    og::sim::LobbyState state;
    state.settings.campaign_id = remote_host_save.current_campaign;
    state.settings.scenario_id = remote_host_save.scen_num;
    state.settings.difficulty = 7;
    state.settings.allied_mode = remote_host_save.allied_mode;
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "Remote Host",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Remote Host", 0)},
        .ready = false,
        .is_host = true,
    });
    state.players.push_back(join_player);
    server_transport->send_lobby_state(
        join_peer_id, std::make_shared<og::sim::LobbyState>(state));

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
                   join_client->status_lines(),
                   "Lobby: 2 players");
    })) << "direct join client should receive injected lobby state";

    EXPECT_EQ(1, save.my_team);
    EXPECT_FALSE(join_client->host_controls_visible());
    EXPECT_TRUE(status_lines_contain_prefix(
        join_client->status_lines(),
        std::format("Direct: ws://127.0.0.1:{}", port)));
    EXPECT_TRUE(
        status_lines_contain_exact(join_client->status_lines(), "Status: connected"));

    std::size_t local_slot_index = save.team_list.size();
    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        const auto& member = save.team_list[slot_index];
        if (member != nullptr &&
            member->name == "Joiner" &&
            join_client->is_save_slot_editable(slot_index))
        {
            local_slot_index = slot_index;
            break;
        }
    }
    ASSERT_LT(local_slot_index, save.team_list.size());
    save.team_list[local_slot_index]->name = "Joiner Prime";
    join_client->sync_from_save();
    og::sim::LobbyMessage sync_message;
    join_client->sync_settings_from_save();
    join_client->set_player_mode(0);
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (peer_id != join_peer_id ||
                message.kind() != og::sim::LobbyMessageKind::Join)
            {
                continue;
            }

            sync_message = std::move(message);
            return true;
        }
        return false;
    })) << "direct join client should re-send its join roster after local save changes";

    const auto& synced_join =
        std::get<og::sim::LobbyJoinMessage>(sync_message.payload);
    ASSERT_EQ(1u, synced_join.player.character_slots.size());
    EXPECT_EQ("Joiner Prime",
              synced_join.player.character_slots.front().character.name);

    join_player = synced_join.player;
    join_player.player_index = 1u;
    join_player.ready = false;
    join_player.is_host = false;
    state.players[1] = join_player;
    server_transport->send_lobby_state(
        join_peer_id, std::make_shared<og::sim::LobbyState>(state));
    send_start_game_message(*server_transport, join_peer_id, 0u);

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return join_client->has_game_start_config();
    })) << "direct join client should receive the host start-game handoff";

    EXPECT_FALSE(join_client->start_request_pending());
    ASSERT_TRUE(join_client->build_game_start_config().has_value());
    const auto start_config = join_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ("org.openglad.gladiator", start_config->save_data.current_campaign);
    EXPECT_EQ(5, start_config->save_data.scen_num);
    EXPECT_EQ(0u, start_config->save_data.numplayers);
    EXPECT_EQ(7, start_config->difficulty);
    EXPECT_EQ(0, start_config->my_team);
    ASSERT_EQ(2u, start_config->save_data.team_list.size());
    EXPECT_EQ(0, start_config->save_data.team_list[0].character.teamnum);
    EXPECT_EQ(0, start_config->save_data.team_list[1].character.teamnum);

    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*join_client));
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));
    og::runtime::clear_local_transport_shadow(*active_game_session());

    // P2: the lobby connection PERSISTS across gameplay. Installing the gameplay
    // runtime hands the transport to the per-level runtime but the join client
    // keeps its own ref, so after the runtime is torn down the socket is still
    // open — the team-build menu reuses it for the next level instead of
    // reconnecting. (Pre-P2 this reset the transport and reverted to
    // "connecting".)
    join_client->poll_and_apply();
    EXPECT_TRUE(
        status_lines_contain_exact(join_client->status_lines(), "Status: connected"));

    join_client->shutdown();
}

TEST(PickerNetworkClient,
     join_direct_flow_keeps_private_roster_while_waiting_for_server_echo)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Joiner", 4);
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    auto server_transport =
        std::make_shared<og::sim::WebSocketServerTransport>(port);
    server_transport->accept_connections();

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Direct;
    options.direct_endpoint = std::format("127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    og::sim::PeerId join_peer_id = 0;
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (message.kind() != og::sim::LobbyMessageKind::Join)
                continue;

            join_peer_id = peer_id;
            return true;
        }
        return false;
    })) << "join client should connect and transmit its pending loaded team";

    og::sim::LobbyState host_only_state;
    host_only_state.settings.campaign_id = save.current_campaign;
    host_only_state.settings.scenario_id = 3;
    host_only_state.settings.difficulty = 2;
    host_only_state.settings.allied_mode = save.allied_mode;
    host_only_state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "Remote Host",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Remote Host", 0)},
        .ready = false,
        .is_host = true,
    });
    server_transport->send_lobby_state(
        join_peer_id,
        std::make_shared<og::sim::LobbyState>(host_only_state));

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Lobby: 1 player");
    })) << "join client should apply the host-only lobby snapshot";

    EXPECT_EQ(1, save.my_team);
    EXPECT_EQ(1u, save.team_size);
    EXPECT_FALSE(save_contains_named_member(save, "Remote Host"));
    EXPECT_TRUE(save_contains_named_member(save, "Joiner"));

    std::size_t joiner_slot_index = save.team_list.size();
    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        const auto& member = save.team_list[slot_index];
        if (member == nullptr)
            continue;
        if (member->name == "Joiner")
            joiner_slot_index = slot_index;
    }

    ASSERT_LT(joiner_slot_index, save.team_list.size());
    EXPECT_EQ(4u, joiner_slot_index)
        << "the lobby must preserve the owner's private save-slot identity";
    EXPECT_TRUE(join_client->is_save_slot_editable(joiner_slot_index));

    join_client->shutdown();
}

TEST(PickerNetworkClient,
     join_runtime_install_keeps_non_host_control_slot_after_runtime_handoff)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 1, "Joiner");
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    auto server_transport =
        std::make_shared<og::sim::WebSocketServerTransport>(port);
    server_transport->accept_connections();
    SaveData remote_host_save;
    prepare_single_member_network_save(remote_host_save, 0, "Remote Host");
    remote_host_save.scen_num = 1;
    remote_host_save.allied_mode = 1;

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Direct;
    options.direct_endpoint = std::format("127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    og::sim::PeerId join_peer_id = 0;
    og::sim::LobbyMessage join_message;
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (message.kind() != og::sim::LobbyMessageKind::Join)
                continue;

            join_peer_id = peer_id;
            join_message = std::move(message);
            return true;
        }
        return false;
    })) << "join client should connect and send its join message";

    ASSERT_EQ(og::sim::LobbyMessageKind::Join, join_message.kind());
    og::sim::LobbyPlayer join_player =
        std::get<og::sim::LobbyJoinMessage>(join_message.payload).player;
    join_player.player_index = 1u;
    join_player.ready = false;
    join_player.is_host = false;

    og::sim::LobbyState state;
    state.settings.campaign_id = remote_host_save.current_campaign;
    state.settings.scenario_id = remote_host_save.scen_num;
    state.settings.difficulty = 3;
    state.settings.allied_mode = remote_host_save.allied_mode;
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "Remote Host",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Remote Host", 0)},
        .ready = false,
        .is_host = true,
    });
    state.players.push_back(join_player);
    server_transport->send_lobby_state(
        join_peer_id, std::make_shared<og::sim::LobbyState>(state));

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Lobby: 2 players");
    })) << "join client should receive lobby state before runtime handoff";

    send_start_game_message(*server_transport, join_peer_id, 0u);
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return join_client->has_game_start_config();
    })) << "join client should receive the host start-game handoff";

    ASSERT_NE(nullptr, active_game_session());
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*join_client));
    ASSERT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    ASSERT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));

    screen* const gameplay_screen = active_game_session()->myscreen_;
    ASSERT_NE(nullptr, gameplay_screen);
    ASSERT_NE(nullptr, gameplay_screen->viewob[0]);
    gameplay_screen->world().end = 0;
    gameplay_screen->viewob[0]->my_team = 0;

    auto make_same_team_control = [&gameplay_screen](int family,
                                                     std::string_view name) {
        walker* const actor =
            gameplay_screen->world().add_ob(Order::Living, family);
        actor->set_owned_myguy(std::make_unique<guy>(family));
        actor->myguy->name = std::string(name);
        actor->myguy->teamnum = 0;
        actor->set_team_num(0);
        actor->set_real_team_num(255);
        return actor;
    };

    walker* const host_control =
        make_same_team_control(FAMILY_SOLDIER, "Host Control");
    walker* const guest_control =
        make_same_team_control(FAMILY_ARCHER, "Guest Control");
    walker* const switched_control =
        make_same_team_control(FAMILY_MAGE, "Guest Switched");
    ASSERT_NE(nullptr, host_control);
    ASSERT_NE(nullptr, guest_control);
    ASSERT_NE(nullptr, switched_control);

    gameplay_screen->world().tick_count_ = 1u;
    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(gameplay_screen->world());
    server_transport->send_snapshot(
        join_peer_id,
        std::make_shared<og::sim::WorldSnapshot>(snapshot));

    auto send_control_change = [&](std::uint8_t player_index,
                                   std::uint32_t entity_id) {
        og::sim::ControlChangeMessage message;
        message.player_index = player_index;
        message.entity_id = entity_id;
        server_transport->send_control_change(
            join_peer_id,
            std::make_shared<og::sim::ControlChangeMessage>(message));
    };

    send_control_change(0u, host_control->entity_id());
    send_control_change(1u, guest_control->entity_id());

    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(*active_game_session());
        return gameplay_screen->viewob[0]->control != nullptr &&
            gameplay_screen->viewob[0]->control->entity_id() ==
                guest_control->entity_id();
    })) << "non-host client should keep its own control slot after runtime install";

    send_control_change(1u, switched_control->entity_id());
    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(*active_game_session());
        return gameplay_screen->viewob[0]->control != nullptr &&
            gameplay_screen->viewob[0]->control->entity_id() ==
                switched_control->entity_id();
    })) << "non-host client should follow its own switched control slot";

    og::runtime::clear_local_transport_shadow(*active_game_session());
    join_client->shutdown();
}

TEST(PickerNetworkClient,
     join_runtime_applies_team_and_survives_split_endgame_transition)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 1, "Joiner");
    save.allied_mode = 0;
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    auto server_transport =
        std::make_shared<og::sim::WebSocketServerTransport>(port);
    server_transport->accept_connections();

    SaveData remote_host_save;
    prepare_single_member_network_save(remote_host_save, 0, "Remote Host");
    remote_host_save.scen_num = 1;
    remote_host_save.allied_mode = 0;

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Direct;
    options.direct_endpoint = std::format("127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    og::sim::PeerId join_peer_id = 0;
    og::sim::LobbyMessage join_message;
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (message.kind() != og::sim::LobbyMessageKind::Join)
                continue;

            join_peer_id = peer_id;
            join_message = std::move(message);
            return true;
        }
        return false;
    })) << "join client should connect and send its join message";

    ASSERT_EQ(og::sim::LobbyMessageKind::Join, join_message.kind());
    og::sim::LobbyPlayer join_player =
        std::get<og::sim::LobbyJoinMessage>(join_message.payload).player;
    join_player.player_index = 1u;
    join_player.ready = false;
    join_player.is_host = false;
    join_player.team = 1;
    for (auto& slot : join_player.character_slots)
        slot.character.teamnum = 1;

    og::sim::LobbyState state;
    state.settings.campaign_id = remote_host_save.current_campaign;
    state.settings.scenario_id = remote_host_save.scen_num;
    state.settings.difficulty = 2;
    state.settings.allied_mode = 0;
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "Remote Host",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Remote Host", 0)},
        .ready = false,
        .is_host = true,
    });
    state.players.push_back(join_player);
    server_transport->send_lobby_state(
        join_peer_id, std::make_shared<og::sim::LobbyState>(state));

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Lobby: 2 players");
    })) << "join client should receive lobby state before runtime handoff";

    send_start_game_message(*server_transport, join_peer_id, 0u);
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return join_client->has_game_start_config();
    })) << "join client should receive the host start-game handoff";

    ASSERT_NE(nullptr, active_game_session());
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*join_client));
    ASSERT_TRUE(og::runtime::local_transport_active(*active_game_session()));

    screen* const gameplay_screen = active_game_session()->myscreen_;
    ASSERT_NE(nullptr, gameplay_screen);
    ASSERT_NE(nullptr, gameplay_screen->viewob[0]);

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = 1;
    initial_setup.current_scenario = 1;
    initial_setup.my_team = 1;
    initial_setup.allied_mode = 0;
    server_transport->send_initial_setup(
        join_peer_id,
        std::make_shared<og::sim::InitialSetupMessage>(initial_setup));
    og::sim::WorldSnapshot initial_snapshot =
        og::sim::capture_keyframe_snapshot(gameplay_screen->world());
    // The display world is reused across picker tests. Make this synthetic
    // authoritative snapshot agree with InitialSetup instead of inheriting a
    // previous test's local team.
    initial_snapshot.my_team = initial_setup.my_team;
    initial_snapshot.allied_mode = initial_setup.allied_mode;
    initial_snapshot.snapshot_hash =
        og::sim::compute_snapshot_hash(initial_snapshot);
    server_transport->send_snapshot(
        join_peer_id,
        std::make_shared<og::sim::WorldSnapshot>(std::move(initial_snapshot)));

    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(*active_game_session());
        return gameplay_screen->world().my_team == 1;
    })) << "client runtime should apply the authoritative initial setup";

    EXPECT_EQ(1, gameplay_screen->world().my_team);
    EXPECT_EQ(1, gameplay_screen->viewob[0]->my_team)
        << "client-only runtime should mirror the authoritative team onto the "
           "display view after initial setup";

    // Dedicated transports may split EndGame and the next InitialSetup across
    // websocket frames (or the per-tick inbound message budget). The old world
    // legitimately has end=1 after this first batch, but the runtime must keep
    // polling instead of permanently latching the session as finished.
    og::sim::SimEventBatch endgame_batch;
    endgame_batch.sequence = 1u;
    endgame_batch.events.push_back({
        .tick = gameplay_screen->world().tick_count_,
        .kind = og::sim::EventKind::EndGame,
        .a = 0u,
        .b = 2u,
        .text = {},
    });
    server_transport->send_game_flow_event_batch(
        join_peer_id,
        std::make_shared<og::sim::SimEventBatch>(endgame_batch));

    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(*active_game_session());
        return gameplay_screen->world().end != 0;
    })) << "the first frame should dispatch EndGame on the old display world";
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));

    // Prove that an empty frame between the two messages does not turn the
    // transient old-world end flag into a sticky terminal latch.
    og::runtime::local_transport_shadow_finish_tick(*active_game_session());
    EXPECT_NE(0, gameplay_screen->world().end);

    og::sim::InitialSetupMessage next_setup;
    next_setup.level_id = 2;
    next_setup.current_scenario = 2;
    next_setup.my_team = 1;
    next_setup.allied_mode = 0;
    server_transport->send_initial_setup(
        join_peer_id,
        std::make_shared<og::sim::InitialSetupMessage>(next_setup));

    ASSERT_TRUE(wait_until([&] {
        og::runtime::local_transport_shadow_finish_tick(*active_game_session());
        return gameplay_screen->world().current_scenario == 2 &&
            gameplay_screen->world().end == 0;
    })) << "a later InitialSetup should revive the same client runtime";
    EXPECT_EQ(1, gameplay_screen->viewob[0]->my_team);

    og::runtime::local_transport_shadow_finish_tick(*active_game_session());
    EXPECT_EQ(0, gameplay_screen->world().end)
        << "the transition latch must clear after the new level is installed";

    og::runtime::clear_local_transport_shadow(*active_game_session());
    join_client->shutdown();
}

TEST(PickerNetworkClient, host_escape_abort_signals_join_runtime_to_end_session)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host");
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(join_session.myscreen_->save_data,
                                       1,
                                       "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                    {
                        if (view != nullptr)
                            view->control = nullptr;
                    }
                    join_session->myscreen_->world().delete_objects();
                }
            }

            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                    {
                        if (view != nullptr)
                            view->control = nullptr;
                    }
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_NE(nullptr, cleanup.host_session);

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();

        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(),
                "Lobby: 2 players");
        }

        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join clients should converge on the same two-player lobby";

    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}))
        << "§4.3: the joiner must be ready before the host may start";
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();

        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }

        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff before runtime install";

    const auto host_start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(host_start_config.has_value());
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_start_config);
        glad_init(false, &*host_start_config);
    }

    std::optional<og::ui::PickerLobbyGameStartConfig> join_start_config;
    {
        auto join_scope = join_session.activate();
        join_start_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_start_config.has_value());
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(*join_session.myscreen_, &*join_start_config);
        glad_init(false, &*join_start_config);
    }

    screen* const host_gameplay_screen = cleanup.host_session->myscreen_;
    ASSERT_NE(nullptr, host_gameplay_screen);
    ASSERT_TRUE(wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_finish_tick(*cleanup.host_session);
            const og::sim::GameClient* const display_client =
                cleanup.host_session->myscreen_->render_interpolation_client();
            host_ready =
                display_client != nullptr && display_client->baseline().has_value();
        }

        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const display_client =
                join_session.myscreen_->render_interpolation_client();
            join_ready =
                display_client != nullptr && display_client->baseline().has_value();
        }

        return host_ready && join_ready;
    })) << "both runtimes should receive their initial gameplay snapshots";

    struct EscapeFrameOutcome
    {
        GameFrameResult result = GameFrameResult::Continue;
        bool done = false;
        int redrawme = 0;
    };

    picker_testing_yes_or_no_queue_clear();
    {
        auto host_scope = cleanup.host_session->activate();
        GameplayRunGuard gameplay_run_guard;
        set_game_speed(0.0f);

        const auto run_host_escape_frame = [&]() -> EscapeFrameOutcome {
            EventScript script;
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = SDLK_ESCAPE;
            script.events.push_back(event);
            g_script = &script;
            host_gameplay_screen->redrawme = 0;

            GameLoopFrameState st;
            GameLoopDeps deps;
            deps.enable_render = false;
            deps.enable_event_poll = true;
            deps.enable_frame_timing = false;
            deps.poll_event = scripted_poll_adapter;

            const GameFrameResult result =
                game_frame_with_result(*host_gameplay_screen, st, deps);
            g_script = nullptr;
            return {
                .result = result,
                .done = st.done,
                .redrawme = static_cast<int>(host_gameplay_screen->redrawme),
            };
        };

        const EscapeFrameOutcome pause_frame = run_host_escape_frame();
        ASSERT_EQ(GameFrameResult::Continue, pause_frame.result);
        ASSERT_FALSE(pause_frame.done);
        ASSERT_EQ(1, pause_frame.redrawme);
        ASSERT_TRUE(host_gameplay_screen->world().paused);

        picker_testing_yes_or_no_queue_push(true);
        const EscapeFrameOutcome abort_frame = run_host_escape_frame();
        EXPECT_EQ(GameFrameResult::AbortedMission, abort_frame.result);
        EXPECT_TRUE(abort_frame.done);
    }
    picker_testing_yes_or_no_queue_clear();

    ASSERT_TRUE(wait_until([&] {
        auto join_scope = join_session.activate();
        og::runtime::local_transport_shadow_finish_tick(join_session);
        return join_session.myscreen_->world().end != 0;
    })) << "remote gameplay runtime should abort after the host/server aborts";

    {
        auto join_scope = join_session.activate();
        EXPECT_EQ(1, static_cast<int>(join_session.myscreen_->world().end));
    }
}

// End-to-end host+join REAL win of level 1 -> level advances -> level 2 loads on
// BOTH peers and stays live (gaps 1, 3, 5), exercising the real
// local_transport_shadow complete_level_and_load_next path. Also a regression
// guard for the freeze bug: on a win the display ran screen::endgame (world.end=1)
// and the next-level load did not clear it, so finish_tick tore the session down
// ("in a level but no one can move"). The win is made deterministic by clearing
// the authoritative server world's foes via a test-only accessor (no flaky
// real-time combat).
TEST(PickerNetworkClient, host_and_join_real_win_returns_both_peers_to_menu)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    // Allied co-op: both players are on team 0, so the foes we clear are NPCs
    // (other teams), never a player character.
    prepare_allied_host_network_save(host_save);
    g_start_game_requested = false;
#ifdef TESTING
    g_test_remove_exits = true; // so clearing foes leaves level_done==2 (a win)
#endif
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    join_session->myscreen_->world().delete_objects();
                }
            }
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}))
        << "§4.3: the joiner must be ready before the host may start";
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }
        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff";

    const auto host_start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(host_start_config.has_value());
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_start_config);
        glad_init(false, &*host_start_config);
    }
    {
        auto join_scope = join_session.activate();
        auto join_start_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_start_config.has_value());
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(*join_session.myscreen_, &*join_start_config);
        glad_init(false, &*join_start_config);
    }

    screen* const host_gameplay_screen = cleanup.host_session->myscreen_;
    ASSERT_NE(nullptr, host_gameplay_screen);
    ASSERT_TRUE(wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_finish_tick(*cleanup.host_session);
            const og::sim::GameClient* const dc =
                cleanup.host_session->myscreen_->render_interpolation_client();
            host_ready = dc != nullptr && dc->baseline().has_value();
        }
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const dc =
                join_session.myscreen_->render_interpolation_client();
            join_ready = dc != nullptr && dc->baseline().has_value();
        }
        return host_ready && join_ready;
    })) << "both runtimes should receive their initial gameplay snapshots";

    // Both peers must currently be on level 1 and live.
    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_EQ(0, static_cast<int>(host_gameplay_screen->world().end));
    }

    // --- Force a deterministic WIN: clear every foe in the AUTHORITATIVE server
    // world (exits already removed by g_test_remove_exits). The sim then sets
    // game_ended/next_level on the next tick and the real transition path runs.
    {
        screen* const server_screen =
            og::runtime::local_transport_shadow_testing_server_screen(
                *cleanup.host_session);
        ASSERT_NE(nullptr, server_screen)
            << "host should expose its authoritative server screen";
        const unsigned char my_team =
            static_cast<unsigned char>(server_screen->world().my_team);
        std::size_t killed = 0;
        for (auto& uptr : server_screen->world().oblist)
        {
            walker* const w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->is_friendly_to_team(my_team) == 0)
            {
                w->set_dead(1);
                ++killed;
            }
        }
        EXPECT_GT(killed, 0u) << "level 1 should contain foes to clear";
    }

    // Pump both sims: the server detects the win and (return-to-lobby mode)
    // finalizes per-player progress + advances the campaign cursor WITHOUT
    // loading the next level, then forwards a terminal EndGame so every display
    // ends its session (world.end=1 -> glad_main returns to the team-build menu).
    // Each peer sends neutral input every tick (the heartbeat the server needs).
    const InputState neutral{};
    std::uint32_t pump_tick = 1;
    const auto pump = [&](int iterations) {
        for (int i = 0; i < iterations; ++i, ++pump_tick)
        {
            {
                auto host_scope = cleanup.host_session->activate();
                og::runtime::local_transport_shadow_send_input(
                    *cleanup.host_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
            }
            {
                auto join_scope = join_session.activate();
                og::runtime::local_transport_shadow_send_input(
                    join_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(join_session);
            }
        }
    };

    const auto peer_finished = [&](og::runtime::GameSession& session) -> bool {
        auto scope = session.activate();
        return session.myscreen_->world().end != 0;
    };

    // The headline contract: a win ENDS both peers' display sessions so each
    // returns to the menu — it must NOT auto-advance into level 2 in-session.
    bool both_ended = false;
    for (int round = 0; round < 80 && !both_ended; ++round)
    {
        pump(5);
        both_ended = peer_finished(*cleanup.host_session) &&
            peer_finished(join_session);
    }
    EXPECT_TRUE(both_ended)
        << "a networked win must end BOTH peers' display sessions (world.end=1) "
           "so each glad_main returns to the team-build menu";

    // The host also persists the authoritative combined roster for server-side
    // resume. Each peer's team-build menu reloads its own private save0.
    {
        SaveData netsession;
        ASSERT_EQ(SaveDataIoError::None,
                  netsession.load_with_error("netsession"))
            << "host should have persisted the networked session roster";
        EXPECT_EQ(2, static_cast<int>(netsession.scen_num))
            << "winning level 1 should advance the campaign cursor to level 2";
        EXPECT_TRUE(netsession.is_level_completed(1));
    }

    // Neither peer auto-advanced in-session: the host display ended on level 1
    // (its last InitialSetup was for level 1), it was never re-set-up for level
    // 2 mid-session — the next level is the menu's job over the live connection.
    {
        auto host_scope = cleanup.host_session->activate();
        const og::sim::GameClient* const dc =
            cleanup.host_session->myscreen_->render_interpolation_client();
        ASSERT_NE(nullptr, dc);
        ASSERT_TRUE(dc->initial_setup().has_value());
        EXPECT_EQ(1, dc->initial_setup()->level_id)
            << "host display must NOT have been re-set-up for level 2 in-session";
    }
}

// Networked EXIT-portal parity with the win test above: taking an exit (the
// distinct forward_level_end_to_clients path, NOT the win broadcast path) must
// also end BOTH peers' display sessions and return them to the team-build menu —
// never auto-advance into the next level in-session.
TEST(PickerNetworkClient, host_and_join_real_exit_returns_both_peers_to_menu)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    prepare_allied_host_network_save(host_save);
    g_start_game_requested = false;
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    join_session->myscreen_->world().delete_objects();
                }
            }
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}))
        << "§4.3: the joiner must be ready before the host may start";
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }
        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff";

    const auto host_start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(host_start_config.has_value());
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_start_config);
        glad_init(false, &*host_start_config);
    }
    {
        auto join_scope = join_session.activate();
        auto join_start_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_start_config.has_value());
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(*join_session.myscreen_, &*join_start_config);
        glad_init(false, &*join_start_config);
    }

    screen* const host_gameplay_screen = cleanup.host_session->myscreen_;
    ASSERT_NE(nullptr, host_gameplay_screen);
    ASSERT_TRUE(wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_finish_tick(*cleanup.host_session);
            const og::sim::GameClient* const dc =
                cleanup.host_session->myscreen_->render_interpolation_client();
            host_ready = dc != nullptr && dc->baseline().has_value();
        }
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const dc =
                join_session.myscreen_->render_interpolation_client();
            join_ready = dc != nullptr && dc->baseline().has_value();
        }
        return host_ready && join_ready;
    })) << "both runtimes should receive their initial gameplay snapshots";

    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_EQ(0, static_cast<int>(host_gameplay_screen->world().end));
    }

    // --- Trigger an EXIT on the host and ACCEPT it. Only the triggering player
    // (host player 0) is prompted; the host display answers Yes. The server then
    // forwards a terminal EndGame to EVERY peer, so both displays end and return
    // to the team-build menu — it must NOT auto-advance into level 2.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_TRUE(og::runtime::local_transport_shadow_testing_open_exit_prompt(
            *cleanup.host_session, /*player_index=*/0u, /*destination_level=*/2))
            << "host player should have a server-side control to take the exit";
    }

    const InputState neutral{};
    std::uint32_t pump_tick = 1;
    const auto pump = [&](int iterations) {
        for (int i = 0; i < iterations; ++i, ++pump_tick)
        {
            {
                auto host_scope = cleanup.host_session->activate();
                og::runtime::local_transport_shadow_send_input(
                    *cleanup.host_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
            }
            {
                auto join_scope = join_session.activate();
                og::runtime::local_transport_shadow_send_input(
                    join_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(join_session);
            }
        }
    };

    const auto peer_finished = [&](og::runtime::GameSession& session) -> bool {
        auto scope = session.activate();
        return session.myscreen_->world().end != 0;
    };

    bool both_ended = false;
    for (int round = 0; round < 80 && !both_ended; ++round)
    {
        pump(5);
        both_ended = peer_finished(*cleanup.host_session) &&
            peer_finished(join_session);
    }
    picker_testing_yes_or_no_queue_clear();
    EXPECT_TRUE(both_ended)
        << "a networked exit must end BOTH peers' display sessions (world.end=1) "
           "so each glad_main returns to the team-build menu";

    {
        SaveData netsession;
        ASSERT_EQ(SaveDataIoError::None,
                  netsession.load_with_error("netsession"))
            << "host should have persisted the networked session roster";
        EXPECT_EQ(2, static_cast<int>(netsession.scen_num))
            << "taking the exit to level 2 should advance the campaign cursor";
    }

    {
        auto host_scope = cleanup.host_session->activate();
        const og::sim::GameClient* const dc =
            cleanup.host_session->myscreen_->render_interpolation_client();
        ASSERT_NE(nullptr, dc);
        ASSERT_TRUE(dc->initial_setup().has_value());
        EXPECT_EQ(1, dc->initial_setup()->level_id)
            << "host display must NOT have been re-set-up for level 2 in-session";
    }
}

// P2 + P3 headline: a networked win returns both peers to the team-build menu,
// and they start the NEXT level over the SAME live connection that survived
// gameplay (no reconnect) — full single-player parity. This is the end-to-end
// proof that the lobby connection persists across glad_main and the menu drives
// the next level.
TEST(PickerNetworkClient, host_and_join_win_level1_then_ready_up_and_load_level2)
{
    IxNetSystemScope net_system;

    // Per-machine private company slots (§2.9 flows 4-6). In production every
    // machine has its OWN disk company; the active-company slot is
    // process-global, so this in-process harness repoints it around each
    // peer's persist/reload sites (the set_active_company_slot calls below) —
    // otherwise both peers would alias one file and the split-wallet /
    // merge-write assertions would be meaningless.
    static constexpr const char kWp4HostSlot[] = "wp4camphost";
    static constexpr const char kWp4JoinSlot[] = "wp4campjoin";
    struct CompanySlotFixture
    {
        ~CompanySlotFixture()
        {
            (void)og::data::set_active_company_slot("save0");
            (void)remove_user_file(std::string("save/") + kWp4HostSlot +
                                   ".gtl");
            (void)remove_user_file(std::string("save/") + kWp4JoinSlot +
                                   ".gtl");
            // The LevelWin persists snapshot one backup per machine per win.
            for (const std::string& name : list_files("save/backups"))
            {
                if (name.starts_with(kWp4HostSlot) ||
                    name.starts_with(kWp4JoinSlot))
                {
                    (void)remove_user_file("save/backups/" + name);
                }
            }
        }
    } company_slot_fixture;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    prepare_allied_host_network_save(host_save);
    const std::string old_host_company = host_save.save_name;
    host_save.save_name = "IRON KETTLE BAND";
    // §4.6 wallet baselines: allied play puts every hero on TEAM 0, so both
    // machines contribute to (and split) the one team-0 pot; team 1 is a
    // sentinel wallet that must never move in either file.
    host_save.m_totalcash[0] = 5000;
    host_save.m_totalcash[1] = 111;
    g_start_game_requested = false;
#ifdef TESTING
    g_test_remove_exits = true; // clearing foes leaves level_done==2 (a win)
#endif
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");
    join_session.myscreen_->save_data.save_name = "JOIN RIVER BAND";
    // The joiner's private company banks on team 0 too (allied play
    // normalizes every hero onto team 0, and a reloaded solo save always
    // reads my_team=0); team 1 is a sentinel that must never move.
    join_session.myscreen_->save_data.m_totalcash[0] = 3000;
    join_session.myscreen_->save_data.m_totalcash[1] = 222;

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    join_session->myscreen_->world().delete_objects();
                }
            }
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    // ---- Lobby converge, then start LEVEL 1. ----
    const auto converge_lobby = [&](const char* phase) {
        ASSERT_TRUE(wait_until([&] {
            host_client->poll_and_apply();
            bool join_lobby_ready = false;
            {
                auto join_scope = join_session.activate();
                join_client->poll_and_apply();
                join_lobby_ready = status_lines_contain_exact(
                    join_client->status_lines(), "Lobby: 2 players");
            }
            return status_lines_contain_exact(host_client->status_lines(),
                                              "Lobby: 2 players") &&
                join_lobby_ready;
        })) << phase << ": host and join should converge on a two-player lobby";
    };

    const auto start_level = [&](const char* phase) {
        // §4.3: each round requires a fresh ready-up (unlock_for_new_round
        // clears ready between levels), so the joiner re-readies before GO.
        ASSERT_TRUE(ready_up_joiners(*host_client,
                                     {{&join_session, join_client.get()}}))
            << phase;
        ASSERT_TRUE(host_client->request_start_game()) << phase;
        ASSERT_TRUE(wait_until([&] {
            host_client->poll_and_apply();
            bool join_has_handoff = false;
            {
                auto join_scope = join_session.activate();
                join_client->poll_and_apply();
                join_has_handoff = join_client->has_game_start_config();
            }
            return host_client->has_game_start_config() && join_has_handoff;
        })) << phase << ": both peers should receive the gameplay handoff";

        const auto host_start_config = host_client->consume_game_start_config();
        ASSERT_TRUE(host_start_config.has_value()) << phase;
        {
            auto host_scope = cleanup.host_session->activate();
            og::data::set_active_company_slot(kWp4HostSlot);
            ActivePickerLobbyClientGuard active_client(host_client.get());
            ready_screen_for_game_start(
                *cleanup.host_session->myscreen_, &*host_start_config);
            glad_init(false, &*host_start_config);
        }
        {
            auto join_scope = join_session.activate();
            og::data::set_active_company_slot(kWp4JoinSlot);
            auto join_start_config = join_client->consume_game_start_config();
            ASSERT_TRUE(join_start_config.has_value()) << phase;
            ActivePickerLobbyClientGuard active_client(join_client.get());
            ready_screen_for_game_start(
                *join_session.myscreen_, &*join_start_config);
            glad_init(false, &*join_start_config);
        }

        ASSERT_TRUE(wait_until([&] {
            bool host_ready = false;
            {
                auto host_scope = cleanup.host_session->activate();
                og::data::set_active_company_slot(kWp4HostSlot);
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
                const og::sim::GameClient* const dc =
                    cleanup.host_session->myscreen_->render_interpolation_client();
                host_ready = dc != nullptr && dc->baseline().has_value();
            }
            bool join_ready = false;
            {
                auto join_scope = join_session.activate();
                og::data::set_active_company_slot(kWp4JoinSlot);
                og::runtime::local_transport_shadow_finish_tick(join_session);
                const og::sim::GameClient* const dc =
                    join_session.myscreen_->render_interpolation_client();
                join_ready = dc != nullptr && dc->baseline().has_value();
            }
            return host_ready && join_ready;
        })) << phase << ": both runtimes should receive initial snapshots";
    };

    converge_lobby("level 1");

    // Each machine's pre-level private company file (the win persists MERGE
    // into these — a missing file is skipped, never clobber-created).
    ASSERT_TRUE(host_save.save(kWp4HostSlot));
    {
        auto join_scope = join_session.activate();
        ASSERT_TRUE(join_session.myscreen_->save_data.save(kWp4JoinSlot));
    }
    start_level("level 1");

    // §4.6: measure each peer's team-0 wallet as the level STARTS (the
    // gameplay display save is the server-built netsession equivalent whose
    // wallets come from the host session, not each machine's private file) so
    // the per-peer fold delta can be derived after the win.
    const std::uint32_t host_cash0_at_start = host_save.m_totalcash[0];
    std::uint32_t join_cash0_at_start = 0;
    {
        auto join_scope = join_session.activate();
        join_cash0_at_start = join_session.myscreen_->save_data.m_totalcash[0];
    }

    // ---- Force a deterministic WIN on level 1, with a REAL pot: seed the
    // authoritative server world's team-0 level score (m_score is
    // snapshot-synced to every mirror, and each peer's fold re-derives
    // save.m_score from ITS world via sync_save_data_from_world), so the
    // §4.6 split has a nonzero delta to distribute. ----
    {
        screen* const server_screen =
            og::runtime::local_transport_shadow_testing_server_screen(
                *cleanup.host_session);
        ASSERT_NE(nullptr, server_screen);
        server_screen->world().m_score[0] = 300;
        const unsigned char my_team =
            static_cast<unsigned char>(server_screen->world().my_team);
        for (auto& uptr : server_screen->world().oblist)
        {
            walker* const w = uptr.get();
            if (w != nullptr && !w->dead() && w->is_friendly_to_team(my_team) == 0)
                w->set_dead(1);
        }
    }

    const InputState neutral{};
    std::uint32_t pump_tick = 1;
    const auto pump = [&](int iterations) {
        for (int i = 0; i < iterations; ++i, ++pump_tick)
        {
            {
                auto host_scope = cleanup.host_session->activate();
                og::data::set_active_company_slot(kWp4HostSlot);
                og::runtime::local_transport_shadow_send_input(
                    *cleanup.host_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
            }
            {
                auto join_scope = join_session.activate();
                og::data::set_active_company_slot(kWp4JoinSlot);
                og::runtime::local_transport_shadow_send_input(
                    join_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(join_session);
            }
        }
    };
    const auto peer_finished = [&](og::runtime::GameSession& session) -> bool {
        auto scope = session.activate();
        return session.myscreen_->world().end != 0;
    };
    bool both_ended = false;
    for (int round = 0; round < 80 && !both_ended; ++round)
    {
        pump(5);
        both_ended = peer_finished(*cleanup.host_session) &&
            peer_finished(join_session);
    }
    ASSERT_TRUE(both_ended) << "both peers must return to the menu after the win";

    // §4.6: derive each peer's team-0 fold delta from its own display save
    // (post-fold minus level-start). Allied play put all three heroes on
    // team 0 — the host contributed TWO deployed characters and the joiner
    // ONE, so the one team-0 pot splits 2:1 below. Both mirrors must agree
    // on the delta (m_score is snapshot-synced).
    const std::uint32_t host_delta0 =
        host_save.m_totalcash[0] - host_cash0_at_start;
    std::uint32_t join_delta0 = 0;
    {
        auto join_scope = join_session.activate();
        join_delta0 =
            join_session.myscreen_->save_data.m_totalcash[0] -
            join_cash0_at_start;
    }
    ASSERT_GT(host_delta0, 0u)
        << "the seeded score + first-win time bonus make a nonzero pot";
    ASSERT_EQ(host_delta0, join_delta0)
        << "both mirrors must fold the same team-0 delta";

    // ---- Simulate each peer's glad_main exit: the per-level runtime is torn
    // down, but the lobby client's transport (and the host's LobbyServer)
    // survive because they kept their own refs. ----
    {
        auto host_scope = cleanup.host_session->activate();
        og::runtime::clear_local_transport_shadow(*cleanup.host_session);
    }
    {
        auto join_scope = join_session.activate();
        og::runtime::clear_local_transport_shadow(join_session);
    }

    // ---- Production glad_main-exit mirror (go_menu): each machine reloads
    // its private company from ITS OWN disk file before re-entering the base
    // camp — the §4.6 bank and the merged own-roster fold live there. The
    // load succeeding at scen 2 is itself the proof that BOTH win-persist
    // paths ran (the host's server-shadow finalize and the joiner's
    // client-side once-latched persist). ----
    {
        auto host_scope = cleanup.host_session->activate();
        og::data::set_active_company_slot(kWp4HostSlot);
        ASSERT_TRUE(host_save.load(kWp4HostSlot))
            << "the host win persist must have written its private file";
    }
    {
        auto join_scope = join_session.activate();
        og::data::set_active_company_slot(kWp4JoinSlot);
        ASSERT_TRUE(join_session.myscreen_->save_data.load(kWp4JoinSlot))
            << "the joiner win persist must have written its private file";
    }

    // §2.9 "split wallets": the ONE team-0 pot splits by deployed-character
    // ratio — the joiner banks floor(delta/3) (its 1 of 3 deployed heroes),
    // the host the rest (2/3 share + the remainder as largest contributor).
    // Each machine's file gains ONLY its own share on its own baseline, the
    // shares conserve the pot exactly, and the sentinel team-1 wallets never
    // move — the replaced full-pot duplication would have banked the whole
    // delta on BOTH machines.
    const std::uint32_t join_share = host_delta0 / 3u;
    const std::uint32_t host_share = host_delta0 - join_share;
    EXPECT_EQ(5000u + host_share, host_save.m_totalcash[0])
        << "host banks baseline + its 2/3-plus-remainder share";
    EXPECT_EQ(111u, host_save.m_totalcash[1])
        << "the host's sentinel team-1 wallet must never move";
    {
        auto join_scope = join_session.activate();
        const SaveData& join_save = join_session.myscreen_->save_data;
        EXPECT_EQ(3000u + join_share, join_save.m_totalcash[0])
            << "joiner banks baseline + its 1/3 share, never the full pot";
        EXPECT_EQ(222u, join_save.m_totalcash[1])
            << "the joiner's sentinel team-1 wallet must never move";
    }

    // ---- Re-enter the lobby over the LIVE connection (what go_menu does via
    // picker_reinitialize_lobby_after_game). Both private files advanced
    // scen_num to 2 on the win persist, so the host re-broadcasts level 2. ----
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        host_client->resume_after_level();
    }
    {
        auto join_scope = join_session.activate();
        ActivePickerLobbyClientGuard active_client(join_client.get());
        join_client->resume_after_level();
    }

    // The lobby re-converges to two players over the SAME connection, and the
    // advanced campaign cursor (scen_num=2) propagates from the host to the
    // join (the menu polls every frame; here we poll until it lands).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_on_level_2 = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_on_level_2 =
                join_session.myscreen_->save_data.scen_num == 2 &&
                status_lines_contain_exact(join_client->status_lines(),
                                           "Lobby: 2 players");
        }
        return host_save.scen_num == 2 &&
            status_lines_contain_exact(host_client->status_lines(),
                                       "Lobby: 2 players") &&
            join_on_level_2;
    })) << "the advanced level-2 cursor must reach both peers over the live "
           "connection while staying a two-player lobby";


    // Both re-sent rosters must land on the server before the merged views
    // are pinned (resume_after_level re-sends each machine's roster; the
    // status-line wait above proves seats, not slot content).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_sees_three = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            ActivePickerLobbyClientGuard active_client(join_client.get());
            og::ui::BaseCampScreenState state;
            og::ui::base_camp_refresh_rows(state);
            join_sees_three = state.slots.size() == 3u;
        }
        bool host_sees_three = false;
        {
            auto host_scope = cleanup.host_session->activate();
            ActivePickerLobbyClientGuard active_client(host_client.get());
            og::ui::BaseCampScreenState state;
            og::ui::base_camp_refresh_rows(state);
            host_sees_three = state.slots.size() == 3u;
        }
        return join_sees_three && host_sees_three;
    })) << "both peers' merged base-camp displays must reach 3 rows";

    // ---- §2.5 between-levels base camp, HOST view: merged roster (own rows
    // first from the reloaded private file, then the joiner's replicated
    // slot), split-wallet GOLD label, and the §9.12 (G5) session-status
    // header: the host sees its role + the machine/player census (this
    // direct-websocket lobby advertises no relay room, so the census
    // stands alone after HOSTING). ----
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ASSERT_TRUE(save_contains_named_member(host_save, "Alpha"));
        ASSERT_TRUE(save_contains_named_member(host_save, "Bravo"));
        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        ASSERT_EQ(3u, state.slots.size())
            << "host view: two own rows + the joiner's replicated row";
        EXPECT_TRUE(state.slots[0].owned);
        EXPECT_TRUE(state.slots[1].owned);
        ASSERT_FALSE(state.slots[2].owned);
        EXPECT_EQ("JOIN RIVER BAND", state.slots[2].company)
            << "the COMPANY column reads the joiner's wire company";
        EXPECT_EQ("Joiner", state.slots[2].character.name);
        EXPECT_EQ(std::format("GOLD {}", host_save.m_totalcash[0]),
                  og::ui::format_base_camp_gold_label(host_save))
            << "the GOLD block reads this machine's own banked wallet";
        const og::ui::BaseCampLineB header = og::ui::compose_base_camp_line_b(
            host_client->connection_alert(),
            host_client->host_controls_visible(),
            host_client->session_room_code(),
            host_client->lobby_players());
        EXPECT_FALSE(header.alert) << "healthy link: status, not the alert";
        EXPECT_EQ("HOSTING 2 MACH / 2 PLYR", header.text)
            << "the host's line B carries role + census";
    }

    // ---- Same window, JOINER view: own row first, the host's two rows
    // foreign, GOLD reads the joiner's own banked (team-0) wallet — a
    // reloaded solo company always reads my_team=0. ----
    {
        auto join_scope = join_session.activate();
        ActivePickerLobbyClientGuard active_client(join_client.get());
        SaveData& join_save = join_session.myscreen_->save_data;
        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        ASSERT_EQ(3u, state.slots.size())
            << "join view: one own row + the host's two replicated rows";
        ASSERT_TRUE(state.slots[0].owned);
        ASSERT_TRUE(join_save.team_list[state.slots[0].save_slot] != nullptr);
        EXPECT_EQ("Joiner",
                  join_save.team_list[state.slots[0].save_slot]->name);
        ASSERT_FALSE(state.slots[1].owned);
        ASSERT_FALSE(state.slots[2].owned);
        EXPECT_EQ("IRON KETTLE BAND", state.slots[1].company);
        EXPECT_EQ("IRON KETTLE BAND", state.slots[2].company);
        // Production shape, pinned as-is: the lobby re-stamps my_team to the
        // SEAT team (1) on every poll, so the in-lobby GOLD label reads the
        // session wallet — while the allied win share banked into the solo
        // team-0 wallet (asserted on the file above). Recorded as a WP7
        // follow-up (display source vs banking team under allied MP).
        ASSERT_EQ(1, join_save.my_team);
        EXPECT_EQ("GOLD 222", og::ui::format_base_camp_gold_label(join_save));
        const og::ui::BaseCampLineB header = og::ui::compose_base_camp_line_b(
            join_client->connection_alert(),
            join_client->host_controls_visible(),
            join_client->session_room_code(),
            join_client->lobby_players());
        EXPECT_FALSE(header.alert) << "healthy link: status, not the alert";
        EXPECT_EQ("JOINED - HOST: IRON KETTLE BAND", header.text)
            << "the joiner's line B names the host machine's company "
               "(direct join: no room code)";
    }

    // ---- §2.9 flow 5 / §4.2 per-level reassembly, through the PRODUCTION
    // base-camp dispatch: benching Bravo flips the private flag the same
    // call, drops the DEP count, runs the §4.3/§3.8 mutation tail (roster
    // re-sync + ready drop + [SAVE-F1] merge autosave into the machine's own
    // file), and the next level's assembly must exclude the benched
    // character. ----
    {
        auto host_scope = cleanup.host_session->activate();
        og::data::set_active_company_slot(kWp4HostSlot);
        ActivePickerLobbyClientGuard active_client(host_client.get());
        const og::ui::MenuScreenSpec* const spec =
            og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
        ASSERT_NE(nullptr, spec);
        ASSERT_NE(nullptr, spec->on_spec_row);
        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        int bravo_row = -1;
        for (std::size_t i = 0; i < state.slots.size(); ++i)
        {
            const og::ui::BaseCampDisplaySlot& slot = state.slots[i];
            if (slot.owned && host_save.team_list[slot.save_slot] != nullptr &&
                host_save.team_list[slot.save_slot]->name == "Bravo")
            {
                bravo_row = static_cast<int>(i);
            }
        }
        ASSERT_GE(bravo_row, 0)
            << "Bravo must still be on the host's private roster";
        trace_clear();
        EXPECT_EQ(MENU_OK, spec->on_spec_row(bravo_row, &state));
        EXPECT_TRUE(trace_contains("basecamp", "deploy slot="));
        bool bravo_benched = false;
        for (const auto& member : host_save.team_list)
        {
            if (member != nullptr && member->name == "Bravo")
                bravo_benched = !member->deployed;
        }
        EXPECT_TRUE(bravo_benched)
            << "the toggle benches Bravo in the private save the same call";
        // DEP drops the same frame on the re-derived display...
        og::ui::base_camp_refresh_rows(state);
        const og::ui::BaseCampDeployCounts deploys =
            og::ui::count_base_camp_display_deploys(state.slots, host_save);
        EXPECT_EQ(2, deploys.deployed);
        EXPECT_EQ(3, deploys.total);
        // ...and the [SAVE-F1] merge autosave banked the flag on disk while
        // keeping the file's cursor at the lobby's level.
        SaveData reloaded;
        ASSERT_EQ(SaveDataIoError::None,
                  reloaded.load_with_error(kWp4HostSlot));
        bool bravo_benched_on_disk = false;
        for (const auto& member : reloaded.team_list)
        {
            if (member != nullptr && member->name == "Bravo")
                bravo_benched_on_disk = !member->deployed;
        }
        EXPECT_TRUE(bravo_benched_on_disk)
            << "the mutation autosave must merge-write the benched flag";
        EXPECT_EQ(2, reloaded.scen_num);
    }

    // The benched flag replicates into the JOINER's merged display (the
    // foreign row's X/- glyph data — §2.5).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool benched_on_join = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            ActivePickerLobbyClientGuard active_client(join_client.get());
            og::ui::BaseCampScreenState state;
            og::ui::base_camp_refresh_rows(state);
            for (const og::ui::BaseCampDisplaySlot& slot : state.slots)
            {
                if (!slot.owned && slot.character.name == "Bravo" &&
                    !slot.deployed)
                {
                    benched_on_join = true;
                }
            }
        }
        return benched_on_join;
    })) << "the benched flag must replicate into the joiner's merged display";

    // ---- Ready up and start LEVEL 2 over the persisted connection. ----
    start_level("level 2");

    const auto check_on_level_2 =
        [&](og::runtime::GameSession& session, const char* who) {
            auto scope = session.activate();
            screen* const s = session.myscreen_;
            const og::sim::GameClient* const dc =
                s->render_interpolation_client();
            ASSERT_NE(nullptr, dc) << who;
            ASSERT_TRUE(dc->initial_setup().has_value()) << who;
            EXPECT_EQ(2, dc->initial_setup()->level_id)
                << who << ": should have received level 2 setup over the live "
                          "connection";
            EXPECT_EQ(2, static_cast<int>(s->world().id))
                << who << ": display world should be level 2";
            EXPECT_EQ(0, static_cast<int>(s->world().end))
                << who << ": level 2 must be live, not stuck-finished";
            ASSERT_NE(nullptr, s->viewob[0]) << who;
            EXPECT_NE(nullptr, s->viewob[0]->control)
                << who << ": player must be controllable on level 2";
        };
    check_on_level_2(*cleanup.host_session, "host");
    check_on_level_2(join_session, "join");

    // §4.2: the level-2 authoritative roster honors the between-levels deploy
    // change — Bravo (benched in the lobby) never spawns on level 2 while
    // Alpha and the joiner's character do.
    {
        screen* const server_screen =
            og::runtime::local_transport_shadow_testing_server_screen(
                *cleanup.host_session);
        ASSERT_NE(nullptr, server_screen);
        auto host_scope = cleanup.host_session->activate();
        EXPECT_NE(nullptr,
                  find_named_team_member(server_screen->world(), "Alpha"))
            << "the still-deployed host character spawns on level 2";
        EXPECT_NE(nullptr,
                  find_named_team_member(server_screen->world(), "Joiner"))
            << "the joiner's character spawns on level 2";
        EXPECT_EQ(nullptr,
                  find_named_team_member(server_screen->world(), "Bravo"))
            << "a character benched between levels must not enter level 2";
    }

    // ---- [NET-R8] §4.6/§4.8: win LEVEL 2 as well and pin cross-machine
    // capture determinism. Level-2 rosters derive from update_guys-pruned
    // lists on BOTH sides, so an asymmetric prune would split the wallets
    // differently per machine — the conservation unit test alone cannot
    // catch that. Both displays latch their NetWinFoldCapture in
    // screen::endgame; the captures must agree and each must conserve its
    // per-team delta through the split table. ----
    {
        screen* const server_screen =
            og::runtime::local_transport_shadow_testing_server_screen(
                *cleanup.host_session);
        ASSERT_NE(nullptr, server_screen);
        server_screen->world().m_score[0] = 450;
        const unsigned char my_team =
            static_cast<unsigned char>(server_screen->world().my_team);
        for (auto& uptr : server_screen->world().oblist)
        {
            walker* const w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->is_friendly_to_team(my_team) == 0)
            {
                w->set_dead(1);
            }
        }
    }
    bool both_ended_level2 = false;
    for (int round = 0; round < 80 && !both_ended_level2; ++round)
    {
        pump(5);
        both_ended_level2 = peer_finished(*cleanup.host_session) &&
            peer_finished(join_session);
    }
    ASSERT_TRUE(both_ended_level2)
        << "both peers must finish the forced level-2 win";

    og::progression::NetWinFoldCapture host_capture;
    og::progression::NetWinFoldCapture join_capture;
    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_TRUE(cleanup.host_session->myscreen_->pending_net_win_capture_
                        .has_value())
            << "the host display must latch the level-2 win capture";
        host_capture =
            *cleanup.host_session->myscreen_->pending_net_win_capture_;
    }
    {
        auto join_scope = join_session.activate();
        ASSERT_TRUE(
            join_session.myscreen_->pending_net_win_capture_.has_value())
            << "the joiner display must latch the level-2 win capture";
        join_capture = *join_session.myscreen_->pending_net_win_capture_;
    }

    // Deployed-roster equality (owner + money team, order-normalized so a
    // benign mirror-rebuild ordering difference can never mask — or fake —
    // an asymmetric prune).
    const auto normalized_deployed =
        [](const og::progression::NetWinFoldCapture& capture) {
            std::vector<std::pair<int, int>> entries;
            entries.reserve(capture.deployed.size());
            for (const auto& contributor : capture.deployed)
            {
                entries.emplace_back(static_cast<int>(contributor.owner),
                                     static_cast<int>(contributor.team));
            }
            std::sort(entries.begin(), entries.end());
            return entries;
        };
    EXPECT_EQ(normalized_deployed(host_capture),
              normalized_deployed(join_capture))
        << "[NET-R8] host and joiner must capture the SAME deploy roster on "
           "the post-prune level";
    // Bravo was benched between levels: exactly Alpha (owner 0) and the
    // joiner's hero (owner 1) contributed, both on the allied money team 0.
    EXPECT_EQ((std::vector<std::pair<int, int>>{{0, 0}, {1, 0}}),
              normalized_deployed(host_capture));

    // The applied fold deltas agree (m_score is snapshot-synced and the
    // fold math is pure).
    EXPECT_EQ(host_capture.cash_delta, join_capture.cash_delta);
    EXPECT_EQ(host_capture.score_delta, join_capture.score_delta);
    EXPECT_GT(host_capture.cash_delta[0], 0u)
        << "the seeded level-2 score must produce a nonzero team-0 pot";

    // Conservation: for every team with >= 1 contributor the split table
    // sums exactly to the captured delta — on BOTH captures.
    const auto expect_conservation =
        [](const og::progression::NetWinFoldCapture& capture,
           const char* who) {
            const og::progression::NetWinShareTable table =
                og::progression::compute_networked_win_shares(capture);
            for (std::size_t team = 0; team < capture.cash_delta.size();
                 ++team)
            {
                bool has_contributor = false;
                for (const auto& contributor : capture.deployed)
                {
                    if (static_cast<std::size_t>(contributor.team) == team)
                        has_contributor = true;
                }
                if (!has_contributor)
                    continue;
                std::uint64_t cash_sum = 0;
                std::uint64_t score_sum = 0;
                for (std::size_t p = 0; p < og::sim::kMaxGlobalPlayers; ++p)
                {
                    cash_sum += table.cash[team][p];
                    score_sum += table.score[team][p];
                }
                EXPECT_EQ(static_cast<std::uint64_t>(
                              capture.cash_delta[team]),
                          cash_sum)
                    << who << ": cash shares must conserve team " << team;
                EXPECT_EQ(static_cast<std::uint64_t>(
                              capture.score_delta[team]),
                          score_sum)
                    << who << ": score shares must conserve team " << team;
            }
        };
    expect_conservation(host_capture, "host");
    expect_conservation(join_capture, "join");

    host_save.save_name = old_host_company;
}

// §4.3 ready gate, end to end over real websockets (cheap: no level runs).
// (1) The host's GO is DENIED while the joiner is unready, and the denial reason
// is echoed to the host without engaging the lobby lock. (2) The joiner readies.
// (3) The deadlock regression: the joiner re-sends its content-identical join
// (what go_menu does every frame) and its ready SURVIVES. (4) The host's GO now
// succeeds.
TEST(PickerNetworkClient, host_go_denied_until_joiner_ready_and_resend_preserves_ready)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_allied_host_network_save(host_save);
    g_start_game_requested = false;
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;
        ~CleanupGuard()
        {
            if (join_session != nullptr && join_client != nullptr)
            {
                auto join_scope = join_session->activate();
                join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup{&join_session, host_client.get(), join_client.get()};

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_two = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_two = status_lines_contain_exact(join_client->status_lines(),
                                                  "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_two;
    })) << "host and join should converge on a two-player lobby";

    // (1) GO denied — the joiner has not readied. The lobby stays LIVE.
    ASSERT_TRUE(host_save.save("save0"));
    EXPECT_FALSE(host_client->request_start_game())
        << "GO must be denied while a non-host machine is unready";
    EXPECT_FALSE(host_client->start_request_pending())
        << "the pending flag is cleared on denial (no go_menu spin)";
    EXPECT_EQ(og::sim::StartDenialReason::MachinesNotReady,
              host_client->last_start_denial())
        << "the denial reason is echoed to the host";
    EXPECT_FALSE(host_client->has_game_start_config());

    // (2) The joiner readies.
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}));

    // (3) Deadlock regression: the joiner re-sends its content-identical join
    // (go_menu's every-frame re-sync) and stays ready.
    {
        auto join_scope = join_session.activate();
        join_client->sync_roster_from_save();
    }
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
        }
        for (const og::sim::LobbyPlayer& player : host_client->lobby_players())
            if (!player.is_host && !player.ready)
                return false;
        return host_client->lobby_players().size() == 2u;
    })) << "a content-identical join re-send must not clear the joiner's ready";

    // (4) GO now succeeds over the still-ready lobby.
    EXPECT_TRUE(host_client->request_start_game())
        << "GO succeeds once every non-host machine is ready";
    EXPECT_EQ(og::sim::StartDenialReason::None, host_client->last_start_denial());

    og::runtime::clear_local_transport_shadow(*active_game_session());
}

// §4.2 deploy filter + §4.4 cross-control propagation, end to end: a host
// benches one of its two characters and flips cross_control ON. The joiner
// learns cross_control over the settings echo (host → all peers; the 7-step
// chain then carries it into every session's game-start save). The started
// level contains ONLY deployed characters — the benched one never spawns,
// never rides the netsession seed, and stays in the host's private save0
// with its flag intact.
TEST(PickerNetworkClient, benched_slot_stays_out_of_level_and_cross_control_propagates)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    prepare_allied_host_network_save(host_save);
    // Bench Bravo (slot 1). Alpha stays deployed.
    ASSERT_NE(nullptr, host_save.team_list[1]);
    host_save.team_list[1]->deployed = false;
    // Host-only §4.4 setting: ON. (Not restored by PickerSaveStateGuard —
    // scoped restore below.)
    struct CrossControlRestore
    {
        SaveData& save;
        short prev;
        ~CrossControlRestore() { save.cross_control = prev; }
    } cross_control_restore{host_save, host_save.cross_control};
    host_save.cross_control = 1;
    g_start_game_requested = false;
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    join_session->myscreen_->world().delete_objects();
                }
            }
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    // Converge, and require the joiner to have LEARNED cross_control=1 from
    // the host's settings echo (§4.4 propagation; sanitize keeps {0,1}).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_converged = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_converged = status_lines_contain_exact(
                                 join_client->status_lines(),
                                 "Lobby: 2 players") &&
                join_session.myscreen_->save_data.cross_control == 1;
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_converged;
    })) << "the joiner must converge AND adopt the host's cross_control=1";

    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}));
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }
        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff";

    const auto host_start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(host_start_config.has_value());
    // The equivalent itself is the assembly: Alpha + Joiner only.
    ASSERT_EQ(2u, host_start_config->save_data.team_list.size())
        << "the benched slot must be filtered out of the game-start roster";
    EXPECT_EQ(1, host_start_config->save_data.cross_control);
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_start_config);
        glad_init(false, &*host_start_config);
    }
    {
        auto join_scope = join_session.activate();
        auto join_start_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_start_config.has_value());
        ASSERT_EQ(2u, join_start_config->save_data.team_list.size())
            << "the joiner mirror must filter the benched slot identically";
        EXPECT_EQ(1, join_start_config->save_data.cross_control);
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(
            *join_session.myscreen_, &*join_start_config);
        glad_init(false, &*join_start_config);
    }

    ASSERT_TRUE(wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_finish_tick(
                *cleanup.host_session);
            const og::sim::GameClient* const dc =
                cleanup.host_session->myscreen_->render_interpolation_client();
            host_ready = dc != nullptr && dc->baseline().has_value();
        }
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const dc =
                join_session.myscreen_->render_interpolation_client();
            join_ready = dc != nullptr && dc->baseline().has_value();
        }
        return host_ready && join_ready;
    })) << "both runtimes should receive initial snapshots";

    // The authoritative server world: deployed characters only (allied mode
    // folds every hero onto gameplay team 0).
    {
        screen* const server_screen =
            og::runtime::local_transport_shadow_testing_server_screen(
                *cleanup.host_session);
        ASSERT_NE(nullptr, server_screen);
        auto host_scope = cleanup.host_session->activate();
        EXPECT_NE(nullptr,
                  find_named_team_member(server_screen->world(), "Alpha"))
            << "the deployed host character spawns";
        EXPECT_NE(nullptr,
                  find_named_team_member(server_screen->world(), "Joiner"))
            << "the joiner's character spawns";
        EXPECT_EQ(nullptr,
                  find_named_team_member(server_screen->world(), "Bravo"))
            << "the benched character must never enter the level";
        EXPECT_EQ(1, static_cast<int>(server_screen->save_data.cross_control))
            << "cross_control must survive the headless save-data copy chain";
    }

    // Save isolation: the netsession seed carries only deployed characters;
    // the private save0 on disk keeps the benched one, flag intact.
    {
        SaveData netsession;
        ASSERT_EQ(SaveDataIoError::None, netsession.load_with_error("netsession"));
        EXPECT_TRUE(save_contains_named_member(netsession, "Alpha"));
        EXPECT_FALSE(save_contains_named_member(netsession, "Bravo"))
            << "the benched character must not ride the netsession seed";

        SaveData private_save;
        ASSERT_EQ(SaveDataIoError::None, private_save.load_with_error("save0"));
        ASSERT_TRUE(save_contains_named_member(private_save, "Bravo"))
            << "the benched character stays in the private save";
        for (const auto& member : private_save.team_list)
        {
            if (member != nullptr && member->name == "Bravo")
                EXPECT_FALSE(member->deployed);
        }
    }
}

// §4.2 [NET-F2] client half of the convergence loop, end to end: a joiner
// whose deployed roster overflows the 24-cap is force-benched by the server;
// the echoed flags are ADOPTED into the joiner's private roster (autosaved
// via the §3.8 merge path — traced), so its next roster re-send is
// content-identical and its ready SURVIVES; the host's GO then succeeds with
// exactly 24 deployed slots in the assembly.
TEST(PickerNetworkClient, overflow_join_reconciles_echoed_bench_flags_and_preserves_ready)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_allied_host_network_save(host_save);
    // Grow the host to a full 24-slot deployed roster: capacity for the
    // joiner is zero.
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
    {
        if (host_save.team_list[slot] == nullptr)
        {
            auto member = std::make_unique<guy>(FAMILY_SOLDIER);
            member->name = std::format("Host{}", slot);
            member->teamnum = 0;
            host_save.team_list[slot] = std::move(member);
        }
    }
    host_save.team_size = static_cast<unsigned char>(MAX_TEAM_SIZE);
    g_start_game_requested = false;
    set_game_speed(0.0f);
#ifdef TESTING
    trace_clear();
#endif

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    SaveData& join_save = join_session.myscreen_->save_data;
    prepare_single_member_network_save(join_save, 1, "JoinA");
    {
        auto member = std::make_unique<guy>(FAMILY_ARCHER);
        member->name = "JoinB";
        member->teamnum = 1;
        join_save.team_list[1] = std::move(member);
        join_save.team_size = 2;
    }

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;
        ~CleanupGuard()
        {
            if (join_session != nullptr && join_client != nullptr)
            {
                auto join_scope = join_session->activate();
                join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup{&join_session, host_client.get(), join_client.get()};

    // Converge AND reconcile: the joiner's two members must both flip to
    // benched in its PRIVATE in-memory roster (adopted from the echo).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool reconciled = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            reconciled =
                join_save.team_list[0] != nullptr &&
                join_save.team_list[1] != nullptr &&
                !join_save.team_list[0]->deployed &&
                !join_save.team_list[1]->deployed;
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            reconciled;
    })) << "the joiner must adopt the server's force-benched flags";
#ifdef TESTING
    EXPECT_TRUE(trace_contains("lobby", "deploy_reconcile"))
        << "the reconciliation must be traced";
    EXPECT_TRUE(trace_contains("lobby", "deploy_limit_benched n=2"))
        << "the 24-cap bench count must be traced (WP4 popup hook)";
#endif

    // Ready up, then re-send the (now reconciled) roster — the §4.2
    // convergence promise: the re-send is content-identical, ready survives.
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}));
    {
        auto join_scope = join_session.activate();
        join_client->sync_roster_from_save();
    }
    // Bounded pump (NOT a wait-until-ready, which would pass before the
    // re-send even lands): drive both sides long enough for the join
    // round-trip, then require ready to have SURVIVED. The strict
    // idempotence guarantee is unit-pinned in
    // LobbyServer.overflow_force_bench_is_idempotent_and_preserves_ready.
    for (int round = 0; round < 40; ++round)
    {
        host_client->poll_and_apply();
        auto join_scope = join_session.activate();
        join_client->poll_and_apply();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    {
        auto join_scope = join_session.activate();
        EXPECT_TRUE(join_client->local_ready())
            << "a reconciled roster re-send must not clear ready ([NET-F2])";
    }

    // GO succeeds; the assembly holds exactly the host's 24 deployed slots.
    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(host_client->request_start_game());
    EXPECT_EQ(og::sim::StartDenialReason::None, host_client->last_start_denial());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        return host_client->has_game_start_config();
    }));
    const auto start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ(24u, start_config->save_data.team_list.size())
        << "the assembly must hold exactly the 24 deployed slots";
    for (const auto& slot : start_config->save_data.team_list)
    {
        EXPECT_NE("JoinA", slot.character.name);
        EXPECT_NE("JoinB", slot.character.name);
    }

    og::runtime::clear_local_transport_shadow(*active_game_session());
}

// P4 edge case: a peer that DISCONNECTS between levels (over the persisted
// connection) must be reconciled by the host's surviving LobbyServer — the host
// drops back to a one-player lobby and can still start the next level solo. This
// exercises the persisted-connection disconnect path (the LobbyServer is kept
// alive across gameplay, so its peer tracking must handle a drop on resume).
TEST(PickerNetworkClient, host_and_join_join_disconnect_between_levels_reconciles_host_lobby)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    prepare_allied_host_network_save(host_save);
    g_start_game_requested = false;
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                    join_session->myscreen_->world().delete_objects();
            }
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                    host_session->myscreen_->world().delete_objects();
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    // Converge a two-player lobby.
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") && join_ready;
    })) << "host and join should converge on a two-player lobby";

    // Start level 1 (establishes the per-level runtime on the live connection).
    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}))
        << "§4.3: the joiner must be ready before the host may start";
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }
        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff";

    {
        const auto host_config = host_client->consume_game_start_config();
        ASSERT_TRUE(host_config.has_value());
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_config);
        glad_init(false, &*host_config);
    }
    {
        auto join_scope = join_session.activate();
        const auto join_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_config.has_value());
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(*join_session.myscreen_, &*join_config);
        glad_init(false, &*join_config);
    }

    // Simulate glad_main exit on both peers (per-level runtime torn down, lobby
    // connection survives) and re-enter the team-build menu over it.
    {
        auto host_scope = cleanup.host_session->activate();
        og::runtime::clear_local_transport_shadow(*cleanup.host_session);
        ActivePickerLobbyClientGuard active_client(host_client.get());
        host_client->resume_after_level();
    }
    {
        auto join_scope = join_session.activate();
        og::runtime::clear_local_transport_shadow(join_session);
        ActivePickerLobbyClientGuard active_client(join_client.get());
        join_client->resume_after_level();
    }

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players");
    })) << "the lobby should re-converge to two players over the live connection";

    // The join LEAVES between levels (closes its connection).
    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    cleanup.join_client = nullptr; // already shut down

    // The host's surviving LobbyServer reconciles the dropped peer.
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 1 player");
    })) << "the host lobby must reconcile to one player after the join leaves";

    // The host can still start the next level solo over the surviving connection.
    EXPECT_TRUE(host_client->request_start_game())
        << "the host should still be able to start a level after a peer leaves";
}

// MULTI-LOCAL-PLAYER HEADLINE: one host machine with TWO local seats plus a
// joiner with FOUR seats and a joiner with ONE seat — an allied 7-player lobby
// across 3 physical peers. End-to-end over real websockets: per-seat global
// player_index allocation, all 7 walkers controlled, strict per-seat input
// routing (slot k drives ONLY its seat's player), per-machine view counts,
// per-seat save0 persistence on a win, and seat re-declaration when the lobby
// resumes over the live connection.
TEST(PickerNetworkClient,
     host_two_seats_plus_multi_seat_joiners_run_seven_player_level)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    prepare_seven_player_host_save(host_save);
    g_start_game_requested = false;
#ifdef TESTING
    g_test_remove_exits = true; // clearing foes leaves level_done==2 (a win)
#endif
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;

    // Joiner A: FOUR local seats on one connection.
    og::runtime::GameSession join_a_session(join_cfg);
    prepare_seven_player_join_a_save(join_a_session.myscreen_->save_data);
    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_a_client;
    {
        auto join_scope = join_a_session.activate();
        join_a_client = og::ui::create_join_picker_lobby_client(join_options);
        join_a_client->initialize_from_save();
    }

    // Deterministic player_index allocation: wait until joiner A's four seats
    // landed (6 players) BEFORE joiner B connects.
    ASSERT_TRUE(wait_until(
        [&] {
            host_client->poll_and_apply();
            {
                auto join_scope = join_a_session.activate();
                join_a_client->poll_and_apply();
            }
            return status_lines_contain_exact(host_client->status_lines(),
                                              "Lobby: 6 players");
        },
        8s))
        << "host + 4-seat joiner should converge on a six-player lobby";

    // Joiner B: ONE seat.
    og::runtime::GameSession join_b_session(join_cfg);
    prepare_seven_player_join_b_save(join_b_session.myscreen_->save_data);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_b_client;
    {
        auto join_scope = join_b_session.activate();
        join_b_client = og::ui::create_join_picker_lobby_client(join_options);
        join_b_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_a_session = nullptr;
        og::runtime::GameSession* join_b_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_a_client = nullptr;
        og::ui::IPickerLobbyClient* join_b_client = nullptr;

        static void teardown_join(og::runtime::GameSession* session,
                                  og::ui::IPickerLobbyClient* client)
        {
            if (session == nullptr)
                return;
            auto scope = session->activate();
            og::runtime::clear_local_transport_shadow(*session);
            if (client != nullptr)
                client->shutdown();
            if (session->myscreen_ != nullptr)
            {
                for (auto& view : session->myscreen_->viewob)
                    if (view != nullptr)
                        view->control = nullptr;
                session->myscreen_->world().delete_objects();
            }
        }

        ~CleanupGuard()
        {
            teardown_join(join_b_session, join_b_client);
            teardown_join(join_a_session, join_a_client);
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_a_session = &join_a_session;
    cleanup.join_b_session = &join_b_session;
    cleanup.host_client = host_client.get();
    cleanup.join_a_client = join_a_client.get();
    cleanup.join_b_client = join_b_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    const auto converge_lobby = [&](const char* phase) {
        ASSERT_TRUE(wait_until(
            [&] {
                host_client->poll_and_apply();
                bool a_ready = false;
                bool b_ready = false;
                {
                    auto join_scope = join_a_session.activate();
                    join_a_client->poll_and_apply();
                    a_ready = status_lines_contain_exact(
                        join_a_client->status_lines(), "Lobby: 7 players");
                }
                {
                    auto join_scope = join_b_session.activate();
                    join_b_client->poll_and_apply();
                    b_ready = status_lines_contain_exact(
                        join_b_client->status_lines(), "Lobby: 7 players");
                }
                return status_lines_contain_exact(host_client->status_lines(),
                                                  "Lobby: 7 players") &&
                    a_ready && b_ready;
            },
            8s))
            << phase
            << ": all three machines should converge on a seven-player lobby";
    };
    converge_lobby("level 1");

    // Lobby replication must never materialize another machine's roster into
    // this peer's editable SaveData. The authoritative combined roster lives
    // in LobbyState/start config; each in-memory picker save stays private so
    // GO, hire, train, and named saves cannot leak remote gladiators.
    EXPECT_EQ(2, static_cast<int>(host_save.team_size));
    EXPECT_TRUE(save_contains_named_member(host_save, "HostAce"));
    EXPECT_TRUE(save_contains_named_member(host_save, "HostBee"));
    EXPECT_FALSE(save_contains_named_member(host_save, "JoinAOne"));
    {
        auto join_scope = join_a_session.activate();
        const SaveData& join_save = join_a_session.myscreen_->save_data;
        EXPECT_EQ(4, static_cast<int>(join_save.team_size));
        EXPECT_TRUE(save_contains_named_member(join_save, "JoinAOne"));
        EXPECT_TRUE(save_contains_named_member(join_save, "JoinAFour"));
        EXPECT_FALSE(save_contains_named_member(join_save, "HostAce"));
        EXPECT_FALSE(save_contains_named_member(join_save, "JoinBSolo"));
    }
    {
        auto join_scope = join_b_session.activate();
        const SaveData& join_save = join_b_session.myscreen_->save_data;
        EXPECT_EQ(1, static_cast<int>(join_save.team_size));
        EXPECT_TRUE(save_contains_named_member(join_save, "JoinBSolo"));
        EXPECT_FALSE(save_contains_named_member(join_save, "HostAce"));
    }

    // Per-seat player_index allocation is (connection order, seat order):
    // host seats 0-1, joiner A seats 2-5, joiner B seat 6.
    {
        const std::vector<og::sim::LobbyPlayer> players =
            host_client->lobby_players();
        ASSERT_EQ(7u, players.size());
        std::set<std::uint8_t> indices;
        std::size_t derived_seat_names = 0;
        for (const og::sim::LobbyPlayer& player : players)
        {
            indices.insert(player.player_index);
            if (player.name.find('#') != std::string::npos)
                ++derived_seat_names;
        }
        EXPECT_EQ((std::set<std::uint8_t>{0, 1, 2, 3, 4, 5, 6}), indices);
        // Seats 1..N-1 of each machine carry derived "base#k" names:
        // host contributes 1, joiner A 3, joiner B 0.
        EXPECT_EQ(4u, derived_seat_names);
    }

    // Pre-session save0: stale names at every slot the machines will merge.
    write_stale_seven_member_save0();

    // ---- Start level 1. ----
    // §4.3: both joiner machines must be ready before the host may start.
    ASSERT_TRUE(ready_up_joiners(
        *host_client,
        {{&join_a_session, join_a_client.get()},
         {&join_b_session, join_b_client.get()}}));
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until(
        [&] {
            host_client->poll_and_apply();
            bool a_handoff = false;
            bool b_handoff = false;
            {
                auto join_scope = join_a_session.activate();
                join_a_client->poll_and_apply();
                a_handoff = join_a_client->has_game_start_config();
            }
            {
                auto join_scope = join_b_session.activate();
                join_b_client->poll_and_apply();
                b_handoff = join_b_client->has_game_start_config();
            }
            return host_client->has_game_start_config() && a_handoff &&
                b_handoff;
        },
        8s))
        << "all three peers should receive the gameplay handoff";

    // Each machine's config carries its OWN seats from the FINAL state.
    const auto host_config = host_client->consume_game_start_config();
    ASSERT_TRUE(host_config.has_value());
    EXPECT_EQ((std::vector<std::uint8_t>{0, 1}),
              host_config->local_player_indices);
    EXPECT_EQ((std::vector<short>{0, 0}), host_config->local_seat_teams)
        << "allied mode folds every seat to gameplay team 0";
    EXPECT_EQ(2, static_cast<int>(host_config->save_data.numplayers));
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_config);
        glad_init(false, &*host_config);
    }
    {
        auto join_scope = join_a_session.activate();
        const auto join_a_config = join_a_client->consume_game_start_config();
        ASSERT_TRUE(join_a_config.has_value());
        EXPECT_EQ((std::vector<std::uint8_t>{2, 3, 4, 5}),
                  join_a_config->local_player_indices);
        EXPECT_EQ((std::vector<short>{0, 0, 0, 0}),
                  join_a_config->local_seat_teams);
        EXPECT_EQ(4, static_cast<int>(join_a_config->save_data.numplayers));
        ActivePickerLobbyClientGuard active_client(join_a_client.get());
        ready_screen_for_game_start(*join_a_session.myscreen_, &*join_a_config);
        glad_init(false, &*join_a_config);
    }
    {
        auto join_scope = join_b_session.activate();
        const auto join_b_config = join_b_client->consume_game_start_config();
        ASSERT_TRUE(join_b_config.has_value());
        EXPECT_EQ((std::vector<std::uint8_t>{6}),
                  join_b_config->local_player_indices);
        EXPECT_EQ((std::vector<short>{0}), join_b_config->local_seat_teams);
        EXPECT_EQ(1, static_cast<int>(join_b_config->save_data.numplayers));
        ActivePickerLobbyClientGuard active_client(join_b_client.get());
        ready_screen_for_game_start(*join_b_session.myscreen_, &*join_b_config);
        glad_init(false, &*join_b_config);
    }

    // Per-machine view counts follow the machine's LOCAL seat count.
    EXPECT_EQ(2, static_cast<int>(cleanup.host_session->myscreen_->numviews));
    EXPECT_EQ(4, static_cast<int>(join_a_session.myscreen_->numviews));
    EXPECT_EQ(1, static_cast<int>(join_b_session.myscreen_->numviews));

    ASSERT_TRUE(wait_until(
        [&] {
            bool host_ready = false;
            bool a_ready = false;
            bool b_ready = false;
            {
                auto host_scope = cleanup.host_session->activate();
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
                const og::sim::GameClient* const dc =
                    cleanup.host_session->myscreen_
                        ->render_interpolation_client();
                host_ready = dc != nullptr && dc->baseline().has_value();
            }
            {
                auto join_scope = join_a_session.activate();
                og::runtime::local_transport_shadow_finish_tick(join_a_session);
                const og::sim::GameClient* const dc =
                    join_a_session.myscreen_->render_interpolation_client();
                a_ready = dc != nullptr && dc->baseline().has_value();
            }
            {
                auto join_scope = join_b_session.activate();
                og::runtime::local_transport_shadow_finish_tick(join_b_session);
                const og::sim::GameClient* const dc =
                    join_b_session.myscreen_->render_interpolation_client();
                b_ready = dc != nullptr && dc->baseline().has_value();
            }
            return host_ready && a_ready && b_ready;
        },
        10s))
        << "all three runtimes should receive their initial snapshots";

    screen* const server_screen =
        og::runtime::local_transport_shadow_testing_server_screen(
            *cleanup.host_session);
    ASSERT_NE(nullptr, server_screen);

    // All 7 global players hold a distinct living walker on the server, and
    // the host display's controlled-ids table mirrors the same mapping.
    const auto host_display_controlled_id =
        [&](std::size_t player) -> std::uint32_t {
        auto host_scope = cleanup.host_session->activate();
        const og::sim::GameClient* const dc =
            cleanup.host_session->myscreen_->render_interpolation_client();
        return dc != nullptr ? dc->controlled_entity_ids()[player] : 0u;
    };
    std::array<std::uint32_t, 7> controlled_before{};
    {
        auto host_scope = cleanup.host_session->activate();
        std::set<std::uint32_t> distinct;
        for (int player = 0; player < 7; ++player)
        {
            controlled_before[static_cast<std::size_t>(player)] =
                server_entity_id_for_user(*server_screen, player);
            ASSERT_NE(0u, controlled_before[static_cast<std::size_t>(player)])
                << "global player " << player
                << " must control a living server walker";
            distinct.insert(controlled_before[static_cast<std::size_t>(player)]);
        }
        EXPECT_EQ(7u, distinct.size());
    }
    for (int player = 0; player < 7; ++player)
    {
        EXPECT_EQ(controlled_before[static_cast<std::size_t>(player)],
                  host_display_controlled_id(static_cast<std::size_t>(player)))
            << "host display mapping for global player " << player;
    }

    // Every machine's display maps view i to ITS seat's global player index.
    const auto check_display_mapping =
        [&](og::runtime::GameSession& session,
            const std::vector<std::uint8_t>& seat_indices,
            const char* who) {
            auto scope = session.activate();
            const og::sim::GameClient* const dc =
                session.myscreen_->render_interpolation_client();
            ASSERT_NE(nullptr, dc) << who;
            for (std::size_t seat = 0; seat < seat_indices.size(); ++seat)
            {
                const std::size_t player =
                    static_cast<std::size_t>(seat_indices[seat]);
                EXPECT_EQ(controlled_before[player],
                          dc->controlled_entity_ids()[player])
                    << who << " seat " << seat;
                ASSERT_NE(nullptr, session.myscreen_->viewob[seat]) << who;
                EXPECT_EQ(0, static_cast<int>(
                                 session.myscreen_->viewob[seat]->my_team))
                    << who << " seat " << seat
                    << ": allied fold stamps every view team 0";
            }
        };
    check_display_mapping(*cleanup.host_session, {0, 1}, "host");
    check_display_mapping(join_a_session, {2, 3, 4, 5}, "joiner A");
    check_display_mapping(join_b_session, {6}, "joiner B");

    // ---- Per-seat input routing. ----
    const InputState neutral{};
    std::uint32_t pump_tick = 1;
    const auto drive_three_tick = [&](const InputState& host_input,
                                      const InputState& a_input,
                                      const InputState& b_input) -> bool {
        const std::uint32_t tick = pump_tick++;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_send_input(
                *cleanup.host_session, host_input, tick);
        }
        {
            auto join_scope = join_a_session.activate();
            og::runtime::local_transport_shadow_send_input(
                join_a_session, a_input, tick);
        }
        {
            auto join_scope = join_b_session.activate();
            og::runtime::local_transport_shadow_send_input(
                join_b_session, b_input, tick);
        }
        return wait_until(
            [&] {
                bool host_ready = false;
                bool a_ready = false;
                bool b_ready = false;
                {
                    auto host_scope = cleanup.host_session->activate();
                    og::runtime::local_transport_shadow_finish_tick(
                        *cleanup.host_session);
                    const og::sim::GameClient* const dc =
                        cleanup.host_session->myscreen_
                            ->render_interpolation_client();
                    host_ready = dc != nullptr &&
                        dc->last_seen_server_tick() >= tick;
                }
                {
                    auto join_scope = join_a_session.activate();
                    og::runtime::local_transport_shadow_finish_tick(
                        join_a_session);
                    const og::sim::GameClient* const dc =
                        join_a_session.myscreen_->render_interpolation_client();
                    a_ready = dc != nullptr &&
                        dc->last_seen_server_tick() >= tick;
                }
                {
                    auto join_scope = join_b_session.activate();
                    og::runtime::local_transport_shadow_finish_tick(
                        join_b_session);
                    const og::sim::GameClient* const dc =
                        join_b_session.myscreen_->render_interpolation_client();
                    b_ready = dc != nullptr &&
                        dc->last_seen_server_tick() >= tick;
                }
                return host_ready && a_ready && b_ready;
            },
            8s);
    };

    // Level 1 fields at least one unclaimed team-0 living even with all 7
    // seats bound, so a SwitchChar has somewhere to go.
    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_GE(count_free_team_livings(*server_screen, 0), 1)
            << "level 1 must keep a free team-0 walker after 7 claims";
    }

    // Joiner A's LOCAL slot 2 presses SwitchChar: ONLY global player 4 (its
    // seat 2) may change controls. Under the legacy single-active-slot
    // heuristic this input would drive the wrong seat; strict per-slot
    // routing is the contract here.
    ASSERT_TRUE(drive_three_tick(
        neutral, make_seat_switch_char_input(2), neutral));
    ASSERT_TRUE(drive_three_tick(neutral, neutral, neutral));
    std::uint32_t player4_after = controlled_before[4];
    for (int round = 0; round < 20; ++round)
    {
        player4_after = host_display_controlled_id(4);
        if (player4_after != 0u && player4_after != controlled_before[4])
            break;
        ASSERT_TRUE(drive_three_tick(neutral, neutral, neutral));
    }
    EXPECT_NE(controlled_before[4], player4_after)
        << "joiner A's slot-2 switch must drive its seat 2 (global player 4)";
    EXPECT_NE(0u, player4_after);
    for (const int player : {0, 1, 2, 3, 5, 6})
    {
        EXPECT_EQ(controlled_before[static_cast<std::size_t>(player)],
                  host_display_controlled_id(static_cast<std::size_t>(player)))
            << "global player " << player
            << " must be untouched by joiner A's slot-2 input";
    }

    // The host's LOCAL slot 1 presses SwitchChar: ONLY global player 1 (its
    // second seat) may change controls.
    std::array<std::uint32_t, 7> controlled_mid{};
    for (int player = 0; player < 7; ++player)
    {
        controlled_mid[static_cast<std::size_t>(player)] =
            host_display_controlled_id(static_cast<std::size_t>(player));
    }
    ASSERT_TRUE(drive_three_tick(
        make_seat_switch_char_input(1), neutral, neutral));
    ASSERT_TRUE(drive_three_tick(neutral, neutral, neutral));
    std::uint32_t player1_after = controlled_mid[1];
    for (int round = 0; round < 20; ++round)
    {
        player1_after = host_display_controlled_id(1);
        if (player1_after != 0u && player1_after != controlled_mid[1])
            break;
        ASSERT_TRUE(drive_three_tick(neutral, neutral, neutral));
    }
    EXPECT_NE(controlled_mid[1], player1_after)
        << "the host's slot-1 switch must drive its seat 1 (global player 1)";
    EXPECT_NE(0u, player1_after);
    for (const int player : {0, 2, 3, 4, 5, 6})
    {
        EXPECT_EQ(controlled_mid[static_cast<std::size_t>(player)],
                  host_display_controlled_id(static_cast<std::size_t>(player)))
            << "global player " << player
            << " must be untouched by the host's slot-1 input";
    }

    // ---- Force a deterministic WIN and let every display session end. ----
    {
        auto host_scope = cleanup.host_session->activate();
        // Pin a nonzero authoritative payout. The time-bonus component remains
        // enabled so the test also proves every peer uses the server-snapshotted
        // level tick rather than its own display cadence.
        server_screen->world().m_score[0] = 123;
        const unsigned char my_team =
            static_cast<unsigned char>(server_screen->world().my_team);
        std::size_t killed = 0;
        for (auto& uptr : server_screen->world().oblist)
        {
            walker* const entity = uptr.get();
            if (entity != nullptr && !entity->dead() &&
                entity->is_friendly_to_team(my_team) == 0)
            {
                entity->set_dead(1);
                ++killed;
            }
        }
        EXPECT_GT(killed, 0u) << "level 1 should contain foes to clear";
    }

    const auto peer_finished = [&](og::runtime::GameSession& session) -> bool {
        auto scope = session.activate();
        return session.myscreen_->world().end != 0;
    };
    const auto pump = [&](int iterations) {
        for (int i = 0; i < iterations; ++i, ++pump_tick)
        {
            {
                auto host_scope = cleanup.host_session->activate();
                og::runtime::local_transport_shadow_send_input(
                    *cleanup.host_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
            }
            {
                auto join_scope = join_a_session.activate();
                og::runtime::local_transport_shadow_send_input(
                    join_a_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(join_a_session);
            }
            {
                auto join_scope = join_b_session.activate();
                og::runtime::local_transport_shadow_send_input(
                    join_b_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(join_b_session);
            }
        }
    };
    bool all_ended = false;
    for (int round = 0; round < 120 && !all_ended; ++round)
    {
        pump(5);
        all_ended = peer_finished(*cleanup.host_session) &&
            peer_finished(join_a_session) && peer_finished(join_b_session);
    }
    ASSERT_TRUE(all_ended)
        << "a networked win must end ALL three machines' display sessions";

    std::uint32_t authoritative_cash = 0;

    // The networked roster advanced to level 2.
    {
        SaveData netsession;
        ASSERT_EQ(SaveDataIoError::None,
                  netsession.load_with_error("netsession"));
        EXPECT_EQ(2, static_cast<int>(netsession.scen_num));
        EXPECT_TRUE(netsession.is_level_completed(1));
        EXPECT_GT(netsession.m_totalcash[0], 5246u)
            << "the authoritative network save must retain the win payout";
        EXPECT_EQ(123u, netsession.m_totalscore[0]);
        authoritative_cash = netsession.m_totalcash[0];
    }

    const std::uint32_t reward_delta = authoritative_cash - 5000u;
    EXPECT_GT(reward_delta, 246u);
    // §4.6: the fold is UNCHANGED, so each machine's in-memory SESSION (combined
    // display) save still folds the whole delta onto its own wallet base. This
    // is the combined session total, NOT the per-machine private share — the
    // deploy-ratio split happens only at the save0 persist, asserted below.
    {
        auto join_scope = join_a_session.activate();
        EXPECT_EQ(6000u + reward_delta,
                  join_a_session.myscreen_->save_data.m_totalcash[0])
            << "the session fold is unchanged: the full delta lands on the "
               "combined display save";
        EXPECT_EQ(123u,
                  join_a_session.myscreen_->save_data.m_totalscore[0]);
    }
    {
        auto join_scope = join_b_session.activate();
        EXPECT_EQ(7000u + reward_delta,
                  join_b_session.myscreen_->save_data.m_totalcash[0])
            << "client B must not reset to the default 5000 wallet";
        EXPECT_EQ(123u,
                  join_b_session.myscreen_->save_data.m_totalscore[0]);
    }

    // Per-seat save0 persistence: every machine merged ALL of its seats'
    // characters into its save0 (this process's shared save0 file receives
    // all three machines' merges at distinct slots), overwriting every stale
    // pre-session name.
    {
        SaveData merged;
        ASSERT_EQ(SaveDataIoError::None, merged.load_with_error("save0"));
        EXPECT_EQ(7, static_cast<int>(merged.team_size));
        EXPECT_EQ(2, static_cast<int>(merged.scen_num))
            << "each machine's save0 cursor advances as if played alone";
        // §4.6 CONSERVATION (the anti-duplication pin): the pre-session save0
        // wallet was the 5000 new-game grubstake (SaveData::reset seeds it), and
        // the three machines each banked only their deploy-ratio SHARE of the
        // pot on top of it. Their shares sum to exactly the pot, so the shared
        // save0 holds baseline + the whole delta once — never the v7 behaviour
        // where each machine overlaid the full pot and the last writer won
        // (which would leave 6381 or 7381 here depending on write order).
        EXPECT_EQ(5000u + reward_delta, merged.m_totalcash[0])
            << "shared save0 accumulates baseline + exactly the pot (shares "
               "conserve; no full-pot duplication)";
        EXPECT_EQ(123u, merged.m_totalscore[0])
            << "score shares likewise conserve to the whole score delta";
        for (const char* const name :
             {"HostAce", "HostBee", "JoinAOne", "JoinATwo", "JoinAThree",
              "JoinAFour", "JoinBSolo"})
        {
            EXPECT_TRUE(save_contains_named_member(merged, name))
                << name << " must have been merged into save0 by its machine";
        }
        for (const auto& member : merged.team_list)
        {
            if (member != nullptr)
            {
                EXPECT_TRUE(member->name.rfind("Stale", 0) != 0)
                    << "stale pre-session member '" << member->name
                    << "' should have been overlaid by a per-seat merge";
            }
        }
    }

    // ---- Return to the lobby over the LIVE connections: every machine
    // re-declares its full seat list. ----
    {
        auto host_scope = cleanup.host_session->activate();
        og::runtime::clear_local_transport_shadow(*cleanup.host_session);
        ActivePickerLobbyClientGuard active_client(host_client.get());
        host_client->resume_after_level();
    }
    {
        auto join_scope = join_a_session.activate();
        og::runtime::clear_local_transport_shadow(join_a_session);
        ActivePickerLobbyClientGuard active_client(join_a_client.get());
        join_a_client->resume_after_level();
    }
    {
        auto join_scope = join_b_session.activate();
        og::runtime::clear_local_transport_shadow(join_b_session);
        ActivePickerLobbyClientGuard active_client(join_b_client.get());
        join_b_client->resume_after_level();
    }

    converge_lobby("post-level resume");
    {
        const std::vector<og::sim::LobbyPlayer> players =
            host_client->lobby_players();
        ASSERT_EQ(7u, players.size())
            << "every machine must re-declare its full seat list on resume";
        std::set<std::uint8_t> indices;
        for (const og::sim::LobbyPlayer& player : players)
            indices.insert(player.player_index);
        EXPECT_EQ((std::set<std::uint8_t>{0, 1, 2, 3, 4, 5, 6}), indices);
    }
    EXPECT_EQ(2, static_cast<int>(host_save.scen_num))
        << "the host's advanced campaign cursor survives the resume";
}

// PVP rules cap: in a NON-allied classic lobby teams stay exclusive, so a
// joiner requesting more seats than there are free teams is truncated to the
// available count — and the client adopts the authoritative seat count
// without desyncing.
TEST(PickerNetworkClient,
     pvp_lobby_truncates_joiner_seats_beyond_free_teams)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    // Host: ONE seat on team 0, allied_mode 0 (exclusive teams).
    reset_network_save_shell(host_save, /*numplayers=*/1, /*my_team=*/0,
                             /*allied_mode=*/0);
    put_named_member(host_save, 0, "PvpHost", 0);
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    {
        SaveData& join_save = join_session.myscreen_->save_data;
        // Joiner asks for FOUR seats, but only teams 1..3 are free: the seat
        // requesting team 0 (held exclusively by the host) must be dropped.
        reset_network_save_shell(join_save, /*numplayers=*/4, /*my_team=*/1,
                                 /*allied_mode=*/0);
        put_named_member(join_save, 0, "PvpJoinOne", 1);
        put_named_member(join_save, 1, "PvpJoinTwo", 2);
        put_named_member(join_save, 2, "PvpJoinThree", 3);
        put_named_member(join_save, 3, "PvpJoinFour", 0);
    }

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    // The lobby settles on FOUR players total (1 host + 3 truncated seats).
    ASSERT_TRUE(wait_until(
        [&] {
            host_client->poll_and_apply();
            bool join_ready = false;
            {
                auto join_scope = join_session.activate();
                join_client->poll_and_apply();
                join_ready = status_lines_contain_exact(
                    join_client->status_lines(), "Lobby: 4 players");
            }
            return status_lines_contain_exact(host_client->status_lines(),
                                              "Lobby: 4 players") &&
                join_ready;
        },
        8s))
        << "the PVP lobby must settle on 4 players (joiner truncated to 3)";

    // The joiner adopted the authoritative count: 3 seats, not 4.
    EXPECT_EQ(3, static_cast<int>(
                     join_session.myscreen_->save_data.numplayers))
        << "the joiner must adopt the truncated seat count";

    // Teams are exclusive and fully occupied: indices 0..3 across 4 distinct
    // teams, with exactly two derived seat names (joiner seats #1 and #2).
    const std::vector<og::sim::LobbyPlayer> players =
        host_client->lobby_players();
    ASSERT_EQ(4u, players.size());
    std::set<std::uint8_t> indices;
    std::set<std::int16_t> teams;
    std::size_t derived_seat_names = 0;
    for (const og::sim::LobbyPlayer& player : players)
    {
        indices.insert(player.player_index);
        teams.insert(player.team);
        if (player.name.find('#') != std::string::npos)
            ++derived_seat_names;
    }
    EXPECT_EQ((std::set<std::uint8_t>{0, 1, 2, 3}), indices);
    EXPECT_EQ(4u, teams.size()) << "PVP seats must land on distinct teams";
    EXPECT_EQ(2u, derived_seat_names)
        << "the joiner's kept seats are its first three (gap-free names)";
}

// A CLIENT choosing "Quit this mission" must withdraw ALL players (everyone
// returns to the team-build menu together), not just leave the client behind as
// AI. End-to-end proof: the join's local abort sends an abort request to the
// server, which ends the level for BOTH peers.
TEST(PickerNetworkClient, host_and_join_client_quit_mission_withdraws_both_peers)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    GameplayRunGuard gameplay_run_guard;
    prepare_allied_host_network_save(host_save);
    g_start_game_requested = false;
    set_game_speed(0.0f);

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    join_session->myscreen_->world().delete_objects();
                }
            }
            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                        if (view != nullptr)
                            view->control = nullptr;
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();
    ASSERT_NE(nullptr, cleanup.host_session);

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") && join_ready;
    })) << "host and join should converge on a two-player lobby";

    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}))
        << "§4.3: the joiner must be ready before the host may start";
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }
        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff";

    {
        const auto host_config = host_client->consume_game_start_config();
        ASSERT_TRUE(host_config.has_value());
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_, &*host_config);
        glad_init(false, &*host_config);
    }
    {
        auto join_scope = join_session.activate();
        const auto join_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_config.has_value());
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(*join_session.myscreen_, &*join_config);
        glad_init(false, &*join_config);
    }

    ASSERT_TRUE(wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_finish_tick(*cleanup.host_session);
            const og::sim::GameClient* const dc =
                cleanup.host_session->myscreen_->render_interpolation_client();
            host_ready = dc != nullptr && dc->baseline().has_value();
        }
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const dc =
                join_session.myscreen_->render_interpolation_client();
            join_ready = dc != nullptr && dc->baseline().has_value();
        }
        return host_ready && join_ready;
    })) << "both runtimes should receive their initial gameplay snapshots";

    // Both peers are live on level 1.
    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_EQ(0, static_cast<int>(
                         cleanup.host_session->myscreen_->world().end));
    }

    // --- The JOIN (a client, not the host) chooses "Quit this mission". This
    // asks the server to withdraw everyone — it must return FALSE (request sent,
    // wait for the server) rather than ending locally.
    {
        auto join_scope = join_session.activate();
        EXPECT_FALSE(og::runtime::local_transport_shadow_abort_level(join_session))
            << "a client abort should defer to the server, not end locally";
    }

    const InputState neutral{};
    std::uint32_t pump_tick = 1;
    const auto pump = [&](int iterations) {
        for (int i = 0; i < iterations; ++i, ++pump_tick)
        {
            {
                auto host_scope = cleanup.host_session->activate();
                og::runtime::local_transport_shadow_send_input(
                    *cleanup.host_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(
                    *cleanup.host_session);
            }
            {
                auto join_scope = join_session.activate();
                og::runtime::local_transport_shadow_send_input(
                    join_session, neutral, pump_tick);
                og::runtime::local_transport_shadow_finish_tick(join_session);
            }
        }
    };
    const auto peer_finished = [&](og::runtime::GameSession& session) -> bool {
        auto scope = session.activate();
        return session.myscreen_->world().end != 0;
    };

    bool both_ended = false;
    for (int round = 0; round < 80 && !both_ended; ++round)
    {
        pump(5);
        both_ended = peer_finished(*cleanup.host_session) &&
            peer_finished(join_session);
    }
    EXPECT_TRUE(both_ended)
        << "a client's 'Quit this mission' must withdraw BOTH peers (the host's "
           "level must end too) — not just leave the client behind as AI";
}

TEST(PickerNetworkClient,
     host_and_join_allied_level1_clear_stale_preclaim_and_keep_free_soldier_switchable)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_allied_host_network_save(host_save);
    ASSERT_EQ("org.openglad.gladiator", host_save.current_campaign);
    ASSERT_EQ(1, host_save.scen_num);
    ASSERT_EQ(1, static_cast<int>(host_save.numplayers));
    ASSERT_EQ(1, host_save.allied_mode);
    ASSERT_EQ(0, host_save.my_team);
    ASSERT_EQ(2, static_cast<int>(host_save.team_size));
    ASSERT_NE(nullptr, host_save.team_list[0]);
    ASSERT_NE(nullptr, host_save.team_list[1]);
    EXPECT_EQ("Alpha", host_save.team_list[0]->name);
    EXPECT_EQ(0, host_save.team_list[0]->teamnum);
    EXPECT_EQ("Bravo", host_save.team_list[1]->name);
    EXPECT_EQ(0, host_save.team_list[1]->teamnum);
    // Cross-control ON (§4.4): this test pins the DELIBERATE v7 shared-pool
    // bind/switch semantics — the joiner's seat rides the host's Bravo and
    // the host steals the joiner's Charlie — which survive only under the
    // legacy policy. The host-only setting propagates through the lobby, and
    // the install derives control_policy 0 from the game-start config (the
    // owner-locked default would give every machine its own characters
    // instead; that shape is pinned by the game-loop install e2e).
    host_save.cross_control = 1;
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_allied_join_network_save(join_session.myscreen_->save_data);
    {
        auto join_scope = join_session.activate();
        SaveData& join_save = join_session.myscreen_->save_data;
        ASSERT_EQ("org.openglad.gladiator", join_save.current_campaign);
        ASSERT_EQ(1, join_save.scen_num);
        ASSERT_EQ(1, static_cast<int>(join_save.numplayers));
        ASSERT_EQ(1, join_save.allied_mode);
        ASSERT_EQ(1, join_save.my_team);
        ASSERT_EQ(1, static_cast<int>(join_save.team_size));
        ASSERT_EQ(nullptr, join_save.team_list[0]);
        ASSERT_EQ(nullptr, join_save.team_list[1]);
        ASSERT_NE(nullptr, join_save.team_list[2]);
        EXPECT_EQ("Charlie", join_save.team_list[2]->name);
        EXPECT_EQ(1, join_save.team_list[2]->teamnum);
        EXPECT_FALSE(save_contains_named_member(join_save, "Alpha"));
        EXPECT_FALSE(save_contains_named_member(join_save, "Bravo"));
    }

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* host_session = nullptr;
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                og::runtime::clear_local_transport_shadow(*join_session);
                if (join_client != nullptr)
                    join_client->shutdown();
                if (join_session->myscreen_ != nullptr)
                {
                    for (auto& view : join_session->myscreen_->viewob)
                    {
                        if (view != nullptr)
                            view->control = nullptr;
                    }
                    join_session->myscreen_->world().delete_objects();
                }
            }

            if (host_session != nullptr)
            {
                og::runtime::clear_local_transport_shadow(*host_session);
                if (host_client != nullptr)
                    host_client->shutdown();
                if (host_session->myscreen_ != nullptr)
                {
                    for (auto& view : host_session->myscreen_->viewob)
                    {
                        if (view != nullptr)
                            view->control = nullptr;
                    }
                    host_session->myscreen_->world().delete_objects();
                }
            }
        }
    } cleanup;

    cleanup.host_session = active_game_session();
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_NE(nullptr, cleanup.host_session);

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();

        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(),
                "Lobby: 2 players");
        }

        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join clients should converge on the same allied lobby";

    ASSERT_TRUE(host_save.save("save0"));
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}))
        << "§4.3: the joiner must be ready before the host may start";
    ASSERT_TRUE(host_client->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();

        bool join_has_handoff = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_has_handoff = join_client->has_game_start_config();
        }

        return host_client->has_game_start_config() && join_has_handoff;
    })) << "both peers should receive the gameplay handoff before runtime install";

    const auto host_start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(host_start_config.has_value());
    {
        auto host_scope = cleanup.host_session->activate();
        ActivePickerLobbyClientGuard active_client(host_client.get());
        ready_screen_for_game_start(
            *cleanup.host_session->myscreen_,
            &*host_start_config);
        glad_init(false, &*host_start_config);
    }

    std::optional<og::ui::PickerLobbyGameStartConfig> join_start_config;
    {
        auto join_scope = join_session.activate();
        join_start_config = join_client->consume_game_start_config();
        ASSERT_TRUE(join_start_config.has_value());
        ActivePickerLobbyClientGuard active_client(join_client.get());
        ready_screen_for_game_start(*join_session.myscreen_, &*join_start_config);
        glad_init(false, &*join_start_config);
    }

    ASSERT_TRUE(wait_until([&] {
        bool host_ready = false;
        {
            auto host_scope = cleanup.host_session->activate();
            og::runtime::local_transport_shadow_finish_tick(*cleanup.host_session);
            const og::sim::GameClient* const display_client =
                cleanup.host_session->myscreen_->render_interpolation_client();
            host_ready = display_client != nullptr &&
                display_client->initial_setup().has_value() &&
                display_client->baseline().has_value();
        }

        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            og::runtime::local_transport_shadow_finish_tick(join_session);
            const og::sim::GameClient* const display_client =
                join_session.myscreen_->render_interpolation_client();
            join_ready = display_client != nullptr &&
                display_client->initial_setup().has_value() &&
                display_client->baseline().has_value();
        }

        return host_ready && join_ready;
    })) << "both runtimes should receive their first gameplay snapshots";

    const og::sim::GameClient* host_display_client = nullptr;
    AlliedClaimObservation host_observation;
    {
        auto host_scope = cleanup.host_session->activate();
        host_display_client =
            cleanup.host_session->myscreen_->render_interpolation_client();
        ASSERT_NE(nullptr, host_display_client);
        host_observation = observe_allied_claim_mapping(
            *cleanup.host_session->myscreen_,
            *host_display_client);
    }

    const og::sim::GameClient* join_display_client = nullptr;
    AlliedClaimObservation join_observation;
    {
        auto join_scope = join_session.activate();
        join_display_client = join_session.myscreen_->render_interpolation_client();
        ASSERT_NE(nullptr, join_display_client);
        join_observation = observe_allied_claim_mapping(
            *join_session.myscreen_,
            *join_display_client);
    }

    ASSERT_NE(nullptr, host_observation.alpha);
    ASSERT_NE(nullptr, host_observation.bravo);
    ASSERT_NE(nullptr, host_observation.charlie);
    ASSERT_NE(nullptr, join_observation.alpha);
    ASSERT_NE(nullptr, join_observation.bravo);
    ASSERT_NE(nullptr, join_observation.charlie);

    const std::uint32_t alpha_id = host_observation.alpha->entity_id();
    const std::uint32_t bravo_id = host_observation.bravo->entity_id();
    const std::uint32_t charlie_id = host_observation.charlie->entity_id();
    const std::set<std::uint32_t> expected_initial_ids = {
        alpha_id,
        bravo_id,
    };

    EXPECT_EQ(alpha_id, join_observation.alpha->entity_id());
    EXPECT_EQ(bravo_id, join_observation.bravo->entity_id());
    EXPECT_EQ(charlie_id, join_observation.charlie->entity_id());

    // Snapshot v9 consumption: the cross-control-ON install derives the
    // LEGACY policy but still stamps the machine map, and both scalars reach
    // each machine's display mirror over the real socket transports.
    EXPECT_EQ(0, static_cast<int>(
                     cleanup.host_session->myscreen_->world().control_policy));
    EXPECT_EQ(0,
              static_cast<int>(join_session.myscreen_->world().control_policy));
    EXPECT_EQ(og::sim::encode_player_machine(0, true),
              join_session.myscreen_->world().player_machine[0]);
    EXPECT_EQ(og::sim::encode_player_machine(1, true),
              join_session.myscreen_->world().player_machine[1]);
    EXPECT_EQ(og::sim::kPlayerMachineNone,
              join_session.myscreen_->world().player_machine[2]);

    ASSERT_EQ(host_observation.claimed_ids, host_observation.mapped_ids)
        << claim_mapping_details(*cleanup.host_session->myscreen_,
                                 host_observation);
    ASSERT_EQ(join_observation.claimed_ids, join_observation.mapped_ids)
        << claim_mapping_details(*join_session.myscreen_, join_observation);
    EXPECT_EQ(expected_initial_ids, host_observation.mapped_ids);
    EXPECT_EQ(expected_initial_ids, join_observation.mapped_ids);
    EXPECT_EQ(host_observation.mapped_ids, join_observation.mapped_ids);
    EXPECT_EQ(nullptr, host_observation.orphaned)
        << claim_mapping_details(*cleanup.host_session->myscreen_,
                                 host_observation);
    EXPECT_EQ(nullptr, join_observation.orphaned)
        << claim_mapping_details(*join_session.myscreen_, join_observation);
    EXPECT_EQ(alpha_id, host_display_client->controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id, host_display_client->controlled_entity_ids()[1]);
    EXPECT_EQ(alpha_id, join_display_client->controlled_entity_ids()[0]);
    EXPECT_EQ(bravo_id, join_display_client->controlled_entity_ids()[1]);
    EXPECT_EQ("Alpha",
              controlled_entity_name(
                  *host_display_client,
                  host_display_client->controlled_entity_ids()[0]));
    EXPECT_EQ("Bravo",
              controlled_entity_name(
                  *host_display_client,
                  host_display_client->controlled_entity_ids()[1]));
    EXPECT_EQ("Alpha",
              controlled_entity_name(
                  *join_display_client,
                  join_display_client->controlled_entity_ids()[0]));
    EXPECT_EQ("Bravo",
              controlled_entity_name(
                  *join_display_client,
                  join_display_client->controlled_entity_ids()[1]));

    {
        auto host_scope = cleanup.host_session->activate();
        ASSERT_NE(nullptr, cleanup.host_session->myscreen_->viewob[0]);
        ASSERT_NE(nullptr, cleanup.host_session->myscreen_->viewob[0]->control);
        EXPECT_EQ(alpha_id,
                  cleanup.host_session->myscreen_->viewob[0]->control->entity_id());
    }
    {
        auto join_scope = join_session.activate();
        ASSERT_NE(nullptr, join_session.myscreen_->viewob[0]);
        ASSERT_NE(nullptr, join_session.myscreen_->viewob[0]->control);
        EXPECT_EQ(bravo_id, join_session.myscreen_->viewob[0]->control->entity_id());
    }

    std::uint32_t next_tick =
        std::max(host_display_client->last_seen_server_tick(),
                 join_display_client->last_seen_server_tick()) +
        1u;
    ASSERT_TRUE(drive_bounded_switch_char_attempt(
        *cleanup.host_session,
        join_session,
        true,
        false,
        next_tick));

    {
        auto host_scope = cleanup.host_session->activate();
        host_display_client =
            cleanup.host_session->myscreen_->render_interpolation_client();
        ASSERT_NE(nullptr, host_display_client);
        const AlliedClaimObservation host_after_switch =
            observe_allied_claim_mapping(
                *cleanup.host_session->myscreen_,
                *host_display_client);
        ASSERT_NE(nullptr, host_after_switch.alpha);
        ASSERT_NE(nullptr, host_after_switch.bravo);
        ASSERT_NE(nullptr, host_after_switch.charlie);
        ASSERT_NE(nullptr, cleanup.host_session->myscreen_->viewob[0]);
        ASSERT_NE(nullptr, cleanup.host_session->myscreen_->viewob[0]->control);
        ASSERT_EQ(host_after_switch.claimed_ids, host_after_switch.mapped_ids)
            << claim_mapping_details(*cleanup.host_session->myscreen_,
                                     host_after_switch);
        EXPECT_EQ(nullptr, host_after_switch.orphaned)
            << claim_mapping_details(*cleanup.host_session->myscreen_,
                                     host_after_switch);
        const std::set<std::uint32_t> expected_after_switch = {
            bravo_id,
            charlie_id,
        };
        EXPECT_EQ(expected_after_switch, host_after_switch.mapped_ids);
        EXPECT_EQ(charlie_id, host_display_client->controlled_entity_ids()[0]);
        EXPECT_EQ(bravo_id, host_display_client->controlled_entity_ids()[1]);
        EXPECT_EQ(host_display_client->controlled_entity_ids()[0],
                  cleanup.host_session->myscreen_->viewob[0]
                      ->control->entity_id());
        EXPECT_EQ("Charlie",
                  controlled_entity_name(
                      *host_display_client,
                      host_display_client->controlled_entity_ids()[0]));
        EXPECT_EQ("Bravo",
                  controlled_entity_name(
                      *host_display_client,
                      host_display_client->controlled_entity_ids()[1]));
        EXPECT_FALSE(host_after_switch.mapped_ids.contains(alpha_id));
    }

    {
        auto join_scope = join_session.activate();
        join_display_client = join_session.myscreen_->render_interpolation_client();
        ASSERT_NE(nullptr, join_display_client);
        const AlliedClaimObservation join_after_switch =
            observe_allied_claim_mapping(
                *join_session.myscreen_,
                *join_display_client);
        ASSERT_NE(nullptr, join_after_switch.alpha);
        ASSERT_NE(nullptr, join_after_switch.bravo);
        ASSERT_NE(nullptr, join_after_switch.charlie);
        ASSERT_NE(nullptr, join_session.myscreen_->viewob[0]);
        ASSERT_NE(nullptr, join_session.myscreen_->viewob[0]->control);
        ASSERT_EQ(join_after_switch.claimed_ids, join_after_switch.mapped_ids)
            << claim_mapping_details(*join_session.myscreen_,
                                     join_after_switch);
        EXPECT_EQ(nullptr, join_after_switch.orphaned)
            << claim_mapping_details(*join_session.myscreen_,
                                     join_after_switch);
        const std::set<std::uint32_t> expected_after_switch = {
            bravo_id,
            charlie_id,
        };
        EXPECT_EQ(expected_after_switch, join_after_switch.mapped_ids);
        EXPECT_EQ(charlie_id, join_display_client->controlled_entity_ids()[0]);
        EXPECT_EQ(bravo_id, join_display_client->controlled_entity_ids()[1]);
        EXPECT_EQ(join_display_client->controlled_entity_ids()[1],
                  join_session.myscreen_->viewob[0]->control->entity_id());
        EXPECT_EQ("Charlie",
                  controlled_entity_name(
                      *join_display_client,
                      join_display_client->controlled_entity_ids()[0]));
        EXPECT_EQ("Bravo",
                  controlled_entity_name(
                      *join_display_client,
                      join_display_client->controlled_entity_ids()[1]));
        EXPECT_FALSE(join_after_switch.mapped_ids.contains(alpha_id));
    }
}

TEST(PickerNetworkClient, join_relay_flow_connects_and_starts_game)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Relay Joiner");
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    FakeRelayServer relay_server(port);

    og::sim::RelayWebSocketTransport relay_host_transport(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-XKCD", port));
    relay_host_transport.accept_connections();
    ASSERT_TRUE(wait_until_host_owns_room(relay_host_transport));

    og::sim::LobbyServer lobby_server(relay_host_transport);

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Relay;
    options.room_code = "glad-xkcd";
    options.relay_base_url = std::format("http://127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    ASSERT_TRUE(wait_until([&] {
        lobby_server.poll_incoming_messages();
        (void)relay_host_transport.poll();
        join_client->poll_and_apply();
        return lobby_server.state().players.size() == 1u &&
            status_lines_contain_exact(
                join_client->status_lines(),
                "Room: GLAD-XKCD") &&
            status_lines_contain_exact(
                join_client->status_lines(),
                "Status: connected");
    })) << "relay join client should connect and become the first lobby player";

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return join_client->host_controls_visible();
    })) << "relay join client should become the visible host once the lobby state arrives";
    join_client->sync_roster_from_save();
    join_client->set_player_mode(0);

    ASSERT_TRUE(wait_until([&] {
        (void)join_client->request_start_game();
        lobby_server.poll_incoming_messages();
        (void)relay_host_transport.poll();
        join_client->poll_and_apply();
        return join_client->has_game_start_config();
    },
    10s)) << "relay join client should receive the start-game handoff";

    EXPECT_FALSE(join_client->start_request_pending());
    ASSERT_TRUE(join_client->build_game_start_config().has_value());
    const auto start_config = join_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ(1, start_config->save_data.scen_num);
    EXPECT_EQ(0u, start_config->save_data.numplayers);
    EXPECT_LE(start_config->save_data.team_list.size(), 1u);
    EXPECT_EQ(1, start_config->difficulty);

    ASSERT_NE(nullptr, active_game_session());
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*join_client));
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));
    og::runtime::clear_local_transport_shadow(*active_game_session());

    // P2: the lobby connection PERSISTS across gameplay. Installing the gameplay
    // runtime hands the transport to the per-level runtime but the join client
    // keeps its own ref, so after the runtime is torn down the socket is still
    // open — the team-build menu reuses it for the next level instead of
    // reconnecting. (Pre-P2 this reset the transport and reverted to
    // "connecting".)
    join_client->poll_and_apply();
    EXPECT_TRUE(
        status_lines_contain_exact(join_client->status_lines(), "Status: connected"));

    join_client->shutdown();
}

// [NET-R3] dedicated-server denial, end to end over the server_main transport
// shape (WebSocketServerTransport + a no-local-session LobbyServer): the
// first-connected peer is the ELECTED host, so a remote host's GO denial is
// the NORMAL dedicated path, not a rare one. The elected host reads the
// precise reason from the LobbyState ECHO (it has no in-process server state
// to consult), the dedicated lobby loop keeps polling on denial
// (consume_start_game_requested() false — server_main.cpp's loop read), and
// once the guest readies the SAME loop's consume flips true, the exact
// condition that breaks server_main into gameplay. Cheap: lobby only, no
// level runs.
TEST(PickerNetworkClient, dedicated_server_denial_echo_reaches_elected_host_then_start)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Elected Host");
    g_start_game_requested = false;

    // server_main's exact transport + lobby shape (no local session).
    const int port = ix::getFreePort();
    og::sim::WebSocketServerTransport::Options transport_options;
    transport_options.host = "127.0.0.1";
    og::sim::WebSocketServerTransport server_transport(port, transport_options);
    server_transport.accept_connections();
    og::sim::LobbyServer lobby_server(server_transport);

    og::ui::PickerJoinGameOptions host_options;
    host_options.mode = og::ui::PickerJoinMode::Direct;
    host_options.direct_endpoint = std::format("127.0.0.1:{}", port);
    auto elected_host = og::ui::create_join_picker_lobby_client(host_options);
    elected_host->initialize_from_save();

    // The elected host must connect FIRST (election is first-connected).
    ASSERT_TRUE(wait_until([&] {
        lobby_server.poll_incoming_messages();
        elected_host->poll_and_apply();
        return elected_host->host_controls_visible();
    })) << "the first-connected peer must be elected host";

    og::runtime::GameSession::Config guest_cfg;
    guest_cfg.create_display = false;
    guest_cfg.install_legacy_globals = false;
    og::runtime::GameSession guest_session(guest_cfg);
    prepare_single_member_network_save(
        guest_session.myscreen_->save_data, 1, "Dedicated Guest");
    og::ui::PickerJoinGameOptions guest_options;
    guest_options.mode = og::ui::PickerJoinMode::Direct;
    guest_options.direct_endpoint = std::format("127.0.0.1:{}", port);
    std::unique_ptr<og::ui::IPickerLobbyClient> guest_client;
    {
        auto guest_scope = guest_session.activate();
        guest_client = og::ui::create_join_picker_lobby_client(guest_options);
        guest_client->initialize_from_save();
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* guest_session = nullptr;
        og::ui::IPickerLobbyClient* elected_host = nullptr;
        og::ui::IPickerLobbyClient* guest_client = nullptr;
        ~CleanupGuard()
        {
            if (guest_session != nullptr && guest_client != nullptr)
            {
                auto guest_scope = guest_session->activate();
                guest_client->shutdown();
            }
            if (elected_host != nullptr)
                elected_host->shutdown();
        }
    } cleanup{&guest_session, elected_host.get(), guest_client.get()};

    const auto pump = [&] {
        lobby_server.poll_incoming_messages();
        elected_host->poll_and_apply();
        auto guest_scope = guest_session.activate();
        guest_client->poll_and_apply();
    };

    ASSERT_TRUE(wait_until([&] {
        pump();
        return lobby_server.state().players.size() == 2u &&
            status_lines_contain_exact(elected_host->status_lines(),
                                       "Lobby: 2 players");
    })) << "both machines should join the dedicated lobby";
    elected_host->sync_roster_from_save();

    // (1) The elected host's GO is DENIED while the guest is unready. The
    // reason arrives via the ASYNC state echo — the electing joiner's only
    // source. Production (go_menu) sends the request ONCE and then only
    // polls (`while (!started && pending) poll`), so this deliberately does
    // NOT retry request_start_game: the drain itself must release the
    // pending flag when the denial echo lands, or go_menu's timeout-less
    // wait loop would block forever on this exact dedicated-server shape.
    EXPECT_FALSE(elected_host->request_start_game());
    ASSERT_TRUE(wait_until([&] {
        pump();
        return !elected_host->start_request_pending();
    })) << "the async denial echo must clear the pending start request "
           "without a request retry (the go_menu hang regression)";
    EXPECT_EQ(og::sim::StartDenialReason::MachinesNotReady,
              elected_host->last_start_denial())
        << "the denial echo must reach the elected host with the precise reason";
    EXPECT_FALSE(g_start_game_requested);
    EXPECT_FALSE(elected_host->has_game_start_config());
    EXPECT_FALSE(lobby_server.consume_start_game_requested())
        << "server_main's loop read: the dedicated loop keeps polling on denial";

    // (2) The guest readies over the live (never locked) lobby.
    {
        auto guest_scope = guest_session.activate();
        (void)guest_client->set_ready(true);
    }
    ASSERT_TRUE(wait_until([&] {
        pump();
        auto guest_scope = guest_session.activate();
        return guest_client->local_ready();
    })) << "the guest's ready must land on the dedicated server";

    // (3) The elected host's GO now passes the gate; the StartGame broadcast
    // reaches BOTH machines and the dedicated loop's consume breaks to
    // gameplay.
    ASSERT_TRUE(wait_until(
        [&] {
            (void)elected_host->request_start_game();
            pump();
            bool guest_has_config = false;
            {
                auto guest_scope = guest_session.activate();
                guest_has_config = guest_client->has_game_start_config();
            }
            return elected_host->has_game_start_config() && guest_has_config;
        },
        10s)) << "both machines should receive the start-game handoff";
    EXPECT_EQ(og::sim::StartDenialReason::None,
              elected_host->last_start_denial())
        << "acceptance clears the denial echo";
    EXPECT_TRUE(lobby_server.consume_start_game_requested())
        << "server_main's break-into-gameplay read";
}

TEST(PickerNetworkClient,
     join_relay_flow_keeps_authoritative_peer_when_room_host_migrates)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Relay Joiner");

    const int port = ix::getFreePort();
    FakeRelayServer relay_server(port);
    const std::string relay_url =
        std::format("ws://127.0.0.1:{}/api/room/GLAD-XKCD", port);

    auto relay_host_transport =
        std::make_unique<og::sim::RelayWebSocketTransport>(relay_url);
    relay_host_transport->accept_connections();
    ASSERT_TRUE(wait_until_host_owns_room(*relay_host_transport));

    og::sim::RelayWebSocketTransport migrated_host_transport(relay_url);
    migrated_host_transport.accept_connections();
    ASSERT_TRUE(wait_until([&] {
        (void)relay_host_transport->poll();
        (void)migrated_host_transport.poll();
        return migrated_host_transport.local_peer_id().has_value() &&
            migrated_host_transport.connected_peers() ==
                std::vector<og::sim::PeerId>{1u};
    })) << "second relay peer should see the original host before migration";

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Relay;
    options.room_code = "glad-xkcd";
    options.relay_base_url = std::format("http://127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    ASSERT_TRUE(wait_until([&] {
        (void)relay_host_transport->poll();
        (void)migrated_host_transport.poll();
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Status: connected");
    })) << "relay join client should connect before host migration";

    relay_host_transport.reset();
    ASSERT_TRUE(wait_until([&] {
        (void)migrated_host_transport.poll();
        join_client->poll_and_apply();
        return migrated_host_transport.local_peer_id().has_value() &&
            migrated_host_transport.host_peer_id() ==
                migrated_host_transport.local_peer_id();
    })) << "remaining relay peer should become the room host after the original host leaves";

    save.team_list[0]->name = "Relay Joiner Prime";
    join_client->sync_roster_from_save();
    join_client->poll_and_apply();

    const bool migrated_host_received_join = wait_until([&] {
        return !poll_lobby_messages(migrated_host_transport).empty();
    }, 500ms);
    EXPECT_FALSE(migrated_host_received_join)
        << "join client should stay targeted at the original authoritative peer";

    join_client->shutdown();
}

TEST(PickerNetworkClient,
     join_relay_flow_reports_connection_failed_when_relay_unreachable)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Relay Joiner");

    // Nothing listens on this port: the relay websocket connect is refused
    // before the link ever comes up.
    const int dead_port = ix::getFreePort();

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Relay;
    options.room_code = "glad-xkcd";
    options.relay_base_url = std::format("http://127.0.0.1:{}", dead_port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Status: connection failed");
    },
    10s)) << "an unreachable relay must surface as a failed connection "
             "instead of an eternal 'Status: connecting'";
    EXPECT_TRUE(status_lines_contain_exact(
        join_client->status_lines(),
        "Room: GLAD-XKCD"));

    // The base camp's §2.5 line-B alert mirrors the failed link, and the
    // free function resolves it through the installed active client (no
    // client installed => no alert => line B keeps the §9.12 session
    // status).
    ASSERT_TRUE(join_client->connection_alert().has_value());
    EXPECT_EQ("Status: connection failed", *join_client->connection_alert());
    EXPECT_FALSE(picker_lobby_connection_alert().has_value());
    {
        ActivePickerLobbyClientGuard active_guard(join_client.get());
        ASSERT_TRUE(picker_lobby_connection_alert().has_value());
        EXPECT_EQ("Status: connection failed",
                  *picker_lobby_connection_alert());
    }

    join_client->shutdown();
}

TEST(PickerNetworkClient,
     join_relay_flow_reports_connection_lost_after_relay_drops)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Relay Joiner");

    const int port = ix::getFreePort();
    auto relay_server = std::make_unique<FakeRelayServer>(port);

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Relay;
    options.room_code = "glad-xkcd";
    options.relay_base_url = std::format("http://127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Status: connected");
    },
    10s)) << "relay join client should report a connected link";
    // Healthy link: no alert — line B keeps the §9.12 session status,
    // which names the joined room.
    EXPECT_FALSE(join_client->connection_alert().has_value());
    EXPECT_EQ("GLAD-XKCD", join_client->session_room_code());

    relay_server.reset();

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
            join_client->status_lines(),
            "Status: connection lost");
    },
    10s)) << "a relay drop after connecting must surface as a lost "
             "connection instead of reverting to 'Status: connecting'";
    ASSERT_TRUE(join_client->connection_alert().has_value());
    EXPECT_EQ("Status: connection lost", *join_client->connection_alert());

    join_client->shutdown();
}

TEST(PickerNetworkClient,
     host_relay_flow_reports_connection_lost_after_relay_drops)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");

    const int relay_port = ix::getFreePort();
    auto relay_server = std::make_unique<FakeRelayServer>(
        relay_port,
        200,
        R"({"code":"glad-xkcd","owner_token":"owner-secret-token"})");

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    options.enable_relay = true;
    options.relay_base_url = std::format("ws://127.0.0.1:{}", relay_port);
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    host_client->initialize_from_save();

    EXPECT_TRUE(status_lines_contain_exact(
        host_client->status_lines(),
        "Room: GLAD-XKCD"));
    // A live advertised room raises no alert on the host's base camp: the
    // §9.12 line B carries the session status with the room code.
    EXPECT_FALSE(host_client->connection_alert().has_value());
    EXPECT_EQ("GLAD-XKCD", host_client->session_room_code());
    {
        const og::ui::BaseCampLineB header = og::ui::compose_base_camp_line_b(
            host_client->connection_alert(),
            host_client->host_controls_visible(),
            host_client->session_room_code(),
            host_client->lobby_players());
        EXPECT_FALSE(header.alert);
        EXPECT_EQ("HOSTING GLAD-XKCD - 1 MACH / 1 PLYR", header.text)
            << "a healthy hosted room shows role + room + census";
    }

    relay_server.reset();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        return status_lines_contain_exact(
            host_client->status_lines(),
            "Relay: connection lost");
    },
    10s)) << "a dead relay room must stop advertising 'Room: GLAD-XKCD'";
    EXPECT_FALSE(status_lines_contain_exact(
        host_client->status_lines(),
        "Room: GLAD-XKCD"));
    ASSERT_TRUE(host_client->connection_alert().has_value());
    EXPECT_EQ("Relay: connection lost", *host_client->connection_alert());
    {
        // §9.12 precedence: the degraded alert takes the line-B slot (and
        // its ORANGE color) over the session status.
        const og::ui::BaseCampLineB header = og::ui::compose_base_camp_line_b(
            host_client->connection_alert(),
            host_client->host_controls_visible(),
            host_client->session_room_code(),
            host_client->lobby_players());
        EXPECT_TRUE(header.alert);
        EXPECT_EQ("Relay: connection lost", header.text);
    }

    host_client->shutdown();
}

TEST(PickerNetworkClient, host_initialization_reports_direct_and_relay_failures)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    g_start_game_requested = false;

    const int blocked_port = ix::getFreePort();
    auto blocking_server =
        std::make_shared<og::sim::WebSocketServerTransport>(blocked_port);
    blocking_server->accept_connections();

    const int relay_port = ix::getFreePort();
    FakeRelayServer relay_server(relay_port, 503, "room service down");
    og::ui::PickerHostGameOptions options;
    options.port = blocked_port;
    options.enable_relay = true;
    options.relay_base_url = std::format("http://127.0.0.1:{}", relay_port);
    auto host_client = og::ui::create_host_picker_lobby_client(options);

    try
    {
        host_client->initialize_from_save();
        FAIL() << "host initialize should fail when both direct and relay transports fail";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_FALSE(message.empty());
    }
}

TEST(PickerNetworkClient, host_initialization_reports_direct_only_failures)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    g_start_game_requested = false;

    const int blocked_port = ix::getFreePort();
    auto blocking_server =
        std::make_shared<og::sim::WebSocketServerTransport>(blocked_port);
    blocking_server->accept_connections();

    og::ui::PickerHostGameOptions options;
    options.port = blocked_port;
    auto host_client = og::ui::create_host_picker_lobby_client(options);

    try
    {
        host_client->initialize_from_save();
        FAIL() << "host initialize should fail when the direct listener cannot bind";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_FALSE(message.empty());
        EXPECT_EQ(std::string::npos, message.find("Relay:"));
    }
}

TEST(PickerNetworkClient, validation_helpers_reject_invalid_network_picker_inputs)
{
    og::ui::PickerHostGameOptions bad_host;
    bad_host.port = 70000;
    EXPECT_THROW(og::ui::create_host_picker_lobby_client(bad_host),
                 std::invalid_argument);

    og::ui::PickerJoinGameOptions direct_options;
    direct_options.mode = og::ui::PickerJoinMode::Direct;
    EXPECT_THROW(og::ui::create_join_picker_lobby_client(direct_options),
                 std::invalid_argument);

    og::ui::PickerJoinGameOptions relay_options;
    relay_options.mode = og::ui::PickerJoinMode::Relay;
    EXPECT_THROW(og::ui::create_join_picker_lobby_client(relay_options),
                 std::invalid_argument);

    EXPECT_FALSE(og::ui::picker_join_mode_supported(
        static_cast<og::ui::PickerJoinMode>(99)));
    EXPECT_EQ("ws://127.0.0.1:12345",
              og::ui::normalize_direct_websocket_url("127.0.0.1:12345"));
    EXPECT_THROW(og::ui::normalize_direct_websocket_url("ftp://relay.invalid"),
                 std::invalid_argument);
    EXPECT_EQ("RELAY-42", og::ui::normalize_relay_room_code(" relay-42 "));
    EXPECT_THROW(og::ui::normalize_relay_room_code("bad room"),
                 std::invalid_argument);
    EXPECT_EQ("https://relay.invalid",
              og::ui::normalize_relay_base_url(" https://relay.invalid/// "));
    EXPECT_THROW(og::ui::normalize_relay_base_url("ftp://relay.invalid"),
                 std::invalid_argument);
}

TEST(PickerNetworkClient, internal_helpers_cover_network_picker_paths)
{
    EXPECT_EQ(
        0,
        og::ui::detail::picker_lobby_network_testing_exercise_internal_helpers());
}

// Team choice + ready over a real host/join lobby: classic exclusivity
// bounces the joiner off the host's team, switching the lobby to the CTF
// campaign relaxes sharing so the same request lands, and the informational
// ready flag round-trips into both peers' lobby_players().
TEST(PickerNetworkClient, host_and_join_team_change_and_ready_round_trip)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host");
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();
    EXPECT_TRUE(host_client->is_networked_session());

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
        EXPECT_TRUE(join_client->is_networked_session());
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    // Classic campaign: one human per team — the joiner's request for the
    // host's team must bounce and leave the joiner where it was.
    {
        auto join_scope = join_session.activate();
        EXPECT_FALSE(join_client->request_team_change(0))
            << "classic lobbies keep exclusive teams";
        EXPECT_EQ(1, join_session.myscreen_->save_data.my_team);
    }

    // CTF campaign settings relax team sharing; the same request lands.
    host_save.current_campaign = "org.openglad.ctf";
    host_client->sync_settings_from_save();
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_sees_ctf = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_sees_ctf = join_session.myscreen_->save_data.current_campaign ==
                "org.openglad.ctf";
        }
        return join_sees_ctf;
    })) << "joiner should receive the CTF campaign settings";

    {
        auto join_scope = join_session.activate();
        EXPECT_TRUE(join_client->request_team_change(0))
            << "CTF lobbies allow shared teams";
        EXPECT_EQ(0, join_session.myscreen_->save_data.my_team);
    }
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        const auto players = host_client->lobby_players();
        if (players.size() != 2u)
            return false;
        return players[0].team == 0 && players[1].team == 0;
    })) << "host should see both players sharing team 0";

    // Ready round trip: the joiner readies up, the host sees the tag (and
    // its own flag stays down until it toggles too). The joiner's send needs
    // the host to pump its LobbyServer, so converge with both sides polling.
    {
        auto join_scope = join_session.activate();
        EXPECT_FALSE(join_client->local_ready());
        (void)join_client->set_ready(true);
    }
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
        }
        for (const og::sim::LobbyPlayer& player : host_client->lobby_players())
        {
            if (!player.is_host && player.ready)
                return true;
        }
        return false;
    })) << "host should see the joiner's ready tag";
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_ready = join_client->local_ready();
        }
        return join_ready;
    })) << "the ready echo should land on the joiner too";
    EXPECT_FALSE(host_client->local_ready());

    EXPECT_TRUE(host_client->set_ready(true));
    ASSERT_TRUE(wait_until([&] {
        bool join_sees_host_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            for (const og::sim::LobbyPlayer& player :
                 join_client->lobby_players())
            {
                if (player.is_host && player.ready)
                    join_sees_host_ready = true;
            }
        }
        return join_sees_host_ready;
    })) << "joiner should see the host's ready tag";

    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    host_client->shutdown();
    cleanup.host_client = nullptr;
    cleanup.join_client = nullptr;
}

// Regression test for the upstream IXSocketServer local fix (see
// docs/external-dependencies.md): SocketServer::stop() used to close _serverFd
// without resetting it to -1, so the destructor chain (~WebSocketServer ->
// stop(), ~SocketServer -> stop()) re-closed the same fd number after it had
// been freed. If another thread had reused the number in between (e.g. glibc
// getaddrinfo's AI_ADDRCONFIG netlink probe on a detached DNS-lookup thread),
// the stale close destroyed that thread's descriptor and glibc aborted the
// whole process ("Unexpected error 9 on netlink descriptor N"). The
// cross-thread collision itself is not deterministically reproducible, but
// the stale close is: occupy every fd number freed by stop() with a
// placeholder, destroy the server, and verify no placeholder was closed.
TEST(WebSocketServerFdLifecycle,
     stop_then_destroy_does_not_close_reused_fd_numbers)
{
    IxNetSystemScope net_scope;

    const auto open_fds = [] {
        std::set<int> fds;
        for (int fd = 0; fd < 1024; ++fd)
        {
            if (fcntl(fd, F_GETFD) != -1)
                fds.insert(fd);
        }
        return fds;
    };

    // Opened before the server so it cannot grab the fd number the server
    // releases later.
    const int placeholder_source = ::open("/dev/null", O_RDONLY);
    ASSERT_NE(placeholder_source, -1);

    auto server = std::make_unique<ix::WebSocketServer>(
        ix::getFreePort(), "127.0.0.1");
    const auto listen_result = server->listen();
    ASSERT_TRUE(listen_result.first) << listen_result.second;
    server->start();
    const std::set<int> fds_while_listening = open_fds();

    server->stop(); // The one legitimate close of the listening fd.
    const std::set<int> fds_after_stop = open_fds();

    // Re-occupy every fd number stop() released, the way a concurrent
    // thread's freshly-opened descriptor would.
    std::vector<int> placeholders;
    for (const int fd : fds_while_listening)
    {
        if (fds_after_stop.count(fd) == 0)
        {
            ASSERT_EQ(::dup2(placeholder_source, fd), fd);
            placeholders.push_back(fd);
        }
    }
    ASSERT_FALSE(placeholders.empty())
        << "stop() should have released the listening fd";

    // Before the fix, the destructor chain re-closed the stale fd number(s)
    // and would destroy the placeholders standing in for another thread's fd.
    server.reset();

    for (const int fd : placeholders)
    {
        EXPECT_NE(fcntl(fd, F_GETFD), -1)
            << "destroying a stopped server closed unrelated fd " << fd;
        ::close(fd);
    }
    ::close(placeholder_source);
}

// ---------------------------------------------------------------------------
// Difficulty-submenu settings must replicate through the lobby wire: the
// host cycles Respawns/Generators (plus delay and permadeath) exactly as the
// subscreen callbacks do (pure cycler + sync_settings_from_save) and the
// joiner's save must reflect every value; a second cycle round must land too.
// ---------------------------------------------------------------------------
#include <openglad/interface/ui/picker_common.h>

TEST(PickerNetworkClient, host_and_join_difficulty_settings_sync_to_joiner_save)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host");
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();
    EXPECT_TRUE(host_client->is_networked_session());

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Joiner");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
        EXPECT_TRUE(join_client->is_networked_session());
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    // The joiner starts on the defaults (all zero = classic behavior).
    {
        auto join_scope = join_session.activate();
        const SaveData& join_save = join_session.myscreen_->save_data;
        EXPECT_EQ(0, static_cast<int>(join_save.respawn_mode));
        EXPECT_EQ(0, static_cast<int>(join_save.generator_rate));
        EXPECT_EQ(0, static_cast<int>(join_save.keep_fallen_heroes));
        EXPECT_EQ(0, static_cast<int>(join_save.ctf_respawn_ticks));
    }

    // Host cycles: Respawns Off->Heroes, Generators Normal->Calm->Frenzy,
    // Delay Normal->Fast, Permadeath On->Off — then one settings sync, the
    // exact shape of the difficulty-subscreen button callbacks.
    og::ui::cycle_respawn_mode(host_save);   // 0 -> 1 (Heroes)
    og::ui::cycle_generator_rate(host_save); // 0 -> 50 (Calm)
    og::ui::cycle_generator_rate(host_save); // 50 -> 200 (Frenzy)
    og::ui::cycle_respawn_delay(host_save);  // 0 -> 60 ticks (Fast)
    og::ui::toggle_permadeath(host_save);    // keep_fallen_heroes = 1
    host_client->sync_settings_from_save();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_synced = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            const SaveData& join_save = join_session.myscreen_->save_data;
            join_synced = join_save.respawn_mode == 1 &&
                join_save.generator_rate == 200 &&
                join_save.ctf_respawn_ticks == 60 &&
                join_save.keep_fallen_heroes == 1;
        }
        return join_synced;
    })) << "the joiner's save must reflect the host's difficulty settings";

    // A second cycle round (Respawns Heroes->Everyone, Generators
    // Frenzy->Normal) must replicate as well, proving the sync is not a
    // one-shot initial-state copy.
    og::ui::cycle_respawn_mode(host_save);   // 1 -> 2 (Everyone)
    og::ui::cycle_generator_rate(host_save); // 200 -> 0 (Normal)
    host_client->sync_settings_from_save();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_synced = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            const SaveData& join_save = join_session.myscreen_->save_data;
            join_synced = join_save.respawn_mode == 2 &&
                join_save.generator_rate == 0 &&
                join_save.ctf_respawn_ticks == 60 &&
                join_save.keep_fallen_heroes == 1;
        }
        return join_synced;
    })) << "a second settings cycle must replicate to the joiner too";

    // The host's own save keeps the values it cycled to (the lobby echo
    // must not clobber the host mid-edit).
    EXPECT_EQ(2, static_cast<int>(host_save.respawn_mode));
    EXPECT_EQ(0, static_cast<int>(host_save.generator_rate));
    EXPECT_EQ(60, static_cast<int>(host_save.ctf_respawn_ticks));
    EXPECT_EQ(1, static_cast<int>(host_save.keep_fallen_heroes));

    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    host_client->shutdown();
    cleanup.host_client = nullptr;
    cleanup.join_client = nullptr;
}

// ---------------------------------------------------------------------------
// [NET-R5] §4.2 + §2.5 U8: networked clients answer is_save_slot_editable
// with OWN slots only. The private picker save's occupied rows are untagged
// (editable, the every-existing-flow shape); a row tagged with one of this
// machine's own player indices (the post-win merged shape) stays editable;
// a FOREIGN owner tag (a netsession-shaped combined roster) locks the row —
// and TrainSession's constructor/PREV/NEXT/seek_slot clamp around it, which
// is the base-camp per-row TRAIN ownership clamp made real at the
// production-client level.
// ---------------------------------------------------------------------------
TEST(PickerNetworkClient,
     networked_editability_locks_foreign_rows_and_clamps_train_cycle)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host Own");
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();
    ASSERT_TRUE(host_client->is_networked_session());

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Join Own");

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
        ASSERT_TRUE(join_client->is_networked_session());
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    // Machine identities from the replicated state (names are generated).
    std::uint8_t host_index = 0xff;
    std::uint8_t join_index = 0xff;
    for (const og::sim::LobbyPlayer& player : host_client->lobby_players())
    {
        if (player.is_host)
            host_index = player.player_index;
        else
            join_index = player.player_index;
    }
    ASSERT_NE(0xff, host_index);
    ASSERT_NE(0xff, join_index);
    ASSERT_NE(host_index, join_index);

    // The host's private untagged roster stays fully editable (the flip
    // never locks a machine's own rows — every legacy flow's shape).
    EXPECT_TRUE(host_client->is_save_slot_editable(0));

    {
        auto join_scope = join_session.activate();
        SaveData& join_save = join_session.myscreen_->save_data;

        // Shape the joiner's in-memory save like the transient post-level
        // window: slot 0 untagged (private roster), slot 1 tagged with the
        // joiner's OWN player index (a merged-back survivor is a copy of a
        // tagged session guy), slot 2 tagged with the HOST's index (a
        // netsession-shaped foreign row).
        auto own_tagged = std::make_unique<guy>(FAMILY_ARCHER);
        own_tagged->name = "Merged Own";
        own_tagged->teamnum = 1;
        own_tagged->owner_player_index = join_index;
        auto foreign = std::make_unique<guy>(FAMILY_MAGE);
        foreign->name = "Foreign Row";
        foreign->teamnum = 0;
        foreign->owner_player_index = host_index;
        join_save.team_list[1] = std::move(own_tagged);
        join_save.team_list[2] = std::move(foreign);
        join_save.team_size = 3;

        EXPECT_TRUE(join_client->is_save_slot_editable(0))
            << "untagged private rows stay editable";
        EXPECT_TRUE(join_client->is_save_slot_editable(1))
            << "own-tagged (merged) rows stay editable";
        EXPECT_FALSE(join_client->is_save_slot_editable(2))
            << "[NET-R5] foreign-tagged rows are never editable";
        EXPECT_TRUE(join_client->is_save_slot_editable(5))
            << "empty slots keep the hire-fillable shape";
        EXPECT_FALSE(join_client->is_save_slot_editable(MAX_TEAM_SIZE));

        // §2.5 U8: TrainSession's cycle clamps to own characters through
        // the production client (the free-fn route the SDL/text/curses
        // sessions all take).
        ActivePickerLobbyClientGuard active_guard(join_client.get());
        og::ui::TrainSession session(join_save);
        ASSERT_FALSE(session.empty());
        EXPECT_EQ(0, session.current_slot());
        session.next_member();
        EXPECT_EQ(1, session.current_slot());
        session.next_member();
        EXPECT_EQ(0, session.current_slot())
            << "NEXT must wrap past the foreign row";
        session.prev_member();
        EXPECT_EQ(1, session.current_slot())
            << "PREV must skip the foreign row";
        EXPECT_FALSE(session.seek_slot(2))
            << "per-row TRAIN seeding on a foreign row is refused";
        EXPECT_EQ(1, session.current_slot())
            << "a refused seek leaves the session where it was";
        EXPECT_TRUE(session.seek_slot(0));
        EXPECT_EQ(0, session.current_slot());
    }

    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    host_client->shutdown();
    cleanup.host_client = nullptr;
    cleanup.join_client = nullptr;
}

// §2.5 MP presentation (stage mp-columns) over a REAL direct lobby: the
// joiner's base-camp display merges its own private rows with the host's
// replicated slots (COMPANY column = LobbyPlayer::company off the wire),
// local_player_indices identifies the own seat, a foreign click pops
// OWNED BY <company>, and an own-row deploy toggle through the production
// spec dispatch clears this machine's ready (§4.3: optimistic local drop +
// the server-side content-change clear, observed from the HOST's state).
TEST(PickerNetworkClient,
     base_camp_mp_display_merges_wire_rows_and_deploy_toggle_clears_ready)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host Front");
    const std::string old_host_company = host_save.save_name;
    host_save.save_name = "IRON HOST BAND";
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();
    ASSERT_TRUE(host_client->is_networked_session());

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Join Front");
    join_session.myscreen_->save_data.save_name = "JOIN SIDE BAND";

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
        ASSERT_TRUE(join_client->is_networked_session());
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    std::uint8_t host_index = 0xff;
    std::uint8_t join_index = 0xff;
    for (const og::sim::LobbyPlayer& player : host_client->lobby_players())
    {
        if (player.is_host)
            host_index = player.player_index;
        else
            join_index = player.player_index;
    }
    ASSERT_NE(0xff, host_index);
    ASSERT_NE(0xff, join_index);

    // The joiner readies up first so the toggle's ready-clear is visible.
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}));

    const og::ui::MenuScreenSpec* const spec =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec);
    ASSERT_NE(nullptr, spec->on_spec_row);

    {
        auto join_scope = join_session.activate();
        ActivePickerLobbyClientGuard active_guard(join_client.get());
        SaveData& join_save = join_session.myscreen_->save_data;

        // The wire identifies this machine's own seat.
        const std::vector<std::uint8_t> local_indices =
            join_client->local_player_indices();
        ASSERT_EQ(1u, local_indices.size());
        EXPECT_EQ(join_index, local_indices[0]);

        // The merged display: own private row first, then the host's
        // replicated slot carrying the wire company string.
        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        ASSERT_EQ(2u, state.slots.size());
        EXPECT_TRUE(state.slots[0].owned);
        EXPECT_EQ(0, state.slots[0].save_slot);
        EXPECT_EQ("JOIN SIDE BAND", state.slots[0].company);
        EXPECT_FALSE(state.slots[1].owned);
        EXPECT_EQ(host_index, state.slots[1].owner_player_index);
        EXPECT_EQ("IRON HOST BAND", state.slots[1].company)
            << "the COMPANY column reads LobbyPlayer::company off the wire";
        EXPECT_EQ("Host Front", state.slots[1].character.name);

        // Foreign row clicks are read-only popups (deploy hit zone + the
        // guarded §9.11 row-body ordinal).
        trace_clear();
        EXPECT_EQ(MENU_OK, spec->on_spec_row(1, &state));
        EXPECT_TRUE(trace_contains("popup", "OWNED BY: IRON HOST BAND"));
        EXPECT_EQ(MENU_OK,
                  spec->on_spec_row(kBaseCampRowBodyBase + 1, &state));
        EXPECT_FALSE(trace_contains("basecamp", "train slot="));
        EXPECT_TRUE(join_client->local_ready())
            << "foreign clicks must not clear ready";

        // Own-row deploy toggle through the production dispatch: the flag
        // flips in the private save the same call (the trace pins it); the
        // §4.3 mutation tail re-syncs the roster (a content change the
        // server answers with a ready-clear) and sends the local ready
        // drop. NOTE: the [NET-F2] adoption is "server echo wins", so a
        // stale in-flight echo may transiently revert the local flag — the
        // convergence loop below pins the settled state, not this instant.
        trace_clear();
        EXPECT_EQ(MENU_OK, spec->on_spec_row(0, &state));
        EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 off"));
    }

    // Server side: the joiner's re-sent roster is a content CHANGE, so the
    // authoritative state clears that machine's ready and echoes the
    // benched flag to every peer (the host display would dim the row; the
    // joiner's READY face reverts). Both views converge on unready.
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_converged = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_converged = !join_client->local_ready() &&
                join_session.myscreen_->save_data.team_list[0] != nullptr &&
                !join_session.myscreen_->save_data.team_list[0]->deployed;
        }
        for (const og::sim::LobbyPlayer& player :
             host_client->lobby_players())
        {
            if (player.player_index != join_index)
                continue;
            return join_converged && !player.ready &&
                !player.character_slots.empty() &&
                !player.character_slots.front().deployed;
        }
        return false;
    })) << "the server must clear the toggling machine's ready and "
           "replicate the benched flag";

    host_save.save_name = old_host_company;
    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    host_client->shutdown();
    cleanup.host_client = nullptr;
    cleanup.join_client = nullptr;
}

// §2.6/§2.7 (stage ready-go-slot) over a REAL direct lobby: the host's GO
// pre-check popups WAITING FOR: <the joiner's company> and sends NO start
// request; the joiner readies through the PRODUCTION base-camp READY twin
// (teams_toggle_ready with the twin's index) and sees the flip in the same
// call (the §2.5 same-frame contract — set_ready blocks on the echo); the
// host's presentation flips yellow -> green; a host cross-control toggle
// through the PRODUCTION TEAMS dispatch propagates to the joiner's save AND
// clears the joiner's ready (§4.5 settings-clear-ready).
TEST(PickerNetworkClient,
     base_camp_ready_go_slot_and_cross_control_clear_ready)
{
    IxNetSystemScope net_system;

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host Front");
    const std::string old_host_company = host_save.save_name;
    host_save.save_name = "IRON HOST BAND";
    host_save.cross_control = 0;
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();
    ASSERT_TRUE(host_client->is_networked_session());

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    prepare_single_member_network_save(
        join_session.myscreen_->save_data, 1, "Join Front");
    join_session.myscreen_->save_data.save_name = "JOIN SIDE BAND";

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
        ASSERT_TRUE(join_client->is_networked_session());
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_lobby_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_lobby_ready = status_lines_contain_exact(
                join_client->status_lines(), "Lobby: 2 players");
        }
        return status_lines_contain_exact(host_client->status_lines(),
                                          "Lobby: 2 players") &&
            join_lobby_ready;
    })) << "host and join should converge on a two-player lobby";

    // §2.6 state 3 on the HOST: yellow face, WAITING FOR: names the
    // joiner's company, and go_menu sends NOTHING.
    {
        ActivePickerLobbyClientGuard active_guard(host_client.get());
        const og::ui::ReadyGoPresentation gated =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::HostGated, gated.state);
        EXPECT_EQ(og::ui::kReadyGoFaceGated, gated.face_color);
        EXPECT_EQ("WAITING FOR OTHERS", gated.caption);
        EXPECT_NE(std::string::npos,
                  og::ui::format_go_blockers(host_client->lobby_players())
                      .find("JOIN SIDE BAND"));

        trace_clear();
        EXPECT_EQ(MENU_REDRAW, go_menu(0));
        EXPECT_TRUE(trace_contains("popup", "WAITING FOR:"));
        EXPECT_TRUE(trace_contains("popup", "JOIN SIDE BAND"));
        EXPECT_FALSE(g_start_game_requested)
            << "state 3 must not launch anything";
        EXPECT_FALSE(host_client->start_request_pending())
            << "state 3 must not even send the request";
    }

    // The joiner readies through the PRODUCTION READY-twin dispatch. The
    // join client's set_ready blocks up to ~500ms for the echo (the §2.5
    // same-frame mechanism against a live remote host); in THIS in-process
    // harness the server only runs inside host_client->poll_and_apply(), so
    // the flip converges once both sides pump — the presentation then
    // re-derives UNREADY/green from the echoed state (the §2.6 ready-trap
    // rule (d): the UI re-derives every frame, never a cached flag).
    {
        auto join_scope = join_session.activate();
        ActivePickerLobbyClientGuard active_guard(join_client.get());
        const og::ui::ReadyGoPresentation before =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::ClientUnready, before.state);
        EXPECT_EQ("READY", before.label);
        EXPECT_EQ(og::ui::kReadyGoFaceUnready, before.face_color);
        EXPECT_EQ(MENU_OK, teams_toggle_ready(kCreateMenuReadyIndex));
    }
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        auto join_scope = join_session.activate();
        join_client->poll_and_apply();
        return join_client->local_ready();
    })) << "the READY twin's set_ready must reach the server and echo back";
    {
        auto join_scope = join_session.activate();
        ActivePickerLobbyClientGuard active_guard(join_client.get());
        const og::ui::ReadyGoPresentation after =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::ClientReady, after.state);
        EXPECT_EQ("UNREADY", after.label);
        EXPECT_EQ(og::ui::kReadyGoFaceGo, after.face_color);
    }

    // The host's slot flips yellow -> green on the last ready (§2.9 flow 6).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        ActivePickerLobbyClientGuard active_guard(host_client.get());
        return picker_compute_ready_go_presentation().state ==
            og::ui::ReadyGoState::HostGo;
    })) << "all-ready + deployed must gate the host GO green";

    // §2.7: the host toggles cross-control through the PRODUCTION TEAMS
    // dispatch — the wire carries it to the joiner's session save AND the
    // settings change clears the joiner's ready (§4.5), flipping the
    // host's GO back to gated.
    const og::ui::MenuScreenSpec* const teams_spec =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Teams).spec;
    ASSERT_NE(nullptr, teams_spec);
    ASSERT_NE(nullptr, teams_spec->on_spec_row);
    {
        ActivePickerLobbyClientGuard active_guard(host_client.get());
        trace_clear();
        EXPECT_EQ(MENU_OK,
                  teams_spec->on_spec_row(kTeamsMenuCrossControlIndex,
                                          nullptr));
        EXPECT_TRUE(trace_contains("teams", "cross_control 1"));
        EXPECT_EQ(1, host_save.cross_control);
    }
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_converged = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_converged =
                join_session.myscreen_->save_data.cross_control == 1 &&
                !join_client->local_ready();
        }
        return join_converged;
    })) << "the settings change must reach the joiner and clear its ready";
    {
        ActivePickerLobbyClientGuard active_guard(host_client.get());
        EXPECT_EQ(og::ui::ReadyGoState::HostGated,
                  picker_compute_ready_go_presentation().state)
            << "the ready-clear re-gates the host GO";
    }

    host_save.save_name = old_host_company;
    host_save.cross_control = 0;
    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    host_client->shutdown();
    cleanup.host_client = nullptr;
    cleanup.join_client = nullptr;
}

// §3.8 [SAVE-F1] over a REAL direct lobby (the WP2-deferred join-and-train
// e2e): a networked-lobby mutation autosave is a MERGE write, never a plain
// save. The joiner's private company file sits at scen 5 with its own
// settings; joining a host parked on scen 2 overwrites the IN-MEMORY save
// with the host's cursor/settings (apply_lobby_state_to_save), and training
// a character runs the PRODUCTION mutation tail (TrainSession::accept +
// picker_base_camp_after_roster_mutation). The autosave must overlay ONLY
// the machine's own roster + owned wallet onto the DISK file, preserve the
// private cursor/settings byte-for-byte, leave the session save untouched —
// and the roster re-sync must clear the machine's ready (§4.3).
TEST(PickerNetworkClient,
     save_f1_join_and_train_merge_preserves_private_cursor)
{
    IxNetSystemScope net_system;

    static constexpr const char kWp4F1JoinSlot[] = "wp4f1join";
    struct SlotFileReaper
    {
        ~SlotFileReaper()
        {
            (void)remove_user_file(std::string("save/") + kWp4F1JoinSlot +
                                   ".gtl");
        }
    } slot_file_reaper;
    og::data::ScopedActiveCompany active_company(kWp4F1JoinSlot);
    ASSERT_TRUE(active_company.applied());

    SaveData& host_save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard host_save_guard(host_save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(host_save, 0, "Host Front");
    const std::string old_host_company = host_save.save_name;
    host_save.save_name = "IRON HOST BAND";
    // The host is parked past the joiner's private cursor: its scen 2
    // becomes the lobby's level and lands in the joiner's SESSION save.
    host_save.scen_num = 2;
    host_save.current_levels[host_save.current_campaign] = 2;
    g_start_game_requested = false;

    og::ui::PickerHostGameOptions host_options;
    host_options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(host_options);
    host_client->initialize_from_save();
    ASSERT_TRUE(host_client->is_networked_session());

    og::runtime::GameSession::Config join_cfg;
    join_cfg.create_display = false;
    join_cfg.install_legacy_globals = false;
    og::runtime::GameSession join_session(join_cfg);
    SaveData& join_save = join_session.myscreen_->save_data;
    prepare_single_member_network_save(join_save, 1, "Join Front");
    join_save.save_name = "F1 PRIVATE BAND";
    join_save.scen_num = 5;
    join_save.current_levels[join_save.current_campaign] = 5;
    join_save.respawn_mode = 1;
    join_save.generator_rate = 2;
    join_save.m_totalcash[1] = 5000;
    // The private on-disk company [SAVE-F1] merges into (a missing file is a
    // logged no-op, never a clobber-create).
    ASSERT_TRUE(join_save.save(kWp4F1JoinSlot));

    og::ui::PickerJoinGameOptions join_options;
    join_options.mode = og::ui::PickerJoinMode::Direct;
    join_options.direct_endpoint =
        std::format("127.0.0.1:{}", host_options.port);
    std::unique_ptr<og::ui::IPickerLobbyClient> join_client;
    {
        auto join_scope = join_session.activate();
        join_client = og::ui::create_join_picker_lobby_client(join_options);
        join_client->initialize_from_save();
        ASSERT_TRUE(join_client->is_networked_session());
    }

    struct CleanupGuard
    {
        og::runtime::GameSession* join_session = nullptr;
        og::ui::IPickerLobbyClient* host_client = nullptr;
        og::ui::IPickerLobbyClient* join_client = nullptr;

        ~CleanupGuard()
        {
            if (join_session != nullptr)
            {
                auto join_scope = join_session->activate();
                if (join_client != nullptr)
                    join_client->shutdown();
            }
            if (host_client != nullptr)
                host_client->shutdown();
        }
    } cleanup;
    cleanup.join_session = &join_session;
    cleanup.host_client = host_client.get();
    cleanup.join_client = join_client.get();

    // Converge: two players, and the host's level-2 cursor lands in the
    // joiner's SESSION save (the in-memory overwrite [SAVE-F1] defends the
    // disk file against).
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_ready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_ready = join_save.scen_num == 2 &&
                status_lines_contain_exact(join_client->status_lines(),
                                           "Lobby: 2 players");
        }
        return join_ready &&
            status_lines_contain_exact(host_client->status_lines(),
                                       "Lobby: 2 players");
    })) << "the joiner must adopt the host's level-2 session cursor";

    std::uint8_t join_index = 0xff;
    for (const og::sim::LobbyPlayer& player : host_client->lobby_players())
    {
        if (!player.is_host)
            join_index = player.player_index;
    }
    ASSERT_NE(0xff, join_index);

    // Ready up first so the mutation's §4.3 ready-clear is observable.
    ASSERT_TRUE(ready_up_joiners(*host_client,
                                 {{&join_session, join_client.get()}}));

    // Train one character through the production session + mutation tail.
    short trained_strength = 0;
    std::uint32_t train_cost = 0;
    {
        auto join_scope = join_session.activate();
        ActivePickerLobbyClientGuard active_guard(join_client.get());
        ASSERT_TRUE(join_save.team_list[0] != nullptr);
        const short base_strength = join_save.team_list[0]->strength;
        og::ui::TrainSession session(join_save);
        ASSERT_FALSE(session.empty());
        session.increase_stat(og::ui::TrainSession::Stat::Strength);
        train_cost = session.current_cost();
        ASSERT_GT(train_cost, 0u);
        ASSERT_LT(train_cost, 5000u);
        ASSERT_TRUE(session.accept());
        trained_strength = join_save.team_list[0]->strength;
        EXPECT_EQ(base_strength + 1, trained_strength);
        EXPECT_EQ(5000u - train_cost, join_save.m_totalcash[1]);
        // The production train-accept tail: roster re-sync (a content change
        // the server answers with a ready-clear), local ready drop, and the
        // [SAVE-F1] merge autosave against the active company slot.
        picker_base_camp_after_roster_mutation();
        EXPECT_EQ(2, join_save.scen_num)
            << "the merge write must never touch the session save";
    }

    // The DISK file: roster + owned wallet overlaid, everything private
    // preserved (the plain-save shape would have rewound scen_num to 2 and
    // adopted the host's settings).
    {
        SaveData reloaded;
        ASSERT_EQ(SaveDataIoError::None,
                  reloaded.load_with_error(kWp4F1JoinSlot));
        EXPECT_EQ(5, reloaded.scen_num)
            << "[SAVE-F1]: the private campaign cursor must survive";
        EXPECT_EQ(5, reloaded.current_levels.at("org.openglad.gladiator"));
        EXPECT_EQ(1, reloaded.respawn_mode)
            << "[SAVE-F1]: private settings must survive";
        EXPECT_EQ(2, reloaded.generator_rate);
        // (my_team is session-only and never serialized — nothing to pin.)
        EXPECT_EQ("F1 PRIVATE BAND", reloaded.save_name);
        ASSERT_TRUE(reloaded.team_list[0] != nullptr);
        EXPECT_EQ(trained_strength, reloaded.team_list[0]->strength)
            << "the trained stat is the roster overlay";
        EXPECT_TRUE(reloaded.team_list[0]->deployed);
        EXPECT_EQ(5000u - train_cost, reloaded.m_totalcash[1])
            << "the owned wallet overlays the training spend";
        EXPECT_NE(0, reloaded.last_played_unix_s)
            << "the autosave stamps last_played";
    }

    // §4.3: the re-synced roster is a content change — the server clears the
    // machine's ready and echoes it to every peer.
    ASSERT_TRUE(wait_until([&] {
        host_client->poll_and_apply();
        bool join_unready = false;
        {
            auto join_scope = join_session.activate();
            join_client->poll_and_apply();
            join_unready = !join_client->local_ready();
        }
        if (!join_unready)
            return false;
        for (const og::sim::LobbyPlayer& player :
             host_client->lobby_players())
        {
            if (player.player_index == join_index)
                return !player.ready;
        }
        return false;
    })) << "the train mutation must clear the joiner's ready on the server";

    host_save.save_name = old_host_company;
    {
        auto join_scope = join_session.activate();
        join_client->shutdown();
    }
    host_client->shutdown();
    cleanup.host_client = nullptr;
    cleanup.join_client = nullptr;
}
