#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>

#include <gtest/gtest.h>

#include <openglad/gameplay/replay.h>

#include "../test_game_world_fixture.h"
#include "../test_network_fixture.h"

#include <algorithm>
#include <array>
#include <memory>

namespace {

void expect_input_state_eq(const InputState& expected, const InputState& actual)
{
    EXPECT_EQ(expected.quit_requested, actual.quit_requested);
    EXPECT_EQ(expected.timer_wait_request, actual.timer_wait_request);
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

og::sim::LobbyState make_lobby_state()
{
    og::sim::LobbyCharacterData character;
    character.guy_id = 7;
    character.name = "Lobby Hero";
    character.family = 1;
    character.strength = 14;
    character.dexterity = 13;
    character.constitution = 12;
    character.intelligence = 11;
    character.armor = 10;
    character.exp = 900u;
    character.level = 4;
    character.teamnum = 2;

    og::sim::LobbyPlayer player;
    player.player_index = 1u;
    player.name = "Lobby Host";
    player.team = 2;
    player.ready = true;
    player.is_host = true;
    player.character_slots.push_back({
        .slot_index = 0u,
        .character = character,
    });

    og::sim::LobbyState state;
    state.settings.campaign_id = "org.openglad.gladiator";
    state.settings.scenario_id = 5;
    state.settings.difficulty = 2;
    state.settings.allied_mode = 1;
    state.players.push_back(player);
    return state;
}

std::pair<short, short> find_damageable_grid_tile(const GameWorld& world)
{
    EXPECT_TRUE(world.grid.valid());
    for (short y = 0; y < world.grid.h; ++y)
    {
        for (short x = 0; x < world.grid.w; ++x)
        {
            const unsigned char value =
                world.grid.data[static_cast<std::size_t>(y) * world.grid.w + x];
            switch (value)
            {
            case PIX_GRASS1:
            case PIX_GRASS2:
            case PIX_GRASS3:
            case PIX_GRASS4:
                return {x, y};

            default:
                break;
            }
        }
    }

    ADD_FAILURE() << "expected a damageable grass tile in the fixture level";
    return {0, 0};
}

unsigned char read_grid_tile(const GameWorld& world, short x, short y)
{
    return world.grid.data[static_cast<std::size_t>(y) * world.grid.w + x];
}

walker* add_network_player_character(GameWorld& world,
                                     int family,
                                     short team,
                                     const char* name)
{
    walker* const actor = world.add_ob(Order::Living, family);
    if (actor == nullptr)
        return nullptr;

    auto member = std::make_unique<guy>(family);
    member->name = name;
    member->teamnum = team;
    actor->set_owned_myguy(std::move(member));
    actor->set_team_num(static_cast<unsigned char>(team));
    actor->set_real_team_num(255);
    actor->set_user(-1);
    actor->set_act_type(ACT_RANDOM);
    actor->set_dead(0);
    if (actor->stats() != nullptr)
        actor->stats()->set_level(actor->myguy->level);
    actor->myguy->update_derived_stats(actor);
    return actor;
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

TEST(NetTransportInProcess, direct_sends_to_multiple_clients_preserve_message_order)
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

TEST(NetTransportInProcessJitter, typed_messages_are_drained_once_per_poll)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair();
    ASSERT_NE(nullptr, pair.server);
    ASSERT_NE(nullptr, pair.client);

    pair.server->send_snapshot(
        pair.peer_id,
        std::make_shared<og::sim::WorldSnapshot>(make_snapshot(5u)));
    pair.server->send_heartbeat(
        pair.peer_id,
        std::make_shared<og::sim::HeartbeatMessage>());

    const std::vector<og::sim::TypedReceivedMessage> first =
        pair.client->poll_typed();
    ASSERT_EQ(2u, first.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Snapshot, first[0].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Heartbeat, first[1].kind);

    const std::vector<og::sim::TypedReceivedMessage> second =
        pair.client->poll_typed();
    EXPECT_TRUE(second.empty());
}

TEST(NetTransportInProcessJitter, empty_polls_do_not_create_periodic_messages)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair();
    ASSERT_NE(nullptr, pair.server);
    ASSERT_NE(nullptr, pair.client);

    EXPECT_TRUE(pair.client->poll_typed().empty());
    EXPECT_TRUE(pair.client->poll_typed().empty());

    pair.server->send_snapshot_hash_check(
        pair.peer_id,
        std::make_shared<og::sim::SnapshotHashCheckMessage>(
            og::sim::SnapshotHashCheckMessage{
                .tick = og::sim::KEYFRAME_INTERVAL_TICKS,
                .snapshot_hash = 0x1234u,
            }));

    ASSERT_EQ(1u, pair.client->poll_typed().size());
    EXPECT_TRUE(pair.client->poll_typed().empty());
}

TEST(NetTransportInProcess,
     game_server_broadcast_current_state_reaches_all_clients_in_order)
{
    TestGameWorld fixture;
    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    auto client_one = server_transport->create_client_transport();
    auto client_two = server_transport->create_client_transport();

    og::sim::GameServer server(fixture.world(), fixture.events,
                               *server_transport);
    server.connect_client(client_one->local_peer_id());
    server.connect_client(client_two->local_peer_id());
    server.bind_player(client_one->local_peer_id(), 0u, 2);
    server.bind_player(client_two->local_peer_id(), 1u, 2);

    fixture.world().tick_count_ = 3u;
    fixture.world().my_team = 2;
    fixture.world().current_palette_id = 1;
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    fixture.world().tick_count_ = 4u;
    fixture.world().current_palette_id = 2;
    fixture.world().pending_exit_prompt = true;
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    const std::vector<og::sim::TypedReceivedMessage> first_client_messages =
        client_one->poll_typed();
    const std::vector<og::sim::TypedReceivedMessage> second_client_messages =
        client_two->poll_typed();

    ASSERT_EQ(2u, first_client_messages.size());
    ASSERT_EQ(2u, second_client_messages.size());

    EXPECT_EQ(client_one->local_peer_id(), first_client_messages[0].peer_id);
    EXPECT_EQ(client_one->local_peer_id(), first_client_messages[1].peer_id);
    EXPECT_EQ(client_two->local_peer_id(), second_client_messages[0].peer_id);
    EXPECT_EQ(client_two->local_peer_id(), second_client_messages[1].peer_id);

    EXPECT_EQ(og::sim::TypedReceivedMessageKind::InitialSetup,
              first_client_messages[0].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Snapshot,
              first_client_messages[1].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::InitialSetup,
              second_client_messages[0].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Snapshot,
              second_client_messages[1].kind);

    ASSERT_NE(nullptr, first_client_messages[1].snapshot);
    ASSERT_NE(nullptr, second_client_messages[1].snapshot);
    ASSERT_NE(nullptr, first_client_messages[0].initial_setup);
    ASSERT_NE(nullptr, second_client_messages[0].initial_setup);

    EXPECT_EQ(2, first_client_messages[0].initial_setup->my_team);
    EXPECT_EQ(2, second_client_messages[0].initial_setup->my_team);
    EXPECT_EQ(3u, first_client_messages[1].snapshot->tick_count);
    EXPECT_EQ(2, first_client_messages[1].snapshot->my_team);
    EXPECT_EQ(1, first_client_messages[1].snapshot->current_palette_id);

    expect_snapshot_eq(*first_client_messages[1].snapshot,
                       *second_client_messages[1].snapshot);
}

TEST(NetTransportInProcess,
     game_server_shared_team_last_survivor_does_not_trigger_false_endgame)
{
    constexpr short kSharedTeam = 5;
    TestGameWorld fixture;
    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    auto client_one = server_transport->create_client_transport();
    auto client_two = server_transport->create_client_transport();

    walker* const first =
        add_network_player_character(fixture.world(), FAMILY_SOLDIER, kSharedTeam,
                                     "Alexander One");
    walker* const second =
        add_network_player_character(fixture.world(), FAMILY_SOLDIER, kSharedTeam,
                                     "Alexander Two");
    walker* const spare =
        add_network_player_character(fixture.world(), FAMILY_SOLDIER, kSharedTeam,
                                     "Alexander Spare");
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    ASSERT_NE(nullptr, spare);

    og::sim::GameServer server(fixture.world(), fixture.events,
                               *server_transport);
    server.connect_client(client_one->local_peer_id());
    server.connect_client(client_two->local_peer_id());
    server.bind_player(client_one->local_peer_id(), 0u, kSharedTeam);
    server.bind_player(client_two->local_peer_id(), 1u, kSharedTeam);

    ASSERT_EQ(first, server.player_control(0));
    ASSERT_EQ(second, server.player_control(1));

    first->set_dead(1);
    second->set_dead(1);
    fixture.world().ending = 0;
    fixture.world().game_ended = false;
    fixture.world().end = 0;

    server.step();

    EXPECT_EQ(0, fixture.world().ending)
        << "a surviving allied character should prevent multiplayer endgame";
    EXPECT_EQ(1u, fixture.world().tick_count_)
        << "the authoritative world should keep ticking when a teammate survives";

    const std::array<walker*, 2> controls = {
        server.player_control(0),
        server.player_control(1),
    };
    EXPECT_EQ(
        1,
        std::count(controls.begin(), controls.end(), spare))
        << "exactly one player should be reassigned to the spare survivor";
    EXPECT_EQ(
        1,
        std::count(controls.begin(), controls.end(), nullptr))
        << "the other player should be left without a direct control instead of ending the match";
}

TEST(NetTransportInProcess,
     game_server_applies_set_palette_event_to_authoritative_state)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    fixture.server_world().current_palette_id = 0;
    fixture.with_server_context([&] {
        fixture.server_events().push(og::sim::EventKind::SetPalette, 1u, 0u);
        fixture.server().broadcast_current_state(
            og::sim::SnapshotCaptureMode::Peek,
            og::sim::EventDeliveryMode::Drain);
    });

