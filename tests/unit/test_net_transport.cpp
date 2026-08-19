#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../test_game_world_fixture.h"

namespace {

void write_u32_le(std::vector<std::uint8_t>& bytes,
                  std::size_t offset,
                  std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

class MockTransport final : public og::sim::ITransport
{
public:
    using og::sim::ITransport::broadcast;

    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_messages_.push_back(
            {peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained =
            std::move(received_messages_);
        received_messages_.clear();
        return drained;
    }

    void accept_connections() override
    {
        accepting_connections_ = true;
    }

    void disconnect(og::sim::PeerId peer_id) override
    {
        disconnected_peers_.push_back(peer_id);
    }

    void broadcast(const std::uint8_t* data, std::size_t len) override
    {
        broadcast_messages_.emplace_back(data, data + len);
        og::sim::ITransport::broadcast(data, len);
    }

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return connected_peers_;
    }

    void set_connected_peers(std::vector<og::sim::PeerId> peers)
    {
        connected_peers_ = std::move(peers);
    }

    void queue_received(og::sim::PeerId peer_id, std::vector<std::uint8_t> data)
    {
        received_messages_.push_back({peer_id, std::move(data)});
    }

    bool accepting_connections() const noexcept
    {
        return accepting_connections_;
    }

    const std::vector<og::sim::ReceivedMessage>& sent_messages() const noexcept
    {
        return sent_messages_;
    }

    void clear_sent_messages()
    {
        sent_messages_.clear();
    }

    const std::vector<std::vector<std::uint8_t>>&
    broadcast_messages() const noexcept
    {
        return broadcast_messages_;
    }

    void clear_broadcast_messages()
    {
        broadcast_messages_.clear();
    }

    const std::vector<og::sim::PeerId>& disconnected_peers() const noexcept
    {
        return disconnected_peers_;
    }

private:
    bool accepting_connections_ = false;
    std::vector<og::sim::PeerId> connected_peers_;
    std::vector<og::sim::ReceivedMessage> received_messages_;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
    std::vector<std::vector<std::uint8_t>> broadcast_messages_;
    std::vector<og::sim::PeerId> disconnected_peers_;
};

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

og::sim::LobbyPlayer make_lobby_player_for_test()
{
    og::sim::LobbyCharacterData character;
    character.guy_id = 42;
    character.name = "Ari";
    character.family = 2;
    character.strength = 12;
    character.dexterity = 13;
    character.constitution = 14;
    character.intelligence = 15;
    character.armor = 16;
    character.exp = 1234u;
    character.kills = 8;
    character.level_kills = 9;
    character.total_damage = 10;
    character.total_hits = 11;
    character.total_shots = 12;
    character.teamnum = 2;
    character.scen_damage = 3.5f;
    character.scen_kills = 4;
    character.scen_damage_taken = 5.5f;
    character.scen_min_hp = 6.5f;
    character.scen_shots = 7;
    character.scen_hits = 8;
    character.level = 9;

    og::sim::LobbyPlayer player;
    player.player_index = 1u;
    player.seat_id = 0x10203040u;
    player.machine_id = 0x50607080u;
    player.name = "Player One";
    // v8: company display name rides the player; deployed rides the slot.
    player.company = "Ari's Company";
    player.team = 2;
    player.ready = true;
    player.is_host = true;
    player.character_slots.push_back({
        .slot_index = 3u,
        .character = character,
        .deployed = false,
    });
    return player;
}

og::sim::InitialSetupGuyData make_initial_setup_guy_for_test()
{
    og::sim::InitialSetupGuyData guy;
    guy.guy_id = 99;
    guy.name = "Setup Guy";
    guy.family = 3;
    guy.strength = 21;
    guy.dexterity = 22;
    guy.constitution = 23;
    guy.intelligence = 24;
    guy.armor = 25;
    guy.exp = 5678u;
    guy.kills = 9;
    guy.level_kills = 10;
    guy.total_damage = 11;
    guy.total_hits = 12;
    guy.total_shots = 13;
    guy.teamnum = 1;
    guy.scen_damage = 14.5f;
    guy.scen_kills = 15;
    guy.scen_damage_taken = 16.5f;
    guy.scen_min_hp = 17.5f;
    guy.scen_shots = 18;
    guy.scen_hits = 19;
    guy.level = 20;
    return guy;
}

og::sim::LobbyState make_lobby_state_for_test()
{
    og::sim::LobbyState state;
    state.settings.campaign_id = "gladiator";
    state.settings.scenario_id = 7;
    state.settings.difficulty = 2;
    state.settings.allied_mode = 1;
    state.settings.ctf_team_count = 3;
    state.settings.ctf_authored_team_mask = 0b1101u;
    state.settings.ctf_capture_limit = 7;
    state.settings.ctf_respawn_ticks = 96;
    state.settings.ctf_strip_scenario_troops = 1;
    // v8: host-only cross-control setting and the start-denial echo field.
    state.settings.cross_control = 1;
    // v11: host-only infinite-gold setting (the twelfth LobbySettings i16).
    state.settings.infinite_gold = 1;
    // v12: versus-campaign shared-teams flag (the thirteenth i16).
    state.settings.shared_teams = 1;
    state.host_player_id = 1u;
    state.last_start_denial =
        og::sim::start_denial_reason_value(
            og::sim::StartDenialReason::MachinesNotReady);
    state.last_start_request_id = 0xa1b2c3d4u;
    state.players.push_back(make_lobby_player_for_test());
    state.local_seat_ids.push_back(state.players.front().seat_id);
    state.last_join_request_id = 0x10293847u;
    state.local_peer_is_host = true;
    return state;
}

const og::sim::EntitySnapshot* find_entity_snapshot(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::uint32_t entity_id)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    return it == entities.end() ? nullptr : &*it;
}

TEST(NetTransport, header_helpers_roundtrip_envelope)
{
    std::vector<std::uint8_t> bytes;
    og::sim::append_transport_header(bytes, og::sim::kHelloMessageType, 0x2211u);

    const std::vector<std::uint8_t> expected = {0x0c, 0x01, 0x11, 0x22};
    EXPECT_EQ(expected, bytes);

    og::sim::TransportEnvelope envelope;
    ASSERT_TRUE(og::sim::decode_transport_envelope(bytes, envelope));
    EXPECT_EQ(og::sim::kNetworkProtocolVersion, envelope.protocol_version);
    EXPECT_EQ(og::sim::kHelloMessageType, envelope.message_type);
    EXPECT_EQ(0x2211u, envelope.payload_length);
}

TEST(NetTransport, default_broadcast_sends_payload_to_all_connected_peers)
{
    MockTransport transport;
    transport.set_connected_peers({3u, 7u, 11u});

    const std::array<std::uint8_t, 3> payload = {0x10, 0x20, 0x30};
    transport.broadcast(payload.data(), payload.size());

    ASSERT_EQ(3u, transport.sent_messages().size());
    EXPECT_EQ(3u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(7u, transport.sent_messages()[1].peer_id);
    EXPECT_EQ(11u, transport.sent_messages()[2].peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0x10, 0x20, 0x30}),
              transport.sent_messages()[0].data);
}

TEST(NetTransport, default_poll_typed_decodes_raw_messages)
{
    MockTransport transport;

    transport.queue_received(
        9u,
        og::sim::serialize_client_ready_message(
            og::sim::ClientReadyMessage{.last_applied_tick = 42u}));

    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(1u, messages.size());
    EXPECT_EQ(9u, messages.front().peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::ClientReady,
              messages.front().kind);
    ASSERT_TRUE(messages.front().client_ready != nullptr);
    EXPECT_EQ(42u, messages.front().client_ready->last_applied_tick);
}

TEST(NetTransport, initial_setup_full_roundtrip_and_decode_received_message)
{
    og::sim::InitialSetupMessage expected;
    expected.level_id = 17;
    expected.level_title = "Arena";
    expected.level_type = 2;
    expected.par_value = 50;
    expected.time_bonus_limit = 300;
    expected.difficulty = 4;
    expected.pixmaxx = 640;
    expected.pixmaxy = 480;
    expected.my_team = 1;
    expected.allied_mode = 0;
    expected.respawn_mode = 3;
    expected.current_scenario = 6;
    expected.guys.push_back(make_initial_setup_guy_for_test());
    expected.completed_levels = {1, 2, 3};
    expected.controlled_entity_ids[0] = 101u;
    expected.controlled_entity_ids[1] = 202u;

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_initial_setup_message(expected);
    const std::optional<og::sim::InitialSetupMessage> decoded =
        og::sim::deserialize_initial_setup_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected, *decoded);

    const og::sim::TypedReceivedMessage typed =
        og::sim::decode_received_message({.peer_id = 4u, .data = bytes});
    EXPECT_EQ(4u, typed.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::InitialSetup, typed.kind);
    ASSERT_NE(nullptr, typed.initial_setup);
    EXPECT_EQ(expected, *typed.initial_setup);
}

TEST(NetTransport, lobby_message_variants_roundtrip_and_decode)
{
    std::vector<og::sim::LobbyMessage> messages;

    og::sim::LobbyMessage join;
    join.payload = og::sim::LobbyJoinMessage{
        .player = make_lobby_player_for_test(),
        .request_id = 0xabcdef12u,
        .resume_after_level = true,
    };
    messages.push_back(join);

    og::sim::LobbyMessage leave;
    leave.payload = og::sim::LobbyLeaveMessage{.player_index = 2u};
    messages.push_back(leave);

    og::sim::LobbyMessage ready;
    ready.payload = og::sim::LobbyReadyMessage{
        .player_index = 3u,
        .ready = true,
    };
    messages.push_back(ready);

    og::sim::LobbyMessage team_change;
    team_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 4u,
        .seat_id = 0x55667788u,
        .team = 1,
    };
    messages.push_back(team_change);

    og::sim::LobbyMessage remove_seat;
    remove_seat.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = 4u,
        .seat_id = 0x55667788u,
    };
    messages.push_back(remove_seat);

    og::sim::LobbyMessage start_game;
    start_game.payload = og::sim::LobbyStartGameMessage{
        .player_index = 5u,
        .request_id = 0x12345678u,
    };
    messages.push_back(start_game);

    og::sim::LobbyMessage settings_change;
    settings_change.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 6u,
        .settings = {
            .campaign_id = "campaign",
            .scenario_id = 8,
            .difficulty = 3,
            .allied_mode = 1,
            .ctf_team_count = 4,
            .ctf_capture_limit = 9,
            .ctf_respawn_ticks = 180,
            .ctf_strip_scenario_troops = 1,
            .respawn_mode = 3,
        },
    };
    messages.push_back(settings_change);

    for (const og::sim::LobbyMessage& message : messages)
    {
        const std::vector<std::uint8_t> bytes =
            og::sim::serialize_lobby_message(message);
        const std::optional<og::sim::LobbyMessage> decoded =
            og::sim::deserialize_lobby_message(bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(message, *decoded);

        const og::sim::TypedReceivedMessage typed =
            og::sim::decode_received_message({.peer_id = 5u, .data = bytes});
        EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage, typed.kind);
        ASSERT_NE(nullptr, typed.lobby_message);
        EXPECT_EQ(message, *typed.lobby_message);
    }

    std::vector<std::uint8_t> bad_kind =
        og::sim::serialize_lobby_message(leave);
    bad_kind[og::sim::kTransportHeaderSize] = 0xffu;
    EXPECT_FALSE(og::sim::deserialize_lobby_message(bad_kind).has_value());
    EXPECT_EQ(
        og::sim::TypedReceivedMessageKind::Malformed,
        og::sim::decode_received_message({.peer_id = 9u, .data = bad_kind}).kind);
}

TEST(NetTransport, lobby_state_roundtrip_and_decode_received_message)
{
    const og::sim::LobbyState expected = make_lobby_state_for_test();

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_lobby_state_message(expected);
    const std::optional<og::sim::LobbyState> decoded =
        og::sim::deserialize_lobby_state_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected, *decoded);
    EXPECT_EQ(0x10293847u, decoded->last_join_request_id);
    EXPECT_TRUE(decoded->local_peer_is_host);

    const og::sim::TypedReceivedMessage typed =
        og::sim::decode_received_message({.peer_id = 6u, .data = bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyState, typed.kind);
    ASSERT_NE(nullptr, typed.lobby_state);
    EXPECT_EQ(expected, *typed.lobby_state);
}

TEST(NetTransport, lobby_remove_seat_rejects_truncated_stable_token)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = 7u,
        .seat_id = 0x10293847u,
    };
    std::vector<std::uint8_t> bytes =
        og::sim::serialize_lobby_message(message);
    ASSERT_GT(bytes.size(), og::sim::kTransportHeaderSize);

    bytes.pop_back();
    const std::uint16_t payload_size = static_cast<std::uint16_t>(
        bytes.size() - og::sim::kTransportHeaderSize);
    bytes[2] = static_cast<std::uint8_t>(payload_size & 0xffu);
    bytes[3] = static_cast<std::uint8_t>((payload_size >> 8) & 0xffu);
    EXPECT_FALSE(og::sim::deserialize_lobby_message(bytes).has_value());
}

// Protocol v8 (company-basecamp design §4.1/§4.8): the slot deployed flag,
// the player company string, the settings cross_control field, and the
// LobbyState last_start_denial echo must survive the wire. The reserved
// slot_flags bits are tolerated (masked to bit0), and an over-length company
// name clamps to 40 chars rather than being rejected.
TEST(NetTransport, v8_deploy_company_cross_control_and_denial_fields_round_trip)
{
    // Cross-control and last_start_denial travel on the lobby state; the
    // test helper sets both non-default, and the roundtrip above already
    // covers equality — assert the raw values reached the decode explicitly.
    const og::sim::LobbyState state = make_lobby_state_for_test();
    const auto state_bytes = og::sim::serialize_lobby_state_message(state);
    const auto decoded_state =
        og::sim::deserialize_lobby_state_message(state_bytes);
    ASSERT_TRUE(decoded_state.has_value());
    EXPECT_EQ(1, decoded_state->settings.cross_control);
    EXPECT_EQ(1, decoded_state->settings.infinite_gold)
        << "protocol v11 appends infinite_gold after cross_control";
    EXPECT_EQ(1, decoded_state->settings.shared_teams)
        << "protocol v12 appends shared_teams after infinite_gold";
    EXPECT_EQ(og::sim::start_denial_reason_value(
                  og::sim::StartDenialReason::MachinesNotReady),
              decoded_state->last_start_denial);
    ASSERT_EQ(1u, decoded_state->players.size());
    EXPECT_EQ("Ari's Company", decoded_state->players.front().company);
    ASSERT_EQ(1u, decoded_state->players.front().character_slots.size());
    // The helper's slot is deployed=false; it must survive the wire.
    EXPECT_FALSE(
        decoded_state->players.front().character_slots.front().deployed);

    // Locate the slot_flags byte structurally by diffing a deployed vs a
    // benched serialization of the same single-seat join, then verify the
    // writer wrote exactly bit0 for each state.
    const auto make_join = [](bool deployed) {
        og::sim::LobbyPlayer player;
        player.player_index = 0u; // empty name + empty company
        player.character_slots.push_back(og::sim::LobbyCharacterSlot{
            .slot_index = 0u,
            .character = {},
            .deployed = deployed,
        });
        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyJoinMessage{.player = player};
        return og::sim::serialize_lobby_message(message);
    };
    const auto deployed_bytes = make_join(true);
    const auto benched_bytes = make_join(false);
    ASSERT_EQ(deployed_bytes.size(), benched_bytes.size());
    std::optional<std::size_t> flags_offset;
    for (std::size_t i = 0; i < deployed_bytes.size(); ++i)
    {
        if (deployed_bytes[i] != benched_bytes[i])
        {
            ASSERT_FALSE(flags_offset.has_value())
                << "only the slot_flags byte should differ";
            flags_offset = i;
        }
    }
    ASSERT_TRUE(flags_offset.has_value());
    EXPECT_EQ(0x01u, deployed_bytes[*flags_offset]);
    EXPECT_EQ(0x00u, benched_bytes[*flags_offset]);

    // Reserved-bit tolerance: a peer that sets bits 1-7 is still read as
    // deployed==bit0 (0xfe clears bit0 -> benched; 0xff sets it -> deployed).
    const auto decode_with_flags = [&](std::uint8_t flags) {
        auto crafted = deployed_bytes;
        crafted[*flags_offset] = flags;
        const auto decoded = og::sim::deserialize_lobby_message(crafted);
        return std::get<og::sim::LobbyJoinMessage>(decoded.value().payload)
            .player.character_slots.front()
            .deployed;
    };
    EXPECT_TRUE(decode_with_flags(0xffu));
    EXPECT_FALSE(decode_with_flags(0xfeu));
    EXPECT_TRUE(decode_with_flags(0x03u));

    // Company name clamps to 40 chars on read rather than rejecting.
    og::sim::LobbyPlayer long_company_player;
    long_company_player.player_index = 0u;
    long_company_player.company = std::string(50, 'C');
    og::sim::LobbyMessage long_message;
    long_message.payload =
        og::sim::LobbyJoinMessage{.player = long_company_player};
    const auto long_bytes = og::sim::serialize_lobby_message(long_message);
    const auto long_decoded = og::sim::deserialize_lobby_message(long_bytes);
    ASSERT_TRUE(long_decoded.has_value());
    const auto& clamped =
        std::get<og::sim::LobbyJoinMessage>(long_decoded->payload).player;
    EXPECT_EQ(og::sim::kMaxLobbyCompanyNameLength, clamped.company.size());
    EXPECT_EQ(std::string(og::sim::kMaxLobbyCompanyNameLength, 'C'),
              clamped.company);
}

