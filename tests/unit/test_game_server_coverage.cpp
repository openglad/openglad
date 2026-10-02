#include <openglad/gameplay/damage_number_event.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/core/constants.h>
#include <openglad/core/sound_ids.h>
#include <openglad/resources/packs.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

namespace {

class CoverageTransport final : public og::sim::ITransport
{
public:
    explicit CoverageTransport(bool typed = false)
        : typed_(typed)
    {
    }

    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_.push_back({peer_id, {data, data + len}});
    }

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> result = std::move(raw_inbound_);
        raw_inbound_.clear();
        return result;
    }

    bool supports_typed_messages() const noexcept override
    {
        return typed_;
    }

    std::vector<og::sim::TypedReceivedMessage> poll_typed() override
    {
        std::vector<og::sim::TypedReceivedMessage> result =
            std::move(typed_inbound_);
        typed_inbound_.clear();
        return result;
    }

    void accept_connections() override {}

    void disconnect(og::sim::PeerId peer_id) override
    {
        disconnected_.push_back(peer_id);
        std::erase(connected_, peer_id);
    }

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return connected_;
    }

    void set_connected(std::vector<og::sim::PeerId> peers)
    {
        std::sort(peers.begin(), peers.end());
        peers.erase(std::unique(peers.begin(), peers.end()), peers.end());
        connected_ = std::move(peers);
    }

    void queue_raw(og::sim::PeerId peer_id, std::vector<std::uint8_t> bytes)
    {
        raw_inbound_.push_back({peer_id, std::move(bytes)});
    }

    void queue_typed(og::sim::TypedReceivedMessage message)
    {
        typed_inbound_.push_back(std::move(message));
    }

    const std::vector<og::sim::ReceivedMessage>& sent() const noexcept
    {
        return sent_;
    }

    void clear_sent()
    {
        sent_.clear();
    }

    const std::vector<og::sim::PeerId>& disconnected() const noexcept
    {
        return disconnected_;
    }

private:
    bool typed_ = false;
    std::vector<og::sim::PeerId> connected_;
    std::vector<og::sim::ReceivedMessage> raw_inbound_;
    std::vector<og::sim::TypedReceivedMessage> typed_inbound_;
    std::vector<og::sim::ReceivedMessage> sent_;
    std::vector<og::sim::PeerId> disconnected_;
};

std::vector<std::uint8_t> malformed_message_for_type(std::uint8_t type)
{
    std::vector<std::uint8_t> bytes;
    switch (type)
    {
    case og::sim::kLobbyMessageType:
    {
        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyLeaveMessage{.player_index = 2u};
        bytes = og::sim::serialize_lobby_message(message);
        break;
    }
    case og::sim::kLobbyStateMessageType:
        bytes = og::sim::serialize_lobby_state_message({});
        break;
    case og::sim::kInputMessageType:
    {
        const auto encoded = og::sim::serialize_input(17u, InputState{});
        bytes.assign(encoded.begin(), encoded.end());
        break;
    }
    case og::sim::kHelloMessageType:
    {
        const auto encoded = og::sim::serialize_hello(og::sim::HelloMessage{});
        bytes.assign(encoded.begin(), encoded.end());
        break;
    }
    case og::sim::kClientReadyMessageType:
        bytes = og::sim::serialize_client_ready_message({.last_applied_tick = 8u});
        break;
    case og::sim::kKeyframeRequestMessageType:
        bytes = og::sim::serialize_keyframe_request_message({.last_seen_tick = 9u});
        break;
    case og::sim::kHeartbeatMessageType:
        bytes = og::sim::serialize_heartbeat_message({});
        break;
    case og::sim::kExitPromptResponseMessageType:
        bytes = og::sim::serialize_exit_prompt_response_message({.accepted = true});
        break;
    case og::sim::kPauseBroadcastMessageType:
        bytes = og::sim::serialize_pause_broadcast_message(
            {.player_index = 1u, .player_name = "Player Two"});
        break;
    case og::sim::kPauseResponseMessageType:
        bytes = og::sim::serialize_pause_response_message({.resume = true});
        break;
    case og::sim::kSnapshotHashCheckMessageType:
        bytes = og::sim::serialize_snapshot_hash_check_message(
            {.tick = 10u, .snapshot_hash = 0x12345678u});
        break;
    case og::sim::kPackRequestMessageType:
        bytes = og::sim::serialize_pack_request_message({.pack_id = "mods"});
        break;
    default:
        return {};
    }

    // Heartbeat has an intentionally empty payload. Give it an unadvertised
    // trailing byte; every other message is made incomplete by truncation.
    // In both cases the outer envelope remains readable and routes through the
    // type-specific server decoder before that decoder rejects the payload.
    if (type == og::sim::kHeartbeatMessageType)
        bytes.push_back(0xa5u);
    else
        bytes.pop_back();
    return bytes;
}

bool type_specific_decoder_rejects(std::uint8_t type,
                                   const std::vector<std::uint8_t>& bytes)
{
    switch (type)
    {
    case og::sim::kLobbyMessageType:
        return !og::sim::deserialize_lobby_message(bytes).has_value();
    case og::sim::kLobbyStateMessageType:
        return !og::sim::deserialize_lobby_state_message(bytes).has_value();
    case og::sim::kInputMessageType:
        return !og::sim::deserialize_input_message(bytes).has_value();
    case og::sim::kHelloMessageType:
        return !og::sim::deserialize_hello_message(bytes).has_value();
    case og::sim::kClientReadyMessageType:
        return !og::sim::deserialize_client_ready_message(bytes).has_value();
    case og::sim::kKeyframeRequestMessageType:
        return !og::sim::deserialize_keyframe_request_message(bytes).has_value();
    case og::sim::kHeartbeatMessageType:
        return !og::sim::deserialize_heartbeat_message(bytes).has_value();
    case og::sim::kExitPromptResponseMessageType:
        return !og::sim::deserialize_exit_prompt_response_message(bytes).has_value();
    case og::sim::kPauseBroadcastMessageType:
        return !og::sim::deserialize_pause_broadcast_message(bytes).has_value();
    case og::sim::kPauseResponseMessageType:
        return !og::sim::deserialize_pause_response_message(bytes).has_value();
    case og::sim::kSnapshotHashCheckMessageType:
        return !og::sim::deserialize_snapshot_hash_check_message(bytes).has_value();
    case og::sim::kPackRequestMessageType:
        return !og::sim::deserialize_pack_request_message(bytes).has_value();
    default:
        return false;
    }
}

std::optional<og::sim::HelloMessage> find_hello(
    const CoverageTransport& transport,
    og::sim::PeerId peer_id)
{
    for (const auto& sent : transport.sent())
    {
        if (sent.peer_id != peer_id)
            continue;
        if (auto hello = og::sim::deserialize_hello_message(sent.data))
            return hello;
    }
    return std::nullopt;
}

std::optional<og::sim::InitialSetupMessage> find_initial_setup(
    const CoverageTransport& transport,
    og::sim::PeerId peer_id)
{
    for (const auto& sent : transport.sent())
    {
        if (sent.peer_id != peer_id)
            continue;
        if (auto setup = og::sim::deserialize_initial_setup_message(sent.data))
            return setup;
    }
    return std::nullopt;
}

std::optional<og::sim::ExitPromptBroadcastMessage> find_exit_prompt(
    const CoverageTransport& transport,
    og::sim::PeerId peer_id)
{
    for (const auto& sent : transport.sent())
    {
        if (sent.peer_id != peer_id)
            continue;
        if (auto prompt =
                og::sim::deserialize_exit_prompt_broadcast_message(sent.data))
        {
            return prompt;
        }
    }
    return std::nullopt;
}

std::optional<og::sim::PauseBroadcastMessage> find_pause_broadcast(
    const CoverageTransport& transport,
    og::sim::PeerId peer_id)
{
    for (const auto& sent : transport.sent())
    {
        if (sent.peer_id != peer_id)
            continue;
        if (auto pause = og::sim::deserialize_pause_broadcast_message(sent.data))
            return pause;
    }
    return std::nullopt;
}

std::optional<og::sim::ControlChangeMessage> find_control_change(
    const CoverageTransport& transport,
    og::sim::PeerId peer_id)
{
    for (const auto& sent : transport.sent())
    {
        if (sent.peer_id != peer_id)
            continue;
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(sent.data, envelope))
            continue;
        if (envelope.message_type != og::sim::kControlChangeMessageType)
            continue;
        if (auto change = og::sim::deserialize_control_change_message(sent.data))
            return change;
    }
    return std::nullopt;
}

std::optional<og::sim::SimEventBatch> find_sim_event_batch(
    const CoverageTransport& transport,
    og::sim::PeerId peer_id)
{
    for (const auto& sent : transport.sent())
    {
        if (sent.peer_id != peer_id)
            continue;
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(sent.data, envelope))
            continue;
        if (envelope.message_type != og::sim::kSimEventBatchMessageType)
            continue;
        return og::sim::deserialize_sim_event_batch(sent.data);
    }
    return std::nullopt;
}

TEST(GameServerCoverage, malformed_payloads_disconnect_each_raw_protocol_peer)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    constexpr std::array<std::uint8_t, 12> message_types = {
        og::sim::kLobbyMessageType,
        og::sim::kLobbyStateMessageType,
        og::sim::kInputMessageType,
        og::sim::kHelloMessageType,
        og::sim::kClientReadyMessageType,
        og::sim::kKeyframeRequestMessageType,
        og::sim::kHeartbeatMessageType,
        og::sim::kExitPromptResponseMessageType,
        og::sim::kPauseBroadcastMessageType,
        og::sim::kPauseResponseMessageType,
        og::sim::kSnapshotHashCheckMessageType,
        og::sim::kPackRequestMessageType,
    };

    std::vector<og::sim::PeerId> peers;
    for (std::size_t index = 0; index < message_types.size(); ++index)
        peers.push_back(static_cast<og::sim::PeerId>(101u + index));
    transport.set_connected(peers);
    server.poll_incoming_messages();

    for (std::size_t index = 0; index < message_types.size(); ++index)
    {
        const std::uint8_t type = message_types[index];
        std::vector<std::uint8_t> malformed = malformed_message_for_type(type);
        og::sim::TransportEnvelope envelope;
        ASSERT_TRUE(og::sim::decode_transport_envelope(malformed, envelope));
        EXPECT_EQ(type, envelope.message_type);
        ASSERT_TRUE(type_specific_decoder_rejects(type, malformed));
        transport.queue_raw(peers[index], std::move(malformed));
    }

    server.poll_incoming_messages();

    EXPECT_EQ(peers, transport.disconnected());
    EXPECT_TRUE(server.last_polled_messages().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
}

