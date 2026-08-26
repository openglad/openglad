#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/net_transport_multiplex.h>
#include <openglad/gameplay/pack_transfer.h>
#include <openglad/core/test_trace.h>
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
#include <openglad/resources/pack_transfer_io.h>
#include <openglad/server/match_stage.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <openglad/platform/net_transport_emscripten_ws.h>
#else
#include "net_transport_websocket_common.h"

#include <ixwebsocket/IXHttpClient.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
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

// picker_dialogs.cpp (trace-only under TESTING): the §4.2 reconcile popup.
void popup_dialog(const char* title, const char* message);

namespace {

constexpr std::string_view kDefaultCampaignId = "gladiator";
constexpr auto kJoinRetryInterval = std::chrono::milliseconds(100);

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
    // Writer-side clamp: keeps serialize/deserialize lossless (see
    // clamp_lobby_guy_name in lobby_state.h).
    character.name = og::sim::clamp_lobby_guy_name(source.name);
    character.family = source.family;
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
    "https://openglad.pages.dev/relay";

#ifdef __EMSCRIPTEN__
EM_JS_DEPS(og_async_relay_room_list, "$stringToNewUTF8");

EM_ASYNC_JS(char*, relay_http_post_text_js, (const char* url_cstr), {
    let timeoutHandle = 0;
    try {
        const url = UTF8ToString(url_cstr);
        // Match the native transport's connect/transfer timeouts: without a
        // signal, a relay that accepts TCP but never responds would keep the
        // wasm Asyncify-suspended (picker frozen, no input) until the
        // browser's own stall timeout, which can be minutes. Promise.race
        // supplies the deadline even on Safari versions without
        // AbortSignal.timeout; AbortController stops the underlying request
        // where supported.
        const options = { method: 'POST' };
        const controller = (typeof AbortController === 'function')
            ? new AbortController()
            : null;
        if (controller)
            options.signal = controller.signal;
        const timeout = new Promise((unusedResolve, reject) => {
            timeoutHandle = setTimeout(() => {
                if (controller)
                    controller.abort();
                reject(new Error('timed out after 10 seconds'));
            }, 10000);
        });
        const response = await Promise.race([fetch(url, options), timeout]);
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
    } finally {
        if (timeoutHandle)
            clearTimeout(timeoutHandle);
    }
});

EM_ASYNC_JS(char*, relay_http_get_text_js, (const char* url_cstr), {
    let timeoutHandle = 0;
    try {
        const url = UTF8ToString(url_cstr);
        // Same stalled-relay guard as relay_http_post_text_js above.
        const options = { method: 'GET' };
        const controller = (typeof AbortController === 'function')
            ? new AbortController()
            : null;
        if (controller)
            options.signal = controller.signal;
        const timeout = new Promise((unusedResolve, reject) => {
            timeoutHandle = setTimeout(() => {
                if (controller)
                    controller.abort();
                reject(new Error('timed out after 10 seconds'));
            }, 10000);
        });
        const response = await Promise.race([fetch(url, options), timeout]);
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
    } finally {
        if (timeoutHandle)
            clearTimeout(timeoutHandle);
    }
});

// Cloud saves (#155): POST with a JSON body. A clone of
// relay_http_post_text_js with options.body + content-type, the same
// Promise.race 10 s timeout, and the same OK/ERR\n<status>\n<body> framing.
EM_ASYNC_JS(char*, relay_http_post_body_text_js,
            (const char* url_cstr, const char* body_cstr), {
    let timeoutHandle = 0;
    try {
        const url = UTF8ToString(url_cstr);
        const options = {
            method: 'POST',
            headers: { 'content-type': 'application/json; charset=utf-8' },
            body: UTF8ToString(body_cstr),
        };
        const controller = (typeof AbortController === 'function')
            ? new AbortController()
            : null;
        if (controller)
            options.signal = controller.signal;
        const timeout = new Promise((unusedResolve, reject) => {
            timeoutHandle = setTimeout(() => {
                if (controller)
                    controller.abort();
                reject(new Error('timed out after 10 seconds'));
            }, 10000);
        });
        const response = await Promise.race([fetch(url, options), timeout]);
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
    } finally {
        if (timeoutHandle)
            clearTimeout(timeoutHandle);
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

EM_JS(int, begin_relay_room_list_request_js, (const char* url_cstr), {
    const root = globalThis;
    let registry = root.__opengladRelayRoomListRequests;
    if (!registry || !(registry.requests instanceof Map)) {
        registry = {
            nextId: 1,
            requests: new Map(),
        };
        root.__opengladRelayRoomListRequests = registry;
    }

    const id = registry.nextId++;
    const state = {
        cancelled: false,
        done: false,
        result: null,
        timedOut: false,
        timeoutHandle: 0,
        controller: (typeof AbortController === 'function')
            ? new AbortController()
            : null,
    };
    registry.requests.set(id, state);

    const options = { method: 'GET' };
    if (state.controller)
        options.signal = state.controller.signal;

    // Promise.race supplies the timeout even on older Safari versions that
    // do not implement AbortSignal.timeout. AbortController is only used to
    // stop the underlying fetch where the browser provides it.
    const timeoutPromise = new Promise((unusedResolve, reject) => {
        state.timeoutHandle = setTimeout(() => {
            state.timedOut = true;
            if (state.controller)
                state.controller.abort();
            reject(new Error('timed out after 10 seconds'));
        }, 10000);
    });
    const fetchPromise = fetch(UTF8ToString(url_cstr), options).then(
        async response => {
            const body = await response.text();
            if (!response.ok) {
                throw new Error(
                    'HTTP ' + String(response.status) + ': ' + body.trim());
            }
            return body;
        });

    Promise.race([fetchPromise, timeoutPromise]).then(
        body => {
            if (state.cancelled)
                return;
            state.result = 'OK\n' + body;
            state.done = true;
        },
        error => {
            if (state.cancelled)
                return;
            const message = state.timedOut
                ? 'Relay room listing timed out after 10 seconds.'
                : 'Relay room listing failed: ' +
                    ((error && error.message) ? error.message : String(error));
            state.result = 'ERR\n' + message;
            state.done = true;
        }).finally(() => {
            if (state.timeoutHandle)
                clearTimeout(state.timeoutHandle);
            state.timeoutHandle = 0;
        });

    return id;
});

EM_JS(char*, poll_relay_room_list_request_js, (int id), {
    const registry = globalThis.__opengladRelayRoomListRequests;
    if (!registry || !(registry.requests instanceof Map))
        return stringToNewUTF8('ERR\nRelay room listing request was lost.');

    const state = registry.requests.get(id);
    if (!state)
        return stringToNewUTF8('ERR\nRelay room listing request was lost.');
    if (!state.done)
        return 0;

    registry.requests.delete(id);
    return stringToNewUTF8(state.result);
});

EM_JS(void, cancel_relay_room_list_request_js, (int id), {
    const registry = globalThis.__opengladRelayRoomListRequests;
    if (!registry || !(registry.requests instanceof Map))
        return;

    const state = registry.requests.get(id);
    if (!state)
        return;
    state.cancelled = true;
    if (state.controller)
        state.controller.abort();
    registry.requests.delete(id);
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
    // IXWebSocket's HTTP client uses the same process-wide socket runtime as
    // its WebSocket transports. Holding the shared guard prevents discovery
    // from running before WSAStartup on Windows, and prevents a concurrently
    // destroyed transport from calling WSACleanup while this request is live.
    og::sim::detail::IxNetSystemGuard net_system_guard;
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

short gameplay_start_team(short allied_mode,
                          short requested_team,
                          short shared_team) noexcept
{
    // allied_mode is retained in old saves and the versioned lobby wire, but
    // a live lobby's explicit per-seat assignment is now authoritative.
    (void)allied_mode;
    (void)shared_team;
    return requested_team;
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

std::string fetch_relay_room_list_body_from_url(const std::string& list_url)
{
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
    // This guard spans the whole HTTP operation, including when this function
    // runs on the owner-managed room-discovery worker. It shares ownership with
    // native WebSocket transports and therefore closes the Windows socket
    // lifetime race between discovery and transport teardown.
    og::sim::detail::IxNetSystemGuard net_system_guard;
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

std::string fetch_relay_room_list_body(std::string_view base_url,
                                       std::string_view campaign_hash)
{
    return fetch_relay_room_list_body_from_url(
        build_relay_room_list_url(std::string(base_url), campaign_hash));
}

og::ui::PickerRelayRoomListResult parse_relay_room_list_result(
    std::string_view body)
{
    og::ui::PickerRelayRoomListResult result;
    try
    {
        result.rooms = parse_relay_room_list(body);
    }
    catch (const std::exception& error)
    {
        result.error = error.what();
    }
    catch (...)
    {
        result.error = "Relay room listing returned invalid data.";
    }
    return result;
}

#ifdef __EMSCRIPTEN__
class EmscriptenRelayRoomListRequest final
    : public og::ui::IPickerRelayRoomListRequest
{
public:
    explicit EmscriptenRelayRoomListRequest(const std::string& list_url)
        : request_id_(begin_relay_room_list_request_js(list_url.c_str()))
    {
    }

    ~EmscriptenRelayRoomListRequest() override
    {
        if (request_id_ != 0)
            cancel_relay_room_list_request_js(request_id_);
    }

    std::optional<og::ui::PickerRelayRoomListResult> poll() override
    {
        if (request_id_ == 0)
            return std::nullopt;

        std::unique_ptr<char, decltype(&std::free)> response(
            poll_relay_room_list_request_js(request_id_),
            &std::free);
        if (!response)
            return std::nullopt;

        request_id_ = 0;
        const std::string encoded_result(response.get());
        constexpr std::string_view ok_prefix = "OK\n";
        constexpr std::string_view error_prefix = "ERR\n";
        if (encoded_result.starts_with(ok_prefix))
        {
            return parse_relay_room_list_result(
                std::string_view(encoded_result).substr(ok_prefix.size()));
        }

        og::ui::PickerRelayRoomListResult result;
        result.error = encoded_result.starts_with(error_prefix)
            ? encoded_result.substr(error_prefix.size())
            : "Relay room listing returned an invalid response.";
        return result;
    }

private:
    int request_id_ = 0;
};
#else
struct NativeRelayRoomListRequestState
{
    std::mutex mutex;
    std::optional<og::ui::PickerRelayRoomListResult> result;
    std::atomic<bool> worker_finished{false};
};

// Owns every native discovery thread until it has actually stopped. Requests
// can still be destroyed immediately (menu BACK/view replacement stays
// nonblocking), while completed threads are reaped on subsequent starts/polls
// and any bounded remainder is joined before process static teardown. The
// long-lived network guard keeps IXWebSocket's socket runtime and its shared
// guard state alive throughout that shutdown join.
class NativeRelayRoomListWorkerOwner
{
public:
    void start(std::shared_ptr<NativeRelayRoomListRequestState> state,
               std::string list_url)
    {
        std::scoped_lock lock(mutex_);
        reap_completed_locked();

        workers_.emplace_back(std::move(state));
        auto worker = std::prev(workers_.end());
        try
        {
            const std::shared_ptr<NativeRelayRoomListRequestState>
                worker_state = worker->state;
            worker->thread = std::thread(
                [worker_state, list_url = std::move(list_url)] {
                    og::ui::PickerRelayRoomListResult result;
                    try
                    {
                        result = parse_relay_room_list_result(
                            fetch_relay_room_list_body_from_url(list_url));
                    }
                    catch (const std::exception& error)
                    {
                        result.error = error.what();
                    }
                    catch (...)
                    {
                        result.error = "Relay room listing failed.";
                    }

                    {
                        std::scoped_lock state_lock(worker_state->mutex);
                        worker_state->result.emplace(std::move(result));
                    }
                    worker_state->worker_finished.store(
                        true,
                        std::memory_order_release);
                });
        }
        catch (...)
        {
            workers_.erase(worker);
            throw;
        }
    }

    void reap_completed()
    {
        std::scoped_lock lock(mutex_);
        reap_completed_locked();
    }

    ~NativeRelayRoomListWorkerOwner()
    {
        // Native HTTP has bounded connect/transfer timeouts. Joining here is
        // allowed to wait at process shutdown, never during picker navigation.
        for (Worker& worker : workers_)
        {
            if (worker.thread.joinable())
                worker.thread.join();
        }
    }

private:
    struct Worker
    {
        explicit Worker(
            std::shared_ptr<NativeRelayRoomListRequestState> state_value)
            : state(std::move(state_value))
        {
        }

        std::shared_ptr<NativeRelayRoomListRequestState> state;
        std::thread thread;
    };

    void reap_completed_locked()
    {
        for (auto worker = workers_.begin(); worker != workers_.end();)
        {
            if (!worker->state->worker_finished.load(
                    std::memory_order_acquire))
            {
                ++worker;
                continue;
            }

            if (worker->thread.joinable())
                worker->thread.join();
            worker = workers_.erase(worker);
        }
    }

    // Constructed before any worker and destroyed after the destructor body,
    // so the socket runtime cannot be torn down before all joins complete.
    og::sim::detail::IxNetSystemGuard net_system_guard_;
    std::mutex mutex_;
    std::list<Worker> workers_;
};

NativeRelayRoomListWorkerOwner& native_relay_room_list_worker_owner()
{
    static NativeRelayRoomListWorkerOwner owner;
    return owner;
}

class NativeRelayRoomListRequest final
    : public og::ui::IPickerRelayRoomListRequest
{
public:
    explicit NativeRelayRoomListRequest(
        std::shared_ptr<NativeRelayRoomListRequestState> state)
        : state_(std::move(state))
    {
    }

    std::optional<og::ui::PickerRelayRoomListResult> poll() override
    {
        std::optional<og::ui::PickerRelayRoomListResult> result;
        {
            std::scoped_lock lock(state_->mutex);
            if (!state_->result.has_value())
                return std::nullopt;

            result = std::move(state_->result);
            state_->result.reset();
        }
        native_relay_room_list_worker_owner().reap_completed();
        return result;
    }

private:
    std::shared_ptr<NativeRelayRoomListRequestState> state_;
};
#endif

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
    // Writer-side label clamps mirror read_lobby_player's, keeping the
    // serializers lossless for anything an honest client sends.
    player.name = og::sim::clamp_lobby_player_label(std::string(player_name));
    // v8: every seat of this machine advertises the active company's display
    // name (SaveData::save_name; <= 40 chars by construction).
    player.company = og::sim::clamp_lobby_player_label(save.save_name);
    player.team = local_team;
    player.ready = false;
    player.is_host = false;

    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        if (excluded_slots != nullptr && (*excluded_slots)[slot_index])
            continue;

        const auto& member = save.team_list[slot_index];
        if (member == nullptr)
            continue;

        player.character_slots.push_back(og::sim::LobbyCharacterSlot{
            .slot_index = static_cast<std::uint8_t>(slot_index),
            .character = make_lobby_character_data(*member),
            // v8: mission-roster selection rides the slot, stamped from the
            // save guy's v14 deploy flag.
            .deployed = member->deployed,
        });
    }

    return player;
}

// Seat naming is display/debug metadata only: seat 0 keeps the machine's base
// name ("net-<hex>"); seat k is derived as base + "#k". Ownership is carried
// exclusively by the server-issued LobbySeatId values in
// LobbyState::local_seat_ids; names are client-controlled and may collide.
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

std::vector<short> seed_local_seat_assignments(const SaveData& save,
                                               bool spectator_mode)
{
    std::vector<short> teams =
        og::ui::derive_local_gameplay_seat_teams(save);
    if (spectator_mode || teams.empty())
        teams.assign(1, resolve_initial_local_team(save));
    return teams;
}

void resize_local_seat_assignments(std::vector<short>& teams,
                                   const SaveData& save,
                                   bool spectator_mode,
                                   int local_player_count)
{
    const std::size_t target_count = spectator_mode
        ? 1u
        : static_cast<std::size_t>(
              std::clamp(local_player_count, 1, MAX_PLAYERS));
    const std::vector<short> seed =
        seed_local_seat_assignments(save, spectator_mode);
    if (teams.size() > target_count)
        teams.resize(target_count);
    while (teams.size() < target_count)
    {
        const std::size_t index = teams.size();
        teams.push_back(index < seed.size()
                ? seed[index]
                : static_cast<short>(index % MAX_PLAYERS));
    }
}

// Build this machine's whole seat list (one LobbyPlayer per local seat).
// Character TEAM is combat allegiance, not ownership or seat assignment.
// Prefer the matching-color seat for stable ownership tags; colors with no
// matching local seat belong to seat 0 so every private roster member still
// enters the lobby exactly once.
std::vector<og::sim::LobbyPlayer> build_local_lobby_seats(
    const SaveData& save,
    std::string_view base_name,
    const std::vector<short>& requested_seat_teams,
    const std::array<bool, MAX_TEAM_SIZE>* excluded_slots = nullptr)
{
    std::vector<short> seat_teams;
    seat_teams.reserve(static_cast<std::size_t>(MAX_PLAYERS));
    for (std::size_t index = 0;
         index < requested_seat_teams.size() &&
             index < static_cast<std::size_t>(MAX_PLAYERS);
         ++index)
    {
        seat_teams.push_back(requested_seat_teams[index]);
    }
    if (seat_teams.empty())
        seat_teams.push_back(resolve_initial_local_team(save));

    std::vector<og::sim::LobbyPlayer> seats;
    seats.reserve(seat_teams.size());
    for (std::size_t seat = 0; seat < seat_teams.size(); ++seat)
    {
        seats.push_back(build_local_lobby_player(
            save,
            derive_local_seat_name(base_name, seat),
            seat_teams[seat],
            excluded_slots));
        seats.back().character_slots.clear();
    }

    for (std::size_t slot_index = 0; slot_index < save.team_list.size();
         ++slot_index)
    {
        if (excluded_slots != nullptr && (*excluded_slots)[slot_index])
            continue;
        const auto& member = save.team_list[slot_index];
        if (member == nullptr)
            continue;

        const auto matching_seat = std::find(
            seat_teams.begin(), seat_teams.end(), member->teamnum);
        const std::size_t owner_seat = matching_seat == seat_teams.end()
            ? 0u
            : static_cast<std::size_t>(matching_seat - seat_teams.begin());
        seats[owner_seat].character_slots.push_back(
            og::sim::LobbyCharacterSlot{
                .slot_index = static_cast<std::uint8_t>(slot_index),
                .character = make_lobby_character_data(*member),
                .deployed = member->deployed,
            });
    }
    return seats;
}

std::vector<og::sim::LobbyPlayer> build_local_lobby_seats(
    const SaveData& save,
    std::string_view base_name,
    short seat0_team,
    std::size_t seat_count,
    const std::array<bool, MAX_TEAM_SIZE>* excluded_slots = nullptr)
{
    const std::size_t count = std::clamp<std::size_t>(
        seat_count, 1u, static_cast<std::size_t>(MAX_PLAYERS));
    return build_local_lobby_seats(
        save,
        base_name,
        resolve_local_seat_declaration_teams(save, seat0_team, count),
        excluded_slots);
}

const og::sim::LobbyPlayer* find_player_by_seat_id(
    const og::sim::LobbyState& state,
    og::sim::LobbySeatId seat_id) noexcept;

// This machine's seats in authoritative local-seat order. The server
// personalizes local_seat_ids for the receiving peer, so no client-controlled
// field (name, company, team, or roster content) participates in ownership.
std::vector<const og::sim::LobbyPlayer*> find_local_seats(
    const og::sim::LobbyState& state)
{
    std::vector<const og::sim::LobbyPlayer*> seats;
    seats.reserve(state.local_seat_ids.size());
    for (const og::sim::LobbySeatId seat_id : state.local_seat_ids)
    {
        if (const og::sim::LobbyPlayer* const player =
                find_player_by_seat_id(state, seat_id))
        {
            seats.push_back(player);
        }
    }
    return seats;
}

std::vector<og::sim::LobbyPlayer> copy_local_seats(
    const og::sim::LobbyState& state)
{
    std::vector<og::sim::LobbyPlayer> seats;
    for (const og::sim::LobbyPlayer* const seat : find_local_seats(state))
    {
        if (seat != nullptr)
            seats.push_back(*seat);
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
            // §4.2: the joiner mirror materializes only DEPLOYED slots,
            // filtered BEFORE densification — must match
            // LobbyServer::build_save_data_equivalent exactly (host and
            // joiner derive the same in-level roster from the same state).
            if (!player.character_slots[slot_order].deployed)
                continue;
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

const og::sim::LobbyPlayer* find_player_by_seat_id(
    const og::sim::LobbyState& state,
    og::sim::LobbySeatId seat_id) noexcept
{
    if (seat_id == og::sim::kInvalidLobbySeatId)
        return nullptr;
    const auto it = std::find_if(
        state.players.begin(),
        state.players.end(),
        [seat_id](const og::sim::LobbyPlayer& player) {
            return player.seat_id == seat_id;
    });
    return it != state.players.end() ? &*it : nullptr;
}

const og::sim::LobbyPlayer* find_local_player(
    const og::sim::LobbyState& state) noexcept
{
    const std::vector<const og::sim::LobbyPlayer*> seats =
        find_local_seats(state);
    return seats.empty() ? nullptr : seats.front();
}

bool local_player_is_host(const og::sim::LobbyState& state) noexcept
{
    const og::sim::LobbyPlayer* const local_player = find_local_player(state);
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
    equivalent.cross_control = state.settings.cross_control;
    equivalent.infinite_gold = state.settings.infinite_gold;
    equivalent.time_limit = state.settings.time_limit;
    equivalent.bot_squad = state.settings.bot_squad;
    equivalent.bot_level = state.settings.bot_level;

    for (const AppliedLobbySlot& slot : collect_applied_lobby_slots(state))
    {
        og::sim::LobbyCharacterSlot compacted = *slot.slot;
        compacted.slot_index = slot.save_slot_index;
        compacted.owner_player_index =
            slot.player != nullptr ? slot.player->player_index : 0xffu;
        // slot_index is the owner's ORIGINAL private-save slot. The applied
        // save_slot_index may be a compacted position in the combined roster
        // and must never be used for owner-filtered persistence.
        compacted.owner_save_slot = slot.slot->slot_index;
        equivalent.team_list.push_back(std::move(compacted));
    }

    og::sim::canonicalize_lobby_gameplay_guy_ids(equivalent.team_list);
    return equivalent;
}

// What applying an authoritative lobby state changed on the PRIVATE save.
// deploy_flags_adopted counts own-seat deploy flags overwritten by the echo
// ([NET-F2] reconciliation); deploy_benched is the subset that flipped
// deployed -> benched (the server's 24-cap force-undeploy). The caller
// autosaves via the §3.8 merge path when adopted > 0 so the machine's next
// join is content-identical (ready survives the convergence loop).
struct LobbyStateApplyResult
{
    int deploy_flags_adopted = 0;
    int deploy_benched = 0;
};

// `adopt_replay_arm` (#207 design point 4, "every machine"): a JOINER's
// cursor writes here are synced, not authored — it cannot click REPLAY, so
// this apply is where its excursion arms. When the synced pair lands the
// cursor on a level completed in THIS machine's own save (same campaign),
// arm with origin = this machine's pre-sync cursor: the joiner's fold then
// restores its own campaign position instead of persisting the walked exit
// (a networked table re-fight never rewrites this machine's cursor — the
// same asymmetry as the dedicated server's lobby-config arm). A landing on
// an uncompleted level, or a campaign switch (no origin to restore to),
// clears the arm; an application that does not move the pair keeps the arm
// exactly as it stands. The HOST passes false: its arm is authored by its
// own REPLAY/VISIT clicks and a stale echo must never overrule them.
LobbyStateApplyResult apply_lobby_state_to_save(
    const og::sim::LobbyState& state,
    SaveData& save,
    bool spectator_mode,
    short local_team,
    std::size_t local_player_count = 1,
    bool adopt_replay_arm = false)
{
    LobbyStateApplyResult result;
    const std::string previous_campaign = save.current_campaign;
    const short previous_scen_num = save.scen_num;
    save.current_campaign = state.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state.settings.campaign_id;
    save.scen_num = state.settings.scenario_id > 0
        ? state.settings.scenario_id
        : 1;
    if (adopt_replay_arm)
    {
        if (save.current_campaign != previous_campaign)
            save.clear_replay_arm();
        else if (save.scen_num != previous_scen_num)
        {
            if (save.is_level_completed(save.scen_num))
            {
                // Keep the FIRST origin across host re-picks: a second
                // completed landing before the excursion resolves must
                // still restore the position this machine started from.
                if (save.replay_level == 0)
                    save.replay_origin = previous_scen_num;
                save.replay_level = save.scen_num;
            }
            else
                save.clear_replay_arm();
        }
    }
    save.allied_mode = state.settings.allied_mode;
    save.ctf_team_count = state.settings.ctf_team_count;
    save.ctf_capture_limit = state.settings.ctf_capture_limit;
    save.ctf_respawn_ticks = state.settings.ctf_respawn_ticks;
    save.ctf_strip_scenario_troops = state.settings.ctf_strip_scenario_troops;
    save.respawn_mode = state.settings.respawn_mode;
    save.generator_rate = state.settings.generator_rate;
    save.keep_fallen_heroes = state.settings.keep_fallen_heroes;
    save.cross_control = state.settings.cross_control;
    save.infinite_gold = state.settings.infinite_gold;
    save.time_limit = state.settings.time_limit;
    for (std::size_t team = 0; team < save.bot_squad.size(); ++team)
    {
        save.bot_squad[team] = state.settings.bot_squad[team];
        save.bot_level[team] = state.settings.bot_level[team];
    }
    save.numplayers = static_cast<unsigned char>(
        spectator_mode
            ? 0u
            : std::clamp<std::size_t>(
                  local_player_count, 1u,
                  static_cast<std::size_t>(MAX_PLAYERS)));
    save.my_team = local_team;

    // Keep screen::save_data private to this machine. The authoritative
    // combined roster lives in LobbyState and the game-start config; copying
    // remote players here used to make every GO/save operation overwrite the
    // peer's real save0. Only stamp echoed local members after an authoritative
    // team change, using their raw PRIVATE slot indices. Truncated/unsent local
    // members deliberately remain in the private roster but do not enter the
    // match.
    for (const og::sim::LobbyPlayer* const seat : find_local_seats(state))
    {
        for (const og::sim::LobbyCharacterSlot& slot :
             seat->character_slots)
        {
            const std::size_t private_slot = slot.slot_index;
            if (private_slot >= save.team_list.size() ||
                save.team_list[private_slot] == nullptr)
            {
                continue;
            }
            save.team_list[private_slot]->teamnum =
                slot.character.teamnum;
            // §4.2 [NET-F2] reconciliation: the server may have
            // force-benched overflow slots (24-cap). Adopt the echoed
            // authoritative deploy flag into the private roster so the
            // machine's next join re-sends exactly what the server
            // stored (content-identical ⇒ ready survives).
            if (save.team_list[private_slot]->deployed != slot.deployed)
            {
                save.team_list[private_slot]->deployed = slot.deployed;
                ++result.deploy_flags_adopted;
                if (!slot.deployed)
                    ++result.deploy_benched;
            }
        }
    }

    if (og::runtime::current_session != nullptr)
    {
        og::runtime::current_session->current_difficulty_ =
            static_cast<std::int32_t>(state.settings.difficulty);
    }

    return result;
}

// §4.2 [NET-F2] second half of the convergence loop: once an echo's deploy
// flags were adopted into the private roster, persist them through the §3.8
// networked MERGE autosave (owner-preserving; never clobber-creates) so the
// reconciled selection survives a restart and the machine's next join is
// content-identical with the server's stored seats. The visible
// "DEPLOY LIMIT 24 — N BENCHED" popup is WP4's; trace-only under TESTING.
void persist_deploy_reconciliation(SaveData& save,
                                   const LobbyStateApplyResult& applied)
{
    if (applied.deploy_flags_adopted == 0)
        return;
    TRACE("lobby", "deploy_reconcile adopted=%d benched=%d",
          applied.deploy_flags_adopted, applied.deploy_benched);
    if (applied.deploy_benched > 0)
    {
        TRACE("lobby", "deploy_limit_benched n=%d", applied.deploy_benched);
        // §4.2 UI popup for a server force-bench (rare: the §2.5 client-side
        // toggle guard pre-empts this path). Trace-only under TESTING.
        if (og::runtime::current_session != nullptr &&
            og::runtime::current_session->myscreen_ != nullptr)
        {
            popup_dialog(
                "DEPLOY LIMIT 24",
                std::format("{} BENCHED", applied.deploy_benched).c_str());
        }
    }
    (void)og::ui::company_autosave_after_mutation(
        save, /*networked_lobby_active=*/true);
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
                                        const std::vector<short>& seat_teams,
                                        const std::array<bool, MAX_TEAM_SIZE>* excluded_slots = nullptr)
{
    // A join declares this machine's WHOLE seat list: seat 0 rides in the
    // legacy player field, seats 1..N-1 in extra_players (wire v7).
    std::vector<og::sim::LobbyPlayer> seats = build_local_lobby_seats(
        save, player_name, seat_teams, excluded_slots);

    og::sim::LobbyMessage message;
    og::sim::LobbyJoinMessage join;
    join.player = std::move(seats.front());
    join.extra_players.assign(
        std::make_move_iterator(seats.begin() + 1),
        std::make_move_iterator(seats.end()));
    message.payload = std::move(join);
    return message;
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

og::sim::LobbyMessage make_team_change_message(
                                               std::uint8_t player_index,
                                               og::sim::LobbySeatId seat_id,
                                               short team)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyTeamChangeMessage{
        .player_index = player_index,
        .seat_id = seat_id,
        .team = team,
    };
    return message;
}

og::sim::LobbyMessage make_remove_seat_message(
    std::uint8_t player_index,
    og::sim::LobbySeatId seat_id)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyRemoveSeatMessage{
        .player_index = player_index,
        .seat_id = seat_id,
    };
    return message;
}

// LINEUP §6: the host's "remove that machine" request. The target is a
// machine id, not a seat token, because a kick takes the whole machine.
og::sim::LobbyMessage make_kick_message(og::sim::LobbyMachineId machine_id)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyKickMessage{
        .machine_id = machine_id,
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

bool start_denial_matches_request(
    const og::sim::LobbyState& state,
    std::uint32_t pending_request_id) noexcept
{
    return pending_request_id != 0 &&
        state.last_start_request_id == pending_request_id &&
        state.last_start_denial != og::sim::start_denial_reason_value(
            og::sim::StartDenialReason::None);
}

bool start_confirmation_matches_request(
    const og::sim::LobbyMessage& message,
    std::uint32_t pending_request_id) noexcept
{
    const auto* const start =
        std::get_if<og::sim::LobbyStartGameMessage>(&message.payload);
    return start != nullptr &&
        (pending_request_id == 0 || start->request_id == pending_request_id);
}

template <typename Poll, typename Accepted>
bool wait_for_authoritative_lobby_value(
    Poll&& poll,
    Accepted&& accepted,
    int attempts = 50,
    bool yield_between_attempts = true)
{
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        poll();
        if (accepted())
            return true;
        if (!yield_between_attempts)
            continue;
#ifdef __EMSCRIPTEN__
        // sleep_for busy-waits under Emscripten; Asyncify must yield so the
        // WebSocket onmessage callback can deliver the authoritative echo.
        emscripten_sleep(10);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    }
    return false;
}

// Authoritative seat mutations are not safely timeoutable: authority may
// commit just after a fixed deadline, leaving the caller's local input-profile
// projection out of step with the accepted seat roster. Wait while the ordered
// transport remains live, and return false only for a definitive denial,
// competing Start, or link loss.
template <typename Poll, typename Accepted, typename Aborted>
bool wait_for_authoritative_lobby_outcome(
    Poll&& poll,
    Accepted&& accepted,
    Aborted&& aborted)
{
    for (;;)
    {
        poll();
        if (accepted())
            return true;
        if (aborted())
            return false;
#ifdef __EMSCRIPTEN__
        emscripten_sleep(10);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    }
}

// [NET-R5] §4.2: networked clients answer is_save_slot_editable with OWN
// SLOTS ONLY (the v7 "all 24 true" shape is gone). The picker save is
// PRIVATE to this machine (apply_lobby_state_to_save never copies remote
// rosters), so an occupied slot normally carries no owner tag
// (guy::kNoOwner => editable) — or, transiently after a networked win
// merge, the tag of one of THIS machine's own seats (editable: the merged
// survivors are copies of tagged session guys). A FOREIGN owner tag means
// the in-memory save is netsession-shaped (a combined session roster);
// those rows are never editable — TrainSession's constructor / PREV/NEXT /
// seek_slot skip them (the §2.5 U8 ownership clamp) and the legacy
// train/rename/team-cycle consumers all gate through
// picker_lobby_save_slot_editable. Empty slots stay "editable" (hire may
// fill them; occupancy is a separate check in every consumer), matching
// the legacy bound-only shape. Server-side seat ownership remains the
// authoritative wire backstop — this is the client-side half.
bool networked_save_slot_editable(
    std::size_t slot_index,
    const SaveData* save,
    const og::sim::LobbyState* state) noexcept
{
    if (slot_index >= static_cast<std::size_t>(MAX_TEAM_SIZE))
        return false;
    if (save == nullptr)
        return true;  // no picker session yet: keep the permissive shape
    const guy* const member = save->team_list[slot_index].get();
    if (member == nullptr)
        return true;
    if (member->owner_player_index == guy::kNoOwner)
        return true;  // private untagged roster: this machine's character
    if (state == nullptr)
        return false;  // tagged row with no lobby view: never assume own
    for (const og::sim::LobbyPlayer* const seat : find_local_seats(*state))
    {
        if (seat->player_index == member->owner_player_index)
            return true;
    }
    return false;
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
    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->myscreen_ != nullptr)
    {
        settings.ctf_authored_team_mask =
            og::ui::ctf_authored_team_mask_for_loaded_level(
                save,
                og::runtime::current_session->myscreen_->world(),
                get_mounted_campaign());
    }
    settings.ctf_capture_limit = save.ctf_capture_limit;
    settings.ctf_respawn_ticks = save.ctf_respawn_ticks;
    settings.ctf_strip_scenario_troops = save.ctf_strip_scenario_troops;
    settings.respawn_mode = save.respawn_mode;
    settings.generator_rate = save.generator_rate;
    settings.keep_fallen_heroes = save.keep_fallen_heroes;
    settings.cross_control = save.cross_control;
    settings.infinite_gold = save.infinite_gold;
    settings.time_limit = save.time_limit;
    // Per-team bot knobs (protocol v16 / GTL v18, LINEUP §3.1): the same
    // dropped-field rule as every other lobby-negotiated match setting.
    for (std::size_t team = 0; team < settings.bot_squad.size(); ++team)
    {
        settings.bot_squad[team] = save.bot_squad[team];
        settings.bot_level[team] = save.bot_level[team];
    }
    // Protocol v12: shared-teams rule rides the wire (matchup: versus).
    settings.shared_teams = og::ui::is_versus_campaign(save) ? 1 : 0;

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

        // Class-pack transfer (protocol v10): the joiner receives these on
        // the same raw lobby transport and feeds them to PackTransferClient.
        case og::sim::kPackManifestMessageType:
        {
            const auto decoded =
                og::sim::deserialize_pack_manifest_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::PackManifest;
            typed_message.pack_manifest =
                std::make_shared<og::sim::PackManifestMessage>(*decoded);
            break;
        }

        case og::sim::kPackFileChunkMessageType:
        {
            const auto decoded =
                og::sim::deserialize_pack_file_chunk_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::PackFileChunk;
            typed_message.pack_file_chunk =
                std::make_shared<og::sim::PackFileChunkMessage>(*decoded);
            break;
        }

        case og::sim::kPackTransferDoneMessageType:
        {
            const auto decoded =
                og::sim::deserialize_pack_transfer_done_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::PackTransferDone;
            typed_message.pack_transfer_done =
                std::make_shared<og::sim::PackTransferDoneMessage>(*decoded);
            break;
        }

        // Staged lobby (#218, protocol v13): the owner's staged-world pair,
        // joiner-bound on the same raw lobby transport.
        case og::sim::kStagedMatchSetupMessageType:
        {
            const auto decoded =
                og::sim::deserialize_staged_match_setup_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::StagedMatchSetup;
            typed_message.staged_match_setup =
                std::make_shared<og::sim::StagedMatchSetupMessage>(*decoded);
            break;
        }

        case og::sim::kStagedMatchKeyframeMessageType:
        {
            const auto decoded =
                og::sim::deserialize_staged_match_keyframe_message(
                    message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::StagedMatchKeyframe;
            typed_message.staged_match_keyframe =
                std::make_shared<og::sim::StagedMatchKeyframeMessage>(
                    *decoded);
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
    const std::vector<short>& sibling_teams = {}) noexcept
{
    // Explicit assignments permit co-op within and across machines. Mirror
    // the selected level's authored CTF domain before the server echo; the
    // server remains authoritative.
    (void)sibling_teams;
    if (og::sim::lobby_team_is_selectable(state.settings, requested_team))
        return requested_team;

    const short fallback =
        og::sim::lobby_first_selectable_team(state.settings);
    return fallback >= 0 ? fallback : requested_team;
}

struct PendingLocalLobbyState {
    og::sim::LobbyState state;
    short local_team = 0;
};

PendingLocalLobbyState build_pending_local_lobby_state(
    const og::sim::LobbyState& state,
    const std::vector<og::sim::LobbyPlayer>& pending_local_seats,
    short requested_local_team)
{
    PendingLocalLobbyState merged{
        .state = state,
        .local_team = requested_local_team,
    };

    if (pending_local_seats.empty() ||
        pending_local_seats.front().character_slots.empty() ||
        merged.state.players.size() >=
            static_cast<std::size_t>(og::sim::kMaxGlobalPlayers) ||
        !merged.state.local_seat_ids.empty())
    {
        return merged;
    }

    // Merge every pending local seat (the join is a whole-seat-list declare),
    // resolving teams in seat order with within-machine distinctness. Give
    // the optimistic copies locally unique high-end seat IDs so the normal
    // authoritative ownership path can consume them without consulting the
    // untrusted name. These IDs never leave this temporary state.
    std::vector<short> sibling_teams;
    og::sim::LobbySeatId pending_seat_id =
        std::numeric_limits<og::sim::LobbySeatId>::max();
    for (std::size_t seat = 0; seat < pending_local_seats.size(); ++seat)
    {
        if (merged.state.players.size() >=
            static_cast<std::size_t>(og::sim::kMaxGlobalPlayers))
            break;

        const og::sim::LobbyPlayer& pending_seat = pending_local_seats[seat];
        const short seat_team = resolve_pending_local_team(
            merged.state,
            seat == 0 ? requested_local_team : pending_seat.team,
            sibling_teams);
        if (seat == 0)
            merged.local_team = seat_team;
        sibling_teams.push_back(seat_team);

        og::sim::LobbyPlayer local_player = pending_seat;
        local_player.player_index = 0xffu;
        while (og::ui::detail::find_player_by_seat_id(
                   merged.state, pending_seat_id) != nullptr)
            --pending_seat_id;
        local_player.seat_id = pending_seat_id--;
        local_player.team = seat_team;
        local_player.ready = false;
        local_player.is_host = false;
        merged.state.local_seat_ids.push_back(local_player.seat_id);
        merged.state.players.push_back(std::move(local_player));
    }
    return merged;
}

[[maybe_unused]] std::string select_lan_ipv4_address(
    const std::optional<std::string>& route_address,
    const std::optional<std::string>& hostname_address)
{
    if (route_address.has_value())
        return *route_address;
    if (hostname_address.has_value())
        return *hostname_address;
    return "127.0.0.1";
}

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
std::optional<std::string> usable_lan_ipv4_string(const in_addr& address)
{
    const std::uint32_t host_order = ntohl(address.s_addr);
    if (host_order == 0 || (host_order >> 24) == 127)
        return std::nullopt;

    std::array<char, INET_ADDRSTRLEN> buffer = {};
    if (inet_ntop(AF_INET, &address, buffer.data(),
                  static_cast<socklen_t>(buffer.size())) == nullptr)
    {
        return std::nullopt;
    }
    return std::string(buffer.data());
}

std::optional<std::string> detect_lan_ipv4_via_udp_route()
{
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

    return usable_lan_ipv4_string(local.sin_addr);
}

std::optional<std::string> detect_lan_ipv4_via_hostname()
{
    std::array<char, 256> hostname = {};
    // Reserve the final byte: POSIX leaves NUL-termination unspecified when
    // the name is truncated, but the zero-initialized buffer keeps [255]=='\0'.
    if (gethostname(hostname.data(), hostname.size() - 1) != 0 ||
        hostname[0] == '\0')
    {
        return std::nullopt;
    }

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

    for (addrinfo* current = results; current != nullptr;
         current = current->ai_next)
    {
        if (current->ai_addr == nullptr ||
            current->ai_addrlen < sizeof(sockaddr_in))
        {
            continue;
        }

        const auto* const address =
            reinterpret_cast<const sockaddr_in*>(current->ai_addr);
        if (const auto detected = usable_lan_ipv4_string(address->sin_addr);
            detected.has_value())
        {
            return detected;
        }
    }

    return std::nullopt;
}
#endif

std::string detect_lan_ipv4_address()
{
#ifdef __EMSCRIPTEN__
    std::unique_ptr<char, decltype(&std::free)> hostname(
        current_browser_hostname_js(), &std::free);
    if (!hostname)
        return "127.0.0.1";

    std::string value = hostname.get();
    if (value.empty())
        return "127.0.0.1";
    return value;
#elif defined(__unix__) || defined(__APPLE__)
    const std::optional<std::string> route_address =
        detect_lan_ipv4_via_udp_route();
    if (route_address.has_value())
        return select_lan_ipv4_address(route_address, std::nullopt);

    return select_lan_ipv4_address(
        route_address,
        detect_lan_ipv4_via_hostname());
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
#include "../../../tests/coverage_internal/picker_lobby_network_internal.inc"
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
        lines.push_back(std::format("Room: {}", relay_room_code));
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
        // Honor the live Base Camp seat count: a networked host declares one
        // lobby seat per local player (0 = spectator, unchanged).
        local_player_count_ = spectator_mode_
            ? 0
            : std::clamp<int>(save->numplayers, 1, MAX_PLAYERS);
        save->numplayers = static_cast<unsigned char>(local_player_count_);
        local_seat_teams_ =
            og::ui::detail::seed_local_seat_assignments(*save, spectator_mode_);
        local_team_ = local_seat_teams_.front();
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
        sync_hosted_packs(*save, /*force=*/true);

        // Staged lobby (#218): the network host stages through the dedicated
        // pipeline, seeded from its own company save (V5 Option A) with the
        // real §4.4 control policy from the lobby bindings. Seed latched once
        // per round; GO consults the stage through the start gate — the gate
        // recomputes the change key AT StartGame time (a roster edit in the
        // same poll batch must reach the launched world) and forces the
        // synchronous restage, so a stale or Failed stage can never launch.
        stage_ = std::make_unique<og::server::MatchStage>(
            og::server::MatchStageConfig{
                .networked = true,
                .arm_policy = og::server::LobbyStartReplayArm::SeededIntent,
                .host_company_save = save,
            });
        stage_broadcast_ = {};
        match_seed_ = og::server::draw_match_seed();
        server_->set_start_gate([this] {
            refresh_stage_inputs();
            return stage_ != nullptr &&
                    stage_->ensure_current(og::server::stage_clock_now_ms())
                ? og::sim::StartDenialReason::None
                : og::sim::StartDenialReason::StageFailed;
        });

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_settings_message(*save));
        send_join_from_save();

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
        stage_.reset();
        stage_broadcast_ = {};
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
        local_seat_teams_.clear();
        start_request_pending_ = false;
        pending_start_request_id_ = 0;
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
        drive_stage();
    }

    [[nodiscard]] const GameWorld* staged_world() const override
    {
        return stage_ && stage_->status() == og::server::StageStatus::Staged
            ? stage_->world()
            : nullptr;
    }

    [[nodiscard]] std::uint32_t stage_generation() const override
    {
        return stage_ ? stage_->stage_generation() : 0;
    }

    [[nodiscard]] StagedPreviewHealth staged_preview_health() const override
    {
        if (!stage_)
            return StagedPreviewHealth::None;
        switch (stage_->status())
        {
            case og::server::StageStatus::Staged:
                return StagedPreviewHealth::Staged;
            case og::server::StageStatus::Failed:
                return StagedPreviewHealth::Failed;
            case og::server::StageStatus::Empty:
                break;
        }
        return StagedPreviewHealth::None;
    }

    [[nodiscard]] bool staged_keyframe_bytes(
        std::uint32_t& generation,
        const std::vector<std::uint8_t>*& setup_bytes,
        const std::vector<std::uint8_t>*& keyframe_bytes) const override
    {
        if (!stage_ || stage_->status() != og::server::StageStatus::Staged ||
            stage_->staged_setup_bytes().empty() ||
            stage_->staged_keyframe_bytes().empty())
            return false;
        generation = stage_->stage_generation();
        setup_bytes = &stage_->staged_setup_bytes();
        keyframe_bytes = &stage_->staged_keyframe_bytes();
        return true;
    }

    [[nodiscard]] og::server::MatchStage* take_match_stage() override
    {
        return stage_.get();
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
        og::ui::detail::resize_local_seat_assignments(
            local_seat_teams_,
            *save,
            spectator_mode_,
            local_player_count_);
        local_team_ = local_seat_teams_.front();
        save->my_team = local_team_;
        send_join_from_save();
        poll_and_apply();
    }

    bool add_local_seat() override
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr || !local_client_transport_ ||
            !state_.has_value() || server_ == nullptr ||
            server_->start_game_requested())
        {
            return false;
        }

        const std::size_t active_count = local_seat_count();
        if (!spectator_mode_ &&
            active_count >= static_cast<std::size_t>(MAX_PLAYERS))
        {
            return false;
        }

        const bool was_spectator = spectator_mode_;
        const std::size_t requested_count =
            spectator_mode_ ? 1u : active_count + 1u;
        spectator_mode_ = false;
        local_player_count_ = static_cast<int>(requested_count);
        save->numplayers = static_cast<unsigned char>(requested_count);
        og::ui::detail::resize_local_seat_assignments(
            local_seat_teams_,
            *save,
            spectator_mode_,
            local_player_count_);
        local_team_ = local_seat_teams_.front();
        save->my_team = local_team_;

        send_join_from_save();
        poll_and_apply();
        const bool accepted = local_seat_count() == requested_count;
        if (!accepted && was_spectator)
        {
            spectator_mode_ = true;
            local_player_count_ = 0;
            save->numplayers = 0;
            og::ui::detail::resize_local_seat_assignments(
                local_seat_teams_,
                *save,
                spectator_mode_,
                local_player_count_);
        }
        return accepted;
    }

    bool remove_local_seat(std::uint8_t player_index,
                           og::sim::LobbySeatId seat_id) override
    {
        if (!local_client_transport_ || !state_.has_value() ||
            server_ == nullptr || server_->start_game_requested() ||
            spectator_mode_ ||
            seat_id == og::sim::kInvalidLobbySeatId)
        {
            return false;
        }

        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(*state_);
        const auto target = std::find_if(
            local_seats.begin(), local_seats.end(),
            [seat_id](const og::sim::LobbyPlayer* seat) {
                return seat != nullptr && seat->seat_id == seat_id;
            });
        if (target == local_seats.end())
            return false;

        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return false;

        const std::size_t requested_count = local_seats.size() - 1u;
        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_remove_seat_message(
                player_index, seat_id));
        poll_and_apply();
        const bool removed = state_.has_value() &&
            og::ui::detail::find_player_by_seat_id(*state_, seat_id) == nullptr &&
            local_seat_count() == requested_count;
        if (!removed)
            return false;

        if (requested_count == 0u)
        {
            // The server keeps the peer connection (and a private dormant
            // token) but publishes no LobbyPlayer for a spectator. It
            // therefore consumes no capacity, ready gate, roster, or gameplay
            // binding.
            spectator_mode_ = true;
            local_player_count_ = 0;
            save->numplayers = 0;
            og::ui::detail::resize_local_seat_assignments(
                local_seat_teams_,
                *save,
                spectator_mode_,
                local_player_count_);
            return true;
        }

        // Re-declare the private roster immediately so characters formerly
        // carried by the removed seat move onto a surviving owned seat.
        send_join_from_save();
        poll_and_apply();
        return state_.has_value() &&
            og::ui::detail::find_player_by_seat_id(*state_, seat_id) == nullptr &&
            local_seat_count() == requested_count;
    }

    [[nodiscard]] std::size_t local_seat_count() const override
    {
        if (state_.has_value())
            return og::ui::detail::find_local_seats(*state_).size();
        if (spectator_mode_)
            return 0u;
        return static_cast<std::size_t>(
            std::clamp(local_player_count_, 0, MAX_PLAYERS));
    }

    bool request_start_game() override
    {
        if (start_request_pending_ ||
            !local_client_transport_ || !state_.has_value() ||
            server_ == nullptr)
        {
            return false;
        }

        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_);

        pending_game_start_config_.reset();
        start_request_pending_ = true;
        pending_start_request_id_ = next_start_request_id_++;
        if (next_start_request_id_ == 0)
            next_start_request_id_ = 1;

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local_player != nullptr
                ? local_player->player_index
                : std::uint8_t{0xffu},
            .request_id = pending_start_request_id_,
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
        else if (!g_start_game_requested)
        {
            // §4.3 denial: the server echoed last_start_denial without locking
            // the lobby. Drop the pending flag so the go_menu poll loop stops
            // spinning to timeout; the reason is read via last_start_denial().
            start_request_pending_ = false;
            pending_start_request_id_ = 0;
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
        const short shared_team =
            og::sim::shared_allied_gameplay_team(authoritative_state);
        og::sim::LobbyState authoritative_local_state = authoritative_state;
        authoritative_local_state.local_seat_ids = state_.has_value()
            ? state_->local_seat_ids
            : std::vector<og::sim::LobbySeatId>{};
        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(authoritative_local_state);
        const bool authoritative_spectator = local_seats.empty();
        config.save_data.numplayers = authoritative_spectator
            ? 0u
            : static_cast<unsigned char>(local_seats.size());
        config.difficulty = state_.has_value()
            ? static_cast<std::int16_t>(state_->settings.difficulty)
            : static_cast<std::int16_t>(authoritative_state.settings.difficulty);
        config.my_team = gameplay_start_team(config.save_data.allied_mode,
                                             local_team_, shared_team);
        config.is_networked = true;
        config.local_player_index =
            static_cast<std::uint8_t>(authoritative_state.host_player_id);
        if (!authoritative_spectator)
        {
            for (const og::sim::LobbyPlayer* const seat : local_seats)
            {
                config.local_player_indices.push_back(seat->player_index);
                config.local_seat_teams.push_back(gameplay_start_team(
                    config.save_data.allied_mode,
                    static_cast<short>(seat->team), shared_team));
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

    // §2.5 line-B alert: the host's own lobby is in-process and cannot die,
    // but an advertised relay room whose socket dropped means remote joiners
    // can no longer reach this machine — surface exactly the drop condition
    // rebuild_status_lines() folds into the "Relay: connection lost" line.
    [[nodiscard]] std::optional<std::string> connection_alert() const override
    {
        if (relay_transport_ && !relay_room_code_.empty())
        {
            const og::sim::TransportLinkState relay_link =
                relay_transport_->link_state();
            if (relay_link == og::sim::TransportLinkState::Failed ||
                relay_link == og::sim::TransportLinkState::Lost)
            {
                return "Relay: connection lost";
            }
        }
        return std::nullopt;
    }

    // §9.12 session status: the advertised relay room, raw — a dead relay
    // room surfaces through connection_alert(), which outranks the status
    // line on the base camp, so this never displays a dead room as healthy.
    [[nodiscard]] std::string session_room_code() const override
    {
        return relay_room_code_;
    }

    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return server_ != nullptr;
    }

    // [NET-R5]: own slots only (see networked_save_slot_editable).
    [[nodiscard]] bool is_save_slot_editable(
        std::size_t slot_index) const noexcept override
    {
        return og::ui::detail::networked_save_slot_editable(
            slot_index,
            current_picker_save(),
            state_.has_value() ? &*state_ : nullptr);
    }

    bool request_seat_team_change(std::uint8_t player_index,
                                  short team) override
    {
        if (!local_client_transport_ || !state_.has_value() ||
            team < 0 || team >= MAX_PLAYERS)
            return false;

        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(*state_);
        const auto target_it = std::find_if(
            local_seats.begin(), local_seats.end(),
            [player_index](const og::sim::LobbyPlayer* seat) {
                return seat->player_index == player_index;
            });
        if (target_it == local_seats.end())
            return false;
        const og::sim::LobbySeatId target_seat_id = (*target_it)->seat_id;

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_team_change_message(
                player_index, target_seat_id, team));
        poll_and_apply();
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const echoed =
            og::ui::detail::find_player_by_seat_id(*state_, target_seat_id);
        return echoed != nullptr && echoed->team == team;
    }

    // LINEUP §6: the host removes one foreign MACHINE. Own seats and unknown
    // ids are refused here as well as on the server — the client-side check
    // keeps a mis-wired row from spending a round trip, the server-side one
    // is the authority (a crafted client reaches it directly).
    bool kick_machine(og::sim::LobbyMachineId machine_id) override
    {
        if (!local_client_transport_ || !state_.has_value() ||
            machine_id == og::sim::kInvalidLobbyMachineId)
        {
            return false;
        }
        for (const og::sim::LobbyPlayer* const seat :
             og::ui::detail::find_local_seats(*state_))
        {
            if (seat->machine_id == machine_id)
                return false; // leaving is disconnect_session(), not a kick
        }
        const bool known = std::any_of(
            state_->players.begin(), state_->players.end(),
            [machine_id](const og::sim::LobbyPlayer& player) {
                return player.machine_id == machine_id;
            });
        if (!known)
            return false;

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_kick_message(machine_id));
        poll_and_apply();
        return true;
    }

    // LINEUP §6: stop hosting. Tearing the server and its transports down is
    // exactly shutdown()'s job, and every connected peer sees the link drop.
    bool disconnect_session() override
    {
        if (!combined_transport_ && !local_client_transport_ &&
            server_ == nullptr)
        {
            return false;
        }
        shutdown();
        return true;
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
            og::ui::detail::find_local_player(*state_);
        return local_player != nullptr && local_player->ready;
    }

    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players() const override
    {
        if (!state_.has_value())
            return {};
        return state_->players;
    }

    [[nodiscard]] std::optional<std::uint8_t>
    authoritative_team_mask() const noexcept override
    {
        if (!state_.has_value())
            return std::nullopt;
        return og::sim::lobby_effective_team_mask(state_->settings);
    }

    // §2.5 base-camp ownership split: this machine's seats from the
    // recipient-specific, server-issued ownership grant.
    [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
        const override
    {
        if (!state_.has_value())
            return {};
        std::vector<std::uint8_t> indices;
        for (const og::sim::LobbyPlayer* const seat :
             og::ui::detail::find_local_seats(*state_))
        {
            indices.push_back(seat->player_index);
        }
        return indices;
    }

    [[nodiscard]] og::sim::StartDenialReason last_start_denial()
        const noexcept override
    {
        if (!state_.has_value())
            return og::sim::StartDenialReason::None;
        return static_cast<og::sim::StartDenialReason>(
            state_->last_start_denial);
    }

    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }

    // LINEUP §6: the host IS the lobby — the moment the server exists this
    // machine is in an established session, roster broadcast or not. Same
    // fact host_controls_visible() reports; spelled out separately so the
    // two questions ("am I in a session" / "may I touch host knobs") stay
    // independent for readers.
    [[nodiscard]] bool session_established() const noexcept override
    {
        return server_ != nullptr;
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
            player_bindings_,
            stage_.get());

        // §2.8 follow caption: stamp each global player's company display
        // name (from the final lobby state) onto the installed runtime.
        if (state_.has_value())
        {
            std::vector<std::pair<std::uint8_t, std::string>> companies;
            for (const og::sim::LobbyPlayer& player : state_->players)
                companies.emplace_back(player.player_index, player.company);
            og::runtime::local_transport_shadow_set_player_companies(
                session, companies);
        }

        // Keep the lobby connection ALIVE across gameplay so the team-build menu
        // can start the next level over it (single-player parity). The runtime
        // holds its own shared_ptr refs to the transports, so they outlive the
        // per-level runtime; the LobbyServer stays alive but dormant (the lobby
        // is polled only from the menu, never the game loop) and resumes via
        // resume_after_level(). Only the per-start scratch state is cleared.
        player_bindings_.clear();
        start_request_pending_ = false;
        pending_start_request_id_ = 0;
        pending_game_start_config_.reset();
        return true;
    }

    void resume_after_level() override
    {
        g_start_game_requested = false;
        start_request_pending_ = false;
        pending_start_request_id_ = 0;
        pending_game_start_config_.reset();
        player_bindings_.clear();

        // If the connection didn't survive (shutdown, or a transport that can't
        // persist), fall back to a fresh lobby from the save.
        if (combined_transport_ == nullptr || local_client_transport_ == nullptr ||
            server_ == nullptr)
        {
            const std::vector<short> preserved_teams = local_seat_teams_;
            initialize_from_save();
            if (!preserved_teams.empty() && !local_seat_teams_.empty())
            {
                local_seat_teams_ = preserved_teams;
                SaveData* const save = current_picker_save();
                if (save != nullptr)
                {
                    og::ui::detail::resize_local_seat_assignments(
                        local_seat_teams_,
                        *save,
                        spectator_mode_,
                        local_player_count_);
                    local_team_ = local_seat_teams_.front();
                    save->my_team = local_team_;
                    sync_roster_from_save();
                }
            }
            return;
        }

        // Each round is a fresh match: dispose any leftover stage and latch a
        // FRESH seed before the sync/poll cycle triggers the next round's
        // first stage (the advanced scen_num moves the change key).
        if (stage_)
            stage_->dispose();
        stage_broadcast_ = {};
        match_seed_ = og::server::draw_match_seed();

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
            og::ui::detail::find_local_player(*state_);
        return local_player != nullptr ? local_player->player_index : 0xffu;
    }

    // Active seats declared in a Join. A spectator remains connected with no
    // authoritative LobbyPlayer and therefore sends Leave instead.
    [[nodiscard]] std::size_t join_seat_count() const noexcept
    {
        return spectator_mode_
            ? 0u
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
        // A campaign switch can change the campaign-embedded pack set;
        // re-offer it so joiners get the new manifests (protocol v10).
        sync_hosted_packs(*save, /*force=*/false);
    }

    // Offer this machine's mounted non-core packs for transfer. Rebuilding
    // hashes every pack file, so only do it when the mounted campaign (the
    // one lobby-time source of pack-set changes) actually changed.
    void sync_hosted_packs(const SaveData& save, bool force)
    {
        if (server_ == nullptr)
            return;
        if (!force && save.current_campaign == hosted_packs_campaign_)
            return;
        hosted_packs_campaign_ = save.current_campaign;
        server_->set_hosted_packs(og::resources::build_transferable_packs());
    }

    void send_join_from_save()
    {
        if (!local_client_transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        if (spectator_mode_)
        {
            og::sim::LobbyMessage message;
            message.payload = og::sim::LobbyLeaveMessage{
                .player_index = local_player_index(),
            };
            og::ui::detail::send_lobby_message(
                *local_client_transport_,
                local_client_transport_->local_peer_id(),
                std::move(message));
            return;
        }

        og::ui::detail::send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            og::ui::detail::make_join_message(
                *save,
                player_name_,
                local_seat_teams_,
                nullptr));
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
                        og::ui::detail::find_local_player(*state_))
                {
                    local_team_ = local_player->team;
                }
                const std::vector<const og::sim::LobbyPlayer*> local_seats =
                    og::ui::detail::find_local_seats(*state_);
                if (!local_seats.empty())
                {
                    local_seat_teams_.clear();
                    for (const og::sim::LobbyPlayer* const seat : local_seats)
                        local_seat_teams_.push_back(seat->team);
                }
            }
            break;

        case og::sim::TypedReceivedMessageKind::LobbyMessage:
            if (message.lobby_message &&
                message.lobby_message->kind() ==
                    og::sim::LobbyMessageKind::StartGame)
            {
                if (!og::ui::detail::start_confirmation_matches_request(
                        *message.lobby_message,
                        start_request_pending_
                            ? pending_start_request_id_
                            : 0))
                {
                    break;
                }
                start_request_pending_ = false;
                pending_start_request_id_ = 0;
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
                og::ui::detail::find_local_seats(*state_);
            if (!local_seats.empty())
                local_player_count_ = static_cast<int>(local_seats.size());
        }

        const og::ui::detail::LobbyStateApplyResult applied =
            og::ui::detail::apply_lobby_state_to_save(
                *state_,
                *save,
                spectator_mode_,
                local_team_,
                static_cast<std::size_t>(std::max(local_player_count_, 0)));
        og::ui::detail::persist_deploy_reconciliation(*save, applied);
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

    // Staged lobby (#218): recompute the change key from the live LobbyServer
    // (the SAME functions the launch consumes). Called from every poll and
    // from the start gate (StartGame time — the key must include every edit
    // the same poll batch already applied).
    void refresh_stage_inputs()
    {
        if (!stage_ || !server_)
            return;
        try
        {
            og::server::MatchStageInputs inputs;
            inputs.equivalent = server_->build_save_data_equivalent();
            inputs.bindings = server_->build_player_bindings();
            inputs.difficulty = server_->state().settings.difficulty;
            inputs.match_seed = match_seed_;
            // Replay-arm intent is a launch input (#207): key on the live
            // save's arm so a VISIT/REPLAY flip restages the census.
            if (const SaveData* const live_save = current_picker_save())
            {
                inputs.replay_level = live_save->replay_level;
                inputs.replay_origin = live_save->replay_origin;
            }
            stage_->observe_inputs(inputs, og::server::stage_clock_now_ms());
        }
        catch (const std::exception& error)
        {
            // The 24-roster equivalent cap lands as a Failed stage (GO denied
            // via StageFailed), never an escape out of the poll loop.
            stage_->mark_failed(error.what());
        }
    }

    void drive_stage()
    {
        if (!stage_ || !server_)
            return;
        refresh_stage_inputs();
        stage_->maintain(og::server::stage_clock_now_ms());
        og::server::deliver_staged_pair(*stage_,
                                        combined_transport_.get(),
                                        stage_broadcast_);
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
    std::unique_ptr<og::server::MatchStage> stage_;
    og::server::StageBroadcastState stage_broadcast_;
    std::uint32_t match_seed_ = 0;
    std::optional<og::sim::LobbyState> state_;
    std::vector<og::sim::LobbyPlayerBinding> player_bindings_;
    std::vector<std::string> status_lines_;
    bool spectator_mode_ = false;
    // Local seats this machine declares (0 = spectator, 1..MAX_PLAYERS).
    int local_player_count_ = 1;
    short local_team_ = 0;
    std::vector<short> local_seat_teams_;
    bool start_request_pending_ = false;
    std::uint32_t next_start_request_id_ = 1;
    std::uint32_t pending_start_request_id_ = 0;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
    std::string relay_room_code_;
    std::string relay_status_message_;
    std::string direct_status_message_;
    // Campaign whose pack set was last offered to the LobbyServer; guards
    // sync_hosted_packs against re-hashing every settings echo.
    std::string hosted_packs_campaign_;
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
        // Honor the live Base Camp seat count: a networked joiner declares one
        // lobby seat per local player (0 = spectator, unchanged).
        local_player_count_ = spectator_mode_
            ? 0
            : std::clamp<int>(save->numplayers, 1, MAX_PLAYERS);
        save->numplayers = static_cast<unsigned char>(local_player_count_);
        local_seat_teams_ =
            og::ui::detail::seed_local_seat_assignments(*save, spectator_mode_);
        local_team_ = local_seat_teams_.front();
        save->my_team = local_team_;
        if (!spectator_mode_)
        {
            pending_local_seats_ = og::ui::detail::build_local_lobby_seats(
                *save,
                player_name_,
                local_seat_teams_,
                nullptr);
        }

        server_peer_id_ = 1;
        server_peer_id_adopted_ = false;
        lobby_states_received_ = 0;
        awaiting_round_ready_reset_ = false;
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

        // Class-pack transfer (protocol v10): compare the host's manifests
        // against local content, pull what is missing, and mount it before
        // ready-up. Progress surfaces through status_lines_ and the log.
        og::sim::PackTransferClient::Callbacks pack_callbacks =
            og::resources::make_pack_transfer_client_callbacks();
        pack_callbacks.log_status = [](const std::string& text) {
            Log("{}\n", text);
        };
        pack_client_ = std::make_unique<og::sim::PackTransferClient>(
            std::move(pack_callbacks));

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
        pack_client_.reset();
        transport_.reset();
        direct_url_.clear();
        relay_room_code_.clear();
        spectator_mode_ = false;
        local_player_count_ = 1;
        local_team_ = 0;
        local_seat_teams_.clear();
        start_request_pending_ = false;
        pending_start_request_id_ = 0;
        deferred_start_requested_ = false;
        join_message_sent_ = false;
        join_confirmation_pending_ = false;
        pending_join_request_id_ = 0;
        pending_join_resume_after_level_ = false;
        join_state_serial_at_send_ = 0;
        next_join_retry_at_ = {};
        settings_dirty_ = false;
        server_peer_id_ = 1;
        server_peer_id_adopted_ = false;
        awaiting_round_ready_reset_ = false;
        pending_local_seats_.clear();
        preview_mirror_.dispose();
    }

    void sync_from_save() override
    {
        settings_dirty_ = true;
        (void)prepare_pending_join_from_save();
        poll_and_apply();
    }

    void sync_roster_from_save() override
    {
        (void)prepare_pending_join_from_save();
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
            join_confirmation_pending_ = false;
            pending_join_request_id_ = 0;
            // In-flight transfers cannot finish on a dead link; the server
            // re-announces its manifests on reconnect.
            if (pack_client_)
                pack_client_->reset();
            // A pending start request can never resolve on a dead connection
            // (neither the StartGame handoff nor the denial echo will arrive);
            // release it so go_menu's wait loop gives up instead of spinning
            // forever, and the user can retry after the reconnect.
            start_request_pending_ = false;
            deferred_start_requested_ = false;
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
            join_message_sent_ = send_join_from_save();
        }
        else if (join_confirmation_pending_ &&
                 std::chrono::steady_clock::now() >= next_join_retry_at_)
        {
            // During the previous level the host GameServer owns this shared
            // transport and legally drains non-game Lobby messages. Keep the
            // desired declaration alive until a matching authoritative echo
            // proves the reopened LobbyServer received it.
            (void)send_pending_join();
        }

        drain_messages();
        apply_state_to_current_save();
        maintain_preview_mirror();
        maybe_dispatch_deferred_start();
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
        og::ui::detail::resize_local_seat_assignments(
            local_seat_teams_,
            *save,
            spectator_mode_,
            local_player_count_);
        local_team_ = local_seat_teams_.front();
        save->my_team = local_team_;
        (void)prepare_pending_join_from_save();
        poll_and_apply();
    }

