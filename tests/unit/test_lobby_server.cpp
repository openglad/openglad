#include <openglad/core/constants.h>
#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/lobby_server.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
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
        return connected_peers_;
    }

    void set_connected_peers(std::vector<og::sim::PeerId> peers)
    {
        connected_peers_ = std::move(peers);
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

    void queue_malformed_typed_message(og::sim::PeerId peer_id)
    {
        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = peer_id;
        typed_message.kind = og::sim::TypedReceivedMessageKind::Malformed;
        received_typed_.push_back(std::move(typed_message));
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
    std::vector<og::sim::PeerId> connected_peers_;
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

TEST(LobbyServer, poll_registers_connected_transport_peers)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);

    transport.set_connected_peers({11u});
    server.poll_incoming_messages();

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(11u, transport.sent_messages().front().peer_id);
    EXPECT_EQ(server.state(), decode_lobby_state(transport.sent_messages().front()));
}

TEST(LobbyServer, poll_disconnects_removed_transport_peers)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);

    transport.set_connected_peers({11u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    transport.set_connected_peers({});
    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{11u}), transport.disconnected_peers());
    EXPECT_TRUE(transport.sent_messages().empty());
}

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
    EXPECT_EQ(0u, state.host_player_id);
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
    // Default settings are allied: cross-peer team sharing is allowed, so the
    // guest keeps its requested team 0 alongside the host.
    EXPECT_EQ(0, state.players[1].team);
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
    // Non-allied classic teams are exclusive again: the shared->exclusive
    // transition de-shares the guest off the host's team.
    EXPECT_EQ(0, state.players[0].team);
    EXPECT_EQ(1, state.players[1].team);
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

TEST(LobbyServer, sanitize_clamps_ctf_settings_and_equivalent_carries_them)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    // Out-of-range CTF settings from the host get clamped, not echoed.
    og::sim::LobbySettings wild;
    wild.campaign_id = "org.openglad.ctf";
    wild.scenario_id = 500;
    wild.difficulty = 1;
    wild.allied_mode = 1;
    wild.ctf_team_count = 9;       // -> 4
    wild.ctf_capture_limit = 99;   // -> 50
    wild.ctf_respawn_ticks = 5;    // nonzero -> raised to 12
    og::sim::LobbyMessage wild_message;
    wild_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = wild,
    };
    transport.queue_lobby_message(11u, wild_message);
    server.poll_incoming_messages();

    og::sim::LobbyState state = server.state();
    EXPECT_EQ(4, state.settings.ctf_team_count);
    EXPECT_EQ(50, state.settings.ctf_capture_limit);
    EXPECT_EQ(12, state.settings.ctf_respawn_ticks);

    // In-range values (and the 0 = map/default sentinels) pass through.
    og::sim::LobbySettings sane = wild;
    sane.ctf_team_count = 3;
    sane.ctf_capture_limit = 0;
    sane.ctf_respawn_ticks = 0;
    og::sim::LobbyMessage sane_message;
    sane_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = sane,
    };
    transport.queue_lobby_message(11u, sane_message);
    server.poll_incoming_messages();

    state = server.state();
    EXPECT_EQ(3, state.settings.ctf_team_count);
    EXPECT_EQ(0, state.settings.ctf_capture_limit);
    EXPECT_EQ(0, state.settings.ctf_respawn_ticks);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    EXPECT_EQ(3, equivalent.ctf_team_count);
    EXPECT_EQ(0, equivalent.ctf_capture_limit);
    EXPECT_EQ(0, equivalent.ctf_respawn_ticks);
}

