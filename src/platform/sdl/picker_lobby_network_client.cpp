#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/net_transport_multiplex.h>
#include <openglad/core/zlib_api.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/net_transport_relay_ws.h>
#include <openglad/platform/picker_lobby_network_runtime.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/io_common.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <openglad/platform/net_transport_emscripten_ws.h>
#else
#include <ixwebsocket/IXHttpClient.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

extern bool g_start_game_requested;

namespace {

constexpr std::string_view kDefaultCampaignId = "org.openglad.gladiator";

struct OrderedLobbySlot {
    std::uint8_t slot_index = 0;
    std::size_t player_order = 0;
    std::size_t slot_order = 0;
    const og::sim::LobbyPlayer* player = nullptr;
    const og::sim::LobbyCharacterSlot* slot = nullptr;
};

struct AppliedLobbySlot {
    std::uint8_t save_slot_index = 0;
    const og::sim::LobbyPlayer* player = nullptr;
    const og::sim::LobbyCharacterSlot* slot = nullptr;
};

SaveData* current_picker_save() noexcept
{
    if (og::runtime::current_session == nullptr ||
        og::runtime::current_session->myscreen_ == nullptr)
    {
        return nullptr;
    }
    return &og::runtime::current_session->myscreen_->save_data;
}

og::sim::LobbyCharacterData make_lobby_character_data(const guy& source)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = source.id;
    character.name = source.name;
    character.family = static_cast<std::int8_t>(source.family);
    character.strength = source.strength;
    character.dexterity = source.dexterity;
    character.constitution = source.constitution;
    character.intelligence = source.intelligence;
    character.armor = source.armor;
    character.exp = source.exp;
    character.kills = source.kills;
    character.level_kills = source.level_kills;
    character.total_damage = source.total_damage;
    character.total_hits = source.total_hits;
    character.total_shots = source.total_shots;
    character.teamnum = source.teamnum;
    character.scen_damage = source.scen_damage;
    character.scen_kills = source.scen_kills;
    character.scen_damage_taken = source.scen_damage_taken;
    character.scen_min_hp = source.scen_min_hp;
    character.scen_shots = source.scen_shots;
    character.scen_hits = source.scen_hits;
    character.level = source.level;
    return character;
}

std::unique_ptr<guy> make_guy_from_lobby_character(
    const og::sim::LobbyCharacterData& character)
{
    auto result = std::make_unique<guy>(character.family);
    result->id = character.guy_id;
    result->name = character.name;
    result->family = static_cast<char>(character.family);
    result->strength = character.strength;
    result->dexterity = character.dexterity;
    result->constitution = character.constitution;
    result->intelligence = character.intelligence;
    result->armor = character.armor;
    result->exp = character.exp;
    result->kills = character.kills;
    result->level_kills = character.level_kills;
    result->total_damage = character.total_damage;
    result->total_hits = character.total_hits;
    result->total_shots = character.total_shots;
    result->teamnum = character.teamnum;
    result->scen_damage = character.scen_damage;
    result->scen_kills = character.scen_kills;
    result->scen_damage_taken = character.scen_damage_taken;
    result->scen_min_hp = character.scen_min_hp;
    result->scen_shots = character.scen_shots;
    result->scen_hits = character.scen_hits;
    result->level = character.level;
    return result;
}

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

constexpr std::string_view kDefaultRelayBaseUrl =
    "https://openglad-relay.yans.workers.dev";

#ifdef __EMSCRIPTEN__
EM_ASYNC_JS(char*, relay_http_post_text_js, (const char* url_cstr), {
    try {
        const url = UTF8ToString(url_cstr);
        // Match the native transport's connect/transfer timeouts: without a
        // signal, a relay that accepts TCP but never responds would keep the
        // wasm Asyncify-suspended (picker frozen, no input) until the
        // browser's own stall timeout, which can be minutes. Feature-detect:
        // AbortSignal.timeout is Safari 16+; older engines just keep the
        // no-timeout behavior instead of failing every fetch.
        const options = { method: 'POST' };
        if (typeof AbortSignal !== 'undefined' &&
            typeof AbortSignal.timeout === 'function') {
            options.signal = AbortSignal.timeout(10000);
        }
        const response = await fetch(url, options);
        const text = await response.text();
        return stringToNewUTF8(
            (response.ok ? 'OK\n' : 'ERR\n') +
            String(response.status) +
            '\n' +
            text);
    } catch (error) {
        const message =
            (error && error.message) ? error.message : String(error);
        return stringToNewUTF8('ERR\n0\n' + message);
    }
});

EM_ASYNC_JS(char*, relay_http_get_text_js, (const char* url_cstr), {
    try {
        const url = UTF8ToString(url_cstr);
        // Same stalled-relay guard as relay_http_post_text_js above.
        const options = { method: 'GET' };
        if (typeof AbortSignal !== 'undefined' &&
            typeof AbortSignal.timeout === 'function') {
            options.signal = AbortSignal.timeout(10000);
        }
        const response = await fetch(url, options);
        const text = await response.text();
        return stringToNewUTF8(
            (response.ok ? 'OK\n' : 'ERR\n') +
            String(response.status) +
            '\n' +
            text);
    } catch (error) {
        const message =
            (error && error.message) ? error.message : String(error);
        return stringToNewUTF8('ERR\n0\n' + message);
    }
});

EM_JS(char*, current_browser_hostname_js, (), {
    const host =
        (typeof window !== 'undefined' && window.location &&
         typeof window.location.hostname === 'string' &&
         window.location.hostname.length > 0)
            ? window.location.hostname
            : '127.0.0.1';
    return stringToNewUTF8(host);
});
#endif

std::string url_encode_component(std::string_view text)
{
    constexpr std::string_view hex_digits = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(text.size());
    for (const char raw_ch : text)
    {
        const unsigned char ch = static_cast<unsigned char>(raw_ch);
        if (std::isalnum(ch) || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~')
        {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(hex_digits[static_cast<std::size_t>((ch >> 4) & 0x0f)]);
        encoded.push_back(hex_digits[static_cast<std::size_t>(ch & 0x0f)]);
    }
    return encoded;
}

std::string relay_api_base_url(std::string base_url)
{
    base_url = trim_copy(std::move(base_url));
    while (!base_url.empty() && base_url.back() == '/')
        base_url.pop_back();
    if (base_url.size() >= 4 &&
        base_url.substr(base_url.size() - 4) == "/api")
    {
        return base_url;
    }
    return base_url + "/api";
}

std::filesystem::path campaign_archive_path(std::string_view campaign_id)
{
    return std::filesystem::path(get_user_path()) / "campaigns" /
        (std::string(campaign_id) + ".glad");
}

struct FileCloser
{
    void operator()(std::FILE* f) const noexcept { if (f) std::fclose(f); }
};

std::optional<std::string> crc32_hex_for_file(const std::filesystem::path& path)
{
    std::unique_ptr<std::FILE, FileCloser> file(
        std::fopen(path.string().c_str(), "rb"));
    if (!file)
        return std::nullopt;

    std::array<unsigned char, 4096> buffer{};
    uLong crc = 0;
    while (true)
    {
        const std::size_t read =
            std::fread(buffer.data(), 1, buffer.size(), file.get());
        if (read > 0)
        {
            crc = crc32(
                crc,
                reinterpret_cast<const Bytef*>(buffer.data()),
                static_cast<uInt>(read));
        }

        if (read < buffer.size())
        {
            if (std::ferror(file.get()) != 0)
                return std::nullopt;
            break;
        }
    }

    return std::format("{:08x}", static_cast<std::uint32_t>(crc));
}

std::string campaign_content_hash(std::string_view campaign_id)
{
    if (campaign_id.empty())
        return {};

    if (const auto hash = crc32_hex_for_file(campaign_archive_path(campaign_id));
        hash.has_value())
    {
        return *hash;
    }

    return {};
}

std::optional<std::size_t> find_json_field_value(std::string_view text,
                                                 std::string_view key)
{
    const std::string needle = std::format("\"{}\"", key);
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string_view::npos)
        return std::nullopt;

    std::size_t colon_pos = text.find(':', key_pos + needle.size());
    if (colon_pos == std::string_view::npos)
        return std::nullopt;
    ++colon_pos;

    while (colon_pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[colon_pos])))
    {
        ++colon_pos;
    }
    if (colon_pos >= text.size())
        return std::nullopt;
    return colon_pos;
}

std::optional<std::string> extract_json_string_field(std::string_view text,
                                                     std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value() || text[*value_pos] != '"')
        return std::nullopt;

    std::string value;
    bool escape = false;
    for (std::size_t index = *value_pos + 1; index < text.size(); ++index)
    {
        const char ch = text[index];
        if (escape)
        {
            value.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
            return value;
        value.push_back(ch);
    }

    return std::nullopt;
}

std::optional<std::uint32_t> extract_json_u32_field(std::string_view text,
                                                    std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    std::size_t end = *value_pos;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end])))
    {
        ++end;
    }
    if (end == *value_pos)
        return std::nullopt;

    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(
        text.data() + *value_pos,
        text.data() + end,
        value);
    if (ec != std::errc{} || ptr != text.data() + end)
        return std::nullopt;
    return value;
}

std::optional<std::int64_t> extract_json_i64_field(std::string_view text,
                                                   std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    std::size_t end = *value_pos;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end])))
    {
        ++end;
    }
    if (end == *value_pos)
        return std::nullopt;

    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(
        text.data() + *value_pos,
        text.data() + end,
        value);
    if (ec != std::errc{} || ptr != text.data() + end)
        return std::nullopt;
    return value;
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

std::string build_relay_room_websocket_url_impl(std::string base_url,
                                                std::string_view room_code,
                                                std::string_view owner_token)
{
    base_url = relay_api_base_url(std::move(base_url));
    if (base_url.rfind("https://", 0) == 0)
        base_url.replace(0, 8, "wss://");
    else if (base_url.rfind("http://", 0) == 0)
        base_url.replace(0, 7, "ws://");

    std::string url = std::format("{}/room/{}", base_url, room_code);
    const std::string encoded_owner_token =
        url_encode_component(owner_token);
    if (!encoded_owner_token.empty())
        url.append(std::format("?owner_token={}", encoded_owner_token));
    return url;
}

std::string build_relay_room_create_url(std::string base_url,
                                        std::string_view campaign_hash,
                                        std::string_view campaign_name,
                                        std::string_view host_name)
{
    base_url = relay_api_base_url(std::move(base_url));
    if (base_url.rfind("ws://", 0) == 0)
        base_url.replace(0, 5, "http://");
    else if (base_url.rfind("wss://", 0) == 0)
        base_url.replace(0, 6, "https://");

    std::string url = base_url + "/create";
    bool has_query = false;
    const auto append_query = [&url, &has_query](
                                  std::string_view key,
                                  std::string_view value) {
        const std::string encoded_value = url_encode_component(value);
        if (encoded_value.empty())
            return;

        url.push_back(has_query ? '&' : '?');
        has_query = true;
        url.append(key);
        url.push_back('=');
        url.append(encoded_value);
    };
    append_query("campaign", campaign_hash);
    append_query("campaign_name", campaign_name);
    append_query("host", host_name);
    return url;
}

std::string build_relay_room_list_url(std::string base_url,
                                      std::string_view campaign_hash)
{
    base_url = relay_api_base_url(std::move(base_url));
    if (base_url.rfind("ws://", 0) == 0)
        base_url.replace(0, 5, "http://");
    else if (base_url.rfind("wss://", 0) == 0)
        base_url.replace(0, 6, "https://");

    std::string url = base_url + "/rooms";
    const std::string encoded_tag = url_encode_component(campaign_hash);
    if (!encoded_tag.empty())
        url.append(std::format("?campaign={}", encoded_tag));
    return url;
}

