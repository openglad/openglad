#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/picker_lobby_network_runtime.h>
#include <openglad/platform/net_transport_relay_ws.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>

#include <gtest/gtest.h>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern bool g_start_game_requested;

namespace og::ui {

std::vector<std::string> build_host_picker_status_lines(
    const std::string& direct_address,
    bool has_direct_transport,
    int port,
    const std::string& direct_status_message,
    const std::string& relay_room_code,
    const std::string& relay_status_message,
    std::optional<std::size_t> player_count);

} // namespace og::ui

namespace og::ui::detail {

std::array<bool, MAX_TEAM_SIZE> build_remote_owned_slot_mask(
    const og::sim::LobbyState& state,
    std::string_view local_player_name);

og::sim::LobbyMessage make_join_message(
    const SaveData& save,
    std::string_view player_name,
    short local_team,
    const std::array<bool, MAX_TEAM_SIZE>* excluded_slots);

} // namespace og::ui::detail

namespace {

using namespace std::chrono_literals;

class IxNetSystemScope
{
public:
    IxNetSystemScope()
    {
        if (!ix::initNetSystem())
            throw std::runtime_error("failed to initialize IXWebSocket network system");
    }

    ~IxNetSystemScope()
    {
        (void)ix::uninitNetSystem();
    }
};

struct PickerSaveStateGuard
{
    SaveData& save;
    std::string current_campaign;
    short scen_num = 0;
    unsigned char team_size = 0;
    unsigned char numplayers = 0;
    short allied_mode = 0;
    short my_team = 0;
    std::unique_ptr<guy> team_list[MAX_TEAM_SIZE];

    explicit PickerSaveStateGuard(SaveData& save_in)
        : save(save_in)
        , current_campaign(save.current_campaign)
        , scen_num(save.scen_num)
        , team_size(save.team_size)
        , numplayers(save.numplayers)
        , allied_mode(save.allied_mode)
        , my_team(save.my_team)
    {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[i] = std::move(save.team_list[i]);
    }

    ~PickerSaveStateGuard()
    {
        save.current_campaign = current_campaign;
        save.scen_num = scen_num;
        save.team_size = team_size;
        save.numplayers = numplayers;
        save.allied_mode = allied_mode;
        save.my_team = my_team;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[i] = std::move(team_list[i]);
    }
};

struct PickerRuntimeGuard
{
    bool start_requested = g_start_game_requested;
    int difficulty = og::runtime::current_session != nullptr
        ? og::runtime::current_session->current_difficulty_
        : 1;

    ~PickerRuntimeGuard()
    {
        g_start_game_requested = start_requested;
        if (og::runtime::current_session != nullptr)
            og::runtime::current_session->current_difficulty_ = difficulty;
    }
};

template <typename Predicate>
bool wait_until(Predicate&& predicate,
                std::chrono::milliseconds timeout = 3s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

bool status_lines_contain_prefix(const std::vector<std::string>& lines,
                                 std::string_view prefix)
{
    return std::any_of(
        lines.begin(),
        lines.end(),
        [prefix](const std::string& line) {
            return line.rfind(prefix, 0) == 0;
        });
}

bool status_lines_contain_exact(const std::vector<std::string>& lines,
                                std::string_view expected)
{
    return std::any_of(
        lines.begin(),
        lines.end(),
        [expected](const std::string& line) {
            return line == expected;
        });
}

void prepare_single_member_network_save(SaveData& save,
                                        short team,
                                        const char* name,
                                        std::size_t slot_index = 0)
{
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.current_levels.clear();
    save.current_levels[save.current_campaign] = save.scen_num;
    save.team_size = static_cast<unsigned char>(1);
    save.numplayers = static_cast<unsigned char>(1);
    save.allied_mode = static_cast<short>(0);
    save.my_team = team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i].reset();

    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = name ? std::string(name) : std::string("Network Tester");
    member->teamnum = team;
    save.team_list[std::min<std::size_t>(slot_index, save.team_list.size() - 1)] =
        std::move(member);
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

og::sim::LobbyCharacterSlot make_lobby_slot(std::uint8_t slot_index,
                                            std::string_view name,
                                            short team)
{
    guy member(FAMILY_SOLDIER);
    member.name = std::string(name);
    member.teamnum = team;
    return og::sim::LobbyCharacterSlot{
        .slot_index = slot_index,
        .character = make_lobby_character_data(member),
    };
}

void send_lobby_message(og::sim::ITransport& transport,
                        og::sim::PeerId peer_id,
                        og::sim::LobbyMessage message)
{
    transport.send_lobby_message(
        peer_id,
        std::make_shared<og::sim::LobbyMessage>(std::move(message)));
}

void send_start_game_message(og::sim::ITransport& transport,
                             og::sim::PeerId peer_id,
                             std::uint8_t player_index)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyStartGameMessage{
        .player_index = player_index,
    };
    send_lobby_message(transport, peer_id, std::move(message));
}

std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>>
poll_lobby_messages(og::sim::ITransport& transport)
{
    std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>> messages;

    if (transport.supports_typed_messages())
    {
        for (const auto& typed_message : transport.poll_typed())
        {
            if (typed_message.kind !=
                    og::sim::TypedReceivedMessageKind::LobbyMessage ||
                !typed_message.lobby_message)
            {
                continue;
            }

            messages.emplace_back(
                typed_message.peer_id, *typed_message.lobby_message);
        }
        return messages;
    }

    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope) ||
            envelope.message_type != og::sim::kLobbyMessageType)
        {
            continue;
        }

        const auto decoded =
            og::sim::deserialize_lobby_message(message.data);
        if (!decoded.has_value())
            continue;

        messages.emplace_back(message.peer_id, *decoded);
    }

    return messages;
}

