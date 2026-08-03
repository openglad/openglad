#pragma once

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/mode/mode_state.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace og::sim {

// Maximum company display-name length preserved by the lobby wire (matches
// the 40-byte SaveData::save_name field the name comes from). The lobby-state
// reader clamps longer strings instead of rejecting them.
inline constexpr std::size_t kMaxLobbyCompanyNameLength = 40;

// Server-issued identity for one lobby seat. Unlike player_index (the dense
// P#/gameplay ordinal), this token survives unrelated peers leaving and the
// resulting player-index rebuild. Zero is reserved for unjoined/client-authored
// seats and is never accepted as a command target.
using LobbySeatId = std::uint32_t;
inline constexpr LobbySeatId kInvalidLobbySeatId = 0;

// Server-issued identity for the network peer that owns one or more seats.
// Every seat from the same peer shares this value, so clients can group seats
// without inferring machine ownership from mutable display names. Zero is
// reserved for unjoined/client-authored players.
using LobbyMachineId = std::uint32_t;
inline constexpr LobbyMachineId kInvalidLobbyMachineId = 0;

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

// A guy id is process-local until the lobby combines several private
// companies. Different machines commonly load the same ids from otherwise
// unrelated saves, while world snapshots require every in-level guy id to be
// unique. Preserve the first occurrence of every valid client id, reserve all
// other valid values so a remap never steals one, and assign invalid/repeated
// entries the lowest available nonnegative ids. The stable slot order makes
// host and joiner game-start configs derive the same mapping independently.
inline void canonicalize_lobby_gameplay_guy_ids(
    std::vector<LobbyCharacterSlot>& slots)
{
    std::set<std::int32_t> used_ids;
    for (const LobbyCharacterSlot& slot : slots)
    {
        if (slot.character.guy_id >= 0)
            used_ids.insert(slot.character.guy_id);
    }

    std::set<std::int32_t> kept_ids;
    std::int32_t next_available = 0;
    for (LobbyCharacterSlot& slot : slots)
    {
        const std::int32_t requested = slot.character.guy_id;
        if (requested >= 0 && kept_ids.insert(requested).second)
            continue;

        while (used_ids.contains(next_available))
            ++next_available;
        slot.character.guy_id = next_available;
        used_ids.insert(next_available);
        kept_ids.insert(next_available);
        ++next_available;
    }
}

struct LobbyPlayer {
    std::uint8_t player_index = 0xff;
    LobbySeatId seat_id = kInvalidLobbySeatId;
    LobbyMachineId machine_id = kInvalidLobbyMachineId;
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

// Content carried by a Join declaration. Server-issued identity, dense P#,
// host/ready state, display names, and assembly-only ownership metadata are
// intentionally excluded; roster slots, deploy choices, character data, and
// the explicit seat team are the declaration that needs confirmation.
inline bool lobby_join_seats_content_identical(
    const std::vector<LobbyPlayer>& lhs,
    const std::vector<LobbyPlayer>& rhs) noexcept
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t seat = 0; seat < lhs.size(); ++seat)
    {
        if (lhs[seat].team != rhs[seat].team ||
            lhs[seat].character_slots.size() !=
                rhs[seat].character_slots.size())
        {
            return false;
        }
        for (std::size_t slot = 0;
             slot < lhs[seat].character_slots.size(); ++slot)
        {
            const LobbyCharacterSlot& lhs_slot =
                lhs[seat].character_slots[slot];
            const LobbyCharacterSlot& rhs_slot =
                rhs[seat].character_slots[slot];
            if (lhs_slot.slot_index != rhs_slot.slot_index ||
                lhs_slot.deployed != rhs_slot.deployed ||
                lhs_slot.character != rhs_slot.character)
            {
                return false;
            }
        }
    }
    return true;
}

