#include <openglad/platform/net_transport_emscripten_ws.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using og::sim::EmscriptenWebSocketTransport;
using og::sim::PeerId;
using og::sim::detail::EmscriptenBool;
using og::sim::detail::EmscriptenResult;
using og::sim::detail::EmscriptenWebSocketApi;
using og::sim::detail::WebSocketCloseCallback;
using og::sim::detail::WebSocketCloseEvent;
using og::sim::detail::WebSocketCreateAttributes;
using og::sim::detail::WebSocketErrorCallback;
using og::sim::detail::WebSocketErrorEvent;
using og::sim::detail::WebSocketHandle;
using og::sim::detail::WebSocketMessageCallback;
using og::sim::detail::WebSocketMessageEvent;
using og::sim::detail::WebSocketOpenCallback;
using og::sim::detail::WebSocketOpenEvent;
using og::sim::detail::kFalse;
using og::sim::detail::kResultFailed;
using og::sim::detail::kResultInvalidTarget;
using og::sim::detail::kResultSuccess;
using og::sim::detail::kTrue;
using og::sim::detail::set_emscripten_websocket_api_for_testing;

constexpr unsigned short kReadyStateOpen = 1;

class FakeWebSocketBackend
{
public:
    struct SocketState {
        unsigned short ready_state = kReadyStateOpen;
        EmscriptenResult ready_state_result = kResultSuccess;
        EmscriptenResult send_result = kResultSuccess;
        void* open_user_data = nullptr;
        WebSocketOpenCallback open_callback = nullptr;
        void* message_user_data = nullptr;
        WebSocketMessageCallback message_callback = nullptr;
        void* error_user_data = nullptr;
        WebSocketErrorCallback error_callback = nullptr;
        void* close_user_data = nullptr;
        WebSocketCloseCallback close_callback = nullptr;
        int close_calls = 0;
        int destroy_calls = 0;
        std::vector<std::vector<std::uint8_t>> sent_payloads;
    };

    FakeWebSocketBackend()
    {
        api.init_create_attributes = &FakeWebSocketBackend::init_create_attributes;
        api.is_supported = &FakeWebSocketBackend::is_supported;
        api.create = &FakeWebSocketBackend::create;
        api.set_onopen = &FakeWebSocketBackend::set_onopen;
        api.set_onmessage = &FakeWebSocketBackend::set_onmessage;
        api.set_onerror = &FakeWebSocketBackend::set_onerror;
        api.set_onclose = &FakeWebSocketBackend::set_onclose;
        api.get_ready_state = &FakeWebSocketBackend::get_ready_state;
        api.send_binary = &FakeWebSocketBackend::send_binary;
        api.close = &FakeWebSocketBackend::close;
        api.destroy = &FakeWebSocketBackend::destroy;
        instance_ = this;
    }

    ~FakeWebSocketBackend()
    {
        instance_ = nullptr;
    }

    WebSocketHandle accept_socket()
    {
        EXPECT_GT(last_created_socket, 0);
        return last_created_socket;
    }

    SocketState& socket_state(WebSocketHandle socket)
    {
        return sockets_.at(socket);
    }

    int close_calls(WebSocketHandle socket) const
    {
        return sockets_.at(socket).close_calls;
    }

    int destroy_calls(WebSocketHandle socket) const
    {
        return sockets_.at(socket).destroy_calls;
    }

    const std::vector<std::vector<std::uint8_t>>& sent_payloads(
        WebSocketHandle socket) const
    {
        return sockets_.at(socket).sent_payloads;
    }

    bool emit_open(WebSocketHandle socket)
    {
        const auto it = sockets_.find(socket);
        if (it == sockets_.end() || it->second.open_callback == nullptr)
            return false;

        WebSocketOpenEvent event{};
        event.socket = socket;
        return it->second.open_callback(0, &event, it->second.open_user_data) ==
               kTrue;
    }