bool wait_until_host_owns_room(og::sim::RelayWebSocketTransport& transport,
                               std::chrono::milliseconds timeout = 3s)
{
    return wait_until(
        [&] {
            (void)transport.poll();
            return transport.local_peer_id().has_value() &&
                transport.host_peer_id() == transport.local_peer_id();
        },
        timeout);
}

og::runtime::GameSession* active_game_session()
{
    if (og::runtime::current_game_session != nullptr)
        return og::runtime::current_game_session;
    return dynamic_cast<og::runtime::GameSession*>(og::runtime::current_session);
}

bool install_gameplay_runtime_from_handoff(og::ui::IPickerLobbyClient& client)
{
    og::runtime::GameSession* const session = active_game_session();
    if (session == nullptr || session->myscreen_ == nullptr)
        return false;
    return og::platform::install_picker_lobby_gameplay_runtime(
        &client,
        *session,
        *session->myscreen_);
}

class FakeRelayServer
{
public:
    explicit FakeRelayServer(int port,
                             int create_status_code = 200,
                             std::string create_response_body =
                                 R"({"room_code":"glad-xkcd"})")
        : server_(port, "127.0.0.1", 5, 16)
        , create_status_code_(create_status_code)
        , create_response_body_(std::move(create_response_body))
    {
        server_.setOnConnectionCallback(
            [this](const ix::HttpRequestPtr& request,
                   const std::shared_ptr<ix::ConnectionState>&) {
                if (request && request->method == "POST" &&
                    request->uri.rfind("/api/create", 0) == 0)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    last_create_uri_ = request->uri;

                    ix::WebSocketHttpHeaders headers;
                    headers["Content-Type"] = "application/json";
                    return std::make_shared<ix::HttpResponse>(
                        create_status_code_,
                        create_status_code_ >= 200 && create_status_code_ < 300
                            ? "OK"
                            : "ERROR",
                        ix::HttpErrorCode::Ok,
                        headers,
                        create_response_body_);
                }

                return std::make_shared<ix::HttpResponse>(
                    404,
                    "Not Found",
                    ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders(),
                    std::string());
            });
        server_.setOnClientMessageCallback(
            [this](std::shared_ptr<ix::ConnectionState> connection_state,
                   ix::WebSocket& websocket,
                   const ix::WebSocketMessagePtr& message) {
                handle_client_message(
                    std::move(connection_state), websocket, message);
            });

        if (!server_.listenAndStart())
            throw std::runtime_error("failed to start fake relay server");
    }

    ~FakeRelayServer()
    {
        server_.stop();
    }

    [[nodiscard]] std::string last_create_uri() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_create_uri_;
    }

