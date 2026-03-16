#include <openglad/platform/local_transport_shadow.h>

#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/platform/game_session.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

short load_saved_game(const char* filename, screen* scr);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

namespace
{

struct LocalTransportClient {
    std::shared_ptr<og::sim::InProcessTransport> transport;
    std::unique_ptr<og::sim::GameClient> game_client;
    bool drives_display = false;

    [[nodiscard]] og::sim::PeerId peer_id() const
    {
        return transport ? transport->local_peer_id() : 0u;
    }
};

struct LocalTransportShadow {
    std::unique_ptr<og::runtime::GameSession> server_session;
    std::shared_ptr<og::sim::InProcessTransport> server_transport;
    std::unique_ptr<og::sim::GameServer> server;
    std::vector<LocalTransportClient> clients;
    std::size_t display_client_index = 0;

    [[nodiscard]] screen* server_screen() const
    {
        return server_session ? server_session->myscreen_ : nullptr;
    }
};

using ShadowMap =
    std::unordered_map<og::runtime::SessionState*,
                       std::shared_ptr<LocalTransportShadow>>;

ShadowMap& local_transport_shadows()
{
    static ShadowMap shadows;
    return shadows;
}

std::mutex& local_transport_shadows_mutex()
{
    static std::mutex mutex;
    return mutex;
}

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
        std::max<short>(gameplay_screen.numviews, 1),
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

        const std::uint32_t entity_id = controlled_entity_ids[index];
        view->control =
            entity_id != 0u ? world->find_by_id(entity_id) : nullptr;
    }
}

void dispatch_display_event_batch(screen& gameplay_screen,
                                  const og::sim::SimEventBatch& batch)
{
    gameplay_screen.dispatch_sim_event_batch(batch);
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

        view->set_display_text(text, 1);
    }
    gameplay_screen.redrawme = 1;
}

} // namespace

