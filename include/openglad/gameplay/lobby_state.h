#pragma once

#include <openglad/core/ctf_constants.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace og::sim {

// Maximum company display-name length preserved by the lobby wire (matches
// the 40-byte SaveData::save_name field the name comes from). The lobby-state
// reader clamps longer strings instead of rejecting them.
inline constexpr std::size_t kMaxLobbyCompanyNameLength = 40;

// Reason a StartGame request was denied by the LobbyServer (protocol v8,
// company-basecamp design §4.3). Serialized as the u8 LobbyState
// last_start_denial echo; None (0) means "no denial recorded".
enum class StartDenialReason : std::uint8_t {
    None = 0,
    NotHost = 1,
    MachinesNotReady = 2,
    NoDeployedCharacters = 3,
};

constexpr std::uint8_t start_denial_reason_value(StartDenialReason reason) noexcept
{
    return static_cast<std::uint8_t>(reason);
}

struct LobbyCharacterData {
    std::int32_t guy_id = 0;
    std::string name;
    std::int8_t family = 0;
    std::int16_t strength = 0;
    std::int16_t dexterity = 0;
    std::int16_t constitution = 0;
    std::int16_t intelligence = 0;
    std::int16_t armor = 0;
    std::uint32_t exp = 0;
    std::int16_t kills = 0;
    std::int32_t level_kills = 0;
    std::int32_t total_damage = 0;
    std::int32_t total_hits = 0;
    std::int32_t total_shots = 0;
    std::int16_t teamnum = 0;
    float scen_damage = 0.0f;
    std::int16_t scen_kills = 0;
    float scen_damage_taken = 0.0f;
    float scen_min_hp = 0.0f;
    std::int16_t scen_shots = 0;
    std::int16_t scen_hits = 0;
    std::int16_t level = 0;

    bool operator==(const LobbyCharacterData&) const = default;
};

struct LobbyCharacterSlot {
    // Slot index maps back to SaveData::team_list when the lobby is applied.
    std::uint8_t slot_index = 0;
    LobbyCharacterData character;
    // Mission-roster selection (protocol v8): whether the owning machine
    // brings this character into the next level. Serialized as bit0 of the
    // slot_flags byte (the writer zeroes bits 1-7; the reader masks bit0 and
    // ignores the rest). Stamped from the save guy's v14 deploy flag by the
    // three slot builders.
    bool deployed = true;
    // Player slot that owns this character (0xff == unowned). Populated only
    // when assembling the combined game-start roster; NOT part of the lobby
    // wire format (the owning player is implicit in the lobby state). Used so
    // each player persists only its own characters.
    std::uint8_t owner_player_index = 0xff;
    // The owning player's own save0 team slot (slot_index gets re-densified for
    // the combined roster, so the original is preserved here). Assembly-only;
    // not part of the lobby wire format.
    std::uint8_t owner_save_slot = 0xff;

    bool operator==(const LobbyCharacterSlot&) const = default;
};

struct LobbyPlayer {
    std::uint8_t player_index = 0xff;
    std::string name;
    // The owning machine's active-company display name (protocol v8),
    // identical across all of one machine's seats. Serialized after name;
    // the reader clamps it to kMaxLobbyCompanyNameLength chars.
    std::string company;
    std::int16_t team = 0;
    std::vector<LobbyCharacterSlot> character_slots;
    bool ready = false;
    bool is_host = false;

    bool operator==(const LobbyPlayer&) const = default;
};

struct LobbySettings {
    std::string campaign_id;
    std::int16_t scenario_id = 0;
    std::int16_t difficulty = 0;
    std::int16_t allied_mode = 0;
    // CTF match settings; only TYPE_CTF maps consume them (0 = map/default).
    std::int16_t ctf_team_count = 0; // 0 = Auto
    std::int16_t ctf_capture_limit = 0;
    std::int16_t ctf_respawn_ticks = 0;
    std::int16_t ctf_strip_scenario_troops = 0; // 0 = keep authored troops
    // Difficulty submenu settings (0 = legacy default behavior for all three).
    std::int16_t respawn_mode = 0;       // 0 = off, 1 = heroes, 2 = everyone
    std::int16_t generator_rate = 0;     // percent; 0 = default (100)
    std::int16_t keep_fallen_heroes = 0; // 0 = permadeath on win (classic)
    // Host-only cross-control setting (protocol v8, company-basecamp design
    // §4.1/§4.4): 0 = owner-locked control in networked levels (default),
    // 1 = players may control other machines' characters. sanitize_settings
    // keeps it in {0, 1}.
    std::int16_t cross_control = 0;