    bool emit_message(WebSocketHandle socket,
                      std::span<const std::uint8_t> payload,
                      bool is_text = false)
    {
        const auto it = sockets_.find(socket);
        if (it == sockets_.end() || it->second.message_callback == nullptr)
            return false;

        std::vector<std::uint8_t> owned_payload(payload.begin(), payload.end());
        WebSocketMessageEvent event{};
        event.socket = socket;
        event.data = owned_payload.empty() ? nullptr : owned_payload.data();
        event.numBytes = static_cast<std::uint32_t>(owned_payload.size());
        event.isText = is_text ? kTrue : kFalse;
        return it->second.message_callback(0,
                                           &event,
                                           it->second.message_user_data) == kTrue;
    }

    bool emit_invalid_binary_message(WebSocketHandle socket,
                                     std::uint32_t payload_size)
    {
        const auto it = sockets_.find(socket);
        if (it == sockets_.end() || it->second.message_callback == nullptr)
            return false;

        WebSocketMessageEvent event{};
        event.socket = socket;
        event.data = nullptr;
        event.numBytes = payload_size;
        event.isText = kFalse;
        return it->second.message_callback(0,
                                           &event,
                                           it->second.message_user_data) == kTrue;
    }

    bool emit_error(WebSocketHandle socket)
    {
        const auto it = sockets_.find(socket);
        if (it == sockets_.end() || it->second.error_callback == nullptr)
            return false;

        WebSocketErrorEvent event{};
        event.socket = socket;
        return it->second.error_callback(0, &event, it->second.error_user_data) ==
               kTrue;
    }

    bool emit_close(WebSocketHandle socket)
    {
        const auto it = sockets_.find(socket);
        if (it == sockets_.end() || it->second.close_callback == nullptr)
            return false;

        WebSocketCloseEvent event{};
        event.socket = socket;
        return it->second.close_callback(0, &event, it->second.close_user_data) ==
               kTrue;
    }

    EmscriptenWebSocketApi api{};
    bool supported = true;
    bool fail_create = false;
    EmscriptenResult create_error = kResultFailed;
    EmscriptenResult set_onopen_result = kResultSuccess;
    EmscriptenResult set_onmessage_result = kResultSuccess;
    EmscriptenResult set_onerror_result = kResultSuccess;
    EmscriptenResult set_onclose_result = kResultSuccess;
    int init_create_attributes_calls = 0;
    int create_calls = 0;
    WebSocketHandle last_created_socket = 0;
    std::string last_url;
    std::string last_protocols;

private:
    static FakeWebSocketBackend& self()
    {
        return *instance_;
    }

    static void init_create_attributes(WebSocketCreateAttributes* attributes)
    {
        FakeWebSocketBackend& backend = self();
        ++backend.init_create_attributes_calls;
        if (attributes != nullptr)
            *attributes = WebSocketCreateAttributes{};
    }

    static EmscriptenBool is_supported()
    {
        return self().supported ? kTrue : kFalse;
    }

    static WebSocketHandle create(WebSocketCreateAttributes* attributes)
    {
        FakeWebSocketBackend& backend = self();
        ++backend.create_calls;
        backend.last_url =
            (attributes != nullptr && attributes->url != nullptr) ? attributes->url
                                                                  : "";
        backend.last_protocols =
            (attributes != nullptr && attributes->protocols != nullptr)
                ? attributes->protocols
                : "";
        if (backend.fail_create)
            return backend.create_error;

        backend.last_created_socket = ++backend.next_socket_;
        backend.sockets_.try_emplace(backend.last_created_socket);
        return backend.last_created_socket;
    }

    static EmscriptenResult set_onopen(WebSocketHandle socket,
                                       void* user_data,
                                       WebSocketOpenCallback callback)
    {
        FakeWebSocketBackend& backend = self();
        if (backend.set_onopen_result != kResultSuccess)
            return backend.set_onopen_result;

        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        it->second.open_user_data = user_data;
        it->second.open_callback = callback;
        return kResultSuccess;
    }

