#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/core/sound_ids.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "../test_game_world_fixture.h"

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

    constexpr std::array<std::uint8_t, 11> message_types = {
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
    server.connect_client(93u);
    server.connect_client(94u);

    server.disconnect_client(93u);

    EXPECT_EQ((std::vector<og::sim::PeerId>{93u, 94u}),
              transport.disconnected());
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

} // namespace
