#include <openglad/core/constants.h>
#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/lobby_server.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
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
        // Record HOW MANY messages had already been handed to the transport
        // when this peer was dropped. A kick notice is only useful if it was
        // queued before the disconnect, and the two live in different logs,
        // so the send count is the ordering witness.
        disconnect_marks_.emplace_back(peer_id, sent_messages_.size());
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

    // Messages already queued when peer_id was disconnected, or nullopt when
    // it never was.
    [[nodiscard]] std::optional<std::size_t> sent_count_at_disconnect(
        og::sim::PeerId peer_id) const noexcept
    {
        for (const auto& [dropped, count] : disconnect_marks_)
        {
            if (dropped == peer_id)
                return count;
        }
        return std::nullopt;
    }

private:
    bool typed_messages_ = false;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
    std::vector<og::sim::ReceivedMessage> received_raw_;
    std::vector<og::sim::TypedReceivedMessage> received_typed_;
    std::vector<og::sim::PeerId> connected_peers_;
    std::vector<og::sim::PeerId> disconnected_peers_;
    std::vector<std::pair<og::sim::PeerId, std::size_t>> disconnect_marks_;
};

og::sim::LobbyCharacterSlot make_slot(std::uint8_t slot_index,
                                      std::int32_t guy_id,
                                      const char* name,
                                      std::int16_t family,
                                      std::int16_t team = 0)
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
    character.teamnum = team;

    return {
        .slot_index = slot_index,
        .character = character,
    };
}

og::sim::LobbyMessage make_join_message(
    const char* name,
    std::int16_t requested_team,
    std::vector<og::sim::LobbyCharacterSlot> slots,
    bool ready = false,
    std::uint32_t request_id = 0)
{
    og::sim::LobbyPlayer player;
    player.name = name;
    player.team = requested_team;
    player.character_slots = std::move(slots);
    player.ready = ready;

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{
        .player = std::move(player),
        .request_id = request_id,
    };
    return message;
}

std::vector<og::sim::LobbyCharacterSlot> make_slots(std::uint8_t first_slot_index,
                                                    std::size_t count,
                                                    std::int32_t first_guy_id,
                                                    std::int16_t family)
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
    {
        og::sim::LobbyState actual = decode_lobby_state(message);
        // Ownership and Join acknowledgement are deliberately recipient-
        // specific; compare the canonical replicated content here and cover
        // those personalized fields explicitly below.
        actual.local_seat_ids.clear();
        actual.last_join_request_id = 0;
        actual.local_peer_is_host = false;
        og::sim::LobbyState canonical_expected = expected;
        canonical_expected.local_seat_ids.clear();
        canonical_expected.last_join_request_id = 0;
        canonical_expected.local_peer_is_host = false;
        EXPECT_EQ(canonical_expected, actual);
    }
}

} // namespace

TEST(LobbyServer, poll_registers_connected_transport_peers)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);

    transport.set_connected_peers({11u});
    server.poll_incoming_messages();

    // v10 handshake: the initial LobbyState is followed by the class-pack
    // manifest announcement (the explicit empty manifest when no packs are
    // hosted).
    ASSERT_EQ(2u, transport.sent_messages().size());
    EXPECT_EQ(11u, transport.sent_messages().front().peer_id);
    og::sim::LobbyState echoed =
        decode_lobby_state(transport.sent_messages().front());
    echoed.local_seat_ids.clear();
    echoed.local_peer_is_host = false;
    EXPECT_EQ(server.state(), echoed);

    EXPECT_EQ(11u, transport.sent_messages()[1].peer_id);
    const std::optional<og::sim::PackManifestMessage> manifest =
        og::sim::deserialize_pack_manifest_message(
            transport.sent_messages()[1].data);
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(0u, manifest->pack_count);
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
    // §4.3: the ready bit a client sends inside a Join is IGNORED — a first
    // join has no matching stored content, so ready lands false regardless of
    // the requested flag. Readiness is negotiated only via LobbyReadyMessage.
    EXPECT_FALSE(state.players[1].ready);
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
    // The legacy allied setting still round-trips, but explicit shared
    // assignments survive a switch to classic settings.
    EXPECT_EQ(0, state.players[0].team);
    EXPECT_EQ(0, state.players[1].team);
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
    EXPECT_EQ(0, equivalent.team_list[2].character.teamnum)
        << "the guest seat team must not repaint its red fighter";
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
    wild.campaign_id = "ctf";
    wild.scenario_id = 500;
    wild.difficulty = 1;
    wild.allied_mode = 1;
    wild.ctf_team_count = 9;       // retired (A3) -> 0, always
    wild.ctf_capture_limit = 99;   // -> 50
    wild.ctf_respawn_ticks = 5;    // nonzero -> raised to 12
    wild.time_limit = 30;          // nonzero -> raised to 720 (#241)
    og::sim::LobbyMessage wild_message;
    wild_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = wild,
    };
    transport.queue_lobby_message(11u, wild_message);
    server.poll_incoming_messages();

    og::sim::LobbyState state = server.state();
    EXPECT_EQ(0, state.settings.ctf_team_count)
        << "the retired TEAMS knob heals to Auto whatever it is sent (A3)";
    EXPECT_EQ(50, state.settings.ctf_capture_limit);
    EXPECT_EQ(12, state.settings.ctf_respawn_ticks);
    EXPECT_EQ(720, state.settings.time_limit);

    // A time limit past the ceiling clamps down rather than reverting.
    og::sim::LobbySettings too_long = wild;
    too_long.time_limit = 32000;
    og::sim::LobbyMessage too_long_message;
    too_long_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = too_long,
    };
    transport.queue_lobby_message(11u, too_long_message);
    server.poll_incoming_messages();
    EXPECT_EQ(21600, server.state().settings.time_limit);

    // In-range values (and the 0 = map/default sentinels) pass through.
    og::sim::LobbySettings sane = wild;
    sane.ctf_team_count = 3;  // a v18 save's legacy value
    sane.ctf_capture_limit = 0;
    sane.ctf_respawn_ticks = 0;
    sane.time_limit = 0; // the map's own value survives the sanitizer
    og::sim::LobbyMessage sane_message;
    sane_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = sane,
    };
    transport.queue_lobby_message(11u, sane_message);
    server.poll_incoming_messages();

    state = server.state();
    EXPECT_EQ(0, state.settings.ctf_team_count)
        << "a legacy 2/3/4 heals to Auto on first sanitize — the one-time "
           "documented migration";
    EXPECT_EQ(0, state.settings.ctf_capture_limit);
    EXPECT_EQ(0, state.settings.ctf_respawn_ticks);
    EXPECT_EQ(0, state.settings.time_limit);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    EXPECT_EQ(0, equivalent.ctf_team_count)
        << "and the healed value is what the launch equivalent carries";
    EXPECT_EQ(0, equivalent.ctf_capture_limit);
    EXPECT_EQ(0, equivalent.ctf_respawn_ticks);
    EXPECT_EQ(0, equivalent.time_limit);

    // And a live limit carries into the launch equivalent (the dropped-field
    // bug class: build_save_data_equivalent is a hand-written field list).
    og::sim::LobbySettings timed = sane;
    timed.time_limit = 7200;
    og::sim::LobbyMessage timed_message;
    timed_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = timed,
    };
    transport.queue_lobby_message(11u, timed_message);
    server.poll_incoming_messages();
    EXPECT_EQ(7200, server.state().settings.time_limit);
    EXPECT_EQ(7200, server.build_save_data_equivalent().time_limit);
}

TEST(LobbyServer, sanitize_admits_troops_matched_and_equivalent_carries_it)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    // The whole TEAMS field is retired (A3): the old Teams: Match sentinel
    // 5, a legacy 2/3/4 and junk all heal to Auto, and the band's BOTS: OFF
    // is where dropping a team lives now.
    og::sim::LobbySettings retired = server.state().settings;
    retired.ctf_team_count = 5;
    og::sim::LobbyMessage retired_message;
    retired_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = retired,
    };
    transport.queue_lobby_message(11u, retired_message);
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.ctf_team_count)
        << "every value of the retired knob is Auto";

    // Amendment B5: TROOPS retired exactly like TEAMS above. Every value a
    // peer, an old .gtl or a crafted client can put in the field — the
    // sentinel 3, plain OWN 2, the retired middle state 1, junk past the
    // top — heals to 0 on the way through the authority, once, silently.
    for (const std::int16_t legacy_value : {std::int16_t{1}, std::int16_t{2},
                                            og::sim::kTroopsMatched,
                                            std::int16_t{4}})
    {
        og::sim::LobbySettings troops = server.state().settings;
        troops.ctf_strip_scenario_troops = legacy_value;
        og::sim::LobbyMessage troops_message;
        troops_message.payload = og::sim::LobbySettingsChangeMessage{
            .player_index = 0u,
            .settings = troops,
        };
        transport.queue_lobby_message(11u, troops_message);
        server.poll_incoming_messages();
        EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops)
            << "legacy troops value " << legacy_value;
    }

    // The game-start save-data equivalent carries both healed knobs.
    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    EXPECT_EQ(0, equivalent.ctf_strip_scenario_troops)
        << "the healed troops value rides";
    EXPECT_EQ(0, equivalent.ctf_team_count) << "the healed team count rides";
}

TEST(LobbyServer, non_host_matched_settings_change_is_dropped)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 1,
                          {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    // A guest's SettingsChange carrying the TROOPS: FAIR sentinel is
    // silently dropped like any other non-host settings message (D19: the
    // gating shape is unchanged — guests cycle cosmetically, the host echo
    // wins).
    transport.clear_sent_messages();
    og::sim::LobbySettings settings = server.state().settings;
    ASSERT_EQ(0, settings.ctf_strip_scenario_troops);
    settings.ctf_strip_scenario_troops = og::sim::kTroopsMatched;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 1u,
        .settings = settings,
    };
    transport.queue_lobby_message(22u, settings_message);
    server.poll_incoming_messages();

    EXPECT_TRUE(transport.sent_messages().empty());
    EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops);
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
    enemy_settings.campaign_id = "gladiator";
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
        .seat_id = server.state().players[1].seat_id,
        .team = 2,
    };
    transport.queue_lobby_message(22u, team_change);
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(2, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(0, server.state().players[1].character_slots[0].character.teamnum)
        << "changing a seat never changes combat allegiance";
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(2u, equivalent.team_list.size());
    EXPECT_EQ(0, equivalent.team_list[1].character.teamnum);

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

TEST(LobbyServer, fifth_classic_join_may_share_an_existing_team)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    for (og::sim::PeerId peer_id = 1u; peer_id <= 5u; ++peer_id)
        server.connect_client(peer_id);

    // Explicit assignments supersede the old classic-team exclusivity rule:
    // a fifth seat may reuse one of the four colors. The independent global
    // kMaxGlobalPlayers cap is covered by the multi-seat tests below.
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

    ASSERT_EQ(5u, server.state().players.size());
    EXPECT_EQ(3, server.state().players[3].team);
    EXPECT_EQ(3, server.state().players[4].team);
    ASSERT_EQ(5u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());
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

TEST(LobbyServer,
     combined_roster_canonicalizes_colliding_and_invalid_private_guy_ids)
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
            {make_slot(0u, 6, "Host Six", FAMILY_SOLDIER),
             make_slot(2u, 1, "Host One", FAMILY_ARCHER)}));
    transport.queue_lobby_message(
        22u,
        make_join_message(
            "Joiner",
            1,
            {make_slot(0u, 6, "Join Six", FAMILY_MAGE),
             make_slot(1u, -1, "JoinInvalid", FAMILY_CLERIC)}));
    server.poll_incoming_messages();

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(4u, equivalent.team_list.size());

    std::set<std::int32_t> assembled_ids;
    for (const og::sim::LobbyCharacterSlot& slot : equivalent.team_list)
    {
        EXPECT_GE(slot.character.guy_id, 0) << slot.character.name;
        EXPECT_TRUE(assembled_ids.insert(slot.character.guy_id).second)
            << slot.character.name;
    }
    EXPECT_EQ(4u, assembled_ids.size());

    const auto id_for = [&](std::string_view name) {
        const auto found = std::find_if(
            equivalent.team_list.begin(),
            equivalent.team_list.end(),
            [name](const og::sim::LobbyCharacterSlot& slot) {
                return slot.character.name == name;
            });
        EXPECT_NE(equivalent.team_list.end(), found);
        return found != equivalent.team_list.end()
            ? found->character.guy_id
            : std::int32_t{-1};
    };

    EXPECT_EQ(6, id_for("Host Six"))
        << "the first valid occurrence keeps its private id";
    EXPECT_EQ(1, id_for("Host One"))
        << "an already-unique valid id must never be stolen by a remap";
    EXPECT_NE(6, id_for("Join Six"));
    EXPECT_GE(id_for("JoinInvalid"), 0);
}