// Staged-lobby hardening (#218): guy and player names clamp on READ to their
// honest widths (guy = the 11-char disk field, player = the same 40-char cap
// company already has). Unbounded names let two hostile peers push the merged
// LobbyState past the u16 wire cap, and the resulting serialize throw
// escaped LobbyServer::broadcast_state — a process exit on openglad_server.
TEST(NetTransport, guy_and_player_names_clamp_on_read)
{
    og::sim::LobbyPlayer player;
    player.player_index = 0u;
    player.name = std::string(50, 'N');
    og::sim::LobbyCharacterSlot slot;
    slot.slot_index = 0u;
    slot.deployed = true;
    slot.character.name = std::string(64, 'G');
    player.character_slots.push_back(slot);

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{.player = player};
    const auto bytes = og::sim::serialize_lobby_message(message);
    const auto decoded = og::sim::deserialize_lobby_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    const auto& read_player =
        std::get<og::sim::LobbyJoinMessage>(decoded->payload).player;
    EXPECT_EQ(std::string(og::sim::kMaxLobbyCompanyNameLength, 'N'),
              read_player.name);
    ASSERT_EQ(1u, read_player.character_slots.size());
    EXPECT_EQ(std::string(og::sim::kMaxLobbyGuyNameLength, 'G'),
              read_player.character_slots.front().character.name);

    // The shared guy reader guards InitialSetup rosters identically.
    og::sim::InitialSetupMessage setup;
    og::sim::InitialSetupGuyData guy;
    guy.guy_id = 4;
    guy.name = std::string(64, 'S');
    setup.guys.push_back(guy);
    const auto setup_bytes = og::sim::serialize_initial_setup_message(setup);
    const auto setup_decoded =
        og::sim::deserialize_initial_setup_message(setup_bytes);
    ASSERT_TRUE(setup_decoded.has_value());
    ASSERT_EQ(1u, setup_decoded->guys.size());
    EXPECT_EQ(std::string(og::sim::kMaxLobbyGuyNameLength, 'S'),
              setup_decoded->guys.front().name);
}