TEST(GameServerCoverage, raw_exit_and_pause_responses_preserve_payloads)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({31u, 32u});
    server.poll_incoming_messages();

    const og::sim::ExitPromptResponseMessage exit{
        .accepted = true,
        .abort_request = false,
    };
    const og::sim::PauseResponseMessage pause{.resume = false};
    transport.queue_raw(31u, og::sim::serialize_exit_prompt_response_message(exit));
    transport.queue_raw(32u, og::sim::serialize_pause_response_message(pause));

    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.last_polled_messages().size());
    const auto& decoded_exit = server.last_polled_messages()[0];
    EXPECT_EQ(31u, decoded_exit.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::ExitPromptResponse,
              decoded_exit.kind);
    ASSERT_NE(nullptr, decoded_exit.exit_prompt_response);
    EXPECT_EQ(exit, *decoded_exit.exit_prompt_response);

    const auto& decoded_pause = server.last_polled_messages()[1];
    EXPECT_EQ(32u, decoded_pause.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PauseResponse,
              decoded_pause.kind);
    ASSERT_NE(nullptr, decoded_pause.pause_response);
    EXPECT_EQ(pause, *decoded_pause.pause_response);
    EXPECT_TRUE(transport.disconnected().empty());
}

TEST(GameServerCoverage, typed_malformed_marker_suppresses_later_peer_messages)
{
    TestGameWorld fixture;
    CoverageTransport transport(/*typed=*/true);
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({41u, 42u});
    server.poll_incoming_messages();
    og::sim::TypedReceivedMessage malformed;
    malformed.peer_id = 41u;
    malformed.kind = og::sim::TypedReceivedMessageKind::Malformed;
    transport.queue_typed(std::move(malformed));

    og::sim::TypedReceivedMessage suppressed_heartbeat;
    suppressed_heartbeat.peer_id = 41u;
    suppressed_heartbeat.kind = og::sim::TypedReceivedMessageKind::Heartbeat;
    suppressed_heartbeat.heartbeat =
        std::make_shared<og::sim::HeartbeatMessage>();
    transport.queue_typed(std::move(suppressed_heartbeat));

    og::sim::TypedReceivedMessage surviving_heartbeat;
    surviving_heartbeat.peer_id = 42u;
    surviving_heartbeat.kind = og::sim::TypedReceivedMessageKind::Heartbeat;
    surviving_heartbeat.heartbeat =
        std::make_shared<og::sim::HeartbeatMessage>();
    transport.queue_typed(std::move(surviving_heartbeat));

    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{41u}), transport.disconnected());
    ASSERT_EQ(1u, server.last_polled_messages().size());
    EXPECT_EQ(42u, server.last_polled_messages().front().peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Heartbeat,
              server.last_polled_messages().front().kind);
}

TEST(GameServerCoverage, snapshot_accumulation_coalesces_tiles_and_high_mask_bits)
{
    og::sim::PerClientState state;
    og::sim::WorldSnapshot baseline;
    baseline.tick_count = 1u;
    baseline.oblist.push_back({.entity_id = 10u});
    baseline.oblist.push_back({.entity_id = 11u});
    og::sim::seed_client_snapshot_baseline(state, baseline);

    // Model an entity discovered earlier in the same accumulation window. It
    // must remain a full resend and must not also acquire a redundant mask.
    state.new_entity_ids.push_back(11u);

    og::sim::WorldSnapshot first;
    first.tick_count = 2u;
    first.grid_dirty = true;
    first.grid_dirty_tiles.push_back({.x = 3, .y = 4, .value = 7u});
    og::sim::EntitySnapshot high_mask_entity;
    high_mask_entity.entity_id = 10u;
    high_mask_entity.dirty_mask[0] = 0u;
    high_mask_entity.dirty_mask[1] = 1ULL << 5;
    first.oblist.push_back(high_mask_entity);
    og::sim::EntitySnapshot already_new_entity;
    already_new_entity.entity_id = 11u;
    already_new_entity.dirty_mask[0] = 1ULL << 7;
    already_new_entity.dirty_mask[1] = 0u;
    first.oblist.push_back(already_new_entity);
    og::sim::accumulate_snapshot_for_client(state, first);

    og::sim::WorldSnapshot latest = first;
    latest.tick_count = 3u;
    latest.grid_dirty_tiles.front().value = 9u;
    og::sim::accumulate_snapshot_for_client(state, latest);

    ASSERT_FALSE(state.accumulated_dirty.contains(11u));
    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(state, latest);

    ASSERT_EQ(1u, delta.grid_dirty_tiles.size());
    EXPECT_EQ(3, delta.grid_dirty_tiles.front().x);
    EXPECT_EQ(4, delta.grid_dirty_tiles.front().y);
    EXPECT_EQ(9u, delta.grid_dirty_tiles.front().value);
    ASSERT_EQ(2u, delta.oblist.size());
    EXPECT_EQ(0u, delta.oblist[0].dirty_mask[0]);
    EXPECT_EQ(1ULL << 5, delta.oblist[0].dirty_mask[1]);
    EXPECT_EQ(~0ULL, delta.oblist[1].dirty_mask[0]);
    EXPECT_EQ(~0ULL, delta.oblist[1].dirty_mask[1]);
    EXPECT_EQ(3u, state.last_sent_tick);
    EXPECT_TRUE(state.accumulated_dirty.empty());
    EXPECT_TRUE(state.new_entity_ids.empty());
    EXPECT_TRUE(state.pending_grid_dirty_tiles.empty());
}

TEST(GameServerCoverage, consuming_initial_snapshot_deduplicates_guy_records)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    guy shared(FAMILY_SOLDIER);
    shared.id = 31415;
    // 11-char max: guy names over the disk width clamp on the wire read.
    shared.name = "Shared Rec";
    walker* first = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* second = fixture.world().add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    first->myguy = &shared;
    second->myguy = &shared;

    server.connect_client(61u);
    server.send_initial_snapshot(61u, og::sim::SnapshotCaptureMode::Consume);

    const auto setup = find_initial_setup(transport, 61u);
    ASSERT_TRUE(setup.has_value());
    ASSERT_EQ(1u, setup->guys.size());
    EXPECT_EQ(shared.id, setup->guys.front().guy_id);
    EXPECT_EQ(shared.name, setup->guys.front().name);

    // A sufficiently newer hash expectation prunes the obsolete one rather
    // than retaining an unbounded per-client history.
    transport.clear_sent();
    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS * 2u + 1u;
    server.send_initial_snapshot(61u, og::sim::SnapshotCaptureMode::Consume);
    const auto second_setup = find_initial_setup(transport, 61u);
    ASSERT_TRUE(second_setup.has_value());
    EXPECT_EQ(setup->guys, second_setup->guys);

    first->myguy = nullptr;
    second->myguy = nullptr;
}

TEST(GameServerCoverage, bind_player_rejects_global_and_local_index_overflow)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    try
    {
        server.bind_player(71u, og::sim::kMaxGlobalPlayers, 0);
        FAIL() << "global index overflow must throw";
    }
    catch (const std::out_of_range& error)
    {
        EXPECT_STREQ("GameServer player index exceeds kMaxGlobalPlayers",
                     error.what());
    }

    try
    {
        server.bind_player(71u, 0u, 0, nullptr,
                           static_cast<std::uint8_t>(MAX_PLAYERS));
        FAIL() << "local slot overflow must throw";
    }
    catch (const std::out_of_range& error)
    {
        EXPECT_STREQ("GameServer local slot exceeds MAX_PLAYERS", error.what());
    }
}

TEST(GameServerCoverage, player_and_spectator_reject_mismatched_session_tokens)
{
    {
        TestGameWorld fixture;
        CoverageTransport transport;
        og::sim::GameServer server(fixture.world(), fixture.events, transport);
        walker* control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, control);
        server.connect_client(81u);
        server.bind_player(81u, 0u, fixture.world().my_team, control);

        const auto zero_hello = og::sim::serialize_hello(og::sim::HelloMessage{});
        transport.queue_raw(81u, {zero_hello.begin(), zero_hello.end()});
        server.step();
        const auto response = find_hello(transport, 81u);
        ASSERT_TRUE(response.has_value());
        ASSERT_FALSE(og::sim::is_zero_session_token(response->session_token));

        transport.clear_sent();
        og::sim::HelloMessage wrong = *response;
        wrong.session_token[0] ^= 0xffu;
        const auto wrong_bytes = og::sim::serialize_hello(wrong);
        transport.queue_raw(81u, {wrong_bytes.begin(), wrong_bytes.end()});
        server.step();
        EXPECT_EQ((std::vector<og::sim::PeerId>{81u}),
                  transport.disconnected());
    }

    {
        TestGameWorld fixture;
        CoverageTransport transport;
        og::sim::GameServer server(fixture.world(), fixture.events, transport);
        server.connect_spectator(82u);

        const auto zero_hello = og::sim::serialize_hello(og::sim::HelloMessage{});
        transport.queue_raw(82u, {zero_hello.begin(), zero_hello.end()});
        server.step();
        const auto response = find_hello(transport, 82u);
        ASSERT_TRUE(response.has_value());
        ASSERT_FALSE(og::sim::is_zero_session_token(response->session_token));

        transport.clear_sent();
        og::sim::HelloMessage wrong = *response;
        wrong.session_token.back() ^= 0xffu;
        const auto wrong_bytes = og::sim::serialize_hello(wrong);
        transport.queue_raw(82u, {wrong_bytes.begin(), wrong_bytes.end()});
        server.step();
        EXPECT_EQ((std::vector<og::sim::PeerId>{82u}),
                  transport.disconnected());
    }
}

