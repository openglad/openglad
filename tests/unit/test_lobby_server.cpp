#include <openglad/core/constants.h>
#include <openglad/gameplay/lobby_server.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

class MockLobbyTransport final : public og::sim::ITransport
{
public:
    explicit MockLobbyTransport(bool typed_messages = false)
        : typed_messages_(typed_messages)
    {
    }

    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_messages_.push_back(
            {peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    [[nodiscard]] bool supports_typed_messages() const noexcept override
    {
        return typed_messages_;
    }

    [[nodiscard]] std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained = std::move(received_raw_);
        received_raw_.clear();
        return drained;
    }

    [[nodiscard]] std::vector<og::sim::TypedReceivedMessage> poll_typed() override
    {
        std::vector<og::sim::TypedReceivedMessage> drained =
            std::move(received_typed_);
        received_typed_.clear();
        return drained;
    }

    void accept_connections() override {}

    void disconnect(og::sim::PeerId peer_id) override
    {
        disconnected_peers_.push_back(peer_id);
    }

    [[nodiscard]] std::vector<og::sim::PeerId> connected_peers() const override
    {
        return {};
    }

    void queue_lobby_message(og::sim::PeerId peer_id,
                             const og::sim::LobbyMessage& message)
    {
        if (typed_messages_)
        {
            og::sim::TypedReceivedMessage typed_message;
            typed_message.peer_id = peer_id;
            typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyMessage;
            typed_message.lobby_message =
                std::make_shared<og::sim::LobbyMessage>(message);
            received_typed_.push_back(std::move(typed_message));
            return;
        }

        received_raw_.push_back(
            {peer_id, og::sim::serialize_lobby_message(message)});
    }

    void queue_raw_message(og::sim::PeerId peer_id,
                           std::vector<std::uint8_t> data)
    {
        received_raw_.push_back({peer_id, std::move(data)});
    }

    void clear_sent_messages()
    {
        sent_messages_.clear();
    }

    [[nodiscard]] const std::vector<og::sim::ReceivedMessage>&
    sent_messages() const noexcept
    {
        return sent_messages_;
    }

    [[nodiscard]] const std::vector<og::sim::PeerId>&
    disconnected_peers() const noexcept
    {
        return disconnected_peers_;
    }

private:
    bool typed_messages_ = false;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
    std::vector<og::sim::ReceivedMessage> received_raw_;
    std::vector<og::sim::TypedReceivedMessage> received_typed_;
    std::vector<og::sim::PeerId> disconnected_peers_;
};

og::sim::LobbyCharacterSlot make_slot(std::uint8_t slot_index,
                                      std::int32_t guy_id,
                                      const char* name,
                                      std::int8_t family)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = guy_id;
    character.name = name;
    character.family = family;
    character.strength = 10;
    character.dexterity = 11;
    character.constitution = 12;
    character.intelligence = 13;
    character.armor = 14;
    character.level = 3;

    return {
        .slot_index = slot_index,
        .character = character,
    };
}

og::sim::LobbyMessage make_join_message(
    const char* name,
    std::int16_t requested_team,
    std::vector<og::sim::LobbyCharacterSlot> slots,
    bool ready = false)
{
    og::sim::LobbyPlayer player;
    player.name = name;
    player.team = requested_team;
    player.character_slots = std::move(slots);
    player.ready = ready;

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{.player = std::move(player)};
    return message;
}

std::vector<og::sim::LobbyCharacterSlot> make_slots(std::uint8_t first_slot_index,
                                                    std::size_t count,
                                                    std::int32_t first_guy_id,
                                                    std::int8_t family)
{
    std::vector<og::sim::LobbyCharacterSlot> slots;
    slots.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        slots.push_back(make_slot(
            static_cast<std::uint8_t>(first_slot_index + index),
            first_guy_id + static_cast<std::int32_t>(index),
            "Guy",
            family));
    }
    return slots;
}

og::sim::LobbyState decode_lobby_state(
    const og::sim::ReceivedMessage& message)
{
    const auto decoded = og::sim::deserialize_lobby_state_message(message.data);
    EXPECT_TRUE(decoded.has_value());
    return decoded.value_or(og::sim::LobbyState{});
}