// §4.2 [NET-F2]: a join whose deployed total would exceed the 24-slot match
// capacity keeps its FULL roster for display but the overflow slots are
// force-BENCHED (deployed cleared) in slot order in the stored seats — the
// v7 hard truncation is gone. The assembly equivalent then materializes
// exactly the 24 deployed slots.
TEST(LobbyServer, join_overflow_force_benches_instead_of_truncating)
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
    enemy_settings.campaign_id = "gladiator";
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
    // The guest's whole 10-slot roster replicates for display; only the 4
    // that fit the 24-cap stay deployed, the tail 6 are force-benched.
    ASSERT_EQ(10u, server.state().players[1].character_slots.size());
    EXPECT_EQ(20u, server.state().players[1].character_slots[0].slot_index);
    EXPECT_EQ(29u, server.state().players[1].character_slots[9].slot_index);
    for (std::size_t slot = 0; slot < 10u; ++slot)
    {
        EXPECT_EQ(slot < 4u,
                  server.state().players[1].character_slots[slot].deployed)
            << "guest slot " << slot;
    }
    // Owner-only benching: the host's own flags are untouched by the guest's
    // overflow join (a join only replaces the SENDER's seats).
    for (const og::sim::LobbyCharacterSlot& slot :
         server.state().players[0].character_slots)
    {
        EXPECT_TRUE(slot.deployed);
    }

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(24u, equivalent.team_list.size());
    EXPECT_EQ(0u, equivalent.team_list.front().slot_index);
    EXPECT_EQ(23u, equivalent.team_list.back().slot_index);
    EXPECT_EQ(0, equivalent.team_list.back().character.teamnum);
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

    // §4.3 ready gate: the non-host guest must be ready before the host may
    // start. Without this the host's StartGame is denied (MachinesNotReady).
    og::sim::LobbyMessage guest_ready;
    guest_ready.payload = og::sim::LobbyReadyMessage{
        .player_index = 1u,
        .ready = true,
    };
    transport.queue_lobby_message(22u, guest_ready);
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[1].ready);

    transport.clear_sent_messages();
    og::sim::LobbyMessage non_host_start;
    non_host_start.payload = og::sim::LobbyStartGameMessage{
        .player_index = 1u,
        .request_id = 90u,
    };
    transport.queue_lobby_message(22u, non_host_start);
    server.poll_incoming_messages();

    EXPECT_FALSE(server.start_game_requested());
    EXPECT_TRUE(transport.sent_messages().empty());

    og::sim::LobbyMessage host_start;
    host_start.payload = og::sim::LobbyStartGameMessage{
        .player_index = 0u,
        .request_id = 91u,
    };
    transport.queue_lobby_message(11u, host_start);
    server.poll_incoming_messages();

    EXPECT_TRUE(server.start_game_requested());
    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const auto& sent_message : transport.sent_messages())
    {
        const og::sim::LobbyMessage decoded = decode_lobby_message(sent_message);
        EXPECT_EQ(og::sim::LobbyMessageKind::StartGame, decoded.kind());
        const auto& confirmation =
            std::get<og::sim::LobbyStartGameMessage>(decoded.payload);
        EXPECT_EQ(0u, confirmation.player_index);
        EXPECT_EQ(91u, confirmation.request_id);
    }
    EXPECT_TRUE(server.consume_start_game_requested());
    EXPECT_FALSE(server.consume_start_game_requested());
    EXPECT_TRUE(server.start_game_requested());

    transport.clear_sent_messages();
    og::sim::LobbyMessage team_change;
    team_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 1u,
        .seat_id = server.state().players[1].seat_id,
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
    enemy_settings.campaign_id = "gladiator";
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

TEST(LobbyServer, allied_mode_preserves_explicit_binding_and_combat_teams)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 2,
                          {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 2)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 3,
                          {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER, 3)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings allied_settings;
    allied_settings.campaign_id = "gladiator";
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
    EXPECT_EQ(2, equivalent.team_list[0].character.teamnum);
    EXPECT_EQ(3, equivalent.team_list[1].character.teamnum);

    const std::vector<og::sim::LobbyPlayerBinding> bindings =
        server.build_player_bindings();
    ASSERT_EQ(2u, bindings.size());
    EXPECT_EQ(2, bindings[0].team);
    EXPECT_EQ(3, bindings[1].team);
}

TEST(LobbyServer, local_game_start_preserves_every_roster_team_in_both_modes)
{
    for (const std::int16_t allied_mode : {std::int16_t{0}, std::int16_t{1}})
    {
        for (std::int16_t team = 0; team < 4; ++team)
        {
            MockLobbyTransport transport(true);
            og::sim::LobbyServer server(transport, /*local_session=*/true);
            server.connect_client(11u);
            transport.clear_sent_messages();

            transport.queue_lobby_message(
                11u,
                make_join_message(
                    "Local", team,
                    {make_slot(0u, 100, "Local Guy", FAMILY_SOLDIER, team)}));
            server.poll_incoming_messages();

            og::sim::LobbySettings settings;
            settings.campaign_id = "gladiator";
            settings.scenario_id = 1;
            settings.difficulty = 1;
            settings.allied_mode = allied_mode;
            og::sim::LobbyMessage settings_message;
            settings_message.payload = og::sim::LobbySettingsChangeMessage{
                .player_index = 0u,
                .settings = std::move(settings),
            };
            transport.queue_lobby_message(11u, settings_message);
            server.poll_incoming_messages();

            const og::sim::LobbySaveDataEquivalent equivalent =
                server.build_save_data_equivalent();
            ASSERT_EQ(1u, equivalent.team_list.size());
            EXPECT_EQ(team, equivalent.team_list[0].character.teamnum)
                << "allied=" << allied_mode << " team=" << team;

            const std::vector<og::sim::LobbyPlayerBinding> bindings =
                server.build_player_bindings();
            ASSERT_EQ(1u, bindings.size());
            EXPECT_EQ(team, bindings[0].team)
                << "allied=" << allied_mode << " team=" << team;
        }
    }
}

TEST(LobbyServer, local_legacy_together_field_does_not_collapse_explicit_seats)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport, /*local_session=*/true);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message("Local P1", 0,
                          {make_slot(0u, 100, "Red One", FAMILY_SOLDIER, 0),
                           make_slot(2u, 300, "Red Two", FAMILY_CLERIC, 0)}));
    transport.queue_lobby_message(
        22u,
        make_join_message("Local P2", 1,
                          {make_slot(1u, 200, "Yellow", FAMILY_ARCHER, 1)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings settings;
    settings.campaign_id = "gladiator";
    settings.scenario_id = 1;
    settings.difficulty = 1;
    settings.allied_mode = 1;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = std::move(settings),
    };
    transport.queue_lobby_message(11u, settings_message);
    server.poll_incoming_messages();

    const auto equivalent = server.build_save_data_equivalent();
    ASSERT_EQ(3u, equivalent.team_list.size());
    EXPECT_EQ(0, equivalent.team_list[0].character.teamnum);
    EXPECT_EQ(1, equivalent.team_list[1].character.teamnum);
    EXPECT_EQ(0, equivalent.team_list[2].character.teamnum);

    const auto bindings = server.build_player_bindings();
    ASSERT_EQ(2u, bindings.size());
    EXPECT_EQ(0, bindings[0].team);
    EXPECT_EQ(1, bindings[1].team)
        << "the legacy Together field no longer overrides an explicit seat";
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

og::sim::LobbySettings make_ctf_lobby_settings(
    std::int16_t team_count = 0,
    std::uint8_t authored_team_mask = 0)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = "ctf";
    settings.scenario_id = 1;
    settings.difficulty = 1;
    settings.allied_mode = 0;
    settings.ctf_team_count = team_count;
    settings.ctf_authored_team_mask = authored_team_mask;
    // Protocol v12: the versus/shared-teams rule rides this flag (the host
    // derives it from the campaign's matchup: yaml key).
    settings.shared_teams = 1;
    return settings;
}

og::sim::LobbyMessage make_team_change_message(
    const og::sim::LobbyPlayer& player,
    std::int16_t team)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = player.player_index,
        .seat_id = player.seat_id,
        .team = team,
    };
    return message;
}

} // namespace

TEST(LobbyState, ctf_team_domain_uses_authored_order_and_safe_fallback)
{
    og::sim::LobbySettings settings = make_ctf_lobby_settings(2);

    // No published map metadata preserves the old explicit numeric clamp.
    EXPECT_EQ(0b0011u, og::sim::lobby_effective_team_mask(settings));

    // Sparse authored teams are truncated by set-bit order, matching CTF
    // gameplay's first-N-authored activation.
    settings.ctf_authored_team_mask = 0b1101u;
    EXPECT_EQ(0b0101u, og::sim::lobby_effective_team_mask(settings));
    EXPECT_TRUE(og::sim::lobby_team_is_selectable(settings, 2));
    EXPECT_FALSE(og::sim::lobby_team_is_selectable(settings, 1));

    settings.ctf_team_count = 0;
    EXPECT_EQ(0b1101u, og::sim::lobby_effective_team_mask(settings));
}

// Amendment B8: NOTHING the band can hold deactivates a team any more, so
// the seat domain is the authored mask and nothing else — the rule that
// stood before A2 added the OFF clause, restored when OFF went away.
TEST(LobbyState, band_knobs_never_narrow_the_seat_domain)
{
    og::sim::LobbySettings settings = make_ctf_lobby_settings();
    settings.ctf_authored_team_mask = 0b1111u;
    ASSERT_EQ(0b1111u, og::sim::lobby_effective_team_mask(settings));

    // Every FILL value, on every team, including the strongest and NONE.
    for (std::int16_t value = og::sim::kFillFair;
         value <= og::sim::kMaxFill; ++value)
    {
        settings.fill.fill(value);
        EXPECT_EQ(0b1111u, og::sim::lobby_effective_team_mask(settings))
            << "fill " << value;
        for (std::int16_t team = 0; team < SCORE_TEAM_COUNT; ++team)
            EXPECT_TRUE(og::sim::lobby_team_is_selectable(settings, team));
    }
    settings.fill.fill(og::sim::kFillNone);
    EXPECT_EQ(0, og::sim::lobby_first_selectable_team(settings));

    // MAP UNITS: OFF on every team says nothing about seats either.
    settings.map_units.fill(og::sim::kMapUnitsOff);
    EXPECT_EQ(0b1111u, og::sim::lobby_effective_team_mask(settings));

    // A sparse authored mask still narrows it — that is the map's own word.
    settings.ctf_authored_team_mask = 0b1101u;
    EXPECT_EQ(0b1101u, og::sim::lobby_effective_team_mask(settings));
    EXPECT_FALSE(og::sim::lobby_team_is_selectable(settings, 1));
}

TEST(LobbyServer, sanitize_heals_every_retired_troops_value_to_zero)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    // Default is 0, and amendment B5 made 0 the ONLY value: the knob is
    // retired, and the authority answers one value for it so an old peer,
    // an old .gtl or a crafted client cannot reintroduce a second rule for
    // whether the map's own cast fights.
    EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops);
    for (const std::int16_t value : {std::int16_t{1}, std::int16_t{2},
                                     og::sim::kTroopsMatched, std::int16_t{4},
                                     std::int16_t{-3}, std::int16_t{30000}})
    {
        og::sim::LobbySettings settings = make_ctf_lobby_settings();
        settings.ctf_strip_scenario_troops = value;
        transport.queue_lobby_message(
            11u, make_settings_change_message(settings));
        server.poll_incoming_messages();
        EXPECT_EQ(0, server.state().settings.ctf_strip_scenario_troops)
            << "troops " << value;
        EXPECT_EQ(
            0, server.build_save_data_equivalent().ctf_strip_scenario_troops)
            << "troops " << value << " into the game-start equivalent";
    }
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
    EXPECT_EQ(0, server.state().settings.cross_control);
    EXPECT_EQ(0, server.state().settings.infinite_gold);

    // In-range values pass through and reach the game-start equivalent.
    og::sim::LobbySettings valid = make_ctf_lobby_settings();
    valid.respawn_mode = 3;
    valid.generator_rate = 200;
    valid.keep_fallen_heroes = 1;
    valid.cross_control = 1;
    valid.infinite_gold = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(valid));
    server.poll_incoming_messages();
    EXPECT_EQ(3, server.state().settings.respawn_mode);
    EXPECT_EQ(200, server.state().settings.generator_rate);
    EXPECT_EQ(1, server.state().settings.keep_fallen_heroes);
    EXPECT_EQ(1, server.state().settings.cross_control);
    EXPECT_EQ(3, server.build_save_data_equivalent().respawn_mode);
    EXPECT_EQ(200, server.build_save_data_equivalent().generator_rate);
    EXPECT_EQ(1, server.build_save_data_equivalent().keep_fallen_heroes);
    EXPECT_EQ(1, server.build_save_data_equivalent().cross_control);
    EXPECT_EQ(1, server.state().settings.infinite_gold);
    EXPECT_EQ(1, server.build_save_data_equivalent().infinite_gold);

    // respawn_mode outside {0,1,2,3} falls back to the current value.
    og::sim::LobbySettings junk_mode = make_ctf_lobby_settings();
    junk_mode.respawn_mode = 4;
    junk_mode.generator_rate = 200;
    junk_mode.keep_fallen_heroes = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(junk_mode));
    server.poll_incoming_messages();
    EXPECT_EQ(3, server.state().settings.respawn_mode);

    og::sim::LobbySettings negative_mode = make_ctf_lobby_settings();
    negative_mode.respawn_mode = -1;
    transport.queue_lobby_message(11u,
                                  make_settings_change_message(negative_mode));
    server.poll_incoming_messages();
    EXPECT_EQ(3, server.state().settings.respawn_mode);

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

    // cross_control is binary-or-fallback (protocol v8). Establish 1 as the
    // accepted value, then a junk value must fall back to it.
    og::sim::LobbySettings cross_on = make_ctf_lobby_settings();
    cross_on.cross_control = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(cross_on));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.cross_control);

    og::sim::LobbySettings junk_cross = make_ctf_lobby_settings();
    junk_cross.cross_control = 5;
    transport.queue_lobby_message(11u, make_settings_change_message(junk_cross));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.cross_control)
        << "out-of-range cross_control falls back to the accepted value";

    og::sim::LobbySettings cross_off = make_ctf_lobby_settings();
    cross_off.cross_control = 0;
    transport.queue_lobby_message(11u, make_settings_change_message(cross_off));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.cross_control);

    // infinite_gold is binary-or-fallback (protocol v11), same shape.
    og::sim::LobbySettings gold_on = make_ctf_lobby_settings();
    gold_on.infinite_gold = 1;
    transport.queue_lobby_message(11u, make_settings_change_message(gold_on));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.infinite_gold);

    og::sim::LobbySettings junk_gold = make_ctf_lobby_settings();
    junk_gold.infinite_gold = 7;
    transport.queue_lobby_message(11u, make_settings_change_message(junk_gold));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.infinite_gold)
        << "out-of-range infinite_gold falls back to the accepted value";
    EXPECT_EQ(1, server.build_save_data_equivalent().infinite_gold);

    og::sim::LobbySettings gold_off = make_ctf_lobby_settings();
    gold_off.infinite_gold = 0;
    transport.queue_lobby_message(11u, make_settings_change_message(gold_off));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.infinite_gold);

    // shared_teams (v12) sanitizes on the same {0, 1} matrix.
    og::sim::LobbySettings junk_shared = make_ctf_lobby_settings();
    junk_shared.shared_teams = 9;
    transport.queue_lobby_message(11u,
                                  make_settings_change_message(junk_shared));
    server.poll_incoming_messages();
    EXPECT_EQ(1, server.state().settings.shared_teams)
        << "out-of-range shared_teams falls back to the accepted value";

    og::sim::LobbySettings shared_off = make_ctf_lobby_settings();
    shared_off.shared_teams = 0;
    transport.queue_lobby_message(11u,
                                  make_settings_change_message(shared_off));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().settings.shared_teams);
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
    transport.queue_lobby_message(
        22u, make_team_change_message(server.state().players[1], 0));
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