// The pause-menu server contract (docs/pause-menu-design.md §2.1): a repeat
// request from the pause OWNER refreshes the auto-resume deadline (the menu's
// ~20s keep-alive), a non-owner repeat stays rejected, the host peer is
// exempt from the anti-grief rate limit, and remote peers keep it.
TEST(GameServerCoverage, pause_keepalive_owner_refresh_and_host_exemption)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    // Sorted discovery order: 91 connects first and becomes the host peer.
    transport.set_connected({91u, 92u});
    server.poll_incoming_messages();

    walker* host_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* remote_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, host_control);
    ASSERT_NE(nullptr, remote_control);
    host_control->set_user(0);
    host_control->set_act_type(ACT_CONTROL);
    remote_control->set_user(1);
    remote_control->set_act_type(ACT_CONTROL);
    server.bind_player(91u, 0u, fixture.world().my_team, host_control);
    server.bind_player(92u, 1u, fixture.world().my_team, remote_control);

    const auto request_pause = [&](og::sim::PeerId peer) {
        transport.queue_raw(
            peer, og::sim::serialize_pause_broadcast_message(
                      {.player_index = 0u, .player_name = ""}));
        server.step();
    };
    const auto respond_resume = [&](og::sim::PeerId peer) {
        transport.queue_raw(
            peer,
            og::sim::serialize_pause_response_message({.resume = true}));
        server.step();
    };

    // The rate-limit phases run FIRST at small clock deltas: any unpaused
    // step more than DISCONNECT_TIMEOUT_MS past the last input would
    // otherwise input-starve both idle peers into a transport disconnect
    // (which also resets host_peer_id_). The keep-alive phase's large jumps
    // all happen while paused (timeouts suspended) and come last.

    // Host exemption: pause, resume, and immediately re-pause inside
    // PAUSE_RATE_LIMIT_MS (closing and reopening the pause menu).
    now_ms = 1'000;
    request_pause(91u);
    ASSERT_TRUE(server.paused());
    EXPECT_EQ(0u, fixture.world().pause_player_index);
    now_ms = 1'100;
    respond_resume(91u);
    ASSERT_FALSE(server.paused());
    now_ms = 1'200;
    request_pause(91u);
    EXPECT_TRUE(server.paused())
        << "the host peer is exempt from the pause rate limit";
    now_ms = 1'300;
    respond_resume(91u);
    ASSERT_FALSE(server.paused());

    // Remote peers keep the anti-grief limit: an immediate re-pause inside
    // the window is rejected, and accepted once the window expires.
    now_ms = 1'400;
    request_pause(92u);
    ASSERT_TRUE(server.paused());
    EXPECT_EQ(1u, fixture.world().pause_player_index);
    now_ms = 1'500;
    respond_resume(92u);
    ASSERT_FALSE(server.paused());
    now_ms = 1'600;
    request_pause(92u);
    EXPECT_FALSE(server.paused())
        << "remote peers keep the pause rate limit";
    now_ms = 1'400 + og::sim::PAUSE_RATE_LIMIT_MS + 1;
    request_pause(92u);
    ASSERT_TRUE(server.paused());
    EXPECT_EQ(1u, fixture.world().pause_player_index);

    // A NON-owner repeat (host peer 91) is rejected while 92's pause is
    // pending: owner unchanged, and — proven below — the auto-resume
    // deadline is NOT refreshed by it.
    const std::uint64_t owner_pause_at = now_ms;
    now_ms = owner_pause_at + 5'000;
    request_pause(91u);
    ASSERT_TRUE(server.paused());
    EXPECT_EQ(1u, fixture.world().pause_player_index);

    // The owner's repeat is the keep-alive: it refreshes the deadline.
    const std::uint64_t keepalive_at = owner_pause_at + 30'000;
    now_ms = keepalive_at;
    request_pause(92u);
    ASSERT_TRUE(server.paused());

    // Past the original deadline and the non-owner repeat's would-be
    // deadline, inside the refreshed one.
    now_ms = owner_pause_at + og::sim::PAUSE_TIMEOUT_MS + 10'000;
    server.step();
    EXPECT_TRUE(server.paused())
        << "the owner keep-alive must defeat the 60s auto-unpause";

    now_ms = keepalive_at + og::sim::PAUSE_TIMEOUT_MS + 100;
    server.step();
    EXPECT_FALSE(server.paused())
        << "an unrefreshed pause still auto-resumes";
}

TEST(GameServerCoverage, disconnecting_pause_owner_clears_authoritative_pause)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({91u});
    server.poll_incoming_messages();

    walker* control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    server.bind_player(91u, 0u, fixture.world().my_team, control);

    transport.queue_raw(
        91u, og::sim::serialize_pause_broadcast_message(
                 {.player_index = 0u, .player_name = ""}));
    server.step();

    EXPECT_TRUE(server.paused());
    EXPECT_TRUE(fixture.world().paused);
    const auto pause = find_pause_broadcast(transport, 91u);
    ASSERT_TRUE(pause.has_value());
    EXPECT_EQ(0u, pause->player_index);

    transport.set_connected({});
    server.poll_incoming_messages();

    EXPECT_FALSE(server.paused());
    EXPECT_FALSE(fixture.world().paused);
    ASSERT_EQ(1u, server.disconnected_players().size());
    EXPECT_EQ(0u, server.disconnected_players().front().player_index);
    EXPECT_EQ(control, server.disconnected_players().front().control);

    // A spectator holds no seat, so handle_pause_request's binding gate
    // refuses its pause outright: a watcher must not be able to freeze the
    // match for the players. The paused/unpaused pair above is the control
    // that proves the same message DOES pause when it comes from a seat.
    transport.set_connected({92u});
    server.poll_incoming_messages();
    server.connect_spectator(92u);
    transport.clear_sent();
    transport.queue_raw(
        92u, og::sim::serialize_pause_broadcast_message(
                 {.player_index = 0u, .player_name = ""}));
    server.step();

    EXPECT_FALSE(server.paused());
    EXPECT_FALSE(fixture.world().paused);
    EXPECT_FALSE(find_pause_broadcast(transport, 92u).has_value());
}

// #239 launch gate bound: a seeded client that never confirms ready holds the
// level start, so it must be CUT exactly one DISCONNECT_TIMEOUT_MS window
// after its keyframe — even while its heartbeats keep refreshing the input
// starvation clock (which is why the starvation clause alone cannot bound
// the gate). Local (same-process) peers stay exempt, as with every cut.
TEST(GameServerCoverage, launch_gate_ready_deadline_cuts_a_dead_handshake)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    transport.set_connected({91u, 92u});
    server.poll_incoming_messages();

    walker* const remote_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const local_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, remote_control);
    ASSERT_NE(nullptr, local_control);
    server.bind_player(91u, 0u, fixture.world().my_team, remote_control);
    server.bind_player(92u, 1u, fixture.world().my_team, local_control);
    server.mark_peer_local(92u);

    // Seed both at level start; neither confirms.
    server.send_initial_snapshot(91u, og::sim::SnapshotCaptureMode::Peek);
    server.send_initial_snapshot(92u, og::sim::SnapshotCaptureMode::Peek);

    // The gate holds tick 1 while the handshakes are outstanding.
    server.step();
    server.step();
    EXPECT_EQ(0u, fixture.world().tick_count_)
        << "seeded-but-unready clients must hold the level start";

    // A heartbeat keeps 91's input-starvation clock fresh but must not feed
    // the ready deadline.
    now_ms = 6'000;
    transport.queue_raw(
        91u, og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    server.step();
    EXPECT_EQ(0u, fixture.world().tick_count_);
    EXPECT_TRUE(transport.disconnected().empty());

    // One full window after the keyframe (stamped at now=1000), the dead
    // handshake is cut; the local peer survives.
    now_ms = 1'000 + og::sim::DISCONNECT_TIMEOUT_MS;
    server.step();
    EXPECT_EQ((std::vector<og::sim::PeerId>{91u}), transport.disconnected())
        << "the ready deadline must cut the remote dead handshake only";

    // The surviving local peer confirms; the gate opens and tick 1 runs.
    transport.queue_raw(
        92u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    server.step();
    EXPECT_EQ(1u, fixture.world().tick_count_)
        << "the confirmed handshake must release the level start";
}

// The TRANSITION-limbo bound (staged-lobby review finding): after a level
// reload, prepare_clients_for_loaded_level re-sends InitialSetup and resets
// has_initial_snapshot — the client is owed a ClientReady BEFORE any keyframe
// goes out, so the seeded-client deadline (stamped only at keyframe send)
// never started for it, and its heartbeats kept the input-starvation clock
// fresh. One wedged or hostile peer could freeze every level transition
// forever for everyone. The deadline is now stamped at the re-setup itself:
// a limbo peer that heartbeats but never re-readies is cut one full
// DISCONNECT_TIMEOUT_MS window after the reload, and the reloaded level's
// tick 1 runs for the survivors.
TEST(GameServerCoverage,
     launch_gate_transition_limbo_deadline_cuts_a_wedged_peer)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    transport.set_connected({91u, 92u});
    server.poll_incoming_messages();

    walker* const wedged_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const benign_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, wedged_control);
    ASSERT_NE(nullptr, benign_control);
    server.bind_player(91u, 0u, fixture.world().my_team, wedged_control);
    server.bind_player(92u, 1u, fixture.world().my_team, benign_control);

    // Level start: both peers complete the handshake and tick 1 runs.
    server.send_initial_snapshot(91u, og::sim::SnapshotCaptureMode::Peek);
    server.send_initial_snapshot(92u, og::sim::SnapshotCaptureMode::Peek);
    transport.queue_raw(
        91u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    transport.queue_raw(
        92u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    server.step();
    ASSERT_EQ(1u, fixture.world().level_tick_count());

    // Peer 92's "quit this mission" reloads the current level; the hook
    // resets it to its start (the dedicated withdraw_headless_level shape).
    server.on_withdraw_accepted = [&](int /*destination*/) {
        fixture.world().tick_count_ = 0;
        fixture.world().reset_level_progress();
        return true;
    };
    now_ms = 2'000;
    transport.queue_raw(
        92u,
        og::sim::serialize_exit_prompt_response_message(
            {.accepted = true, .abort_request = true}));
    server.step();
    ASSERT_EQ(0u, fixture.world().level_tick_count())
        << "the reload must be back at its level start, gate closed";

    // 92 completes the re-handshake (ready -> catch-up keyframe -> re-ready).
    transport.queue_raw(
        92u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    server.step();
    transport.queue_raw(
        92u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    server.step();
    EXPECT_EQ(0u, fixture.world().level_tick_count())
        << "peer 91's outstanding re-handshake must still hold the reload";

    // Both peers heartbeat just inside the window: the starvation clocks are
    // fresh, so ONLY the transition-limbo ready deadline can bound peer 91.
    now_ms = 2'000 + og::sim::DISCONNECT_TIMEOUT_MS - 1;
    transport.queue_raw(
        91u, og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    transport.queue_raw(
        92u, og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    server.step();
    EXPECT_TRUE(transport.disconnected().empty())
        << "inside the window nothing may be cut";
    EXPECT_EQ(0u, fixture.world().level_tick_count());

    // One full window after the re-setup, the wedged limbo handshake is cut
    // and the reloaded level starts for the survivor.
    now_ms = 2'000 + og::sim::DISCONNECT_TIMEOUT_MS;
    server.step();
    EXPECT_EQ((std::vector<og::sim::PeerId>{91u}), transport.disconnected())
        << "the re-setup deadline must cut the never-readying limbo peer";
    EXPECT_EQ(1u, fixture.world().level_tick_count())
        << "cutting the limbo peer must open the gate for the ready survivor";
}

// The launch-gate handshake pump's budget arms: budgeted catch-up keyframes
// (a KeyframeRequest raised mid-transition) are bounded at
// kMaxKeyframesPerTick per gate step — the third requester is deferred with
// force_keyframe latched and served the very next step — and a NEW bound
// peer arriving mid-gate is seeded with its full initial snapshot through
// the same pump.
TEST(GameServerCoverage, launch_gate_budgets_catchup_keyframes_per_step)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    transport.set_connected({91u, 92u, 93u});
    server.poll_incoming_messages();

    std::array<walker*, 3> controls{};
    for (std::size_t i = 0; i < controls.size(); ++i)
    {
        controls[i] = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, controls[i]);
        server.bind_player(static_cast<og::sim::PeerId>(91u + i),
                           static_cast<std::uint8_t>(i),
                           fixture.world().my_team, controls[i]);
    }

    // Level start: all three complete the handshake and tick 1 runs.
    for (og::sim::PeerId peer = 91u; peer <= 93u; ++peer)
    {
        server.send_initial_snapshot(peer, og::sim::SnapshotCaptureMode::Peek);
        transport.queue_raw(peer, og::sim::serialize_client_ready_message(
                                      og::sim::ClientReadyMessage{}));
    }
    server.step();
    ASSERT_EQ(1u, fixture.world().level_tick_count());

    // A quit-mission reload puts every client into the transition limbo
    // (setup re-sent, keyframe owed).
    server.on_withdraw_accepted = [&](int /*destination*/) {
        fixture.world().tick_count_ = 0;
        fixture.world().reset_level_progress();
        return true;
    };
    now_ms = 2'000;
    transport.queue_raw(
        91u,
        og::sim::serialize_exit_prompt_response_message(
            {.accepted = true, .abort_request = true}));
    server.step();
    ASSERT_EQ(0u, fixture.world().level_tick_count());

    // All three raise ready + an explicit KeyframeRequest in the same poll
    // batch: every catch-up keyframe this step is BUDGETED, and the budget
    // is two — the pump must serve 91 and 92 and defer 93.
    const auto count_snapshots_for = [&](og::sim::PeerId peer) {
        int count = 0;
        for (const auto& sent : transport.sent())
        {
            if (sent.peer_id == peer && sent.data.size() > 1 &&
                sent.data[1] == og::sim::kSnapshotMessageType)
                ++count;
        }
        return count;
    };
    transport.clear_sent();
    for (og::sim::PeerId peer = 91u; peer <= 93u; ++peer)
    {
        transport.queue_raw(peer, og::sim::serialize_client_ready_message(
                                      og::sim::ClientReadyMessage{}));
        transport.queue_raw(peer, og::sim::serialize_keyframe_request_message(
                                      {.last_seen_tick = 0u}));
    }
    server.step();
    EXPECT_EQ(1, count_snapshots_for(91u));
    EXPECT_EQ(1, count_snapshots_for(92u));
    EXPECT_EQ(0, count_snapshots_for(93u))
        << "the third budgeted keyframe must be deferred, not sent";
    EXPECT_EQ(0u, fixture.world().level_tick_count())
        << "the gate stays closed while a keyframe is owed";

    // The deferred peer is served on the very next gate step.
    server.step();
    EXPECT_EQ(1, count_snapshots_for(93u))
        << "the deferred keyframe must go out one step later";

    // A NEW bound peer arriving mid-gate is seeded (setup + keyframe)
    // through the handshake pump itself.
    transport.set_connected({91u, 92u, 93u, 94u});
    server.poll_incoming_messages();
    walker* const late_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, late_control);
    server.bind_player(94u, 3u, fixture.world().my_team, late_control);
    server.step();
    EXPECT_EQ(1, count_snapshots_for(94u))
        << "a mid-gate join must be seeded by the handshake pump";

    // Everyone re-readies; the reloaded level's tick 1 runs.
    for (og::sim::PeerId peer = 91u; peer <= 94u; ++peer)
    {
        transport.queue_raw(peer, og::sim::serialize_client_ready_message(
                                      og::sim::ClientReadyMessage{}));
    }
    server.step();
    EXPECT_EQ(1u, fixture.world().level_tick_count())
        << "serving every owed keyframe must reopen the launch gate";
}

// The staged-lobby adoption seam: SimEventLog::append transfers already-
// stamped events verbatim — each keeps its own tick stamp (never restamped to
// current_tick_), suppression does not apply (adoption is an explicit
// transfer, not a gameplay push), and an empty append is a no-op.
TEST(GameServerCoverage, sim_event_log_append_preserves_event_stamps)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 7;
    log.push_notification("live line", 40);

    std::vector<og::sim::Event> staged;
    og::sim::Event staged_event;
    staged_event.tick = 1;
    staged_event.kind = og::sim::EventKind::Notification;
    staged_event.a = 80;
    staged_event.text = "staged line";
    staged.push_back(staged_event);

    {
        og::sim::SimEventLogSuppressGuard guard(log);
        log.append(std::move(staged));
    }

    ASSERT_EQ(2u, log.size());
    EXPECT_EQ(7u, log.events()[0].tick);
    EXPECT_EQ("live line", log.events()[0].text);
    EXPECT_EQ(1u, log.events()[1].tick);
    EXPECT_EQ("staged line", log.events()[1].text);
    EXPECT_EQ(80u, log.events()[1].a);

    log.append({});
    EXPECT_EQ(2u, log.size());
}

TEST(GameServerCoverage, disconnecting_withdraw_owner_clears_targeted_prompt)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({92u});
    server.poll_incoming_messages();

    walker* control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    control->set_skip_exit(10);
    server.bind_player(92u, 0u, fixture.world().my_team, control);

    fixture.events.push(og::sim::EventKind::WithdrawToLevel, 7u);
    fixture.events.push(og::sim::EventKind::RequestExitConfirmation, 7u, 1u);
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Drain);

    EXPECT_TRUE(server.pending_exit_prompt());
    EXPECT_TRUE(fixture.world().pending_exit_prompt);
    EXPECT_TRUE(fixture.world().withdraw_requested);
    EXPECT_EQ(7, fixture.world().withdraw_level);
    const auto prompt = find_exit_prompt(transport, 92u);
    ASSERT_TRUE(prompt.has_value());
    EXPECT_EQ(7, prompt->destination_level);
    EXPECT_TRUE(prompt->withdraw_prompt);
    EXPECT_EQ("Withdraw to Level 7?", prompt->prompt_text);

    transport.set_connected({});
    server.poll_incoming_messages();

    EXPECT_FALSE(server.pending_exit_prompt());
    EXPECT_FALSE(fixture.world().pending_exit_prompt);
    EXPECT_FALSE(fixture.world().withdraw_requested);
    EXPECT_EQ(-1, fixture.world().withdraw_level);
}

