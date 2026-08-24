#pragma once

#include <openglad/gameplay/lobby_server.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameWorld;

namespace og::server {
class MatchStage;
} // namespace og::server

namespace og::ui {

struct PickerLobbyGameStartConfig
{
    og::sim::LobbySaveDataEquivalent save_data;
    std::int16_t difficulty = 1;
    std::int16_t my_team = 0;
    // True only for genuine networked sessions (network host or join client).
    // Drives save isolation: the live combined roster is kept off the player's
    // real save0, and each player persists only its own characters.
    bool is_networked = false;
    // This peer's own player slot in the session (0xff if unknown / local game).
    // Seat 0 of local_player_indices below; kept for single-seat compatibility.
    std::uint8_t local_player_index = 0xff;
    // All of this machine's seats, in local seat order (seat 0 first), captured
    // from the FINAL authoritative lobby state when the start was accepted:
    // the global player_index each seat was assigned, and the explicit
    // GAMEPLAY team its view renders for. Character roster colors remain
    // independent combat teams.
    // Empty for local (non-networked) games.
    std::vector<std::uint8_t> local_player_indices = {};
    std::vector<short> local_seat_teams = {};
};

class IPickerLobbyClient
{
public:
    virtual ~IPickerLobbyClient() = default;

    virtual void initialize_from_save() = 0;
    // Re-enter the lobby after a level ends (between-levels return to the
    // team-build menu). The default rebuilds from the current save (local /
    // single-player). Networked clients override this to REUSE the live
    // connection that survived gameplay instead of reconnecting, re-syncing the
    // advanced campaign cursor from the save so the next level is coordinated.
    virtual void resume_after_level()
    {
        initialize_from_save();
    }
    virtual void shutdown() = 0;
    virtual void sync_from_save() = 0;
    virtual void sync_roster_from_save() = 0;
    virtual void sync_settings_from_save() = 0;
    virtual void poll_and_apply() = 0;
    // 0 = spectate, 1..MAX_PLAYERS = local seats. Networked clients honor the
    // requested count too, declaring one lobby seat per local player.
    virtual void set_player_mode(int player_count) = 0;
    // Add one active seat owned by this machine. The authority may refuse the
    // request when either the per-machine or global lobby cap is full.
    virtual bool add_local_seat()
    {
        return false;
    }
    // Remove the exact owned seat represented by one displayed LobbyPlayer.
    // seat_id is the stable authoritative target; player_index is retained as
    // display/diagnostic context and may already be stale after global P#
    // compaction. Networked clients leave a connected zero-seat spectator;
    // local/offline clients refuse to remove their sole seat.
    virtual bool remove_local_seat(std::uint8_t player_index,
                                   og::sim::LobbySeatId seat_id)
    {
        (void)player_index;
        (void)seat_id;
        return false;
    }
    // Active local controls (a connected network spectator has zero).
    [[nodiscard]] virtual std::size_t local_seat_count() const
    {
        const std::vector<std::uint8_t> local_indices =
            local_player_indices();
        if (!local_indices.empty() || is_networked_session())
            return local_indices.size();
        return lobby_players().size();
    }
    virtual bool request_start_game() = 0;
    [[nodiscard]] virtual std::optional<PickerLobbyGameStartConfig>
    build_game_start_config() const = 0;
    [[nodiscard]] virtual std::optional<PickerLobbyGameStartConfig>
    consume_game_start_config() = 0;
    [[nodiscard]] virtual bool start_request_pending() const noexcept = 0;
    [[nodiscard]] virtual bool has_game_start_config() const noexcept
    {
        return false;
    }
    [[nodiscard]] virtual std::vector<std::string> status_lines() const
    {
        return {};
    }
    // A short degraded-link banner for the base camp's §2.5 line-B slot, or
    // nullopt while the session is healthy (the §9.12 session status carries
    // the healthy state). Local/solo clients never alert. Join clients report the
    // transport link ("Status: connecting / connection failed / connection
    // lost"); the host reports a dead relay room ("Relay: connection lost").
    [[nodiscard]] virtual std::optional<std::string> connection_alert() const
    {
        return std::nullopt;
    }
    // §9.12 (G5) display-only: the relay room code this session advertises
    // (host) or joined through (relay joiner). Empty for local sessions,
    // direct (LAN) joins, and relay-less hosts. Returned raw even while the
    // relay link is degraded — connection_alert() outranks the session
    // status on the base camp, so a dead room never displays as healthy.
    [[nodiscard]] virtual std::string session_room_code() const
    {
        return {};
    }
    [[nodiscard]] virtual bool host_controls_visible() const noexcept
    {
        return true;
    }
    [[nodiscard]] virtual bool is_save_slot_editable(
        std::size_t slot_index) const noexcept
    {
        (void)slot_index;
        return true;
    }
    // Ask the lobby to assign one owned seat to a gameplay team. The global
    // player_index is the current display handle; network clients resolve it
    // to the server-issued LobbySeatId granted in their personalized state
    // before sending, and reject foreign/stale indexes. Returns true when the
    // lobby echo confirms the requested assignment. A client with no lobby
    // behind it refuses.
    virtual bool request_seat_team_change(std::uint8_t player_index, short team)
    {
        (void)player_index;
        (void)team;
        return false;
    }
    // Stable-token form used by live seat controls. Both values come from the
    // same displayed LobbyPlayer; if a disconnect/rejoin reindexed the lobby
    // before the click lands, the pair no longer matches and the request is
    // refused instead of aliasing another owned seat.
    virtual bool request_seat_team_change(std::uint8_t player_index,
                                          og::sim::LobbySeatId seat_id,
                                          short team)
    {
        if (seat_id != og::sim::kInvalidLobbySeatId)
        {
            const std::vector<og::sim::LobbyPlayer> players = lobby_players();
            const auto seat = std::find_if(
                players.begin(), players.end(),
                [player_index, seat_id](const og::sim::LobbyPlayer& player) {
                    return player.player_index == player_index &&
                        player.seat_id == seat_id;
                });
            if (seat == players.end())
                return false;
        }
        return request_seat_team_change(player_index, team);
    }
    // Networked-only informational ready flag (LobbyReadyMessage).
    virtual bool set_ready(bool ready)
    {
        (void)ready;
        return false;
    }
    [[nodiscard]] virtual bool local_ready() const noexcept
    {
        return false;
    }
    // The most recent StartGame denial the server echoed to this client
    // (protocol v9, §4.3). None for local/solo sessions (never gated) and
    // until the host's first denied GO. The go_menu / dedicated-server host
    // reads this to surface WHO is blocking the start.
    [[nodiscard]] virtual og::sim::StartDenialReason last_start_denial()
        const noexcept
    {
        return og::sim::StartDenialReason::None;
    }
    // The replicated lobby roster (local clients expose their synthetic
    // per-seat players too). Empty before any state broadcast.
    [[nodiscard]] virtual std::vector<og::sim::LobbyPlayer> lobby_players() const
    {
        return {};
    }
    // Effective team domain from the latest authoritative lobby settings.
    // nullopt before a state echo; consumers may use their locally loaded map
    // as a temporary fallback.
    [[nodiscard]] virtual std::optional<std::uint8_t>
    authoritative_team_mask() const noexcept
    {
        return std::nullopt;
    }
    // THIS machine's seats' global player indexes in the replicated lobby
    // state, in local seat order. Empty for local sessions and before the
    // first state broadcast. The base-camp MP roster (§2.5) uses it to
    // split lobby_players() into own rows (private-save authority) and
    // foreign read-only rows.
    [[nodiscard]] virtual std::vector<std::uint8_t> local_player_indices() const
    {
        return {};
    }
    // True only for genuine networked sessions (network host or join client).
    [[nodiscard]] virtual bool is_networked_session() const noexcept
    {
        return false;
    }
    // --- Staged lobby (#218) ------------------------------------------------
    // The staged authoritative world this client can show: the owner's real
    // MatchStage world (host/local), or a joiner's preview mirror (C9).
    // nullptr while nothing is staged (default for clients without staging).
    [[nodiscard]] virtual const GameWorld* staged_world() const
    {
        return nullptr;
    }
    // Monotonic per-restage counter (0 = never staged). Preview refresh keys
    // on it.
    [[nodiscard]] virtual std::uint32_t stage_generation() const
    {
        return 0;
    }
    // The preview's honest degradation states, mapped by each client from
    // its own machinery: owners from MatchStage::status(), joiners from
    // MirrorStatus. None = nothing staged yet (a joiner still waiting for
    // its first pair reads None too); Unavailable = the joiner's retained
    // pair could not be applied locally.
    enum class StagedPreviewHealth : std::uint8_t {
        None = 0,
        Staged,
        Failed,
        Unavailable,
    };
    [[nodiscard]] virtual StagedPreviewHealth staged_preview_health() const
    {
        return staged_world() != nullptr ? StagedPreviewHealth::Staged
                                         : StagedPreviewHealth::None;
    }
    // The serialized generation-paired StagedMatchSetup/StagedMatchKeyframe
    // inner bytes (host/local: MatchStage's cached broadcast pair; joiner:
    // the retained newest received pair — retained so a preview pane opened
    // long after the broadcast still heals from the SAME bytes the host
    // pane reads). False while no complete pair exists. Pointers borrow the
    // client's storage and are valid until the next poll.
    [[nodiscard]] virtual bool staged_keyframe_bytes(
        std::uint32_t& generation,
        const std::vector<std::uint8_t>*& setup_bytes,
        const std::vector<std::uint8_t>*& keyframe_bytes) const
    {
        (void)generation;
        (void)setup_bytes;
        (void)keyframe_bytes;
        return false;
    }
    // The launch adoption seam: owners (host/local) hand their MatchStage to
    // glad_init's transport-shadow install; joiners return nullptr (their
    // install stays the client shadow). Non-owning — the lobby client keeps
    // ownership and the adopter disposes the stage's content after adoption.
    [[nodiscard]] virtual og::server::MatchStage* take_match_stage()
    {
        return nullptr;
    }
};

std::unique_ptr<IPickerLobbyClient> create_local_picker_lobby_client();
void install_active_picker_lobby_client(IPickerLobbyClient* client) noexcept;
IPickerLobbyClient* active_picker_lobby_client() noexcept;
using PickerSaveSlotEditableCallback = bool (*)(int);
inline PickerSaveSlotEditableCallback g_picker_save_slot_editable_callback =
    nullptr;

// Null in every shipping build: menu screens are single-threaded there.
// Test injector threads install a pump so their save/config mutations run
// between menu frames instead of racing the main thread's poll (issue #257 —
// completed_levels is node-based, so one concurrent insert dangles the
// digest walk's iterator).
using PickerMainThreadPump = void (*)();
inline PickerMainThreadPump g_picker_main_thread_pump = nullptr;

} // namespace og::ui

