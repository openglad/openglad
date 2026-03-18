#pragma once

#include <openglad/interface/ui/picker_lobby_client.h>

#include <cstdint>
#include <memory>
#include <string>

namespace og::ui {

enum class PickerJoinMode : std::int32_t
{
    Direct,
    Relay,
};

struct PickerHostGameOptions
{
    int port = 12345;
    bool enable_relay = false;
    bool relay_required = false;
    std::string relay_base_url;
};

struct PickerJoinGameOptions
{
    PickerJoinMode mode = PickerJoinMode::Direct;
    std::string direct_endpoint;
    std::string room_code;
    std::string relay_base_url;
};

std::unique_ptr<IPickerLobbyClient>
create_host_picker_lobby_client(const PickerHostGameOptions& options);
std::unique_ptr<IPickerLobbyClient>
create_join_picker_lobby_client(const PickerJoinGameOptions& options);
bool picker_join_mode_supported(PickerJoinMode mode) noexcept;
std::string normalize_direct_websocket_url(const std::string& endpoint);
std::string normalize_relay_room_code(const std::string& room_code);
std::string normalize_relay_base_url(const std::string& base_url);
std::string default_relay_base_url();

} // namespace og::ui