TEST(GameServerCoverage, explicit_host_disconnect_cascades_to_all_clients)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({93u, 94u});
    server.poll_incoming_messages();
    server.connect_client(93u);
    server.connect_client(94u);

    // The cascade must also unwind the paused state a cascaded seat owned:
    // otherwise the host quitting while a client's pause menu is open leaves
    // the authority frozen with nobody left to resume it.
    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    server.bind_player(94u, 0u, fixture.world().my_team, control);
    transport.queue_raw(
        94u, og::sim::serialize_pause_broadcast_message(
                 {.player_index = 0u, .player_name = ""}));
    server.step();
    ASSERT_TRUE(server.paused());
    ASSERT_TRUE(fixture.world().paused);

    server.disconnect_client(93u);

    EXPECT_EQ((std::vector<og::sim::PeerId>{93u, 94u}),
              transport.disconnected());
    EXPECT_FALSE(server.paused())
        << "the cascade must clear the pause its cascaded seat owned";
    EXPECT_FALSE(fixture.world().paused);
    transport.clear_sent();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    EXPECT_TRUE(transport.sent().empty());
}

TEST(GameServerCoverage,
     spectator_disconnects_retain_distinct_reconnect_tokens)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    const auto zero_hello =
        og::sim::serialize_hello(og::sim::HelloMessage{});

    transport.set_connected({201u});
    server.poll_incoming_messages();
    server.connect_spectator(201u);
    transport.queue_raw(201u, {zero_hello.begin(), zero_hello.end()});
    server.step();
    const auto first_hello = find_hello(transport, 201u);
    ASSERT_TRUE(first_hello.has_value());
    ASSERT_FALSE(
        og::sim::is_zero_session_token(first_hello->session_token));

    transport.clear_sent();
    transport.set_connected({});
    server.poll_incoming_messages();

    transport.set_connected({202u});
    server.poll_incoming_messages();
    server.connect_spectator(202u);
    transport.queue_raw(202u, {zero_hello.begin(), zero_hello.end()});
    server.step();
    const auto second_hello = find_hello(transport, 202u);
    ASSERT_TRUE(second_hello.has_value());
    ASSERT_FALSE(
        og::sim::is_zero_session_token(second_hello->session_token));
    EXPECT_NE(first_hello->session_token,
              second_hello->session_token);

    transport.clear_sent();
    transport.set_connected({});
    server.poll_incoming_messages();

    // Reconnect with the first token after a second spectator has also left.
    // Both records must coexist; storing the second must not overwrite the
    // first merely because the disconnected-spectator list was non-empty.
    transport.set_connected({203u});
    server.poll_incoming_messages();
    const auto reconnect =
        og::sim::serialize_hello(*first_hello);
    transport.queue_raw(203u, {reconnect.begin(), reconnect.end()});
    server.step();
    const auto reconnected_hello = find_hello(transport, 203u);
    ASSERT_TRUE(reconnected_hello.has_value());
    EXPECT_EQ(first_hello->session_token,
              reconnected_hello->session_token);
}

TEST(GameServerCoverage,
     explicit_disconnect_only_removes_matching_grace_records)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    const auto zero_hello =
        og::sim::serialize_hello(og::sim::HelloMessage{});

    transport.set_connected({211u});
    server.poll_incoming_messages();
    server.bind_player(211u, 0u, 0);
    server.bind_player(211u, 1u, 1, nullptr, 1);
    transport.queue_raw(211u, {zero_hello.begin(), zero_hello.end()});
    server.step();
    ASSERT_TRUE(find_hello(transport, 211u).has_value());

    transport.set_connected({});
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.disconnected_players().size());
    EXPECT_EQ(0u, server.disconnected_players().front().player_index);
    EXPECT_EQ(1u, server.disconnected_players().back().player_index);

    transport.set_connected({212u});
    server.poll_incoming_messages();
    server.bind_player(212u, 0u, 0);
    server.disconnect_client(212u);

    ASSERT_EQ(1u, server.disconnected_players().size());
    EXPECT_EQ(1u, server.disconnected_players().front().player_index);
    EXPECT_EQ(212u, transport.disconnected().back());
}