TEST(NetTransport, lightweight_message_roundtrips_and_decode)
{
    const auto expect_client_ready = [] {
        const og::sim::ClientReadyMessage expected{.last_applied_tick = 77u};
        const auto bytes = og::sim::serialize_client_ready_message(expected);
        const auto decoded = og::sim::deserialize_client_ready_message(bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(expected, *decoded);
        const auto typed =
            og::sim::decode_received_message({.peer_id = 1u, .data = bytes});
        EXPECT_EQ(og::sim::TypedReceivedMessageKind::ClientReady, typed.kind);
        ASSERT_NE(nullptr, typed.client_ready);
        EXPECT_EQ(expected, *typed.client_ready);
    };
    expect_client_ready();

    const auto expect_keyframe = [] {
        const og::sim::KeyframeRequestMessage expected{.last_seen_tick = 88u};
        const auto bytes = og::sim::serialize_keyframe_request_message(expected);
        const auto decoded = og::sim::deserialize_keyframe_request_message(bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(expected, *decoded);
        const auto typed =
            og::sim::decode_received_message({.peer_id = 1u, .data = bytes});
        EXPECT_EQ(og::sim::TypedReceivedMessageKind::KeyframeRequest, typed.kind);
        ASSERT_NE(nullptr, typed.keyframe_request);
        EXPECT_EQ(expected, *typed.keyframe_request);
    };
    expect_keyframe();

    const og::sim::HeartbeatMessage heartbeat;
    const auto heartbeat_bytes = og::sim::serialize_heartbeat_message(heartbeat);
    ASSERT_TRUE(og::sim::deserialize_heartbeat_message(heartbeat_bytes).has_value());
    const auto heartbeat_typed =
        og::sim::decode_received_message({.peer_id = 1u, .data = heartbeat_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Heartbeat, heartbeat_typed.kind);
    ASSERT_NE(nullptr, heartbeat_typed.heartbeat);
    auto bad_heartbeat = heartbeat_bytes;
    bad_heartbeat.push_back(0u);
    bad_heartbeat[2] = 1u;
    EXPECT_FALSE(og::sim::deserialize_heartbeat_message(bad_heartbeat).has_value());

    const og::sim::ExitPromptBroadcastMessage exit_broadcast{
        .destination_level = 3,
        .withdraw_prompt = true,
        .prompt_text = "Leave now?",
    };
    const auto exit_broadcast_bytes =
        og::sim::serialize_exit_prompt_broadcast_message(exit_broadcast);
    ASSERT_EQ(exit_broadcast,
              *og::sim::deserialize_exit_prompt_broadcast_message(
                  exit_broadcast_bytes));
    const auto exit_broadcast_typed =
        og::sim::decode_received_message({.peer_id = 1u,
                                          .data = exit_broadcast_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::ExitPromptBroadcast,
              exit_broadcast_typed.kind);
    ASSERT_NE(nullptr, exit_broadcast_typed.exit_prompt_broadcast);
    EXPECT_EQ(exit_broadcast, *exit_broadcast_typed.exit_prompt_broadcast);

    const og::sim::ExitPromptResponseMessage exit_response{
        .accepted = true,
        .abort_request = true,
    };
    const auto exit_response_bytes =
        og::sim::serialize_exit_prompt_response_message(exit_response);
    ASSERT_EQ(exit_response,
              *og::sim::deserialize_exit_prompt_response_message(
                  exit_response_bytes));
    const auto exit_response_typed =
        og::sim::decode_received_message({.peer_id = 1u,
                                          .data = exit_response_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::ExitPromptResponse,
              exit_response_typed.kind);
    ASSERT_NE(nullptr, exit_response_typed.exit_prompt_response);
    EXPECT_EQ(exit_response, *exit_response_typed.exit_prompt_response);

    const og::sim::PauseBroadcastMessage pause_broadcast{
        .player_index = 2u,
        .player_name = "Player",
    };
    const auto pause_broadcast_bytes =
        og::sim::serialize_pause_broadcast_message(pause_broadcast);
    ASSERT_EQ(pause_broadcast,
              *og::sim::deserialize_pause_broadcast_message(
                  pause_broadcast_bytes));
    const auto pause_broadcast_typed =
        og::sim::decode_received_message({.peer_id = 1u,
                                          .data = pause_broadcast_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PauseBroadcast,
              pause_broadcast_typed.kind);
    ASSERT_NE(nullptr, pause_broadcast_typed.pause_broadcast);
    EXPECT_EQ(pause_broadcast, *pause_broadcast_typed.pause_broadcast);

    const og::sim::PauseResponseMessage pause_response{.resume = false};
    const auto pause_response_bytes =
        og::sim::serialize_pause_response_message(pause_response);
    ASSERT_EQ(pause_response,
              *og::sim::deserialize_pause_response_message(pause_response_bytes));
    const auto pause_response_typed =
        og::sim::decode_received_message({.peer_id = 1u,
                                          .data = pause_response_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PauseResponse,
              pause_response_typed.kind);
    ASSERT_NE(nullptr, pause_response_typed.pause_response);
    EXPECT_EQ(pause_response, *pause_response_typed.pause_response);

    const og::sim::ControlChangeMessage control_change{
        .player_index = 3u,
        .entity_id = 404u,
    };
    const auto control_change_bytes =
        og::sim::serialize_control_change_message(control_change);
    ASSERT_EQ(control_change,
              *og::sim::deserialize_control_change_message(control_change_bytes));
    const auto control_change_typed =
        og::sim::decode_received_message({.peer_id = 1u,
                                          .data = control_change_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::ControlChange,
              control_change_typed.kind);
    ASSERT_NE(nullptr, control_change_typed.control_change);
    EXPECT_EQ(control_change, *control_change_typed.control_change);

    const og::sim::SnapshotHashCheckMessage hash_check{
        .tick = 500u,
        .snapshot_hash = 0xabcdef12u,
    };
    const auto hash_check_bytes =
        og::sim::serialize_snapshot_hash_check_message(hash_check);
    ASSERT_EQ(hash_check,
              *og::sim::deserialize_snapshot_hash_check_message(hash_check_bytes));
    const auto hash_check_typed =
        og::sim::decode_received_message({.peer_id = 1u,
                                          .data = hash_check_bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::SnapshotHashCheck,
              hash_check_typed.kind);
    ASSERT_NE(nullptr, hash_check_typed.snapshot_hash_check);
    EXPECT_EQ(hash_check, *hash_check_typed.snapshot_hash_check);
}

TEST(NetTransport, default_send_wrappers_emit_messages_and_ignore_nulls)
{
    MockTransport transport;

    EXPECT_FALSE(transport.supports_typed_messages());
    transport.send_snapshot(1u, {});
    transport.send_delta_snapshot(1u, {});
    transport.send_input(1u, {}, 12u);
    transport.send_sim_event_batch(1u, {});
    transport.send_game_flow_event_batch(1u, {});
    transport.send_lobby_message(1u, {});
    transport.send_lobby_state(1u, {});
    transport.send_initial_setup(1u, {});
    transport.send_hello(1u, {});
    transport.send_client_ready(1u, {});
    transport.send_keyframe_request(1u, {});
    transport.send_heartbeat(1u, {});
    transport.send_exit_prompt_broadcast(1u, {});
    transport.send_exit_prompt_response(1u, {});
    transport.send_pause_broadcast(1u, {});
    transport.send_pause_response(1u, {});
    transport.send_control_change(1u, {});
    transport.send_snapshot_hash_check(1u, {});
    EXPECT_TRUE(transport.sent_messages().empty());

    auto snapshot = std::make_shared<og::sim::WorldSnapshot>();
    snapshot->tick_count = 99u;
    snapshot->rng_state = 123u;
    transport.send_snapshot(2u, snapshot);

    auto delta = std::make_shared<og::sim::WorldSnapshot>();
    delta->tick_count = 100u;
    delta->level_tick_count = 3u;
    delta->level_done = 1;
    transport.send_delta_snapshot(2u, delta);

    auto input = std::make_shared<InputState>();
    input->quit_requested = true;
    transport.send_input(2u, input, 44u);

    auto sim_batch = std::make_shared<og::sim::SimEventBatch>();
    sim_batch->sequence = 5u;
    sim_batch->events.push_back({
        .tick = 5u,
        .kind = og::sim::EventKind::Notification,
        .a = 0u,
        .b = 0u,
        .text = "sim-event",
    });
    transport.send_sim_event_batch(2u, sim_batch);

    auto game_flow_batch = std::make_shared<og::sim::SimEventBatch>();
    game_flow_batch->sequence = 6u;
    game_flow_batch->events.push_back({
        .tick = 6u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 2u,
        .text = {},
    });
    transport.send_game_flow_event_batch(2u, game_flow_batch);

    const auto lobby_message = std::make_shared<og::sim::LobbyMessage>();
    lobby_message->payload =
        og::sim::LobbyStartGameMessage{.player_index = 2u};
    transport.send_lobby_message(2u, lobby_message);

    transport.send_lobby_state(
        2u,
        std::make_shared<og::sim::LobbyState>(make_lobby_state_for_test()));
    transport.send_initial_setup(
        2u,
        std::make_shared<og::sim::InitialSetupMessage>(
            og::sim::InitialSetupMessage{}));
    transport.send_hello(
        2u,
        std::make_shared<og::sim::HelloMessage>(og::sim::HelloMessage{}));
    transport.send_client_ready(
        2u,
        std::make_shared<og::sim::ClientReadyMessage>(
            og::sim::ClientReadyMessage{.last_applied_tick = 1u}));
    transport.send_keyframe_request(
        2u,
        std::make_shared<og::sim::KeyframeRequestMessage>(
            og::sim::KeyframeRequestMessage{.last_seen_tick = 2u}));
    transport.send_heartbeat(
        2u,
        std::make_shared<og::sim::HeartbeatMessage>(
            og::sim::HeartbeatMessage{}));
    transport.send_exit_prompt_broadcast(
        2u,
        std::make_shared<og::sim::ExitPromptBroadcastMessage>(
            og::sim::ExitPromptBroadcastMessage{
                .destination_level = 1,
                .withdraw_prompt = false,
                .prompt_text = "Prompt",
            }));
    transport.send_exit_prompt_response(
        2u,
        std::make_shared<og::sim::ExitPromptResponseMessage>(
            og::sim::ExitPromptResponseMessage{
                .accepted = true,
                .abort_request = false,
            }));
    transport.send_pause_broadcast(
        2u,
        std::make_shared<og::sim::PauseBroadcastMessage>(
            og::sim::PauseBroadcastMessage{
                .player_index = 3u,
                .player_name = "Player",
            }));
    transport.send_pause_response(
        2u,
        std::make_shared<og::sim::PauseResponseMessage>(
            og::sim::PauseResponseMessage{.resume = true}));
    transport.send_control_change(
        2u,
        std::make_shared<og::sim::ControlChangeMessage>(
            og::sim::ControlChangeMessage{
                .player_index = 4u,
                .entity_id = 5u,
            }));
    transport.send_snapshot_hash_check(
        2u,
        std::make_shared<og::sim::SnapshotHashCheckMessage>(
            og::sim::SnapshotHashCheckMessage{
                .tick = 6u,
                .snapshot_hash = 7u,
            }));

    const std::array<og::sim::TypedReceivedMessageKind, 18> expected_kinds = {
        og::sim::TypedReceivedMessageKind::Snapshot,
        og::sim::TypedReceivedMessageKind::DeltaSnapshot,
        og::sim::TypedReceivedMessageKind::Input,
        og::sim::TypedReceivedMessageKind::SimEventBatch,
        og::sim::TypedReceivedMessageKind::GameFlowEventBatch,
        og::sim::TypedReceivedMessageKind::LobbyMessage,
        og::sim::TypedReceivedMessageKind::LobbyState,
        og::sim::TypedReceivedMessageKind::InitialSetup,
        og::sim::TypedReceivedMessageKind::Hello,
        og::sim::TypedReceivedMessageKind::ClientReady,
        og::sim::TypedReceivedMessageKind::KeyframeRequest,
        og::sim::TypedReceivedMessageKind::Heartbeat,
        og::sim::TypedReceivedMessageKind::ExitPromptBroadcast,
        og::sim::TypedReceivedMessageKind::ExitPromptResponse,
        og::sim::TypedReceivedMessageKind::PauseBroadcast,
        og::sim::TypedReceivedMessageKind::PauseResponse,
        og::sim::TypedReceivedMessageKind::ControlChange,
        og::sim::TypedReceivedMessageKind::SnapshotHashCheck,
    };
    ASSERT_EQ(expected_kinds.size(), transport.sent_messages().size());
    for (std::size_t i = 0; i < transport.sent_messages().size(); ++i)
    {
        const og::sim::ReceivedMessage& sent = transport.sent_messages()[i];
        EXPECT_EQ(2u, sent.peer_id);
        const og::sim::TypedReceivedMessage typed =
            og::sim::decode_received_message(sent);
        EXPECT_EQ(expected_kinds[i], typed.kind);
        switch (typed.kind)
        {
        case og::sim::TypedReceivedMessageKind::Snapshot:
            ASSERT_NE(nullptr, typed.snapshot);
            EXPECT_EQ(99u, typed.snapshot->tick_count);
            EXPECT_EQ(123u, typed.snapshot->rng_state);
            break;
        case og::sim::TypedReceivedMessageKind::DeltaSnapshot:
            ASSERT_NE(nullptr, typed.snapshot);
            EXPECT_EQ(100u, typed.snapshot->tick_count);
            EXPECT_EQ(3u, typed.snapshot->level_tick_count);
            EXPECT_EQ(1, typed.snapshot->level_done);
            break;
        case og::sim::TypedReceivedMessageKind::Input:
            ASSERT_NE(nullptr, typed.input);
            EXPECT_EQ(44u, typed.tick);
            EXPECT_TRUE(typed.input->quit_requested);
            break;
        case og::sim::TypedReceivedMessageKind::SimEventBatch:
            ASSERT_NE(nullptr, typed.event_batch);
            EXPECT_EQ(sim_batch->sequence, typed.event_batch->sequence);
            ASSERT_EQ(sim_batch->events.size(), typed.event_batch->events.size());
            EXPECT_EQ(sim_batch->events[0], typed.event_batch->events[0]);
            break;
        case og::sim::TypedReceivedMessageKind::GameFlowEventBatch:
            ASSERT_NE(nullptr, typed.event_batch);
            EXPECT_EQ(game_flow_batch->sequence, typed.event_batch->sequence);
            ASSERT_EQ(game_flow_batch->events.size(),
                      typed.event_batch->events.size());
            EXPECT_EQ(game_flow_batch->events[0], typed.event_batch->events[0]);
            break;
        default:
            EXPECT_NE(og::sim::TypedReceivedMessageKind::Malformed,
                      typed.kind);
            break;
        }
    }
}

TEST(NetTransport, decode_received_message_reports_malformed_inputs)
{
    EXPECT_EQ(
        og::sim::TypedReceivedMessageKind::Malformed,
        og::sim::decode_received_message({.peer_id = 7u, .data = {0x01}}).kind);

    std::vector<std::uint8_t> unknown_message;
    og::sim::append_transport_header(unknown_message, 0xffu, 0u);
    EXPECT_EQ(
        og::sim::TypedReceivedMessageKind::Malformed,
        og::sim::decode_received_message({.peer_id = 7u,
                                          .data = unknown_message}).kind);

    std::vector<std::uint8_t> malformed_client_ready;
    og::sim::append_transport_header(
        malformed_client_ready,
        og::sim::kClientReadyMessageType,
        1u);
    malformed_client_ready.push_back(0u);
    EXPECT_EQ(
        og::sim::TypedReceivedMessageKind::Malformed,
        og::sim::decode_received_message({.peer_id = 7u,
                                          .data = malformed_client_ready}).kind);
}

TEST(NetTransport, decode_received_message_reports_malformed_typed_payloads)
{
    const auto malformed_payload = [](std::uint8_t message_type,
                                      std::uint16_t payload_length) {
        std::vector<std::uint8_t> bytes;
        og::sim::append_transport_header(bytes, message_type, payload_length);
        bytes.resize(og::sim::kTransportHeaderSize + payload_length, 0u);
        return bytes;
    };

    const std::array<std::pair<std::uint8_t, std::uint16_t>, 22> cases = {
        std::pair{og::sim::kSnapshotMessageType, 1u},
        std::pair{og::sim::kDeltaSnapshotMessageType, 1u},
        std::pair{og::sim::kInputMessageType, 1u},
        std::pair{og::sim::kSimEventBatchMessageType, 1u},
        std::pair{og::sim::kGameFlowEventBatchMessageType, 1u},
        std::pair{og::sim::kLobbyMessageType, 1u},
        std::pair{og::sim::kLobbyStateMessageType, 1u},
        std::pair{og::sim::kInitialSetupMessageType, 1u},
        std::pair{og::sim::kHelloMessageType, 1u},
        std::pair{og::sim::kClientReadyMessageType, 1u},
        std::pair{og::sim::kKeyframeRequestMessageType, 1u},
        std::pair{og::sim::kHeartbeatMessageType, 1u},
        std::pair{og::sim::kExitPromptBroadcastMessageType, 1u},
        std::pair{og::sim::kExitPromptResponseMessageType, 1u},
        std::pair{og::sim::kPauseBroadcastMessageType, 1u},
        std::pair{og::sim::kPauseResponseMessageType, 0u},
        std::pair{og::sim::kControlChangeMessageType, 1u},
        std::pair{og::sim::kSnapshotHashCheckMessageType, 1u},
        std::pair{og::sim::kPackManifestMessageType, 1u},
        std::pair{og::sim::kPackRequestMessageType, 1u},
        std::pair{og::sim::kPackFileChunkMessageType, 1u},
        std::pair{og::sim::kPackTransferDoneMessageType, 1u},
    };

    for (const auto& [message_type, payload_length] : cases)
    {
        const og::sim::TypedReceivedMessage decoded =
            og::sim::decode_received_message({
                .peer_id = 7u,
                .data = malformed_payload(message_type, payload_length),
            });
        EXPECT_EQ(7u, decoded.peer_id);
        EXPECT_EQ(og::sim::TypedReceivedMessageKind::Malformed,
                  decoded.kind)
            << "message type " << static_cast<int>(message_type);
    }
}

TEST(NetTransport, serialize_hello_emits_expected_wire_format)
{
    og::sim::HelloMessage message;
    message.snapshot_format_version = 3;
    message.session_token = {
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f,
    };
    message.campaign_content_hash = 0x11223344u;

    constexpr std::array<std::uint8_t, og::sim::kSerializedHelloMessageSize>
        expected = {
            0x0c, 0x01, 0x17, 0x00,
            0x0c, 0x0c, 0x03,
            0x00, 0x01, 0x02, 0x03,
            0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b,
            0x0c, 0x0d, 0x0e, 0x0f,
            0x44, 0x33, 0x22, 0x11,
        };

    EXPECT_EQ(expected, og::sim::serialize_hello(message));
}

TEST(NetTransport, hello_roundtrip_preserves_versions_token_and_campaign_hash)
{
    og::sim::HelloMessage expected;
    expected.snapshot_format_version = 7;
    expected.session_token = {
        0xf0, 0xe1, 0xd2, 0xc3,
        0xb4, 0xa5, 0x96, 0x87,
        0x78, 0x69, 0x5a, 0x4b,
        0x3c, 0x2d, 0x1e, 0x0f,
    };
    expected.campaign_content_hash = 0xa1b2c3d4u;

    const auto bytes = og::sim::serialize_hello(expected);
    const std::optional<og::sim::HelloMessage> decoded =
        og::sim::deserialize_hello_message(bytes);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected.protocol_version, decoded->protocol_version);
    EXPECT_EQ(expected.min_protocol_version, decoded->min_protocol_version);
    EXPECT_EQ(expected.snapshot_format_version,
              decoded->snapshot_format_version);
    EXPECT_EQ(expected.session_token, decoded->session_token);
    EXPECT_EQ(expected.campaign_content_hash, decoded->campaign_content_hash);
}

TEST(NetTransport, decode_rejects_truncated_and_wrong_version_headers)
{
    og::sim::TransportEnvelope envelope;

    const std::array<std::uint8_t, 3> truncated = {0x03, 0x01, 0x00};
    EXPECT_FALSE(og::sim::decode_transport_envelope(truncated, envelope));

    const std::array<std::uint8_t, 4> wrong_version = {
        og::sim::kNetworkProtocolVersion + 1, 0x01, 0x00, 0x00};
    EXPECT_FALSE(og::sim::decode_transport_envelope(wrong_version, envelope));
}

TEST(NetTransport, deserialize_initial_setup_rejects_oversized_counts)
{
    const auto bytes = og::sim::serialize_initial_setup_message(
        og::sim::InitialSetupMessage{});

    auto oversized_guy_count = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    write_u32_le(oversized_guy_count, 37, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(oversized_guy_count)
            .has_value());

    auto oversized_level_count = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    write_u32_le(oversized_level_count, 41, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(oversized_level_count)
            .has_value());
}

// v7: the trailing controlled-entity-id block is u8-count-prefixed. The count
// byte sits kMaxGlobalPlayers u32s + 1 from the end of the message.
namespace {
std::size_t controlled_id_count_offset(const std::vector<std::uint8_t>& bytes)
{
    return bytes.size() -
        (1u + og::sim::kMaxGlobalPlayers * sizeof(std::uint32_t));
}
} // namespace

TEST(NetTransport, initial_setup_controlled_ids_count_prefix_roundtrip)
{
    og::sim::InitialSetupMessage expected;
    expected.controlled_entity_ids[0] = 11u;
    expected.controlled_entity_ids[3] = 44u;
    expected.controlled_entity_ids[og::sim::kMaxGlobalPlayers - 1] = 160u;

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_initial_setup_message(expected);
    // Sender always writes the full kMaxGlobalPlayers block.
    EXPECT_EQ(static_cast<std::uint8_t>(og::sim::kMaxGlobalPlayers),
              bytes[controlled_id_count_offset(bytes)]);

    const auto decoded = og::sim::deserialize_initial_setup_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected, *decoded);

    // A shorter (still valid) count decodes with the tail zero-filled.
    const std::size_t count_offset = controlled_id_count_offset(bytes);
    std::vector<std::uint8_t> short_count(bytes.begin(),
                                          bytes.begin() +
                                              static_cast<std::ptrdiff_t>(count_offset));
    short_count.push_back(4u);
    short_count.insert(
        short_count.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(count_offset + 1),
        bytes.begin() + static_cast<std::ptrdiff_t>(
            count_offset + 1 + 4 * sizeof(std::uint32_t)));
    const std::uint16_t payload_length = static_cast<std::uint16_t>(
        short_count.size() - og::sim::kTransportHeaderSize);
    short_count[2] = static_cast<std::uint8_t>(payload_length & 0xffu);
    short_count[3] = static_cast<std::uint8_t>((payload_length >> 8) & 0xffu);

    const auto short_decoded =
        og::sim::deserialize_initial_setup_message(short_count);
    ASSERT_TRUE(short_decoded.has_value());
    EXPECT_EQ(11u, short_decoded->controlled_entity_ids[0]);
    EXPECT_EQ(44u, short_decoded->controlled_entity_ids[3]);
    EXPECT_EQ(0u,
              short_decoded->controlled_entity_ids[og::sim::kMaxGlobalPlayers - 1]);
}

TEST(NetTransport, initial_setup_rejects_oversized_controlled_id_count)
{
    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_initial_setup_message(og::sim::InitialSetupMessage{});

    auto oversized = bytes;
    oversized[controlled_id_count_offset(bytes)] = static_cast<std::uint8_t>(
        og::sim::kMaxGlobalPlayers + 1);
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(oversized).has_value());

    // A count that under-claims the bytes actually present must also fail
    // (the strict finished() check refuses trailing garbage).
    auto undersized = bytes;
    undersized[controlled_id_count_offset(bytes)] = 2u;
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(undersized).has_value());
}

TEST(NetTransport, lobby_join_extra_players_roundtrip)
{
    // Zero extra seats: byte-level shape is the Join nonce, seat 0, then a
    // zero extra-seat count.
    og::sim::LobbyMessage single;
    single.payload = og::sim::LobbyJoinMessage{
        .player = make_lobby_player_for_test(),
        .request_id = 0x13572468u,
    };
    const auto single_bytes = og::sim::serialize_lobby_message(single);
    const auto single_decoded = og::sim::deserialize_lobby_message(single_bytes);
    ASSERT_TRUE(single_decoded.has_value());
    EXPECT_EQ(single, *single_decoded);
    const auto& decoded_single =
        std::get<og::sim::LobbyJoinMessage>(single_decoded->payload);
    EXPECT_TRUE(decoded_single.extra_players.empty());
    EXPECT_EQ(0x13572468u, decoded_single.request_id);

    // Three extra seats (a 4-seat machine) roundtrip in order.
    og::sim::LobbyJoinMessage join;
    join.player = make_lobby_player_for_test();
    join.request_id = 0x24681357u;
    for (int seat = 1; seat <= 3; ++seat)
    {
        og::sim::LobbyPlayer extra = make_lobby_player_for_test();
        extra.name = std::string("seat#") + std::to_string(seat);
        extra.team = static_cast<std::int16_t>(seat);
        join.extra_players.push_back(std::move(extra));
    }
    og::sim::LobbyMessage message;
    message.payload = join;

    const auto bytes = og::sim::serialize_lobby_message(message);
    const auto decoded = og::sim::deserialize_lobby_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(message, *decoded);
    const auto& decoded_join =
        std::get<og::sim::LobbyJoinMessage>(decoded->payload);
    ASSERT_EQ(3u, decoded_join.extra_players.size());
    EXPECT_EQ(0x24681357u, decoded_join.request_id);
    EXPECT_EQ("seat#1", decoded_join.extra_players[0].name);
    EXPECT_EQ(3, decoded_join.extra_players[2].team);

    // decode_received_message routes the multi-seat join too.
    const og::sim::TypedReceivedMessage typed =
        og::sim::decode_received_message({.peer_id = 6u, .data = bytes});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage, typed.kind);
    ASSERT_NE(nullptr, typed.lobby_message);
    EXPECT_EQ(message, *typed.lobby_message);
}

TEST(NetTransport, lobby_join_rejects_malformed_extra_seat_count)
{
    og::sim::LobbyMessage message;
    message.payload =
        og::sim::LobbyJoinMessage{.player = make_lobby_player_for_test()};

    // With zero extras the extra-seat count is the final payload byte; a
    // crafted count with no seat bytes behind it must be rejected.
    auto bytes = og::sim::serialize_lobby_message(message);
    ASSERT_EQ(0u, bytes.back());
    bytes.back() = 0xffu;
    EXPECT_FALSE(og::sim::deserialize_lobby_message(bytes).has_value());

    // A count that claims more seats than the payload carries is rejected
    // even when some seat bytes follow.
    og::sim::LobbyJoinMessage join;
    join.player = make_lobby_player_for_test();
    join.extra_players.push_back(make_lobby_player_for_test());
    og::sim::LobbyMessage with_extra;
    with_extra.payload = join;
    auto extra_bytes = og::sim::serialize_lobby_message(with_extra);
    // The count byte precedes the serialized extra seat; recompute its offset
    // from the single-seat encoding (same prefix).
    const auto single_bytes = og::sim::serialize_lobby_message(message);
    const std::size_t count_offset = single_bytes.size() - 1;
    ASSERT_EQ(1u, extra_bytes[count_offset]);
    extra_bytes[count_offset] = 0xffu;
    EXPECT_FALSE(og::sim::deserialize_lobby_message(extra_bytes).has_value());
}

TEST(NetTransport, deserialize_hello_rejects_wrong_size_version_type_and_range)
{
    og::sim::HelloMessage message;
    message.snapshot_format_version = 5;
    message.campaign_content_hash = 0x55667788u;

    const auto bytes = og::sim::serialize_hello(message);

    ASSERT_FALSE(
        og::sim::deserialize_hello_message(
            std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1))
            .has_value());

    auto bad_version = bytes;
    bad_version[0] = static_cast<std::uint8_t>(
        og::sim::kNetworkProtocolVersion + 1);
    ASSERT_FALSE(og::sim::deserialize_hello_message(bad_version).has_value());

    auto bad_type = bytes;
    bad_type[1] = og::sim::kSnapshotMessageType;
    ASSERT_FALSE(og::sim::deserialize_hello_message(bad_type).has_value());

    auto bad_length = bytes;
    bad_length[2] = 0;
    bad_length[3] = 0;
    ASSERT_FALSE(og::sim::deserialize_hello_message(bad_length).has_value());

    auto bad_payload_version = bytes;
    bad_payload_version[og::sim::kTransportHeaderSize] = static_cast<std::uint8_t>(
        og::sim::kNetworkProtocolVersion + 1);
    ASSERT_FALSE(
        og::sim::deserialize_hello_message(bad_payload_version).has_value());

    auto bad_version_range = bytes;
    bad_version_range[og::sim::kTransportHeaderSize + 1] =
        static_cast<std::uint8_t>(bytes[og::sim::kTransportHeaderSize] + 1);
    ASSERT_FALSE(
        og::sim::deserialize_hello_message(bad_version_range).has_value());
}

TEST(NetTransport,
     non_hello_deserializers_reject_wrong_transport_header_version)
{
    const auto bad_version = [](std::vector<std::uint8_t> bytes) {
        bytes[0] = static_cast<std::uint8_t>(
            og::sim::kNetworkProtocolVersion + 1);
        return bytes;
    };

    const auto initial_setup = bad_version(
        og::sim::serialize_initial_setup_message(og::sim::InitialSetupMessage{}));
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(initial_setup).has_value());

    const auto client_ready = bad_version(
        og::sim::serialize_client_ready_message({.last_applied_tick = 7u}));
    EXPECT_FALSE(
        og::sim::deserialize_client_ready_message(client_ready).has_value());

    const auto heartbeat = bad_version(
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    EXPECT_FALSE(
        og::sim::deserialize_heartbeat_message(heartbeat).has_value());

    const auto control_change = bad_version(
        og::sim::serialize_control_change_message({
            .player_index = 1u,
            .entity_id = 42u,
        }));
    EXPECT_FALSE(
        og::sim::deserialize_control_change_message(control_change).has_value());
}

TEST(NetTransport, interface_is_mockable_and_preserves_message_buffers)
{
    MockTransport transport;
    transport.accept_connections();
    EXPECT_TRUE(transport.accepting_connections());

    transport.set_connected_peers({7u, 11u});
    const std::vector<og::sim::PeerId> peers = transport.connected_peers();
    EXPECT_EQ((std::vector<og::sim::PeerId>{7u, 11u}), peers);

    const std::array<std::uint8_t, 3> outbound = {0xaa, 0xbb, 0xcc};
    transport.send(7u, outbound.data(), outbound.size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(7u, transport.sent_messages().front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0xaa, 0xbb, 0xcc}),
              transport.sent_messages().front().data);

    transport.queue_received(11u, {0x10, 0x20});
    const std::vector<og::sim::ReceivedMessage> received = transport.poll();
    ASSERT_EQ(1u, received.size());
    EXPECT_EQ(11u, received.front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0x10, 0x20}), received.front().data);
    EXPECT_TRUE(transport.poll().empty());

    transport.disconnect(11u);
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u}),
              transport.disconnected_peers());
}