og::ui::detail::RelayRoomCreateInfo
parse_relay_room_create_response_impl(std::string_view body)
{
    const std::string trimmed = trim_copy(std::string(body));
    if (trimmed.empty())
        throw std::runtime_error("Relay room creation returned an empty response");

    if (trimmed.front() == '{')
    {
        if (const auto code = extract_json_string_field(trimmed, "code");
            code.has_value())
        {
            return {
                .room_code = *code,
                .owner_token = extract_json_string_field(trimmed, "owner_token")
                    .value_or(""),
            };
        }
        if (const auto code =
                extract_json_string_field(trimmed, "room_code");
            code.has_value())
        {
            return {
                .room_code = *code,
                .owner_token = extract_json_string_field(trimmed, "owner_token")
                    .value_or(""),
            };
        }
    }

    return {
        .room_code = trimmed,
        .owner_token = {},
    };
}

og::ui::detail::RelayRoomCreateInfo create_relay_room(
    std::string_view base_url,
    std::string_view campaign_hash,
    std::string_view campaign_name,
    std::string_view host_name)
{
    const std::string create_url = build_relay_room_create_url(
        std::string(base_url),
        campaign_hash,
        campaign_name,
        host_name);

#ifdef __EMSCRIPTEN__
    std::unique_ptr<char, decltype(&std::free)> response_text(
        relay_http_post_text_js(create_url.c_str()),
        &std::free);
    const std::string response =
        response_text ? std::string(response_text.get()) : std::string();
    const std::size_t first_newline = response.find('\n');
    const std::size_t second_newline =
        first_newline == std::string::npos
            ? std::string::npos
            : response.find('\n', first_newline + 1);
    if (first_newline == std::string::npos ||
        second_newline == std::string::npos)
    {
        throw std::runtime_error("Relay room creation returned an invalid response");
    }

    const std::string status = response.substr(0, first_newline);
    const std::string status_code =
        response.substr(first_newline + 1,
                        second_newline - first_newline - 1);
    const std::string body = response.substr(second_newline + 1);
    if (status != "OK")
    {
        throw std::runtime_error(std::format(
            "Relay room creation failed (status {}): {}",
            status_code,
            trim_copy(body)));
    }
    return parse_relay_room_create_response_impl(body);
#else
    ix::HttpClient client;
    ix::HttpRequestArgsPtr args = client.createRequest(create_url, ix::HttpClient::kPost);
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    args->followRedirects = true;
    const ix::HttpResponsePtr response = client.post(create_url, std::string(), args);
    if (!response)
        throw std::runtime_error("Relay room creation failed: no HTTP response");
    if (response->statusCode < 200 || response->statusCode >= 300)
    {
        throw std::runtime_error(std::format(
            "Relay room creation failed (HTTP {}): {}",
            response->statusCode,
            trim_copy(response->body)));
    }
    return parse_relay_room_create_response_impl(response->body);
#endif
}

std::string current_campaign_hash()
{
    if (SaveData* const save = current_picker_save(); save != nullptr)
        return campaign_content_hash(save->current_campaign);
    return campaign_content_hash(kDefaultCampaignId);
}

std::string current_campaign_name()
{
    // Display metadata for the relay room list only — room MATCHING stays on
    // current_campaign_hash() of the raw id.
    if (SaveData* const save = current_picker_save(); save != nullptr)
        return og::data::campaign_display_title(save->current_campaign);
    return og::data::campaign_display_title(std::string(kDefaultCampaignId));
}

std::string current_host_name(short local_team)
{
    SaveData* const save = current_picker_save();
    if (save == nullptr)
        return {};

    for (const auto& member : save->team_list)
    {
        if (member != nullptr && member->teamnum == local_team)
        {
            const std::string name = trim_copy(member->name);
            if (!name.empty())
                return name;
        }
    }

    for (const auto& member : save->team_list)
    {
        if (member == nullptr)
            continue;

        const std::string name = trim_copy(member->name);
        if (!name.empty())
            return name;
    }

    return {};
}

std::string make_network_player_name()
{
    const std::uint64_t now =
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    return std::format("net-{:016x}", now);
}

short resolve_initial_local_team(const SaveData& save)
{
    const auto has_team = [&save](short team) {
        return std::any_of(
            save.team_list.begin(),
            save.team_list.end(),
            [team](const auto& member) {
                return member != nullptr && member->teamnum == team;
            });
    };

    if (save.my_team >= 0 && save.my_team < MAX_PLAYERS && has_team(save.my_team))
        return save.my_team;

    for (const auto& member : save.team_list)
    {
        if (member != nullptr && member->teamnum >= 0 &&
            member->teamnum < MAX_PLAYERS)
        {
            return member->teamnum;
        }
    }

    return 0;
}

short gameplay_start_team(short allied_mode, short requested_team) noexcept
{
    return allied_mode != 0 ? 0 : requested_team;
}

std::vector<std::string> split_top_level_json_objects(std::string_view text)
{
    std::vector<std::string> objects;
    const std::string trimmed = trim_copy(std::string(text));
    if (trimmed.empty())
        return objects;

    std::string_view array_view = trimmed;
    if (trimmed.front() == '{')
    {
        const auto value_pos = find_json_field_value(trimmed, "rooms");
        if (!value_pos.has_value() || trimmed[*value_pos] != '[')
            return objects;
        array_view = std::string_view(trimmed).substr(*value_pos);
    }

    if (array_view.empty() || array_view.front() != '[')
        return objects;

    bool in_string = false;
    bool escape = false;
    int depth = 0;
    std::size_t object_start = std::string_view::npos;
    for (std::size_t index = 0; index < array_view.size(); ++index)
    {
        const char ch = array_view[index];
        if (escape)
        {
            escape = false;
            continue;
        }
        if (in_string)
        {
            if (ch == '\\')
                escape = true;
            else if (ch == '"')
                in_string = false;
            continue;
        }
        if (ch == '"')
        {
            in_string = true;
            continue;
        }
        if (ch == '{')
        {
            if (depth == 0)
                object_start = index;
            ++depth;
            continue;
        }
        if (ch == '}')
        {
            --depth;
            if (depth == 0 && object_start != std::string_view::npos)
            {
                objects.emplace_back(array_view.substr(
                    object_start,
                    index - object_start + 1));
                object_start = std::string_view::npos;
            }
        }
    }

    return objects;
}

std::vector<og::ui::PickerRelayRoomInfo> parse_relay_room_list(
    std::string_view body)
{
    std::vector<og::ui::PickerRelayRoomInfo> rooms;
    for (const std::string& object : split_top_level_json_objects(body))
    {
        const auto code = extract_json_string_field(object, "code");
        if (!code.has_value())
            continue;

        og::ui::PickerRelayRoomInfo room;
        room.code = og::ui::normalize_relay_room_code(*code);
        room.campaign_hash =
            extract_json_string_field(object, "campaign_hash").value_or("");
        room.campaign_name =
            extract_json_string_field(object, "campaign_name").value_or("");
        room.host_name =
            extract_json_string_field(object, "host_name").value_or("");
        room.player_count =
            extract_json_u32_field(object, "player_count").value_or(0u);
        room.created_at_ms =
            extract_json_i64_field(object, "created_at").value_or(0);
        rooms.push_back(std::move(room));
    }
    return rooms;
}

std::string fetch_relay_room_list_body(std::string_view base_url,
                                       std::string_view campaign_hash)
{
    const std::string list_url = build_relay_room_list_url(
        std::string(base_url),
        campaign_hash);

#ifdef __EMSCRIPTEN__
    std::unique_ptr<char, decltype(&std::free)> response_text(
        relay_http_get_text_js(list_url.c_str()),
        &std::free);
    const std::string response =
        response_text ? std::string(response_text.get()) : std::string();
    const std::size_t first_newline = response.find('\n');
    const std::size_t second_newline =
        first_newline == std::string::npos
            ? std::string::npos
            : response.find('\n', first_newline + 1);
    if (first_newline == std::string::npos ||
        second_newline == std::string::npos)
    {
        throw std::runtime_error("Relay room listing returned an invalid response");
    }

    const std::string status = response.substr(0, first_newline);
    const std::string status_code =
        response.substr(first_newline + 1,
                        second_newline - first_newline - 1);
    const std::string body = response.substr(second_newline + 1);
    if (status != "OK")
    {
        throw std::runtime_error(std::format(
            "Relay room listing failed (status {}): {}",
            status_code,
            trim_copy(body)));
    }
    return body;
#else
    ix::HttpClient client;
    ix::HttpRequestArgsPtr args = client.createRequest(list_url, ix::HttpClient::kGet);
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    args->followRedirects = true;
    const ix::HttpResponsePtr response = client.get(list_url, args);
    if (!response)
        throw std::runtime_error("Relay room listing failed: no HTTP response");
    if (response->statusCode < 200 || response->statusCode >= 300)
    {
        throw std::runtime_error(std::format(
            "Relay room listing failed (HTTP {}): {}",
            response->statusCode,
            trim_copy(response->body)));
    }
    return response->body;
#endif
}

} // namespace

namespace og::ui::detail {

std::string build_relay_room_websocket_url(std::string base_url,
                                           std::string_view room_code,
                                           std::string_view owner_token)
{
    return build_relay_room_websocket_url_impl(
        std::move(base_url),
        room_code,
        owner_token);
}

RelayRoomCreateInfo parse_relay_room_create_response(std::string_view body)
{
    return parse_relay_room_create_response_impl(body);
}

og::sim::LobbyPlayer build_local_lobby_player(
    const SaveData& save,
    std::string_view player_name,
    short local_team,
    const std::array<bool, MAX_TEAM_SIZE>* excluded_slots = nullptr)
{
    og::sim::LobbyPlayer player;
    player.name = std::string(player_name);
    player.team = local_team;
    player.ready = false;
    player.is_host = false;

    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        if (excluded_slots != nullptr && (*excluded_slots)[slot_index])
            continue;

        const auto& member = save.team_list[slot_index];
        if (member == nullptr || member->teamnum != local_team)
            continue;

        player.character_slots.push_back(og::sim::LobbyCharacterSlot{
            .slot_index = static_cast<std::uint8_t>(slot_index),
            .character = make_lobby_character_data(*member),
        });
    }

    return player;
}

// Seat naming: seat 0 keeps the machine's base name ("net-<hex>"); seat k is
// derived as base + "#k". The LobbyServer preserves client-sent names, so a
// machine identifies its own seats by exact-matching these derived names.
std::string derive_local_seat_name(std::string_view base_name, std::size_t seat)
{
    if (seat == 0)
        return std::string(base_name);
    return std::format("{}#{}", base_name, seat);
}

bool is_local_seat_name(std::string_view name,
                        std::string_view base_name) noexcept
{
    if (name == base_name)
        return true;
    return name.size() > base_name.size() + 1 &&
        name.substr(0, base_name.size()) == base_name &&
        name[base_name.size()] == '#';
}

// Seat teams for an N-seat declaration: seat 0 keeps the tracked local team
// (single-seat behavior unchanged), seats 1..N-1 take the save's remaining
// distinct seat teams (derive_local_seat_teams order) padded with free team
// candidates — mirroring the offline splitscreen mapping.
std::vector<short> resolve_local_seat_declaration_teams(
    const SaveData& save,
    short seat0_team,
    std::size_t seat_count)
{
    std::vector<short> teams;
    teams.push_back(seat0_team);
    for (const short team : og::ui::derive_local_seat_teams(save))
    {
        if (teams.size() >= seat_count)
            break;
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }
    for (short candidate = 0;
         teams.size() < seat_count && candidate < MAX_PLAYERS;
         ++candidate)
    {
        if (std::find(teams.begin(), teams.end(), candidate) == teams.end())
            teams.push_back(candidate);
    }
    if (teams.size() > seat_count)
        teams.resize(seat_count);
    return teams;
}