TEST(GameServerCoverage,
     transport_disconnect_waits_for_budgeted_typed_message)
{
    TestGameWorld fixture;
    CoverageTransport transport(/*typed=*/true);
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({221u});
    server.poll_incoming_messages();
    server.bind_player(221u, 0u, 0);

    og::sim::TypedReceivedMessage hello;
    hello.peer_id = 221u;
    hello.kind = og::sim::TypedReceivedMessageKind::Hello;
    hello.hello = std::make_shared<og::sim::HelloMessage>();
    transport.queue_typed(std::move(hello));
    server.step();
    const auto hello_response = find_hello(transport, 221u);
    ASSERT_TRUE(hello_response.has_value());
    ASSERT_FALSE(
        og::sim::is_zero_session_token(hello_response->session_token));

    og::sim::TypedReceivedMessage heartbeat;
    heartbeat.peer_id = 221u;
    heartbeat.kind = og::sim::TypedReceivedMessageKind::Heartbeat;
    heartbeat.heartbeat =
        std::make_shared<og::sim::HeartbeatMessage>();
    transport.queue_typed(std::move(heartbeat));
    transport.set_connected({});

    server.poll_incoming_messages(0);
    EXPECT_EQ(0, server.messages_drained_last_call());
    EXPECT_EQ(1u, server.pending_inbound_message_count());
    EXPECT_TRUE(server.disconnected_players().empty());

    server.poll_incoming_messages(1);
    EXPECT_EQ(1, server.messages_drained_last_call());
    EXPECT_EQ(0u, server.pending_inbound_message_count());
    ASSERT_EQ(1u, server.last_polled_messages().size());
    EXPECT_EQ(221u, server.last_polled_messages().front().peer_id);
    ASSERT_EQ(1u, server.disconnected_players().size());
    EXPECT_EQ(0u, server.disconnected_players().front().player_index);
}

TEST(GameServerCoverage, backward_wall_clock_does_not_timeout_a_client)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1000u;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    server.connect_client(95u);

    now_ms = 999u;
    server.step();

    EXPECT_TRUE(transport.disconnected().empty());
    EXPECT_EQ(1u, fixture.world().tick_count_);
}

// Issue #145: the server drops SimInputResult.play_sound / .notify_text, so a
// yell only reaches the clients if the sim layer emits it into the event log
// that step() drains into the tick's broadcast batch.
TEST(GameServerCoverage, yell_input_broadcasts_yo_sound_and_notification)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({96u});
    server.poll_incoming_messages();
    server.connect_client(96u);

    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(64, 64);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    control->set_yo_delay(0);
    server.bind_player(96u, 0u, fixture.world().my_team, control);

    // Event batches only go out to clients that hold an initial snapshot AND
    // have reported ready: one step to push the initial setup + keyframe, then
    // the ready message, then the yell.
    server.step();
    transport.queue_raw(
        96u, og::sim::serialize_client_ready_message({.last_applied_tick = 0u}));
    server.step();

    InputState yell;
    yell.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    const auto input_bytes =
        og::sim::serialize_input(fixture.world().tick_count_ + 1u, yell);
    transport.queue_raw(
        96u, std::vector<std::uint8_t>(input_bytes.begin(), input_bytes.end()));

    transport.clear_sent();
    server.step();

    EXPECT_EQ(30, control->yo_delay()) << "the yell should have been applied";

    const auto batch = find_sim_event_batch(transport, 96u);
    ASSERT_TRUE(batch.has_value()) << "the tick should broadcast a sim event batch";

    const bool has_sound = std::any_of(
        batch->events.begin(), batch->events.end(),
        [](const og::sim::Event& event) {
            return event.kind == og::sim::EventKind::PlaySound &&
                   event.a == static_cast<std::uint32_t>(SOUND_YO);
        });
    const bool has_notification = std::any_of(
        batch->events.begin(), batch->events.end(),
        [](const og::sim::Event& event) {
            return event.kind == og::sim::EventKind::Notification &&
                   event.text == "Yo!";
        });

    EXPECT_TRUE(has_sound) << "the broadcast batch should carry the yo sound";
    EXPECT_TRUE(has_notification)
        << "the broadcast batch should carry the Yo! notification";
}

// P13/Q3: the 2013 floating damage/heal overlay is pushed by the sim into the
// AUTHORITATIVE walkers' lists, but every display renders a mirror world that
// never ticks the sim. GameServer::broadcast_current_state lifts the lists of
// EVERY walker onto the tick's sim event batch — seat-bound owners first,
// under a per-tick budget — and drains EVERY list. Which pane paints which
// number stays a render rule (walker_draw.cpp gates on the pane's control),
// so a spectator or follow pane watching a walker no seat owns gets its
// numbers too. The drain is unconditional because render is the only eraser
// in the classic code, so a headless authority otherwise grows the lists for
// the life of the level.
TEST(GameServerCoverage, step_lifts_every_owner_damage_numbers_and_drains_every_list)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({96u});
    server.poll_incoming_messages();
    server.connect_client(96u);

    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(64, 64);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    walker* const foe = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe);
    foe->setxy(96, 64);
    server.bind_player(96u, 0u, fixture.world().my_team, control);

    // Event batches only reach a client that holds an initial snapshot AND
    // has reported ready: one step for the setup + keyframe, then ready.
    server.step();
    transport.queue_raw(
        96u, og::sim::serialize_client_ready_message({.last_applied_tick = 0u}));
    server.step();

    control->do_hit_effects(control, foe, 7);
    ASSERT_EQ(1u, control->damage_numbers.size())
        << "the attacker keeps the orange copy";
    ASSERT_EQ(1u, foe->damage_numbers.size())
        << "the target keeps the red copy";
    const std::uint32_t created_tick = control->damage_numbers.front().created_tick;
    const float expected_x =
        static_cast<float>(foe->xpos() + foe->sizex() / 2);
    const float expected_y = static_cast<float>(foe->ypos());

    transport.clear_sent();
    server.step();

    const auto batch = find_sim_event_batch(transport, 96u);
    ASSERT_TRUE(batch.has_value()) << "the tick should broadcast a sim event batch";
    const auto lifted_count = std::count_if(
        batch->events.begin(), batch->events.end(),
        [](const og::sim::Event& event) {
            return event.kind == og::sim::EventKind::DamageNumber;
        });
    ASSERT_EQ(2, lifted_count)
        << "both the bound control's orange copy and the unbound foe's red "
           "copy ride the batch: the per-pane filter is a render rule";

    const auto find_for_owner = [&batch](std::uint32_t owner_entity_id) {
        std::optional<og::sim::DecodedDamageNumber> found;
        for (const og::sim::Event& event : batch->events)
        {
            const auto decoded = og::sim::decode_damage_number_event(event);
            if (decoded.has_value() &&
                decoded->owner_entity_id == owner_entity_id)
            {
                found = decoded;
            }
        }
        return found;
    };

    const auto attacker_copy = find_for_owner(control->entity_id());
    ASSERT_TRUE(attacker_copy.has_value())
        << "the seat-bound attacker's own copy must be on the wire";
    EXPECT_FLOAT_EQ(7.0f, attacker_copy->number.value);
    EXPECT_EQ(235, static_cast<int>(attacker_copy->number.color))
        << "the attacker's own copy is orange (235), not RED";
    EXPECT_FLOAT_EQ(expected_x, attacker_copy->number.x)
        << "the attacker's number is anchored at the TARGET's centre";
    EXPECT_FLOAT_EQ(expected_y, attacker_copy->number.y);
    EXPECT_EQ(created_tick, attacker_copy->number.created_tick);

    const auto target_copy = find_for_owner(foe->entity_id());
    ASSERT_TRUE(target_copy.has_value())
        << "the unbound foe owns the red copy a spectator/follow pane needs";
    EXPECT_FLOAT_EQ(7.0f, target_copy->number.value);
    EXPECT_EQ(static_cast<int>(RED),
              static_cast<int>(target_copy->number.color))
        << "the target's copy is RED (40)";
    EXPECT_FLOAT_EQ(expected_x, target_copy->number.x)
        << "both copies are anchored at the TARGET's centre";
    EXPECT_FLOAT_EQ(expected_y, target_copy->number.y);
    EXPECT_EQ(created_tick, target_copy->number.created_tick);

    EXPECT_TRUE(control->damage_numbers.empty())
        << "the lift drains the authoritative list";
    EXPECT_TRUE(foe->damage_numbers.empty())
        << "unbound walkers are drained too (the headless growth)";

    transport.clear_sent();
    server.step();
    const auto second = find_sim_event_batch(transport, 96u);
    ASSERT_TRUE(second.has_value())
        << "every tick broadcasts a batch, so the re-send check is unguarded";
    EXPECT_EQ(0, std::count_if(
                     second->events.begin(), second->events.end(),
                     [](const og::sim::Event& event) {
                         return event.kind == og::sim::EventKind::DamageNumber;
                     }))
        << "a drained list must not be re-sent on the next tick";
}

// Rule (src/gameplay/game_server.cpp:616-635, and the comment there): pack
// transfers are a lobby-phase concern. A stale PackRequest that slips into the
// gameplay stream is legal traffic the dispatcher simply ignores — it must NOT
// mark the peer malformed, because dropping a live player over a late lobby
// frame is a lost match.
TEST(GameServerCoverage, stale_pack_request_in_the_gameplay_stream_is_kept)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({33u});
    server.poll_incoming_messages();

    transport.queue_raw(
        33u, og::sim::serialize_pack_request_message({.pack_id = "mods"}));
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.last_polled_messages().size());
    const og::sim::TypedReceivedMessage& decoded =
        server.last_polled_messages().front();
    EXPECT_EQ(33u, decoded.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PackRequest, decoded.kind);
    ASSERT_NE(nullptr, decoded.pack_request);
    EXPECT_EQ("mods", decoded.pack_request->pack_id);
    EXPECT_TRUE(transport.disconnected().empty());
    EXPECT_EQ((std::vector<og::sim::PeerId>{33u}), transport.connected_peers());
}

// Rule (src/gameplay/game_server.cpp:264-275): a peer holding exactly ONE seat
// keeps the historic single-active-slot heuristic, so a legacy client (text,
// curses, an old test) that always fills InputState slot 0 still drives a seat
// bound to any global player index. The heuristic must refuse to guess when
// two slots are active at once: a two-seat envelope on a one-seat binding is
// ambiguous, and adopting the wrong slot would let one machine's keys move
// another machine's hero.
TEST(GameServerCoverage,
     legacy_single_seat_input_adopts_one_active_slot_and_refuses_two)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected({96u});
    server.poll_incoming_messages();
    server.connect_client(96u);

    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(64, 64);
    control->set_user(1);
    control->set_act_type(ACT_CONTROL);
    control->set_yo_delay(0);
    const std::uint32_t control_id = control->entity_id();
    // Global player index 1, but the peer only ever fills InputState slot 0.
    server.bind_player(96u, 1u, fixture.world().my_team, control);

    server.step();
    transport.queue_raw(
        96u, og::sim::serialize_client_ready_message({.last_applied_tick = 0u}));
    server.step();

    const auto send_input = [&](const InputState& input) {
        const auto bytes =
            og::sim::serialize_input(fixture.world().tick_count_ + 1u, input);
        transport.queue_raw(
            96u, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        server.step();
    };
    // Re-find the hero by id after every tick rather than holding the pointer
    // across step().
    const auto yo_delay_now = [&]() -> int {
        const walker* const found = fixture.world().find_by_id(control_id);
        EXPECT_NE(nullptr, found) << "the bound hero must survive the tick";
        return found != nullptr ? found->yo_delay() : -1;
    };

    // Ambiguous: BOTH active slots carry a yell, so ANY adoption at all would
    // fire the yo sound and set yo_delay to 30. Only the refusal leaves seat 1
    // on its own (empty) slot 1, which yells nothing.
    InputState ambiguous;
    ambiguous.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    ambiguous.players[2].pressed[static_cast<int>(InputAction::Yell)] = true;
    ambiguous.players[2].held[static_cast<int>(InputAction::MoveLeft)] = true;
    send_input(ambiguous);
    EXPECT_EQ(0, yo_delay_now())
        << "two active slots are ambiguous for a single-seat peer";

    // Unambiguous: slot 0 is the only active slot, so it drives seat 1.
    InputState legacy;
    legacy.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    send_input(legacy);
    EXPECT_EQ(30, yo_delay_now())
        << "the one active slot must drive the single bound seat";
}

