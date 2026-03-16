#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/input_state.h>

#include <gtest/gtest.h>

#include <openglad/gameplay/replay.h>

#include "../test_network_fixture.h"

#include <array>

namespace {

void expect_input_state_eq(const InputState& expected, const InputState& actual)
{
    EXPECT_EQ(expected.quit_requested, actual.quit_requested);
    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            EXPECT_EQ(expected.players[player].held[key],
                      actual.players[player].held[key]);
            EXPECT_EQ(expected.players[player].pressed[key],
                      actual.players[player].pressed[key]);
        }
    }
}

void expect_snapshot_eq(const og::sim::WorldSnapshot& expected,
                        const og::sim::WorldSnapshot& actual)
{
    const auto failure = og::sim::find_first_snapshot_difference(
        expected.tick_count, expected, actual);
    ASSERT_FALSE(failure.has_value())
        << "snapshot mismatch at " << (failure ? failure->field : std::string{})
        << " expected "
        << (failure ? failure->expected_value : std::string{})
        << " actual "
        << (failure ? failure->actual_value : std::string{});
}

void expect_event_batch_eq(const og::sim::SimEventBatch& expected,
                           const og::sim::SimEventBatch& actual)
{
    ASSERT_EQ(expected.sequence, actual.sequence);
    ASSERT_EQ(expected.events.size(), actual.events.size());
    for (std::size_t index = 0; index < expected.events.size(); ++index)
        EXPECT_EQ(expected.events[index], actual.events[index]);
}

og::sim::WorldSnapshot make_snapshot(std::uint32_t tick)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = tick;
    snapshot.rng_state = tick * 17u;
    snapshot.level_tick_count = tick * 3u;
    snapshot.current_palette_id = 1;
    snapshot.pending_exit_prompt = true;
    snapshot.paused = true;
    snapshot.pause_player_index = 2;
    snapshot.my_team = 3;
    snapshot.allied_mode = 1;
    snapshot.difficulty = 125;
    snapshot.grid_width = 2;
    snapshot.grid_height = 2;
    snapshot.grid_dirty = true;
    snapshot.grid_full_resend = true;
    snapshot.full_grid_data = {1u, 2u, 3u, 4u};
    snapshot.removed_entity_ids = {10u, 11u};
    return snapshot;
}

og::sim::WorldSnapshot make_delta_snapshot(std::uint32_t tick)
{
    og::sim::WorldSnapshot delta = make_snapshot(tick);
    delta.removed_entity_ids.clear();
    return delta;
}

og::sim::SimEventBatch make_event_batch(std::uint32_t sequence)
{
    og::sim::SimEventBatch batch;
    batch.sequence = sequence;
    batch.events.push_back({
        .tick = sequence,
        .kind = og::sim::EventKind::Notification,
        .a = 120,
        .b = 0,
        .text = "sync ok",
    });
    batch.events.push_back({
        .tick = sequence,
        .kind = og::sim::EventKind::SetPalette,
        .a = 1,
        .b = 0,
        .text = {},
    });
    return batch;
}

} // namespace

TEST(NetTransportInProcess, linked_pair_preserves_raw_send_receive)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair();
    ASSERT_NE(nullptr, pair.server);
    ASSERT_NE(nullptr, pair.client);
    EXPECT_EQ((std::vector<og::sim::PeerId>{pair.peer_id}),
              pair.server->connected_peers());
    EXPECT_EQ((std::vector<og::sim::PeerId>{pair.peer_id}),
              pair.client->connected_peers());

    const std::array<std::uint8_t, 3> outbound = {0xaa, 0xbb, 0xcc};
    pair.server->send(pair.peer_id, outbound.data(), outbound.size());

    const std::vector<og::sim::ReceivedMessage> received = pair.client->poll();
    ASSERT_EQ(1u, received.size());
    EXPECT_EQ(pair.peer_id, received.front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0xaa, 0xbb, 0xcc}),
              received.front().data);
}

