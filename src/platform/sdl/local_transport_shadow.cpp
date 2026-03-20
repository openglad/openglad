#include <openglad/platform/local_transport_shadow.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/platform/game_session.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

short load_saved_game(const char* filename, screen* scr);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Uint32 get_time_bonus(int playernum);

namespace
{

struct LocalTransportClient {
    std::shared_ptr<og::sim::ITransport> transport;
    og::sim::PeerId server_peer_id = 0;
    std::unique_ptr<og::sim::GameClient> game_client;
    bool drives_display = false;
};

} // namespace

namespace og::runtime {

struct LocalTransportRuntime {
    enum class Mode : std::uint8_t {
        Authoritative,
        ClientOnly,
    };

    Mode mode = Mode::Authoritative;
    std::unique_ptr<og::runtime::GameSession> server_session;
    std::shared_ptr<og::sim::ITransport> server_transport;
    std::unique_ptr<og::sim::GameServer> server;
    std::vector<LocalTransportClient> clients;
    std::size_t display_client_index = 0;
    bool display_session_finished = false;

    [[nodiscard]] screen* server_screen() const
    {
        return server_session ? server_session->myscreen_ : nullptr;
    }

    [[nodiscard]] og::sim::GameClient* display_client() const
    {
        if (display_client_index >= clients.size())
            return nullptr;
        return clients[display_client_index].game_client.get();
    }

    [[nodiscard]] bool authoritative_mode() const noexcept
    {
        return mode == Mode::Authoritative && server_session != nullptr &&
            server != nullptr && server_transport != nullptr;
    }
};

} // namespace og::runtime

namespace
{
class ScopedSessionGameplayActivation
{
public:
    explicit ScopedSessionGameplayActivation(og::runtime::SessionState& session)
        : session_(session)
        , previous_(session.gameplay_active_)
    {
        session_.gameplay_active_ = true;
    }

    ~ScopedSessionGameplayActivation()
    {
        session_.gameplay_active_ = previous_;
    }

    ScopedSessionGameplayActivation(const ScopedSessionGameplayActivation&) = delete;
    ScopedSessionGameplayActivation& operator=(
        const ScopedSessionGameplayActivation&) = delete;

private:
    og::runtime::SessionState& session_;
    bool previous_ = false;
};

std::size_t compute_local_player_count(const screen& gameplay_screen)
{
    return std::min<std::size_t>(
        std::max<short>(
            gameplay_screen.save_data.numplayers > 0
                ? gameplay_screen.save_data.numplayers
                : gameplay_screen.numviews,
            1),
        static_cast<std::size_t>(MAX_PLAYERS));
}

void prepare_server_session_for_gameplay(og::runtime::GameSession& server_session)
{
    screen* const server_screen = server_session.myscreen_;
    if (server_screen == nullptr)
        return;

    server_screen->world().tick_count_ = 0;
    server_screen->world().reset_level_progress();
    server_screen->world().clear_removed_entity_ids();
    server_screen->world().clear_grid_dirty_tiles();
    if (current_game != nullptr && current_game->sim_events != nullptr)
        current_game->sim_events->clear();
}

void apply_palette_id(screen& gameplay_screen, std::uint8_t palette_id)
{
    if (palette_id == 0u)
    {
        gameplay_screen.world().current_palette_id = 0;
        set_palette(gameplay_screen.ourpalette);
    }
    else
    {
        gameplay_screen.world().current_palette_id = 1;
        set_palette(gameplay_screen.bluepalette);
    }
    gameplay_screen.redrawme = 1;
}

void release_screen_control_claims(screen& gameplay_screen)
{
    for (auto& uptr : gameplay_screen.world().oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->user() == -1)
            continue;

        entity->set_user(-1);
        entity->restore_act_type();
    }

    for (auto& view : gameplay_screen.viewob)
    {
        if (view == nullptr)
            continue;
        view->control = nullptr;
    }
    gameplay_screen.world().control_hp = 0.0f;
}