// Rule (src/gameplay/game_server.cpp:1923-1936): the input tick is
// attacker-controlled wire data, and only ticks <= the expected tick are ever
// consumed, so an input more than KEYFRAME_INTERVAL_TICKS ahead of the live
// window is dropped outright — otherwise a flood of distinct far-future ticks
// grows pending_inputs without bound (memory exhaustion). An input exactly AT
// the bound is legal and must still apply when the world reaches its tick.
TEST(GameServerCoverage, input_tick_past_the_future_window_is_dropped)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });

    transport.set_connected({96u});
    server.poll_incoming_messages();
    server.connect_client(96u);

    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(64, 64);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    control->set_yo_delay(0);
    const std::uint32_t control_id = control->entity_id();
    server.bind_player(96u, 0u, fixture.world().my_team, control);

    server.step();
    transport.queue_raw(
        96u, og::sim::serialize_client_ready_message({.last_applied_tick = 0u}));
    server.step();

    // Re-find the hero by id after every tick rather than holding the pointer
    // across step().
    const auto yo_delay_now = [&]() -> int {
        const walker* const found = fixture.world().find_by_id(control_id);
        EXPECT_NE(nullptr, found) << "the bound hero must survive the tick";
        return found != nullptr ? found->yo_delay() : -1;
    };

    const std::uint32_t expected_tick = fixture.world().tick_count_ + 1u;
    constexpr std::uint32_t kBound =
        static_cast<std::uint32_t>(og::sim::KEYFRAME_INTERVAL_TICKS);

    InputState yell;
    yell.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    const auto queue_yell_at = [&](std::uint32_t tick) {
        const auto bytes = og::sim::serialize_input(tick, yell);
        transport.queue_raw(
            96u, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    };
    queue_yell_at(expected_tick + kBound);
    queue_yell_at(expected_tick + kBound + 1u);

    // The first step of the loop polls both frames against `expected_tick`.
    // Run the world up to the accepted tick; the at-the-bound yell applies.
    // The step budget is a ceiling, not the oracle: if a future change ever
    // stalls the world tick (launch gate, pause, ready deadline) this fails in
    // milliseconds instead of spinning forever.
    constexpr std::uint32_t kStepCeiling = 2u * kBound;
    std::uint32_t steps = 0u;
    while (fixture.world().tick_count_ < expected_tick + kBound &&
           steps < kStepCeiling)
    {
        server.step();
        ++steps;
    }
    ASSERT_LT(steps, kStepCeiling)
        << "the world stalled before reaching the future-window bound";
    ASSERT_EQ(expected_tick + kBound, fixture.world().tick_count_);
    EXPECT_EQ(30, yo_delay_now())
        << "an input exactly at the future-window bound must be kept";

    // One tick further: the over-bound yell was never stored, so nothing fires.
    walker* const before_last_tick = fixture.world().find_by_id(control_id);
    ASSERT_NE(nullptr, before_last_tick);
    before_last_tick->set_yo_delay(0);
    server.step();
    ASSERT_EQ(expected_tick + kBound + 1u, fixture.world().tick_count_);
    EXPECT_EQ(0, yo_delay_now())
        << "an input one tick past the bound must have been dropped";
}

// Rule (src/gameplay/game_server.cpp:1665-1678, 2676-2685, 2833-2851): while a
// respawn mode is running, the engine puts a dead hero back with its user tag
// intact but the seat is holding a null control. The seat must adopt that
// walker again — for a connected seat and for one still inside its disconnect
// grace window — or the reviving player comes back as a statue: a body on
// screen that nobody drives and a HUD reading someone else's health.
TEST(GameServerCoverage, revived_walker_is_reclaimed_for_connected_and_grace_seats)
{
    {
        TestGameWorld fixture;
        fixture.world().respawn_mode = og::sim::kRespawnModeHeroes;
        CoverageTransport transport;
        og::sim::GameServer server(fixture.world(), fixture.events, transport);
        transport.set_connected({96u});
        server.poll_incoming_messages();
        server.connect_client(96u);

        walker* const revived =
            fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, revived);
        revived->setxy(64, 64);
        revived->set_user(0);
        revived->set_act_type(ACT_CONTROL);
        ASSERT_NE(nullptr, revived->stats());
        const std::uint32_t revived_id = revived->entity_id();
        const float revived_hp = revived->stats()->hitpoints();

        // The seat is bound with no control at all: this is the state a hero
        // leaves behind when it dies into the respawn engine.
        server.bind_player(96u, 0u, fixture.world().my_team, nullptr);
        ASSERT_EQ(nullptr, server.player_control(0));

        server.step();
        transport.queue_raw(
            96u,
            og::sim::serialize_client_ready_message({.last_applied_tick = 0u}));
        server.step();

        ASSERT_NE(nullptr, server.player_control(0));
        EXPECT_EQ(revived_id, server.player_control(0)->entity_id());
        EXPECT_EQ(revived_hp, fixture.world().control_hp);
        const auto change = find_control_change(transport, 96u);
        ASSERT_TRUE(change.has_value())
            << "the mirror must be told which entity it now drives";
        EXPECT_EQ(0u, change->player_index);
        EXPECT_EQ(revived_id, change->entity_id);
    }

    {
        TestGameWorld fixture;
        fixture.world().respawn_mode = og::sim::kRespawnModeHeroes;
        CoverageTransport transport;
        og::sim::GameServer server(fixture.world(), fixture.events, transport);
        transport.set_connected({97u});
        server.poll_incoming_messages();
        server.bind_player(97u, 0u, fixture.world().my_team, nullptr);

        transport.set_connected({});
        server.poll_incoming_messages();
        ASSERT_EQ(1u, server.disconnected_players().size());
        ASSERT_EQ(nullptr, server.disconnected_players().front().control);
        ASSERT_EQ(nullptr, server.player_control(0));

        walker* const revived =
            fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, revived);
        revived->setxy(64, 64);
        revived->set_user(0);
        revived->set_act_type(ACT_CONTROL);
        ASSERT_NE(nullptr, revived->stats());
        const std::uint32_t revived_id = revived->entity_id();
        const float revived_hp = revived->stats()->hitpoints();

        server.step();

        ASSERT_NE(nullptr, server.player_control(0));
        EXPECT_EQ(revived_id, server.player_control(0)->entity_id());
        EXPECT_EQ(revived_hp, fixture.world().control_hp);
        ASSERT_EQ(1u, server.disconnected_players().size());
        ASSERT_NE(nullptr, server.disconnected_players().front().control);
        EXPECT_EQ(revived_id,
                  server.disconnected_players().front().control->entity_id())
            << "a grace-window seat must adopt its revived hero too";
    }
}

std::vector<std::uint8_t> input_frame(std::uint32_t tick, const InputState& input)
{
    const auto bytes = og::sim::serialize_input(tick, input);
    return {bytes.begin(), bytes.end()};
}

std::vector<std::uint8_t> hello_frame(const og::sim::SessionToken& token)
{
    og::sim::HelloMessage hello;
    hello.session_token = token;
    const auto bytes = og::sim::serialize_hello(hello);
    return {bytes.begin(), bytes.end()};
}

// Rule (game_server.cpp handle_transport_disconnect): a seat parked in its
// grace window keeps playing out of its NEWEST pending input, not whichever
// one the unordered pending map happens to yield last. Two future inputs are
// pending at the disconnect; the repeated input carries the later tick's
// held keys (pressed flags are cleared).
TEST(GameServerCoverage, grace_seat_repeats_its_newest_pending_input)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({96u});
    server.poll_incoming_messages();

    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(64, 64);
    server.bind_player(96u, 0u, fixture.world().my_team, control);
    server.step();
    transport.queue_raw(
        96u, og::sim::serialize_client_ready_message({.last_applied_tick = 0u}));
    server.step();

    const std::uint32_t next_tick = fixture.world().tick_count_ + 1u;
    InputState older;
    older.players[0].held[static_cast<int>(InputAction::MoveLeft)] = true;
    older.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    InputState newer;
    newer.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    newer.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    // Queue the newer tick FIRST so arrival order cannot stand in for tick
    // order. Both sit ahead of the tick this step consumes.
    transport.queue_raw(96u, input_frame(next_tick + 6u, newer));
    transport.queue_raw(96u, input_frame(next_tick + 3u, older));
    server.step();
    ASSERT_TRUE(server.disconnected_players().empty());

    transport.set_connected({});
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.disconnected_players().size());
    const PlayerInput& repeated =
        server.disconnected_players().front().repeated_input;
    EXPECT_TRUE(repeated.held[static_cast<int>(InputAction::MoveRight)])
        << "the grace seat must repeat the newest pending input";
    EXPECT_FALSE(repeated.held[static_cast<int>(InputAction::MoveLeft)])
        << "the older pending input must not win";
    EXPECT_FALSE(repeated.pressed[static_cast<int>(InputAction::Fire)])
        << "a parked seat never repeats a press";
}

// Rule (handle_transport_disconnect): one grace record per seat. When the
// same player seat disconnects again while an older record for it is still
// parked, the record is REPLACED with the newer seat state instead of a second
// record being appended for the same player.
TEST(GameServerCoverage, second_disconnect_of_a_seat_refreshes_its_grace_record)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    walker* const first = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const second = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);

    transport.set_connected({97u});
    server.poll_incoming_messages();
    server.bind_player(97u, 0u, fixture.world().my_team, first);
    transport.set_connected({});
    server.poll_incoming_messages();

    // Paired control: the first disconnect parks exactly one record.
    ASSERT_EQ(1u, server.disconnected_players().size());
    EXPECT_EQ(first, server.disconnected_players().front().control);
    EXPECT_EQ(0u, server.disconnected_players().front().player_index);

    // The same player seat, now bound on another connection, drops too.
    transport.set_connected({98u});
    server.poll_incoming_messages();
    server.bind_player(98u, 0u, fixture.world().my_team, second);
    transport.set_connected({});
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.disconnected_players().size())
        << "a seat's second disconnect must not duplicate its grace record";
    EXPECT_EQ(second, server.disconnected_players().front().control)
        << "the record must carry the newer seat's state";
    EXPECT_EQ(0u, server.disconnected_players().front().player_index);
}

