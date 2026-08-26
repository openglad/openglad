#pragma once

#include <openglad/core/constants.h>
#include <openglad/gameplay/mode/mode_state.h>

#include <algorithm>
#include <array>
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

// Maximum guy name length preserved by the wire (the 12-byte SaveData disk
// field holds 11 chars + NUL). The shared guy reader clamps longer strings
// instead of rejecting them: under lobby staging, roster payloads become
// world-construction inputs, and unbounded names let hostile peers push the
// merged LobbyState past the u16 transport cap (a serialize throw that would
// otherwise exit a dedicated server).
inline constexpr std::size_t kMaxLobbyGuyNameLength = 11;

// Writer-side twin of the read clamps above: every builder that stamps a name
// into a lobby/setup wire struct must run it through these, so serialize →
// deserialize is lossless for any message an honest peer sends. (The
// VALIDATE_SERIALIZATION lanes round-trip every in-process typed message and
// fail on the first field the readers would alter; the read clamps alone made
// oversized names exactly such a field.) The branch lives here so call sites
// stay straight-line.
[[nodiscard]] inline std::string clamp_lobby_guy_name(std::string name)
{
    if (name.size() > kMaxLobbyGuyNameLength)
        name.resize(kMaxLobbyGuyNameLength);
    return name;
}

[[nodiscard]] inline std::string clamp_lobby_player_label(std::string label)
{
    if (label.size() > kMaxLobbyCompanyNameLength)
        label.resize(kMaxLobbyCompanyNameLength);
    return label;
}

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
    // Protocol v13 (#218): the owner's staged world is in the Failed state
    // (unloadable level, oversize keyframe, roster over the 24-cap), so GO
    // cannot adopt it. Reported through the owner-injected start gate; the
    // host fixes the blocker (level/roster/settings) and retries.
    StageFailed = 4,
};

constexpr std::uint8_t start_denial_reason_value(StartDenialReason reason) noexcept
{
    return static_cast<std::uint8_t>(reason);
}

// The lobby wire's copy of a roster character. It is deliberately a SUBSET of
// `guy`: fields absent here (the `deployed` flag, the GTL v16 `campaign_tag`)
// do not travel, so every consumer that rebuilds a roster from this struct
// must re-stamp them from somewhere else — `deployed` from LobbyCharacterSlot
// below, `campaign_tag` from the private save record the rebuild overwrites
// (see guy.h). Adding a guy field without deciding which of the two it is has
// silently dropped data before.
struct LobbyCharacterData {
    std::int32_t guy_id = 0;
    std::string name;
    // Family id. The wire carries one unsigned byte (0..255 — every value a
    // loader slot can hold, NUM_FAMILY_SLOTS == 256), so the field must be
    // wider than the byte: an int8_t here aliased wire families 128..255 to
    // negatives AND made the merge's `family >= NUM_FAMILY_SLOTS` guard a
    // clang-diagnosed tautology (an int8_t can never reach 256), leaving the
    // in-process typed channel — which skips the wire readers entirely —
    // without any live upper bound.
    std::int16_t family = 0;
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

// Scenario-troops sentinel for "TROOPS: FAIR" (matched-teams design
// D25-D27): the third value of ctf_strip_scenario_troops. It strips the
// authored cast exactly like OWN (2) — every strip consumer reads any value
// above 0 as strip-on — and additionally sizes the bot squads the scripted
// modes generate to the human census instead of the difficulty formula.
// 3 because 1 is the retired middle state (legacy saves and peers still
// hand it over meaning OWN) and 2 is OWN itself. Old builds degrade it to
// plain OWN: their strip rules read it as strip-on, their sanitize reverts
// it at publish, and their toggle cycles it back to ALL.
//
// RETIRED by amendment B5: ctf_strip_scenario_troops is sanitized to 0 at
// every authority (sanitize_settings, both sync_world_from_save_data twins,
// apply_mode_state), so nothing writes this value any more and no strip
// consumer ever sees it. The per-team MAP UNITS box answers the question
// TROOPS used to ask. The constant stays because legacy saves and legacy
// peers still hand the value over and the migration tests name it.
inline constexpr std::int16_t kTroopsMatched = 3;

// The per-team LINEUP band knobs (amendment B1-B4). Eight scalars — one
// FILL wheel and one MAP UNITS box per team — carried through the whole
// match-knob chain beside the older ctf_* knobs.
//
//   fill[t]:      the matched solver WITH A MULTIPLIER (B2). 0 = FAIR (the
//                 default, so all-zero is the default state), 1 = NONE (no
//                 squad on this team at all), 2 = WEAK, 3 = STRONG,
//                 4 = BRUTAL. The engine stores and clamps the code and
//                 nothing else: the multiplier table that turns a code into
//                 a target lives in the mode Lua, which is the only layer
//                 that solves anything, so there is no percent twin here.
//                 Wheel order on the band is NONE, WEAK, FAIR, STRONG,
//                 BRUTAL — FAIR in the middle, where the default belongs.
//   map_units[t]: whether the map's OWN authored units on this team are
//                 fielded (B4). 0 = on (the default and the classic
//                 behaviour), 1 = off. The retired TROOPS knob asked this
//                 once for the whole map; the box asks it per team.
inline constexpr std::int16_t kFillFair = 0;
inline constexpr std::int16_t kFillNone = 1;
inline constexpr std::int16_t kFillWeak = 2;
inline constexpr std::int16_t kFillStrong = 3;
inline constexpr std::int16_t kFillBrutal = 4;
inline constexpr std::int16_t kMaxFill = kFillBrutal;

inline constexpr std::int16_t kMapUnitsOn = 0;
inline constexpr std::int16_t kMapUnitsOff = 1;
inline constexpr std::int16_t kMaxMapUnits = kMapUnitsOff;

// The ONE implementation of each band-knob bound. Every clamp home calls
// these — sanitize_settings (lobby authority), clamp_match_setting (the
// menu/script provider), both sync_world_from_save_data twins (the
// hand-edited-save route into the sim) and world_snapshot apply_mode_state
// (the crafted-snapshot route into a mirror). A divergent copy is a
// host/mirror hash mismatch, so there are no copies.
[[nodiscard]] inline std::int16_t clamp_fill(std::int32_t value) noexcept
{
    return static_cast<std::int16_t>(
        std::clamp<std::int32_t>(value, 0, kMaxFill));
}

[[nodiscard]] inline std::int16_t clamp_map_units(std::int32_t value) noexcept
{
    return static_cast<std::int16_t>(
        std::clamp<std::int32_t>(value, 0, kMaxMapUnits));
}

struct LobbySettings {
    std::string campaign_id;
    std::int16_t scenario_id = 0;
    std::int16_t difficulty = 0;
    std::int16_t allied_mode = 0;
    // Match settings (og.match_setting); scripted maps consume them (0 = map/default).
    std::int16_t ctf_team_count = 0; // 0 = Auto
    // Lower SCORE_TEAM_COUNT bits identify the teams with authored flags in
    // the selected CTF level (protocol v9). Zero means the level metadata is
    // not available yet, so clients and authority behave as if all four were
    // authored (Auto exposes all; explicit N still exposes numeric first N)
    // until the host publishes the loaded map's mask.
    std::uint8_t ctf_authored_team_mask = 0;
    std::int16_t ctf_capture_limit = 0;
    std::int16_t ctf_respawn_ticks = 0;
    std::int16_t ctf_strip_scenario_troops = 0; // 0 = keep; 2 = own; 3 = Fair (kTroopsMatched)
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
    // Versus-campaign flag (protocol v12): 1 when the selected campaign's
    // yaml carries matchup: versus. Set by the HOST from
    // campaign_matchup(campaign_id) at publish time — the joiner may not
    // have the campaign package, so the shared-teams rule rides the wire
    // instead of comparing campaign ids. sanitize_settings keeps it in
    // {0, 1}.
    std::int16_t shared_teams = 0;
    // Match time limit in SIM TICKS (protocol v15); 0 = the map's own value.
    // The fourteenth i16, appended LAST in append/read_lobby_settings.
    // sanitize_settings clamps a non-zero request into [720, 21600].
    std::int16_t time_limit = 0;
    // Per-team FILL / MAP UNITS (protocol v16, amendment B1-B4). Eight i16s
    // appended LAST in append/read_lobby_settings, after time_limit.
    // sanitize_settings clamps through clamp_fill / clamp_map_units.
    std::array<std::int16_t, SCORE_TEAM_COUNT> fill = {};
    std::array<std::int16_t, SCORE_TEAM_COUNT> map_units = {};

