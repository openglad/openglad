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
    std::vector<LobbyCharacterSlot> team_list;

    bool operator==(const LobbySaveDataEquivalent&) const = default;
};

class LobbyServer
{
public:
    explicit LobbyServer(ITransport& transport);

    void connect_client(PeerId peer_id);
    void disconnect_client(PeerId peer_id);
    void poll_incoming_messages();

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

private:
    struct ConnectedPeerState {
        std::uint64_t connection_order = 0;
        std::optional<LobbyPlayer> player = std::nullopt;
    };

    [[nodiscard]] bool is_team_available(std::int16_t team,
                                         PeerId peer_id) const noexcept;
    [[nodiscard]] std::int16_t resolve_team(
        PeerId peer_id,
        std::int16_t requested_team,
        std::optional<std::int16_t> current_team) const noexcept;
    void rebuild_state();
    void send_state(PeerId peer_id) const;
    void broadcast_state() const;
    void broadcast_start_game(std::uint8_t player_index) const;
    void reassign_host_peer();
    void process_lobby_message(PeerId peer_id, const LobbyMessage& message);

    ITransport& transport_;
    LobbyState state_;
    std::unordered_map<PeerId, ConnectedPeerState> peers_;
    std::optional<PeerId> host_peer_id_ = std::nullopt;
    std::uint64_t next_connection_order_ = 1;
    bool start_game_requested_ = false;
    bool lobby_locked_ = false;
};

} // namespace og::sim