private:
    struct PeerState {
        og::sim::PeerId peer_id = 0;
        std::weak_ptr<ix::WebSocket> socket;
    };

    static constexpr std::uint8_t kSendToPeerTag = 1u;
    static constexpr std::uint8_t kReceiveFromPeerTag = 2u;

    void handle_client_message(
        const std::shared_ptr<ix::ConnectionState>& connection_state,
        ix::WebSocket& websocket,
        const ix::WebSocketMessagePtr& message)
    {
        if (!connection_state || !message)
            return;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
            handle_open(connection_state->getId(), websocket);
            break;

        case ix::WebSocketMessageType::Message:
            if (message->binary)
                handle_binary(connection_state->getId(), message->str);
            break;

        case ix::WebSocketMessageType::Close:
        case ix::WebSocketMessageType::Error:
            handle_close(connection_state->getId());
            break;

        default:
            break;
        }
    }

    void handle_open(const std::string& connection_id, ix::WebSocket& websocket)
    {
        const std::shared_ptr<ix::WebSocket> socket = resolve_socket(websocket);
        if (!socket)
            return;

        og::sim::PeerId peer_id = 0;
        og::sim::PeerId host_peer_id = 0;
        std::vector<og::sim::PeerId> peer_ids;
        std::vector<std::shared_ptr<ix::WebSocket>> peer_joined_targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            peer_id = next_peer_id_++;
            peers_.emplace(connection_id,
                           PeerState{
                               .peer_id = peer_id,
                               .socket = socket,
                           });
            if (!host_peer_id_.has_value())
                host_peer_id_ = peer_id;
            host_peer_id = *host_peer_id_;

            peer_ids.reserve(peers_.size());
            for (const auto& [other_connection_id, other_peer] : peers_)
            {
                peer_ids.push_back(other_peer.peer_id);
                if (other_connection_id == connection_id)
                    continue;

                if (const std::shared_ptr<ix::WebSocket> other_socket =
                        other_peer.socket.lock())
                {
                    peer_joined_targets.push_back(other_socket);
                }
            }
        }

        std::sort(peer_ids.begin(), peer_ids.end());
        send_text(socket, make_joined_text(peer_id, host_peer_id));
        send_text(socket, make_peer_list_text(peer_ids, host_peer_id));

        const std::string peer_joined_text =
            make_peer_joined_text(peer_id, host_peer_id);
        for (const auto& other_socket : peer_joined_targets)
            send_text(other_socket, peer_joined_text);
    }

    void handle_binary(const std::string& connection_id, const std::string& payload)
    {
        og::sim::PeerId source_peer_id = 0;
        std::shared_ptr<ix::WebSocket> target_socket;
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(payload.data()),
            payload.size());
        if (bytes.size() < 5u || bytes[0] != kSendToPeerTag)
            return;

        const og::sim::PeerId target_peer_id =
            static_cast<og::sim::PeerId>(bytes[1]) |
            (static_cast<og::sim::PeerId>(bytes[2]) << 8) |
            (static_cast<og::sim::PeerId>(bytes[3]) << 16) |
            (static_cast<og::sim::PeerId>(bytes[4]) << 24);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto peer_it = peers_.find(connection_id);
            if (peer_it == peers_.end())
                return;
            source_peer_id = peer_it->second.peer_id;

            const auto target_it = std::find_if(
                peers_.begin(),
                peers_.end(),
                [target_peer_id](const auto& entry) {
                    return entry.second.peer_id == target_peer_id;
                });
            if (target_it == peers_.end())
                return;

            target_socket = target_it->second.socket.lock();
        }
        if (!target_socket)
            return;

        std::string outbound;
        outbound.reserve(payload.size());
        outbound.push_back(static_cast<char>(kReceiveFromPeerTag));
        append_peer_id(outbound, source_peer_id);
        outbound.append(payload.begin() + 5, payload.end());
        (void)target_socket->sendBinary(outbound);
    }

    void handle_close(const std::string& connection_id)
    {
        og::sim::PeerId peer_id = 0;
        bool was_host = false;
        std::optional<og::sim::PeerId> new_host_peer_id;
        std::vector<std::shared_ptr<ix::WebSocket>> recipients;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto peer_it = peers_.find(connection_id);
            if (peer_it == peers_.end())
                return;

            peer_id = peer_it->second.peer_id;
            was_host =
                host_peer_id_.has_value() && *host_peer_id_ == peer_id;
            peers_.erase(peer_it);

            if (was_host)
            {
                host_peer_id_.reset();
                for (const auto& [_, peer] : peers_)
                {
                    if (!host_peer_id_.has_value() ||
                        peer.peer_id < *host_peer_id_)
                    {
                        host_peer_id_ = peer.peer_id;
                    }
                }
                new_host_peer_id = host_peer_id_;
            }

            recipients.reserve(peers_.size());
            for (const auto& [_, peer] : peers_)
            {
                if (const std::shared_ptr<ix::WebSocket> socket =
                        peer.socket.lock())
                {
                    recipients.push_back(socket);
                }
            }
        }

        send_text_to_sockets(recipients, make_peer_left_text(peer_id));
        if (was_host && new_host_peer_id.has_value())
        {
            send_text_to_sockets(
                recipients,
                make_host_changed_text(*new_host_peer_id));
        }
    }

    static std::string make_joined_text(og::sim::PeerId peer_id,
                                        og::sim::PeerId host_peer_id)
    {
        return std::format(
            "{{\"type\":\"joined\",\"peer_id\":{},\"host\":{}}}",
            peer_id,
            host_peer_id);
    }

    static std::string make_peer_joined_text(og::sim::PeerId peer_id,
                                             og::sim::PeerId host_peer_id)
    {
        return std::format(
            "{{\"type\":\"peer_joined\",\"peer_id\":{},\"is_host\":{}}}",
            peer_id,
            peer_id == host_peer_id ? "true" : "false");
    }

    static std::string make_peer_left_text(og::sim::PeerId peer_id)
    {
        return std::format(
            "{{\"type\":\"peer_left\",\"peer_id\":{}}}",
            peer_id);
    }

    static std::string make_host_changed_text(og::sim::PeerId host_peer_id)
    {
        return std::format(
            "{{\"type\":\"host_changed\",\"new_host\":{}}}",
            host_peer_id);
    }

    static std::string make_peer_list_text(
        const std::vector<og::sim::PeerId>& peer_ids,
        og::sim::PeerId host_peer_id)
    {
        std::string peers_json = "[";
        for (std::size_t index = 0; index < peer_ids.size(); ++index)
        {
            if (index != 0)
                peers_json.push_back(',');
            peers_json.append(std::to_string(peer_ids[index]));
        }
        peers_json.push_back(']');

        return std::format(
            "{{\"type\":\"peer_list\",\"peers\":{},\"host\":{}}}",
            peers_json,
            host_peer_id);
    }

    static void send_text_to_sockets(
        const std::vector<std::shared_ptr<ix::WebSocket>>& sockets,
        const std::string& text)
    {
        for (const auto& socket : sockets)
            send_text(socket, text);
    }

    static void send_text(const std::shared_ptr<ix::WebSocket>& socket,
                          const std::string& text)
    {
        if (!socket)
            return;
        (void)socket->send(text);
    }

    static void append_peer_id(std::string& payload, og::sim::PeerId peer_id)
    {
        payload.push_back(static_cast<char>(peer_id & 0xffu));
        payload.push_back(static_cast<char>((peer_id >> 8) & 0xffu));
        payload.push_back(static_cast<char>((peer_id >> 16) & 0xffu));
        payload.push_back(static_cast<char>((peer_id >> 24) & 0xffu));
    }

    std::shared_ptr<ix::WebSocket> resolve_socket(ix::WebSocket& websocket)
    {
        for (const auto& client : server_.getClients())
        {
            if (client.get() == &websocket)
                return client;
        }
        return {};
    }

    ix::HttpServer server_;
    mutable std::mutex mutex_;
    og::sim::PeerId next_peer_id_ = 1;
    std::optional<og::sim::PeerId> host_peer_id_;
    std::map<std::string, PeerState> peers_;
    int create_status_code_ = 200;
    std::string create_response_body_;
    std::string last_create_uri_;
};

} // namespace

