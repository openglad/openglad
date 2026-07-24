#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/walker.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>

#include <gtest/gtest.h>

#include <ixwebsocket/IXGetFreePort.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../test_game_world_fixture.h"

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

template <typename SendAction, typename Predicate>
std::optional<og::sim::ReceivedMessage> send_until_matching_message(
    og::sim::ITransport& sender,
    SendAction&& send_action,
    og::sim::ITransport& receiver,
    Predicate&& predicate,
    std::chrono::milliseconds timeout = 5s)
{
    std::optional<og::sim::ReceivedMessage> matched_message;
    const bool ok = wait_until(
        [&] {
            (void)sender.poll();
            send_action();

            std::vector<og::sim::ReceivedMessage> polled = receiver.poll();
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
     validates_configuration_and_preserves_idle_state_on_noop_operations)
{
    EXPECT_THROW(
        {
            og::sim::WebSocketClientTransport client("");
        },
        std::invalid_argument);

    og::sim::WebSocketClientTransport::Options invalid_peer_options;
    invalid_peer_options.remote_peer_id = 0u;
    EXPECT_THROW(
        {
            og::sim::WebSocketClientTransport client(
                "ws://127.0.0.1:1", invalid_peer_options);
        },
        std::invalid_argument);

    og::sim::WebSocketClientTransport::Options options;
    options.remote_peer_id = 42u;
    options.min_reconnect_wait_ms = 250u;
    options.max_reconnect_wait_ms = 5u;
    auto client = std::make_unique<og::sim::WebSocketClientTransport>(
        "ws://127.0.0.1:1", options);

    const std::uint8_t payload = 0x5au;
    EXPECT_THROW(client->send(options.remote_peer_id, nullptr, 1u),
                 std::runtime_error);
    EXPECT_NO_THROW(client->send(99u, &payload, 1u));
    client->disconnect(99u);

    EXPECT_EQ(og::sim::TransportLinkState::Connecting, client->link_state());
    EXPECT_TRUE(client->connected_peers().empty());

    client->disconnect(options.remote_peer_id);
    EXPECT_EQ(og::sim::TransportLinkState::Failed, client->link_state());
    EXPECT_TRUE(client->poll().empty());
    client.reset();
}

TEST(NetTransportWebSocketClient,
     oversized_server_frame_disconnects_without_delivering_payload)
{
    const int port = ix::getFreePort();

    og::sim::WebSocketServerTransport::Options server_options;
    server_options.host = "127.0.0.1";
    og::sim::WebSocketServerTransport server(port, server_options);
    server.accept_connections();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 73u;
    client_options.automatic_reconnection = false;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", port), client_options);
    client.accept_connections();

    ASSERT_TRUE(poll_until_peer_count(client, 1u));
    ASSERT_TRUE(poll_until_peer_count(server, 1u));
    ASSERT_EQ(og::sim::TransportLinkState::Connected, client.link_state());

    constexpr std::size_t kMaximumInboundFrameBytes = 128u * 1024u;
    const std::vector<std::uint8_t> oversized_payload(
        kMaximumInboundFrameBytes + 1u, 0xa5u);
    server.send(server.connected_peers().front(),
                oversized_payload.data(),
                oversized_payload.size());

    std::vector<og::sim::ReceivedMessage> received_messages;
    ASSERT_TRUE(wait_until(
        [&] {
            std::vector<og::sim::ReceivedMessage> polled = client.poll();
            received_messages.insert(
                received_messages.end(),
                std::make_move_iterator(polled.begin()),
                std::make_move_iterator(polled.end()));
            (void)server.poll();
            return client.link_state() == og::sim::TransportLinkState::Lost;
        },
        5s));

    EXPECT_TRUE(received_messages.empty());
    EXPECT_TRUE(client.connected_peers().empty());
    EXPECT_TRUE(poll_until_peer_count(server, 0u));
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

    ASSERT_TRUE(wait_until(
        [&] {
            (void)client.poll();
            (void)server.poll();
            const std::vector<og::sim::PeerId> peers = server.connected_peers();
            return peers.size() == 1u && peers.front() != first_server_peer_id;
        },
        5s));

    ASSERT_TRUE(poll_until_peer_count(client, 1u));
    EXPECT_EQ((std::vector<og::sim::PeerId>{client_options.remote_peer_id}),
              client.connected_peers());

    // Drain any residual Connect/Disconnect transitions that the tight 1-20ms
    // backoff can produce on slow CI runners (e.g. coverage builds) before
    // exercising the post-reconnect send path.
    for (int i = 0; i < 10; ++i)
    {
        (void)client.poll();
        (void)server.poll();
        std::this_thread::sleep_for(5ms);
    }

    const auto server_message = send_until_matching_message(
        client,
        [&] {
            client.send_client_ready(
                client_options.remote_peer_id,
                std::make_shared<og::sim::ClientReadyMessage>(
                    og::sim::ClientReadyMessage{.last_applied_tick = 88u}));
        },
        server,
        [first_server_peer_id](const og::sim::ReceivedMessage& message) {
            return message.peer_id != first_server_peer_id &&
                decode_client_ready_tick(message.data) == 88u;
        },
        15s);
    ASSERT_TRUE(server_message.has_value());

    const auto client_message = send_until_matching_message(
        server,
        [&] {
            const std::vector<og::sim::PeerId> peers = server.connected_peers();
            if (peers.empty())
                return;

            server.send_keyframe_request(
                peers.front(),
                std::make_shared<og::sim::KeyframeRequestMessage>(
                    og::sim::KeyframeRequestMessage{.last_seen_tick = 77u}));
        },
        client,
        [remote_peer_id = client_options.remote_peer_id](
            const og::sim::ReceivedMessage& message) {
            return message.peer_id == remote_peer_id &&
                decode_keyframe_request_tick(message.data) == 77u;
        },
        15s);
    ASSERT_TRUE(client_message.has_value());

    client.disconnect(client_options.remote_peer_id);
    ASSERT_TRUE(poll_until_peer_count(server, 0u));
}

TEST(NetTransportWebSocketClient,
     game_client_resends_hello_after_websocket_auto_reconnect)
{
    const int port = ix::getFreePort();

    og::sim::WebSocketServerTransport::Options server_options;
    server_options.host = "127.0.0.1";
    og::sim::WebSocketServerTransport server_transport(port, server_options);
    server_transport.accept_connections();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 1u;
    client_options.automatic_reconnection = true;
    client_options.min_reconnect_wait_ms = 1u;
    client_options.max_reconnect_wait_ms = 20u;
    og::sim::WebSocketClientTransport client_transport(
        std::format("ws://127.0.0.1:{}", port),
        client_options);
    client_transport.accept_connections();

    ASSERT_TRUE(poll_until_peer_count(client_transport, 1u));

    TestGameWorld fixture;
    og::sim::GameServer server(fixture.world(), fixture.events, server_transport);
    og::sim::GameClient client(client_transport, client_options.remote_peer_id);

    ASSERT_TRUE(wait_until(
        [&] {
            server.poll_incoming_messages();
            return server_transport.connected_peers().size() == 1u;
        },
        5s));

    const og::sim::PeerId first_server_peer_id =
        server_transport.connected_peers().front();

    walker* const available_control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, available_control);
    available_control->set_team_num(
        static_cast<unsigned char>(fixture.world().my_team));
    available_control->set_real_team_num(255);
    available_control->set_dead(0);
    available_control->set_user(-1);
    available_control->set_act_type(ACT_RANDOM);
    available_control->setxy(32, 48);

    server.bind_player(first_server_peer_id, 0u, fixture.world().my_team);
    ASSERT_EQ(available_control, server.player_control(0u));

    ASSERT_TRUE(wait_until(
        [&] {
            client.poll_messages();
            server.step();
            client.poll_messages();
            return !og::sim::is_zero_session_token(client.session_token()) &&
                client.initial_setup().has_value() &&
                client.baseline().has_value();
        },
        5s));

    const og::sim::SessionToken session_token = client.session_token();
    ASSERT_FALSE(og::sim::is_zero_session_token(session_token));

    server_transport.disconnect(first_server_peer_id);

    ASSERT_TRUE(wait_until(
        [&] {
            server.poll_incoming_messages();
            const std::vector<og::sim::PeerId> peers =
                server_transport.connected_peers();
            return peers.size() == 1u &&
                peers.front() != first_server_peer_id &&
                server.disconnected_players().size() == 1u;
        },
        10s));

    ASSERT_EQ(1u, server.disconnected_players().size());
    EXPECT_EQ(session_token, server.disconnected_players().front().session_token);
    EXPECT_EQ(available_control, server.player_control(0u));

    bool saw_reconnect_hello = false;
    bool saw_reconnect_initial_setup = false;
    bool saw_reconnect_control_change = false;
    bool saw_reconnect_snapshot = false;
    const auto note_reconnect_messages =
        [&client,
         &session_token,
         available_control,
         &saw_reconnect_hello,
         &saw_reconnect_initial_setup,
         &saw_reconnect_control_change,
         &saw_reconnect_snapshot]() {
            for (const og::sim::TypedReceivedMessage& message :
                 client.last_polled_messages())
            {
                if (message.kind == og::sim::TypedReceivedMessageKind::Hello &&
                    message.hello != nullptr &&
                    message.hello->session_token == session_token)
                {
                    saw_reconnect_hello = true;
                }

                if (message.kind ==
                        og::sim::TypedReceivedMessageKind::InitialSetup &&
                    message.initial_setup != nullptr &&
                    message.initial_setup->controlled_entity_ids[0] ==
                        available_control->entity_id())
                {
                    saw_reconnect_initial_setup = true;
                }

                if (message.kind ==
                        og::sim::TypedReceivedMessageKind::ControlChange &&
                    message.control_change != nullptr &&
                    message.control_change->player_index == 0u &&
                    message.control_change->entity_id ==
                        available_control->entity_id())
                {
                    saw_reconnect_control_change = true;
                }

                if (message.kind == og::sim::TypedReceivedMessageKind::Snapshot &&
                    message.snapshot != nullptr)
                {
                    saw_reconnect_snapshot = true;
                }
            }
        };

    ASSERT_TRUE(wait_until(
        [&] {
            client.poll_messages();
            note_reconnect_messages();
            server.step();
            client.poll_messages();
            note_reconnect_messages();
            return server.disconnected_players().empty() &&
                saw_reconnect_hello &&
                saw_reconnect_initial_setup &&
                saw_reconnect_control_change &&
                saw_reconnect_snapshot;
        },
        10s));

    EXPECT_TRUE(server.disconnected_players().empty());
    EXPECT_EQ(session_token, client.session_token());
    EXPECT_EQ(available_control, server.player_control(0u));
    EXPECT_EQ(0, static_cast<int>(available_control->user()));
    EXPECT_EQ(ACT_CONTROL, available_control->act_type());

    client_transport.disconnect(client_options.remote_peer_id);
    ASSERT_TRUE(wait_until(
        [&] {
            server.poll_incoming_messages();
            return server_transport.connected_peers().empty();
        },
        5s));
}

TEST(NetTransportWebSocketClient,
     link_state_reports_failed_when_server_is_unreachable)
{
    const int port = ix::getFreePort();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 1u;
    client_options.automatic_reconnection = false;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", port),
        client_options);

    EXPECT_EQ(og::sim::TransportLinkState::Connecting, client.link_state());
    client.accept_connections();

    ASSERT_TRUE(wait_until(
        [&] {
            (void)client.poll();
            return client.link_state() ==
                og::sim::TransportLinkState::Failed;
        },
        5s)) << "connection refused should surface as Failed, never Lost";
    EXPECT_TRUE(client.connected_peers().empty());
}

TEST(NetTransportWebSocketClient,
     link_state_reports_lost_after_server_drops_connection)
{
    const int port = ix::getFreePort();

    og::sim::WebSocketServerTransport::Options server_options;
    server_options.host = "127.0.0.1";
    auto server = std::make_unique<og::sim::WebSocketServerTransport>(
        port, server_options);
    server->accept_connections();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 1u;
    client_options.automatic_reconnection = false;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", port),
        client_options);
    client.accept_connections();

    ASSERT_TRUE(wait_until(
        [&] {
            (void)client.poll();
            return client.link_state() ==
                og::sim::TransportLinkState::Connected;
        },
        5s));

    server.reset();

    ASSERT_TRUE(wait_until(
        [&] {
            (void)client.poll();
            return client.link_state() == og::sim::TransportLinkState::Lost;
        },
        5s)) << "a drop after connecting should surface as Lost, not Failed";
    EXPECT_TRUE(client.connected_peers().empty());
}

} // namespace