    bool add_local_seat() override
    {
        poll_and_apply();
        SaveData* const save = current_picker_save();
        if (save == nullptr || !transport_ ||
            transport_->connected_peers().empty() || !state_.has_value() ||
            start_request_pending_ || pending_game_start_config_.has_value() ||
            g_start_game_requested || awaiting_round_ready_reset_ ||
            join_confirmation_pending_)
        {
            return false;
        }

        const std::size_t active_count = local_seat_count();
        if (active_count >= static_cast<std::size_t>(MAX_PLAYERS))
        {
            return false;
        }

        // An active non-host machine must first publish unready. Whichever
        // message reaches authority first then has a safe outcome: Start wins
        // and this mutation aborts, or Ready(false) wins and Start is denied
        // until the changed roster is explicitly re-readied.
        if (!local_player_is_host() && local_ready() &&
            !set_ready(false))
        {
            return false;
        }
        if (start_request_pending_ || pending_game_start_config_.has_value() ||
            g_start_game_requested || awaiting_round_ready_reset_ ||
            join_confirmation_pending_)
        {
            return false;
        }

        const bool previous_spectator = spectator_mode_;
        const int previous_player_count = local_player_count_;
        const unsigned char previous_numplayers = save->numplayers;
        const short previous_local_team = local_team_;
        const std::vector<short> previous_seat_teams = local_seat_teams_;
        const std::size_t requested_count = active_count + 1u;
        spectator_mode_ = false;
        local_player_count_ = static_cast<int>(requested_count);
        save->numplayers = static_cast<unsigned char>(requested_count);
        og::ui::detail::resize_local_seat_assignments(
            local_seat_teams_,
            *save,
            spectator_mode_,
            local_player_count_);
        local_team_ = local_seat_teams_.front();
        save->my_team = local_team_;
        if (!prepare_pending_join_from_save())
        {
            spectator_mode_ = previous_spectator;
            local_player_count_ = previous_player_count;
            save->numplayers = previous_numplayers;
            local_team_ = previous_local_team;
            local_seat_teams_ = previous_seat_teams;
            save->my_team = local_team_;
            return false;
        }

        const bool accepted =
            og::ui::detail::wait_for_authoritative_lobby_outcome(
                [this] { poll_and_apply(); },
                [this, requested_count] {
                    return !join_confirmation_pending_ &&
                        local_seat_count() == requested_count;
                },
                [this] {
                    if (!transport_ ||
                        transport_->connected_peers().empty() ||
                        !join_confirmation_pending_ ||
                        g_start_game_requested ||
                        pending_game_start_config_.has_value())
                    {
                        return true;
                    }
                    const og::sim::TransportLinkState link =
                        transport_->link_state();
                    return link == og::sim::TransportLinkState::Failed ||
                        link == og::sim::TransportLinkState::Lost;
                });
        if (accepted)
            return true;

        // A correlated authoritative denial, a competing Start, or link loss
        // is definitive. Stop retries and restore the exact local projection;
        // no compensating whole Join is needed (and one could remap stable
        // middle-seat identities by ordinal after a disconnect).
        join_confirmation_pending_ = false;
        pending_join_request_id_ = 0;
        pending_join_resume_after_level_ = false;
        spectator_mode_ = previous_spectator;
        local_player_count_ = previous_player_count;
        save->numplayers = previous_numplayers;
        local_team_ = previous_local_team;
        local_seat_teams_ = previous_seat_teams;
        save->my_team = local_team_;
        pending_local_seats_ = state_.has_value()
            ? og::ui::detail::copy_local_seats(*state_)
            : std::vector<og::sim::LobbyPlayer>{};
        join_message_sent_ = true;
        return false;
    }