TEST(PickerNetworkClient, host_direct_flow_syncs_save_and_builds_start_config)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 2, "Host", 4);
    save.my_team = MAX_PLAYERS;
    g_start_game_requested = false;
    og::runtime::current_session->current_difficulty_ = 4;

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    ASSERT_NE(nullptr, active_game_session());
    ASSERT_NE(nullptr, active_game_session()->myscreen_);
    EXPECT_FALSE(install_gameplay_runtime_from_handoff(*host_client));
    host_client->initialize_from_save();

    EXPECT_EQ(2, save.my_team);
    ASSERT_TRUE(save.team_list[0] != nullptr);
    EXPECT_EQ("Host", save.team_list[0]->name);
    EXPECT_TRUE(status_lines_contain_prefix(host_client->status_lines(), "LAN: "));
    EXPECT_TRUE(
        status_lines_contain_exact(host_client->status_lines(), "Lobby: 1 player"));
    EXPECT_TRUE(host_client->host_controls_visible());

    save.scen_num = 2;
    save.allied_mode = 1;
    save.team_list[0]->name = "Host Prime";
    og::runtime::current_session->current_difficulty_ = 6;
    host_client->sync_from_save();
    host_client->sync_settings_from_save();
    host_client->sync_roster_from_save();
    host_client->set_player_mode(0);

    ASSERT_TRUE(host_client->request_start_game());
    EXPECT_FALSE(host_client->start_request_pending());
    ASSERT_TRUE(host_client->has_game_start_config());
    ASSERT_TRUE(host_client->build_game_start_config().has_value());

    const auto start_config = host_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ("org.openglad.gladiator", start_config->save_data.current_campaign);
    EXPECT_EQ(2, start_config->save_data.scen_num);
    EXPECT_EQ(0u, start_config->save_data.numplayers);
    EXPECT_EQ(1, start_config->save_data.allied_mode);
    EXPECT_EQ(6, start_config->difficulty);
    ASSERT_EQ(1u, start_config->save_data.team_list.size());
    EXPECT_EQ("Host Prime", start_config->save_data.team_list[0].character.name);
    EXPECT_FALSE(host_client->consume_game_start_config().has_value());

    ASSERT_TRUE(save.save("save0"));
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*host_client));
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));
    og::runtime::clear_local_transport_shadow(*active_game_session());

    host_client->shutdown();
}