    static EmscriptenResult set_onmessage(WebSocketHandle socket,
                                          void* user_data,
                                          WebSocketMessageCallback callback)
    {
        FakeWebSocketBackend& backend = self();
        if (backend.set_onmessage_result != kResultSuccess)
            return backend.set_onmessage_result;

        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        it->second.message_user_data = user_data;
        it->second.message_callback = callback;
        return kResultSuccess;
    }

    static EmscriptenResult set_onerror(WebSocketHandle socket,
                                        void* user_data,
                                        WebSocketErrorCallback callback)
    {
        FakeWebSocketBackend& backend = self();
        if (backend.set_onerror_result != kResultSuccess)
            return backend.set_onerror_result;

        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        it->second.error_user_data = user_data;
        it->second.error_callback = callback;
        return kResultSuccess;
    }

    static EmscriptenResult set_onclose(WebSocketHandle socket,
                                        void* user_data,
                                        WebSocketCloseCallback callback)
    {
        FakeWebSocketBackend& backend = self();
        if (backend.set_onclose_result != kResultSuccess)
            return backend.set_onclose_result;

        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        it->second.close_user_data = user_data;
        it->second.close_callback = callback;
        return kResultSuccess;
    }

    static EmscriptenResult get_ready_state(WebSocketHandle socket,
                                            unsigned short* ready_state)
    {
        FakeWebSocketBackend& backend = self();
        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        if (it->second.ready_state_result != kResultSuccess)
            return it->second.ready_state_result;

        if (ready_state != nullptr)
            *ready_state = it->second.ready_state;
        return kResultSuccess;
    }

    static EmscriptenResult send_binary(WebSocketHandle socket,
                                        void* binary_data,
                                        std::uint32_t data_length)
    {
        FakeWebSocketBackend& backend = self();
        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        if (it->second.send_result != kResultSuccess)
            return it->second.send_result;

        const auto* bytes = static_cast<const std::uint8_t*>(binary_data);
        if (bytes == nullptr && data_length != 0)
            return kResultFailed;

        it->second.sent_payloads.emplace_back(bytes, bytes + data_length);
        return kResultSuccess;
    }

    static EmscriptenResult close(WebSocketHandle socket,
                                  unsigned short,
                                  const char*)
    {
        FakeWebSocketBackend& backend = self();
        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        ++it->second.close_calls;
        return kResultSuccess;
    }

    static EmscriptenResult destroy(WebSocketHandle socket)
    {
        FakeWebSocketBackend& backend = self();
        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        ++it->second.destroy_calls;
        return kResultSuccess;
    }

    inline static FakeWebSocketBackend* instance_ = nullptr;

    WebSocketHandle next_socket_ = 100;
    std::unordered_map<WebSocketHandle, SocketState> sockets_;
};

class EmscriptenWebSocketTransportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        set_emscripten_websocket_api_for_testing(&backend_.api);
    }

    void TearDown() override
    {
        set_emscripten_websocket_api_for_testing(nullptr);
    }

    WebSocketHandle accept_transport(EmscriptenWebSocketTransport& transport)
    {
        transport.accept_connections();
        return backend_.accept_socket();
    }

    WebSocketHandle connect_transport(EmscriptenWebSocketTransport& transport)
    {
        const WebSocketHandle socket = accept_transport(transport);
        EXPECT_TRUE(backend_.emit_open(socket));
        EXPECT_TRUE(transport.poll().empty());
        return socket;
    }

    static EmscriptenWebSocketTransport::Options make_options()
    {
        EmscriptenWebSocketTransport::Options options;
        options.remote_peer_id = 42u;
        options.protocols = "og-binary";
        return options;
    }

    FakeWebSocketBackend backend_;
};

