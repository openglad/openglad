#include <openglad/platform/net_transport_relay_ws.h>

#include <gtest/gtest.h>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

class FakeRelayServer
{
public:
    explicit FakeRelayServer(int port)
        : server_(port, "127.0.0.1", 5, 16)
    {
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
        {
            send_text(other_socket, peer_joined_text);
        }
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

    ix::WebSocketServer server_;
    std::mutex mutex_;
    og::sim::PeerId next_peer_id_ = 1;
    std::optional<og::sim::PeerId> host_peer_id_;
    std::map<std::string, PeerState> peers_;
};

template <typename Predicate>
bool wait_until(Predicate&& predicate,
                std::chrono::milliseconds timeout = 5s)
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

bool poll_until_peer_count(og::sim::RelayWebSocketTransport& transport,
                           std::size_t expected_count,
                           std::chrono::milliseconds timeout = 5s)
{
    return wait_until(
        [&] {
            (void)transport.poll();
            return transport.connected_peers().size() == expected_count;
        },
        timeout);
}

bool wait_until_host_owns_room(og::sim::RelayWebSocketTransport& transport,
                               std::chrono::milliseconds timeout = 5s)
{
    return wait_until(
        [&] {
            (void)transport.poll();
            return transport.local_peer_id().has_value() &&
                transport.host_peer_id() == transport.local_peer_id();
        },
        timeout);
}

std::vector<og::sim::ReceivedMessage> poll_until_messages(
    og::sim::RelayWebSocketTransport& transport,
    std::size_t expected_count,
    std::chrono::milliseconds timeout = 5s)
{
    std::vector<og::sim::ReceivedMessage> messages;
    const bool ok = wait_until(
        [&] {
            std::vector<og::sim::ReceivedMessage> polled = transport.poll();
            messages.insert(messages.end(),
                            std::make_move_iterator(polled.begin()),
                            std::make_move_iterator(polled.end()));
            return messages.size() >= expected_count;
        },
        timeout);
    EXPECT_TRUE(ok);
    return messages;
}

std::uint32_t decode_client_ready_tick(std::span<const std::uint8_t> bytes)
{
    const auto decoded = og::sim::deserialize_client_ready_message(bytes);
    EXPECT_TRUE(decoded.has_value());
    return decoded ? decoded->last_applied_tick : 0u;
}

std::uint32_t decode_keyframe_request_tick(std::span<const std::uint8_t> bytes)
{
    const auto decoded = og::sim::deserialize_keyframe_request_message(bytes);
    EXPECT_TRUE(decoded.has_value());
    return decoded ? decoded->last_seen_tick : 0u;
}

TEST(NetTransportRelayWs,
     relay_targeted_frames_roundtrip_with_distinct_peer_ids)
{
    IxNetSystemScope net_system;
    const int port = ix::getFreePort();
    FakeRelayServer server(port);

    og::sim::RelayWebSocketTransport host(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-TEST", port));
    og::sim::RelayWebSocketTransport client(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-TEST", port));
    host.accept_connections();

    ASSERT_TRUE(wait_until_host_owns_room(host));
    ASSERT_EQ(std::optional<og::sim::PeerId>(1u), host.local_peer_id());
    ASSERT_EQ(host.local_peer_id(), host.host_peer_id());

    client.accept_connections();

    ASSERT_TRUE(poll_until_peer_count(host, 1u));
    ASSERT_TRUE(poll_until_peer_count(client, 1u));

    const og::sim::PeerId host_peer_id = *host.local_peer_id();
    const og::sim::PeerId client_peer_id = *client.local_peer_id();

    EXPECT_EQ(std::optional<og::sim::PeerId>(2u), client.local_peer_id());
    EXPECT_EQ(host.local_peer_id(), client.host_peer_id());
    EXPECT_EQ((std::vector<og::sim::PeerId>{client_peer_id}), host.connected_peers());
    EXPECT_EQ((std::vector<og::sim::PeerId>{host_peer_id}), client.connected_peers());

    client.send_client_ready(
        host_peer_id,
        std::make_shared<og::sim::ClientReadyMessage>(
            og::sim::ClientReadyMessage{.last_applied_tick = 17u}));

    const auto host_messages = poll_until_messages(host, 1u);
    ASSERT_EQ(1u, host_messages.size());
    EXPECT_EQ(2u, host_messages.front().peer_id);
    EXPECT_EQ(17u, decode_client_ready_tick(host_messages.front().data));

    host.send_keyframe_request(
        client_peer_id,
        std::make_shared<og::sim::KeyframeRequestMessage>(
            og::sim::KeyframeRequestMessage{.last_seen_tick = 33u}));

    const auto client_messages = poll_until_messages(client, 1u);
    ASSERT_EQ(1u, client_messages.size());
    EXPECT_EQ(1u, client_messages.front().peer_id);
    EXPECT_EQ(33u, decode_keyframe_request_tick(client_messages.front().data));
}

TEST(NetTransportRelayWs, host_disconnect_reassigns_host_and_updates_peer_sets)
{
    IxNetSystemScope net_system;
    const int port = ix::getFreePort();
    FakeRelayServer server(port);

    auto host = std::make_unique<og::sim::RelayWebSocketTransport>(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-TEST", port));
    og::sim::RelayWebSocketTransport client_one(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-TEST", port));
    og::sim::RelayWebSocketTransport client_two(
        std::format("ws://127.0.0.1:{}/api/room/GLAD-TEST", port));
    host->accept_connections();

    ASSERT_TRUE(wait_until_host_owns_room(*host));
    ASSERT_EQ(std::optional<og::sim::PeerId>(1u), host->local_peer_id());

    client_one.accept_connections();
    ASSERT_TRUE(wait_until(
        [&] {
            (void)host->poll();
            (void)client_one.poll();
            return host->connected_peers().size() == 1u &&
                client_one.connected_peers().size() == 1u;
        },
        5s));

    client_two.accept_connections();

    ASSERT_TRUE(wait_until(
        [&] {
            (void)host->poll();
            (void)client_one.poll();
            (void)client_two.poll();
            return host->connected_peers().size() == 2u &&
                client_one.connected_peers().size() == 2u &&
                client_two.connected_peers().size() == 2u;
        },
        5s));

    const og::sim::PeerId client_one_peer_id = *client_one.local_peer_id();
    const og::sim::PeerId client_two_peer_id = *client_two.local_peer_id();

    host.reset();

    ASSERT_TRUE(wait_until(
        [&] {
            (void)client_one.poll();
            (void)client_two.poll();
            return client_one.connected_peers().size() == 1u &&
                client_two.connected_peers().size() == 1u &&
                client_one.host_peer_id() == std::optional<og::sim::PeerId>(client_one_peer_id) &&
                client_two.host_peer_id() == std::optional<og::sim::PeerId>(client_one_peer_id);
        },
        5s));

    EXPECT_EQ(std::optional<og::sim::PeerId>(2u), client_one.local_peer_id());
    EXPECT_EQ(std::optional<og::sim::PeerId>(3u), client_two.local_peer_id());
    EXPECT_EQ((std::vector<og::sim::PeerId>{client_two_peer_id}), client_one.connected_peers());
    EXPECT_EQ((std::vector<og::sim::PeerId>{client_one_peer_id}), client_two.connected_peers());
}

} // namespace