bool save_shadow_save_data(screen& gameplay_screen, const char* action)
{
    const SaveDataIoError save_error =
        gameplay_screen.save_data.save_with_error("save0");
    if (save_error == SaveDataIoError::None)
        return true;

    LogError("local_transport_shadow_save_failed action={} error={}\n",
             action,
             static_cast<int>(save_error));
    return false;
}

bool load_shadow_level_from_save(screen& gameplay_screen, const char* action)
{
    if (load_saved_game("", &gameplay_screen) == 0)
    {
        LogError("local_transport_shadow_level_load_failed action={} level={}\n",
                 action,
                 gameplay_screen.save_data.scen_num);
        return false;
    }

    release_screen_control_claims(gameplay_screen);
    return true;
}

bool complete_level_and_load_next(screen& gameplay_screen, int next_level)
{
    gameplay_screen.sync_save_data_from_world();

    for (std::size_t team_index = 0;
         team_index < std::size(gameplay_screen.save_data.m_score);
         ++team_index)
    {
        gameplay_screen.save_data.m_totalscore[team_index] +=
            gameplay_screen.save_data.m_score[team_index];
        gameplay_screen.save_data.m_totalcash[team_index] +=
            gameplay_screen.save_data.m_score[team_index] * 2u;
    }

    const bool already_completed =
        gameplay_screen.save_data.is_level_completed(
            gameplay_screen.save_data.scen_num);
    for (std::size_t team_index = 0;
         team_index < std::size(gameplay_screen.save_data.m_score);
         ++team_index)
    {
        if (!already_completed)
        {
            gameplay_screen.save_data.m_totalcash[team_index] +=
                get_time_bonus(static_cast<int>(team_index));
        }
        gameplay_screen.save_data.m_score[team_index] = 0;
    }

    gameplay_screen.save_data.add_level_completed(
        gameplay_screen.save_data.current_campaign,
        gameplay_screen.save_data.scen_num);
    gameplay_screen.save_data.scen_num = static_cast<short>(next_level);
    gameplay_screen.save_data.update_guys(gameplay_screen.world().oblist);
    if (!save_shadow_save_data(gameplay_screen, "complete_level"))
        return false;

    return load_shadow_level_from_save(gameplay_screen, "complete_level");
}

bool withdraw_and_load_level(screen& gameplay_screen, int destination_level)
{
    const SaveDataIoError load_error =
        gameplay_screen.save_data.load_with_error("save0");
    if (load_error != SaveDataIoError::None)
    {
        LogError("local_transport_shadow_withdraw_load_failed level={} error={}\n",
                 destination_level,
                 static_cast<int>(load_error));
        return false;
    }

    gameplay_screen.save_data.scen_num = static_cast<short>(destination_level);
    if (!save_shadow_save_data(gameplay_screen, "withdraw"))
        return false;

    return load_shadow_level_from_save(gameplay_screen, "withdraw");
}

void apply_initial_setup_to_client_save(
    screen& gameplay_screen,
    const og::sim::InitialSetupMessage& message)
{
    gameplay_screen.save_data.scen_num = static_cast<short>(message.level_id);
    gameplay_screen.save_data.my_team = static_cast<short>(message.my_team);
    gameplay_screen.save_data.allied_mode =
        static_cast<short>(message.allied_mode);

    std::set<int>& completed =
        gameplay_screen.save_data
            .completed_levels[gameplay_screen.save_data.current_campaign];
    completed.clear();
    for (const std::int32_t level_id : message.completed_levels)
        completed.insert(level_id);
}

bool prepare_display_level_for_initial_setup(
    screen& gameplay_screen,
    const og::sim::InitialSetupMessage& message)
{
    gameplay_screen.cleanup(gameplay_screen.numviews);
    gameplay_screen.initialize_views();
    apply_initial_setup_to_client_save(gameplay_screen, message);
    gameplay_screen.sync_world_from_save_data();
    gameplay_screen.world().id = static_cast<short>(message.level_id);
    if (!gameplay_screen.load_level())
    {
        LogError("local_transport_shadow_client_level_load_failed level={}\n",
                 message.level_id);
        return false;
    }

    gameplay_screen.sync_world_from_save_data();
    gameplay_screen.world().tick_count_ = 0;
    gameplay_screen.world().reset_level_progress();
    gameplay_screen.world().clear_removed_entity_ids();
    gameplay_screen.world().clear_grid_dirty_tiles();
    gameplay_screen.redrawme = 1;
    return true;
}