TEST(LobbyServer, classic_lobby_allows_shared_explicit_teams)
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

    // Non-allied classic settings no longer imply one seat per color. The
    // guest may explicitly join the host's team.
    og::sim::LobbySettings classic_settings;
    classic_settings.allied_mode = 0;
    transport.queue_lobby_message(
        11u, make_settings_change_message(classic_settings));
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        22u, make_team_change_message(server.state().players[1], 0));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(0, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(0, server.state().players[1].character_slots[0].character.teamnum);
}

// bots_off_clamps_team_choice retired with the OFF value (amendment B8):
// no band knob narrows the seat domain, so there is no clamp left to pin.
// What still clamps a team choice — the AUTHORED mask — is pinned by
// sparse_ctf_authored_domain_matches_gameplay_and_reteams below.

TEST(LobbyServer, sparse_ctf_authored_domain_matches_gameplay_and_reteams)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message(
                 "Host", 0,
                 {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER, 0)}));
    transport.queue_lobby_message(
        22u, make_join_message(
                 "Guest", 3,
                 {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER, 3)}));
    server.poll_incoming_messages();
    ASSERT_EQ(3, server.state().players[1].team);

    // Auto exposes every authored flag team, including sparse ids.
    constexpr std::uint8_t kTeamsZeroTwoThree = 0b1101u;
    transport.queue_lobby_message(
        11u,
        make_settings_change_message(
            make_ctf_lobby_settings(0, kTeamsZeroTwoThree)));
    server.poll_incoming_messages();
    EXPECT_EQ(kTeamsZeroTwoThree,
              server.state().settings.ctf_authored_team_mask);
    EXPECT_EQ(3, server.state().players[1].team);

    // A crafted request for missing authored team 1 is denied, while sparse
    // authored team 2 is accepted.
    transport.queue_lobby_message(
        22u, make_team_change_message(server.state().players[1], 1));
    server.poll_incoming_messages();
    EXPECT_EQ(3, server.state().players[1].team);
    transport.queue_lobby_message(
        22u, make_team_change_message(server.state().players[1], 2));
    server.poll_incoming_messages();
    EXPECT_EQ(2, server.state().players[1].team);

    // The AUTHORED mask is the only thing that narrows this domain now
    // (amendment B8 retired the band's OFF value, which was the other
    // narrowing rule): a level that stops authoring team 2 re-resolves the
    // seat sitting there to the first fielded team, and a request to go
    // back is denied like any unauthored colour.
    constexpr std::uint8_t kTeamsZeroThree = 0b1001u;
    transport.queue_lobby_message(
        11u,
        make_settings_change_message(
            make_ctf_lobby_settings(0, kTeamsZeroThree)));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().players[1].team);
    transport.queue_lobby_message(
        22u, make_team_change_message(server.state().players[1], 2));
    server.poll_incoming_messages();
    EXPECT_EQ(0, server.state().players[1].team);

    // A new level's authored domain invalidates the old assignment. The
    // settings transition deterministically moves it to the first active
    // authored team without repainting its character's combat allegiance.
    constexpr std::uint8_t kTeamsOneThree = 0b1010u;
    transport.queue_lobby_message(
        11u,
        make_settings_change_message(
            make_ctf_lobby_settings(0, kTeamsOneThree)));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(1, server.state().players[0].team);
    EXPECT_EQ(1, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(3,
              server.state().players[1].character_slots[0].character.teamnum);
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

    // A level whose authored mask has no team 3 strands the guest there:
    // the server re-resolves the seat into the fielded domain without
    // repainting the fighter.
    transport.clear_sent_messages();
    transport.queue_lobby_message(
        11u, make_settings_change_message(
                 make_ctf_lobby_settings(0, 0b0111u)));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_GE(server.state().players[1].team, 0);
    EXPECT_NE(3, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(0, server.state().players[1].character_slots[0].character.teamnum);
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());
}

TEST(LobbyServer, settings_change_to_classic_preserves_shared_assignments)
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
    transport.queue_lobby_message(
        22u, make_team_change_message(server.state().players[1], 0));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    ASSERT_EQ(0, server.state().players[0].team);
    ASSERT_EQ(0, server.state().players[1].team);

    // Host switches the campaign back to classic. Explicit assignments are
    // mode-independent, so both seats remain on team 0 (with fighter colors
    // unchanged); the settings change itself still broadcasts.
    og::sim::LobbySettings classic = make_ctf_lobby_settings();
    classic.campaign_id = "gladiator";
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_settings_change_message(classic));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(0, server.state().players[1].team);
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ(0, server.state().players[1].character_slots[0].character.teamnum);
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
    std::int32_t first_guy_id = 100,
    og::sim::LobbyMachineId client_authored_machine_id =
        og::sim::kInvalidLobbyMachineId)
{
    og::sim::LobbyJoinMessage join;
    for (std::size_t seat = 0; seat < seat_teams.size(); ++seat)
    {
        og::sim::LobbyPlayer player;
        player.name = seat == 0
            ? std::string(name)
            : std::string(name) + "#" + std::to_string(seat);
        player.machine_id = client_authored_machine_id;
        player.team = seat_teams[seat];
        player.character_slots = {make_slot(
            static_cast<std::uint8_t>(first_slot_index + seat),
            first_guy_id + static_cast<std::int32_t>(seat),
            "Guy",
            FAMILY_SOLDIER,
            seat_teams[seat])};
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
        11u, make_multi_seat_join_message(
                 "Host", {0, 1}, 0u, 100, 0xdeadbeefu));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message(
                 "Guest", {0, 1, 2}, 2u, 200, 0xdeadbeefu));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(5u, state.players.size());
    // Flatten order: (connection order, seat order); indices sequential.
    for (std::size_t index = 0; index < state.players.size(); ++index)
    {
        EXPECT_EQ(index, state.players[index].player_index);
        EXPECT_NE(og::sim::kInvalidLobbySeatId, state.players[index].seat_id);
    }
    std::set<og::sim::LobbySeatId> seat_ids;
    for (const og::sim::LobbyPlayer& player : state.players)
        seat_ids.insert(player.seat_id);
    EXPECT_EQ(state.players.size(), seat_ids.size());
    EXPECT_NE(og::sim::kInvalidLobbyMachineId, state.players[0].machine_id);
    EXPECT_EQ(state.players[0].machine_id, state.players[1].machine_id);
    EXPECT_NE(og::sim::kInvalidLobbyMachineId, state.players[2].machine_id);
    EXPECT_EQ(state.players[2].machine_id, state.players[3].machine_id);
    EXPECT_EQ(state.players[2].machine_id, state.players[4].machine_id);
    EXPECT_NE(state.players[0].machine_id, state.players[2].machine_id);
    EXPECT_NE(0xdeadbeefu, state.players[0].machine_id);
    EXPECT_NE(0xdeadbeefu, state.players[2].machine_id)
        << "the server must ignore a client-authored machine ID";
    EXPECT_EQ("Host", state.players[0].name);
    EXPECT_EQ("Host#1", state.players[1].name);
    EXPECT_EQ("Guest", state.players[2].name);
    EXPECT_EQ("Guest#2", state.players[4].name);
    // is_host marks only the host peer's seat 0.
    EXPECT_TRUE(state.players[0].is_host);
    EXPECT_FALSE(state.players[1].is_host);
    EXPECT_FALSE(state.players[2].is_host);
    EXPECT_EQ(0u, state.host_player_id);
    // Explicit assignments are preserved exactly across and within peers.
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
    const std::array<std::int16_t, 5> expected_teams = {0, 1, 0, 1, 2};
    for (std::size_t index = 0; index < bindings.size(); ++index)
    {
        EXPECT_EQ(index, bindings[index].player_index);
        EXPECT_EQ(expected_teams[index], bindings[index].team);
    }
}

TEST(LobbyServer, multi_seat_join_allows_within_peer_duplicate_teams)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);

    // Both seats request team 0: the duplicate is intentional and preserved.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 0}));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(2u, state.players.size());
    EXPECT_EQ(0, state.players[0].team);
    EXPECT_EQ(0, state.players[1].team);
    // Preserving a duplicate seat must not repaint its fighter.
    ASSERT_EQ(1u, state.players[1].character_slots.size());
    EXPECT_EQ(0, state.players[1].character_slots[0].character.teamnum);
}

TEST(LobbyServer, non_allied_lobby_keeps_duplicate_seats_beyond_four_players)
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

    // Host declares three seats.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1, 2}, 0u, 100));
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());

    // The guest asks for two seats on the same remaining color. Both survive;
    // only the independent 16-seat global cap can truncate the declaration.
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {3, 3}, 3u, 200));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(5u, state.players.size());
    EXPECT_EQ("Guest", state.players[3].name);
    EXPECT_EQ(3, state.players[3].team);
    EXPECT_EQ("Guest#1", state.players[4].name);
    EXPECT_EQ(3, state.players[4].team);
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
    // (the requester only receives the authoritative, correlated state echo).
    transport.clear_sent_messages();
    og::sim::LobbyMessage overflow =
        make_multi_seat_join_message("Overflow", {0}, 0u, 600);
    std::get<og::sim::LobbyJoinMessage>(overflow.payload).request_id = 404u;
    transport.queue_lobby_message(6u, overflow);
    server.poll_incoming_messages();
    ASSERT_EQ(16u, server.state().players.size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(6u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(
        404u,
        decode_lobby_state(transport.sent_messages()[0])
            .last_join_request_id);
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

TEST(LobbyServer,
     remove_seat_erases_the_exact_owned_middle_seat_and_clears_ready)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1, 2}));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {3}));
    server.poll_incoming_messages();
    ASSERT_EQ(4u, server.state().players.size());

    const og::sim::LobbySeatId first_id =
        server.state().players[0].seat_id;
    const og::sim::LobbySeatId removed_id =
        server.state().players[1].seat_id;
    const og::sim::LobbySeatId third_id =
        server.state().players[2].seat_id;
    const og::sim::LobbySeatId guest_id =
        server.state().players[3].seat_id;

    og::sim::LobbyMessage ready;
    ready.payload = og::sim::LobbyReadyMessage{
        .player_index = 0u,
        .ready = true,
    };
    transport.queue_lobby_message(11u, ready);
    transport.queue_lobby_message(22u, ready);
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[0].ready);
    ASSERT_TRUE(server.state().players[1].ready);
    ASSERT_TRUE(server.state().players[2].ready);

    transport.clear_sent_messages();
    og::sim::LobbyMessage remove;
    remove.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = 1u,
        .seat_id = removed_id,
    };
    transport.queue_lobby_message(11u, remove);
    server.poll_incoming_messages();

    ASSERT_EQ(3u, server.state().players.size());
    EXPECT_EQ(first_id, server.state().players[0].seat_id);
    EXPECT_EQ(third_id, server.state().players[1].seat_id);
    EXPECT_EQ(guest_id, server.state().players[2].seat_id);
    EXPECT_FALSE(server.state().players[0].ready);
    EXPECT_FALSE(server.state().players[1].ready);
    EXPECT_TRUE(server.state().players[2].ready)
        << "removing one machine's seat must not unready another peer";
    ASSERT_EQ(2u, transport.sent_messages().size());
    expect_all_sent_states_equal(transport.sent_messages(), server.state());

    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const og::sim::LobbyState recipient = decode_lobby_state(sent);
        if (sent.peer_id == 11u)
        {
            EXPECT_EQ(
                (std::vector<og::sim::LobbySeatId>{first_id, third_id}),
                recipient.local_seat_ids);
        }
        else if (sent.peer_id == 22u)
        {
            EXPECT_EQ(
                (std::vector<og::sim::LobbySeatId>{guest_id}),
                recipient.local_seat_ids);
        }
    }

    // The surviving third seat has compacted to local slot 1 without taking
    // the removed seat's stable command token.
    const std::vector<og::sim::LobbyPlayerBinding> bindings =
        server.build_player_bindings();
    ASSERT_EQ(3u, bindings.size());
    EXPECT_EQ(11u, bindings[1].peer_id);
    EXPECT_EQ(1u, bindings[1].local_slot);
    EXPECT_EQ(third_id, server.state().players[1].seat_id);
}

TEST(LobbyServer, remove_seat_rejects_foreign_or_stale_tokens)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1}));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Guest", {2}));
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());

    const og::sim::LobbyState before = server.state();
    transport.clear_sent_messages();
    og::sim::LobbyMessage foreign_remove;
    foreign_remove.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = before.players[2].player_index,
        .seat_id = before.players[2].seat_id,
    };
    transport.queue_lobby_message(11u, foreign_remove);
    server.poll_incoming_messages();

    EXPECT_EQ(before, server.state());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(11u, transport.sent_messages().front().peer_id);
    og::sim::LobbyState echoed =
        decode_lobby_state(transport.sent_messages().front());
    EXPECT_EQ(
        (std::vector<og::sim::LobbySeatId>{
            before.players[0].seat_id,
            before.players[1].seat_id}),
        echoed.local_seat_ids);
    echoed.local_seat_ids.clear();
    echoed.local_peer_is_host = false;
    EXPECT_EQ(before, echoed);

    // A token that was once owned but has already been removed is stale, not
    // permission to target whichever sibling now occupies its dense P#.
    const og::sim::LobbySeatId stale_id = before.players[1].seat_id;
    og::sim::LobbyMessage valid_remove;
    valid_remove.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = before.players[1].player_index,
        .seat_id = stale_id,
    };
    transport.queue_lobby_message(11u, valid_remove);
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    const og::sim::LobbyState after_valid_remove = server.state();

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, valid_remove);
    server.poll_incoming_messages();
    EXPECT_EQ(after_valid_remove, server.state());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(11u, transport.sent_messages().front().peer_id);
}

