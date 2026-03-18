#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport_multiplex.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/net_transport_relay_ws.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <openglad/platform/net_transport_emscripten_ws.h>
#else
#include <ixwebsocket/IXHttpClient.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>
#endif

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#endif

extern bool g_start_game_requested;

namespace {

constexpr std::string_view kDefaultCampaignId = "org.openglad.gladiator";

struct OrderedLobbySlot {
    std::uint8_t slot_index = 0;
    std::size_t player_order = 0;
    std::size_t slot_order = 0;
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
    "https://relay.openglad.example";

#ifdef __EMSCRIPTEN__
EM_ASYNC_JS(char*, relay_http_post_text_js, (const char* url_cstr), {
    try {
        const url = UTF8ToString(url_cstr);
        const response = await fetch(url, { method: 'POST' });
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
    constexpr char hex_digits[] = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(text.size());
    for (const unsigned char ch :
         std::string(text.begin(), text.end()))
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~')
        {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(hex_digits[(ch >> 4) & 0x0f]);
        encoded.push_back(hex_digits[ch & 0x0f]);
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

std::string build_relay_room_websocket_url(std::string base_url,
                                           std::string_view room_code)
{
    base_url = relay_api_base_url(std::move(base_url));
    if (base_url.rfind("https://", 0) == 0)
        base_url.replace(0, 8, "wss://");
    else if (base_url.rfind("http://", 0) == 0)
        base_url.replace(0, 7, "ws://");
    return std::format("{}/room/{}", base_url, room_code);
}

std::string build_relay_room_create_url(std::string base_url,
                                        std::string_view campaign_tag)
{
    base_url = relay_api_base_url(std::move(base_url));
    if (base_url.rfind("ws://", 0) == 0)
        base_url.replace(0, 5, "http://");
    else if (base_url.rfind("wss://", 0) == 0)
        base_url.replace(0, 6, "https://");

    std::string url = base_url + "/create";
    const std::string encoded_tag = url_encode_component(campaign_tag);
    if (!encoded_tag.empty())
        url.append(std::format("?campaign={}", encoded_tag));
    return url;
}

std::string extract_room_code_from_create_response(std::string_view body)
{
    const std::string trimmed = trim_copy(std::string(body));
    if (trimmed.empty())
        throw std::runtime_error("Relay room creation returned an empty response");

    if (trimmed.front() == '{')
    {
        if (const auto code = extract_json_string_field(trimmed, "code");
            code.has_value())
        {
            return *code;
        }
        if (const auto code =
                extract_json_string_field(trimmed, "room_code");
            code.has_value())
        {
            return *code;
        }
    }

    return trimmed;
}

std::string create_relay_room_code(std::string_view base_url,
                                   std::string_view campaign_tag)
{
    const std::string create_url = build_relay_room_create_url(
        std::string(base_url),
        campaign_tag);

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
    return extract_room_code_from_create_response(body);
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
    return extract_room_code_from_create_response(response->body);
#endif
}

std::string current_campaign_tag()
{
    if (SaveData* const save = current_picker_save(); save != nullptr)
        return save->current_campaign;
    return std::string(kDefaultCampaignId);
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

og::sim::LobbyPlayer build_local_lobby_player(const SaveData& save,
                                              std::string_view player_name,
                                              short local_team)
{
    og::sim::LobbyPlayer player;
    player.name = std::string(player_name);
    player.team = local_team;
    player.ready = false;
    player.is_host = false;

    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
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

og::sim::LobbySaveDataEquivalent build_save_data_equivalent_from_state(
    const og::sim::LobbyState& state,
    bool spectator_mode)
{
    og::sim::LobbySaveDataEquivalent equivalent;
    equivalent.current_campaign = state.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state.settings.campaign_id;
    equivalent.scen_num =
        state.settings.scenario_id > 0 ? state.settings.scenario_id : 1;
    equivalent.numplayers = spectator_mode ? 0u : 1u;
    equivalent.allied_mode = state.settings.allied_mode;

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

    for (std::size_t index = 0; index < ordered_slots.size(); ++index)
    {
        og::sim::LobbyCharacterSlot compacted = *ordered_slots[index].slot;
        if (!slots_are_dense)
            compacted.slot_index = static_cast<std::uint8_t>(index);
        equivalent.team_list.push_back(std::move(compacted));
    }

    return equivalent;
}

void apply_lobby_state_to_save(const og::sim::LobbyState& state,
                               SaveData& save,
                               bool spectator_mode,
                               short local_team)
{
    save.current_campaign = state.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state.settings.campaign_id;
    save.scen_num = state.settings.scenario_id > 0
        ? state.settings.scenario_id
        : 1;
    save.allied_mode = state.settings.allied_mode;
    save.numplayers = static_cast<unsigned char>(spectator_mode ? 0 : 1);
    save.my_team = local_team;

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

    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;

    for (std::size_t index = 0; index < ordered_slots.size(); ++index)
    {
        std::uint8_t slot_index = ordered_slots[index].slot_index;
        if (!slots_are_dense)
            slot_index = static_cast<std::uint8_t>(index);
        if (slot_index >= save.team_list.size())
            continue;

        save.team_list[slot_index] =
            make_guy_from_lobby_character(ordered_slots[index].slot->character);
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
                                        short local_team)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{
        .player = build_local_lobby_player(save, player_name, local_team),
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

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0xffu,
        .settings = std::move(settings),
    };
    return message;
}

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

std::string detect_lan_ipv4_address()
{
#ifdef __EMSCRIPTEN__
    char* const hostname = current_browser_hostname_js();
    if (hostname == nullptr)
        return "127.0.0.1";

    std::string value = hostname;
    std::free(hostname);
    if (value.empty())
        return "127.0.0.1";
    return value;
#elif defined(__unix__) || defined(__APPLE__)
    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0 || ifaddr == nullptr)
        return "127.0.0.1";

    std::string result = "127.0.0.1";
    for (ifaddrs* current = ifaddr; current != nullptr; current = current->ifa_next)
    {
        if (current->ifa_addr == nullptr ||
            current->ifa_addr->sa_family != AF_INET ||
            (current->ifa_flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }

        char buffer[INET_ADDRSTRLEN] = {};
        const sockaddr_in* const address =
            reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer)))
        {
            result = buffer;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return result;
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

} // namespace

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

        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        spectator_mode_ = og::ui::is_spectator_mode(*save);
        local_team_ = resolve_initial_local_team(*save);
        save->numplayers = static_cast<unsigned char>(spectator_mode_ ? 0 : 1);
        save->my_team = local_team_;

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
                relay_room_code_ = og::ui::normalize_relay_room_code(
                    create_relay_room_code(relay_base_url,
                                           current_campaign_tag()));
                relay_transport_ =
                    std::make_shared<og::sim::RelayWebSocketTransport>(
                        build_relay_room_websocket_url(relay_base_url,
                                                       relay_room_code_));
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

        send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            make_settings_message(*save));
        send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            make_join_message(*save, player_name_, local_team_));

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
        websocket_server_transport_.reset();
        relay_transport_.reset();
        local_client_transport_.reset();
        local_server_transport_.reset();
        spectator_mode_ = false;
        local_team_ = 0;
        start_request_pending_ = false;
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
        save->numplayers = static_cast<unsigned char>(spectator_mode_ ? 0 : 1);
        send_join_from_save();
        poll_and_apply();
    }

    bool request_start_game() override
    {
        if (!local_client_transport_ || !state_.has_value())
            return false;

        const og::sim::LobbyPlayer* const local_player =
            find_local_player(*state_, player_name_);
        if (local_player == nullptr || !local_player->is_host)
            return false;

        pending_game_start_config_.reset();
        start_request_pending_ = true;

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local_player->player_index,
        };
        send_lobby_message(
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
        config.save_data.numplayers = spectator_mode_ ? 0u : 1u;
        config.difficulty = state_.has_value()
            ? static_cast<std::int16_t>(state_->settings.difficulty)
            : static_cast<std::int16_t>(server_->state().settings.difficulty);
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

    bool install_gameplay_runtime(og::runtime::GameSession& session,
                                  screen& gameplay_screen) override
    {
        if (!combined_transport_ || !local_client_transport_)
            return false;

        if (player_bindings_.empty() && server_ != nullptr)
            player_bindings_ = server_->build_player_bindings();

        og::runtime::reset_network_host_transport_shadow(
            session,
            gameplay_screen,
            combined_transport_,
            local_client_transport_,
            player_bindings_);

        server_.reset();
        combined_transport_.reset();
        websocket_server_transport_.reset();
        relay_transport_.reset();
        local_client_transport_.reset();
        local_server_transport_.reset();
        state_.reset();
        player_bindings_.clear();
        start_request_pending_ = false;
        pending_game_start_config_.reset();
        return true;
    }

private:
    void send_settings_from_save()
    {
        if (!local_client_transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            make_settings_message(*save));
    }

    void send_join_from_save()
    {
        if (!local_client_transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        send_lobby_message(
            *local_client_transport_,
            local_client_transport_->local_peer_id(),
            make_join_message(*save, player_name_, local_team_));
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
                        find_local_player(*state_, player_name_))
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

        apply_lobby_state_to_save(*state_, *save, spectator_mode_, local_team_);
    }

    void rebuild_status_lines()
    {
        status_lines_ = og::ui::build_host_picker_status_lines(
            detect_lan_ipv4_address(),
            static_cast<bool>(websocket_server_transport_),
            options_.port,
            direct_status_message_,
            relay_room_code_,
            relay_status_message_,
            state_.has_value()
                ? std::optional<std::size_t>(state_->players.size())
                : std::nullopt);
    }

    og::ui::PickerHostGameOptions options_;
    std::string player_name_;
    std::shared_ptr<og::sim::InProcessTransport> local_server_transport_;
    std::shared_ptr<og::sim::InProcessTransport> local_client_transport_;
    std::shared_ptr<og::sim::ITransport> combined_transport_;
    std::shared_ptr<og::sim::ITransport> websocket_server_transport_;
    std::shared_ptr<og::sim::RelayWebSocketTransport> relay_transport_;
    std::unique_ptr<og::sim::LobbyServer> server_;
    std::optional<og::sim::LobbyState> state_;
    std::vector<og::sim::LobbyPlayerBinding> player_bindings_;
    std::vector<std::string> status_lines_;
    bool spectator_mode_ = false;
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

        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        spectator_mode_ = og::ui::is_spectator_mode(*save);
        local_team_ = resolve_initial_local_team(*save);
        save->numplayers = static_cast<unsigned char>(spectator_mode_ ? 0 : 1);
        save->my_team = local_team_;

        server_peer_id_ = 1;
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
                build_relay_room_websocket_url(
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
        local_team_ = 0;
        start_request_pending_ = false;
        join_message_sent_ = false;
        settings_dirty_ = false;
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
        save->numplayers = static_cast<unsigned char>(spectator_mode_ ? 0 : 1);
        join_message_sent_ = false;
        poll_and_apply();
    }

    bool request_start_game() override
    {
        if (!transport_ || !local_player_is_host())
            return false;

        const og::sim::LobbyPlayer* const local_player =
            find_local_player(*state_, player_name_);
        if (local_player == nullptr)
            return false;

        pending_game_start_config_.reset();
        start_request_pending_ = true;

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local_player->player_index,
        };
        send_lobby_message(*transport_, server_peer_id_, std::move(message));

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

        og::ui::PickerLobbyGameStartConfig config;
        config.save_data =
            build_save_data_equivalent_from_state(*state_, spectator_mode_);
        config.difficulty =
            static_cast<std::int16_t>(state_->settings.difficulty);
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

    bool install_gameplay_runtime(og::runtime::GameSession& session,
                                  screen& gameplay_screen) override
    {
        if (!transport_)
            return false;

        og::runtime::reset_network_client_transport_shadow(
            session,
            gameplay_screen,
            transport_,
            server_peer_id_);
        transport_.reset();
        state_.reset();
        start_request_pending_ = false;
        pending_game_start_config_.reset();
        return true;
    }

private:
    [[nodiscard]] bool local_player_is_host() const
    {
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const local_player =
            find_local_player(*state_, player_name_);
        return local_player != nullptr && local_player->is_host;
    }

    void send_settings_from_save()
    {
        if (!transport_ || !state_.has_value())
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        send_lobby_message(
            *transport_,
            server_peer_id_,
            make_settings_message(*save));
    }

    void send_join_from_save()
    {
        if (!transport_)
            return;
        SaveData* const save = current_picker_save();
        if (save == nullptr)
            return;

        send_lobby_message(
            *transport_,
            server_peer_id_,
            make_join_message(*save, player_name_, local_team_));
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
                        find_local_player(*state_, player_name_))
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

        if (const auto host_peer_id = relay_transport->host_peer_id();
            host_peer_id.has_value() && *host_peer_id != 0)
        {
            server_peer_id_ = *host_peer_id;
        }
    }

    void apply_state_to_current_save()
    {
        SaveData* const save = current_picker_save();
        if (save == nullptr || !state_.has_value())
            return;

        apply_lobby_state_to_save(*state_, *save, spectator_mode_, local_team_);
    }

    void rebuild_status_lines()
    {
        status_lines_.clear();
        if (!direct_url_.empty())
            status_lines_.push_back(std::format("Direct: {}", direct_url_));
        if (!relay_room_code_.empty())
            status_lines_.push_back(std::format("Relay: {}", relay_room_code_));
        status_lines_.push_back(
            transport_ && !transport_->connected_peers().empty()
                ? "Status: connected"
                : "Status: connecting");
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
    std::optional<og::sim::LobbyState> state_;
    std::vector<std::string> status_lines_;
    bool spectator_mode_ = false;
    short local_team_ = 0;
    bool start_request_pending_ = false;
    bool join_message_sent_ = false;
    bool settings_dirty_ = false;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
};

} // namespace

namespace og::ui {

std::unique_ptr<IPickerLobbyClient>
create_host_picker_lobby_client(const PickerHostGameOptions& options)
{
    if (options.port <= 0 || options.port > 65535)
        throw std::invalid_argument("Host port must be in the range 1-65535");
    return std::make_unique<HostPickerLobbyClient>(options);
}

std::unique_ptr<IPickerLobbyClient>
create_join_picker_lobby_client(const PickerJoinGameOptions& options)
{
    if (options.mode == PickerJoinMode::Direct &&
        trim_copy(options.direct_endpoint).empty())
    {
        throw std::invalid_argument("Direct connect requires an IP:port");
    }
    if (options.mode == PickerJoinMode::Relay &&
        trim_copy(options.room_code).empty())
    {
        throw std::invalid_argument("Relay join requires a room code");
    }
    return std::make_unique<JoinPickerLobbyClient>(options);
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

} // namespace og::ui
