#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/platform/net_transport_websocket_server.h>

#include <gtest/gtest.h>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../test_network_fixture.h"

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

class WebSocketClientProbe
{
public:
    explicit WebSocketClientProbe(std::string url)
        : url_(std::move(url))
    {
        websocket_.disableAutomaticReconnection();
        websocket_.setUrl(url_);
        websocket_.setOnMessageCallback(
            [this](const ix::WebSocketMessagePtr& message) {
                handle_message(message);
            });
    }

    ~WebSocketClientProbe()
    {
        stop();
    }

    void start()
    {
        websocket_.start();
        started_ = true;
    }

    void stop()
    {
        if (!started_)
            return;

        websocket_.stop();
        started_ = false;
    }

    bool wait_until_open(std::chrono::milliseconds timeout = 5s)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] {
            return open_ || !error_.empty();
        }) && open_;
    }

    bool wait_for_binary_message_count(std::size_t expected_count,
                                       std::chrono::milliseconds timeout = 5s)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this, expected_count] {
            return binary_messages_.size() >= expected_count || !error_.empty();
        }) && binary_messages_.size() >= expected_count;
    }

    std::vector<std::vector<std::uint8_t>> binary_messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return binary_messages_;
    }

    std::string error() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }

    bool send_binary(std::span<const std::uint8_t> bytes)
    {
        const char* raw_bytes = reinterpret_cast<const char*>(bytes.data());
        const std::string payload =
            (raw_bytes == nullptr || bytes.empty())
                ? std::string()
                : std::string(raw_bytes, raw_bytes + bytes.size());
        const ix::WebSocketSendInfo send_info = websocket_.sendBinary(payload);
        if (send_info.success)
            return true;

        std::lock_guard<std::mutex> lock(mutex_);
        if (error_.empty())
            error_ = "failed to send websocket binary payload";
        condition_.notify_all();
        return false;
    }

private:
    void handle_message(const ix::WebSocketMessagePtr& message)
    {
        if (!message)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
            open_ = true;
            break;

        case ix::WebSocketMessageType::Message:
            if (message->binary)
            {
                binary_messages_.emplace_back(message->str.begin(),
                                              message->str.end());
            }
            break;

        case ix::WebSocketMessageType::Error:
            error_ = message->errorInfo.reason;
            break;

        default:
            break;
        }

        condition_.notify_all();
    }

    std::string url_;
    ix::WebSocket websocket_;
    bool started_ = false;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool open_ = false;
    std::string error_;
    std::vector<std::vector<std::uint8_t>> binary_messages_;
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