TEST(LobbyServer,
     queued_remove_seat_keeps_stable_target_when_p_numbers_redensify)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    server.connect_client(33u);
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0}));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Leaving", {1}));
    transport.queue_lobby_message(
        33u, make_multi_seat_join_message("Survivor", {1, 2, 3}));
    server.poll_incoming_messages();
    ASSERT_EQ(5u, server.state().players.size());

    const og::sim::LobbyPlayer captured_target =
        server.state().players[3];
    const og::sim::LobbySeatId survivor_first_id =
        server.state().players[2].seat_id;
    const og::sim::LobbySeatId survivor_third_id =
        server.state().players[4].seat_id;
    og::sim::LobbyMessage queued_remove;
    queued_remove.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = captured_target.player_index,
        .seat_id = captured_target.seat_id,
    };

    server.disconnect_client(22u);
    ASSERT_EQ(4u, server.state().players.size());
    EXPECT_EQ(captured_target.player_index,
              server.state().players[3].player_index);
    EXPECT_EQ(survivor_third_id, server.state().players[3].seat_id)
        << "the captured dense P# now aliases the third sibling";

    transport.queue_lobby_message(33u, queued_remove);
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());
    EXPECT_EQ(survivor_first_id, server.state().players[1].seat_id);
    EXPECT_EQ(survivor_third_id, server.state().players[2].seat_id);
    EXPECT_EQ(
        server.state().players.end(),
        std::find_if(
            server.state().players.begin(), server.state().players.end(),
            [seat_id = captured_target.seat_id](
                const og::sim::LobbyPlayer& player) {
                return player.seat_id == seat_id;
            }));
}

TEST(LobbyServer,
     final_remove_is_a_zero_seat_spectator_and_reuses_its_private_token)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u,
        make_join_message(
            "Host", 0,
            {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u,
        make_join_message(
            "Guest", 1,
            {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());

    const og::sim::LobbySeatId guest_id =
        server.state().players[1].seat_id;
    og::sim::LobbyMessage remove_guest;
    remove_guest.payload = og::sim::LobbyRemoveSeatMessage{
        // Deliberately stale: the stable server token is authoritative.
        .player_index = 0xffu,
        .seat_id = guest_id,
    };
    transport.clear_sent_messages();
    transport.queue_lobby_message(22u, remove_guest);
    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.state().players.size());
    EXPECT_EQ("Host", server.state().players.front().name);
    const std::vector<og::sim::LobbyPlayerBinding> spectator_bindings =
        server.build_player_bindings();
    ASSERT_EQ(1u, spectator_bindings.size());
    EXPECT_EQ(11u, spectator_bindings.front().peer_id);
    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const og::sim::LobbyState recipient = decode_lobby_state(sent);
        if (sent.peer_id == 11u)
        {
            EXPECT_TRUE(recipient.local_peer_is_host);
            ASSERT_EQ(1u, recipient.local_seat_ids.size());
        }
        else
        {
            EXPECT_FALSE(recipient.local_peer_is_host);
            EXPECT_TRUE(recipient.local_seat_ids.empty());
        }
    }

    // [+] creates a real active seat again, but the stable command identity
    // comes from server-private dormant peer state rather than a published
    // placeholder player.
    transport.queue_lobby_message(
        22u,
        make_join_message(
            "Guest", 1,
            {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)},
            false,
            77u));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ(guest_id, server.state().players[1].seat_id);

    og::sim::LobbyMessage ready_guest;
    ready_guest.payload = og::sim::LobbyReadyMessage{
        .player_index = 1u,
        .ready = true,
    };
    transport.queue_lobby_message(22u, ready_guest);
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[1].ready);

    // Host authority belongs to peer 11, not its seat. Removing the host's
    // final active seat keeps Scenario/GO authority, excludes that seat from
    // bindings, and lets the spectator host start the ready guest.
    const og::sim::LobbySeatId host_id =
        server.state().players[0].seat_id;
    og::sim::LobbyMessage remove_host;
    remove_host.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = 0u,
        .seat_id = host_id,
    };
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, remove_host);
    server.poll_incoming_messages();
    ASSERT_EQ(1u, server.state().players.size());
    EXPECT_FALSE(server.state().players.front().is_host);
    EXPECT_EQ(0xffu, server.state().host_player_id);
    ASSERT_EQ(1u, server.build_player_bindings().size());
    EXPECT_EQ(22u, server.build_player_bindings().front().peer_id);
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const og::sim::LobbyState recipient = decode_lobby_state(sent);
        EXPECT_EQ(sent.peer_id == 11u, recipient.local_peer_is_host);
        if (sent.peer_id == 11u)
        {
            EXPECT_TRUE(recipient.local_seat_ids.empty());
        }
    }

    og::sim::LobbyMessage start;
    start.payload = og::sim::LobbyStartGameMessage{
        .player_index = 0xffu,
    };
    transport.queue_lobby_message(11u, start);
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
}

TEST(LobbyServer, disconnecting_zero_seat_host_republishes_peer_authority)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    server.disconnect_client(11u);

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(22u, transport.sent_messages().front().peer_id);
    const og::sim::LobbyState promoted =
        decode_lobby_state(transport.sent_messages().front());
    EXPECT_TRUE(promoted.players.empty());
    EXPECT_TRUE(promoted.local_seat_ids.empty());
    EXPECT_TRUE(promoted.local_peer_is_host);
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
        .seat_id = server.state().players[1].seat_id,
        .team = 3,
    };
    transport.queue_lobby_message(11u, team_change);
    server.poll_incoming_messages();

    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);

    // A stale/foreign player index is denied and must not mutate seat 0.
    og::sim::LobbyMessage fallback_change;
    fallback_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 0xaau,
        .seat_id = 0xdeadbeefu,
        .team = 2,
    };
    transport.queue_lobby_message(11u, fallback_change);
    server.poll_incoming_messages();

    EXPECT_EQ(0, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);

    // Moving seat 0 onto seat 1's team is an accepted duplicate assignment.
    transport.clear_sent_messages();
    og::sim::LobbyMessage collide_change;
    collide_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 0u,
        .seat_id = server.state().players[0].seat_id,
        .team = 3,
    };
    transport.queue_lobby_message(11u, collide_change);
    server.poll_incoming_messages();

    EXPECT_EQ(3, server.state().players[0].team);
    EXPECT_EQ(3, server.state().players[1].team);
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(11u, transport.sent_messages()[0].peer_id);
    og::sim::LobbyState echoed =
        decode_lobby_state(transport.sent_messages()[0]);
    ASSERT_EQ(2u, echoed.local_seat_ids.size());
    echoed.local_seat_ids.clear();
    echoed.local_peer_is_host = false;
    EXPECT_EQ(server.state(), echoed);
}

TEST(LobbyServer,
     queued_team_change_keeps_stable_target_when_disconnect_redensifies_p_numbers)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    server.connect_client(33u);

    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0}));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Leaving", {1}));
    transport.queue_lobby_message(
        33u, make_multi_seat_join_message("Survivor", {1, 2}));
    server.poll_incoming_messages();

    ASSERT_EQ(4u, server.state().players.size());
    const og::sim::LobbyPlayer old_survivor_first = server.state().players[2];
    const og::sim::LobbySeatId survivor_second_id =
        server.state().players[3].seat_id;
    ASSERT_EQ(2u, old_survivor_first.player_index);
    ASSERT_EQ(3u, server.state().players[3].player_index);

    // This request was authored against the pre-disconnect state. Once P2
    // leaves, its captured global index 2 aliases Survivor's SECOND seat.
    // The stable token must still select Survivor's FIRST seat.
    og::sim::LobbyMessage queued_change;
    queued_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = old_survivor_first.player_index,
        .seat_id = old_survivor_first.seat_id,
        .team = 3,
    };

    server.disconnect_client(22u);
    ASSERT_EQ(3u, server.state().players.size());
    ASSERT_EQ(1u, server.state().players[1].player_index);
    ASSERT_EQ(old_survivor_first.seat_id,
              server.state().players[1].seat_id);
    ASSERT_EQ(2u, server.state().players[2].player_index);
    ASSERT_EQ(survivor_second_id, server.state().players[2].seat_id);

    transport.queue_lobby_message(33u, queued_change);
    server.poll_incoming_messages();

    EXPECT_EQ(3, server.state().players[1].team)
        << "the queued command must follow its stable seat token";
    EXPECT_EQ(2, server.state().players[2].team)
        << "the re-densified P3 must not inherit stale P3 input";
}

TEST(LobbyServer, deferred_disconnect_is_published_before_same_batch_start)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    transport.set_connected_peers({11u, 22u, 33u});
    server.poll_incoming_messages();

    transport.queue_lobby_message(
        11u, make_join_message(
                 "Host", 0,
                 {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message(
                 "Leaving", 1,
                 {make_slot(1u, 200, "Leaving Guy", FAMILY_ARCHER)}));
    transport.queue_lobby_message(
        33u, make_join_message(
                 "Survivor", 2,
                 {make_slot(2u, 300, "Survivor Guy", FAMILY_MAGE)}));
    server.poll_incoming_messages();
    og::sim::LobbyMessage leaving_ready;
    leaving_ready.payload = og::sim::LobbyReadyMessage{
        .player_index = 1u,
        .ready = true,
    };
    og::sim::LobbyMessage survivor_ready;
    survivor_ready.payload = og::sim::LobbyReadyMessage{
        .player_index = 2u,
        .ready = true,
    };
    transport.queue_lobby_message(22u, leaving_ready);
    transport.queue_lobby_message(33u, survivor_ready);
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());

    // The transport sees peer 22 disconnect, but its final idempotent Ready
    // and the host Start arrive in the same drain. The final message is
    // processed; the deferred removal must then be broadcast before Start.
    transport.clear_sent_messages();
    transport.set_connected_peers({11u, 33u});
    og::sim::LobbyMessage start;
    start.payload = og::sim::LobbyStartGameMessage{
        .player_index = 0u,
        .request_id = 77u,
    };
    transport.queue_lobby_message(22u, leaving_ready);
    transport.queue_lobby_message(11u, start);
    server.poll_incoming_messages();

    ASSERT_TRUE(server.start_game_requested());
    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ("Host", server.state().players[0].name);
    EXPECT_EQ("Survivor", server.state().players[1].name);

    std::optional<og::sim::LobbyState> latest_survivor_state;
    bool saw_start = false;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        if (sent.peer_id != 33u)
            continue;
        if (const auto state =
                og::sim::deserialize_lobby_state_message(sent.data);
            state.has_value())
        {
            EXPECT_FALSE(saw_start)
                << "no revised roster state may trail StartGame";
            latest_survivor_state = *state;
            continue;
        }
        const auto message = og::sim::deserialize_lobby_message(sent.data);
        if (!message.has_value() ||
            message->kind() != og::sim::LobbyMessageKind::StartGame)
        {
            continue;
        }
        saw_start = true;
        ASSERT_TRUE(latest_survivor_state.has_value())
            << "the revised roster state must precede StartGame";
        ASSERT_EQ(2u, latest_survivor_state->players.size());
        EXPECT_EQ("Host", latest_survivor_state->players[0].name);
        EXPECT_EQ("Survivor", latest_survivor_state->players[1].name);
    }
    EXPECT_TRUE(saw_start);
}

TEST(LobbyServer, state_echo_proves_local_seat_ownership_with_server_tokens)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Same Name", {0, 1}));
    transport.queue_lobby_message(
        22u, make_multi_seat_join_message("Same Name", {2}));
    server.poll_incoming_messages();

    ASSERT_EQ(3u, server.state().players.size());
    EXPECT_TRUE(server.state().local_seat_ids.empty())
        << "the canonical server state is not recipient-specific";
    EXPECT_EQ(server.state().players[0].machine_id,
              server.state().players[1].machine_id);
    EXPECT_NE(server.state().players[0].machine_id,
              server.state().players[2].machine_id)
        << "identical display names must not merge distinct network peers";

    transport.clear_sent_messages();
    transport.queue_lobby_message(
        11u, make_team_change_message(server.state().players[0], 3));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const og::sim::ReceivedMessage& message : transport.sent_messages())
    {
        const og::sim::LobbyState recipient = decode_lobby_state(message);
        if (message.peer_id == 11u)
        {
            ASSERT_EQ(2u, recipient.local_seat_ids.size());
            EXPECT_EQ(server.state().players[0].seat_id,
                      recipient.local_seat_ids[0]);
            EXPECT_EQ(server.state().players[1].seat_id,
                      recipient.local_seat_ids[1]);
        }
        else
        {
            ASSERT_EQ(22u, message.peer_id);
            ASSERT_EQ(1u, recipient.local_seat_ids.size());
            EXPECT_EQ(server.state().players[2].seat_id,
                      recipient.local_seat_ids[0]);
        }
    }

    // A real token owned by another peer is still foreign. Even if the
    // request advertises this sender's current dense P#, authority is the
    // (sending peer, seat_id) pair and the guest seat remains unchanged.
    transport.clear_sent_messages();
    og::sim::LobbyMessage forged_change;
    forged_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = server.state().players[2].player_index,
        .seat_id = server.state().players[0].seat_id,
        .team = 3,
    };
    transport.queue_lobby_message(22u, forged_change);
    server.poll_incoming_messages();

    EXPECT_EQ(2, server.state().players[2].team);
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(22u, transport.sent_messages()[0].peer_id);
    const og::sim::LobbyState denial =
        decode_lobby_state(transport.sent_messages()[0]);
    ASSERT_EQ(1u, denial.local_seat_ids.size());
    EXPECT_EQ(server.state().players[2].seat_id,
              denial.local_seat_ids[0]);
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
    const og::sim::LobbySeatId host_seat_0_id =
        server.state().players[0].seat_id;
    const og::sim::LobbySeatId host_seat_1_id =
        server.state().players[1].seat_id;
    const og::sim::LobbySeatId guest_seat_id =
        server.state().players[4].seat_id;
    const og::sim::LobbyMachineId host_machine_id =
        server.state().players[0].machine_id;
    const og::sim::LobbyMachineId guest_machine_id =
        server.state().players[4].machine_id;

    // set_player_mode 4 -> 2 re-sends the join with two seats: the seat list
    // shrinks and every global index is re-densified. Surviving local ordinals
    // retain their stable command tokens, including the shifted guest seat.
    transport.queue_lobby_message(
        11u, make_multi_seat_join_message("Host", {0, 1}, 0u, 100));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(3u, state.players.size());
    EXPECT_EQ("Host", state.players[0].name);
    EXPECT_EQ("Host#1", state.players[1].name);
    EXPECT_EQ("Guest", state.players[2].name);
    EXPECT_EQ(host_seat_0_id, state.players[0].seat_id);
    EXPECT_EQ(host_seat_1_id, state.players[1].seat_id);
    EXPECT_EQ(guest_seat_id, state.players[2].seat_id);
    EXPECT_EQ(host_machine_id, state.players[0].machine_id);
    EXPECT_EQ(host_machine_id, state.players[1].machine_id);
    EXPECT_EQ(guest_machine_id, state.players[2].machine_id);
    EXPECT_NE(host_machine_id, guest_machine_id);
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
    // Allied mode changes seat assignment, never the character's combat team.
    EXPECT_EQ(0, equivalent.team_list[0].character.teamnum);
    EXPECT_EQ(1, equivalent.team_list[1].character.teamnum);
    EXPECT_EQ(2, equivalent.team_list[2].character.teamnum);
}