TEST(NetTransport, game_client_send_input_uses_raw_fallback)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 9u);

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 9;
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    input.players[1].pressed[static_cast<int>(InputAction::Fire)] = true;
    client.send_input(input, 12u);

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(9u, transport.sent_messages().front().peer_id);

    const auto expected =
        og::sim::serialize_input(12u, input);
    EXPECT_EQ((std::vector<std::uint8_t>(expected.begin(), expected.end())),
              transport.sent_messages().front().data);
}

TEST(NetTransport,
     game_server_polls_raw_input_messages_when_typed_path_is_unavailable)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 4;
    input.players[0].held[static_cast<int>(InputAction::MoveLeft)] = true;
    input.players[1].pressed[static_cast<int>(InputAction::Fire)] = true;

    const auto bytes = og::sim::serialize_input(14u, input);
    transport.queue_received(
        5u,
        std::vector<std::uint8_t>(bytes.begin(), bytes.end()));

    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.last_polled_messages().size());
    const og::sim::TypedReceivedMessage& message =
        server.last_polled_messages().front();
    EXPECT_EQ(5u, message.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Input, message.kind);
    EXPECT_EQ(14u, message.tick);
    ASSERT_NE(nullptr, message.input);
    expect_input_state_eq(input, *message.input);
}

TEST(NetTransport,
     game_server_polls_raw_lobby_messages_when_typed_path_is_unavailable)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    og::sim::LobbyMessage lobby_message;
    lobby_message.payload =
        og::sim::LobbyJoinMessage{make_lobby_player_for_test()};
    const og::sim::LobbyState lobby_state = make_lobby_state_for_test();

    transport.queue_received(
        5u, og::sim::serialize_lobby_message(lobby_message));
    transport.queue_received(
        5u, og::sim::serialize_lobby_state_message(lobby_state));

    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.last_polled_messages().size());

    const og::sim::TypedReceivedMessage& decoded_message =
        server.last_polled_messages()[0];
    EXPECT_EQ(5u, decoded_message.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              decoded_message.kind);
    ASSERT_NE(nullptr, decoded_message.lobby_message);
    EXPECT_EQ(lobby_message, *decoded_message.lobby_message);

    const og::sim::TypedReceivedMessage& decoded_state =
        server.last_polled_messages()[1];
    EXPECT_EQ(5u, decoded_state.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyState,
              decoded_state.kind);
    ASSERT_NE(nullptr, decoded_state.lobby_state);
    EXPECT_EQ(lobby_state, *decoded_state.lobby_state);
}

TEST(NetTransport, game_server_registers_connected_transport_peers_on_poll)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});

    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    ASSERT_EQ(2u, transport.sent_messages().size());
    EXPECT_EQ(7u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(7u, transport.sent_messages()[1].peer_id);
}

TEST(NetTransport, game_server_drops_removed_transport_peers_on_poll)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    transport.set_connected_peers({});
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    EXPECT_TRUE(transport.disconnected_peers().empty());
    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransport, game_server_keeps_remaining_clients_when_host_peer_is_removed)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u, 13u});
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();

    transport.set_connected_peers({11u, 13u});
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    EXPECT_TRUE(transport.disconnected_peers().empty());

    ASSERT_EQ(4u, transport.sent_messages().size());
    std::vector<og::sim::PeerId> recipients;
    recipients.reserve(transport.sent_messages().size());
    for (const auto& sent : transport.sent_messages())
        recipients.push_back(sent.peer_id);
    std::sort(recipients.begin(), recipients.end());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u, 11u, 13u, 13u}),
              recipients);
}

TEST(NetTransport,
     game_server_malformed_host_peer_does_not_disconnect_other_clients)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u, 13u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    transport.queue_received(7u, {0x01, 0x06, 0x01});
    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());

    transport.set_connected_peers({11u, 13u});
    transport.clear_sent_messages();
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    std::vector<og::sim::PeerId> recipients;
    recipients.reserve(transport.sent_messages().size());
    for (const auto& sent : transport.sent_messages())
        recipients.push_back(sent.peer_id);
    std::sort(recipients.begin(), recipients.end());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u, 11u, 13u, 13u}),
              recipients);
}

TEST(NetTransport,
     game_server_invalid_host_hello_does_not_disconnect_other_clients)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u, 13u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    const auto invalid_hello = og::sim::serialize_hello(og::sim::HelloMessage{});
    transport.queue_received(
        7u,
        std::vector<std::uint8_t>(invalid_hello.begin(), invalid_hello.end()));
    server.step();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());

    std::vector<og::sim::PeerId> recipients;
    recipients.reserve(transport.sent_messages().size());
    for (const auto& sent : transport.sent_messages())
        recipients.push_back(sent.peer_id);
    std::sort(recipients.begin(), recipients.end());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u, 11u, 13u, 13u}),
              recipients);
}

TEST(NetTransport, heartbeat_resets_server_input_timeout)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    std::uint64_t now_ms = 1000;
    server.set_wall_clock_ms_source([&] { return now_ms; });

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) - 1u;
    transport.queue_received(
        7u,
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty());

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) - 1u;
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty());

    now_ms += 2u;
    server.step();
    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
}

TEST(NetTransport, game_client_sends_automatic_heartbeats_when_idle)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    client.poll_messages();

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_TRUE(
        og::sim::deserialize_hello_message(transport.sent_messages()[0].data)
            .has_value());

    transport.clear_sent_messages();
    client.testing_set_last_outbound_activity_elapsed_ms(2100.0f);
    client.poll_messages();

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_TRUE(
        og::sim::deserialize_heartbeat_message(transport.sent_messages()[0].data)
            .has_value());

    transport.clear_sent_messages();
    client.poll_messages();
    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransportJitter, held_input_suppresses_automatic_heartbeat_cadence)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    client.poll_messages();
    transport.clear_sent_messages();

    InputState input{};
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    client.send_input(input, 1u);
    transport.clear_sent_messages();

    client.testing_set_last_outbound_activity_elapsed_ms(1000.0f);
    client.poll_messages();
    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransport, game_client_notifies_when_server_is_gone_for_too_long)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    int connection_lost_count = 0;
    client.set_connection_lost_callback([&connection_lost_count] {
        ++connection_lost_count;
    });

    transport.set_connected_peers({7u});
    client.poll_messages();

    transport.set_connected_peers({});
    client.poll_messages();
    EXPECT_EQ(0, connection_lost_count);

    client.testing_set_transport_disconnect_elapsed_ms(
        static_cast<float>(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS + 1u));
    client.poll_messages();
    EXPECT_EQ(1, connection_lost_count);

    client.poll_messages();
    EXPECT_EQ(1, connection_lost_count);
}

// #175: an installer-supplied control used to skip the bind-time claim, so the
// initial keyframe advertised the walker as the player's control while the
// walker itself still read user == -1. The first tick then claimed it, which
// made tick 0 the one snapshot in a session that never matched the mirror and
// cost the desync detector its first tick.
TEST(NetTransport, bind_player_claims_an_unclaimed_explicit_control)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({11u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    ASSERT_EQ(-1, static_cast<int>(control->user()))
        << "a freshly spawned walker is unclaimed";

    server.bind_player(11u, 0u, fixture.world().my_team, control);

    EXPECT_EQ(0, static_cast<int>(control->user()))
        << "the seat must own its control before the first snapshot leaves";
    EXPECT_EQ(ACT_CONTROL, static_cast<int>(control->act_type()));
}

// The claim is a claim, not an overwrite: a control already owned by another
// seat keeps its owner.
TEST(NetTransport, bind_player_leaves_an_already_claimed_control_alone)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({12u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->set_user(1);

    server.bind_player(12u, 0u, fixture.world().my_team, control);

    EXPECT_EQ(1, static_cast<int>(control->user()));
}

// A peer that reconnects after its released walker died binds against a
// corpse. Claiming that would put a dead walker into ACT_CONTROL.
TEST(NetTransport, bind_player_does_not_claim_a_dead_explicit_control)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({13u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->set_dead(1);

    server.bind_player(13u, 0u, fixture.world().my_team, control);

    EXPECT_EQ(-1, static_cast<int>(control->user()));
}

TEST(NetTransport,
     disconnect_grace_uses_last_pending_held_input_from_removed_peer)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(32, 48);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    server.bind_player(7u, 0u, fixture.world().my_team, control);

    InputState move_right;
    move_right.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    const auto input_bytes = og::sim::serialize_input(1u, move_right);
    transport.queue_received(
        7u,
        std::vector<std::uint8_t>(input_bytes.begin(), input_bytes.end()));
    transport.set_connected_peers({});

    server.step();

    EXPECT_EQ(0, static_cast<int>(control->user()));
    ASSERT_EQ(1u, server.disconnected_players().size());

    const PlayerInput& repeated_input =
        server.disconnected_players().front().repeated_input;
    EXPECT_TRUE(repeated_input.held[static_cast<int>(InputAction::MoveRight)]);
    EXPECT_FALSE(
        repeated_input.pressed[static_cast<int>(InputAction::MoveRight)]);
}

TEST(NetTransport, game_server_snapshot_hash_check_is_strict_per_peer_per_tick)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u});
    server.poll_incoming_messages();

    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot first_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data);
    transport.clear_sent_messages();

    fixture.world().current_palette_id = 1;
    server.send_initial_snapshot(11u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot second_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data);
    ASSERT_EQ(first_snapshot.tick_count, second_snapshot.tick_count);
    ASSERT_NE(first_snapshot.snapshot_hash, second_snapshot.snapshot_hash);

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = first_snapshot.tick_count,
            .snapshot_hash = first_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = first_snapshot.tick_count,
            .snapshot_hash = second_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(1u, server.snapshot_hash_mismatch_count());
}

TEST(NetTransport, game_server_snapshot_hash_check_preserves_same_peer_same_tick_order)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    server.connect_client(7u);

    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot first_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data);
    transport.clear_sent_messages();

    fixture.world().current_palette_id = 1;
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot second_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data);
    ASSERT_EQ(first_snapshot.tick_count, second_snapshot.tick_count);
    ASSERT_NE(first_snapshot.snapshot_hash, second_snapshot.snapshot_hash);

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = first_snapshot.tick_count,
            .snapshot_hash = first_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = second_snapshot.tick_count,
            .snapshot_hash = second_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());
}