og::sim::LobbyMessage decode_lobby_message(
    const og::sim::ReceivedMessage& message)
{
    const auto decoded = og::sim::deserialize_lobby_message(message.data);
    EXPECT_TRUE(decoded.has_value());
    return decoded.value_or(og::sim::LobbyMessage{});
}

void expect_all_sent_states_equal(
    const std::vector<og::sim::ReceivedMessage>& sent_messages,
    const og::sim::LobbyState& expected)
{
    ASSERT_FALSE(sent_messages.empty());
    for (const auto& message : sent_messages)
        EXPECT_EQ(expected, decode_lobby_state(message));
}

} // namespace

TEST(LobbyServer, raw_join_flow_broadcasts_state_and_populates_save_data)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message(
            "Host",
            0,
            {make_slot(1u, 101, "Mage", FAMILY_MAGE),
             make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    og::sim::LobbyState state = server.state();
    ASSERT_EQ(1u, state.players.size());
    EXPECT_EQ("Host", state.players[0].name);
    EXPECT_EQ(0u, state.players[0].player_index);
    EXPECT_TRUE(state.players[0].is_host);
    EXPECT_EQ(0, state.players[0].team);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), state);

    transport.clear_sent_messages();
    transport.queue_lobby_message(
        22u,
        make_join_message("Guest",
                          0,
                          {make_slot(2u, 200, "Archer", FAMILY_ARCHER)},
                          true));
    server.poll_incoming_messages();

    state = server.state();
    ASSERT_EQ(2u, state.players.size());
    EXPECT_EQ(0, state.players[0].team);
    EXPECT_EQ(1, state.players[1].team);
    EXPECT_TRUE(state.players[1].ready);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), state);

    transport.clear_sent_messages();
    og::sim::LobbySettings settings;
    settings.campaign_id = "custom.campaign";
    settings.scenario_id = 7;
    settings.difficulty = 2;
    settings.allied_mode = 0;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = settings,
    };
    transport.queue_lobby_message(11u, settings_message);
    server.poll_incoming_messages();

    state = server.state();
    EXPECT_EQ("custom.campaign", state.settings.campaign_id);
    EXPECT_EQ(7, state.settings.scenario_id);
    EXPECT_EQ(2, state.settings.difficulty);
    EXPECT_EQ(0, state.settings.allied_mode);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), state);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    EXPECT_EQ(2u, equivalent.numplayers);
    EXPECT_EQ(0, equivalent.allied_mode);
    EXPECT_EQ("custom.campaign", equivalent.current_campaign);
    EXPECT_EQ(7, equivalent.scen_num);
    ASSERT_EQ(3u, equivalent.team_list.size());
    EXPECT_EQ(0u, equivalent.team_list[0].slot_index);
    EXPECT_EQ(1u, equivalent.team_list[1].slot_index);
    EXPECT_EQ(2u, equivalent.team_list[2].slot_index);
    EXPECT_EQ("Soldier", equivalent.team_list[0].character.name);
    EXPECT_EQ("Mage", equivalent.team_list[1].character.name);
    EXPECT_EQ("Archer", equivalent.team_list[2].character.name);
    EXPECT_EQ(0, equivalent.team_list[0].character.teamnum);
    EXPECT_EQ(0, equivalent.team_list[1].character.teamnum);
    EXPECT_EQ(1, equivalent.team_list[2].character.teamnum);
}

TEST(LobbyServer, ready_team_change_and_leave_update_state_and_broadcasts)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.clear_sent_messages();
    og::sim::LobbyMessage ready_message;
    ready_message.payload = og::sim::LobbyReadyMessage{
        .player_index = 1u,
        .ready = true,
    };
    transport.queue_lobby_message(22u, ready_message);
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_TRUE(server.state().players[1].ready);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());

    transport.clear_sent_messages();
    og::sim::LobbyMessage team_change;
    team_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 1u,
        .team = 2,
    };
    transport.queue_lobby_message(22u, team_change);
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(2, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(2, server.state().players[1].character_slots[0].character.teamnum);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(2u, equivalent.team_list.size());
    EXPECT_EQ(2, equivalent.team_list[1].character.teamnum);

    transport.clear_sent_messages();
    og::sim::LobbyMessage leave_message;
    leave_message.payload = og::sim::LobbyLeaveMessage{.player_index = 1u};
    transport.queue_lobby_message(22u, leave_message);
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.state().players.size());
    EXPECT_EQ("Host", server.state().players[0].name);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());
}