// Build this machine's whole seat list (one LobbyPlayer per local seat). Seat
// k contributes the save's members on its seat team, so within one machine
// every roster member lands on exactly one seat.
std::vector<og::sim::LobbyPlayer> build_local_lobby_seats(
    const SaveData& save,
    std::string_view base_name,
    short seat0_team,
    std::size_t seat_count,
    const std::array<bool, MAX_TEAM_SIZE>* excluded_slots = nullptr)
{
    const std::size_t count = std::clamp<std::size_t>(
        seat_count, 1u, static_cast<std::size_t>(MAX_PLAYERS));
    const std::vector<short> seat_teams =
        resolve_local_seat_declaration_teams(save, seat0_team, count);

    std::vector<og::sim::LobbyPlayer> seats;
    seats.reserve(seat_teams.size());
    for (std::size_t seat = 0; seat < seat_teams.size(); ++seat)
    {
        seats.push_back(build_local_lobby_player(
            save,
            derive_local_seat_name(base_name, seat),
            seat_teams[seat],
            excluded_slots));
    }
    return seats;
}

const og::sim::LobbyPlayer* find_local_player(
    const og::sim::LobbyState& state,
    std::string_view player_name) noexcept;

// This machine's seats in the authoritative state, in local seat order.
// Truncated joins keep the FIRST K seats server-side, so the derived names are
// gap-free: stop at the first missing one.
std::vector<const og::sim::LobbyPlayer*> find_local_seats(
    const og::sim::LobbyState& state,
    std::string_view base_name)
{
    std::vector<const og::sim::LobbyPlayer*> seats;
    for (std::size_t seat = 0; seat < static_cast<std::size_t>(MAX_PLAYERS);
         ++seat)
    {
        const og::sim::LobbyPlayer* const player = find_local_player(
            state, derive_local_seat_name(base_name, seat));
        if (player == nullptr)
            break;
        seats.push_back(player);
    }
    return seats;
}

std::vector<AppliedLobbySlot> collect_applied_lobby_slots(
    const og::sim::LobbyState& state)
{
    std::vector<OrderedLobbySlot> ordered_slots;
    for (std::size_t player_index = 0; player_index < state.players.size();
         ++player_index)
    {
        const og::sim::LobbyPlayer& player = state.players[player_index];
        for (std::size_t slot_order = 0;
             slot_order < player.character_slots.size();
             ++slot_order)
        {
            ordered_slots.push_back(OrderedLobbySlot{
                .slot_index = player.character_slots[slot_order].slot_index,
                .player_order = player_index,
                .slot_order = slot_order,
                .player = &player,
                .slot = &player.character_slots[slot_order],
            });
        }
    }

    std::sort(ordered_slots.begin(), ordered_slots.end(),
              [](const OrderedLobbySlot& lhs, const OrderedLobbySlot& rhs) {
                  if (lhs.slot_index != rhs.slot_index)
                      return lhs.slot_index < rhs.slot_index;
                  if (lhs.player_order != rhs.player_order)
                      return lhs.player_order < rhs.player_order;
                  return lhs.slot_order < rhs.slot_order;
              });

    const bool slots_are_dense = std::all_of(
        ordered_slots.begin(), ordered_slots.end(),
        [&ordered_slots](const OrderedLobbySlot& slot) {
            return static_cast<std::size_t>(slot.slot_index) ==
                static_cast<std::size_t>(&slot - ordered_slots.data());
        });

    std::vector<AppliedLobbySlot> applied_slots;
    applied_slots.reserve(ordered_slots.size());
    for (std::size_t index = 0; index < ordered_slots.size(); ++index)
    {
        std::uint8_t save_slot_index = ordered_slots[index].slot_index;
        if (!slots_are_dense)
            save_slot_index = static_cast<std::uint8_t>(index);
        applied_slots.push_back(AppliedLobbySlot{
            .save_slot_index = save_slot_index,
            .player = ordered_slots[index].player,
            .slot = ordered_slots[index].slot,
        });
    }

    return applied_slots;
}

const og::sim::LobbyPlayer* find_local_player(
    const og::sim::LobbyState& state,
    std::string_view player_name) noexcept
{
    const auto it = std::find_if(
        state.players.begin(),
        state.players.end(),
        [player_name](const og::sim::LobbyPlayer& player) {
            return player.name == player_name;
    });
    return it != state.players.end() ? &*it : nullptr;
}

bool local_player_is_host(const og::sim::LobbyState& state,
                          std::string_view player_name) noexcept
{
    const og::sim::LobbyPlayer* const local_player =
        find_local_player(state, player_name);
    return local_player != nullptr &&
        (local_player->is_host ||
         local_player->player_index == state.host_player_id);
}

og::sim::LobbySaveDataEquivalent build_save_data_equivalent_from_state(
    const og::sim::LobbyState& state,
    bool spectator_mode,
    std::size_t local_player_count = 1)
{
    og::sim::LobbySaveDataEquivalent equivalent;
    equivalent.current_campaign = state.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state.settings.campaign_id;
    equivalent.scen_num =
        state.settings.scenario_id > 0 ? state.settings.scenario_id : 1;
    equivalent.numplayers = spectator_mode
        ? 0u
        : static_cast<unsigned char>(std::clamp<std::size_t>(
              local_player_count, 1u, static_cast<std::size_t>(MAX_PLAYERS)));
    equivalent.allied_mode = state.settings.allied_mode;
    equivalent.ctf_team_count = state.settings.ctf_team_count;
    equivalent.ctf_capture_limit = state.settings.ctf_capture_limit;
    equivalent.ctf_respawn_ticks = state.settings.ctf_respawn_ticks;
    equivalent.ctf_strip_scenario_troops = state.settings.ctf_strip_scenario_troops;
    equivalent.respawn_mode = state.settings.respawn_mode;
    equivalent.generator_rate = state.settings.generator_rate;
    equivalent.keep_fallen_heroes = state.settings.keep_fallen_heroes;

    for (const AppliedLobbySlot& slot : collect_applied_lobby_slots(state))
    {
        og::sim::LobbyCharacterSlot compacted = *slot.slot;
        compacted.slot_index = slot.save_slot_index;
        compacted.character.teamnum = gameplay_start_team(
            state.settings.allied_mode,
            compacted.character.teamnum);
        compacted.owner_player_index =
            slot.player != nullptr ? slot.player->player_index : 0xffu;
        compacted.owner_save_slot = slot.save_slot_index;
        equivalent.team_list.push_back(std::move(compacted));
    }

    return equivalent;
}

std::array<bool, MAX_TEAM_SIZE> build_remote_owned_slot_mask(
    const og::sim::LobbyState& state,
    std::string_view local_player_name)
{
    std::array<bool, MAX_TEAM_SIZE> remote_slots{};
    remote_slots.fill(false);

    for (const AppliedLobbySlot& slot : collect_applied_lobby_slots(state))
    {
        if (slot.save_slot_index >= remote_slots.size())
            continue;
        // ALL of this machine's seats are local ("base" and "base#k"): a
        // multi-seat machine must keep editing every one of its own seats'
        // characters.
        if (slot.player == nullptr ||
            is_local_seat_name(slot.player->name, local_player_name))
            continue;
        remote_slots[slot.save_slot_index] = true;
    }

    return remote_slots;
}

void apply_lobby_state_to_save(const og::sim::LobbyState& state,
                               SaveData& save,
                               bool spectator_mode,
                               short local_team,
                               std::size_t local_player_count = 1)
{
    save.current_campaign = state.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state.settings.campaign_id;
    save.scen_num = state.settings.scenario_id > 0
        ? state.settings.scenario_id
        : 1;
    save.allied_mode = state.settings.allied_mode;
    save.ctf_team_count = state.settings.ctf_team_count;
    save.ctf_capture_limit = state.settings.ctf_capture_limit;
    save.ctf_respawn_ticks = state.settings.ctf_respawn_ticks;
    save.ctf_strip_scenario_troops = state.settings.ctf_strip_scenario_troops;
    save.respawn_mode = state.settings.respawn_mode;
    save.generator_rate = state.settings.generator_rate;
    save.keep_fallen_heroes = state.settings.keep_fallen_heroes;
    save.numplayers = static_cast<unsigned char>(
        spectator_mode
            ? 0u
            : std::clamp<std::size_t>(
                  local_player_count, 1u,
                  static_cast<std::size_t>(MAX_PLAYERS)));
    save.my_team = local_team;

    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;

    for (const AppliedLobbySlot& slot : collect_applied_lobby_slots(state))
    {
        if (slot.save_slot_index >= save.team_list.size())
            continue;

        save.team_list[slot.save_slot_index] =
            make_guy_from_lobby_character(slot.slot->character);
        ++save.team_size;
    }

    if (og::runtime::current_session != nullptr)
    {
        og::runtime::current_session->current_difficulty_ =
            static_cast<std::int32_t>(state.settings.difficulty);
    }
}

void send_lobby_message(og::sim::ITransport& transport,
                        og::sim::PeerId peer_id,
                        og::sim::LobbyMessage message)
{
    transport.send_lobby_message(
        peer_id,
        std::make_shared<og::sim::LobbyMessage>(std::move(message)));
}

og::sim::LobbyMessage make_join_message(const SaveData& save,
                                        std::string_view player_name,
                                        short local_team,
                                        const std::array<bool, MAX_TEAM_SIZE>* excluded_slots = nullptr,
                                        std::size_t seat_count = 1)
{
    // A join declares this machine's WHOLE seat list: seat 0 rides in the
    // legacy player field, seats 1..N-1 in extra_players (wire v7).
    std::vector<og::sim::LobbyPlayer> seats = build_local_lobby_seats(
        save,
        player_name,
        local_team,
        seat_count,
        excluded_slots);

    og::sim::LobbyMessage message;
    og::sim::LobbyJoinMessage join;
    join.player = std::move(seats.front());
    join.extra_players.assign(
        std::make_move_iterator(seats.begin() + 1),
        std::make_move_iterator(seats.end()));
    message.payload = std::move(join);
    return message;
}

og::sim::LobbyMessage make_team_change_message(std::uint8_t player_index,
                                               short team)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = player_index,
        .team = team,
    };
    return message;
}

og::sim::LobbyMessage make_ready_message(std::uint8_t player_index, bool ready)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyReadyMessage{
        .player_index = player_index,
        .ready = ready,
    };
    return message;
}

og::sim::LobbyMessage make_settings_message(const SaveData& save)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = save.current_campaign;
    settings.scenario_id = save.scen_num;
    settings.difficulty =
        static_cast<std::int16_t>(
            og::runtime::current_session != nullptr
                ? og::runtime::current_session->current_difficulty_
                : 1);
    settings.allied_mode = save.allied_mode;
    settings.ctf_team_count = save.ctf_team_count;
    settings.ctf_capture_limit = save.ctf_capture_limit;
    settings.ctf_respawn_ticks = save.ctf_respawn_ticks;
    settings.ctf_strip_scenario_troops = save.ctf_strip_scenario_troops;
    settings.respawn_mode = save.respawn_mode;
    settings.generator_rate = save.generator_rate;
    settings.keep_fallen_heroes = save.keep_fallen_heroes;

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0xffu,
        .settings = std::move(settings),
    };
    return message;
}

} // namespace og::ui::detail

namespace {

std::vector<og::sim::TypedReceivedMessage> poll_lobby_transport_messages(
    og::sim::ITransport& transport)
{
    if (transport.supports_typed_messages())
        return transport.poll_typed();

    std::vector<og::sim::TypedReceivedMessage> typed_messages;
    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
            continue;

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        switch (envelope.message_type)
        {
        case og::sim::kLobbyMessageType:
        {
            const auto decoded =
                og::sim::deserialize_lobby_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyMessage;
            typed_message.lobby_message =
                std::make_shared<og::sim::LobbyMessage>(*decoded);
            break;
        }

        case og::sim::kLobbyStateMessageType:
        {
            const auto decoded =
                og::sim::deserialize_lobby_state_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyState;
            typed_message.lobby_state =
                std::make_shared<og::sim::LobbyState>(*decoded);
            break;
        }

        default:
            continue;
        }

        typed_messages.push_back(std::move(typed_message));
    }

    return typed_messages;
}