TEST(LobbyServer, local_session_sanitize_preserves_tower_campaign_pair)
{
    // Tower-triple §5.9 layer 3, locality amendment: the solo picker
    // round-trips its OWN settings through an in-process LobbyServer built
    // with local_session=true — the tower pick must survive that echo (the
    // unconditional rejection silently reverted a just-picked tower to
    // gladiator/scen1 without a remount: the ghost-session regression).
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport, /*local_session=*/true);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings tower;
    tower.campaign_id = std::string(og::kTowerCampaignId);
    tower.scenario_id = 700;
    tower.difficulty = 1;
    tower.allied_mode = 1;
    og::sim::LobbyMessage tower_message;
    tower_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = tower,
    };
    transport.queue_lobby_message(11u, tower_message);
    server.poll_incoming_messages();

    const og::sim::LobbyState state = server.state();
    EXPECT_EQ(std::string(og::kTowerCampaignId), state.settings.campaign_id);
    EXPECT_EQ(700, state.settings.scenario_id);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    EXPECT_EQ(std::string(og::kTowerCampaignId), equivalent.current_campaign);
    EXPECT_EQ(700, equivalent.scen_num);
}

TEST(LobbyServer, default_networked_sanitize_still_rejects_tower_campaign)
{
    // The flag defaults to false: every networked construction site keeps
    // the crafted-client backstop without opting into anything.
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    const og::sim::LobbySettings before = server.state().settings;
    ASSERT_NE(std::string(og::kTowerCampaignId), before.campaign_id);

    og::sim::LobbySettings tower;
    tower.campaign_id = std::string(og::kTowerCampaignId);
    tower.scenario_id = 700;
    tower.difficulty = 1;
    tower.allied_mode = 1;
    og::sim::LobbyMessage tower_message;
    tower_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = tower,
    };
    transport.queue_lobby_message(11u, tower_message);
    server.poll_incoming_messages();

    const og::sim::LobbyState state = server.state();
    EXPECT_EQ(before.campaign_id, state.settings.campaign_id);
    EXPECT_EQ(before.scenario_id, state.settings.scenario_id);
}

TEST(LobbyServer, build_save_data_equivalent_tags_owner_and_origin_slot)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    // Host (player 0) brings two characters from non-contiguous save slots; the
    // guest (player 1) brings one from a high slot. The combined roster gets
    // re-densified (slot_index 0,1,2), but every entry must remember WHO owns it
    // and the owner's ORIGINAL save slot — that is how each peer later writes
    // progress back to the right slot in its own save0.
    transport.queue_lobby_message(
        11u,
        make_join_message("Host",
                          0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER),
                           make_slot(3u, 101, "Mage", FAMILY_MAGE)}));
    server.poll_incoming_messages();
    transport.queue_lobby_message(
        22u,
        make_join_message("Guest",
                          0,
                          {make_slot(5u, 200, "Archer", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(3u, equivalent.team_list.size());

    // Combined indices are re-densified...
    EXPECT_EQ(0u, equivalent.team_list[0].slot_index);
    EXPECT_EQ(1u, equivalent.team_list[1].slot_index);
    EXPECT_EQ(2u, equivalent.team_list[2].slot_index);

    // ...but owner + the owner's original save slot are preserved.
    EXPECT_EQ("Soldier", equivalent.team_list[0].character.name);
    EXPECT_EQ(0u, equivalent.team_list[0].owner_player_index);
    EXPECT_EQ(0u, equivalent.team_list[0].owner_save_slot);

    EXPECT_EQ("Mage", equivalent.team_list[1].character.name);
    EXPECT_EQ(0u, equivalent.team_list[1].owner_player_index);
    EXPECT_EQ(3u, equivalent.team_list[1].owner_save_slot);

    EXPECT_EQ("Archer", equivalent.team_list[2].character.name);
    EXPECT_EQ(1u, equivalent.team_list[2].owner_player_index);
    EXPECT_EQ(5u, equivalent.team_list[2].owner_save_slot);
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

    og::sim::LobbySettings enemy_settings;
    enemy_settings.campaign_id = "org.openglad.gladiator";
    enemy_settings.scenario_id = 1;
    enemy_settings.difficulty = 1;
    enemy_settings.allied_mode = 0;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = std::move(enemy_settings),
    };
    transport.queue_lobby_message(11u, settings_message);
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
    EXPECT_EQ(0u, migrated.host_player_id);
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
    EXPECT_EQ(0xffu, server.state().host_player_id);

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
    EXPECT_EQ(0u, server.state().host_player_id);
    EXPECT_EQ("Guest", server.state().players[1].name);
    EXPECT_FALSE(server.state().players[1].is_host);
    EXPECT_EQ(1u, server.state().players[1].player_index);
}

TEST(LobbyServer, malformed_lobby_message_disconnects_peer)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.clear_sent_messages();

    std::vector<std::uint8_t> malformed = og::sim::serialize_lobby_message(
        make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    malformed.pop_back();
    transport.queue_raw_message(11u, std::move(malformed));

    EXPECT_NO_THROW(server.poll_incoming_messages());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u}), transport.disconnected_peers());
    EXPECT_TRUE(server.state().players.empty());
}