walker* resolve_control_from_entity_id(GameWorld& world, std::uint32_t entity_id)
{
    return entity_id != 0u ? world.find_by_id(entity_id) : nullptr;
}

void dispatch_display_event_batch(screen& gameplay_screen,
                                  const og::sim::SimEventBatch& batch)
{
    gameplay_screen.dispatch_sim_event_batch(batch);
}

bool batch_ends_display_session(const og::sim::SimEventBatch& batch)
{
    return std::any_of(
        batch.events.begin(),
        batch.events.end(),
        [](const og::sim::Event& event) {
            return event.kind == og::sim::EventKind::EndGame ||
                event.kind == og::sim::EventKind::SetEnd;
        });
}

void respond_to_exit_prompt(screen& gameplay_screen,
                            og::sim::GameClient& game_client,
                            const og::sim::ExitPromptBroadcastMessage& prompt)
{
    const bool accepted =
        yes_or_no_prompt("Exit Field", prompt.prompt_text.c_str(), false);
    gameplay_screen.redrawme = 1;
    game_client.send_exit_prompt_response(accepted);
}

std::string pause_overlay_text(const og::sim::PauseBroadcastMessage* pause)
{
    if (pause == nullptr || pause->player_name.empty())
        return "PAUSED";

    return "PAUSED by " + pause->player_name;
}

void render_pause_overlay(screen& gameplay_screen,
                          const og::sim::GameClient& game_client)
{
    if (!game_client.baseline().has_value() ||
        !game_client.baseline()->paused)
    {
        return;
    }

    const std::string text =
        pause_overlay_text(game_client.last_pause_broadcast().has_value()
                               ? &*game_client.last_pause_broadcast()
                               : nullptr);
    for (int index = 0; index < gameplay_screen.numviews; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        if (view == nullptr)
            continue;

        view->refresh_display_text(text, 1);
    }
    gameplay_screen.redrawme = 1;
}

void configure_background_game_client(og::sim::GameClient& game_client)
{
    game_client.set_initial_setup_callback(
        [&game_client](
            const og::sim::InitialSetupMessage&,
            bool is_level_transition) {
            if (is_level_transition)
                game_client.send_client_ready();
        });
}

walker* find_control_for_view(viewscreen* view,
                              std::size_t fallback_index,
                              const std::array<std::uint32_t, MAX_PLAYERS>&
                                  controlled_entity_ids,
                              GameWorld* world)
{
    if (view == nullptr || world == nullptr)
        return nullptr;

    for (const std::uint32_t entity_id : controlled_entity_ids)
    {
        walker* const entity = resolve_control_from_entity_id(*world, entity_id);
        if (entity != nullptr && entity->team_num() == view->my_team)
            return entity;
    }

    if (fallback_index < controlled_entity_ids.size())
    {
        return resolve_control_from_entity_id(
            *world,
            controlled_entity_ids[fallback_index]);
    }
    return nullptr;
}

void sync_display_controls(screen& gameplay_screen,
                           const std::array<std::uint32_t, MAX_PLAYERS>&
                               controlled_entity_ids,
                           GameWorld* world)
{
    if (world == nullptr)
        return;

    const std::size_t player_count = compute_local_player_count(gameplay_screen);
    for (std::size_t index = 0; index < player_count; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        if (view == nullptr)
            continue;

        view->control =
            find_control_for_view(view, index, controlled_entity_ids, world);
    }
}