TEST(PickerNetworkClient, host_relay_flow_creates_room_code_and_encodes_campaign_tag)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    save.current_campaign = "relay test/space";
    g_start_game_requested = false;

    const int relay_port = ix::getFreePort();
    FakeRelayServer relay_server(relay_port, 200, R"({"code":"glad-xkcd"})");

    og::ui::PickerHostGameOptions options;
    options.port = ix::getFreePort();
    options.enable_relay = true;
    options.relay_base_url = std::format("ws://127.0.0.1:{}", relay_port);
    auto host_client = og::ui::create_host_picker_lobby_client(options);
    host_client->initialize_from_save();

    const auto status = host_client->status_lines();
    EXPECT_TRUE(status_lines_contain_prefix(status, "LAN: "));
    EXPECT_TRUE(status_lines_contain_exact(status, "Relay: GLAD-XKCD"));
    EXPECT_TRUE(status_lines_contain_exact(status, "Lobby: 1 player"));

    const std::string create_uri = relay_server.last_create_uri();
    EXPECT_NE(std::string::npos, create_uri.find("/api/create?campaign="));
    EXPECT_NE(std::string::npos, create_uri.find("%20"));
    EXPECT_NE(std::string::npos, create_uri.find("%2F"));

    host_client->shutdown();
}

TEST(PickerNetworkClient, host_status_builder_reports_direct_and_relay_errors)
{
    const auto lines = og::ui::build_host_picker_status_lines(
        "192.168.1.5",
        false,
        12345,
        "bind failed",
        "",
        "room service down",
        2u);

    EXPECT_TRUE(status_lines_contain_exact(lines, "Direct: bind failed"));
    EXPECT_TRUE(status_lines_contain_exact(lines, "Relay: room service down"));
    EXPECT_TRUE(status_lines_contain_exact(lines, "Lobby: 2 players"));
}

TEST(PickerNetworkClient, host_sync_roster_ignores_remote_owned_slots)
{
    SaveData save;
    prepare_single_member_network_save(save, 0, "Host");
    auto remote_member = std::make_unique<guy>(FAMILY_SOLDIER);
    remote_member->name = "Remote Joiner";
    remote_member->teamnum = 1;
    save.team_list[1] = std::move(remote_member);
    save.team_size = 2;

    og::sim::LobbyState state;
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "host-player",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Host", 0)},
        .ready = false,
        .is_host = true,
    });
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 1u,
        .name = "remote-player",
        .team = 1,
        .character_slots = {make_lobby_slot(0u, "Remote Joiner", 1)},
        .ready = false,
        .is_host = false,
    });

    const auto excluded_slots =
        og::ui::detail::build_remote_owned_slot_mask(state, "host-player");
    EXPECT_FALSE(excluded_slots[0]);
    EXPECT_TRUE(excluded_slots[1]);

    save.team_list[0]->name = "Host Prime";
    save.team_list[1]->teamnum = 0;
    save.team_list[1]->name = "Stolen";

    const og::sim::LobbyMessage message = og::ui::detail::make_join_message(
        save,
        "host-player",
        0,
        &excluded_slots);

    ASSERT_EQ(og::sim::LobbyMessageKind::Join, message.kind());
    const auto& join = std::get<og::sim::LobbyJoinMessage>(message.payload);
    EXPECT_EQ("host-player", join.player.name);
    EXPECT_EQ(0, join.player.team);
    ASSERT_EQ(1u, join.player.character_slots.size());
    EXPECT_EQ(0u, join.player.character_slots[0].slot_index);
    EXPECT_EQ("Host Prime", join.player.character_slots[0].character.name);
}

