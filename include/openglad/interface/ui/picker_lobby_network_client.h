#pragma once

#include <openglad/interface/ui/picker_lobby_client.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    std::string relay_base_url;
};

struct PickerJoinGameOptions
{
    PickerJoinMode mode = PickerJoinMode::Direct;
    std::string direct_endpoint;
    std::string room_code;
    std::string relay_base_url;
};

struct PickerRelayRoomInfo
{
    std::string code;
    std::string campaign_hash;
    std::string campaign_name;
    std::string host_name;
    std::size_t player_count = 0;
    std::int64_t created_at_ms = 0;
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
std::vector<PickerRelayRoomInfo> list_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag = {});
std::string build_relay_room_prompt_message(
    const std::vector<PickerRelayRoomInfo>& rooms,
    const std::string& campaign_tag,
    const std::string& list_error = {});

namespace detail {

struct RelayRoomCreateInfo
{
    std::string room_code;
    std::string owner_token;
};

std::string build_relay_room_websocket_url(std::string base_url,
                                           std::string_view room_code,
                                           std::string_view owner_token = {});
RelayRoomCreateInfo parse_relay_room_create_response(std::string_view body);

} // namespace detail

} // namespace og::ui
