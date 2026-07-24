#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class RawTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_.push_back(
            {peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> messages = std::move(queued_);
        queued_.clear();
        return messages;
    }

    void accept_connections() override {}

    void disconnect(og::sim::PeerId peer_id) override
    {
        disconnected_.push_back(peer_id);
    }

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return {};
    }

    void queue(og::sim::PeerId peer_id, std::vector<std::uint8_t> bytes)
    {
        queued_.push_back({peer_id, std::move(bytes)});
    }

    const std::vector<og::sim::ReceivedMessage>& sent() const noexcept
    {
        return sent_;
    }

    const std::vector<og::sim::PeerId>& disconnected() const noexcept
    {
        return disconnected_;
    }

private:
    std::vector<og::sim::ReceivedMessage> queued_;
    std::vector<og::sim::ReceivedMessage> sent_;
    std::vector<og::sim::PeerId> disconnected_;
};

template <typename ByteContainer>
std::vector<std::uint8_t> to_vector(const ByteContainer& bytes)
{
    return {bytes.begin(), bytes.end()};
}

template <typename ByteContainer>
std::vector<std::uint8_t> with_wrong_payload_length(
    const ByteContainer& bytes)
{
    std::vector<std::uint8_t> malformed = to_vector(bytes);
    if (malformed.size() >= og::sim::kTransportHeaderSize)
        malformed[2] ^= 1u;
    return malformed;
}

} // namespace

TEST(GameClientCoverage, raw_transport_decodes_input_and_heartbeat_payloads)
{
    constexpr og::sim::PeerId kServerPeer = 7u;
    RawTransport transport;

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 13;
    input.players[0].held[static_cast<int>(InputAction::MoveLeft)] = true;
    input.players[1].pressed[static_cast<int>(InputAction::Fire)] = true;
    transport.queue(kServerPeer,
                    to_vector(og::sim::serialize_input(44u, input)));
    transport.queue(
        kServerPeer,
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));

    og::sim::GameClient client(transport, kServerPeer);
    client.poll_messages();

    ASSERT_EQ(2u, client.last_polled_messages().size());
    const og::sim::TypedReceivedMessage& decoded_input =
        client.last_polled_messages()[0];
    EXPECT_EQ(kServerPeer, decoded_input.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Input, decoded_input.kind);
    ASSERT_NE(nullptr, decoded_input.input);
    EXPECT_EQ(44u, decoded_input.tick);
    EXPECT_TRUE(decoded_input.input->quit_requested);
    EXPECT_EQ(13, decoded_input.input->timer_wait_request);
    EXPECT_TRUE(decoded_input.input->players[0]
                    .held[static_cast<int>(InputAction::MoveLeft)]);
    EXPECT_TRUE(decoded_input.input->players[1]
                    .pressed[static_cast<int>(InputAction::Fire)]);

    const og::sim::TypedReceivedMessage& decoded_heartbeat =
        client.last_polled_messages()[1];
    EXPECT_EQ(kServerPeer, decoded_heartbeat.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Heartbeat,
              decoded_heartbeat.kind);
    EXPECT_NE(nullptr, decoded_heartbeat.heartbeat);
    EXPECT_EQ(2, client.messages_drained_last_call());
    EXPECT_TRUE(transport.disconnected().empty());
}

