#include <openglad/gameplay/net_transport_multiplex.h>

#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace og::sim {
namespace {

std::optional<TypedReceivedMessage> decode_raw_typed_message(
    const ReceivedMessage& message)
{
    TransportEnvelope envelope;
    if (!decode_transport_envelope(message.data, envelope))
        return std::nullopt;

    TypedReceivedMessage typed_message;
    typed_message.peer_id = message.peer_id;

    switch (envelope.message_type)
    {
    case kSnapshotMessageType:
        typed_message.kind = TypedReceivedMessageKind::Snapshot;
        typed_message.snapshot = std::make_shared<WorldSnapshot>(
            deserialize_snapshot(message.data.data(), message.data.size()));
        return typed_message;

    case kDeltaSnapshotMessageType:
        typed_message.kind = TypedReceivedMessageKind::DeltaSnapshot;
        typed_message.snapshot = std::make_shared<WorldSnapshot>(
            deserialize_delta(message.data.data(), message.data.size()));
        return typed_message;

    case kInputMessageType:
    {
        const std::optional<InputStateMessage> decoded =
            deserialize_input_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::Input;
        typed_message.input = std::make_shared<InputState>(decoded->input);
        typed_message.tick = decoded->tick;
        return typed_message;
    }

    case kSimEventBatchMessageType:
        typed_message.kind = TypedReceivedMessageKind::SimEventBatch;
        typed_message.event_batch = std::make_shared<SimEventBatch>(
            deserialize_sim_event_batch(message.data.data(), message.data.size()));
        return typed_message;

    case kGameFlowEventBatchMessageType:
        typed_message.kind = TypedReceivedMessageKind::GameFlowEventBatch;
        typed_message.event_batch = std::make_shared<SimEventBatch>(
            deserialize_game_flow_event_batch(message.data.data(),
                                              message.data.size()));
        return typed_message;

    case kLobbyMessageType:
    {
        const auto decoded = deserialize_lobby_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::LobbyMessage;
        typed_message.lobby_message =
            std::make_shared<LobbyMessage>(std::move(*decoded));
        return typed_message;
    }

    case kLobbyStateMessageType:
    {
        const auto decoded = deserialize_lobby_state_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::LobbyState;
        typed_message.lobby_state =
            std::make_shared<LobbyState>(std::move(*decoded));
        return typed_message;
    }

    case kInitialSetupMessageType:
    {
        const auto decoded = deserialize_initial_setup_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::InitialSetup;
        typed_message.initial_setup =
            std::make_shared<InitialSetupMessage>(std::move(*decoded));
        return typed_message;
    }

    case kClientReadyMessageType:
    {
        const auto decoded = deserialize_client_ready_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::ClientReady;
        typed_message.client_ready =
            std::make_shared<ClientReadyMessage>(std::move(*decoded));
        return typed_message;
    }

    case kKeyframeRequestMessageType:
    {
        const auto decoded = deserialize_keyframe_request_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::KeyframeRequest;
        typed_message.keyframe_request =
            std::make_shared<KeyframeRequestMessage>(std::move(*decoded));
        return typed_message;
    }

    case kExitPromptBroadcastMessageType:
    {
        const auto decoded =
            deserialize_exit_prompt_broadcast_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::ExitPromptBroadcast;
        typed_message.exit_prompt_broadcast =
            std::make_shared<ExitPromptBroadcastMessage>(std::move(*decoded));
        return typed_message;
    }

    case kExitPromptResponseMessageType:
    {
        const auto decoded =
            deserialize_exit_prompt_response_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::ExitPromptResponse;
        typed_message.exit_prompt_response =
            std::make_shared<ExitPromptResponseMessage>(std::move(*decoded));
        return typed_message;
    }

    case kPauseBroadcastMessageType:
    {
        const auto decoded = deserialize_pause_broadcast_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::PauseBroadcast;
        typed_message.pause_broadcast =
            std::make_shared<PauseBroadcastMessage>(std::move(*decoded));
        return typed_message;
    }

    case kPauseResponseMessageType:
    {
        const auto decoded = deserialize_pause_response_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::PauseResponse;
        typed_message.pause_response =
            std::make_shared<PauseResponseMessage>(std::move(*decoded));
        return typed_message;
    }

    case kControlChangeMessageType:
    {
        const auto decoded = deserialize_control_change_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::ControlChange;
        typed_message.control_change =
            std::make_shared<ControlChangeMessage>(std::move(*decoded));
        return typed_message;
    }

    case kSnapshotHashCheckMessageType:
    {
        const auto decoded =
            deserialize_snapshot_hash_check_message(message.data);
        if (!decoded.has_value())
            return std::nullopt;

        typed_message.kind = TypedReceivedMessageKind::SnapshotHashCheck;
        typed_message.snapshot_hash_check =
            std::make_shared<SnapshotHashCheckMessage>(std::move(*decoded));
        return typed_message;
    }

    default:
        return std::nullopt;
    }
}

} // namespace

MultiplexTransport::MultiplexTransport(
    std::vector<std::shared_ptr<ITransport>> transports)
{
    endpoints_.reserve(transports.size());
    for (std::shared_ptr<ITransport>& transport : transports)
    {
        if (!transport)
            throw std::invalid_argument(
                "MultiplexTransport transport must not be null");
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

            std::optional<TypedReceivedMessage> typed_message =
                decode_raw_typed_message(message);
            if (!typed_message.has_value())
                continue;

            typed_message->peer_id =
                ensure_public_peer(endpoint, typed_message->peer_id);
            messages.push_back(std::move(*typed_message));
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