    EXPECT_EQ(1, fixture.server_world().current_palette_id);

    const std::vector<og::sim::TypedReceivedMessage> messages =
        fixture.client_transport(0).poll_typed();
    const auto snapshot_it = std::find_if(
        messages.begin(),
        messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return (message.kind == og::sim::TypedReceivedMessageKind::Snapshot ||
                    message.kind ==
                        og::sim::TypedReceivedMessageKind::DeltaSnapshot) &&
                message.snapshot != nullptr;
        });
    ASSERT_NE(messages.end(), snapshot_it);
    EXPECT_EQ(1, snapshot_it->snapshot->current_palette_id);
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
    input.timer_wait_request = 5;
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

TEST(NetTransportInProcess, validating_mode_roundtrips_lobby_messages)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair(
        {.validate_serialization = true});

    const og::sim::LobbyState state = make_lobby_state();
    pair.server->send_lobby_state(
        pair.peer_id,
        std::make_shared<og::sim::LobbyState>(state));

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 1u,
        .settings =
            {
                .campaign_id = "org.openglad.gladiator",
                .scenario_id = 6,
                .difficulty = 1,
                .allied_mode = 0,
            },
    };
    pair.client->send_lobby_message(
        pair.peer_id,
        std::make_shared<og::sim::LobbyMessage>(message));

    const std::vector<og::sim::TypedReceivedMessage> client_messages =
        pair.client->poll_typed();
    ASSERT_EQ(1u, client_messages.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyState,
              client_messages[0].kind);
    ASSERT_NE(nullptr, client_messages[0].lobby_state);
    EXPECT_EQ(state, *client_messages[0].lobby_state);

    const std::vector<og::sim::TypedReceivedMessage> server_messages =
        pair.server->poll_typed();
    ASSERT_EQ(1u, server_messages.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              server_messages[0].kind);
    ASSERT_NE(nullptr, server_messages[0].lobby_message);
    EXPECT_EQ(message, *server_messages[0].lobby_message);
}

