#pragma once

#include <openglad/core/ctf_constants.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace og::sim {

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

    bool operator==(const LobbySettings&) const = default;
};

// CTF lobbies let multiple humans share a team (the mode has respawns and
// per-team flags); classic lobbies keep one human per team.
inline bool lobby_settings_allow_shared_teams(const LobbySettings& settings) noexcept
{
    return settings.campaign_id == og::kCtfCampaignId;
}

struct LobbyState {
    LobbySettings settings;
    std::uint8_t host_player_id = 0xff;
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
    LobbyPlayer player;

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
