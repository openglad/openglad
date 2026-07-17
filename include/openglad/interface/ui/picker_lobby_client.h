#pragma once

#include <openglad/gameplay/lobby_server.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    // the global player_index each seat was assigned, and the GAMEPLAY team its
    // view renders for (allied mode folds every seat to team 0). Empty for
    // local (non-networked) games.
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
    // Ask the lobby to move THIS peer (all of its characters) to `team`.
    // Local sessions validate roster membership and re-seat P1 (my_team);
    // networked sessions send LobbyTeamChangeMessage for themselves. Returns
    // true when the lobby landed on the requested team.
    virtual bool request_team_change(short team)
    {
        (void)team;
        return false;
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
    // The replicated lobby roster (local clients expose their synthetic
    // per-seat players too). Empty before any state broadcast.
    [[nodiscard]] virtual std::vector<og::sim::LobbyPlayer> lobby_players() const
    {
        return {};
    }
    // True only for genuine networked sessions (network host or join client).
    [[nodiscard]] virtual bool is_networked_session() const noexcept
    {
        return false;
    }
};

std::unique_ptr<IPickerLobbyClient> create_local_picker_lobby_client();
void install_active_picker_lobby_client(IPickerLobbyClient* client) noexcept;
IPickerLobbyClient* active_picker_lobby_client() noexcept;
using PickerSaveSlotEditableCallback = bool (*)(int);
inline PickerSaveSlotEditableCallback g_picker_save_slot_editable_callback =
    nullptr;

} // namespace og::ui

void picker_lobby_initialize_from_save();
void picker_lobby_shutdown();
void picker_lobby_sync_from_save();
void picker_lobby_sync_roster_from_save();
void picker_lobby_sync_settings_from_save();
void picker_reinitialize_lobby_after_game();
void picker_lobby_poll();
void picker_lobby_set_player_mode(int player_count);
bool picker_lobby_request_start();
std::optional<og::ui::PickerLobbyGameStartConfig>
picker_lobby_consume_game_start_config();
bool picker_lobby_start_request_pending();
bool picker_lobby_has_game_start_config();
std::vector<std::string> picker_lobby_status_lines();
bool picker_lobby_host_controls_visible();
bool picker_lobby_request_team_change(short team);
bool picker_lobby_set_ready(bool ready);
bool picker_lobby_local_ready();
std::vector<og::sim::LobbyPlayer> picker_lobby_players();
bool picker_lobby_is_networked();
inline bool picker_lobby_save_slot_editable(int slot_index)
{
    if (slot_index < 0)
        return false;
    if (og::ui::g_picker_save_slot_editable_callback)
        return og::ui::g_picker_save_slot_editable_callback(slot_index);
    return true;
}