short resolve_pending_local_team(
    const og::sim::LobbyState& state,
    short requested_team,
    std::string_view local_player_name,
    const std::vector<short>& sibling_teams = {}) noexcept
{
    // Mirror the server's seat rule: within one machine seats need DISTINCT
    // teams; across machines a team is occupied unless the lobby is shareable
    // (CTF campaign or allied mode).
    const bool shareable = og::sim::lobby_teams_shareable(state.settings);
    const auto team_available = [&](short team) {
        if (team < 0 || team >= MAX_PLAYERS)
            return false;
        if (std::find(sibling_teams.begin(), sibling_teams.end(), team) !=
            sibling_teams.end())
            return false;
        if (shareable)
            return true;

        return std::none_of(
            state.players.begin(),
            state.players.end(),
            [team, local_player_name](const og::sim::LobbyPlayer& player) {
                return !og::ui::detail::is_local_seat_name(
                           player.name, local_player_name) &&
                    player.team == team;
            });
    };

    if (team_available(requested_team))
        return requested_team;

    for (short candidate = 0; candidate < MAX_PLAYERS; ++candidate)
    {
        if (team_available(candidate))
            return candidate;
    }

    return requested_team;
}

struct PendingLocalLobbyState {
    og::sim::LobbyState state;
    short local_team = 0;
};

PendingLocalLobbyState build_pending_local_lobby_state(
    const og::sim::LobbyState& state,
    const std::vector<og::sim::LobbyPlayer>& pending_local_seats,
    short requested_local_team,
    std::string_view local_player_name)
{
    PendingLocalLobbyState merged{
        .state = state,
        .local_team = requested_local_team,
    };

    if (pending_local_seats.empty() ||
        pending_local_seats.front().character_slots.empty() ||
        merged.state.players.size() >=
            static_cast<std::size_t>(og::sim::kMaxGlobalPlayers) ||
        og::ui::detail::find_local_player(merged.state, local_player_name) !=
            nullptr)
    {
        return merged;
    }

    // Merge every pending local seat (the join is a whole-seat-list declare),
    // resolving teams in seat order with within-machine distinctness.
    std::vector<short> sibling_teams;
    for (std::size_t seat = 0; seat < pending_local_seats.size(); ++seat)
    {
        if (merged.state.players.size() >=
            static_cast<std::size_t>(og::sim::kMaxGlobalPlayers))
            break;

        const og::sim::LobbyPlayer& pending_seat = pending_local_seats[seat];
        const short seat_team = resolve_pending_local_team(
            merged.state,
            seat == 0 ? requested_local_team : pending_seat.team,
            local_player_name,
            sibling_teams);
        if (seat == 0)
            merged.local_team = seat_team;
        sibling_teams.push_back(seat_team);

        og::sim::LobbyPlayer local_player = pending_seat;
        local_player.player_index = 0xffu;
        local_player.team = seat_team;
        local_player.ready = false;
        local_player.is_host = false;
        for (auto& slot : local_player.character_slots)
            slot.character.teamnum = seat_team;

        merged.state.players.push_back(std::move(local_player));
    }
    return merged;
}

std::string detect_lan_ipv4_address()
{
#ifdef __EMSCRIPTEN__
    std::unique_ptr<char, decltype(&std::free)> hostname(current_browser_hostname_js(), &std::free);
    if (!hostname)
        return "127.0.0.1";

    std::string value = hostname.get();
    if (value.empty())
        return "127.0.0.1";
    return value;
#elif defined(__unix__) || defined(__APPLE__)
    const auto is_usable_ipv4 = [](const in_addr& address) {
        const std::uint32_t host_order = ntohl(address.s_addr);
        return host_order != 0 && (host_order >> 24) != 127;
    };

    const auto to_ipv4_string = [&](const in_addr& address)
        -> std::optional<std::string> {
        if (!is_usable_ipv4(address))
            return std::nullopt;

        std::array<char, INET_ADDRSTRLEN> buffer = {};
        if (inet_ntop(AF_INET, &address, buffer.data(),
                      static_cast<socklen_t>(buffer.size())) == nullptr)
            return std::nullopt;
        return std::string(buffer.data());
    };

    const auto detect_via_udp_socket = [&]() -> std::optional<std::string> {
        const int descriptor = socket(AF_INET, SOCK_DGRAM, 0);
        if (descriptor < 0)
            return std::nullopt;

        // descriptor is provably >= 0 here, so close it exactly once at scope
        // exit; this removes the three hand-placed close() calls and their
        // associated leak hazard on any future early return.
        struct FdCloser {
            int fd;
            ~FdCloser() noexcept { close(fd); }
        } fd_guard{descriptor};

        sockaddr_in remote = {};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(9);
        if (inet_pton(AF_INET, "198.18.0.1", &remote.sin_addr) != 1)
            return std::nullopt;

        const int connect_result = connect(
            descriptor,
            reinterpret_cast<const sockaddr*>(&remote),
            sizeof(remote));
        if (connect_result != 0)
            return std::nullopt;

        sockaddr_in local = {};
        socklen_t local_size = sizeof(local);
        const int name_result = getsockname(
            descriptor,
            reinterpret_cast<sockaddr*>(&local),
            &local_size);
        if (name_result != 0 || local.sin_family != AF_INET)
            return std::nullopt;

        return to_ipv4_string(local.sin_addr);
    };

    const auto detect_via_hostname = [&]() -> std::optional<std::string> {
        std::array<char, 256> hostname = {};
        // Reserve the final byte: POSIX leaves NUL-termination unspecified when
        // the name is truncated, but the zero-initialized buffer keeps [255]=='\0'.
        if (gethostname(hostname.data(), hostname.size() - 1) != 0 || hostname[0] == '\0')
            return std::nullopt;

        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        addrinfo* results = nullptr;
        if (getaddrinfo(hostname.data(), nullptr, &hints, &results) != 0 ||
            results == nullptr)
        {
            return std::nullopt;
        }

        struct AddrInfoDeleter {
            void operator()(addrinfo* p) const noexcept { freeaddrinfo(p); }
        };
        std::unique_ptr<addrinfo, AddrInfoDeleter> results_guard(results);

        std::optional<std::string> detected_address;
        for (addrinfo* current = results; current != nullptr;
             current = current->ai_next)
        {
            if (current->ai_addr == nullptr || current->ai_addrlen < sizeof(sockaddr_in))
                continue;

            const auto* const address =
                reinterpret_cast<const sockaddr_in*>(current->ai_addr);
            detected_address = to_ipv4_string(address->sin_addr);
            if (detected_address.has_value())
                break;
        }

        return detected_address;
    };

    if (const auto address = detect_via_udp_socket(); address.has_value())
        return *address;
    if (const auto address = detect_via_hostname(); address.has_value())
        return *address;
    return "127.0.0.1";
#else
    return "127.0.0.1";
#endif
}

std::string build_host_transport_failure_message(
    std::string_view direct_status_message,
    std::string_view relay_status_message)
{
    if (!direct_status_message.empty() && !relay_status_message.empty())
    {
        return std::format("Direct: {}\nRelay: {}",
                           direct_status_message,
                           relay_status_message);
    }
    if (!direct_status_message.empty())
        return std::string(direct_status_message);
    if (!relay_status_message.empty())
        return std::string(relay_status_message);
    return "Unable to host a network lobby.";
}

void clear_active_gameplay_shadow() noexcept
{
    // The picker can resume on the same client object after gameplay hands
    // its transports into the local shadow runtime. Tear that runtime down
    // before any reconnect or direct-listener rebind.
    if (og::runtime::current_game_session != nullptr)
    {
        og::runtime::clear_local_transport_shadow(
            *og::runtime::current_game_session);
        return;
    }

    if (auto* const session =
            dynamic_cast<og::runtime::GameSession*>(
                og::runtime::current_session))
    {
        og::runtime::clear_local_transport_shadow(*session);
    }
}

} // namespace