std::vector<og::sim::ReceivedMessage> poll_until_messages(
    og::sim::WebSocketServerTransport& transport,
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

bool poll_until_peer_count(og::sim::WebSocketServerTransport& transport,
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

TEST(NetTransportWebSocketServer,
     poll_defers_peer_state_until_game_thread_and_roundtrips_binary_frames)
{
    IxNetSystemScope net_system;
    const int port = ix::getFreePort();
    og::sim::WebSocketServerTransport::Options options;
    options.host = "127.0.0.1";

    og::sim::WebSocketServerTransport transport(port, options);
    transport.accept_connections();

    WebSocketClientProbe client(std::format("ws://127.0.0.1:{}", port));
    client.start();
    ASSERT_TRUE(client.wait_until_open()) << client.error();

    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_TRUE(transport.poll().empty());

    ASSERT_TRUE(poll_until_peer_count(transport, 1u));
    const std::vector<og::sim::PeerId> peers = transport.connected_peers();
    ASSERT_EQ(1u, peers.size());
    const og::sim::PeerId peer_id = peers.front();

    const std::vector<std::uint8_t> client_ready =
        og::sim::serialize_client_ready_message(
            og::sim::ClientReadyMessage{.last_applied_tick = 17u});
    ASSERT_TRUE(client.send_binary(client_ready)) << client.error();

    const auto received_messages = poll_until_messages(transport, 1u);
    ASSERT_EQ(1u, received_messages.size());
    EXPECT_EQ(peer_id, received_messages.front().peer_id);
    EXPECT_EQ(17u, decode_client_ready_tick(received_messages.front().data));

    transport.send_keyframe_request(
        peer_id,
        std::make_shared<og::sim::KeyframeRequestMessage>(
            og::sim::KeyframeRequestMessage{.last_seen_tick = 33u}));

    ASSERT_TRUE(client.wait_for_binary_message_count(1u)) << client.error();
    const auto client_messages = client.binary_messages();
    ASSERT_EQ(1u, client_messages.size());
    EXPECT_EQ(33u, decode_keyframe_request_tick(client_messages.front()));

    client.stop();
    EXPECT_EQ((std::vector<og::sim::PeerId>{peer_id}), transport.connected_peers());
    ASSERT_TRUE(poll_until_peer_count(transport, 0u));
    EXPECT_TRUE(transport.connected_peers().empty());
}

TEST(NetTransportWebSocketServer,
     preserves_per_peer_message_order_under_concurrent_bidirectional_traffic)
{
    IxNetSystemScope net_system;
    const int port = ix::getFreePort();
    constexpr std::size_t kClientCount = 4;
    constexpr std::uint32_t kMessageCount = 24;
    og::sim::WebSocketServerTransport::Options options;
    options.host = "127.0.0.1";

    og::sim::WebSocketServerTransport transport(port, options);
    transport.accept_connections();

    std::vector<std::unique_ptr<WebSocketClientProbe>> clients;
    clients.reserve(kClientCount);
    for (std::size_t index = 0; index < kClientCount; ++index)
    {
        auto client = std::make_unique<WebSocketClientProbe>(
            std::format("ws://127.0.0.1:{}", port));
        client->start();
        ASSERT_TRUE(client->wait_until_open()) << client->error();
        clients.push_back(std::move(client));
    }

    ASSERT_TRUE(poll_until_peer_count(transport, kClientCount));
    const std::vector<og::sim::PeerId> peers = transport.connected_peers();
    ASSERT_EQ(kClientCount, peers.size());

    std::vector<std::thread> sender_threads;
    sender_threads.reserve(kClientCount);
    for (std::size_t client_index = 0; client_index < clients.size(); ++client_index)
    {
        sender_threads.emplace_back([&, client_index] {
            for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
            {
                const auto bytes = og::sim::serialize_client_ready_message(
                    og::sim::ClientReadyMessage{.last_applied_tick = sequence});
                if (!clients[client_index]->send_binary(bytes))
                    return;
            }
        });
    }

    std::vector<og::sim::ReceivedMessage> inbound_messages;
    inbound_messages.reserve(kClientCount * kMessageCount);
    for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
    {
        for (const og::sim::PeerId peer_id : peers)
        {
            transport.send_keyframe_request(
                peer_id,
                std::make_shared<og::sim::KeyframeRequestMessage>(
                    og::sim::KeyframeRequestMessage{.last_seen_tick = sequence}));
        }

        std::vector<og::sim::ReceivedMessage> polled = transport.poll();
        inbound_messages.insert(inbound_messages.end(),
                                std::make_move_iterator(polled.begin()),
                                std::make_move_iterator(polled.end()));
    }

    for (auto& sender : sender_threads)
        sender.join();

    ASSERT_TRUE(wait_until(
        [&] {
            std::vector<og::sim::ReceivedMessage> polled = transport.poll();
            inbound_messages.insert(inbound_messages.end(),
                                    std::make_move_iterator(polled.begin()),
                                    std::make_move_iterator(polled.end()));
            return inbound_messages.size() >= kClientCount * kMessageCount;
        },
        5s));

    std::vector<std::vector<std::uint32_t>> inbound_sequences(peers.size());
    for (const auto& message : inbound_messages)
    {
        const auto peer_it =
            std::find(peers.begin(), peers.end(), message.peer_id);
        ASSERT_NE(peers.end(), peer_it);
        const std::size_t peer_index =
            static_cast<std::size_t>(std::distance(peers.begin(), peer_it));
        inbound_sequences[peer_index].push_back(
            decode_client_ready_tick(message.data));
    }

    for (const auto& sequences : inbound_sequences)
    {
        ASSERT_EQ(kMessageCount, sequences.size());
        for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
            EXPECT_EQ(sequence, sequences[static_cast<std::size_t>(sequence)]);
    }

    for (const auto& client : clients)
    {
        ASSERT_TRUE(client->wait_for_binary_message_count(kMessageCount)) << client->error();
        const auto client_messages = client->binary_messages();
        ASSERT_EQ(kMessageCount, client_messages.size());
        for (std::uint32_t sequence = 0; sequence < kMessageCount; ++sequence)
        {
            EXPECT_EQ(sequence,
                      decode_keyframe_request_tick(
                          client_messages[static_cast<std::size_t>(sequence)]));
        }
    }

    for (auto& client : clients)
        client->stop();

    ASSERT_TRUE(poll_until_peer_count(transport, 0u));
}

TEST(NetTransportWebSocketServer,
     network_fixture_keeps_four_clients_in_sync_over_loopback_websocket_at_12hz)
{
    constexpr std::uint32_t kStressTicks =
        static_cast<std::uint32_t>(og::sim::DEFAULT_SIM_TICKS_PER_SEC * 5);

    og::sim::test::NetworkTestFixture fixture({
        .player_count = 4,
        .level_id = 1,
        .tick_count = kStressTicks,
        .validate_serialization = false,
        .player_teams = {},
        .input_sequence =
            [](std::size_t, std::uint32_t) {
                InputState input{};
                return input;
            },
        .transport_backend =
            og::sim::test::NetworkTransportBackend::WebSocketLoopback,
        .network_timeout = 10s,
    });

    fixture.run();
    EXPECT_EQ(0u, fixture.server().snapshot_hash_mismatch_count());
    fixture.expect_clients_match_server();
}

} // namespace