// ---------------------------------------------------------------------------
// §4.3 ready system (protocol v8): server-authoritative ready, content-
// identical join preservation, the StartGame gate, and the denial echo.
// ---------------------------------------------------------------------------

namespace {

og::sim::LobbyMessage make_ready_message(std::uint8_t player_index, bool ready)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyReadyMessage{
        .player_index = player_index,
        .ready = ready,
    };
    return message;
}

og::sim::LobbyMessage make_start_message(std::uint8_t player_index,
                                         std::uint32_t request_id = 0)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyStartGameMessage{
        .player_index = player_index,
        .request_id = request_id,
    };
    return message;
}

// Host (11u) + ready guest (22u), each with one deployed character over a typed
// (in-process loopback) transport — the shape the go_menu deadlock bit on.
void ready_two_peer_lobby(MockLobbyTransport& transport,
                          og::sim::LobbyServer& server)
{
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[1].ready);
}

} // namespace

TEST(LobbyServer, content_identical_join_resend_preserves_ready)
{
    // The deadlock regression: go_menu re-sends the machine's join right before
    // requesting start. A content-identical re-send must NOT clear ready.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    // Re-send the guest's IDENTICAL join (same seat, team, and character).
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready)
        << "a content-identical join re-send preserves ready";

    // And the host can now start over the re-synced-but-still-ready lobby.
    transport.queue_lobby_message(11u, make_start_message(0u, 101u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial);
}

TEST(LobbyServer, content_identical_comparison_excludes_owner_fields)
{
    // [NET-R7]: the assembly-only owner_player_index/owner_save_slot are NOT
    // content — the loopback host path never normalizes them, so a re-send that
    // differs ONLY in those fields still preserves ready.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    og::sim::LobbyCharacterSlot slot = make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER);
    slot.owner_player_index = 7u;   // differs from the stored default (0xff)
    slot.owner_save_slot = 9u;
    transport.queue_lobby_message(22u, make_join_message("Guest", 1, {slot}));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready)
        << "owner_* fields are excluded from the content-identical comparison";
}

TEST(LobbyServer, join_rename_and_company_change_preserve_ready)
{
    // A rename (or company change) never un-readies anyone.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    og::sim::LobbyMessage renamed = make_join_message(
        "Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)});
    std::get<og::sim::LobbyJoinMessage>(renamed.payload).player.company =
        "New Company Name";
    transport.queue_lobby_message(22u, renamed);
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready);
}

TEST(LobbyServer, join_roster_change_clears_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    // Re-send with an ADDED character: a real roster change clears ready.
    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 1,
                          {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER),
                           make_slot(2u, 201, "Recruit", FAMILY_MAGE)}));
    server.poll_incoming_messages();
    EXPECT_FALSE(server.state().players[1].ready)
        << "an added character clears ready";
}

TEST(LobbyServer, join_deploy_change_clears_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    // Re-send with the same character BENCHED: the deploy flag IS content.
    og::sim::LobbyCharacterSlot benched =
        make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER);
    benched.deployed = false;
    transport.queue_lobby_message(22u, make_join_message("Guest", 1, {benched}));
    server.poll_incoming_messages();
    EXPECT_FALSE(server.state().players[1].ready)
        << "changing a deploy selection clears ready";
}

TEST(LobbyServer, team_change_clears_that_machines_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    og::sim::LobbyMessage team_change;
    team_change.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = 1u,
        .seat_id = server.state().players[1].seat_id,
        .team = 2,
    };
    transport.queue_lobby_message(22u, team_change);
    server.poll_incoming_messages();
    ASSERT_EQ(2, server.state().players[1].team);
    EXPECT_FALSE(server.state().players[1].ready)
        << "an accepted team change clears the machine's ready";
}

TEST(LobbyServer, host_settings_change_clears_only_nonhost_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);
    // Ready the host too (informational) to prove ONLY the non-host is cleared.
    transport.queue_lobby_message(11u, make_ready_message(0u, true));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[0].ready);

    og::sim::LobbySettings settings = server.state().settings;
    settings.difficulty = settings.difficulty == 3 ? 2 : 3; // a real change
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = settings,
    };
    transport.queue_lobby_message(11u, settings_message);
    server.poll_incoming_messages();

    EXPECT_TRUE(server.state().players[0].ready) << "host ready is not cleared";
    EXPECT_FALSE(server.state().players[1].ready)
        << "a host settings change clears every non-host machine's ready";
}

TEST(LobbyServer, identical_settings_change_preserves_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    // Echo the CURRENT settings (what go_menu re-sends on every start attempt):
    // identical after sanitize ⇒ no ready clear.
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = server.state().settings,
    };
    transport.queue_lobby_message(11u, settings_message);
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready)
        << "an identical settings echo is a no-op for ready";
}

TEST(LobbyServer, start_denied_when_nonhost_not_ready_keeps_lobby_live)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    // Guest is not ready: the host's GO is denied WITHOUT locking the lobby.
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_start_message(0u, 101u));
    server.poll_incoming_messages();
    EXPECT_FALSE(server.start_game_requested());
    EXPECT_EQ(start_denial_reason_value(og::sim::StartDenialReason::MachinesNotReady),
              server.state().last_start_denial);
    EXPECT_EQ(101u, server.state().last_start_request_id);
    // The denial is echoed (the host must see WHO is blocking).
    ASSERT_FALSE(transport.sent_messages().empty());

    // The lobby is still LIVE — a subsequent ready is processed, not eaten by a
    // lock, and the next start is accepted.
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[1].ready);
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial) << "acceptance clears the denial";
}

TEST(LobbyServer, dedicated_denial_keeps_loop_polling_and_echoes_reason_to_every_peer)
{
    // The dedicated-server shape (server_main.cpp): a LobbyServer with NO
    // local session whose host is the FIRST-CONNECTED peer — the ELECTED host,
    // the NORMAL openglad_server path per [NET-R3]. Pins the two reads that
    // shape drives:
    //   1. consume_start_game_requested() — server_main's lobby-loop read
    //      (server_main.cpp `if (lobby_server.consume_start_game_requested())
    //      break;`) — stays FALSE on a denied GO, so the dedicated loop keeps
    //      polling instead of breaking into gameplay;
    //   2. the denial reason rides the serialized LobbyState ECHO to EVERY
    //      peer. The elected host has no in-process server.state() to read —
    //      the echo is its ONLY source of the reason — and guests render the
    //      same echo.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u); // first-connected peer ⇒ elected host
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Elected Host", 0,
                          {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u,
        make_join_message("Guest", 1,
                          {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();
    ASSERT_EQ(0u, server.state().host_player_id)
        << "the first-connected peer is the elected host";

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_start_message(0u, 101u));
    server.poll_incoming_messages();

    EXPECT_FALSE(server.consume_start_game_requested())
        << "server_main's loop read: a denied GO keeps the lobby loop polling";

    const std::uint8_t expected_reason = start_denial_reason_value(
        og::sim::StartDenialReason::MachinesNotReady);
    std::set<og::sim::PeerId> echoed_peers;
    for (const og::sim::ReceivedMessage& message : transport.sent_messages())
    {
        const og::sim::LobbyState echoed = decode_lobby_state(message);
        EXPECT_EQ(expected_reason, echoed.last_start_denial)
            << "every state echo after the denial carries the reason";
        EXPECT_EQ(101u, echoed.last_start_request_id);
        echoed_peers.insert(message.peer_id);
    }
    EXPECT_EQ((std::set<og::sim::PeerId>{11u, 22u}), echoed_peers)
        << "the denial echo must reach the elected host AND the guest";

    // Retry without changing the blocker. Clearing then restoring the same
    // denial reason leaves LobbyState byte-identical, but this request still
    // needs its own authoritative echo or the async host waits forever.
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_start_message(0u, 101u));
    server.poll_incoming_messages();
    EXPECT_FALSE(server.consume_start_game_requested());
    echoed_peers.clear();
    for (const og::sim::ReceivedMessage& message : transport.sent_messages())
    {
        const og::sim::LobbyState echoed = decode_lobby_state(message);
        EXPECT_EQ(expected_reason, echoed.last_start_denial);
        EXPECT_EQ(101u, echoed.last_start_request_id);
        echoed_peers.insert(message.peer_id);
    }
    EXPECT_EQ((std::set<og::sim::PeerId>{11u, 22u}), echoed_peers)
        << "an identical repeated denial still resolves the new request";

    // The dedicated loop stays live: the guest readies, the elected host's
    // next GO is accepted, and consume_start_game_requested() flips true —
    // the exact condition that breaks server_main into gameplay.
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_start_message(0u, 103u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.consume_start_game_requested())
        << "server_main's break-into-gameplay read";
    EXPECT_EQ(0u, server.state().last_start_denial);
    EXPECT_EQ(0u, server.state().last_start_request_id)
        << "accepted requests correlate through the StartGame confirmation";
    ASSERT_FALSE(transport.sent_messages().empty());
    std::size_t confirmation_count = 0;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const auto confirmation =
            og::sim::deserialize_lobby_message(sent.data);
        if (!confirmation.has_value())
            continue; // accepted-after-denial also broadcasts the cleared state
        ASSERT_EQ(og::sim::LobbyMessageKind::StartGame,
                  confirmation->kind());
        EXPECT_EQ(103u,
                  std::get<og::sim::LobbyStartGameMessage>(
                      confirmation->payload)
                      .request_id);
        ++confirmation_count;
    }
    EXPECT_EQ(2u, confirmation_count);
}

TEST(LobbyServer, start_denial_survives_interleaved_join_cleared_on_next_start)
{
    // [NET-R4]: the denial is cleared ONLY by the next StartGame request — a
    // queued joiner message between denial and read must not wipe it.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    ASSERT_EQ(
        start_denial_reason_value(og::sim::StartDenialReason::MachinesNotReady),
        server.state().last_start_denial);

    // Interleaved non-StartGame message (a content-identical guest re-join):
    // the recorded denial must survive it.
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();
    EXPECT_EQ(
        start_denial_reason_value(og::sim::StartDenialReason::MachinesNotReady),
        server.state().last_start_denial)
        << "an interleaved join must not wipe the recorded denial";

    // A ready message likewise leaves it in place.
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    EXPECT_EQ(
        start_denial_reason_value(og::sim::StartDenialReason::MachinesNotReady),
        server.state().last_start_denial);

    // The NEXT StartGame clears and re-evaluates it (now accepted).
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial);
}

TEST(LobbyServer, start_denied_when_no_deployed_characters)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    og::sim::LobbyCharacterSlot benched =
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER);
    benched.deployed = false;
    transport.queue_lobby_message(11u, make_join_message("Host", 0, {benched}));
    server.poll_incoming_messages();

    // Single-peer lobby ⇒ rule 3 passes vacuously, but zero deployed ⇒ rule 4
    // denies with NoDeployedCharacters.
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_FALSE(server.start_game_requested());
    EXPECT_EQ(start_denial_reason_value(
                  og::sim::StartDenialReason::NoDeployedCharacters),
              server.state().last_start_denial);
}

TEST(LobbyServer, local_session_start_bypasses_ready_and_deploy_gates)
{
    // Rule 1: a local/split-screen lobby GOes exactly as today — never gated on
    // ready. Two local peers join, neither readies; the start is still accepted.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport, /*local_session=*/true);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_join_message("P1", 0, {make_slot(0u, 100, "A", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("P2", 1, {make_slot(1u, 200, "B", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial);
}

// Protocol v13 (#218): the owner-injected start gate is start_allowed's final
// rule. A Failed MatchStage denies GO with StageFailed, the lobby stays live
// (never locked), and a recovered stage lets the retry through.
TEST(LobbyServer, start_gate_stage_failed_denies_go_and_recovery_admits_retry)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    int gate_calls = 0;
    og::sim::StartDenialReason gate_reason =
        og::sim::StartDenialReason::StageFailed;
    server.set_start_gate([&gate_calls, &gate_reason] {
        ++gate_calls;
        return gate_reason;
    });

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_start_message(0u, 301u));
    server.poll_incoming_messages();
    EXPECT_EQ(1, gate_calls) << "rules 2-4 pass, so the gate is consulted";
    EXPECT_FALSE(server.start_game_requested())
        << "a failed stage must never lock the lobby";
    EXPECT_EQ(start_denial_reason_value(
                  og::sim::StartDenialReason::StageFailed),
              server.state().last_start_denial);
    EXPECT_EQ(301u, server.state().last_start_request_id);
    ASSERT_FALSE(transport.sent_messages().empty())
        << "the StageFailed denial is echoed like every other reason";

    // The owner restages successfully; the retry passes through the gate.
    gate_reason = og::sim::StartDenialReason::None;
    transport.queue_lobby_message(11u, make_start_message(0u, 302u));
    server.poll_incoming_messages();
    EXPECT_EQ(2, gate_calls);
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial)
        << "acceptance clears the denial";
}