namespace og::runtime {

bool local_transport_active(const SessionState& session) noexcept
{
    std::lock_guard lock(local_transport_shadows_mutex());
    return local_transport_shadows().contains(
        const_cast<SessionState*>(&session));
}

void reset_local_transport_shadow(SessionState& session, screen& gameplay_screen)
{
    if (current_game == nullptr || current_game->sim_events == nullptr)
    {
        clear_local_transport_shadow(session);
        return;
    }

    std::shared_ptr<LocalTransportShadow> old_shadow;
    {
        std::lock_guard lock(local_transport_shadows_mutex());
        auto it = local_transport_shadows().find(&session);
        if (it != local_transport_shadows().end())
        {
            old_shadow = std::move(it->second);
            local_transport_shadows().erase(it);
        }
    }

    auto shadow = std::make_shared<LocalTransportShadow>();
    GameSession::Config server_cfg;
    server_cfg.numviews = gameplay_screen.numviews;
    server_cfg.create_display = false;
    server_cfg.install_legacy_globals = false;
    shadow->server_session = std::make_unique<GameSession>(server_cfg);
    shadow->server_transport = og::sim::InProcessTransport::create_server();
    shadow->server_transport->accept_connections();

    {
        auto server_scope = shadow->server_session->activate();
        screen* const server_screen = shadow->server_screen();
        if (server_screen == nullptr ||
            load_saved_game("save0", server_screen) == 0)
        {
            return;
        }
        prepare_server_session_for_gameplay(*shadow->server_session);
        og::sim::apply_snapshot(
            server_screen->world(),
            og::sim::capture_keyframe_snapshot(gameplay_screen.world()));
    }

    screen* const server_screen = shadow->server_screen();
    if (server_screen == nullptr)
        return;

    shadow->server = std::make_unique<og::sim::GameServer>(
        server_screen->world(),
        *shadow->server_session->game_.sim_events,
        *shadow->server_transport);

    const std::size_t player_count = compute_local_player_count(gameplay_screen);
    shadow->clients.reserve(player_count);
    for (std::size_t index = 0; index < player_count; ++index)
    {
        LocalTransportClient client;
        client.transport = shadow->server_transport->create_client_transport();
        client.drives_display = (index == 0);

        const og::sim::PeerId peer_id = client.peer_id();
        shadow->server->connect_client(peer_id);
        shadow->server->bind_player(
            peer_id,
            index,
            server_screen->viewob[index]
                ? server_screen->viewob[index]->my_team
                : server_screen->world().my_team,
            server_screen->viewob[index]
                ? server_screen->viewob[index]->control
                : nullptr);
        client.game_client = std::make_unique<og::sim::GameClient>(
            *client.transport,
            peer_id,
            client.drives_display ? &gameplay_screen.world() : nullptr);
        if (client.drives_display)
        {
            og::sim::GameClient* const display_client = client.game_client.get();
            display_client->set_control_mapping_callback(
                [&gameplay_screen](
                    const std::array<std::uint32_t, MAX_PLAYERS>&
                        controlled_entity_ids,
                    GameWorld* world) {
                    sync_display_controls(
                        gameplay_screen, controlled_entity_ids, world);
                });
            display_client->set_sim_event_batch_callback(
                [&gameplay_screen](const og::sim::SimEventBatch& batch) {
                    dispatch_display_event_batch(gameplay_screen, batch);
                });
            display_client->set_game_flow_event_batch_callback(
                [&gameplay_screen](const og::sim::SimEventBatch& batch) {
                    dispatch_display_event_batch(gameplay_screen, batch);
                });
            display_client->set_exit_prompt_callback(
                [&gameplay_screen, display_client](
                    const og::sim::ExitPromptBroadcastMessage& prompt) {
                    respond_to_exit_prompt(
                        gameplay_screen, *display_client, prompt);
                });
            display_client->set_pause_broadcast_callback(
                [&gameplay_screen](
                    const og::sim::PauseBroadcastMessage& pause) {
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
        }
        shadow->clients.push_back(std::move(client));
    }

    {
        auto server_scope = shadow->server_session->activate();
        shadow->server->send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    }
    for (auto& client : shadow->clients)
        client.game_client->poll_messages();

    {
        std::lock_guard lock(local_transport_shadows_mutex());
        local_transport_shadows()[&session] = shadow;
    }
}

void clear_local_transport_shadow(SessionState& session) noexcept
{
    std::shared_ptr<LocalTransportShadow> owned_shadow;
    {
        std::lock_guard lock(local_transport_shadows_mutex());
        auto it = local_transport_shadows().find(&session);
        if (it == local_transport_shadows().end())
            return;
        owned_shadow = std::move(it->second);
        local_transport_shadows().erase(it);
    }
}

void local_transport_shadow_send_input(SessionState& session,
                                       const InputState& input,
                                       std::uint32_t tick)
{
    std::shared_ptr<LocalTransportShadow> shadow;
    {
        std::lock_guard lock(local_transport_shadows_mutex());
        const auto it = local_transport_shadows().find(&session);
        if (it == local_transport_shadows().end())
            return;
        shadow = it->second;
    }

    const bool spectator =
        session.myscreen_ != nullptr &&
        og::ui::is_spectator_mode(session.myscreen_->save_data);
    for (std::size_t index = 0; index < shadow->clients.size(); ++index)
    {
        InputState player_input{};
        player_input.quit_requested = input.quit_requested;
        if (!spectator && index < static_cast<std::size_t>(MAX_PLAYERS))
            player_input.players[index] = input.players[index];
        shadow->clients[index].game_client->send_input(player_input, tick);
    }
}

void local_transport_shadow_finish_tick(SessionState& session)
{
    std::shared_ptr<LocalTransportShadow> shadow;
    {
        std::lock_guard lock(local_transport_shadows_mutex());
        const auto it = local_transport_shadows().find(&session);
        if (it == local_transport_shadows().end())
            return;
        shadow = it->second;
    }

    if (session.myscreen_ == nullptr || shadow->server == nullptr ||
        shadow->server_session == nullptr)
    {
        return;
    }

    {
        auto server_scope = shadow->server_session->activate();
        ScopedSessionGameplayActivation gameplay_active(*shadow->server_session);
        shadow->server->step();
    }
    for (auto& client : shadow->clients)
        client.game_client->poll_messages();
    if (shadow->display_client_index < shadow->clients.size() &&
        shadow->clients[shadow->display_client_index].game_client)
    {
        render_pause_overlay(
            *session.myscreen_,
            *shadow->clients[shadow->display_client_index].game_client);
    }
}

} // namespace og::runtime