#ifdef TESTING
namespace og::ui::detail {

// GCOVR_EXCL_START -- test-only coverage harness in src/, not shipped code.
int picker_lobby_network_testing_exercise_internal_helpers()
{
    int checks = 0;
    int check_index = 0;
    int failed_check = 0;
    bool failed = false;
    const auto check = [&checks, &check_index, &failed_check, &failed](bool condition) {
        ++check_index;
        if (condition)
            ++checks;
        else
        {
            failed = true;
            if (failed_check == 0)
                failed_check = check_index;
        }
    };

    check(trim_copy(" \t host player \n ") == "host player");
    check(trim_copy(" \t\n ") == "");
    check(url_encode_component("a b+/~") == "a%20b%2B%2F~");
    check(relay_api_base_url(" https://relay.example/api/// ") ==
          "https://relay.example/api");
    check(relay_api_base_url("https://relay.example/base") ==
          "https://relay.example/base/api");
    check(build_relay_room_websocket_url_impl(
              "https://relay.example",
              "ROOM-1",
              "owner token") ==
          "wss://relay.example/api/room/ROOM-1?owner_token=owner%20token");
    check(build_relay_room_websocket_url_impl(
              "http://relay.example/api",
              "ROOM-2",
              "") ==
          "ws://relay.example/api/room/ROOM-2");
    check(build_relay_room_create_url(
              "wss://relay.example/",
              "",
              "Campaign One",
              "Host+Name") ==
          "https://relay.example/api/create?campaign_name=Campaign%20One&host=Host%2BName");
    check(build_relay_room_list_url("ws://relay.example", "hash one") ==
          "http://relay.example/api/rooms?campaign=hash%20one");

    check(!find_json_field_value(R"({"code" "missing-colon"})", "code").has_value());
    check(!find_json_field_value(R"({"other":1})", "code").has_value());
    check(extract_json_string_field(R"({"text":"a\\b"})", "text").value_or("") ==
          "a\\b");
    check(!extract_json_string_field(R"({"text":123})", "text").has_value());
    check(!extract_json_string_field(R"({"text":"unterminated})", "text").has_value());
    check(extract_json_u32_field(R"({"count":42})", "count").value_or(0) == 42);
    check(!extract_json_u32_field(R"({"count":"42"})", "count").has_value());
    check(!extract_json_u32_field(
              R"({"count":999999999999999999999999})",
              "count").has_value());
    check(extract_json_i64_field(R"({"created_at":123456})", "created_at").value_or(0) ==
          123456);
    check(!extract_json_i64_field(R"({"created_at":-1})", "created_at").has_value());

    const auto created_from_code =
        parse_relay_room_create_response_impl(
            R"({"code":"ROOM-A","owner_token":"owner-a"})");
    check(created_from_code.room_code == "ROOM-A" &&
          created_from_code.owner_token == "owner-a");
    const auto created_from_room_code =
        parse_relay_room_create_response_impl(R"({"room_code":"ROOM-B"})");
    check(created_from_room_code.room_code == "ROOM-B" &&
          created_from_room_code.owner_token.empty());
    const auto created_from_plain =
        parse_relay_room_create_response_impl(" ROOM-C ");
    check(created_from_plain.room_code == "ROOM-C");
    try
    {
        (void)parse_relay_room_create_response_impl(" \n\t ");
        failed = true;
    }
    catch (const std::runtime_error&)
    {
        ++checks;
    }

    check(split_top_level_json_objects("").empty());
    check(split_top_level_json_objects(R"({"not_rooms":[]})").empty());
    check(split_top_level_json_objects(R"(not-json)").empty());
    const auto split_objects = split_top_level_json_objects(
        R"([{"code":"A","note":"literal { brace }"},{"code":"B","note":"escaped \\ slash"}])");
    check(split_objects.size() == 2);
    const auto relay_rooms = parse_relay_room_list(
        R"([{"campaign_hash":"skip"},{"code":" room-1 ","campaign_hash":"hash","campaign_name":"Campaign","host_name":"Host","player_count":3,"created_at":55}])");
    check(relay_rooms.size() == 1 &&
          relay_rooms[0].code == "ROOM-1" &&
          relay_rooms[0].player_count == 3 &&
          relay_rooms[0].created_at_ms == 55);
    const auto wrapped_relay_rooms =
        parse_relay_room_list(R"({"rooms":[{"code":"room-2"}]})");
    check(wrapped_relay_rooms.size() == 1);
    check(!wrapped_relay_rooms.empty() &&
          wrapped_relay_rooms[0].code == "ROOM-2" &&
          wrapped_relay_rooms[0].campaign_hash.empty() &&
          wrapped_relay_rooms[0].player_count == 0 &&
          wrapped_relay_rooms[0].created_at_ms == 0);

    SaveData save;
    save.current_campaign = "campaign-one";
    save.scen_num = 4;
    save.allied_mode = 1;
    save.my_team = 2;
    auto make_member = [](int family,
                          const std::string& name,
                          short team,
                          int id) {
        auto member = std::make_unique<guy>(family);
        member->name = name;
        member->teamnum = team;
        member->id = id;
        member->strength = static_cast<short>(10 + id);
        member->dexterity = static_cast<short>(11 + id);
        member->constitution = static_cast<short>(12 + id);
        member->intelligence = static_cast<short>(13 + id);
        member->armor = static_cast<short>(14 + id);
        member->exp = static_cast<std::uint32_t>(100 + id);
        member->kills = static_cast<short>(id);
        member->level_kills = id + 1;
        member->total_damage = id + 2;
        member->total_hits = id + 3;
        member->total_shots = id + 4;
        member->scen_damage = static_cast<float>(id + 5);
        member->scen_kills = static_cast<short>(id + 6);
        member->scen_damage_taken = static_cast<float>(id + 7);
        member->scen_min_hp = static_cast<float>(id + 8);
        member->scen_shots = static_cast<short>(id + 9);
        member->scen_hits = static_cast<short>(id + 10);
        member->level = static_cast<short>(id + 11);
        return member;
    };
    save.team_list[0] = make_member(0, "Local A", 2, 1);
    save.team_list[2] = make_member(1, "Local B", 2, 2);
    save.team_list[3] = make_member(2, "Other Team", 3, 3);
    save.team_size = 3;
    save.numplayers = 1;

    check(resolve_initial_local_team(save) == 2);
    save.my_team = 7;
    check(resolve_initial_local_team(save) == 2);
    SaveData empty_save;
    check(resolve_initial_local_team(empty_save) == 0);
    check(gameplay_start_team(0, 3) == 3);
    check(gameplay_start_team(1, 3) == 0);

    std::array<bool, MAX_TEAM_SIZE> excluded_slots{};
    excluded_slots.fill(false);
    excluded_slots[2] = true;
    og::sim::LobbyPlayer local_player =
        build_local_lobby_player(save, "local-player", 2, &excluded_slots);
    check(local_player.name == "local-player" &&
          local_player.character_slots.size() == 1 &&
          local_player.character_slots[0].slot_index == 0);
    og::sim::LobbyMessage join_message =
        make_join_message(save, "local-player", 2, &excluded_slots);
    check(join_message.kind() == og::sim::LobbyMessageKind::Join);
    check(std::get<og::sim::LobbyJoinMessage>(join_message.payload)
              .extra_players.empty());

    // Multi-seat declarations: derived names, distinct seat teams (tracked
    // seat-0 team first, then the save's remaining teams, then free padding).
    check(derive_local_seat_name("base", 0) == "base");
    check(derive_local_seat_name("base", 2) == "base#2");
    check(is_local_seat_name("base", "base"));
    check(is_local_seat_name("base#3", "base"));
    check(!is_local_seat_name("base3", "base"));
    check(!is_local_seat_name("other", "base"));
    const std::vector<short> declared_teams =
        resolve_local_seat_declaration_teams(save, 2, 3);
    check(declared_teams.size() == 3 &&
          declared_teams[0] == 2 &&
          declared_teams[1] == 3 &&
          declared_teams[2] == 0);
    const std::vector<og::sim::LobbyPlayer> declared_seats =
        build_local_lobby_seats(save, "local-player", 2, 2, &excluded_slots);
    check(declared_seats.size() == 2 &&
          declared_seats[0].name == "local-player" &&
          declared_seats[1].name == "local-player#1" &&
          declared_seats[0].team == 2 &&
          declared_seats[1].team == 3);
    const og::sim::LobbyMessage multi_join_message =
        make_join_message(save, "local-player", 2, &excluded_slots, 2);
    const auto& multi_join =
        std::get<og::sim::LobbyJoinMessage>(multi_join_message.payload);
    check(multi_join.player.name == "local-player" &&
          multi_join.extra_players.size() == 1 &&
          multi_join.extra_players[0].name == "local-player#1");
    og::sim::LobbyMessage settings_message = make_settings_message(save);
    check(settings_message.kind() == og::sim::LobbyMessageKind::SettingsChange);

    og::sim::LobbyState state;
    state.settings.campaign_id = "";
    state.settings.scenario_id = 0;
    state.settings.difficulty = 5;
    state.settings.allied_mode = 1;
    state.host_player_id = 1;
    local_player.player_index = 1;
    local_player.is_host = true;
    og::sim::LobbyPlayer remote_player;
    remote_player.player_index = 2;
    remote_player.name = "remote-player";
    remote_player.team = 3;
    remote_player.character_slots.push_back(local_player.character_slots[0]);
    remote_player.character_slots[0].slot_index = 5;
    remote_player.character_slots[0].character.name = "Remote";
    remote_player.character_slots[0].character.teamnum = 3;
    state.players = {remote_player, local_player};

    check(find_local_player(state, "local-player") != nullptr);
    check(find_local_player(state, "missing") == nullptr);
    check(local_player_is_host(state, "local-player"));
    check(!local_player_is_host(state, "remote-player"));
    const auto equivalent =
        build_save_data_equivalent_from_state(state, false);
    check(equivalent.current_campaign == std::string(kDefaultCampaignId) &&
          equivalent.scen_num == 1 &&
          equivalent.numplayers == 1 &&
          equivalent.team_list.size() == 2 &&
          equivalent.team_list[0].character.teamnum == 0);
    check(build_save_data_equivalent_from_state(state, true).numplayers == 0);
    const auto remote_slots =
        build_remote_owned_slot_mask(state, "local-player");
    check(remote_slots[1]);
    SaveData applied_save;
    apply_lobby_state_to_save(state, applied_save, true, 4);
    check(applied_save.current_campaign == std::string(kDefaultCampaignId) &&
          applied_save.scen_num == 1 &&
          applied_save.numplayers == 0 &&
          applied_save.my_team == 4 &&
          applied_save.team_size == 2);

    og::sim::LobbyState oversized_state = state;
    oversized_state.players.clear();
    oversized_state.players.push_back(remote_player);
    oversized_state.players[0].character_slots.clear();
    for (std::uint8_t slot = 0; slot < MAX_TEAM_SIZE + 1; ++slot)
    {
        og::sim::LobbyCharacterSlot lobby_slot = local_player.character_slots[0];
        lobby_slot.slot_index = slot;
        oversized_state.players[0].character_slots.push_back(lobby_slot);
    }
    apply_lobby_state_to_save(oversized_state, applied_save, false, 1);
    check(applied_save.team_size == MAX_TEAM_SIZE);

    class RawTransport final : public og::sim::ITransport {
    public:
        std::vector<og::sim::ReceivedMessage> incoming;
        std::vector<std::vector<std::uint8_t>> sent;

        void send(og::sim::PeerId,
                  const std::uint8_t* data,
                  std::size_t len) override
        {
            sent.emplace_back(data, data + len);
        }

        std::vector<og::sim::ReceivedMessage> poll() override
        {
            auto result = std::move(incoming);
            incoming.clear();
            return result;
        }

        void accept_connections() override {}
        void disconnect(og::sim::PeerId) override {}
        std::vector<og::sim::PeerId> connected_peers() const override
        {
            return {7};
        }
    };

    RawTransport raw_transport;
    raw_transport.incoming.push_back({1, {0}});
    std::vector<std::uint8_t> unknown_message;
    og::sim::append_transport_header(unknown_message, 99, 0);
    raw_transport.incoming.push_back({2, unknown_message});
    std::vector<std::uint8_t> malformed_lobby;
    og::sim::append_transport_header(
        malformed_lobby,
        og::sim::kLobbyMessageType,
        0);
    raw_transport.incoming.push_back({3, malformed_lobby});
    raw_transport.incoming.push_back(
        {4, og::sim::serialize_lobby_message(join_message)});
    raw_transport.incoming.push_back(
        {5, og::sim::serialize_lobby_state_message(state)});
    const auto typed_from_raw = poll_lobby_transport_messages(raw_transport);
    check(typed_from_raw.size() == 2 &&
          typed_from_raw[0].kind == og::sim::TypedReceivedMessageKind::LobbyMessage &&
          typed_from_raw[1].kind == og::sim::TypedReceivedMessageKind::LobbyState);
    send_lobby_message(raw_transport, 9, std::move(join_message));
    check(!raw_transport.sent.empty());

    class TypedTransport final : public og::sim::ITransport {
    public:
        std::vector<og::sim::TypedReceivedMessage> typed;

        bool supports_typed_messages() const noexcept override
        {
            return true;
        }

        std::vector<og::sim::TypedReceivedMessage> poll_typed() override
        {
            return std::move(typed);
        }

        void send(og::sim::PeerId,
                  const std::uint8_t*,
                  std::size_t) override {}
        std::vector<og::sim::ReceivedMessage> poll() override
        {
            return {};
        }
        void accept_connections() override {}
        void disconnect(og::sim::PeerId) override {}
        std::vector<og::sim::PeerId> connected_peers() const override
        {
            return {};
        }
    };

    TypedTransport typed_transport;
    og::sim::TypedReceivedMessage typed_message;
    typed_message.peer_id = 1;
    typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyState;
    typed_message.lobby_state = std::make_shared<og::sim::LobbyState>(state);
    typed_transport.typed.push_back(std::move(typed_message));
    check(poll_lobby_transport_messages(typed_transport).size() == 1);

    check(resolve_pending_local_team(state, -1, "local-player") == 0);
    og::sim::LobbyState full_state;
    for (short team = 0; team < MAX_PLAYERS; ++team)
    {
        og::sim::LobbyPlayer player;
        player.name = std::format("player-{}", team);
        player.team = team;
        full_state.players.push_back(std::move(player));
    }
    check(resolve_pending_local_team(full_state, 3, "local-player") == 3);
    og::sim::LobbyPlayer empty_pending;
    check(build_pending_local_lobby_state(
              state,
              {empty_pending},
              1,
              "pending-player").state.players.size() == state.players.size());
    og::sim::LobbyPlayer pending_player = local_player;
    pending_player.name = "pending-player";
    // state has allied_mode = 1, so cross-peer team sharing is allowed and
    // the requested team 3 sticks even though "remote-player" holds it.
    const auto pending_state = build_pending_local_lobby_state(
        state,
        {pending_player},
        3,
        "pending-player");
    check(pending_state.state.players.size() == state.players.size() + 1 &&
          pending_state.local_team == 3);
    check(build_pending_local_lobby_state(
              state,
              {pending_player},
              3,
              "local-player").state.players.size() == state.players.size());
    // Exclusive lobbies (allied off, non-CTF) keep the old bounce-to-free-team
    // resolution.
    og::sim::LobbyState exclusive_state = state;
    exclusive_state.settings.allied_mode = 0;
    const auto exclusive_pending = build_pending_local_lobby_state(
        exclusive_state,
        {pending_player},
        3,
        "pending-player");
    check(exclusive_pending.local_team == 0);
    // Multi-seat pending merges keep within-machine teams distinct.
    og::sim::LobbyPlayer pending_seat1 = pending_player;
    pending_seat1.name = "pending-player#1";
    pending_seat1.team = 3;
    const auto multi_pending = build_pending_local_lobby_state(
        state,
        {pending_player, pending_seat1},
        3,
        "pending-player");
    check(multi_pending.state.players.size() == state.players.size() + 2 &&
          multi_pending.local_team == 3 &&
          multi_pending.state.players.back().team == 0);

    check(build_host_transport_failure_message("direct failed", "relay failed") ==
          "Direct: direct failed\nRelay: relay failed");
    check(build_host_transport_failure_message("direct failed", "") ==
          "direct failed");
    check(build_host_transport_failure_message("", "relay failed") ==
          "relay failed");
    check(build_host_transport_failure_message("", "") ==
          "Unable to host a network lobby.");

    return failed ? -failed_check : 0;
}
// GCOVR_EXCL_STOP

} // namespace og::ui::detail
#endif