TEST(PickerNetworkClient, join_direct_flow_receives_remote_host_start_and_syncs_roster)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 1, "Joiner");
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    auto server_transport =
        std::make_shared<og::sim::WebSocketServerTransport>(port);
    server_transport->accept_connections();
    SaveData remote_host_save;
    prepare_single_member_network_save(remote_host_save, 0, "Remote Host");
    remote_host_save.scen_num = 5;
    remote_host_save.allied_mode = 1;

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Direct;
    options.direct_endpoint = std::format("127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    ASSERT_NE(nullptr, active_game_session());
    ASSERT_NE(nullptr, active_game_session()->myscreen_);
    EXPECT_FALSE(install_gameplay_runtime_from_handoff(*join_client));
    join_client->initialize_from_save();

    og::sim::PeerId join_peer_id = 0;
    og::sim::LobbyMessage join_message;
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (message.kind() != og::sim::LobbyMessageKind::Join)
                continue;

            join_peer_id = peer_id;
            join_message = std::move(message);
            return true;
        }
        return false;
    })) << "direct join client should connect and send its join message";

    ASSERT_EQ(og::sim::LobbyMessageKind::Join, join_message.kind());
    og::sim::LobbyPlayer join_player =
        std::get<og::sim::LobbyJoinMessage>(join_message.payload).player;
    join_player.player_index = 1u;
    join_player.ready = false;
    join_player.is_host = false;

    og::sim::LobbyState state;
    state.settings.campaign_id = remote_host_save.current_campaign;
    state.settings.scenario_id = remote_host_save.scen_num;
    state.settings.difficulty = 7;
    state.settings.allied_mode = remote_host_save.allied_mode;
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "Remote Host",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Remote Host", 0)},
        .ready = false,
        .is_host = true,
    });
    state.players.push_back(join_player);
    server_transport->send_lobby_state(
        join_peer_id, std::make_shared<og::sim::LobbyState>(state));

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return status_lines_contain_exact(
                   join_client->status_lines(),
                   "Lobby: 2 players");
    })) << "direct join client should receive injected lobby state";

    EXPECT_FALSE(join_client->host_controls_visible());
    EXPECT_TRUE(status_lines_contain_prefix(
        join_client->status_lines(),
        std::format("Direct: ws://127.0.0.1:{}", port)));
    EXPECT_TRUE(
        status_lines_contain_exact(join_client->status_lines(), "Status: connected"));

    std::size_t local_slot_index = save.team_list.size();
    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        const auto& member = save.team_list[slot_index];
        if (member != nullptr &&
            member->name == "Joiner" &&
            join_client->is_save_slot_editable(slot_index))
        {
            local_slot_index = slot_index;
            break;
        }
    }
    ASSERT_LT(local_slot_index, save.team_list.size());
    save.team_list[local_slot_index]->name = "Joiner Prime";
    join_client->sync_from_save();
    og::sim::LobbyMessage sync_message;
    join_client->sync_settings_from_save();
    join_client->set_player_mode(0);
    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        for (auto& [peer_id, message] : poll_lobby_messages(*server_transport))
        {
            if (peer_id != join_peer_id ||
                message.kind() != og::sim::LobbyMessageKind::Join)
            {
                continue;
            }

            sync_message = std::move(message);
            return true;
        }
        return false;
    })) << "direct join client should re-send its join roster after local save changes";

    const auto& synced_join =
        std::get<og::sim::LobbyJoinMessage>(sync_message.payload);
    ASSERT_EQ(1u, synced_join.player.character_slots.size());
    EXPECT_EQ("Joiner Prime",
              synced_join.player.character_slots.front().character.name);

    join_player = synced_join.player;
    join_player.player_index = 1u;
    join_player.ready = false;
    join_player.is_host = false;
    state.players[1] = join_player;
    server_transport->send_lobby_state(
        join_peer_id, std::make_shared<og::sim::LobbyState>(state));
    send_start_game_message(*server_transport, join_peer_id, 0u);

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return join_client->has_game_start_config();
    })) << "direct join client should receive the host start-game handoff";

    EXPECT_FALSE(join_client->start_request_pending());
    ASSERT_TRUE(join_client->build_game_start_config().has_value());
    const auto start_config = join_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ("org.openglad.gladiator", start_config->save_data.current_campaign);
    EXPECT_EQ(5, start_config->save_data.scen_num);
    EXPECT_EQ(0u, start_config->save_data.numplayers);
    EXPECT_EQ(7, start_config->difficulty);
    ASSERT_EQ(2u, start_config->save_data.team_list.size());

    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*join_client));
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));
    og::runtime::clear_local_transport_shadow(*active_game_session());

    join_client->poll_and_apply();
    EXPECT_TRUE(
        status_lines_contain_exact(join_client->status_lines(), "Status: connecting"));

    join_client->shutdown();
}