struct LobbySettings {
    std::string campaign_id;
    std::int16_t scenario_id = 0;
    std::int16_t difficulty = 0;
    std::int16_t allied_mode = 0;
    // CTF match settings; only TYPE_CTF maps consume them (0 = map/default).
    std::int16_t ctf_team_count = 0; // 0 = Auto
    // Lower SCORE_TEAM_COUNT bits identify the teams with authored flags in
    // the selected CTF level (protocol v9). Zero means the level metadata is
    // not available yet, so clients and authority behave as if all four were
    // authored (Auto exposes all; explicit N still exposes numeric first N)
    // until the host publishes the loaded map's mask.
    std::uint8_t ctf_authored_team_mask = 0;
    std::int16_t ctf_capture_limit = 0;
    std::int16_t ctf_respawn_ticks = 0;
    std::int16_t ctf_strip_scenario_troops = 0; // 0 = keep authored troops
    // Difficulty submenu settings (0 = legacy default behavior for all three).
    // 0 = off, 1 = heroes, 2 = everyone, 3 = Team 1 heroes only.
    std::int16_t respawn_mode = 0;
    std::int16_t generator_rate = 0;     // percent; 0 = default (100)
    std::int16_t keep_fallen_heroes = 0; // 0 = permadeath on win (classic)
    // Host-only cross-control setting (protocol v8, company-basecamp design
    // §4.1/§4.4): 0 = owner-locked control in networked levels (default),
    // 1 = players may control other machines' characters. sanitize_settings
    // keeps it in {0, 1}.
    std::int16_t cross_control = 0;
    // Host-only infinite-gold setting (protocol v11): 0 = classic economy,
    // 1 = hire/train purchases are free on every peer. Wallets are never
    // written, so this never reaches the sim or any save file.
    // sanitize_settings keeps it in {0, 1}.
    std::int16_t infinite_gold = 0;

    bool operator==(const LobbySettings&) const = default;
};

// Legacy helper name retained for the CTF-specific assignment rules (notably
// the explicit team-count clamp). It originally distinguished modes where
// cross-peer sharing was legal; explicit seat choices now share in all modes.
inline bool lobby_settings_allow_shared_teams(const LobbySettings& settings) noexcept
{
    return settings.campaign_id == og::kCtfCampaignId;
}

inline constexpr std::uint8_t kAllLobbyTeamMask =
    static_cast<std::uint8_t>((1u << SCORE_TEAM_COUNT) - 1u);

// Team domain used by every lobby surface and by server authority. Gameplay
// activates authored flag teams in numeric order, stopping after explicit N;
// this therefore deliberately differs from a numeric [0,N) clamp on sparse
// maps (authored {0,2,3}, N=2 means teams {0,2}). The clamp itself is
// og::sim::effective_team_mask — the ONE copy of the rule, shared with the
// og.effective_team_mask binding (mode/mode_state.h).
inline std::uint8_t
lobby_effective_team_mask(const LobbySettings& settings) noexcept
{
    const std::uint8_t published_authored = static_cast<std::uint8_t>(
        settings.ctf_authored_team_mask & kAllLobbyTeamMask);
    if (!lobby_settings_allow_shared_teams(settings))
        return kAllLobbyTeamMask;
    const std::uint8_t authored = published_authored == 0
        ? kAllLobbyTeamMask
        : published_authored;
    return og::sim::effective_team_mask(authored, settings.ctf_team_count);
}

inline bool lobby_team_is_selectable(const LobbySettings& settings,
                                     std::int16_t team) noexcept
{
    if (team < 0 || team >= SCORE_TEAM_COUNT)
        return false;
    const auto bit =
        static_cast<std::uint8_t>(1u << static_cast<unsigned>(team));
    return (lobby_effective_team_mask(settings) & bit) != 0;
}

inline std::int16_t
lobby_first_selectable_team(const LobbySettings& settings) noexcept
{
    for (std::int16_t team = 0; team < SCORE_TEAM_COUNT; ++team)
    {
        if (lobby_team_is_selectable(settings, team))
            return team;
    }
    return -1;
}

inline std::int16_t lobby_next_selectable_team(
    const LobbySettings& settings,
    std::int16_t current_team) noexcept
{
    int normalized = current_team % SCORE_TEAM_COUNT;
    if (normalized < 0)
        normalized += SCORE_TEAM_COUNT;
    for (int offset = 1; offset <= SCORE_TEAM_COUNT; ++offset)
    {
        const std::int16_t candidate = static_cast<std::int16_t>(
            (normalized + offset) % SCORE_TEAM_COUNT);
        if (lobby_team_is_selectable(settings, candidate))
            return candidate;
    }
    return lobby_first_selectable_team(settings);
}

