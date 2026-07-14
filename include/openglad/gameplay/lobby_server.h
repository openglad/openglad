#pragma once

#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace og::sim {

struct LobbySaveDataEquivalent {
    std::string current_campaign = "org.openglad.gladiator";
    std::int16_t scen_num = 1;
    std::uint8_t numplayers = 0;
    std::int16_t allied_mode = 1;
    std::int16_t ctf_team_count = 0; // 0 = Auto
    std::int16_t ctf_capture_limit = 0;
    std::int16_t ctf_respawn_ticks = 0;
    std::int16_t ctf_strip_scenario_troops = 0;
    // Difficulty submenu settings (0 = legacy default behavior for all three).
    std::int16_t respawn_mode = 0;
    std::int16_t generator_rate = 0;
    std::int16_t keep_fallen_heroes = 0;
    std::vector<LobbyCharacterSlot> team_list;

    bool operator==(const LobbySaveDataEquivalent&) const = default;
};

struct LobbyPlayerBinding {
    PeerId peer_id = 0;
    std::uint8_t player_index = 0xff;
    std::int16_t team = 0;

    bool operator==(const LobbyPlayerBinding&) const = default;
};

class LobbyServer
{
public:
    // `local_session` marks a single-machine lobby (the solo picker's
    // in-process settings echo): local-only mode campaigns (the Endless
    // Tower) survive sanitize_settings there. The default (false) keeps the
    // crafted-client rejection backstop for every networked construction
    // site (tower-triple §5.9 layer 3) — a networked host must opt nothing
    // in to stay protected.
    explicit LobbyServer(ITransport& transport, bool local_session = false);

    void connect_client(PeerId peer_id);
    void disconnect_client(PeerId peer_id);
    void poll_incoming_messages();

    // Re-open the lobby after a level (return-to-lobby between levels). Starting
    // a game locks the lobby (lobby_locked_) so no further roster/settings
    // changes are accepted; that lock is permanent in the original one-shot flow
    // where the server is destroyed at game start. When the connection persists
    // across gameplay, the same server runs the NEXT level's lobby, so the lock
    // must be cleared to accept the new round's settings/joins.
    void unlock_for_new_round() noexcept
    {
        lobby_locked_ = false;
        start_game_requested_ = false;
    }

    [[nodiscard]] const LobbyState& state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] bool start_game_requested() const noexcept
    {
        return lobby_locked_;
    }

    [[nodiscard]] bool consume_start_game_requested() noexcept;
    [[nodiscard]] LobbySaveDataEquivalent build_save_data_equivalent() const;
    [[nodiscard]] std::vector<LobbyPlayerBinding> build_player_bindings() const;

private:
    void synchronize_transport_peers(
        const std::vector<std::pair<PeerId, LobbyMessage>>& messages);
    void apply_transport_disconnects();
    struct ConnectedPeerState {
        std::uint64_t connection_order = 0;
        std::optional<LobbyPlayer> player = std::nullopt;
    };

    [[nodiscard]] std::int16_t effective_team_limit() const noexcept;
    [[nodiscard]] bool is_team_available(std::int16_t team,
                                         PeerId peer_id) const noexcept;
    [[nodiscard]] std::int16_t resolve_team(
        PeerId peer_id,
        std::int16_t requested_team,
        std::optional<std::int16_t> current_team) const noexcept;
    [[nodiscard]] std::size_t remaining_team_capacity(PeerId peer_id) const noexcept;
    void rebuild_state();
    void send_state(PeerId peer_id) const;
    void broadcast_state() const;
    void broadcast_start_game(std::uint8_t player_index) const;
    void reassign_host_peer();
    void process_lobby_message(PeerId peer_id, const LobbyMessage& message);

    ITransport& transport_;
    bool local_session_ = false;
    LobbyState state_;
    std::vector<PeerId> connected_transport_peers_;
    std::vector<PeerId> pending_transport_disconnects_;
    std::unordered_map<PeerId, ConnectedPeerState> peers_;
    std::optional<PeerId> host_peer_id_ = std::nullopt;
    std::uint64_t next_connection_order_ = 1;
    bool start_game_requested_ = false;
    bool lobby_locked_ = false;
};

} // namespace og::sim