TEST(LobbyServer, malformed_typed_message_disconnects_peer)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.clear_sent_messages();

    transport.queue_malformed_typed_message(11u);

    EXPECT_NO_THROW(server.poll_incoming_messages());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u}), transport.disconnected_peers());
    EXPECT_TRUE(server.state().players.empty());
}

TEST(LobbyServer, fifth_join_is_rejected_when_lobby_is_full)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    for (og::sim::PeerId peer_id = 1u; peer_id <= 5u; ++peer_id)
        server.connect_client(peer_id);

    // Non-allied classic teams are exclusive, so four seats exhaust the team
    // range (the rules cap; allied/CTF lobbies share teams and admit up to
    // kMaxGlobalPlayers seats instead).
    og::sim::LobbySettings classic_settings;
    classic_settings.allied_mode = 0;
    og::sim::LobbyMessage classic_message;
    classic_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = classic_settings,
    };
    transport.queue_lobby_message(1u, classic_message);
    server.poll_incoming_messages();
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
    EXPECT_EQ(0xffu, server.state().host_player_id);

    transport.clear_sent_messages();
    server.disconnect_client(11u);

    ASSERT_EQ((std::vector<og::sim::PeerId>{11u}), transport.disconnected_peers());
    ASSERT_EQ(1u, transport.sent_messages().size());

    const og::sim::LobbyState promoted = decode_lobby_state(transport.sent_messages()[0]);
    ASSERT_EQ(1u, promoted.players.size());
    EXPECT_EQ("Guest", promoted.players[0].name);
    EXPECT_TRUE(promoted.players[0].is_host);
    EXPECT_EQ(0u, promoted.players[0].player_index);
    EXPECT_EQ(0u, promoted.host_player_id);
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

    og::sim::LobbySettings enemy_settings;
    enemy_settings.campaign_id = "org.openglad.gladiator";
    enemy_settings.scenario_id = 1;
    enemy_settings.difficulty = 1;
    enemy_settings.allied_mode = 0;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = std::move(enemy_settings),
    };
    transport.queue_lobby_message(11u, settings_message);
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

TEST(LobbyServer,
     build_player_bindings_follow_final_player_indices_and_exclude_unjoined_peers)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    server.connect_client(33u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings enemy_settings;
    enemy_settings.campaign_id = "org.openglad.gladiator";
    enemy_settings.scenario_id = 1;
    enemy_settings.difficulty = 1;
    enemy_settings.allied_mode = 0;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = std::move(enemy_settings),
    };
    transport.queue_lobby_message(11u, settings_message);
    server.poll_incoming_messages();

    const std::vector<og::sim::LobbyPlayerBinding> bindings =
        server.build_player_bindings();
    ASSERT_EQ(2u, bindings.size());
    EXPECT_EQ((og::sim::LobbyPlayerBinding{
                  .peer_id = 11u,
                  .player_index = 0u,
                  .team = 0,
              }),
              bindings[0]);
    EXPECT_EQ((og::sim::LobbyPlayerBinding{
                  .peer_id = 22u,
                  .player_index = 1u,
                  .team = 1,
              }),
              bindings[1]);
}