TEST(GameClientCoverage,
     raw_transport_rejects_malformed_payloads_at_each_protocol_decoder)
{
    constexpr og::sim::PeerId kServerPeer = 7u;
    const og::sim::LobbyMessage lobby_message;
    const og::sim::LobbyState lobby_state;
    const og::sim::InitialSetupMessage initial_setup;
    const og::sim::HelloMessage hello;
    const og::sim::HeartbeatMessage heartbeat;
    const og::sim::ExitPromptBroadcastMessage exit_prompt;
    const og::sim::PauseBroadcastMessage pause;
    const og::sim::ControlChangeMessage control_change;
    const InputState input{};
    const og::sim::WorldSnapshot snapshot;

    struct MalformedCase
    {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };
    const std::array<MalformedCase, 10> malformed_cases = {{
        {"lobby", with_wrong_payload_length(
                      og::sim::serialize_lobby_message(lobby_message))},
        {"lobby_state", with_wrong_payload_length(
                            og::sim::serialize_lobby_state_message(lobby_state))},
        {"input", with_wrong_payload_length(
                      og::sim::serialize_input(3u, input))},
        {"initial_setup", with_wrong_payload_length(
                              og::sim::serialize_initial_setup_message(
                                  initial_setup))},
        {"hello", with_wrong_payload_length(og::sim::serialize_hello(hello))},
        {"heartbeat", with_wrong_payload_length(
                          og::sim::serialize_heartbeat_message(heartbeat))},
        {"exit_prompt", with_wrong_payload_length(
                            og::sim::serialize_exit_prompt_broadcast_message(
                                exit_prompt))},
        {"pause", with_wrong_payload_length(
                     og::sim::serialize_pause_broadcast_message(pause))},
        {"control_change", with_wrong_payload_length(
                              og::sim::serialize_control_change_message(
                                  control_change))},
        {"snapshot_exception", with_wrong_payload_length(
                                   og::sim::serialize_snapshot(snapshot))},
    }};

    for (const MalformedCase& malformed : malformed_cases)
    {
        SCOPED_TRACE(malformed.name);
        RawTransport transport;
        transport.queue(kServerPeer, malformed.bytes);
        og::sim::GameClient client(transport, kServerPeer);

        client.poll_messages();

        EXPECT_EQ((std::vector<og::sim::PeerId>{kServerPeer}),
                  transport.disconnected());
        EXPECT_TRUE(client.last_polled_messages().empty());
        EXPECT_EQ(0, client.messages_drained_last_call());
        EXPECT_EQ(0u, client.pending_inbound_message_count());
    }
}

TEST(GameClientCoverage,
     foreign_message_is_ignored_and_delta_without_baseline_requests_keyframe)
{
    constexpr og::sim::PeerId kServerPeer = 7u;
    constexpr og::sim::PeerId kForeignPeer = 99u;
    RawTransport transport;
    transport.queue(
        kForeignPeer,
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    og::sim::WorldSnapshot delta;
    delta.tick_count = 6u;
    transport.queue(kServerPeer, og::sim::serialize_delta(delta));

    og::sim::GameClient client(transport, kServerPeer);
    client.poll_messages();

    ASSERT_EQ(2u, client.last_polled_messages().size());
    EXPECT_EQ(kForeignPeer, client.last_polled_messages()[0].peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::DeltaSnapshot,
              client.last_polled_messages()[1].kind);
    EXPECT_FALSE(client.baseline().has_value());
    EXPECT_TRUE(client.waiting_for_keyframe());
    EXPECT_EQ(1u, client.keyframe_request_count());
    EXPECT_EQ(0u, client.last_seen_server_tick());

    ASSERT_EQ(1u, transport.sent().size());
    EXPECT_EQ(kServerPeer, transport.sent().front().peer_id);
    const auto request = og::sim::deserialize_keyframe_request_message(
        transport.sent().front().data);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(0u, request->last_seen_tick);
}

TEST(GameClientCoverage, sim_event_sequence_gap_keeps_both_batches_in_order)
{
    constexpr og::sim::PeerId kServerPeer = 7u;
    RawTransport transport;
    transport.queue(
        kServerPeer,
        og::sim::serialize_sim_event_batch(
            og::sim::SimEventBatch{.sequence = 4u, .events = {}}));
    transport.queue(
        kServerPeer,
        og::sim::serialize_sim_event_batch(
            og::sim::SimEventBatch{.sequence = 9u, .events = {}}));

    std::vector<std::uint32_t> callback_sequences;
    og::sim::GameClient client(transport, kServerPeer);
    client.set_sim_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            callback_sequences.push_back(batch.sequence);
        });

    client.poll_messages();

    ASSERT_EQ(2u, client.sim_event_batches().size());
    EXPECT_EQ(4u, client.sim_event_batches()[0].sequence);
    EXPECT_EQ(9u, client.sim_event_batches()[1].sequence);
    EXPECT_EQ((std::vector<std::uint32_t>{4u, 9u}), callback_sequences);
    EXPECT_EQ(2, client.messages_drained_last_call());
    EXPECT_TRUE(transport.disconnected().empty());
}

TEST(GameClientCoverage, snapshot_hash_without_world_or_baseline_is_noop)
{
    RawTransport transport;
    og::sim::GameClient client(transport, 7u);

    client.send_snapshot_hash_check();

    EXPECT_TRUE(transport.sent().empty());
    EXPECT_EQ(0u, client.snapshot_hash_check_count());
}
