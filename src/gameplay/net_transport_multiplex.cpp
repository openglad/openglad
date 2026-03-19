#include <openglad/gameplay/net_transport_multiplex.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace og::sim {

MultiplexTransport::MultiplexTransport(
    std::vector<std::shared_ptr<ITransport>> transports)
{
    endpoints_.reserve(transports.size());
    for (std::shared_ptr<ITransport>& transport : transports)
    {
        if (!transport)
        {
            throw std::invalid_argument(
                "MultiplexTransport transport must not be null");
        }

        endpoints_.push_back(EndpointState{
            .transport = std::move(transport),
            .native_to_public = {},
            .public_to_native = {},
        });
    }
}

bool MultiplexTransport::supports_typed_messages() const noexcept
{
    return true;
}

PeerId MultiplexTransport::ensure_public_peer(EndpointState& endpoint,
                                              PeerId native_peer_id) const
{
    const auto existing = endpoint.native_to_public.find(native_peer_id);
    if (existing != endpoint.native_to_public.end())
        return existing->second;

    const PeerId public_peer_id = next_public_peer_id_++;
    endpoint.native_to_public.emplace(native_peer_id, public_peer_id);
    endpoint.public_to_native.emplace(public_peer_id, native_peer_id);
    return public_peer_id;
}

void MultiplexTransport::sync_endpoint_peers(EndpointState& endpoint) const
{
    const std::vector<PeerId> native_peers = endpoint.transport->connected_peers();
    std::unordered_set<PeerId> native_peer_set(native_peers.begin(), native_peers.end());

    for (const PeerId native_peer_id : native_peers)
        (void)ensure_public_peer(endpoint, native_peer_id);

    std::vector<PeerId> removed_native_peers;
    removed_native_peers.reserve(endpoint.native_to_public.size());
    for (const auto& [native_peer_id, public_peer_id] : endpoint.native_to_public)
    {
        (void)public_peer_id;
        if (!native_peer_set.contains(native_peer_id))
            removed_native_peers.push_back(native_peer_id);
    }

    for (const PeerId native_peer_id : removed_native_peers)
    {
        const auto public_it = endpoint.native_to_public.find(native_peer_id);
        if (public_it == endpoint.native_to_public.end())
            continue;
        endpoint.public_to_native.erase(public_it->second);
        endpoint.native_to_public.erase(public_it);
    }
}

MultiplexTransport::EndpointState* MultiplexTransport::find_endpoint_for_public_peer(
    PeerId peer_id) noexcept
{
    for (EndpointState& endpoint : endpoints_)
    {
        const auto native_it = endpoint.public_to_native.find(peer_id);
        if (native_it != endpoint.public_to_native.end())
            return &endpoint;
    }
    return nullptr;
}

void MultiplexTransport::send(PeerId peer_id,
                              const std::uint8_t* data,
                              std::size_t len)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;

    const PeerId native_peer_id = endpoint->public_to_native[peer_id];
    endpoint->transport->send(native_peer_id, data, len);
}

void MultiplexTransport::broadcast(const std::uint8_t* data, std::size_t len)
{
    for (EndpointState& endpoint : endpoints_)
    {
        sync_endpoint_peers(endpoint);
        if (endpoint.public_to_native.empty())
            continue;
        endpoint.transport->broadcast(data, len);
    }
}

void MultiplexTransport::send_snapshot(PeerId peer_id,
                                       std::shared_ptr<WorldSnapshot> snapshot)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_snapshot(
        endpoint->public_to_native[peer_id],
        std::move(snapshot));
}

void MultiplexTransport::send_delta_snapshot(
    PeerId peer_id,
    std::shared_ptr<WorldSnapshot> snapshot)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_delta_snapshot(
        endpoint->public_to_native[peer_id],
        std::move(snapshot));
}

void MultiplexTransport::send_input(PeerId peer_id,
                                    std::shared_ptr<InputState> input,
                                    std::uint32_t tick)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_input(
        endpoint->public_to_native[peer_id],
        std::move(input),
        tick);
}

void MultiplexTransport::send_sim_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_sim_event_batch(
        endpoint->public_to_native[peer_id],
        std::move(batch));
}

void MultiplexTransport::send_game_flow_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_game_flow_event_batch(
        endpoint->public_to_native[peer_id],
        std::move(batch));
}