TEST(NetTransport,
     game_server_accepts_in_flight_old_level_hashes_after_synchronous_transition)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    constexpr std::array<og::sim::PeerId, 2> peers{7u, 11u};
    transport.set_connected_peers(
        std::vector<og::sim::PeerId>(peers.begin(), peers.end()));
    server.poll_incoming_messages();
    for (std::size_t index = 0; index < peers.size(); ++index)
    {
        server.bind_player(peers[index], index, fixture.world().my_team);
        server.send_initial_snapshot(
            peers[index], og::sim::SnapshotCaptureMode::Peek);
        transport.queue_received(
            peers[index],
            og::sim::serialize_client_ready_message({
                .last_applied_tick = fixture.world().tick_count_,
            }));
    }
    server.step();
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    // Leave an ordinary cadence keyframe unacknowledged, then synchronously
    // send a different terminal keyframe at the same old-level tick and load
    // the next world before either hash check can return.
    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);
    og::sim::WorldSnapshot cadence_snapshot;
    bool saw_cadence_snapshot = false;
    for (const auto& sent : transport.sent_messages())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(sent.data, envelope) ||
            envelope.message_type != og::sim::kSnapshotMessageType)
        {
            continue;
        }
        if (sent.peer_id != peers[0])
            continue;
        cadence_snapshot = og::sim::deserialize_snapshot(sent.data);
        saw_cadence_snapshot = true;
        break;
    }
    ASSERT_TRUE(saw_cadence_snapshot);

    // Peer 0's cadence acknowledgement is already on the wire when the
    // transition begins. prepare_clients_for_loaded_level polls during that
    // synchronous load; it must requeue, not discard, this message. Peer 1
    // exercises the ordinary shape where both old-level checks arrive later.
    transport.queue_received(
        peers[0],
        og::sim::serialize_snapshot_hash_check_message({
            .tick = cadence_snapshot.tick_count,
            .snapshot_hash = cadence_snapshot.snapshot_hash,
        }));
    transport.queue_received(
        peers[0],
        og::sim::serialize_client_ready_message({
            .last_applied_tick = cadence_snapshot.tick_count,
        }));

    transport.clear_sent_messages();
    transport.clear_broadcast_messages();
    fixture.world().current_palette_id = 1u;
    fixture.world().game_ended = true;
    fixture.world().ending = 0;
    fixture.world().next_level = 2;
    server.on_level_transition = [&](int next_level) {
        fixture.world().id = static_cast<short>(next_level);
        fixture.world().current_scenario = static_cast<short>(next_level);
        fixture.world().tick_count_ = 0;
        fixture.world().game_ended = false;
        fixture.world().next_level = -1;
        fixture.world().ending = 0;
        fixture.world().end = 0;
        return true;
    };
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Drain);

    og::sim::WorldSnapshot terminal_snapshot;
    bool saw_terminal_snapshot = false;
    for (const auto& sent : transport.sent_messages())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(sent.data, envelope) ||
            envelope.message_type != og::sim::kSnapshotMessageType)
        {
            continue;
        }
        if (sent.peer_id != peers[0])
            continue;
        terminal_snapshot = og::sim::deserialize_snapshot(sent.data);
        saw_terminal_snapshot = true;
        break;
    }
    ASSERT_TRUE(saw_terminal_snapshot);
    ASSERT_EQ(cadence_snapshot.tick_count, terminal_snapshot.tick_count);
    ASSERT_NE(cadence_snapshot.snapshot_hash, terminal_snapshot.snapshot_hash);
    ASSERT_EQ(2, fixture.world().id);

    transport.clear_sent_messages();
    transport.clear_broadcast_messages();
    transport.queue_received(
        peers[0],
        og::sim::serialize_snapshot_hash_check_message({
            .tick = terminal_snapshot.tick_count,
            .snapshot_hash = terminal_snapshot.snapshot_hash,
        }));
    for (const og::sim::WorldSnapshot* const snapshot :
         {&cadence_snapshot, &terminal_snapshot})
    {
        transport.queue_received(
            peers[1],
            og::sim::serialize_snapshot_hash_check_message({
                .tick = snapshot->tick_count,
                .snapshot_hash = snapshot->snapshot_hash,
            }));
    }
    server.step();

    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count())
        << "late old-level acknowledgements must survive the level reset";
    const bool sent_new_level_snapshot = std::any_of(
        transport.sent_messages().begin(),
        transport.sent_messages().end(),
        [&](const og::sim::ReceivedMessage& sent) {
            if (sent.peer_id != peers[0])
                return false;
            og::sim::TransportEnvelope envelope;
            if (!og::sim::decode_transport_envelope(sent.data, envelope) ||
                envelope.message_type != og::sim::kSnapshotMessageType)
            {
                return false;
            }
            return !og::sim::deserialize_snapshot(sent.data).game_ended;
        });
    EXPECT_TRUE(sent_new_level_snapshot)
        << "a ClientReady racing InitialSetup must not be discarded";
}

TEST(NetTransport, game_server_bounds_sustained_hash_mismatches_with_disconnect)
{
    // WI-3(b) server side: every hash mismatch forces a full keyframe; a
    // REMOTE client that NEVER heals (map-level divergence, or a crafted
    // client) must be cut off after a bounded number of consecutive strikes
    // instead of rubber-banding (and amplifying bandwidth) forever.
    //
    // Every remembered snapshot is consumed by exactly ONE comparison (a
    // mismatched front no longer wedges the queue — the paused seat-churn
    // regression), so each strike here pairs a fresh snapshot with a wrong
    // check. Peer 1 connects first and holds the host exemption; peer 7 is
    // the remote under test.
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    server.connect_client(1u);
    server.connect_client(7u);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);

    // step() ticks this fixture's world, so every keyframe carries the tick
    // it was captured at: each check answers the LATEST sent snapshot.
    const auto last_snapshot = [&transport] {
        return og::sim::deserialize_snapshot(
            transport.sent_messages().back().data);
    };
    const auto queue_check = [&transport, &last_snapshot](bool matching) {
        const og::sim::WorldSnapshot snapshot = last_snapshot();
        transport.queue_received(
            7u,
            og::sim::serialize_snapshot_hash_check_message({
                .tick = snapshot.tick_count,
                .snapshot_hash = matching ? snapshot.snapshot_hash
                                          : snapshot.snapshot_hash + 1u,
            }));
    };
    const auto queue_strike = [&server, &queue_check] {
        server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
        queue_check(false);
    };

    // One short of the bound (the initial snapshot pairs with the first
    // wrong check): still connected...
    queue_check(false);
    for (std::uint32_t i = 1;
         i + 1 < og::sim::kMaxConsecutiveSnapshotHashMismatches; ++i)
    {
        queue_strike();
    }
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty());

    // ...and a single MATCHING check resets the strike counter.
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    queue_check(true);
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty());

    // The full bound, uninterrupted: the desynced client is disconnected.
    for (std::uint32_t i = 0;
         i < og::sim::kMaxConsecutiveSnapshotHashMismatches; ++i)
    {
        queue_strike();
    }
    server.step();
    ASSERT_FALSE(transport.disconnected_peers().empty());
    EXPECT_EQ(7u, transport.disconnected_peers().front());
}

TEST(NetTransport, game_server_never_desync_disconnects_the_host_peer)
{
    // The host peer is this machine's own display client: closing its
    // in-process transport makes the very next local send throw
    // ("InProcessTransport peer N is not connected" — the mid-game
    // ADD/REMOVE PLAYER crash), so sustained strikes keep force-keyframing
    // it instead of cutting it.
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    server.connect_client(7u); // first client == host
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data);

    for (std::uint32_t i = 0;
         i < og::sim::kMaxConsecutiveSnapshotHashMismatches * 2u; ++i)
    {
        server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
        transport.queue_received(
            7u,
            og::sim::serialize_snapshot_hash_check_message({
                .tick = snapshot.tick_count,
                .snapshot_hash = snapshot.snapshot_hash + 1u,
            }));
    }
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty())
        << "the host peer must survive sustained hash mismatches";
}

TEST(NetTransport, game_server_never_desync_disconnects_marked_local_peers)
{
    // Same reasoning as the host-peer exemption above, extended to every
    // same-process peer: a LOCAL splitscreen session runs one in-process
    // client per seat, and cutting a background seat's peer as "desynced"
    // makes the display's next send throw ("InProcessTransport peer N is not
    // connected"). The local transport shadow marks all of its clients; a
    // marked peer keeps being force-keyframed instead of being cut. Remote
    // peers keep the strike-out (pinned by
    // game_server_bounds_sustained_hash_mismatches_with_disconnect above).
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    server.connect_client(1u); // first client == host
    server.connect_client(2u); // a background local seat's peer
    server.mark_peer_local(2u);

    server.send_initial_snapshot(2u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages().back().data);

    for (std::uint32_t i = 0;
         i < og::sim::kMaxConsecutiveSnapshotHashMismatches * 2u; ++i)
    {
        server.send_initial_snapshot(2u, og::sim::SnapshotCaptureMode::Peek);
        transport.queue_received(
            2u,
            og::sim::serialize_snapshot_hash_check_message({
                .tick = snapshot.tick_count,
                .snapshot_hash = snapshot.snapshot_hash + 1u,
            }));
    }
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty())
        << "a marked local peer must survive sustained hash mismatches";
}

TEST(NetTransport, game_client_bounds_rejected_keyframes_into_fatal_desync)
{
    // WI-3(b) client side: a keyframe whose full-grid payload does not match
    // this world's grid means the client renders a DIFFERENT map than the
    // authority simulates (the tower ghost-session shape). Bounded strikes,
    // then the connection-lost surface fires once and the session ends.
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameClient client(transport, 1u, &fixture.world());
    int lost_count = 0;
    client.set_connection_lost_callback([&lost_count] { ++lost_count; });

    og::sim::WorldSnapshot good =
        og::sim::peek_keyframe_snapshot(fixture.world());
    ASSERT_TRUE(good.grid_full_resend);
    ASSERT_TRUE(good.grid_dirty);

    // Internally consistent snapshot of a DIFFERENT-SIZED map (the wire
    // format enforces payload == width*height, so the mismatch must come
    // from foreign dimensions — exactly what a diverged authority sends).
    og::sim::WorldSnapshot bad = good;
    bad.grid_width = static_cast<std::uint8_t>(good.grid_width + 1);
    bad.full_grid_data.assign(
        static_cast<std::size_t>(bad.grid_width) * bad.grid_height, 0u);

    for (std::uint32_t strike = 1;
         strike < og::sim::kMaxRejectedKeyframeStrikes; ++strike)
    {
        transport.queue_received(1u, og::sim::serialize_snapshot(bad));
        client.poll_messages();
        EXPECT_FALSE(client.fatal_desync());
        EXPECT_EQ(0, lost_count);
    }

    // A cleanly applied keyframe resets the strikes.
    transport.queue_received(1u, og::sim::serialize_snapshot(good));
    client.poll_messages();
    EXPECT_FALSE(client.fatal_desync());

    // Only the full CONSECUTIVE bound trips the fatal state.
    for (std::uint32_t strike = 0;
         strike < og::sim::kMaxRejectedKeyframeStrikes; ++strike)
    {
        EXPECT_FALSE(client.fatal_desync());
        transport.queue_received(1u, og::sim::serialize_snapshot(bad));
        client.poll_messages();
    }
    EXPECT_TRUE(client.fatal_desync());
    EXPECT_EQ(1, lost_count)
        << "the connection-lost surface fires exactly once";

    // Latched: further rejected keyframes do not re-fire the callback.
    transport.queue_received(1u, og::sim::serialize_snapshot(bad));
    client.poll_messages();
    EXPECT_TRUE(client.fatal_desync());
    EXPECT_EQ(1, lost_count);
}

TEST(NetTransport,
     game_server_snapshot_hash_check_ignores_superseded_same_tick_delta_on_pause)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.connect_client(7u);
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();

    transport.queue_received(
        7u,
        og::sim::serialize_client_ready_message({
            .last_applied_tick = fixture.world().tick_count_,
        }));
    server.step();
    transport.clear_sent_messages();

    const std::uint32_t paused_tick = fixture.world().tick_count_ + 1u;
    fixture.world().tick_count_ = paused_tick;
    fixture.world().current_palette_id = 1;

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    ASSERT_EQ(1u, transport.sent_messages().size());
    og::sim::TransportEnvelope envelope;
    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages().front().data,
        envelope));
    EXPECT_EQ(og::sim::kDeltaSnapshotMessageType, envelope.message_type);
    transport.clear_sent_messages();

    transport.queue_received(
        7u, og::sim::serialize_pause_broadcast_message({}));
    server.step();

    const auto paused_snapshot_it = std::find_if(
        transport.sent_messages().begin(),
        transport.sent_messages().end(),
        [](const og::sim::ReceivedMessage& message) {
            og::sim::TransportEnvelope message_envelope;
            return og::sim::decode_transport_envelope(message.data,
                                                      message_envelope) &&
                message_envelope.message_type == og::sim::kSnapshotMessageType;
        });
    ASSERT_NE(transport.sent_messages().end(), paused_snapshot_it);

    const og::sim::WorldSnapshot paused_snapshot =
        og::sim::deserialize_snapshot(paused_snapshot_it->data);
    EXPECT_EQ(paused_tick, paused_snapshot.tick_count);
    EXPECT_TRUE(paused_snapshot.paused);
    EXPECT_EQ(0u, paused_snapshot.pause_player_index);

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = paused_snapshot.tick_count,
            .snapshot_hash = paused_snapshot.snapshot_hash,
        }));
    server.step();

    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());
}

TEST(NetTransport, game_server_broadcast_current_state_uses_raw_fallback)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    server.connect_client(7u);
    server.bind_player(7u, 0u, 2);

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

    ASSERT_EQ(2u, transport.sent_messages().size());

    og::sim::TransportEnvelope envelope;
    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages()[0].data,
        envelope));
    EXPECT_EQ(og::sim::kInitialSetupMessageType, envelope.message_type);
    const auto initial_setup =
        og::sim::deserialize_initial_setup_message(
            transport.sent_messages()[0].data);
    ASSERT_TRUE(initial_setup.has_value());
    EXPECT_EQ(2, initial_setup->my_team);
    EXPECT_EQ(fixture.world().id, initial_setup->level_id);

    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages()[1].data,
        envelope));
    EXPECT_EQ(og::sim::kSnapshotMessageType, envelope.message_type);
    const og::sim::WorldSnapshot initial =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data);
    EXPECT_EQ(3u, initial.tick_count);
    EXPECT_EQ(2, initial.my_team);
    EXPECT_EQ(1, initial.current_palette_id);
}

TEST(NetTransport, game_server_forward_event_batch_skips_unready_raw_clients)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    server.connect_client(7u);

    og::sim::SimEventBatch batch;
    batch.sequence = 9u;
    batch.events.push_back({
        .tick = 9u,
        .kind = og::sim::EventKind::Notification,
        .a = 30u,
        .b = 0u,
        .text = "sim",
    });
    batch.events.push_back({
        .tick = 9u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 2u,
        .text = {},
    });

    server.forward_event_batch(batch);

    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransport, game_server_forward_event_batch_uses_ready_raw_fallback)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    server.connect_client(7u);
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);

    og::sim::ClientReadyMessage ready;
    ready.last_applied_tick = fixture.world().tick_count_;
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    server.step();

    const std::size_t sent_before = transport.sent_messages().size();

    og::sim::SimEventBatch batch;
    batch.sequence = 9u;
    batch.events.push_back({
        .tick = 9u,
        .kind = og::sim::EventKind::Notification,
        .a = 30u,
        .b = 0u,
        .text = "sim",
    });
    batch.events.push_back({
        .tick = 9u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 2u,
        .text = {},
    });

    server.forward_event_batch(batch);

    ASSERT_EQ(sent_before + 2u, transport.sent_messages().size());

    og::sim::TransportEnvelope envelope;
    const auto& sim_message = transport.sent_messages()[sent_before];
    EXPECT_EQ(7u, sim_message.peer_id);
    ASSERT_TRUE(og::sim::decode_transport_envelope(sim_message.data, envelope));
    EXPECT_EQ(og::sim::kSimEventBatchMessageType, envelope.message_type);
    const og::sim::SimEventBatch sim_batch =
        og::sim::deserialize_sim_event_batch(sim_message.data);
    EXPECT_NE(0u, sim_batch.sequence);
    ASSERT_EQ(1u, sim_batch.events.size());
    EXPECT_EQ(og::sim::EventKind::Notification, sim_batch.events[0].kind);
    EXPECT_EQ("sim", sim_batch.events[0].text);

    const auto& game_flow_message = transport.sent_messages()[sent_before + 1u];
    EXPECT_EQ(7u, game_flow_message.peer_id);
    ASSERT_TRUE(
        og::sim::decode_transport_envelope(game_flow_message.data, envelope));
    EXPECT_EQ(og::sim::kGameFlowEventBatchMessageType, envelope.message_type);
    const og::sim::SimEventBatch game_flow_batch =
        og::sim::deserialize_game_flow_event_batch(game_flow_message.data);
    EXPECT_NE(0u, game_flow_batch.sequence);
    ASSERT_EQ(1u, game_flow_batch.events.size());
    EXPECT_EQ(og::sim::EventKind::EndGame, game_flow_batch.events[0].kind);
    EXPECT_EQ(1u, game_flow_batch.events[0].a);
    EXPECT_EQ(2u, game_flow_batch.events[0].b);
}