TEST(LobbyServer, start_gate_runs_after_ready_rule_and_never_for_nonhost)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    int gate_calls = 0;
    server.set_start_gate([&gate_calls] {
        ++gate_calls;
        return og::sim::StartDenialReason::StageFailed;
    });

    // Guest not ready: rule 3 denies FIRST — the gate is never consulted, so
    // the denial reads MachinesNotReady, not StageFailed.
    transport.queue_lobby_message(11u, make_start_message(0u, 401u));
    server.poll_incoming_messages();
    EXPECT_EQ(0, gate_calls);
    EXPECT_EQ(start_denial_reason_value(
                  og::sim::StartDenialReason::MachinesNotReady),
              server.state().last_start_denial);

    // A non-host StartGame stays silently ignored — no gate consult either.
    transport.queue_lobby_message(22u, make_start_message(1u, 402u));
    server.poll_incoming_messages();
    EXPECT_EQ(0, gate_calls);
    EXPECT_FALSE(server.start_game_requested());
}

TEST(LobbyServer, local_session_start_ignores_start_gate)
{
    // Rule 1 outranks the gate: solo/split-screen GO keeps its unconditional
    // pass (today's first-level load fallback covers a failed local stage).
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport, /*local_session=*/true);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u, make_join_message("P1", 0, {make_slot(0u, 100, "A", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    int gate_calls = 0;
    server.set_start_gate([&gate_calls] {
        ++gate_calls;
        return og::sim::StartDenialReason::StageFailed;
    });

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_EQ(0, gate_calls) << "local sessions never consult the gate";
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial);
}

TEST(LobbyServer, unlock_for_new_round_clears_all_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);
    transport.queue_lobby_message(11u, make_ready_message(0u, true));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[0].ready);
    ASSERT_TRUE(server.state().players[1].ready);

    // Start (locks the lobby), then reopen it for the next round.
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.start_game_requested());

    transport.clear_sent_messages();
    server.unlock_for_new_round();
    EXPECT_FALSE(server.start_game_requested());
    for (const og::sim::LobbyPlayer& player : server.state().players)
        EXPECT_FALSE(player.ready) << "every machine re-readies each round";
    ASSERT_EQ(2u, transport.sent_messages().size())
        << "the new-round ready clear must be echoed to both machines";
    for (const og::sim::ReceivedMessage& message : transport.sent_messages())
    {
        const og::sim::LobbyState echoed = decode_lobby_state(message);
        ASSERT_EQ(2u, echoed.players.size());
        EXPECT_FALSE(echoed.players[0].ready);
        EXPECT_FALSE(echoed.players[1].ready);
    }

    // The reopened lobby accepts messages again (the lock was cleared).
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready);
}

TEST(LobbyServer, identical_resume_join_repeats_missed_ready_reset_to_sender)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.start_game_requested());

    // Model the guest's still-active GameClient draining the unlock broadcast
    // before its picker resumes.
    server.unlock_for_new_round();
    transport.clear_sent_messages();

    // The private save did not change, so the resume declaration is exactly
    // the stored roster. It still needs a fresh recipient-specific echo to
    // release the next-round ready guard.
    transport.queue_lobby_message(
        22u, make_join_message(
                 "Guest", 1,
                 {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)},
                 false,
                 0x11223344u));
    server.poll_incoming_messages();

    EXPECT_EQ(0u, server.state().last_join_request_id)
        << "the canonical state must remain recipient-neutral";
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(22u, transport.sent_messages().front().peer_id);
    const og::sim::LobbyState echoed =
        decode_lobby_state(transport.sent_messages().front());
    ASSERT_EQ(2u, echoed.players.size());
    EXPECT_FALSE(echoed.players[1].ready);
    ASSERT_EQ(1u, echoed.local_seat_ids.size());
    EXPECT_EQ(echoed.players[1].seat_id, echoed.local_seat_ids.front());
    EXPECT_EQ(0x11223344u, echoed.last_join_request_id);
}

TEST(LobbyServer, join_ack_is_recipient_specific_and_repeatable)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message(
                 "Host", 0,
                 {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)},
                 false,
                 101u));
    transport.queue_lobby_message(
        22u, make_join_message(
                 "Guest", 1,
                 {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)},
                 false,
                 202u));
    server.poll_incoming_messages();
    EXPECT_EQ(0u, server.state().last_join_request_id);

    // A retry reuses the same nonce. Content-identical declarations do not
    // change canonical state, but still receive a fresh correlated echo.
    transport.clear_sent_messages();
    transport.queue_lobby_message(
        22u, make_join_message(
                 "Guest", 1,
                 {make_slot(1u, 200, "Guest Guy", FAMILY_ARCHER)},
                 false,
                 202u));
    server.poll_incoming_messages();
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(22u, transport.sent_messages().front().peer_id);
    EXPECT_EQ(
        202u,
        decode_lobby_state(transport.sent_messages().front())
            .last_join_request_id);

    // Later broadcasts retain each recipient's own acknowledgement. A guest
    // must never observe the host's nonce (or vice versa).
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_ready_message(0u, true));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const og::sim::LobbyState recipient = decode_lobby_state(sent);
        EXPECT_EQ(sent.peer_id == 11u ? 101u : 202u,
                  recipient.last_join_request_id);
    }
}

TEST(LobbyServer, next_round_join_received_while_locked_is_applied_on_unlock)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.start_game_requested());

    // The guest reaches Base Camp first and sends its sole resume Join while
    // the host is still on the locked previous round. It must not mutate the
    // running round, but it also must not be lost.
    transport.clear_sent_messages();
    transport.queue_lobby_message(
        22u, make_join_message(
                 "Guest", 1,
                 {make_slot(1u, 200, "Racing Mutation", FAMILY_ARCHER)},
                 false,
                 302u));
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ("Guest Guy",
              server.state().players[1].character_slots[0].character.name);
    EXPECT_TRUE(transport.sent_messages().empty())
        << "an ordinary Join that loses the Start race is dropped, not queued";

    og::sim::LobbyMessage resume_join = make_join_message(
        "Guest", 1,
        {make_slot(1u, 201, "AdvGuest", FAMILY_ARCHER)},
        false,
        303u);
    std::get<og::sim::LobbyJoinMessage>(
        resume_join.payload).resume_after_level = true;
    transport.queue_lobby_message(22u, resume_join);
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ("Guest Guy",
              server.state().players[1].character_slots[0].character.name);
    EXPECT_TRUE(transport.sent_messages().empty())
        << "a locked Join stays invisible to the running round";

    server.unlock_for_new_round();
    ASSERT_EQ(2u, server.state().players.size());
    ASSERT_EQ(1u, server.state().players[1].character_slots.size());
    EXPECT_EQ("AdvGuest",
              server.state().players[1].character_slots[0].character.name)
        << "unlock must apply the early resume declaration";
    for (const og::sim::LobbyPlayer& player : server.state().players)
        EXPECT_FALSE(player.ready);

    std::uint32_t last_guest_join_ack = 0;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        if (sent.peer_id == 22u)
        {
            last_guest_join_ack =
                decode_lobby_state(sent).last_join_request_id;
        }
    }
    EXPECT_EQ(303u, last_guest_join_ack)
        << "the queued Join is acknowledged only when unlock processes it";
}

TEST(LobbyServer, disconnect_removes_a_queued_next_round_join)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    ready_two_peer_lobby(transport, server);

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.start_game_requested());

    og::sim::LobbyMessage resume_join = make_join_message(
        "Guest", 1,
        {make_slot(1u, 201, "Should Not Return", FAMILY_ARCHER)},
        false,
        404u);
    std::get<og::sim::LobbyJoinMessage>(
        resume_join.payload).resume_after_level = true;
    transport.queue_lobby_message(22u, resume_join);
    server.poll_incoming_messages();

    server.disconnect_client(22u);
    server.unlock_for_new_round();

    ASSERT_EQ(1u, server.state().players.size());
    EXPECT_EQ("Host", server.state().players.front().name);
    EXPECT_EQ((std::vector<og::sim::PeerId>{22u}),
              transport.disconnected_peers());
}

// §4.2: only DEPLOYED slots consume the combined 24-slot capacity. A host
// with half its 20-slot roster benched leaves 24-10 = 14 slots of budget, so
// a 10-slot guest fits fully deployed — and the assembly equivalent
// materializes exactly the deployed slots (benched filtered BEFORE
// densification, owner mapping intact through compaction).
TEST(LobbyServer, benched_slots_free_capacity_and_assembly_filters_them)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    std::vector<og::sim::LobbyCharacterSlot> host_slots =
        make_slots(0u, 20u, 100, FAMILY_SOLDIER);
    for (std::size_t slot = 10; slot < host_slots.size(); ++slot)
        host_slots[slot].deployed = false;
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, std::move(host_slots)));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, make_slots(30u, 10u, 200, FAMILY_ARCHER)));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    ASSERT_EQ(20u, server.state().players[0].character_slots.size());
    ASSERT_EQ(10u, server.state().players[1].character_slots.size());
    for (const og::sim::LobbyCharacterSlot& slot :
         server.state().players[1].character_slots)
    {
        EXPECT_TRUE(slot.deployed)
            << "benched host slots must not crowd the guest out of the 24-cap";
    }

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(20u, equivalent.team_list.size())
        << "assembly materializes only the 10+10 deployed slots";
    // Sparse deployed indices (0..9 host + 30..39 guest) compact to 0..19
    // while owner_save_slot keeps the owner's ORIGINAL private slot.
    EXPECT_EQ(0u, equivalent.team_list.front().slot_index);
    EXPECT_EQ(19u, equivalent.team_list.back().slot_index);
    EXPECT_EQ(0u, equivalent.team_list.front().owner_save_slot);
    EXPECT_EQ(39u, equivalent.team_list.back().owner_save_slot);
    for (const og::sim::LobbyCharacterSlot& slot : equivalent.team_list)
        EXPECT_TRUE(slot.deployed);
}

// §4.2 [NET-F2] server half of the convergence loop: the overflow
// force-bench is IDEMPOTENT against re-sends. A client that re-sends its
// optimistic (all-deployed) join gets force-cleared to the same stored
// seats — content-identical, ready survives — and one that re-sends the
// ADOPTED (reconciled) flags is content-identical too.
TEST(LobbyServer, overflow_force_bench_is_idempotent_and_preserves_ready)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, make_slots(0u, 24u, 100, FAMILY_SOLDIER)));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, make_slots(0u, 3u, 200, FAMILY_ARCHER)));
    server.poll_incoming_messages();

    // Capacity is exhausted by the host: every guest slot is force-benched
    // and the echo carries the flags.
    ASSERT_EQ(3u, server.state().players[1].character_slots.size());
    for (const og::sim::LobbyCharacterSlot& slot :
         server.state().players[1].character_slots)
    {
        EXPECT_FALSE(slot.deployed);
    }

    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    ASSERT_TRUE(server.state().players[1].ready);

    // Optimistic re-send (client not yet reconciled: claims deployed=true).
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, make_slots(0u, 3u, 200, FAMILY_ARCHER)));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready)
        << "the force-bench must be idempotent: an optimistic re-send "
           "converges to the same stored seats and ready survives";

    // Reconciled re-send (client adopted the echoed flags).
    std::vector<og::sim::LobbyCharacterSlot> adopted =
        make_slots(0u, 3u, 200, FAMILY_ARCHER);
    for (og::sim::LobbyCharacterSlot& slot : adopted)
        slot.deployed = false;
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, std::move(adopted)));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.state().players[1].ready)
        << "a reconciled re-send is content-identical with the stored seats";

    // The gate still passes: the host's 24 deployed satisfy rule 4.
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(24u, server.build_save_data_equivalent().team_list.size());
}

// §4.3 rule 4 has NO per-machine minimum (with or without cross-control): a
// host machine with its whole roster benched starts fine when any other
// machine deploys at least one character.
TEST(LobbyServer, zero_deploy_host_starts_when_another_machine_deploys)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    og::sim::LobbyCharacterSlot benched_host =
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER);
    benched_host.deployed = false;
    og::sim::LobbyCharacterSlot deployed_guest =
        make_slot(0u, 200, "Guest Guy", FAMILY_ARCHER);
    deployed_guest.character.teamnum = 1;
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {benched_host}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {deployed_guest}));
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();

    // cross_control stays OFF (default 0): the per-machine minimum is gone
    // regardless — only the GLOBAL >= 1 deployed rule exists server-side.
    ASSERT_EQ(0, server.state().settings.cross_control);
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial);

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(1u, equivalent.team_list.size())
        << "only the guest's deployed character enters the match";
    EXPECT_EQ(server.state().players[1].player_index,
              equivalent.team_list[0].owner_player_index);

    const auto bindings = server.build_player_bindings();
    ASSERT_EQ(2u, bindings.size());
    EXPECT_EQ(0, bindings[0].team);
    EXPECT_EQ(1, bindings[1].team)
        << "explicit bindings stay exact even when the host has no deployed fighter";
}

// §4.3 rule 4 stays GLOBAL under cross-control: an all-benched lobby is
// denied NoDeployedCharacters even with cross_control ON (cross-control
// removes per-machine minimums, never the global >= 1).
TEST(LobbyServer, all_benched_start_denied_even_with_cross_control_on)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);

    og::sim::LobbyCharacterSlot benched_host =
        make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER);
    benched_host.deployed = false;
    og::sim::LobbyCharacterSlot benched_guest =
        make_slot(0u, 200, "Guest Guy", FAMILY_ARCHER);
    benched_guest.deployed = false;
    transport.queue_lobby_message(
        11u, make_join_message("Host", 0, {benched_host}));
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {benched_guest}));
    server.poll_incoming_messages();

    // Host flips cross-control ON (host-only settings path; §4.4).
    og::sim::LobbySettings cross_on = server.state().settings;
    cross_on.cross_control = 1;
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = std::move(cross_on),
    };
    transport.queue_lobby_message(11u, settings_message);
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    server.poll_incoming_messages();
    ASSERT_EQ(1, server.state().settings.cross_control);
    ASSERT_TRUE(server.state().players[1].ready);

    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_FALSE(server.start_game_requested());
    EXPECT_EQ(start_denial_reason_value(
                  og::sim::StartDenialReason::NoDeployedCharacters),
              server.state().last_start_denial);

    // Deploying one character anywhere clears the blocker.
    transport.queue_lobby_message(
        22u, make_join_message("Guest", 1, {make_slot(0u, 200, "Guest Guy", FAMILY_ARCHER)}));
    transport.queue_lobby_message(22u, make_ready_message(1u, true));
    transport.queue_lobby_message(11u, make_start_message(0u));
    server.poll_incoming_messages();
    EXPECT_TRUE(server.start_game_requested());
    EXPECT_EQ(0u, server.state().last_start_denial);
}

