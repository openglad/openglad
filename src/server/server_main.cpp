#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/net_transport_websocket_server.h>
#include <openglad/resources/campaign_io.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

void io_init(int argc, char* argv[]);
void io_exit();

namespace og::runtime {

static SessionState s_headless_session{};
thread_local SessionState* current_session = &s_headless_session;
std::atomic<SessionState*> primary_session{&s_headless_session};
std::atomic<GameplayContext*> primary_game{&s_headless_session.game_};

} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 12345;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace {

constexpr int kDefaultPort = 12345;
constexpr int kDefaultLobbyPollMs = 10;
constexpr std::array<int, DIFFICULTY_SETTINGS> kDifficultyPercent = {
    50,
    100,
    200,
};
constexpr float kInitialMinHpSentinel = 5'000'000.0f;

std::atomic<bool> g_shutdown_requested = false;

struct ServerArgs {
    std::string host = "0.0.0.0";
    int port = kDefaultPort;
    int lobby_poll_ms = kDefaultLobbyPollMs;
};

void request_shutdown(int)
{
    g_shutdown_requested.store(true, std::memory_order_release);
}

void print_usage()
{
    std::fprintf(
        stderr,
        "Usage: openglad_server [options]\n"
        "  --host <addr>         Listen address (default: 0.0.0.0)\n"
        "  --port <num>          Listen port (default: 12345)\n"
        "  --lobby-poll-ms <n>   Lobby poll interval in ms (default: 10)\n"
        "  --help, -h            Show this help message\n");
}

bool parse_int_arg(const char* text, int& out)
{
    if (text == nullptr || text[0] == '\0')
        return false;

    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == nullptr || *end != '\0')
        return false;
    if (parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max())
    {
        return false;
    }

    out = static_cast<int>(parsed);
    return true;
}

bool parse_args(int argc, char* argv[], ServerArgs& args)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg = argv[index];
        if (arg == "--host")
        {
            if (index + 1 >= argc)
            {
                std::fprintf(stderr, "--host requires a value\n");
                return false;
            }
            args.host = argv[++index];
            continue;
        }

        if (arg == "--port")
        {
            if (index + 1 >= argc ||
                !parse_int_arg(argv[index + 1], args.port) ||
                args.port <= 0)
            {
                std::fprintf(stderr, "--port requires a positive integer\n");
                return false;
            }
            ++index;
            continue;
        }

        if (arg == "--lobby-poll-ms")
        {
            if (index + 1 >= argc ||
                !parse_int_arg(argv[index + 1], args.lobby_poll_ms) ||
                args.lobby_poll_ms < 0)
            {
                std::fprintf(stderr, "--lobby-poll-ms requires a non-negative integer\n");
                return false;
            }
            ++index;
            continue;
        }

        if (arg == "--help" || arg == "-h")
        {
            print_usage();
            return false;
        }

        std::fprintf(stderr, "Unknown argument: %.*s\n",
                     static_cast<int>(arg.size()),
                     arg.data());
        return false;
    }

    return true;
}