TEST(NetTransportInProcess,
     network_fixture_accepts_timer_wait_request_only_from_host_client)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    fixture.server_world().timer_wait = 6;

    InputState host_input{};
    host_input.timer_wait_request = 4;
    InputState guest_input{};
    guest_input.timer_wait_request = 18;

    std::uint32_t tick = fixture.server_world().tick_count_ + 1;
    fixture.client(1).send_input(guest_input, tick);
    fixture.client(0).send_input(host_input, tick);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_EQ(4, fixture.server_world().timer_wait);

    InputState guest_only_input{};
    guest_only_input.timer_wait_request = 12;
    tick = fixture.server_world().tick_count_ + 1;
    fixture.client(1).send_input(guest_only_input, tick);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_EQ(4, fixture.server_world().timer_wait);
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

    const auto input_it = std::find_if(
        fixture.server_inbox().begin(),
        fixture.server_inbox().end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::Input;
        });
    ASSERT_NE(fixture.server_inbox().end(), input_it);
    ASSERT_NE(nullptr, input_it->input);
    EXPECT_TRUE(input_it->input->players[0].held[static_cast<int>(
        InputAction::MoveRight)]);
    EXPECT_NE(nullptr, fixture.server_control(0));
    EXPECT_GT(fixture.server_world().control_hp, 0.0f);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_requests_keyframe_after_delta_gap_and_recovers)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    ASSERT_FALSE(fixture.client_transport(0).poll_typed().empty())
        << "dropping the first post-ready keyframe should create the gap";

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_TRUE(fixture.client(0).waiting_for_keyframe());
    EXPECT_EQ(1u, fixture.client(0).keyframe_request_count());

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_FALSE(fixture.client(0).waiting_for_keyframe());
    ASSERT_TRUE(fixture.client(0).baseline().has_value());
    EXPECT_EQ(fixture.server_world().tick_count_,
              fixture.client(0).baseline()->tick_count);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess, network_fixture_freezes_on_exit_prompt_and_resumes)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    fixture.with_server_context([&] {
        fixture.server_events().push_with_text(
            og::sim::EventKind::RequestExitConfirmation,
            "Exit now?",
            5u,
            0u);
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    ASSERT_TRUE(fixture.server().pending_exit_prompt());
    ASSERT_TRUE(fixture.client(0).last_exit_prompt().has_value());
    EXPECT_EQ(5, fixture.client(0).last_exit_prompt()->destination_level);
    EXPECT_EQ("Exit now?", fixture.client(0).last_exit_prompt()->prompt_text);

    const std::uint32_t frozen_tick = fixture.server_world().tick_count_;
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_EQ(frozen_tick, fixture.server_world().tick_count_);

    fixture.client(0).send_exit_prompt_response(false);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_FALSE(fixture.server().pending_exit_prompt());
    EXPECT_GT(fixture.server_world().tick_count_, frozen_tick);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess, network_fixture_keeps_two_clients_in_sync)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(3);

    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess, network_fixture_keeps_four_clients_in_sync)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 4,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(3);

    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_merges_late_pressed_input_with_exact_tick_input)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);
    EXPECT_EQ(0, control->yo_delay());

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    InputState late_input;
    late_input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    fixture.client(0).send_input(late_input, 1u);

    InputState exact_input;
    fixture.client(0).send_input(exact_input, 2u);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_GT(control->yo_delay(), 0);
}

TEST(NetTransportInProcess, network_fixture_pause_broadcast_freezes_and_resumes)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    fixture.client(0).send_pause_request();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    ASSERT_TRUE(fixture.server().paused());
    ASSERT_TRUE(fixture.client(0).last_pause_broadcast().has_value());
    ASSERT_TRUE(fixture.client(1).last_pause_broadcast().has_value());
    EXPECT_EQ(0u, fixture.client(0).last_pause_broadcast()->player_index);
    EXPECT_EQ(0u, fixture.client(1).last_pause_broadcast()->player_index);

    const std::uint32_t frozen_tick = fixture.server_world().tick_count_;
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);
    EXPECT_EQ(frozen_tick, fixture.server_world().tick_count_);
    EXPECT_EQ(0u, fixture.server().snapshot_hash_mismatch_count());

    fixture.client(1).send_pause_response();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    EXPECT_FALSE(fixture.server().paused());
    EXPECT_GT(fixture.server_world().tick_count_, frozen_tick);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_pause_auto_unpauses_after_timeout)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    fixture.client(0).send_pause_request();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    ASSERT_TRUE(fixture.server().paused());
    const std::uint32_t frozen_tick = fixture.server_world().tick_count_;

    now_ms += static_cast<std::uint64_t>(og::sim::PAUSE_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    EXPECT_FALSE(fixture.server().paused());
    EXPECT_GT(fixture.server_world().tick_count_, frozen_tick);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_pause_request_rate_limit_rejects_spam_until_window_expires)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    fixture.client(0).send_pause_request();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    ASSERT_TRUE(fixture.server().paused());

    fixture.client(0).send_pause_response();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    ASSERT_FALSE(fixture.server().paused());

    now_ms += static_cast<std::uint64_t>(og::sim::PAUSE_RATE_LIMIT_MS) - 1u;
    const std::uint32_t pre_rate_limited_tick = fixture.server_world().tick_count_;
    fixture.client(0).send_pause_request();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_FALSE(fixture.server().paused());
    EXPECT_GT(fixture.server_world().tick_count_, pre_rate_limited_tick);

    now_ms += 2u;
    fixture.client(0).send_pause_request();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_TRUE(fixture.server().paused());
}