TEST(NetTransport,
     game_server_broadcast_current_state_uses_transport_broadcast_for_shared_keyframes)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u});
    server.connect_client(7u);
    server.connect_client(11u);
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.bind_player(11u, 1u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    server.send_initial_snapshot(11u, og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    const og::sim::ClientReadyMessage ready{
        .last_applied_tick = fixture.world().tick_count_,
    };
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    transport.queue_received(
        11u, og::sim::serialize_client_ready_message(ready));
    server.step();
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    fixture.world().current_palette_id = 3;

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    ASSERT_EQ(1u, transport.broadcast_messages().size());
    ASSERT_EQ(2u, transport.sent_messages().size());
    EXPECT_EQ(7u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(11u, transport.sent_messages()[1].peer_id);
    EXPECT_EQ(transport.broadcast_messages().front(),
              transport.sent_messages()[0].data);
    EXPECT_EQ(transport.broadcast_messages().front(),
              transport.sent_messages()[1].data);

    const og::sim::WorldSnapshot snapshot =
        og::sim::deserialize_snapshot(transport.broadcast_messages().front());
    EXPECT_EQ(og::sim::KEYFRAME_INTERVAL_TICKS, snapshot.tick_count);
    EXPECT_EQ(3, snapshot.current_palette_id);
}

TEST(NetTransportJitter, keyframe_broadcast_cadence_follows_interval_ticks)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.connect_client(7u);
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();

    const og::sim::ClientReadyMessage ready{
        .last_applied_tick = fixture.world().tick_count_,
    };
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    server.step();
    transport.clear_sent_messages();

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS - 1u;
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);
    ASSERT_EQ(1u, transport.sent_messages().size());
    og::sim::TransportEnvelope envelope;
    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages().front().data,
        envelope));
    EXPECT_EQ(og::sim::kDeltaSnapshotMessageType, envelope.message_type);

    transport.clear_sent_messages();
    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);
    ASSERT_EQ(1u, transport.sent_messages().size());
    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages().front().data,
        envelope));
    EXPECT_EQ(og::sim::kSnapshotMessageType, envelope.message_type);
}

TEST(NetTransport,
     endgame_event_reaches_every_ready_peer_after_full_state_despite_keyframe_budget)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    constexpr std::array<og::sim::PeerId, 3> peers{7u, 11u, 13u};

    transport.set_connected_peers(
        std::vector<og::sim::PeerId>(peers.begin(), peers.end()));
    server.poll_incoming_messages();
    for (std::size_t index = 0; index < peers.size(); ++index)
    {
        server.bind_player(peers[index], index, fixture.world().my_team);
        server.send_initial_snapshot(
            peers[index], og::sim::SnapshotCaptureMode::Peek);
        transport.queue_received(
            peers[index],
            og::sim::serialize_client_ready_message({
                .last_applied_tick = fixture.world().tick_count_,
            }));
    }
    server.step();
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    // Three simultaneous resyncs exceed the ordinary two-keyframe per-tick
    // budget. A terminal frame must bypass that budget: otherwise peer 3 sees
    // EndGame first and folds stale score/time into its wallet.
    for (const og::sim::PeerId peer : peers)
    {
        transport.queue_received(
            peer,
            og::sim::serialize_keyframe_request_message({
                .last_seen_tick = fixture.world().tick_count_,
            }));
    }
    fixture.world().m_score[2] = 321u;
    fixture.world().set_level_tick_count(77u);
    // Exercise the event-only path used by synchronous flow sites: the world
    // flag is deliberately false, but EndGame still requires a full snapshot.
    fixture.world().game_ended = false;
    fixture.world().ending = 0;
    fixture.world().next_level = -1;
    fixture.events.push(
        og::sim::EventKind::EndGame,
        0u,
        static_cast<std::uint32_t>(static_cast<std::int32_t>(-1)));

    server.step();

    std::array<bool, peers.size()> saw_terminal_snapshot{};
    std::array<bool, peers.size()> saw_endgame{};
    for (const auto& sent : transport.sent_messages())
    {
        const auto peer_it = std::find(peers.begin(), peers.end(), sent.peer_id);
        ASSERT_NE(peers.end(), peer_it);
        const std::size_t peer_index =
            static_cast<std::size_t>(peer_it - peers.begin());

        og::sim::TransportEnvelope envelope;
        ASSERT_TRUE(
            og::sim::decode_transport_envelope(sent.data, envelope));
        if (envelope.message_type == og::sim::kSnapshotMessageType)
        {
            const og::sim::WorldSnapshot snapshot =
                og::sim::deserialize_snapshot(sent.data);
            EXPECT_FALSE(snapshot.game_ended);
            EXPECT_EQ(fixture.world().tick_count_, snapshot.tick_count);
            EXPECT_EQ(fixture.world().level_tick_count(),
                      snapshot.level_tick_count);
            EXPECT_EQ(321u, snapshot.m_score[2]);
            saw_terminal_snapshot[peer_index] = true;
        }
        else if (envelope.message_type ==
                 og::sim::kGameFlowEventBatchMessageType)
        {
            const og::sim::SimEventBatch batch =
                og::sim::deserialize_game_flow_event_batch(sent.data);
            const bool contains_endgame = std::any_of(
                batch.events.begin(), batch.events.end(),
                [](const og::sim::Event& event) {
                    return event.kind == og::sim::EventKind::EndGame;
                });
            if (contains_endgame)
            {
                EXPECT_TRUE(saw_terminal_snapshot[peer_index])
                    << "EndGame outran the authoritative terminal snapshot";
                saw_endgame[peer_index] = true;
            }
        }
    }

    EXPECT_EQ((std::array<bool, peers.size()>{true, true, true}),
              saw_terminal_snapshot);
    EXPECT_EQ((std::array<bool, peers.size()>{true, true, true}), saw_endgame);
}

TEST(NetTransport,
     game_server_keyframe_preserves_hurt_flash_before_authoritative_consumption)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    const og::sim::ClientReadyMessage ready{
        .last_applied_tick = fixture.world().tick_count_,
    };
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    server.step();
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    actor->set_hurt_flash(true);

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Consume,
                                   og::sim::EventDeliveryMode::Skip);

    ASSERT_EQ(1u, transport.broadcast_messages().size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    const og::sim::WorldSnapshot snapshot =
        og::sim::deserialize_snapshot(transport.broadcast_messages().front());
    const og::sim::EntitySnapshot* actor_snapshot =
        find_entity_snapshot(snapshot.oblist, actor->entity_id());
    ASSERT_NE(nullptr, actor_snapshot);
    EXPECT_EQ(1u, actor_snapshot->hurt_flash);
    EXPECT_FALSE(actor->hurt_flash());
    EXPECT_NE(
        0ULL,
        actor->dirty_mask_word(og::dirty::BIT_HURT_FLASH / 64) &
            (1ULL << (og::dirty::BIT_HURT_FLASH % 64)));
}

TEST(NetTransport,
     initial_setup_control_change_and_snapshot_hash_messages_roundtrip)
{
    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = 7;
    initial_setup.level_title = "Test Level";
    initial_setup.level_type = 3;
    initial_setup.par_value = 11;
    initial_setup.time_bonus_limit = 222;
    initial_setup.difficulty = 140;
    initial_setup.pixmaxx = 1024;
    initial_setup.pixmaxy = 768;
    initial_setup.my_team = 2;
    initial_setup.allied_mode = 1;
    initial_setup.current_scenario = 7;
    initial_setup.completed_levels = {1, 4, 7};
    initial_setup.controlled_entity_ids = {10u, 20u, 30u, 40u};
    initial_setup.guys.push_back({
        .guy_id = 99,
        .name = "Ari",
        .family = 2,
        .strength = 12,
        .dexterity = 13,
        .constitution = 14,
        .intelligence = 15,
        .armor = 16,
        .exp = 1234u,
        .kills = 8,
        .level_kills = 9,
        .total_damage = 10,
        .total_hits = 11,
        .total_shots = 12,
        .teamnum = 2,
        .scen_damage = 3.5f,
        .scen_kills = 4,
        .scen_damage_taken = 5.5f,
        .scen_min_hp = 6.5f,
        .scen_shots = 7,
        .scen_hits = 8,
        .level = 9,
    });

    const std::vector<std::uint8_t> initial_setup_bytes =
        og::sim::serialize_initial_setup_message(initial_setup);
    const auto decoded_initial_setup =
        og::sim::deserialize_initial_setup_message(initial_setup_bytes);
    ASSERT_TRUE(decoded_initial_setup.has_value());
    EXPECT_EQ(initial_setup, *decoded_initial_setup);

    og::sim::ControlChangeMessage control_change;
    control_change.player_index = 2;
    control_change.entity_id = 444u;
    const std::vector<std::uint8_t> control_change_bytes =
        og::sim::serialize_control_change_message(control_change);
    const auto decoded_control_change =
        og::sim::deserialize_control_change_message(control_change_bytes);
    ASSERT_TRUE(decoded_control_change.has_value());
    EXPECT_EQ(control_change, *decoded_control_change);

    og::sim::SnapshotHashCheckMessage hash_check;
    hash_check.tick = 55u;
    hash_check.snapshot_hash = 0xaabbccddU;
    const std::vector<std::uint8_t> hash_check_bytes =
        og::sim::serialize_snapshot_hash_check_message(hash_check);
    const auto decoded_hash_check =
        og::sim::deserialize_snapshot_hash_check_message(hash_check_bytes);
    ASSERT_TRUE(decoded_hash_check.has_value());
    EXPECT_EQ(hash_check, *decoded_hash_check);
}

TEST(NetTransport, lobby_state_and_messages_roundtrip)
{
    const og::sim::LobbyPlayer player = make_lobby_player_for_test();
    const og::sim::LobbyState state = make_lobby_state_for_test();

    const std::vector<std::uint8_t> state_bytes =
        og::sim::serialize_lobby_state_message(state);
    const auto decoded_state =
        og::sim::deserialize_lobby_state_message(state_bytes);
    ASSERT_TRUE(decoded_state.has_value());
    EXPECT_EQ(state, *decoded_state);

    std::vector<og::sim::LobbyMessage> messages;

    og::sim::LobbyMessage join;
    join.payload = og::sim::LobbyJoinMessage{player};
    messages.push_back(join);

    og::sim::LobbyMessage leave;
    leave.payload = og::sim::LobbyLeaveMessage{.player_index = 1u};
    messages.push_back(leave);

    og::sim::LobbyMessage ready;
    ready.payload =
        og::sim::LobbyReadyMessage{.player_index = 1u, .ready = false};
    messages.push_back(ready);

    og::sim::LobbyMessage team_change;
    team_change.payload =
        og::sim::LobbyTeamChangeMessage{
            .player_index = 1u,
            .seat_id = player.seat_id,
            .team = 3,
        };
    messages.push_back(team_change);

    og::sim::LobbyMessage remove_seat;
    remove_seat.payload =
        og::sim::LobbyRemoveSeatMessage{
            .player_index = 1u,
            .seat_id = player.seat_id,
        };
    messages.push_back(remove_seat);

    og::sim::LobbyMessage start_game;
    start_game.payload = og::sim::LobbyStartGameMessage{
        .player_index = 1u,
        .request_id = 0x87654321u,
    };
    messages.push_back(start_game);

    og::sim::LobbyMessage settings_change;
    settings_change.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 1u,
        .settings =
            {
                .campaign_id = "gladiator",
                .scenario_id = 8,
                .difficulty = 1,
                .allied_mode = 0,
            },
    };
    messages.push_back(settings_change);

    for (const auto& message : messages)
    {
        const std::vector<std::uint8_t> bytes =
            og::sim::serialize_lobby_message(message);
        const auto decoded = og::sim::deserialize_lobby_message(bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(message.kind(), decoded->kind());
        EXPECT_EQ(message, *decoded);
    }
}

TEST(NetTransport,
     deserialize_lobby_messages_rejects_unknown_kinds_and_oversized_counts)
{
    // Wire layout of an empty LobbyState (protocol v12): 4-byte transport
    // header, then the settings block (4-byte empty campaign string + 13 i16
    // fields and the authored-team-mask u8 = 31 bytes), then the 1-byte host
    // player id, 1-byte last_start_denial echo, and u32 request correlation —
    // so player-count sits at offset 41. A trailing u8 local-seat-id count
    // follows the players, then a u32 recipient-specific Join acknowledgement
    // and a bool recipient-host flag.
    const auto empty_state_bytes =
        og::sim::serialize_lobby_state_message(og::sim::LobbyState{});
    auto oversized_player_count =
        std::vector<std::uint8_t>(empty_state_bytes.begin(),
                                  empty_state_bytes.end());
    write_u32_le(oversized_player_count, 41, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(oversized_player_count)
            .has_value());

    auto oversized_local_seat_count = empty_state_bytes;
    ASSERT_GE(oversized_local_seat_count.size(), sizeof(std::uint32_t) + 1u);
    const std::size_t local_seat_count_offset =
        oversized_local_seat_count.size() -
        sizeof(std::uint32_t) - sizeof(std::uint8_t) - 1u;
    oversized_local_seat_count[local_seat_count_offset] =
        static_cast<std::uint8_t>(MAX_PLAYERS + 1);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(oversized_local_seat_count)
            .has_value());

    auto truncated_host_flag = empty_state_bytes;
    truncated_host_flag.pop_back();
    const std::uint16_t truncated_payload_size =
        static_cast<std::uint16_t>(
            truncated_host_flag.size() - og::sim::kTransportHeaderSize);
    truncated_host_flag[2] =
        static_cast<std::uint8_t>(truncated_payload_size & 0xffu);
    truncated_host_flag[3] =
        static_cast<std::uint8_t>((truncated_payload_size >> 8) & 0xffu);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(truncated_host_flag)
            .has_value());

    auto truncated_join_ack = truncated_host_flag;
    truncated_join_ack.pop_back();
    const std::uint16_t shorter_payload_size =
        static_cast<std::uint16_t>(
            truncated_join_ack.size() - og::sim::kTransportHeaderSize);
    truncated_join_ack[2] =
        static_cast<std::uint8_t>(shorter_payload_size & 0xffu);
    truncated_join_ack[3] =
        static_cast<std::uint8_t>((shorter_payload_size >> 8) & 0xffu);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(truncated_join_ack)
            .has_value());

    og::sim::LobbyPlayer player;
    player.player_index = 0u;
    og::sim::LobbyState state_with_player;
    state_with_player.players.push_back(player);
    const auto player_state_bytes =
        og::sim::serialize_lobby_state_message(state_with_player);
    auto oversized_slot_count =
        std::vector<std::uint8_t>(player_state_bytes.begin(),
                                  player_state_bytes.end());
    // First player record (v9): index u8 + seat-id u32 + machine-id u32 +
    // empty-name u32 + empty-company u32 + team i16 + ready/host bools =
    // 21 bytes after the player-count u32 at 41 (v12 settings block), putting
    // its slot-count u32 at 66.
    write_u32_le(oversized_slot_count, 66, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(oversized_slot_count)
            .has_value());

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyStartGameMessage{.player_index = 0u};
    auto bad_kind = og::sim::serialize_lobby_message(message);
    bad_kind[og::sim::kTransportHeaderSize] = 0xffu;
    EXPECT_FALSE(og::sim::deserialize_lobby_message(bad_kind).has_value());
}