TEST(PickerNetworkClient, join_sync_roster_ignores_remote_owned_slots)
{
    SaveData save;
    prepare_single_member_network_save(save, 1, "Joiner", 1);
    auto remote_member = std::make_unique<guy>(FAMILY_SOLDIER);
    remote_member->name = "Remote Host";
    remote_member->teamnum = 0;
    save.team_list[0] = std::move(remote_member);
    save.team_size = 2;
    save.my_team = 1;

    og::sim::LobbyState state;
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 0u,
        .name = "host-player",
        .team = 0,
        .character_slots = {make_lobby_slot(0u, "Remote Host", 0)},
        .ready = false,
        .is_host = true,
    });
    state.players.push_back(og::sim::LobbyPlayer{
        .player_index = 1u,
        .name = "join-player",
        .team = 1,
        .character_slots = {make_lobby_slot(0u, "Joiner", 1)},
        .ready = false,
        .is_host = false,
    });

    const auto excluded_slots =
        og::ui::detail::build_remote_owned_slot_mask(state, "join-player");
    EXPECT_TRUE(excluded_slots[0]);
    EXPECT_FALSE(excluded_slots[1]);

    save.team_list[0]->teamnum = 1;
    save.team_list[0]->name = "Stolen";
    save.team_list[1]->name = "Joiner Prime";

    const og::sim::LobbyMessage message = og::ui::detail::make_join_message(
        save,
        "join-player",
        1,
        &excluded_slots);

    ASSERT_EQ(og::sim::LobbyMessageKind::Join, message.kind());
    const auto& join = std::get<og::sim::LobbyJoinMessage>(message.payload);
    EXPECT_EQ("join-player", join.player.name);
    EXPECT_EQ(1, join.player.team);
    ASSERT_EQ(1u, join.player.character_slots.size());
    EXPECT_EQ(1u, join.player.character_slots[0].slot_index);
    EXPECT_EQ("Joiner Prime", join.player.character_slots[0].character.name);
}

TEST(PickerNetworkClient, join_relay_flow_connects_and_starts_game)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Relay Joiner");
    g_start_game_requested = false;

    const int port = ix::getFreePort();
    FakeRelayServer relay_server(port);

    og::sim::RelayWebSocketTransport relay_host_transport(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-XKCD", port));
    relay_host_transport.accept_connections();
    ASSERT_TRUE(wait_until_host_owns_room(relay_host_transport));

    og::sim::LobbyServer lobby_server(relay_host_transport);

    og::ui::PickerJoinGameOptions options;
    options.mode = og::ui::PickerJoinMode::Relay;
    options.room_code = "glad-xkcd";
    options.relay_base_url = std::format("http://127.0.0.1:{}", port);
    auto join_client = og::ui::create_join_picker_lobby_client(options);
    join_client->initialize_from_save();

    ASSERT_TRUE(wait_until([&] {
        lobby_server.poll_incoming_messages();
        (void)relay_host_transport.poll();
        join_client->poll_and_apply();
        return lobby_server.state().players.size() == 1u &&
            status_lines_contain_exact(
                join_client->status_lines(),
                "Relay: GLAD-XKCD") &&
            status_lines_contain_exact(
                join_client->status_lines(),
                "Status: connected");
    })) << "relay join client should connect and become the first lobby player";

    ASSERT_TRUE(wait_until([&] {
        join_client->poll_and_apply();
        return join_client->host_controls_visible();
    })) << "relay join client should become the visible host once the lobby state arrives";
    join_client->sync_roster_from_save();
    join_client->set_player_mode(0);

    ASSERT_TRUE(wait_until([&] {
        (void)join_client->request_start_game();
        lobby_server.poll_incoming_messages();
        (void)relay_host_transport.poll();
        join_client->poll_and_apply();
        return join_client->has_game_start_config();
    },
    10s)) << "relay join client should receive the start-game handoff";

    EXPECT_FALSE(join_client->start_request_pending());
    ASSERT_TRUE(join_client->build_game_start_config().has_value());
    const auto start_config = join_client->consume_game_start_config();
    ASSERT_TRUE(start_config.has_value());
    EXPECT_EQ(1, start_config->save_data.scen_num);
    EXPECT_EQ(0u, start_config->save_data.numplayers);
    EXPECT_LE(start_config->save_data.team_list.size(), 1u);
    EXPECT_EQ(1, start_config->difficulty);

    ASSERT_NE(nullptr, active_game_session());
    ASSERT_TRUE(install_gameplay_runtime_from_handoff(*join_client));
    EXPECT_TRUE(og::runtime::local_transport_active(*active_game_session()));
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(*active_game_session()));
    og::runtime::clear_local_transport_shadow(*active_game_session());

    join_client->poll_and_apply();
    EXPECT_TRUE(
        status_lines_contain_exact(join_client->status_lines(), "Status: connecting"));

    join_client->shutdown();
}