TEST(NetTransportInProcess,
     network_fixture_level_transition_uses_callbacks_and_waits_for_ready)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    std::size_t save_sync_count = 0;
    std::vector<int> transitioned_levels;
    fixture.with_server_context([&] {
        fixture.server().on_save_sync = [&] { ++save_sync_count; };
        fixture.server().on_level_transition = [&](int level_id) {
            transitioned_levels.push_back(level_id);

            GameWorld& world = fixture.server_world();
            world.id = static_cast<short>(level_id);
            world.current_scenario = static_cast<short>(level_id);
            world.title = "Transitioned Level";
            world.tick_count_ = 0;
            world.reset_level_progress();
            world.game_ended = false;
            world.next_level = -1;
            world.ending = 0;
            world.level_done = 0;
            world.end = 0;
            world.retry = false;
            world.withdraw_requested = false;
            world.withdraw_level = -1;
            world.pending_exit_prompt = false;
            world.paused = false;
            world.pause_player_index = og::sim::kNoPausePlayerIndex;
            world.completed_levels.insert(1);

            // Simulate load-time cosmetic events that should be discarded.
            fixture.server_events().push_notification("loaded new level");
            return true;
        };

        GameWorld& world = fixture.server_world();
        world.game_ended = true;
        world.ending = 0;
        world.next_level = 2;
        fixture.server().broadcast_current_state(
            og::sim::SnapshotCaptureMode::Peek,
            og::sim::EventDeliveryMode::Skip);
    });
    fixture.poll_client_messages(0);

    ASSERT_EQ(1u, save_sync_count);
    ASSERT_EQ(std::vector<int>{2}, transitioned_levels);
    ASSERT_TRUE(fixture.client(0).initial_setup().has_value());
    EXPECT_EQ(2, fixture.client(0).initial_setup()->level_id);
    EXPECT_EQ("Transitioned Level", fixture.client(0).initial_setup()->level_title);
    EXPECT_FALSE(fixture.client(0).baseline().has_value());
    EXPECT_EQ(1u, fixture.client(0).client_ready_count())
        << "level transition should wait for an explicit post-load ready";

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    const bool saw_unready_snapshot = std::any_of(
        fixture.client(0).last_polled_messages().begin(),
        fixture.client(0).last_polled_messages().end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::Snapshot ||
                message.kind == og::sim::TypedReceivedMessageKind::DeltaSnapshot;
        });
    EXPECT_FALSE(saw_unready_snapshot);
    EXPECT_FALSE(fixture.client(0).baseline().has_value());

    fixture.client(0).send_client_ready();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    ASSERT_TRUE(fixture.client(0).baseline().has_value());
    EXPECT_EQ(fixture.server_world().id, fixture.client(0).initial_setup()->level_id);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_level_transition_preserves_same_team_player_bindings)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    ASSERT_NE(nullptr, fixture.server_control(0));
    ASSERT_NE(nullptr, fixture.server_control(1));
    const std::uint32_t player_zero_control_id =
        fixture.server_control(0)->entity_id();
    const std::uint32_t player_one_control_id =
        fixture.server_control(1)->entity_id();
    ASSERT_NE(player_zero_control_id, player_one_control_id);

    fixture.with_server_context([&] {
        fixture.server().on_level_transition = [&](int level_id) {
            GameWorld& world = fixture.server_world();
            world.id = static_cast<short>(level_id);
            world.current_scenario = static_cast<short>(level_id);
            world.title = "Transitioned Level";
            world.tick_count_ = 0;
            world.reset_level_progress();
            world.game_ended = false;
            world.next_level = -1;
            world.ending = 0;
            world.level_done = 0;
            world.end = 0;
            world.retry = false;
            world.withdraw_requested = false;
            world.withdraw_level = -1;
            world.pending_exit_prompt = false;
            world.paused = false;
            world.pause_player_index = og::sim::kNoPausePlayerIndex;

            // A fresh level load recreates controls with no user claims.
            for (auto& uptr : world.oblist)
            {
                walker* const control = uptr.get();
                if (control == nullptr)
                    continue;

                control->set_user(-1);
                control->restore_act_type();
            }
            return true;
        };

        GameWorld& world = fixture.server_world();
        world.game_ended = true;
        world.ending = 0;
        world.next_level = 2;
        fixture.server().broadcast_current_state(
            og::sim::SnapshotCaptureMode::Peek,
            og::sim::EventDeliveryMode::Skip);
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    ASSERT_NE(nullptr, fixture.server_control(0));
    ASSERT_NE(nullptr, fixture.server_control(1));
    EXPECT_EQ(player_zero_control_id, fixture.server_control(0)->entity_id());
    EXPECT_EQ(player_one_control_id, fixture.server_control(1)->entity_id());

    for (std::size_t client_index = 0; client_index < 2; ++client_index)
    {
        ASSERT_TRUE(fixture.client(client_index).initial_setup().has_value());
        EXPECT_EQ(player_zero_control_id,
                  fixture.client(client_index).controlled_entity_ids()[0]);
        EXPECT_EQ(player_one_control_id,
                  fixture.client(client_index).controlled_entity_ids()[1]);
    }

    fixture.client(0).send_client_ready();
    fixture.client(1).send_client_ready();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    ASSERT_TRUE(fixture.client(0).baseline().has_value());
    ASSERT_TRUE(fixture.client(1).baseline().has_value());
    fixture.expect_clients_match_server();
}

// Regression: a level transition that happens while the game is paused (e.g. a
// player opens the pause menu and withdraws/exits to another level) must not
// leave the next level frozen. The server-side pending_pause_state_ /
// pending_exit_prompt_state_ optionals are not visible to the level-load path,
// so prepare_clients_for_loaded_level() must clear them — otherwise step()
// refuses to tick the freshly loaded level and "no one can move".
TEST(NetTransportInProcess,
     network_fixture_level_transition_clears_lingering_pause_state)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    // Pause the game (as the pause menu does before offering withdraw/exit).
    fixture.client(0).send_pause_request();
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);
    ASSERT_TRUE(fixture.server().paused());

    // While paused, a level transition is requested and the next level loads.
    fixture.with_server_context([&] {
        fixture.server().on_level_transition = [&](int level_id) {
            GameWorld& world = fixture.server_world();
            world.id = static_cast<short>(level_id);
            world.current_scenario = static_cast<short>(level_id);
            world.tick_count_ = 0;
            world.reset_level_progress();
            world.game_ended = false;
            world.next_level = -1;
            world.ending = 0;
            world.level_done = 0;
            world.end = 0;
            // A fresh level load clears the *world* pause flag, but cannot touch
            // the server-private pending_pause_state_ optional.
            world.paused = false;
            for (auto& uptr : world.oblist)
            {
                walker* const control = uptr.get();
                if (control == nullptr)
                    continue;
                control->set_user(-1);
                control->restore_act_type();
            }
            return true;
        };

        GameWorld& world = fixture.server_world();
        world.game_ended = true;
        world.ending = 0;
        world.next_level = 2;
        fixture.server().broadcast_current_state(
            og::sim::SnapshotCaptureMode::Peek,
            og::sim::EventDeliveryMode::Skip);
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);

    EXPECT_FALSE(fixture.server().paused())
        << "a level transition must clear lingering server-side pause state";
    EXPECT_FALSE(fixture.server().pending_exit_prompt());

    // The freshly loaded level must actually tick again (players can move).
    fixture.client(0).send_client_ready();
    fixture.client(1).send_client_ready();
    const std::uint32_t tick_before = fixture.server_world().tick_count_;
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);
    fixture.poll_client_messages(1);
    EXPECT_GT(fixture.server_world().tick_count_, tick_before)
        << "world must tick after the transition; lingering pause froze it";
}