TEST_F(EmscriptenWebSocketTransportTest,
       poll_defers_connect_state_until_game_thread_and_delivers_binary_frames)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());

    const WebSocketHandle socket = accept_transport(transport);
    EXPECT_EQ(1, backend_.init_create_attributes_calls);
    EXPECT_EQ(1, backend_.create_calls);
    EXPECT_EQ("ws://example.test/socket", backend_.last_url);
    EXPECT_EQ("og-binary", backend_.last_protocols);
    EXPECT_TRUE(transport.connected_peers().empty());

    const std::array<std::uint8_t, 3> payload{0x10, 0x20, 0x30};
    ASSERT_TRUE(backend_.emit_open(socket));
    ASSERT_TRUE(backend_.emit_message(socket, payload));

    EXPECT_TRUE(transport.connected_peers().empty());

    const std::vector<og::sim::ReceivedMessage> messages = transport.poll();
    ASSERT_EQ(1u, messages.size());
    EXPECT_EQ(42u, messages.front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0x10, 0x20, 0x30}),
              messages.front().data);
    EXPECT_EQ((std::vector<PeerId>{42u}), transport.connected_peers());
}

TEST_F(EmscriptenWebSocketTransportTest,
       ignores_text_frames_and_invalid_binary_payloads)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);
    const std::array<std::uint8_t, 2> payload{0xaa, 0xbb};

    ASSERT_TRUE(backend_.emit_message(socket, payload, true));
    EXPECT_TRUE(transport.poll().empty());

    ASSERT_TRUE(backend_.emit_invalid_binary_message(socket, 3u));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ((std::vector<PeerId>{42u}), transport.connected_peers());
}

TEST_F(EmscriptenWebSocketTransportTest,
       error_callback_disconnects_and_disposes_socket_on_poll)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);

    ASSERT_TRUE(backend_.emit_error(socket));
    EXPECT_EQ((std::vector<PeerId>{42u}), transport.connected_peers());

    EXPECT_TRUE(transport.poll().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       close_callback_disconnects_and_disposes_socket_on_poll)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);

    ASSERT_TRUE(backend_.emit_close(socket));
    EXPECT_TRUE(transport.poll().empty());

    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       send_binary_uses_only_the_connected_remote_peer)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);
    const std::array<std::uint8_t, 4> payload{1, 2, 3, 4};

    transport.send(7u, payload.data(), payload.size());
    EXPECT_TRUE(backend_.sent_payloads(socket).empty());

    transport.send(42u, payload.data(), payload.size());
    ASSERT_EQ(1u, backend_.sent_payloads(socket).size());
    EXPECT_EQ((std::vector<std::uint8_t>{1, 2, 3, 4}),
              backend_.sent_payloads(socket).front());
    EXPECT_EQ((std::vector<PeerId>{42u}), transport.connected_peers());
}

TEST_F(EmscriptenWebSocketTransportTest,
       send_failure_disconnects_transport_and_destroys_socket_after_poll)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);
    backend_.socket_state(socket).send_result = kResultFailed;
    const std::array<std::uint8_t, 2> payload{9, 8};

    transport.send(42u, payload.data(), payload.size());

    EXPECT_TRUE(transport.poll().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_TRUE(backend_.sent_payloads(socket).empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       send_ready_state_failure_disconnects_without_sending)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);
    backend_.socket_state(socket).ready_state = 0;
    const std::array<std::uint8_t, 1> payload{0xff};

    transport.send(42u, payload.data(), payload.size());

    EXPECT_TRUE(transport.poll().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_TRUE(backend_.sent_payloads(socket).empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       disconnect_clears_pending_events_and_disposes_socket_immediately)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = connect_transport(transport);
    const std::array<std::uint8_t, 2> payload{0x55, 0x66};

    ASSERT_TRUE(backend_.emit_message(socket, payload));
    transport.disconnect(42u);

    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       accept_connections_throws_when_browser_websockets_are_unavailable)
{
    backend_.supported = false;
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());

    EXPECT_THROW(transport.accept_connections(), std::runtime_error);
}

TEST_F(EmscriptenWebSocketTransportTest,
       accept_connections_throws_when_callback_registration_fails)
{
    backend_.set_onmessage_result = kResultFailed;
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());

    EXPECT_THROW(transport.accept_connections(), std::runtime_error);
    EXPECT_GE(backend_.close_calls(backend_.last_created_socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(backend_.last_created_socket));
}

} // namespace