TEST(NetTransport, game_client_dispatches_callbacks_for_runtime_state)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const first = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const second = fixture.world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    first->setxy(32, 32);
    second->setxy(48, 48);

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = fixture.world().id;
    initial_setup.level_title = fixture.world().title;
    initial_setup.level_type = fixture.world().type;
    initial_setup.par_value = fixture.world().par_value;
    initial_setup.time_bonus_limit = fixture.world().time_bonus_limit;
    initial_setup.difficulty = fixture.world().difficulty;
    initial_setup.pixmaxx = fixture.world().pixmaxx;
    initial_setup.pixmaxy = fixture.world().pixmaxy;
    initial_setup.my_team = fixture.world().my_team;
    initial_setup.allied_mode = fixture.world().allied_mode;
    initial_setup.current_scenario = fixture.world().current_scenario;
    initial_setup.controlled_entity_ids = {
        first->entity_id(), 0u, 0u, 0u};

    og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fixture.world());
    snapshot.tick_count = 1u;
    snapshot.current_palette_id = 1;

    og::sim::SimEventBatch sim_batch;
    sim_batch.sequence = 1u;
    sim_batch.events.push_back({
        .tick = 1u,
        .kind = og::sim::EventKind::Notification,
        .a = 25u,
        .text = "sim",
    });

    og::sim::SimEventBatch game_flow_batch;
    game_flow_batch.sequence = 1u;
    game_flow_batch.events.push_back({
        .tick = 1u,
        .kind = og::sim::EventKind::EndGame,
        .a = 0u,
        .b = 2u,
        .text = {},
    });

    og::sim::ExitPromptBroadcastMessage exit_prompt;
    exit_prompt.destination_level = 3;
    exit_prompt.prompt_text = "Exit now?";

    og::sim::PauseBroadcastMessage pause_broadcast;
    pause_broadcast.player_index = 0u;
    pause_broadcast.player_name = "Ari";

    og::sim::ControlChangeMessage control_change;
    control_change.player_index = 0u;
    control_change.entity_id = second->entity_id();

    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(initial_setup));
    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(7u, og::sim::serialize_sim_event_batch(sim_batch));
    transport.queue_received(
        7u,
        og::sim::serialize_game_flow_event_batch(game_flow_batch));
    transport.queue_received(
        7u,
        og::sim::serialize_exit_prompt_broadcast_message(exit_prompt));
    transport.queue_received(
        7u,
        og::sim::serialize_pause_broadcast_message(pause_broadcast));
    transport.queue_received(
        7u,
        og::sim::serialize_control_change_message(control_change));

    og::sim::GameClient client(transport, 7u, &fixture.world());
    std::vector<bool> initial_setup_transition_flags;
    std::vector<std::uint32_t> mapped_entity_ids;
    std::vector<std::uint32_t> resolved_entity_ids;
    std::vector<std::uint8_t> synced_palette_ids;
    std::vector<og::sim::SimEventBatch> dispatched_sim_batches;
    std::vector<og::sim::SimEventBatch> dispatched_game_flow_batches;
    std::optional<og::sim::ExitPromptBroadcastMessage> received_exit_prompt;
    std::optional<og::sim::PauseBroadcastMessage> received_pause_broadcast;

    client.set_initial_setup_callback(
        [&](const og::sim::InitialSetupMessage&, bool is_level_transition) {
            initial_setup_transition_flags.push_back(is_level_transition);
        });
    client.set_control_mapping_callback(
        [&](const og::sim::ControlledEntityIds& controlled_entity_ids,
            GameWorld* world) {
            mapped_entity_ids.push_back(controlled_entity_ids[0]);
            walker* const mapped =
                (world != nullptr && controlled_entity_ids[0] != 0u)
                    ? world->find_by_id(controlled_entity_ids[0])
                    : nullptr;
            resolved_entity_ids.push_back(
                mapped != nullptr ? mapped->entity_id() : 0u);
        });
    client.set_sim_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            dispatched_sim_batches.push_back(batch);
        });
    client.set_game_flow_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            dispatched_game_flow_batches.push_back(batch);
        });
    client.set_exit_prompt_callback(
        [&](const og::sim::ExitPromptBroadcastMessage& message) {
            received_exit_prompt = message;
        });
    client.set_pause_broadcast_callback(
        [&](const og::sim::PauseBroadcastMessage& message) {
            received_pause_broadcast = message;
        });
    client.set_palette_sync_callback(
        [&](std::uint8_t palette_id) {
            synced_palette_ids.push_back(palette_id);
        });

    client.poll_messages();

    ASSERT_TRUE(client.baseline().has_value());
    EXPECT_EQ(1u, client.baseline()->tick_count);
    ASSERT_EQ((std::vector<bool>{false}), initial_setup_transition_flags);
    ASSERT_GE(mapped_entity_ids.size(), 2u);
    EXPECT_EQ(second->entity_id(), mapped_entity_ids.back());
    EXPECT_EQ(second->entity_id(), resolved_entity_ids.back());
    EXPECT_NE(mapped_entity_ids.end(),
              std::find(mapped_entity_ids.begin(),
                        mapped_entity_ids.end(),
                        first->entity_id()));
    ASSERT_EQ((std::vector<std::uint8_t>{1u}), synced_palette_ids);

    ASSERT_EQ(1u, dispatched_sim_batches.size());
    EXPECT_EQ(sim_batch.sequence, dispatched_sim_batches.front().sequence);
    EXPECT_EQ(sim_batch.events, dispatched_sim_batches.front().events);
    ASSERT_EQ(1u, dispatched_game_flow_batches.size());
    EXPECT_EQ(game_flow_batch.sequence,
              dispatched_game_flow_batches.front().sequence);
    EXPECT_EQ(game_flow_batch.events,
              dispatched_game_flow_batches.front().events);
    ASSERT_TRUE(received_exit_prompt.has_value());
    EXPECT_EQ(exit_prompt, *received_exit_prompt);
    ASSERT_TRUE(received_pause_broadcast.has_value());
    EXPECT_EQ(pause_broadcast, *received_pause_broadcast);
}

TEST(NetTransport, game_client_tracks_interpolated_positions_across_snapshots)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    const auto initial_pos = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(initial_pos.has_value());
    EXPECT_FLOAT_EQ(32.0f, initial_pos->worldx);
    EXPECT_FLOAT_EQ(48.0f, initial_pos->worldy);
    EXPECT_FLOAT_EQ(32.0f, initial_pos->xpos);
    EXPECT_FLOAT_EQ(48.0f, initial_pos->ypos);

    actor->setxy(80, 96);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(delta));

    client.poll_messages();

    const auto start = client.render_position(actor->entity_id(), 0.0f);
    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    const auto end = client.render_position(actor->entity_id(), 1.5f);
    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(middle.has_value());
    ASSERT_TRUE(end.has_value());

    EXPECT_FLOAT_EQ(32.0f, start->worldx);
    EXPECT_FLOAT_EQ(48.0f, start->worldy);
    EXPECT_FLOAT_EQ(56.0f, middle->worldx);
    EXPECT_FLOAT_EQ(72.0f, middle->worldy);
    EXPECT_FLOAT_EQ(80.0f, end->worldx);
    EXPECT_FLOAT_EQ(96.0f, end->worldy);
    EXPECT_FLOAT_EQ(56.0f, middle->xpos);
    EXPECT_FLOAT_EQ(72.0f, middle->ypos);
}

TEST(NetTransport,
     game_client_keeps_render_anchors_on_fractional_world_positions)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setworldxy(32.25f, 48.75f);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    actor->setworldxy(33.75f, 50.25f);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(delta));

    client.poll_messages();

    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(middle.has_value());

    EXPECT_FLOAT_EQ(33.0f, middle->worldx);
    EXPECT_FLOAT_EQ(49.5f, middle->worldy);
    EXPECT_FLOAT_EQ(middle->worldx, middle->xpos);
    EXPECT_FLOAT_EQ(middle->worldy, middle->ypos);
}

TEST(NetTransport,
     game_client_snaps_spawn_positions_and_suppresses_dead_entity_interpolation)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().tick_count_ = 1u;
    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    walker* const spawned =
        fixture.world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, spawned);
    spawned->setxy(90, 110);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot spawn_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(spawn_delta));

    client.poll_messages();

    const auto spawn_pos = client.render_position(spawned->entity_id(), 0.0f);
    ASSERT_TRUE(spawn_pos.has_value());
    EXPECT_FLOAT_EQ(90.0f, spawn_pos->worldx);
    EXPECT_FLOAT_EQ(110.0f, spawn_pos->worldy);
    EXPECT_FLOAT_EQ(90.0f, spawn_pos->xpos);
    EXPECT_FLOAT_EQ(110.0f, spawn_pos->ypos);

    spawned->set_dead(1);
    fixture.world().tick_count_ = 3u;
    const og::sim::WorldSnapshot death_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(death_delta));

    client.poll_messages();

    EXPECT_FALSE(client.render_position(spawned->entity_id(), 0.5f).has_value());
}

TEST(NetTransport, game_client_render_interpolation_alpha_respects_game_speed)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().timer_wait = 6;
    fixture.world().tick_count_ = 1u;
    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    client.testing_set_render_interpolation_elapsed_ms(20.5f);
    EXPECT_NEAR(0.25f, client.render_interpolation_alpha(1.0f), 0.02f);

    client.testing_set_render_interpolation_elapsed_ms(20.5f);
    EXPECT_NEAR(0.5f, client.render_interpolation_alpha(2.0f), 0.02f);

    client.testing_set_render_interpolation_elapsed_ms(20.5f);
    EXPECT_FLOAT_EQ(1.0f, client.render_interpolation_alpha(0.0f));
}

TEST(NetTransportJitter, interpolation_alpha_uses_rounded_timer_wait_interval)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().timer_wait = 6;
    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS - 1u;
    transport.queue_received(
        7u,
        og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(fixture.world())));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    client.testing_set_render_interpolation_elapsed_ms(41.0f);
    EXPECT_NEAR(0.5f, client.render_interpolation_alpha(1.0f), 0.02f);
}

TEST(NetTransport,
     game_client_render_interpolation_alpha_treats_zero_timer_wait_as_immediate)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().timer_wait = 0;
    fixture.world().tick_count_ = 1u;
    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    client.testing_set_render_interpolation_elapsed_ms(1.0f);
    EXPECT_FLOAT_EQ(1.0f, client.render_interpolation_alpha(1.0f));
}

TEST(NetTransportJitter, snapshot_hash_checks_follow_keyframe_interval)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().timer_wait = 6;
    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS - 1u;
    transport.queue_received(
        7u,
        og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(fixture.world())));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();
    const std::uint32_t after_initial_hash_checks =
        client.snapshot_hash_check_count();

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));
    client.poll_messages();
    EXPECT_EQ(after_initial_hash_checks + 1u,
              client.snapshot_hash_check_count());

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS + 1u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));
    client.poll_messages();
    EXPECT_EQ(after_initial_hash_checks + 1u,
              client.snapshot_hash_check_count());
}

TEST(NetTransport,
     game_client_continues_interpolation_from_current_rendered_position)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    actor->setxy(80, 96);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot first_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(first_delta));

    client.poll_messages();

    actor->setxy(128, 144);
    fixture.world().tick_count_ = 3u;
    const og::sim::WorldSnapshot second_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(second_delta));

    client.poll_messages(0.5f);

    const auto start = client.render_position(actor->entity_id(), 0.0f);
    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(middle.has_value());

    EXPECT_NEAR(56.0f, start->worldx, 0.1f);
    EXPECT_NEAR(72.0f, start->worldy, 0.1f);
    EXPECT_NEAR(56.0f, start->xpos, 0.1f);
    EXPECT_NEAR(72.0f, start->ypos, 0.1f);
    EXPECT_NEAR(92.0f, middle->worldx, 0.1f);
    EXPECT_NEAR(108.0f, middle->worldy, 0.1f);
    EXPECT_NEAR(92.0f, middle->xpos, 0.1f);
    EXPECT_NEAR(108.0f, middle->ypos, 0.1f);
}

TEST(NetTransport, game_client_consumes_explicit_render_alpha_once_per_poll)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    actor->setxy(80, 96);
    fixture.world().tick_count_ = 2u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));
    client.poll_messages();

    actor->setxy(128, 144);
    fixture.world().tick_count_ = 3u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));

    actor->setxy(176, 192);
    fixture.world().tick_count_ = 4u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));

    client.poll_messages(0.5f);

    const auto start = client.render_position(actor->entity_id(), 0.0f);
    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(middle.has_value());

    EXPECT_NEAR(56.0f, start->worldx, 0.1f);
    EXPECT_NEAR(72.0f, start->worldy, 0.1f);
    EXPECT_NEAR(56.0f, start->xpos, 0.1f);
    EXPECT_NEAR(72.0f, start->ypos, 0.1f);
    EXPECT_NEAR(116.0f, middle->worldx, 0.1f);
    EXPECT_NEAR(132.0f, middle->worldy, 0.1f);
    EXPECT_NEAR(116.0f, middle->xpos, 0.1f);
    EXPECT_NEAR(132.0f, middle->ypos, 0.1f);
}

TEST(NetTransport, game_client_notifies_level_transition_before_next_keyframe)
{
    MockTransport transport;
    TestGameWorld fixture;

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = fixture.world().id;
    initial_setup.level_title = fixture.world().title;
    initial_setup.level_type = fixture.world().type;
    initial_setup.par_value = fixture.world().par_value;
    initial_setup.time_bonus_limit = fixture.world().time_bonus_limit;
    initial_setup.difficulty = fixture.world().difficulty;
    initial_setup.pixmaxx = fixture.world().pixmaxx;
    initial_setup.pixmaxy = fixture.world().pixmaxy;
    initial_setup.my_team = fixture.world().my_team;
    initial_setup.allied_mode = fixture.world().allied_mode;
    initial_setup.current_scenario = fixture.world().current_scenario;

    og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fixture.world());
    snapshot.tick_count = 1u;

    og::sim::InitialSetupMessage transition_setup = initial_setup;
    transition_setup.level_id = fixture.world().id + 1;
    transition_setup.current_scenario = fixture.world().current_scenario + 1;

    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(initial_setup));
    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(transition_setup));

    og::sim::GameClient client(transport, 7u, &fixture.world());
    std::vector<bool> transition_flags;
    client.set_initial_setup_callback(
        [&](const og::sim::InitialSetupMessage&, bool is_level_transition) {
            transition_flags.push_back(is_level_transition);
            if (is_level_transition)
                client.send_client_ready();
        });

    client.poll_messages();

    ASSERT_EQ((std::vector<bool>{false, true}), transition_flags);
    std::vector<og::sim::ClientReadyMessage> ready_messages;
    for (const auto& sent : transport.sent_messages())
    {
        const auto ready =
            og::sim::deserialize_client_ready_message(sent.data);
        if (ready.has_value())
            ready_messages.push_back(*ready);
    }

    ASSERT_EQ(2u, ready_messages.size());
    EXPECT_EQ(1u, ready_messages[0].last_applied_tick);
    EXPECT_EQ(0u, ready_messages[1].last_applied_tick);
}