TEST(NetTransportInProcess,
     network_fixture_auto_declines_exit_prompt_when_triggering_player_disconnects)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);

    fixture.with_server_context([&] {
        control->set_skip_exit(10);
        fixture.server_events().push_with_text(
            og::sim::EventKind::RequestExitConfirmation,
            "Exit now?",
            5u,
            0u);
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    ASSERT_TRUE(fixture.server().pending_exit_prompt());
    const std::uint32_t frozen_tick = fixture.server_world().tick_count_;

    fixture.with_server_context([&] {
        fixture.server().disconnect_client(
            fixture.client_transport(0).local_peer_id());
    });

    EXPECT_FALSE(fixture.server().pending_exit_prompt());
    EXPECT_FALSE(fixture.server_world().pending_exit_prompt);

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_GT(fixture.server_world().tick_count_, frozen_tick);
}

TEST(NetTransportInProcess, network_fixture_disconnects_client_after_input_timeout)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    InputState empty_input;
    fixture.client(0).send_input(empty_input, 1u);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);
    EXPECT_EQ(1u, fixture.server_transport().connected_peers().size());
    EXPECT_EQ(0, static_cast<int>(control->user()));

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
    });

    EXPECT_TRUE(fixture.server_transport().connected_peers().empty());
    EXPECT_EQ(-1, static_cast<int>(control->user()));
    EXPECT_NE(ACT_CONTROL, control->act_type());
}

TEST(NetTransportInProcess,
     network_fixture_reconnects_with_session_token_and_reclaims_control)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);
    const std::uint32_t control_id = control->entity_id();
    const og::sim::SessionToken session_token = fixture.client(0).session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(session_token));

    const og::sim::PeerId disconnected_peer =
        fixture.client_transport(0).local_peer_id();
    fixture.client_transport(0).disconnect(disconnected_peer);
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
        fixture.server().poll_incoming_messages();
    });

    EXPECT_TRUE(fixture.server_transport().connected_peers().empty());
    EXPECT_EQ(0, static_cast<int>(control->user()));
    EXPECT_EQ(ACT_CONTROL, control->act_type());

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
    });

    EXPECT_EQ(-1, static_cast<int>(control->user()));
    EXPECT_NE(ACT_CONTROL, control->act_type());

    auto reconnect_transport = fixture.server_transport().create_client_transport();
    const og::sim::PeerId reconnect_peer = reconnect_transport->local_peer_id();
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    og::sim::HelloMessage hello;
    hello.session_token = session_token;
    reconnect_transport->send_hello(
        reconnect_peer,
        std::make_shared<og::sim::HelloMessage>(hello));

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    const std::vector<og::sim::TypedReceivedMessage> reconnected_messages =
        reconnect_transport->poll_typed();

    const auto hello_it = std::find_if(
        reconnected_messages.begin(),
        reconnected_messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::Hello &&
                message.hello != nullptr;
        });
    ASSERT_NE(reconnected_messages.end(), hello_it);
    EXPECT_EQ(session_token, hello_it->hello->session_token);

    const auto initial_setup_it = std::find_if(
        reconnected_messages.begin(),
        reconnected_messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::InitialSetup &&
                message.initial_setup != nullptr;
        });
    ASSERT_NE(reconnected_messages.end(), initial_setup_it);
    EXPECT_EQ(control_id, initial_setup_it->initial_setup->controlled_entity_ids[0]);

    const auto control_change_it = std::find_if(
        reconnected_messages.begin(),
        reconnected_messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::ControlChange &&
                message.control_change != nullptr;
        });
    ASSERT_NE(reconnected_messages.end(), control_change_it);
    EXPECT_EQ(0u, control_change_it->control_change->player_index);
    EXPECT_EQ(control_id, control_change_it->control_change->entity_id);

    const auto snapshot_it = std::find_if(
        reconnected_messages.begin(),
        reconnected_messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::Snapshot &&
                message.snapshot != nullptr;
        });
    ASSERT_NE(reconnected_messages.end(), snapshot_it);
    EXPECT_GE(snapshot_it->snapshot->tick_count, 1u);

    EXPECT_EQ(0, static_cast<int>(control->user()));
    EXPECT_EQ(ACT_CONTROL, control->act_type());
}

