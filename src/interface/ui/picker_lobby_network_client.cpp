#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kDefaultRelayBaseUrl =
    "https://relay.openglad.example";
constexpr std::array<std::string_view, 2> kNetworkingMenuInstructionLines{{
    "HOST uses current save. JOIN uses IP/ROOM.",
    "Fresh run? BEGIN NEW GAME, then HOST.",
}};

std::string trim_copy(std::string value)
{
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    const auto first = std::find_if(value.begin(), value.end(), not_space);
    const auto last =
        std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

std::string relay_base_url_or_default(std::string configured_url)
{
    configured_url = trim_copy(std::move(configured_url));
    if (!configured_url.empty())
        return configured_url;

    if (const char* env_value = std::getenv("OPENGLAD_RELAY_BASE_URL");
        env_value != nullptr && env_value[0] != '\0')
    {
        return env_value;
    }

    return std::string(kDefaultRelayBaseUrl);
}

std::string join_lines(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines)
    {
        if (line.empty())
            continue;
        if (!text.empty())
            text.push_back('\n');
        text.append(line);
    }
    return text;
}

} // namespace

namespace og::ui {

std::unique_ptr<IPickerLobbyClient>
create_host_picker_lobby_client(const PickerHostGameOptions& options)
{
    const PlatformBridge& bridge = platform_bridge();
    if (!bridge.create_host_picker_lobby_client)
        throw std::runtime_error("Network lobby support is unavailable.");
    return bridge.create_host_picker_lobby_client(options);
}

std::unique_ptr<IPickerLobbyClient>
create_join_picker_lobby_client(const PickerJoinGameOptions& options)
{
    const PlatformBridge& bridge = platform_bridge();
    if (!bridge.create_join_picker_lobby_client)
        throw std::runtime_error("Network lobby support is unavailable.");
    return bridge.create_join_picker_lobby_client(options);
}

bool picker_join_mode_supported(PickerJoinMode mode) noexcept
{
    return mode == PickerJoinMode::Direct || mode == PickerJoinMode::Relay;
}

std::string normalize_direct_websocket_url(const std::string& endpoint)
{
    const std::string trimmed = trim_copy(endpoint);
    if (trimmed.empty())
        throw std::invalid_argument("Direct connect requires an IP:port");

    if (trimmed.rfind("ws://", 0) == 0 || trimmed.rfind("wss://", 0) == 0)
        return trimmed;
    if (trimmed.find("://") != std::string::npos)
    {
        throw std::invalid_argument(
            "Direct connect URLs must use ws:// or wss://");
    }
    return std::string("ws://") + trimmed;
}

std::string normalize_relay_room_code(const std::string& room_code)
{
    std::string normalized = trim_copy(room_code);
    if (normalized.empty())
        throw std::invalid_argument("Relay room code must not be empty");

    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });

    const bool valid = std::all_of(
        normalized.begin(),
        normalized.end(),
        [](unsigned char ch) {
            return std::isalnum(ch) || ch == '-';
        });
    if (!valid)
        throw std::invalid_argument("Relay room codes may only use A-Z, 0-9, and -");
    return normalized;
}

std::string normalize_relay_base_url(const std::string& base_url)
{
    std::string normalized = relay_base_url_or_default(base_url);
    normalized = trim_copy(std::move(normalized));
    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    if (normalized.empty())
        throw std::invalid_argument("Relay base URL must not be empty");

    const bool supported_scheme =
        normalized.rfind("http://", 0) == 0 ||
        normalized.rfind("https://", 0) == 0 ||
        normalized.rfind("ws://", 0) == 0 ||
        normalized.rfind("wss://", 0) == 0;
    if (!supported_scheme)
    {
        throw std::invalid_argument(
            "Relay base URL must use http://, https://, ws://, or wss://");
    }

    return normalized;
}

std::string default_relay_base_url()
{
    return normalize_relay_base_url({});
}

std::span<const std::string_view> networking_menu_instruction_lines()
{
    return kNetworkingMenuInstructionLines;
}

std::vector<PickerRelayRoomInfo> list_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag)
{
    const PlatformBridge& bridge = platform_bridge();
    if (!bridge.list_relay_rooms)
        return {};
    return bridge.list_relay_rooms(
        normalize_relay_base_url(base_url),
        campaign_tag);
}

std::string build_relay_room_prompt_message(
    const std::vector<PickerRelayRoomInfo>& rooms,
    const std::string& campaign_tag,
    const std::string& list_error)
{
    std::vector<std::string> lines;
    lines.push_back("ENTER RELAY ROOM CODE");
    lines.push_back("Type a code manually or choose an active room.");

    if (!list_error.empty())
    {
        lines.push_back(std::format("Room list unavailable: {}", list_error));
        return join_lines(lines);
    }

    if (rooms.empty())
    {
        lines.push_back(
            campaign_tag.empty()
                ? "No active relay rooms found."
                : "No active relay rooms match this campaign.");
        return join_lines(lines);
    }

    lines.push_back("Active rooms:");
    const std::size_t display_count = std::min<std::size_t>(rooms.size(), 5u);
    for (std::size_t index = 0; index < display_count; ++index)
    {
        const PickerRelayRoomInfo& room = rooms[index];
        std::string line = std::format(
            "{}  {} player{}",
            room.code,
            room.player_count,
            room.player_count == 1 ? "" : "s");
        if (!room.host_name.empty())
            line.append(std::format("  {}", room.host_name));
        else if (!room.campaign_name.empty() && campaign_tag.empty())
            line.append(std::format("  {}", room.campaign_name));
        lines.push_back(std::move(line));
    }
    if (rooms.size() > display_count)
    {
        lines.push_back(std::format(
            "+{} more room{}",
            rooms.size() - display_count,
            rooms.size() - display_count == 1 ? "" : "s"));
    }
    return join_lines(lines);
}

} // namespace og::ui