TEST(NetTransportInProcess, multi_client_broadcast_preserves_message_order)
{
    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto client_one = server->create_client_transport();
    auto client_two = server->create_client_transport();

    const auto peer_one = client_one->local_peer_id();
    const auto peer_two = client_two->local_peer_id();

    server->send_snapshot(peer_one,
                          std::make_shared<og::sim::WorldSnapshot>(
                              make_snapshot(1u)));
    server->send_delta_snapshot(peer_one,
                                std::make_shared<og::sim::WorldSnapshot>(
                                    make_delta_snapshot(2u)));
    server->send_sim_event_batch(peer_one,
                                 std::make_shared<og::sim::SimEventBatch>(
                                     make_event_batch(2u)));

    server->send_snapshot(peer_two,
                          std::make_shared<og::sim::WorldSnapshot>(
                              make_snapshot(3u)));

    const std::vector<og::sim::TypedReceivedMessage> first_messages =
        client_one->poll_typed();
    ASSERT_EQ(3u, first_messages.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Snapshot,
              first_messages[0].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::DeltaSnapshot,
              first_messages[1].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::SimEventBatch,
              first_messages[2].kind);
    ASSERT_NE(nullptr, first_messages[0].snapshot);
    ASSERT_NE(nullptr, first_messages[1].snapshot);
    ASSERT_NE(nullptr, first_messages[2].event_batch);
    EXPECT_EQ(1u, first_messages[0].snapshot->tick_count);
    EXPECT_EQ(2u, first_messages[1].snapshot->tick_count);
    EXPECT_EQ(2u, first_messages[2].event_batch->sequence);

    const std::vector<og::sim::TypedReceivedMessage> second_messages =
        client_two->poll_typed();
    ASSERT_EQ(1u, second_messages.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Snapshot,
              second_messages[0].kind);
    ASSERT_NE(nullptr, second_messages[0].snapshot);
    EXPECT_EQ(3u, second_messages[0].snapshot->tick_count);
}

TEST(NetTransportInProcess, validating_mode_roundtrips_all_typed_messages)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair(
        {.validate_serialization = true});

    const og::sim::WorldSnapshot snapshot = make_snapshot(7u);
    pair.server->send_snapshot(pair.peer_id,
                               std::make_shared<og::sim::WorldSnapshot>(
                                   snapshot));
    const og::sim::WorldSnapshot delta = make_delta_snapshot(8u);
    pair.server->send_delta_snapshot(
        pair.peer_id,
        std::make_shared<og::sim::WorldSnapshot>(delta));

    InputState input{};
    input.quit_requested = true;
    input.players[0].held[static_cast<int>(InputAction::MoveLeft)] = true;
    input.players[1].pressed[static_cast<int>(InputAction::Fire)] = true;
    pair.client->send_input(pair.peer_id,
                            std::make_shared<InputState>(input),
                            19u);

    const og::sim::SimEventBatch batch = make_event_batch(9u);
    pair.server->send_sim_event_batch(
        pair.peer_id,
        std::make_shared<og::sim::SimEventBatch>(batch));
    const og::sim::SimEventBatch game_flow_batch = make_event_batch(10u);
    pair.server->send_game_flow_event_batch(
        pair.peer_id,
        std::make_shared<og::sim::SimEventBatch>(game_flow_batch));

    const std::vector<og::sim::TypedReceivedMessage> client_messages =
        pair.client->poll_typed();
    ASSERT_EQ(4u, client_messages.size());
    ASSERT_NE(nullptr, client_messages[0].snapshot);
    ASSERT_NE(nullptr, client_messages[1].snapshot);
    ASSERT_NE(nullptr, client_messages[2].event_batch);
    ASSERT_NE(nullptr, client_messages[3].event_batch);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Snapshot,
              client_messages[0].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::DeltaSnapshot,
              client_messages[1].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::SimEventBatch,
              client_messages[2].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::GameFlowEventBatch,
              client_messages[3].kind);
    expect_snapshot_eq(snapshot, *client_messages[0].snapshot);
    expect_snapshot_eq(delta, *client_messages[1].snapshot);
    expect_event_batch_eq(batch, *client_messages[2].event_batch);
    expect_event_batch_eq(game_flow_batch, *client_messages[3].event_batch);

    const std::vector<og::sim::TypedReceivedMessage> server_messages =
        pair.server->poll_typed();
    ASSERT_EQ(1u, server_messages.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Input,
              server_messages[0].kind);
    EXPECT_EQ(19u, server_messages[0].tick);
    ASSERT_NE(nullptr, server_messages[0].input);
    expect_input_state_eq(input, *server_messages[0].input);
}

TEST(NetTransportInProcess, network_fixture_loads_ticks_and_keeps_client_in_sync)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 10,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.run();
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_applies_input_sequence_and_keeps_client_in_sync)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 1,
        .validate_serialization = true,
        .input_sequence =
            [](std::size_t client_index, std::uint32_t) {
                InputState input{};
                input.players[client_index].held[static_cast<int>(
                    InputAction::MoveRight)] = true;
                return input;
            },
    });

    fixture.run();

    ASSERT_EQ(1u, fixture.server_inbox().size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Input,
              fixture.server_inbox()[0].kind);
    ASSERT_NE(nullptr, fixture.server_inbox()[0].input);
    EXPECT_TRUE(fixture.server_inbox()[0].input->players[0].held[static_cast<int>(
        InputAction::MoveRight)]);
    EXPECT_NE(nullptr, fixture.server_control(0));
    EXPECT_GT(fixture.server_world().control_hp, 0.0f);
    fixture.expect_clients_match_server();
}