TEST(NetTransportInProcess,
     network_fixture_staggers_reconnect_keyframes_when_more_than_two_players_reconnect)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 4,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    std::array<og::sim::SessionToken, 4> session_tokens = {};
    for (std::size_t index = 0; index < session_tokens.size(); ++index)
    {
        session_tokens[index] = fixture.client(index).session_token();
        ASSERT_FALSE(og::sim::is_zero_session_token(session_tokens[index]));
    }

    for (std::size_t index = 0; index < session_tokens.size(); ++index)
    {
        const og::sim::PeerId disconnected_peer =
            fixture.client_transport(index).local_peer_id();
        fixture.client_transport(index).disconnect(disconnected_peer);
    }
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
        fixture.server().poll_incoming_messages();
    });

    ASSERT_EQ(4u, fixture.server().disconnected_players().size());

    std::vector<std::shared_ptr<og::sim::InProcessTransport>> reconnect_transports;
    std::vector<og::sim::PeerId> reconnect_peers;
    reconnect_transports.reserve(session_tokens.size());
    reconnect_peers.reserve(session_tokens.size());
    for (std::size_t index = 0; index < session_tokens.size(); ++index)
    {
        auto reconnect_transport =
            fixture.server_transport().create_client_transport();
        reconnect_peers.push_back(reconnect_transport->local_peer_id());
        reconnect_transports.push_back(std::move(reconnect_transport));
    }
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    for (std::size_t index = 0; index < session_tokens.size(); ++index)
    {
        og::sim::HelloMessage hello;
        hello.session_token = session_tokens[index];
        reconnect_transports[index]->send_hello(
            reconnect_peers[index],
            std::make_shared<og::sim::HelloMessage>(hello));
    }

    const auto count_snapshot_messages =
        [](const std::vector<og::sim::TypedReceivedMessage>& messages) {
            return static_cast<std::size_t>(std::count_if(
                messages.begin(),
                messages.end(),
                [](const og::sim::TypedReceivedMessage& message) {
                    return message.kind ==
                            og::sim::TypedReceivedMessageKind::Snapshot &&
                        message.snapshot != nullptr;
                }));
        };

    std::array<bool, 4> saw_snapshot = {};

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    std::size_t first_tick_snapshot_count = 0;
    for (std::size_t index = 0; index < reconnect_transports.size(); ++index)
    {
        const std::vector<og::sim::TypedReceivedMessage> messages =
            reconnect_transports[index]->poll_typed();
        first_tick_snapshot_count += count_snapshot_messages(messages);
        saw_snapshot[index] = count_snapshot_messages(messages) != 0u;
    }
    EXPECT_EQ(2u, first_tick_snapshot_count);

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    std::size_t second_tick_snapshot_count = 0;
    for (std::size_t index = 0; index < reconnect_transports.size(); ++index)
    {
        const std::vector<og::sim::TypedReceivedMessage> messages =
            reconnect_transports[index]->poll_typed();
        second_tick_snapshot_count += count_snapshot_messages(messages);
        saw_snapshot[index] =
            saw_snapshot[index] || count_snapshot_messages(messages) != 0u;
    }
    EXPECT_EQ(2u, second_tick_snapshot_count);
    EXPECT_TRUE(std::all_of(
        saw_snapshot.begin(), saw_snapshot.end(), [](bool value) { return value; }));
}

