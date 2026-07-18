#pragma once

#include <openglad/interface/ui/picker_lobby_client.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
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
    // Relay API compatibility field. The current worker reports connected
    // machines here, not authoritative lobby seats; UI must not call this a
    // player count.
    std::size_t player_count = 0;
    std::int64_t created_at_ms = 0;
};

struct PickerRelayRoomListResult
{
    std::vector<PickerRelayRoomInfo> rooms;
    std::string error;
};

class IPickerRelayRoomListRequest
{
public:
    virtual ~IPickerRelayRoomListRequest() = default;

    // Returns no value while the request is pending. The terminal result is
    // transferred by the first successful poll; callers should then destroy
    // the request.
    virtual std::optional<PickerRelayRoomListResult> poll() = 0;
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
std::span<const std::string_view> networking_menu_instruction_lines();
std::vector<PickerRelayRoomInfo> list_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag = {});
std::unique_ptr<IPickerRelayRoomListRequest> begin_list_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag = {});
std::string build_relay_room_prompt_message(
    const std::vector<PickerRelayRoomInfo>& rooms,
    const std::string& campaign_tag,
    const std::string& list_error = {});
// One ACTIVE GAMES list-row label: "CODE  <host>", truncated to max_chars
// (button faces draw centered text with no clipping). The relay reports
// machine connections, not local seats, so it is intentionally not presented
// as a player count.
std::string format_relay_room_button_label(const PickerRelayRoomInfo& room,
                                           std::size_t max_chars);

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