    bool remove_local_seat(std::uint8_t player_index,
                           og::sim::LobbySeatId seat_id) override
    {
        poll_and_apply();
        if (!transport_ || transport_->connected_peers().empty() ||
            !state_.has_value() || local_seat_count() == 0u ||
            start_request_pending_ || pending_game_start_config_.has_value() ||
            g_start_game_requested || awaiting_round_ready_reset_ ||
            join_confirmation_pending_ ||
            seat_id == og::sim::kInvalidLobbySeatId)
        {
            return false;
        }

        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(*state_);
        const auto target = std::find_if(
            local_seats.begin(), local_seats.end(),
            [seat_id](const og::sim::LobbyPlayer* seat) {
                return seat != nullptr && seat->seat_id == seat_id;
            });
        if (target == local_seats.end())
            return false;

        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return false;

        if (!local_player_is_host() && local_ready() &&
            !set_ready(false))
        {
            return false;
        }
        if (start_request_pending_ || pending_game_start_config_.has_value() ||
            g_start_game_requested || awaiting_round_ready_reset_ ||
            join_confirmation_pending_)
        {
            return false;
        }

        const std::size_t requested_count = local_seats.size() - 1u;
        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            og::ui::detail::make_remove_seat_message(
                player_index, seat_id));
        const bool removed =
            og::ui::detail::wait_for_authoritative_lobby_outcome(
                [this] { poll_and_apply(); },
                [this, seat_id, requested_count] {
                    return state_.has_value() &&
                        og::ui::detail::find_player_by_seat_id(
                            *state_, seat_id) == nullptr &&
                        local_seat_count() == requested_count;
                },
                [this] {
                    if (!transport_ ||
                        transport_->connected_peers().empty() ||
                        g_start_game_requested ||
                        pending_game_start_config_.has_value())
                    {
                        return true;
                    }
                    const og::sim::TransportLinkState link =
                        transport_->link_state();
                    return link == og::sim::TransportLinkState::Failed ||
                        link == og::sim::TransportLinkState::Lost;
                });
        if (!removed)
            return false;