TEST(NetTransportInProcess,
     network_fixture_reconnects_while_dead_without_restoring_control)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);
    const std::uint32_t control_id = control->entity_id();
    const og::sim::SessionToken session_token = fixture.client(0).session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(session_token));

    const og::sim::PeerId disconnected_peer =
        fixture.client_transport(0).local_peer_id();
    fixture.client_transport(0).disconnect(disconnected_peer);
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
        fixture.server().poll_incoming_messages();
    });

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
        control->set_dead(1);
    });

    auto reconnect_transport = fixture.server_transport().create_client_transport();
    const og::sim::PeerId reconnect_peer = reconnect_transport->local_peer_id();
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    og::sim::HelloMessage hello;
    hello.session_token = session_token;
    reconnect_transport->send_hello(
        reconnect_peer,
        std::make_shared<og::sim::HelloMessage>(hello));

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    const std::vector<og::sim::TypedReceivedMessage> reconnected_messages =
        reconnect_transport->poll_typed();

    const auto initial_setup_it = std::find_if(
        reconnected_messages.begin(),
        reconnected_messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::InitialSetup &&
                message.initial_setup != nullptr;
        });
    ASSERT_NE(reconnected_messages.end(), initial_setup_it);
    EXPECT_EQ(control_id, initial_setup_it->initial_setup->controlled_entity_ids[0]);

    const auto snapshot_it = std::find_if(
        reconnected_messages.begin(),
        reconnected_messages.end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::Snapshot &&
                message.snapshot != nullptr;
        });
    ASSERT_NE(reconnected_messages.end(), snapshot_it);

    const auto alive_control_snapshot_it = std::find_if(
        snapshot_it->snapshot->oblist.begin(),
        snapshot_it->snapshot->oblist.end(),
        [control_id](const og::sim::EntitySnapshot& entity) {
            return entity.entity_id == control_id && entity.dead == 0;
        });
    EXPECT_EQ(snapshot_it->snapshot->oblist.end(), alive_control_snapshot_it);

    const auto control_snapshot_it = std::find_if(
        snapshot_it->snapshot->oblist.begin(),
        snapshot_it->snapshot->oblist.end(),
        [control_id](const og::sim::EntitySnapshot& entity) {
            return entity.entity_id == control_id;
        });
    if (control_snapshot_it != snapshot_it->snapshot->oblist.end())
    {
        EXPECT_NE(0, control_snapshot_it->dead);
        EXPECT_EQ(-1, static_cast<int>(control_snapshot_it->user));
    }

    EXPECT_TRUE(control->dead());
    EXPECT_EQ(-1, static_cast<int>(control->user()));
    EXPECT_NE(ACT_CONTROL, control->act_type());
}

TEST(NetTransportInProcess,
     network_fixture_reconnected_host_retains_timer_wait_authority)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    fixture.server_world().timer_wait = 6;

    const og::sim::SessionToken session_token = fixture.client(0).session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(session_token));

    const og::sim::PeerId disconnected_peer =
        fixture.client_transport(0).local_peer_id();
    fixture.client_transport(0).disconnect(disconnected_peer);
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
        fixture.server().poll_incoming_messages();
    });

    auto reconnect_transport = fixture.server_transport().create_client_transport();
    const og::sim::PeerId reconnect_peer = reconnect_transport->local_peer_id();
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    og::sim::HelloMessage hello;
    hello.session_token = session_token;
    reconnect_transport->send_hello(
        reconnect_peer,
        std::make_shared<og::sim::HelloMessage>(hello));

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    const std::vector<og::sim::TypedReceivedMessage> reconnected_messages =
        reconnect_transport->poll_typed();
    EXPECT_FALSE(reconnected_messages.empty());

    InputState reconnected_host_input{};
    reconnected_host_input.timer_wait_request = 3;
    std::uint32_t tick = fixture.server_world().tick_count_ + 1;
    reconnect_transport->send_input(
        reconnect_peer,
        std::make_shared<InputState>(reconnected_host_input),
        tick);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_EQ(3, fixture.server_world().timer_wait);

    InputState guest_only_input{};
    guest_only_input.timer_wait_request = 11;
    tick = fixture.server_world().tick_count_ + 1;
    fixture.client(1).send_input(guest_only_input, tick);
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_EQ(3, fixture.server_world().timer_wait);
}

TEST(NetTransportInProcess,
     network_fixture_rejects_unknown_session_token_reconnect_during_game)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    og::sim::SessionToken invalid_token = fixture.client(0).session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(invalid_token));
    invalid_token[0] ^= 0xffu;
    if (og::sim::is_zero_session_token(invalid_token))
        invalid_token[0] = 1u;

    auto rejected_transport = fixture.server_transport().create_client_transport();
    const og::sim::PeerId rejected_peer = rejected_transport->local_peer_id();
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    og::sim::HelloMessage hello;
    hello.session_token = invalid_token;
    rejected_transport->send_hello(
        rejected_peer,
        std::make_shared<og::sim::HelloMessage>(hello));

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    EXPECT_TRUE(rejected_transport->connected_peers().empty());
    EXPECT_TRUE(rejected_transport->poll_typed().empty());
    EXPECT_EQ(1u, fixture.server_transport().connected_peers().size());
}

TEST(NetTransportInProcess,
     network_fixture_rejects_expired_session_token_reconnect)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);
    const og::sim::SessionToken session_token = fixture.client(0).session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(session_token));

    const og::sim::PeerId disconnected_peer =
        fixture.client_transport(0).local_peer_id();
    fixture.client_transport(0).disconnect(disconnected_peer);
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
        fixture.server().poll_incoming_messages();
    });

    ASSERT_EQ(1u, fixture.server().disconnected_players().size());

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    EXPECT_EQ(-1, static_cast<int>(control->user()));

    now_ms += static_cast<std::uint64_t>(og::sim::PAUSE_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
    });

    EXPECT_TRUE(fixture.server().disconnected_players().empty());

    auto reconnect_transport = fixture.server_transport().create_client_transport();
    const og::sim::PeerId reconnect_peer = reconnect_transport->local_peer_id();
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    og::sim::HelloMessage hello;
    hello.session_token = session_token;
    reconnect_transport->send_hello(
        reconnect_peer,
        std::make_shared<og::sim::HelloMessage>(hello));

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    EXPECT_TRUE(reconnect_transport->connected_peers().empty());
    EXPECT_TRUE(reconnect_transport->poll_typed().empty());
}