TEST(LobbyServer, allied_mode_normalizes_game_start_teams_to_team_zero)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 2, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 3, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings allied_settings;
    allied_settings.campaign_id = "org.openglad.gladiator";
    allied_settings.scenario_id = 1;
    allied_settings.difficulty = 1;
    allied_settings.allied_mode = 1;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = std::move(allied_settings),
    };
    transport.queue_lobby_message(11u, settings_message);
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(2, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(2u, equivalent.team_list.size());
    EXPECT_EQ(1, equivalent.allied_mode);
    EXPECT_EQ(0, equivalent.team_list[0].character.teamnum);
    EXPECT_EQ(0, equivalent.team_list[1].character.teamnum);

    const std::vector<og::sim::LobbyPlayerBinding> bindings =
        server.build_player_bindings();
    ASSERT_EQ(2u, bindings.size());
    EXPECT_EQ(0, bindings[0].team);
    EXPECT_EQ(0, bindings[1].team);
}

namespace {

og::sim::LobbyMessage make_settings_change_message(
    const og::sim::LobbySettings& settings)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = settings,
    };
    return message;
}

og::sim::LobbySettings make_ctf_lobby_settings(std::int16_t team_count = 0)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = "org.openglad.ctf";
    settings.scenario_id = 1;
    settings.difficulty = 1;
    settings.allied_mode = 0;
    settings.ctf_team_count = team_count;
    return settings;
}

og::sim::LobbyMessage make_team_change_message(std::int16_t team)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 0xffu, // identity comes from the sending peer
        .team = team,
    };
    return message;
}

} // namespace

TEST(LobbyServer, sanitize_strip_flag_accepts_binary_and_rejects_junk)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    // Default is 0 (keep authored troops).
    EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops);

    // Junk falls back to the current value (0).
    og::sim::LobbySettings junk = make_ctf_lobby_settings();
    junk.ctf_strip_scenario_troops = 2;
    transport.queue_lobby_message(11u, make_settings_change_message(junk));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops);

    // 1 is accepted and carried into the game-start equivalent.
    og::sim::LobbySettings strip_on = make_ctf_lobby_settings();
    strip_on.ctf_strip_scenario_troops = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(strip_on));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.ctf_strip_scenario_troops);
    EXPECT_EQ(1, server.build_save_data_equivalent().ctf_strip_scenario_troops);

    // Junk now falls back to the accepted value (1), not 0.
    og::sim::LobbySettings junk_again = make_ctf_lobby_settings();
    junk_again.ctf_strip_scenario_troops = -3;
    transport.queue_lobby_message(11u, make_settings_change_message(junk_again));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.ctf_strip_scenario_troops);

    // 0 passes through.
    og::sim::LobbySettings strip_off = make_ctf_lobby_settings();
    strip_off.ctf_strip_scenario_troops = 0;
    transport.queue_lobby_message(11u, make_settings_change_message(strip_off));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops);
}