void MultiplexTransport::send_lobby_message(
    PeerId peer_id,
    std::shared_ptr<LobbyMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_lobby_message(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_lobby_state(PeerId peer_id,
                                          std::shared_ptr<LobbyState> state)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_lobby_state(
        endpoint->public_to_native[peer_id],
        std::move(state));
}

void MultiplexTransport::send_initial_setup(
    PeerId peer_id,
    std::shared_ptr<InitialSetupMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_initial_setup(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_hello(PeerId peer_id,
                                    std::shared_ptr<HelloMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_hello(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_client_ready(
    PeerId peer_id,
    std::shared_ptr<ClientReadyMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_client_ready(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_keyframe_request(
    PeerId peer_id,
    std::shared_ptr<KeyframeRequestMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_keyframe_request(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_heartbeat(
    PeerId peer_id,
    std::shared_ptr<HeartbeatMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_heartbeat(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_exit_prompt_broadcast(
    PeerId peer_id,
    std::shared_ptr<ExitPromptBroadcastMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_exit_prompt_broadcast(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_exit_prompt_response(
    PeerId peer_id,
    std::shared_ptr<ExitPromptResponseMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_exit_prompt_response(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_pause_broadcast(
    PeerId peer_id,
    std::shared_ptr<PauseBroadcastMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_pause_broadcast(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_pause_response(
    PeerId peer_id,
    std::shared_ptr<PauseResponseMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_pause_response(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_control_change(
    PeerId peer_id,
    std::shared_ptr<ControlChangeMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_control_change(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

void MultiplexTransport::send_snapshot_hash_check(
    PeerId peer_id,
    std::shared_ptr<SnapshotHashCheckMessage> message)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;
    endpoint->transport->send_snapshot_hash_check(
        endpoint->public_to_native[peer_id],
        std::move(message));
}

std::vector<ReceivedMessage> MultiplexTransport::poll()
{
    std::vector<ReceivedMessage> messages;
    for (EndpointState& endpoint : endpoints_)
    {
        std::vector<ReceivedMessage> endpoint_messages = endpoint.transport->poll();
        for (ReceivedMessage& message : endpoint_messages)
        {
            if (message.peer_id == 0)
                continue;
            message.peer_id = ensure_public_peer(endpoint, message.peer_id);
            messages.push_back(std::move(message));
        }
        sync_endpoint_peers(endpoint);
    }
    return messages;
}

std::vector<TypedReceivedMessage> MultiplexTransport::poll_typed()
{
    std::vector<TypedReceivedMessage> messages;
    for (EndpointState& endpoint : endpoints_)
    {
        if (endpoint.transport->supports_typed_messages())
        {
            for (TypedReceivedMessage& message : endpoint.transport->poll_typed())
            {
                if (message.peer_id == 0)
                    continue;
                message.peer_id = ensure_public_peer(endpoint, message.peer_id);
                messages.push_back(std::move(message));
            }
        }

        for (ReceivedMessage& message : endpoint.transport->poll())
        {
            if (message.peer_id == 0)
                continue;

            TypedReceivedMessage typed_message = decode_received_message(message);
            typed_message.peer_id =
                ensure_public_peer(endpoint, typed_message.peer_id);
            messages.push_back(std::move(typed_message));
        }
        sync_endpoint_peers(endpoint);
    }
    return messages;
}

void MultiplexTransport::accept_connections()
{
    for (EndpointState& endpoint : endpoints_)
        endpoint.transport->accept_connections();
}

void MultiplexTransport::disconnect(PeerId peer_id)
{
    EndpointState* const endpoint = find_endpoint_for_public_peer(peer_id);
    if (endpoint == nullptr)
        return;

    const auto native_it = endpoint->public_to_native.find(peer_id);
    if (native_it == endpoint->public_to_native.end())
        return;

    endpoint->transport->disconnect(native_it->second);
    endpoint->native_to_public.erase(native_it->second);
    endpoint->public_to_native.erase(native_it);
}

std::vector<PeerId> MultiplexTransport::connected_peers() const
{
    std::vector<PeerId> peers;
    for (EndpointState& endpoint : endpoints_)
    {
        sync_endpoint_peers(endpoint);
        peers.reserve(peers.size() + endpoint.public_to_native.size());
        for (const auto& [public_peer_id, native_peer_id] : endpoint.public_to_native)
        {
            (void)native_peer_id;
            peers.push_back(public_peer_id);
        }
    }

    std::sort(peers.begin(), peers.end());
    return peers;
}

} // namespace og::sim