// Rule (process_non_input_messages, Input): input from a connected peer that
// is neither bound to a seat nor an admitted spectator is ignored entirely;
// even the host's timer-wait request inside it has no effect until the peer
// holds a seat.
TEST(GameServerCoverage, input_from_an_unseated_peer_is_ignored_entirely)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({91u});
    server.poll_incoming_messages();
    server.connect_client(91u);
    fixture.world().timer_wait = 3;

    InputState input;
    input.timer_wait_request = 11;
    transport.queue_raw(91u, input_frame(1u, input));
    server.step();
    EXPECT_EQ(3, fixture.world().timer_wait)
        << "an unseated peer's input must not reach the timer";

    // Paired control: the same frame from the same (host) peer is honoured
    // once it holds a seat.
    walker* const control = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    server.bind_player(91u, 0u, fixture.world().my_team, control);
    transport.queue_raw(91u, input_frame(1u, input));
    server.step();
    EXPECT_EQ(11, fixture.world().timer_wait);
}

// Rule (process_non_input_messages, Input): a seat's pending-input queue is
// capped at 256 distinct ticks; a 257th distinct tick arriving while the
// queue is full is dropped. Past ticks bypass the future window, so the flood
// uses ticks behind the live one: 256 idle frames, then the newest frame
// holding MoveRight. The newest pending tick's held keys become the seat's
// last known input, read back through the grace record after a disconnect.
TEST(GameServerCoverage, pending_input_queue_is_capped_at_256_ticks)
{
    const auto held_right_after_flood = [](std::uint32_t filler_count) {
        TestGameWorld fixture;
        CoverageTransport transport;
        og::sim::GameServer server(fixture.world(), fixture.events, transport);
        transport.set_connected({96u});
        server.poll_incoming_messages();
        walker* const control =
            fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
        EXPECT_NE(nullptr, control);
        if (control == nullptr)
            return false;
        control->setxy(64, 64);
        server.bind_player(96u, 0u, fixture.world().my_team, control);
        server.step();
        transport.queue_raw(96u, og::sim::serialize_client_ready_message(
                                     {.last_applied_tick = 0u}));
        server.step();
        // Run the world past 300 ticks so 257 distinct PAST ticks exist.
        for (int i = 0; i < 300; ++i)
            server.step();
        const std::uint32_t live_tick = fixture.world().tick_count_;
        EXPECT_GT(live_tick, 290u) << "the world must actually tick";

        const InputState idle;
        InputState right;
        right.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
        for (std::uint32_t i = 0; i < filler_count; ++i)
            transport.queue_raw(96u, input_frame(i + 1u, idle));
        transport.queue_raw(96u, input_frame(live_tick - 1u, right));
        server.step();

        transport.set_connected({});
        server.poll_incoming_messages();
        EXPECT_EQ(1u, server.disconnected_players().size());
        if (server.disconnected_players().size() != 1u)
            return false;
        return server.disconnected_players()
            .front()
            .repeated_input.held[static_cast<int>(InputAction::MoveRight)];
    };

    // Paired control: 255 fillers leave room, the newest frame is stored.
    EXPECT_TRUE(held_right_after_flood(255u))
        << "the 256th distinct pending tick must still be accepted";
    EXPECT_FALSE(held_right_after_flood(256u))
        << "a 257th distinct pending tick must be dropped at the cap";
}

// Rule (broadcast_current_state): at most kMaxKeyframesPerTick (2) budgeted
// recovery keyframes go out per tick during live play; a third requesting
// client is deferred with its keyframe still owed and served next tick.
TEST(GameServerCoverage, live_recovery_keyframes_are_budgeted_two_per_tick)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    transport.set_connected({91u, 92u, 93u});
    server.poll_incoming_messages();

    for (std::size_t i = 0; i < 3u; ++i)
    {
        walker* const control =
            fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, control);
        server.bind_player(static_cast<og::sim::PeerId>(91u + i),
                           static_cast<std::uint8_t>(i),
                           fixture.world().my_team, control);
    }
    // A living foe keeps the level running: with no enemy left the level is
    // won, and a terminal EndGame keyframe is exempt from the budget.
    walker* const foe = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe);
    foe->set_team_num(static_cast<unsigned char>(fixture.world().my_team + 1));
    foe->setxy(400, 400);
    for (og::sim::PeerId peer = 91u; peer <= 93u; ++peer)
    {
        server.send_initial_snapshot(peer, og::sim::SnapshotCaptureMode::Peek);
        transport.queue_raw(peer, og::sim::serialize_client_ready_message(
                                      og::sim::ClientReadyMessage{}));
    }
    server.step();
    server.step();
    ASSERT_EQ(2u, fixture.world().level_tick_count())
        << "the launch gate must be open: this pins LIVE play";
    ASSERT_EQ(0, fixture.world().ending) << "the level must still be running";

    const auto count_keyframes_for = [&](og::sim::PeerId peer) {
        int count = 0;
        for (const auto& sent : transport.sent())
        {
            if (sent.peer_id == peer && sent.data.size() > 1 &&
                sent.data[1] == og::sim::kSnapshotMessageType)
                ++count;
        }
        return count;
    };

    transport.clear_sent();
    for (og::sim::PeerId peer = 91u; peer <= 93u; ++peer)
    {
        transport.queue_raw(peer, og::sim::serialize_keyframe_request_message(
                                      {.last_seen_tick = 1u}));
    }
    server.step();
    ASSERT_EQ(3u, fixture.world().level_tick_count());
    const std::string counts = std::format(
        "keyframes 91={} 92={} 93={}", count_keyframes_for(91u),
        count_keyframes_for(92u), count_keyframes_for(93u));
    EXPECT_EQ(1, count_keyframes_for(91u)) << counts;
    EXPECT_EQ(1, count_keyframes_for(92u)) << counts;
    EXPECT_EQ(0, count_keyframes_for(93u))
        << "the third budgeted keyframe in one tick must be deferred; "
        << counts;

    transport.clear_sent();
    server.step();
    EXPECT_EQ(1, count_keyframes_for(93u))
        << "the deferred keyframe must go out on the next tick";
}

// Rule (handle_hello reconnect): a player whose hero died during the grace
// window resumes in the dead state and the corpse's player tag is released
// (user -> -1), so the dead walker is not left claimed by a seat that no
// longer drives it.
TEST(GameServerCoverage, reconnect_to_a_hero_that_died_in_grace_releases_the_corpse)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({97u});
    server.poll_incoming_messages();

    // A named hero (it carries its guy record), so its corpse stays in the
    // world after death exactly as a player character's does.
    walker* const hero = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    auto record = std::make_unique<guy>(FAMILY_SOLDIER);
    record->name = "Grace";
    hero->set_owned_myguy(std::move(record));
    hero->setxy(64, 64);
    const std::uint32_t hero_id = hero->entity_id();
    server.bind_player(97u, 0u, fixture.world().my_team, hero);
    transport.queue_raw(97u, hello_frame(og::sim::kZeroSessionToken));
    server.step();
    const auto hello = find_hello(transport, 97u);
    ASSERT_TRUE(hello.has_value());
    ASSERT_FALSE(og::sim::is_zero_session_token(hello->session_token));

    transport.set_connected({});
    server.poll_incoming_messages();
    ASSERT_EQ(1u, server.disconnected_players().size());

    // An enemy lands a lethal blow while the seat is parked.
    walker* const parked = fixture.world().find_by_id(hero_id);
    ASSERT_NE(nullptr, parked);
    walker* const raider = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, raider);
    raider->setxy(80, 64);
    raider->set_team_num(static_cast<unsigned char>(parked->team_num() + 1u));
    raider->set_damage(10000);
    raider->attack(parked);
    server.step();
    walker* const corpse = fixture.world().find_by_id(hero_id);
    ASSERT_NE(nullptr, corpse) << "a living corpse persists";
    ASSERT_TRUE(corpse->dead()) << "the hero must die during the grace window";
    ASSERT_EQ(0, corpse->user())
        << "precondition: the corpse still carries the seat's player tag";

    transport.set_connected({98u});
    server.poll_incoming_messages();
    transport.queue_raw(98u, hello_frame(hello->session_token));
    server.step();

    ASSERT_TRUE(server.disconnected_players().empty())
        << "the reconnect must have consumed the grace record";
    const walker* const released = fixture.world().find_by_id(hero_id);
    ASSERT_NE(nullptr, released);
    EXPECT_EQ(-1, released->user())
        << "the dead hero's player tag must be released on reconnect";
}

// Rule (handle_hello reconnect, ruling R5 — INTENDED): a reconnecting seat
// reclaims its parked hero that the AI holds (user() == -1) UNCONDITIONALLY
// — player tag and ACT_CONTROL — even when the control policy refuses that
// seat a fresh claim of the same hero. The fresh-bind half is the positive
// control: bind_player re-checks an installer-supplied control against
// control_claim_allowed and leaves it unclaimed (and the reconnect re-runs
// that same refusing bind before its own loop overrides it). The state is
// product-reachable: an installer-supplied control under an owner-locked
// policy whose machine map has no entry for the seat, and a joiner that drops
// and returns during the level-start handshake. That timing also isolates the
// rule: no tick has run this level, so the reconnect's own InitialSetup holds
// the launch gate and the tick's input-handler claim cannot mask the
// reconnect's claim.
TEST(GameServerCoverage,
     reconnect_reclaims_the_ai_held_hero_regardless_of_the_claim_policy)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({97u});
    server.poll_incoming_messages();

    walker* const hero = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    auto record = std::make_unique<guy>(FAMILY_SOLDIER);
    record->name = "Owner";
    record->owner_player_index = 0;
    hero->set_owned_myguy(std::move(record));
    hero->setxy(64, 64);
    const std::uint32_t hero_id = hero->entity_id();
    ASSERT_EQ(-1, hero->user()) << "precondition: an AI-held hero";
    const char ai_act_type = hero->act_type();
    ASSERT_NE(ACT_CONTROL, ai_act_type);

    // Owner-locked with no machine entry for seat 0: seat 0 claims nothing.
    std::array<std::uint8_t, og::sim::kPlayerMachineSlots> machines;
    machines.fill(og::sim::kPlayerMachineNone);
    og::sim::set_control_policy(fixture.world(),
                                og::sim::kControlPolicyOwnerLocked, machines);
    ASSERT_EQ(og::sim::kControlPolicyOwnerLocked,
              fixture.world().control_policy);

    // Positive control: the FRESH bind of the supplied hero is refused.
    server.bind_player(97u, 0u, fixture.world().my_team, hero);
    ASSERT_FALSE(og::sim::control_claim_allowed(fixture.world(), hero, 0))
        << "the policy must refuse seat 0 a fresh claim of this hero";
    ASSERT_EQ(-1, hero->user())
        << "a fresh bind honours the policy and leaves the hero unclaimed";
    ASSERT_EQ(ai_act_type, hero->act_type());

    // The joiner drops before tick 1: its seat parks in the grace window.
    transport.set_connected({});
    server.poll_incoming_messages();
    ASSERT_EQ(1u, server.disconnected_players().size());
    const og::sim::SessionToken token =
        server.disconnected_players().front().session_token;
    ASSERT_FALSE(og::sim::is_zero_session_token(token));
    ASSERT_EQ(-1, fixture.world().find_by_id(hero_id)->user());

    transport.set_connected({98u});
    server.poll_incoming_messages();
    transport.queue_raw(98u, hello_frame(token));
    server.step();

    ASSERT_EQ(0u, fixture.world().level_tick_count())
        << "isolation: the reconnect step must not have ticked the world";
    EXPECT_TRUE(server.disconnected_players().empty())
        << "the reconnect must have consumed the grace record";
    const auto resumed = find_hello(transport, 98u);
    ASSERT_TRUE(resumed.has_value());
    EXPECT_EQ(token, resumed->session_token)
        << "the reconnect resumes the same session";
    walker* const reclaimed = fixture.world().find_by_id(hero_id);
    ASSERT_NE(nullptr, reclaimed);
    EXPECT_FALSE(og::sim::control_claim_allowed(fixture.world(), reclaimed, 0))
        << "the policy still refuses the claim the reconnect just made";
    EXPECT_EQ(0, reclaimed->user())
        << "the reconnecting seat reclaims its AI-held hero regardless";
    EXPECT_EQ(ACT_CONTROL, reclaimed->act_type());
    EXPECT_EQ(reclaimed, server.player_control(0u));
}

