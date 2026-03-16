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
#include <unordered_map>
#include <vector>

short load_saved_game(const char* filename, screen* scr);

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
    std::unordered_map<og::runtime::SessionState*, std::unique_ptr<LocalTransportShadow>>;

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

void sync_display_controls(screen& gameplay_screen, LocalTransportShadow& shadow)
{
    const std::size_t player_count = compute_local_player_count(gameplay_screen);
    for (std::size_t index = 0; index < player_count; ++index)
    {
        if (!gameplay_screen.viewob[index])
            continue;

        walker* server_control = shadow.server->player_control(index);
        gameplay_screen.viewob[index]->control =
            (server_control != nullptr)
                ? gameplay_screen.world().find_by_id(server_control->entity_id())
                : nullptr;
    }
}

og::sim::SimEventBatch collect_display_event_batch(
    const LocalTransportShadow& shadow)
{
    og::sim::SimEventBatch combined;
    const screen* const server_screen = shadow.server_screen();
    if (server_screen != nullptr)
        combined.sequence = server_screen->world().tick_count_;

    if (shadow.clients.empty())
        return combined;

    const LocalTransportClient& display_client =
        shadow.clients.at(shadow.display_client_index);
    if (!display_client.game_client)
        return combined;

    for (const auto& message : display_client.game_client->last_polled_messages())
    {
        if ((message.kind != og::sim::TypedReceivedMessageKind::SimEventBatch &&
             message.kind !=
                 og::sim::TypedReceivedMessageKind::GameFlowEventBatch) ||
            !message.event_batch)
        {
            continue;
        }

        combined.events.insert(combined.events.end(),
                               message.event_batch->events.begin(),
                               message.event_batch->events.end());
    }

    return combined;
}

void mirror_client_prompt_state(LocalTransportShadow& shadow,
                                const screen& gameplay_screen)
{
    screen* const server_screen = shadow.server_screen();
    if (server_screen == nullptr)
        return;

    server_screen->world().withdraw_requested =
        gameplay_screen.world().withdraw_requested;
    server_screen->world().withdraw_level =
        gameplay_screen.world().withdraw_level;
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

    std::unique_ptr<LocalTransportShadow> old_shadow;
    {
        std::lock_guard lock(local_transport_shadows_mutex());
        auto it = local_transport_shadows().find(&session);
        if (it != local_transport_shadows().end())
        {
            old_shadow = std::move(it->second);
            local_transport_shadows().erase(it);
        }
    }

    auto shadow = std::make_unique<LocalTransportShadow>();
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
        shadow->clients.push_back(std::move(client));
    }

    {
        auto server_scope = shadow->server_session->activate();
        shadow->server->send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    }
    for (auto& client : shadow->clients)
        client.game_client->poll_messages();
    sync_display_controls(gameplay_screen, *shadow);

    {
        std::lock_guard lock(local_transport_shadows_mutex());
        local_transport_shadows()[&session] = std::move(shadow);
    }
}

void clear_local_transport_shadow(SessionState& session) noexcept
{
    std::unique_ptr<LocalTransportShadow> owned_shadow;
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
    std::lock_guard lock(local_transport_shadows_mutex());
    const auto it = local_transport_shadows().find(&session);
    if (it == local_transport_shadows().end())
        return;

    const bool spectator =
        session.myscreen_ != nullptr &&
        og::ui::is_spectator_mode(session.myscreen_->save_data);
    for (std::size_t index = 0; index < it->second->clients.size(); ++index)
    {
        InputState player_input{};
        player_input.quit_requested = input.quit_requested;
        if (!spectator && index < static_cast<std::size_t>(MAX_PLAYERS))
            player_input.players[index] = input.players[index];
        it->second->clients[index].game_client->send_input(player_input, tick);
    }
}

void local_transport_shadow_finish_tick(SessionState& session)
{
    std::lock_guard lock(local_transport_shadows_mutex());
    const auto it = local_transport_shadows().find(&session);
    if (it == local_transport_shadows().end())
        return;

    if (session.myscreen_ == nullptr || it->second->server == nullptr ||
        it->second->server_session == nullptr)
    {
        return;
    }

    {
        auto server_scope = it->second->server_session->activate();
        ScopedSessionGameplayActivation gameplay_active(*it->second->server_session);
        it->second->server->step();
    }
    for (auto& client : it->second->clients)
        client.game_client->poll_messages();
    sync_display_controls(*session.myscreen_, *it->second);

    const og::sim::SimEventBatch batch = collect_display_event_batch(*it->second);
    session.myscreen_->dispatch_sim_event_batch(batch);
    mirror_client_prompt_state(*it->second, *session.myscreen_);
}

} // namespace og::runtime