TEST(LobbyServer, typed_messages_reject_non_host_settings_and_migrate_host_on_disconnect)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.clear_sent_messages();
    og::sim::LobbySettings settings = server.state().settings;
    settings.scenario_id = 9;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 1u,
        .settings = settings,
    };
    transport.queue_lobby_message(22u, settings_message);
    server.poll_incoming_messages();

    EXPECT_TRUE(transport.sent_messages().empty());
    EXPECT_EQ(1, server.state().settings.scenario_id);

    server.disconnect_client(11u);
    ASSERT_EQ((std::vector<og::sim::PeerId>{11u}), transport.disconnected_peers());
    ASSERT_EQ(1u, transport.sent_messages().size());

    const og::sim::LobbyState migrated = decode_lobby_state(transport.sent_messages()[0]);
    ASSERT_EQ(1u, migrated.players.size());
    EXPECT_EQ("Guest", migrated.players[0].name);
    EXPECT_TRUE(migrated.players[0].is_host);
    EXPECT_EQ(0u, migrated.players[0].player_index);
    EXPECT_EQ(1, migrated.players[0].team);
}

TEST(LobbyServer, first_connected_peer_remains_host_even_if_another_peer_joins_first)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.state().players.size());
    EXPECT_EQ("Guest", server.state().players[0].name);
    EXPECT_FALSE(server.state().players[0].is_host);
    EXPECT_EQ(0u, server.state().players[0].player_index);

    transport.clear_sent_messages();
    og::sim::LobbySettings settings = server.state().settings;
    settings.scenario_id = 9;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = settings,
    };
    transport.queue_lobby_message(22u, settings_message);

    og::sim::LobbyMessage start_message;
    start_message.payload = og::sim::LobbyStartGameMessage{.player_index = 0u};
    transport.queue_lobby_message(22u, start_message);
    server.poll_incoming_messages();

    EXPECT_EQ(1, server.state().settings.scenario_id);
    EXPECT_FALSE(server.start_game_requested());
    EXPECT_TRUE(transport.sent_messages().empty());

    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ("Host", server.state().players[0].name);
    EXPECT_TRUE(server.state().players[0].is_host);
    EXPECT_EQ(0u, server.state().players[0].player_index);
    EXPECT_EQ("Guest", server.state().players[1].name);
    EXPECT_FALSE(server.state().players[1].is_host);
    EXPECT_EQ(1u, server.state().players[1].player_index);
}

TEST(LobbyServer, malformed_lobby_message_throws)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);

    std::vector<std::uint8_t> malformed = og::sim::serialize_lobby_message(
        make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    malformed.pop_back();
    transport.queue_raw_message(11u, std::move(malformed));

    EXPECT_THROW(server.poll_incoming_messages(), std::runtime_error);
}

TEST(LobbyServer, fifth_join_is_rejected_when_lobby_is_full)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    for (og::sim::PeerId peer_id = 1u; peer_id <= 5u; ++peer_id)
        server.connect_client(peer_id);
    transport.clear_sent_messages();

    for (og::sim::PeerId peer_id = 1u; peer_id <= 4u; ++peer_id)
    {
        transport.queue_lobby_message(
            peer_id,
            make_join_message("Player", static_cast<std::int16_t>(peer_id - 1u),
                              {make_slot(static_cast<std::uint8_t>(peer_id - 1u),
                                         static_cast<std::int32_t>(100 + peer_id),
                                         "Guy",
                                         FAMILY_SOLDIER)}));
    }
    server.poll_incoming_messages();

    transport.clear_sent_messages();
    transport.queue_lobby_message(
        5u,
        make_join_message("Overflow", 3, {make_slot(4u, 500, "Extra Guy", FAMILY_MAGE)}));
    server.poll_incoming_messages();

    ASSERT_EQ(4u, server.state().players.size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(5u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(server.state(), decode_lobby_state(transport.sent_messages()[0]));
}

TEST(LobbyServer, disconnecting_unjoined_host_promotes_joined_player_immediately)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.state().players.size());
    EXPECT_FALSE(server.state().players[0].is_host);

    transport.clear_sent_messages();
    server.disconnect_client(11u);

    ASSERT_EQ((std::vector<og::sim::PeerId>{11u}), transport.disconnected_peers());
    ASSERT_EQ(1u, transport.sent_messages().size());

    const og::sim::LobbyState promoted = decode_lobby_state(transport.sent_messages()[0]);
    ASSERT_EQ(1u, promoted.players.size());
    EXPECT_EQ("Guest", promoted.players[0].name);
    EXPECT_TRUE(promoted.players[0].is_host);
    EXPECT_EQ(0u, promoted.players[0].player_index);
}