void configure_display_game_client(og::runtime::LocalTransportRuntime& runtime,
                                   screen& gameplay_screen,
                                   og::sim::GameClient& display_client)
{
    gameplay_screen.set_render_interpolation_client(&display_client);
    display_client.set_initial_setup_callback(
        [&gameplay_screen, display_client_ptr = &display_client](
            const og::sim::InitialSetupMessage& message,
            bool is_level_transition) {
            if (!is_level_transition)
                return;

            if (!prepare_display_level_for_initial_setup(
                    gameplay_screen, message))
            {
                return;
            }

            display_client_ptr->send_client_ready();
        });
    display_client.set_control_mapping_callback(
        [&gameplay_screen](
            const std::array<std::uint32_t, MAX_PLAYERS>& controlled_entity_ids,
            GameWorld* world) {
            sync_display_controls(
                gameplay_screen, controlled_entity_ids, world);
        });
    display_client.set_sim_event_batch_callback(
        [&gameplay_screen](const og::sim::SimEventBatch& batch) {
            dispatch_display_event_batch(gameplay_screen, batch);
        });
    display_client.set_game_flow_event_batch_callback(
        [&gameplay_screen, runtime_ptr = &runtime](
            const og::sim::SimEventBatch& batch) {
            dispatch_display_event_batch(gameplay_screen, batch);
            if (runtime_ptr != nullptr &&
                (gameplay_screen.world().end != 0 ||
                 batch_ends_display_session(batch)))
            {
                runtime_ptr->display_session_finished = true;
            }
        });
    display_client.set_message_processing_break_callback(
        [&gameplay_screen]() {
            return gameplay_screen.world().end != 0;
        });
    display_client.set_exit_prompt_callback(
        [&gameplay_screen, display_client_ptr = &display_client](
            const og::sim::ExitPromptBroadcastMessage& prompt) {
            respond_to_exit_prompt(
                gameplay_screen, *display_client_ptr, prompt);
        });
    display_client.set_pause_broadcast_callback(
        [&gameplay_screen](const og::sim::PauseBroadcastMessage& pause) {
            const std::string text = pause_overlay_text(&pause);
            for (int view_index = 0; view_index < gameplay_screen.numviews;
                 ++view_index)
            {
                viewscreen* const view =
                    gameplay_screen.viewob[view_index].get();
                if (view == nullptr)
                    continue;
                view->set_display_text(text, 1);
            }
            gameplay_screen.redrawme = 1;
        });
    display_client.set_palette_sync_callback(
        [&gameplay_screen](std::uint8_t palette_id) {
            apply_palette_id(gameplay_screen, palette_id);
        });
}

} // namespace