void picker_lobby_initialize_from_save();
void picker_lobby_shutdown();
void picker_lobby_sync_from_save();
void picker_lobby_sync_roster_from_save();
void picker_lobby_sync_settings_from_save();
void picker_reinitialize_lobby_after_game();
void picker_lobby_poll();
void picker_lobby_set_player_mode(int player_count);
bool picker_lobby_add_local_seat();
bool picker_lobby_remove_local_seat(std::uint8_t player_index,
                                    og::sim::LobbySeatId seat_id);
std::size_t picker_lobby_local_seat_count();
bool picker_lobby_request_start();
std::optional<og::ui::PickerLobbyGameStartConfig>
picker_lobby_consume_game_start_config();
bool picker_lobby_start_request_pending();
bool picker_lobby_has_game_start_config();
std::vector<std::string> picker_lobby_status_lines();
std::optional<std::string> picker_lobby_connection_alert();
std::string picker_lobby_session_room_code();
bool picker_lobby_host_controls_visible();
bool picker_lobby_request_seat_team_change(std::uint8_t player_index,
                                           short team);
bool picker_lobby_request_seat_team_change(std::uint8_t player_index,
                                           og::sim::LobbySeatId seat_id,
                                           short team);
bool picker_lobby_set_ready(bool ready);
bool picker_lobby_local_ready();
og::sim::StartDenialReason picker_lobby_last_start_denial();
std::vector<og::sim::LobbyPlayer> picker_lobby_players();
std::optional<std::uint8_t> picker_lobby_authoritative_team_mask();
std::vector<std::uint8_t> picker_lobby_local_player_indices();
bool picker_lobby_is_networked();
inline bool picker_lobby_save_slot_editable(int slot_index)
{
    if (slot_index < 0)
        return false;
    if (og::ui::g_picker_save_slot_editable_callback)
        return og::ui::g_picker_save_slot_editable_callback(slot_index);
    return true;
}