TEST(PickerNetworkClient, host_initialization_reports_direct_and_relay_failures)
{
    IxNetSystemScope net_system;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    g_start_game_requested = false;

    const int blocked_port = ix::getFreePort();
    auto blocking_server =
        std::make_shared<og::sim::WebSocketServerTransport>(blocked_port);
    blocking_server->accept_connections();

    const int relay_port = ix::getFreePort();
    FakeRelayServer relay_server(relay_port, 503, "room service down");
    og::ui::PickerHostGameOptions options;
    options.port = blocked_port;
    options.enable_relay = true;
    options.relay_base_url = std::format("http://127.0.0.1:{}", relay_port);
    auto host_client = og::ui::create_host_picker_lobby_client(options);

    try
    {
        host_client->initialize_from_save();
        FAIL() << "host initialize should fail when both direct and relay transports fail";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_FALSE(message.empty());
    }
}

TEST(PickerNetworkClient, host_initialization_reports_direct_only_failures)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    PickerSaveStateGuard save_guard(save);
    PickerRuntimeGuard runtime_guard;
    prepare_single_member_network_save(save, 0, "Host");
    g_start_game_requested = false;

    const int blocked_port = ix::getFreePort();
    auto blocking_server =
        std::make_shared<og::sim::WebSocketServerTransport>(blocked_port);
    blocking_server->accept_connections();

    og::ui::PickerHostGameOptions options;
    options.port = blocked_port;
    auto host_client = og::ui::create_host_picker_lobby_client(options);

    try
    {
        host_client->initialize_from_save();
        FAIL() << "host initialize should fail when the direct listener cannot bind";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_FALSE(message.empty());
        EXPECT_EQ(std::string::npos, message.find("Relay:"));
    }
}

TEST(PickerNetworkClient, validation_helpers_reject_invalid_network_picker_inputs)
{
    og::ui::PickerHostGameOptions bad_host;
    bad_host.port = 70000;
    EXPECT_THROW(og::ui::create_host_picker_lobby_client(bad_host),
                 std::invalid_argument);

    og::ui::PickerJoinGameOptions direct_options;
    direct_options.mode = og::ui::PickerJoinMode::Direct;
    EXPECT_THROW(og::ui::create_join_picker_lobby_client(direct_options),
                 std::invalid_argument);

    og::ui::PickerJoinGameOptions relay_options;
    relay_options.mode = og::ui::PickerJoinMode::Relay;
    EXPECT_THROW(og::ui::create_join_picker_lobby_client(relay_options),
                 std::invalid_argument);

    EXPECT_FALSE(og::ui::picker_join_mode_supported(
        static_cast<og::ui::PickerJoinMode>(99)));
    EXPECT_EQ("ws://127.0.0.1:12345",
              og::ui::normalize_direct_websocket_url("127.0.0.1:12345"));
    EXPECT_THROW(og::ui::normalize_direct_websocket_url("ftp://relay.invalid"),
                 std::invalid_argument);
    EXPECT_EQ("RELAY-42", og::ui::normalize_relay_room_code(" relay-42 "));
    EXPECT_THROW(og::ui::normalize_relay_room_code("bad room"),
                 std::invalid_argument);
    EXPECT_EQ("https://relay.invalid",
              og::ui::normalize_relay_base_url(" https://relay.invalid/// "));
    EXPECT_THROW(og::ui::normalize_relay_base_url("ftp://relay.invalid"),
                 std::invalid_argument);
}