namespace og::ui {

std::vector<std::string> build_host_picker_status_lines(
    const std::string& direct_address,
    bool has_direct_transport,
    int port,
    const std::string& direct_status_message,
    const std::string& relay_room_code,
    const std::string& relay_status_message,
    std::optional<std::size_t> player_count)
{
    std::vector<std::string> lines;
    if (has_direct_transport)
    {
        lines.push_back(
            std::format("LAN: {}:{}",
                        direct_address,
                        port));
    }
    else if (!direct_status_message.empty())
    {
        lines.push_back(std::format("Direct: {}", direct_status_message));
    }
    if (!relay_room_code.empty())
        lines.push_back(std::format("Relay: {}", relay_room_code));
    else if (!relay_status_message.empty())
        lines.push_back(std::format("Relay: {}", relay_status_message));
    if (player_count.has_value())
    {
        lines.push_back(
            std::format("Lobby: {} player{}",
                        *player_count,
                        *player_count == 1 ? "" : "s"));
    }
    return lines;
}

} // namespace og::ui

namespace {

class HostPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    explicit HostPickerLobbyClient(og::ui::PickerHostGameOptions options)
        : options_(std::move(options))
        , player_name_(make_network_player_name())
    {
    }

    void initialize_from_save() override
    {
        shutdown();
        clear_active_gameplay_shadow();

        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        spectator_mode_ = og::ui::is_spectator_mode(*save);
        local_team_ = resolve_initial_local_team(*save);
        // Honor the main menu's 1..4-player mode: a networked host declares
        // one lobby seat per local player (0 = spectator, unchanged).
        local_player_count_ = spectator_mode_
            ? 0
            : std::clamp<int>(save->numplayers, 1, MAX_PLAYERS);
        save->numplayers = static_cast<unsigned char>(local_player_count_);
        save->my_team = local_team_;
        direct_address_ = detect_lan_ipv4_address();

        local_server_transport_ = og::sim::InProcessTransport::create_server();
        local_server_transport_->accept_connections();
        local_client_transport_ = local_server_transport_->create_client_transport();

        relay_room_code_.clear();
        relay_status_message_.clear();
        direct_status_message_.clear();

        if (options_.enable_relay)
        {
            try
            {
                const std::string relay_base_url =
                    relay_base_url_or_default(options_.relay_base_url);
                const og::ui::detail::RelayRoomCreateInfo relay_room =
                    create_relay_room(
                    relay_base_url,
                    current_campaign_hash(),
                    current_campaign_name(),
                    current_host_name(local_team_));
                relay_room_code_ = og::ui::normalize_relay_room_code(
                    relay_room.room_code);
                relay_transport_ =
                    std::make_shared<og::sim::RelayWebSocketTransport>(
                        og::ui::detail::build_relay_room_websocket_url(
                            relay_base_url,
                            relay_room_code_,
                            relay_room.owner_token));
                relay_transport_->accept_connections();
            }
            catch (const std::exception& error)
            {
                relay_room_code_.clear();
                relay_transport_.reset();
                relay_status_message_ = error.what();
            }
        }

#ifdef __EMSCRIPTEN__
        direct_status_message_ =
            "Direct hosting is unavailable in browser builds.";
#else
        try
        {
            websocket_server_transport_ =
                std::make_shared<og::sim::WebSocketServerTransport>(options_.port);
        }
        catch (const std::exception& error)
        {
            websocket_server_transport_.reset();
            direct_status_message_ = error.what();
        }
#endif

        if (!websocket_server_transport_ && !relay_transport_)
        {
            throw std::runtime_error(build_host_transport_failure_message(
                direct_status_message_,
                relay_status_message_));
        }

        std::vector<std::shared_ptr<og::sim::ITransport>> transports;
        transports.push_back(local_server_transport_);
        if (websocket_server_transport_)
            transports.push_back(websocket_server_transport_);
        if (relay_transport_)
            transports.push_back(relay_transport_);

        combined_transport_ = std::make_shared<og::sim::MultiplexTransport>(
            std::move(transports));
        combined_transport_->accept_connections();
        server_ = std::make_unique<og::sim::LobbyServer>(*combined_transport_);

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_settings_message(*save));
        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_join_message(
                *save,
                player_name_,
                local_team_,
                &remote_owned_save_slots_,
                join_seat_count()));

        poll_messages();
        apply_state_to_current_save();
        rebuild_status_lines();
    }

    void shutdown() override
    {
        pending_game_start_config_.reset();
        player_bindings_.clear();
        state_.reset();
        status_lines_.clear();
        server_.reset();
        combined_transport_.reset();
        // Stop the relay websocket client first: its reset joins the
        // ix::WebSocket background thread that spawns DNS lookups, so the
        // listening-socket teardown below does not overlap our own in-flight
        // getaddrinfo work (detached resolver threads can still outlive this;
        // the upstream IXSocketServer stop() fix is the load-bearing guard).
        relay_transport_.reset();
        websocket_server_transport_.reset();
        local_client_transport_.reset();
        local_server_transport_.reset();
        spectator_mode_ = false;
        local_player_count_ = 1;
        local_team_ = 0;
        start_request_pending_ = false;
        remote_owned_save_slots_.fill(false);
        direct_address_.clear();
        relay_room_code_.clear();
        relay_status_message_.clear();
        direct_status_message_.clear();
    }

    void sync_from_save() override
    {
        send_settings_from_save();
        send_join_from_save();
        poll_and_apply();
    }

    void sync_roster_from_save() override
    {
        send_join_from_save();
        poll_and_apply();
    }

    void sync_settings_from_save() override
    {
        send_settings_from_save();
        poll_and_apply();
    }

    void poll_and_apply() override
    {
        poll_messages();
        apply_state_to_current_save();
        rebuild_status_lines();
    }

    void set_player_mode(int player_count) override
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        spectator_mode_ = player_count == 0;
        local_player_count_ =
            std::clamp<int>(player_count, 0, MAX_PLAYERS);
        save->numplayers = static_cast<unsigned char>(local_player_count_);
        send_join_from_save();
        poll_and_apply();
    }

    bool request_start_game() override
    {
        if (!local_client_transport_ || !state_.has_value() ||
            !og::ui::detail::local_player_is_host(*state_, player_name_))
        {
            return false;
        }

        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_, player_name_);
        if (local_player == nullptr)
            return false;

        pending_game_start_config_.reset();
        start_request_pending_ = true;

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local_player->player_index,
        };
        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            std::move(message));

        poll_and_apply();
        if (g_start_game_requested && server_ != nullptr)
        {
            player_bindings_ = server_->build_player_bindings();
            pending_game_start_config_ = build_game_start_config();
        }
        return g_start_game_requested;
    }

    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        if (server_ == nullptr)
            return std::nullopt;

        og::ui::PickerLobbyGameStartConfig config;
        config.save_data = server_->build_save_data_equivalent();
        // Capture this machine's seats from the FINAL authoritative state:
        // player indices are recomputed on every lobby edit, so an earlier
        // echo could silently drive the wrong walkers.
        const og::sim::LobbyState& authoritative_state = server_->state();
        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(authoritative_state, player_name_);
        config.save_data.numplayers = spectator_mode_
            ? 0u
            : static_cast<unsigned char>(std::clamp<std::size_t>(
                  local_seats.size(), 1u,
                  static_cast<std::size_t>(MAX_PLAYERS)));
        config.difficulty = state_.has_value()
            ? static_cast<std::int16_t>(state_->settings.difficulty)
            : static_cast<std::int16_t>(authoritative_state.settings.difficulty);
        config.my_team = gameplay_start_team(config.save_data.allied_mode,
                                             local_team_);
        config.is_networked = true;
        config.local_player_index =
            static_cast<std::uint8_t>(authoritative_state.host_player_id);
        if (!spectator_mode_)
        {
            for (const og::sim::LobbyPlayer* const seat : local_seats)
            {
                config.local_player_indices.push_back(seat->player_index);
                config.local_seat_teams.push_back(gameplay_start_team(
                    config.save_data.allied_mode,
                    static_cast<short>(seat->team)));
            }
        }
        return config;
    }

    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        std::optional<og::ui::PickerLobbyGameStartConfig> config =
            std::move(pending_game_start_config_);
        pending_game_start_config_.reset();
        return config;
    }

    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return start_request_pending_;
    }

    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return pending_game_start_config_.has_value();
    }

    [[nodiscard]] std::vector<std::string> status_lines() const override
    {
        return status_lines_;
    }

    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        if (!state_.has_value())
            return server_ != nullptr;
        return og::ui::detail::local_player_is_host(*state_, player_name_);
    }

    [[nodiscard]] bool is_save_slot_editable(
        std::size_t slot_index) const noexcept override
    {
        return slot_index < remote_owned_save_slots_.size() &&
            !remote_owned_save_slots_[slot_index];
    }

    bool request_team_change(short team) override
    {
        if (!local_client_transport_)
            return false;

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_team_change_message(local_player_index(), team));
        poll_and_apply();
        return local_team_ == team;
    }

    bool set_ready(bool ready) override
    {
        if (!local_client_transport_)
            return false;

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_ready_message(local_player_index(), ready));
        poll_and_apply();
        return local_ready() == ready;
    }

    [[nodiscard]] bool local_ready() const noexcept override
    {
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_, player_name_);
        return local_player != nullptr && local_player->ready;
    }

    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players() const override
    {
        if (!state_.has_value())
            return {};
        return state_->players;
    }

    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }

    bool install_gameplay_runtime(og::runtime::GameSession& session,
                                  screen& gameplay_screen)
    {
        if (!combined_transport_ || !local_client_transport_)
            return false;

        const bool using_relay = static_cast<bool>(relay_transport_);
        session.relay_transport_active_ = using_relay;
        session.relay_speed_warning_shown_ = false;

        if (player_bindings_.empty() && server_ != nullptr)
            player_bindings_ = server_->build_player_bindings();

        og::runtime::reset_network_host_transport_shadow(
            session,
            gameplay_screen,
            combined_transport_,
            local_client_transport_,
            player_bindings_);

        // Keep the lobby connection ALIVE across gameplay so the team-build menu
        // can start the next level over it (single-player parity). The runtime
        // holds its own shared_ptr refs to the transports, so they outlive the
        // per-level runtime; the LobbyServer stays alive but dormant (the lobby
        // is polled only from the menu, never the game loop) and resumes via
        // resume_after_level(). Only the per-start scratch state is cleared.
        player_bindings_.clear();
        start_request_pending_ = false;
        pending_game_start_config_.reset();
        return true;
    }

    void resume_after_level() override
    {
        g_start_game_requested = false;
        start_request_pending_ = false;
        pending_game_start_config_.reset();
        player_bindings_.clear();

        // If the connection didn't survive (shutdown, or a transport that can't
        // persist), fall back to a fresh lobby from the save.
        if (combined_transport_ == nullptr || local_client_transport_ == nullptr ||
            server_ == nullptr)
        {
            initialize_from_save();
            return;
        }

        // The LobbyServer locked itself when the previous level started; re-open
        // it so the next round's settings/joins are accepted again.
        server_->unlock_for_new_round();

        // Reuse the live connection: re-broadcast the advanced campaign cursor
        // (scen_num) and this host's own roster from the reloaded save0, then
        // poll so the LobbyServer re-converges every still-connected peer on the
        // next level. (sync_from_save = settings + join + poll.)
        sync_from_save();
    }