// Compatibility query retained for callers that still carry the legacy
// allied_mode setting. Explicit per-seat assignments make every in-range team
// shareable: seats on the same or different machines may intentionally choose
// the same team (for example 1,1 versus 2,2). CTF's authored-map domain
// remains a separate validity rule in lobby_effective_team_mask().
//
// Historically CTF and allied lobbies shared teams only across peers, while
// non-allied classic lobbies kept one seat per team and sibling seats always
// stayed distinct. Keep that old rule recorded here even though the picker now
// expresses the player's intent directly.
inline bool lobby_teams_shareable(const LobbySettings& settings) noexcept
{
    (void)settings;
    return true;
}

struct LobbyState {
    LobbySettings settings;
    std::uint8_t host_player_id = 0xff;
    // Echo of the most recent StartGame denial (protocol v8, [NET-R3]):
    // 0 = none, else a StartDenialReason value. Recorded by the LobbyServer
    // so every peer (including a remote host elected on a dedicated server)
    // can render the precise reason instead of a poll timeout.
    std::uint8_t last_start_denial = 0;
    // Correlates that echo with the host's latest StartGame request (protocol
    // v9). Zero means no correlated request has been processed.
    std::uint32_t last_start_request_id = 0;
    std::vector<LobbyPlayer> players;
    // Recipient-specific ownership proof (protocol v9). The server fills this
    // with only the seat tokens owned by the peer receiving this state; its
    // canonical state keeps the vector empty. Clients use tokens rather than
    // player/company names, which are display data and need not be unique.
    std::vector<LobbySeatId> local_seat_ids;
    // Recipient-specific acknowledgement of the most recent Join declaration
    // processed for this peer (protocol v9). Zero means no correlated Join has
    // been processed. The canonical server state keeps this zero; personalized
    // echoes let a client distinguish its Join response from an unrelated
    // ready/team/reset broadcast.
    std::uint32_t last_join_request_id = 0;
    // Recipient-specific peer authority (protocol v9). Host authority belongs
    // to the connected machine, not one of its active seats, so this remains
    // true when the elected host is spectating with zero LobbyPlayers.
    bool local_peer_is_host = false;

    bool operator==(const LobbyState&) const = default;
};

enum class LobbyMessageKind : std::uint8_t {
    Join = 1,
    Leave = 2,
    Ready = 3,
    TeamChange = 4,
    StartGame = 5,
    SettingsChange = 6,
    RemoveSeat = 7,
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
    // Client-issued correlation token (protocol v9). Correlated clients use a
    // nonzero value and reuse it for retries; LobbyState::last_join_request_id
    // echoes it only after the LobbyServer has processed that Join. Zero opts
    // out for fire-and-forget local/host/terminal declarations.
    std::uint32_t request_id = 0;
    // Only the between-level resume handshake may survive a currently locked
    // lobby. Ordinary roster mutations that race StartGame must be dropped,
    // never queued as a silent next-round change.
    bool resume_after_level = false;

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
    // Dense P# captured when the request was made. Retained on the wire for
    // diagnostics/display compatibility; seat_id is the authoritative target.
    std::uint8_t player_index = 0xff;
    LobbySeatId seat_id = kInvalidLobbySeatId;
    std::int16_t team = 0;

    bool operator==(const LobbyTeamChangeMessage&) const = default;
};

struct LobbyRemoveSeatMessage {
    // Dense P# captured when the request was made. Retained on the wire for
    // diagnostics/display compatibility; seat_id is the authoritative target.
    std::uint8_t player_index = 0xff;
    LobbySeatId seat_id = kInvalidLobbySeatId;

    bool operator==(const LobbyRemoveSeatMessage&) const = default;
};

struct LobbyStartGameMessage {
    std::uint8_t player_index = 0xff;
    // Client-issued correlation token; authority echoes it in either the
    // accepted StartGame or LobbyState::last_start_request_id on denial.
    std::uint32_t request_id = 0;

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
                 LobbyRemoveSeatMessage,
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
                if constexpr (std::is_same_v<Message, LobbyRemoveSeatMessage>)
                    return LobbyMessageKind::RemoveSeat;
                if constexpr (std::is_same_v<Message, LobbyStartGameMessage>)
                    return LobbyMessageKind::StartGame;
                return LobbyMessageKind::SettingsChange;
            },
            payload);
    }

    bool operator==(const LobbyMessage&) const = default;
};

} // namespace og::sim