namespace og::runtime {

bool local_transport_active(const SessionState& session) noexcept
{
    return session.has_local_transport_runtime();
}

std::size_t local_transport_client_count(const GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    return runtime != nullptr ? runtime->clients.size() : 0u;
}

bool local_transport_shadow_is_paused(const GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    const og::sim::GameClient* const display_client =
        runtime != nullptr ? runtime->display_client() : nullptr;
    return display_client != nullptr &&
        display_client->baseline().has_value() &&
        display_client->baseline()->paused;
}

bool local_transport_shadow_toggle_pause(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    og::sim::GameClient* const display_client =
        runtime != nullptr ? runtime->display_client() : nullptr;
    if (display_client == nullptr)
        return false;

    if (display_client->baseline().has_value() &&
        display_client->baseline()->paused)
    {
        display_client->send_pause_response();
    }
    else
    {
        display_client->send_pause_request();
    }

    if (session.myscreen_ != nullptr)
        session.myscreen_->redrawme = 1;
    return true;
}

void reset_local_transport_shadow(GameSession& session, screen& gameplay_screen)
{
    if (current_game == nullptr || current_game->sim_events == nullptr)
    {
        clear_local_transport_shadow(session);
        return;
    }

    gameplay_screen.set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();
    session.relay_transport_active_ = false;
    session.relay_speed_warning_shown_ = false;

    auto runtime = std::make_shared<LocalTransportRuntime>();
    const std::size_t player_count = compute_local_player_count(gameplay_screen);
    std::array<std::uint32_t, MAX_PLAYERS> control_entity_ids = {};
    std::array<short, MAX_PLAYERS> player_teams = {};
    for (std::size_t index = 0; index < player_count; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        player_teams[index] =
            view != nullptr ? view->my_team : gameplay_screen.world().my_team;
        control_entity_ids[index] =
            view != nullptr && view->control != nullptr
                ? view->control->entity_id()
                : 0u;
    }

    GameSession::Config server_cfg;
    server_cfg.numviews = gameplay_screen.numviews;
    server_cfg.create_display = false;
    server_cfg.install_legacy_globals = false;
    runtime->server_session = std::make_unique<GameSession>(server_cfg);
    runtime->server_transport = og::sim::InProcessTransport::create_server();
    runtime->server_transport->accept_connections();
    const auto inprocess_server_transport =
        std::dynamic_pointer_cast<og::sim::InProcessTransport>(
            runtime->server_transport);
    if (!inprocess_server_transport)
        return;

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        screen* const server_screen = runtime->server_screen();
        if (server_screen == nullptr ||
            load_saved_game("save0", server_screen) == 0)
        {
            return;
        }
        prepare_server_session_for_gameplay(*runtime->server_session);
        og::sim::apply_snapshot(
            server_screen->world(),
            og::sim::capture_keyframe_snapshot(gameplay_screen.world()));
        for (std::size_t index = 0; index < player_count; ++index)
        {
            if (server_screen->viewob[index] == nullptr)
                continue;
            server_screen->viewob[index]->my_team = player_teams[index];
            server_screen->viewob[index]->control = resolve_control_from_entity_id(
                server_screen->world(), control_entity_ids[index]);
        }
    }

    screen* const server_screen = runtime->server_screen();
    if (server_screen == nullptr)
        return;

    runtime->server = std::make_unique<og::sim::GameServer>(
        server_screen->world(),
        *runtime->server_session->game_.sim_events,
        *runtime->server_transport);
    runtime->server->on_save_sync = [server_screen] {
        server_screen->sync_save_data_from_world();
    };
    runtime->server->on_level_transition =
        [server_screen,
         display_screen = &gameplay_screen,
         server_session = runtime->server_session.get()](
            int level_id) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            if (!complete_level_and_load_next(*server_screen, level_id))
                return false;

            prepare_server_session_for_gameplay(*server_session);
            return true;
        };
    runtime->server->on_exit_accepted =
        [server_screen,
         display_screen = &gameplay_screen,
         server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            if (!complete_level_and_load_next(*server_screen, destination))
                return false;

            prepare_server_session_for_gameplay(*server_session);
            return true;
        };
    runtime->server->on_withdraw_accepted =
        [server_screen, server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            if (!withdraw_and_load_level(*server_screen, destination))
                return false;

            prepare_server_session_for_gameplay(*server_session);
            return true;
        };

    runtime->clients.reserve(player_count);
    for (std::size_t index = 0; index < player_count; ++index)
    {
        LocalTransportClient client;
        client.transport = inprocess_server_transport->create_client_transport();
        client.server_peer_id =
            std::dynamic_pointer_cast<og::sim::InProcessTransport>(
                client.transport)
                ->local_peer_id();
        client.drives_display = (index == 0);

        const og::sim::PeerId peer_id = client.server_peer_id;
        runtime->server->connect_client(peer_id);
        walker* const initial_control = resolve_control_from_entity_id(
            server_screen->world(), control_entity_ids[index]);
        runtime->server->bind_player(
            peer_id,
            index,
            player_teams[index],
            initial_control);
        client.game_client = std::make_unique<og::sim::GameClient>(
            *client.transport,
            peer_id,
            client.drives_display ? &gameplay_screen.world() : nullptr);
        og::sim::GameClient* const local_client = client.game_client.get();
        configure_background_game_client(*local_client);
        if (client.drives_display)
            configure_display_game_client(*runtime, gameplay_screen, *local_client);
        runtime->clients.push_back(std::move(client));
    }

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        runtime->server->send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    }
    {
        auto client_scope = session.activate();
        GameplayContextGuard client_gameplay_scope(&session.game_);
        for (auto& client : runtime->clients)
            client.game_client->poll_messages();
    }

    session.local_transport_runtime_ = std::move(runtime);
}

