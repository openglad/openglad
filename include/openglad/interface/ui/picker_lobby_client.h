#pragma once

#include <openglad/gameplay/lobby_server.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace og::ui {

struct PickerLobbyGameStartConfig
{
    og::sim::LobbySaveDataEquivalent save_data;
    std::int16_t difficulty = 1;
};

class IPickerLobbyClient
{
public:
    virtual ~IPickerLobbyClient() = default;

    virtual void initialize_from_save() = 0;
    virtual void shutdown() = 0;
    virtual void sync_from_save() = 0;
    virtual void sync_roster_from_save() = 0;
    virtual void sync_settings_from_save() = 0;
    virtual void poll_and_apply() = 0;
    virtual void set_player_mode(int player_count) = 0;
    virtual bool request_start_game() = 0;
    [[nodiscard]] virtual std::optional<PickerLobbyGameStartConfig>
    build_game_start_config() const = 0;
    [[nodiscard]] virtual std::optional<PickerLobbyGameStartConfig>
    consume_game_start_config() = 0;
    [[nodiscard]] virtual bool start_request_pending() const noexcept = 0;
};

std::unique_ptr<IPickerLobbyClient> create_local_picker_lobby_client();
void install_active_picker_lobby_client(IPickerLobbyClient* client) noexcept;
IPickerLobbyClient* active_picker_lobby_client() noexcept;

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