int difficulty_percent_from_setting(int difficulty) noexcept
{
    const int normalized =
        ((difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) %
        DIFFICULTY_SETTINGS;
    return kDifficultyPercent[static_cast<std::size_t>(normalized)];
}

std::unique_ptr<guy> make_guy_from_lobby_character(
    const og::sim::LobbyCharacterData& character)
{
    auto result = std::make_unique<guy>(character.family);
    result->id = character.guy_id;
    result->name = character.name;
    result->family = static_cast<char>(character.family);
    result->strength = character.strength;
    result->dexterity = character.dexterity;
    result->constitution = character.constitution;
    result->intelligence = character.intelligence;
    result->armor = character.armor;
    result->exp = character.exp;
    result->kills = character.kills;
    result->level_kills = character.level_kills;
    result->total_damage = character.total_damage;
    result->total_hits = character.total_hits;
    result->total_shots = character.total_shots;
    result->teamnum = character.teamnum;
    result->scen_damage = character.scen_damage;
    result->scen_kills = character.scen_kills;
    result->scen_damage_taken = character.scen_damage_taken;
    result->scen_min_hp = character.scen_min_hp;
    result->scen_shots = character.scen_shots;
    result->scen_hits = character.scen_hits;
    result->level = character.level;
    return result;
}

void update_primary_team_totals(SaveData& save)
{
    const int team_index =
        (save.my_team >= 0 && save.my_team < MAX_PLAYERS) ? save.my_team : 0;
    save.score = save.m_score[team_index];
    save.totalcash = save.m_totalcash[team_index];
    save.totalscore = save.m_totalscore[team_index];
}

void copy_save_data(SaveData& destination, const SaveData& source)
{
    destination.save_name = source.save_name;
    destination.current_campaign = source.current_campaign;
    destination.scen_num = source.scen_num;
    destination.completed_levels = source.completed_levels;
    destination.current_levels = source.current_levels;
    destination.score = source.score;
    std::copy(std::begin(source.m_score), std::end(source.m_score),
              std::begin(destination.m_score));
    destination.totalcash = source.totalcash;
    std::copy(std::begin(source.m_totalcash), std::end(source.m_totalcash),
              std::begin(destination.m_totalcash));
    destination.totalscore = source.totalscore;
    std::copy(std::begin(source.m_totalscore), std::end(source.m_totalscore),
              std::begin(destination.m_totalscore));
    destination.my_team = source.my_team;
    destination.team_size = source.team_size;
    destination.numplayers = source.numplayers;
    destination.allied_mode = source.allied_mode;

    for (std::size_t index = 0; index < destination.team_list.size(); ++index)
    {
        if (source.team_list[index])
            destination.team_list[index] =
                std::make_unique<guy>(*source.team_list[index]);
        else
            destination.team_list[index].reset();
    }
}

void apply_lobby_game_start_config(
    SaveData& save,
    const og::sim::LobbySaveDataEquivalent& config_save)
{
    save.current_campaign = config_save.current_campaign.empty()
        ? std::string("org.openglad.gladiator")
        : config_save.current_campaign;
    save.scen_num = config_save.scen_num > 0 ? config_save.scen_num : 1;
    save.current_levels[save.current_campaign] = save.scen_num;
    save.numplayers = config_save.numplayers;
    save.allied_mode = static_cast<short>(config_save.allied_mode);
    save.my_team = 0;

    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;

    for (const auto& slot : config_save.team_list)
    {
        if (slot.slot_index >= save.team_list.size())
            continue;

        save.team_list[slot.slot_index] =
            make_guy_from_lobby_character(slot.character);
        ++save.team_size;
    }

    update_primary_team_totals(save);
}

void sync_world_from_save_data(GameWorld& world, const SaveData& save)
{
    world.my_team = save.my_team;
    world.allied_mode = save.allied_mode;
    world.current_scenario = save.scen_num;
    for (int index = 0; index < MAX_PLAYERS; ++index)
        world.m_score[index] = save.m_score[index];

    const auto completed_it = save.completed_levels.find(save.current_campaign);
    if (completed_it != save.completed_levels.end())
        world.completed_levels = completed_it->second;
    else
        world.completed_levels.clear();

    world.withdraw_requested = false;
    world.withdraw_level = -1;
}

void sync_save_data_from_world(SaveData& save, const GameWorld& world)
{
    save.my_team = world.my_team;
    save.allied_mode = world.allied_mode;
    save.scen_num = static_cast<short>(world.current_scenario);
    save.current_levels[save.current_campaign] = save.scen_num;
    for (int index = 0; index < MAX_PLAYERS; ++index)
        save.m_score[index] = world.m_score[index];

    save.completed_levels[save.current_campaign] = world.completed_levels;
    update_primary_team_totals(save);
}

walker* find_first_of(GameWorld& world,
                      Order order,
                      unsigned char family,
                      int team_num = -1)
{
    for (const auto& entity : world.oblist)
    {
        walker* const walker_ptr = entity.get();
        if (walker_ptr == nullptr || walker_ptr->dead())
            continue;
        if (walker_ptr->query_order() != order || walker_ptr->family() != family)
            continue;
        if (team_num != -1 && walker_ptr->team_num() != team_num)
            continue;
        return walker_ptr;
    }

    return nullptr;
}

void clear_battle_stats(guy& member)
{
    member.scen_damage = 0.0f;
    member.scen_kills = 0;
    member.scen_damage_taken = 0.0f;
    member.scen_min_hp = kInitialMinHpSentinel;
    member.scen_shots = 0;
    member.scen_hits = 0;
}

walker* create_team_walker(GameWorld& world, const guy& source)
{
    walker* const created =
        world.add_ob(Order::Living, static_cast<std::int32_t>(source.family));
    if (created == nullptr)
        return nullptr;

    created->set_owned_myguy(std::make_unique<guy>(source));
    if (created->stats() != nullptr)
        created->stats()->set_level(created->myguy->level);

    created->myguy->update_derived_stats(created);
    created->set_team_num(static_cast<unsigned char>(created->myguy->teamnum));
    created->set_real_team_num(255);
    return created;
}

void spawn_team_from_save(GameWorld& world, const SaveData& save)
{
    for (const auto& member : save.team_list)
    {
        if (!member)
            continue;

        walker* const created = create_team_walker(world, *member);
        if (created == nullptr || created->myguy == nullptr)
            continue;

        clear_battle_stats(*created->myguy);

        walker* marker = find_first_of(
            world,
            Order::Special,
            static_cast<unsigned char>(FAMILY_RESERVED_TEAM),
            static_cast<int>(member->teamnum));
        if (marker == nullptr)
        {
            marker = find_first_of(
                world,
                Order::Special,
                static_cast<unsigned char>(FAMILY_RESERVED_TEAM));
        }

        if (marker != nullptr)
        {
            created->setxy(marker->xpos(), marker->ypos());
            marker->set_dead(1);
        }
        else
        {
            created->teleport();
        }
    }

    while (walker* marker = find_first_of(
               world,
               Order::Special,
               static_cast<unsigned char>(FAMILY_RESERVED_TEAM)))
    {
        marker->set_dead(1);
    }
}

bool should_preserve_completed_level_entity(const walker& entity)
{
    const Order order = entity.query_order();
    const int family = entity.family();
    if ((entity.team_num() == 0 || entity.myguy != nullptr) &&
        order == Order::Living)
    {
        return true;
    }

    if (order == Order::Treasure &&
        (family == FAMILY_EXIT || family == FAMILY_TELEPORTER))
    {
        return true;
    }

    return false;
}

template <typename EntityList>
void clear_completed_level_entities(EntityList& entities)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr || should_preserve_completed_level_entity(*entity))
            continue;
        entity->set_dead(1);
    }
}

