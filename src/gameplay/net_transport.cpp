#include <openglad/gameplay/net_transport.h>

#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/world_snapshot.h>

namespace og::sim {

bool ITransport::supports_typed_messages() const noexcept
{
    return false;
}

void ITransport::send_snapshot(PeerId peer_id,
                               std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::vector<std::uint8_t> bytes = serialize_snapshot(*snapshot);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_delta_snapshot(PeerId peer_id,
                                     std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::vector<std::uint8_t> bytes = serialize_delta(*snapshot);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_input(PeerId peer_id,
                            std::shared_ptr<InputState> input,
                            std::uint32_t tick)
{
    if (!input)
        return;

    const auto bytes = serialize_input(tick, *input);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_sim_event_batch(PeerId peer_id,
                                      std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::vector<std::uint8_t> bytes = serialize_sim_event_batch(*batch);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_game_flow_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_game_flow_event_batch(*batch);
    send(peer_id, bytes.data(), bytes.size());
}

std::vector<TypedReceivedMessage> ITransport::poll_typed()
{
    return {};
}

} // namespace og::sim