// Rule (send_forced_keyframe_to_ready_clients): when a mission abort is
// accepted, the forced old-level keyframe goes only to clients ready to take
// snapshots. A peer that joined mid-level and has not confirmed its initial
// keyframe (seeded, not ready) receives nothing.
TEST(GameServerCoverage, forced_abort_keyframe_skips_a_client_that_is_not_ready)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({91u});
    server.poll_incoming_messages();
    walker* const host_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, host_control);
    server.bind_player(91u, 0u, fixture.world().my_team, host_control);
    server.send_initial_snapshot(91u, og::sim::SnapshotCaptureMode::Peek);
    transport.queue_raw(91u, og::sim::serialize_client_ready_message(
                                 og::sim::ClientReadyMessage{}));
    server.step();
    ASSERT_EQ(1u, fixture.world().level_tick_count());

    // A second peer joins mid-level: the handshake pump seeds it (setup +
    // initial keyframe) but it never confirms ready.
    transport.set_connected({91u, 92u});
    server.poll_incoming_messages();
    walker* const late_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, late_control);
    server.bind_player(92u, 1u, fixture.world().my_team, late_control);
    server.step();
    ASSERT_TRUE(find_initial_setup(transport, 92u).has_value())
        << "precondition: the late peer was seeded";

    const auto keyframes_at = [&](og::sim::PeerId peer, std::uint32_t tick) {
        int count = 0;
        for (const auto& sent : transport.sent())
        {
            if (sent.peer_id != peer || sent.data.size() <= 1 ||
                sent.data[1] != og::sim::kSnapshotMessageType)
                continue;
            if (og::sim::deserialize_snapshot(sent.data).tick_count == tick)
                ++count;
        }
        return count;
    };
    const auto snapshots_for = [&](og::sim::PeerId peer) {
        int count = 0;
        for (const auto& sent : transport.sent())
        {
            if (sent.peer_id == peer && sent.data.size() > 1 &&
                (sent.data[1] == og::sim::kSnapshotMessageType ||
                 sent.data[1] == og::sim::kDeltaSnapshotMessageType))
                ++count;
        }
        return count;
    };

    transport.clear_sent();
    const std::uint32_t abort_tick = fixture.world().tick_count_;
    transport.queue_raw(
        91u,
        og::sim::serialize_exit_prompt_response_message(
            {.accepted = true, .abort_request = true}));
    server.step();

    // Paired control: the ready host takes exactly one keyframe of the
    // pre-abort state.
    EXPECT_EQ(1, keyframes_at(91u, abort_tick))
        << "the ready client must receive the forced keyframe";
    EXPECT_EQ(0, snapshots_for(92u))
        << "an unready client must not be sent the forced keyframe";
}

// A client in the level-transition limbo (InitialSetup re-sent, keyframe
// owed, NOT yet ClientReady) gets no keyframe from broadcast_current_state —
// the direct broadcast the local shadow's Esc-abort / RESTART issue while a
// reload is still handshaking. Control: once that client readies, the
// handshake pump serves it exactly one keyframe.
TEST(GameServerCoverage, unready_limbo_client_gets_no_keyframe_from_a_direct_broadcast)
{
    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1'000;
    server.set_wall_clock_ms_source([&] { return now_ms; });
    transport.set_connected({91u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    server.bind_player(91u, 0u, fixture.world().my_team, control);
    server.send_initial_snapshot(91u, og::sim::SnapshotCaptureMode::Peek);
    transport.queue_raw(
        91u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    server.step();
    ASSERT_EQ(1u, fixture.world().level_tick_count());

    // Quit-mission reload: the client re-enters the limbo.
    server.on_withdraw_accepted = [&](int /*destination*/) {
        fixture.world().tick_count_ = 0;
        fixture.world().reset_level_progress();
        return true;
    };
    now_ms = 2'000;
    transport.queue_raw(
        91u,
        og::sim::serialize_exit_prompt_response_message(
            {.accepted = true, .abort_request = true}));
    server.step();
    ASSERT_EQ(0u, fixture.world().level_tick_count());
    ASSERT_TRUE(find_initial_setup(transport, 91u).has_value())
        << "the reload must have re-sent the InitialSetup";

    const auto count_snapshots = [&] {
        int count = 0;
        for (const auto& sent : transport.sent())
        {
            if (sent.peer_id == 91u && sent.data.size() > 1 &&
                sent.data[1] == og::sim::kSnapshotMessageType)
                ++count;
        }
        return count;
    };

    transport.clear_sent();
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);
    EXPECT_EQ(0, count_snapshots())
        << "an unready limbo client must not be sent its keyframe";

    // Control: the client readies; the pump serves the owed keyframe.
    transport.queue_raw(
        91u,
        og::sim::serialize_client_ready_message(og::sim::ClientReadyMessage{}));
    server.step();
    EXPECT_EQ(1, count_snapshots())
        << "a ready limbo client is owed exactly one keyframe";
}

// Rule (sim_process_player_input, Switch Special — issue #321): the special a
// press lands on is decided by the LIVE family registry for every registered
// family, core or pack. A class-pack hero above the core span (id >= 21)
// cycles through the slots its pack declared and wraps past the last one,
// through the real server path (seat input -> step). Core soldier is the
// control: the same press moves it 1 -> 2.
TEST(GameServerCoverage, a_pack_family_hero_cycles_its_declared_specials)
{
    // Declared before the world so the pack family outlives every walker
    // that carries its id; frees the mod slots on the way in and out.
    og::test::ScopedPackStoreState pack_store_restore;
    struct ModSlots
    {
        ModSlots() { init_all_registries(); reset_all_registry_mod_slots(); }
        ~ModSlots() { reset_all_registry_mod_slots(); }
    } mod_slots;

    og::data::ClasspackData data;
    data.pack = "switchtest";
    og::data::ClasspackLivingEntry entry;
    entry.id = "switchtest:switcher";
    entry.wire_id = "auto";
    entry.name = "SWITCHER";
    std::vector<og::data::ClasspackSpecialEntry> specials(2);
    specials[0].id = "alpha";
    specials[0].name = "ALPHA";
    specials[0].mp_cost = 1;
    specials[0].slot = 1;
    specials[1].id = "beta";
    specials[1].name = "BETA";
    specials[1].mp_cost = 1;
    specials[1].slot = 2;
    entry.specials = std::move(specials);
    data.living.push_back(std::move(entry));
    ASSERT_EQ(1, og::resources::install_classpack_data(std::move(data)));
    const FamilyDescriptor* const pack_family =
        get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(nullptr, pack_family) << "the pack family lands at id 21";

    TestGameWorld fixture;
    CoverageTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected({96u, 97u});
    server.poll_incoming_messages();

    // The loader has no art for a pack id, so add_ob would hand back a
    // SOLDIER; the family byte is set on the live walker instead (the shape
    // test_glad_hud.cpp's pack-family HUD test takes).
    walker* const switcher =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const soldier =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, switcher);
    ASSERT_NE(nullptr, soldier);
    switcher->set_family(static_cast<char>(NUM_FAMILIES));
    ASSERT_EQ(NUM_FAMILIES,
              static_cast<int>(static_cast<unsigned char>(switcher->family())));
    switcher->setxy(64, 64);
    soldier->setxy(128, 64);
    for (walker* const hero : {switcher, soldier})
    {
        hero->stats()->set_level(4);  // unlocks exactly slots 1 and 2
        hero->set_current_special(1);
    }
    server.bind_player(96u, 0u, fixture.world().my_team, switcher);
    server.bind_player(97u, 1u, fixture.world().my_team, soldier);
    server.step();
    for (const og::sim::PeerId peer : {96u, 97u})
    {
        transport.queue_raw(peer, og::sim::serialize_client_ready_message(
                                      {.last_applied_tick = 0u}));
    }
    server.step();

    const auto press_switch_special = [&](bool pressed) {
        // Both seat slots carry the press: whichever slot the server maps a
        // peer's frame to, each hero sees it.
        InputState input;
        for (const int slot : {0, 1})
        {
            input.players[slot]
                .pressed[static_cast<int>(InputAction::SwitchSpecial)] = pressed;
        }
        const auto bytes =
            og::sim::serialize_input(fixture.world().tick_count_ + 1u, input);
        for (const og::sim::PeerId peer : {96u, 97u})
        {
            transport.queue_raw(
                peer, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        }
        server.step();
    };

    press_switch_special(true);
    EXPECT_EQ(2, switcher->current_special())
        << "a pack hero lands on its declared slot 2 (BETA)";
    EXPECT_EQ(2, soldier->current_special())
        << "control: the core soldier lands on slot 2";

    press_switch_special(false);
    press_switch_special(true);
    EXPECT_EQ(1, switcher->current_special())
        << "slot 3 is NONE for the pack family: the press wraps to slot 1";
    EXPECT_EQ(1, soldier->current_special())
        << "control: slot 3 needs level 7, the level gate wraps to slot 1";
}

} // namespace
