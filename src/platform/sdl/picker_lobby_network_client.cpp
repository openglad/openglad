#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport_multiplex.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>

#ifdef __EMSCRIPTEN__
#include <openglad/platform/net_transport_emscripten_ws.h>
#else
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
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

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
std::string detect_lan_ipv4_address()
{
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
}
#else
std::string detect_lan_ipv4_address()
{
    return "127.0.0.1";
}
#endif

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
#ifdef __EMSCRIPTEN__
        throw std::runtime_error("Browser hosting requires relay support");
#else
        websocket_server_transport_ =
            std::make_shared<og::sim::WebSocketServerTransport>(options_.port);
#endif
        combined_transport_ = std::make_shared<og::sim::MultiplexTransport>(
            std::vector<std::shared_ptr<og::sim::ITransport>>{
                local_server_transport_,
                websocket_server_transport_,
            });
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
        local_client_transport_.reset();
        local_server_transport_.reset();
        spectator_mode_ = false;
        local_team_ = 0;
        start_request_pending_ = false;
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
             local_client_transport_->poll_typed())
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
        status_lines_.clear();
        status_lines_.push_back(
            std::format("LAN: {}:{}", detect_lan_ipv4_address(), options_.port));
        if (state_.has_value())
        {
            status_lines_.push_back(
                std::format("Lobby: {} player{}",
                            state_->players.size(),
                            state_->players.size() == 1 ? "" : "s"));
        }
    }

    og::ui::PickerHostGameOptions options_;
    std::string player_name_;
    std::shared_ptr<og::sim::InProcessTransport> local_server_transport_;
    std::shared_ptr<og::sim::InProcessTransport> local_client_transport_;
    std::shared_ptr<og::sim::ITransport> combined_transport_;
    std::shared_ptr<og::sim::ITransport> websocket_server_transport_;
    std::unique_ptr<og::sim::LobbyServer> server_;
    std::optional<og::sim::LobbyState> state_;
    std::vector<og::sim::LobbyPlayerBinding> player_bindings_;
    std::vector<std::string> status_lines_;
    bool spectator_mode_ = false;
    short local_team_ = 0;
    bool start_request_pending_ = false;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
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

        if (options_.mode != og::ui::PickerJoinMode::Direct)
            throw std::runtime_error("Relay room codes require Phase 32");

        direct_url_ = og::ui::normalize_direct_websocket_url(
            options_.direct_endpoint);
#ifdef __EMSCRIPTEN__
        transport_ =
            std::make_shared<og::sim::EmscriptenWebSocketTransport>(direct_url_);
#else
        transport_ =
            std::make_shared<og::sim::WebSocketClientTransport>(direct_url_);
#endif
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

        for (const og::sim::TypedReceivedMessage& message : transport_->poll_typed())
            handle_typed_message(message);
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
    if (options.mode == PickerJoinMode::Relay)
        throw std::invalid_argument("Relay room codes require Phase 32");
    if (trim_copy(options.direct_endpoint).empty())
        throw std::invalid_argument("Direct connect requires an IP:port");
    return std::make_unique<JoinPickerLobbyClient>(options);
}

bool picker_join_mode_supported(PickerJoinMode mode) noexcept
{
    return mode == PickerJoinMode::Direct;
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

} // namespace og::ui