    bool operator==(const LobbySettings&) const = default;
};

// Legacy helper name retained for the versus-specific assignment rules
// (notably the explicit team-count clamp). It originally distinguished modes
// where cross-peer sharing was legal; explicit seat choices now share in all
// modes. Since protocol v12 the rule rides the wire (the host derives
// shared_teams from the campaign's matchup: yaml key) instead of comparing
// campaign ids.
inline bool lobby_settings_allow_shared_teams(const LobbySettings& settings) noexcept
{
    return settings.shared_teams != 0;
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
    // The authored mask, clamped, and nothing else. Amendment B8 deleted the
    // one band value that could narrow this: BOTS: OFF is gone, and no value
    // the band can hold deactivates a team any more, so a seat may go
    // wherever the map authored a team — exactly the rule that stood before
    // amendment A2 added the OFF clause.
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
    // Protocol v16 (LINEUP §6): host -> server, "remove that machine from my
    // lobby"; and the server -> peer courtesy notice that precedes the
    // disconnect, so the kicked client can say WHY its link died instead of
    // rendering a bare "connection lost".
    Kick = 8,
    Kicked = 9,
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

// Host -> server (protocol v16). The target is a MACHINE, not a seat: a kick
// removes every seat that machine owns and drops its transport connection.
// The server refuses a non-host sender, and refuses the host's own machine
// (leaving is DISCONNECT, not a kick) — a refusal answers with a state echo
// so the requester never waits on a timeout.
struct LobbyKickMessage {
    LobbyMachineId machine_id = kInvalidLobbyMachineId;

    bool operator==(const LobbyKickMessage&) const = default;
};

// Server -> peer (protocol v16). Sent immediately BEFORE the disconnect so it
// is still deliverable on the live link; carries no payload because the only
// fact it conveys is "the host removed you".
struct LobbyKickedMessage {
    bool operator==(const LobbyKickedMessage&) const = default;
};

using LobbyMessagePayload =
    std::variant<LobbyJoinMessage,
                 LobbyLeaveMessage,
                 LobbyReadyMessage,
                 LobbyTeamChangeMessage,
                 LobbyRemoveSeatMessage,
                 LobbyStartGameMessage,
                 LobbySettingsChangeMessage,
                 LobbyKickMessage,
                 LobbyKickedMessage>;

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
                if constexpr (std::is_same_v<Message, LobbyKickMessage>)
                    return LobbyMessageKind::Kick;
                if constexpr (std::is_same_v<Message, LobbyKickedMessage>)
                    return LobbyMessageKind::Kicked;
                return LobbyMessageKind::SettingsChange;
            },
            payload);
    }

    bool operator==(const LobbyMessage&) const = default;
};

} // namespace og::sim
