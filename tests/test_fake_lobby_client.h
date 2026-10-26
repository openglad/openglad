#pragma once

// A minimal picker lobby client for engine-screen tests: host-visible, with a
// consumable game-start config so the remote-start checkers see a launchable
// start, and counters for what a screen asked of the lobby (polls, READY
// requests, seat edits). Hoisted out of tests/integration/test_menu_engine.cpp
// when the ABILITIES flows (test_abilities_flow.cpp) needed the same fake:
// one fake, not two that drift.
//
// Install it with og::ui::install_active_picker_lobby_client(&fake) and put
// the previous client back when the test ends.

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/io_common.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern bool g_start_game_requested;

// Minimal lobby client: host-visible, with a consumable game-start config so
// the remote-start checkers see a launchable start.
struct FakeLobbyClient final : og::ui::IPickerLobbyClient
{
    bool host = true;
    bool has_config = true;
    bool networked = false;
    bool add_seat_result = false;
    bool remove_seat_result = false;
    bool set_ready_result = false;

    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override
    {
        ++settings_syncs;
        const screen* const myscreen =
            og::runtime::current_session->myscreen_;
        synced_save_level = myscreen->save_data.scen_num;
        synced_world_level = myscreen->world().id;
        synced_ctf_team_mask =
            og::ui::ctf_authored_team_mask_for_loaded_level(
                myscreen->save_data,
                myscreen->world(),
            get_mounted_campaign());
    }
    void poll_and_apply() override
    {
        ++poll_count;
        if (start_on_next_poll.exchange(false))
            g_start_game_requested = true;
    }
    void set_player_mode(int) override {}
    bool add_local_seat() override
    {
        ++add_seat_calls;
        return add_seat_result;
    }
    bool remove_local_seat(std::uint8_t player_index,
                           og::sim::LobbySeatId seat_id) override
    {
        removed_seats.emplace_back(player_index, seat_id);
        return remove_seat_result;
    }
    bool request_start_game() override { return true; }
    std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return has_config
            ? std::optional<og::ui::PickerLobbyGameStartConfig>{
                  og::ui::PickerLobbyGameStartConfig{}}
            : std::nullopt;
    }
    std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return build_game_start_config();
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return has_config;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return networked;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer>
    lobby_players() const override
    {
        return players;
    }
    [[nodiscard]] std::vector<std::uint8_t>
    local_player_indices() const override
    {
        return local_indices;
    }
    bool set_ready(bool ready) override
    {
        ready_requests.push_back(ready);
        return set_ready_result;
    }
    [[nodiscard]] std::size_t local_seat_count() const override
    {
        return local_seats;
    }
    [[nodiscard]] StagedPreviewHealth staged_preview_health() const override
    {
        return staged_health;
    }

    StagedPreviewHealth staged_health = StagedPreviewHealth::None;
    int settings_syncs = 0;
    short synced_save_level = -1;
    int synced_world_level = -1;
    std::uint8_t synced_ctf_team_mask = 0;
    std::size_t local_seats = 1;
    int add_seat_calls = 0;
    std::vector<std::pair<std::uint8_t, og::sim::LobbySeatId>> removed_seats;
    std::vector<bool> ready_requests;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
    std::atomic<int> poll_count{0};
    std::atomic<bool> start_on_next_poll{false};
};