TEST(NetTransport,
     game_client_processes_queued_transition_messages_after_endgame)
{
    MockTransport transport;
    TestGameWorld fixture;

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = fixture.world().id;
    initial_setup.level_title = fixture.world().title;
    initial_setup.level_type = fixture.world().type;
    initial_setup.par_value = fixture.world().par_value;
    initial_setup.time_bonus_limit = fixture.world().time_bonus_limit;
    initial_setup.difficulty = fixture.world().difficulty;
    initial_setup.pixmaxx = fixture.world().pixmaxx;
    initial_setup.pixmaxy = fixture.world().pixmaxy;
    initial_setup.my_team = fixture.world().my_team;
    initial_setup.allied_mode = fixture.world().allied_mode;
    initial_setup.current_scenario = fixture.world().current_scenario;

    og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fixture.world());
    snapshot.tick_count = 1u;

    og::sim::SimEventBatch endgame_batch;
    endgame_batch.sequence = 1u;
    endgame_batch.events.push_back({
        .tick = 1u,
        .kind = og::sim::EventKind::EndGame,
        .a = 0u,
        .b = 2u,
        .text = {},
    });

    og::sim::InitialSetupMessage transition_setup = initial_setup;
    transition_setup.level_id = fixture.world().id + 1;
    transition_setup.current_scenario = fixture.world().current_scenario + 1;

    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(initial_setup));
    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(
        7u, og::sim::serialize_game_flow_event_batch(endgame_batch));
    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(transition_setup));

    og::sim::GameClient client(transport, 7u, &fixture.world());
    std::vector<bool> transition_flags;
    client.set_initial_setup_callback(
        [&](const og::sim::InitialSetupMessage&, bool is_level_transition) {
            transition_flags.push_back(is_level_transition);
        });
    client.set_game_flow_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            if (!batch.events.empty() &&
                batch.events.back().kind == og::sim::EventKind::EndGame)
            {
                fixture.world().end = 1;
            }
        });
    client.set_message_processing_break_callback([&fixture]() {
        return fixture.world().end != 0;
    });

    client.poll_messages();

    ASSERT_EQ((std::vector<bool>{false, true}), transition_flags);
    ASSERT_TRUE(client.initial_setup().has_value());
    EXPECT_EQ(transition_setup.level_id, client.initial_setup()->level_id);
    EXPECT_EQ(transition_setup.current_scenario,
              client.initial_setup()->current_scenario);
}

TEST(NetTransport, game_client_polls_raw_messages_when_typed_path_is_unavailable)
{
    MockTransport transport;

    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = 1u;
    snapshot.my_team = 2;
    snapshot.current_palette_id = 1;

    og::sim::WorldSnapshot delta;
    delta.tick_count = 2u;
    delta.my_team = 3;

    og::sim::SimEventBatch sim_batch;
    sim_batch.sequence = 2u;
    sim_batch.events.push_back({
        .tick = 2u,
        .kind = og::sim::EventKind::Notification,
        .a = 60u,
        .text = "sim",
    });

    og::sim::SimEventBatch game_flow_batch;
    game_flow_batch.sequence = 2u;
    game_flow_batch.events.push_back({
        .tick = 2u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 7u,
        .text = {},
    });

    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(7u, og::sim::serialize_delta(delta));
    transport.queue_received(7u, og::sim::serialize_sim_event_batch(sim_batch));
    transport.queue_received(7u,
                             og::sim::serialize_game_flow_event_batch(
                                 game_flow_batch));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    ASSERT_EQ(4u, client.last_polled_messages().size());
    ASSERT_TRUE(client.baseline().has_value());
    EXPECT_EQ(2u, client.baseline()->tick_count);
    EXPECT_EQ(3, client.baseline()->my_team);
    ASSERT_EQ(1u, client.sim_event_batches().size());
    EXPECT_EQ(sim_batch.sequence, client.sim_event_batches().front().sequence);
    EXPECT_EQ(sim_batch.events, client.sim_event_batches().front().events);
    ASSERT_EQ(1u, client.game_flow_event_batches().size());
    EXPECT_EQ(game_flow_batch.sequence,
              client.game_flow_event_batches().front().sequence);
    EXPECT_EQ(game_flow_batch.events,
              client.game_flow_event_batches().front().events);
}

TEST(NetTransport,
     game_client_polls_raw_lobby_messages_when_typed_path_is_unavailable)
{
    MockTransport transport;

    og::sim::LobbyMessage lobby_message;
    lobby_message.payload =
        og::sim::LobbyJoinMessage{make_lobby_player_for_test()};
    const og::sim::LobbyState lobby_state = make_lobby_state_for_test();

    transport.queue_received(
        7u, og::sim::serialize_lobby_message(lobby_message));
    transport.queue_received(
        7u, og::sim::serialize_lobby_state_message(lobby_state));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    ASSERT_EQ(2u, client.last_polled_messages().size());

    const og::sim::TypedReceivedMessage& decoded_message =
        client.last_polled_messages()[0];
    EXPECT_EQ(7u, decoded_message.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              decoded_message.kind);
    ASSERT_NE(nullptr, decoded_message.lobby_message);
    EXPECT_EQ(lobby_message, *decoded_message.lobby_message);

    const og::sim::TypedReceivedMessage& decoded_state =
        client.last_polled_messages()[1];
    EXPECT_EQ(7u, decoded_state.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyState,
              decoded_state.kind);
    ASSERT_NE(nullptr, decoded_state.lobby_state);
    EXPECT_EQ(lobby_state, *decoded_state.lobby_state);
}

TEST(NetTransport, game_server_disconnects_peers_that_send_malformed_messages)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    transport.queue_received(7u, {0x01, 0x06, 0x01});
    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(server.last_polled_messages().empty());
}

TEST(NetTransport, game_server_set_player_control_updates_bound_seat_and_notifies_peer)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    walker* first = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* replacement = fixture.world().add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, replacement);
    server.bind_player(7u, 0u, fixture.world().my_team, first);
    transport.clear_sent_messages();

    server.set_player_control(0u, replacement);
    EXPECT_EQ(replacement, server.player_control(0u));
    ASSERT_EQ(1u, transport.sent_messages().size());
    const auto change = og::sim::deserialize_control_change_message(
        transport.sent_messages().front().data);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(0u, change->player_index);
    EXPECT_EQ(replacement->entity_id(), change->entity_id);

    server.set_player_control(og::sim::kMaxGlobalPlayers, first);
    EXPECT_EQ(nullptr, server.player_control(og::sim::kMaxGlobalPlayers));
}

TEST(NetTransport, game_server_disconnects_peers_that_send_unknown_raw_message_types)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    std::vector<std::uint8_t> unknown_type =
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{});
    unknown_type[1] = 0xffu;
    transport.queue_received(7u, std::move(unknown_type));

    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(server.last_polled_messages().empty());
}

TEST(NetTransport, game_client_disconnects_when_server_message_is_malformed)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    transport.queue_received(7u, {0x04, 0x02, 0x01, 0x00, 0xff});

    client.poll_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(client.last_polled_messages().empty());
}

TEST(NetTransport, game_client_disconnects_when_server_message_type_is_unknown)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    std::vector<std::uint8_t> unknown_type =
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{});
    unknown_type[1] = 0xffu;
    transport.queue_received(7u, std::move(unknown_type));

    client.poll_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(client.last_polled_messages().empty());
}

} // namespace

TEST(NetTransport,
     game_server_multi_seat_peer_routes_inputs_strictly_by_local_slot)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    // Two seats on one peer, deliberately bound to global player indices
    // (2, 3) that DIFFER from their local slots (0, 1): the legacy
    // single-active-slot heuristic would look at input slots 2/3 (idle here),
    // so only strict slot mapping routes these inputs.
    walker* const seat0_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const seat1_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seat0_control);
    ASSERT_NE(nullptr, seat1_control);
    seat0_control->setxy(32, 48);
    seat1_control->setxy(96, 48);
    seat0_control->set_user(2);
    seat1_control->set_user(3);
    seat0_control->set_act_type(ACT_CONTROL);
    seat1_control->set_act_type(ACT_CONTROL);
    server.bind_player(7u, 2u, fixture.world().my_team, seat0_control, 0u);
    server.bind_player(7u, 3u, fixture.world().my_team, seat1_control, 1u);
    EXPECT_EQ(seat0_control, server.player_control(2u));
    EXPECT_EQ(seat1_control, server.player_control(3u));

    InputState seats_input;
    seats_input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    seats_input.players[1].held[static_cast<int>(InputAction::MoveLeft)] = true;
    const auto input_bytes = og::sim::serialize_input(1u, seats_input);
    transport.queue_received(
        7u,
        std::vector<std::uint8_t>(input_bytes.begin(), input_bytes.end()));
    transport.set_connected_peers({});

    server.step();

    // The peer dropped with the input still pending: one DisconnectedPlayer
    // PER SEAT, sharing the peer's session token, each repeating the held
    // input of ITS OWN slot.
    ASSERT_EQ(2u, server.disconnected_players().size());
    const auto find_seat = [&server](std::size_t player_index)
        -> const og::sim::DisconnectedPlayer* {
        for (const auto& entry : server.disconnected_players())
        {
            if (entry.player_index == player_index)
                return &entry;
        }
        return nullptr;
    };
    const og::sim::DisconnectedPlayer* const seat0 = find_seat(2u);
    const og::sim::DisconnectedPlayer* const seat1 = find_seat(3u);
    ASSERT_NE(nullptr, seat0);
    ASSERT_NE(nullptr, seat1);
    EXPECT_EQ(0u, seat0->local_slot);
    EXPECT_EQ(1u, seat1->local_slot);
    EXPECT_EQ(seat0->session_token, seat1->session_token);
    EXPECT_TRUE(
        seat0->repeated_input.held[static_cast<int>(InputAction::MoveRight)]);
    EXPECT_FALSE(
        seat0->repeated_input.held[static_cast<int>(InputAction::MoveLeft)]);
    EXPECT_TRUE(
        seat1->repeated_input.held[static_cast<int>(InputAction::MoveLeft)]);
    EXPECT_FALSE(
        seat1->repeated_input.held[static_cast<int>(InputAction::MoveRight)]);

    // A single hello carrying the shared session token reconnects the peer
    // and rebinds EVERY seat.
    const og::sim::SessionToken token = seat0->session_token;
    transport.set_connected_peers({9u});
    server.poll_incoming_messages();
    og::sim::HelloMessage hello;
    hello.session_token = token;
    const auto hello_bytes = og::sim::serialize_hello(hello);
    transport.queue_received(
        9u,
        std::vector<std::uint8_t>(hello_bytes.begin(), hello_bytes.end()));

    server.step();

    EXPECT_TRUE(server.disconnected_players().empty());
    EXPECT_EQ(seat0_control, server.player_control(2u));
    EXPECT_EQ(seat1_control, server.player_control(3u));
    EXPECT_EQ(2, static_cast<int>(seat0_control->user()));
    EXPECT_EQ(3, static_cast<int>(seat1_control->user()));
}

TEST(NetTransport,
     game_server_multi_seat_disconnect_drops_all_seats_to_ai_and_reconnects)
{
    // A 2-seat machine drops mid-level: after the grace window EVERY seat's
    // walker must fall to AI (user tag released, act restored), and a single
    // token reconnect must restore EVERY seat's control.
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    std::uint64_t now_ms = 1000;
    server.set_wall_clock_ms_source([&] { return now_ms; });

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    walker* const seat0_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const seat1_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seat0_control);
    ASSERT_NE(nullptr, seat1_control);
    seat0_control->setxy(32, 48);
    seat1_control->setxy(96, 48);
    seat0_control->set_user(2);
    seat1_control->set_user(3);
    seat0_control->set_act_type(ACT_CONTROL);
    seat1_control->set_act_type(ACT_CONTROL);
    server.bind_player(7u, 2u, fixture.world().my_team, seat0_control, 0u);
    server.bind_player(7u, 3u, fixture.world().my_team, seat1_control, 1u);

    transport.set_connected_peers({});
    server.step();
    ASSERT_EQ(2u, server.disconnected_players().size());
    // Inside the grace window the seats keep their user claims (the server
    // repeats each seat's last input).
    EXPECT_EQ(2, static_cast<int>(seat0_control->user()));
    EXPECT_EQ(3, static_cast<int>(seat1_control->user()));
    const og::sim::SessionToken token =
        server.disconnected_players().front().session_token;

    // Grace expires: EVERY seat's walker drops to AI.
    now_ms += og::sim::DISCONNECT_TIMEOUT_MS + 1u;
    server.step();
    for (const og::sim::DisconnectedPlayer& seat :
         server.disconnected_players())
    {
        EXPECT_TRUE(seat.ai_control_enabled)
            << "seat player " << seat.player_index;
    }
    EXPECT_EQ(-1, static_cast<int>(seat0_control->user()))
        << "seat 0's walker must be released to AI";
    EXPECT_EQ(-1, static_cast<int>(seat1_control->user()))
        << "seat 1's walker must be released to AI";

    // One hello with the shared token restores BOTH seats' controls.
    transport.set_connected_peers({9u});
    server.poll_incoming_messages();
    og::sim::HelloMessage hello;
    hello.session_token = token;
    const auto hello_bytes = og::sim::serialize_hello(hello);
    transport.queue_received(
        9u,
        std::vector<std::uint8_t>(hello_bytes.begin(), hello_bytes.end()));
    server.step();

    EXPECT_TRUE(server.disconnected_players().empty());
    EXPECT_EQ(seat0_control, server.player_control(2u));
    EXPECT_EQ(seat1_control, server.player_control(3u));
    EXPECT_EQ(2, static_cast<int>(seat0_control->user()));
    EXPECT_EQ(3, static_cast<int>(seat1_control->user()));
}

TEST(NetTransport,
     game_server_single_seat_peer_keeps_active_slot_input_heuristic)
{
    // The openglad_text / curses shape: a single-binding peer bound to a
    // NONZERO global player index keeps sending its input in slot 0. The
    // legacy single-active-slot heuristic must keep routing it — strict
    // local_slot mapping applies only to multi-binding peers.
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(32, 48);
    control->set_user(2);
    control->set_act_type(ACT_CONTROL);
    server.bind_player(7u, 2u, fixture.world().my_team, control);

    InputState slot0_input;
    slot0_input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    const auto input_bytes = og::sim::serialize_input(1u, slot0_input);
    transport.queue_received(
        7u,
        std::vector<std::uint8_t>(input_bytes.begin(), input_bytes.end()));
    transport.set_connected_peers({});

    server.step();

    // The disconnect snapshot captures the seat's last effective input: the
    // heuristic must have routed the slot-0 hold onto player_index 2.
    ASSERT_EQ(1u, server.disconnected_players().size());
    const og::sim::DisconnectedPlayer& seat =
        server.disconnected_players().front();
    EXPECT_EQ(2u, seat.player_index);
    EXPECT_EQ(0u, seat.local_slot);
    EXPECT_TRUE(
        seat.repeated_input.held[static_cast<int>(InputAction::MoveRight)]);
}

TEST(NetTransport, game_client_control_change_reaches_high_player_indices)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 1u);

    // Global player indices above the per-machine MAX_PLAYERS are valid wire
    // targets now: index 12 lands in the widened controlled-entity table.
    transport.queue_received(
        1u,
        og::sim::serialize_control_change_message({
            .player_index = 12u,
            .entity_id = 777u,
        }));
    client.poll_messages();

    EXPECT_EQ(777u, client.controlled_entity_ids()[12]);
    // ...and an out-of-range index is ignored, not written out of bounds.
    transport.queue_received(
        1u,
        og::sim::serialize_control_change_message({
            .player_index = static_cast<std::uint8_t>(
                og::sim::kMaxGlobalPlayers),
            .entity_id = 888u,
        }));
    client.poll_messages();
    for (const std::uint32_t entity_id : client.controlled_entity_ids())
        EXPECT_NE(888u, entity_id);
}