TEST(LobbyServer, sanitize_difficulty_submenu_settings)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    // Defaults are 0 (legacy behavior).
    EXPECT_EQ(0, server.state().settings.respawn_mode);
    EXPECT_EQ(0, server.state().settings.generator_rate);
    EXPECT_EQ(0, server.state().settings.keep_fallen_heroes);

    // In-range values pass through and reach the game-start equivalent.
    og::sim::LobbySettings valid = make_ctf_lobby_settings();
    valid.respawn_mode = 2;
    valid.generator_rate = 200;
    valid.keep_fallen_heroes = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(valid));
    server.poll_incoming_messages();
    EXPECT_EQ(2, server.state().settings.respawn_mode);
    EXPECT_EQ(200, server.state().settings.generator_rate);
    EXPECT_EQ(1, server.state().settings.keep_fallen_heroes);
    EXPECT_EQ(2, server.build_save_data_equivalent().respawn_mode);
    EXPECT_EQ(200, server.build_save_data_equivalent().generator_rate);
    EXPECT_EQ(1, server.build_save_data_equivalent().keep_fallen_heroes);

    // respawn_mode outside {0,1,2} falls back to the current value.
    og::sim::LobbySettings junk_mode = make_ctf_lobby_settings();
    junk_mode.respawn_mode = 3;
    junk_mode.generator_rate = 200;
    junk_mode.keep_fallen_heroes = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(junk_mode));
    server.poll_incoming_messages();
    EXPECT_EQ(2, server.state().settings.respawn_mode);

    og::sim::LobbySettings negative_mode = make_ctf_lobby_settings();
    negative_mode.respawn_mode = -1;
    transport.queue_lobby_message(11u,
                                  make_settings_change_message(negative_mode));
    server.poll_incoming_messages();
    EXPECT_EQ(2, server.state().settings.respawn_mode);

    // generator_rate: 0 = default passes through; nonzero clamps to [25,400].
    og::sim::LobbySettings low_rate = make_ctf_lobby_settings();
    low_rate.generator_rate = 3;
    transport.queue_lobby_message(11u, make_settings_change_message(low_rate));
    server.poll_incoming_messages();
    EXPECT_EQ(25, server.state().settings.generator_rate);

    og::sim::LobbySettings high_rate = make_ctf_lobby_settings();
    high_rate.generator_rate = 5000;
    transport.queue_lobby_message(11u, make_settings_change_message(high_rate));
    server.poll_incoming_messages();
    EXPECT_EQ(400, server.state().settings.generator_rate);

    og::sim::LobbySettings default_rate = make_ctf_lobby_settings();
    default_rate.generator_rate = 0;
    transport.queue_lobby_message(11u,
                                  make_settings_change_message(default_rate));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.generator_rate);

    // keep_fallen_heroes is binary-or-fallback.
    og::sim::LobbySettings keep_on = make_ctf_lobby_settings();
    keep_on.keep_fallen_heroes = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(keep_on));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.keep_fallen_heroes);

    og::sim::LobbySettings junk_keep = make_ctf_lobby_settings();
    junk_keep.keep_fallen_heroes = 7;
    junk_keep.respawn_mode = 2;
    transport.queue_lobby_message(11u, make_settings_change_message(junk_keep));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.keep_fallen_heroes)
        << "junk falls back to the previously accepted value";
}

TEST(LobbyServer, ctf_lobby_allows_shared_teams)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        11u, make_settings_change_message(make_ctf_lobby_settings()));
    server.poll_incoming_messages();

    // The guest moves onto the host's team: accepted under CTF settings.
    transport.clear_sent_messages();
    transport.queue_lobby_message(22u, make_team_change_message(0));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(0, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(0, server.state().players[1].character_slots[0].character.teamnum);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());

    // A fresh join requesting the occupied team also lands on it.
    server.connect_client(33u);
    transport.queue_lobby_message(
        33u, make_join_message("Third", 0, {make_slot(2u, 300, "Third Guy", FAMILY_MAGE)}));
    server.poll_incoming_messages();

    ASSERT_EQ(3u, server.state().players.size());
    EXPECT_EQ(0, server.state().players[2].team);
}

TEST(LobbyServer, classic_lobby_keeps_exclusive_teams)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    // Non-allied classic settings: teams are exclusive, so the host's team is
    // taken and the guest's TeamChange resolves back to its current team.
    og::sim::LobbySettings classic_settings;
    classic_settings.allied_mode = 0;
    transport.queue_lobby_message(
        11u, make_settings_change_message(classic_settings));
    server.poll_incoming_messages();

    transport.queue_lobby_message(22u, make_team_change_message(0));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(1, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(1, server.state().players[1].character_slots[0].character.teamnum);
}

TEST(LobbyServer, ctf_team_count_clamps_team_choice)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        11u, make_settings_change_message(make_ctf_lobby_settings(2)));
    server.poll_incoming_messages();

    // Team 3 is outside the explicit 2-team range: the change resolves back
    // to the guest's current (in-range) team.
    transport.queue_lobby_message(22u, make_team_change_message(3));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().players[1].team);

    // An in-range shared team is still accepted.
    transport.queue_lobby_message(22u, make_team_change_message(0));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().players[1].team);

    // A fresh join asking for team 3 is resolved into the valid range.
    server.connect_client(33u);
    transport.queue_lobby_message(
        33u, make_join_message("Third", 3, {make_slot(2u, 300, "Third Guy", FAMILY_MAGE)}));
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());
    EXPECT_LT(server.state().players[2].team, 2);
    EXPECT_GE(server.state().players[2].team, 0);
}

