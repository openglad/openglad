#include <openglad/platform/local_transport_shadow.h>

#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{

struct LocalTransportShadow {
    std::shared_ptr<og::sim::InProcessTransport> server_transport;
    std::shared_ptr<og::sim::InProcessTransport> client_transport;
    std::unique_ptr<og::sim::GameServer> server;
    std::unique_ptr<og::sim::GameClient> client;
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

} // namespace

namespace og::runtime {

void reset_local_transport_shadow(SessionState& session, screen& gameplay_screen)
{
    std::lock_guard lock(local_transport_shadows_mutex());

    if (current_game == nullptr || current_game->sim_events == nullptr)
    {
        local_transport_shadows().erase(&session);
        return;
    }

    auto shadow = std::make_unique<LocalTransportShadow>();
    shadow->server_transport = og::sim::InProcessTransport::create_server();
    shadow->server_transport->accept_connections();
    shadow->client_transport = shadow->server_transport->create_client_transport();

    const og::sim::PeerId peer_id = shadow->client_transport->local_peer_id();
    shadow->server = std::make_unique<og::sim::GameServer>(
        gameplay_screen.world(),
        *current_game->sim_events,
        *shadow->server_transport);
    shadow->server->connect_client(peer_id);
    shadow->client = std::make_unique<og::sim::GameClient>(
        *shadow->client_transport,
        peer_id);

    shadow->server->send_initial_snapshot(peer_id,
                                          og::sim::SnapshotCaptureMode::Peek);
    shadow->client->poll_messages();

    local_transport_shadows()[&session] = std::move(shadow);
}

void clear_local_transport_shadow(SessionState& session) noexcept
{
    std::lock_guard lock(local_transport_shadows_mutex());
    local_transport_shadows().erase(&session);
}

void local_transport_shadow_send_input(SessionState& session,
                                       const InputState& input,
                                       std::uint32_t tick)
{
    std::lock_guard lock(local_transport_shadows_mutex());
    const auto it = local_transport_shadows().find(&session);
    if (it == local_transport_shadows().end())
        return;

    it->second->client->send_input(input, tick);
}

void local_transport_shadow_finish_tick(SessionState& session)
{
    std::lock_guard lock(local_transport_shadows_mutex());
    const auto it = local_transport_shadows().find(&session);
    if (it == local_transport_shadows().end())
        return;

    it->second->server->poll_incoming_messages();
    it->second->server->broadcast_current_state(
        og::sim::SnapshotCaptureMode::Peek,
        og::sim::EventDeliveryMode::Skip);
    it->second->client->poll_messages();
}

} // namespace og::runtime