void reset_network_host_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> server_transport,
    std::shared_ptr<og::sim::InProcessTransport> local_client_transport,
    const std::vector<og::sim::LobbyPlayerBinding>& player_bindings)
{
    if (current_game == nullptr || current_game->sim_events == nullptr ||
        !server_transport || !local_client_transport)
    {
        clear_local_transport_shadow(session);
        return;
    }

    gameplay_screen.set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();

    auto runtime = std::make_shared<LocalTransportRuntime>();
    runtime->mode = LocalTransportRuntime::Mode::Authoritative;
    runtime->server_transport = std::move(server_transport);
    runtime->server_transport->accept_connections();

    GameSession::Config server_cfg;
    server_cfg.numviews = gameplay_screen.numviews;
    server_cfg.create_display = false;
    server_cfg.install_legacy_globals = false;
    runtime->server_session = std::make_unique<GameSession>(server_cfg);

    const short display_team =
        gameplay_screen.viewob[0] != nullptr
            ? gameplay_screen.viewob[0]->my_team
            : gameplay_screen.world().my_team;
    const std::uint32_t display_control_entity_id =
        gameplay_screen.viewob[0] != nullptr &&
            gameplay_screen.viewob[0]->control != nullptr
        ? gameplay_screen.viewob[0]->control->entity_id()
        : 0u;

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        screen* const server_screen = runtime->server_screen();
        if (server_screen == nullptr ||
            load_saved_game("save0", server_screen) == 0)
        {
            return;
        }

        prepare_server_session_for_gameplay(*runtime->server_session);
        og::sim::apply_snapshot(
            server_screen->world(),
            og::sim::capture_keyframe_snapshot(gameplay_screen.world()));
        if (server_screen->viewob[0] != nullptr)
        {
            server_screen->viewob[0]->my_team = display_team;
            server_screen->viewob[0]->control = resolve_control_from_entity_id(
                server_screen->world(), display_control_entity_id);
        }
    }

    screen* const server_screen = runtime->server_screen();
    if (server_screen == nullptr)
        return;

    runtime->server = std::make_unique<og::sim::GameServer>(
        server_screen->world(),
        *runtime->server_session->game_.sim_events,
        *runtime->server_transport);
    runtime->server->on_save_sync = [server_screen] {
        server_screen->sync_save_data_from_world();
    };
    runtime->server->on_level_transition =
        [server_screen,
         display_screen = &gameplay_screen,
         server_session = runtime->server_session.get()](
            int level_id) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            if (!complete_level_and_load_next(*server_screen, level_id))
                return false;

            prepare_server_session_for_gameplay(*server_session);
            return true;
        };
    runtime->server->on_exit_accepted =
        [server_screen,
         display_screen = &gameplay_screen,
         server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            if (!complete_level_and_load_next(*server_screen, destination))
                return false;

            prepare_server_session_for_gameplay(*server_session);
            return true;
        };
    runtime->server->on_withdraw_accepted =
        [server_screen, server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            if (!withdraw_and_load_level(*server_screen, destination))
                return false;

            prepare_server_session_for_gameplay(*server_session);
            return true;
        };

    for (const og::sim::LobbyPlayerBinding& binding : player_bindings)
    {
        runtime->server->connect_client(binding.peer_id);
        runtime->server->bind_player(
            binding.peer_id,
            binding.player_index,
            binding.team,
            nullptr);
    }

    LocalTransportClient client;
    client.transport = local_client_transport;
    client.server_peer_id = local_client_transport->local_peer_id();
    client.drives_display = true;
    client.game_client = std::make_unique<og::sim::GameClient>(
        *client.transport,
        client.server_peer_id,
        &gameplay_screen.world());
    configure_background_game_client(*client.game_client);
    configure_display_game_client(*runtime, gameplay_screen, *client.game_client);
    runtime->clients.push_back(std::move(client));

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        runtime->server->send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    }
    {
        auto client_scope = session.activate();
        GameplayContextGuard client_gameplay_scope(&session.game_);
        for (auto& local_client : runtime->clients)
            local_client.game_client->poll_messages();
    }

    session.local_transport_runtime_ = std::move(runtime);
}