void apply_completed_level_cleanup(GameWorld& world)
{
    clear_completed_level_entities(world.oblist);
    clear_completed_level_entities(world.weaplist);
    clear_completed_level_entities(world.fxlist);
}

std::uint32_t calculate_headless_time_bonus(const GameWorld& world,
                                            const SaveData& save,
                                            int player_index)
{
    if (player_index > 0)
        return 0;

    const std::uint32_t frames = world.level_tick_count();
    const std::uint32_t time_limit =
        world.time_bonus_limit > 0
            ? static_cast<std::uint32_t>(world.time_bonus_limit)
            : 0U;
    if (time_limit == 0 || frames >= time_limit)
        return 0;

    const float multiplier =
        (1.0f + static_cast<float>(world.par_value) / 10.0f) *
        (static_cast<float>(time_limit - frames) /
         static_cast<float>(time_limit));
    return static_cast<std::uint32_t>(
        static_cast<float>(save.m_score[player_index]) * multiplier);
}

void prepare_world_for_gameplay(LevelRuntimeData& level_data,
                                og::sim::SimEventLog& events)
{
    GameWorld& world = level_data.world();
    world.tick_count_ = 0;
    world.reset_level_progress();
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
    events.clear();
}

bool load_level_from_save(LevelRuntimeData& level_data,
                          SaveData& save,
                          int difficulty_setting)
{
    const int current_level =
        load_campaign(save.current_campaign, save.current_levels, save.scen_num);
    if (current_level < 0)
    {
        LogError("headless_server_campaign_load_failed campaign={} error={}\n",
                 save.current_campaign,
                 current_level);
        return false;
    }
    if (current_level != save.scen_num)
    {
        LogError("headless_server_level_mismatch campaign={} save_level={} current_level={}\n",
                 save.current_campaign,
                 save.scen_num,
                 current_level);
    }

    GameWorld& world = level_data.world();
    sync_world_from_save_data(world, save);
    world.id = save.scen_num;
    if (!level_data.load())
    {
        const short requested_level = save.scen_num;
        LogError("headless_server_level_load_failed level={} action=fallback_to_1\n",
                 requested_level);
        save.scen_num = 1;
        save.current_levels[save.current_campaign] = save.scen_num;
        sync_world_from_save_data(world, save);
        world.id = save.scen_num;
        if (!level_data.load())
        {
            LogError("headless_server_level_fallback_failed requested={} fallback=1\n",
                     requested_level);
            return false;
        }
    }

    sync_world_from_save_data(world, save);
    world.difficulty = static_cast<short>(
        difficulty_percent_from_setting(difficulty_setting));
    for (const auto& entity : world.oblist)
    {
        walker* const walker_ptr = entity.get();
        if (walker_ptr != nullptr && walker_ptr->stats() != nullptr)
            walker_ptr->set_difficulty(static_cast<std::uint32_t>(
                walker_ptr->stats()->level()));
    }

    spawn_team_from_save(world, save);
    if (save.is_level_completed(save.scen_num))
        apply_completed_level_cleanup(world);

    prepare_world_for_gameplay(
        level_data,
        *og::runtime::current_session->ctx_.sim_events);
    return true;
}