TEST(LobbyServer, settings_change_reteams_out_of_range_players)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 3, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();
    ASSERT_EQ(3, server.state().players[1].team);

    // Lowering the CTF team count to 2 strands the guest on team 3: the
    // server re-resolves it into range and re-stamps the guest's slots.
    transport.clear_sent_messages();
    transport.queue_lobby_message(
        11u, make_settings_change_message(make_ctf_lobby_settings(2)));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_GE(server.state().players[1].team, 0);
    EXPECT_LT(server.state().players[1].team, 2);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(server.state().players[1].team,
              server.state().players[1].character_slots[0].character.teamnum);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());
}

TEST(LobbyServer, settings_change_to_classic_deshares_teams)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        11u, make_settings_change_message(make_ctf_lobby_settings()));
    server.poll_incoming_messages();

    // Both humans legitimately share team 0 under CTF settings.
    transport.queue_lobby_message(22u, make_team_change_message(0));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    ASSERT_EQ(0, server.state().players[0].team);
    ASSERT_EQ(0, server.state().players[1].team);

    // Host switches the campaign back to classic: exclusivity returns, so
    // the later-connected player must be moved off the shared team (with
    // slots re-stamped) and the new state broadcast.
    og::sim::LobbySettings classic = make_ctf_lobby_settings();
    classic.campaign_id = "org.openglad.gladiator";
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_settings_change_message(classic));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(0, server.state().players[0].team)
        << "the earliest-connected player keeps the shared team";
    EXPECT_NE(0, server.state().players[1].team);
    EXPECT_GE(server.state().players[1].team, 0);
    EXPECT_LT(server.state().players[1].team, 4);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(server.state().players[1].team,
              server.state().players[1].character_slots[0].character.teamnum);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());
}

// ---------------------------------------------------------------------------
// Multi-seat (v7) coverage: one peer declaring several local players.
// ---------------------------------------------------------------------------

namespace {

og::sim::LobbyMessage make_multi_seat_join_message(
    const char* name,
    const std::vector<std::int16_t>& seat_teams,
    std::uint8_t first_slot_index = 0u,
    std::int32_t first_guy_id = 100)
{
    og::sim::LobbyJoinMessage join;
    for (std::size_t seat = 0; seat < seat_teams.size(); ++seat)
    {
        og::sim::LobbyPlayer player;
        player.name = seat == 0
            ? std::string(name)
            : std::string(name) + "#" + std::to_string(seat);
        player.team = seat_teams[seat];
        player.character_slots = {make_slot(
            static_cast<std::uint8_t>(first_slot_index + seat),
            first_guy_id + static_cast<std::int32_t>(seat),
            "Guy",
            FAMILY_SOLDIER)};
        if (seat == 0)
            join.player = std::move(player);
        else
            join.extra_players.push_back(std::move(player));
    }

    og::sim::LobbyMessage message;
    message.payload = std::move(join);
    return message;
}

} // namespace