private:
    [[nodiscard]] std::uint8_t local_player_index() const
    {
        // The LobbyServer attributes messages to the SENDING PEER; the index
        // is informational, so an unknown index (0xff) is harmless.
        if (!state_.has_value())
            return 0xffu;
        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_, player_name_);
        return local_player != nullptr ? local_player->player_index : 0xffu;
    }

    // Seats declared in a join: a spectator still occupies one lobby seat
    // (unchanged single-seat path); otherwise one seat per local player.
    [[nodiscard]] std::size_t join_seat_count() const noexcept
    {
        return spectator_mode_
            ? 1u
            : static_cast<std::size_t>(
                  std::clamp<int>(local_player_count_, 1, MAX_PLAYERS));
    }

    void send_settings_from_save()
    {
        if (!local_client_transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;
        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_settings_message(*save));
    }

    void send_join_from_save()
    {
        if (!local_client_transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_join_message(
                *save,
                player_name_,
                local_team_,
                &remote_owned_save_slots_,
                join_seat_count()));
    }

    void handle_typed_message(const og::sim::TypedReceivedMessage& message)
    {
        switch (message.kind)
        {
        case og::sim::TypedReceivedMessageKind::LobbyState:
            if (message.lobby_state)
            {
                state_ = *message.lobby_state;
                if (const og::sim::LobbyPlayer* const local_player =
                        og::ui::detail::find_local_player(*state_, player_name_))
                {
                    local_team_ = local_player->team;
                }
            }
            break;

        case og::sim::TypedReceivedMessageKind::LobbyMessage:
            if (message.lobby_message &&
                message.lobby_message->kind() ==
                    og::sim::LobbyMessageKind::StartGame)
            {
                start_request_pending_ = false;
                g_start_game_requested = true;
                if (server_ != nullptr)
                    player_bindings_ = server_->build_player_bindings();
                pending_game_start_config_ = build_game_start_config();
            }
            break;

        default:
            break;
        }
    }

    void poll_messages()
    {
        if (server_ == nullptr || local_client_transport_ == nullptr)
            return;

        server_->poll_incoming_messages();
        for (const og::sim::TypedReceivedMessage& message :
             poll_lobby_transport_messages(*local_client_transport_))
        {
            handle_typed_message(message);
        }
    }

    void apply_state_to_current_save()
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr || !state_.has_value())
            return;

        // Adopt the authoritative seat count (the server truncates a join
        // that exceeded lobby capacity).
        if (!spectator_mode_)
        {
            const std::vector<const og::sim::LobbyPlayer*> local_seats =
                og::ui::detail::find_local_seats(*state_, player_name_);
            if (!local_seats.empty())
                local_player_count_ = static_cast<int>(local_seats.size());
        }

        og::ui::detail::apply_lobby_state_to_save(
            *state_,
            *save,
            spectator_mode_,
            local_team_,
            static_cast<std::size_t>(std::max(local_player_count_, 0)));
        remote_owned_save_slots_ =
            og::ui::detail::build_remote_owned_slot_mask(*state_, player_name_);
    }

    void rebuild_status_lines()
    {
        std::string relay_room_code = relay_room_code_;
        std::string relay_status_message = relay_status_message_;
        if (relay_transport_ && !relay_room_code_.empty())
        {
            const og::sim::TransportLinkState relay_link =
                relay_transport_->link_state();
            if (relay_link == og::sim::TransportLinkState::Failed ||
                relay_link == og::sim::TransportLinkState::Lost)
            {
                // The room was created but the relay socket is gone: stop
                // advertising a dead room code and surface the drop instead.
                relay_room_code.clear();
                relay_status_message = "connection lost";
            }
        }
        status_lines_ = og::ui::build_host_picker_status_lines(
            direct_address_,
            static_cast<bool>(websocket_server_transport_),
            options_.port,
            direct_status_message_,
            relay_room_code,
            relay_status_message,
            state_.has_value()
                ? std::optional<std::size_t>(state_->players.size())
                : std::nullopt);
    }

    og::ui::PickerHostGameOptions options_;
    std::string player_name_;
    std::string direct_address_;
    std::shared_ptr<og::sim::InProcessTransport> local_server_transport_;
    std::shared_ptr<og::sim::InProcessTransport> local_client_transport_;
    std::shared_ptr<og::sim::ITransport> combined_transport_;
    std::shared_ptr<og::sim::ITransport> websocket_server_transport_;
    std::shared_ptr<og::sim::RelayWebSocketTransport> relay_transport_;
    std::unique_ptr<og::sim::LobbyServer> server_;
    std::optional<og::sim::LobbyState> state_;
    std::vector<og::sim::LobbyPlayerBinding> player_bindings_;
    std::vector<std::string> status_lines_;
    std::array<bool, MAX_TEAM_SIZE> remote_owned_save_slots_{};
    bool spectator_mode_ = false;
    // Local seats this machine declares (0 = spectator, 1..MAX_PLAYERS).
    int local_player_count_ = 1;
    short local_team_ = 0;
    bool start_request_pending_ = false;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
    std::string relay_room_code_;
    std::string relay_status_message_;
    std::string direct_status_message_;
};

class JoinPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    explicit JoinPickerLobbyClient(og::ui::PickerJoinGameOptions options)
        : options_(std::move(options))
        , player_name_(make_network_player_name())
    {
    }

    void initialize_from_save() override
    {
        shutdown();
        clear_active_gameplay_shadow();

        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        spectator_mode_ = og::ui::is_spectator_mode(*save);
        local_team_ = resolve_initial_local_team(*save);
        // Honor the main menu's 1..4-player mode: a networked joiner declares
        // one lobby seat per local player (0 = spectator, unchanged).
        local_player_count_ = spectator_mode_
            ? 0
            : std::clamp<int>(save->numplayers, 1, MAX_PLAYERS);
        save->numplayers = static_cast<unsigned char>(local_player_count_);
        save->my_team = local_team_;
        pending_local_seats_ = og::ui::detail::build_local_lobby_seats(
            *save,
            player_name_,
            local_team_,
            join_seat_count(),
            &remote_owned_save_slots_);

        server_peer_id_ = 1;
        server_peer_id_adopted_ = false;
        relay_room_code_.clear();
        direct_url_.clear();

        if (options_.mode == og::ui::PickerJoinMode::Direct)
        {
            direct_url_ = og::ui::normalize_direct_websocket_url(
                options_.direct_endpoint);
#ifdef __EMSCRIPTEN__
            transport_ =
                std::make_shared<og::sim::EmscriptenWebSocketTransport>(
                    direct_url_);
#else
            transport_ =
                std::make_shared<og::sim::WebSocketClientTransport>(direct_url_);
#endif
        }
        else
        {
            relay_room_code_ = og::ui::normalize_relay_room_code(
                options_.room_code);
            transport_ = std::make_shared<og::sim::RelayWebSocketTransport>(
                og::ui::detail::build_relay_room_websocket_url(
                    relay_base_url_or_default(options_.relay_base_url),
                    relay_room_code_));
        }

        transport_->accept_connections();
        join_message_sent_ = false;
        settings_dirty_ = true;

        poll_and_apply();
    }

    void shutdown() override
    {
        pending_game_start_config_.reset();
        state_.reset();
        status_lines_.clear();
        transport_.reset();
        direct_url_.clear();
        relay_room_code_.clear();
        spectator_mode_ = false;
        local_player_count_ = 1;
        local_team_ = 0;
        start_request_pending_ = false;
        remote_owned_save_slots_.fill(false);
        join_message_sent_ = false;
        settings_dirty_ = false;
        server_peer_id_ = 1;
        server_peer_id_adopted_ = false;
        pending_local_seats_.clear();
    }

    void sync_from_save() override
    {
        settings_dirty_ = true;
        join_message_sent_ = false;
        poll_and_apply();
    }

    void sync_roster_from_save() override
    {
        join_message_sent_ = false;
        poll_and_apply();
    }

    void sync_settings_from_save() override
    {
        settings_dirty_ = true;
        poll_and_apply();
    }

    void poll_and_apply() override
    {
        if (!transport_)
        {
            rebuild_status_lines();
            return;
        }

        drain_messages();
        update_server_peer_id();
        if (transport_->connected_peers().empty())
        {
            join_message_sent_ = false;
            rebuild_status_lines();
            return;
        }

        if (settings_dirty_ && local_player_is_host())
        {
            send_settings_from_save();
            settings_dirty_ = false;
        }
        if (!join_message_sent_)
        {
            send_join_from_save();
            join_message_sent_ = true;
        }

        drain_messages();
        apply_state_to_current_save();
        rebuild_status_lines();
    }

    void set_player_mode(int player_count) override
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        spectator_mode_ = player_count == 0;
        local_player_count_ =
            std::clamp<int>(player_count, 0, MAX_PLAYERS);
        save->numplayers = static_cast<unsigned char>(local_player_count_);
        join_message_sent_ = false;
        poll_and_apply();
    }

    bool request_start_game() override
    {
        if (!transport_ || !local_player_is_host())
            return false;

        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_, player_name_);
        if (local_player == nullptr)
            return false;

        pending_game_start_config_.reset();
        start_request_pending_ = true;

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local_player->player_index,
        };
        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            std::move(message));

        poll_and_apply();
        if (g_start_game_requested)
            pending_game_start_config_ = build_game_start_config();
        return g_start_game_requested;
    }

    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        if (!state_.has_value())
            return std::nullopt;

        // Capture this machine's seats from the FINAL authoritative state
        // that accompanied the start (player indices are recomputed on every
        // lobby edit, so an earlier echo could drive the wrong walkers).
        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(*state_, player_name_);

        og::ui::PickerLobbyGameStartConfig config;
        config.save_data =
            og::ui::detail::build_save_data_equivalent_from_state(
                *state_,
                spectator_mode_,
                std::max<std::size_t>(local_seats.size(), 1u));
        config.difficulty =
            static_cast<std::int16_t>(state_->settings.difficulty);
        config.my_team = gameplay_start_team(config.save_data.allied_mode,
                                             local_team_);
        config.is_networked = true;
        if (!local_seats.empty())
            config.local_player_index = local_seats.front()->player_index;
        if (!spectator_mode_)
        {
            for (const og::sim::LobbyPlayer* const seat : local_seats)
            {
                config.local_player_indices.push_back(seat->player_index);
                config.local_seat_teams.push_back(gameplay_start_team(
                    config.save_data.allied_mode,
                    static_cast<short>(seat->team)));
            }
        }
        return config;
    }

    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        std::optional<og::ui::PickerLobbyGameStartConfig> config =
            std::move(pending_game_start_config_);
        pending_game_start_config_.reset();
        return config;
    }

    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return start_request_pending_;
    }

    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return pending_game_start_config_.has_value();
    }

    [[nodiscard]] std::vector<std::string> status_lines() const override
    {
        return status_lines_;
    }

    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return local_player_is_host();
    }

    [[nodiscard]] bool is_save_slot_editable(
        std::size_t slot_index) const noexcept override
    {
        return slot_index < remote_owned_save_slots_.size() &&
            !remote_owned_save_slots_[slot_index];
    }

    bool request_team_change(short team) override
    {
        if (!transport_ || transport_->connected_peers().empty())
            return false;

        const std::uint64_t states_before = lobby_states_received_;
        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            og::ui::detail::make_team_change_message(local_player_index(), team));
        // The remote round trip is asynchronous, but the server answers every
        // request (denials get an explicit state echo): exit on the first
        // received state instead of burning the full window on a bounce.
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            poll_and_apply();
            if (local_team_ == team)
                return true;
            if (lobby_states_received_ != states_before)
                return false;
#ifdef __EMSCRIPTEN__
            // std::this_thread::sleep_for busy-waits on emscripten without
            // returning to the browser event loop, so the WebSocket echo this
            // loop waits for could never arrive in-window. emscripten_sleep
            // yields via Asyncify and lets onmessage fire.
            emscripten_sleep(10);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
        }
        return false;
    }

    bool set_ready(bool ready) override
    {
        if (!transport_ || transport_->connected_peers().empty())
            return false;

        const std::uint64_t states_before = lobby_states_received_;
        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            og::ui::detail::make_ready_message(local_player_index(), ready));
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            poll_and_apply();
            if (local_ready() == ready)
                return true;
            if (lobby_states_received_ != states_before)
                return local_ready() == ready;