        local_player_count_ = static_cast<int>(requested_count);
        save->numplayers = static_cast<unsigned char>(requested_count);
        if (requested_count == 0u)
        {
            spectator_mode_ = true;
            og::ui::detail::resize_local_seat_assignments(
                local_seat_teams_,
                *save,
                spectator_mode_,
                local_player_count_);
            join_confirmation_pending_ = false;
            pending_join_request_id_ = 0;
            pending_join_resume_after_level_ = false;
            pending_local_seats_.clear();
            join_message_sent_ = true;
            return true;
        }

        if (!prepare_pending_join_from_save())
            return true;

        // Re-declare the private roster after the exact removal so fighters
        // carried by that seat move to a surviving stable seat. The exact
        // RemoveSeat is already committed, so a delayed roster echo must not
        // make the UI skip its matching local-control profile compaction.
        (void)og::ui::detail::wait_for_authoritative_lobby_value(
            [this] { poll_and_apply(); },
            [this, seat_id, requested_count] {
                return !join_confirmation_pending_ &&
                    state_.has_value() &&
                    og::ui::detail::find_player_by_seat_id(
                        *state_, seat_id) == nullptr &&
                    local_seat_count() == requested_count;
            });
        return true;
    }

    [[nodiscard]] std::size_t local_seat_count() const override
    {
        if (state_.has_value())
            return og::ui::detail::find_local_seats(*state_).size();
        if (spectator_mode_)
            return 0u;
        return static_cast<std::size_t>(
            std::clamp(local_player_count_, 0, MAX_PLAYERS));
    }

    bool request_start_game() override
    {
        if (start_request_pending_ || !transport_ || !local_player_is_host() ||
            pending_game_start_config_.has_value() ||
            g_start_game_requested)
            return false;

        if (join_confirmation_pending_ || awaiting_round_ready_reset_)
        {
            // GO immediately after a roster edit is a queued intent, not a
            // silent no-op. The poll loop dispatches it only after every
            // authoritative mutation/redistribution echo has settled.
            deferred_start_requested_ = true;
            start_request_pending_ = true;
            return false;
        }

        if (!dispatch_start_request())
            return false;

        // Over real sockets the verdict is usually ASYNC: the accept arrives
        // as a StartGame message, a denial as a later denial-bearing
        // LobbyState echo — BOTH are resolved inside handle_typed_message
        // (which drops start_request_pending_ on a denial so go_menu's
        // timeout-less wait loop can exit and surface last_start_denial()).
        // This poll only catches a same-call reply when the echo is already
        // queued (e.g. localhost loopback).
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
            og::ui::detail::find_local_seats(*state_);
        const short shared_team =
            og::sim::shared_allied_gameplay_team(*state_);
        const bool authoritative_spectator = local_seats.empty();

        og::ui::PickerLobbyGameStartConfig config;
        config.save_data =
            og::ui::detail::build_save_data_equivalent_from_state(
                *state_,
                authoritative_spectator,
                local_seats.size());
        config.difficulty =
            static_cast<std::int16_t>(state_->settings.difficulty);
        config.my_team = gameplay_start_team(config.save_data.allied_mode,
                                             local_team_, shared_team);
        config.is_networked = true;
        if (!local_seats.empty())
            config.local_player_index = local_seats.front()->player_index;
        if (!authoritative_spectator)
        {
            for (const og::sim::LobbyPlayer* const seat : local_seats)
            {
                config.local_player_indices.push_back(seat->player_index);
                config.local_seat_teams.push_back(gameplay_start_team(
                    config.save_data.allied_mode,
                    static_cast<short>(seat->team), shared_team));
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

    // §2.5 line-B alert: mirror of rebuild_status_lines()'s "Status: ..."
    // line, read live from the transport so the base camp shows the link
    // state every frame — Connected clears the alert and hands line B back
    // to the §9.12 session status.
    [[nodiscard]] std::optional<std::string> connection_alert() const override
    {
        // LINEUP §6: the host's kick outranks every link state. The server
        // sends the notice and then drops us, so without this the only thing
        // on screen would be a bare "connection lost".
        if (was_kicked_)
            return "KICKED BY HOST";
        if (!transport_)
            return "Status: connecting";
        switch (transport_->link_state())
        {
        case og::sim::TransportLinkState::Connecting:
            return "Status: connecting";
        case og::sim::TransportLinkState::Connected:
            // The link is fine but the session's packs could not be
            // installed; the reason came from PackTransferClient.
            if (pack_client_ && pack_client_->failed())
                return pack_client_->status_text();
            return std::nullopt;
        case og::sim::TransportLinkState::Failed:
            return "Status: connection failed";
        case og::sim::TransportLinkState::Lost:
            return "Status: connection lost";
        }
        return std::nullopt;
    }

    // §9.12 session status: the room this client joined through (empty for
    // direct/LAN joins — the status helper falls back to "JOINED - ...").
    [[nodiscard]] std::string session_room_code() const override
    {
        return relay_room_code_;
    }

    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return local_player_is_host();
    }

    // [NET-R5]: own slots only (see networked_save_slot_editable).
    [[nodiscard]] bool is_save_slot_editable(
        std::size_t slot_index) const noexcept override
    {
        return og::ui::detail::networked_save_slot_editable(
            slot_index,
            current_picker_save(),
            state_.has_value() ? &*state_ : nullptr);
    }

    bool request_seat_team_change(std::uint8_t player_index,
                                  short team) override
    {
        if (!transport_ || transport_->connected_peers().empty())
            return false;
        if (!state_.has_value() || team < 0 || team >= MAX_PLAYERS)
            return false;

        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            og::ui::detail::find_local_seats(*state_);
        const auto target_it = std::find_if(
            local_seats.begin(), local_seats.end(),
            [player_index](const og::sim::LobbyPlayer* seat) {
                return seat->player_index == player_index;
            });
        if (target_it == local_seats.end())
            return false;
        const og::sim::LobbySeatId target_seat_id = (*target_it)->seat_id;

        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            og::ui::detail::make_team_change_message(
                player_index, target_seat_id, team));
        // The remote round trip is asynchronous, but the server answers every
        // request (denials get an explicit state echo). An older unrelated
        // state may already be queued ahead of that echo, so only the stable
        // target's requested value proves acceptance; otherwise wait through
        // the bounded window instead of misreporting the first stale state as
        // a denial.
        return og::ui::detail::wait_for_authoritative_lobby_value(
            [this] { poll_and_apply(); },
            [this, target_seat_id, team] {
                if (!state_.has_value())
                    return false;
                const og::sim::LobbyPlayer* const echoed =
                    og::ui::detail::find_player_by_seat_id(
                        *state_, target_seat_id);
                return echoed != nullptr && echoed->team == team;
            });
    }

    // LINEUP §6: an ELECTED host kicks too. On a dedicated server (and after
    // a host migration) the machine that runs the lobby is a JOIN client, and
    // every other host control it owns is already live here — settings, the
    // start gate, the seat authority. Refusing the kick left the one machine
    // entitled to remove a peer as the only one that could not. The refusals
    // mirror the host client's: they save a round trip, and the server's own
    // host gate stays the authority (a crafted client reaches it directly).
    bool kick_machine(og::sim::LobbyMachineId machine_id) override
    {
        if (!transport_ || !state_.has_value() || !local_player_is_host() ||
            machine_id == og::sim::kInvalidLobbyMachineId)
        {
            return false;
        }
        for (const og::sim::LobbyPlayer* const seat :
             og::ui::detail::find_local_seats(*state_))
        {
            if (seat->machine_id == machine_id)
                return false; // leaving is disconnect_session(), not a kick
        }
        const bool known = std::any_of(
            state_->players.begin(), state_->players.end(),
            [machine_id](const og::sim::LobbyPlayer& player) {
                return player.machine_id == machine_id;
            });
        if (!known)
            return false;

        og::ui::detail::send_lobby_message(
            *transport_, server_peer_id_,
            og::ui::detail::make_kick_message(machine_id));
        poll_and_apply();
        return true;
    }

    // LINEUP §6: leave the session. shutdown() drops the transport, which is
    // what the host sees; the caller swaps in a local client afterwards.
    bool disconnect_session() override
    {
        if (!transport_)
            return false;
        shutdown();
        return true;
    }

    [[nodiscard]] bool was_kicked() const noexcept override
    {
        return was_kicked_;
    }

    bool set_ready(bool ready) override
    {
        if (!transport_ || transport_->connected_peers().empty())
            return false;

        if (ready && pack_client_)
        {
            // Packs mount before play: readiness waits for in-flight
            // transfers (bounded — the status line keeps showing progress on
            // a rejected click) and a failed transfer refuses ready outright,
            // which keeps the host's start gate (MachinesNotReady) closed.
            if (pack_client_->busy())
            {
                (void)og::ui::detail::wait_for_authoritative_lobby_value(
                    [this] { poll_and_apply(); },
                    [this] {
                        return !pack_client_->busy() || pack_client_->failed();
                    });
            }
            if (pack_client_->busy() || pack_client_->failed())
                return false;
        }

        if (ready &&
            (awaiting_round_ready_reset_ || join_confirmation_pending_))
        {
            // An early Ready click can race the host's return from gameplay.
            // Wait for the server's ordered new-round ready=false state before
            // sending, and also wait for this round's Join acknowledgement:
            // otherwise the active GameServer can drain the one-shot Join and
            // a Ready would arm the server's stale previous-round roster. The
            // wait is bounded, so a joiner that returns before the host simply
            // gets a rejected click and may retry once the host unlocks.
            const bool reset_observed =
                og::ui::detail::wait_for_authoritative_lobby_value(
                    [this] { poll_and_apply(); },
                    [this] {
                        return !awaiting_round_ready_reset_ &&
                            !join_confirmation_pending_;
                    });
            if (!reset_observed)
                return false;
        }

        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            og::ui::detail::make_ready_message(local_player_index(), ready));
        // Ignore unrelated LobbyState traffic queued before this request.
        // Only observing the requested value proves acceptance; an unchanged
        // denial is resolved by the bounded timeout.
        return og::ui::detail::wait_for_authoritative_lobby_value(
            [this] { poll_and_apply(); },
            [this, ready] { return local_ready() == ready; });
    }

    [[nodiscard]] bool local_ready() const noexcept override
    {
        if (!state_.has_value() || awaiting_round_ready_reset_ ||
            join_confirmation_pending_)
            return false;
        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_);
        return local_player != nullptr && local_player->ready;
    }

    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players() const override
    {
        if (!state_.has_value())
            return {};
        return state_->players;
    }

    [[nodiscard]] std::optional<std::uint8_t>
    authoritative_team_mask() const noexcept override
    {
        if (!state_.has_value())
            return std::nullopt;
        return og::sim::lobby_effective_team_mask(state_->settings);
    }

    // §2.5 base-camp ownership split: this machine's seats from the
    // recipient-specific, server-issued ownership grant.
    [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
        const override
    {
        if (!state_.has_value())
            return {};
        std::vector<std::uint8_t> indices;
        for (const og::sim::LobbyPlayer* const seat :
             og::ui::detail::find_local_seats(*state_))
        {
            indices.push_back(seat->player_index);
        }
        return indices;
    }

    [[nodiscard]] og::sim::StartDenialReason last_start_denial()
        const noexcept override
    {
        if (!state_.has_value())
            return og::sim::StartDenialReason::None;
        return static_cast<og::sim::StartDenialReason>(
            state_->last_start_denial);
    }

    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }

    // LINEUP §6: a joiner is in a session once the lobby state has landed —
    // and only while the link still carries it. state_ is kept across a drop
    // on purpose (the roster stays on screen under the "connection lost"
    // alert instead of blinking away), so the link has to be consulted
    // separately or a dead session would keep answering "established" and
    // hide the HOST / JOIN retry behind DISCONNECT.
    [[nodiscard]] bool session_established() const noexcept override
    {
        if (!state_.has_value() || transport_ == nullptr)
            return false;
        const og::sim::TransportLinkState link = transport_->link_state();
        return link != og::sim::TransportLinkState::Failed &&
            link != og::sim::TransportLinkState::Lost;
    }

    // Staged lobby (#218, C9): the joiner's preview surface is its mirror —
    // a headless local load healed by the owner's broadcast pair. The
    // install seam stays the client shadow (take_match_stage stays nullptr).
    [[nodiscard]] const GameWorld* staged_world() const override
    {
        return preview_mirror_.world();
    }

    [[nodiscard]] std::uint32_t stage_generation() const override
    {
        return preview_mirror_.refresh_serial();
    }

    [[nodiscard]] StagedPreviewHealth staged_preview_health() const override
    {
        switch (preview_mirror_.status())
        {
            case og::server::MirrorStatus::Staged:
                return StagedPreviewHealth::Staged;
            case og::server::MirrorStatus::Unavailable:
                return StagedPreviewHealth::Unavailable;
            case og::server::MirrorStatus::Empty:
                break;
        }
        return StagedPreviewHealth::None;
    }

    [[nodiscard]] bool staged_keyframe_bytes(
        std::uint32_t& generation,
        const std::vector<std::uint8_t>*& setup_bytes,
        const std::vector<std::uint8_t>*& keyframe_bytes) const override
    {
        return preview_mirror_.retained_pair(generation, setup_bytes,
                                             keyframe_bytes);
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
        // renders that seat's explicit gameplay team without recoloring any
        // fighter.
        std::vector<og::runtime::LocalSeatBinding> local_seats;
        bool authoritative_spectator = false;
        if (state_.has_value())
        {
            const short allied_mode = state_->settings.allied_mode;
            const short shared_team =
                og::sim::shared_allied_gameplay_team(*state_);
            for (const og::sim::LobbyPlayer* const seat :
                 og::ui::detail::find_local_seats(*state_))
            {
                local_seats.push_back(og::runtime::LocalSeatBinding{
                    .player_index = seat->player_index,
                    .team = gameplay_start_team(
                        allied_mode, static_cast<short>(seat->team),
                    shared_team),
                });
            }
            authoritative_spectator = local_seats.empty();
        }
        if (local_seats.empty() && !authoritative_spectator)
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

        // §2.8 follow caption: stamp each global player's company display
        // name (from the last lobby state) onto the installed runtime.
        if (state_.has_value())
        {
            std::vector<std::pair<std::uint8_t, std::string>> companies;
            for (const og::sim::LobbyPlayer& player : state_->players)
                companies.emplace_back(player.player_index, player.company);
            og::runtime::local_transport_shadow_set_player_companies(
                session, companies);
        }

        // Keep the connection ALIVE across gameplay (see the host note): the
        // runtime holds its own ref to the transport, so it outlives the
        // per-level runtime, and resume_after_level() rejoins the lobby over the
        // still-open socket. Only the per-start scratch state is cleared.
        start_request_pending_ = false;
        pending_start_request_id_ = 0;
        deferred_start_requested_ = false;
        pending_game_start_config_.reset();
        return true;
    }

    void resume_after_level() override
    {
        start_request_pending_ = false;
        pending_start_request_id_ = 0;
        deferred_start_requested_ = false;
        pending_game_start_config_.reset();

        // If the socket object survived but its upstream did not, it is no
        // more reusable than a null transport. Recreate it instead of sending
        // the one resume Join into a permanently Lost/Failed connection and
        // keeping the previous round's lobby cache forever.
        const bool transport_unusable = transport_ == nullptr ||
            transport_->link_state() == og::sim::TransportLinkState::Failed ||
            transport_->link_state() == og::sim::TransportLinkState::Lost ||
            transport_->connected_peers().empty();
        if (transport_unusable)
        {
            const std::vector<short> preserved_teams = local_seat_teams_;
            initialize_from_save();
            if (!preserved_teams.empty() && !local_seat_teams_.empty())
            {
                local_seat_teams_ = preserved_teams;
                SaveData* const save = current_picker_save();
                if (save != nullptr)
                {
                    og::ui::detail::resize_local_seat_assignments(
                        local_seat_teams_,
                        *save,
                        spectator_mode_,
                        local_player_count_);
                    local_team_ = local_seat_teams_.front();
                    save->my_team = local_team_;
                    sync_roster_from_save();
                }
            }
            return;
        }

        // The cached state can still say ready=true from the completed round.
        // Do not let that stale value satisfy a new set_ready(true) request:
        // unlock_for_new_round publishes ready=false before accepting the next
        // round's declarations, and handle_typed_message releases this guard
        // only after that authoritative reset reaches this peer.
        awaiting_round_ready_reset_ = true;

        // A new round is a fresh match: the previous round's mirror is stale
        // by construction (the host re-latches a fresh seed and restages).
        // Empty is the honest between-rounds preview state until the fresh
        // pair lands.
        preview_mirror_.dispose();

        // Reuse the still-open socket: this explicitly marked resume Join is
        // the only declaration a still-locked LobbyServer may retain for the
        // next round. Ordinary seat mutations are never queued across Start.
        settings_dirty_ = true;
        (void)prepare_pending_join_from_save(true);
        poll_and_apply();
    }