TEST(LobbyServer, multi_seat_join_flattens_seats_and_binds_local_slots)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    // Default (allied) settings share teams across peers. Host declares two
    // seats, guest declares three.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1}, 0u, 100));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {0, 1, 2}, 2u, 200));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(5u, state.players.size());
    // Flatten order: (connection order, seat order); indices sequential.
    for (std::size_t index = 0; index < state.players.size(); ++index)
        EXPECT_EQ(index, state.players[index].player_index);
    EXPECT_EQ("Host", state.players[0].name);
    EXPECT_EQ("Host#1", state.players[1].name);
    EXPECT_EQ("Guest", state.players[2].name);
    EXPECT_EQ("Guest#2", state.players[4].name);
    // is_host marks only the host peer's seat 0.
    EXPECT_TRUE(state.players[0].is_host);
    EXPECT_FALSE(state.players[1].is_host);
    EXPECT_FALSE(state.players[2].is_host);
    EXPECT_EQ(0u, state.host_player_id);
    // Cross-peer sharing allowed (allied): guest seat 0 shares team 0 with
    // host seat 0; within-peer teams stay distinct.
    EXPECT_EQ(0, state.players[0].team);
    EXPECT_EQ(1, state.players[1].team);
    EXPECT_EQ(0, state.players[2].team);
    EXPECT_EQ(1, state.players[3].team);
    EXPECT_EQ(2, state.players[4].team);

    // One binding per seat, carrying the seat's local slot on its peer.
    const std::vector<og::sim::LobbyPlayerBinding> bindings =
        server.build_player_bindings();
    ASSERT_EQ(5u, bindings.size());
    EXPECT_EQ(11u, bindings[0].peer_id);
    EXPECT_EQ(0u, bindings[0].local_slot);
    EXPECT_EQ(11u, bindings[1].peer_id);
    EXPECT_EQ(1u, bindings[1].local_slot);
    EXPECT_EQ(22u, bindings[2].peer_id);
    EXPECT_EQ(0u, bindings[2].local_slot);
    EXPECT_EQ(22u, bindings[3].peer_id);
    EXPECT_EQ(1u, bindings[3].local_slot);
    EXPECT_EQ(22u, bindings[4].peer_id);
    EXPECT_EQ(2u, bindings[4].local_slot);
    for (std::size_t index = 0; index < bindings.size(); ++index)
        EXPECT_EQ(index, bindings[index].player_index);
}

TEST(LobbyServer, multi_seat_join_enforces_within_peer_distinct_teams)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);

    // Both seats request team 0: seat 1 must be bumped to a distinct team
    // even though allied settings share teams across peers.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 0}));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(2u, state.players.size());
    EXPECT_EQ(0, state.players[0].team);
    EXPECT_NE(state.players[0].team, state.players[1].team);
    EXPECT_GE(state.players[1].team, 0);
    EXPECT_LT(state.players[1].team, MAX_PLAYERS);
    // Character slots are re-stamped with the resolved seat team.
    ASSERT_EQ(1u, state.players[1].character_slots.size());
    EXPECT_EQ(state.players[1].team,
              state.players[1].character_slots[0].character.teamnum);
}

TEST(LobbyServer, non_allied_lobby_truncates_seats_beyond_the_team_range)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    og::sim::LobbySettings classic_settings;
    classic_settings.allied_mode = 0;
    transport.queue_lobby_message(
        11u, make_settings_change_message(classic_settings));
    server.poll_incoming_messages();

    // Host takes three of the four exclusive teams.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1, 2}, 0u, 100));
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());

    // The guest asks for two seats but only one distinct team remains: the
    // join is truncated to one seat (the rules-driven 4-seat PVP cap).
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {3, 3}, 3u, 200));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(4u, state.players.size());
    EXPECT_EQ("Guest", state.players[3].name);
    EXPECT_EQ(3, state.players[3].team);
}

TEST(LobbyServer, global_seat_cap_truncates_and_rejects_joins)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    for (og::sim::PeerId peer_id = 1u; peer_id <= 6u; ++peer_id)
        server.connect_client(peer_id);

    // Four peers with four seats each fill kMaxGlobalPlayers = 16 minus one:
    // peers 1..3 take 12 seats, peer 4 takes 3 -> 15 seats used.
    for (og::sim::PeerId peer_id = 1u; peer_id <= 3u; ++peer_id)
    {
        transport.queue_lobby_message(
            peer_id,
            make_multi_seat_join_message("Peer", {0, 1, 2, 3}, 0u,
                                         static_cast<std::int32_t>(peer_id) * 100));
        server.poll_incoming_messages();
    }
    transport.queue_lobby_message(
        4u, make_multi_seat_join_message("Late", {0, 1, 2}, 0u, 400));
    server.poll_incoming_messages();
    ASSERT_EQ(15u, server.state().players.size());

    // A four-seat join with one seat of capacity left is truncated to one.
    transport.queue_lobby_message(
        5u, make_multi_seat_join_message("Squeeze", {0, 1, 2, 3}, 0u, 500));
    server.poll_incoming_messages();
    ASSERT_EQ(16u, server.state().players.size());
    EXPECT_EQ("Squeeze", server.state().players[15].name);
    EXPECT_EQ(15u, server.state().players[15].player_index);

    // With the lobby at the global cap a further join is rejected outright
    // (the requester only receives the authoritative state echo).
    transport.clear_sent_messages();
    transport.queue_lobby_message(
        6u, make_multi_seat_join_message("Overflow", {0}, 0u, 600));
    server.poll_incoming_messages();
    ASSERT_EQ(16u, server.state().players.size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(6u, transport.sent_messages()[0].peer_id);
}