bool complete_level_and_load_next(LevelRuntimeData& level_data,
                                  SaveData& active_save,
                                  SaveData& checkpoint_save,
                                  int difficulty_setting,
                                  int next_level)
{
    GameWorld& world = level_data.world();
    sync_save_data_from_world(active_save, world);

    for (std::size_t team_index = 0;
         team_index < std::size(active_save.m_score);
         ++team_index)
    {
        active_save.m_totalscore[team_index] += active_save.m_score[team_index];
        active_save.m_totalcash[team_index] += active_save.m_score[team_index] * 2u;
    }

    const bool already_completed = active_save.is_level_completed(active_save.scen_num);
    for (std::size_t team_index = 0;
         team_index < std::size(active_save.m_score);
         ++team_index)
    {
        if (!already_completed)
        {
            active_save.m_totalcash[team_index] += calculate_headless_time_bonus(
                world,
                active_save,
                static_cast<int>(team_index));
        }
        active_save.m_score[team_index] = 0;
    }

    active_save.add_level_completed(
        active_save.current_campaign,
        active_save.scen_num);
    active_save.scen_num = static_cast<short>(next_level);
    active_save.current_levels[active_save.current_campaign] = active_save.scen_num;
    active_save.update_guys(world.oblist);
    update_primary_team_totals(active_save);
    copy_save_data(checkpoint_save, active_save);

    return load_level_from_save(level_data, active_save, difficulty_setting);
}

bool withdraw_and_load_level(LevelRuntimeData& level_data,
                             SaveData& active_save,
                             SaveData& checkpoint_save,
                             int difficulty_setting,
                             int destination_level)
{
    copy_save_data(active_save, checkpoint_save);
    active_save.scen_num = static_cast<short>(destination_level);
    active_save.current_levels[active_save.current_campaign] = active_save.scen_num;
    update_primary_team_totals(active_save);
    copy_save_data(checkpoint_save, active_save);
    return load_level_from_save(level_data, active_save, difficulty_setting);
}

class GameplayActiveScope
{
public:
    explicit GameplayActiveScope(og::runtime::SessionState& session)
        : session_(session)
        , previous_(session.gameplay_active_)
    {
        session_.gameplay_active_ = true;
    }

    ~GameplayActiveScope()
    {
        session_.gameplay_active_ = previous_;
    }

    GameplayActiveScope(const GameplayActiveScope&) = delete;
    GameplayActiveScope& operator=(const GameplayActiveScope&) = delete;

private:
    og::runtime::SessionState& session_;
    bool previous_ = false;
};

} // namespace