private:
    [[nodiscard]] bool local_player_is_host() const
    {
        if (!state_.has_value())
            return false;
        return state_->local_peer_is_host;
    }

    [[nodiscard]] std::uint8_t local_player_index() const
    {
        // Informational only: the LobbyServer keys on the sending peer.
        if (!state_.has_value())
            return 0xffu;
        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_);
        return local_player != nullptr ? local_player->player_index : 0xffu;
    }

    // Active seats declared in a Join. A spectator is a connected zero-seat
    // peer and consumes no lobby/gameplay capacity.
    [[nodiscard]] std::size_t join_seat_count() const noexcept
    {
        return spectator_mode_
            ? 0u
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

    bool send_pending_join()
    {
        if (!transport_ || pending_local_seats_.empty() ||
            pending_join_request_id_ == 0)
            return false;

        og::sim::LobbyMessage message;
        og::sim::LobbyJoinMessage join;
        join.request_id = pending_join_request_id_;
        join.resume_after_level = pending_join_resume_after_level_;
        join.player = pending_local_seats_.front();
        join.extra_players.assign(
            pending_local_seats_.begin() + 1,
            pending_local_seats_.end());
        message.payload = std::move(join);
        og::ui::detail::send_lobby_message(
            *transport_,
            server_peer_id_,
            std::move(message));
        join_confirmation_pending_ = true;
        join_state_serial_at_send_ = lobby_states_received_;
        next_join_retry_at_ =
            std::chrono::steady_clock::now() + kJoinRetryInterval;
        return true;
    }

    bool prepare_pending_join_from_save(bool resume_after_level = false)
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return false;

        if (spectator_mode_)
        {
            pending_local_seats_.clear();
            pending_join_request_id_ = 0;
            pending_join_resume_after_level_ = false;
            join_confirmation_pending_ = false;
            join_message_sent_ = false;
            return true;
        }

        pending_local_seats_ = og::ui::detail::build_local_lobby_seats(
            *save,
            player_name_,
            local_seat_teams_,
            nullptr);
        pending_join_request_id_ = next_join_request_id_++;
        if (next_join_request_id_ == 0)
            next_join_request_id_ = 1;
        pending_join_resume_after_level_ = resume_after_level;
        join_confirmation_pending_ = true;
        join_state_serial_at_send_ = lobby_states_received_;
        join_message_sent_ = false;
        return true;
    }

    bool send_join_from_save()
    {
        if (!transport_)
            return false;
        if (spectator_mode_)
        {
            og::sim::LobbyMessage message;
            message.payload = og::sim::LobbyLeaveMessage{
                .player_index = local_player_index(),
            };
            og::ui::detail::send_lobby_message(
                *transport_, server_peer_id_, std::move(message));
            join_confirmation_pending_ = false;
            pending_join_request_id_ = 0;
            pending_join_resume_after_level_ = false;
            pending_local_seats_.clear();
            return true;
        }
        if (pending_join_request_id_ == 0 &&
            !prepare_pending_join_from_save())
        {
            return false;
        }
        return send_pending_join();
    }

    bool dispatch_start_request()
    {
        if (!transport_ || !state_.has_value() ||
            !local_player_is_host() || join_confirmation_pending_ ||
            awaiting_round_ready_reset_ ||
            pending_game_start_config_.has_value() ||
            g_start_game_requested)
        {
            return false;
        }

        const og::sim::LobbyPlayer* const local_player =
            og::ui::detail::find_local_player(*state_);
        pending_game_start_config_.reset();
        deferred_start_requested_ = false;
        start_request_pending_ = true;
        pending_start_request_id_ = next_start_request_id_++;
        if (next_start_request_id_ == 0)
            next_start_request_id_ = 1;

        // Mirror the server's [NET-R4] clear-on-next-StartGame in the CACHED
        // state: a denial left over from a PREVIOUS attempt must not be read
        // as this request's verdict.
        state_->last_start_denial = og::sim::start_denial_reason_value(
            og::sim::StartDenialReason::None);

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local_player != nullptr
                ? local_player->player_index
                : std::uint8_t{0xffu},
            .request_id = pending_start_request_id_,
        };
        og::ui::detail::send_lobby_message(
            *transport_, server_peer_id_, std::move(message));
        return true;
    }

    void maybe_dispatch_deferred_start()
    {
        if (!deferred_start_requested_)
            return;
        if (!transport_ || !state_.has_value() || !local_player_is_host())
        {
            deferred_start_requested_ = false;
            start_request_pending_ = false;
            pending_start_request_id_ = 0;
            return;
        }
        (void)dispatch_start_request();
    }

    void handle_typed_message(const og::sim::TypedReceivedMessage& message)
    {
        if (pack_client_ && transport_ &&
            pack_client_->handle_message(*transport_, server_peer_id_,
                                         message))
        {
            return;
        }
        switch (message.kind)
        {
        case og::sim::TypedReceivedMessageKind::LobbyState:
            if (message.lobby_state)
            {
                ++lobby_states_received_;
                state_ = *message.lobby_state;
                // §4.3 [NET-R3] ASYNC denial resolution: a denied StartGame is
                // answered by a denial-bearing LobbyState echo and NO StartGame
                // message will ever follow it, so an outstanding request must
                // be released HERE — otherwise go_menu's timeout-less
                // `while (pending) poll` wait blocks forever on the
                // dedicated-server ELECTED host and the relay host (the
                // in-process direct host resolves synchronously and never
                // needs this). Match the echoed request ID too: a denial from
                // an older attempt may already be queued and must not release
                // a newer pending request.
                if (start_request_pending_ &&
                    og::ui::detail::start_denial_matches_request(
                        *state_, pending_start_request_id_))
                {
                    start_request_pending_ = false;
                    pending_start_request_id_ = 0;
                    deferred_start_requested_ = false;
                }
                const std::vector<const og::sim::LobbyPlayer*> local_seats =
                    og::ui::detail::find_local_seats(*state_);
                const std::vector<og::sim::LobbyPlayer> echoed_local_seats =
                    og::ui::detail::copy_local_seats(*state_);

                const bool all_local_seats_unready =
                    std::all_of(
                        local_seats.begin(), local_seats.end(),
                        [](const og::sim::LobbyPlayer* seat) {
                            return seat != nullptr && !seat->ready;
                        });
                if (awaiting_round_ready_reset_ &&
                    all_local_seats_unready)
                {
                    // Empty is intentionally vacuously unready: a spectator
                    // has no ready bit to reset between levels.
                    awaiting_round_ready_reset_ = false;
                }

                if (join_confirmation_pending_ &&
                    lobby_states_received_ > join_state_serial_at_send_ &&
                    pending_join_request_id_ != 0 &&
                    state_->last_join_request_id ==
                        pending_join_request_id_ &&
                    !awaiting_round_ready_reset_)
                {
                    // The correlated result can be empty when the global
                    // capacity rejects a spectator's ADD PLAYER. Empty is still a
                    // complete authoritative answer and must stop retries.
                    join_confirmation_pending_ = false;
                    pending_join_request_id_ = 0;
                    pending_join_resume_after_level_ = false;
                    pending_local_seats_ = echoed_local_seats;
                }

                if (!local_seats.empty())
                {
                    // A stale state may arrive after the resume declaration
                    // was drained by the still-running GameServer. Do not let
                    // it overwrite the desired seat vector; retry that vector
                    // until the LobbyServer confirms it.
                    if (!join_confirmation_pending_)
                    {
                        pending_local_seats_ = echoed_local_seats;
                        local_team_ = local_seats.front()->team;
                        local_seat_teams_.clear();
                        for (const og::sim::LobbyPlayer* const seat :
                             local_seats)
                        {
                            local_seat_teams_.push_back(seat->team);
                        }
                    }
                }
                else if (!join_confirmation_pending_ && join_message_sent_)
                {
                    pending_local_seats_.clear();
                }
            }
            break;

        case og::sim::TypedReceivedMessageKind::LobbyMessage:
            // LINEUP §6: the host's kick notice, sent immediately before the
            // server drops this peer. Latch it — the connection dies right
            // after, so the flag is the only surviving evidence of WHY, and
            // it must outlive shutdown() so the swap-in code can still read
            // it. Nothing else in this client's lifetime clears it.
            if (message.lobby_message &&
                message.lobby_message->kind() ==
                    og::sim::LobbyMessageKind::Kicked)
            {
                was_kicked_ = true;
                break;
            }
            if (message.lobby_message &&
                message.lobby_message->kind() ==
                    og::sim::LobbyMessageKind::StartGame)
            {
                if (!og::ui::detail::start_confirmation_matches_request(
                        *message.lobby_message,
                        start_request_pending_
                            ? pending_start_request_id_
                            : 0))
                {
                    break;
                }
                start_request_pending_ = false;
                pending_start_request_id_ = 0;
                deferred_start_requested_ = false;
                g_start_game_requested = true;
                pending_game_start_config_ = build_game_start_config();
            }
            break;

        // Staged lobby (#218, C9): retain the owner's generation-paired
        // staged pair; the heavy apply runs in maintain_preview_mirror() on
        // the poll cadence.
        case og::sim::TypedReceivedMessageKind::StagedMatchSetup:
            if (message.staged_match_setup)
                preview_mirror_.receive_setup(*message.staged_match_setup);
            break;

        case og::sim::TypedReceivedMessageKind::StagedMatchKeyframe:
            if (message.staged_match_keyframe)
                preview_mirror_.receive_keyframe(
                    *message.staged_match_keyframe);
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

    // Staged lobby (#218, C9): apply the retained pair into the headless
    // preview mirror. Runs AFTER apply_state_to_current_save so the local
    // level load reads the lobby-synced campaign (the same-batch ordering
    // race the mirror's campaign-moved retry also covers).
    void maintain_preview_mirror()
    {
        const SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;
        const int difficulty = state_.has_value()
            ? static_cast<int>(state_->settings.difficulty)
            : 1;
        preview_mirror_.ensure_applied(save->current_campaign,
                                       difficulty,
                                       og::server::stage_clock_now_ms());
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
            og::ui::detail::find_local_seats(*state_);
        if (join_confirmation_pending_ && !local_seats.empty())
        {
            // This can be a stale previous-round state received while the
            // desired Join is still being retried. Apply synchronized match
            // settings, but withhold local ownership so stale team/deploy
            // fields cannot overwrite the private company before authority
            // acknowledges the declaration.
            pending_merged_state = *state_;
            pending_merged_state->local_seat_ids.clear();
            applied_state = &*pending_merged_state;
        }
        else if (!local_seats.empty())
        {
            local_team_ = local_seats.front()->team;
            local_seat_teams_.clear();
            for (const og::sim::LobbyPlayer* const seat : local_seats)
                local_seat_teams_.push_back(seat->team);
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
                local_team_);
            local_team_ = merged.local_team;
            pending_merged_state = std::move(merged.state);
            applied_state = &*pending_merged_state;
        }

        const og::ui::detail::LobbyStateApplyResult applied =
            og::ui::detail::apply_lobby_state_to_save(
                *applied_state,
                *save,
                spectator_mode_,
                local_team_,
                static_cast<std::size_t>(std::max(local_player_count_, 0)),
                /*adopt_replay_arm=*/true);
        // A pending-merged/masked state mirrors or withholds this machine's
        // unconfirmed seats, so adoption can only fire off a REAL server echo.
        og::ui::detail::persist_deploy_reconciliation(*save, applied);
    }

    void rebuild_status_lines()
    {
        status_lines_.clear();
        if (!direct_url_.empty())
            status_lines_.push_back(std::format("Direct: {}", direct_url_));
        if (!relay_room_code_.empty())
            status_lines_.push_back(std::format("Room: {}", relay_room_code_));
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
        if (pack_client_)
        {
            // "Receiving pack X (N%)" while transferring, the failure
            // reason after a refused transfer, empty otherwise.
            const std::string pack_status = pack_client_->status_text();
            if (!pack_status.empty())
                status_lines_.push_back(pack_status);
        }
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
    // Class-pack transfer collector (protocol v10); lives for one join
    // connection, fed by handle_typed_message.
    std::unique_ptr<og::sim::PackTransferClient> pack_client_;
    og::sim::PeerId server_peer_id_ = 1;
    // True once the relay's announced room host has been adopted as the
    // authoritative server binding; later room-host migrations must not move
    // it (see update_server_peer_id).
    bool server_peer_id_adopted_ = false;
    std::optional<og::sim::LobbyState> state_;
    std::vector<std::string> status_lines_;
    bool spectator_mode_ = false;
    // Local seats this machine declares (0 = spectator, 1..MAX_PLAYERS).
    int local_player_count_ = 1;
    short local_team_ = 0;
    std::vector<short> local_seat_teams_;
    bool start_request_pending_ = false;
    bool deferred_start_requested_ = false;
    std::uint32_t next_start_request_id_ = 1;
    std::uint32_t pending_start_request_id_ = 0;
    bool join_message_sent_ = false;
    bool join_confirmation_pending_ = false;
    std::uint32_t next_join_request_id_ = 1;
    std::uint32_t pending_join_request_id_ = 0;
    bool pending_join_resume_after_level_ = false;
    std::uint64_t join_state_serial_at_send_ = 0;
    std::chrono::steady_clock::time_point next_join_retry_at_{};
    bool settings_dirty_ = false;
    // A completed round leaves ready=true in the last cached LobbyState.
    // While resuming, readiness stays locally false until the server's
    // new-round ready=false broadcast arrives.
    bool awaiting_round_ready_reset_ = false;
    // Counts authoritative states so relay host discovery is adopted only
    // before the first server binding.
    std::uint64_t lobby_states_received_ = 0;
    // This machine's declared seat list (seat 0 first), used to optimistically
    // merge our seats into echoed states that predate our join.
    std::vector<og::sim::LobbyPlayer> pending_local_seats_;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
    // Staged lobby (#218, C9): the joiner's staged preview mirror.
    og::server::StagedPreviewMirror preview_mirror_;
    // LINEUP §6: latched on a LobbyKickedMessage. Deliberately NOT cleared by
    // shutdown() — the kick is followed immediately by the disconnect, and the
    // UI reads this after tearing the client down to explain the drop.
    bool was_kicked_ = false;
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

std::unique_ptr<og::ui::IPickerRelayRoomListRequest>
begin_platform_list_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag)
{
    // Campaign/resource access and URL construction belong to the main
    // thread. The native worker receives only this immutable URL and its own
    // state, so it cannot observe a later PlatformBridge/GameSession swap.
    const std::string list_url = build_relay_room_list_url(
        base_url,
        campaign_content_hash(campaign_tag));

#ifdef __EMSCRIPTEN__
    return std::make_unique<EmscriptenRelayRoomListRequest>(list_url);
#else
    auto state = std::make_shared<NativeRelayRoomListRequestState>();
    native_relay_room_list_worker_owner().start(state, list_url);
    return std::make_unique<NativeRelayRoomListRequest>(std::move(state));
#endif
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

// ---------------------------------------------------------------------------
// Cloud saves (#155): blocking text HTTP for /api/save/<KEY>. The parsers
// live in the pure layer (cloud_save_client.cpp); this is transport only.
// ---------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__
namespace {

// Parse the EM_ASYNC_JS helpers' OK/ERR\n<status>\n<body> framing (the
// create_relay_room three-line split) into a CloudHttpResult. status 0 =
// transport failure; the message rides `error`.
og::ui::cloud::CloudHttpResult parse_cloud_http_framing(char* raw)
{
    const std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    og::ui::cloud::CloudHttpResult result;
    const std::string response =
        owned ? std::string(owned.get()) : std::string();
    const std::size_t first_newline = response.find('\n');
    const std::size_t second_newline =
        first_newline == std::string::npos
            ? std::string::npos
            : response.find('\n', first_newline + 1);
    if (first_newline == std::string::npos ||
        second_newline == std::string::npos)
    {
        result.error = "Cloud sync returned an invalid response";
        return result;
    }
    const std::string status_text =
        response.substr(first_newline + 1,
                        second_newline - first_newline - 1);
    int status = 0;
    (void)std::from_chars(status_text.data(),
                          status_text.data() + status_text.size(), status);
    result.status = status;
    result.body = response.substr(second_newline + 1);
    if (status == 0)
    {
        result.error = trim_copy(result.body);
        result.body.clear();
    }
    return result;
}

} // namespace
#endif

og::ui::cloud::CloudHttpResult platform_cloud_http_get(const std::string& url)
{
#ifdef __EMSCRIPTEN__
    return parse_cloud_http_framing(relay_http_get_text_js(url.c_str()));
#else
    og::sim::detail::IxNetSystemGuard net_system_guard;
    ix::HttpClient client;
    ix::HttpRequestArgsPtr args =
        client.createRequest(url, ix::HttpClient::kGet);
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    args->followRedirects = true;
    const ix::HttpResponsePtr response = client.get(url, args);
    og::ui::cloud::CloudHttpResult result;
    if (!response)
    {
        result.error = "no HTTP response";
        return result;
    }
    result.status = response->statusCode;
    result.body = response->body;
    if (result.status == 0)
        result.error = response->errorMsg.empty() ? "network error"
                                                  : response->errorMsg;
    return result;
#endif
}

og::ui::cloud::CloudHttpResult platform_cloud_http_post(
    const std::string& url,
    const std::string& json_body)
{
#ifdef __EMSCRIPTEN__
    return parse_cloud_http_framing(
        relay_http_post_body_text_js(url.c_str(), json_body.c_str()));
#else
    og::sim::detail::IxNetSystemGuard net_system_guard;
    ix::HttpClient client;
    ix::HttpRequestArgsPtr args =
        client.createRequest(url, ix::HttpClient::kPost);
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    args->followRedirects = true;
    args->extraHeaders["Content-Type"] = "application/json; charset=utf-8";
    const ix::HttpResponsePtr response = client.post(url, json_body, args);
    og::ui::cloud::CloudHttpResult result;
    if (!response)
    {
        result.error = "no HTTP response";
        return result;
    }
    result.status = response->statusCode;
    result.body = response->body;
    if (result.status == 0)
        result.error = response->errorMsg.empty() ? "network error"
                                                  : response->errorMsg;
    return result;
#endif
}

} // namespace og::platform