    bool operator==(const LobbySettings&) const = default;
};

// CTF lobbies let multiple humans share a team (the mode has respawns and
// per-team flags); classic lobbies keep one human per team.
inline bool lobby_settings_allow_shared_teams(const LobbySettings& settings) noexcept
{
    return settings.campaign_id == og::kCtfCampaignId;
}

// Cross-peer team sharing: CTF campaigns (as before) plus allied lobbies,
// where everyone plays one gameplay team anyway. Non-allied classic lobbies
// keep one seat per team, which caps them at MAX_PLAYERS seats TOTAL (a
// rules-driven cap, not an array cap). Within one peer, seats always keep
// DISTINCT lobby teams regardless of this rule.
inline bool lobby_teams_shareable(const LobbySettings& settings) noexcept
{
    return lobby_settings_allow_shared_teams(settings) ||
        settings.allied_mode != 0;
}

struct LobbyState {
    LobbySettings settings;
    std::uint8_t host_player_id = 0xff;
    // Echo of the most recent StartGame denial (protocol v8, [NET-R3]):
    // 0 = none, else a StartDenialReason value. Recorded by the LobbyServer
    // so every peer (including a remote host elected on a dedicated server)
    // can render the precise reason instead of a poll timeout.
    std::uint8_t last_start_denial = 0;
    std::vector<LobbyPlayer> players;

    bool operator==(const LobbyState&) const = default;
};

enum class LobbyMessageKind : std::uint8_t {
    Join = 1,
    Leave = 2,
    Ready = 3,
    TeamChange = 4,
    StartGame = 5,
    SettingsChange = 6,
};

constexpr std::uint8_t lobby_message_kind_value(LobbyMessageKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind);
}

struct LobbyJoinMessage {
    // Seat 0 of the joining peer. A join REPLACES the peer's whole seat list.
    LobbyPlayer player;
    // Seats 1..N-1 of the joining peer (empty for single-seat clients, so the
    // curses/text clients and pre-seat callers behave unchanged). Per-machine
    // seat count is capped at MAX_PLAYERS.
    std::vector<LobbyPlayer> extra_players = {};

    bool operator==(const LobbyJoinMessage&) const = default;
};

struct LobbyLeaveMessage {
    std::uint8_t player_index = 0xff;

    bool operator==(const LobbyLeaveMessage&) const = default;
};

struct LobbyReadyMessage {
    std::uint8_t player_index = 0xff;
    bool ready = false;

    bool operator==(const LobbyReadyMessage&) const = default;
};

struct LobbyTeamChangeMessage {
    std::uint8_t player_index = 0xff;
    std::int16_t team = 0;

    bool operator==(const LobbyTeamChangeMessage&) const = default;
};

struct LobbyStartGameMessage {
    std::uint8_t player_index = 0xff;

    bool operator==(const LobbyStartGameMessage&) const = default;
};

struct LobbySettingsChangeMessage {
    std::uint8_t player_index = 0xff;
    LobbySettings settings;

    bool operator==(const LobbySettingsChangeMessage&) const = default;
};

using LobbyMessagePayload =
    std::variant<LobbyJoinMessage,
                 LobbyLeaveMessage,
                 LobbyReadyMessage,
                 LobbyTeamChangeMessage,
                 LobbyStartGameMessage,
                 LobbySettingsChangeMessage>;

struct LobbyMessage {
    LobbyMessagePayload payload = LobbyJoinMessage{};

    [[nodiscard]] LobbyMessageKind kind() const noexcept
    {
        return std::visit(
            [](const auto& message) -> LobbyMessageKind {
                using Message = std::decay_t<decltype(message)>;
                if constexpr (std::is_same_v<Message, LobbyJoinMessage>)
                    return LobbyMessageKind::Join;
                if constexpr (std::is_same_v<Message, LobbyLeaveMessage>)
                    return LobbyMessageKind::Leave;
                if constexpr (std::is_same_v<Message, LobbyReadyMessage>)
                    return LobbyMessageKind::Ready;
                if constexpr (std::is_same_v<Message, LobbyTeamChangeMessage>)
                    return LobbyMessageKind::TeamChange;
                if constexpr (std::is_same_v<Message, LobbyStartGameMessage>)
                    return LobbyMessageKind::StartGame;
                return LobbyMessageKind::SettingsChange;
            },
            payload);
    }

    bool operator==(const LobbyMessage&) const = default;
};

} // namespace og::sim
