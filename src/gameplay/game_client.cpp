#include <openglad/gameplay/game_client.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/input_state_net.h>

#include <stdexcept>
#include <utility>

namespace
{

void clear_transport_only_snapshot_state(og::sim::WorldSnapshot& snapshot)
{
    snapshot.removed_entity_ids.clear();
}

void clear_transport_only_world_state(GameWorld& world)
{
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
}

std::vector<og::sim::TypedReceivedMessage> poll_client_messages(
    og::sim::ITransport& transport)
{
    if (transport.supports_typed_messages())
        return transport.poll_typed();

    std::vector<og::sim::TypedReceivedMessage> typed_messages;
    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            throw std::runtime_error(
                "GameClient received malformed transport header");
        }

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        switch (envelope.message_type)
        {
        case og::sim::kSnapshotMessageType:
            typed_message.kind = og::sim::TypedReceivedMessageKind::Snapshot;
            typed_message.snapshot = std::make_shared<og::sim::WorldSnapshot>(
                og::sim::deserialize_snapshot(message.data.data(),
                                              message.data.size()));
            break;

        case og::sim::kDeltaSnapshotMessageType:
            typed_message.kind = og::sim::TypedReceivedMessageKind::DeltaSnapshot;
            typed_message.snapshot = std::make_shared<og::sim::WorldSnapshot>(
                og::sim::deserialize_delta(message.data.data(),
                                           message.data.size()));
            break;

        case og::sim::kSimEventBatchMessageType:
            typed_message.kind = og::sim::TypedReceivedMessageKind::SimEventBatch;
            typed_message.event_batch = std::make_shared<og::sim::SimEventBatch>(
                og::sim::deserialize_sim_event_batch(message.data.data(),
                                                     message.data.size()));
            break;

        case og::sim::kGameFlowEventBatchMessageType:
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::GameFlowEventBatch;
            typed_message.event_batch = std::make_shared<og::sim::SimEventBatch>(
                og::sim::deserialize_game_flow_event_batch(message.data.data(),
                                                           message.data.size()));
            break;

        case og::sim::kInputMessageType:
        {
            const std::optional<og::sim::InputStateMessage> decoded =
                og::sim::deserialize_input_message(message.data);
            if (!decoded.has_value())
            {
                throw std::runtime_error(
                    "GameClient failed to deserialize input message");
            }
            typed_message.kind = og::sim::TypedReceivedMessageKind::Input;
            typed_message.input = std::make_shared<InputState>(decoded->input);
            typed_message.tick = decoded->tick;
            break;
        }

        default:
            continue;
        }

        typed_messages.push_back(std::move(typed_message));
    }

    return typed_messages;
}

} // namespace

namespace og::sim {

GameClient::GameClient(ITransport& transport,
                       PeerId server_peer_id,
                       GameWorld* world)
    : transport_(transport)
    , server_peer_id_(server_peer_id)
    , world_(world)
{
}

void GameClient::send_input(const InputState& input, std::uint32_t tick)
{
    transport_.send_input(server_peer_id_,
                          std::make_shared<InputState>(input),
                          tick);
}

void GameClient::poll_messages()
{
    last_polled_messages_ = poll_client_messages(transport_);
    sim_event_batches_.clear();
    game_flow_event_batches_.clear();
    for (const auto& message : last_polled_messages_)
    {
        switch (message.kind)
        {
        case TypedReceivedMessageKind::Snapshot:
            if (!message.snapshot)
                break;
            baseline_ = *message.snapshot;
            if (world_ != nullptr)
            {
                apply_snapshot(*world_, *baseline_);
                clear_transport_only_world_state(*world_);
            }
            clear_transport_only_snapshot_state(*baseline_);
            break;

        case TypedReceivedMessageKind::DeltaSnapshot:
            if (!message.snapshot)
                break;
            if (!baseline_.has_value())
            {
                throw std::runtime_error(
                    "GameClient received delta snapshot before baseline");
            }
            apply_delta(*baseline_, *message.snapshot);
            if (world_ != nullptr)
            {
                apply_snapshot(*world_, *baseline_);
                clear_transport_only_world_state(*world_);
            }
            clear_transport_only_snapshot_state(*baseline_);
            break;

        case TypedReceivedMessageKind::SimEventBatch:
            if (message.event_batch)
                sim_event_batches_.push_back(*message.event_batch);
            break;

        case TypedReceivedMessageKind::GameFlowEventBatch:
            if (message.event_batch)
                game_flow_event_batches_.push_back(*message.event_batch);
            break;

        case TypedReceivedMessageKind::Input:
            break;
        }
    }
}

} // namespace og::sim