TEST(LobbyServer, slot_sanitization_and_save_data_mapping_compact_sparse_slots)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message(
            "Host",
            0,
            {make_slot(5u, 100, "First", FAMILY_SOLDIER),
             make_slot(5u, 101, "Duplicate", FAMILY_MAGE),
             make_slot(9u, 102, "Second", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.state().players.size());
    ASSERT_EQ(2u, server.state().players[0].character_slots.size());
    EXPECT_EQ("First", server.state().players[0].character_slots[0].character.name);
    EXPECT_EQ("Second", server.state().players[0].character_slots[1].character.name);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(2u, equivalent.team_list.size());
    EXPECT_EQ(0u, equivalent.team_list[0].slot_index);
    EXPECT_EQ(1u, equivalent.team_list[1].slot_index);
    EXPECT_EQ("First", equivalent.team_list[0].character.name);
    EXPECT_EQ("Second", equivalent.team_list[1].character.name);
}

TEST(LobbyServer, join_trims_total_character_count_to_save_data_limit)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, make_slots(0u, 20u, 100, FAMILY_SOLDIER)));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, make_slots(20u, 10u, 200, FAMILY_ARCHER)));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(20u, server.state().players[0].character_slots.size());
    EXPECT_EQ(4u, server.state().players[1].character_slots.size());
    EXPECT_EQ(20u, server.state().players[1].character_slots[0].slot_index);
    EXPECT_EQ(23u, server.state().players[1].character_slots[3].slot_index);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(24u, equivalent.team_list.size());
    EXPECT_EQ(0u, equivalent.team_list.front().slot_index);
    EXPECT_EQ(23u, equivalent.team_list.back().slot_index);
    EXPECT_EQ(1, equivalent.team_list.back().character.teamnum);
}

TEST(LobbyServer, host_only_start_broadcasts_confirmation_and_freezes_lobby_state)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.clear_sent_messages();
    og::sim::LobbyMessage non_host_start;
    non_host_start.payload = og::sim::LobbyStartGameMessage{.player_index = 1u};
    transport.queue_lobby_message(22u, non_host_start);
    server.poll_incoming_messages();

    EXPECT_FALSE(server.start_game_requested());
    EXPECT_TRUE(transport.sent_messages().empty());

    og::sim::LobbyMessage host_start;
    host_start.payload = og::sim::LobbyStartGameMessage{.player_index = 0u};
    transport.queue_lobby_message(11u, host_start);
    server.poll_incoming_messages();

    EXPECT_TRUE(server.start_game_requested());
    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const auto& sent_message : transport.sent_messages())
    {
        const og::sim::LobbyMessage decoded = decode_lobby_message(sent_message);
        EXPECT_EQ(og::sim::LobbyMessageKind::StartGame, decoded.kind());
        EXPECT_EQ(0u, std::get<og::sim::LobbyStartGameMessage>(decoded.payload).player_index);
    }
    EXPECT_TRUE(server.consume_start_game_requested());
    EXPECT_FALSE(server.consume_start_game_requested());
    EXPECT_TRUE(server.start_game_requested());

    transport.clear_sent_messages();
    og::sim::LobbyMessage team_change;
    team_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 1u,
        .team = 2,
    };
    transport.queue_lobby_message(22u, team_change);
    server.poll_incoming_messages();

    EXPECT_TRUE(transport.sent_messages().empty());
    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(1, server.state().players[1].team);
}