TEST(NetTransportInProcess,
     network_fixture_clears_disconnected_players_on_level_transition)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    const og::sim::SessionToken session_token = fixture.client(0).session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(session_token));

    const og::sim::PeerId disconnected_peer =
        fixture.client_transport(0).local_peer_id();
    fixture.client_transport(0).disconnect(disconnected_peer);
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
        fixture.server().poll_incoming_messages();
    });

    ASSERT_EQ(1u, fixture.server().disconnected_players().size());

    fixture.with_server_context([&] {
        fixture.server().on_level_transition = [&](int level_id) {
            GameWorld& world = fixture.server_world();
            world.id = static_cast<short>(level_id);
            world.current_scenario = static_cast<short>(level_id);
            world.title = "Transitioned Level";
            world.tick_count_ = 0;
            world.reset_level_progress();
            world.game_ended = false;
            world.next_level = -1;
            world.ending = 0;
            world.level_done = 0;
            world.end = 0;
            world.retry = false;
            world.withdraw_requested = false;
            world.withdraw_level = -1;
            world.pending_exit_prompt = false;
            world.paused = false;
            world.pause_player_index = og::sim::kNoPausePlayerIndex;
            return true;
        };

        GameWorld& world = fixture.server_world();
        world.game_ended = true;
        world.ending = 0;
        world.next_level = 2;
        fixture.server().broadcast_current_state(
            og::sim::SnapshotCaptureMode::Peek,
            og::sim::EventDeliveryMode::Skip);
    });

    EXPECT_TRUE(fixture.server().disconnected_players().empty());

    auto reconnect_transport = fixture.server_transport().create_client_transport();
    const og::sim::PeerId reconnect_peer = reconnect_transport->local_peer_id();
    fixture.with_server_context([&] {
        fixture.server().poll_incoming_messages();
    });

    og::sim::HelloMessage hello;
    hello.session_token = session_token;
    reconnect_transport->send_hello(
        reconnect_peer,
        std::make_shared<og::sim::HelloMessage>(hello));

    fixture.with_server_context([&] {
        fixture.server().step();
    });

    EXPECT_TRUE(reconnect_transport->connected_peers().empty());
    EXPECT_TRUE(reconnect_transport->poll_typed().empty());
}

TEST(NetTransportInProcess,
     network_fixture_auto_declines_exit_prompt_when_triggering_player_dies)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);

    fixture.with_server_context([&] {
        control->set_skip_exit(10);
        fixture.server_events().push_with_text(
            og::sim::EventKind::RequestExitConfirmation,
            "Exit now?",
            5u,
            0u);
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    ASSERT_TRUE(fixture.server().pending_exit_prompt());
    const std::uint32_t frozen_tick = fixture.server_world().tick_count_;

    fixture.with_server_context([&] {
        control->set_dead(1);
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_FALSE(fixture.server().pending_exit_prompt());
    EXPECT_GT(fixture.server_world().tick_count_, frozen_tick);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_auto_declines_exit_prompt_after_timeout)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    std::uint64_t now_ms = 1000;
    fixture.server().set_wall_clock_ms_source([&] { return now_ms; });

    fixture.with_server_context([&] {
        fixture.server_events().push_with_text(
            og::sim::EventKind::RequestExitConfirmation,
            "Exit now?",
            5u,
            0u);
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    ASSERT_TRUE(fixture.server().pending_exit_prompt());
    const std::uint32_t frozen_tick = fixture.server_world().tick_count_;

    now_ms += static_cast<std::uint64_t>(og::sim::EXIT_PROMPT_TIMEOUT_MS) + 1u;
    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_FALSE(fixture.server().pending_exit_prompt());
    EXPECT_GT(fixture.server_world().tick_count_, frozen_tick);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_detects_snapshot_hash_mismatch_and_resends_keyframe)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();
    fixture.step_ticks(1);

    walker* const control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);

    fixture.with_client_context(0, [&] {
        walker* const mirror =
            fixture.client_world(0).find_by_id(control->entity_id());
        ASSERT_NE(nullptr, mirror);
        mirror->set_xpos(static_cast<short>(mirror->xpos() + GRID_SIZE));
        fixture.client(0).send_snapshot_hash_check();
    });

    fixture.with_server_context([&] {
        fixture.server().step();
    });
    fixture.poll_client_messages(0);

    EXPECT_GE(fixture.server().snapshot_hash_mismatch_count(), 1u);
    const bool saw_keyframe = std::any_of(
        fixture.client(0).last_polled_messages().begin(),
        fixture.client(0).last_polled_messages().end(),
        [](const og::sim::TypedReceivedMessage& message) {
            return message.kind == og::sim::TypedReceivedMessageKind::Snapshot;
        });
    EXPECT_TRUE(saw_keyframe);
    fixture.expect_clients_match_server();
}

TEST(NetTransportInProcess,
     network_fixture_replicates_server_grid_mutation_to_client)
{
    og::sim::test::NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .tick_count = 0,
        .validate_serialization = true,
        .input_sequence = {},
    });

    fixture.load_level();
    fixture.initial_sync();

    const auto [tile_x, tile_y] = find_damageable_grid_tile(fixture.server_world());
    const unsigned char client_before =
        read_grid_tile(fixture.client_world(0), tile_x, tile_y);
    const unsigned char server_after = static_cast<unsigned char>(
        fixture.server_world().damage_tile(
            static_cast<short>(tile_x * GRID_SIZE),
            static_cast<short>(tile_y * GRID_SIZE)));
    ASSERT_NE(client_before, server_after)
        << "server grid mutation should change the tile value";

    fixture.step_ticks(1);

    EXPECT_EQ(server_after,
              read_grid_tile(fixture.client_world(0), tile_x, tile_y));
    fixture.expect_clients_match_server();
}