#ifdef __EMSCRIPTEN__
            // See request_team_change: sleep_for busy-waits on emscripten.
            emscripten_sleep(10);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
        }
        return false;
    }

    [[nodiscard]] bool local_ready() const noexcept override
    {
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_, player_name_);
        return local_player != nullptr && local_player->ready;
    }

    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players() const override
    {
        if (!state_.has_value())
            return {};
        return state_->players;
    }

    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }

    bool install_gameplay_runtime(og::runtime::GameSession& session,
                                  screen& gameplay_screen)
    {
        if (!transport_)
            return false;

        const bool using_relay =
            dynamic_cast<og::sim::RelayWebSocketTransport*>(transport_.get()) !=
            nullptr;
        session.relay_transport_active_ = using_relay;
        session.relay_speed_warning_shown_ = false;

        // One connection, one GameClient, N local seat bindings: view/input
        // slot k follows the global player_index its seat was assigned, and
        // renders that seat's GAMEPLAY team (allied folds every seat to 0).
        std::vector<og::runtime::LocalSeatBinding> local_seats;
        if (state_.has_value())
        {
            const short allied_mode = state_->settings.allied_mode;
            for (const og::sim::LobbyPlayer* const seat :
                 og::ui::detail::find_local_seats(*state_, player_name_))
            {
                local_seats.push_back(og::runtime::LocalSeatBinding{
                    .player_index = seat->player_index,
                    .team = gameplay_start_team(
                        allied_mode, static_cast<short>(seat->team)),
                });
            }
        }
        if (local_seats.empty())
        {
            local_seats.push_back(og::runtime::LocalSeatBinding{
                .player_index = 0,
                .team = gameplay_screen.save_data.my_team,
            });
        }

        og::runtime::reset_network_client_transport_shadow(
            session,
            gameplay_screen,
            transport_,
            server_peer_id_,
            std::move(local_seats));

        // Keep the connection ALIVE across gameplay (see the host note): the
        // runtime holds its own ref to the transport, so it outlives the
        // per-level runtime, and resume_after_level() rejoins the lobby over the
        // still-open socket. Only the per-start scratch state is cleared.
        start_request_pending_ = false;
        pending_game_start_config_.reset();
        return true;
    }

    void resume_after_level() override
    {
        start_request_pending_ = false;
        pending_game_start_config_.reset();

        // If the socket didn't survive, reconnect from scratch.
        if (transport_ == nullptr)
        {
            initialize_from_save();
            return;
        }

        // Reuse the still-open socket: re-send this peer's join from the
        // reloaded (advanced) save0 and re-poll so the host's LobbyServer
        // re-adds us to the level-N lobby. (sync_from_save re-sends settings +
        // join and polls.)
        sync_from_save();
    }

private:
    [[nodiscard]] bool local_player_is_host() const
    {
        if (!state_.has_value())
            return false;
        return og::ui::detail::local_player_is_host(*state_, player_name_);
    }

    [[nodiscard]] std::uint8_t local_player_index() const
    {
        // Informational only: the LobbyServer keys on the sending peer.
        if (!state_.has_value())
            return 0xffu;
        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_, player_name_);
        return local_player != nullptr ? local_player->player_index : 0xffu;
    }

    // Seats declared in a join: a spectator still occupies one lobby seat
    // (unchanged single-seat path); otherwise one seat per local player.
    [[nodiscard]] std::size_t join_seat_count() const noexcept
    {
        return spectator_mode_
            ? 1u
            : static_cast<std::size_t>(
                  std::clamp<int>(local_player_count_, 1, MAX_PLAYERS));
    }

    void send_settings_from_save()
    {
        if (!transport_ || !state_.has_value())
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            og::ui::detail::make_settings_message(*save));
    }

    void send_join_from_save()
    {
        if (!transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        pending_local_seats_ = og::ui::detail::build_local_lobby_seats(
            *save,
            player_name_,
            local_team_,
            join_seat_count(),
            &remote_owned_save_slots_);

        og::sim::LobbyMessage message;
        og::sim::LobbyJoinMessage join;
        join.player = pending_local_seats_.front();
        join.extra_players.assign(
            pending_local_seats_.begin() + 1,
            pending_local_seats_.end());
        message.payload = std::move(join);
        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            std::move(message));
    }

    void handle_typed_message(const og::sim::TypedReceivedMessage& message)
    {
        switch (message.kind)
        {
        case og::sim::TypedReceivedMessageKind::LobbyState:
            if (message.lobby_state)
            {
                ++lobby_states_received_;
                state_ = *message.lobby_state;
                const std::vector<const og::sim::LobbyPlayer*> local_seats =
                    og::ui::detail::find_local_seats(*state_, player_name_);
                if (!local_seats.empty())
                {
                    pending_local_seats_.clear();
                    for (const og::sim::LobbyPlayer* const seat : local_seats)
                        pending_local_seats_.push_back(*seat);
                    local_team_ = local_seats.front()->team;
                }
            }
            break;

        case og::sim::TypedReceivedMessageKind::LobbyMessage:
            if (message.lobby_message &&
                message.lobby_message->kind() ==
                    og::sim::LobbyMessageKind::StartGame)
            {
                start_request_pending_ = false;
                g_start_game_requested = true;
                pending_game_start_config_ = build_game_start_config();
            }
            break;

        default:
            break;
        }
    }

    void drain_messages()
    {
        if (!transport_)
            return;

        for (const og::sim::TypedReceivedMessage& message :
             poll_lobby_transport_messages(*transport_))
            handle_typed_message(message);
    }

    void update_server_peer_id()
    {
        const auto* const relay_transport =
            dynamic_cast<og::sim::RelayWebSocketTransport*>(transport_.get());
        if (relay_transport == nullptr)
            return;

        // Adopt the relay's announced room host for the INITIAL binding: if
        // the relay assigns the owner a peer id other than 1, messages pinned
        // to 1 would be silently dropped by the transport's remote-peers
        // filter and the lobby would look connected but dead.
        //
        // Adopt exactly once, and only before the first lobby state: relay
        // room host migration is a room-management concept, and the
        // authoritative lobby/game server stays on the originally announced
        // host peer id even if the room host changes later.
        if (lobby_states_received_ == 0 && !server_peer_id_adopted_)
        {
            if (const std::optional<og::sim::PeerId> host =
                    relay_transport->host_peer_id();
                host.has_value())
            {
                server_peer_id_ = *host;
                server_peer_id_adopted_ = true;
            }
        }
    }

    void apply_state_to_current_save()
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr || !state_.has_value())
            return;

        const og::sim::LobbyState* applied_state = &*state_;
        std::optional<og::sim::LobbyState> pending_merged_state;
        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(*state_, player_name_);
        if (!local_seats.empty())
        {
            local_team_ = local_seats.front()->team;
            // Adopt the authoritative seat count (the server truncates a
            // join that exceeded lobby capacity).
            if (!spectator_mode_)
                local_player_count_ = static_cast<int>(local_seats.size());
        }
        else if (!pending_local_seats_.empty())
        {
            PendingLocalLobbyState merged = build_pending_local_lobby_state(
                *state_,
                pending_local_seats_,
                local_team_,
                player_name_);
            local_team_ = merged.local_team;
            pending_merged_state = std::move(merged.state);
            applied_state = &*pending_merged_state;
        }

        og::ui::detail::apply_lobby_state_to_save(
            *applied_state,
            *save,
            spectator_mode_,
            local_team_,
            static_cast<std::size_t>(std::max(local_player_count_, 0)));
        remote_owned_save_slots_ =
            og::ui::detail::build_remote_owned_slot_mask(
                *applied_state,
                player_name_);
    }

    void rebuild_status_lines()
    {
        status_lines_.clear();
        if (!direct_url_.empty())
            status_lines_.push_back(std::format("Direct: {}", direct_url_));
        if (!relay_room_code_.empty())
            status_lines_.push_back(std::format("Relay: {}", relay_room_code_));
        const char* status_text = "Status: connecting";
        if (transport_)
        {
            switch (transport_->link_state())
            {
            case og::sim::TransportLinkState::Connecting:
                break;
            case og::sim::TransportLinkState::Connected:
                status_text = "Status: connected";
                break;
            case og::sim::TransportLinkState::Failed:
                status_text = "Status: connection failed";
                break;
            case og::sim::TransportLinkState::Lost:
                status_text = "Status: connection lost";
                break;
            }
        }
        status_lines_.push_back(status_text);
        if (state_.has_value())
        {
            status_lines_.push_back(
                std::format("Lobby: {} player{}",
                            state_->players.size(),
                            state_->players.size() == 1 ? "" : "s"));
        }
    }

    og::ui::PickerJoinGameOptions options_;
    std::string player_name_;
    std::string direct_url_;
    std::string relay_room_code_;
    std::shared_ptr<og::sim::ITransport> transport_;
    og::sim::PeerId server_peer_id_ = 1;
    // True once the relay's announced room host has been adopted as the
    // authoritative server binding; later room-host migrations must not move
    // it (see update_server_peer_id).
    bool server_peer_id_adopted_ = false;
    std::optional<og::sim::LobbyState> state_;
    std::vector<std::string> status_lines_;
    std::array<bool, MAX_TEAM_SIZE> remote_owned_save_slots_{};
    bool spectator_mode_ = false;
    // Local seats this machine declares (0 = spectator, 1..MAX_PLAYERS).
    int local_player_count_ = 1;
    short local_team_ = 0;
    bool start_request_pending_ = false;
    bool join_message_sent_ = false;
    bool settings_dirty_ = false;
    // Counts every authoritative LobbyState received; request windows use it
    // to detect the server's denial echo without burning the full timeout.
    std::uint64_t lobby_states_received_ = 0;
    // This machine's declared seat list (seat 0 first), used to optimistically
    // merge our seats into echoed states that predate our join.
    std::vector<og::sim::LobbyPlayer> pending_local_seats_;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
};

} // namespace

namespace og::platform {

std::vector<og::ui::PickerRelayRoomInfo> list_platform_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag)
{
    return parse_relay_room_list(
        fetch_relay_room_list_body(
            base_url,
            campaign_content_hash(campaign_tag)));
}

std::unique_ptr<og::ui::IPickerLobbyClient>
create_platform_host_picker_lobby_client(
    const og::ui::PickerHostGameOptions& options)
{
    if (options.port <= 0 || options.port > 65535)
        throw std::invalid_argument("Host port must be in the range 1-65535");
    return std::make_unique<HostPickerLobbyClient>(options);
}

std::unique_ptr<og::ui::IPickerLobbyClient>
create_platform_join_picker_lobby_client(
    const og::ui::PickerJoinGameOptions& options)
{
    if (options.mode == og::ui::PickerJoinMode::Direct &&
        trim_copy(options.direct_endpoint).empty())
    {
        throw std::invalid_argument("Direct connect requires an IP:port");
    }
    if (options.mode == og::ui::PickerJoinMode::Relay &&
        trim_copy(options.room_code).empty())
    {
        throw std::invalid_argument("Relay join requires a room code");
    }
    return std::make_unique<JoinPickerLobbyClient>(options);
}

bool install_picker_lobby_gameplay_runtime(
    og::ui::IPickerLobbyClient* client,
    og::runtime::GameSession& session,
    screen& gameplay_screen)
{
    if (auto* const host_client = dynamic_cast<HostPickerLobbyClient*>(client))
        return host_client->install_gameplay_runtime(session, gameplay_screen);
    if (auto* const join_client = dynamic_cast<JoinPickerLobbyClient*>(client))
        return join_client->install_gameplay_runtime(session, gameplay_screen);
    return false;
}

} // namespace og::platform