// §4.2: LOCAL sessions deliberately DO NOT filter benched slots out of the
// save-data equivalent — the local start path seeds the player's real
// active-company save from it, so filtering would drop benched members from
// the save file. The local assembly filter lives at spawn time instead
// (spawn_team_from_save), keeping solo saves loss-free and byte-identical.
TEST(LobbyServer, local_session_equivalent_keeps_benched_slots)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport, /*local_session=*/true);
    server.connect_client(11u);

    og::sim::LobbyCharacterSlot deployed =
        make_slot(0u, 100, "Front", FAMILY_SOLDIER);
    og::sim::LobbyCharacterSlot benched =
        make_slot(1u, 101, "Reserve", FAMILY_MAGE);
    benched.deployed = false;
    transport.queue_lobby_message(
        11u, make_join_message("P1", 0, {deployed, benched}));
    server.poll_incoming_messages();

    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    ASSERT_EQ(2u, equivalent.team_list.size());
    EXPECT_TRUE(equivalent.team_list[0].deployed);
    EXPECT_FALSE(equivalent.team_list[1].deployed)
        << "the benched member must ride the local equivalent (and thus the "
           "save seed) with its flag intact";
    EXPECT_EQ("Reserve", equivalent.team_list[1].character.name);
}

TEST(LobbyServer, raw_poll_rejects_bad_envelope_before_same_peer_message)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_raw_message(11u, {0xffu});
    transport.queue_lobby_message(
        11u,
        make_join_message(
            "Rejected",
            0,
            {make_slot(0u, 100, "Rejected Guy", FAMILY_SOLDIER)}));
    const auto hello = og::sim::serialize_hello(og::sim::HelloMessage{});
    transport.queue_raw_message(
        22u,
        std::vector<std::uint8_t>(hello.begin(), hello.end()));

    EXPECT_NO_THROW(server.poll_incoming_messages());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u}),
              transport.disconnected_peers());
    EXPECT_TRUE(server.state().players.empty())
        << "a valid message later in the malformed peer's batch is discarded";

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(22u, transport.sent_messages().front().peer_id);
    const og::sim::LobbyState promoted =
        decode_lobby_state(transport.sent_messages().front());
    EXPECT_TRUE(promoted.players.empty());
    EXPECT_TRUE(promoted.local_peer_is_host);
}

TEST(LobbyServer, blank_multiseat_rejoin_and_reconnect_preserve_identity)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);

    og::sim::LobbyMessage join = make_join_message(
        "",
        0,
        {make_slot(0u, 100, "First", FAMILY_SOLDIER),
         make_slot(0u, 999, "Duplicate", FAMILY_MAGE)});
    og::sim::LobbyPlayer extra;
    extra.team = 0;
    extra.character_slots = {
        make_slot(1u, 101, "Second", FAMILY_ARCHER),
    };
    std::get<og::sim::LobbyJoinMessage>(join.payload)
        .extra_players.push_back(extra);

    transport.queue_lobby_message(11u, join);
    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ("Player 1", server.state().players[0].name);
    EXPECT_EQ("Player 1#1", server.state().players[1].name);
    ASSERT_EQ(1u, server.state().players[0].character_slots.size());
    EXPECT_EQ(100, server.state().players[0].character_slots[0].character.guy_id)
        << "duplicate save-slot declarations keep the first character";
    const og::sim::LobbyState first_state = server.state();

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, join);
    server.poll_incoming_messages();
    ASSERT_EQ(2u, server.state().players.size());
    EXPECT_EQ("Player 1", server.state().players[0].name);
    EXPECT_EQ("Player 1#1", server.state().players[1].name);
    EXPECT_EQ(first_state.players[0].seat_id,
              server.state().players[0].seat_id);
    EXPECT_EQ(first_state.players[1].seat_id,
              server.state().players[1].seat_id);

    const og::sim::LobbyState before_reconnect = server.state();
    transport.clear_sent_messages();
    server.connect_client(11u);
    EXPECT_EQ(before_reconnect, server.state());
    // v10: the reconnect echo is the LobbyState plus the (empty) class-pack
    // manifest announcement.
    ASSERT_EQ(2u, transport.sent_messages().size());
    ASSERT_TRUE(og::sim::deserialize_pack_manifest_message(
                    transport.sent_messages()[1].data)
                    .has_value());
    const og::sim::LobbyState reconnect_echo =
        decode_lobby_state(transport.sent_messages().front());
    EXPECT_EQ((std::vector<og::sim::LobbySeatId>{
                  before_reconnect.players[0].seat_id,
                  before_reconnect.players[1].seat_id}),
              reconnect_echo.local_seat_ids);

    transport.clear_sent_messages();
    server.disconnect_client(99u);
    EXPECT_EQ((std::vector<og::sim::PeerId>{99u}),
              transport.disconnected_peers());
    EXPECT_EQ(before_reconnect, server.state());
    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(LobbyServer, unjoined_peer_is_not_a_player_or_ready_blocker)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(22u);
    transport.clear_sent_messages();

    transport.queue_lobby_message(
        11u,
        make_join_message(
            "Host",
            0,
            {make_slot(0u, 100, "Host Guy", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const og::sim::LobbyState echoed = decode_lobby_state(sent);
        ASSERT_EQ(1u, echoed.players.size());
        EXPECT_EQ("Host", echoed.players.front().name);
        if (sent.peer_id == 22u)
        {
            EXPECT_TRUE(echoed.local_seat_ids.empty());
        }
    }

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_start_message(0u, 700u));
    server.poll_incoming_messages();

    EXPECT_TRUE(server.start_game_requested());
    ASSERT_EQ(2u, transport.sent_messages().size());
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        const og::sim::LobbyMessage confirmation = decode_lobby_message(sent);
        ASSERT_EQ(og::sim::LobbyMessageKind::StartGame,
                  confirmation.kind());
        EXPECT_EQ(700u,
                  std::get<og::sim::LobbyStartGameMessage>(
                      confirmation.payload)
                      .request_id);
    }
}

TEST(LobbyServer, shared_team_fallback_handles_invalid_and_benched_leaders)
{
    og::sim::LobbyState state;
    og::sim::LobbyPlayer leader;
    leader.team = -1;
    state.players.push_back(leader);
    EXPECT_EQ(0, og::sim::shared_allied_gameplay_team(state));

    state.players.front().team = 3;
    og::sim::LobbyCharacterSlot benched =
        make_slot(0u, 100, "Reserve", FAMILY_SOLDIER, 3);
    benched.deployed = false;
    state.players.front().character_slots = {benched};
    EXPECT_EQ(3, og::sim::shared_allied_gameplay_team(state));
}

// Staged-lobby hardening (#218): under staging, lobby rosters become
// world-construction inputs, so the merge must bound what it accepts.

// Two hostile peers with ~33 KB player names each used to push the merged
// LobbyState past the u16 transport cap; serialize_lobby_state_message then
// threw out of broadcast_state — on openglad_server, a process exit. The
// read-side clamps shrink hostile names before they ever merge.
TEST(LobbyServer, oversize_hostile_names_cannot_throw_out_of_broadcast)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    transport.set_connected_peers({1u, 2u});

    const std::string huge_name(33000, 'A');
    for (const og::sim::PeerId peer : {1u, 2u})
    {
        og::sim::LobbyPlayer player;
        player.name = huge_name;
        player.team = peer == 1u ? 0 : 1;
        player.character_slots = {make_slot(0u, 1, "Guy", FAMILY_SOLDIER)};
        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyJoinMessage{.player = player};
        transport.queue_lobby_message(peer, message);
    }
    ASSERT_NO_THROW(server.poll_incoming_messages());

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(2u, state.players.size());
    for (const og::sim::LobbyPlayer& seat : state.players)
        EXPECT_EQ(std::string(og::sim::kMaxLobbyCompanyNameLength, 'A'),
                  seat.name);
    EXPECT_TRUE(transport.disconnected_peers().empty());
}

// Defense in depth: content that bypasses the wire readers entirely (typed
// in-process messages) can still assemble an oversize state. The serialize
// guard in broadcast_state/send_state must log and skip the send — never
// throw out of poll_incoming_messages. Names no longer work as the oversize
// vehicle (the merge clamps them for typed content too), so drive it through
// the one string sanitize leaves unbounded: a host-authored campaign id.
TEST(LobbyServer, broadcast_serialize_guard_swallows_oversize_state)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    server.connect_client(1u);
    server.connect_client(2u);

    transport.queue_lobby_message(
        1u, make_join_message("Host", 0,
                              {make_slot(0u, 1, "Guy", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        2u, make_join_message("Guest", 1,
                              {make_slot(0u, 2, "Guy", FAMILY_SOLDIER)}));
    ASSERT_NO_THROW(server.poll_incoming_messages());
    ASSERT_EQ(2u, server.state().players.size());
    transport.clear_sent_messages();

    og::sim::LobbySettings oversize = server.state().settings;
    oversize.campaign_id = std::string(70000, 'B');
    og::sim::LobbyMessage settings_message;
    settings_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = oversize,
    };
    transport.queue_lobby_message(1u, settings_message);
    ASSERT_NO_THROW(server.poll_incoming_messages());

    // The vehicle worked: the merged state really is oversize...
    ASSERT_EQ(70000u, server.state().settings.campaign_id.size());
    // ...and its broadcast was skipped, not sent and not fatal.
    std::size_t states_sent = 0;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        if (og::sim::deserialize_lobby_state_message(sent.data).has_value())
            ++states_sent;
    }
    EXPECT_EQ(0u, states_sent);
    EXPECT_TRUE(transport.disconnected_peers().empty());
}

// Family and level become world-construction inputs at stage time: a slot
// whose family is outside [0, NUM_FAMILY_SLOTS) is dropped at the merge and
// a non-positive level clamps to 1. Stats/exp stay accepted-as-sent (the
// same trust as a local save file). The out-of-range families only exist on
// the typed in-process channel (the wire byte is always in range), which is
// exactly why the merge must bound them itself — and why the bound must
// compare at a width that can actually hold a value >= NUM_FAMILY_SLOTS
// (an int8_t field made the upper arm a tautology and aliased real pack
// slots >= 128 to negatives).
TEST(LobbyServer, sanitize_drops_invalid_family_and_clamps_level)
{
    // Typed delivery: the raw path's wire byte can never carry an
    // out-of-range family, so the merge bound is only observable here.
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    transport.set_connected_peers({1u});

    og::sim::LobbyCharacterSlot bad_family =
        make_slot(0u, 1, "BadFam", -3);
    og::sim::LobbyCharacterSlot zero_level =
        make_slot(1u, 2, "ZeroLvl", FAMILY_SOLDIER);
    zero_level.character.level = 0;
    og::sim::LobbyCharacterSlot negative_level =
        make_slot(2u, 3, "NegLvl", FAMILY_ELF);
    negative_level.character.level = -7;
    // Above the registry capacity: must be DROPPED by the upper bound.
    og::sim::LobbyCharacterSlot oversized_family =
        make_slot(3u, 4, "BigFam",
                  static_cast<std::int16_t>(NUM_FAMILY_SLOTS + 44));
    // A class-pack loader slot in the 128..255 half of the byte: must be
    // KEPT, at its real id.
    og::sim::LobbyCharacterSlot pack_family = make_slot(4u, 5, "PackFam", 200);

    transport.queue_lobby_message(
        1u,
        make_join_message("Player", 0,
                          {bad_family, zero_level, negative_level,
                           oversized_family, pack_family}));
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(1u, state.players.size());
    const auto& slots = state.players.front().character_slots;
    ASSERT_EQ(3u, slots.size());
    EXPECT_EQ("ZeroLvl", slots[0].character.name);
    EXPECT_EQ(1, slots[0].character.level);
    EXPECT_EQ("NegLvl", slots[1].character.name);
    EXPECT_EQ(1, slots[1].character.level);
    EXPECT_EQ("PackFam", slots[2].character.name);
    EXPECT_EQ(200, slots[2].character.family);
}

// Typed in-process joins never meet the wire readers, so the merge itself
// must bound the display strings the state re-serializes to every peer
// (unbounded names can push the merged LobbyState past the u16 transport
// cap).
TEST(LobbyServer, typed_join_clamps_guy_and_player_names_at_the_merge)
{
    MockLobbyTransport transport(true);
    og::sim::LobbyServer server(transport);
    transport.set_connected_peers({1u});

    og::sim::LobbyCharacterSlot long_named =
        make_slot(0u, 1, "Guy", FAMILY_SOLDIER);
    long_named.character.name = std::string(64, 'G');

    og::sim::LobbyMessage join =
        make_join_message("Player", 0, {long_named});
    auto& join_player = std::get<og::sim::LobbyJoinMessage>(join.payload).player;
    join_player.name = std::string(50, 'N');
    join_player.company = std::string(50, 'C');

    transport.queue_lobby_message(1u, join);
    server.poll_incoming_messages();

    const og::sim::LobbyState& state = server.state();
    ASSERT_EQ(1u, state.players.size());
    const og::sim::LobbyPlayer& stored = state.players.front();
    EXPECT_EQ(std::string(og::sim::kMaxLobbyCompanyNameLength, 'N'),
              stored.name);
    EXPECT_EQ(std::string(og::sim::kMaxLobbyCompanyNameLength, 'C'),
              stored.company);
    ASSERT_EQ(1u, stored.character_slots.size());
    EXPECT_EQ(std::string(og::sim::kMaxLobbyGuyNameLength, 'G'),
              stored.character_slots.front().character.name);
}