TEST(LobbyServer, ready_applies_to_every_seat_of_the_sender)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);

    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1, 2}));
    server.poll_incoming_messages();

    og::sim::LobbyMessage ready_message;
    ready_message.payload = og::sim::LobbyReadyMessage{
        .player_index = 0u,
        .ready = true,
    };
    transport.queue_lobby_message(11u, ready_message);
    server.poll_incoming_messages();

    ASSERT_EQ(3u, server.state().players.size());
    for (const og::sim::LobbyPlayer& player : server.state().players)
        EXPECT_TRUE(player.ready) << "seat " << int(player.player_index);
}

TEST(LobbyServer, team_change_retargets_the_matching_seat)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);

    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1}));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());

    // Target the SECOND seat by its global player index.
    og::sim::LobbyMessage team_change;
    team_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 1u,
        .team = 3,
    };
    transport.queue_lobby_message(11u, team_change);
    server.poll_incoming_messages();

    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);

    // A team change targeting an unknown player index falls back to seat 0.
    og::sim::LobbyMessage fallback_change;
    fallback_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 0xaau,
        .team = 2,
    };
    transport.queue_lobby_message(11u, fallback_change);
    server.poll_incoming_messages();

    EXPECT_EQ(2, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);

    // Within-peer distinctness holds for team changes: moving seat 0 onto
    // seat 1's team is refused (state unchanged, echo sent).
    transport.clear_sent_messages();
    og::sim::LobbyMessage collide_change;
    collide_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 0u,
        .team = 3,
    };
    transport.queue_lobby_message(11u, collide_change);
    server.poll_incoming_messages();

    EXPECT_EQ(2, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(11u, transport.sent_messages()[0].peer_id);
}

TEST(LobbyServer, rejoin_replaces_the_whole_seat_list)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1, 2, 3}, 0u, 100));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {0}, 4u, 200));
    server.poll_incoming_messages();
    ASSERT_EQ(5u, server.state().players.size());

    // set_player_mode 4 -> 2 re-sends the join with two seats: the seat list
    // shrinks and every global index is re-densified.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1}, 0u, 100));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(3u, state.players.size());
    EXPECT_EQ("Host", state.players[0].name);
    EXPECT_EQ("Host#1", state.players[1].name);
    EXPECT_EQ("Guest", state.players[2].name);
    for (std::size_t index = 0; index < state.players.size(); ++index)
        EXPECT_EQ(index, state.players[index].player_index);
}

TEST(LobbyServer, save_data_equivalent_tags_owner_per_seat)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1}, 0u, 100));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {2}, 2u, 200));
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(3u, equivalent.team_list.size());
    // Each seat is a full LobbyPlayer, so owner tags stamp per SEAT: seat 1's
    // character belongs to global player 1, not to "the host machine".
    EXPECT_EQ(0u, equivalent.team_list[0].owner_player_index);
    EXPECT_EQ(1u, equivalent.team_list[1].owner_player_index);
    EXPECT_EQ(2u, equivalent.team_list[2].owner_player_index);
    EXPECT_EQ(0u, equivalent.team_list[0].owner_save_slot);
    EXPECT_EQ(1u, equivalent.team_list[1].owner_save_slot);
    EXPECT_EQ(2u, equivalent.team_list[2].owner_save_slot);
    // Allied gameplay folds every seat onto team 0.
    for (const auto& slot : equivalent.team_list)
        EXPECT_EQ(0, slot.character.teamnum);
}