int main(int argc, char* argv[])
{
    ServerArgs args;
    if (!parse_args(argc, argv, args))
        return 0;

    std::signal(SIGINT, request_shutdown);
#ifdef SIGTERM
    std::signal(SIGTERM, request_shutdown);
#endif

    bool io_initialized = false;
    try
    {
        io_init(argc, argv);
        io_initialized = true;

        cfg.load_settings();
        init_all_registries();

        og::runtime::SessionState& session = og::runtime::s_headless_session;
        ProductionRandom production_rng;
        session.ctx_.rng = &production_rng;
        session.game_.sim_events = session.ctx_.sim_events.get();
        session.game_.config = &cfg;
        session.game_.session_rng_ref = &session.ctx_.rng;
        session.game_.gameplay_active_ref = &session.gameplay_active_;
        og::runtime::primary_game.store(&session.game_, std::memory_order_release);

        og::sim::WebSocketServerTransport::Options transport_options;
        transport_options.host = args.host;
        og::sim::WebSocketServerTransport transport(args.port, transport_options);
        transport.accept_connections();
        Log("headless_server_listening host={} port={}\n", args.host, args.port);

        og::sim::LobbyServer lobby_server(transport);
        while (!g_shutdown_requested.load(std::memory_order_acquire))
        {
            lobby_server.poll_incoming_messages();
            if (lobby_server.consume_start_game_requested())
                break;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(args.lobby_poll_ms));
        }

        if (g_shutdown_requested.load(std::memory_order_acquire))
        {
            if (io_initialized)
                io_exit();
            return 0;
        }

        const og::sim::LobbySaveDataEquivalent lobby_save =
            lobby_server.build_save_data_equivalent();
        const std::vector<og::sim::LobbyPlayerBinding> player_bindings =
            lobby_server.build_player_bindings();

        SaveData active_save;
        apply_lobby_game_start_config(active_save, lobby_save);
        SaveData checkpoint_save;
        copy_save_data(checkpoint_save, active_save);

        session.current_difficulty_ = static_cast<std::int32_t>(
            lobby_server.state().settings.difficulty);

        LevelRuntimeData level_data(
            active_save.scen_num,
            true,
            &headless_level_data_hooks());
        level_data.set_sim_context(
            &active_save,
            &level_data.world().enemy_freeze,
            session.ctx_.sim_events.get(),
            session.ctx_.rng,
            &cfg);
        session.game_.world = &level_data.world();
        session.game_.save = &active_save;

        GameplayActiveScope gameplay_active(session);
        GameplayContextGuard gameplay_guard(&session.game_);
        if (!load_level_from_save(
                level_data,
                active_save,
                session.current_difficulty_))
        {
            if (io_initialized)
                io_exit();
            return 1;
        }

        og::sim::GameServer game_server(
            level_data.world(),
            *session.ctx_.sim_events,
            transport);
        for (const og::sim::PeerId peer_id : transport.connected_peers())
            game_server.connect_client(peer_id);
        for (const og::sim::LobbyPlayerBinding& binding : player_bindings)
        {
            game_server.bind_player(binding.peer_id,
                                    binding.player_index,
                                    static_cast<short>(binding.team),
                                    nullptr);
        }

        game_server.on_save_sync = [&active_save, &level_data] {
            sync_save_data_from_world(active_save, level_data.world());
        };
        game_server.on_level_transition =
            [&level_data,
             &active_save,
             &checkpoint_save,
             &session](int next_level) {
                return complete_level_and_load_next(
                    level_data,
                    active_save,
                    checkpoint_save,
                    session.current_difficulty_,
                    next_level);
            };
        game_server.on_exit_accepted =
            [&level_data,
             &active_save,
             &checkpoint_save,
             &session](int destination) {
                return complete_level_and_load_next(
                    level_data,
                    active_save,
                    checkpoint_save,
                    session.current_difficulty_,
                    destination);
            };
        game_server.on_withdraw_accepted =
            [&level_data,
             &active_save,
             &checkpoint_save,
             &session](int destination) {
                return withdraw_and_load_level(
                    level_data,
                    active_save,
                    checkpoint_save,
                    session.current_difficulty_,
                    destination);
            };

        while (!g_shutdown_requested.load(std::memory_order_acquire))
        {
            const auto tick_start = std::chrono::steady_clock::now();
            game_server.step();

            const int wait_ticks =
                std::max<int>(level_data.world().timer_wait, 0);
            if (wait_ticks == 0)
            {
                std::this_thread::yield();
                continue;
            }

            const auto tick_deadline =
                tick_start +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double, std::milli>(
                        static_cast<double>(wait_ticks) *
                        og::sim::TIMER_WAIT_TO_MS));
            std::this_thread::sleep_until(tick_deadline);
        }

        if (io_initialized)
            io_exit();
        return 0;
    }
    catch (const std::exception& ex)
    {
        LogError("headless_server_fatal {}\n", ex.what());
    }
    catch (...)
    {
        LogError("headless_server_fatal unknown_exception\n");
    }

    if (io_initialized)
        io_exit();
    return 1;
}