void reset_network_client_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> client_transport,
    og::sim::PeerId server_peer_id)
{
    if (!client_transport || server_peer_id == 0)
    {
        clear_local_transport_shadow(session);
        return;
    }

    gameplay_screen.set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();

    auto runtime = std::make_shared<LocalTransportRuntime>();
    runtime->mode = LocalTransportRuntime::Mode::ClientOnly;

    LocalTransportClient client;
    client.transport = std::move(client_transport);
    client.server_peer_id = server_peer_id;
    client.drives_display = true;
    client.game_client = std::make_unique<og::sim::GameClient>(
        *client.transport,
        client.server_peer_id,
        &gameplay_screen.world());
    configure_background_game_client(*client.game_client);
    configure_display_game_client(*runtime, gameplay_screen, *client.game_client);
    runtime->clients.push_back(std::move(client));

    {
        auto client_scope = session.activate();
        GameplayContextGuard client_gameplay_scope(&session.game_);
        runtime->clients.front().game_client->poll_messages();
    }

    session.local_transport_runtime_ = std::move(runtime);
}

void clear_local_transport_shadow(GameSession& session) noexcept
{
    if (session.myscreen_ != nullptr)
        session.myscreen_->set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();
    session.relay_transport_active_ = false;
    session.relay_speed_warning_shown_ = false;
}

void local_transport_shadow_send_input(GameSession& session,
                                       const InputState& input,
                                       std::uint32_t tick)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr)
        return;

    const bool spectator =
        session.myscreen_ != nullptr &&
        og::ui::is_spectator_mode(session.myscreen_->save_data);
    for (std::size_t index = 0; index < runtime->clients.size(); ++index)
    {
        InputState player_input{};
        player_input.quit_requested = input.quit_requested;
        player_input.timer_wait_request =
            index == runtime->display_client_index
                ? input.timer_wait_request
                : kNoTimerWaitRequest;
        if (!spectator && index < static_cast<std::size_t>(MAX_PLAYERS))
            player_input.players[index] = input.players[index];
        runtime->clients[index].game_client->send_input(player_input, tick);
    }
}

void local_transport_shadow_finish_tick(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr)
        return;

    if (session.myscreen_ == nullptr)
        return;

    if (runtime->display_session_finished || session.myscreen_->world().end != 0)
    {
        runtime->display_session_finished = true;
        session.myscreen_->world().end = 1;
        return;
    }

    if (runtime->authoritative_mode())
    {
        // Single-thread authoritative order:
        //  3-9. Install the server session/context and run one authoritative step.
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        ScopedSessionGameplayActivation gameplay_active(*runtime->server_session);
        runtime->server->step();
    }

    // 10-14. Restore the display session, then drain snapshots/events into the
    // client mirror before rendering.
    {
        auto client_scope = session.activate();
        GameplayContextGuard client_gameplay_scope(&session.game_);
        for (auto& client : runtime->clients)
        {
            client.game_client->poll_messages();
            if (runtime->display_session_finished ||
                session.myscreen_->world().end != 0)
            {
                runtime->display_session_finished = true;
                session.myscreen_->world().end = 1;
                return;
            }
        }
        if (runtime->display_client_index < runtime->clients.size() &&
            runtime->clients[runtime->display_client_index].game_client)
        {
            render_pause_overlay(
                *session.myscreen_,
                *runtime->clients[runtime->display_client_index].game_client);
        }
    }
}

} // namespace og::runtime
