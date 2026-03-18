#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>

#include <gtest/gtest.h>

#include <ixwebsocket/IXGetFreePort.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

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

std::vector<og::sim::ReceivedMessage> poll_until_messages(
    og::sim::ITransport& transport,
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

bool poll_until_peer_count(og::sim::ITransport& transport,
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

template <typename Predicate>
std::optional<og::sim::ReceivedMessage> poll_until_matching_message(
    og::sim::ITransport& transport,
    Predicate&& predicate,
    std::chrono::milliseconds timeout = 5s)
{
    std::optional<og::sim::ReceivedMessage> matched_message;
    const bool ok = wait_until(
        [&] {
            std::vector<og::sim::ReceivedMessage> polled = transport.poll();
            for (auto& message : polled)
            {
                if (!predicate(message))
                    continue;

                matched_message = std::move(message);
                return true;
            }
            return false;
        },
        timeout);
    EXPECT_TRUE(ok);
    return matched_message;
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

TEST(NetTransportWebSocketClient,
     poll_defers_peer_state_until_game_thread_and_roundtrips_binary_frames)
{
    const int port = ix::getFreePort();

    og::sim::WebSocketServerTransport::Options server_options;
    server_options.host = "127.0.0.1";
    og::sim::WebSocketServerTransport server(port, server_options);
    server.accept_connections();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 42u;
    client_options.automatic_reconnection = false;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", port),
        client_options);
    client.accept_connections();

    EXPECT_TRUE(client.connected_peers().empty());
    EXPECT_TRUE(client.poll().empty());

    ASSERT_TRUE(poll_until_peer_count(client, 1u));
    ASSERT_TRUE(poll_until_peer_count(server, 1u));

    EXPECT_EQ((std::vector<og::sim::PeerId>{client_options.remote_peer_id}),
              client.connected_peers());

    const std::vector<og::sim::PeerId> server_peers = server.connected_peers();
    ASSERT_EQ(1u, server_peers.size());
    const og::sim::PeerId server_peer_id = server_peers.front();

    client.send_client_ready(
        client_options.remote_peer_id,
        std::make_shared<og::sim::ClientReadyMessage>(
            og::sim::ClientReadyMessage{.last_applied_tick = 17u}));

    const auto received_messages = poll_until_messages(server, 1u);
    ASSERT_EQ(1u, received_messages.size());
    EXPECT_EQ(server_peer_id, received_messages.front().peer_id);
    EXPECT_EQ(17u, decode_client_ready_tick(received_messages.front().data));

    server.send_keyframe_request(
        server_peer_id,
        std::make_shared<og::sim::KeyframeRequestMessage>(
            og::sim::KeyframeRequestMessage{.last_seen_tick = 33u}));

    const auto client_messages = poll_until_messages(client, 1u);
    ASSERT_EQ(1u, client_messages.size());
    EXPECT_EQ(client_options.remote_peer_id, client_messages.front().peer_id);
    EXPECT_EQ(33u, decode_keyframe_request_tick(client_messages.front().data));

    client.disconnect(client_options.remote_peer_id);
    ASSERT_TRUE(poll_until_peer_count(client, 0u));
    ASSERT_TRUE(poll_until_peer_count(server, 0u));
}

TEST(NetTransportWebSocketClient,
     preserves_message_order_under_concurrent_bidirectional_traffic)
{
    const int port = ix::getFreePort();
    constexpr std::uint32_t kMessageCount = 32u;

    og::sim::WebSocketServerTransport::Options server_options;
    server_options.host = "127.0.0.1";
    og::sim::WebSocketServerTransport server(port, server_options);
    server.accept_connections();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 7u;
    client_options.automatic_reconnection = false;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", port),
        client_options);
    client.accept_connections();

    ASSERT_TRUE(poll_until_peer_count(client, 1u));
    ASSERT_TRUE(poll_until_peer_count(server, 1u));

    const og::sim::PeerId server_peer_id = server.connected_peers().front();

    std::thread client_sender([&] {
        for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
        {
            client.send_client_ready(
                client_options.remote_peer_id,
                std::make_shared<og::sim::ClientReadyMessage>(
                    og::sim::ClientReadyMessage{.last_applied_tick = sequence}));
        }
    });

    std::vector<og::sim::ReceivedMessage> server_messages;
    std::vector<og::sim::ReceivedMessage> client_messages;
    server_messages.reserve(kMessageCount);
    client_messages.reserve(kMessageCount);

    for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
    {
        server.send_keyframe_request(
            server_peer_id,
            std::make_shared<og::sim::KeyframeRequestMessage>(
                og::sim::KeyframeRequestMessage{.last_seen_tick = sequence}));

        std::vector<og::sim::ReceivedMessage> polled_server = server.poll();
        server_messages.insert(server_messages.end(),
                               std::make_move_iterator(polled_server.begin()),
                               std::make_move_iterator(polled_server.end()));

        std::vector<og::sim::ReceivedMessage> polled_client = client.poll();
        client_messages.insert(client_messages.end(),
                               std::make_move_iterator(polled_client.begin()),
                               std::make_move_iterator(polled_client.end()));
    }

    client_sender.join();

    ASSERT_TRUE(wait_until(
        [&] {
            std::vector<og::sim::ReceivedMessage> polled_server = server.poll();
            server_messages.insert(server_messages.end(),
                                   std::make_move_iterator(polled_server.begin()),
                                   std::make_move_iterator(polled_server.end()));

            std::vector<og::sim::ReceivedMessage> polled_client = client.poll();
            client_messages.insert(client_messages.end(),
                                   std::make_move_iterator(polled_client.begin()),
                                   std::make_move_iterator(polled_client.end()));
            return server_messages.size() >= kMessageCount &&
                client_messages.size() >= kMessageCount;
        },
        5s));

    ASSERT_EQ(kMessageCount, server_messages.size());
    for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
    {
        EXPECT_EQ(server_peer_id,
                  server_messages[static_cast<std::size_t>(sequence)].peer_id);
        EXPECT_EQ(sequence,
                  decode_client_ready_tick(
                      server_messages[static_cast<std::size_t>(sequence)].data));
    }

    ASSERT_EQ(kMessageCount, client_messages.size());
    for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
    {
        EXPECT_EQ(client_options.remote_peer_id,
                  client_messages[static_cast<std::size_t>(sequence)].peer_id);
        EXPECT_EQ(sequence,
                  decode_keyframe_request_tick(
                      client_messages[static_cast<std::size_t>(sequence)].data));
    }

    client.disconnect(client_options.remote_peer_id);
    ASSERT_TRUE(poll_until_peer_count(server, 0u));
}

TEST(NetTransportWebSocketClient,
     automatically_reconnects_after_server_disconnect_with_configured_backoff)
{
    const int port = ix::getFreePort();

    og::sim::WebSocketServerTransport::Options server_options;
    server_options.host = "127.0.0.1";
    og::sim::WebSocketServerTransport server(port, server_options);
    server.accept_connections();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 9u;
    client_options.automatic_reconnection = true;
    client_options.min_reconnect_wait_ms = 1u;
    client_options.max_reconnect_wait_ms = 20u;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", port),
        client_options);
    client.accept_connections();

    ASSERT_TRUE(poll_until_peer_count(client, 1u));
    ASSERT_TRUE(poll_until_peer_count(server, 1u));

    const og::sim::PeerId first_server_peer_id = server.connected_peers().front();
    server.disconnect(first_server_peer_id);

    EXPECT_NO_THROW(
        client.send_client_ready(
            client_options.remote_peer_id,
            std::make_shared<og::sim::ClientReadyMessage>(
                og::sim::ClientReadyMessage{.last_applied_tick = 55u})));

    og::sim::PeerId reconnected_server_peer_id = 0;
    ASSERT_TRUE(wait_until(
        [&] {
            (void)client.poll();
            (void)server.poll();
            const std::vector<og::sim::PeerId> peers = server.connected_peers();
            if (peers.size() != 1u || peers.front() == first_server_peer_id)
                return false;
            reconnected_server_peer_id = peers.front();
            return true;
        },
        5s));

    ASSERT_TRUE(poll_until_peer_count(client, 1u));
    EXPECT_EQ((std::vector<og::sim::PeerId>{client_options.remote_peer_id}),
              client.connected_peers());

    client.send_client_ready(
        client_options.remote_peer_id,
        std::make_shared<og::sim::ClientReadyMessage>(
            og::sim::ClientReadyMessage{.last_applied_tick = 88u}));
    const auto server_message = poll_until_matching_message(
        server,
        [reconnected_server_peer_id](const og::sim::ReceivedMessage& message) {
            return message.peer_id == reconnected_server_peer_id &&
                decode_client_ready_tick(message.data) == 88u;
        });
    ASSERT_TRUE(server_message.has_value());

    server.send_keyframe_request(
        reconnected_server_peer_id,
        std::make_shared<og::sim::KeyframeRequestMessage>(
            og::sim::KeyframeRequestMessage{.last_seen_tick = 77u}));
    const auto client_message = poll_until_matching_message(
        client,
        [remote_peer_id = client_options.remote_peer_id](
            const og::sim::ReceivedMessage& message) {
            return message.peer_id == remote_peer_id &&
                decode_keyframe_request_tick(message.data) == 77u;
        });
    ASSERT_TRUE(client_message.has_value());

    client.disconnect(client_options.remote_peer_id);
    ASSERT_TRUE(poll_until_peer_count(server, 0u));
}

} // namespace