// Restage determinism input: preview and launch derive gameplay guy ids
// independently, so the canonicalization must be a pure function of the
// stable slot order — same input, same output, and idempotent.
TEST(LobbyState, canonicalize_guy_ids_is_deterministic_and_exact)
{
    std::vector<og::sim::LobbyCharacterSlot> slots = {
        make_slot(0u, 5, "A", FAMILY_SOLDIER),
        make_slot(1u, 5, "B", FAMILY_SOLDIER),
        make_slot(2u, -1, "C", FAMILY_SOLDIER),
        make_slot(3u, 3, "D", FAMILY_SOLDIER),
    };
    std::vector<og::sim::LobbyCharacterSlot> twin = slots;

    og::sim::canonicalize_lobby_gameplay_guy_ids(slots);
    og::sim::canonicalize_lobby_gameplay_guy_ids(twin);
    ASSERT_EQ(4u, slots.size());
    EXPECT_EQ(5, slots[0].character.guy_id);
    EXPECT_EQ(0, slots[1].character.guy_id);
    EXPECT_EQ(1, slots[2].character.guy_id);
    EXPECT_EQ(3, slots[3].character.guy_id);
    EXPECT_EQ(slots, twin);

    std::vector<og::sim::LobbyCharacterSlot> again = slots;
    og::sim::canonicalize_lobby_gameplay_guy_ids(again);
    EXPECT_EQ(slots, again);
}

// --- LINEUP §3.1: the eight per-team bot knobs ---------------------------

TEST(LobbyServer, sanitize_clamps_bot_knobs_and_equivalent_carries_them)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings wild;
    wild.campaign_id = "modes";
    wild.scenario_id = 820;
    wild.difficulty = 1;
    wild.allied_mode = 1;
    // Out of range in both directions, and one legal value per array so a
    // clamp that flattened everything would be caught.
    wild.fill = {-4, 3, 99, 0};
    wild.map_units = {-400, 4, 400, 0};
    og::sim::LobbyMessage wild_message;
    wild_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = wild,
    };
    transport.queue_lobby_message(11u, wild_message);
    server.poll_incoming_messages();

    const og::sim::LobbyState state = server.state();
    // fill clamps into [0, kMaxFill] and map_units into [0, kMaxMapUnits]
    // (amendment B1-B4); both floors are 0, so a negative has a floor to
    // hit as well as a ceiling.
    const std::array<std::int16_t, 4> expected_fill = {
        0, 3, og::sim::kMaxFill, 0};
    const std::array<std::int16_t, 4> expected_map_units = {
        0, og::sim::kMaxMapUnits, og::sim::kMaxMapUnits, 0};
    EXPECT_EQ(expected_fill, state.settings.fill);
    EXPECT_EQ(expected_map_units, state.settings.map_units);

    // The launch equivalent is a hand-written field list — the documented
    // dropped-field bug class.
    const og::sim::LobbySaveDataEquivalent equivalent =
        server.build_save_data_equivalent();
    EXPECT_EQ(expected_fill, equivalent.fill);
    EXPECT_EQ(expected_map_units, equivalent.map_units);
}

TEST(LobbyServer, bot_knobs_replicate_to_joiners_and_a_joiner_change_is_dropped)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u); // first connection is elected host
    server.connect_client(12u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        12u,
        make_join_message("Guest", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    og::sim::LobbySettings hosted;
    hosted.campaign_id = "modes";
    hosted.scenario_id = 820;
    hosted.difficulty = 1;
    hosted.allied_mode = 1;
    hosted.fill = {0, 2, 1, 4};
    hosted.map_units = {0, 1, 1, 0};
    og::sim::LobbyMessage hosted_message;
    hosted_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0u,
        .settings = hosted,
    };
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, hosted_message);
    server.poll_incoming_messages();

    // The joiner's replicated copy comes off the wire, not the server's
    // in-memory struct: this is the round trip a missing serializer field
    // would break.
    bool guest_saw_the_knobs = false;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        if (sent.peer_id != 12u)
            continue;
        const auto decoded =
            og::sim::deserialize_lobby_state_message(sent.data);
        if (!decoded.has_value())
            continue;
        guest_saw_the_knobs =
            decoded->settings.fill == hosted.fill &&
            decoded->settings.map_units == hosted.map_units;
        if (guest_saw_the_knobs)
            break;
    }
    EXPECT_TRUE(guest_saw_the_knobs)
        << "the host's band knobs must reach the joiner over the wire";

    // A non-host SettingsChange is ignored outright (the historic rule).
    og::sim::LobbySettings guest_attempt = hosted;
    guest_attempt.fill = {1, 1, 1, 1};
    guest_attempt.map_units = {1, 1, 1, 1};
    og::sim::LobbyMessage guest_message;
    guest_message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 1u,
        .settings = guest_attempt,
    };
    transport.queue_lobby_message(12u, guest_message);
    server.poll_incoming_messages();
    EXPECT_EQ(hosted.fill, server.state().settings.fill);
    EXPECT_EQ(hosted.map_units, server.state().settings.map_units);
}

// --- LINEUP §6: kick by machine id ---------------------------------------

namespace {

og::sim::LobbyMessage make_kick_message(og::sim::LobbyMachineId machine_id)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyKickMessage{.machine_id = machine_id};
    return message;
}

// The machine id the server issued to the peer whose seats carry `name`.
og::sim::LobbyMachineId machine_id_of(const og::sim::LobbyState& state,
                                      const std::string& name)
{
    for (const og::sim::LobbyPlayer& player : state.players)
    {
        if (player.name == name)
            return player.machine_id;
    }
    return og::sim::kInvalidLobbyMachineId;
}

std::size_t index_of_kicked_message(const MockLobbyTransport& transport,
                                    og::sim::PeerId peer_id)
{
    const std::vector<og::sim::ReceivedMessage>& sent =
        transport.sent_messages();
    for (std::size_t i = 0; i < sent.size(); ++i)
    {
        if (sent[i].peer_id != peer_id)
            continue;
        const auto decoded = og::sim::deserialize_lobby_message(sent[i].data);
        if (decoded.has_value() &&
            decoded->kind() == og::sim::LobbyMessageKind::Kicked)
        {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

} // namespace

TEST(LobbyServer, host_kick_removes_every_seat_of_the_machine_and_reindexes)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u); // host
    server.connect_client(12u); // guest with TWO seats
    server.connect_client(13u); // bystander

    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    og::sim::LobbyMessage guest_join =
        make_join_message("Guest", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)});
    {
        og::sim::LobbyPlayer second_seat;
        second_seat.name = "Guest";
        second_seat.team = 1;
        second_seat.character_slots = {
            make_slot(1u, 201, "Mage", FAMILY_MAGE)};
        std::get<og::sim::LobbyJoinMessage>(guest_join.payload)
            .extra_players.push_back(std::move(second_seat));
    }
    transport.queue_lobby_message(12u, guest_join);
    transport.queue_lobby_message(
        13u,
        make_join_message("Third", 2,
                          {make_slot(0u, 300, "Thief", FAMILY_THIEF)}));
    server.poll_incoming_messages();

    ASSERT_EQ(4u, server.state().players.size())
        << "host + two guest seats + bystander";
    const og::sim::LobbyMachineId guest_machine =
        machine_id_of(server.state(), "Guest");
    ASSERT_NE(og::sim::kInvalidLobbyMachineId, guest_machine);

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_kick_message(guest_machine));
    server.poll_incoming_messages();

    const og::sim::LobbyState after = server.state();
    EXPECT_EQ(2u, after.players.size()) << "both guest seats leave together";
    for (const og::sim::LobbyPlayer& player : after.players)
        EXPECT_NE("Guest", player.name);
    // The survivors re-index densely: the bystander inherits P2.
    ASSERT_EQ(2u, after.players.size());
    EXPECT_EQ(0u, after.players[0].player_index);
    EXPECT_EQ(1u, after.players[1].player_index);
    EXPECT_EQ("Third", after.players[1].name);

    EXPECT_NE(transport.disconnected_peers().end(),
              std::find(transport.disconnected_peers().begin(),
                        transport.disconnected_peers().end(),
                        og::sim::PeerId{12u}))
        << "the kicked machine's transport connection is dropped";
}

TEST(LobbyServer, kicked_notice_precedes_the_disconnect)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(12u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        12u,
        make_join_message("Guest", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    const og::sim::LobbyMachineId guest_machine =
        machine_id_of(server.state(), "Guest");
    ASSERT_NE(og::sim::kInvalidLobbyMachineId, guest_machine);

    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_kick_message(guest_machine));
    server.poll_incoming_messages();

    const std::size_t kicked_index = index_of_kicked_message(transport, 12u);
    ASSERT_NE(std::numeric_limits<std::size_t>::max(), kicked_index)
        << "the kicked peer must receive a Kicked notice";
    const std::optional<std::size_t> sent_at_disconnect =
        transport.sent_count_at_disconnect(12u);
    ASSERT_TRUE(sent_at_disconnect.has_value());
    EXPECT_LT(kicked_index, *sent_at_disconnect)
        << "the notice must be queued BEFORE the disconnect, or the peer "
           "never receives it and can only render a bare link failure";
}

TEST(LobbyServer, non_host_kick_is_denied_and_echoes_state)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u); // host
    server.connect_client(12u); // guest
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        12u,
        make_join_message("Guest", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    const og::sim::LobbyMachineId host_machine =
        machine_id_of(server.state(), "Host");
    ASSERT_NE(og::sim::kInvalidLobbyMachineId, host_machine);

    transport.clear_sent_messages();
    transport.queue_lobby_message(12u, make_kick_message(host_machine));
    server.poll_incoming_messages();

    EXPECT_EQ(2u, server.state().players.size())
        << "a guest cannot kick the host";
    EXPECT_TRUE(transport.disconnected_peers().empty());
    EXPECT_EQ(std::numeric_limits<std::size_t>::max(),
              index_of_kicked_message(transport, 11u));

    // The refusal answers with a state echo to the requester, so a client
    // detects the bounce without waiting on a timeout.
    bool echoed_to_requester = false;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        if (sent.peer_id == 12u &&
            og::sim::deserialize_lobby_state_message(sent.data).has_value())
        {
            echoed_to_requester = true;
        }
    }
    EXPECT_TRUE(echoed_to_requester);
}

// LINEUP §6: the kick gate follows the host, it does not follow the peer that
// opened the lobby. After a host disconnect the promoted survivor's kick is
// honoured — which is exactly the authority the clients now grant an ELECTED
// host (a join client on a dedicated server, or one promoted here).
TEST(LobbyServer, elected_host_may_kick_after_migration)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u); // opening host
    server.connect_client(12u); // the successor
    server.connect_client(13u); // the peer the successor will remove
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        12u,
        make_join_message("Second", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)}));
    transport.queue_lobby_message(
        13u,
        make_join_message("Third", 2,
                          {make_slot(0u, 300, "Thief", FAMILY_THIEF)}));
    server.poll_incoming_messages();
    ASSERT_EQ(3u, server.state().players.size());

    const og::sim::LobbyMachineId third_machine =
        machine_id_of(server.state(), "Third");
    ASSERT_NE(og::sim::kInvalidLobbyMachineId, third_machine);

    // Before migration the successor is an ordinary guest: refused.
    transport.queue_lobby_message(12u, make_kick_message(third_machine));
    server.poll_incoming_messages();
    EXPECT_EQ(3u, server.state().players.size())
        << "a guest's kick is refused however soon it will be host";

    server.disconnect_client(11u);
    ASSERT_EQ(2u, server.state().players.size());
    ASSERT_EQ("Second", server.state().players[0].name);
    ASSERT_TRUE(server.state().players[0].is_host)
        << "the survivor is promoted";

    transport.clear_sent_messages();
    transport.queue_lobby_message(12u, make_kick_message(third_machine));
    server.poll_incoming_messages();

    EXPECT_EQ(1u, server.state().players.size())
        << "the elected host's kick is honoured";
    EXPECT_EQ("Second", server.state().players[0].name);
    EXPECT_NE(std::numeric_limits<std::size_t>::max(),
              index_of_kicked_message(transport, 13u))
        << "and the removed peer still learns why";
}

TEST(LobbyServer, kick_of_self_or_unknown_machine_echoes_state_only)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(12u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        12u,
        make_join_message("Guest", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    const og::sim::LobbyMachineId host_machine =
        machine_id_of(server.state(), "Host");

    // Kicking yourself is not a kick — leaving is DISCONNECT.
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_kick_message(host_machine));
    server.poll_incoming_messages();
    EXPECT_EQ(2u, server.state().players.size());
    EXPECT_TRUE(transport.disconnected_peers().empty());

    // An id nobody holds (including the invalid sentinel) is a no-op echo.
    transport.clear_sent_messages();
    transport.queue_lobby_message(11u, make_kick_message(0x7fffffffu));
    transport.queue_lobby_message(
        11u, make_kick_message(og::sim::kInvalidLobbyMachineId));
    server.poll_incoming_messages();
    EXPECT_EQ(2u, server.state().players.size());
    EXPECT_TRUE(transport.disconnected_peers().empty());
    bool echoed = false;
    for (const og::sim::ReceivedMessage& sent : transport.sent_messages())
    {
        if (sent.peer_id == 11u &&
            og::sim::deserialize_lobby_state_message(sent.data).has_value())
        {
            echoed = true;
        }
    }
    EXPECT_TRUE(echoed);
}

TEST(LobbyServer, a_kicked_message_reaching_the_server_is_ignored)
{
    MockLobbyTransport transport;
    og::sim::LobbyServer server(transport);
    server.connect_client(11u);
    server.connect_client(12u);
    transport.queue_lobby_message(
        11u,
        make_join_message("Host", 0,
                          {make_slot(0u, 100, "Soldier", FAMILY_SOLDIER)}));
    transport.queue_lobby_message(
        12u,
        make_join_message("Guest", 1,
                          {make_slot(0u, 200, "Archer", FAMILY_ARCHER)}));
    server.poll_incoming_messages();

    // Kicked is a server->peer notice. A peer echoing it back must not
    // disturb the lobby (a crafted client is the only sender).
    og::sim::LobbyMessage kicked;
    kicked.payload = og::sim::LobbyKickedMessage{};
    transport.queue_lobby_message(12u, kicked);
    transport.queue_lobby_message(11u, kicked);
    server.poll_incoming_messages();

    EXPECT_EQ(2u, server.state().players.size());
    EXPECT_TRUE(transport.disconnected_peers().empty());
}
